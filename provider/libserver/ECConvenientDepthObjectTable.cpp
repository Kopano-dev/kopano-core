/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#include <kopano/platform.h>
#include <list>
#include <memory>
#include <new>
#include <utility>
#include "ECDatabase.h"

#include <mapidefs.h>
#include <mapitags.h>
#include "ECSecurity.h"
#include "ECSessionManager.h"
#include "ECConvenientDepthObjectTable.h"
#include "ECSession.h"
#include "ECMAPI.h"
#include <kopano/stringutil.h>
#include <kopano/tie.hpp>
#include <kopano/EMSAbTag.h>
#include <kopano/Util.h>
#include <list>

namespace KC {

struct CONTAINERINFO {
	unsigned int ulId, ulDepth;
	std::string strPath;
};

ECConvenientDepthObjectTable::ECConvenientDepthObjectTable(ECSession *ses,
    unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId,
    unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale) :
	ECStoreObjectTable(ses, ulStoreId, lpGuid, 0, ulObjType, ulFlags, 0, locale),
	m_ulFolderId(ulFolderId)
{}

/*
 * Loads an entire multi-depth hierarchy table recursively.
 *
 * The only way to do this nicely is by recursively getting all the hierarchy IDs for all folders under the root folder X
 *
 * Because these queries are really light and fast, the main goals is to limit the amount of calls to mysql to an absolute minimum. We do
 * this by querying all the information we know until now; We first request the subfolders for folder X. In the next call, we request all
 * the subfolders for *ALL* those subfolders. After that, we get all the subfolders for all those subfolders, etc.
 *
 * This means that the number of SQL calls we have to do is equal to the maximum depth level in a given tree hierarchy, which is usually
 * around 5 or so.
 *
 */

ECRESULT ECConvenientDepthObjectTable::Create(ECSession *lpSession,
    unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId,
    unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale,
    ECStoreObjectTable **lppTable)
{
	return alloc_wrap<ECConvenientDepthObjectTable>(lpSession, ulStoreId,
	       lpGuid, ulFolderId, ulObjType, ulFlags, locale).put(lppTable);
}

ECRESULT ECConvenientDepthObjectTable::Load() {
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult;
	auto lpData = static_cast<const ECODStore *>(m_lpObjectData);
	std::list<unsigned int> lstFolders, lstObjIds;
	unsigned int ulDepth = 0, ulFlags = lpData->ulFlags;

	auto cache = lpSession->GetSessionManager()->GetCacheManager();
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	lstFolders.emplace_back(m_ulFolderId);

	for (auto iterFolders = lstFolders.cbegin(); iterFolders != lstFolders.cend(); ) {
		std::string strQuery = "SELECT hierarchy.id, hierarchy.parent, hierarchy.owner, hierarchy.flags, hierarchy.type FROM hierarchy WHERE hierarchy.type = " + stringify(MAPI_FOLDER) + " AND hierarchy.flags & " + stringify(MSGFLAG_DELETED) + " = " + stringify(ulFlags & MSGFLAG_DELETED);
		strQuery += " AND hierarchy.parent IN(";
		
		while (iterFolders != lstFolders.cend()) {
		    strQuery += stringify(*iterFolders);
		    ++iterFolders;
		    if (iterFolders != lstFolders.cend())
    		    strQuery += ",";
        }
        
        strQuery += ")";

		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess)
			return er;

		while (1) {
			auto lpDBRow = lpDBResult.fetch_row();
			if (lpDBRow == NULL)
				break;

			if (lpDBRow[0] == nullptr || lpDBRow[1] == nullptr || lpDBRow[2] == nullptr || lpDBRow[3] == nullptr || lpDBRow[4] == nullptr)
				continue;

            // Since we have this information, give the cache manager the hierarchy information for this object
			cache->SetObject(atoui(lpDBRow[0]), atoui(lpDBRow[1]), atoui(lpDBRow[2]), atoui(lpDBRow[3]), atoui(lpDBRow[4]));
			
			// Push folders onto end of list
			lstFolders.emplace_back(atoui(lpDBRow[0]));
            
            // If we were pointing at the last item, point at the freshly inserted item
            if(iterFolders == lstFolders.cend())
			--iterFolders;
		}

		// If you're insane enough to have more than 256 levels over folders than we cut it off here because this function's
		// memory usage goes up exponentially ..
		if (++ulDepth > 256)
		    break;
	}

	lstFolders.remove(m_ulFolderId);
	lstObjIds = std::move(lstFolders);

    LoadRows(&lstObjIds, 0);
	return erSuccess;
}

