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
#include <memory>
#include <new>          // std::bad_alloc
#include <list>          // std::list
#include "ArchiveControlImpl.h"
#include "ECArchiverLogger.h"
#include "ArchiverSession.h"
#include "ArchiveStateCollector.h"
#include "ArchiveStateUpdater.h"
#include <kopano/userutil.h>
#include <kopano/mapiext.h>
#include "helpers/StoreHelper.h"
#include "operations/copier.h"
#include "operations/deleter.h"
#include "operations/stubber.h"
#include <kopano/restrictionutil.h>
#include <kopano/ECConfig.h>
#include "ECIterators.h"
#include <kopano/ECRestriction.h>
#include "HrException.h"
#include "ArchiveManage.h"
#include <kopano/MAPIErrors.h>

using namespace za::helpers;
using namespace za::operations;

/**
 * Create a new Archive object.
 *
 * @param[in]	lpSession
 *					Pointer to the Session.
 * @param[in]	lpConfig
 *					Pointer to an ECConfig object that determines the operational options.
 * @param[in]	lpLogger
 *					Pointer to an ECLogger object that's used for logging.
 * @param[in]	bForceCleanup	Force a cleanup operation to continue, even
 * 								if the settings aren't safe.
 * @param[out]	lpptrArchiver
 *					Pointer to a ArchivePtr that will be assigned the address of the returned object.
 */
HRESULT ArchiveControlImpl::Create(ArchiverSessionPtr ptrSession, ECConfig *lpConfig, ECLogger *lpLogger, bool bForceCleanup, ArchiveControlPtr *lpptrArchiveControl)
{
	HRESULT hr = hrSuccess;
	std::unique_ptr<ArchiveControlImpl> ptrArchiveControl;

	try {
		ptrArchiveControl.reset(new ArchiveControlImpl(ptrSession, lpConfig, lpLogger, bForceCleanup));
	} catch (std::bad_alloc &) {
		hr = MAPI_E_NOT_ENOUGH_MEMORY;
		goto exit;
	}

	hr = ptrArchiveControl->Init();
	if (hr != hrSuccess)
		goto exit;

	*lpptrArchiveControl = std::move(ptrArchiveControl);
exit:
	return hr;
}

/**
 * @param[in]	lpSession
 *					Pointer to the Session.
 * @param[in]	lpConfig
 *					Pointer to an ECConfig object that determines the operational options.
 * @param[in]	lpLogger
 *					Pointer to an ECLogger object that's used for logging.
  * @param[in]	bForceCleanup	Force a cleanup operation to continue, even
 * 								if the settings aren't safe.
 */
ArchiveControlImpl::ArchiveControlImpl(ArchiverSessionPtr ptrSession, ECConfig *lpConfig, ECLogger *lpLogger, bool bForceCleanup)
: m_ptrSession(ptrSession)
, m_lpConfig(lpConfig)
, m_lpLogger(new ECArchiverLogger(lpLogger))
, m_bArchiveEnable(true)
, m_ulArchiveAfter(30)
, m_bDeleteEnable(false)
, m_bDeleteUnread(false)
, m_ulDeleteAfter(0)
, m_bStubEnable(false)
, m_bStubUnread(false)
, m_ulStubAfter(0)
, m_bPurgeEnable(false)
, m_ulPurgeAfter(2555)
, m_cleanupAction(caStore)
, m_bCleanupFollowPurgeAfter(false)
, m_bForceCleanup(bForceCleanup)
{ }

ArchiveControlImpl::~ArchiveControlImpl()
{
	m_lpLogger->Release();
}

/**
 * Initialize the Archiver object.
 */
HRESULT ArchiveControlImpl::Init()
{
	m_bArchiveEnable = parseBool(m_lpConfig->GetSetting("archive_enable", "", "no"));
	m_ulArchiveAfter = atoi(m_lpConfig->GetSetting("archive_after", "", "30"));

	m_bDeleteEnable = parseBool(m_lpConfig->GetSetting("delete_enable", "", "no"));
	m_bDeleteUnread = parseBool(m_lpConfig->GetSetting("delete_unread", "", "no"));
	m_ulDeleteAfter = atoi(m_lpConfig->GetSetting("delete_after", "", "0"));

	m_bStubEnable = parseBool(m_lpConfig->GetSetting("stub_enable", "", "no"));
	m_bStubUnread = parseBool(m_lpConfig->GetSetting("stub_unread", "", "no"));
	m_ulStubAfter = atoi(m_lpConfig->GetSetting("stub_after", "", "0"));

	m_bPurgeEnable = parseBool(m_lpConfig->GetSetting("purge_enable", "", "no"));
	m_ulPurgeAfter = atoi(m_lpConfig->GetSetting("purge_after", "", "2555"));

	const char *lpszCleanupAction = m_lpConfig->GetSetting("cleanup_action");
	if (lpszCleanupAction == NULL || *lpszCleanupAction == '\0') {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Empty cleanup_action specified in config.");
		return MAPI_E_INVALID_PARAMETER;
	}

	if (strcasecmp(lpszCleanupAction, "delete") == 0)
		m_cleanupAction = caDelete;
	else if (strcasecmp(lpszCleanupAction, "store") == 0)
		m_cleanupAction = caStore;
	else if (strcasecmp(lpszCleanupAction, "none") == 0)
		m_cleanupAction = caNone;
	else {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unknown cleanup_action specified in config: '%s'", lpszCleanupAction);
		return MAPI_E_INVALID_PARAMETER;
	}

	m_bCleanupFollowPurgeAfter = parseBool(m_lpConfig->GetSetting("cleanup_follow_purge_after", "", "no"));
	GetSystemTimeAsFileTime(&m_ftCurrent);
	return hrSuccess;
}

/**
 * Archive messages for all users. Optionaly only user that have their store on the server
 * to which the archiver is connected will have their messages archived.
 *
 * @param[in]	bLocalOnly
 *					If set to true only  messsages for users that have their store on the local server
 *					will be archived.
 */
eResult ArchiveControlImpl::ArchiveAll(bool bLocalOnly, bool bAutoAttach, unsigned int ulFlags)
{
	HRESULT hr = hrSuccess;

	if (ulFlags != ArchiveManage::Writable &&
	    ulFlags != ArchiveManage::ReadOnly && ulFlags != 0)
		return MAPIErrorToArchiveError(MAPI_E_INVALID_PARAMETER);

	if (bAutoAttach || parseBool(m_lpConfig->GetSetting("enable_auto_attach"))) {
		ArchiveStateCollectorPtr ptrArchiveStateCollector;
		ArchiveStateUpdaterPtr ptrArchiveStateUpdater;

		hr = ArchiveStateCollector::Create(m_ptrSession, m_lpLogger, &ptrArchiveStateCollector);
		if (hr != hrSuccess)
			return MAPIErrorToArchiveError(hr);

		hr = ptrArchiveStateCollector->GetArchiveStateUpdater(&ptrArchiveStateUpdater);
		if (hr != hrSuccess)
			return MAPIErrorToArchiveError(hr);

		if (ulFlags == 0) {
			if (parseBool(m_lpConfig->GetSetting("auto_attach_writable")))
				ulFlags = ArchiveManage::Writable;
			else
				ulFlags = ArchiveManage::ReadOnly;
		}

		hr = ptrArchiveStateUpdater->UpdateAll(ulFlags);
		if (hr != hrSuccess)
			return MAPIErrorToArchiveError(hr);
	}

	return MAPIErrorToArchiveError(ProcessAll(bLocalOnly, &ArchiveControlImpl::DoArchive));
}

/**
 * Archive the messages of a particular user.
 *
 * @param[in]	strUser
 *					The username for which to archive the messages.
 */
eResult ArchiveControlImpl::Archive(const tstring &strUser, bool bAutoAttach, unsigned int ulFlags)
{
	HRESULT hr = hrSuccess;
    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): function entry.");
    ScopedUserLogging sul(m_lpLogger, strUser);

	if (ulFlags != ArchiveManage::Writable && ulFlags != ArchiveManage::ReadOnly && ulFlags != 0) {
		hr = MAPI_E_INVALID_PARAMETER;
        m_lpLogger->Log(EC_LOGLEVEL_INFO, "ArchiveControlImpl::Archive(): invalid parameter.");
		goto exit;
	}

	if (bAutoAttach || parseBool(m_lpConfig->GetSetting("enable_auto_attach"))) {
		ArchiveStateCollectorPtr ptrArchiveStateCollector;
		ArchiveStateUpdaterPtr ptrArchiveStateUpdater;

        m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): about to create collector.");
		hr = ArchiveStateCollector::Create(m_ptrSession, m_lpLogger, &ptrArchiveStateCollector);
		if (hr != hrSuccess)
			goto exit;

        m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): about to get updater.");
		hr = ptrArchiveStateCollector->GetArchiveStateUpdater(&ptrArchiveStateUpdater);
		if (hr != hrSuccess)
			goto exit;

		if (ulFlags == 0) {
			if (parseBool(m_lpConfig->GetSetting("auto_attach_writable")))
				ulFlags = ArchiveManage::Writable;
			else
				ulFlags = ArchiveManage::ReadOnly;
		}

        m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): about to update store of user %ls. Flags: 0x%08X", strUser.c_str(), ulFlags);
		hr = ptrArchiveStateUpdater->Update(strUser, ulFlags);
		if (hr != hrSuccess)
			goto exit;
	}

    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): about to do real archive run.");
	hr = DoArchive(strUser);

