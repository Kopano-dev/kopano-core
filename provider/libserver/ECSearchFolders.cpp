/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <utility>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <pthread.h>
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
#include "StatsClient.h"
#include "ECIndexer.h"
#include <kopano/ECTags.h>
#include "cmdutil.hpp"
#include <kopano/stringutil.h>
#include "ECSearchClient.h"

namespace KC {

struct THREADINFO {
	std::shared_ptr<SEARCHFOLDER> lpFolder;
    ECSearchFolders *lpSearchFolders;
};

ECSearchFolders::ECSearchFolders(ECSessionManager *lpSessionManager,
    ECDatabaseFactory *lpFactory) :
	m_lpDatabaseFactory(lpFactory), m_lpSessionManager(lpSessionManager)
{
	auto ret = pthread_create(&m_threadProcess, nullptr, ECSearchFolders::ProcessThread, this);
	if (ret != 0) {
		ec_log_err("Could not create ECSearchFolders thread: %s", strerror(ret));
		return;
	}
	m_thread_active = true;
    set_thread_name(m_threadProcess, "SearchFolders");
}

ECSearchFolders::~ECSearchFolders() {
	ulock_rec l_sf(m_mutexMapSearchFolders);
	m_mapSearchFolders.clear();
	l_sf.unlock();

	ulock_rec l_events(m_mutexEvents);
    m_bExitThread = true;
	m_condEvents.notify_all();
	l_events.unlock();
	if (m_thread_active)
		pthread_join(m_threadProcess, nullptr);
}

// Only loads the search criteria for all search folders. Used once at boot time
ECRESULT ECSearchFolders::LoadSearchFolders()
{
    ECDatabase *lpDatabase = NULL;
	DB_RESULT lpResult;
    DB_ROW lpRow = NULL;
	unsigned int ulStoreId = 0;

    // Search for all folders with PR_EC_SEARCHCRIT that are not deleted. Note that this query can take quite some time on large databases
	auto strQuery = "SELECT h.id, p.val_ulong, p2.val_string FROM hierarchy AS h"
		" LEFT JOIN properties AS p ON h.id=p.hierarchyid AND p.tag=" + stringify(PROP_ID(PR_EC_SEARCHFOLDER_STATUS)) + " AND p.type=" + stringify(PROP_TYPE(PR_EC_SEARCHFOLDER_STATUS)) +
		" LEFT JOIN properties AS p2 ON h.id=p2.hierarchyid AND p2.tag=" + stringify(PROP_ID(PR_EC_SEARCHCRIT)) + " AND p2.type=" + stringify(PROP_TYPE(PR_EC_SEARCHCRIT)) +
		" WHERE h.type=3 AND h.flags=2";
    struct searchCriteria *lpSearchCriteria = NULL;

    // Get database
	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess)
		return er;
	ec_log_notice("Querying database for searchfolders. This may take a while.");
    er = lpDatabase->DoSelect(strQuery, &lpResult);
    if(er != erSuccess)
		return er;

	ec_log_notice("Loading search folders.");
	while ((lpRow = lpResult.fetch_row()) != nullptr) {
		if (lpRow[0] == nullptr || lpRow[2] == nullptr)
			continue;
		unsigned int ulStatus = (lpRow[1] == nullptr) ? EC_SEARCHFOLDER_STATUS_RUNNING : atoui(lpRow[1]);
		if (ulStatus == EC_SEARCHFOLDER_STATUS_STOPPED)
			/* Only load the table if it is not stopped */
			continue;
		auto ulFolderId = atoi(lpRow[0]);
        er = m_lpSessionManager->GetCacheManager()->GetStore(ulFolderId, &ulStoreId, NULL);
        if(er != erSuccess) {
            er = erSuccess;
            continue;
        }
		er = LoadSearchCriteria2(lpRow[2], &lpSearchCriteria);
		if (er != erSuccess) {
			er = erSuccess;
			continue;
		}

		if (ulStatus == EC_SEARCHFOLDER_STATUS_REBUILD)
			ec_log_info("Rebuilding search folder %d", ulFolderId);
		// If the folder was in the process of rebuilding, then completely rebuild the search results (we don't know how far the search got)
		er = AddSearchFolder(ulStoreId, ulFolderId, ulStatus == EC_SEARCHFOLDER_STATUS_REBUILD, lpSearchCriteria);
		if (er != erSuccess) {
			er = erSuccess; // just try to skip the error
			continue;
		}
		FreeSearchCriteria(lpSearchCriteria);
		lpSearchCriteria = nullptr;
    }

    if(lpSearchCriteria)
        FreeSearchCriteria(lpSearchCriteria);
	ec_log_notice("Done loading search folders.");
    return er;
}

// Called from IMAPIContainer::SetSearchCriteria
ECRESULT ECSearchFolders::SetSearchCriteria(unsigned int ulStoreId,
    unsigned int ulFolderId, const struct searchCriteria *lpSearchCriteria)
{
    if(lpSearchCriteria == NULL) {
        /* Always return successful, so that Outlook 2007 works */
        CancelSearchFolder(ulStoreId, ulFolderId);
        return erSuccess;
	}
	auto er = AddSearchFolder(ulStoreId, ulFolderId, true, lpSearchCriteria);
	if (er != erSuccess)
		return er;
	return SaveSearchCriteria(ulFolderId, lpSearchCriteria);
}

// Gets the search criteria from in-memory
ECRESULT ECSearchFolders::GetSearchCriteria(unsigned int ulStoreId, unsigned int ulFolderId, struct searchCriteria **lppSearchCriteria, unsigned int *lpulFlags)
{
	scoped_rlock l_sf(m_mutexMapSearchFolders);

    // See if there are any searches for this store first
	auto iterStore = m_mapSearchFolders.find(ulStoreId);
	if (iterStore == m_mapSearchFolders.cend())
		return KCERR_NOT_INITIALIZED;
	FOLDERIDSEARCH::const_iterator iterFolder = iterStore->second.find(ulFolderId);
	if (iterFolder == iterStore->second.cend())
		return KCERR_NOT_INITIALIZED;
	auto er = CopySearchCriteria(nullptr, iterFolder->second->lpSearchCriteria, lppSearchCriteria);
    if(er != erSuccess)
		return er;
    er = GetState(ulStoreId, ulFolderId, lpulFlags);
    if(er != erSuccess)
		return er;
    // Add recursive flag if necessary
    *lpulFlags |= iterFolder->second->lpSearchCriteria->ulFlags & SEARCH_RECURSIVE;
    return er;
}

