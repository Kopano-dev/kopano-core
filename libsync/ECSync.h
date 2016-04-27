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

#ifndef ECSYNC_H
#define ECSYNC_H

#ifdef HAVE_OFFLINE_SUPPORT

#include <kopano/zcdefs.h>
#include <string>
#include <mapidefs.h>
#include <mapispi.h>
#include <mapix.h>
#include <map>
#include <set>
#include <pthread.h>
#include <edkmdb.h>
#include "IECSync.h"
#include <kopano/ECUnknown.h>

#include "ECLibSync.h"
#include "ECSyncContext.h"
#include "../provider/client/ECICS.h"

/*
in PR_EC_OFFLINE_SYNC_STATUS and PR_EC_ONLINE_SYNC_STATUS the status is saved as:
ULONG Version number
ULONG sync status count
	ULONG size sourcekey
		BYTE[] sourcekey
	ULONG size sync status
		BYTE[] sync status
*/

typedef	std::map<std::string,LPSTREAM>		StatusStreamMap;
typedef std::map<std::string,SSyncState>	SyncStateMap;

class ECLogger;
class ECSyncDataProvider;
class ECResyncSet;

HRESULT ECLIBSYNC_API CreateECSync(LPMSPROVIDER lpOfflineProvider, LPMSPROVIDER lpOnlineProvider, LPMAPISUP lpSupport, ULONG cbStoreID, LPENTRYID lpStoreID, IECSync **lppSync);
HRESULT ECLIBSYNC_API CreateECSync(LPMAPISESSION lpSession, IECSync **lppSync);

class ECSync : public ECUnknown {
protected:
	void init();

	ECSync(LPMAPISESSION lpSession);
	ECSync(const std::string &strProfileName);
	ECSync(LPMSPROVIDER lpOfflineProvider, LPMSPROVIDER lpOnlineProvider, LPMAPISUP lpSupport, ULONG cbStoreID, LPENTRYID lpStoreID);
	
	//constructor for testing with 2 profiles
	ECSync(const std::string &strOfflineProfile, const std::string &strOnlineProfile);
	~ECSync();
public:
	ULONG AddRef() {
		return ECUnknown::AddRef();
	}

	ULONG Release() {
		return ECUnknown::Release();
	}

	static HRESULT Create(LPMAPISESSION lpSession, ECSync **lppECSync);
	static HRESULT Create(const std::string &strProfileName, ECSync **lppECSync);
	static HRESULT Create(LPMSPROVIDER lpOfflineProvider, LPMSPROVIDER lpOnlineProvider, LPMAPISUP lpSupport, ULONG cbStoreID, LPENTRYID lpStoreID, ECSync **lppECSync);
	static HRESULT Create(const std::string &strOfflineProfile, const std::string &strOnlineProfile, ECSync **lppECSync);

	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid);

	//sync store properties and all folders
	//call SyncStoreInfo, FolderSync and SaveSyncStatus
	HRESULT FirstFolderSync();

	//start sync thread
	HRESULT StartSync();
	//stop sync thread
	HRESULT StopSync();

	HRESULT CancelSync();

	HRESULT SetSyncStatusCallBack(LPVOID lpSyncStatusCallBackObject, SyncStatusCallBack lpSyncStatusCallBack);
	HRESULT SetSyncProgressCallBack(LPVOID lpSyncProgressCallBackObject, SyncProgressCallBack lpSyncProgressCallBack);
	HRESULT SetSyncPopupErrorCallBack(LPVOID lpSyncPopupErrorCallBackObject, SyncPopupErrorCallBack lpSyncPopupErrorCallBack);

	HRESULT GetSyncStatus(ULONG * lpulStatus);

	HRESULT SetWaitTime(ULONG ulWaitTime);
	HRESULT GetWaitTime(ULONG* lpulWaitTime);

	HRESULT SetSyncOnNotify(ULONG ulEventMask);
	HRESULT GetSyncOnNotify(ULONG* lpulEventMask);

	//HRESULT GetOnlineStore(LPMDB* lppMDBOnline);

	HRESULT GetSyncError();
	HRESULT SetThreadLocale(LCID locale);