exit:
    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive() at exit. Return code before transformation: 0x%08x (%s).", hr, GetMAPIErrorMessage(hr));
	return MAPIErrorToArchiveError(hr);
}

/**
 * Cleanup the archive(s) of all users. Optionaly only user that have their store on the server
 * to which the archiver is connected will have their messages archived.
 *
 * @param[in]	bLocalOnly
 *					If set to true only  messsages for users that have their store on the local server
 *					will be archived.
 */
eResult ArchiveControlImpl::CleanupAll(bool bLocalOnly)
{
	HRESULT hr = hrSuccess;

	hr = CheckSafeCleanupSettings();

	if (hr == hrSuccess)
		hr = ProcessAll(bLocalOnly, &ArchiveControlImpl::DoCleanup);

	return MAPIErrorToArchiveError(hr);
}

/**
 * Cleanup the archive(s) of a particular user.
 * Cleaning up is currently defined as detecting which messages were deleted
 * from the primary store and moving the archives of those messages to the
 * special deleted folder.
 *
 * @param[in]	strUser
 *					The username for which to archive the messages.
 */
eResult ArchiveControlImpl::Cleanup(const tstring &strUser)
{
	HRESULT hr = hrSuccess;
    ScopedUserLogging sul(m_lpLogger, strUser);
	
	hr = CheckSafeCleanupSettings();

	if (hr == hrSuccess)
		hr = DoCleanup(strUser);

	return MAPIErrorToArchiveError(hr);
}

/**
 * Process all users.
 *
 * @param[in]	bLocalOnly	Limit to users that have a store on the local server.
 * @param[in]	fnProcess	The method to execute to do the actual processing.
 */ 
HRESULT ArchiveControlImpl::ProcessAll(bool bLocalOnly, fnProcess_t fnProcess)
{
	typedef std::list<tstring> StringList;
	
	HRESULT hr = hrSuccess;
	StringList lstUsers;
	UserList lstUserEntries;
	bool bHaveErrors = false;

	hr = GetArchivedUserList(m_lpLogger, 
							 m_ptrSession->GetMAPISession(),
							 m_ptrSession->GetSSLPath(),
							 m_ptrSession->GetSSLPass(),
							 &lstUsers, bLocalOnly);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to obtain user list. (hr=0x%08x)", hr);
		goto exit;
	}

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Processing " SIZE_T_PRINTF "%s users.", lstUsers.size(), (bLocalOnly ? " local" : ""));
	for (const auto &user : lstUsers) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Processing user '" TSTRING_PRINTF "'.", user.c_str());
		HRESULT hrTmp = (this->*fnProcess)(user);
		if (FAILED(hrTmp)) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to process user '" TSTRING_PRINTF "'. (hr=0x%08x)", user.c_str(), hrTmp);
			bHaveErrors = true;
		} else if (hrTmp == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Errors occurred while processing user '" TSTRING_PRINTF "'.", user.c_str());
			bHaveErrors = true;
		}
	}

exit:
	if (hr == hrSuccess && bHaveErrors)
		hr = MAPI_W_PARTIAL_COMPLETION;

	return hr;
}

/**
 * Get the name of a folder from a IMAPIFolder object
 *
 * @param[in]  folder  Pointer to IMAPIFolder object to be queried
 * @return      tstring containing folder name
 */
tstring
ArchiveControlImpl::getfoldername(LPMAPIFOLDER folder)
{
    SPropValuePtr foldername;
    HrGetOneProp(folder, PR_DISPLAY_NAME, &foldername);
    return tstring(foldername->Value.LPSZ);
}

/**
 * Remove soft-deleted items from a IMAPIFolder object
 *
 * @param[in]  folder  Pointer to IMAPIFolder object to be queried
 * @param[in]  strUser    String constaining name of store
 * @return      HRESULT. 0 on succes, error code on failure
 */
HRESULT
ArchiveControlImpl::purgesoftdeleteditems(LPMAPIFOLDER folder, const tstring& strUser)
{
   HRESULT hr = hrSuccess;
    MAPITablePtr table;
    if ((hr = folder->GetContentsTable(SHOW_SOFT_DELETES, &table)) != hrSuccess) {
        m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get search folder contents table. (hr=%s)", stringify(hr, true).c_str());
    } else {
        SizedSPropTagArray(1, props) = {1, {PR_ENTRYID}};
        if ((hr = table->SetColumns((LPSPropTagArray)&props, 0)) != hrSuccess) {
            m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to set columns on table. (hr=%s)", stringify(hr, true).c_str());
        } else {
            unsigned int found = 0;
            unsigned int totalfound = 0;
            do {
                SRowSetPtr rowSet;
                if ((hr = table->QueryRows(100, 0, &rowSet)) != hrSuccess) {
                    m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get rows from table. (hr=%s)", stringify(hr, true).c_str());
                } else {
                    found = rowSet.size();
                    totalfound += found;
                    EntryListPtr ptrEntryList;
                    hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &ptrEntryList);
                    if (hr == hrSuccess) {
                        hr = MAPIAllocateMore(sizeof(SBinary), ptrEntryList, (LPVOID*)&ptrEntryList->lpbin);
                        if (hr == hrSuccess) {
                            ptrEntryList->cValues = 1;
                            for (unsigned int i = 0; i < found; ++i) {
                                ptrEntryList->lpbin[0].cb  = rowSet->aRow[i].lpProps[0].Value.bin.cb;
                                ptrEntryList->lpbin[0].lpb = rowSet->aRow[i].lpProps[0].Value.bin.lpb;
                                if ((hr = folder->DeleteMessages(ptrEntryList, 0, NULL, DELETE_HARD_DELETE)) != hrSuccess) {
                                    m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to delete message. (hr=%s)", stringify(hr, true).c_str());
                                }
                            }
                        }
                    }
                }
            } while (found);
            if (totalfound) {
                m_lpLogger->Log(
                    EC_LOGLEVEL_INFO,
                    "Store %ls: %u soft-deleted messages removed from folder %ls",
                    strUser.c_str(),
                    totalfound,
                    getfoldername(folder).c_str()
                );
            }
        }
    }
    return hr;
}

/**
 * Remove soft-deleted items of a user store
 *
 * @param[in]  strUser     tstring containing user name
 * @return      HRESULT. 0 on succes, error code on failure
 */
HRESULT
ArchiveControlImpl::purgesoftdeletedmessages(const tstring& strUser)
{
    MsgStorePtr store;
    HRESULT hr = m_ptrSession->OpenStoreByName(strUser, &store);
    if (hr != hrSuccess) {
        m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to open user store. (hr=%s)", stringify(hr, true).c_str());
    } else {
        SPropValuePtr ptrPropValue;
        if ((hr = HrGetOneProp(store, PR_IPM_SUBTREE_ENTRYID, &ptrPropValue)) != hrSuccess) {
            m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get PR_IPM_SUBTREE_ENTRYID. (hr=%s)", stringify(hr, true).c_str());
        } else {
            MAPIFolderPtr ipmSubtree;
            ULONG type = 0;
            if ((hr = store->OpenEntry(ptrPropValue->Value.bin.cb, (LPENTRYID)ptrPropValue->Value.bin.lpb, NULL, MAPI_BEST_ACCESS|fMapiDeferredErrors, &type, &ipmSubtree)) != hrSuccess) {
                m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to open ipmSubtree. (hr=%s)", stringify(hr, true).c_str());
            } else {
                ECFolderIterator iEnd;
                for (ECFolderIterator i = ECFolderIterator(ipmSubtree, fMapiDeferredErrors, 0); i != iEnd; ++i) {
                    hr = purgesoftdeleteditems(*i, strUser);
                }
            }
        }
    }
    return hr;
}

