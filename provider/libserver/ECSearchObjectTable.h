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

#ifndef ECSEARCHOBJECTTABLE_H
#define ECSEARCHOBJECTTABLE_H

#include <kopano/zcdefs.h>
#include "ECStoreObjectTable.h"

// The search folders only differ from normal 'store' tables in that they load the object list
// from the searchresults instead of from the hierarchy table.
class ECSearchObjectTable _zcp_final : public ECStoreObjectTable {
protected:
	ECSearchObjectTable(ECSession *lpSession, unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale, ECSearchObjectTable **lppTable);

    virtual ECRESULT Load();
    
private:
    unsigned int m_ulFolderId;
    unsigned int m_ulStoreId;
};

#endif