ECRESULT ECConvenientDepthObjectTable::GetComputedDepth(struct soap *soap,
    ECSession *ses, unsigned int ulObjId, struct propVal *lpPropVal)
{
	unsigned int ulObjType;

	lpPropVal->ulPropTag = PR_DEPTH;
	lpPropVal->__union = SOAP_UNION_propValData_ul;
	lpPropVal->Value.ul = 0;
	auto cache = ses->GetSessionManager()->GetCacheManager();
	while(ulObjId != m_ulFolderId && lpPropVal->Value.ul < 50){
		auto er = cache->GetObject(ulObjId, &ulObjId, nullptr, nullptr, &ulObjType);
		if(er != erSuccess) {
			// should never happen
			assert(false);
			return KCERR_NOT_FOUND;
		}
		if (ulObjType != MAPI_FOLDER)
			return KCERR_NOT_FOUND;
		++lpPropVal->Value.ul;
	}
	return erSuccess;
}

ECConvenientDepthABObjectTable::ECConvenientDepthABObjectTable(ECSession *ses,
    unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId,
    unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale) :
	ECABObjectTable(ses, ulABId, ulABType, ulABParentId, ulABParentType,
	    ulFlags, locale)
{
	m_lpfnQueryRowData = ECConvenientDepthABObjectTable::QueryRowData;
	/* We require the details to construct the PR_EMS_AB_HIERARCHY_PATH */
	m_ulUserManagementFlags &= ~USERMANAGEMENT_IDS_ONLY;
}

ECRESULT ECConvenientDepthABObjectTable::Create(ECSession *lpSession,
    unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId,
    unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale,
    ECABObjectTable **lppTable)
{
	return alloc_wrap<ECConvenientDepthABObjectTable>(lpSession, ulABId,
	       ulABType, ulABParentId, ulABParentType, ulFlags, locale).put(lppTable);
}

/*
 * We override the standard QueryRowData call so that we can correctly generate
 * PR_DEPTH and PR_EMS_AB_HIERARCHY_PARENT. Since this is dependent on data
 * which is not available for ECUserManagement, we have to do those properties
 * here.
 */
ECRESULT ECConvenientDepthABObjectTable::QueryRowData(ECGenericObjectTable *lpGenTable,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bTableData, bool bTableLimit)
{
	unsigned int n = 0;
	struct propVal *lpProp = nullptr;
	auto lpThis = static_cast<ECConvenientDepthABObjectTable *>(lpGenTable);
	auto er = ECABObjectTable::QueryRowData(lpThis, soap, lpSession, lpRowList, lpsPropTagArray, lpObjectData, lppRowSet, bTableData, bTableLimit);
	if (er != erSuccess)
		return er;

	// Insert the PR_DEPTH for all the rows since the row engine has no knowledge of depth
	for (const auto &row : *lpRowList) {
		lpProp = FindProp(&(*lppRowSet)->__ptr[n], CHANGE_PROP_TYPE(PR_DEPTH, PT_ERROR));
		if (lpProp != nullptr) {
			lpProp->Value.ul = lpThis->m_mapDepth[row.ulObjId];
			lpProp->ulPropTag = PR_DEPTH;
			lpProp->__union = SOAP_UNION_propValData_ul;
		}
		lpProp = FindProp(&(*lppRowSet)->__ptr[n], CHANGE_PROP_TYPE(PR_EMS_AB_HIERARCHY_PATH, PT_ERROR));
		if (lpProp != nullptr) {
			lpProp->Value.lpszA = s_strcpy(soap, lpThis->m_mapPath[row.ulObjId].c_str());
			lpProp->ulPropTag = PR_EMS_AB_HIERARCHY_PATH;
			lpProp->__union = SOAP_UNION_propValData_lpszA;
		}
		++n;
	}
	return erSuccess;
}

/*
 * Loads an entire multi-depth hierarchy table recursively.
 */
ECRESULT ECConvenientDepthABObjectTable::Load()
{
	auto lpODAB = static_cast<const ECODAB *>(m_lpObjectData);
	std::list<CONTAINERINFO> lstObjects;

	if (lpODAB->ulABType != MAPI_ABCONT)
		return KCERR_INVALID_PARAMETER;

	// Load this (abparentid) container
	// Our children are at depth 0, so the root object is depth -1. Note that this object is not actually added as a row in the end.
	lstObjects.emplace_back(CONTAINERINFO{lpODAB->ulABParentId, static_cast<unsigned int>(-1)});

	// 'Recursively' loop through all our containers and add each of those children to our object list
	for (const auto &obj : lstObjects) {
		std::unique_ptr<std::list<localobjectdetails_t> > lpSubObjects;
		if (LoadHierarchyContainer(obj.ulId, 0, &unique_tie(lpSubObjects)) != erSuccess)
			continue;
		for (const auto &subobj : *lpSubObjects)
			lstObjects.emplace_back(CONTAINERINFO{subobj.ulId, obj.ulDepth + 1,
				obj.strPath + "/" + subobj.GetPropString(OB_PROP_S_LOGIN)});
	}

	// Add all the rows into the row engine, except the root object (the folder itself does not show in its own hierarchy table)
	for (const auto &obj : lstObjects) {
		if (obj.ulId == lpODAB->ulABParentId)
			continue;
		m_mapDepth[obj.ulId] = obj.ulDepth;
		m_mapPath[obj.ulId] = obj.strPath;
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, obj.ulId, 0);
	}
	return erSuccess;
}

} /* namespace */
