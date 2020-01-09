/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <algorithm>
#include <memory>
#include <new>          // std::bad_alloc
#include <list>          // std::list
#include <utility>
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
#include <kopano/ECConfig.h>
#include "ECIterators.h"
#include <kopano/ECRestriction.h>
#include <kopano/hl.hpp>
#include "ArchiveManage.h"
#include <kopano/MAPIErrors.h>
#include <kopano/charset/convert.h>
#include <kopano/scope.hpp>

using namespace KC::helpers;
using namespace KC::operations;

namespace KC {

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
HRESULT ArchiveControlImpl::Create(ArchiverSessionPtr ptrSession,
    ECConfig *lpConfig, std::shared_ptr<ECLogger> lpLogger, bool bForceCleanup,
    ArchiveControlPtr *lpptrArchiveControl)
{
	std::unique_ptr<ArchiveControlImpl> ptrArchiveControl(
		new(std::nothrow) ArchiveControlImpl(ptrSession, lpConfig,
		std::move(lpLogger), bForceCleanup));
	if (ptrArchiveControl == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	HRESULT hr = ptrArchiveControl->Init();
	if (hr != hrSuccess)
		return hr;
	*lpptrArchiveControl = std::move(ptrArchiveControl);
	return hrSuccess;
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
ArchiveControlImpl::ArchiveControlImpl(ArchiverSessionPtr ptrSession,
    ECConfig *lpConfig, std::shared_ptr<ECLogger> lpLogger, bool bForceCleanup) :
	m_ptrSession(ptrSession), m_lpConfig(lpConfig),
	m_lpLogger(new ECArchiverLogger(std::move(lpLogger))),
	m_cleanupAction(caStore), m_bForceCleanup(bForceCleanup), m_propmap(5)
{
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
		m_lpLogger->logf(EC_LOGLEVEL_FATAL, "Unknown cleanup_action specified in config: \"%s\"", lpszCleanupAction);
		return MAPI_E_INVALID_PARAMETER;
	}

	m_bCleanupFollowPurgeAfter = parseBool(m_lpConfig->GetSetting("cleanup_follow_purge_after", "", "no"));
	GetSystemTimeAsFileTime(&m_ftCurrent);
	return hrSuccess;
}

/**
 * Archive messages for all users. Optionally only user that have their store on the server
 * to which the archiver is connected will have their messages archived.
 *
 * @param[in]	bLocalOnly
 *					If set to true only  messages for users that have their store on the local server
 *					will be archived.
 */
eResult ArchiveControlImpl::ArchiveAll(bool bLocalOnly, bool bAutoAttach, unsigned int ulFlags)
{
	if (ulFlags != ArchiveManage::Writable &&
	    ulFlags != ArchiveManage::ReadOnly && ulFlags != 0)
		return MAPIErrorToArchiveError(MAPI_E_INVALID_PARAMETER);

	if (!bAutoAttach && !parseBool(m_lpConfig->GetSetting("enable_auto_attach")))
		return MAPIErrorToArchiveError(ProcessAll(bLocalOnly, &ArchiveControlImpl::DoArchive));

	ArchiveStateCollectorPtr ptrArchiveStateCollector;
	ArchiveStateUpdaterPtr ptrArchiveStateUpdater;

	auto hr = ArchiveStateCollector::Create(m_ptrSession, m_lpLogger, &ptrArchiveStateCollector);
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
	return MAPIErrorToArchiveError(ProcessAll(bLocalOnly, &ArchiveControlImpl::DoArchive));
}

/**
 * Archive the messages of a particular user.
 *
 * @param[in]	strUser
 *					The username for which to archive the messages.
 */
HRESULT ArchiveControlImpl::Archive2(const tstring &strUser, bool bAutoAttach,
    unsigned int ulFlags)
{
    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): function entry.");
    ScopedUserLogging sul(m_lpLogger, strUser);

	if (ulFlags != ArchiveManage::Writable && ulFlags != ArchiveManage::ReadOnly && ulFlags != 0) {
        m_lpLogger->Log(EC_LOGLEVEL_INFO, "ArchiveControlImpl::Archive(): invalid parameter.");
		return MAPI_E_INVALID_PARAMETER;
	}

	if (bAutoAttach || parseBool(m_lpConfig->GetSetting("enable_auto_attach"))) {
		ArchiveStateCollectorPtr ptrArchiveStateCollector;
		ArchiveStateUpdaterPtr ptrArchiveStateUpdater;

        m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): about to create collector.");
		auto hr = ArchiveStateCollector::Create(m_ptrSession, m_lpLogger, &ptrArchiveStateCollector);
		if (hr != hrSuccess)
			return hr;
        m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): about to get updater.");
		hr = ptrArchiveStateCollector->GetArchiveStateUpdater(&ptrArchiveStateUpdater);
		if (hr != hrSuccess)
			return hr;
		if (ulFlags == 0) {
			if (parseBool(m_lpConfig->GetSetting("auto_attach_writable")))
				ulFlags = ArchiveManage::Writable;
			else
				ulFlags = ArchiveManage::ReadOnly;
		}
		m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): about to update store of user \"%ls\". Flags: 0x%08X", strUser.c_str(), ulFlags);
		hr = ptrArchiveStateUpdater->Update(strUser, ulFlags);
		if (hr != hrSuccess)
			return hr;
	}

    m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive(): about to do real archive run.");
	return DoArchive(strUser);
}

eResult ArchiveControlImpl::Archive(const tstring &user, bool auto_attach,
    unsigned int flags)
{
	auto hr = Archive2(user, auto_attach, flags);
	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "ArchiveControlImpl::Archive() at exit. Return code before transformation: %s (%x)", GetMAPIErrorMessage(hr), hr);
	return MAPIErrorToArchiveError(hr);
}