/**
 * Perform the actual archive operation for a specific user.
 * 
 * @param[in]	strUser	tstring containing user name
 */
HRESULT ArchiveControlImpl::DoArchive(const tstring& strUser)
{
	HRESULT hr = hrSuccess;
	MsgStorePtr ptrUserStore;
	StoreHelperPtr ptrStoreHelper;
	MAPIFolderPtr ptrSearchArchiveFolder;
	MAPIFolderPtr ptrSearchDeleteFolder;
	MAPIFolderPtr ptrSearchStubFolder;
	ObjectEntryList lstArchives;
	bool bHaveErrors = false;

	CopierPtr	ptrCopyOp;
	DeleterPtr	ptrDeleteOp;
	StubberPtr	ptrStubOp;

	if (strUser.empty()) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Archiving store for user '" TSTRING_PRINTF "'", strUser.c_str());

	hr = m_ptrSession->OpenStoreByName(strUser, &ptrUserStore);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to open store. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	PROPMAP_INIT_NAMED_ID(ARCHIVE_STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidStoreEntryIds)
	PROPMAP_INIT_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidItemEntryIds)
	PROPMAP_INIT_NAMED_ID(ORIGINAL_SOURCEKEY, PT_BINARY, PSETID_Archive, dispidOrigSourceKey)
	PROPMAP_INIT_NAMED_ID(STUBBED, PT_BOOLEAN, PSETID_Archive, dispidStubbed)
	PROPMAP_INIT_NAMED_ID(DIRTY, PT_BOOLEAN, PSETID_Archive, dispidDirty)
	PROPMAP_INIT(ptrUserStore)

	hr = StoreHelper::Create(ptrUserStore, &ptrStoreHelper);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to create store helper. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	hr = ptrStoreHelper->GetArchiveList(&lstArchives);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_CORRUPT_DATA) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "List of archives is corrupt for user '" TSTRING_PRINTF "', skipping user.", strUser.c_str());
			hr = hrSuccess;
		} else
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get list of archives. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	if (lstArchives.empty()) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "'" TSTRING_PRINTF "' has no attached archives", strUser.c_str());
		goto exit;
	}

	hr = ptrStoreHelper->GetSearchFolders(&ptrSearchArchiveFolder, &ptrSearchDeleteFolder, &ptrSearchStubFolder);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get the search folders. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	// Create and hook the three dependent steps
	if (m_bArchiveEnable && m_ulArchiveAfter >= 0) {
		SizedSPropTagArray(5, sptaExcludeProps) = {5, {PROP_ARCHIVE_STORE_ENTRYIDS, PROP_ARCHIVE_ITEM_ENTRYIDS, PROP_STUBBED, PROP_DIRTY, PROP_ORIGINAL_SOURCEKEY}};
		ptrCopyOp.reset(new Copier(m_ptrSession, m_lpConfig, m_lpLogger, lstArchives, (LPSPropTagArray)&sptaExcludeProps, m_ulArchiveAfter, true));
	}

	if (m_bDeleteEnable && m_ulDeleteAfter >= 0) {
		ptrDeleteOp.reset(new Deleter(m_lpLogger, m_ulDeleteAfter, m_bDeleteUnread));
		if (ptrCopyOp)
			ptrCopyOp->SetDeleteOperation(ptrDeleteOp);
	}

	if (m_bStubEnable && m_ulStubAfter >= 0) {
		ptrStubOp.reset(new Stubber(m_lpLogger, PROP_STUBBED, m_ulStubAfter, m_bStubUnread));
		if (ptrCopyOp)
			ptrCopyOp->SetStubOperation(ptrStubOp);
	}

	// Now execute them
	if (ptrCopyOp) {
		// Archive all unarchived messages that are old enough
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Archiving messages");
		hr = ProcessFolder(ptrSearchArchiveFolder, ptrCopyOp);
		if (FAILED(hr)) {
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to archive messages. (hr=%s)", stringify(hr, true).c_str());
			goto exit;
		} else if (hr == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Some message could not be archived");
			bHaveErrors = true;
			hr = hrSuccess;
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done archiving messages");
	}

	if (ptrDeleteOp) {
		// First delete all messages that are elegible for deletion, so we don't unneccesary stub them first
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Deleting old messages");
		hr = ProcessFolder(ptrSearchDeleteFolder, ptrDeleteOp);
		if (FAILED(hr)) {
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to delete old messages. (hr=%s)", stringify(hr, true).c_str());
			goto exit;
		} else if (hr == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Some message could not be deleted");
			bHaveErrors = true;
			hr = hrSuccess;
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done deleting messages");
	}

	if (ptrStubOp) {
		// Now stub the remaing messages (if they're old enough)
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Stubbing messages");
		hr = ProcessFolder(ptrSearchStubFolder, ptrStubOp);
		if (FAILED(hr)) {
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to stub messages. (hr=%s)", stringify(hr, true).c_str());
			goto exit;
		} else if (hr == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Some message could not be stubbed");
			bHaveErrors = true;
			hr = hrSuccess;
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done stubbing messages");
	}

	if (m_bPurgeEnable) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Purging archive(s)");
		hr = PurgeArchives(lstArchives);
		if (FAILED(hr)) {
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to purge archive(s). (hr=%s)", stringify(hr, true).c_str());
			goto exit;
		} else if (hr == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Some archives could not be purged");
			bHaveErrors = true;
			hr = hrSuccess;
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done purging archive(s)");
	}

exit:
	if (hr == hrSuccess && bHaveErrors)
		hr = MAPI_W_PARTIAL_COMPLETION;

	return hr;
}

/**
 * Perform the actual cleanup operation for a specific user.
 * 
 * @param[in]	strUser	tstring containing user name
 */
HRESULT ArchiveControlImpl::DoCleanup(const tstring &strUser)
{
	HRESULT hr;
	MsgStorePtr ptrUserStore;
	StoreHelperPtr ptrStoreHelper;
	ObjectEntryList lstArchives;
	SRestrictionPtr ptrRestriction;

	if (strUser.empty())
		return MAPI_E_INVALID_PARAMETER;

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Cleanup store for user '" TSTRING_PRINTF "', mode=%s", strUser.c_str(), m_lpConfig->GetSetting("cleanup_action"));
	
	if (m_bCleanupFollowPurgeAfter) {
		ULARGE_INTEGER li;
		SPropValue sPropRefTime;

		const ECOrRestriction resDefault(
			ECAndRestriction(
				ECExistRestriction(PR_MESSAGE_DELIVERY_TIME) +
				ECPropertyRestriction(RELOP_LT, PR_MESSAGE_DELIVERY_TIME, &sPropRefTime, ECRestriction::Cheap)
			) +
			ECAndRestriction(
				ECExistRestriction(PR_CLIENT_SUBMIT_TIME) +
				ECPropertyRestriction(RELOP_LT, PR_CLIENT_SUBMIT_TIME, &sPropRefTime, ECRestriction::Cheap)
			)
		);

		li.LowPart = m_ftCurrent.dwLowDateTime;
		li.HighPart = m_ftCurrent.dwHighDateTime;
		
		li.QuadPart -= (m_ulPurgeAfter * _DAY);
		
		sPropRefTime.ulPropTag = PROP_TAG(PT_SYSTIME, 0);
		sPropRefTime.Value.ft.dwLowDateTime = li.LowPart;
		sPropRefTime.Value.ft.dwHighDateTime = li.HighPart;

		hr = resDefault.CreateMAPIRestriction(&ptrRestriction, 0);
		if (hr != hrSuccess)
			return hr;
	}

	hr = m_ptrSession->OpenStoreByName(strUser, &ptrUserStore);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to open store. (hr=0x%08x)", hr);
		return hr;
	}

	hr = StoreHelper::Create(ptrUserStore, &ptrStoreHelper);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to create store helper. (hr=0x%08x)", hr);
		return hr;
	}

	hr = ptrStoreHelper->GetArchiveList(&lstArchives);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_CORRUPT_DATA) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "List of archives is corrupt for user '" TSTRING_PRINTF "', skipping user.", strUser.c_str());
			hr = hrSuccess;
		} else
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get list of archives. (hr=0x%08x)", hr);
		return hr;
	}

	if (lstArchives.empty()) {
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "'" TSTRING_PRINTF "' has no attached archives", strUser.c_str());
		return hr;
	}

	for (const auto &arc : lstArchives) {
		HRESULT hrTmp = hrSuccess;

		hrTmp = CleanupArchive(arc, ptrUserStore, ptrRestriction);
		if (hrTmp != hrSuccess)
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to cleanup archive. (hr=0x%08x)", hr);
	}
	return hrSuccess;
}

