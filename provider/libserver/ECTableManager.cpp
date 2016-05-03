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

#include <sstream>

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
#include "ECConvenientDepthABObjectTable.h"
#include "ECTableManager.h"
#include "ECSessionManager.h"
#include "ECSession.h"
#include "ECDatabaseUtils.h"
#include "ECSecurity.h"
#include "ECSubRestriction.h"
#include <kopano/ECDefs.h>
#include "SOAPUtils.h"
#include <kopano/stringutil.h>
#include <kopano/Trace.h>
#include "ECServerEntrypoint.h"
#include "ECMailBoxTable.h"

#include <kopano/mapiext.h>
#include <edkmdb.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

void FreeRowSet(struct rowSet *lpRowSet, bool bBasePointerDel);

unsigned int sContentsProps[] = { PR_ENTRYID, PR_DISPLAY_NAME, PR_MESSAGE_FLAGS, PR_SUBJECT, PR_STORE_ENTRYID, PR_STORE_RECORD_KEY, PR_STORE_SUPPORT_MASK, PR_INSTANCE_KEY, PR_RECORD_KEY, PR_ACCESS, PR_ACCESS_LEVEL };
unsigned int sHierarchyProps[] = { PR_ENTRYID, PR_DISPLAY_NAME, PR_CONTENT_COUNT, PR_CONTENT_UNREAD, PR_STORE_ENTRYID, PR_STORE_RECORD_KEY, PR_STORE_SUPPORT_MASK, PR_INSTANCE_KEY, PR_RECORD_KEY, PR_ACCESS, PR_ACCESS_LEVEL };
unsigned int sABContentsProps[] = { PR_ENTRYID, PR_DISPLAY_NAME, PR_INSTANCE_KEY, PR_ADDRTYPE, PR_DISPLAY_TYPE, PR_DISPLAY_TYPE_EX, PR_EMAIL_ADDRESS, PR_SMTP_ADDRESS, PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY, PR_DEPARTMENT_NAME, PR_OFFICE_TELEPHONE_NUMBER, PR_OFFICE_LOCATION, PR_PRIMARY_FAX_NUMBER };
unsigned int sABHierarchyProps[] = { PR_ENTRYID, PR_DISPLAY_NAME, PR_INSTANCE_KEY, PR_DISPLAY_TYPE, PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY };

unsigned int sUserStoresProps[] = { PR_EC_USERNAME, PR_EC_STOREGUID, PR_EC_STORETYPE, PR_DISPLAY_NAME, PR_EC_COMPANYID, PR_EC_COMPANY_NAME, PR_STORE_ENTRYID, PR_LAST_MODIFICATION_TIME, PR_MESSAGE_SIZE_EXTENDED};

// stats tables
unsigned int sSystemStatsProps[] = { PR_DISPLAY_NAME, PR_EC_STATS_SYSTEM_DESCRIPTION, PR_EC_STATS_SYSTEM_VALUE };
unsigned int sSessionStatsProps[] = { PR_EC_STATS_SESSION_ID, PR_EC_STATS_SESSION_GROUP_ID, PR_EC_STATS_SESSION_IPADDRESS, PR_EC_STATS_SESSION_IDLETIME, PR_EC_STATS_SESSION_CAPABILITY, PR_EC_STATS_SESSION_LOCKED, PR_EC_USERNAME, PR_EC_STATS_SESSION_BUSYSTATES, PR_EC_STATS_SESSION_PROCSTATES, PR_EC_STATS_SESSION_CPU_USER, PR_EC_STATS_SESSION_CPU_SYSTEM, PR_EC_STATS_SESSION_CPU_REAL, PR_EC_STATS_SESSION_PEER_PID, PR_EC_STATS_SESSION_CLIENT_VERSION, PR_EC_STATS_SESSION_CLIENT_APPLICATION, PR_EC_STATS_SESSION_REQUESTS, PR_EC_STATS_SESSION_PORT, PR_EC_STATS_SESSION_PROXY, PR_EC_STATS_SESSION_URL, PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION, PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC };
unsigned int sUserStatsProps[] = { PR_EC_COMPANY_NAME, PR_EC_USERNAME, PR_DISPLAY_NAME, PR_SMTP_ADDRESS, PR_EC_NONACTIVE, PR_EC_ADMINISTRATOR, PR_EC_HOMESERVER_NAME,
								   PR_MESSAGE_SIZE_EXTENDED, PR_QUOTA_WARNING_THRESHOLD, PR_QUOTA_SEND_THRESHOLD, PR_QUOTA_RECEIVE_THRESHOLD, PR_EC_QUOTA_MAIL_TIME,
								   PR_EC_OUTOFOFFICE, PR_LAST_LOGON_TIME, PR_LAST_LOGOFF_TIME };
