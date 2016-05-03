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

#include <mapidefs.h>
#include <mapitags.h>

#include "ECMAPI.h"
#include "ECSession.h"
#include <kopano/ECKeyTable.h>
#include <kopano/ECLogger.h>
#include "ECStoreObjectTable.h"
#include "ECSubRestriction.h"
#include "ECSearchFolders.h"
#include "ECSessionManager.h"
#include "ECStatsCollector.h"
#include "ECIndexer.h"
#include <kopano/ECTags.h>
#include "cmdutil.hpp"
#include <kopano/stringutil.h>

#include "ECSearchClient.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

typedef struct {
    SEARCHFOLDER 	*lpFolder;
    ECSearchFolders *lpSearchFolders;
} THREADINFO;

ECSearchFolders::ECSearchFolders(ECSessionManager *lpSessionManager,
    ECDatabaseFactory *lpFactory)
{
    this->m_lpSessionManager = lpSessionManager;
    this->m_lpDatabaseFactory = lpFactory;
    this->m_bExitThread = false;
    this->m_bRunning = false;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&m_mutexMapSearchFolders, &attr);
    pthread_cond_init(&m_condThreadExited, NULL);
    
    pthread_mutex_init(&m_mutexEvents, &attr);
    pthread_cond_init(&m_condEvents, NULL);
    
    pthread_create(&m_threadProcess, NULL, ECSearchFolders::ProcessThread, (void *)this);
    set_thread_name(m_threadProcess, "SearchFolders");
}

ECSearchFolders::~ECSearchFolders() {
    STOREFOLDERIDSEARCH::iterator iterStore;
    FOLDERIDSEARCH::iterator iterFolder;

	pthread_mutex_lock(&m_mutexMapSearchFolders);

	for (iterStore = m_mapSearchFolders.begin();
	     iterStore != m_mapSearchFolders.end(); ++iterStore) {
		for (iterFolder = iterStore->second.begin();
		     iterFolder != iterStore->second.end(); ++iterFolder)
			delete iterFolder->second;
		iterStore->second.clear();
	}
    
	m_mapSearchFolders.clear();
	pthread_mutex_unlock(&m_mutexMapSearchFolders);
    
    pthread_mutex_destroy(&m_mutexMapSearchFolders);
    pthread_cond_destroy(&m_condThreadExited);
    
	pthread_mutex_lock(&m_mutexEvents);
    m_bExitThread = true;
	pthread_cond_broadcast(&m_condEvents);
	pthread_mutex_unlock(&m_mutexEvents);

    pthread_join(m_threadProcess, NULL);
    
    pthread_mutex_destroy(&m_mutexEvents);
    pthread_cond_destroy(&m_condEvents);
}

// Only loads the search criteria for all search folders. Used once at boot time
ECRESULT ECSearchFolders::LoadSearchFolders() 
{
    ECRESULT er = erSuccess;
    ECDatabase *lpDatabase = NULL;
    DB_RESULT lpResult = NULL;
    DB_ROW lpRow = NULL;
    unsigned int ulFolderId = 0;
    unsigned int ulStoreId = 0;
    unsigned int ulStatus = 0;

    // Search for all folders with PR_EC_SEARCHCRIT that are not deleted. Note that this query can take quite some time on large databases
    std::string strQuery = "SELECT hierarchy.id, properties.val_ulong FROM hierarchy LEFT JOIN properties ON properties.hierarchyid=hierarchy.id AND properties.tag=" + stringify(PROP_ID(PR_EC_SEARCHFOLDER_STATUS)) +" AND properties.type=" + stringify(PROP_TYPE(PR_EC_SEARCHFOLDER_STATUS)) + " WHERE hierarchy.type=3 AND hierarchy.flags=2";
    struct searchCriteria *lpSearchCriteria = NULL;
    
    // Get database
    er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess)
        goto exit;

    er = lpDatabase->DoSelect(strQuery, &lpResult);
    if(er != erSuccess)
        goto exit;
        
    while((lpRow = lpDatabase->FetchRow(lpResult))) {

        if(lpRow[0] == NULL)
            continue;
            
        if(lpRow[1] != NULL)
            ulStatus = atoi(lpRow[1]);
        else
            ulStatus = EC_SEARCHFOLDER_STATUS_RUNNING; // this is the default if no property is found
            
        ulFolderId = atoi(lpRow[0]);
        
        er = m_lpSessionManager->GetCacheManager()->GetStore(ulFolderId, &ulStoreId, NULL);
        if(er != erSuccess) {
            er = erSuccess;
            continue;
        }
        
        // Only load the table if it is not stopped
        if(ulStatus != EC_SEARCHFOLDER_STATUS_STOPPED) {
            er = LoadSearchCriteria(ulStoreId, ulFolderId, &lpSearchCriteria);
            if(er != erSuccess) {
                er = erSuccess;
                continue;
            }
            
            if(ulStatus == EC_SEARCHFOLDER_STATUS_REBUILD) 
                ec_log_info("Rebuilding search folder %d", ulFolderId);
                
            // If the folder was in the process of rebuilding, then completely rebuild the search results (we don't know how far the search got)
            er = AddSearchFolder(ulStoreId, ulFolderId, ulStatus == EC_SEARCHFOLDER_STATUS_REBUILD, lpSearchCriteria);
            if(er != erSuccess) {
                er = erSuccess; // just try to skip the error
                continue;
            }
            
            
            if(lpSearchCriteria) {
                FreeSearchCriteria(lpSearchCriteria);
                lpSearchCriteria = NULL;
            }
        }
    }

exit:
    if(lpResult)
        lpDatabase->FreeResult(lpResult);

    if(lpSearchCriteria)
        FreeSearchCriteria(lpSearchCriteria);
        
    return er;
}

// Called from IMAPIContainer::SetSearchCriteria
ECRESULT ECSearchFolders::SetSearchCriteria(unsigned int ulStoreId, unsigned int ulFolderId, struct searchCriteria *lpSearchCriteria)
{
    ECRESULT er =erSuccess;

    if(lpSearchCriteria == NULL) {
        /* Always return successful, so that Outlook 2007 works */
        CancelSearchFolder(ulStoreId, ulFolderId);
    } else {

        er = AddSearchFolder(ulStoreId, ulFolderId, true, lpSearchCriteria);
        if(er != erSuccess)
            goto exit;

        er = SaveSearchCriteria(ulStoreId, ulFolderId, lpSearchCriteria);
        if(er != erSuccess)
            goto exit;
    }

exit:
    return er;
}

// Gets the search criteria from in-memory
ECRESULT ECSearchFolders::GetSearchCriteria(unsigned int ulStoreId, unsigned int ulFolderId, struct searchCriteria **lppSearchCriteria, unsigned int *lpulFlags)
{
	ECRESULT er = erSuccess;
	STOREFOLDERIDSEARCH::const_iterator iterStore;
	FOLDERIDSEARCH::const_iterator iterFolder;

    pthread_mutex_lock(&m_mutexMapSearchFolders);
    
    // See if there are any searches for this store first
    iterStore = m_mapSearchFolders.find(ulStoreId);
    
    if(iterStore == m_mapSearchFolders.end()) {
        er = KCERR_NOT_INITIALIZED;
        goto exit;
    }
    
    iterFolder = iterStore->second.find(ulFolderId);
    if(iterFolder == iterStore->second.end()) {
        er = KCERR_NOT_INITIALIZED;
        goto exit;
    }
    
    er = CopySearchCriteria(NULL, iterFolder->second->lpSearchCriteria, lppSearchCriteria);
    if(er != erSuccess)
        goto exit;
        
    er = GetState(ulStoreId, ulFolderId, lpulFlags);
    if(er != erSuccess)
        goto exit;
        
    // Add recursive flag if necessary
    *lpulFlags |= iterFolder->second->lpSearchCriteria->ulFlags & SEARCH_RECURSIVE;
        
exit:
    pthread_mutex_unlock(&m_mutexMapSearchFolders); 
    
    return er;
}

// Add or modify a search folder
ECRESULT ECSearchFolders::AddSearchFolder(unsigned int ulStoreId, unsigned int ulFolderId, bool bReStartSearch, struct searchCriteria *lpSearchCriteria)
{
    ECRESULT er = erSuccess;
    struct searchCriteria *lpCriteria = NULL;
    STOREFOLDERIDSEARCH::iterator iterStore;
    SEARCHFOLDER *lpSearchFolder = NULL;    
    unsigned int ulParent = 0;

    if(lpSearchCriteria == NULL) {
        er = LoadSearchCriteria(ulStoreId, ulFolderId, &lpCriteria);
        
        if(er != erSuccess)
            goto exit;
            
        lpSearchCriteria = lpCriteria;
    }

    // Cancel any running searches
    CancelSearchFolder(ulStoreId, ulFolderId);

    if(bReStartSearch) {
        // Set the status of the table as rebuilding
        SetStatus(ulFolderId, EC_SEARCHFOLDER_STATUS_REBUILD);
        
        // Remove any results for this folder if we're restarting the search
        er = ResetResults(ulStoreId, ulFolderId);
        if(er != erSuccess)
            goto exit;
    }
        
    // Tell tables that we've reset:

    // 1. Reset cache for this folder
    m_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, ulFolderId);
    
    // 2. Send reset table contents notification
    er = m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE, 0, ulFolderId, 0, MAPI_MESSAGE);
    if(er != erSuccess)
        goto exit;
        
    // 3. Send change tables viewing us (hierarchy tables)
    if(m_lpSessionManager->GetCacheManager()->GetParent(ulFolderId, &ulParent) == erSuccess) {
        er = m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParent, ulFolderId, MAPI_FOLDER);
        if(er != erSuccess)
            goto exit;
    }

    lpSearchFolder = new SEARCHFOLDER(ulStoreId, ulFolderId);

    er = CopySearchCriteria(NULL, lpSearchCriteria, &lpSearchFolder->lpSearchCriteria);
    if(er != erSuccess) {
        delete lpSearchFolder;
        goto exit;
    }

    pthread_mutex_lock(&m_mutexMapSearchFolders);
    
    // Get searches for this store, or add it to the list.
	iterStore = m_mapSearchFolders.insert(STOREFOLDERIDSEARCH::value_type(ulStoreId, FOLDERIDSEARCH())).first;

    iterStore->second.insert(FOLDERIDSEARCH::value_type(ulFolderId, lpSearchFolder));
	g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_COUNT);
        
    if(bReStartSearch) {
        lpSearchFolder->bThreadFree = false;
        
        THREADINFO *ti = new THREADINFO;
        ti->lpSearchFolders = this;
        ti->lpFolder = lpSearchFolder;
        
        // Insert the actual folder with the criteria
        // Start the thread (will store the thread id in the original list)
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#ifdef LINUX
        pthread_attr_setstacksize(&attr, 512*1024); // 512KB stack space for search threads
#endif
        int err = 0;
        
        if((err = pthread_create(&lpSearchFolder->sThreadId, &attr, ECSearchFolders::SearchThread, (void *)ti)) != 0) {
            ec_log_crit("Unable to spawn thread for search: %s", strerror(err));
            er = KCERR_NOT_ENOUGH_MEMORY;
        }
	set_thread_name(lpSearchFolder->sThreadId, "SearchFolders:Folder");
    }
    
    pthread_mutex_unlock(&m_mutexMapSearchFolders);