/**
 * Process a search folder and place an additional restriction on it to get the messages
 * that should really be archived.
 *
 * @param[in]	ptrFolder
 *					A MAPIFolderPtr that points to the search folder to be processed.
 * @param[in]	lpArchiveOperation
 *					The pointer to a IArchiveOperation derived object that's used to perform
 *					the actual processing.
 * @param[in]	ulAge
 *					The age in days since the message was delivered, that a message must be before
 *					it will be processed.
 * @param[in]	bProcessUnread
 *					If set to true, unread messages will also be processed. Otherwise unread message
 *					will be left untouched.
 */
HRESULT ArchiveControlImpl::ProcessFolder(MAPIFolderPtr &ptrFolder, ArchiveOperationPtr ptrArchiveOperation)
{
	HRESULT hr = hrSuccess;
	MAPITablePtr ptrTable;
	SRestrictionPtr ptrRestriction;
	SSortOrderSetPtr ptrSortOrder;
	SRowSetPtr ptrRowSet;
	MessagePtr ptrMessage;
	bool bHaveErrors = false;
	const tstring strFolderRestore = m_lpLogger->GetFolder();

	SizedSPropTagArray(3, sptaProps) = {3, {PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID}};

	hr = ptrFolder->GetContentsTable(fMapiDeferredErrors, &ptrTable);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get search folder contents table. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	hr = ptrTable->SetColumns((LPSPropTagArray)&sptaProps, TBL_BATCH);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to set columns on table. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	hr = ptrArchiveOperation->GetRestriction(ptrFolder, &ptrRestriction);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get restriction from operation. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	hr = ptrTable->Restrict(ptrRestriction, TBL_BATCH);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to set restriction on table. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	hr = MAPIAllocateBuffer(CbNewSSortOrderSet(1), &ptrSortOrder);
	if (hr != hrSuccess)
		goto exit;

	ptrSortOrder->cSorts = 1;
	ptrSortOrder->cCategories = 0;
	ptrSortOrder->cExpanded = 0;
	ptrSortOrder->aSort[0].ulPropTag = PR_PARENT_ENTRYID;
	ptrSortOrder->aSort[0].ulOrder = TABLE_SORT_ASCEND ;

	hr = ptrTable->SortTable(ptrSortOrder, TBL_BATCH);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to sort table. (hr=%s)", stringify(hr, true).c_str());
		goto exit;
	}

	do {
		hr = ptrTable->QueryRows(50, 0, &ptrRowSet);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get rows from table. (hr=%s)", stringify(hr, true).c_str());
			goto exit;
		}

		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Processing batch of %u messages", ptrRowSet.size());
		for (ULONG i = 0; i < ptrRowSet.size(); ++i) {
			hr = ptrArchiveOperation->ProcessEntry(ptrFolder, ptrRowSet[i].cValues, ptrRowSet[i].lpProps);
			if (hr != hrSuccess) {
				bHaveErrors = true;
				m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to process entry. (hr=%s)", stringify(hr, true).c_str());
				if (hr == MAPI_E_STORE_FULL) {
					m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Disk full or over quota.");
					goto exit;
				}
				continue;
			}
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done processing batch");
	} while (ptrRowSet.size() == 50);

exit:
	if (hr == hrSuccess && bHaveErrors)
		hr = MAPI_W_PARTIAL_COMPLETION;

	m_lpLogger->SetFolder(strFolderRestore);

	return hr;
}

/**
 * Purge a set of archives. Purging an archive is defined as deleting all
 * messages that are older than a set amount of days.
 *
 * @param[in]	lstArchives		The list of archives to purge.
 */
HRESULT ArchiveControlImpl::PurgeArchives(const ObjectEntryList &lstArchives)
{
	HRESULT hr = hrSuccess;
	bool bErrorOccurred = false;
	LPSRestriction lpRestriction = NULL;
	SPropValue sPropCreationTime;
	ULARGE_INTEGER li;
	SRowSetPtr ptrRowSet;

	SizedSPropTagArray(2, sptaFolderProps) = {2, {PR_ENTRYID, PR_DISPLAY_NAME}};
    enum {IDX_ENTRYID, IDX_DISPLAY_NAME};

	// Create the common restriction that determines which messages are old enough to purge.
	CREATE_RESTRICTION(lpRestriction);
	DATA_RES_PROPERTY_CHEAP(lpRestriction, *lpRestriction, RELOP_LT, PR_MESSAGE_DELIVERY_TIME, &sPropCreationTime);

	li.LowPart = m_ftCurrent.dwLowDateTime;
	li.HighPart = m_ftCurrent.dwHighDateTime;

	li.QuadPart -= (m_ulPurgeAfter * _DAY);

	sPropCreationTime.ulPropTag = PR_MESSAGE_DELIVERY_TIME;
	sPropCreationTime.Value.ft.dwLowDateTime = li.LowPart;
	sPropCreationTime.Value.ft.dwHighDateTime = li.HighPart;

	for (const auto &arc : lstArchives) {
		MsgStorePtr ptrArchiveStore;
		MAPIFolderPtr ptrArchiveRoot;
		ULONG ulType = 0;
		MAPITablePtr ptrFolderTable;
		SRowSetPtr ptrFolderRows;

		hr = m_ptrSession->OpenStore(arc.sStoreEntryId, &ptrArchiveStore);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to open archive. (entryid=%s, hr=%s)", arc.sStoreEntryId.tostring().c_str(), stringify(hr, true).c_str());
			bErrorOccurred = true;
			continue;
		}

		// Purge root of archive
		hr = PurgeArchiveFolder(ptrArchiveStore, arc.sItemEntryId, lpRestriction);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to purge archive root. (entryid=%s, hr=%s)", arc.sItemEntryId.tostring().c_str(), stringify(hr, true).c_str());
			bErrorOccurred = true;
			continue;
		}

		// Get all subfolders and purge those as well.
		hr = ptrArchiveStore->OpenEntry(arc.sItemEntryId.size(), arc.sItemEntryId, &ptrArchiveRoot.iid, MAPI_BEST_ACCESS|fMapiDeferredErrors, &ulType, &ptrArchiveRoot);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to open archive root. (entryid=%s, hr=%s)", arc.sItemEntryId.tostring().c_str(), stringify(hr, true).c_str());
			bErrorOccurred = true;
			continue;
		}

		hr = ptrArchiveRoot->GetHierarchyTable(CONVENIENT_DEPTH|fMapiDeferredErrors, &ptrFolderTable);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to get archive hierarchy table. (hr=%s)", stringify(hr, true).c_str());
			bErrorOccurred = true;
			continue;
		}

		hr = ptrFolderTable->SetColumns((LPSPropTagArray)&sptaFolderProps, TBL_BATCH);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to select folder table columns. (hr=%s)", stringify(hr, true).c_str());
			bErrorOccurred = true;
			continue;
		}

		while (true) {
			hr = ptrFolderTable->QueryRows(50, 0, &ptrFolderRows);
			if (hr != hrSuccess) {
				m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get rows from folder table. (hr=%s)", stringify(hr, true).c_str());
				goto exit;
			}

			for (ULONG i = 0; i < ptrFolderRows.size(); ++i) {
				ScopedFolderLogging sfl(m_lpLogger, ptrFolderRows[i].lpProps[IDX_DISPLAY_NAME].ulPropTag == PR_DISPLAY_NAME ? ptrFolderRows[i].lpProps[IDX_DISPLAY_NAME].Value.LPSZ : _T("<Unnamed>"));

				hr = PurgeArchiveFolder(ptrArchiveStore, ptrFolderRows[i].lpProps[IDX_ENTRYID].Value.bin, lpRestriction);
				if (hr != hrSuccess) {
					m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to purge archive folder. (entryid=%s, hr=%s)", bin2hex(ptrFolderRows[i].lpProps[IDX_ENTRYID].Value.bin.cb, ptrFolderRows[i].lpProps[IDX_ENTRYID].Value.bin.lpb).c_str(), stringify(hr, true).c_str());
					bErrorOccurred = true;
				}
			}

			if (ptrFolderRows.size() < 50)
				break;
		}
	}

exit:
	MAPIFreeBuffer(lpRestriction);
	if (hr == hrSuccess && bErrorOccurred)
		hr = MAPI_W_PARTIAL_COMPLETION;

	return hr;
}

/**
 * Purge an archive folder.
 *
 * @param[in]	ptrArchive		The archive store containing the folder to purge.
 * @param[in]	folderEntryID	The entryid of the folder to purge.
 * @param[in]	lpRestriction	The restriction to use to determine which messages to delete.
 */
