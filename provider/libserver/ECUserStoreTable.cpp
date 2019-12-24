/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include "ECDatabase.h"
#include <mapidefs.h>
#include <edkmdb.h>
#include "ECSecurity.h"
#include "ECSessionManager.h"
#include "ECUserStoreTable.h"
#include "ECGenProps.h"
#include "ECSession.h"
#include <kopano/stringutil.h>
#include <kopano/Util.h>

namespace KC {

// 1 == MAPI_STORE.. does it even matter?
ECUserStoreTable::ECUserStoreTable(ECSession *ses, unsigned int ulFlags,
    const ECLocale &locale) :
	ECGenericObjectTable(ses, 1, ulFlags, locale)
{
	// Set callback function for queryrowdata (again?)
	m_lpfnQueryRowData = QueryRowData;
}

ECRESULT ECUserStoreTable::Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECUserStoreTable **lppTable)
{
	return alloc_wrap<ECUserStoreTable>(lpSession, ulFlags, locale).put(lppTable);
}

ECRESULT ECUserStoreTable::QueryRowData(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	auto pThis = dynamic_cast<ECUserStoreTable *>(lpThis);
	if (pThis == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto lpsRowSet = s_alloc<rowSet>(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;
	if(lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a rectangular array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpsRowSet->__size);
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpsRowSet->__size);

	// Allocate memory for all rows
	for (gsoap_size_t i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr = s_alloc<propVal>(soap, lpsPropTagArray->__size);
		memset(lpsRowSet->__ptr[i].__ptr, 0, sizeof(propVal) * lpsPropTagArray->__size);
	}

	gsoap_size_t i = 0;
	for (const auto &row : *lpRowList) {
		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			auto &m = lpsRowSet->__ptr[i].__ptr[k];
			m.ulPropTag = PROP_TAG(PT_ERROR, lpsPropTagArray->__ptr[k]);
			m.__union = SOAP_UNION_propValData_ul;
			m.Value.ul = KCERR_NOT_FOUND;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key
				m.__union = SOAP_UNION_propValData_bin;
				m.ulPropTag = PR_INSTANCE_KEY;
				m.Value.bin = s_alloc<xsd__base64Binary>(soap);
				m.Value.bin->__size = sizeof(sObjectTableKey);
				m.Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(sObjectTableKey));
				memcpy(m.Value.bin->__ptr, &row, sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_EC_USERNAME):
				if (pThis->m_mapUserStoreData[row.ulObjId].strUsername.empty())
					break;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union = SOAP_UNION_propValData_lpszA;
				m.Value.lpszA = s_strcpy(soap, const_cast<char *>(pThis->m_mapUserStoreData[row.ulObjId].strUsername.c_str()));
				break;
			case PROP_ID(PR_DISPLAY_NAME):
				if (pThis->m_mapUserStoreData[row.ulObjId].strGuessname.empty())
					break;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union = SOAP_UNION_propValData_lpszA;
				m.Value.lpszA = s_strcpy(soap, const_cast<char *>(pThis->m_mapUserStoreData[row.ulObjId].strGuessname.c_str()));
				break;
			case PROP_ID(PR_EC_STOREGUID): {
				const auto &g = pThis->m_mapUserStoreData[row.ulObjId].sGuid;
				if (g.empty())
					break;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union = SOAP_UNION_propValData_bin;
				m.Value.bin = s_alloc<xsd__base64Binary>(soap);
				m.Value.bin->__size = g.size();
				m.Value.bin->__ptr = s_alloc<unsigned char>(soap, g.size());
				memcpy(m.Value.bin->__ptr, g.c_str(), g.size());
				break;
			}
			case PROP_ID(PR_EC_STORETYPE):
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union = SOAP_UNION_propValData_ul;
				m.Value.ul = pThis->m_mapUserStoreData[row.ulObjId].ulStoreType;
				break;
			case PROP_ID(PR_EC_COMPANYID):
				if (pThis->m_mapUserStoreData[row.ulObjId].ulCompanyId == 0)
					break;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union = SOAP_UNION_propValData_ul;
				m.Value.ul = pThis->m_mapUserStoreData[row.ulObjId].ulCompanyId;
				break;
			case PROP_ID(PR_EC_COMPANY_NAME):
				if (pThis->m_mapUserStoreData[row.ulObjId].strCompanyName.empty())
					break;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union = SOAP_UNION_propValData_lpszA;
				m.Value.lpszA = s_strcpy(soap, const_cast<char *>(pThis->m_mapUserStoreData[row.ulObjId].strCompanyName.c_str()));
				break;
			case PROP_ID(PR_STORE_ENTRYID):
				// ignore errors
				ECGenProps::GetPropComputedUncached(soap, NULL,
					lpSession, PR_STORE_ENTRYID,
					pThis->m_mapUserStoreData[row.ulObjId].ulObjId,
					0, pThis->m_mapUserStoreData[row.ulObjId].ulObjId,
					0, MAPI_STORE, &m);
				break;
			case PROP_ID(PR_LAST_MODIFICATION_TIME): {
				if (pThis->m_mapUserStoreData[row.ulObjId].tModTime == 0)
					break;
				auto ftTmp = UnixTimeToFileTime(pThis->m_mapUserStoreData[row.ulObjId].tModTime);
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union = SOAP_UNION_propValData_hilo;
				m.Value.hilo = s_alloc<struct hiloLong>(soap);
				m.Value.hilo->hi = ftTmp.dwHighDateTime;
				m.Value.hilo->lo = ftTmp.dwLowDateTime;
				break;
			}
			case PROP_ID(PR_MESSAGE_SIZE_EXTENDED):
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union = SOAP_UNION_propValData_li;
				m.Value.li = pThis->m_mapUserStoreData[row.ulObjId].ullStoreSize;
				break;
			case PROP_ID(PR_OBJECT_TYPE):
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.__union   = SOAP_UNION_propValData_ul;
				m.Value.li  = pThis->m_mapUserStoreData[row.ulObjId].sExternId.objclass;
				break;
			default:
				m.ulPropTag = PROP_TAG(PT_ERROR, lpsPropTagArray->__ptr[k]);
				m.__union = SOAP_UNION_propValData_ul;
				m.Value.ul = KCERR_NOT_FOUND;
				break;
			};
		}
		++i;
	}

	*lppRowSet = lpsRowSet;
	return erSuccess;
}