exit:
    if (lpCriteria)
        FreeSearchCriteria(lpCriteria);

    return er;
}

// See if a folder is a search folder
ECRESULT ECSearchFolders::IsSearchFolder(unsigned int ulStoreID, unsigned int ulFolderId)
{
    ECRESULT er = erSuccess;
	ECDatabase		*lpDatabase = NULL;
	DB_RESULT		lpDBResult = NULL;
	DB_ROW			lpDBRow = NULL;
	std::string		strQuery;

	/* Get database */
	er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		goto exit;

	// Find out what kind of table this is
	strQuery = "SELECT flags FROM hierarchy WHERE id=" + stringify(ulFolderId);

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	lpDBRow = lpDatabase->FetchRow(lpDBResult);
	if(!lpDBRow || lpDBRow[0] == NULL) {
	    er = KCERR_NOT_FOUND;
	    goto exit;
    }
    
    if(atoui(lpDBRow[0]) != FOLDER_SEARCH) {
        er = KCERR_NOT_FOUND;
        goto exit;
    }

exit:

    if(lpDBResult)
        lpDatabase->FreeResult(lpDBResult);

    return er;
}

// Cancel a search: stop any rebuild thread and stop processing updates for this search folder
ECRESULT ECSearchFolders::CancelSearchFolder(unsigned int ulStoreID, unsigned int ulFolderId)
{
    ECRESULT er = erSuccess;
    STOREFOLDERIDSEARCH::iterator iterStore;
    FOLDERIDSEARCH::iterator iterFolder;
    SEARCHFOLDER *lpFolder = NULL;

    // Lock the list, preventing other Cancel requests messing with the thread    
    pthread_mutex_lock(&m_mutexMapSearchFolders);
    
    iterStore = m_mapSearchFolders.find(ulStoreID);
    if(iterStore == m_mapSearchFolders.end()) {
        pthread_mutex_unlock(&m_mutexMapSearchFolders);
        er = KCERR_NOT_FOUND;
        goto exit;
    }

    iterFolder = iterStore->second.find(ulFolderId);
    if(iterFolder == iterStore->second.end()) {
        pthread_mutex_unlock(&m_mutexMapSearchFolders);
        er = KCERR_NOT_FOUND;
        goto exit;
    }
    
    lpFolder = iterFolder->second;
    
    // Remove the item from the list
    iterStore->second.erase(iterFolder);
	g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_COUNT, -1);
    
    pthread_mutex_unlock(&m_mutexMapSearchFolders);

	DestroySearchFolder(lpFolder);

exit:    
    return er;
}

void ECSearchFolders::DestroySearchFolder(SEARCHFOLDER *lpFolder)
{
	unsigned int ulFolderId = lpFolder->ulFolderId;
    // Nobody can access lpFolder now, except for us and the search thread
    // FIXME check this assumption !!!
    
    // Signal the thread to exit
    lpFolder->bThreadExit = true;
    
    pthread_mutex_lock(&lpFolder->mMutexThreadFree);
    
    while(lpFolder->bThreadFree == false) {
        // Wait for the thread to signal that lpFolder is no longer in use by the thread
        
        // The condition is used for all threads, so it may have been fired for a different
        // thread. This is efficient enough.
        pthread_cond_wait(&m_condThreadExited, &lpFolder->mMutexThreadFree);
    }
    
    pthread_mutex_unlock(&lpFolder->mMutexThreadFree);

    // Nobody is using lpFolder now
    delete lpFolder;
    
    // Set the search as stopped in the database
    SetStatus(ulFolderId, EC_SEARCHFOLDER_STATUS_STOPPED);

}

/**
 * Cancel all the search folders on a store and removing the results
 */
ECRESULT ECSearchFolders::RemoveSearchFolder(unsigned int ulStoreID)
{
	ECRESULT er = erSuccess;
	STOREFOLDERIDSEARCH::iterator iterStore;
	FOLDERIDSEARCH::iterator iterFolder;
	unsigned int ulFolderID;
	std::list<SEARCHFOLDER*>	listSearchFolders;
	std::list<SEARCHFOLDER *>::const_iterator iterSearchFolder;

	// Lock the list, preventing other Cancel requests messing with the thread
	pthread_mutex_lock(&m_mutexMapSearchFolders);

	iterStore = m_mapSearchFolders.find(ulStoreID);
	if(iterStore == m_mapSearchFolders.end()) {
		pthread_mutex_unlock(&m_mutexMapSearchFolders);
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	for (iterFolder = iterStore->second.begin();
	     iterFolder != iterStore->second.end(); ++iterFolder)
		listSearchFolders.push_back(iterFolder->second);

	iterStore->second.clear();
	
	// Remove store from list, items of the store will be delete in 'DestroySearchFolder'
	m_mapSearchFolders.erase(iterStore);

	pthread_mutex_unlock(&m_mutexMapSearchFolders);

//@fixme: server shutdown can result in a crash?
	for (iterSearchFolder = listSearchFolders.begin();
	     iterSearchFolder != listSearchFolders.end(); ++iterSearchFolder) {
		g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_COUNT, -1);
		ulFolderID = (*iterSearchFolder)->ulFolderId;

		// Wait and free searchfolder data
		DestroySearchFolder((*iterSearchFolder));

		// Remove results from database
		ResetResults(ulStoreID, ulFolderID);
	}

exit:
	return er;
}

// Removing a search folder is subtly different from cancelling it; removing a search folder
// also removes all search results
ECRESULT ECSearchFolders::RemoveSearchFolder(unsigned int ulStoreId, unsigned int ulFolderId)
{
    ECRESULT er = erSuccess;

    // Cancel any running (rebuilding) searches
    CancelSearchFolder(ulStoreId, ulFolderId);
    // Ignore errors
    
    // Remove results from database
    ResetResults(ulStoreId, ulFolderId);

    return er;
}
    
// WARNING: THIS FUNCTION IS *NOT* THREADSAFE. IT SHOULD ONLY BE CALLED AT STARTUP WHILE SINGLE-THREADED
ECRESULT ECSearchFolders::RestartSearches()
{
	ECRESULT er = erSuccess;
	STOREFOLDERIDSEARCH::const_iterator iterStore;
	FOLDERIDSEARCH::const_iterator iterFolder;

    ec_log_crit("Starting rebuild of search folders... This may take a while.");
    
    for (iterStore = m_mapSearchFolders.begin();
         iterStore != m_mapSearchFolders.end(); ++iterStore)
    {
        ec_log_crit("  Rebuilding searchfolders of store %d", iterStore->first);
        for (iterFolder = iterStore->second.begin();
             iterFolder != iterStore->second.end(); ++iterFolder) {
            ResetResults(iterStore->first, iterFolder->first);
            Search(iterStore->first, iterFolder->first, iterFolder->second->lpSearchCriteria, NULL, false);
        }
    }    

    ec_log_info("Finished rebuild.");
    
    return er;
}
   
// Should be called for *any* change of *any* message object in the database. 
ECRESULT ECSearchFolders::UpdateSearchFolders(unsigned int ulStoreId, unsigned int ulFolderId, unsigned int ulObjId, ECKeyTable::UpdateType ulType)
{
    ECRESULT er = erSuccess;
    
    EVENT ev;
    
    ev.ulStoreId = ulStoreId;
    ev.ulFolderId = ulFolderId;
    ev.ulObjectId = ulObjId;
    ev.ulType = ulType;
    
    pthread_mutex_lock(&m_mutexEvents);

    // Add the event to the queue
	m_lstEvents.push_back(ev);
    
    // Signal a change in the queue (actually only needed for the first event, but this wastes almost
    // no time and is safer)
	pthread_cond_broadcast(&m_condEvents);

	pthread_mutex_unlock(&m_mutexEvents);
    
    return er;
}