private:
	enum {
		ignoreFromState = 1,
		ignoreToState = 2,
		resyncFrom = 4
	};

	//continuasly call FolderSync and MessageSync and SaveSyncStatus and pauze a minute
	static LPVOID SyncThread(void *lpTmpECSync);
	LPVOID SyncThread();

	//logon on with the provider or the profile
	HRESULT Logon();
	HRESULT Logoff();
	HRESULT ReLogin();

	HRESULT SyncAB();
	HRESULT DoSyncAB();
	HRESULT ResetAB();

	//synchronize the folders under the root folder
	HRESULT FolderSync();
	HRESULT FolderSyncOneWay(ECSyncContext *lpFromContext, ECSyncContext *lpToContext, double dProgressStart);

	//get the folder tree and call MessageSyncFolder for each folder
	HRESULT CreateChangeList(std::list<SBinary> *lplstChanges, ULONG *lpulTotalSteps);
	HRESULT ReleaseChangeList(std::list<SBinary> &lstChanges);
	HRESULT MessageSync(std::list<SBinary> &lstChanges);

	//sync all messsages of 1 folder
	HRESULT MessageSyncFolder(SBinary sEntryID);
	HRESULT MessageSyncFolderOneWay(LPMAPIFOLDER lpFromFolder, LPMAPIFOLDER lpToFolder, LPSTREAM lpFromStream, LPSTREAM lpToStream, ULONG ulFlags = 0);
	
	//sync all store properties except PR_EC_OFFLINE_SYNC_STATUS and PR_EC_ONLINE_SYNC_STATUS
	HRESULT SyncStoreInfo();
	HRESULT HrCreateRemindersFolder();
	HRESULT HrSyncReceiveFolders(ECSyncContext *lpFromContext, ECSyncContext *lpToContext);
	
	//join sync status map to binary string and put it in PR_EC_SYNC_STATUS on the store
	HRESULT SaveSyncStatusStreams();

	//fill sync status map using PR_EC_SYNC_STATUS on the store
	HRESULT HrLoadSyncStatusStream(ECSyncContext *lpContext, LPMDB lpStore, ULONG ulPropTag);
	HRESULT LoadSyncStatusStreams();
	HRESULT UpdateSyncStatus(ULONG ulAddStatus, ULONG ulRemoveStatus);
	HRESULT ReportError(LPCTSTR lpszReportText);

	// Register ExchangeChangeAdvisors
	HRESULT HrSetupChangeAdvisors();
	HRESULT HrLoadChangeNotificationStatusStreams(LPSTREAM *lppOnlineStream, LPSTREAM *lppOfflineStream, bool bAllowCreate);
	HRESULT HrSaveChangeNotificationStatusStreams();
	HRESULT HrSaveChangeNotificationStatusStream(ECSyncContext *lpContext, ULONG ulStatePropTag);
	HRESULT HrCreateChangeNotificationStatusStreams(LPSTREAM *lppOnlineStream, LPSTREAM *lppOfflineStream);
	HRESULT HrAddFolderSyncStateToEntryList(ECSyncContext *lpContext, SBinary *lpsEntryID, LPENTRYLIST lpEntryList);
	static HRESULT HrEntryListToChangeNotificationStream(IECChangeAdvisor *lpChangeAdvisor, LPMDB lpStore, ULONG ulPropTag, LPENTRYLIST lpEntryList, LPSTREAM *lppStream);

	// Check for cancel and exit sync
	HRESULT CheckExit();

	// Configure subobjects
	HRESULT HrConfigureImporter(LPEXCHANGEIMPORTCONTENTSCHANGES lpImporter, LPSTREAM lpStream);
	HRESULT HrConfigureExporter(LPEXCHANGEEXPORTCHANGES lpExporter, LPSTREAM lpStream, LPEXCHANGEIMPORTCONTENTSCHANGES lpImporter, ULONG ulExtraFlags = 0);

	ULONG OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifs);

	// Resync related methods
	enum eResyncReason {
		ResyncReasonNone,		//!< No resync requested
		ResyncReasonRequested,	//!< Resync requested because differences in folder content were observer
		ResyncReasonRelocated	//!< Resync required because online store was relocated
	};

	HRESULT GetResyncReason(eResyncReason *lpResyncState);

	HRESULT ResyncFoldersBothWays(LPMAPIFOLDER lpOnlineFolder, LPMAPIFOLDER lpOfflineFolder);
	HRESULT ResyncFoldersOneWay(LPMAPIFOLDER lpFromFolder, LPMAPIFOLDER lpToFolder);
	HRESULT CreateMessageList(LPMAPIFOLDER lpFolder, ECResyncSet *lpResyncSet);
	HRESULT FilterAndCreateMessageLists(LPMAPIFOLDER lpFolder, ECResyncSet *lpOtherResyncSet, ECResyncSet *lpResyncSet, bool bCompareLastModTime);
	HRESULT ResyncFromSet(LPMAPIFOLDER lpFromFolder, LPMAPIFOLDER lpToFolder, LPSTREAM lpToStream, ECResyncSet &resyncSet);
	HRESULT FilterAddsAndCreateDeleteLists(LPMAPIFOLDER lpFolder, ECResyncSet *lpAddSet, LPENTRYLIST *lppDelList);

	HRESULT UpdateProfileAndOpenOnlineStore(LPMAPISESSION lpSession, LPMDB *lppStore);

	ECSyncContext			*m_lpOfflineContext;
	ECSyncContext			*m_lpOnlineContext;

	SyncStatusCallBack		m_lpSyncStatusCallBack;
	LPVOID					m_lpSyncStatusCallBackObject;
	SyncProgressCallBack	m_lpSyncProgressCallBack;
	LPVOID					m_lpSyncProgressCallBackObject;
	SyncPopupErrorCallBack m_lpSyncPopupErrorCallBack;
	LPVOID					m_lpSyncPopupErrorCallBackObject;

	pthread_t			m_hSyncThread;

	// Cancel a sync and stop/exit sync thread
	bool				m_bExit;
	bool				m_bCancel;
	ULONG				m_ulStatus;
	pthread_cond_t		m_hExitSignal;
	pthread_mutex_t		m_hMutex;			///> Lock used for the internal state (m_bExit, m_bCancel, m_ulStatus

	LPMSPROVIDER	m_lpOfflineProvider;
	LPMSPROVIDER	m_lpOnlineProvider;
	LPMAPISUP		m_lpSupport;
	ULONG			m_cbStoreID;
	LPENTRYID		m_lpStoreID;

	std::string		m_strProfileName;
	LPMAPISESSION	m_lpSession;

	std::string		m_strOfflineProfile;
	std::string		m_strOnlineProfile;

	LPMAPISESSION	m_lpOfflineSession;
	LPMAPISESSION	m_lpOnlineSession;

	ULONG			m_ulWaitTime;		///> Time to wait between syns in seconds
	ULONG			m_ulTotalSteps;		///> Total number of steps for all message changes
	ULONG			m_ulTotalStep;		///> Total number of steps done for all message changes

	ULONG			m_ulOnlineAdviseConnection;		///> Connection for advise of online store
	std::list<LPNOTIFICATION> m_lstNewMailNotifs;	///> Queue of new mail notifications

	ULONG			m_ulSyncFlags;
	ULONG			m_ulSyncOnNotify;	///> Notifications to start sync on

	HRESULT			m_hrSyncError;		///> Last error code from syncing
	ECLogger		*m_lpLogger;

	eResyncReason	m_eResyncReason;
	bool			m_bSupressErrorPopup;
	LCID			m_localeOLK;