unsigned int sCompanyStatsProps[] = { PR_EC_COMPANY_NAME, PR_EC_COMPANY_ADMIN, PR_MESSAGE_SIZE_EXTENDED,
									  PR_QUOTA_WARNING_THRESHOLD, PR_QUOTA_SEND_THRESHOLD, PR_QUOTA_RECEIVE_THRESHOLD, PR_EC_QUOTA_MAIL_TIME };
unsigned int sServerStatsProps[] = { PR_EC_STATS_SERVER_NAME, PR_EC_STATS_SERVER_HOST, PR_EC_STATS_SERVER_HTTPPORT, PR_EC_STATS_SERVER_SSLPORT, PR_EC_STATS_SERVER_PROXYURL, PR_EC_STATS_SERVER_HTTPURL,
									 PR_EC_STATS_SERVER_HTTPSURL, PR_EC_STATS_SERVER_FILEURL };

struct propTagArray sPropTagArrayContents = { (unsigned int *)&sContentsProps, ARRAY_SIZE(sContentsProps)};
struct propTagArray sPropTagArrayHierarchy = { (unsigned int *)&sHierarchyProps, ARRAY_SIZE(sHierarchyProps)};
struct propTagArray sPropTagArrayABContents = { (unsigned int *)&sABContentsProps, ARRAY_SIZE(sABContentsProps)};
struct propTagArray sPropTagArrayABHierarchy = { (unsigned int *)&sABHierarchyProps, ARRAY_SIZE(sABHierarchyProps)};
struct propTagArray sPropTagArrayUserStores = { (unsigned int *)&sUserStoresProps, ARRAY_SIZE(sUserStoresProps)};

struct propTagArray sPropTagArraySystemStats = { (unsigned int *)&sSystemStatsProps, ARRAY_SIZE(sSystemStatsProps)};
struct propTagArray sPropTagArraySessionStats = { (unsigned int *)&sSessionStatsProps, ARRAY_SIZE(sSessionStatsProps)};
struct propTagArray sPropTagArrayUserStats = { (unsigned int *)&sUserStatsProps, ARRAY_SIZE(sUserStatsProps)};
struct propTagArray sPropTagArrayCompanyStats = { (unsigned int *)&sCompanyStatsProps, ARRAY_SIZE(sCompanyStatsProps)};
struct propTagArray sPropTagArrayServerStats = { (unsigned int *)&sServerStatsProps, ARRAY_SIZE(sServerStatsProps)};

ECTableManager::ECTableManager(ECSession *lpSession)
{
	this->lpSession = lpSession;
	this->ulNextTableId = 1;
	pthread_mutexattr_t mattr;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	// make recursive lock, since destructor uses this->CloseTable()
	pthread_mutex_init(&this->hListMutex, &mattr);
}

ECTableManager::~ECTableManager()
{
	std::map<unsigned int, TABLE_ENTRY *>::const_iterator iterTables;
	std::map<unsigned int, TABLE_ENTRY *>::const_iterator iterNext;

	pthread_mutex_lock(&hListMutex);

	iterTables = mapTable.begin();
	// Clean up tables, if CloseTable(..) isn't called 
	while(iterTables != mapTable.end()) {
		iterNext = iterTables;
		++iterNext;
		CloseTable(iterTables->first);
		iterTables = iterNext;
	}

	pthread_mutex_unlock(&hListMutex);
	pthread_mutex_destroy(&hListMutex);

	mapTable.clear();

}

void ECTableManager::AddTableEntry(TABLE_ENTRY *lpEntry, unsigned int *lpulTableId)
{
	pthread_mutex_lock(&hListMutex);

	mapTable[ulNextTableId] = lpEntry;

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
	pthread_mutex_unlock(&hListMutex);
	
}