ECRESULT ECUserStoreTable::Load() {
	ECListIntIterator i;
    ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult;
    std::list<unsigned int> lstObjIds;
	ECUserStore sUserStore;
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	ECSecurity *lpSecurity = lpSession->GetSecurity();
	objectdetails_t sUserDetails, sDetails;
	objectclass_t objclass = OBJECTCLASS_UNKNOWN;
	enum cols { USERID = 0, EXTERNID, OBJCLASS, UCOMPANY, STOREGUID, STORETYPE, USERNAME, SCOMPANY, HIERARCHYID, STORESIZE, MODTIME_HI, MODTIME_LO };

	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

    Clear();
	/*
	 * The next query will first get the list of all users with their primary store details or NULL if
	 * no primary store was found. Secondly it will get the list of all stores with their owner or NULL
	 * if they're detached.
	 * The most important difference is that the first query will return no store for users without a
	 * primary store, even if they do have an archive store attached, while the second query will
	 * return all stores types.
	 */
	std::string strQuery =
		" SELECT u.id, u.externid, u.objectclass, u.company, s.guid, s.type, s.user_name, s.company, s.hierarchy_id, p.val_longint, m.val_hi, m.val_lo FROM users AS u"
		"  LEFT JOIN stores AS s ON s.user_id=u.id AND s.type=" + stringify(ECSTORE_TYPE_PRIVATE) + " LEFT JOIN hierarchy AS h ON h.id=s.hierarchy_id"
		"  LEFT JOIN properties AS p ON p.hierarchyid=s.hierarchy_id and p.tag=3592 and p.type=20"
		"  LEFT JOIN properties AS m ON m.hierarchyid=s.hierarchy_id and m.tag=26274 and m.type=64"
		" UNION"
		" SELECT u.id, u.externid, u.objectclass, u.company, s.guid, s.type, s.user_name, s.company, s.hierarchy_id, p.val_longint, m.val_hi, m.val_lo FROM users AS u"
		"  RIGHT JOIN stores AS s ON s.user_id=u.id LEFT JOIN hierarchy AS h ON h.id=s.hierarchy_id"
		"  LEFT JOIN properties AS p ON p.hierarchyid=s.hierarchy_id and p.tag=3592 and p.type=20"
		"  LEFT JOIN properties AS m ON m.hierarchyid=s.hierarchy_id and m.tag=26274 and m.type=64";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	int iRowId = 0;
	while(1) {
		auto lpDBRow = lpDBResult.fetch_row();
		if(lpDBRow == NULL)
			break;
		auto lpDBLength = lpDBResult.fetch_row_lengths();
		if (lpDBRow[OBJCLASS])
			objclass = (objectclass_t)atoi(lpDBRow[OBJCLASS]);
		if (lpDBRow[USERID]) {
			sUserStore.ulUserId = atoi(lpDBRow[USERID]);
			if (sUserStore.ulUserId == KOPANO_UID_SYSTEM) // everyone already filtered by object type
				continue;
		} else {
			sUserStore.ulUserId = -1;
		}

		sUserStore.ulCompanyId = 0;
		if (lpDBRow[UCOMPANY])
			sUserStore.ulCompanyId = atoi(lpDBRow[UCOMPANY]);
		if (lpDBRow[SCOMPANY])
			sUserStore.ulCompanyId = atoi(lpDBRow[SCOMPANY]); // might override from user.company
		// check if we're admin over this company
		if (lpSecurity->IsAdminOverUserObject(sUserStore.ulCompanyId) != erSuccess)
			continue;

		if (lpDBRow[EXTERNID]) {
			sUserStore.sExternId.id.assign(lpDBRow[EXTERNID], lpDBLength[EXTERNID]);
			sUserStore.sExternId.objclass = objclass;
		} else {
			sUserStore.sExternId.id.clear();
			sUserStore.sExternId.objclass = OBJECTCLASS_UNKNOWN;
		}

		sUserStore.strUsername.clear();
		// find and override real username if possible
		if (sUserStore.ulUserId != -1 && lpUserManagement->GetObjectDetails(sUserStore.ulUserId, &sUserDetails) == erSuccess) {
			if (lpSession->GetSessionManager()->IsDistributedSupported() &&
			    sUserDetails.GetPropString(OB_PROP_S_SERVERNAME).compare(lpSession->GetSessionManager()->GetConfig()->GetSetting("server_name")) != 0)
				continue; // user not on this server
			sUserStore.strUsername = sUserDetails.GetPropString(OB_PROP_S_LOGIN);
		}

		if (lpDBRow[STOREGUID])
			sUserStore.sGuid.assign(lpDBRow[STOREGUID], lpDBLength[STOREGUID]);

		if (lpDBRow[STORETYPE])
			sUserStore.ulStoreType = atoi(lpDBRow[STORETYPE]);
		else
			sUserStore.ulStoreType = ECSTORE_TYPE_PRIVATE; // or invalid value?

		if (lpDBRow[USERNAME])
			sUserStore.strGuessname = lpDBRow[USERNAME];
		else
			sUserStore.strGuessname.clear();

		if (sUserStore.ulCompanyId > 0 && lpUserManagement->GetObjectDetails(sUserStore.ulCompanyId, &sDetails) == erSuccess)
			sUserStore.strCompanyName = sDetails.GetPropString(OB_PROP_S_LOGIN);

		if(lpDBRow[HIERARCHYID])
			sUserStore.ulObjId = atoui(lpDBRow[HIERARCHYID]);
		else
			sUserStore.ulObjId = 0;

		sUserStore.tModTime = 0;
		if (lpDBRow[MODTIME_HI] != nullptr && lpDBRow[MODTIME_LO] != nullptr)
			sUserStore.tModTime = FileTimeToUnixTime({atoui(lpDBRow[MODTIME_LO]), atoui(lpDBRow[MODTIME_HI])});
		if(lpDBRow[STORESIZE])
			sUserStore.ullStoreSize = atoll(lpDBRow[STORESIZE]);
		else
			sUserStore.ullStoreSize = 0;

		// add to table
		lstObjIds.emplace_back(iRowId);
		// remember details
		m_mapUserStoreData.emplace(iRowId++, sUserStore);
	}

	LoadRows(&lstObjIds, 0);
	return erSuccess;
}

} /* namespace KC */