// Add or modify a search folder
ECRESULT ECSearchFolders::AddSearchFolder(unsigned int ulStoreId,
    unsigned int ulFolderId, bool bReStartSearch,
    const struct searchCriteria *lpSearchCriteria)
{
    struct searchCriteria *lpCriteria = NULL;
    unsigned int ulParent = 0;
	ulock_rec l_sf(m_mutexMapSearchFolders, std::defer_lock_t());
	auto cleanup = make_scope_success([&]() { FreeSearchCriteria(lpCriteria); });

    if(lpSearchCriteria == NULL) {
		auto er = LoadSearchCriteria(ulFolderId, &lpCriteria);
        if(er != erSuccess)
		return er;
        lpSearchCriteria = lpCriteria;
    }

    // Cancel any running searches
    CancelSearchFolder(ulStoreId, ulFolderId);
	auto cache = m_lpSessionManager->GetCacheManager();
    if(bReStartSearch) {
        // Set the status of the table as rebuilding
        SetStatus(ulFolderId, EC_SEARCHFOLDER_STATUS_REBUILD);
        // Remove any results for this folder if we're restarting the search
		auto er = ResetResults(ulFolderId);
        if(er != erSuccess)
			return er;
    }

    // Tell tables that we've reset:

    // 1. Reset cache for this folder
	cache->Update(fnevObjectModified, ulFolderId);
    // 2. Send reset table contents notification
	auto er = m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE, 0, ulFolderId, 0, MAPI_MESSAGE);
    if(er != erSuccess)
		return er;
    // 3. Send change tables viewing us (hierarchy tables)
	if (cache->GetParent(ulFolderId, &ulParent) == erSuccess) {
        er = m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParent, ulFolderId, MAPI_FOLDER);
        if(er != erSuccess)
			return er;
    }

	std::shared_ptr<SEARCHFOLDER> lpSearchFolder(new(std::nothrow) SEARCHFOLDER(ulStoreId, ulFolderId));
	if (lpSearchFolder == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
    er = CopySearchCriteria(NULL, lpSearchCriteria, &lpSearchFolder->lpSearchCriteria);
	if (er != erSuccess)
		return er;

    // Get searches for this store, or add it to the list.
	l_sf.lock();
	auto iterStore = m_mapSearchFolders.emplace(ulStoreId, FOLDERIDSEARCH()).first;
	iterStore->second.emplace(ulFolderId, lpSearchFolder);
	g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_COUNT);
	if (!bReStartSearch)
		return erSuccess;
	lpSearchFolder->bThreadFree = false;
	auto ti = make_unique_nt<THREADINFO>();
	if (ti == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	ti->lpSearchFolders = this;
	ti->lpFolder = lpSearchFolder;
	// Insert the actual folder with the criteria
	// Start the thread (will store the thread id in the original list)
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attr, 512*1024); // 512KB stack space for search threads
	auto err = pthread_create(&lpSearchFolder->sThreadId, &attr, ECSearchFolders::SearchThread, ti.get());
	if (err != 0) {
		ec_log_crit("Unable to spawn thread for search: %s", strerror(err));
		return KCERR_NOT_ENOUGH_MEMORY;
	} else {
		ti.release();
	}
	set_thread_name(lpSearchFolder->sThreadId, "SearchFolders:Folder");
	l_sf.unlock();
	return er;
}

// Cancel a search: stop any rebuild thread and stop processing updates for this search folder
ECRESULT ECSearchFolders::CancelSearchFolder(unsigned int ulStoreID, unsigned int ulFolderId)
{
    // Lock the list, preventing other Cancel requests messing with the thread
	ulock_rec l_sf(m_mutexMapSearchFolders);

	auto iterStore = m_mapSearchFolders.find(ulStoreID);
	if (iterStore == m_mapSearchFolders.cend())
		return KCERR_NOT_FOUND;
	auto iterFolder = iterStore->second.find(ulFolderId);
	if (iterFolder == iterStore->second.cend())
		return KCERR_NOT_FOUND;

	auto lpFolder = iterFolder->second;
    // Remove the item from the list
    iterStore->second.erase(iterFolder);
	g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_COUNT, -1);
	l_sf.unlock();
	DestroySearchFolder(std::move(lpFolder));
	return erSuccess;
}

void ECSearchFolders::DestroySearchFolder(std::shared_ptr<SEARCHFOLDER> &&lpFolder)
{
	unsigned int ulFolderId = lpFolder->ulFolderId;
    // Nobody can access lpFolder now, except for us and the search thread
    // FIXME check this assumption !!!
    // Signal the thread to exit
    lpFolder->bThreadExit = true;
	/*
	 * Wait for the thread to signal that lpFolder is no longer in use by
	 * the thread The condition is used for all threads, so it may have
	 * been fired for a different thread. This is efficient enough.
	 */
	ulock_normal lk(lpFolder->mMutexThreadFree);
	m_condThreadExited.wait(lk, [=](void) { return lpFolder->bThreadFree; });
	lk.unlock();
	lpFolder.reset();
    // Set the search as stopped in the database
    SetStatus(ulFolderId, EC_SEARCHFOLDER_STATUS_STOPPED);
}

/**
 * Cancel all the search folders on a store and removing the results
 */
ECRESULT ECSearchFolders::RemoveSearchFolder(unsigned int ulStoreID)
{
	std::list<std::shared_ptr<SEARCHFOLDER>> listSearchFolders;
	// Lock the list, preventing other Cancel requests messing with the thread
	ulock_rec l_sf(m_mutexMapSearchFolders);

	auto iterStore = m_mapSearchFolders.find(ulStoreID);
	if (iterStore == m_mapSearchFolders.cend())
		return KCERR_NOT_FOUND;
	for (const auto &p : iterStore->second)
		listSearchFolders.emplace_back(p.second);
	iterStore->second.clear();

	// Remove store from list, items of the store will be delete in 'DestroySearchFolder'
	m_mapSearchFolders.erase(iterStore);
	l_sf.unlock();

//@fixme: server shutdown can result in a crash?
	for (auto srfolder : listSearchFolders) {
		g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_COUNT, -1);
		auto ulFolderID = srfolder->ulFolderId;
		// Wait and free searchfolder data
		DestroySearchFolder(std::move(srfolder));
		// Remove results from database
		ResetResults(ulFolderID);
	}
	return erSuccess;
}

// Removing a search folder is subtly different from cancelling it; removing a search folder
// also removes all search results
ECRESULT ECSearchFolders::RemoveSearchFolder(unsigned int ulStoreId, unsigned int ulFolderId)
{
    // Cancel any running (rebuilding) searches
    CancelSearchFolder(ulStoreId, ulFolderId);
    // Ignore errors
    // Remove results from database
    ResetResults(ulFolderId);
	return erSuccess;
}

// WARNING: THIS FUNCTION IS *NOT* THREADSAFE. IT SHOULD ONLY BE CALLED AT STARTUP WHILE SINGLE-THREADED
ECRESULT ECSearchFolders::RestartSearches()
{
    ec_log_crit("Starting rebuild of search folders... This may take a while.");

    for (const auto &store_p : m_mapSearchFolders) {
        ec_log_crit("  Rebuilding searchfolders of store %d", store_p.first);
        for (const auto &folder_p : store_p.second) {
            ResetResults(folder_p.first);
            Search(store_p.first, folder_p.first, folder_p.second->lpSearchCriteria, NULL, false);
        }
    }

    ec_log_info("Finished rebuild.");
	return erSuccess;
}

// Should be called for *any* change of *any* message object in the database.
ECRESULT ECSearchFolders::UpdateSearchFolders(unsigned int ulStoreId, unsigned int ulFolderId, unsigned int ulObjId, ECKeyTable::UpdateType ulType)
{
    EVENT ev;

    ev.ulStoreId = ulStoreId;
    ev.ulFolderId = ulFolderId;
    ev.ulObjectId = ulObjId;
    ev.ulType = ulType;

	scoped_rlock l_ev(m_mutexEvents);
    // Add the event to the queue
	m_lstEvents.emplace_back(std::move(ev));
	/*
	 * Signal a change in the queue (actually only needed for the first
	 * event, but this wastes almost no time and is safer.
	 */
	m_condEvents.notify_all();
	return erSuccess;
}

