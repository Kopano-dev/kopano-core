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
#include <string>
#include <utility>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <mapitags.h>
#include "ECMAPI.h"
#include "ECGenericObjectTable.h"
#include "ECSearchObjectTable.h"
#include "ECConvenientDepthObjectTable.h"
#include "ECStoreObjectTable.h"
#include "ECMultiStoreTable.h"
#include "ECUserStoreTable.h"
#include "ECABObjectTable.h"
#include "ECStatsTables.h"
#include "ECTableManager.h"
#include "ECSessionManager.h"
#include "ECSession.h"
#include "ECDatabaseUtils.h"
#include "ECSecurity.h"
#include "ECSubRestriction.h"
#include <kopano/ECDefs.h>
#include "SOAPUtils.h"
#include <kopano/stringutil.h>
#include "ECServerEntrypoint.h"
#include "ECMailBoxTable.h"

#include <kopano/mapiext.h>
#include <edkmdb.h>

namespace KC {

void FreeRowSet(struct rowSet *lpRowSet, bool bBasePointerDel);

static const unsigned int sContentsProps[] = {
	PR_ENTRYID, PR_DISPLAY_NAME, PR_MESSAGE_FLAGS, PR_SUBJECT,
	PR_STORE_ENTRYID, PR_STORE_RECORD_KEY, PR_STORE_SUPPORT_MASK,
	PR_INSTANCE_KEY, PR_RECORD_KEY, PR_ACCESS, PR_ACCESS_LEVEL,
};
static const unsigned int sHierarchyProps[] = {
	PR_ENTRYID, PR_DISPLAY_NAME, PR_CONTENT_COUNT, PR_CONTENT_UNREAD,
	PR_STORE_ENTRYID, PR_STORE_RECORD_KEY, PR_STORE_SUPPORT_MASK,
	PR_INSTANCE_KEY, PR_RECORD_KEY, PR_ACCESS, PR_ACCESS_LEVEL,
};
static const unsigned int sABContentsProps[] = {
	PR_ENTRYID, PR_DISPLAY_NAME, PR_INSTANCE_KEY, PR_ADDRTYPE,
	PR_DISPLAY_TYPE, PR_DISPLAY_TYPE_EX, PR_EMAIL_ADDRESS, PR_SMTP_ADDRESS,
	PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY, PR_DEPARTMENT_NAME,
	PR_OFFICE_TELEPHONE_NUMBER, PR_OFFICE_LOCATION, PR_PRIMARY_FAX_NUMBER,
};
static const unsigned int sABHierarchyProps[] = {
	PR_ENTRYID, PR_DISPLAY_NAME, PR_INSTANCE_KEY, PR_DISPLAY_TYPE,
	PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY,
};

static const unsigned int sUserStoresProps[] = {
	PR_EC_USERNAME, PR_EC_STOREGUID, PR_EC_STORETYPE, PR_DISPLAY_NAME,
	PR_EC_COMPANYID, PR_EC_COMPANY_NAME, PR_STORE_ENTRYID,
	PR_LAST_MODIFICATION_TIME, PR_MESSAGE_SIZE_EXTENDED,
};

// stats tables
static const unsigned int sSystemStatsProps[] = {
	PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_DESCRIPTION,
	PR_EC_STATS_SYSTEM_VALUE,
};
static const unsigned int sSessionStatsProps[] = {
	PR_EC_STATS_SESSION_ID, PR_EC_STATS_SESSION_GROUP_ID,
	PR_EC_STATS_SESSION_IPADDRESS, PR_EC_STATS_SESSION_IDLETIME,
	PR_EC_STATS_SESSION_CAPABILITY, PR_EC_STATS_SESSION_LOCKED,
	PR_EC_USERNAME, PR_EC_STATS_SESSION_BUSYSTATES,
	PR_EC_STATS_SESSION_PROCSTATES, PR_EC_STATS_SESSION_CPU_USER,
	PR_EC_STATS_SESSION_CPU_SYSTEM, PR_EC_STATS_SESSION_CPU_REAL,
	PR_EC_STATS_SESSION_PEER_PID, PR_EC_STATS_SESSION_CLIENT_VERSION,
	PR_EC_STATS_SESSION_CLIENT_APPLICATION, PR_EC_STATS_SESSION_REQUESTS,
	PR_EC_STATS_SESSION_PORT, PR_EC_STATS_SESSION_PROXY,
	PR_EC_STATS_SESSION_URL, PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION,
	PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC,
};
static const unsigned int sUserStatsProps[] = {
	PR_EC_COMPANY_NAME, PR_EC_USERNAME, PR_DISPLAY_NAME, PR_SMTP_ADDRESS,
	PR_EC_NONACTIVE, PR_EC_ADMINISTRATOR, PR_EC_HOMESERVER_NAME,
	PR_MESSAGE_SIZE_EXTENDED, PR_QUOTA_WARNING_THRESHOLD,
	PR_QUOTA_SEND_THRESHOLD, PR_QUOTA_RECEIVE_THRESHOLD,
	PR_EC_QUOTA_MAIL_TIME, PR_EC_OUTOFOFFICE, PR_LAST_LOGON_TIME,
	PR_LAST_LOGOFF_TIME,
};
static const unsigned int sCompanyStatsProps[] = {
	PR_EC_COMPANY_NAME, PR_EC_COMPANY_ADMIN, PR_MESSAGE_SIZE_EXTENDED,
	PR_QUOTA_WARNING_THRESHOLD, PR_QUOTA_SEND_THRESHOLD,
	PR_QUOTA_RECEIVE_THRESHOLD, PR_EC_QUOTA_MAIL_TIME,
};
static const unsigned int sServerStatsProps[] = {
	PR_EC_STATS_SERVER_NAME, PR_EC_STATS_SERVER_HOST,
	PR_EC_STATS_SERVER_HTTPPORT, PR_EC_STATS_SERVER_SSLPORT,
	PR_EC_STATS_SERVER_PROXYURL, PR_EC_STATS_SERVER_HTTPURL,
	PR_EC_STATS_SERVER_HTTPSURL, PR_EC_STATS_SERVER_FILEURL,
};

static const struct propTagArray sPropTagArrayContents =
	{const_cast<unsigned int *>(sContentsProps), ARRAY_SIZE(sContentsProps)};
static const struct propTagArray sPropTagArrayHierarchy =
	{const_cast<unsigned int *>(sHierarchyProps), ARRAY_SIZE(sHierarchyProps)};
static const struct propTagArray sPropTagArrayABContents =
	{const_cast<unsigned int *>(sABContentsProps), ARRAY_SIZE(sABContentsProps)};
static const struct propTagArray sPropTagArrayABHierarchy =
	{const_cast<unsigned int *>(sABHierarchyProps), ARRAY_SIZE(sABHierarchyProps)};
static const struct propTagArray sPropTagArrayUserStores =
	{const_cast<unsigned int *>(sUserStoresProps), ARRAY_SIZE(sUserStoresProps)};

static const struct propTagArray sPropTagArraySystemStats =
	{const_cast<unsigned int *>(sSystemStatsProps), ARRAY_SIZE(sSystemStatsProps)};
static const struct propTagArray sPropTagArraySessionStats =
	{const_cast<unsigned int *>(sSessionStatsProps), ARRAY_SIZE(sSessionStatsProps)};
static const struct propTagArray sPropTagArrayUserStats =
	{const_cast<unsigned int *>(sUserStatsProps), ARRAY_SIZE(sUserStatsProps)};
static const struct propTagArray sPropTagArrayCompanyStats =
	{const_cast<unsigned int *>(sCompanyStatsProps), ARRAY_SIZE(sCompanyStatsProps)};
static const struct propTagArray sPropTagArrayServerStats =
	{const_cast<unsigned int *>(sServerStatsProps), ARRAY_SIZE(sServerStatsProps)};

ECTableManager::ECTableManager(ECSession *lpSession)
{
	this->lpSession = lpSession;
}

ECTableManager::~ECTableManager()
{
	scoped_rlock lock(hListMutex);

	// Clean up tables, if CloseTable(..) isn't called 
	for (auto iterTables = mapTable.cbegin();
	     iterTables != mapTable.cend(); ) {
		auto iterNext = iterTables;
		++iterNext;
		CloseTable(iterTables->first);
		iterTables = iterNext;
	}
	mapTable.clear();
}

void ECTableManager::AddTableEntry(std::unique_ptr<TABLE_ENTRY> &&arg,
    unsigned int *lpulTableId)
{
	scoped_rlock lock(hListMutex);
	mapTable[ulNextTableId] = std::move(arg);
	auto &lpEntry = mapTable[ulNextTableId];
	lpEntry->lpTable->AddRef();

	*lpulTableId = ulNextTableId;
	
	lpEntry->lpTable->SetTableId(*lpulTableId);

	// Subscribe to events for this table, if needed
	switch(lpEntry->ulTableType) {
	case TABLE_ENTRY::TABLE_TYPE_GENERIC:
        	lpSession->GetSessionManager()->SubscribeTableEvents(lpEntry->ulTableType,
			lpEntry->sTable.sGeneric.ulParentId, lpEntry->sTable.sGeneric.ulObjectType,
			lpEntry->sTable.sGeneric.ulObjectFlags, lpSession->GetSessionId());
        	break;
	case TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE:
	        lpSession->GetSessionManager()->SubscribeTableEvents(lpEntry->ulTableType,
			lpEntry->sTable.sOutgoingQueue.ulFlags & EC_SUBMIT_MASTER ? 0 : lpEntry->sTable.sOutgoingQueue.ulStoreId,
			MAPI_MESSAGE, lpEntry->sTable.sOutgoingQueue.ulFlags, lpSession->GetSessionId());
	        break;
        default:
            // Other table types don't need updates from other sessions
            break;
    }
	++ulNextTableId;
}

ECRESULT ECTableManager::OpenOutgoingQueueTable(unsigned int ulStoreId, unsigned int *lpulTableId)
{
	object_ptr<ECStoreObjectTable> lpTable;
	std::unique_ptr<TABLE_ENTRY> lpEntry;
	DB_RESULT lpDBResult;
	DB_ROW		lpDBRow = NULL;
	std::string strQuery;
	struct propTagArray *lpsPropTags = NULL;
	GUID sGuid;
	sObjectTableKey sRowItem;
	const ECLocale locale = lpSession->GetSessionManager()->GetSortLocale(ulStoreId);
	ECDatabase *lpDatabase = NULL;

	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	if(ulStoreId) {
		er = lpSession->GetSessionManager()->GetCacheManager()->GetStore(ulStoreId, &ulStoreId, &sGuid);

		if(er != erSuccess)
			goto exit;
		er = ECStoreObjectTable::Create(lpSession, ulStoreId, &sGuid, 0, MAPI_MESSAGE, 0, 0, locale, &~lpTable);
	} else {
		// FIXME check permissions for master outgoing table
		// Master outgoing table has different STORE_ENTRYID and GUID per row
		er = ECStoreObjectTable::Create(lpSession, 0, NULL, 0, MAPI_MESSAGE, 0, 0, locale, &~lpTable);
	}
	if(er != erSuccess)
		return er;

	// Select outgoingqueue
	strQuery = "SELECT o.hierarchy_id, i.hierarchyid FROM outgoingqueue AS o LEFT JOIN indexedproperties AS i ON i.hierarchyid=o.hierarchy_id and i.tag=4095";
	if(ulStoreId)
		strQuery += " WHERE o.flags & 1 =" + stringify(EC_SUBMIT_LOCAL) + " AND o.store_id=" + stringify(ulStoreId);
	else
		strQuery += " WHERE o.flags & 1 =" + stringify(EC_SUBMIT_MASTER);

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if(lpDBRow[0] == NULL)
			continue;

		// Item found without entryid, item already deleted, so delete the item from the queue
		if (lpDBRow[1] == NULL) {
			ec_log_err("Removing stray object \"%s\" from outgoing table", lpDBRow[0]);
			strQuery = "DELETE FROM outgoingqueue WHERE hierarchy_id=" + std::string(lpDBRow[0]);
			lpDatabase->DoDelete(strQuery); //ignore errors
			continue;
		}
		
		lpTable->UpdateRow(ECKeyTable::TABLE_ROW_ADD, atoi(lpDBRow[0]), 0);
	}