HRESULT ArchiveControlImpl::PurgeArchiveFolder(MsgStorePtr &ptrArchive, const entryid_t &folderEntryID, const LPSRestriction lpRestriction)
{
	HRESULT hr;
	ULONG ulType = 0;
	MAPIFolderPtr ptrFolder;
	MAPITablePtr ptrContentsTable;
	std::list<entryid_t> lstEntries;
	SRowSetPtr ptrRows;
	EntryListPtr ptrEntryList;
	ULONG ulIdx = 0;

	SizedSPropTagArray(1, sptaTableProps) = {1, {PR_ENTRYID}};

	hr = ptrArchive->OpenEntry(folderEntryID.size(), folderEntryID, &ptrFolder.iid, MAPI_BEST_ACCESS|fMapiDeferredErrors, &ulType, &ptrFolder);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to open archive folder. (entryid=%s, hr=%s)", folderEntryID.tostring().c_str(), stringify(hr, true).c_str());
		return hr;
	}

	hr = ptrFolder->GetContentsTable(fMapiDeferredErrors, &ptrContentsTable);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to open contents table. (hr=%s)", stringify(hr, true).c_str());
		return hr;
	}

	hr = ptrContentsTable->SetColumns((LPSPropTagArray)&sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to select table columns. (hr=%s)", stringify(hr, true).c_str());
		return hr;
	}

	hr = ptrContentsTable->Restrict(lpRestriction, TBL_BATCH);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to restrict contents table. (hr=%s)", stringify(hr, true).c_str());
		return hr;
	}

	while (true) {
		hr = ptrContentsTable->QueryRows(50, 0, &ptrRows);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to get rows from contents table. (hr=%s)", stringify(hr, true).c_str());
			return hr;
		}

		for (ULONG i = 0; i < ptrRows.size(); ++i)
			lstEntries.push_back(ptrRows[i].lpProps[0].Value.bin);

		if (ptrRows.size() < 50)
			break;
	}

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Purging %lu messaged from archive folder", lstEntries.size());
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &ptrEntryList);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(lstEntries.size() * sizeof(SBinary), ptrEntryList, (LPVOID*)&ptrEntryList->lpbin);
	if (hr != hrSuccess)
		return hr;

	ptrEntryList->cValues = lstEntries.size();
	for (const auto &e : lstEntries) {
		ptrEntryList->lpbin[ulIdx].cb = e.size();
		ptrEntryList->lpbin[ulIdx++].lpb = e;
	}

	hr = ptrFolder->DeleteMessages(ptrEntryList, 0, NULL, 0);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to delete %u messages. (hr=%s)", ptrEntryList->cValues, stringify(hr, true).c_str());
		return hr;
	}
	return hrSuccess;
}

/**
 * Cleanup an archive.
 *
 * @param[in]	archiveEntry	SObjectEntry specifyinf the archive to cleanup
 * @param[in]	lpUserStore		The primary store, used to check the references
 * @param[in]	lpRestriction	The restriction that's used to make sure the archived items are old enough.
 */
HRESULT ArchiveControlImpl::CleanupArchive(const SObjectEntry &archiveEntry, IMsgStore* lpUserStore, LPSRestriction lpRestriction)
{
	HRESULT hr;
	SPropValuePtr ptrPropVal;
	EntryIDSet setRefs;
	EntryIDSet setEntries;
	EntryIDSet setDead;
	
	ArchiveHelperPtr ptrArchiveHelper;
	MAPIFolderPtr ptrArchiveFolder;
	ECFolderIterator iEnd;

	hr = ArchiveHelper::Create(m_ptrSession, archiveEntry, m_lpLogger, &ptrArchiveHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveHelper->GetArchiveFolder(true, &ptrArchiveFolder);
	if (hr != hrSuccess)
		return hr;

	if (m_cleanupAction == caStore) {
		// If the cleanup action is store, we need to perform the hierarchy cleanup
		// before cleaning up messages so the hierarchy gets preserved.
		hr = CleanupHierarchy(ptrArchiveHelper, ptrArchiveFolder, lpUserStore);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to cleanup hierarchy.");
			return hr;
		}
	}

	// Get the archive store GUID (PR_STORE_RECORD_KEY)
	hr = HrGetOneProp(ptrArchiveHelper->GetMsgStore(), PR_STORE_RECORD_KEY, &ptrPropVal);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get store GUID of archive store.");
		return hr;
	}
	
	if (ptrPropVal->Value.bin.cb != sizeof(GUID)) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Store record key size does not match that of a GUID. size=%u", ptrPropVal->Value.bin.cb);
		return MAPI_E_CORRUPT_DATA;
	}
	
	// Get a set of all primary messages that have a reference to this archive.
	hr = GetAllReferences(lpUserStore, (LPGUID)ptrPropVal->Value.bin.lpb, &setRefs);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to get all references from primary store. (hr=0x%08x)", hr);
		return hr;
	}

	hr = GetAllEntries(ptrArchiveHelper, ptrArchiveFolder, lpRestriction, &setEntries);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to get all entries from archive store. (hr=0x%08x)", hr);
		return hr;
	}

	// We now have a set containing the entryids of all messages in the archive and a set containing all
	// references to archives in the primary store, which are those same entryids.
	// We simply check which entries are in the set of entries from the archive and not in the set of
	// entries in the primary store. Those can be deleted (or stored).
	
	//The difference of two sets is formed by the elements that are present in the first set, but not in
	//the second one. Notice that this is a directional operation.
	std::set_difference(setEntries.begin(), setEntries.end(), setRefs.begin(), setRefs.end(), std::inserter(setDead, setDead.begin()));
	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Found " SIZE_T_PRINTF " dead entries in archive.", setDead.size());
	
	if (m_cleanupAction != caNone) {
		if (!setDead.empty()) {
			if (m_cleanupAction == caStore)
				hr = MoveAndDetachMessages(ptrArchiveHelper, ptrArchiveFolder, setDead);
			else
				hr = DeleteMessages(ptrArchiveFolder, setDead);
		}
		
		if (m_cleanupAction == caDelete) {
			// If the cleanup action is delete, we need to cleanup the hierarchy after cleaning the
			// messages because we won't delete non-empty folders. So we want to get rid of the
			// messages first.
			hr = CleanupHierarchy(ptrArchiveHelper, ptrArchiveFolder, lpUserStore);
			if (hr != hrSuccess) {
				m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to cleanup hierarchy.");
				return hr;
			}
		}
	} else {
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "cleanup_action is set to none, therefore skipping cleanup action.");
	}
	return hr;
}

/**
 * Get all references to archived items from the primary store. A reference is the entryid of
 * the archived messages.
 * 
 * @param[in]	lpUserStore		The primary store containing the references.
 * @param[in]	lpArchiveGuid	The GUID of the archive store for which to get the references.
 * @param[out]	lpReferences	An EntryIDSet containing all references.
 */
HRESULT ArchiveControlImpl::GetAllReferences(LPMDB lpUserStore, LPGUID lpArchiveGuid, EntryIDSet *lpReferences)
{
	HRESULT hr;
	EntryIDSet setRefs;
	SPropValuePtr ptrPropVal;
	ULONG ulType = 0;
	MAPIFolderPtr ptrIpmSubtree;
	ECFolderIterator iEnd;

	// Find the primary store IPM subtree
	hr = HrGetOneProp(lpUserStore, PR_IPM_SUBTREE_ENTRYID, &ptrPropVal);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to locate ipm subtree of primary store. (hr=0x%08x)", hr);
		return hr;
	}
	
	hr = lpUserStore->OpenEntry(ptrPropVal->Value.bin.cb, (LPENTRYID)ptrPropVal->Value.bin.lpb, &ptrIpmSubtree.iid, 0, &ulType, &ptrIpmSubtree);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open ipm subtree of primary store. (hr=0x%08x)", hr);
		return hr;
	}
	
	hr = AppendAllReferences(ptrIpmSubtree, lpArchiveGuid, &setRefs);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get all references from the ipm subtree. (hr=0x%08x)", hr);
		return hr;
	}
	
	try {
		for (ECFolderIterator i = ECFolderIterator(ptrIpmSubtree, fMapiDeferredErrors, 0); i != iEnd; ++i) {
			hr = AppendAllReferences(*i, lpArchiveGuid, &setRefs);
			if (hr != hrSuccess) {
				m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get all references from primary folder. (hr=0x%08x)", hr);
				return hr;
			}
		}
	} catch (const HrException &he) {
		hr = he.hr();
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to iterate primary folders. (hr=0x%08x)", hr);
		return hr;
	}
	
	lpReferences->swap(setRefs);
	return hrSuccess;
}