// Process a list of message changes in a single folder of a certain type
ECRESULT ECSearchFolders::ProcessMessageChange(unsigned int ulStoreId, unsigned int ulFolderId, ECObjectTableList *lstObjectIDs, ECKeyTable::UpdateType ulType)
{
    std::set<unsigned int> setParents;
    ECSession *lpSession = NULL;
	ECODStore ecOBStore;
    struct rowSet *lpRowSet = NULL;
    struct propTagArray *lpPropTags = NULL;
	unsigned int ulOwner = 0, ulParent = 0, ulFlags = 0, ulSCFolderId = 0;
	ECDatabase *lpDatabase = NULL;
	std::list<ULONG> lstPrefix;
	bool fInserted = false;

	lstPrefix.emplace_back(PR_MESSAGE_FLAGS);
	ECLocale locale = m_lpSessionManager->GetSortLocale(ulStoreId);
	ulock_rec l_sf(m_mutexMapSearchFolders);

	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess)
		return er;
	STOREFOLDERIDSEARCH::const_iterator iterStore = m_mapSearchFolders.find(ulStoreId);
    if (iterStore == m_mapSearchFolders.cend())
        // There are no search folders in the target store. We will therefore never match any search
        // result and might as well exit now.
		return erSuccess;

    // OPTIMIZATION: if a target folder == root folder of ulStoreId, and a recursive searchfolder, then
    // the following check is always TRUE
    // Get the owner of the search folder. This *could* be different from the owner of the objects!
    er = m_lpSessionManager->GetCacheManager()->GetObject(ulStoreId, NULL, &ulOwner, NULL, NULL);
    if(er != erSuccess)
		return er;

    // FIXME FIXME FIXME we still need to check MAPI_ASSOCIATED and MSGFLAG_DELETED and exclude them.. better if the caller does this.
    // We now have to see if the folder in which the object resides is actually a target of a search folder.
    // We do this by checking whether the specified folder is a searchfolder target folder, or a child of
    // a target folder if it is a recursive search.
    // Loop through search folders for this store
	auto cache = m_lpSessionManager->GetCacheManager();
	for (const auto &folder : iterStore->second) {
		ULONG ulAttempts = 4;	// Random number

		do {
			bool bIsInTargetFolder = false;
			int lCount = 0; /* Number of messages added, positive means more added, negative means more discarded */
			int lUnreadCount = 0; /* Same, but for unread count */

			if (folder.second->lpSearchCriteria->lpFolders == NULL ||
			    folder.second->lpSearchCriteria->lpRestrict == NULL)
				continue;
			auto dtx = lpDatabase->Begin(er);
			if (er != erSuccess)
				goto exit;

			// Lock searchfolder
			WITH_SUPPRESSED_LOGGING(lpDatabase)
				er = lpDatabase->DoSelect("SELECT properties.val_ulong FROM properties WHERE hierarchyid = " +
				     stringify(folder.first) + " FOR UPDATE", NULL);
			if (er == KCERR_DATABASE_ERROR) {
				DB_ERROR dberr = lpDatabase->GetLastError();
				if (dberr != DB_E_LOCK_WAIT_TIMEOUT && dberr != DB_E_LOCK_DEADLOCK) {
					ec_log_err("ECSearchFolders::ProcessMessageChange(): select failed");
					goto exit;
				}
				er = dtx.rollback();
				if (er != erSuccess) {
					ec_log_crit("ECSearchFolders::ProcessMessageChange(): database rollback failed %d", er);
					goto exit;
				}
				g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_UPDATE_RETRY);
				continue;
			} else if (er != erSuccess) {
				ec_log_crit("ECSearchFolders::ProcessMessageChange(): unexpected error %d", er);
				goto exit;
			}

			if(ulType != ECKeyTable::TABLE_ROW_DELETE) {
				// Loop through all targets for each searchfolder, if one matches, then match the restriction with the objects
				for (unsigned int i = 0; i < folder.second->lpSearchCriteria->lpFolders->__size; ++i)
					if (cache->GetObjectFromEntryId(&folder.second->lpSearchCriteria->lpFolders->__ptr[i], &ulSCFolderId) == erSuccess &&
						ulSCFolderId == ulFolderId)
					{
						bIsInTargetFolder = true;
						break;
					}

				if (!bIsInTargetFolder && folder.second->lpSearchCriteria->ulFlags & RECURSIVE_SEARCH) {
					// The item is not in one of the base folders, but it may be in one of children of the folders.
					// We do it in this order because the GetParent() calls below may cause database accesses, so
					// we only actually do those database accesses if we have to.
					unsigned int ulAncestor = ulFolderId;

					// Get all the parents of this object (usually around 5 or 6)
					setParents.emplace(ulFolderId);

					while(1) {
						er = cache->GetParent(ulAncestor, &ulAncestor);
						if(er != erSuccess)
							break;
						setParents.emplace(ulAncestor);
					}

					// setParents now contains all the parent of this object, now we can check if any of the ancestors
					// are in the search target
					for (unsigned int i = 0; i < folder.second->lpSearchCriteria->lpFolders->__size; ++i) {
						if (cache->GetObjectFromEntryId(&folder.second->lpSearchCriteria->lpFolders->__ptr[i], &ulSCFolderId) != erSuccess)
							continue;
						auto iterParents = setParents.find(ulSCFolderId);
						if (iterParents != setParents.cend()) {
							bIsInTargetFolder = true;
							break;
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
					lpSession->lock();
				}

				ecOBStore.ulStoreId = ulStoreId;
				ecOBStore.ulFolderId = 0;
				ecOBStore.ulFlags = 0;
				ecOBStore.ulObjType = MAPI_MESSAGE;
				ecOBStore.lpGuid = NULL;

				if(ulType == ECKeyTable::TABLE_ROW_ADD || ulType == ECKeyTable::TABLE_ROW_MODIFY) {
					std::list<ULONG> lstPropTags;

					// Get the restriction ready for this search folder
					er = ECGenericObjectTable::GetRestrictPropTags(folder.second->lpSearchCriteria->lpRestrict, &lstPrefix, &lpPropTags);
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
					SUBRESTRICTIONRESULTS sub_results;
					er = RunSubRestrictions(lpSession, &ecOBStore, folder.second->lpSearchCriteria->lpRestrict, lstObjectIDs, locale, sub_results);
					if(er != erSuccess) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): RunSubRestrictions failed %d", er);
						goto exit;
					}

					auto iterObjectIDs = lstObjectIDs->cbegin();
					// Check if the item matches for each item
					for (gsoap_size_t i = 0; i < lpRowSet->__size; ++i, ++iterObjectIDs) {
						bool fMatch;

						// Match the restriction
						er = ECGenericObjectTable::MatchRowRestrict(cache, &lpRowSet->__ptr[i], folder.second->lpSearchCriteria->lpRestrict, &sub_results, locale, &fMatch);
						if (er != erSuccess)
							continue;
						if (fMatch) {
							if(lpRowSet->__ptr[i].__ptr[0].ulPropTag != PR_MESSAGE_FLAGS)
								continue;

							// Get the read flag for this message
							ulFlags = lpRowSet->__ptr[i].__ptr[0].Value.ul & MSGFLAG_READ;

							// Update on-disk search folder
							if (AddResults(folder.first, iterObjectIDs->ulObjId, ulFlags, &fInserted) == erSuccess) {
								if(fInserted) {
									// One more match
									++lCount;
									if(!ulFlags)
										++lUnreadCount;
									// Send table notification
									m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD, 0, folder.first, iterObjectIDs->ulObjId, MAPI_MESSAGE);
								} else {
									// Row was modified, so flags has changed. Since the only possible values are MSGFLAG_READ or 0, we know the new flags.
									if(ulFlags)
										--lUnreadCount; // New state is read, so old state was unread, so --unread
									else
										++lUnreadCount; // New state is unread, so old state was read, so ++unread
									// Send table notification
									m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, folder.first, iterObjectIDs->ulObjId, MAPI_MESSAGE);
								}
							} else {
								// AddResults will return an error if the call didn't do anything (record was already in the table).
								// Even though, we should still send notifications since the row changed
								m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, folder.first, iterObjectIDs->ulObjId, MAPI_MESSAGE);
							}
						} else if (ulType == ECKeyTable::TABLE_ROW_MODIFY) {
							// Only delete modified items, not new items
							if (DeleteResults(folder.first, iterObjectIDs->ulObjId, &ulFlags) == erSuccess) {
								--lCount;
								if(!ulFlags)
									--lUnreadCount; // Removed message was unread
								m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE, 0, folder.first, iterObjectIDs->ulObjId, MAPI_MESSAGE);
							}
						}
						// Ignore errors from the updates
						er = erSuccess;
					}
				} else {
					// Message was deleted anyway, update on-disk search folder and send table notification
					for (const auto &obj_id : *lstObjectIDs)
						if (DeleteResults(folder.first, obj_id.ulObjId, &ulFlags) == erSuccess) {
							m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE, 0, folder.first, obj_id.ulObjId, MAPI_MESSAGE);
							--lCount;
							if(!ulFlags)
								--lUnreadCount; // Removed message was unread
						}
				}

				FreePropTagArray(lpPropTags);
				lpPropTags = nullptr;
				FreeRowSet(lpRowSet, true);
				lpRowSet = nullptr;
			} else {
				// Not in a target folder, remove from search results
				for (const auto &obj_id : *lstObjectIDs)
					if (DeleteResults(folder.first, obj_id.ulObjId, &ulFlags) == erSuccess) {
						m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE, 0, folder.first, obj_id.ulObjId, MAPI_MESSAGE);
						--lCount;
						if(!ulFlags)
							--lUnreadCount; // Removed message was unread
					}
			}

			if(lCount || lUnreadCount) {
				// If the searchfolder has changed, update counts and send notifications
				WITH_SUPPRESSED_LOGGING(lpDatabase) {
					er = UpdateFolderCount(lpDatabase, folder.first, PR_CONTENT_COUNT, lCount);
					if (er == erSuccess)
						er = UpdateFolderCount(lpDatabase, folder.first, PR_CONTENT_UNREAD, lUnreadCount);
				}

				if (er == KCERR_DATABASE_ERROR) {
					DB_ERROR dberr = lpDatabase->GetLastError();
					if (dberr == DB_E_LOCK_WAIT_TIMEOUT || dberr == DB_E_LOCK_DEADLOCK) {
						ec_log_crit("ECSearchFolders::ProcessMessageChange(): database error(1) %d", dberr);
						er = dtx.rollback();
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

				cache->Update(fnevObjectModified, folder.first);
				er = m_lpSessionManager->NotificationModified(MAPI_FOLDER, folder.first);
				if (cache->GetParent(folder.first, &ulParent) == erSuccess)
					er = m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParent, folder.first, MAPI_FOLDER);
				er = erSuccess; // Ignore errors
			}

			er = dtx.commit();
			if (er == KCERR_DATABASE_ERROR) {
				DB_ERROR dberr = lpDatabase->GetLastError();
				if (dberr == DB_E_LOCK_WAIT_TIMEOUT || dberr == DB_E_LOCK_DEADLOCK) {
					ec_log_crit("ECSearchFolders::ProcessMessageChange(): database error(2) %d", dberr);
					er = dtx.rollback();
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
			assert(er != KCERR_DATABASE_ERROR);
			g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_UPDATE_FAIL);
		}
    }
 exit:
	l_sf.unlock();
    if(lpPropTags)
        FreePropTagArray(lpPropTags);
    if(lpSession) {
		lpSession->unlock();
        m_lpSessionManager->RemoveSessionInternal(lpSession);
    }
    if(lpRowSet)
        FreeRowSet(lpRowSet, true);
    return er;
}