// Process a list of message changes in a single folder of a certain type
ECRESULT ECSearchFolders::ProcessMessageChange(unsigned int ulStoreId, unsigned int ulFolderId, ECObjectTableList *lstObjectIDs, ECKeyTable::UpdateType ulType)
{
    ECRESULT er = erSuccess;
	STOREFOLDERIDSEARCH::const_iterator iterStore;
	FOLDERIDSEARCH::const_iterator iterFolder;
    bool bIsInTargetFolder = false;
    std::set<unsigned int> setParents;
	std::set<unsigned int>::const_iterator iterParents;
	ECObjectTableList::const_iterator iterObjectIDs;
    unsigned int ulOwner = 0;
    ECSession *lpSession = NULL;
	ECODStore ecOBStore;
	SUBRESTRICTIONRESULTS *lpSubResults = NULL;
    struct rowSet *lpRowSet = NULL;
    struct propTagArray *lpPropTags = NULL;
    unsigned int ulParent = 0;
    unsigned int ulFlags = 0;
	unsigned int ulSCFolderId = 0;
	int lCount = 0; // Number of messages added, positive means more added, negative means more discarded
	int lUnreadCount = 0; // Same, but for unread count
	ECDatabase *lpDatabase = NULL;
	std::list<ULONG> lstPrefix;
	bool fInserted = false;
	
	lstPrefix.push_back(PR_MESSAGE_FLAGS);

	ECLocale locale = m_lpSessionManager->GetSortLocale(ulStoreId);
	
    pthread_mutex_lock(&m_mutexMapSearchFolders);

    er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess)
        goto exit;

    iterStore = m_mapSearchFolders.find(ulStoreId);
    if(iterStore == m_mapSearchFolders.end()) 
        // There are no search folders in the target store. We will therefore never match any search
        // result and might as well exit now.
        goto exit;

    // OPTIMIZATION: if a target folder == root folder of ulStoreId, and a recursive searchfolder, then
    // the following check is always TRUE

    // Get the owner of the search folder. This *could* be different from the owner of the objects!
    er = m_lpSessionManager->GetCacheManager()->GetObject(ulStoreId, NULL, &ulOwner, NULL, NULL);
    if(er != erSuccess)
        goto exit;

    // FIXME FIXME FIXME we still need to check MAPI_ASSOCIATED and MSGFLAG_DELETED and exclude them.. better if the caller does this.    

    // We now have to see if the folder in which the object resides is actually a target of a search folder.
    // We do this by checking whether the specified folder is a searchfolder target folder, or a child of
    // a target folder if it is a recursive search.
    
    // Loop through search folders for this store
    for (iterFolder = iterStore->second.begin();
         iterFolder != iterStore->second.end(); ++iterFolder) {
		ULONG ulAttempts = 4;	// Random number
		
		do {
			bIsInTargetFolder = false;
			lCount = 0;
			lUnreadCount = 0;
			
			if(iterFolder->second->lpSearchCriteria->lpFolders == NULL || iterFolder->second->lpSearchCriteria->lpRestrict == NULL)
				continue;
				
			er = lpDatabase->Begin();
			if (er != erSuccess)
				goto exit;
			
			// Lock searchfolder
			WITH_SUPPRESSED_LOGGING(lpDatabase)
				er = lpDatabase->DoSelect("SELECT properties.val_ulong FROM properties WHERE hierarchyid = " + stringify(iterFolder->first) + " FOR UPDATE", NULL);
			if (er == KCERR_DATABASE_ERROR) {
				DB_ERROR dberr = lpDatabase->GetLastError();
				if (dberr == DB_E_LOCK_WAIT_TIMEOUT || dberr == DB_E_LOCK_DEADLOCK) {
					er = lpDatabase->Rollback();
					if (er != erSuccess) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): database rollback failed %d", er);
						goto exit;
					}
					g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_UPDATE_RETRY);
					continue;
				}
				else {
					ec_log_err("ECSearchFolders::ProcessMessageChange(): select failed");
					goto exit;
				}
			} else if (er != erSuccess) {
				ec_log_crit("ECSearchFolders::ProcessMessageChange(): unexpected error %d", er);
				goto exit;
			}
			
			if(ulType != ECKeyTable::TABLE_ROW_DELETE) {
				// Loop through all targets for each searchfolder, if one matches, then match the restriction with the objects
				for (unsigned int i = 0; i < iterFolder->second->lpSearchCriteria->lpFolders->__size; ++i)
					if(m_lpSessionManager->GetCacheManager()->GetObjectFromEntryId(&iterFolder->second->lpSearchCriteria->lpFolders->__ptr[i], &ulSCFolderId) == erSuccess &&
						ulSCFolderId == ulFolderId)
					{
						bIsInTargetFolder = true;
						break;
					}

				if(!bIsInTargetFolder && iterFolder->second->lpSearchCriteria->ulFlags & RECURSIVE_SEARCH) {
					// The item is not in one of the base folders, but it may be in one of children of the folders.
					// We do it in this order because the GetParent() calls below may cause database accesses, so
					// we only actually do those database accesses if we have to.
					
					unsigned int ulAncestor = ulFolderId;
					
					// Get all the parents of this object (usually around 5 or 6)
					setParents.insert(ulFolderId);
					
					while(1) {
						er = m_lpSessionManager->GetCacheManager()->GetParent(ulAncestor, &ulAncestor);
						if(er != erSuccess)
							break;
							
						setParents.insert(ulAncestor);
					}
					
					// setParents now contains all the parent of this object, now we can check if any of the ancestors
					// are in the search target
					for (unsigned int i = 0; i < iterFolder->second->lpSearchCriteria->lpFolders->__size; ++i) {
						if(m_lpSessionManager->GetCacheManager()->GetObjectFromEntryId(&iterFolder->second->lpSearchCriteria->lpFolders->__ptr[i], &ulSCFolderId) == erSuccess)
						{

							iterParents = setParents.find(ulSCFolderId);

							if(iterParents != setParents.end()) {
								bIsInTargetFolder = true;
								break;
							}
						}
					}
				}
			} else {
				// Table type DELETE, so the item is definitely not in the search path. Just delete it
				bIsInTargetFolder = false;
			}
			
			// The folder in which the modify message is, is in our search path for this searchfolder
			if(bIsInTargetFolder) {
				// Create a session for the target user
				if(lpSession == NULL) {
					er = m_lpSessionManager->CreateSessionInternal(&lpSession, ulOwner);
					if(er != erSuccess) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): CreateSessionInternal failed %d", er);
						goto exit;
					}
						
					lpSession->Lock();
				}
				
				ecOBStore.ulStoreId = ulStoreId;
				ecOBStore.ulFolderId = 0;
				ecOBStore.ulFlags = 0;
				ecOBStore.ulObjType = MAPI_MESSAGE;
				ecOBStore.lpGuid = NULL;

				if(ulType == ECKeyTable::TABLE_ROW_ADD || ulType == ECKeyTable::TABLE_ROW_MODIFY) {
					std::list<ULONG> lstPropTags;
					
					// Get the restriction ready for this search folder
					er = ECGenericObjectTable::GetRestrictPropTags(iterFolder->second->lpSearchCriteria->lpRestrict, &lstPrefix, &lpPropTags);
					if(er != erSuccess) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): ECGenericObjectTable::GetRestrictPropTags failed %d", er);
						goto exit;
					}
						
					// Get necessary row data for the object    
					er = ECStoreObjectTable::QueryRowData(NULL, NULL, lpSession, lstObjectIDs, lpPropTags, &ecOBStore, &lpRowSet, false, false);
					if(er != erSuccess) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): ECStoreObjectTable::QueryRowData failed %d", er);
						goto exit;
					}
						
					er = RunSubRestrictions(lpSession, &ecOBStore, iterFolder->second->lpSearchCriteria->lpRestrict, lstObjectIDs, locale, &lpSubResults);
					if(er != erSuccess) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): RunSubRestrictions failed %d", er);
						goto exit;
					}

					iterObjectIDs = lstObjectIDs->begin();
		
					// Check if the item matches for each item
					for (int i = 0; i < lpRowSet->__size; ++i, ++iterObjectIDs) {
						bool fMatch;

						// Match the restriction
						er = ECGenericObjectTable::MatchRowRestrict(lpSession->GetSessionManager()->GetCacheManager(), &lpRowSet->__ptr[i], iterFolder->second->lpSearchCriteria->lpRestrict, lpSubResults, locale, &fMatch);

						if(er == erSuccess) {
							if (fMatch) {
								if(lpRowSet->__ptr[i].__ptr[0].ulPropTag != PR_MESSAGE_FLAGS)
									continue;
									
								// Get the read flag for this message
								ulFlags = lpRowSet->__ptr[i].__ptr[0].Value.ul & MSGFLAG_READ;
								
								// Update on-disk search folder
								if(AddResults(ulStoreId, iterFolder->first, iterObjectIDs->ulObjId, ulFlags, &fInserted) == erSuccess) {
									if(fInserted) {
										// One more match
										++lCount;
										if(!ulFlags)
											++lUnreadCount;

										// Send table notification
										m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD, 0, iterFolder->first, iterObjectIDs->ulObjId, MAPI_MESSAGE);
									} else {
										// Row was modified, so flags has changed. Since the only possible values are MSGFLAG_READ or 0, we know the new flags.
										
										if(ulFlags)
											--lUnreadCount; // New state is read, so old state was unread, so --unread
										else
											++lUnreadCount; // New state is unread, so old state was read, so ++unread
										
										// Send table notification
										m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, iterFolder->first, iterObjectIDs->ulObjId, MAPI_MESSAGE);
									}
									
								} else {
									// AddResults will return an error if the call didn't do anything (record was already in the table).
									
									// Even though, we should still send notifications since the row changed
									m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, iterFolder->first, iterObjectIDs->ulObjId, MAPI_MESSAGE);
								}
							} else if (ulType == ECKeyTable::TABLE_ROW_MODIFY) {
								// Only delete modified items, not new items
								if (DeleteResults(ulStoreId, iterFolder->first, iterObjectIDs->ulObjId, &ulFlags) == erSuccess) {
									--lCount;
									if(!ulFlags)
										--lUnreadCount; // Removed message was unread
									m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE, 0, iterFolder->first, iterObjectIDs->ulObjId, MAPI_MESSAGE);
								}
							}

							// Ignore errors from the updates
							er = erSuccess;
						}
					}
				} else {
					// Message was deleted anyway, update on-disk search folder and send table notification
					for (iterObjectIDs = lstObjectIDs->begin();
					     iterObjectIDs != lstObjectIDs->end(); ++iterObjectIDs)
						if (DeleteResults(ulStoreId, iterFolder->first, iterObjectIDs->ulObjId, &ulFlags) == erSuccess) {
							m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE, 0, iterFolder->first, iterObjectIDs->ulObjId, MAPI_MESSAGE); 
							--lCount;
							if(!ulFlags)
								--lUnreadCount; // Removed message was unread
						}
				}

				if(lpPropTags) {
					FreePropTagArray(lpPropTags);
					lpPropTags = NULL;
				}
				if(lpRowSet) {
					FreeRowSet(lpRowSet, true);
					lpRowSet = NULL;
				}
				if(lpSubResults){
					FreeSubRestrictionResults(lpSubResults);
					lpSubResults = NULL;
				}

			} else {
				// Not in a target folder, remove from search results
				for (iterObjectIDs = lstObjectIDs->begin();
				     iterObjectIDs != lstObjectIDs->end();
				     ++iterObjectIDs)
					if(DeleteResults(ulStoreId, iterFolder->first, iterObjectIDs->ulObjId, &ulFlags) == erSuccess) {
						m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE, 0, iterFolder->first, iterObjectIDs->ulObjId, MAPI_MESSAGE); 
						--lCount;
						if(!ulFlags)
							--lUnreadCount; // Removed message was unread
					}
			}

			if(lCount || lUnreadCount) {
				// If the searchfolder has changed, update counts and send notifications
				WITH_SUPPRESSED_LOGGING(lpDatabase) {
					er = UpdateFolderCount(lpDatabase, iterFolder->first, PR_CONTENT_COUNT, lCount);
					if (er == erSuccess)
						er = UpdateFolderCount(lpDatabase, iterFolder->first, PR_CONTENT_UNREAD, lUnreadCount);
				}
				
				if (er == KCERR_DATABASE_ERROR) {
					DB_ERROR dberr = lpDatabase->GetLastError();
					if (dberr == DB_E_LOCK_WAIT_TIMEOUT || dberr == DB_E_LOCK_DEADLOCK) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): database error(1) %d", dberr);
						er = lpDatabase->Rollback();
						if (er != erSuccess) {
							ec_log_crit("ECSearchFolders::ProcessMessageChange(): database rollback failed(1) %d", er);
							goto exit;
						}
						g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_UPDATE_RETRY);
						continue;
					} else
						goto exit;
				} else if (er != erSuccess) {
					ec_log_crit("ECSearchFolders::ProcessMessageChange(): unexpected error(1) %d", er);
					goto exit;
				}
				
				m_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, iterFolder->first);
				er = m_lpSessionManager->NotificationModified(MAPI_FOLDER, iterFolder->first);
				
				if(m_lpSessionManager->GetCacheManager()->GetParent(iterFolder->first, &ulParent) == erSuccess)
					er = m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParent, iterFolder->first, MAPI_FOLDER);

				er = erSuccess; // Ignore errors
			}        
			
			er = lpDatabase->Commit();
			if (er == KCERR_DATABASE_ERROR) {
				DB_ERROR dberr = lpDatabase->GetLastError();
				if (dberr == DB_E_LOCK_WAIT_TIMEOUT || dberr == DB_E_LOCK_DEADLOCK) {
					ec_log_crit("ECSearchFolders::ProcessMessageChange(): database error(2) %d", dberr);
					er = lpDatabase->Rollback();
					if (er != erSuccess) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): database rollback failed(2) %d", er);
						goto exit;
					}
					g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_UPDATE_RETRY);
					continue;
				} else
					goto exit;
			} else if (er != erSuccess) {
				ec_log_crit("ECSearchFolders::ProcessMessageChange(): unexpected error(2) %d", er);
				goto exit;
			}
				
			break;	// Break the do-while loop since we succeeded
		} while (--ulAttempts);
		
		if (ulAttempts == 0) {
			// The only way to get here is if all attempts failed with an SQL error.
			ASSERT(er != KCERR_DATABASE_ERROR);
			g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_UPDATE_FAIL);
		}
    }