ECRESULT ECTableManager::OpenOutgoingQueueTable(unsigned int ulStoreId, unsigned int *lpulTableId)
{
	ECRESULT er = erSuccess;
	ECStoreObjectTable *lpTable = NULL;
	TABLE_ENTRY	*lpEntry;
	DB_RESULT	lpDBResult = NULL;
	DB_ROW		lpDBRow = NULL;
	std::string strQuery;
	struct propTagArray *lpsPropTags = NULL;
	GUID sGuid;
	sObjectTableKey sRowItem;
	const ECLocale locale = lpSession->GetSessionManager()->GetSortLocale(ulStoreId);
	ECDatabase *lpDatabase = NULL;

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	if(ulStoreId) {
		er = lpSession->GetSessionManager()->GetCacheManager()->GetStore(ulStoreId, &ulStoreId, &sGuid);

		if(er != erSuccess)
			goto exit;

		er = ECStoreObjectTable::Create(lpSession, ulStoreId, &sGuid, 0, MAPI_MESSAGE, 0, 0, locale, &lpTable);

	} else {
		// FIXME check permissions for master outgoing table
		// Master outgoing table has different STORE_ENTRYID and GUID per row
		er = ECStoreObjectTable::Create(lpSession, 0, NULL, 0, MAPI_MESSAGE, 0, 0, locale, &lpTable);
	}
	if(er != erSuccess)
		goto exit;

	// Select outgoingqueue
	strQuery = "SELECT o.hierarchy_id, i.hierarchyid FROM outgoingqueue AS o LEFT JOIN indexedproperties AS i ON i.hierarchyid=o.hierarchy_id and i.tag=0xFFF";
	
	if(ulStoreId)
		strQuery += " WHERE o.flags & 1 =" + stringify(EC_SUBMIT_LOCAL) + " AND o.store_id=" + stringify(ulStoreId);
	else
		strQuery += " WHERE o.flags & 1 =" + stringify(EC_SUBMIT_MASTER);

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	while( (lpDBRow = lpDatabase->FetchRow(lpDBResult)) != NULL) {
		if(lpDBRow[0] == NULL)
			continue;

		// Item found without entryid, item already deleted, so delete the item from the queue
		if (lpDBRow[1] == NULL) {
			ec_log_err("Removing stray object \"%s\" from outgoing table", lpDBRow[0]);
			strQuery = "DELETE FROM outgoingqueue WHERE hierarchy_id=" + string(lpDBRow[0]);
			
			lpDatabase->DoDelete(strQuery); //ignore errors
			continue;
		}
		
		lpTable->UpdateRow(ECKeyTable::TABLE_ROW_ADD, atoi(lpDBRow[0]), 0);
	}

	lpEntry = new TABLE_ENTRY;
	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE;
	lpEntry->sTable.sOutgoingQueue.ulStoreId = ulStoreId;
	lpEntry->sTable.sOutgoingQueue.ulFlags = ulStoreId ? EC_SUBMIT_LOCAL : EC_SUBMIT_MASTER;

	AddTableEntry(lpEntry, lpulTableId);

	if(lpTable->GetColumns(NULL, TBL_ALL_COLUMNS, &lpsPropTags) == erSuccess) {
		lpTable->SetColumns(lpsPropTags, false);
	}

	lpTable->SeekRow(BOOKMARK_BEGINNING, 0, NULL);

exit:
	if(lpTable)
		lpTable->Release();

	if(lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	if(lpsPropTags)
		FreePropTagArray(lpsPropTags);

	return er;
}

ECRESULT ECTableManager::OpenUserStoresTable(unsigned int ulFlags, unsigned int *lpulTableId)
{
	ECRESULT er = erSuccess;
	ECUserStoreTable *lpTable = NULL;
	TABLE_ENTRY	*lpEntry = NULL;
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");

	er = ECUserStoreTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), &lpTable);
	if (er != erSuccess)
		goto exit;

	lpEntry = new TABLE_ENTRY;

	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_USERSTORES;
	memset(&lpEntry->sTable, 0, sizeof(TABLE_ENTRY::__sTable));

	er = lpTable->SetColumns(&sPropTagArrayUserStores, true);
	if (er != erSuccess)
		goto exit;

	AddTableEntry(lpEntry, lpulTableId);