/**
 * Get all references to archived items from a primary folder and add them to the
 * passed set.
 * 
 * @param[in]	lpUserStore		The primary store containing the references.
 * @param[in]	lpArchiveGuid	The GUID of the archive store for which to get the references.
 * @param[out]	lpReferences	The EntryIDSet to add the references to.
 */
HRESULT ArchiveControlImpl::AppendAllReferences(LPMAPIFOLDER lpFolder, LPGUID lpArchiveGuid, EntryIDSet *lpReferences)
{
	HRESULT hr = hrSuccess;
	BYTE prefixData[4 + sizeof(GUID)] = {0};
	
	const ULONG ulFlagArray[] = {0, SHOW_SOFT_DELETES};
	
	SizedSPropTagArray(1, sptaContentProps) = {1, {PT_NULL}};

	PROPMAP_START
	PROPMAP_NAMED_ID(ITEM_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidItemEntryIds)
	PROPMAP_INIT(lpFolder)
	
	sptaContentProps.aulPropTag[0] = PROP_ITEM_ENTRYIDS;
	
	memcpy(prefixData + 4, lpArchiveGuid, sizeof(GUID));
	
	for (size_t i = 0; i < arraySize(ulFlagArray); ++i) {
		MAPITablePtr ptrTable;
		
		hr = lpFolder->GetContentsTable(ulFlagArray[i], &ptrTable);
		if (hr != hrSuccess)
			goto exit;
		
		hr = ptrTable->SetColumns((LPSPropTagArray)&sptaContentProps, TBL_BATCH);
		if (hr != hrSuccess)
			goto exit;
		
		while (true) {
			SRowSetPtr ptrRows;
			const ULONG batch_size = 128;
			
			hr = ptrTable->QueryRows(batch_size, 0, &ptrRows);
			if (hr != hrSuccess)
				goto exit;
			
			for (SRowSetPtr::size_type j = 0; j < ptrRows.size(); ++j) {
				if (PROP_TYPE(ptrRows[j].lpProps[0].ulPropTag) == PT_ERROR)
					continue;
				
				for (ULONG k = 0; k < ptrRows[j].lpProps[0].Value.MVbin.cValues; ++k) {
					if (ptrRows[j].lpProps[0].Value.MVbin.lpbin[k].cb >= sizeof(prefixData) &&
						memcmp(ptrRows[j].lpProps[0].Value.MVbin.lpbin[k].lpb, prefixData, sizeof(prefixData)) == 0) {
						lpReferences->insert(ptrRows[j].lpProps[0].Value.MVbin.lpbin[k]);
					}
				}
			}
			
			if (ptrRows.size() < batch_size)
				break;
		}
	}

exit:
	return hr;
}

/**
 * Get the entryid of almost all messages in an archive store. Everything that's
 * below the special folder root is excluded because those don't necessarily get
 * referenced from the primary store anymore.
 *
 * @param[in]	ptrArchiveHelper	The ArchiverHelper instance for the archive to process.
 * @param[in]	lpArchive			The root of the archive.
 * @param[in]	lpRestriction	The restriction that's used to make sure the archived items are old enough.
 * @param[out]	lpEntryies			An EntryIDSet containing all the entryids.
 */
HRESULT ArchiveControlImpl::GetAllEntries(ArchiveHelperPtr ptrArchiveHelper, LPMAPIFOLDER lpArchive, LPSRestriction lpRestriction, EntryIDSet *lpEntries)
{
	HRESULT hr;
	EntryIDSet setEntries;
	ECFolderIterator iEnd;
	EntryIDSet setFolderExcludes;
	MAPIFolderPtr ptrFolder;

	hr = AppendAllEntries(lpArchive, lpRestriction, &setEntries);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get all entries from the root archive folder. (hr=0x%08x)", hr);
		return hr;
	}

	// Exclude everything below the special folder root because that's were we store messages
	// that have not references to the primary store.
	hr = ptrArchiveHelper->GetSpecialsRootFolder(&ptrFolder);
	if (hr == hrSuccess)
		hr = AppendFolderEntries(ptrFolder, &setFolderExcludes);

	ptrFolder.reset();

	try {
		for (ECFolderIterator i = ECFolderIterator(lpArchive, fMapiDeferredErrors, 0); i != iEnd; ++i) {
			SPropValuePtr ptrProp;
			
			hr = HrGetOneProp(*i, PR_ENTRYID, &ptrProp);
			if (hr != hrSuccess)
				return hr;
			
			if (setFolderExcludes.find(ptrProp->Value.bin) != setFolderExcludes.end()) {
				m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Skipping special folder");
				continue;
			}
			
			hr = AppendAllEntries(*i, lpRestriction, &setEntries);
			if (hr != hrSuccess) {
				m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get all references from archive folder. (hr=0x%08x)", hr);
				return hr;
			}
		}
	} catch (const HrException &he) {
		hr = he.hr();
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to iterate archive folders. (hr=0x%08x)", hr);
		return hr;
	}
	
	lpEntries->swap(setEntries);
	return hrSuccess;
}

/**
 * Get the entryid of all messages in an archive folder and append them to the passed set.
 *
 * @param[in]		ptrArchiveHelper	The ArchiverHelper instance for the archive to process.
 * @param[in]		lpArchive			The root of the archive.
 * @param[in]		lpRestriction		The restriction that's used to make sure the archived items are old enough.
 * @param[in,out]	lpEntryies			The EntryIDSet to add the items to.
 */
HRESULT ArchiveControlImpl::AppendAllEntries(LPMAPIFOLDER lpArchive, LPSRestriction lpRestriction, EntryIDSet *lpEntries)
{
	HRESULT hr = hrSuccess;
	MAPITablePtr ptrTable;
	ECAndRestriction resContent;
	
	SizedSPropTagArray(1, sptaContentProps) = {1, {PR_ENTRYID}};
	
	PROPMAP_START
	PROPMAP_NAMED_ID(REF_ITEM_ENTRYID, PT_BINARY, PSETID_Archive, dispidRefItemEntryId)
	PROPMAP_INIT(lpArchive)
	
	resContent.append(ECExistRestriction(PROP_REF_ITEM_ENTRYID));
	if (lpRestriction)
		resContent.append(ECRawRestriction(lpRestriction, ECRestriction::Cheap));
	
	hr = lpArchive->GetContentsTable(0, &ptrTable);
	if (hr != hrSuccess)
		goto exit;
	
	hr = ptrTable->SetColumns((LPSPropTagArray)&sptaContentProps, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;
	
	hr = resContent.RestrictTable(ptrTable);
	if (hr != hrSuccess)
		goto exit;
	
	while (true) {
		SRowSetPtr ptrRows;
		const ULONG batch_size = 128;
		
		hr = ptrTable->QueryRows(batch_size, 0, &ptrRows);
		if (hr != hrSuccess)
			goto exit;
		
		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (PROP_TYPE(ptrRows[i].lpProps[0].ulPropTag) == PT_ERROR) {
				hr = ptrRows[i].lpProps[0].Value.err;
				goto exit;
			}
			
			lpEntries->insert(ptrRows[i].lpProps[0].Value.bin);
		}
		
		if (ptrRows.size() < batch_size)
			break;
	}

exit:
	return hr;
}

/**
 * Cleanup the archive hierarchy. This works by going through the hierarchy
 * and check for each folder if the back reference still works. If it doesn't
 * the folder should be deleted or moved to the deleted items folder.
 * When the cleanup_action is 'delete', the folder will only be deleted if
 * it's empty.
 * If the cleanup action is 'store', the folder will be moved to the deleted
 * items folder as is, leaving the hierarchy in tact.
 * 
 * @param[in]	ptrArchiveHelper	The ArchiverHelper instance for the archive to process.
 * @param[in]	lpArchiveRoot		The root of the archive.
 * @param[out]	lpUserStore			The users primary store.
 */