exit:    
    pthread_mutex_unlock(&m_mutexMapSearchFolders);

    if(lpPropTags)
        FreePropTagArray(lpPropTags);
        
    if(lpSession) {
        lpSession->Unlock();
        m_lpSessionManager->RemoveSessionInternal(lpSession);
    }
        
	delete lpSubResults;
        
    if(lpRowSet)
        FreeRowSet(lpRowSet, true);
        
    return er;
}

/**
 * Process a set of rows and add them to the searchresults table if they match the restriction
 *
 * Special note on transactioning:
 *
 * If you pass bNotify == false, you must begin()/commit() yourself.
 * If you pass bNotify == true, this function will begin()/commit() for you.
 *
 * @param[in] lpDatabase Database
 * @param[in] lpSession Session for the user owning the searchfolder
 * @param[in] lpRestrict Restriction to test items against
 * @param[in] lpbCancel Stops processing if it is set to TRUE while running (from another thread)
 * @param[in] ulStoreId Store that the searchfolder is in
 * @param[in] ulFolderId ID of the searchfolder
 * @param[in] lpODStore Information for the objects in the table
 * @param[in] ecRows Rows to process
 * @param[in] lpPropTags Precalculated proptags needed to check lpRestrict
 * @param[in] locale Locale to process rows in (for comparisons)
 * @param[in] bNotify TRUE if we should notify tables in this function, see note about transactions above
 * @return result
 */
ECRESULT ECSearchFolders::ProcessCandidateRows(ECDatabase *lpDatabase,
    ECSession *lpSession, struct restrictTable *lpRestrict, bool *lpbCancel,
    unsigned int ulStoreId, unsigned int ulFolderId, ECODStore *lpODStore,
    ECObjectTableList ecRows, struct propTagArray *lpPropTags,
    const ECLocale &locale, bool bNotify)
{
	ECRESULT er = erSuccess;
	ECObjectTableList::const_iterator iterRows;
	struct rowSet *lpRowSet = NULL;
	SUBRESTRICTIONRESULTS *lpSubResults = NULL;
	int lCount = 0;
	int lUnreadCount = 0;
	bool fMatch = false;
	unsigned int ulObjFlags = 0;
	unsigned int ulParent = 0;
	std::list<unsigned int> lstMatches;
	std::list<unsigned int> lstFlags;
	
	ASSERT(lpPropTags->__ptr[0] == PR_MESSAGE_FLAGS);
    
    iterRows = ecRows.begin();
    
    if(bNotify) {
        er = lpDatabase->Begin();
		if (er != erSuccess) {
			ec_log_err("ECSearchFolders::ProcessCandidateRows() BEGIN failed %d", er);
			goto exit;
		}
            
        er = lpDatabase->DoSelect("SELECT properties.val_ulong FROM properties WHERE hierarchyid = " + stringify(ulFolderId) + " FOR UPDATE", NULL);
		if (er != erSuccess) {
			ec_log_err("ECSearchFolders::ProcessCandidateRows() SELECT failed %d", er);
			goto exit;
    }
	}
    
    // Get the row data for the search
    er = ECStoreObjectTable::QueryRowData(NULL, NULL, lpSession, &ecRows, lpPropTags, lpODStore, &lpRowSet, false, false);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() ECStoreObjectTable::QueryRowData failed %d", er);
		goto exit;
	}
        
    // Get the subrestriction results for the search
    er = RunSubRestrictions(lpSession, lpODStore, lpRestrict, &ecRows, locale, &lpSubResults);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() RunSubRestrictions failed %d", er);
		goto exit;
	}
    
    // Loop through the results data                
    lCount=0;
    lUnreadCount=0;
    for (int j = 0; j< lpRowSet->__size && (!lpbCancel || !*lpbCancel); ++j, ++iterRows) {
        if(ECGenericObjectTable::MatchRowRestrict(lpSession->GetSessionManager()->GetCacheManager(), &lpRowSet->__ptr[j], lpRestrict, lpSubResults, locale, &fMatch) != erSuccess)
            continue;

        if(!fMatch)
            continue;
            
        if(lpRowSet->__ptr[j].__ptr[0].ulPropTag != PR_MESSAGE_FLAGS)
            continue;
            
        ulObjFlags = lpRowSet->__ptr[j].__ptr[0].Value.ul & MSGFLAG_READ;
        
        lstMatches.push_back(iterRows->ulObjId);
        lstFlags.push_back(ulObjFlags);
    }
        
    // Add matched row to database
    er = AddResults(ulStoreId, ulFolderId, lstMatches, lstFlags, &lCount, &lUnreadCount);
	if (er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() AddResults failed %d", er);
		goto exit;
	}

    if(lCount || lUnreadCount) {
        er = UpdateFolderCount(lpDatabase, ulFolderId, PR_CONTENT_COUNT, lCount);
		if (er == erSuccess) {
			er = UpdateFolderCount(lpDatabase, ulFolderId, PR_CONTENT_UNREAD, lUnreadCount);
			if (er != erSuccess)
				ec_log_crit("ECSearchFolders::ProcessCandidateRows() UpdateFolderCount failed(2) %d", er);
		}
		else {
			ec_log_crit("ECSearchFolders::ProcessCandidateRows() UpdateFolderCount failed(1) %d", er);
			goto exit;
	}
	}

    if(bNotify) {
        er = lpDatabase->Commit();
		if (er != erSuccess) {
			ec_log_err("ECSearchFolders::ProcessCandidateRows() DB commit failed %d", er);
			goto exit;
		}
    
        // Add matched row and send a notification to update views of this search (if any are open)
        m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD, 0, ulFolderId, lstMatches, MAPI_MESSAGE);
    }
    
    // If the searchfolder counte have changed, update counts and send notifications
    if(bNotify) {
		m_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, ulFolderId);
		m_lpSessionManager->NotificationModified(MAPI_FOLDER, ulFolderId); // folder has modified due to PR_CONTENT_*
		
		if(m_lpSessionManager->GetCacheManager()->GetParent(ulFolderId, &ulParent) == erSuccess)
			m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParent, ulFolderId, MAPI_FOLDER); // PR_CONTENT_* has changed in tables too
    }