exit:
	if (lpTable)
		lpTable->Release();

	if (er != erSuccess)
		delete lpEntry;

	return er;
}

ECRESULT ECTableManager::OpenMultiStoreTable(unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId)
{
	ECRESULT er = erSuccess;
	ECMultiStoreTable *lpTable = NULL;
	TABLE_ENTRY	*lpEntry = NULL;
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");

	// Open an empty table. Contents will be provided by client in a later call.
	er = ECMultiStoreTable::Create(lpSession, ulObjType, ulFlags, createLocaleFromName(lpszLocaleId), &lpTable);
	if (er != erSuccess)
		goto exit;

	lpEntry = new TABLE_ENTRY;

	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_MULTISTORE;
	memset(&lpEntry->sTable, 0, sizeof(TABLE_ENTRY::__sTable));

	AddTableEntry(lpEntry, lpulTableId);

exit:
	if (lpTable)
		lpTable->Release();

	return er;
}

ECRESULT ECTableManager::OpenGenericTable(unsigned int ulParent, unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId, bool fLoad)
{
	ECRESULT		er = erSuccess;
	std::string		strQuery;
	ECStoreObjectTable	*lpTable = NULL;
	TABLE_ENTRY		*lpEntry = NULL;

	unsigned int	ulStoreId = 0;
	GUID			sGuid;
	ECLocale			locale;
	ECDatabase *lpDatabase = NULL;

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	er = lpSession->GetSessionManager()->GetCacheManager()->GetStore(ulParent, &ulStoreId, &sGuid);
	if(er != erSuccess)
		goto exit;

	locale = lpSession->GetSessionManager()->GetSortLocale(ulStoreId);
	if(lpSession->GetSessionManager()->GetSearchFolders()->IsSearchFolder(ulStoreId, ulParent) == erSuccess) {
	    if((ulFlags & MSGFLAG_DELETED) | (ulFlags & MAPI_ASSOCIATED)) {
	        er = KCERR_NO_SUPPORT;
	        goto exit;
        }
	    if(lpSession->GetSecurity()->IsStoreOwner(ulParent) != erSuccess && lpSession->GetSecurity()->IsAdminOverOwnerOfObject(ulParent) != erSuccess) {
	        // Search folders are not visible at all to other users, therefore NOT_FOUND, not NO_ACCESS
            er = KCERR_NOT_FOUND; 
            goto exit;
	    } else {
			er = ECSearchObjectTable::Create(lpSession, ulStoreId, &sGuid, ulParent, ulObjType, ulFlags, locale, (ECSearchObjectTable**)&lpTable);
        }
	} else if(ulObjType == MAPI_FOLDER && (ulFlags & CONVENIENT_DEPTH))
		er = ECConvenientDepthObjectTable::Create(lpSession, ulStoreId, &sGuid, ulParent, ulObjType, ulFlags, locale, (ECConvenientDepthObjectTable**)&lpTable);
	else
		er = ECStoreObjectTable::Create(lpSession, ulStoreId, &sGuid, ulParent, ulObjType, ulFlags, 0, locale, &lpTable);

	if (er != erSuccess)
		goto exit;

	lpEntry = new TABLE_ENTRY;
	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_GENERIC;
	lpEntry->sTable.sGeneric.ulObjectFlags = ulFlags & (MAPI_ASSOCIATED | MSGFLAG_DELETED); // MSGFLAG_DELETED because of conversion in ns__tableOpen
	lpEntry->sTable.sGeneric.ulObjectType = ulObjType;
	lpEntry->sTable.sGeneric.ulParentId = ulParent;

	// First, add table to internal list of tables. This means we can already start
	// receiving notifications on this table
	AddTableEntry(lpEntry, lpulTableId);

	// Load a default column set
	if(ulObjType == MAPI_MESSAGE) {
		lpTable->SetColumns(&sPropTagArrayContents, true);
	} else {
		lpTable->SetColumns(&sPropTagArrayHierarchy, true);
	}

exit:
	if (lpTable)
		lpTable->Release();

	return er;
}

