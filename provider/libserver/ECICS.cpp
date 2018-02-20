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
#include <list>
#include <memory>
#include <utility>
#include <kopano/scope.hpp>
#include <kopano/tie.hpp>
#include "kcore.hpp"
#include <mapidefs.h>
#include <edkmdb.h>

#include <kopano/stringutil.h>
#include "ECSessionManager.h"
#include "ECSecurity.h"

#include "ics.h"
#include "cmdutil.hpp"
#include "ECStoreObjectTable.h"

#include <kopano/ECGuid.h>
#include "ECICS.h"
#include "ECICSHelpers.h"
#include "ECMAPI.h"
#include "soapH.h"
#include "SOAPUtils.h"

namespace KC {

extern ECSessionManager*	g_lpSessionManager;

struct ABChangeRecord {
	ULONG id;
	std::string strItem;
	std::string strParent;
	ULONG change_type;

	ABChangeRecord(ULONG id, const std::string &strItem, const std::string &strParent, ULONG change_type);
};

inline ABChangeRecord::ABChangeRecord(ULONG _id, const std::string &_strItem, const std::string &_strParent, ULONG _change_type)
: id(_id)
, strItem(_strItem)
, strParent(_strParent)
, change_type(_change_type)
{ }

typedef std::list<ABChangeRecord> ABChangeRecordList;

static bool isICSChange(unsigned int ulChange)
{
	switch(ulChange){
	case ICS_MESSAGE_CHANGE:
	case ICS_MESSAGE_FLAG:
	case ICS_MESSAGE_SOFT_DELETE:
	case ICS_MESSAGE_HARD_DELETE:
	case ICS_MESSAGE_NEW:
	case ICS_FOLDER_CHANGE:
	case ICS_FOLDER_SOFT_DELETE:
	case ICS_FOLDER_HARD_DELETE:
	case ICS_FOLDER_NEW:
		return true;
	default:
		return false;
	}
}

static ECRESULT FilterUserIdsByCompany(ECDatabase *lpDatabase, const std::set<unsigned int> &sUserIds, unsigned int ulCompanyFilter, std::set<unsigned int> *lpsFilteredIds)
{
	DB_RESULT lpDBResult;
	assert(!sUserIds.empty());

	std::string strQuery = "SELECT id FROM users where company=" + stringify(ulCompanyFilter) + " AND id IN (";
	for (const auto i : sUserIds)
		strQuery.append(stringify(i) + ",");
	strQuery.resize(strQuery.size() - 1);
	strQuery.append(1, ')');

	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	unsigned int ulRows = lpDBResult.get_num_rows();
	if (ulRows == 0) {
		lpsFilteredIds->clear();
		return erSuccess;
	}

	DB_ROW lpDBRow = NULL;
	std::set<unsigned int> sFilteredIds;
	for (unsigned int i = 0; i < ulRows; ++i) {
		lpDBRow = lpDBResult.fetch_row();
		if (lpDBRow == NULL || lpDBRow[0] == NULL) {
			ec_log_crit("%s:%d unexpected null pointer", __FUNCTION__, __LINE__);
			return KCERR_DATABASE_ERROR;
		}
		sFilteredIds.emplace(atoui(lpDBRow[0]));
	}
	*lpsFilteredIds = std::move(sFilteredIds);
	return erSuccess;
}

static ECRESULT ConvertABEntryIDToSoapSourceKey(struct soap *soap,
    ECSession *lpSession, bool bUseV1, unsigned int cbEntryId,
    char *lpEntryId, xsd__base64Binary *lpSourceKey)
{
	unsigned int		cbAbeid		= cbEntryId;
	auto lpAbeid = reinterpret_cast<ABEID *>(lpEntryId);
	entryId sEntryId;
	SOURCEKEY			sSourceKey;
	objectid_t			sExternId;

	if (cbEntryId < CbNewABEID("") || lpEntryId == NULL ||
	    lpSourceKey == NULL)
		return KCERR_INVALID_PARAMETER;

	if (lpAbeid->ulVersion == 1 && !bUseV1)
	{
		auto er = MAPITypeToType(lpAbeid->ulType, &sExternId.objclass);
		if (er != erSuccess)
			return er;
		er = ABIDToEntryID(soap, lpAbeid->ulId, sExternId, &sEntryId);	// Creates a V0 EntryID, because sExternId.id is empty
		if (er != erSuccess)
			return er;
		lpAbeid = reinterpret_cast<ABEID *>(sEntryId.__ptr);
		cbAbeid = sEntryId.__size;
	} 
	else if (lpAbeid->ulVersion == 0 && bUseV1) 
	{
		auto er = lpSession->GetUserManagement()->GetABSourceKeyV1(lpAbeid->ulId, &sSourceKey);
		if (er != erSuccess)
			return er;

		lpAbeid = reinterpret_cast<ABEID *>(static_cast<unsigned char *>(sSourceKey));
		cbAbeid = sSourceKey.size();
	}

	lpSourceKey->__size = cbAbeid;
	lpSourceKey->__ptr = (unsigned char*)s_memcpy(soap, (char*)lpAbeid, cbAbeid);
	return erSuccess;
}

static void AddChangeKeyToChangeList(std::string *strChangeList,
    ULONG cbChangeKey, const char *lpChangeKey)
{
	if(cbChangeKey <= sizeof(GUID) || cbChangeKey > 255)
		return;
	bool bFound = false;

	std::string strChangeKey{static_cast<char>(cbChangeKey)};
	strChangeKey.append(lpChangeKey, cbChangeKey);

	for (ULONG ulPos = 0; ulPos < strChangeList->size(); ) {
		ULONG ulSize = strChangeList->at(ulPos);
		if(ulSize <= sizeof(GUID) || ulSize == (ULONG)-1){
			break;
		}else if(memcmp(strChangeList->substr(ulPos+1, ulSize).c_str(), lpChangeKey, sizeof(GUID)) == 0){
			strChangeList->replace(ulPos, ulSize+1, strChangeKey);
			bFound = true;
		}
		ulPos += ulSize + 1;
	}
	if (!bFound)
		strChangeList->append(strChangeKey);
}

ECRESULT AddChange(BTSession *lpSession, unsigned int ulSyncId,
    const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey,
    unsigned int ulChange, unsigned int ulFlags, bool fForceNewChangeKey,
    std::string *lpstrChangeKey, std::string *lpstrChangeList)
{
	ECDatabase*		lpDatabase = NULL;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow = NULL;
	DB_LENGTHS		lpDBLen = NULL;
	unsigned int	changeid = 0;
	unsigned int	ulObjId = 0;

	char			szChangeKey[20];
	std::string		strChangeList;

	std::set<unsigned int>	syncids;

	if (!isICSChange(ulChange))
		return KCERR_INVALID_TYPE;
	if (sSourceKey == sParentSourceKey || sSourceKey.empty() || sParentSourceKey.empty())
		return KCERR_INVALID_PARAMETER;
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	// Always log folder changes
	if(ulChange & ICS_MESSAGE) {
		// See if anybody is interested in this change. If nobody has subscribed to this folder (ie nobody has got a state on this folder)
		// then we can ignore the change.

		std::string strQuery = "SELECT id FROM syncs "
			"WHERE sourcekey=" + lpDatabase->EscapeBinary(sParentSourceKey);
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			return er;

		while (true) {
			lpDBRow = lpDBResult.fetch_row();
			if (lpDBRow == NULL)
				break;
			unsigned int ulTmp = atoui((char*)lpDBRow[0]);
			if (ulTmp != ulSyncId)
				syncids.emplace(ulTmp);
		}
    }

	// Record the change
	std::string strQuery = "REPLACE INTO changes(change_type, sourcekey, parentsourcekey, sourcesync, flags) "
				"VALUES (" + stringify(ulChange) +
				  ", " + lpDatabase->EscapeBinary(sSourceKey) +
				  ", " + lpDatabase->EscapeBinary(sParentSourceKey) +
				  ", " + stringify(ulSyncId) +
				  ", " + stringify(ulFlags) +
				")";

	er = lpDatabase->DoInsert(strQuery, &changeid , NULL);
	if(er != erSuccess)
		return er;

	if ((ulChange & ICS_HARD_DELETE) == ICS_HARD_DELETE || (ulChange & ICS_SOFT_DELETE) == ICS_SOFT_DELETE) {
		if (ulSyncId != 0)
			er = RemoveFromLastSyncedMessagesSet(lpDatabase, ulSyncId, sSourceKey, sParentSourceKey);
		// The real item is removed from the database, So no further actions needed
		goto exit;
	}

	if (ulChange == ICS_MESSAGE_NEW && ulSyncId != 0) {
		er = AddToLastSyncedMessagesSet(lpDatabase, ulSyncId, sSourceKey, sParentSourceKey);
		if (er != erSuccess)
			return er;
	}

	// Add change key and predecessor change list
	er = g_lpSessionManager->GetCacheManager()->GetObjectFromProp(PROP_ID(PR_SOURCE_KEY), sSourceKey.size(), sSourceKey, &ulObjId);
	if(er == KCERR_NOT_FOUND){
		er = erSuccess; //hard deleted items can't be updated
		goto exit;
	}
	if(er != erSuccess)
		return er;
	strQuery = "SELECT val_binary, tag FROM properties "
				"WHERE tag IN (" + stringify(PROP_ID(PR_PREDECESSOR_CHANGE_LIST)) +
				" , " + stringify(PROP_ID(PR_CHANGE_KEY)) + " )" +
				" AND type IN ( " + stringify(PROP_TYPE(PR_PREDECESSOR_CHANGE_LIST)) +
				" , " + stringify(PROP_TYPE(PR_CHANGE_KEY)) + " )" +
				" AND hierarchyid = " + stringify(ulObjId);

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		lpDBLen = lpDBResult.fetch_row_lengths();
		if (lpDBRow[0] == nullptr || lpDBRow[1] == nullptr || lpDBLen == nullptr)
			continue;

		if (lpDBRow[1] == stringify(PROP_ID(PR_PREDECESSOR_CHANGE_LIST)) && lpDBLen[0] > 16)
			strChangeList.assign(lpDBRow[0], lpDBLen[0]);
		else if (lpDBRow[1] == stringify(PROP_ID(PR_CHANGE_KEY)) && lpDBLen[0] > 16)
			AddChangeKeyToChangeList(&strChangeList, lpDBLen[0], lpDBRow[0]);
	}