exit:    
    if(bNotify && lpDatabase && er != erSuccess)
        lpDatabase->Rollback();
        
    if(lpRowSet) {
        FreeRowSet(lpRowSet, true);
        lpRowSet = NULL;
    }
    
    if(lpSubResults) {
        FreeSubRestrictionResults(lpSubResults);
        lpSubResults = NULL;
    }
    
    return er;
}    


// Does an actual search of a specific search Criteria in store ulStoreId, search folder ulFolderId. Will cancel if *lpbCancel
// is TRUE. We check after each message row set to see if the cancel has been requested.
ECRESULT ECSearchFolders::Search(unsigned int ulStoreId, unsigned int ulFolderId, struct searchCriteria *lpSearchCrit, bool *lpbCancel, bool bNotify)
{
    ECRESULT			er = erSuccess;
	ECListInt			lstFolders;
	ECListInt::const_iterator iterFolders;
	ECObjectTableList ecRows;

	sObjectTableKey sRow;
	ECODStore ecODStore;
	ECSession *lpSession = NULL;
	unsigned int ulUserId = 0;
	std::string strQuery;
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult = NULL;
	DB_ROW lpDBRow = NULL;
	struct propTagArray *lpPropTags = NULL;
	unsigned int i=0;
	struct restrictTable *lpAdditionalRestrict = NULL;
	unsigned int ulParent = 0;
	std::list<unsigned int>::const_iterator iterResults;

	std::list<ULONG> lstPrefix;
	lstPrefix.push_back(PR_MESSAGE_FLAGS);

	//Indexer
	std::list<unsigned int> lstIndexerResults;
	GUID guidServer;
	GUID guidStore;

	ECLocale locale = m_lpSessionManager->GetSortLocale(ulStoreId);    
    
    ecODStore.ulStoreId = ulStoreId;
    ecODStore.ulFolderId = 0;
    ecODStore.ulFlags = 0;
    ecODStore.ulObjType = 0;
    ecODStore.lpGuid = NULL; // FIXME: optimisation possible
    
    if(lpSearchCrit->lpFolders == NULL || lpSearchCrit->lpRestrict == NULL) {
        er = KCERR_NOT_FOUND;
	ec_log_err("ECSearchFolders::Search() no folder or search criteria");
        goto exit;
    }

	er = m_lpSessionManager->GetCacheManager()->GetStore(ulStoreId, NULL, &guidStore);
	if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::Search() GetStore failed: 0x%x", er);
		goto exit;
	}
	ecODStore.lpGuid = &guidStore;
    
    // Get the owner of the store
    er = m_lpSessionManager->GetCacheManager()->GetObject(ulStoreId, NULL, &ulUserId, NULL, NULL);
    if(er != erSuccess) {
	ec_log_crit("ECSearchFolders::Search() GetObject failed: 0x%x", er);
        goto exit;
    }
    
    // Create a session with the security credentials of the owner of the store
    er = m_lpSessionManager->CreateSessionInternal(&lpSession, ulUserId);
    if(er != erSuccess) {
	ec_log_crit("ECSearchFolders::Search() CreateSessionInternal failed: 0x%x", er);
        goto exit;
    }
        
    lpSession->Lock();

	er = lpSession->GetDatabase(&lpDatabase);
	if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::Search() GetDatabase failed: 0x%x", er);
		goto exit;
	}

    // Get target folders
	er = m_lpSessionManager->GetCacheManager()->GetEntryListToObjectList(lpSearchCrit->lpFolders, &lstFolders);
	if (er != erSuccess) {
		ec_log_crit("ECSearchFolders::Search() GetEntryListToObjectList failed: 0x%x", er);
		goto exit;
	}
    
    // Reset search results in database
    er = ResetResults(ulStoreId, ulFolderId);
	if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::Search() ResetResults failed: 0x%x", er);
		goto exit;
	}

	// Expand target folders if recursive
    if(lpSearchCrit->ulFlags & RECURSIVE_SEARCH) {
		iterFolders = lstFolders.begin();

		while(iterFolders != lstFolders.end()) {

			strQuery = "SELECT hierarchy.id from hierarchy WHERE hierarchy.parent = " + stringify(*iterFolders) + " AND hierarchy.type=3 AND hierarchy.flags & " + stringify(MSGFLAG_DELETED|MSGFLAG_ASSOCIATED) + " = 0 ORDER by hierarchy.id DESC";

			er = lpDatabase->DoSelect(strQuery, &lpDBResult);
			if(er != erSuccess)
				continue; // Try to continue if the query failed ..

			while((lpDBRow = lpDatabase->FetchRow(lpDBResult)) != NULL) {
				if(lpDBRow && lpDBRow[0])
					lstFolders.push_back(atoi(lpDBRow[0]));
			}

			if(lpDBResult){ lpDatabase->FreeResult(lpDBResult); lpDBResult = NULL; }
			++iterFolders;
		}
	}

	// Check if we can use indexed search
	er = m_lpSessionManager->GetServerGUID(&guidServer);
	if(er != erSuccess)
		goto exit;
	
	if (GetIndexerResults(lpDatabase, m_lpSessionManager->GetConfig(), m_lpSessionManager->GetCacheManager(), &guidServer, &guidStore, lstFolders, lpSearchCrit->lpRestrict, &lpAdditionalRestrict, lstIndexerResults) == erSuccess) {
		// Get the additional restriction properties ready
		er = ECGenericObjectTable::GetRestrictPropTags(lpAdditionalRestrict, &lstPrefix, &lpPropTags);
		if(er != erSuccess) {
			ec_log_err("ECSearchFolders::Search() ECGenericObjectTable::GetRestrictPropTags failed: 0x%x", er);
			goto exit;
		}

        // Since an indexed search should be fast, do the entire query as a single transaction, and notify after Commit()
		er = lpDatabase->Begin();
		if (er != erSuccess) {
			ec_log_err("ECSearchFolders::Search() database begin failed: 0x%x", er);
			goto exit;
		}

        iterResults = lstIndexerResults.begin();
        while(1) {
            // Loop through the results data
            int n = 0;
            
            ecRows.clear();
            for (; iterResults != lstIndexerResults.end() && (!lpbCancel || !*lpbCancel) && n < 200; ++iterResults) {
                sRow.ulObjId = *iterResults;
                sRow.ulOrderId = 0;
                
                ecRows.push_back(sRow);
            }
            
            if(ecRows.empty())
                break; // no more rows
                
            // Note that we do not want ProcessCandidateRows to send notifications since we will send a bulk TABLE_CHANGE later, so bNotify == false here
            er = ProcessCandidateRows(lpDatabase, lpSession, lpAdditionalRestrict, lpbCancel, ulStoreId, ulFolderId, &ecODStore, ecRows, lpPropTags, locale, false);
            if (er != erSuccess) {
			ec_log_err("ECSearchFolders::Search() ProcessCandidateRows failed: 0x%x", er);
			goto exit;
        }            
        }            
		
		er = lpDatabase->Commit();
		if (er != erSuccess) {
			ec_log_err("ECSearchFolders::Search() database commit failed: 0x%x", er);
			goto exit;
		}

		// Notify the search folder and his parent
		if(bNotify) {
			// Add matched rows and send a notification to update views of this search (if any are open)
			m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE, 0, ulFolderId, 0, MAPI_MESSAGE);

			m_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, ulFolderId);
			m_lpSessionManager->NotificationModified(MAPI_FOLDER, ulFolderId); // folder has modified due to PR_CONTENT_*

			if(m_lpSessionManager->GetCacheManager()->GetParent(ulFolderId, &ulParent) == erSuccess)
				m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParent, ulFolderId, MAPI_FOLDER); // PR_CONTENT_* has changed in tables too
		}

	} else {
		// Get the restriction ready for this search folder
		er = ECGenericObjectTable::GetRestrictPropTags(lpSearchCrit->lpRestrict, &lstPrefix, &lpPropTags);
		if(er != erSuccess) {
			ec_log_err("ECSearchFolders::Search() ECGenericObjectTable::GetRestrictPropTags failed: 0x%x", er);
			goto exit;
		}

		// If we needn't notify, we don't need to commit each message before notifying, so Begin() here
		if(!bNotify)
			lpDatabase->Begin();

		// lstFolders now contains all folders to search through
		for (iterFolders = lstFolders.begin(); iterFolders != lstFolders.end() && (!lpbCancel || !*lpbCancel); ++iterFolders) {
			// Optimisation: we know the folder id of the objects we're querying
			ecODStore.ulFolderId = *iterFolders;
			
			// Get a list of messages in folders, sorted descending by creation date so the newest are found first
			strQuery = "SELECT hierarchy.id from hierarchy WHERE hierarchy.parent = " + stringify(*iterFolders) + " AND hierarchy.type=5 AND hierarchy.flags & " + stringify(MSGFLAG_DELETED|MSGFLAG_ASSOCIATED) + " = 0 ORDER by hierarchy.id DESC";

			er = lpDatabase->DoSelect(strQuery, &lpDBResult);
			if(er != erSuccess) {
				ec_log_err("ECSearchFolders::Search() SELECT failed: 0x%x", er);
				continue;
			}
			
			while(1) {	

				if(lpbCancel && *lpbCancel)
					break;
			        
				// Read max. 20 rows from the database
				i = 0;
			    
				ecRows.clear();
				while(i < 20 && (lpDBRow = lpDatabase->FetchRow(lpDBResult)) != NULL) {
					if(lpDBRow[0] == NULL)
						continue;
					sRow.ulObjId = atoui(lpDBRow[0]);
					sRow.ulOrderId = 0;
			        
					ecRows.push_back(sRow);
					++i;
				}
				
				if(ecRows.empty())
					break; // no more rows
					
				er = ProcessCandidateRows(lpDatabase, lpSession, lpSearchCrit->lpRestrict, lpbCancel, ulStoreId, ulFolderId, &ecODStore, ecRows, lpPropTags, locale, bNotify);
				if (er != erSuccess) {
					ec_log_err("ECSearchFolders::Search() ProcessCandidateRows failed: 0x%x", er);
					goto exit;
				}
			}
			
			if(lpDBResult) {
				lpDatabase->FreeResult(lpDBResult);
				lpDBResult = NULL;
			}
		}

		// Search done
		// If we needn't notify, we don't need to commit each message before notifying, so Commit() here
		if(!bNotify)
			lpDatabase->Commit();

	} //if(!bUseIndexer)
    
    // Save this information in the database.
    SetStatus(ulFolderId, EC_SEARCHFOLDER_STATUS_RUNNING);
    