/**
 * Cleanup the archive(s) of all users. Optionally only user that have their store on the server
 * to which the archiver is connected will have their messages archived.
 *
 * @param[in]	bLocalOnly
 *					If set to true only  messages for users that have their store on the local server
 *					will be archived.
 */
eResult ArchiveControlImpl::CleanupAll(bool bLocalOnly)
{
	auto hr = CheckSafeCleanupSettings();
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
    ScopedUserLogging sul(m_lpLogger, strUser);
	auto hr = CheckSafeCleanupSettings();
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
	std::list<tstring> lstUsers;
	bool bHaveErrors = false;

	auto hr = GetArchivedUserList(m_ptrSession->GetMAPISession(),
	          m_ptrSession->GetSSLPath(), m_ptrSession->GetSSLPass(),
	          &lstUsers, bLocalOnly);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to obtain user list", hr);

	m_lpLogger->logf(EC_LOGLEVEL_INFO, "Processing %zu%s users.", lstUsers.size(), (bLocalOnly ? " local" : ""));
	for (const auto &user : lstUsers) {
		m_lpLogger->logf(EC_LOGLEVEL_INFO, "Processing user \"" TSTRING_PRINTF "\".", user.c_str());
		HRESULT hrTmp = (this->*fnProcess)(user);
		if (FAILED(hrTmp)) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to process user \"" TSTRING_PRINTF "\": %s (%x)",
				user.c_str(), GetMAPIErrorMessage(hrTmp), hrTmp);
			bHaveErrors = true;
		} else if (hrTmp == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Errors occurred while processing user \"" TSTRING_PRINTF "\".", user.c_str());
			bHaveErrors = true;
		}
	}
	if (hr == hrSuccess && bHaveErrors)
		return MAPI_W_PARTIAL_COMPLETION;
	return hr;
}

/**
 * Perform the actual archive operation for a specific user.
 *
 * @param[in]	strUser	tstring containing user name
 */