ECRESULT ECSearchFolders::ProcessCandidateRowsNotify(ECDatabase *lpDatabase,
    ECSession *lpSession, const struct restrictTable *lpRestrict, bool *lpbCancel,
    unsigned int ulStoreId, unsigned int ulFolderId, ECODStore *lpODStore,
    ECObjectTableList ecRows, struct propTagArray *lpPropTags,
    const ECLocale &locale)
{
	unsigned int ulParent = 0;
	auto cache = m_lpSessionManager->GetCacheManager();
	ECRESULT er = erSuccess;
	auto dtx = lpDatabase->Begin(er);
	if (er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() BEGIN failed %d", er);
		return er;
	}
	er = lpDatabase->DoSelect("SELECT properties.val_ulong FROM properties WHERE hierarchyid = " + stringify(ulFolderId) + " FOR UPDATE", NULL);
	if (er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() SELECT failed %d", er);
		return er;
	}

	std::list<unsigned int> lst;
	er = ProcessCandidateRows(lpDatabase, lpSession, lpRestrict, lpbCancel, ulStoreId, ulFolderId, lpODStore, ecRows, lpPropTags, locale, lst);
	if (er != erSuccess)
		return er;
	er = dtx.commit();
	if (er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() DB commit failed %d", er);
		return er;
	}

	// Add matched row and send a notification to update views of this search (if any are open)
	m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD, 0, ulFolderId, lst, MAPI_MESSAGE);
	cache->Update(fnevObjectModified, ulFolderId);
	m_lpSessionManager->NotificationModified(MAPI_FOLDER, ulFolderId); // folder has modified due to PR_CONTENT_*
	if (cache->GetParent(ulFolderId, &ulParent) == erSuccess)
		m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParent, ulFolderId, MAPI_FOLDER); // PR_CONTENT_* has changed in tables too
	return er;
}

ECRESULT ECSearchFolders::ProcessCandidateRows(ECDatabase *lpDatabase,
    ECSession *lpSession, const struct restrictTable *lpRestrict, bool *lpbCancel,
    unsigned int ulStoreId, unsigned int ulFolderId, ECODStore *lpODStore,
    ECObjectTableList ecRows, struct propTagArray *lpPropTags,
    const ECLocale &locale)
{
	std::list<unsigned int> lst;
	return ProcessCandidateRows(lpDatabase, lpSession, lpRestrict, lpbCancel, ulStoreId, ulFolderId, lpODStore, ecRows, lpPropTags, locale, lst);
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
    ECSession *lpSession, const struct restrictTable *lpRestrict, bool *lpbCancel,
    unsigned int ulStoreId, unsigned int ulFolderId, ECODStore *lpODStore,
    ECObjectTableList ecRows, struct propTagArray *lpPropTags,
    const ECLocale &locale, std::list<unsigned int> &lstMatches)
{
	struct rowSet *lpRowSet = NULL;
	bool fMatch = false;
	std::list<unsigned int> lstFlags;
	SUBRESTRICTIONRESULTS sub_results;

	assert(lpPropTags->__ptr[0] == PR_MESSAGE_FLAGS);
	auto iterRows = ecRows.cbegin();
	auto cache = lpSession->GetSessionManager()->GetCacheManager();

    // Get the row data for the search
	auto cleanup = make_scope_success([&]() { FreeRowSet(lpRowSet, true); });
	auto er = ECStoreObjectTable::QueryRowData(nullptr, nullptr, lpSession, &ecRows, lpPropTags, lpODStore, &lpRowSet, false, false);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() ECStoreObjectTable::QueryRowData failed %d", er);
		return er;
	}
    // Get the subrestriction results for the search
    er = RunSubRestrictions(lpSession, lpODStore, lpRestrict, &ecRows, locale, sub_results);
	if(er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() RunSubRestrictions failed %d", er);
		return er;
	}

    // Loop through the results data
	int lCount = 0, lUnreadCount = 0;
    for (gsoap_size_t j = 0; j< lpRowSet->__size && (!lpbCancel || !*lpbCancel); ++j, ++iterRows) {
		if (ECGenericObjectTable::MatchRowRestrict(cache, &lpRowSet->__ptr[j], lpRestrict, &sub_results, locale, &fMatch) != erSuccess)
            continue;
        if(!fMatch)
            continue;
        if(lpRowSet->__ptr[j].__ptr[0].ulPropTag != PR_MESSAGE_FLAGS)
            continue;
		unsigned int ulObjFlags = lpRowSet->__ptr[j].__ptr[0].Value.ul & MSGFLAG_READ;
		lstMatches.emplace_back(iterRows->ulObjId);
		lstFlags.emplace_back(ulObjFlags);
    }

    // Add matched row to database
    er = AddResults(ulFolderId, lstMatches, lstFlags, &lCount, &lUnreadCount);
	if (er != erSuccess) {
		ec_log_err("ECSearchFolders::ProcessCandidateRows() AddResults failed %d", er);
		return er;
	}
	if (lCount == 0 && lUnreadCount == 0)
		return erSuccess;
	er = UpdateFolderCount(lpDatabase, ulFolderId, PR_CONTENT_COUNT, lCount);
	if (er != erSuccess) {
		ec_log_crit("ECSearchFolders::ProcessCandidateRows() UpdateFolderCount failed(1) %d", er);
		return er;
	}
	er = UpdateFolderCount(lpDatabase, ulFolderId, PR_CONTENT_UNREAD, lUnreadCount);
	if (er != erSuccess)
		ec_log_crit("ECSearchFolders::ProcessCandidateRows() UpdateFolderCount failed(2) %d", er);
    return er;
}