	/**
	 * There are two reasons for generating a new change key:
	 * 1. ulSyncId == 0. When this happens we have a modification initiated from the online server.
	 *    Since we got this far, there are other servers/devices monitoring this object, so a new
	 *    change key needs to be generated.
	 * 2. fForceNewChangeKey == true. When this happens the caller has determined that a new change key
	 *    needs to be generated. (We can't do this much earlier as we need the changeid, which we only
	 *    get once the change has been registed in the database.) At this time this will only happen when
	 *    no change key was sent by the client when calling ns__saveObject. This is the case when a change
	 *    occurs on the local server (actually making check 1 superfluous) or when a change is received
	 *    through z-push.
	 **/
	if(ulSyncId == 0 || fForceNewChangeKey == true)
	{
		struct propVal sProp;
		struct xsd__base64Binary sBin;
		struct sObjectTableKey key;
		
		// Add the new change key to the predecessor change list
		lpSession->GetServerGUID((GUID*)szChangeKey);
		memcpy(szChangeKey + sizeof(GUID), &changeid, 4);

		AddChangeKeyToChangeList(&strChangeList, sizeof(szChangeKey), szChangeKey);

		// Write PR_PREDECESSOR_CHANGE_LIST and PR_CHANGE_KEY
		sProp.ulPropTag = PR_PREDECESSOR_CHANGE_LIST;
		sProp.__union = SOAP_UNION_propValData_bin;
		sProp.Value.bin = &sBin;
		sProp.Value.bin->__ptr = (BYTE *)strChangeList.c_str();
		sProp.Value.bin->__size = strChangeList.size();
		
		er = WriteProp(lpDatabase, ulObjId, 0, &sProp);
		if(er != erSuccess)
			return er;
			
		key.ulObjId = ulObjId;
		key.ulOrderId = 0;
		auto cache = lpSession->GetSessionManager()->GetCacheManager();
		cache->SetCell(&key, PR_PREDECESSOR_CHANGE_LIST, &sProp);

		sProp.ulPropTag = PR_CHANGE_KEY;
		sProp.__union = SOAP_UNION_propValData_bin;
		sProp.Value.bin = &sBin;
		sProp.Value.bin->__ptr = (BYTE *)szChangeKey;
		sProp.Value.bin->__size = sizeof(szChangeKey);
		
		er = WriteProp(lpDatabase, ulObjId, 0, &sProp);
		if(er != erSuccess)
			return er;
		key.ulObjId = ulObjId;
		key.ulOrderId = 0;
		cache->SetCell(&key, PR_CHANGE_KEY, &sProp);
		if (lpstrChangeKey != nullptr)
			lpstrChangeKey->assign(szChangeKey, sizeof(szChangeKey));
		if (lpstrChangeList != nullptr)
			lpstrChangeList->assign(strChangeList);
	}

exit:
	// FIXME: We should not send notifications while we're in the middle of a transaction.
	if (er == erSuccess && !syncids.empty())
		g_lpSessionManager->NotificationChange(syncids, changeid, ulChange);
	return er;
}