HRESULT ArchiveControlImpl::CleanupHierarchy(ArchiveHelperPtr ptrArchiveHelper, LPMAPIFOLDER lpArchiveRoot, LPMDB lpUserStore)
{
	HRESULT hr = hrSuccess;
	MAPITablePtr ptrTable;
	SRestriction resHierarchy = {0};
	
	SizedSSortOrderSet(1, ssosHierarchy) = {0};
	SizedSPropTagArray(5, sptaHierarchyProps) = {5, {PR_NULL, PR_ENTRYID, PR_CONTENT_COUNT, PR_FOLDER_CHILD_COUNT, PR_DISPLAY_NAME}};
	enum {IDX_REF_ITEM_ENTRYID, IDX_ENTRYID, IDX_CONTENT_COUNT, IDX_FOLDER_CHILD_COUNT, IDX_DISPLAY_NAME};
	
	PROPMAP_START
	PROPMAP_NAMED_ID(REF_ITEM_ENTRYID, PT_BINARY, PSETID_Archive, dispidRefItemEntryId)
	PROPMAP_INIT(lpArchiveRoot)
	
	sptaHierarchyProps.aulPropTag[IDX_REF_ITEM_ENTRYID] = PROP_REF_ITEM_ENTRYID;
	
	resHierarchy.rt = RES_EXIST;
	resHierarchy.res.resExist.ulPropTag = PROP_REF_ITEM_ENTRYID;
	
	ssosHierarchy.cSorts = 1;
	ssosHierarchy.cCategories = 0;
	ssosHierarchy.cExpanded = 0;
	ssosHierarchy.aSort[0].ulPropTag = PR_DEPTH;
	ssosHierarchy.aSort[0].ulOrder = TABLE_SORT_ASCEND;

	hr = lpArchiveRoot->GetHierarchyTable(CONVENIENT_DEPTH, &ptrTable);
	if (hr != hrSuccess)
		goto exit;
	
	hr = ptrTable->SetColumns((LPSPropTagArray)&sptaHierarchyProps, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;
	
	hr = ptrTable->Restrict(&resHierarchy, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;
		
	hr = ptrTable->SortTable((LPSSortOrderSet)&ssosHierarchy, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;
	
	while (true) {
		SRowSetPtr ptrRows;
		
		hr = ptrTable->QueryRows(64, 0, &ptrRows);
		if (hr != hrSuccess)
			goto exit;
			
		if (ptrRows.empty())
			break;
		
		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			ULONG ulType = 0;
			MAPIFolderPtr ptrPrimaryFolder;

			ScopedFolderLogging sfl(m_lpLogger, ptrRows[i].lpProps[IDX_DISPLAY_NAME].ulPropTag == PR_DISPLAY_NAME ? ptrRows[i].lpProps[IDX_DISPLAY_NAME].Value.LPSZ : _T("<Unnamed>"));
			
			// If the cleanup action is delete, we don't want to delete a folder that's not empty because it might contain messages that
			// have been moved in the primary store before the original folder was deleted. If we were to delete the folder in the archive
			// we would lose that data.
			// But if the cleanup action is store, we do want to move the folder with content so the hierarchy is preserverd.
			
			if (m_cleanupAction == caDelete) {
				// The content count and folder child count should always exist. If not we'll skip the folder
				// just to be safe.
				if (PROP_TYPE(ptrRows[i].lpProps[IDX_CONTENT_COUNT].ulPropTag) == PT_ERROR) {
					m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to obtain folder content count. Skipping folder. (hr=0x%08x)", ptrRows[i].lpProps[IDX_CONTENT_COUNT].Value.err);
					continue;
				} else if (ptrRows[i].lpProps[IDX_CONTENT_COUNT].Value.l != 0) {
					m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Folder contains messages. Skipping folder.");
					continue;
				}
				
				if (PROP_TYPE(ptrRows[i].lpProps[IDX_FOLDER_CHILD_COUNT].ulPropTag) == PT_ERROR)	{
					m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to obtain folder child count. Skipping folder. (hr=0x%08x)", ptrRows[i].lpProps[IDX_FOLDER_CHILD_COUNT].Value.err);
					continue;
				} else if (ptrRows[i].lpProps[IDX_FOLDER_CHILD_COUNT].Value.l != 0) {
					m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Folder has subfolders in it. Skipping folder.");
					continue;
				}
			}
			
			hr = lpUserStore->OpenEntry(ptrRows[i].lpProps[IDX_REF_ITEM_ENTRYID].Value.bin.cb, (LPENTRYID)ptrRows[i].lpProps[IDX_REF_ITEM_ENTRYID].Value.bin.lpb,
										&ptrPrimaryFolder.iid, 0, &ulType, &ptrPrimaryFolder);
			if (hr == MAPI_E_NOT_FOUND) {
				MAPIFolderPtr ptrArchiveFolder;
				SPropValuePtr ptrProp;
				
				hr = lpArchiveRoot->OpenEntry(ptrRows[i].lpProps[IDX_ENTRYID].Value.bin.cb, (LPENTRYID)ptrRows[i].lpProps[IDX_ENTRYID].Value.bin.lpb,
											  &ptrArchiveFolder.iid, MAPI_MODIFY, &ulType, &ptrArchiveFolder);
				if (hr != hrSuccess)
					goto exit;
				
				// Check if we still have a back-ref
				if (HrGetOneProp(ptrArchiveFolder, PROP_REF_ITEM_ENTRYID, &ptrProp) != hrSuccess) {
					m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Back ref is gone. Folder is possibly moved to the deleted items already.");
					continue;
				}
				
				// The primary folder does not exist anymore and this folder is empty. Time to get rid of it.
				m_lpLogger->Log(EC_LOGLEVEL_INFO, "Primary folder seems to have been deleted");
				if (m_cleanupAction == caStore)
					hr = MoveAndDetachFolder(ptrArchiveHelper, ptrArchiveFolder);
				else
					hr = DeleteFolder(ptrArchiveFolder);
				if (hr != hrSuccess)
					m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to process dead folder. (hr=0x%08x)", hr);
			}
			if (hr != hrSuccess)
				goto exit;
		}
	}
	
exit:
	return hr;
}

/**
 * Move a set of messages to the special 'Deleted Items' folder and remove their reference to a
 * primary message that was deleted.
 *
 * @param[in]	ptrArchiveHelper	An ArchiveHelper object containing the archive store that's being processed.
 * @param[in]	lpArchiveFolder		The archive folder containing the messages to move.
 * @param[in]	setEIDs				The set with entryids of the messages to process.
 */
HRESULT ArchiveControlImpl::MoveAndDetachMessages(ArchiveHelperPtr ptrArchiveHelper, LPMAPIFOLDER lpArchiveFolder, const EntryIDSet &setEIDs)
{
	HRESULT hr;
	MAPIFolderPtr ptrDelItemsFolder;
	EntryListPtr ptrMessageList;

	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Moving " SIZE_T_PRINTF " messages to the special 'Deleted Items' folder...", setEIDs.size());

	hr = ptrArchiveHelper->GetDeletedItemsFolder(&ptrDelItemsFolder);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to get deleted items folder. (hr=0x%08x)", hr);
		return hr;
	}

	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &ptrMessageList);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to allocate " SIZE_T_PRINTF " bytes of memory. (hr=0x%08x)", sizeof(ENTRYLIST), hr);
		return hr;
	}

	ptrMessageList->cValues = 0;

	hr = MAPIAllocateMore(sizeof(SBinary) * setEIDs.size(), ptrMessageList, (LPVOID*)&ptrMessageList->lpbin);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to allocate " SIZE_T_PRINTF " bytes of memory. (hr=0x%08x)", sizeof(SBinary) * setEIDs.size(), hr);
		return hr;
	}

	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Processing " SIZE_T_PRINTF " messages", setEIDs.size());
	for (const auto &e : setEIDs) {
		ULONG ulType;
		MAPIPropPtr ptrMessage;
		MAPIPropHelperPtr ptrHelper;

		hr = lpArchiveFolder->OpenEntry(e.size(), e, &ptrMessage.iid, MAPI_MODIFY, &ulType, &ptrMessage);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to open message. (hr=0x%08x)", hr);
			return hr;
		}

		hr = MAPIPropHelper::Create(ptrMessage, &ptrHelper);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to create helper object. (hr=0x%08x)", hr);
			return hr;
		}

		hr = ptrHelper->ClearReference(true);
		if (hr != hrSuccess) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to clear back reference. (hr=0x%08x)", hr);
			return hr;
		}
		ptrMessageList->lpbin[ptrMessageList->cValues].cb = e.size();
		ptrMessageList->lpbin[ptrMessageList->cValues++].lpb = e;
		ASSERT(ptrMessageList->cValues <= setEIDs.size());
	}

	hr = lpArchiveFolder->CopyMessages(ptrMessageList, &ptrDelItemsFolder.iid, ptrDelItemsFolder, 0, NULL, MESSAGE_MOVE);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to move messages. (hr=0x%08x)", hr);
		return hr;
	}
	return hrSuccess;
}

/**
 * Move a folder to the special 'Deleted Items' folder and remove its reference to the
 * primary folder that was deleted.
 *
 * @param[in]	ptrArchiveHelper	An ArchiveHelper object containing the archive store that's being processed.
 * @param[in]	lpArchiveFolder		The archive folder to move.
 */