exit:
    if(lpDatabase && er != erSuccess)
        lpDatabase->Rollback();
        
    if(lpSession) {
        lpSession->Unlock();
        m_lpSessionManager->RemoveSessionInternal(lpSession);
    }
        
    if(lpPropTags)
        FreePropTagArray(lpPropTags);

    if(lpDBResult)
        lpDatabase->FreeResult(lpDBResult);

    if (lpAdditionalRestrict)
        FreeRestrictTable(lpAdditionalRestrict);

    return er;
}

// Return whether we are stopped (no entry found), active (no thread found), or rebuilding (thread active)
ECRESULT ECSearchFolders::GetState(unsigned int ulStoreId, unsigned int ulFolderId, unsigned int *lpulState)
{
	ECRESULT er = erSuccess;
	STOREFOLDERIDSEARCH::const_iterator iterStore;
	FOLDERIDSEARCH::const_iterator iterFolder;
	unsigned int ulState = 0;
    
    iterStore = m_mapSearchFolders.find(ulStoreId);
    if(iterStore == m_mapSearchFolders.end()) {
        ulState = 0;
    } else {
        
        iterFolder = iterStore->second.find(ulFolderId);
        if(iterFolder == iterStore->second.end()) {
            ulState = 0;
        } else {
            
            ulState = SEARCH_RUNNING;
            
            if(iterFolder->second->bThreadFree == false)
                ulState |= SEARCH_REBUILD;
        }
    }    

    *lpulState = ulState;

    return er;
}

// Entrypoint for the SearchThread
void* ECSearchFolders::SearchThread(void *lpParam)
{
    THREADINFO *ti = (THREADINFO*)lpParam;
    SEARCHFOLDER *lpFolder = ti->lpFolder; 					// The entry in the m_mapSearchFolders map
    ECSearchFolders *lpSearchFolders = ti->lpSearchFolders; // The main ECSearchFolders object

    // We no longer need this
    delete ti;

	g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_THREADS);

    // Start the search    
    lpSearchFolders->Search(lpFolder->ulStoreId, lpFolder->ulFolderId, lpFolder->lpSearchCriteria, &lpFolder->bThreadExit);

    // Signal search complete to clients
    lpSearchFolders->m_lpSessionManager->NotificationSearchComplete(lpFolder->ulFolderId, lpFolder->ulStoreId);
    
    // Signal exit from thread
    pthread_mutex_lock(&lpFolder->mMutexThreadFree);
    lpFolder->bThreadFree = true;
    pthread_cond_broadcast(&lpSearchFolders->m_condThreadExited);
    pthread_mutex_unlock(&lpFolder->mMutexThreadFree);
    
    // We may not access lpFolder from this point on (it will be freed when the searchfolder is removed)
    lpFolder = NULL;

	g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_THREADS, -1);
    
    return NULL;
}

// Functions to do things in the database
ECRESULT ECSearchFolders::ResetResults(unsigned int ulStoreId, unsigned int ulFolderId) 
{
    ECDatabase *lpDatabase = NULL;
    ECRESULT er = erSuccess;
    unsigned int ulParentId = 0;
    std::string strQuery;
    
    er = m_lpSessionManager->GetCacheManager()->GetParent(ulFolderId, &ulParentId);
    if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::ResetResults(): GetParent failed 0x%x", er);
		goto exit;
	}
    
    er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::ResetResults(): GetThreadLocalDatabase failed 0x%x", er);
		goto exit;
	}
        
    er = lpDatabase->Begin();
    if (er != erSuccess) {
		ec_log_err("ECSearchFolders::ResetResults(): BEGIN failed 0x%x", er);
		goto exit;
	}
	
	er = lpDatabase->DoSelect("SELECT properties.val_ulong FROM properties WHERE hierarchyid = " + stringify(ulFolderId) + " FOR UPDATE", NULL);
	if (er != erSuccess) {
		ec_log_err("ECSearchFolders::ResetResults(): SELECT failed 0x%x", er);
		goto exit;
	}
        
    strQuery = "DELETE FROM searchresults WHERE folderid = " + stringify(ulFolderId);
    er = lpDatabase->DoDelete(strQuery);
    if(er != erSuccess) {
		ec_log_err("ECSearchFolders::ResetResults(): DELETE failed 0x%x", er);
		goto exit;
	}
        
	strQuery = "UPDATE properties SET val_ulong = 0 WHERE hierarchyid = " + stringify(ulFolderId) + " AND tag IN(" + stringify(PROP_ID(PR_CONTENT_COUNT)) + "," + stringify(PROP_ID(PR_CONTENT_UNREAD)) + ") AND type = " + stringify(PROP_TYPE(PR_CONTENT_COUNT));
	er = lpDatabase->DoUpdate(strQuery);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::ResetResults(): DoUpdate failed 0x%x", er);
		goto exit;
	}

	er = UpdateTProp(lpDatabase, PR_CONTENT_COUNT, ulParentId, ulFolderId);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::ResetResults(): UpdateTProp failed(1) 0x%x", er);
		goto exit;		
	}
		
	er = UpdateTProp(lpDatabase, PR_CONTENT_UNREAD, ulParentId, ulFolderId);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::ResetResults(): UpdateTProp failed(2) 0x%x", er);
		goto exit;
	}
	
	er = lpDatabase->Commit();
	if (er != erSuccess)
		ec_log_err("ECSearchFolders::ResetResults(): database commit failed 0x%x", er);
		
exit:
	if (er != erSuccess && lpDatabase)
		lpDatabase->Rollback();
        
    return er;
}

// Add a single search result message (eg one match in a search folder)
ECRESULT ECSearchFolders::AddResults(unsigned int ulStoreId, unsigned int ulFolderId, unsigned int ulObjId, unsigned int ulFlags, bool *lpfInserted)
{
    ECDatabase *lpDatabase = NULL;
    ECRESULT er = erSuccess;
    std::string strQuery;
    DB_RESULT lpDBResult = NULL;
    DB_ROW lpDBRow = NULL;
    
    ASSERT((ulFlags &~ MSGFLAG_READ) == 0);
    
    er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::AddResults(): GetThreadLocalDatabase failed 0x%x", er);
		goto exit;
	}

    strQuery = "SELECT flags FROM searchresults WHERE folderid = " + stringify(ulFolderId) + " AND hierarchyid = " + stringify(ulObjId);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::AddResults(): select searchresults failed 0x%x", er);
		goto exit;
	}
		
	lpDBRow = lpDatabase->FetchRow(lpDBResult);
	
	if(lpDBRow && lpDBRow[0] && atoui(lpDBRow[0]) == ulFlags) {
		// The record in the database is the same as what we're trying to insert; this is an error because we can't update or insert the record
		er = KCERR_NOT_FOUND;
		goto exit;
	}
        
	// This will either update or insert the record
    strQuery = "INSERT INTO searchresults (folderid, hierarchyid, flags) VALUES(" + stringify(ulFolderId) + "," + stringify(ulObjId) + "," + stringify(ulFlags) + ") ON DUPLICATE KEY UPDATE flags=" + stringify(ulFlags);
    er = lpDatabase->DoInsert(strQuery);
    if(er != erSuccess) {
		ec_log_err("ECSearchFolders::AddResults(): INSERT failed 0x%x", er);
		goto exit;
	}
        
	// We have inserted if the previous SELECT returned no row
	if (lpfInserted)
		*lpfInserted = (lpDBRow == NULL);
        
exit:
	if (lpDBResult)
		lpDatabase->FreeResult(lpDBResult);
        
    return er;
}