// Does an actual search of a specific search Criteria in store ulStoreId, search folder ulFolderId. Will cancel if *lpbCancel
// is TRUE. We check after each message row set to see if the cancel has been requested.
ECRESULT ECSearchFolders::Search(unsigned int ulStoreId, unsigned int ulFolderId,
    const struct searchCriteria *lpSearchCrit, bool *lpbCancel, bool bNotify)
{
	ECListInt			lstFolders;
	ECObjectTableList ecRows;
	ECODStore ecODStore;
	ECSession *lpSession = NULL;
	unsigned int ulUserId = 0;
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	struct restrictTable *lpAdditionalRestrict = NULL;
	std::string suggestion;
	//Indexer
	std::list<unsigned int> lstIndexerResults;
	GUID guidServer, guidStore;

    ecODStore.ulStoreId = ulStoreId;
    ecODStore.ulFolderId = 0;
    ecODStore.ulFlags = 0;
    ecODStore.ulObjType = 0;
    ecODStore.lpGuid = NULL; // FIXME: optimisation possible

    if(lpSearchCrit->lpFolders == NULL || lpSearchCrit->lpRestrict == NULL) {
	ec_log_err("ECSearchFolders::Search() no folder or search criteria");
		return KCERR_NOT_FOUND;
    }
    auto cache = m_lpSessionManager->GetCacheManager();
	auto er = cache->GetStore(ulStoreId, nullptr, &guidStore);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() GetStore failed", er);
	ecODStore.lpGuid = &guidStore;

    // Get the owner of the store
    er = cache->GetObject(ulStoreId, NULL, &ulUserId, NULL, NULL);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() GetObject failed", er);
    // Create a session with the security credentials of the owner of the store
    er = m_lpSessionManager->CreateSessionInternal(&lpSession, ulUserId);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() CreateSessionInternal failed", er);
	lpSession->lock();
	auto cleanup = make_scope_success([&]() {
		lpSession->unlock();
		m_lpSessionManager->RemoveSessionInternal(lpSession);
		FreeRestrictTable(lpAdditionalRestrict);
	});
	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() GetDatabase failed", er);
    // Get target folders
	er = cache->GetEntryListToObjectList(lpSearchCrit->lpFolders, &lstFolders);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() GetEntryListToObjectList failed", er);
    // Reset search results in database
    er = ResetResults(ulFolderId);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() ResetResults failed", er);

	// Expand target folders if recursive
    if(lpSearchCrit->ulFlags & RECURSIVE_SEARCH) {
		for (auto iterFolders = lstFolders.cbegin();
		     iterFolders != lstFolders.cend(); ++iterFolders) {
			std::string strQuery = "SELECT hierarchy.id from hierarchy WHERE hierarchy.parent = " + stringify(*iterFolders) + " AND hierarchy.type=3 AND hierarchy.flags & " + stringify(MSGFLAG_DELETED|MSGFLAG_ASSOCIATED) + " = 0 ORDER by hierarchy.id DESC";
			er = lpDatabase->DoSelect(strQuery, &lpDBResult);
			if (er != erSuccess) {
				ec_perror("ECSearchFolders::Search() could not expand target folders", er);
				continue;
			}
			while ((lpDBRow = lpDBResult.fetch_row()) != nullptr)
				if (lpDBRow && lpDBRow[0])
					lstFolders.emplace_back(atoi(lpDBRow[0]));
		}
	}

	// Check if we can use indexed search
	er = m_lpSessionManager->GetServerGUID(&guidServer);
	if(er != erSuccess)
		return er;

	if (GetIndexerResults(lpDatabase, m_lpSessionManager->GetConfig().get(), cache,
	    &guidServer, &guidStore, lstFolders, lpSearchCrit->lpRestrict,
	    &lpAdditionalRestrict, lstIndexerResults, suggestion) == erSuccess)
		er = search_r1(lpDatabase, lpSession, std::move(ecODStore),
		     cache, lpAdditionalRestrict, ulStoreId, ulFolderId,
		     lstIndexerResults, suggestion, bNotify, lpbCancel);
	else
		er = search_r2(lpDatabase, lpSession, std::move(ecODStore),
		     lpSearchCrit, ulStoreId, ulFolderId, lstFolders, bNotify,
		     lpbCancel);
	if (er != erSuccess)
		return er;

    // Save this information in the database.
    SetStatus(ulFolderId, EC_SEARCHFOLDER_STATUS_RUNNING);
    return erSuccess;
}

ECRESULT ECSearchFolders::search_r1(ECDatabase *lpDatabase, ECSession *lpSession,
    ECODStore &&ecODStore, ECCacheManager *cache,
    const struct restrictTable *lpAdditionalRestrict, unsigned int ulStoreId,
    unsigned int ulFolderId, const std::list<unsigned int> &lstIndexerResults,
    const std::string &suggestion, bool bNotify, bool *lpbCancel)
{
	std::string strQuery = "INSERT INTO properties (hierarchyid, tag, type, val_string) VALUES(" + stringify(ulFolderId) + "," + stringify(PROP_ID(PR_EC_SUGGESTION)) + "," + stringify(PROP_TYPE(PR_EC_SUGGESTION)) + ",'" + lpDatabase->Escape(suggestion) + "') ON DUPLICATE KEY UPDATE val_string='" + lpDatabase->Escape(suggestion) + "'";
	auto er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess) {
		ec_log_err("ECSearchFolders::Search(): could not add suggestion");
		return er;
	}
	// Get the additional restriction properties ready
	std::list<unsigned int> lstPrefix;
	lstPrefix.emplace_back(PR_MESSAGE_FLAGS);
	struct propTagArray *lpPropTags = nullptr;
	auto cleantags = make_scope_success([&]() { FreePropTagArray(lpPropTags); });
	er = ECGenericObjectTable::GetRestrictPropTags(lpAdditionalRestrict, &lstPrefix, &lpPropTags);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() ECGenericObjectTable::GetRestrictPropTags failed", er);
	// Since an indexed search should be fast, do the entire query as a single transaction, and notify after Commit()
	auto dtx = lpDatabase->Begin(er);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() database begin failed", er);

	auto iterResults = lstIndexerResults.cbegin();
	while (1) {
		// Loop through the results data
		int n = 0;
		ECObjectTableList ecRows;
		for (; iterResults != lstIndexerResults.cend() &&
		     (lpbCancel == NULL || !*lpbCancel) && n < 200; ++iterResults) {
			sObjectTableKey sRow;
			sRow.ulObjId = *iterResults;
			sRow.ulOrderId = 0;
			ecRows.emplace_back(sRow);
		}
		if (ecRows.empty())
			break; // no more rows
		// Note that we do not want ProcessCandidateRows to send notifications since we will send a bulk TABLE_CHANGE later, so bNotify == false here
		er = ProcessCandidateRows(lpDatabase, lpSession, lpAdditionalRestrict, lpbCancel, ulStoreId, ulFolderId, &ecODStore, ecRows, lpPropTags, m_lpSessionManager->GetSortLocale(ulStoreId));
		if (er != erSuccess)
			return ec_perror("ECSearchFolders::Search() ProcessCandidateRows failed", er);
	}

	er = dtx.commit();
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() database commit failed", er);

	if (!bNotify)
		return erSuccess;
	// Notify the search folder and his parent
	// Add matched rows and send a notification to update views of this search (if any are open)
	m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE, 0, ulFolderId, 0, MAPI_MESSAGE);
	cache->Update(fnevObjectModified, ulFolderId);
	m_lpSessionManager->NotificationModified(MAPI_FOLDER, ulFolderId); // folder has modified due to PR_CONTENT_*
	unsigned int ulParent = 0;
	if (cache->GetParent(ulFolderId, &ulParent) == erSuccess)
		m_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, 0, ulParent, ulFolderId, MAPI_FOLDER); // PR_CONTENT_* has changed in tables too
	return erSuccess;
}