static void AuditStatsAccess(ECSession *lpSession, const char *access, const char *table)
{
	if (lpSession->GetSessionManager()->GetAudit()) {
		std::string strUsername;
		std::string strImpersonator;
		
		lpSession->GetSecurity()->GetUsername(&strUsername);
		if (lpSession->GetSecurity()->GetImpersonator(&strImpersonator) == erSuccess) {
			ZLOG_AUDIT(lpSession->GetSessionManager()->GetAudit(), "access %s table='%s stats' username=%s impersonator=%s", access, table, strUsername.c_str(), strImpersonator.c_str());
		} else {
			ZLOG_AUDIT(lpSession->GetSessionManager()->GetAudit(), "access %s table='%s stats' username=%s", access, table, strUsername.c_str());
		}
	}
}

ECRESULT ECTableManager::OpenStatsTable(unsigned int ulTableType, unsigned int ulFlags, unsigned int *lpulTableId)
{
	ECRESULT er = erSuccess;
	ECGenericObjectTable *lpTable = NULL;
	TABLE_ENTRY	*lpEntry = NULL;
	int adminlevel = lpSession->GetSecurity()->GetAdminLevel();
	bool hosted = lpSession->GetSessionManager()->IsHostedSupported();
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");
	
	// TABLETYPE_STATS_SYSTEM: only for (sys)admins
	// TABLETYPE_STATS_SESSIONS: only for (sys)admins
	// TABLETYPE_STATS_USERS: full list: only for (sys)admins, company list: only for admins

	lpEntry = new TABLE_ENTRY;

	switch (ulTableType) {
	case TABLETYPE_STATS_SYSTEM:
		if ((hosted && adminlevel < ADMIN_LEVEL_SYSADMIN) || (!hosted && adminlevel < ADMIN_LEVEL_ADMIN)) {
			AuditStatsAccess(lpSession, "denied", "system");
			er = KCERR_NO_ACCESS;
			goto exit;
		}
		er = ECSystemStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), (ECSystemStatsTable**)&lpTable);
		if (er != erSuccess)
			goto exit;

		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_SYSTEMSTATS;
		er = lpTable->SetColumns(&sPropTagArraySystemStats, true);
		break;
	case TABLETYPE_STATS_SESSIONS:
		if ((hosted && adminlevel < ADMIN_LEVEL_SYSADMIN) || (!hosted && adminlevel < ADMIN_LEVEL_ADMIN)) {
			AuditStatsAccess(lpSession, "denied", "session");
			er = KCERR_NO_ACCESS;
			goto exit;
		}
		er = ECSessionStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), (ECSessionStatsTable**)&lpTable);
		if (er != erSuccess)
			goto exit;

		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_SESSIONSTATS;
		er = lpTable->SetColumns(&sPropTagArraySessionStats, true);
		break;
	case TABLETYPE_STATS_USERS:
		if (adminlevel < ADMIN_LEVEL_ADMIN) {
			AuditStatsAccess(lpSession, "denied", "user");
			er = KCERR_NO_ACCESS;
			goto exit;
		}
		er = ECUserStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), (ECUserStatsTable**)&lpTable);
		if (er != erSuccess)
			goto exit;

		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_USERSTATS;
		er = lpTable->SetColumns(&sPropTagArrayUserStats, true);
		break;
	case TABLETYPE_STATS_COMPANY:
		if (!hosted) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		if (adminlevel < ADMIN_LEVEL_SYSADMIN) {
			AuditStatsAccess(lpSession, "denied", "company");
			er = KCERR_NO_ACCESS;
			goto exit;
		}
		er = ECCompanyStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), (ECCompanyStatsTable**)&lpTable);
		if (er != erSuccess)
			goto exit;

		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_COMPANYSTATS;
		er = lpTable->SetColumns(&sPropTagArrayCompanyStats, true);
		break;
	case TABLETYPE_STATS_SERVERS:
		if (adminlevel < ADMIN_LEVEL_SYSADMIN) {
			AuditStatsAccess(lpSession, "denied", "company");
			er = KCERR_NO_ACCESS;
			goto exit;
		}
		er = ECServerStatsTable::Create(lpSession, ulFlags, createLocaleFromName(lpszLocaleId), (ECServerStatsTable**)&lpTable);
		if (er != erSuccess)
			goto exit;

		lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_SERVERSTATS;
		er = lpTable->SetColumns(&sPropTagArrayServerStats, true);
		break;
	default:
		er = KCERR_UNKNOWN;
		break;
	}
	if (er != erSuccess)
		goto exit;

	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	memset(&lpEntry->sTable, 0, sizeof(TABLE_ENTRY::__sTable));

	AddTableEntry(lpEntry, lpulTableId);