	lpEntry.reset(new TABLE_ENTRY);
	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE;
	lpEntry->sTable.sOutgoingQueue.ulStoreId = ulStoreId;
	lpEntry->sTable.sOutgoingQueue.ulFlags = ulStoreId ? EC_SUBMIT_LOCAL : EC_SUBMIT_MASTER;
	AddTableEntry(std::move(lpEntry), lpulTableId);
	if (lpTable->GetColumns(NULL, TBL_ALL_COLUMNS, &lpsPropTags) == erSuccess)
		lpTable->SetColumns(lpsPropTags, false);
	lpTable->SeekRow(BOOKMARK_BEGINNING, 0, NULL);

exit:
	if(lpsPropTags)
		FreePropTagArray(lpsPropTags);

	return er;
}

ECRESULT ECTableManager::OpenUserStoresTable(unsigned int ulFlags, unsigned int *lpulTableId)
{
	object_ptr<ECUserStoreTable> lpTable;
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");

	auto er = ECUserStoreTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);
	if (er != erSuccess)
		return er;

	std::unique_ptr<TABLE_ENTRY> lpEntry(new(std::nothrow) TABLE_ENTRY);
	if (lpEntry == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_USERSTORES;
	memset(&lpEntry->sTable, 0, sizeof(lpEntry->sTable));
	er = lpTable->SetColumns(&sPropTagArrayUserStores, true);
	if (er != erSuccess)
		return er;
	AddTableEntry(std::move(lpEntry), lpulTableId);
	return erSuccess;
}