void* CleanupSyncsTable(void* lpTmpMain){
	kcsrv_blocksigs();
	ECRESULT		er = erSuccess;
	std::string		strQuery;
	ECDatabase*		lpDatabase = NULL;
	ECSession*		lpSession = NULL;
	unsigned int	ulDeletedSyncs = 0;

	ec_log_info("Start syncs table clean up");

	auto ulSyncLifeTime = atoui(g_lpSessionManager->GetConfig()->GetSetting("sync_lifetime"));
	if(ulSyncLifeTime == 0)
		goto exit;

	er = g_lpSessionManager->CreateSessionInternal(&lpSession);
	if(er != erSuccess)
		goto exit;

	lpSession->Lock();

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;
	
	strQuery = "DELETE FROM syncs WHERE sync_time < DATE_SUB(FROM_UNIXTIME("+stringify(time(NULL))+"), INTERVAL " + stringify(ulSyncLifeTime) + " DAY)";
	er = lpDatabase->DoDelete(strQuery, &ulDeletedSyncs);
	if(er != erSuccess)
		goto exit;

exit:
	if(er == erSuccess)
		ec_log_info("syncs table clean up done: removed syncs: %d", ulDeletedSyncs);
	else
		ec_log_info("syncs table clean up failed: error 0x%08X, removed syncs: %d", er, ulDeletedSyncs);

	if(lpSession) {
		lpSession->Unlock(); // Lock the session
		g_lpSessionManager->RemoveSessionInternal(lpSession);
	}

	// ECScheduler does nothing with the returned value
	return NULL;
}

void *CleanupSyncedMessagesTable(void *lpTmpMain)
{
	kcsrv_blocksigs();
	std::string		strQuery;
	ECDatabase*		lpDatabase = NULL;
	ECSession*		lpSession = NULL;
	unsigned int ulDeleted = 0;

	ec_log_info("Start syncedmessages table clean up");
	auto er = g_lpSessionManager->CreateSessionInternal(&lpSession);
	if(er != erSuccess)
		goto exit;

	lpSession->Lock();

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;

    strQuery =  "DELETE syncedmessages.* FROM syncedmessages "
                 "LEFT JOIN syncs ON syncs.id = syncedmessages.sync_id " 	/* join with syncs */
                 "WHERE syncs.id IS NULL";									/* with no existing sync, and therefore not being tracked */

	er = lpDatabase->DoDelete(strQuery, &ulDeleted);
	if(er != erSuccess)
		goto exit;

exit:
	if(er == erSuccess)
		ec_log_info("syncedmessages table clean up done, %d entries removed", ulDeleted);
	else
		ec_log_info("syncedmessages table clean up failed, error: 0x%08X, %d entries removed", er, ulDeleted);

	if(lpSession) {
		lpSession->Unlock(); // Lock the session
		g_lpSessionManager->RemoveSessionInternal(lpSession);
	}

	// ECScheduler does nothing with the returned value
	return NULL;
}

