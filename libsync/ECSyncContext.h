/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSYNCCONTEXT_H
#define ECSYNCCONTEXT_H

#include <mutex>
#include <mapidefs.h>
#include "../provider/client/ics_client.hpp"
#include <map>
#include <set>
#include <string>
#include <kopano/IECInterfaces.hpp>
#include <kopano/memory.hpp>

namespace KC {

typedef std::map<std::string,SSyncState>	SyncStateMap;
typedef	std::map<ULONG,ULONG>				NotifiedSyncIdMap;

class ECLogger;
class ECSyncSettings;

/**
 * ECSyncContext:	This class encapsulates all synchronization related information that is
 *					only related to one side of the sync process (online or offline).
 */
class ECSyncContext final {
public:
	/**
	 * Construct a sync context.
	 *
	 * @param[in]	lpStore
	 *					The store for which to create the sync context.
	 * @param[in]	lpLogger
	 *					The logger to log to.
	 */
	ECSyncContext(LPMDB lpStore, std::shared_ptr<ECLogger>);

	/**
	 * Get a pointer to the message store on which this sync context operates.
	 * The underlying objects reference will be incremented, so the obtained
	 * pointer needs to be released when it's not needed anymore.
	 *
	 * @param[out]	lppMsgStore
	 *					Pointer to a IMsgStore pointer, which will contain
	 *					a pointer to the requested messages store upon successful
	 *					completion.
	 */
	HRESULT HrGetMsgStore(LPMDB *lppMsgStore);

	/**
	 * Get the receive folder of the message store on which this sync context operates.
	 * The underlying objects reference will be incremented, so the obtained
	 * pointer needs to be released when it's not needed anymore.
	 * 
	 * @param[out]	lppInboxFolder
	 *					Pointer to a IMAPIFolder pointer, which will contain a
	 *					pointer to the requested folder upon successful completion.
	 */
	HRESULT HrGetReceiveFolder(LPMAPIFOLDER *lppInboxFolder);

	/**
	 * Get the change advisor for this sync context.
	 * The underlying objects reference will be incremented, so the obtained
	 * pointer needs to be released when it's not needed anymore.
	 *
	 * @param[out]	lppChangeAdvisor
	 *					Pointer to a IECChangeAdvisor pointer, which will contain a
	 *					pointer to the change advisor upon successful completion.
	 * @return MAPI_E_NO_SUPPORT if the change notification system is disabled.
	 */
	HRESULT HrGetChangeAdvisor(IECChangeAdvisor **lppChangeAdvisor);

	/**
	 * Replace the change advisor. This causes all the registered change advises
	 * to be dropped. Also the map with received states is cleared.
	 */
	HRESULT HrResetChangeAdvisor();

	/**
	 * Get the change advise sink for this sync context.
	 * The underlying objects reference will be incremented, so the obtained
	 * pointer needs to be released when it's not needed anymore.
	 *
	 * @param[out]	lppChangeAdviseSink
	 *					Pointer to a IECChangeAdviseSInk pointer, which will contain a
	 *					pointer to the change advise sink upon successful completion.
	 */
	HRESULT HrGetChangeAdviseSink(IECChangeAdviseSink **lppChangeAdviseSink);

	/**
	 * Get the full hierarchy for the store on which this sync context operates.
	 *
	 * @param[in]	lpsPropTags
	 *					The proptags of the properties that should be obtained
	 *					from the hierarchy table.
	 * @param[out]	lppRows
	 *					Pointer to a SRowSet pointer, which will be populated with
	 *					the rows from the hierarchy table. Needs to be freed with
	 *					FreePRows by the caller.
	 */
	HRESULT HrQueryHierarchyTable(LPSPropTagArray lpsPropTags, LPSRowSet *lppRows);

	/**
	 * Get the root folder for the current sync context.
	 *
	 * @param[in]	lppRootFolder
	 *					Pointer to a IMAPIFolder pointer that will contain a pointer
	 *					to the root folder upon successful completion.
	 * @param[in]	lppMsgStore
	 *					Pointer to a IMsgStore pointer that will contain a pointer to
	 *					the message store upon successful completion. Passing NULL will
	 *					cause no MsgStore pointer to be returned.
	 */
	HRESULT HrOpenRootFolder(LPMAPIFOLDER *lppRootFolder, LPMDB *lppMsgStore = NULL);

	/**
	 * Open a folder using the store used by the current sync context.
	 *
	 * @param[in]	lpsEntryID
	 *					Pointer to a SBinary structure that will be interpreted as the
	 *					entry id of the folder to open.
	 * @param[out]	lppFolder
	 *					Pointer to a IMAPIFolder pointer that will contain a pointer to
	 *					the requested folder upon successful completion.
	 */
	HRESULT HrOpenFolder(SBinary *lpsEntryID, LPMAPIFOLDER *lppFolder);

	/**
	 * Send a new mail notification through the current sync context.
	 *
	 * @Param[in]	lpNotification
	 *					Pointer to a NOTIFICATION structure that will be sent as the
	 *					new mail notification.
	 */
	HRESULT HrNotifyNewMail(LPNOTIFICATION lpNotification);

	/**
	 * Get the number of steps necessary to complete a sync on a particular folder.
	 *
	 * @param[in]	lpEntryID
	 *					Pointer to a SBinary structure that will be interpreted as the
	 *					entry id of the folder to synchronize.
	 * @param[in]	lpSourceKey
	 *					Pointer to a SBinary structure that will be interpreted as the
	 *					source key of the folder to synchronize.
	 * @param[in]	ulSyncFlags
	 *					Flags that control the behavior of the sync operation.
	 * @param[out]	lpulSteps
	 *					Pointer to a ULONG variable that will contain the number of steps
	 *					to complete a synchronization on the selected folder upon successful
	 *					completion.
	 */
	HRESULT HrGetSteps(SBinary *lpEntryID, SBinary *lpSourceKey, ULONG ulSyncFlags, ULONG *lpulSteps);

