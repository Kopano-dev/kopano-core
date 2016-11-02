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

#ifndef EC_USERSTORE_TABLE_H
#define EC_USERSTORE_TABLE_H

#include <kopano/zcdefs.h>
/* #include "ECStoreObjectTable.h" */
#include "ECGenericObjectTable.h"
#include <kopano/pcuser.hpp>
#include <map>

class ECSession;

typedef struct {
	long long		ulUserId;		// id of user (-1 if there is no user)
	objectid_t		sExternId;		// externid of user
	std::string		strUsername;	// actual username from ECUserManagement
	std::string		strGuessname;	// "guess" from user_name column in stores table
	unsigned int	ulCompanyId;	// company id of store (or user if store is not found)
	GUID			sGuid;			// The GUID of the store
	unsigned int	ulStoreType;	// Type of the store (private, public, archive)
	unsigned int	ulObjId;		// Hierarchyid of the store
	std::string		strCompanyName;	// Company name of the store. (can be empty)
	time_t			tModTime;		// Modification time of the store
	unsigned long long ullStoreSize;// Size of the store
} ECUserStore;

class ECUserStoreTable _kc_final : public ECGenericObjectTable {
protected:
	ECUserStoreTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale);

public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECUserStoreTable **lppTable);

	static ECRESULT QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit);

    virtual ECRESULT Load();

private:
	std::map<unsigned int, ECUserStore> m_mapUserStoreData;
};

#endif