ECRESULT GetChanges(struct soap *soap, ECSession *lpSession, SOURCEKEY sFolderSourceKey, unsigned int ulSyncId, unsigned int ulChangeId, unsigned int ulChangeType, unsigned int ulFlags, struct restrictTable *lpsRestrict, unsigned int *lpulMaxChangeId, icsChangesArray **lppChanges){
	class sfree_delete {
		public:
		void operator()(void *x) const { s_free(nullptr, x); }
	};
	unsigned int dummy;
	ECDatabase*		lpDatabase = NULL;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow;
	DB_LENGTHS		lpDBLen;
	unsigned int	ulMaxChange = 0;
	unsigned int	i = 0;
	unsigned int	ulChanges = 0;
	std::string		strQuery;
	unsigned int	ulFolderId = 0;
	icsChangesArray*lpChanges = NULL;
	bool			bAcceptABEID = false;
	unsigned int	cbSourceKeyData = 0;
	std::list<unsigned int> lstFolderIds, lstChanges;
	std::unique_ptr<ECGetContentChangesHelper> lpHelper;
	
	ec_log(EC_LOGLEVEL_ICS, "K-1200: sourcekey=%s, syncid=%d, changetype=%d, flags=%d", bin2hex(sFolderSourceKey).c_str(), ulSyncId, ulChangeType, ulFlags);
	auto gcache = g_lpSessionManager->GetCacheManager();

    // Get database object
	auto er = lpSession->GetDatabase(&lpDatabase);
    if (er != erSuccess)
		return er;
    er = lpDatabase->Begin();
    if (er != erSuccess)
		return er;
	auto cleanup = make_scope_success([&]() {
		if (er != erSuccess)
			lpDatabase->Rollback();
	});

    // CHeck if the client understands the new ABEID.
	if (lpSession->GetCapabilities() & KOPANO_CAP_MULTI_SERVER)
		bAcceptABEID = true;

	if(ulChangeType != ICS_SYNC_AB) {
        // You must first register your sync via setSyncStatus()
		if (ulSyncId == 0)
			return er = KCERR_INVALID_PARAMETER;

        // Add change key and predecessor change list
        if(ulChangeId > 0) {
            // Change ID > 0, which means we will be doing a real differential sync starting from the
            // given change ID.
            strQuery = 
				"SELECT change_id,sourcekey,sync_type "
				"FROM syncs "
				"WHERE id=" + stringify(ulSyncId) + " FOR UPDATE";

            er = lpDatabase->DoSelect(strQuery, &lpDBResult);
            if(er != erSuccess)
				return er;
			if (lpDBResult.get_num_rows() == 0)
				return er = KCERR_NOT_FOUND;
            lpDBRow = lpDBResult.fetch_row();
			if (lpDBRow == nullptr) {
				ec_log_err("K-1201: No row retrievable"); /* despite get_num_rows>0! */
				return er = KCERR_DATABASE_ERROR;
			}
            lpDBLen = lpDBResult.fetch_row_lengths();
			if (lpDBRow[0] == nullptr || lpDBRow[1] == nullptr || lpDBRow[2] == nullptr) {
				ec_log_err("K-1202: NULL values were returned from SQL");
				return er = KCERR_DATABASE_ERROR; /* this should never happen */
            }

            if((dummy = atoui(lpDBRow[2])) != ulChangeType){
				ec_log_err("K-1203: unexpected change type %u/%u", dummy, ulChangeType);
				return er = KCERR_COLLISION;
            }

		} else {

			strQuery = 
				"SELECT change_id,sourcekey "
				"FROM syncs "
				"WHERE id=" + stringify(ulSyncId) + " FOR UPDATE";
			er = lpDatabase->DoSelect(strQuery, &lpDBResult);
			if(er != erSuccess)
				return er;
			lpDBRow = lpDBResult.fetch_row();
			lpDBLen = lpDBResult.fetch_row_lengths();
			if (lpDBRow == nullptr) {
				std::string username;

				er = lpSession->GetSecurity()->GetUsername(&username);
				ec_log_warn("K-1204: The sync ID %u does not exist. Session user name: %s.",
					ulSyncId, username.c_str());
				return er = KCERR_DATABASE_ERROR;
			} else if (lpDBRow[0] == nullptr || lpDBRow[1] == nullptr) {
				std::string username;
				er = lpSession->GetSecurity()->GetUsername(&username);
				ec_log_warn("K-1205: Received NULL values from SQL for sync id %u. Session username: %s.",
					ulSyncId, username.c_str());
				return er = KCERR_DATABASE_ERROR;
			}
		}

		ulMaxChange = atoui(lpDBRow[0]);
		sFolderSourceKey = SOURCEKEY(lpDBLen[1], lpDBRow[1]);

        if(!sFolderSourceKey.empty()) {
			er = gcache->GetObjectFromProp(PROP_ID(PR_SOURCE_KEY), sFolderSourceKey.size(), sFolderSourceKey, &ulFolderId);
            if(er != erSuccess)
				return er;
        } else {
            ulFolderId = 0;
        }
        
        if(ulFolderId == 0) {
            if(lpSession->GetSecurity()->GetAdminLevel() != ADMIN_LEVEL_SYSADMIN)
                er = KCERR_NO_ACCESS;
        } else {
            if (ulChangeType == ICS_SYNC_CONTENTS)
                er = lpSession->GetSecurity()->CheckPermission(ulFolderId, ecSecurityRead);
            else if (ulChangeType == ICS_SYNC_HIERARCHY)
                er = lpSession->GetSecurity()->CheckPermission(ulFolderId, ecSecurityFolderVisible);
            else
                er = KCERR_INVALID_TYPE;
        }
        
        if(er != erSuccess)
			return er;
	}

	if(ulChangeType == ICS_SYNC_CONTENTS){
		er = ECGetContentChangesHelper::Create(soap, lpSession,
		     lpDatabase, sFolderSourceKey, ulSyncId, ulChangeId,
		     ulFlags, lpsRestrict, &unique_tie(lpHelper));
		if (er != erSuccess)
			return er;
		er = lpHelper->QueryDatabase(&lpDBResult);
		if (er != erSuccess)
			return er;

		std::vector<DB_ROW> db_rows;
		std::vector<DB_LENGTHS> db_lengths;

		static constexpr unsigned int ncols = 7;
		unsigned long col_lengths[1000*ncols];
		unsigned int length_counter = 0;

		while (lpDBResult && (lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			lpDBLen = lpDBResult.fetch_row_lengths();
			if (lpDBLen == NULL)
				continue;
			memcpy(&col_lengths[length_counter*ncols], lpDBLen, ncols * sizeof(*col_lengths));
			lpDBLen = &col_lengths[length_counter*ncols];
			++length_counter;

			if (lpDBRow[icsSourceKey] == NULL || lpDBRow[icsParentSourceKey] == NULL) {
				ec_log_err("K-1206: Received NULL values from SQL");
				return er = KCERR_DATABASE_ERROR;
			}
			db_rows.emplace_back(lpDBRow);
			db_lengths.emplace_back(lpDBLen);
			if (db_rows.size() == 1000) {
				er = lpHelper->ProcessRows(db_rows, db_lengths);
				if (er != erSuccess)
					return er;
				db_rows.clear();
				db_lengths.clear();
				length_counter = 0;
			}
		}

		if (!db_rows.empty()) {
			er = lpHelper->ProcessRows(db_rows, db_lengths);
			if (er != erSuccess)
				return er;
		}
		
		er = lpHelper->ProcessResidualMessages();
		if (er != erSuccess)
			return er;
		er = lpHelper->Finalize(&ulMaxChange, &lpChanges);
		if (er != erSuccess)
			return er;
		// Do stuff with lppChanges etc.

	}else if(ulChangeType == ICS_SYNC_HIERARCHY){

		// We traverse the tree by just looking at the current hierarchy. This means we will not traverse into deleted
		// folders and changes within those deleted folders will therefore never reach whoever is requesting changes. In
		// practice this shouldn't matter because the folder above will be deleted correctly.
		lstFolderIds.emplace_back(ulFolderId);

		// Recursive loop through all folders
		for (auto folder_id : lstFolderIds) {
			if (folder_id == 0) {
				if (lpSession->GetSecurity()->GetAdminLevel() != ADMIN_LEVEL_SYSADMIN)
					return er = KCERR_NO_ACCESS;
            } else if (lpSession->GetSecurity()->CheckPermission(folder_id, ecSecurityFolderVisible) != erSuccess) {
                // We cannot traverse folders that we have no permission to. This means that changes under the inaccessible folder
                // will disappear - this will cause the sync peer to go out-of-sync. Fix would be to remember changes in security
                // as deletes and adds.
				continue;
			}

			if(ulChangeId != 0){
				std::unique_ptr<unsigned char[], sfree_delete> lpSourceKeyData;
				if (folder_id != 0 && gcache->GetPropFromObject(PROP_ID(PR_SOURCE_KEY), folder_id, nullptr, &cbSourceKeyData, &unique_tie(lpSourceKeyData)) != erSuccess)
					continue; // Item is hard deleted?

				// Search folder changed folders
				strQuery = 	"SELECT changes.id "
							"FROM changes "
							"WHERE "
							"  changes.id > " + stringify(ulChangeId) + 									// Change ID is N or later
							"  AND changes.change_type >= " + stringify(ICS_FOLDER) +						// query optimizer
							"  AND changes.change_type & " + stringify(ICS_FOLDER) + " != 0" +				// Change is a folder change
							"  AND changes.sourcesync != " + stringify(ulSyncId);
							
                if (folder_id != 0)
					strQuery += "  AND parentsourcekey=" + lpDatabase->EscapeBinary(lpSourceKeyData.get(), cbSourceKeyData);

				er = lpDatabase->DoSelect(strQuery, &lpDBResult);
				if(er != erSuccess)
					return er;

				while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
					if (lpDBRow[0] == nullptr) {
						ec_log_err("K-1207: Received NULL values from SQL");
						return er = KCERR_DATABASE_ERROR; /* this should never happen */
					}
					lstChanges.emplace_back(atoui(lpDBRow[0]));
				}
			}

			if (folder_id != 0) {
                // Get subfolders for recursion
                strQuery = "SELECT id FROM hierarchy WHERE parent = " + stringify(folder_id) + " AND type = " + stringify(MAPI_FOLDER) + " AND flags = " + stringify(FOLDER_GENERIC);
                if (ulFlags & SYNC_NO_SOFT_DELETIONS)
                    strQuery += " AND hierarchy.flags & 1024 = 0";

                er = lpDatabase->DoSelect(strQuery, &lpDBResult);
                if(er != erSuccess)
					return er;

                while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
					if (lpDBRow[0] == nullptr) {
						ec_log_err("K-1208: Received NULL values from SQL");
						return er = KCERR_DATABASE_ERROR; /* this should never happen */
                    }
					lstFolderIds.emplace_back(atoui(lpDBRow[0]));
                }
            }
		}

		lstChanges.sort();
		lstChanges.unique();
		
		// We now have both a list of all folders, and and list of changes.

		if(ulChangeId == 0) {
		    if (ulFolderId == 0 && (ulFlags & SYNC_CATCHUP) == 0)
		        // Do not allow initial sync of all server folders
				return er = KCERR_NO_SUPPORT;
			// New sync, just return all the folders as changes
			lpChanges = (icsChangesArray *)soap_malloc(soap, sizeof(icsChangesArray));
			lpChanges->__ptr = (icsChange *)soap_malloc(soap, sizeof(icsChange) * lstFolderIds.size());

			// Search folder changed folders
			strQuery = "SELECT MAX(id) FROM changes";
			er = lpDatabase->DoSelect(strQuery, &lpDBResult);
			if(er != erSuccess)
				return er;
			lpDBRow = lpDBResult.fetch_row();
			if( lpDBRow == NULL){
				ec_log_err("K-1209: The \"changes\" table seems to be empty");
				return er = KCERR_DATABASE_ERROR; /* this should never happen */
			}
			ulMaxChange = (lpDBRow[0] == NULL ? 0 : atoui(lpDBRow[0]));

			i = 0;
			
			if((ulFlags & SYNC_CATCHUP) == 0) {
                for (auto folder_id : lstFolderIds) {
                    if (folder_id == ulFolderId)
                        continue; // don't send the folder itself as a change

                    strQuery = 	"SELECT sourcekey.val_binary, parentsourcekey.val_binary "
                                "FROM hierarchy "
                                "JOIN indexedproperties AS sourcekey ON hierarchy.id=sourcekey.hierarchyid AND sourcekey.tag=" + stringify(PROP_ID(PR_SOURCE_KEY)) + " "
                                "JOIN indexedproperties AS parentsourcekey ON hierarchy.parent=parentsourcekey.hierarchyid AND parentsourcekey.tag=" + stringify(PROP_ID(PR_SOURCE_KEY)) + " "
                                "WHERE hierarchy.id=" + stringify(folder_id) + " LIMIT 1";

                    er = lpDatabase->DoSelect(strQuery, &lpDBResult);
                    if(er != erSuccess)
						return er;
                    lpDBRow = lpDBResult.fetch_row();
                    lpDBLen = lpDBResult.fetch_row_lengths();
                    if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL)
                        continue;

                    lpChanges->__ptr[i].ulChangeId = ulMaxChange; // All items have the latest change ID because this is an initial sync

                    lpChanges->__ptr[i].sSourceKey.__ptr = (unsigned char *)soap_malloc(soap, lpDBLen[0]);
                    lpChanges->__ptr[i].sSourceKey.__size = lpDBLen[0];
                    memcpy(lpChanges->__ptr[i].sSourceKey.__ptr, lpDBRow[0], lpDBLen[0]);

                    lpChanges->__ptr[i].sParentSourceKey.__ptr = (unsigned char *)soap_malloc(soap, lpDBLen[1]);
                    lpChanges->__ptr[i].sParentSourceKey.__size = lpDBLen[1];
                    memcpy(lpChanges->__ptr[i].sParentSourceKey.__ptr, lpDBRow[1], lpDBLen[1]);

                    lpChanges->__ptr[i].ulChangeType = ICS_FOLDER_NEW;

                    lpChanges->__ptr[i].ulFlags = 0;
                    ++i;
                }
            }
			lpChanges->__size = i;
		} else {
			// Return all the found changes
			lpChanges = (icsChangesArray *)soap_malloc(soap, sizeof(icsChangesArray));
			lpChanges->__ptr = (icsChange *)soap_malloc(soap, sizeof(icsChange) * lstChanges.size());
			lpChanges->__size = lstChanges.size();

			i = 0;
			for (auto chg_id : lstChanges) {
				strQuery = "SELECT changes.id, changes.sourcekey, changes.parentsourcekey, changes.change_type, changes.flags FROM changes WHERE changes.id=" + stringify(chg_id) + " LIMIT 1";

				er = lpDatabase->DoSelect(strQuery, &lpDBResult);
				if(er != erSuccess)
					return er;
				lpDBRow = lpDBResult.fetch_row();
				lpDBLen = lpDBResult.fetch_row_lengths();
				if (lpDBRow == nullptr) {
					ec_log_err("K-1210: changes.id %d not found", chg_id);
					return er = KCERR_DATABASE_ERROR;
				} else if (lpDBRow[0] == nullptr || lpDBRow[1] == nullptr || lpDBRow[2] == nullptr || lpDBRow[3] == nullptr) {
					ec_log_err("K-1211: Received NULL values from SQL");
					return er = KCERR_DATABASE_ERROR;
				}

				lpChanges->__ptr[i].ulChangeId = atoui(lpDBRow[0]);
				if(lpChanges->__ptr[i].ulChangeId > ulMaxChange)
					ulMaxChange = lpChanges->__ptr[i].ulChangeId;

				lpChanges->__ptr[i].sSourceKey.__ptr = (unsigned char *)soap_malloc(soap, lpDBLen[1]);
				lpChanges->__ptr[i].sSourceKey.__size = lpDBLen[1];
				memcpy(lpChanges->__ptr[i].sSourceKey.__ptr, lpDBRow[1], lpDBLen[1]);

				lpChanges->__ptr[i].sParentSourceKey.__ptr = (unsigned char *)soap_malloc(soap, lpDBLen[2]);
				lpChanges->__ptr[i].sParentSourceKey.__size = lpDBLen[2];
				memcpy(lpChanges->__ptr[i].sParentSourceKey.__ptr, lpDBRow[2], lpDBLen[2]);

				lpChanges->__ptr[i].ulChangeType = atoui(lpDBRow[3]);

				if (lpDBRow[4] != nullptr)
					lpChanges->__ptr[i].ulFlags = atoui(lpDBRow[4]);
				else
					lpChanges->__ptr[i].ulFlags = 0;
				++i;
			}
			lpChanges->__size = i;
		}
	} else if(ulChangeType == ICS_SYNC_AB) {
		// filter on the current logged on company (0 when non-hosted server)
		// since we need to filter, we can't filter on "viewable
		// companies" because after updating the allowed viewable, we
		// won't see the changes as newly created anymore, and thus
		// still don't have that company in the offline gab.
		unsigned int ulCompanyId = 0;

		// returns 0 for KOPANO_UID_SYSTEM on hosted environments, and then the filter correctly doesn't filter anything.
		lpSession->GetSecurity()->GetUserCompany(&ulCompanyId);

	    if(ulChangeId > 0) {
			std::set<unsigned int> sUserIds;
			ABChangeRecordList lstChanges;

			// sourcekey is actually an entryid .. don't let the naming confuse you
            strQuery = "SELECT id, sourcekey, parentsourcekey, change_type FROM abchanges WHERE change_type & " + stringify(ICS_AB) + " AND id > " + stringify(ulChangeId) + " ORDER BY id";

            er = lpDatabase->DoSelect(strQuery, &lpDBResult, true);
            if(er != erSuccess)
				return er;

			while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
				lpDBLen = lpDBResult.fetch_row_lengths();
				if (lpDBRow[0] == nullptr || lpDBRow[1] == nullptr || lpDBRow[2] == nullptr || lpDBRow[3] == nullptr) {
					ec_log_err("K-1212: Received NULL values from SQL");
					return er = KCERR_DATABASE_ERROR; /* this should never happen */
                }

				if (lpDBLen[1] < CbNewABEID("")) {
					ec_log_err("K-1213: invalid size for ab entryid %lu", static_cast<unsigned long>(lpDBLen[1]));
					return er = KCERR_DATABASE_ERROR; /* this should never happen */
				}
				lstChanges.emplace_back(atoui(lpDBRow[0]), std::string(lpDBRow[1], lpDBLen[1]), std::string(lpDBRow[2], lpDBLen[2]), atoui(lpDBRow[3]));
				sUserIds.emplace(reinterpret_cast<ABEID *>(lpDBRow[1])->ulId);
			}

			if (!sUserIds.empty() && ulCompanyId != 0 && (lpSession->GetCapabilities() & KOPANO_CAP_MAX_ABCHANGEID)) {
				er = FilterUserIdsByCompany(lpDatabase, sUserIds, ulCompanyId, &sUserIds);
				if (er != erSuccess)
					return er;
			}

			// We'll reserve enough space for all the changes. We just might not use it all
            ulChanges = lstChanges.size();

            lpChanges = (icsChangesArray *)soap_malloc(soap, sizeof(icsChangesArray));
            lpChanges->__ptr = (icsChange *)soap_malloc(soap, sizeof(icsChange) * ulChanges);
            lpChanges->__size = 0;

            memset(lpChanges->__ptr, 0, sizeof(icsChange) * ulChanges);

            i=0;
			for (const auto &iter : lstChanges) {
				unsigned int ulUserId = reinterpret_cast<const ABEID *>(iter.strItem.data())->ulId;

				if (iter.change_type != ICS_AB_DELETE && sUserIds.find(ulUserId) == sUserIds.end())
					continue;

				er = ConvertABEntryIDToSoapSourceKey(soap, lpSession, bAcceptABEID, iter.strItem.size(), const_cast<char *>(iter.strItem.data()), &lpChanges->__ptr[i].sSourceKey);
                if (er != erSuccess) {
                    er = erSuccess;
                    continue;
                }

				lpChanges->__ptr[i].ulChangeId = iter.id;
				lpChanges->__ptr[i].sParentSourceKey.__ptr = reinterpret_cast<unsigned char *>(s_memcpy(soap, iter.strParent.data(), iter.strParent.size()));
				lpChanges->__ptr[i].sParentSourceKey.__size = iter.strParent.size();
				lpChanges->__ptr[i].ulChangeType = iter.change_type;

				if (iter.id > ulMaxChange)
					ulMaxChange = iter.id;
                ++i;
            }

            lpChanges->__size = i;
        } else {
            // During initial sync, just get all the current users from the database and output them as ICS_AB_NEW changes
            ABEID eid(MAPI_ABCONT, MUIDECSAB, 1);
            
            strQuery = "SELECT max(id) FROM abchanges";
            er = lpDatabase->DoSelect(strQuery, &lpDBResult);
            if (er != erSuccess)
				return er;
            lpDBRow = lpDBResult.fetch_row();
            if (lpDBRow == nullptr || lpDBRow[0] == nullptr)
                // You *should* always have something there if you have more than 0 users. However, just to make us compatible with
                // people truncating the table and then doing a resync, we'll go to change ID 0. This means that at the next sync,
                // we will do another full sync. We can't really fix that at the moment since going to change ID 1 would be even worse,
                // since it would skip the first change in the table. This could cause the problem that deletes after the first initial
                // sync and before the second would skip any deletes. This is acceptable for now since it is rare to do this, and it's
                // better than any other alternative.
                ulMaxChange = 0;
            else
                ulMaxChange = atoui(lpDBRow[0]);
            
			// Skip 'everyone', 'system' and users outside our company space, except when we're system
            strQuery = "SELECT id, objectclass, externid FROM users WHERE id not in (1,2)";
			if (lpSession->GetSecurity()->GetUserId() != KOPANO_UID_SYSTEM)
				strQuery += " AND company = " + stringify(ulCompanyId);
            
            er = lpDatabase->DoSelect(strQuery, &lpDBResult);
            if (er != erSuccess)
				return er;
            ulChanges = lpDBResult.get_num_rows();
            lpChanges = (icsChangesArray *)soap_malloc(soap, sizeof(icsChangesArray));
            lpChanges->__ptr = (icsChange *)soap_malloc(soap, sizeof(icsChange) * ulChanges);
            lpChanges->__size = 0;

            memset(lpChanges->__ptr, 0, sizeof(icsChange) * ulChanges);

            i=0;
                
			auto usrmgt = lpSession->GetUserManagement();
            while(1) {
                objectid_t id;
                unsigned int ulType;
                
                lpDBRow = lpDBResult.fetch_row();
                lpDBLen = lpDBResult.fetch_row_lengths();
                if(lpDBRow == NULL)
                    break;
                    
                if(lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
					ec_log_err("K-1214: Received NULL values from SQL");
					return er = KCERR_DATABASE_ERROR;
                }
                
                id.id.assign(lpDBRow[2], lpDBLen[2]);
                id.objclass = (objectclass_t)atoui(lpDBRow[1]);
                
                er = TypeToMAPIType((objectclass_t)atoui(lpDBRow[1]), (ULONG *)&ulType);
                if(er != erSuccess)
					return er;
                lpChanges->__ptr[i].ulChangeId = ulMaxChange;
				er = usrmgt->CreateABEntryID(soap,
                     bAcceptABEID ? 1 : 0, atoui(lpDBRow[0]), ulType, &id,
                     &lpChanges->__ptr[i].sSourceKey.__size,
                     reinterpret_cast<ABEID **>(&lpChanges->__ptr[i].sSourceKey.__ptr));
                if (er != erSuccess)
					return er;
                lpChanges->__ptr[i].sParentSourceKey.__size = sizeof(eid);
                lpChanges->__ptr[i].sParentSourceKey.__ptr = s_alloc<unsigned char>(soap, sizeof(eid));
                memcpy(lpChanges->__ptr[i].sParentSourceKey.__ptr, &eid, sizeof(eid));
                lpChanges->__ptr[i].ulChangeType = ICS_AB_NEW;
                ++i;
            }
            
            lpChanges->__size = i;
        }
	}

	er = lpDatabase->Commit();
	if (er != erSuccess)
		return er;
	*lpulMaxChangeId = ulMaxChange;
	*lppChanges = lpChanges;
	return er;
}