ECRESULT ECSearchFolders::AddResults(unsigned int ulStoreId, unsigned int ulFolderId, std::list<unsigned int> &lstObjId, std::list<unsigned int>& lstFlags, int *lpulCount, int *lpulUnread)
{
    ECDatabase *lpDatabase = NULL;
    ECRESULT er = erSuccess;
    std::string strQuery;
    unsigned int ulInserted = 0;
    unsigned int ulModified = 0;
    
    ASSERT(lstObjId.size() == lstFlags.size());
    
    if(lstObjId.empty())
        goto exit;
    
    er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::AddResults(): GetThreadLocalDatabase failed 0x%x", er);
		goto exit;
	}

    strQuery = "INSERT IGNORE INTO searchresults (folderid, hierarchyid, flags) VALUES";
    for (std::list<unsigned int>::const_iterator i = lstObjId.begin();
         i != lstObjId.end(); ++i)
    {
        strQuery += "(";
        strQuery += stringify(ulFolderId);
        strQuery += ",";
        strQuery += stringify(*i);
        strQuery += ",1),";
    }
    strQuery.resize(strQuery.size()-1);
    
    er = lpDatabase->DoInsert(strQuery, NULL, &ulInserted);
    if (er != erSuccess) {
		ec_log_err("ECSearchFolders::AddResults(): DoInsert failed 0x%x", er);
		goto exit;
	}

    /*
     * Combining the following queries in one query seems to cause MySQL to do a range- or gaplock, causing deadlocks
     * when folders with adjacent ids are updated at the same time.
     */
    for (std::list<unsigned int>::const_iterator i = lstFlags.begin(),
         j = lstObjId.begin(); i != lstFlags.end(); ++i, ++j)
    {
        if(*i == 0) {
            unsigned int modified = 0;
            
            strQuery = "UPDATE searchresults SET flags = 0 WHERE hierarchyid = " + stringify(*j) + " AND folderid = " + stringify(ulFolderId);
            er = lpDatabase->DoUpdate(strQuery, &modified);
            if (er != erSuccess) {
			ec_log_err("ECSearchFolders::AddResults(): UPDATE failed 0x%x", er);
			goto exit;
	}

            ulModified += modified;
        }
    }

    if(lpulCount)
        *lpulCount += ulInserted;
    if(lpulUnread)
        *lpulUnread += ulModified;
        
exit:
    return er;
}


// Remove a single search result (so one message in a search folder). Returns NOT_FOUND if the item wasn't in the database in the first place
ECRESULT ECSearchFolders::DeleteResults(unsigned int ulStoreId, unsigned int ulFolderId, unsigned int ulObjId, unsigned int *lpulOldFlags)
{
    ECDatabase *lpDatabase = NULL;
    ECRESULT er = erSuccess;
    std::string strQuery;
    DB_RESULT lpResult = NULL;
    DB_ROW lpRow = NULL;
    
    unsigned int ulAffected = 0;
    
	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		goto exit;

	if(lpulOldFlags) {
		strQuery = "SELECT flags FROM searchresults WHERE folderid=" + stringify(ulFolderId) + " AND hierarchyid=" + stringify(ulObjId);
		er = lpDatabase->DoSelect(strQuery, &lpResult);
		if(er != erSuccess) {
			ec_log_err("ECSearchFolders::DeleteResults(): SELECT failed 0x%x", er);
			goto exit;
		}
			
		lpRow = lpDatabase->FetchRow(lpResult);
		if(lpRow == NULL || lpRow[0] == NULL) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		
		*lpulOldFlags = atoui(lpRow[0]);
	}
        
    strQuery = "DELETE FROM searchresults WHERE folderid=" + stringify(ulFolderId) + " AND hierarchyid=" + stringify(ulObjId);
    er = lpDatabase->DoDelete(strQuery, &ulAffected);
    if(er != erSuccess) {
		ec_log_err("ECSearchFolders::DeleteResults(): DELETE failed 0x%x", er);
		goto exit;
	}
        
    if(ulAffected == 0) {
        er = KCERR_NOT_FOUND;
        goto exit;
    }
        
exit:
	if(lpResult)
		lpDatabase->FreeResult(lpResult);
		
    return er;
}

// Write the status of a search folder to the PR_EC_SEARCHFOLDER_STATUS property
ECRESULT ECSearchFolders::SetStatus(unsigned int ulFolderId, unsigned int ulStatus)
{
    ECDatabase *lpDatabase = NULL;
    ECRESULT er = erSuccess;
    std::string strQuery;
   
	// Do not use transactions because this function is called inside a transaction.
 
    er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::SetStatus(): GetThreadLocalDatabase failed 0x%x", er);
		goto exit;
	}
        
    // No record == running
    if(ulStatus != EC_SEARCHFOLDER_STATUS_RUNNING) {
        strQuery = "REPLACE INTO properties (tag, type, hierarchyid, val_ulong) "
                   "VALUES(" + stringify(PROP_ID(PR_EC_SEARCHFOLDER_STATUS)) + "," + 
                               stringify(PROP_TYPE(PR_EC_SEARCHFOLDER_STATUS)) + "," + 
                               stringify(ulFolderId) + "," + 
                               stringify(ulStatus) + ")";

        er = lpDatabase->DoInsert(strQuery);
        if(er != erSuccess) {
		ec_log_err("ECSearchFolders::SetStatus(): DoInsert failed 0x%x", er);
		goto exit;
	}
    } else {
		strQuery = "DELETE FROM properties "
					"WHERE hierarchyid=" + stringify(ulFolderId) +
					" AND tag=" + stringify(PROP_ID(PR_EC_SEARCHFOLDER_STATUS)) +
					" AND type=" + stringify(PROP_TYPE(PR_EC_SEARCHFOLDER_STATUS));

		er = lpDatabase->DoDelete(strQuery);
		if (er != erSuccess) {
			ec_log_err("ECSearchFolders::SetStatus(): DELETE failed 0x%x", er);
			goto exit;
	}
	}
        
exit:
	return er;
}

// Get all results of a certain search folder in a list of hierarchy IDs
ECRESULT ECSearchFolders::GetSearchResults(unsigned int ulStoreId, unsigned int ulFolderId,  std::list<unsigned int> *lstObjIds)
{
    ECDatabase *lpDatabase = NULL;
    DB_RESULT lpResult = NULL;
    DB_ROW lpRow = NULL;
    ECRESULT er = erSuccess;
    std::string strQuery;
    
    er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::GetSearchResults(): GetThreadLocalDatabase failed 0x%x", er);
		goto exit;
	}
        
    strQuery = "SELECT hierarchyid FROM searchresults WHERE folderid=" + stringify(ulFolderId);
    er = lpDatabase->DoSelect(strQuery, &lpResult);
    if(er != erSuccess) {
		ec_log_err("ECSearchFolders::GetSearchResults(): SELECT failed 0x%x", er);
		goto exit;
	}
        
    lstObjIds->clear();

    while(1) {
        lpRow = lpDatabase->FetchRow(lpResult);
        if(lpRow == NULL || lpRow[0] == NULL)
            break;

        lstObjIds->push_back(atoui(lpRow[0]));
    }
    
exit:
    if(lpResult)
        lpDatabase->FreeResult(lpResult);
        
    return er;
}

// Loads the search criteria from the database
ECRESULT ECSearchFolders::LoadSearchCriteria(unsigned int ulStoreId, unsigned int ulFolderId, struct searchCriteria **lppSearchCriteria)
{
    ECRESULT er = erSuccess;
	ECDatabase		*lpDatabase = NULL;
	DB_RESULT		lpDBResult = NULL;
	DB_ROW			lpDBRow = NULL;
	std::string		strQuery;
	struct searchCriteria *lpSearchCriteria = NULL;
	struct soap				xmlsoap;

    // Get database
    er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::LoadSearchCriteria(): GetThreadLocalDatabase failed 0x%x", er);
		goto exit;
	}

	// We use the soap serializer / deserializer to store the data
	soap_set_mode(&xmlsoap, SOAP_XML_TREE | SOAP_C_UTFSTRING);
	
	// Find out what kind of table this is
	strQuery = "SELECT hierarchy.flags, properties.val_string FROM hierarchy JOIN properties on hierarchy.id=properties.hierarchyid AND properties.tag =" + stringify(PROP_ID(PR_EC_SEARCHCRIT)) + " AND properties.type =" + stringify(PROP_TYPE(PR_EC_SEARCHCRIT)) + " WHERE hierarchy.id =" + stringify(ulFolderId);

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::LoadSearchCriteria(): SELECT failed 0x%x", er);
		goto exit;
	}

	lpDBRow = lpDatabase->FetchRow(lpDBResult);

	if(lpDBRow && lpDBRow[0] && atoi(lpDBRow[0]) == 2 && lpDBRow[1]) {
		std::string xmldata(lpDBRow[1]);
		std::istringstream xml(xmldata);

		lpSearchCriteria = new struct searchCriteria;

		xmlsoap.is = &xml;
		soap_default_searchCriteria(&xmlsoap, lpSearchCriteria);
		if (soap_begin_recv(&xmlsoap) != 0) {
			er = KCERR_NETWORK_ERROR;
			goto exit;
		}
		soap_get_searchCriteria(&xmlsoap, lpSearchCriteria, "SearchCriteria", NULL);

		// We now have the object, allocated by xmlsoap object,
		if (soap_end_recv(&xmlsoap) != 0)
			er = KCERR_NETWORK_ERROR;
		else
			er = CopySearchCriteria(NULL, lpSearchCriteria, lppSearchCriteria);

		/*
		 * We do not need the error here: lppSearchCriteria will not be
		 * touched, and we need to free the soap structs.
		 */
		soap_destroy(&xmlsoap);
		soap_end(&xmlsoap);
		soap_done(&xmlsoap);
	} else {
		er = KCERR_NOT_FOUND;
	}

exit:
	delete lpSearchCriteria;

	if(lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

    return er;
}

// Saves the search criteria in the database
ECRESULT ECSearchFolders::SaveSearchCriteria(unsigned int ulStoreId, unsigned int ulFolderId, struct searchCriteria *lpSearchCriteria)
{
    ECRESULT er = erSuccess;
	ECDatabase		*lpDatabase = NULL;

    // Get database
    er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess) {
		ec_log_crit("ECSearchFolders::SaveSearchCriteria(): GetThreadLocalDatabase failed 0x%x", er);
		goto exit;
	}
  
	er = lpDatabase->Begin();
	if(er != hrSuccess) {
		ec_log_err("ECSearchFolders::SaveSearchCriteria(): BEGIN failed 0x%x", er);
		goto exit;
	}

	er = SaveSearchCriteria(lpDatabase, ulStoreId, ulFolderId, lpSearchCriteria);
	if(er != hrSuccess) {
		ec_log_err("ECSearchFolders::SaveSearchCriteria(): SaveSearchCriteria failed 0x%x", er);
		goto exit;
	}

	er = lpDatabase->Commit();
	if (er != hrSuccess)
		ec_log_err("ECSearchFolders::SaveSearchCriteria(): commit failed 0x%x", er);

