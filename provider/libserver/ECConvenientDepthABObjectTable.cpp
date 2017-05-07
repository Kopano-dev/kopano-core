/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <kopano/platform.h>
#include <memory>
#include <new>
#include <utility>
#include <kopano/tie.hpp>
#include "ECDatabase.h"

#include <mapidefs.h>
#include <mapitags.h>
#include <kopano/EMSAbTag.h>

#include "ECSessionManager.h"
#include "ECConvenientDepthABObjectTable.h"
#include "ECSession.h"
#include "ECMAPI.h"
#include <kopano/stringutil.h>

using namespace KCHL;

namespace KC {

ECConvenientDepthABObjectTable::ECConvenientDepthABObjectTable(ECSession *lpSession, unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId, unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale) : ECABObjectTable(lpSession, ulABId, ulABType, ulABParentId, ulABParentType, ulFlags, locale) {
    m_lpfnQueryRowData = ECConvenientDepthABObjectTable::QueryRowData;

	/* We require the details to construct the PR_EMS_AB_HIERARCHY_PATH */
	m_ulUserManagementFlags &= ~USERMANAGEMENT_IDS_ONLY;
}

ECRESULT ECConvenientDepthABObjectTable::Create(ECSession *lpSession,
    unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId,
    unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale,
    ECABObjectTable **lppTable)
{
	*lppTable = new(std::nothrow) ECConvenientDepthABObjectTable(lpSession,
	            ulABId, ulABType, ulABParentId, ulABParentType, ulFlags,
	            locale);
	if (*lppTable == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	(*lppTable)->AddRef();

	return erSuccess;
}

/*
 * We override the standard QueryRowData call so that we can correctly generate PR_DEPTH and PR_EMS_AB_HIERARCHY_PARENT. Since this is
 * dependent on data which is not available for ECUserManagement, we have to do those properties here.
 */
ECRESULT ECConvenientDepthABObjectTable::QueryRowData(ECGenericObjectTable *lpGenTable, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bTableData,bool bTableLimit)
{
    unsigned int n = 0;
    struct propVal *lpProp = NULL;
	auto lpThis = static_cast<ECConvenientDepthABObjectTable *>(lpGenTable);
	auto er = ECABObjectTable::QueryRowData(lpThis, soap, lpSession, lpRowList, lpsPropTagArray, lpObjectData, lppRowSet, bTableData, bTableLimit);
    if(er != erSuccess)
		return er;

    // Insert the PR_DEPTH for all the rows since the row engine has no knowledge of depth
	for (const auto &row : *lpRowList) {
        lpProp = FindProp(&(*lppRowSet)->__ptr[n], PROP_TAG(PT_ERROR, PROP_ID(PR_DEPTH)));
        
        if(lpProp) {
            lpProp->Value.ul = lpThis->m_mapDepth[row.ulObjId];
            lpProp->ulPropTag = PR_DEPTH;
            lpProp->__union = SOAP_UNION_propValData_ul;
        }
        
        lpProp = FindProp(&(*lppRowSet)->__ptr[n], PROP_TAG(PT_ERROR, PROP_ID(PR_EMS_AB_HIERARCHY_PATH)));
        
        if(lpProp) {
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
	auto lpODAB = static_cast<ECODAB *>(m_lpObjectData);
	sObjectTableKey	sRowItem;
	std::list<CONTAINERINFO> lstObjects;
	CONTAINERINFO root;

	if (lpODAB->ulABType != MAPI_ABCONT)
		return KCERR_INVALID_PARAMETER;

	// Load this container
	root.ulId = lpODAB->ulABParentId;
	root.ulDepth = -1; // Our children are at depth 0, so the root object is depth -1. Note that this object is not actually added as a row in the end.
	root.strPath = "";
	lstObjects.push_back(std::move(root));

    // 'Recursively' loop through all our containers and add each of those children to our object list
	for (const auto &obj : lstObjects) {
		std::unique_ptr<std::list<localobjectdetails_t> > lpSubObjects;
		if (LoadHierarchyContainer(obj.ulId, 0, &unique_tie(lpSubObjects)) != erSuccess)
			continue;
		for (const auto &subobj : *lpSubObjects) {
			CONTAINERINFO folder;
			folder.ulId = subobj.ulId;
			folder.ulDepth = obj.ulDepth + 1;
			folder.strPath = obj.strPath + "/" + subobj.GetPropString(OB_PROP_S_LOGIN);
			lstObjects.push_back(std::move(folder));
		}
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