ECRESULT ECTableManager::OpenMultiStoreTable(unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId)
{
	object_ptr<ECMultiStoreTable> lpTable;
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");

	// Open an empty table. Contents will be provided by client in a later call.
	auto er = ECMultiStoreTable::Create(lpSession, ulObjType, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);
	if (er != erSuccess)
		return er;

	std::unique_ptr<TABLE_ENTRY> lpEntry(new(std::nothrow) TABLE_ENTRY);
	if (lpEntry == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_MULTISTORE;
	memset(&lpEntry->sTable, 0, sizeof(lpEntry->sTable));
	AddTableEntry(std::move(lpEntry), lpulTableId);
	return er;
}

ECRESULT ECTableManager::OpenGenericTable(unsigned int ulParent, unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId, bool fLoad)
{
	std::string		strQuery;
	object_ptr<ECStoreObjectTable> lpTable;
	std::unique_ptr<TABLE_ENTRY> lpEntry;
	unsigned int	ulStoreId = 0;
	GUID			sGuid;
	ECDatabase *lpDatabase = NULL;

	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	auto sesmgr = lpSession->GetSessionManager();
	er = sesmgr->GetCacheManager()->GetStore(ulParent, &ulStoreId, &sGuid);
	if(er != erSuccess)
		return er;

	auto locale = sesmgr->GetSortLocale(ulStoreId);
	if (sesmgr->GetSearchFolders()->IsSearchFolder(ulParent) == erSuccess) {
		if (ulObjType == MAPI_FOLDER || ulFlags & (MSGFLAG_DELETED | MAPI_ASSOCIATED))
			return KCERR_NO_SUPPORT;
		er = lpSession->GetSecurity()->CheckPermission(ulParent, ecSecurityFolderVisible);
		if(er != erSuccess)
			return er;
		else
			er = ECSearchObjectTable::Create(lpSession, ulStoreId, &sGuid, ulParent, ulObjType, ulFlags, locale, &~lpTable);

	} else if(ulObjType == MAPI_FOLDER && (ulFlags & CONVENIENT_DEPTH))
		er = ECConvenientDepthObjectTable::Create(lpSession, ulStoreId, &sGuid, ulParent, ulObjType, ulFlags, locale, &~lpTable);
	else
		er = ECStoreObjectTable::Create(lpSession, ulStoreId, &sGuid, ulParent, ulObjType, ulFlags, 0, locale, &~lpTable);

	if (er != erSuccess)
		return er;

	lpEntry.reset(new(std::nothrow) TABLE_ENTRY);
	if (lpEntry == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_GENERIC;
	lpEntry->sTable.sGeneric.ulObjectFlags = ulFlags & (MAPI_ASSOCIATED | MSGFLAG_DELETED); // MSGFLAG_DELETED because of conversion in ns__tableOpen
	lpEntry->sTable.sGeneric.ulObjectType = ulObjType;
	lpEntry->sTable.sGeneric.ulParentId = ulParent;

	// First, add table to internal list of tables. This means we can already start
	// receiving notifications on this table
	AddTableEntry(std::move(lpEntry), lpulTableId);

	// Load a default column set
	if (ulObjType == MAPI_MESSAGE)
		lpTable->SetColumns(&sPropTagArrayContents, true);
	else
		lpTable->SetColumns(&sPropTagArrayHierarchy, true);
	return erSuccess;
}

static void AuditStatsAccess(ECSession *lpSession, const char *access, const char *table)
{
	if (!lpSession->GetSessionManager()->GetAudit())
		return;
	std::string strUsername;
	std::string strImpersonator;
	auto sec = lpSession->GetSecurity();
	sec->GetUsername(&strUsername);
	auto audit = lpSession->GetSessionManager()->GetAudit();
	if (sec->GetImpersonator(&strImpersonator) == erSuccess)
		ZLOG_AUDIT(audit, "access %s table='%s stats' username=%s impersonator=%s", access, table, strUsername.c_str(), strImpersonator.c_str());
	else
		ZLOG_AUDIT(audit, "access %s table='%s stats' username=%s", access, table, strUsername.c_str());
}

ECRESULT ECTableManager::OpenStatsTable(unsigned int ulTableType, unsigned int ulFlags, unsigned int *lpulTableId)
{
	ECRESULT er = erSuccess;
	object_ptr<ECGenericObjectTable> lpTable;
	std::unique_ptr<TABLE_ENTRY> lpEntry;
	int adminlevel = lpSession->GetSecurity()->GetAdminLevel();
	bool hosted = lpSession->GetSessionManager()->IsHostedSupported();
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");
	
	// TABLETYPE_STATS_SYSTEM: only for (sys)admins
	// TABLETYPE_STATS_SESSIONS: only for (sys)admins
	// TABLETYPE_STATS_USERS: full list: only for (sys)admins, company list: only for admins

	lpEntry.reset(new(std::nothrow) TABLE_ENTRY);
	if (lpEntry == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	switch (ulTableType) {
	case TABLETYPE_STATS_SYSTEM:
		if ((hosted && adminlevel < ADMIN_LEVEL_SYSADMIN) || (!hosted && adminlevel < ADMIN_LEVEL_ADMIN)) {
			AuditStatsAccess(lpSession, "denied", "system");
			return KCERR_NO_ACCESS;
		}
		er = ECSystemStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);
		if (er != erSuccess)
			return er;
		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_SYSTEMSTATS;
		er = lpTable->SetColumns(&sPropTagArraySystemStats, true);
		break;
	case TABLETYPE_STATS_SESSIONS:
		if ((hosted && adminlevel < ADMIN_LEVEL_SYSADMIN) || (!hosted && adminlevel < ADMIN_LEVEL_ADMIN)) {
			AuditStatsAccess(lpSession, "denied", "session");
			return KCERR_NO_ACCESS;
		}
		er = ECSessionStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);
		if (er != erSuccess)
			return er;
		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_SESSIONSTATS;
		er = lpTable->SetColumns(&sPropTagArraySessionStats, true);
		break;
	case TABLETYPE_STATS_USERS:
		if (adminlevel < ADMIN_LEVEL_ADMIN) {
			AuditStatsAccess(lpSession, "denied", "user");
			return KCERR_NO_ACCESS;
		}
		er = ECUserStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);
		if (er != erSuccess)
			return er;
		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_USERSTATS;
		er = lpTable->SetColumns(&sPropTagArrayUserStats, true);
		break;
	case TABLETYPE_STATS_COMPANY:
		if (!hosted)
			return KCERR_NOT_FOUND;
		if (adminlevel < ADMIN_LEVEL_SYSADMIN) {
			AuditStatsAccess(lpSession, "denied", "company");
			return KCERR_NO_ACCESS;
		}
		er = ECCompanyStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);
		if (er != erSuccess)
			return er;
		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_COMPANYSTATS;
		er = lpTable->SetColumns(&sPropTagArrayCompanyStats, true);
		break;
	case TABLETYPE_STATS_SERVERS:
		if (adminlevel < ADMIN_LEVEL_SYSADMIN) {
			AuditStatsAccess(lpSession, "denied", "company");
			return KCERR_NO_ACCESS;
		}
		er = ECServerStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);
		if (er != erSuccess)
			return er;
		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_SERVERSTATS;
		er = lpTable->SetColumns(&sPropTagArrayServerStats, true);
		break;
	default:
		er = KCERR_UNKNOWN;
		break;
	}
	if (er != erSuccess)
		return er;

	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	memset(&lpEntry->sTable, 0, sizeof(lpEntry->sTable));
	AddTableEntry(std::move(lpEntry), lpulTableId);
	return erSuccess;
}