exit:
	if (er != erSuccess)
		delete lpEntry;

	if (lpTable)
		lpTable->Release();

	return er;
}

ECRESULT ECTableManager::OpenMailBoxTable(unsigned int ulflags, unsigned int *lpulTableId)
{
	ECRESULT er = erSuccess;
	ECMailBoxTable *lpTable = NULL;
	TABLE_ENTRY	*lpEntry = NULL;
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");

	er = ECMailBoxTable::Create(lpSession, ulflags, createLocaleFromName(lpszLocaleId), &lpTable);
	if (er != erSuccess)
		goto exit;

	lpEntry = new TABLE_ENTRY;

	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_MAILBOX;
	memset(&lpEntry->sTable, 0, sizeof(TABLE_ENTRY::__sTable));

	//@todo check this list!!!
	er = lpTable->SetColumns(&sPropTagArrayUserStores, true);
	if (er != erSuccess)
		goto exit;

	AddTableEntry(lpEntry, lpulTableId);

exit:
	if (lpTable)
		lpTable->Release();

	if (er != erSuccess)
		delete lpEntry;

	return er;
}

/*
	Open an addressbook table
*/
ECRESULT ECTableManager::OpenABTable(unsigned int ulParent, unsigned int ulParentType, unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulTableId)
{
	ECRESULT er = erSuccess;
	ECABObjectTable	*lpTable = NULL;
	TABLE_ENTRY *lpEntry = NULL;
	const char *lpszLocaleId = lpSession->GetSessionManager()->GetConfig()->GetSetting("default_sort_locale_id");

	// Open first container
	if (ulFlags & CONVENIENT_DEPTH)
		er = ECConvenientDepthABObjectTable::Create(lpSession, 1, ulObjType, ulParent, ulParentType, ulFlags, createLocaleFromName(lpszLocaleId), (ECConvenientDepthABObjectTable**)&lpTable);
	else
		er = ECABObjectTable::Create(lpSession, 1, ulObjType, ulParent, ulParentType, ulFlags, createLocaleFromName(lpszLocaleId), &lpTable);

	if (er != erSuccess)
		goto exit;

	lpEntry = new TABLE_ENTRY;

	// Add the open table to the list of current tables
	lpEntry->lpTable = lpTable;
	lpEntry->ulTableType = TABLE_ENTRY::TABLE_TYPE_GENERIC;
	lpEntry->sTable.sGeneric.ulObjectFlags = ulFlags & (MAPI_ASSOCIATED | MSGFLAG_DELETED); // MSGFLAG_DELETED because of conversion in ns__tableOpen
	lpEntry->sTable.sGeneric.ulObjectType = ulObjType;
	lpEntry->sTable.sGeneric.ulParentId = ulParent;

	AddTableEntry(lpEntry, lpulTableId);

	if (ulObjType == MAPI_ABCONT || ulObjType == MAPI_DISTLIST)
		lpTable->SetColumns(&sPropTagArrayABHierarchy, true);
	else
		lpTable->SetColumns(&sPropTagArrayABContents, true);

exit:
	if (lpTable)
		lpTable->Release();

	return er;
}

