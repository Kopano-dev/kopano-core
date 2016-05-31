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
#include "ECDatabase.h"

#include <mapidefs.h>
#include <edkmdb.h>

#include "ECSecurity.h"
#include "ECSessionManager.h"
#include "ECGenProps.h"
#include "ECSession.h"
#include <kopano/stringutil.h>

#include "ECMailBoxTable.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

ECMailBoxTable::ECMailBoxTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale) : 
	ECStoreObjectTable(lpSession, 0, NULL, 0, MAPI_STORE, ulFlags, TABLE_FLAG_OVERRIDE_HOME_MDB, locale)
{
	m_ulStoreTypes = 3; // 1. Show all users store 2. Public stores
}

ECRESULT ECMailBoxTable::Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECMailBoxTable **lppTable)
{
	*lppTable = new ECMailBoxTable(lpSession, ulFlags, locale);

	(*lppTable)->AddRef();

	return erSuccess;
}

ECRESULT ECMailBoxTable::Load()
{
	ECRESULT er = erSuccess;
	ECDatabase *lpDatabase = NULL;
	DB_RESULT 	lpDBResult = NULL;
	DB_ROW		lpDBRow = NULL;
	std::string strQuery;
	std::list<unsigned int> lstObjIds;

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	Clear();

	//@todo Load all stores depends on m_ulStoreTypes, 1. privates, 2. publics or both
	strQuery = "SELECT hierarchy_id FROM stores";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	while(1) {
		lpDBRow = lpDatabase->FetchRow(lpDBResult);

		if(lpDBRow == NULL)
			break;

		if (!lpDBRow[0])
			continue; // Broken store table?

		lstObjIds.push_back(atoui(lpDBRow[0]));
	}

	LoadRows(&lstObjIds, 0);

exit:
	if (lpDBResult) {
		lpDatabase->FreeResult(lpDBResult);
		lpDBResult = NULL;
	}
	return er;
}