private:
	class xMAPIAdviseSink _zcp_final : public IMAPIAdviseSink {
	public:
		HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
		ULONG __stdcall AddRef(void) _zcp_override;
		ULONG __stdcall Release(void) _zcp_override;

		ULONG __stdcall OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifs);
	} m_xMAPIAdviseSink;

	class xECSync _zcp_final : public IECSync {
		ULONG AddRef(void) _zcp_override;
		ULONG Release(void) _zcp_override;
		HRESULT QueryInterface(REFIID refiid, void **lpvoid) _zcp_override;
		HRESULT FirstFolderSync();
		HRESULT StartSync();
		HRESULT StopSync();
		HRESULT CancelSync();
		HRESULT SetSyncStatusCallBack(LPVOID lpSyncStatusCallBackObject, SyncStatusCallBack lpSyncStatusCallBack);
		HRESULT SetSyncProgressCallBack(LPVOID lpSyncProgressCallBackObject, SyncProgressCallBack lpSyncProgressCallBack);
		HRESULT SetSyncPopupErrorCallBack(LPVOID lpSyncPopupErrorCallBackObject, SyncPopupErrorCallBack lpSyncPopupErrorCallBack);
		HRESULT GetSyncStatus(ULONG * lpulStatus);
		HRESULT SetWaitTime(ULONG ulWaitTime);
		HRESULT GetWaitTime(ULONG* lpulWaitTime);
		HRESULT SetSyncOnNotify(ULONG ulEventMask);
		HRESULT GetSyncOnNotify(ULONG* lpulEventMask);
		HRESULT GetSyncError();
		HRESULT SetThreadLocale(LCID locale);
		HRESULT Logoff();
	} m_xECSync;

	friend class ECSyncDataProvider;
};

#endif

#endif // ECSYNC_H