ECRESULT ECSearchFolders::search_r2(ECDatabase *lpDatabase, ECSession *lpSession,
    ECODStore &&ecODStore, const struct searchCriteria *lpSearchCrit,
    unsigned int ulStoreId, unsigned int ulFolderId, const ECListInt &lstFolders,
    bool bNotify, bool *lpbCancel)
{
	// Get the restriction ready for this search folder
	std::list<unsigned int> lstPrefix;
	lstPrefix.emplace_back(PR_MESSAGE_FLAGS);
	struct propTagArray *lpPropTags = nullptr;
	auto cleantags = make_scope_success([&]() { FreePropTagArray(lpPropTags); });
	auto er = ECGenericObjectTable::GetRestrictPropTags(lpSearchCrit->lpRestrict, &lstPrefix, &lpPropTags);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::Search() ECGenericObjectTable::GetRestrictPropTags failed", er);
	// If we needn't notify, we don't need to commit each message before notifying, so Begin() here
	kd_trans dtx;
	if (!bNotify)
		dtx = lpDatabase->Begin(er);

	// lstFolders now contains all folders to search through
	for (auto iterFolders = lstFolders.cbegin();
	     iterFolders != lstFolders.cend() &&
	     (lpbCancel == NULL || !*lpbCancel); ++iterFolders) {
		// Optimisation: we know the folder id of the objects we're querying
		ecODStore.ulFolderId = *iterFolders;

		// Get a list of messages in folders, sorted descending by creation date so the newest are found first
		std::string strQuery = "SELECT hierarchy.id from hierarchy WHERE hierarchy.parent = " + stringify(*iterFolders) + " AND hierarchy.type=5 AND hierarchy.flags & " + stringify(MSGFLAG_DELETED|MSGFLAG_ASSOCIATED) + " = 0 ORDER by hierarchy.id DESC";
		DB_RESULT lpDBResult;
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess) {
			ec_perror("ECSearchFolders::Search() SELECT failed", er);
			continue;
		}

		while (1) {
			if (lpbCancel && *lpbCancel)
				break;

			// Read max. 20 rows from the database
			unsigned int i = 0;
			ECObjectTableList ecRows;
			DB_ROW lpDBRow = nullptr;
			while (i < 20 && (lpDBRow = lpDBResult.fetch_row()) != nullptr) {
				if (lpDBRow[0] == NULL)
					continue;
				sObjectTableKey sRow;
				sRow.ulObjId = atoui(lpDBRow[0]);
				sRow.ulOrderId = 0;
				ecRows.emplace_back(sRow);
				++i;
			}

			if (ecRows.empty())
				break; // no more rows
			if (bNotify)
				er = ProcessCandidateRowsNotify(lpDatabase, lpSession, lpSearchCrit->lpRestrict, lpbCancel, ulStoreId, ulFolderId, &ecODStore, ecRows, lpPropTags, m_lpSessionManager->GetSortLocale(ulStoreId));
			else
				er = ProcessCandidateRows(lpDatabase, lpSession, lpSearchCrit->lpRestrict, lpbCancel, ulStoreId, ulFolderId, &ecODStore, ecRows, lpPropTags, m_lpSessionManager->GetSortLocale(ulStoreId));
			if (er != erSuccess)
				return ec_perror("ECSearchFolders::Search() ProcessCandidateRows failed", er);
		}
	}

	// Search done
	// If we needn't notify, we don't need to commit each message before notifying, so Commit() here
	if (!bNotify)
		dtx.commit();
	return erSuccess;
}

// Return whether we are stopped (no entry found), active (no thread found), or rebuilding (thread active)
ECRESULT ECSearchFolders::GetState(unsigned int ulStoreId, unsigned int ulFolderId, unsigned int *lpulState)
{
	unsigned int ulState = 0;

	auto iterStore = m_mapSearchFolders.find(ulStoreId);
	if (iterStore == m_mapSearchFolders.cend()) {
        ulState = 0;
    } else {
		auto iterFolder = iterStore->second.find(ulFolderId);
		if (iterFolder == iterStore->second.cend()) {
            ulState = 0;
        } else {
            ulState = SEARCH_RUNNING;
			if (!iterFolder->second->bThreadFree)
                ulState |= SEARCH_REBUILD;
        }
    }
    *lpulState = ulState;
    return erSuccess;
}

// Entrypoint for the SearchThread
void* ECSearchFolders::SearchThread(void *lpParam)
{
	kcsrv_blocksigs();
	auto ti = static_cast<THREADINFO *>(lpParam);
	auto lpFolder = std::move(ti->lpFolder); // The entry in the m_mapSearchFolders map
	auto lpSearchFolders = ti->lpSearchFolders; // The main ECSearchFolders object

    // We no longer need this
    delete ti;
	g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_THREADS);
    // Start the search
    lpSearchFolders->Search(lpFolder->ulStoreId, lpFolder->ulFolderId, lpFolder->lpSearchCriteria, &lpFolder->bThreadExit);
    // Signal search complete to clients
    lpSearchFolders->m_lpSessionManager->NotificationSearchComplete(lpFolder->ulFolderId, lpFolder->ulStoreId);
	/* Signal exit from thread */
	ulock_normal l_thr(lpFolder->mMutexThreadFree);
	lpFolder->bThreadFree = true;
	lpSearchFolders->m_condThreadExited.notify_one();
	l_thr.unlock();
    // We may not access lpFolder from this point on (it will be freed when the searchfolder is removed)
    lpFolder = NULL;
	g_lpStatsCollector->Increment(SCN_SEARCHFOLDER_THREADS, -1);
    return NULL;
}

// Functions to do things in the database
ECRESULT ECSearchFolders::ResetResults(unsigned int ulFolderId)
{
    ECDatabase *lpDatabase = NULL;
    unsigned int ulParentId = 0;
	auto er = m_lpSessionManager->GetCacheManager()->GetParent(ulFolderId, &ulParentId);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::ResetResults(): GetParent failed", er);
	er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::ResetResults(): GetThreadLocalDatabase failed", er);
	auto dtx = lpDatabase->Begin(er);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::ResetResults(): BEGIN failed", er);
	er = lpDatabase->DoSelect("SELECT properties.val_ulong FROM properties WHERE hierarchyid = " + stringify(ulFolderId) + " FOR UPDATE", NULL);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::ResetResults(): SELECT failed", er);
	auto strQuery = "DELETE FROM searchresults WHERE folderid = " + stringify(ulFolderId);
    er = lpDatabase->DoDelete(strQuery);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::ResetResults(): DELETE failed", er);
	strQuery = "UPDATE properties SET val_ulong = 0 WHERE hierarchyid = " + stringify(ulFolderId) + " AND tag IN(" + stringify(PROP_ID(PR_CONTENT_COUNT)) + "," + stringify(PROP_ID(PR_CONTENT_UNREAD)) + ") AND type = " + stringify(PROP_TYPE(PR_CONTENT_COUNT));
	er = lpDatabase->DoUpdate(strQuery);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::ResetResults(): DoUpdate failed", er);
	er = UpdateTProp(lpDatabase, PR_CONTENT_COUNT, ulParentId, ulFolderId);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::ResetResults(): UpdateTProp failed(1)", er);
	er = UpdateTProp(lpDatabase, PR_CONTENT_UNREAD, ulParentId, ulFolderId);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::ResetResults(): UpdateTProp failed(2)", er);
	er = dtx.commit();
	if (er != erSuccess)
		ec_perror("ECSearchFolders::ResetResults(): database commit failed", er);
	return erSuccess;
}

// Add a single search result message (eg one match in a search folder)
ECRESULT ECSearchFolders::AddResults(unsigned int ulFolderId, unsigned int ulObjId, unsigned int ulFlags, bool *lpfInserted)
{
    ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult;

    assert((ulFlags &~ MSGFLAG_READ) == 0);
	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::AddResults(): GetThreadLocalDatabase failed", er);
	std::string strQuery = "SELECT flags FROM searchresults WHERE folderid = " + stringify(ulFolderId) + " AND hierarchyid = " + stringify(ulObjId) + " LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::AddResults(): select searchresults failed", er);
	auto lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow != nullptr && lpDBRow[0] != nullptr && atoui(lpDBRow[0]) == ulFlags)
		// The record in the database is the same as what we're trying to insert; this is an error because we can't update or insert the record
		return KCERR_NOT_FOUND;

	// This will either update or insert the record
    strQuery = "INSERT INTO searchresults (folderid, hierarchyid, flags) VALUES(" + stringify(ulFolderId) + "," + stringify(ulObjId) + "," + stringify(ulFlags) + ") ON DUPLICATE KEY UPDATE flags=" + stringify(ulFlags);
    er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::AddResults(): INSERT failed", er);
	// We have inserted if the previous SELECT returned no row
	if (lpfInserted)
		*lpfInserted = (lpDBRow == NULL);
	return erSuccess;
}

