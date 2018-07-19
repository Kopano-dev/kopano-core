/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSTORE_OBJECTTABLE_H
#define ECSTORE_OBJECTTABLE_H

#include <kopano/Util.h>
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
	virtual ECRESULT Load() override;

	//Overrides
	virtual ECRESULT GetColumnsAll(ECListInt *props) override;
    
	// Static database row functions, can be used externally aswell .. Obviously these are *not* threadsafe, make sure that
	// you either lock the passed arguments or all arguments are from the local stack.

	static ECRESULT CopyEmptyCellToSOAPPropVal(struct soap *soap, unsigned int ulPropTag, struct propVal *lpPropVal);
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit);
	static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool cache_table_data, bool table_limit, bool sub_objects);

protected:
	virtual ECRESULT AddRowKey(ECObjectTableList *rows, unsigned int *loaded, unsigned int flags, bool first_load, bool override, struct restrictTable *override_tbl) override;
	static ECRESULT QueryRowDataByColumn(ECGenericObjectTable *, struct soap *, ECSession *, const std::multimap<unsigned int, unsigned int> &columns, unsigned int folder, const std::map<sObjectTableKey, unsigned int> &objids, struct rowSet *);
	static ECRESULT QueryRowDataByRow(ECGenericObjectTable *, struct soap *, ECSession *, const sObjectTableKey &, unsigned int rownum, std::multimap<unsigned int, unsigned int> &columns, bool table_limit, struct rowSet *);

private:
	static ECRESULT GetMVRowCountHelper(ECDatabase *db, std::string query, std::list<unsigned int> &ids, std::map<unsigned int, unsigned int> &count);
	virtual ECRESULT GetMVRowCount(std::list<unsigned int> &&obj_ids, std::map<unsigned int, unsigned int> &count) override;
	virtual ECRESULT ReloadTableMVData(ECObjectTableList *rows, ECListInt *mvproptags) override;
	virtual ECRESULT CheckPermissions(unsigned int obj_id) override;

	unsigned int ulPermission = 0;
	bool fPermissionRead = false;
	ALLOC_WRAP_FRIEND;
};

ECRESULT GetDeferredTableUpdates(ECDatabase *lpDatabase, unsigned int ulFolderId, std::list<unsigned int> *lpDeferred);
extern ECRESULT GetDeferredTableUpdates(ECDatabase *, const ECObjectTableList *, std::list<unsigned int> *deferred);

} /* namespace */

#endif // OBJECTTABLE_H