	/**
	 * Update the change id for a particular sync id, based on a state stream.
	 * This will cause folders that have pending changes to be removed if the change id
	 * in the stream is greater or equal to the change id for which the pending change
	 * was queued.
	 *
	 * @param[in]	lpStream
	 *					The state stream from which the sync id and change id will be
	 *					extracted.
	 */
	HRESULT HrUpdateChangeId(LPSTREAM lpStream);

	/**
	 * Check if the sync status streams have been loaded.
	 *
	 * @return true if sync status streams have been loaded, false otherwise.
	 */
	bool SyncStatusLoaded() const { return !m_mapSyncStatus.empty(); }

	/**
	 * Clear the sync status streams.
	 */
	HRESULT HrClearSyncStatus();

	/**
	 * Load the sync status streams.
	 *
	 * @param[in]	lpsSyncState
	 *					The SBinary structure containing the data to be decoded.
	 */
	HRESULT HrLoadSyncStatus(SBinary *lpsSyncState);

	/**
	 * Save the sync status streams.
	 *
	 * @param[in]	lppSyncStatusProp
	 *					Pointer to a SPropValue pointer that will be populated with
	 *					the binary data that's made out of the status streams.
	 */
	HRESULT HrSaveSyncStatus(LPSPropValue *lppSyncStatusProp);

	/**
	 * Get the sync status stream for a particular folder.
	 *
	 * @param[in]	lpFolder
	 *					The folder for which to get the sync status stream.
	 * @param[out]	lppStream
	 *					Pointer to a IStream pointer that will contain the
	 *					sync status stream on successful completion.
	 */
	HRESULT HrGetSyncStatusStream(LPMAPIFOLDER lpFolder, LPSTREAM *lppStream);

	/**
	 * Get the sync status stream for a particular folder.
	 *
	 * @param[in]	lpSourceKey
	 *					An SBinary structure that will be interprested as a
	 *					sourcekey that specifies a folder.
	 * @param[out]	lppStream
	 *					Pointer to a IStream pointer that will contain the
	 *					sync status stream on successful completion.
	 */
	HRESULT HrGetSyncStatusStream(SBinary *lpsSourceKey, LPSTREAM *lppStream);

	/**
	 * Get the resync id from the store.
	 * This id is incremented with kopano-admin on the online store if a folder
	 * resync is required. If the online and offline id differ, a resync will be
	 * initiated. Afterwards the offline id is copied from the online id.
	 *
	 * @param[out]	lpulResyncID	The requested id.
	 */
	HRESULT GetResyncID(ULONG *lpulResyncID);

	/**
	 * Set the resync id on the store.
	 * @see GetResyncID
	 *
	 * @param[in]	ulResyncID		The id to set.
	 */
	HRESULT SetResyncID(ULONG ulResyncID);

	/**
	 * Get stored server UID.
	 * Get the stored onlinse server UID. This is compared to the current online
	 * server UID in order to determine if the online store was relocated. In that
	 * case a resync must be performed in order for ICS to function properly.
	 * This server UID is stored during the first folder sync step or whenever it's
	 * absent (for older profiles).
	 * Only applicable on offline stores.
	 *
	 * @param[out]	lpServerUid		The requested server UID.
	 */
	HRESULT GetStoredServerUid(LPGUID lpServerUid);

	/**
	 * Set stored server UID.
	 * @see GetStoredServerUid
	 *
	 * @param[in]	lpServerUid		The server uid to set.
	 */
	HRESULT SetStoredServerUid(LPGUID lpServerUid);

	/**
	 * Get the server UID.
	 * This is used to compare with the stores server UID.
	 * @see GetStoredServerUid
	 *
	 * @param[out]	lpServerUid		The requested server UID.
	 */
	HRESULT GetServerUid(LPGUID lpServerUid);

private:	// methods
	/**
	 * Get the sync state for a sourcekey
	 *
	 * @param[in]	lpSourceKey
	 *					The sourcekey for which to get the sync state.
	 * @param[out]	lpsSyncState
	 *					Pointer to a SSyncState structure that will be populated
	 *					with the retrieved sync state.
	 */
	HRESULT HrGetSyncStateFromSourceKey(SBinary *lpSourceKey, SSyncState *lpsSyncState);

	/**
	 * Handle change events (through the ChangeAdviseSink).
	 *
	 * @param[in]	ulFlags
	 *					Unused
	 * @param[in]	lpEntryList
	 *					List of sync states that have changes pending.
	 * @return 0
	 */
	ULONG	OnChange(ULONG ulFlags, LPENTRYLIST lpEntryList);

	/**
	 * Release the change advisor and clear the map with received states.
	 */
	HRESULT HrReleaseChangeAdvisor();

	std::shared_ptr<ECLogger> m_lpLogger;
	object_ptr<IMsgStore> m_lpStore;
	ECSyncSettings			*m_lpSettings;
	object_ptr<IECChangeAdviseSink> m_lpChangeAdviseSink;
	object_ptr<IECChangeAdvisor> m_lpChangeAdvisor;
	std::map<std::string, object_ptr<IStream>> m_mapSyncStatus;
	SyncStateMap			m_mapStates;
	NotifiedSyncIdMap		m_mapNotifiedSyncIds;
	std::mutex m_hMutex;
};

} /* namespace */

#endif // ndef ECSYNCCONTEXT_H