ECRESULT ECSearchFolders::AddResults(unsigned int ulFolderId, std::list<unsigned int> &lstObjId, std::list<unsigned int>& lstFlags, int *lpulCount, int *lpulUnread)
{
    ECDatabase *lpDatabase = NULL;
	unsigned int ulInserted = 0, ulModified = 0;

	assert(lstObjId.size() == lstFlags.size());
    if(lstObjId.empty())
		return erSuccess;
	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::AddResults(): GetThreadLocalDatabase failed", er);
	auto strQuery = "SELECT 1 FROM searchresults WHERE folderid = " +
		stringify(ulFolderId) + " AND hierarchyid IN (" +
		kc_join(lstObjId, ",", stringify) + ") FOR UPDATE";
	er = lpDatabase->DoSelect(strQuery, NULL);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::AddResults(): DoSelect failed", er);
	strQuery = "INSERT IGNORE INTO searchresults (folderid, hierarchyid, flags) VALUES";
	for (const auto n : lstObjId)
		strQuery += "(" + stringify(ulFolderId) + "," + stringify(n) + ",1),";
	strQuery.resize(strQuery.size()-1);
    er = lpDatabase->DoInsert(strQuery, NULL, &ulInserted);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::AddResults(): DoInsert failed", er);

	unsigned int n = 0;
	strQuery = "UPDATE searchresults SET flags = 0 WHERE hierarchyid IN (";
	for (auto i = lstFlags.cbegin(), j = lstObjId.cbegin(); i != lstFlags.cend(); i++, j++) {
		if (*i != 0)
			continue;

		strQuery += stringify(*j) + ",";
		n++;
	}

	if (n > 0) {
		strQuery.resize(strQuery.size()-1);
		strQuery += ") AND folderid = " + stringify(ulFolderId);
		er = lpDatabase->DoUpdate(strQuery, &ulModified);
		if (er != erSuccess)
			return ec_perror("ECSearchFolders::AddResults(): UPDATE failed", er);
	}

	if (lpulCount != NULL)
		*lpulCount += ulInserted;
	if (lpulUnread != NULL)
		*lpulUnread += ulModified;
	return erSuccess;
}

// Remove a single search result (so one message in a search folder). Returns NOT_FOUND if the item wasn't in the database in the first place
ECRESULT ECSearchFolders::DeleteResults(unsigned int ulFolderId, unsigned int ulObjId, unsigned int *lpulOldFlags)
{
    ECDatabase *lpDatabase = NULL;
	DB_RESULT lpResult;

    unsigned int ulAffected = 0;
	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		return er;

	if(lpulOldFlags) {
		std::string strQuery = "SELECT flags FROM searchresults WHERE folderid=" + stringify(ulFolderId) + " AND hierarchyid=" + stringify(ulObjId) + " LIMIT 1";
		er = lpDatabase->DoSelect(strQuery, &lpResult);
		if (er != erSuccess)
			return ec_perror("ECSearchFolders::DeleteResults(): SELECT failed", er);
		auto lpRow = lpResult.fetch_row();
		if (lpRow == nullptr || lpRow[0] == nullptr)
			return KCERR_NOT_FOUND;
		*lpulOldFlags = atoui(lpRow[0]);
	}

	std::string strQuery = "DELETE FROM searchresults WHERE folderid=" + stringify(ulFolderId) + " AND hierarchyid=" + stringify(ulObjId);
    er = lpDatabase->DoDelete(strQuery, &ulAffected);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::DeleteResults(): DELETE failed", er);
	return ulAffected != 0 ? erSuccess : KCERR_NOT_FOUND;
}

// Write the status of a search folder to the PR_EC_SEARCHFOLDER_STATUS property
ECRESULT ECSearchFolders::SetStatus(unsigned int ulFolderId, unsigned int ulStatus)
{
    ECDatabase *lpDatabase = NULL;

	// Do not use transactions because this function is called inside a transaction.
	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::SetStatus(): GetThreadLocalDatabase failed", er);

    // No record == running
    if(ulStatus != EC_SEARCHFOLDER_STATUS_RUNNING) {
		std::string strQuery = "REPLACE INTO properties (tag, type, hierarchyid, val_ulong) "
                   "VALUES(" + stringify(PROP_ID(PR_EC_SEARCHFOLDER_STATUS)) + "," +
                               stringify(PROP_TYPE(PR_EC_SEARCHFOLDER_STATUS)) + "," +
                               stringify(ulFolderId) + "," +
                               stringify(ulStatus) + ")";
        er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			return ec_perror("ECSearchFolders::SetStatus(): DoInsert failed", er);
		return erSuccess;
	}
	std::string strQuery = "DELETE FROM properties "
					"WHERE hierarchyid=" + stringify(ulFolderId) +
					" AND tag=" + stringify(PROP_ID(PR_EC_SEARCHFOLDER_STATUS)) +
					" AND type=" + stringify(PROP_TYPE(PR_EC_SEARCHFOLDER_STATUS));
	er = lpDatabase->DoDelete(strQuery);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::SetStatus(): DELETE failed", er);
	return erSuccess;
}

// Get all results of a certain search folder in a list of hierarchy IDs
ECRESULT ECSearchFolders::GetSearchResults(unsigned int ulStoreId, unsigned int ulFolderId,  std::list<unsigned int> *lstObjIds)
{
    ECDatabase *lpDatabase = NULL;
	DB_RESULT lpResult;
	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::GetSearchResults(): GetThreadLocalDatabase failed", er);
	std::string strQuery = "SELECT hierarchyid FROM searchresults WHERE folderid=" + stringify(ulFolderId);
    er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::GetSearchResults(): SELECT failed", er);
    lstObjIds->clear();

    while(1) {
		auto lpRow = lpResult.fetch_row();
        if(lpRow == NULL || lpRow[0] == NULL)
            break;
		lstObjIds->emplace_back(atoui(lpRow[0]));
    }
	return erSuccess;
}

// Loads the search criteria from the database
ECRESULT ECSearchFolders::LoadSearchCriteria(unsigned int ulFolderId, struct searchCriteria **lppSearchCriteria)
{
	ECDatabase		*lpDatabase = NULL;
	DB_RESULT lpDBResult;

    // Get database
	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::LoadSearchCriteria(): GetThreadLocalDatabase failed", er);
	// Find out what kind of table this is
	std::string strQuery = "SELECT hierarchy.flags, properties.val_string FROM hierarchy JOIN properties on hierarchy.id=properties.hierarchyid AND properties.tag =" + stringify(PROP_ID(PR_EC_SEARCHCRIT)) + " AND properties.type =" + stringify(PROP_TYPE(PR_EC_SEARCHCRIT)) + " WHERE hierarchy.id =" + stringify(ulFolderId) + " LIMIT 1";

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::LoadSearchCriteria(): SELECT failed", er);
	auto lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == nullptr || lpDBRow[0] == nullptr || atoi(lpDBRow[0]) != 2 || lpDBRow[1] == nullptr)
		return KCERR_NOT_FOUND;
	return LoadSearchCriteria2(lpDBRow[1], lppSearchCriteria);
}

ECRESULT ECSearchFolders::LoadSearchCriteria2(const std::string &xmldata,
    struct searchCriteria **lppSearchCriteria)
{
	std::istringstream xml(xmldata);
	struct soap xmlsoap;
	struct searchCriteria crit;
	ECRESULT er = erSuccess;

	/* Use the soap (de)serializer to store the data */
	soap_set_mode(&xmlsoap, SOAP_XML_TREE | SOAP_C_UTFSTRING);
	xmlsoap.is = &xml;
	soap_default_searchCriteria(&xmlsoap, &crit);
	if (soap_begin_recv(&xmlsoap) != 0)
		return KCERR_NETWORK_ERROR;
	soap_get_searchCriteria(&xmlsoap, &crit, "SearchCriteria", NULL);