HRESULT ArchiveControlImpl::MoveAndDetachFolder(ArchiveHelperPtr ptrArchiveHelper, LPMAPIFOLDER lpArchiveFolder)
{
	HRESULT hr;
	SPropValuePtr ptrEntryID;
	MAPIFolderPtr ptrDelItemsFolder;
	MAPIPropHelperPtr ptrHelper;
	ECFolderIterator iEnd;

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Moving folder to the special 'Deleted Items' folder...");

	hr = HrGetOneProp(lpArchiveFolder, PR_ENTRYID, &ptrEntryID);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to get folder entryid. (hr=0x%08x)", hr);
		return hr;
	}

	hr = ptrArchiveHelper->GetDeletedItemsFolder(&ptrDelItemsFolder);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to get deleted items folder. (hr=0x%08x)", hr);
		return hr;
	}

	hr = MAPIPropHelper::Create(MAPIPropPtr(lpArchiveFolder, true), &ptrHelper);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to create helper object. (hr=0x%08x)", hr);
		return hr;
	}

	hr = ptrHelper->ClearReference(true);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to clear back reference. (hr=0x%08x)", hr);
		return hr;
	}
	
	// Get rid of references of all subfolders
	try {
		for (ECFolderIterator i = ECFolderIterator(lpArchiveFolder, fMapiDeferredErrors, 0); i != iEnd; ++i) {
			MAPIPropHelperPtr ptrSubHelper;
			
			hr = MAPIPropHelper::Create(MAPIPropPtr(*i, true), &ptrSubHelper);
			if (hr != hrSuccess)
				return hr;
			
			hr = ptrSubHelper->ClearReference(true);
			if (hr != hrSuccess)
				m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to clean reference of subfolder.");
		}
	} catch (const HrException &he) {
		hr = he.hr();
		m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to iterate folders. (hr=0x%08x)", hr);
		return hr;
	}

	hr = lpArchiveFolder->CopyFolder(ptrEntryID->Value.bin.cb, (LPENTRYID)ptrEntryID->Value.bin.lpb, &ptrDelItemsFolder.iid, ptrDelItemsFolder, NULL, 0, NULL, FOLDER_MOVE);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to move folder. (hr=0x%08x)", hr);
		return hr;
	}
	return hrSuccess;
}

/**
 * Delete the messages in setEIDs from the folder lpArchiveFolder.
 *
 * @param[in]	lpArchiveFolder		The folder to delete the messages from.
 * @param[in]	setEIDs				The set of entryids of the messages to delete.
 */
HRESULT ArchiveControlImpl::DeleteMessages(LPMAPIFOLDER lpArchiveFolder, const EntryIDSet &setEIDs)
{
	HRESULT hr;
	EntryListPtr ptrMessageList;
	
	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Deleting " SIZE_T_PRINTF " messages...", setEIDs.size());

	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &ptrMessageList);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to allocate " SIZE_T_PRINTF " bytes of memory. (hr=0x%08x)", sizeof(ENTRYLIST), hr);
		return hr;
	}

	ptrMessageList->cValues = 0;

	hr = MAPIAllocateMore(sizeof(SBinary) * setEIDs.size(), ptrMessageList, (LPVOID*)&ptrMessageList->lpbin);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to allocate " SIZE_T_PRINTF " bytes of memory. (hr=0x%08x)", sizeof(SBinary) * setEIDs.size(), hr);
		return hr;
	}

	m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Processing " SIZE_T_PRINTF " messages", setEIDs.size());
	for (const auto &e : setEIDs) {
		ptrMessageList->lpbin[ptrMessageList->cValues].cb = e.size();
		ptrMessageList->lpbin[ptrMessageList->cValues++].lpb = e;
	}

	return lpArchiveFolder->DeleteMessages(ptrMessageList, 0, NULL, 0);
}

/**
 * Delete the folder specified by lpArchiveFolder
 *
 * @param[in]	lpArchiveFolder		Folder to delete.
 */
HRESULT ArchiveControlImpl::DeleteFolder(LPMAPIFOLDER lpArchiveFolder)
{
	HRESULT hr;
	SPropValuePtr ptrEntryId;
	
	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Deleting folder...");

	hr = HrGetOneProp(lpArchiveFolder, PR_ENTRYID, &ptrEntryId);
	if (hr != hrSuccess) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to get folder entryid (hr=0x%08x)", hr);
		return hr;
	}

	// Delete yourself!
	hr = lpArchiveFolder->DeleteFolder(ptrEntryId->Value.bin.cb, (LPENTRYID)ptrEntryId->Value.bin.lpb, 0, NULL, DEL_FOLDERS|DEL_MESSAGES|DEL_ASSOCIATED);
	if (FAILED(hr)) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to delete folder (hr=0x%08x)", hr);
		return hr;
	} else if (hr != hrSuccess)
		m_lpLogger->Log(EC_LOGLEVEL_WARNING, "Folder only got partially deleted (hr=0x%08x)", hr);

	return hr;
}

/**
 * Append the entryid of the passed folder and all its subfolder to a list.
 * @param[in]	lpBase		The folder to start processing.
 * @param[out]	lpEntries	The returned set of entryids.
 */
HRESULT ArchiveControlImpl::AppendFolderEntries(LPMAPIFOLDER lpBase, EntryIDSet *lpEntries)
{
	HRESULT hr;
	SPropValuePtr ptrProp;
	MAPITablePtr ptrTable;
	
	SizedSPropTagArray(1, sptaTableProps) = {1, {PR_ENTRYID}};
	
	hr = HrGetOneProp(lpBase, PR_ENTRYID, &ptrProp);
	if (hr != hrSuccess)
		return hr;
	
	lpEntries->insert(ptrProp->Value.bin);
	hr = lpBase->GetHierarchyTable(CONVENIENT_DEPTH, &ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns((LPSPropTagArray)&sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	
	while (true) {
		SRowSetPtr ptrRows;
		
		hr = ptrTable->QueryRows(128, 0, &ptrRows);
		if (hr != hrSuccess)
			return hr;
		if (ptrRows.empty())
			break;
		
		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i)
			lpEntries->insert(ptrRows[i].lpProps[0].Value.bin);
	}
	return hrSuccess;
}

/**
 * This method checks the settings to see if they're safe when performing
 * a cleanup run. It's unsafe to run a cleanup when the delete operation is
 * enabled, and the cleanup doesn't check the purge_after option or if the
 * purge_after option is set to 0.
 * 
 * See ZCP-10571.
 */
HRESULT ArchiveControlImpl::CheckSafeCleanupSettings()
{
	int loglevel = (m_bForceCleanup ? EC_LOGLEVEL_WARNING : EC_LOGLEVEL_FATAL);
	
	if (m_bDeleteEnable && !m_bCleanupFollowPurgeAfter) {
		m_lpLogger->Log(loglevel, "'delete_enable' is set to '%s' and 'cleanup_follow_purge_after' is set to '%s'", 
						m_lpConfig->GetSetting("delete_enable", "", "no"),
						m_lpConfig->GetSetting("cleanup_follow_purge_after", "", "no"));
		m_lpLogger->Log(loglevel, "This can cause messages to be deleted from the archive while they shouldn't be deleted.");
		if (!m_bForceCleanup) {
			m_lpLogger->Log(loglevel, "Please correct your configuration or pass '--force-cleanup' at the commandline if you");
			m_lpLogger->Log(loglevel, "know what you're doing (not recommended).");
			return MAPI_E_UNABLE_TO_COMPLETE;
		}
		m_lpLogger->Log(loglevel, "User forced continuation!");
	}
	
	else if (m_bDeleteEnable && m_bCleanupFollowPurgeAfter && m_ulPurgeAfter == 0) {
		m_lpLogger->Log(loglevel, "'delete_enable' is set to '%s' and 'cleanup_follow_purge_after' is set to '%s'", 
						m_lpConfig->GetSetting("delete_enable", "", "no"),
						m_lpConfig->GetSetting("cleanup_follow_purge_after", "", "no"));
		m_lpLogger->Log(loglevel, "but 'purge_after' is set to '0'");
		m_lpLogger->Log(loglevel, "This can cause messages to be deleted from the archive while they shouldn't be deleted.");
		if (!m_bForceCleanup) {
			m_lpLogger->Log(loglevel, "Please correct your configuration or pass '--force-cleanup' at the commandline if you");
			m_lpLogger->Log(loglevel, "know what you're doing (not recommended).");
			return MAPI_E_UNABLE_TO_COMPLETE;
		}
		m_lpLogger->Log(loglevel, "User forced continuation!");
	}
	
	return hrSuccess;
}

