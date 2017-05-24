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
#include "ECDatabase.h"

#include <mapidefs.h>

#include "ECSecurity.h"
#include "ECSessionManager.h"
#include "ECMultiStoreTable.h"
#include "ECSession.h"
#include "ECMAPI.h"
#include <kopano/stringutil.h>
#include <kopano/Util.h>

namespace KC {

ECMultiStoreTable::ECMultiStoreTable(ECSession *lpSession, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale) : ECStoreObjectTable(lpSession, 0, NULL, 0, ulObjType, ulFlags, 0, locale) {
}

ECRESULT ECMultiStoreTable::Create(ECSession *lpSession, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale, ECMultiStoreTable **lppTable)
{
	return alloc_wrap<ECMultiStoreTable>(lpSession, ulObjType,
	       ulFlags, locale).put(lppTable);
}

ECRESULT ECMultiStoreTable::SetEntryIDs(ECListInt *lplObjectList) {
	m_lstObjects = *lplObjectList;
	return erSuccess;
}

ECRESULT ECMultiStoreTable::Load() {
	Clear();
	for (auto i = m_lstObjects.begin(); i != m_lstObjects.end(); ++i)
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, *i, 0);
	return erSuccess;
}

} /* namespace */