ECRESULT AddABChange(BTSession *lpSession, unsigned int ulChange, SOURCEKEY sSourceKey, SOURCEKEY sParentSourceKey)
{
	ECDatabase*		lpDatabase = NULL;

	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	// Add/Replace new change
	auto strQuery = "REPLACE INTO abchanges (sourcekey, parentsourcekey, change_type) VALUES(" + lpDatabase->EscapeBinary(sSourceKey) + "," + lpDatabase->EscapeBinary(sParentSourceKey) + "," + stringify(ulChange) + ")";
	return lpDatabase->DoInsert(strQuery);
}

ECRESULT GetSyncStates(struct soap *soap, ECSession *lpSession, mv_long ulaSyncId, syncStateArray *lpsaSyncState)
{
	ECDatabase*		lpDatabase = NULL;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow;

	if (ulaSyncId.__size == 0) {
		memset(lpsaSyncState, 0, sizeof *lpsaSyncState);
		return erSuccess;
	}
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	std::string strQuery = "SELECT id,change_id FROM syncs WHERE id IN (" + stringify(ulaSyncId.__ptr[0]);
	for (gsoap_size_t i = 1; i < ulaSyncId.__size; ++i)
		strQuery += "," + stringify(ulaSyncId.__ptr[i]);
	strQuery += ")";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;

	unsigned int ulResults = lpDBResult.get_num_rows();
    if (ulResults == 0){
		memset(lpsaSyncState, 0, sizeof *lpsaSyncState);
		return erSuccess;
    }

	lpsaSyncState->__size = 0;
	lpsaSyncState->__ptr = s_alloc<syncState>(soap, ulResults);

	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
			ec_log_crit("%s:%d unexpected null pointer", __FUNCTION__, __LINE__);
			return KCERR_DATABASE_ERROR;
		}

		lpsaSyncState->__ptr[lpsaSyncState->__size].ulSyncId = atoui(lpDBRow[0]);
		lpsaSyncState->__ptr[lpsaSyncState->__size++].ulChangeId = atoui(lpDBRow[1]);
	}
	assert(lpsaSyncState->__size == ulResults);
	return erSuccess;
}

