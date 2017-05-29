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
#include <kopano/lockhelper.hpp>
#include <kopano/Util.h>
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
    sObjectTableKey		sRowItem;
    std::list<unsigned int> lstObjId;
	scoped_rlock biglock(m_hLock);

	if (m_ulFolderId == 0)
		return erSuccess;
	// Get the search results
	auto er = lpSession->GetSessionManager()->GetSearchFolders()->GetSearchResults(m_ulStoreId, m_ulFolderId, &lstObjId);
	if (er != erSuccess)
		return er;
	return UpdateRows(ECKeyTable::TABLE_ROW_ADD, &lstObjId, 0, true);
}

} /* namespace */