ECRESULT ECTableManager::OpenMailBoxTable(unsigned int ulflags, unsigned int *lpulTableId)
{
	object_ptr<ECMailBoxTable> lpTable;
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");

	auto er = ECMailBoxTable::Create(lpSession, ulflags, createLocaleFromName(lpszLocaleId), &~lpTable);
	if (er != erSuccess)
		return er;

	std::unique_ptr<TABLE_ENTRY> lpEntry(new(std::nothrow) TABLE_ENTRY);
	if (lpEntry == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_MAILBOX;
	memset(&lpEntry->sTable, 0, sizeof(lpEntry->sTable));

	//@todo check this list!!!
	er = lpTable->SetColumns(&sPropTagArrayUserStores, true);
	if (er != erSuccess)
		return er;
	AddTableEntry(std::move(lpEntry), lpulTableId);
	return erSuccess;
}

/*
	Open an addressbook table
*/
ECRESULT ECTableManager::OpenABTable(unsigned int ulParent, unsigned int ulParentType, unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId)
{
	ECRESULT er = erSuccess;
	object_ptr<ECABObjectTable> lpTable;
	std::unique_ptr<TABLE_ENTRY> lpEntry;
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");

	// Open first container
	if (ulFlags & CONVENIENT_DEPTH)
		er = ECConvenientDepthABObjectTable::Create(lpSession, 1, ulObjType, ulParent, ulParentType, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);
	else
		er = ECABObjectTable::Create(lpSession, 1, ulObjType, ulParent, ulParentType, ulFlags, createLocaleFromName(lpszLocaleId), &~lpTable);

	if (er != erSuccess)
		return er;

	lpEntry.reset(new TABLE_ENTRY);
	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_GENERIC;
	lpEntry->sTable.sGeneric.ulObjectFlags = ulFlags & (MAPI_ASSOCIATED | MSGFLAG_DELETED); // MSGFLAG_DELETED because of conversion in ns__tableOpen
	lpEntry->sTable.sGeneric.ulObjectType = ulObjType;
	lpEntry->sTable.sGeneric.ulParentId = ulParent;
	AddTableEntry(std::move(lpEntry), lpulTableId);
	if (ulObjType == MAPI_ABCONT || ulObjType == MAPI_DISTLIST)
		lpTable->SetColumns(&sPropTagArrayABHierarchy, true);
	else
		lpTable->SetColumns(&sPropTagArrayABContents, true);
	return erSuccess;
}

ECRESULT ECTableManager::GetTable(unsigned int ulTableId, ECGenericObjectTable **lppTable)
{
	scoped_rlock lock(hListMutex);

	auto iterTables = mapTable.find(ulTableId);
	if (iterTables == mapTable.cend())
		return KCERR_NOT_FOUND;
	ECGenericObjectTable *lpTable = iterTables->second->lpTable;
	if (lpTable == NULL)
		return KCERR_NOT_FOUND;
	lpTable->AddRef();
	*lppTable = lpTable;
	return erSuccess;
}

ECRESULT ECTableManager::CloseTable(unsigned int ulTableId)
{
	ECRESULT er = erSuccess;
	ulock_rec lk(hListMutex);
	auto iterTables = mapTable.find(ulTableId);
	if (iterTables == mapTable.cend())
		return er;

	auto &lpEntry = iterTables->second;

	// Unsubscribe if needed
	switch (lpEntry->ulTableType) {
	case TABLE_ENTRY::TABLE_TYPE_GENERIC:
		lpSession->GetSessionManager()->UnsubscribeTableEvents(lpEntry->ulTableType,
			lpEntry->sTable.sGeneric.ulParentId, lpEntry->sTable.sGeneric.ulObjectType,
			lpEntry->sTable.sGeneric.ulObjectFlags, lpSession->GetSessionId());
		break;
	case TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE:
		lpSession->GetSessionManager()->UnsubscribeTableEvents(lpEntry->ulTableType,
			lpEntry->sTable.sOutgoingQueue.ulFlags & EC_SUBMIT_MASTER ? 0 : lpEntry->sTable.sOutgoingQueue.ulStoreId,
			MAPI_MESSAGE, lpEntry->sTable.sOutgoingQueue.ulFlags, lpSession->GetSessionId());
		break;
	default:
		break;
	}

	// Remember the table entry struct
	auto lpEntry2 = std::move(lpEntry);
	// Now, remove the table from the open table list
	mapTable.erase(ulTableId);

	// Unlock the table now as the search thread may not be able to exit without a hListMutex lock
	lk.unlock();

	// Free table data and threads running
	lpEntry2->lpTable->Release();
	return er;
}

ECRESULT ECTableManager::UpdateOutgoingTables(ECKeyTable::UpdateType ulType, unsigned ulStoreId, std::list<unsigned int> &lstObjId, unsigned int ulFlags, unsigned int ulObjType)
{
	ECRESULT er = erSuccess;
	sObjectTableKey	sRow;
	scoped_rlock lock(hListMutex);

	for (const auto &t : mapTable) {
		bool k = t.second->ulTableType == TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE &&
		         (t.second->sTable.sOutgoingQueue.ulStoreId == ulStoreId ||
		         t.second->sTable.sOutgoingQueue.ulStoreId == 0) &&
		         t.second->sTable.sOutgoingQueue.ulFlags == (ulFlags & EC_SUBMIT_MASTER);
		if (!k)
			continue;
		er = t.second->lpTable->UpdateRows(ulType, &lstObjId, OBJECTTABLE_NOTIFY, false);
		// ignore errors from the update
		er = erSuccess;
	}
	return er;
}

ECRESULT ECTableManager::UpdateTables(ECKeyTable::UpdateType ulType, unsigned int ulFlags, unsigned int ulObjId, std::list<unsigned int> &lstChildId, unsigned int ulObjType)
{
	ECRESULT er = erSuccess;
	sObjectTableKey	sRow;
	scoped_rlock lock(hListMutex);
	bool filter_private = false;
	std::string strInQuery;
	std::string strQuery;
	std::set<unsigned int> setObjIdPrivate;
	std::list<unsigned int> lstChildId2;
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult;
	DB_ROW                  lpDBRow = NULL;

	// This is called when a table has changed, so we have to see if the table in question is actually loaded by this table
	// manager, and then update the row if required.

	// Outlook may show the subject of sensitive messages (e.g. in
	// reminder popup), so filter these from shared store notifications

	if(lpSession->GetSecurity()->IsStoreOwner(ulObjId) == KCERR_NO_ACCESS &&
	    lpSession->GetSecurity()->GetAdminLevel() == 0 &&
	    lstChildId.size() != 0) {

		er = lpSession->GetDatabase(&lpDatabase);
		if (er != erSuccess)
			return er;

		for(auto it = lstChildId.begin(); it != lstChildId.end(); ++it) {
			if(it != lstChildId.begin())
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

		for(auto it = lstChildId.begin(); it != lstChildId.end(); ++it)
			if (setObjIdPrivate.find(*it) == setObjIdPrivate.end())
				lstChildId2.emplace_back(*it);
		filter_private = true;
	}

	// First, do all the actual contents tables and hierarchy tables
	for (const auto &t : mapTable) {
		bool k = t.second->ulTableType == TABLE_ENTRY::TABLE_TYPE_GENERIC &&
		         t.second->sTable.sGeneric.ulParentId == ulObjId &&
		         t.second->sTable.sGeneric.ulObjectFlags == ulFlags &&
		         t.second->sTable.sGeneric.ulObjectType == ulObjType;
		if (!k)
			continue;
		if(filter_private)
			er = t.second->lpTable->UpdateRows(ulType, &lstChildId2, OBJECTTABLE_NOTIFY, false);
		else
			er = t.second->lpTable->UpdateRows(ulType, &lstChildId, OBJECTTABLE_NOTIFY, false);
		// ignore errors from the update
		er = erSuccess;
	}
	return er;
}

/**
 * Get table statistics
 *
 * @param[out] lpulTables		The amount of open tables
 * @param[out] lpulObjectSize	The total memory usages of the tables
 *
 */
ECRESULT ECTableManager::GetStats(unsigned int *lpulTables, unsigned int *lpulObjectSize)
{
	scoped_rlock lock(hListMutex);

	unsigned int ulTables = mapTable.size();
	unsigned int ulSize = MEMORY_USAGE_MAP(ulTables, TABLEENTRYMAP);
	for (const auto &e : mapTable)
		if (e.second->ulTableType != TABLE_ENTRY::TABLE_TYPE_SYSTEMSTATS)
			/* Skip system stats since it would recursively include itself */
			ulSize += e.second->lpTable->GetObjectSize();

	*lpulTables = ulTables;
	*lpulObjectSize = ulSize;
	return erSuccess;
}

} /* namespace */