ECRESULT AddToLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey)
{
	DB_RESULT lpDBResult;
	
	std::string strQuery = "SELECT MAX(change_id) FROM syncedmessages WHERE sync_id=" + stringify(ulSyncId);	
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL) {
		ec_log_crit("%s:%d unexpected null pointer", __FUNCTION__, __LINE__);
		return KCERR_DATABASE_ERROR;
	}
	
	if (lpDBRow[0] == NULL)	// none set for this sync id.
		return erSuccess;
	
	strQuery = "INSERT INTO syncedmessages (sync_id,change_id,sourcekey,parentsourcekey) VALUES (" +
				stringify(ulSyncId) + "," +
				lpDBRow[0] + "," + 
				lpDatabase->EscapeBinary(sSourceKey) + "," +
				lpDatabase->EscapeBinary(sParentSourceKey) + ")";
	return lpDatabase->DoInsert(strQuery);
}

/** 
 * Check if the object to remove is in the sync set. If it is outside,
 * we don't need to remove it at all.
 * 
 * @param lpDatabase Database
 * @param ulSyncId sync id, must be != 0
 * @param sSourceKey sourcekey of the object
 * 
 * @return Kopano error code
 */
ECRESULT CheckWithinLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey)
{
	DB_RESULT lpDBResult;

	std::string strQuery = "SELECT MAX(change_id) FROM syncedmessages WHERE sync_id=" + stringify(ulSyncId);	
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL) {
		ec_log_crit("%s:%d unexpected null pointer", __FUNCTION__, __LINE__);
		return KCERR_DATABASE_ERROR;
	}
	
	if (lpDBRow[0] == NULL)	// none set for this sync id, not an error
		return erSuccess;

	// check if the delete would remove the message
	strQuery = "SELECT 0 FROM syncedmessages WHERE sync_id=" + stringify(ulSyncId) +
				" AND change_id=" + lpDBRow[0] +
				" AND sourcekey=" + lpDatabase->EscapeBinary(sSourceKey) + " LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL)
		return KCERR_NOT_FOUND;
	return erSuccess;
}

ECRESULT RemoveFromLastSyncedMessagesSet(ECDatabase *lpDatabase, unsigned int ulSyncId, const SOURCEKEY &sSourceKey, const SOURCEKEY &sParentSourceKey)
{
	DB_RESULT lpDBResult;
	
	std::string strQuery = "SELECT MAX(change_id) FROM syncedmessages WHERE sync_id=" + stringify(ulSyncId);	
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL) {
		ec_log_crit("RemoveFromLastSyncedMessagesSet(): fetchrow return null");
		return KCERR_DATABASE_ERROR;
	}
	
	if (lpDBRow[0] == NULL)	// none set for this sync id.
		return erSuccess;
	
	strQuery = "DELETE FROM syncedmessages WHERE sync_id=" + stringify(ulSyncId) +
				" AND change_id=" + lpDBRow[0] +
				" AND sourcekey=" + lpDatabase->EscapeBinary(sSourceKey);
	return lpDatabase->DoDelete(strQuery);
}

} /* namespace */
