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
#include <new>
#include <kopano/platform.h>
#include <kopano/Util.h>
#include "ECSecurity.h"
#include "ECDatabase.h"

#include <mapidefs.h>

#include "ECSessionManager.h"
#include "ECSearchObjectTable.h"
#include "ECSession.h"

namespace KC {

ECSearchObjectTable::ECSearchObjectTable(ECSession *lpSession, unsigned int ulStoreId, LPGUID lpGuid, unsigned int ulFolderId, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale) : ECStoreObjectTable(lpSession, ulStoreId, lpGuid, 0, ulObjType, ulFlags, 0, locale) {
	// We don't pass ulFolderId to ECStoreObjectTable (see constructor above passing '0'), because 
	// it will assume that all rows are in that folder if we do that. But we still want to 
	// remember the folder ID for our own use.
	
	m_ulFolderId = ulFolderId;
	m_ulStoreId = ulStoreId;
}

ECRESULT ECSearchObjectTable::Create(ECSession *lpSession,
    unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId,
    unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale,
    ECStoreObjectTable **lppTable)
{
	return alloc_wrap<ECSearchObjectTable>(lpSession, ulStoreId, lpGuid,
	       ulFolderId, ulObjType, ulFlags, locale).put(lppTable);
}

ECRESULT ECSearchObjectTable::Load() {
	ECRESULT er = erSuccess;
	sObjectTableKey		sRowItem;
	std::list<unsigned int> lstObjId;
	scoped_rlock biglock(m_hLock);
	std::string strInQuery;
	std::string strQuery;
	std::set<unsigned int> setObjIdPrivate;
	std::list<unsigned int> lstObjId2;
	ECDatabase*             lpDatabase = NULL;
	DB_RESULT lpDBResult;
	DB_ROW                  lpDBRow = NULL;

	if (m_ulFolderId == 0)
		return er;
	// Get the search results
	er = lpSession->GetSessionManager()->GetSearchFolders()->GetSearchResults(m_ulStoreId, m_ulFolderId, &lstObjId);
	if (er != erSuccess)
		return er;

	if(lpSession->GetSecurity()->IsStoreOwner(m_ulFolderId) != KCERR_NO_ACCESS ||
	    lpSession->GetSecurity()->GetAdminLevel() > 0 ||
	    lstObjId.size() == 0)
		return UpdateRows(ECKeyTable::TABLE_ROW_ADD, &lstObjId, 0, true);

	// Outlook may show the subject of sensitive messages (e.g. in
	// reminder popup), so filter these from shared store searches

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	for(auto it = lstObjId.begin(); it != lstObjId.end(); ++it) {
		if(it != lstObjId.begin())
			strInQuery += ",";
		strInQuery += stringify(*it);
	}

	strQuery = "SELECT hierarchyid FROM properties WHERE hierarchyid IN (" + strInQuery + ") AND tag = " + stringify(PROP_ID(PR_SENSITIVITY)) + " AND val_ulong >= 2;";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if(lpDBRow == NULL || lpDBRow[0] == NULL)
			continue;
		setObjIdPrivate.emplace(atoui(lpDBRow[0]));
	}

	for(auto it = lstObjId.begin(); it != lstObjId.end(); ++it)
		if (setObjIdPrivate.find(*it) == setObjIdPrivate.end())
			lstObjId2.emplace_back(*it);

	return UpdateRows(ECKeyTable::TABLE_ROW_ADD, &lstObjId2, 0, true);
}

} /* namespace */