	// We now have the object, allocated by xmlsoap object,
	if (soap_end_recv(&xmlsoap) != 0)
		er = KCERR_NETWORK_ERROR;
	else
		er = CopySearchCriteria(nullptr, &crit, lppSearchCriteria);
	/*
	 * We do not need the error here: lppSearchCriteria will not be
	 * touched, and we need to free the soap structs.
	 */
	soap_destroy(&xmlsoap);
	soap_end(&xmlsoap);
	soap_done(&xmlsoap);
	return er;
}

// Saves the search criteria in the database
ECRESULT ECSearchFolders::SaveSearchCriteria(unsigned int ulFolderId,
    const struct searchCriteria *lpSearchCriteria)
{
	ECDatabase		*lpDatabase = NULL;

    // Get database
	auto er = GetThreadLocalDatabase(m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		return ec_perror("ECSearchFolders::SaveSearchCriteria(): GetThreadLocalDatabase failed", er);
	auto dtx = lpDatabase->Begin(er);
	if (er != hrSuccess)
		return ec_perror("ECSearchFolders::SaveSearchCriteria(): BEGIN failed", er);
	er = SaveSearchCriteriaRow(lpDatabase, ulFolderId, lpSearchCriteria);
	if (er != hrSuccess)
		return ec_perror("ECSearchFolders::SaveSearchCriteria(): SaveSearchCriteriaRow failed", er);
	er = dtx.commit();
	if (er != hrSuccess)
		ec_perror("ECSearchFolders::SaveSearchCriteria(): commit failed", er);
	return erSuccess;
}

// Serialize and save the search criteria for a certain folder. The property is saved as a PR_EC_SEARCHCRIT property
ECRESULT ECSearchFolders::SaveSearchCriteriaRow(ECDatabase *lpDatabase,
    unsigned int ulFolderId, const struct searchCriteria *lpSearchCriteria)
{
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
	if (soap_begin_send(&xmlsoap) != 0 ||
	    soap_put_searchCriteria(&xmlsoap, &sSearchCriteria, "SearchCriteria", nullptr) != 0)
		return KCERR_NOT_ENOUGH_MEMORY;
	if (soap_end_send(&xmlsoap) != 0)
		return KCERR_NETWORK_ERROR;

	// Make sure we're linking with the correct SOAP (c++ version)
	assert(!xml.str().empty());
	// xml now contains XML version of search criteria
	// Replace PR_EC_SEARCHCRIT in database
	std::string strQuery = "REPLACE INTO properties (hierarchyid, tag, type, val_string) VALUES(" + stringify(ulFolderId) + "," + stringify(PROP_ID(PR_EC_SEARCHCRIT)) + "," + stringify(PROP_TYPE(PR_EC_SEARCHCRIT)) + ",'" + lpDatabase->Escape( xml.str() ) + "')";
	return lpDatabase->DoInsert(strQuery);
}

void ECSearchFolders::FlushAndWait()
{
	ulock_rec l_ev(m_mutexEvents);
	m_condEvents.notify_all();
	l_ev.unlock();
	// let ProcessThread get this lock, and mark the thread running
	l_ev.lock();
	l_ev.unlock();
	// wait for an inactive search thread
	while (m_bRunning) Sleep(10);
}

/*
 * This is the main processing thread, which processes changes from the queue. After processing it removes them from the queue and waits for
 * new events
 */
void * ECSearchFolders::ProcessThread(void *lpSearchFolders)
{
	kcsrv_blocksigs();
	auto lpThis = static_cast<ECSearchFolders *>(lpSearchFolders);

    while(1) {
        // Get events to process
		ulock_rec l_ev(lpThis->m_mutexEvents);
		if (lpThis->m_bExitThread)
			break;
		if (lpThis->m_lstEvents.empty())
			/*
			 * No events, wait until one arrives (the mutex is
			 * unlocked by pthread_cond_wait so people are able to
			 * add new events). The condition also occurs when the
			 * server is exiting.
			 */
			lpThis->m_condEvents.wait(l_ev);

		lpThis->m_bRunning = true;
		/*
		 * The condition ended. Two things can have happened: there is
		 * now at least one event waiting, or and exit has been
		 * requested. In both cases, we simply unlock the mutex and
		 * process any (may be 0) events currently in the queue. This
		 * means that the caller must make sure that no new events can
		 * be added after the m_bThreadExit flag is set to TRUE.
		 */
		l_ev.unlock();
        lpThis->FlushEvents();
        Sleep(1000);
		lpThis->m_bRunning = false;
        // Check if we need to exit
    }
    return NULL;
}

// Process all waiting events in an efficient order
ECRESULT ECSearchFolders::FlushEvents()
{
    std::list<EVENT> lstEvents;
    ECObjectTableList lstObjectIDs;
    sObjectTableKey sRow;

    // We do a copy-remove-process cycle here to keep the event queue locked for the least time as possible with
    // 500 events at a time
	ulock_rec l_ev(m_mutexEvents);
    for (int i = 0; i < 500; ++i) {
        // Move the first element of m_lstEvents to the head of our list.
        if(m_lstEvents.empty())
            break;
        lstEvents.splice(lstEvents.end(), m_lstEvents, m_lstEvents.begin());
    }
	l_ev.unlock();
    // Sort the items by folder. The order of DELETE and ADDs will remain unchanged. This is important
    // because the order of the incoming ADD or DELETE is obviously important for the final result.
	lstEvents.sort([](const EVENT &a, const EVENT &b) { return a.ulFolderId < b.ulFolderId; });

    // Send the changes grouped by folder (and therefore also by store)
	unsigned int ulStoreId = 0, ulFolderId = 0;
	ECKeyTable::UpdateType ulType = ECKeyTable::TABLE_ROW_MODIFY;

    // Process changes by finding sequences of events of the same type (eg ADD ADD ADD DELETE will result in two sequences: 3xADD + 1xDELETE)
    for (const auto &event : lstEvents) {
        if (event.ulFolderId != ulFolderId || event.ulType != ulType) {
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
        ulStoreId = event.ulStoreId;
        ulFolderId = event.ulFolderId;
        ulType = event.ulType;
        sRow.ulObjId = event.ulObjectId;
        sRow.ulOrderId = 0;
		lstObjectIDs.emplace_back(sRow);
    }

	if (lstObjectIDs.empty())
		return erSuccess;
	// Flush last set
	// This is important: make the events unique. We need to do this because the ECStoreObjectTable
	// row engine does not support requesting the exact same row twice within the same call. If we have
	// duplicates here, this will filter through to the row engine and cause all kinds of nastiness, mainly
	// causing the item to be deleted from search folders irrespective of whether it should have been deleted
	// or added.
	lstObjectIDs.sort();
	lstObjectIDs.unique();
	ProcessMessageChange(ulStoreId, ulFolderId, &lstObjectIDs, ulType);
    return erSuccess;
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
	memset(&sStats, 0, sizeof(sSearchFolderStats));
	ulock_rec l_sf(m_mutexMapSearchFolders);

	sStats.ulStores = m_mapSearchFolders.size();
	sStats.ullSize = sStats.ulStores * sizeof(STOREFOLDERIDSEARCH::value_type);

	for (const auto &storefolder : m_mapSearchFolders) {
		sStats.ulFolders += storefolder.second.size();
		sStats.ullSize += storefolder.second.size() * (sizeof(FOLDERIDSEARCH::value_type) + sizeof(SEARCHFOLDER));
		for (const auto &fs : storefolder.second)
			sStats.ullSize += SearchCriteriaSize(fs.second->lpSearchCriteria);
	}
	l_sf.unlock();

	ulock_rec l_ev(m_mutexEvents);
	sStats.ulEvents = m_lstEvents.size();
	l_ev.unlock();
	sStats.ullSize += sStats.ulEvents * sizeof(EVENT);
	return erSuccess;
}

} /* namespace */
