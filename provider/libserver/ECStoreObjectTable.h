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

#ifndef ECSTORE_OBJECTTABLE_H
#define ECSTORE_OBJECTTABLE_H

#include "soapH.h"
#include "ECDatabase.h"

#include "ECGenericObjectTable.h"

struct soap;

namespace KC {

/*
 * This object is an actual table, with a cursor in-memory. We also keep the complete
 * keyset of the table in memory, so seeks and queries can be really fast. Also, we
 * sort the table once on loading, and simply use that keyset when the rows are actually
 * required. The actual data is always loaded directly from the database.
 */
class ECSession;

// Objectdata for a store
struct ECODStore {
	unsigned int	ulStoreId;		// The Store ID this table is watching (0 == multi-store)
	unsigned int	ulFolderId;		// The Folder ID this table is watching (0 == multi-folder)
	unsigned int	ulObjType;
	unsigned int	ulFlags;
	unsigned int 	ulTableFlags;
	GUID*			lpGuid;			// The GUID of the store
};

// For ulTableFlags
#define TABLE_FLAG_OVERRIDE_HOME_MDB 0x00000001

class ECStoreObjectTable : public ECGenericObjectTable {
protected:
	ECStoreObjectTable(ECSession *lpSession, unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId, unsigned int ulObjType, unsigned int ulFlags, unsigned int ulTableFlags, const ECLocale &locale);
	virtual ~ECStoreObjectTable();
public:
	static ECRESULT Create(ECSession *lpSession, unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId, unsigned int ulObjType, unsigned int ulFlags, unsigned int ulTableFlags, const ECLocale &locale, ECStoreObjectTable **lppTable);
	virtual ECRESULT Load();

	//Overrides
	virtual ECRESULT GetColumnsAll(ECListInt* lplstProps);
    
	// Static database row functions, can be used externally aswell .. Obviously these are *not* threadsafe, make sure that
	// you either lock the passed arguments or all arguments are from the local stack.

	static ECRESULT CopyEmptyCellToSOAPPropVal(struct soap *soap, unsigned int ulPropTag, struct propVal *lpPropVal);
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, ECObjectTableList *, struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit);
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, ECObjectTableList *, struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit, bool sub_objects);

protected:
	virtual ECRESULT AddRowKey(ECObjectTableList* lpRows, unsigned int *lpulLoaded, unsigned int ulFlags, bool bInitialLoad, bool bOverride, struct restrictTable *lpOverride);
	
	static ECRESULT QueryRowDataByColumn(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSesion, const std::multimap<unsigned int, unsigned int> &mapColumns, unsigned int ulFolderId, const std::map<sObjectTableKey, unsigned int> &mapObjIds, struct rowSet *lpRowSet);
	static ECRESULT QueryRowDataByRow(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, const sObjectTableKey &sKey, unsigned int ulRowNum, std::multimap<unsigned int, unsigned int> &mapColumns, bool bTableLimit, struct rowSet *lpsRowSet);

private:
	virtual ECRESULT GetMVRowCount(unsigned int ulObjId, unsigned int *lpulCount);
	virtual ECRESULT ReloadTableMVData(ECObjectTableList* lplistRows, ECListInt* lplistMVPropTag);
	virtual ECRESULT CheckPermissions(unsigned int ulObjId);

	unsigned int ulPermission = 0;
	bool fPermissionRead = false;
};

ECRESULT GetDeferredTableUpdates(ECDatabase *lpDatabase, unsigned int ulFolderId, std::list<unsigned int> *lpDeferred);
ECRESULT GetDeferredTableUpdates(ECDatabase *lpDatabase, ECObjectTableList* lpRowList, std::list<unsigned int> *lpDeferred);

} /* namespace */

#endif // OBJECTTABLE_H
