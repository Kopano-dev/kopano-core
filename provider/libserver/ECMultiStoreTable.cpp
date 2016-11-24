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

#include "ECSecurity.h"
#include "ECSessionManager.h"
#include "ECMultiStoreTable.h"
#include "ECSession.h"
#include "ECMAPI.h"
#include <kopano/stringutil.h>

namespace KC {

ECMultiStoreTable::ECMultiStoreTable(ECSession *lpSession, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale) : ECStoreObjectTable(lpSession, 0, NULL, 0, ulObjType, ulFlags, 0, locale) {
}

ECRESULT ECMultiStoreTable::Create(ECSession *lpSession, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale, ECMultiStoreTable **lppTable)
{
	*lppTable = new ECMultiStoreTable(lpSession, ulObjType, ulFlags, locale);

	(*lppTable)->AddRef();

	return erSuccess;
}

ECRESULT ECMultiStoreTable::SetEntryIDs(ECListInt *lplObjectList) {
	ECRESULT er = erSuccess;
	
	m_lstObjects = *lplObjectList;

	return er;
}

ECRESULT ECMultiStoreTable::Load() {
	ECRESULT er = erSuccess;
	ECListIntIterator i;
	sObjectTableKey		sRowItem;

	Clear();
	for (i = m_lstObjects.begin(); i != m_lstObjects.end(); ++i)
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, *i, 0);
	return er;
}

} /* namespace */