ECRESULT ECTableManager::GetTable(unsigned int ulTableId, ECGenericObjectTable **lppTable)
{
	ECRESULT		er = erSuccess;
	ECGenericObjectTable	*lpTable = NULL;
	std::map<unsigned int, TABLE_ENTRY *>::const_iterator iterTables;

	pthread_mutex_lock(&hListMutex);

	iterTables = mapTable.find(ulTableId);
	if(iterTables == mapTable.end()) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	lpTable = iterTables->second->lpTable;

	if(lpTable == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	lpTable->AddRef();
	*lppTable = lpTable;

exit:
	pthread_mutex_unlock(&hListMutex);

	return er;
}

ECRESULT ECTableManager::CloseTable(unsigned int ulTableId)
{
	ECRESULT er = erSuccess;
	std::map<unsigned int, TABLE_ENTRY *>::const_iterator iterTables;
	TABLE_ENTRY *lpEntry = NULL;

	pthread_mutex_lock(&hListMutex);
	iterTables = mapTable.find(ulTableId);

	if(iterTables != mapTable.end())
	{
		// Remember the table entry struct
		lpEntry = iterTables->second;
		
        // Unsubscribe if needed		
        switch(lpEntry->ulTableType) {
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
		
		// Now, remove the table from the open table list
		mapTable.erase(ulTableId);
    
		// Unlock the table now as the search thread may not be able to exit without a hListMutex lock
		pthread_mutex_unlock(&hListMutex);

		// Free table data and threads running
		lpEntry->lpTable->Release();
		delete lpEntry;
	} else {
		pthread_mutex_unlock(&hListMutex);
	}
	
	return er;
}

ECRESULT ECTableManager::UpdateOutgoingTables(ECKeyTable::UpdateType ulType, unsigned ulStoreId, std::list<unsigned int> &lstObjId, unsigned int ulFlags, unsigned int ulObjType)
{
	ECRESULT er = erSuccess;
	std::map<unsigned int, TABLE_ENTRY *>::const_iterator iterTables;
	std::map<unsigned int, unsigned int>::const_iterator iterFolders;

	sObjectTableKey	sRow;

	pthread_mutex_lock(&hListMutex);

	for (iterTables = mapTable.begin(); iterTables != mapTable.end(); ++iterTables) {
		if(	iterTables->second->ulTableType == TABLE_ENTRY::TABLE_TYPE_OUTGOINGQUEUE &&
			(iterTables->second->sTable.sOutgoingQueue.ulStoreId == ulStoreId ||
			 iterTables->second->sTable.sOutgoingQueue.ulStoreId == 0) &&
			 iterTables->second->sTable.sOutgoingQueue.ulFlags == (ulFlags & EC_SUBMIT_MASTER)) {

			er = iterTables->second->lpTable->UpdateRows(ulType, &lstObjId, OBJECTTABLE_NOTIFY, false);

			// ignore errors from the update
			er = erSuccess;

		}
	}

	pthread_mutex_unlock(&hListMutex);
	return er;
}

ECRESULT ECTableManager::UpdateTables(ECKeyTable::UpdateType ulType, unsigned int ulFlags, unsigned int ulObjId, std::list<unsigned int> &lstChildId, unsigned int ulObjType)
{
	ECRESULT er = erSuccess;
	std::map<unsigned int, TABLE_ENTRY *>::const_iterator iterTables;
	sObjectTableKey	sRow;

	pthread_mutex_lock(&hListMutex);

	// This is called when a table has changed, so we have to see if the table in question is actually loaded by this table
	// manager, and then update the row if required.

	// First, do all the actual contents tables and hierarchy tables
	for (iterTables = mapTable.begin(); iterTables != mapTable.end(); ++iterTables) {
		if(	iterTables->second->ulTableType == TABLE_ENTRY::TABLE_TYPE_GENERIC &&
			iterTables->second->sTable.sGeneric.ulParentId == ulObjId && 
			iterTables->second->sTable.sGeneric.ulObjectFlags == ulFlags &&
			iterTables->second->sTable.sGeneric.ulObjectType == ulObjType) {

			er = iterTables->second->lpTable->UpdateRows(ulType, &lstChildId, OBJECTTABLE_NOTIFY, false);

			// ignore errors from the update
			er = erSuccess;

		}
	}

	pthread_mutex_unlock(&hListMutex);
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
	TABLEENTRYMAP::const_iterator iterEntry;
	unsigned int ulSize = 0;
	unsigned int ulTables = 0; 
	
	pthread_mutex_lock(&hListMutex);

	ulTables = mapTable.size();
	ulSize = MEMORY_USAGE_MAP(ulTables, TABLEENTRYMAP);

	for (iterEntry = mapTable.begin(); iterEntry !=  mapTable.end(); ++iterEntry)
		if(iterEntry->second->ulTableType != TABLE_ENTRY::TABLE_TYPE_SYSTEMSTATS) // Skip system stats since it would recursively include itself
			ulSize += iterEntry->second->lpTable->GetObjectSize();

	pthread_mutex_unlock(&hListMutex);

	*lpulTables = ulTables;
	*lpulObjectSize = ulSize;

	return erSuccess;
}