exit:
    if(lpDatabase && er != erSuccess)
        lpDatabase->Rollback();
    
    return er;
}

// Serialize and save the search criteria for a certain folder. The property is saved as a PR_EC_SEARCHCRIT property
ECRESULT ECSearchFolders::SaveSearchCriteria(ECDatabase *lpDatabase, unsigned int ulStoreId, unsigned int ulFolderId, struct searchCriteria *lpSearchCriteria)
{
    ECRESULT er = erSuccess;
	std::string		strQuery;

	struct soap				xmlsoap;
	struct searchCriteria	sSearchCriteria;
	std::ostringstream		xml;


	// We use the soap serializer / deserializer to store the data
	soap_set_mode(&xmlsoap, SOAP_XML_TREE | SOAP_C_UTFSTRING);

	sSearchCriteria.lpFolders = lpSearchCriteria->lpFolders;
	sSearchCriteria.lpRestrict = lpSearchCriteria->lpRestrict;
	sSearchCriteria.ulFlags = lpSearchCriteria->ulFlags;

	xmlsoap.os = &xml;
	soap_serialize_searchCriteria(&xmlsoap, &sSearchCriteria);
	soap_begin_send(&xmlsoap);
	soap_put_searchCriteria(&xmlsoap, &sSearchCriteria, "SearchCriteria",NULL);
	soap_end_send(&xmlsoap);

	// Make sure we're linking with the correct SOAP (c++ version)
	ASSERT(!xml.str().empty());

	// xml now contains XML version of search criteria

	// Replace PR_EC_SEARCHCRIT in database
	strQuery = "DELETE from properties WHERE properties.hierarchyid = " + stringify(ulFolderId) + " AND tag=" + stringify(PROP_ID(PR_EC_SEARCHCRIT)) + " AND type=" + stringify(PROP_TYPE(PR_EC_SEARCHCRIT));

	er = lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		goto exit;

	strQuery = "INSERT INTO properties (hierarchyid, tag, type, val_string) VALUES(" + stringify(ulFolderId) + "," + stringify(PROP_ID(PR_EC_SEARCHCRIT)) + "," + stringify(PROP_TYPE(PR_EC_SEARCHCRIT)) + ",'" + lpDatabase->Escape( xml.str() ) + "')";

	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		goto exit;
		
 exit:
    
    return er;
}

void ECSearchFolders::FlushAndWait()
{
	pthread_mutex_lock(&m_mutexEvents);
	pthread_cond_broadcast(&m_condEvents);
	pthread_mutex_unlock(&m_mutexEvents);
	// let ProcessThread get this lock, and mark the thread running
	pthread_mutex_lock(&m_mutexEvents);
	pthread_mutex_unlock(&m_mutexEvents);
	// wait for an inactive search thread
	while (m_bRunning) Sleep(10);
}

/*
 * This is the main processing thread, which processes changes from the queue. After processing it removes them from the queue and waits for
 * new events
 */
 
void * ECSearchFolders::ProcessThread(void *lpSearchFolders)
{
    ECSearchFolders *lpThis = (ECSearchFolders *)lpSearchFolders;
    
    while(1) {    
        // Get events to process
        pthread_mutex_lock(&lpThis->m_mutexEvents);
        
        if(lpThis->m_bExitThread) {
			pthread_mutex_unlock(&lpThis->m_mutexEvents);
            break;
		}

        if(lpThis->m_lstEvents.empty()) {
            // No events, wait until one arrives (the mutex is unlocked by pthread_cond_wait so people are able to add new
            // events). The condition also occurs when the server is exiting.
            pthread_cond_wait(&lpThis->m_condEvents, &lpThis->m_mutexEvents);
        }

		lpThis->m_bRunning = true;

        // The condition ended. Two things can have happened: there is now at least one event waiting, or and exit has been requested.
        // In both cases, we simply unlock the mutex and process any (may be 0) events currently in the queue. This means that the caller
        // must make sure that no new events can be added after the m_bThreadExit flag is set to TRUE.
        pthread_mutex_unlock(&lpThis->m_mutexEvents);
        
        lpThis->FlushEvents();
        Sleep(1000);
		lpThis->m_bRunning = false;

        // Check if we need to exit
    }
        
    return NULL;
}

typedef struct {
    bool operator () (const EVENT &a, const EVENT &b) { return a.ulFolderId < b.ulFolderId; } 
} FOLDERSORT;

// Process all waiting events in an efficient order
ECRESULT ECSearchFolders::FlushEvents()
{
    ECRESULT er = erSuccess;
    std::list<EVENT>::const_iterator iterEvents;
    std::list<EVENT> lstEvents;
    ECObjectTableList lstObjectIDs;
    sObjectTableKey sRow;
    FOLDERSORT sort;
    
    unsigned int ulStoreId = 0;
    unsigned int ulFolderId = 0;
    ECKeyTable::UpdateType ulType;
    
    // We do a copy-remove-process cycle here to keep the event queue locked for the least time as possible with
    // 500 events at a time
    pthread_mutex_lock(&m_mutexEvents);
    for (int i = 0; i < 500; ++i) {
        // Move the first element of m_lstEvents to the head of our list.
        if(m_lstEvents.empty())
            break;
        lstEvents.splice(lstEvents.end(), m_lstEvents, m_lstEvents.begin());
    }
    pthread_mutex_unlock(&m_mutexEvents);
    
    // Sort the items by folder. The order of DELETE and ADDs will remain unchanged. This is important
    // because the order of the incoming ADD or DELETE is obviously important for the final result.
    lstEvents.sort(sort);
    
    // Send the changes grouped by folder (and therefore also by store)
    ulStoreId = 0;
    ulFolderId = 0;
    ulType = ECKeyTable::TABLE_ROW_MODIFY;
    
    // Process changes by finding sequences of events of the same type (eg ADD ADD ADD DELETE will result in two sequences: 3xADD + 1xDELETE)
    for (iterEvents = lstEvents.begin(); iterEvents != lstEvents.end(); ++iterEvents) {
        if(iterEvents->ulFolderId != ulFolderId || iterEvents->ulType != ulType) {
            if(!lstObjectIDs.empty()) {
                // This is important: make the events unique. We need to do this because the ECStoreObjectTable
                // row engine does not support requesting the exact same row twice within the same call. If we have
                // duplicates here, this will filter through to the row engine and cause all kinds of nastiness, mainly
                // causing the item to be deleted from search folders irrespective of whether it should have been deleted
                // or added.
                lstObjectIDs.sort();
                lstObjectIDs.unique();
                
                ProcessMessageChange(ulStoreId, ulFolderId, &lstObjectIDs, ulType);
                lstObjectIDs.clear();
            }
        }
        ulStoreId = iterEvents->ulStoreId;
        ulFolderId = iterEvents->ulFolderId;
        ulType = iterEvents->ulType;
            
        sRow.ulObjId = iterEvents->ulObjectId;
        sRow.ulOrderId = 0;
        
        lstObjectIDs.push_back(sRow);
    }

    // Flush last set
    if(!lstObjectIDs.empty()) {
        // This is important: make the events unique. We need to do this because the ECStoreObjectTable
        // row engine does not support requesting the exact same row twice within the same call. If we have
        // duplicates here, this will filter through to the row engine and cause all kinds of nastiness, mainly
        // causing the item to be deleted from search folders irrespective of whether it should have been deleted
        // or added.
        lstObjectIDs.sort();
        lstObjectIDs.unique();
        ProcessMessageChange(ulStoreId, ulFolderId, &lstObjectIDs, ulType);
    }    
    
    return er;
}

/**
 * Get object statistics
 *
 * @param[out] sStats	Reference to searchfolder statistics
 *
 * @return This functions return always success
 */
ECRESULT ECSearchFolders::GetStats(sSearchFolderStats &sStats)
{
	STOREFOLDERIDSEARCH::const_iterator iterStoreFolder;
	FOLDERIDSEARCH::const_iterator iterFS;

	memset(&sStats, 0, sizeof(sSearchFolderStats));

	pthread_mutex_lock(&m_mutexMapSearchFolders);

	sStats.ulStores = m_mapSearchFolders.size();
	sStats.ullSize = sStats.ulStores * sizeof(STOREFOLDERIDSEARCH::value_type);

	for (iterStoreFolder = m_mapSearchFolders.begin();
	     iterStoreFolder != m_mapSearchFolders.end(); ++iterStoreFolder) {
		sStats.ulFolders+= iterStoreFolder->second.size();
		sStats.ullSize+= iterStoreFolder->second.size() * (sizeof(FOLDERIDSEARCH::value_type) + sizeof(SEARCHFOLDER));
		for (iterFS = iterStoreFolder->second.begin();
		     iterFS != iterStoreFolder->second.end(); ++iterFS)
			sStats.ullSize+= SearchCriteriaSize(iterFS->second->lpSearchCriteria);
	}
	
	pthread_mutex_unlock(&m_mutexMapSearchFolders);

	pthread_mutex_lock(&m_mutexEvents);
	sStats.ulEvents = m_lstEvents.size();
	pthread_mutex_unlock(&m_mutexEvents);

	sStats.ullSize += sStats.ulEvents * sizeof(EVENT);

	return erSuccess;
}