HRESULT ArchiveControlImpl::DoArchive(const tstring& strUser)
{
	if (strUser.empty())
		return MAPI_E_INVALID_PARAMETER;

	MsgStorePtr ptrUserStore;
	StoreHelperPtr ptrStoreHelper;
	MAPIFolderPtr ptrSearchArchiveFolder, ptrSearchDeleteFolder, ptrSearchStubFolder;
	ObjectEntryList lstArchives;
	bool bHaveErrors = false;
	std::shared_ptr<Copier> ptrCopyOp;
	DeleterPtr	ptrDeleteOp;
	StubberPtr	ptrStubOp;

	m_lpLogger->logf(EC_LOGLEVEL_INFO, "Archiving store for user \"" TSTRING_PRINTF "\"", strUser.c_str());
	auto hr = m_ptrSession->OpenStoreByName(strUser, &~ptrUserStore);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to open store", hr);

	PROPMAP_INIT_NAMED_ID(ARCHIVE_STORE_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidStoreEntryIds)
	PROPMAP_INIT_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidItemEntryIds)
	PROPMAP_INIT_NAMED_ID(ORIGINAL_SOURCEKEY, PT_BINARY, PSETID_Archive, dispidOrigSourceKey)
	PROPMAP_INIT_NAMED_ID(STUBBED, PT_BOOLEAN, PSETID_Archive, dispidStubbed)
	PROPMAP_INIT_NAMED_ID(DIRTY, PT_BOOLEAN, PSETID_Archive, dispidDirty)
	PROPMAP_INIT(ptrUserStore)

	auto laters = make_scope_success([&]() {
		if (hr == hrSuccess && bHaveErrors)
			hr = MAPI_W_PARTIAL_COMPLETION;
	});

	hr = StoreHelper::Create(ptrUserStore, &ptrStoreHelper);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to create store helper", hr);

	hr = ptrStoreHelper->GetArchiveList(&lstArchives);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_CORRUPT_DATA) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "List of archives is corrupt for user \"" TSTRING_PRINTF "\", skipping user.", strUser.c_str());
			hr = hrSuccess;
		} else
			m_lpLogger->perr("Failed to get list of archives", hr);
		return hr;
	}

	if (lstArchives.empty()) {
		m_lpLogger->logf(EC_LOGLEVEL_INFO, "\"" TSTRING_PRINTF "\" has no attached archives", strUser.c_str());
		return hr;
	}
	hr = ptrStoreHelper->GetSearchFolders(&~ptrSearchArchiveFolder, &~ptrSearchDeleteFolder, &~ptrSearchStubFolder);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get the search folders", hr);

	// Create and hook the three dependent steps
	if (m_bArchiveEnable && m_ulArchiveAfter >= 0) {
		SizedSPropTagArray(5, sptaExcludeProps) = {5, {PROP_ARCHIVE_STORE_ENTRYIDS, PROP_ARCHIVE_ITEM_ENTRYIDS, PROP_STUBBED, PROP_DIRTY, PROP_ORIGINAL_SOURCEKEY}};
		ptrCopyOp = std::make_shared<Copier>(m_ptrSession, m_lpConfig, m_lpLogger,
			lstArchives, sptaExcludeProps, m_ulArchiveAfter, true);
	}

	if (m_bDeleteEnable && m_ulDeleteAfter >= 0) {
		ptrDeleteOp = std::make_shared<Deleter>(m_lpLogger, m_ulDeleteAfter, m_bDeleteUnread);
		if (ptrCopyOp)
			ptrCopyOp->SetDeleteOperation(ptrDeleteOp);
	}

	if (m_bStubEnable && m_ulStubAfter >= 0) {
		ptrStubOp = std::make_shared<Stubber>(m_lpLogger, PROP_STUBBED, m_ulStubAfter, m_bStubUnread);
		if (ptrCopyOp)
			ptrCopyOp->SetStubOperation(ptrStubOp);
	}

	// Now execute them
	if (ptrCopyOp) {
		// Archive all unarchived messages that are old enough
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Archiving messages");
		hr = ProcessFolder(ptrSearchArchiveFolder, ptrCopyOp);
		if (FAILED(hr)) {
			return m_lpLogger->perr("Failed to archive messages", hr);
		} else if (hr == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Some message could not be archived");
			bHaveErrors = true;
			hr = hrSuccess;
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done archiving messages");
	}

	if (ptrDeleteOp) {
		// First delete all messages that are eligible for deletion, so we do not unnecessary stub them first
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Deleting old messages");
		hr = ProcessFolder(ptrSearchDeleteFolder, ptrDeleteOp);
		if (FAILED(hr)) {
			return m_lpLogger->perr("Failed to delete old messages", hr);
		} else if (hr == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Some message could not be deleted");
			bHaveErrors = true;
			hr = hrSuccess;
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done deleting messages");
	}

	if (ptrStubOp) {
		// Now stub the remaining messages (if they are old enough)
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Stubbing messages");
		hr = ProcessFolder(ptrSearchStubFolder, ptrStubOp);
		if (FAILED(hr)) {
			return m_lpLogger->perr("Failed to stub messages", hr);
		} else if (hr == MAPI_W_PARTIAL_COMPLETION) {
			m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Some message could not be stubbed");
			bHaveErrors = true;
			hr = hrSuccess;
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done stubbing messages");
	}

	if (!m_bPurgeEnable)
		return hr;
	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Purging archive(s)");
	hr = PurgeArchives(lstArchives);
	if (FAILED(hr)) {
		return m_lpLogger->perr("Failed to purge archive(s)", hr);
	} else if (hr == MAPI_W_PARTIAL_COMPLETION) {
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Some archives could not be purged");
		bHaveErrors = true;
		hr = hrSuccess;
	}
	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done purging archive(s)");
	return hr;
}

/**
 * Perform the actual cleanup operation for a specific user.
 *
 * @param[in]	strUser	tstring containing user name
 */
HRESULT ArchiveControlImpl::DoCleanup(const tstring &strUser)
{
	MsgStorePtr ptrUserStore;
	StoreHelperPtr ptrStoreHelper;
	ObjectEntryList lstArchives;
	SRestrictionPtr ptrRestriction;

	if (strUser.empty())
		return MAPI_E_INVALID_PARAMETER;
	m_lpLogger->logf(EC_LOGLEVEL_INFO, "Cleanup store for user \"" TSTRING_PRINTF "\", mode=%s", strUser.c_str(), m_lpConfig->GetSetting("cleanup_action"));

	if (m_bCleanupFollowPurgeAfter) {
		SPropValue sPropRefTime;

		auto qp = (static_cast<uint64_t>(m_ftCurrent.dwHighDateTime) << 32) | m_ftCurrent.dwLowDateTime;
		qp -= m_ulPurgeAfter * ARC_DAY;
		sPropRefTime.ulPropTag = PROP_TAG(PT_SYSTIME, 0);
		sPropRefTime.Value.ft.dwLowDateTime  = qp & 0xffffffff;
		sPropRefTime.Value.ft.dwHighDateTime = qp >> 32;
		auto hr = ECOrRestriction(
			ECAndRestriction(
				ECExistRestriction(PR_MESSAGE_DELIVERY_TIME) +
				ECPropertyRestriction(RELOP_LT, PR_MESSAGE_DELIVERY_TIME, &sPropRefTime, ECRestriction::Cheap)
			) +
			ECAndRestriction(
				ECExistRestriction(PR_CLIENT_SUBMIT_TIME) +
				ECPropertyRestriction(RELOP_LT, PR_CLIENT_SUBMIT_TIME, &sPropRefTime, ECRestriction::Cheap)
			)
		).CreateMAPIRestriction(&~ptrRestriction, 0);
		if (hr != hrSuccess)
			return hr;
	}

	auto hr = m_ptrSession->OpenStoreByName(strUser, &~ptrUserStore);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to open store", hr);
	hr = StoreHelper::Create(ptrUserStore, &ptrStoreHelper);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to create store helper", hr);
	hr = ptrStoreHelper->GetArchiveList(&lstArchives);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_CORRUPT_DATA) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "List of archives is corrupt for user \"" TSTRING_PRINTF "\", skipping user.", strUser.c_str());
			hr = hrSuccess;
		} else
			m_lpLogger->perr("Failed to get list of archives", hr);
		return hr;
	}

	if (lstArchives.empty()) {
		m_lpLogger->logf(EC_LOGLEVEL_INFO, "\"" TSTRING_PRINTF "\" has no attached archives", strUser.c_str());
		return hr;
	}

	for (const auto &arc : lstArchives) {
		auto hrTmp = CleanupArchive(arc, ptrUserStore, ptrRestriction);
		if (hrTmp != hrSuccess)
			m_lpLogger->perr("Failed to cleanup archive", hrTmp);
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
HRESULT ArchiveControlImpl::ProcessFolder2(object_ptr<IMAPIFolder> &ptrFolder,
    std::shared_ptr<IArchiveOperation> ptrArchiveOperation, bool &bHaveErrors)
{
	MAPITablePtr ptrTable;
	SRestrictionPtr ptrRestriction;
	memory_ptr<SSortOrderSet> ptrSortOrder;
	SRowSetPtr ptrRowSet;
	MessagePtr ptrMessage;
	static constexpr const SizedSPropTagArray(3, sptaProps) =
		{3, {PR_ENTRYID, PR_PARENT_ENTRYID, PR_STORE_ENTRYID}};
	static constexpr const SizedSSortOrderSet(1, sptaOrder) =
		{1, 0, 0, {{PR_PARENT_ENTRYID, TABLE_SORT_ASCEND}}};

	auto hr = ptrFolder->GetContentsTable(fMapiDeferredErrors, &~ptrTable);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get search folder contents table", hr);
	hr = ptrTable->SetColumns(sptaProps, TBL_BATCH);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to set columns on table", hr);
	hr = ptrArchiveOperation->GetRestriction(ptrFolder, &~ptrRestriction);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get restriction from operation", hr);
	hr = ptrTable->Restrict(ptrRestriction, TBL_BATCH);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to set restriction on table", hr);
	hr = ptrTable->SortTable(sptaOrder, TBL_BATCH);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to sort table", hr);

	do {
		hr = ptrTable->QueryRows(50, 0, &~ptrRowSet);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to get rows from table", hr);
		m_lpLogger->logf(EC_LOGLEVEL_INFO, "Processing batch of %u messages", ptrRowSet.size());
		for (ULONG i = 0; i < ptrRowSet.size(); ++i) {
			hr = ptrArchiveOperation->ProcessEntry(ptrFolder, ptrRowSet[i]);
			if (hr == hrSuccess)
				continue;
			bHaveErrors = true;
			m_lpLogger->perr("Failed to process entry", hr);
			if (hr == MAPI_E_STORE_FULL) {
				m_lpLogger->Log(EC_LOGLEVEL_FATAL, "Disk full or over quota.");
				return hr;
			}
		}
		m_lpLogger->Log(EC_LOGLEVEL_INFO, "Done processing batch");
	} while (ptrRowSet.size() == 50);
	return hrSuccess;
}

HRESULT ArchiveControlImpl::ProcessFolder(object_ptr<IMAPIFolder> &fld,
    std::shared_ptr<IArchiveOperation> aop)
{
	const tstring strFolderRestore = m_lpLogger->GetFolder();
	bool bHaveErrors = false;
	auto hr = ProcessFolder2(fld, aop, bHaveErrors);
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
	bool bErrorOccurred = false;
	memory_ptr<SRestriction> lpRestriction;
	SPropValue sPropCreationTime;
	SRowSetPtr ptrRowSet;
	static constexpr const SizedSPropTagArray(2, sptaFolderProps) =
		{2, {PR_ENTRYID, PR_DISPLAY_NAME}};
    enum {IDX_ENTRYID, IDX_DISPLAY_NAME};

	// Create the common restriction that determines which messages are old enough to purge.
	auto qp = (static_cast<uint64_t>(m_ftCurrent.dwHighDateTime) << 32) | m_ftCurrent.dwLowDateTime;
	qp -= m_ulPurgeAfter * ARC_DAY;
	sPropCreationTime.ulPropTag = PR_MESSAGE_DELIVERY_TIME;
	sPropCreationTime.Value.ft.dwLowDateTime  = qp & 0xffffffff;
	sPropCreationTime.Value.ft.dwHighDateTime = qp >> 32;
	auto hr = ECPropertyRestriction(RELOP_LT, PR_MESSAGE_DELIVERY_TIME, &sPropCreationTime, ECRestriction::Cheap)
	          .CreateMAPIRestriction(&~lpRestriction, ECRestriction::Cheap);
	if (hr != hrSuccess)
		return hr;

	for (const auto &arc : lstArchives) {
		MsgStorePtr ptrArchiveStore;
		MAPIFolderPtr ptrArchiveRoot;
		ULONG ulType = 0;
		MAPITablePtr ptrFolderTable;
		SRowSetPtr ptrFolderRows;

		hr = m_ptrSession->OpenStore(arc.sStoreEntryId, &~ptrArchiveStore);
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to open archive (entryid=%s): %s (%x)",
				arc.sStoreEntryId.tostring().c_str(), GetMAPIErrorMessage(hr), hr);
			bErrorOccurred = true;
			continue;
		}

		// Purge root of archive
		hr = PurgeArchiveFolder(ptrArchiveStore, arc.sItemEntryId, lpRestriction);
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to purge archive root (entryid=%s): %s (%x)",
				arc.sItemEntryId.tostring().c_str(), GetMAPIErrorMessage(hr), hr);
			bErrorOccurred = true;
			continue;
		}

		// Get all subfolders and purge those as well.
		hr = ptrArchiveStore->OpenEntry(arc.sItemEntryId.size(), arc.sItemEntryId, &iid_of(ptrArchiveRoot), MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~ptrArchiveRoot);
		if (hr != hrSuccess) {
			m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to open archive root (entryid=%s): %s (%x)",
				arc.sItemEntryId.tostring().c_str(), GetMAPIErrorMessage(hr), hr);
			bErrorOccurred = true;
			continue;
		}
		hr = ptrArchiveRoot->GetHierarchyTable(CONVENIENT_DEPTH | fMapiDeferredErrors, &~ptrFolderTable);
		if (hr != hrSuccess) {
			m_lpLogger->perr("Failed to get archive hierarchy table", hr);
			bErrorOccurred = true;
			continue;
		}
		hr = ptrFolderTable->SetColumns(sptaFolderProps, TBL_BATCH);
		if (hr != hrSuccess) {
			m_lpLogger->perr("Failed to select folder table columns", hr);
			bErrorOccurred = true;
			continue;
		}

		while (true) {
			hr = ptrFolderTable->QueryRows(50, 0, &~ptrFolderRows);
			if (hr != hrSuccess)
				return m_lpLogger->perr("Failed to get rows from folder table", hr);

			for (ULONG i = 0; i < ptrFolderRows.size(); ++i) {
				ScopedFolderLogging sfl(m_lpLogger, ptrFolderRows[i].lpProps[IDX_DISPLAY_NAME].ulPropTag == PR_DISPLAY_NAME ? ptrFolderRows[i].lpProps[IDX_DISPLAY_NAME].Value.LPSZ : KC_T("<Unnamed>"));
				hr = PurgeArchiveFolder(ptrArchiveStore, ptrFolderRows[i].lpProps[IDX_ENTRYID].Value.bin, lpRestriction);
				if (hr != hrSuccess) {
					m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to purge archive folder (entryid=%s): %s (%x)",
						bin2hex(ptrFolderRows[i].lpProps[IDX_ENTRYID].Value.bin).c_str(),
						GetMAPIErrorMessage(hr), hr);
					bErrorOccurred = true;
				}
			}

			if (ptrFolderRows.size() < 50)
				break;
		}
	}

	if (hr == hrSuccess && bErrorOccurred)
		return MAPI_W_PARTIAL_COMPLETION;
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
	ULONG ulType = 0;
	MAPIFolderPtr ptrFolder;
	MAPITablePtr ptrContentsTable;
	std::list<entryid_t> lstEntries;
	SRowSetPtr ptrRows;
	EntryListPtr ptrEntryList;
	ULONG ulIdx = 0;
	static constexpr const SizedSPropTagArray(1, sptaTableProps) = {1, {PR_ENTRYID}};

	auto hr = ptrArchive->OpenEntry(folderEntryID.size(), folderEntryID, &iid_of(ptrFolder), MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~ptrFolder);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to open archive folder (entryid=%s): %s (%x)",
			folderEntryID.tostring().c_str(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	hr = ptrFolder->GetContentsTable(fMapiDeferredErrors, &~ptrContentsTable);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to open contents table", hr);
	hr = ptrContentsTable->SetColumns(sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to select table columns", hr);
	hr = ptrContentsTable->Restrict(lpRestriction, TBL_BATCH);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to restrict contents table", hr);

	while (true) {
		hr = ptrContentsTable->QueryRows(50, 0, &~ptrRows);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to get rows from contents table", hr);
		for (ULONG i = 0; i < ptrRows.size(); ++i)
			lstEntries.emplace_back(ptrRows[i].lpProps[0].Value.bin);
		if (ptrRows.size() < 50)
			break;
	}

	m_lpLogger->logf(EC_LOGLEVEL_INFO, "Purging %zu messaged from archive folder", lstEntries.size());
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~ptrEntryList);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(lstEntries.size() * sizeof(SBinary), ptrEntryList, reinterpret_cast<void **>(&ptrEntryList->lpbin));
	if (hr != hrSuccess)
		return hr;

	ptrEntryList->cValues = lstEntries.size();
	for (const auto &e : lstEntries) {
		ptrEntryList->lpbin[ulIdx].cb = e.size();
		ptrEntryList->lpbin[ulIdx++].lpb = e;
	}

	hr = ptrFolder->DeleteMessages(ptrEntryList, 0, NULL, 0);
	if (hr != hrSuccess)
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to delete %u messages: %s (%x)",
			ptrEntryList->cValues, GetMAPIErrorMessage(hr), hr);
	return hr;
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
	SPropValuePtr ptrPropVal;
	EntryIDSet setRefs, setEntries, setDead;
	ArchiveHelperPtr ptrArchiveHelper;
	MAPIFolderPtr ptrArchiveFolder;

	auto hr = ArchiveHelper::Create(m_ptrSession, archiveEntry, m_lpLogger, &ptrArchiveHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveHelper->GetArchiveFolder(true, &~ptrArchiveFolder);
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
	hr = HrGetOneProp(ptrArchiveHelper->GetMsgStore(), PR_STORE_RECORD_KEY, &~ptrPropVal);
	if (hr != hrSuccess) {
		m_lpLogger->perr("Unable to get store GUID of archive store", hr);
		return hr;
	}
	if (ptrPropVal->Value.bin.cb != sizeof(GUID)) {
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Store record key size does not match that of a GUID. size=%u", ptrPropVal->Value.bin.cb);
		return MAPI_E_CORRUPT_DATA;
	}
	// Get a set of all primary messages that have a reference to this archive.
	hr = GetAllReferences(lpUserStore, (LPGUID)ptrPropVal->Value.bin.lpb, &setRefs);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get all references from primary store", hr);
	hr = GetAllEntries(ptrArchiveHelper, ptrArchiveFolder, lpRestriction, &setEntries);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get all entries from archive store", hr);

	// We now have a set containing the entryids of all messages in the archive and a set containing all
	// references to archives in the primary store, which are those same entryids.
	// We simply check which entries are in the set of entries from the archive and not in the set of
	// entries in the primary store. Those can be deleted (or stored).

	//The difference of two sets is formed by the elements that are present in the first set, but not in
	//the second one. Notice that this is a directional operation.
	std::set_difference(setEntries.begin(), setEntries.end(), setRefs.begin(), setRefs.end(), std::inserter(setDead, setDead.begin()));
	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Found %zu dead entries in archive.", setDead.size());
	if (m_cleanupAction == caNone) {
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "cleanup_action is set to none, therefore skipping cleanup action.");
		return hr;
	}
	if (!setDead.empty()) {
		if (m_cleanupAction == caStore)
			hr = MoveAndDetachMessages(ptrArchiveHelper, ptrArchiveFolder, setDead);
		else
			hr = DeleteMessages(ptrArchiveFolder, setDead);
	}
	if (m_cleanupAction != caDelete)
		return hr;
	// If the cleanup action is delete, we need to cleanup the hierarchy after cleaning the
	// messages because we won't delete non-empty folders. So we want to get rid of the
	// messages first.
	hr = CleanupHierarchy(ptrArchiveHelper, ptrArchiveFolder, lpUserStore);
	if (hr != hrSuccess)
		m_lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to cleanup hierarchy.");
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
HRESULT ArchiveControlImpl::GetAllReferences(IMsgStore *lpUserStore,
    const GUID *lpArchiveGuid, EntryIDSet *lpReferences)
{
	EntryIDSet setRefs;
	SPropValuePtr ptrPropVal;
	ULONG ulType = 0;
	MAPIFolderPtr ptrIpmSubtree;
	ECFolderIterator iEnd;

	// Find the primary store IPM subtree
	auto hr = HrGetOneProp(lpUserStore, PR_IPM_SUBTREE_ENTRYID, &~ptrPropVal);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Unable to locate ipm subtree of primary store", hr);
	hr = lpUserStore->OpenEntry(ptrPropVal->Value.bin.cb, reinterpret_cast<ENTRYID *>(ptrPropVal->Value.bin.lpb), &iid_of(ptrIpmSubtree), 0, &ulType, &~ptrIpmSubtree);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Unable to open ipm subtree of primary store", hr);
	hr = AppendAllReferences(ptrIpmSubtree, lpArchiveGuid, &setRefs);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Unable to get all references from the ipm subtree", hr);

	try {
		for (ECFolderIterator i = ECFolderIterator(ptrIpmSubtree, fMapiDeferredErrors, 0); i != iEnd; ++i) {
			hr = AppendAllReferences(*i, lpArchiveGuid, &setRefs);
			if (hr != hrSuccess)
				return m_lpLogger->perr("Unable to get all references from primary folder", hr);
		}
	} catch (const KMAPIError &e) {
		return m_lpLogger->perr("Failed to iterate primary folders", e.code());
	}
	*lpReferences = std::move(setRefs);
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
HRESULT ArchiveControlImpl::AppendAllReferences(IMAPIFolder *lpFolder,
    const GUID *lpArchiveGuid, EntryIDSet *lpReferences)
{
	BYTE prefixData[4 + sizeof(GUID)] = {0};
	static constexpr const ULONG ulFlagArray[] = {0, SHOW_SOFT_DELETES};
	SizedSPropTagArray(1, sptaContentProps) = {1, {PT_NULL}};

	PROPMAP_START(1)
	PROPMAP_NAMED_ID(ITEM_ENTRYIDS, PT_MV_BINARY, PSETID_Archive, dispidItemEntryIds)
	PROPMAP_INIT(lpFolder)
	sptaContentProps.aulPropTag[0] = PROP_ITEM_ENTRYIDS;
	memcpy(prefixData + 4, lpArchiveGuid, sizeof(GUID));

	for (size_t i = 0; i < ARRAY_SIZE(ulFlagArray); ++i) {
		MAPITablePtr ptrTable;

		auto hr = lpFolder->GetContentsTable(ulFlagArray[i], &~ptrTable);
		if (hr != hrSuccess)
			return hr;
		hr = ptrTable->SetColumns(sptaContentProps, TBL_BATCH);
		if (hr != hrSuccess)
			return hr;

		while (true) {
			SRowSetPtr ptrRows;
			const ULONG batch_size = 128;

			hr = ptrTable->QueryRows(batch_size, 0, &~ptrRows);
			if (hr != hrSuccess)
				return hr;

			for (SRowSetPtr::size_type j = 0; j < ptrRows.size(); ++j) {
				const auto &prop = ptrRows[j].lpProps[0];
				if (PROP_TYPE(prop.ulPropTag) == PT_ERROR)
					continue;
				const auto &m = prop.Value.MVbin;
				std::copy_if(&m.lpbin[0], &m.lpbin[m.cValues], std::inserter(*lpReferences, lpReferences->begin()),
					[&](const auto &e) { return e.cb >= sizeof(prefixData) && memcmp(e.lpb, prefixData, sizeof(prefixData)) == 0; });
			}
			if (ptrRows.size() < batch_size)
				break;
		}
	}
	return hrSuccess;
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
	EntryIDSet setEntries, setFolderExcludes;
	ECFolderIterator iEnd;
	MAPIFolderPtr ptrFolder;

	auto hr = AppendAllEntries(lpArchive, lpRestriction, &setEntries);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Unable to get all entries from the root archive folder", hr);

	// Exclude everything below the special folder root because that's were we store messages
	// that have not references to the primary store.
	hr = ptrArchiveHelper->GetSpecialsRootFolder(&~ptrFolder);
	if (hr == hrSuccess)
		hr = AppendFolderEntries(ptrFolder, &setFolderExcludes);

	ptrFolder.reset();

	try {
		for (ECFolderIterator i = ECFolderIterator(lpArchive, fMapiDeferredErrors, 0); i != iEnd; ++i) {
			SPropValuePtr ptrProp;

			hr = HrGetOneProp(*i, PR_ENTRYID, &~ptrProp);
			if (hr != hrSuccess)
				return hr;
			if (setFolderExcludes.find(ptrProp->Value.bin) != setFolderExcludes.end()) {
				m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Skipping special folder");
				continue;
			}
			hr = AppendAllEntries(*i, lpRestriction, &setEntries);
			if (hr != hrSuccess)
				return m_lpLogger->perr("Unable to get all references from archive folder", hr);
		}
	} catch (const KMAPIError &e) {
		return m_lpLogger->perr("Failed to iterate archive folders", e.code());
	}
	*lpEntries = std::move(setEntries);
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
	MAPITablePtr ptrTable;
	ECAndRestriction resContent;
	static constexpr const SizedSPropTagArray(1, sptaContentProps) = {1, {PR_ENTRYID}};

	PROPMAP_START(1)
	PROPMAP_NAMED_ID(REF_ITEM_ENTRYID, PT_BINARY, PSETID_Archive, dispidRefItemEntryId)
	PROPMAP_INIT(lpArchive)

	resContent += ECExistRestriction(PROP_REF_ITEM_ENTRYID);
	if (lpRestriction)
		resContent += ECRawRestriction(lpRestriction, ECRestriction::Cheap);
	auto hr = lpArchive->GetContentsTable(0, &~ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns(sptaContentProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = resContent.RestrictTable(ptrTable);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		SRowSetPtr ptrRows;
		const ULONG batch_size = 128;

		hr = ptrTable->QueryRows(batch_size, 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;
		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (PROP_TYPE(ptrRows[i].lpProps[0].ulPropTag) == PT_ERROR)
				return ptrRows[i].lpProps[0].Value.err;
			lpEntries->insert(ptrRows[i].lpProps[0].Value.bin);
		}
		if (ptrRows.size() < batch_size)
			break;
	}
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
	MAPITablePtr ptrTable;
	static constexpr const SizedSSortOrderSet(1, ssosHierarchy) = {1, 0, 0, {{PR_DEPTH, TABLE_SORT_ASCEND}}};
	SizedSPropTagArray(5, sptaHierarchyProps) = {5, {PR_NULL, PR_ENTRYID, PR_CONTENT_COUNT, PR_FOLDER_CHILD_COUNT, PR_DISPLAY_NAME}};
	enum {IDX_REF_ITEM_ENTRYID, IDX_ENTRYID, IDX_CONTENT_COUNT, IDX_FOLDER_CHILD_COUNT, IDX_DISPLAY_NAME};

	PROPMAP_START(1)
	PROPMAP_NAMED_ID(REF_ITEM_ENTRYID, PT_BINARY, PSETID_Archive, dispidRefItemEntryId)
	PROPMAP_INIT(lpArchiveRoot)

	sptaHierarchyProps.aulPropTag[IDX_REF_ITEM_ENTRYID] = PROP_REF_ITEM_ENTRYID;
	auto hr = lpArchiveRoot->GetHierarchyTable(CONVENIENT_DEPTH, &~ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns(sptaHierarchyProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ECExistRestriction(PROP_REF_ITEM_ENTRYID)
	     .RestrictTable(ptrTable, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SortTable(ssosHierarchy, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		SRowSetPtr ptrRows;

		hr = ptrTable->QueryRows(64, 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;
		if (ptrRows.empty())
			break;

		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			ULONG ulType = 0;
			MAPIFolderPtr ptrPrimaryFolder;

			ScopedFolderLogging sfl(m_lpLogger, ptrRows[i].lpProps[IDX_DISPLAY_NAME].ulPropTag == PR_DISPLAY_NAME ? ptrRows[i].lpProps[IDX_DISPLAY_NAME].Value.LPSZ : KC_T("<Unnamed>"));

			// If the cleanup action is delete, we don't want to delete a folder that's not empty because it might contain messages that
			// have been moved in the primary store before the original folder was deleted. If we were to delete the folder in the archive
			// we would lose that data.
			// But if the cleanup action is store, we do want to move the folder with content so the hierarchy is preserved.

			if (m_cleanupAction == caDelete) {
				// The content count and folder child count should always exist. If not we'll skip the folder
				// just to be safe.
				if (PROP_TYPE(ptrRows[i].lpProps[IDX_CONTENT_COUNT].ulPropTag) == PT_ERROR) {
					m_lpLogger->pwarn("Skipping folder due to inability to obtain folder content count",
						ptrRows[i].lpProps[IDX_CONTENT_COUNT].Value.err);
					continue;
				} else if (ptrRows[i].lpProps[IDX_CONTENT_COUNT].Value.l != 0) {
					m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Folder contains messages. Skipping folder.");
					continue;
				}
				if (PROP_TYPE(ptrRows[i].lpProps[IDX_FOLDER_CHILD_COUNT].ulPropTag) == PT_ERROR)	{
					m_lpLogger->pwarn("Skipping folder due to inability to obtain folder child count",
						ptrRows[i].lpProps[IDX_FOLDER_CHILD_COUNT].Value.err);
					continue;
				} else if (ptrRows[i].lpProps[IDX_FOLDER_CHILD_COUNT].Value.l != 0) {
					m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Folder has subfolders in it. Skipping folder.");
					continue;
				}
			}

			hr = lpUserStore->OpenEntry(ptrRows[i].lpProps[IDX_REF_ITEM_ENTRYID].Value.bin.cb, reinterpret_cast<ENTRYID *>(ptrRows[i].lpProps[IDX_REF_ITEM_ENTRYID].Value.bin.lpb),
			     &iid_of(ptrPrimaryFolder), 0, &ulType, &~ptrPrimaryFolder);
			if (hr == MAPI_E_NOT_FOUND) {
				MAPIFolderPtr ptrArchiveFolder;
				SPropValuePtr ptrProp;

				hr = lpArchiveRoot->OpenEntry(ptrRows[i].lpProps[IDX_ENTRYID].Value.bin.cb, reinterpret_cast<ENTRYID *>(ptrRows[i].lpProps[IDX_ENTRYID].Value.bin.lpb),
				     &iid_of(ptrArchiveFolder), MAPI_MODIFY, &ulType, &~ptrArchiveFolder);
				if (hr != hrSuccess)
					return hr;
				// Check if we still have a back-ref
				if (HrGetOneProp(ptrArchiveFolder, PROP_REF_ITEM_ENTRYID, &~ptrProp) != hrSuccess) {
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
					m_lpLogger->pwarn("Unable to process dead folder", hr);
			}
			if (hr != hrSuccess)
				return hr;
		}
	}
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
	MAPIFolderPtr ptrDelItemsFolder;
	EntryListPtr ptrMessageList;

	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Moving %zu messages to the special \"Deleted Items\" folder...", setEIDs.size());
	auto hr = ptrArchiveHelper->GetDeletedItemsFolder(&~ptrDelItemsFolder);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get deleted items folder", hr);
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~ptrMessageList);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to allocate %zu bytes of memory: %s (%x)",
			sizeof(ENTRYLIST), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	ptrMessageList->cValues = 0;

	hr = MAPIAllocateMore(sizeof(SBinary) * setEIDs.size(), ptrMessageList, reinterpret_cast<void **>(&ptrMessageList->lpbin));
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to allocate %zu bytes of memory: %s (%x)",
			sizeof(SBinary) * setEIDs.size(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Processing %zu messages", setEIDs.size());
	for (const auto &e : setEIDs) {
		ULONG ulType;
		MAPIPropPtr ptrMessage;
		MAPIPropHelperPtr ptrHelper;

		hr = lpArchiveFolder->OpenEntry(e.size(), e, &iid_of(ptrMessage), MAPI_MODIFY, &ulType, &~ptrMessage);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to open message", hr);
		hr = MAPIPropHelper::Create(ptrMessage, &ptrHelper);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to create helper object", hr);
		hr = ptrHelper->ClearReference(true);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to clear back reference", hr);
		ptrMessageList->lpbin[ptrMessageList->cValues].cb = e.size();
		ptrMessageList->lpbin[ptrMessageList->cValues++].lpb = e;
		assert(ptrMessageList->cValues <= setEIDs.size());
	}

	hr = lpArchiveFolder->CopyMessages(ptrMessageList, &iid_of(ptrDelItemsFolder), ptrDelItemsFolder, 0, NULL, MESSAGE_MOVE);
	if (hr != hrSuccess)
		m_lpLogger->perr("Failed to move messages", hr);
	return hr;
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
	SPropValuePtr ptrEntryID;
	MAPIFolderPtr ptrDelItemsFolder;
	MAPIPropHelperPtr ptrHelper;
	ECFolderIterator iEnd;

	m_lpLogger->logf(EC_LOGLEVEL_INFO, "Moving folder to the special \"Deleted Items\" folder...");
	auto hr = HrGetOneProp(lpArchiveFolder, PR_ENTRYID, &~ptrEntryID);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get folder entryid", hr);
	hr = ptrArchiveHelper->GetDeletedItemsFolder(&~ptrDelItemsFolder);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get deleted items folder", hr);
	hr = MAPIPropHelper::Create(object_ptr<IMAPIProp>(lpArchiveFolder), &ptrHelper);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to create helper object", hr);
	hr = ptrHelper->ClearReference(true);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to clear back reference", hr);

	// Get rid of references of all subfolders
	try {
		for (ECFolderIterator i = ECFolderIterator(lpArchiveFolder, fMapiDeferredErrors, 0); i != iEnd; ++i) {
			MAPIPropHelperPtr ptrSubHelper;

			hr = MAPIPropHelper::Create(object_ptr<IMAPIProp>(*i), &ptrSubHelper);
			if (hr != hrSuccess)
				return hr;
			hr = ptrSubHelper->ClearReference(true);
			if (hr != hrSuccess)
				m_lpLogger->Log(EC_LOGLEVEL_INFO, "Failed to clean reference of subfolder.");
		}
	} catch (const KMAPIError &e) {
		return m_lpLogger->perr("Failed to iterate folders", e.code());
	}

	hr = lpArchiveFolder->CopyFolder(ptrEntryID->Value.bin.cb, (LPENTRYID)ptrEntryID->Value.bin.lpb, &iid_of(ptrDelItemsFolder), ptrDelItemsFolder, NULL, 0, NULL, FOLDER_MOVE);
	if (hr != hrSuccess)
		m_lpLogger->perr("Failed to move folder", hr);
	return hr;
}

/**
 * Delete the messages in setEIDs from the folder lpArchiveFolder.
 *
 * @param[in]	lpArchiveFolder		The folder to delete the messages from.
 * @param[in]	setEIDs				The set of entryids of the messages to delete.
 */
HRESULT ArchiveControlImpl::DeleteMessages(LPMAPIFOLDER lpArchiveFolder, const EntryIDSet &setEIDs)
{
	EntryListPtr ptrMessageList;

	m_lpLogger->logf(EC_LOGLEVEL_INFO, "Deleting %zu messages...", setEIDs.size());
	auto hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~ptrMessageList);
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to allocate %zu bytes of memory: %s (%x)",
			sizeof(ENTRYLIST), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	ptrMessageList->cValues = 0;

	hr = MAPIAllocateMore(sizeof(SBinary) * setEIDs.size(), ptrMessageList, reinterpret_cast<void **>(&ptrMessageList->lpbin));
	if (hr != hrSuccess) {
		m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to allocate %zu bytes of memory: %s (%x)",
			sizeof(SBinary) * setEIDs.size(), GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Processing %zu messages", setEIDs.size());
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
	SPropValuePtr ptrEntryId;

	m_lpLogger->Log(EC_LOGLEVEL_INFO, "Deleting folder...");
	auto hr = HrGetOneProp(lpArchiveFolder, PR_ENTRYID, &~ptrEntryId);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get folder entryid", hr);

	// Delete yourself!
	hr = lpArchiveFolder->DeleteFolder(ptrEntryId->Value.bin.cb, (LPENTRYID)ptrEntryId->Value.bin.lpb, 0, NULL, DEL_FOLDERS|DEL_MESSAGES|DEL_ASSOCIATED);
	if (FAILED(hr))
		m_lpLogger->perr("Failed to delete folder", hr);
	else if (hr != hrSuccess)
		m_lpLogger->pwarn("Folder only got partially deleted", hr);
	return hr;
}

/**
 * Append the entryid of the passed folder and all its subfolder to a list.
 * @param[in]	lpBase		The folder to start processing.
 * @param[out]	lpEntries	The returned set of entryids.
 */
HRESULT ArchiveControlImpl::AppendFolderEntries(LPMAPIFOLDER lpBase, EntryIDSet *lpEntries)
{
	SPropValuePtr ptrProp;
	MAPITablePtr ptrTable;
	static constexpr const SizedSPropTagArray(1, sptaTableProps) = {1, {PR_ENTRYID}};

	auto hr = HrGetOneProp(lpBase, PR_ENTRYID, &~ptrProp);
	if (hr != hrSuccess)
		return hr;
	lpEntries->insert(ptrProp->Value.bin);
	hr = lpBase->GetHierarchyTable(CONVENIENT_DEPTH, &~ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns(sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		SRowSetPtr ptrRows;

		hr = ptrTable->QueryRows(128, 0, &~ptrRows);
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
		m_lpLogger->logf(loglevel, "\"delete_enable\" is set to \"%s\" and \"cleanup_follow_purge_after\" is set to \"%s\"",
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
		m_lpLogger->logf(loglevel, "\"delete_enable\" is set to \"%s\" and \"cleanup_follow_purge_after\" is set to \"%s\"",
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

} /* namespace */
