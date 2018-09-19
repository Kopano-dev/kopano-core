/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef TABLEMANAGER_H
#define TABLEMANAGER_H

#include <kopano/zcdefs.h>
#include <map>
#include <memory>
#include "ECDatabase.h"
#include "ECGenericObjectTable.h"
#include <kopano/kcodes.h>
#include <kopano/Util.h>
#include "ECStoreObjectTable.h"

namespace KC {

class ECSession;
class ECSessionManager;

/*
 * The table manager is responsible for opening tables, and providing
 * access to open tables by a handle ID for each table, and updating tables when
 * a change is made to the underlying data and sending notifications to clients
 * which require table update notifications.
 *
 * Each session has its own table manager. This could be optimised in the future
 * by having one large table manager, with each table having multiple cursors for
 * each user that has the table open. This would save memory on overlapping tables
 * (probably resulting in around 30% less memory usage for the server).
 */

struct TABLE_ENTRY {
	enum TABLE_TYPE {
		TABLE_TYPE_GENERIC, TABLE_TYPE_OUTGOINGQUEUE, TABLE_TYPE_MULTISTORE, TABLE_TYPE_USERSTORES,
		TABLE_TYPE_SYSTEMSTATS, TABLE_TYPE_THREADSTATS, TABLE_TYPE_USERSTATS, TABLE_TYPE_SESSIONSTATS, TABLE_TYPE_COMPANYSTATS, TABLE_TYPE_SERVERSTATS,
		TABLE_TYPE_MAILBOX,
	};

    TABLE_TYPE ulTableType;

	union {
		struct {
			unsigned int ulParentId;
			unsigned int ulObjectType;
			unsigned int ulObjectFlags;
		} sGeneric ;
		struct {
			unsigned int ulStoreId;
			unsigned int ulFlags;
		} sOutgoingQueue;
	} sTable;
	ECGenericObjectTable *lpTable; // Actual table object
	unsigned int ulSubscriptionId; // Subscription ID for table event subscription on session manager
};

class ECTableManager final {
public:
	ECTableManager(ECSession *s) : lpSession(s) {}
	~ECTableManager();
	ECRESULT	OpenGenericTable(unsigned int ulParent, unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId, bool fLoad = true);
	ECRESULT	OpenOutgoingQueueTable(unsigned int ulStoreId, unsigned int *lpulTableId);
	ECRESULT	OpenABTable(unsigned int ulParent, unsigned int ulParentType, unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId);
	ECRESULT	OpenMultiStoreTable(unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId);
	ECRESULT	OpenUserStoresTable(unsigned int ulFlags, unsigned int *lpulTableId);
	ECRESULT	OpenStatsTable(unsigned int ulTableType, unsigned int ulFlags, unsigned int *lpulTableId);
	ECRESULT	OpenMailBoxTable(unsigned int ulflags, unsigned int *lpulTableId);
	ECRESULT	GetTable(unsigned int lpulTableId, ECGenericObjectTable **lppTable);
	ECRESULT	CloseTable(unsigned int lpulTableId);
	ECRESULT	UpdateOutgoingTables(ECKeyTable::UpdateType ulType, unsigned int ulStoreId, std::list<unsigned int> &lstObjId, unsigned int ulFlags, unsigned int ulObjType);
	ECRESULT	UpdateTables(ECKeyTable::UpdateType ulType, unsigned int ulFlags, unsigned int ulObjId, std::list<unsigned int> &lstChildId, unsigned int ulObjType);
	ECRESULT	GetStats(unsigned int *lpulTables, unsigned int *lpulObjectSize);

private:
	static	void *	SearchThread(void *lpParam);
	void AddTableEntry(std::unique_ptr<TABLE_ENTRY> &&, unsigned int *id);

	ECSession								*lpSession;
	typedef std::map<unsigned int, std::unique_ptr<TABLE_ENTRY>> TABLEENTRYMAP;
	TABLEENTRYMAP							mapTable;
	unsigned int ulNextTableId = 1;
	std::recursive_mutex hListMutex;
};

class _kc_export_dycast ECMultiStoreTable final : public ECStoreObjectTable {
	protected:
	_kc_hidden ECMultiStoreTable(ECSession *, unsigned int obj_type, unsigned int flags, const ECLocale &);

	public:
	_kc_hidden static ECRESULT Create(ECSession *, unsigned int obj_type, unsigned int flags, const ECLocale &, ECMultiStoreTable **ret);
	_kc_hidden virtual ECRESULT SetEntryIDs(ECListInt *obj_list);
	_kc_hidden virtual ECRESULT Load(void);

	private:
	std::list<unsigned int> m_lstObjects;
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif // TABLEMANAGER_H
