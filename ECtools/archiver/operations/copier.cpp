/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <utility>
#include <kopano/ECConfig.h>
#include <kopano/ECRestriction.h>
#include <kopano/MAPIErrors.h>
#include "ECArchiverLogger.h"
#include "copier.h"
#include "deleter.h"
#include "stubber.h"
#include "instanceidmapper.h"
#include "transaction.h"
#include "postsaveiidupdater.h"
#include "helpers/MAPIPropHelper.h"
#include "helpers/ArchiveHelper.h"
#include "ArchiverSession.h"
#include <kopano/Util.h>
#include <kopano/mapiguidext.h>
#include <list>
#include <string>

using namespace KC::helpers;

namespace KC { namespace operations {

Copier::Helper::Helper(ArchiverSessionPtr ptrSession, std::shared_ptr<ECLogger> lpLogger,
    const InstanceIdMapperPtr &ptrMapper, const SPropTagArray *lpExcludeProps,
    LPMAPIFOLDER lpFolder) :
	m_ptrSession(ptrSession), m_lpLogger(std::move(lpLogger)),
	m_lpExcludeProps(lpExcludeProps), m_ptrFolder(lpFolder, true),
	// do an AddRef so we don't take ownership of the folder
	m_ptrMapper(ptrMapper)
{
}

HRESULT Copier::Helper::CreateArchivedMessage(LPMESSAGE lpSource, const SObjectEntry &archiveEntry, const SObjectEntry &refMsgEntry, LPMESSAGE *lppArchivedMsg, PostSaveActionPtr *lpptrPSAction)
{
	MAPIFolderPtr ptrArchiveFolder;
	MessagePtr ptrNewMessage;
	PostSaveActionPtr ptrPSAction;

	auto hr = GetArchiveFolder(archiveEntry, &~ptrArchiveFolder);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveFolder->CreateMessage(&iid_of(ptrNewMessage), fMapiDeferredErrors, &~ptrNewMessage);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to create archive message", hr);
	hr = ArchiveMessage(lpSource, &refMsgEntry, ptrNewMessage, &ptrPSAction);
	if (hr != hrSuccess)
		return hr;
	hr = ptrNewMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppArchivedMsg));
	if (hr != hrSuccess)
		return hr;
	*lpptrPSAction = std::move(ptrPSAction);
	return hrSuccess;
}

HRESULT Copier::Helper::GetArchiveFolder(const SObjectEntry &archiveEntry, LPMAPIFOLDER *lppArchiveFolder)
{
	if (lppArchiveFolder == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	MAPIFolderPtr ptrArchiveFolder;
	auto iArchiveFolder = m_mapArchiveFolders.find(archiveEntry.sStoreEntryId);
	if (iArchiveFolder == m_mapArchiveFolders.cend()) {
		ArchiveHelperPtr ptrArchiveHelper;

		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Archive folder not found in cache");
		// Find the associated archive folder
		auto hr = ArchiveHelper::Create(m_ptrSession, archiveEntry, m_lpLogger, &ptrArchiveHelper);
		if (hr != hrSuccess)
			return hr;
		hr = ptrArchiveHelper->GetArchiveFolderFor(m_ptrFolder, m_ptrSession, &~ptrArchiveFolder);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to get archive folder", hr);
		m_mapArchiveFolders.emplace(archiveEntry.sStoreEntryId, ptrArchiveFolder);
	} else {
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Archive folder found in cache");
		ptrArchiveFolder = iArchiveFolder->second;
	}

	static constexpr const SizedSPropTagArray(2, sptaProps) =
		{2, {PR_DISPLAY_NAME_A, PR_ENTRYID}};
	SPropArrayPtr props;
	ULONG cb;
	HRESULT hrTmp = ptrArchiveFolder->GetProps(sptaProps, 0, &cb, &~props);
	if (!FAILED(hrTmp)) {
		if (PROP_TYPE(props[0].ulPropTag) != PT_ERROR)
			m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Archive folder: \"%s\'", props[0].Value.lpszA);
		else
			m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Archive folder: has no name");
		m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Archive folder entryid: %s", bin2hex(props[1].Value.bin).c_str());
	}
	return ptrArchiveFolder->QueryInterface(IID_IMAPIFolder,
		reinterpret_cast<LPVOID *>(lppArchiveFolder));
}

HRESULT Copier::Helper::ArchiveMessage(LPMESSAGE lpSource, const SObjectEntry *lpMsgEntry, LPMESSAGE lpDest, PostSaveActionPtr *lpptrPSAction)
{
	if (lpSource == nullptr || lpDest == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	MAPIPropHelperPtr ptrMsgHelper;
	SPropValuePtr ptrEntryId;
	SPropValue sPropArchFlags = {0};
	PostSaveActionPtr ptrPSAction;

	PROPMAP_START(1)
	PROPMAP_NAMED_ID(FLAGS, PT_LONG, PSETID_Archive, dispidFlags)
	PROPMAP_INIT(lpDest)

	auto hr = lpSource->CopyTo(0, nullptr, m_lpExcludeProps, 0, nullptr, &IID_IMessage, lpDest, 0, nullptr);
	// @todo: What to do with warnings?
	if (FAILED(hr))
		return m_lpLogger->perr("Failed to copy message", hr);
	hr = UpdateIIDs(lpSource, lpDest, &ptrPSAction);
	if (hr != hrSuccess) {
		m_lpLogger->perr("Failed to update single instance IDs, continuing with copies.", hr);
		hr = hrSuccess;
	}

	sPropArchFlags.ulPropTag = PROP_FLAGS;
	sPropArchFlags.Value.ul = ARCH_NEVER_DELETE | ARCH_NEVER_STUB;

	hr = lpDest->SetProps(1, &sPropArchFlags, NULL);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to set flags on archive message", hr);
	hr = MAPIPropHelper::Create(MAPIPropPtr(lpDest, true), &ptrMsgHelper);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to create prop helper", hr);
	if (lpMsgEntry) {
		hr = ptrMsgHelper->SetReference(*lpMsgEntry);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to set reference to original message", hr);
	}
	*lpptrPSAction = std::move(ptrPSAction);
	return hr;
}

HRESULT Copier::Helper::UpdateIIDs(LPMESSAGE lpSource, LPMESSAGE lpDest, PostSaveActionPtr *lpptrPSAction)
{
	if (lpSource == nullptr || lpDest == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	MAPITablePtr ptrSourceTable, ptrDestTable;
	unsigned int ulSourceRows = 0, ulDestRows = 0;
	SPropValuePtr ptrSourceServerUID, ptrDestServerUID;
	TaskList lstDeferred;
	static constexpr const SizedSPropTagArray(1, sptaAttachProps) = {1, {PR_ATTACH_NUM}};
	enum {IDX_ATTACH_NUM};

	auto hr = HrGetOneProp(lpSource, PR_EC_SERVER_UID, &~ptrSourceServerUID);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get source server UID", hr);
	hr = HrGetOneProp(lpDest, PR_EC_SERVER_UID, &~ptrDestServerUID);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get dest server UID", hr);
	if (Util::CompareSBinary(ptrSourceServerUID->Value.bin, ptrDestServerUID->Value.bin) == 0) {
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Source and destination live on the same server, no explicit deduplication required.");
		return hr;
	}
	hr = lpSource->GetAttachmentTable(0, &~ptrSourceTable);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get source attachment table", hr);
	hr = ptrSourceTable->SetColumns(sptaAttachProps, TBL_BATCH);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to set source attachment columns", hr);
	hr = ptrSourceTable->GetRowCount(0, &ulSourceRows);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get source attachment count", hr);
	if (ulSourceRows == 0) {
		m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "No attachments in source message, nothing to deduplicate.");
		return hr;
	}
	hr = lpDest->GetAttachmentTable(0, &~ptrDestTable);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get dest attachment table", hr);
	hr = ptrDestTable->SetColumns(sptaAttachProps, TBL_BATCH);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to set dest attachment columns", hr);
	hr = ptrDestTable->GetRowCount(0, &ulDestRows);
	if (hr != hrSuccess)
		return m_lpLogger->perr("Failed to get dest attachment count", hr);
	if (ulSourceRows != ulDestRows) {
		m_lpLogger->logf(EC_LOGLEVEL_WARNING, "Source has %u attachments, destination has %u. No idea how to match them...", ulSourceRows, ulDestRows);
		return MAPI_E_NO_SUPPORT;
	}

	// We'll go through the table one row at a time (from each table) and assume the attachments
	// are sorted the same. We will do a sanity check on the size property, though.
	while (true) {
		SRowSetPtr ptrSourceRows, ptrDestRows;

		hr = ptrSourceTable->QueryRows(16, 0, &~ptrSourceRows);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to query source rows", hr);
		if (ptrSourceRows.empty())
			break;
		hr = ptrDestTable->QueryRows(16, 0, &~ptrDestRows);
		if (hr != hrSuccess)
			return m_lpLogger->perr("Failed to query source rows", hr);
		assert(ptrSourceRows.size() == ptrDestRows.size());
		for (SRowSetPtr::size_type i = 0; i < ptrSourceRows.size(); ++i) {
			AttachPtr ptrSourceAttach, ptrDestAttach;
			SPropValuePtr ptrAttachMethod;
			object_ptr<IECSingleInstance> ptrInstance;
			unsigned int cbSourceSIID, cbDestSIID;
			EntryIdPtr ptrSourceSIID, ptrDestSIID;

			auto hrTmp = lpSource->OpenAttach(ptrSourceRows[i].lpProps[IDX_ATTACH_NUM].Value.ul, nullptr, MAPI_DEFERRED_ERRORS, &~ptrSourceAttach);
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to open source attachment %u: %s (%x). Skipping attachment.",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}
			hrTmp = HrGetOneProp(ptrSourceAttach, PR_ATTACH_METHOD, &~ptrAttachMethod);
			if (hrTmp == MAPI_E_NOT_FOUND) {
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "No PR_ATTACH_METHOD found for attachment %u: %s (%x). Assuming NO_ATTACHMENT. So, nothing to deduplicate.",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to get PR_ATTACH_METHOD for attachment %u: %s (%x). Skipping attachment.",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}
			if (ptrAttachMethod->Value.ul != ATTACH_BY_VALUE) {
				m_lpLogger->logf(EC_LOGLEVEL_DEBUG, "Attachment method for attachment %u is not ATTACH_BY_VALUE. So nothing to deduplicate.", i);
				continue;
			}
			hrTmp = ptrSourceAttach->QueryInterface(iid_of(ptrInstance), &~ptrInstance);
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Unable to get single instance interface for source attachment %u: %s (%x). Skipping attachment.",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}
			hrTmp = ptrInstance->GetSingleInstanceId(&cbSourceSIID, &~ptrSourceSIID);
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Unable to get single instance ID for source attachment %u: %s (%x). Skipping attachment.",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}
			if (cbSourceSIID == 0 || !ptrSourceSIID) {
				m_lpLogger->logf(EC_LOGLEVEL_WARNING, "Got empty single instance ID for attachment %u. That's not suitable for deduplication.", i);
				continue;
			}
			hrTmp = m_ptrMapper->GetMappedInstanceId(ptrSourceServerUID->Value.bin, cbSourceSIID, ptrSourceSIID, ptrDestServerUID->Value.bin, &cbDestSIID, &~ptrDestSIID);
			if (hrTmp == MAPI_E_NOT_FOUND) {
				m_lpLogger->Log(EC_LOGLEVEL_DEBUG, "No mapped IID found, list message for deferred creation of mapping");
				lstDeferred.emplace_back(new TaskMapInstanceId(ptrSourceAttach, MessagePtr(lpDest, true), i));
				continue;
			}
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Unable to get mapped instance ID for attachment %u: %s (%x). Skipping attachment.",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}
			hrTmp = lpDest->OpenAttach(ptrDestRows[i].lpProps[IDX_ATTACH_NUM].Value.ul, nullptr, MAPI_DEFERRED_ERRORS, &~ptrDestAttach);
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Failed to open dest attachment %u: %s (%x). Skipping attachment.",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}
			hrTmp = ptrDestAttach->QueryInterface(iid_of(ptrInstance), &~ptrInstance);
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Unable to get single instance interface for dest attachment %u: %s (%x). Skipping attachment.",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}

			hrTmp = ptrInstance->SetSingleInstanceId(cbDestSIID, ptrDestSIID);
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Unable to set single instance ID for dest attachment %u: %s (%x)",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}

			hrTmp = ptrDestAttach->SaveChanges(0);
			if (hrTmp != hrSuccess) {
				m_lpLogger->logf(EC_LOGLEVEL_ERROR, "Unable to save single instance ID for dest attachment %u: %s (%x)",
					i, GetMAPIErrorMessage(hrTmp), hrTmp);
				continue;
			}

			lstDeferred.emplace_back(new TaskVerifyAndUpdateInstanceId(ptrSourceAttach, MessagePtr(lpDest, true), i, cbDestSIID, ptrDestSIID));
		}
	}

	static_assert(sizeof(PostSaveInstanceIdUpdater) || true, "incomplete type must not be used");
	if (lstDeferred.empty())
		lpptrPSAction->reset();
	else
		lpptrPSAction->reset(new PostSaveInstanceIdUpdater(PR_ATTACH_DATA_BIN, m_ptrMapper, lstDeferred));
	return hrSuccess;
}

/**
 * @param[in]	lpSession
 *					Pointer to the session.
 * @param[in]	lpLogger
 *					Pointer to the logger.
 * @param[in]	lstArchives
 *					The list of attached archives for this store.
 * @param[in]	lpExcludeProps
 *					The list of properties that will not be copied during the archive operation.
 */
Copier::Copier(ArchiverSessionPtr ptrSession, ECConfig *lpConfig,
    std::shared_ptr<ECArchiverLogger> lpLogger, const ObjectEntryList &lstArchives,
    const SPropTagArray *lpExcludeProps, int ulAge, bool bProcessUnread) :
	ArchiveOperationBaseEx(lpLogger, ulAge, bProcessUnread, ARCH_NEVER_ARCHIVE),
	m_ptrSession(ptrSession), m_lpConfig(lpConfig),
	m_lstArchives(lstArchives),
	m_ptrTransaction(new Transaction(SObjectEntry()))
{
	if (KAllocCopy(lpExcludeProps, CbNewSPropTagArray(lpExcludeProps->cValues), &~m_ptrExcludeProps) != hrSuccess)
		throw std::bad_alloc();
	// If the next call fails, m_ptrMapper will have NULL ptr, which we'll check later.
	InstanceIdMapper::Create(lpLogger, lpConfig, &m_ptrMapper);
}

Copier::~Copier()
{
	m_ptrTransaction->PurgeDeletes(m_ptrSession);
}

HRESULT Copier::GetRestriction(LPMAPIPROP lpMapiProp, LPSRestriction *lppRestriction)
{
	ECOrRestriction resResult;
	SRestrictionPtr ptrRestriction;

	PROPMAP_START(1)
	PROPMAP_NAMED_ID(ORIGINAL_SOURCE_KEY, PT_BINARY, PSETID_Archive, dispidOrigSourceKey)
	PROPMAP_INIT(lpMapiProp)

	// Start out with the base restriction, which checks if a message is
	// old enough to be processed.
	auto hr = ArchiveOperationBaseEx::GetRestriction(lpMapiProp, &~ptrRestriction);
	if (hr != hrSuccess)
		return hr;
	resResult += ECRawRestriction(ptrRestriction, ECRestriction::Cheap);

	// A reason to process a message before being old enough is when
	// it's already archived (by archive-on-delivery or because the required
	// age has changed). We'll check that by checking if PROP_ORIGINAL_SOURCE_KEY
	// is present.
	resResult += ECExistRestriction(PROP_ORIGINAL_SOURCE_KEY);
	return resResult.CreateMAPIRestriction(lppRestriction, ECRestriction::Full);
}

HRESULT Copier::EnterFolder(LPMAPIFOLDER lpFolder)
{
	if (!m_ptrMapper)
		return MAPI_E_UNCONFIGURED;

	m_ptrHelper.reset(new Helper(m_ptrSession, Logger(), m_ptrMapper, m_ptrExcludeProps, lpFolder));
	return hrSuccess;
}

HRESULT Copier::LeaveFolder()
{
	if (!m_ptrMapper)
		return MAPI_E_UNCONFIGURED;

	m_ptrHelper.reset();
	return hrSuccess;
}

HRESULT Copier::DoProcessEntry(const SRow &proprow)
{
	if (m_ptrMapper == nullptr)
		return MAPI_E_UNCONFIGURED;

	SObjectEntry refObjectEntry;
	MessagePtr ptrMessageRaw, ptrMessage;
	ULONG ulType = 0;
	MAPIPropHelperPtr ptrMsgHelper;
	MessageState state;
	ObjectEntryList lstMsgArchives, lstNewMsgArchives;
	TransactionList lstTransactions;
	RollbackList lstRollbacks;

	auto lpEntryId = proprow.cfind(PR_ENTRYID);
	if (lpEntryId == NULL) {
		Logger()->Log(EC_LOGLEVEL_FATAL, "PR_ENTRYID missing");
		return MAPI_E_NOT_FOUND;
	}
	auto lpStoreEntryId = proprow.cfind(PR_STORE_ENTRYID);
	if (lpStoreEntryId == NULL) {
		Logger()->Log(EC_LOGLEVEL_FATAL, "PR_STORE_ENTRYID missing");
		return MAPI_E_NOT_FOUND;
	}

	// Set the reference to the original message
	refObjectEntry.sStoreEntryId = lpStoreEntryId->Value.bin;
	refObjectEntry.sItemEntryId = lpEntryId->Value.bin;
	Logger()->logf(EC_LOGLEVEL_DEBUG, "Opening message (%s)", bin2hex(lpEntryId->Value.bin).c_str());
	auto hr = CurrentFolder()->OpenEntry(lpEntryId->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpEntryId->Value.bin.lpb), &IID_IECMessageRaw, MAPI_MODIFY|fMapiDeferredErrors, &ulType, &~ptrMessageRaw);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to open message", hr);
	hr = VerifyRestriction(ptrMessageRaw);
	if (hr == MAPI_E_NOT_FOUND) {
		Logger()->Log(EC_LOGLEVEL_WARNING, "Ignoring message because it doesn't match the criteria for begin archived.");
		Logger()->Log(EC_LOGLEVEL_WARNING, "This can happen when huge amounts of message are being processed.");

		// This is not an error
		hr = hrSuccess;

		// We could process subrestrictions for this message, but that will be picked up at a later stage anyway.
		return hr;
	} else if (hr != hrSuccess) {
		return Logger()->pwarn("Failed to verify message criteria", hr);
	}

	hr = MAPIPropHelper::Create(ptrMessageRaw.as<MAPIPropPtr>(), &ptrMsgHelper);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to create prop helper", hr);
	hr = ptrMsgHelper->GetMessageState(m_ptrSession, &state);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to determine message state", hr);
	if (state.isCopy() || state.isMove()) {
		Logger()->logf(EC_LOGLEVEL_INFO, "Found %s archive, treating it as a new message.", (state.isCopy() ? "copied" : "moved"));

		// Here we reopen the source message with destubbing enabled. This message
		// will be used as source for creating the new archive message. Note that
		// we leave the ptrMsgHelper as is, operating on the raw message.
		hr = CurrentFolder()->OpenEntry(lpEntryId->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpEntryId->Value.bin.lpb), &IID_IMessage, fMapiDeferredErrors, &ulType, &~ptrMessage);
		if (hr != hrSuccess)
			return Logger()->perr("Failed to reopen message", hr);
	} else
		ptrMessage = ptrMessageRaw;

	// From here on we work on ptrMessage, except for ExecuteSubOperations.
	if (!state.isCopy()) {		// Include state.isMove()
		hr = ptrMsgHelper->GetArchiveList(&lstMsgArchives);
		if (hr != hrSuccess) {
			if (hr == MAPI_E_CORRUPT_DATA) {
				Logger()->Log(EC_LOGLEVEL_ERROR, "Existing list of archives is corrupt, skipping message.");
				hr = hrSuccess;
			} else
				Logger()->perr("Failed to get list of archives", hr);
			return hr;
		}
	}

	for (const auto &arc : m_lstArchives) {
		TransactionPtr ptrTransaction;
		auto iArchivedMsg = find_if(lstMsgArchives.cbegin(), lstMsgArchives.cend(), StoreCompare(arc));
		// Check if this message is archived to the current archive.
		if (iArchivedMsg == lstMsgArchives.cend()) {
			// It's possible to have a dirty message that has not been archived to the current archive if at the time of the
			// previous archiver run, this archive was unavailable for some reason.
			// If this happens, we'll can't do much more than just archive the current version of the message to the archive.
			//
			// Alternatively we could get the previous version from another archive and copy that to this archive if the
			// configuration dictates that old archives are linked. We won't do that though!
			Logger()->logf(EC_LOGLEVEL_DEBUG, "Message not yet archived to archive (%s)", arc.sStoreEntryId.tostring().c_str());
			hr = DoInitialArchive(ptrMessage, arc, refObjectEntry, &ptrTransaction);
		} else if (state.isDirty()) {
			// We found an archived version for the current message. However, the message is marked dirty...
			// There are two things we can do:
			// 1. Update the current archived version.
			// 2. Make a new archived message and link that to the old version, basically tracking history.
			//
			// This will be configurable through the track_history option.

			Logger()->Log(EC_LOGLEVEL_INFO, "Found archive for dirty message.");
			if (parseBool(m_lpConfig->GetSetting("track_history"))) {
				Logger()->Log(EC_LOGLEVEL_DEBUG, "Creating new archived message.");
				// DoTrackAndRearchive will always place the new archive in the
				// correct folder and place the old ones in the history folder.
				// However, when the message has moved, the ref entryids need to
				// be updated in the old messages.
				// DoTrackAndRearchive will do that when it detects that the passed
				// refObjectEntry is different than the stored reference it the most
				// recent archive.
				hr = DoTrackAndRearchive(ptrMessage, arc, *iArchivedMsg, refObjectEntry, state.isMove(), &ptrTransaction);
			} else if (!state.isMove()) {
				Logger()->Log(EC_LOGLEVEL_DEBUG, "Updating archived message.");
				hr = DoUpdateArchive(ptrMessage, *iArchivedMsg, refObjectEntry, &ptrTransaction);
			} else {	// Moved, dirty and not tracking history
				Logger()->Log(EC_LOGLEVEL_DEBUG, "Updating and moving archived message.");
				// We could do a move and update here, but since we're not tracking history
				// we can just make a new archived message and get rid of the old one.
				hr = DoInitialArchive(ptrMessage, arc, refObjectEntry, &ptrTransaction);
				if (hr == hrSuccess)
					hr = ptrTransaction->Delete(*iArchivedMsg);
			}
		} else if (!state.isMove()) {
			Logger()->Log(EC_LOGLEVEL_WARNING, "Ignoring already archived message.");
			ptrTransaction = std::make_shared<Transaction>(*iArchivedMsg);
		} else {	// Moved
			Logger()->Log(EC_LOGLEVEL_DEBUG, "Moving archived message.");
			hr = DoMoveArchive(arc, *iArchivedMsg, refObjectEntry, &ptrTransaction);
		}
		if (hr != hrSuccess)
			return hr;
		lstTransactions.emplace_back(ptrTransaction);
	}

	// Once we reach this point all messages have been created and/or updated. We need to
	// save them now. When a transaction is saved it will return a Rollback object we can
	// use to undo the changes when a later save fails.
	for (const auto &ta : lstTransactions) {
		RollbackPtr ptrRollback;
		hr = ta->SaveChanges(m_ptrSession, &ptrRollback);
		if (FAILED(hr)) {
			Logger()->perr("Failed to save changes into archive, rolling back", hr);

			// Rollback
			for (const auto &rb : lstRollbacks) {
				HRESULT hrTmp = rb->Execute(m_ptrSession);
				if (hrTmp != hrSuccess)
					Logger()->perr("Failed to rollback transaction. The archive is consistent, but possibly cluttered.", hrTmp);
			}
			return hr;
		}
		hr = hrSuccess;
		lstRollbacks.emplace_back(ptrRollback);
		lstNewMsgArchives.emplace_back(ta->GetObjectEntry());
	}

	if (state.isDirty()) {
		hr = ptrMsgHelper->SetClean();
		if (hr != hrSuccess) {
			Logger()->Log(EC_LOGLEVEL_WARNING, "Failed to unmark message as dirty.");
			return hr;
		}
	}

	lstNewMsgArchives.sort();
	lstNewMsgArchives.unique();

	hr = ptrMsgHelper->SetArchiveList(lstNewMsgArchives, true);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to save list of archives for this message", hr);
	for (const auto &ta : lstTransactions) {
		HRESULT hrTmp = ta->PurgeDeletes(m_ptrSession, m_ptrTransaction);
		if (hrTmp != hrSuccess)
			Logger()->perr("Failed to remove old archives", hrTmp);
	}
	auto hrTemp = ExecuteSubOperations(ptrMessageRaw, CurrentFolder(), proprow);
	if (hrTemp != hrSuccess)
		Logger()->pwarn("Unable to execute next operation - the operation is postponed, not cancelled.", hrTemp);
	return hrSuccess;
}

void Copier::SetDeleteOperation(DeleterPtr ptrDeleteOp)
{
	m_ptrDeleteOp = ptrDeleteOp;
}

void Copier::SetStubOperation(StubberPtr ptrStubOp)
{
	m_ptrStubOp = ptrStubOp;
}

HRESULT Copier::DoInitialArchive(LPMESSAGE lpMessage, const SObjectEntry &archiveRootEntry, const SObjectEntry &refMsgEntry, TransactionPtr *lpptrTransaction)
{
	MessagePtr ptrNewArchive;
	SPropValuePtr ptrEntryId;
	SObjectEntry objectEntry;
	PostSaveActionPtr ptrPSAction;
	TransactionPtr ptrTransaction;

	assert(lpMessage != NULL);
	assert(lpptrTransaction != NULL);
	auto hr = m_ptrHelper->CreateArchivedMessage(lpMessage, archiveRootEntry, refMsgEntry, &~ptrNewArchive, &ptrPSAction);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrNewArchive, PR_ENTRYID, &~ptrEntryId);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to get entry id of archive message", hr);

	// Update the list of archive messages for this message.
	objectEntry.sStoreEntryId = archiveRootEntry.sStoreEntryId;
	objectEntry.sItemEntryId = ptrEntryId->Value.bin;
	ptrTransaction = std::make_shared<Transaction>(objectEntry);
	hr = ptrTransaction->Save(ptrNewArchive, true, ptrPSAction);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to add archive message to transaction", hr);
	*lpptrTransaction = std::move(ptrTransaction);
	return hrSuccess;
}

HRESULT Copier::DoTrackAndRearchive(LPMESSAGE lpMessage, const SObjectEntry &archiveRootEntry, const SObjectEntry &archiveMsgEntry, const SObjectEntry &refMsgEntry, bool bUpdateHistory, TransactionPtr *lpptrTransaction)
{
	MessagePtr ptrNewArchive, ptrMovedMessage;
	SObjectEntry newArchiveEntry, movedEntry;
	MAPIPropHelperPtr ptrMsgHelper;
	SPropValuePtr ptrEntryId;
	PostSaveActionPtr ptrPSAction;
	TransactionPtr ptrTransaction;

	assert(lpMessage != NULL);
	assert(lpptrTransaction != NULL);
	auto hr = m_ptrHelper->CreateArchivedMessage(lpMessage, archiveRootEntry, refMsgEntry, &~ptrNewArchive, &ptrPSAction);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrNewArchive, PR_ENTRYID, &~ptrEntryId);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to get entry id of archive message", hr);

	// Create the transaction, which is needed by CopyToHistory, now.
	newArchiveEntry.sStoreEntryId = archiveRootEntry.sStoreEntryId;
	newArchiveEntry.sItemEntryId = ptrEntryId->Value.bin;
	ptrTransaction = std::make_shared<Transaction>(newArchiveEntry);
	hr = MoveToHistory(archiveRootEntry, archiveMsgEntry, ptrTransaction, &movedEntry, &~ptrMovedMessage);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to move old archive to history folder", hr);

	hr = MAPIPropHelper::Create(ptrNewArchive.as<MAPIPropPtr>(), &ptrMsgHelper);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to create prop helper", hr);
	hr = ptrMsgHelper->ReferencePrevious(movedEntry);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to set reference to previous archive", hr);
	hr = ptrTransaction->Save(ptrNewArchive, true, ptrPSAction);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to add new archive message to transaction", hr);

	if (bUpdateHistory) {
		assert(ptrMovedMessage);
		// Since the first history message was just moved but not yet saved, we'll set that
		// reference here. The other history messages do exist on the server, so those can
		// be updated through UpdateHistoryRefs.

		hr = MAPIPropHelper::Create(ptrMovedMessage.as<MAPIPropPtr>(), &ptrMsgHelper);
		if (hr != hrSuccess)
			return Logger()->perr("Failed to create prop helper", hr);
		hr = ptrMsgHelper->SetReference(refMsgEntry);
		if (hr != hrSuccess)
			return Logger()->perr("Failed to set reference", hr);
		hr = UpdateHistoryRefs(ptrMovedMessage, refMsgEntry, ptrTransaction);
		if (hr != hrSuccess)
			return hr;
	}
	*lpptrTransaction = std::move(ptrTransaction);
	return hrSuccess;
}

HRESULT Copier::DoUpdateArchive(LPMESSAGE lpMessage, const SObjectEntry &archiveMsgEntry, const SObjectEntry &refMsgEntry, TransactionPtr *lpptrTransaction)
{
	MsgStorePtr ptrArchiveStore;
	ULONG ulType;
	MessagePtr ptrArchivedMsg;
	SPropTagArrayPtr ptrPropList;
	PostSaveActionPtr ptrPSAction;
	TransactionPtr ptrTransaction;

	assert(lpMessage != NULL);
	assert(lpptrTransaction != NULL);
	auto hr = m_ptrSession->OpenStore(archiveMsgEntry.sStoreEntryId, &~ptrArchiveStore);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to open archive store", hr);
	/**
	 * We used to get the raw message here to ensure we're not getting the extra destubbing layer here. However, the messages
	 * in the archive should never be stubbed, so there's no problem.
	 * The main reason to not try to get the raw message through IID_IECMessageRaw is that archive stores don't support it.
	 * @todo Should we verify if the item is really not stubbed?
	 */
	hr = ptrArchiveStore->OpenEntry(archiveMsgEntry.sItemEntryId.size(), archiveMsgEntry.sItemEntryId, &iid_of(ptrArchivedMsg), MAPI_BEST_ACCESS | fMapiDeferredErrors, &ulType, &~ptrArchivedMsg);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to open existing archived message", hr);
	hr = ptrArchivedMsg->GetPropList(fMapiUnicode, &~ptrPropList);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to get property list", hr);
	hr = ptrArchivedMsg->DeleteProps(ptrPropList, NULL);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to delete properties", hr);
	hr = Util::HrDeleteAttachments(ptrArchivedMsg);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to delete attachments", hr);
	hr = Util::HrDeleteRecipients(ptrArchivedMsg);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to delete recipients", hr);
	hr = m_ptrHelper->ArchiveMessage(lpMessage, &refMsgEntry, ptrArchivedMsg, &ptrPSAction);
	if (hr != hrSuccess)
		return hr;
	ptrTransaction = std::make_shared<Transaction>(archiveMsgEntry);
	hr = ptrTransaction->Save(ptrArchivedMsg, false, ptrPSAction);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to add archive message to transaction", hr);
	*lpptrTransaction = std::move(ptrTransaction);
	return hrSuccess;
}

HRESULT Copier::DoMoveArchive(const SObjectEntry &archiveRootEntry, const SObjectEntry &archiveMsgEntry, const SObjectEntry &refMsgEntry, TransactionPtr *lpptrTransaction)
{
	MAPIFolderPtr ptrArchiveFolder;
	MsgStorePtr ptrArchiveStore;
	ULONG ulType;
	MessagePtr ptrArchive, ptrArchiveCopy;
	MAPIPropHelperPtr ptrPropHelper;
	SPropValuePtr ptrEntryId;
	SObjectEntry objectEntry;
	TransactionPtr ptrTransaction;

	assert(lpptrTransaction != NULL);
	auto hr = m_ptrHelper->GetArchiveFolder(archiveRootEntry, &~ptrArchiveFolder);
	if (hr != hrSuccess)
		return hr;
	hr = m_ptrSession->OpenStore(archiveMsgEntry.sStoreEntryId, &~ptrArchiveStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveStore->OpenEntry(archiveMsgEntry.sItemEntryId.size(), archiveMsgEntry.sItemEntryId, &iid_of(ptrArchive), 0, &ulType, &~ptrArchive);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveFolder->CreateMessage(&iid_of(ptrArchiveCopy), 0, &~ptrArchiveCopy);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchive->CopyTo(0, NULL, NULL, 0, NULL, &iid_of(ptrArchiveCopy), ptrArchiveCopy, 0, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIPropHelper::Create(ptrArchiveCopy.as<MAPIPropPtr>(), &ptrPropHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrPropHelper->SetReference(refMsgEntry);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrArchiveCopy, PR_ENTRYID, &~ptrEntryId);
	if (hr != hrSuccess)
		return Logger()->perr("Failed to get entry id of archive message", hr);

	// Create the transaction, which is needed by CopyToHistory, now.
	objectEntry.sStoreEntryId = archiveRootEntry.sStoreEntryId;
	objectEntry.sItemEntryId = ptrEntryId->Value.bin;
	ptrTransaction = std::make_shared<Transaction>(objectEntry);
	hr = ptrTransaction->Save(ptrArchiveCopy, true);
	if (hr == hrSuccess)
		hr = ptrTransaction->Delete(archiveMsgEntry);
	if (hr != hrSuccess)
		return hr;
	hr = UpdateHistoryRefs(ptrArchiveCopy, refMsgEntry, ptrTransaction);
	if (hr != hrSuccess)
		return hr;
	*lpptrTransaction = std::move(ptrTransaction);
	return hrSuccess;
}

HRESULT Copier::ExecuteSubOperations(IMessage *lpMessage,
    IMAPIFolder *lpFolder, const SRow &proprow)
{
	HRESULT hr = hrSuccess;
	assert(lpMessage != NULL);
	assert(lpFolder != NULL);
	if (lpMessage == NULL || lpFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (!m_ptrDeleteOp && !m_ptrStubOp)
		return hrSuccess;

	// First see if the deleter restriction matches, in that case we run the deleter
	// and be done with it.
	if (m_ptrDeleteOp) {
		hr = m_ptrDeleteOp->VerifyRestriction(lpMessage);
		if (hr == hrSuccess) {
			Logger()->Log(EC_LOGLEVEL_DEBUG, "Executing delete operation.");
			hr = m_ptrDeleteOp->ProcessEntry(lpFolder, proprow);
			if (hr != hrSuccess)
				return Logger()->pwarn("Delete operation failed, postponing next attempt", hr);
			Logger()->Log(EC_LOGLEVEL_DEBUG, "Delete operation executed.");
			return hr; /* No point in trying to stub a deleted message. */
		} else if (hr != MAPI_E_NOT_FOUND)
			return hr;

		hr = hrSuccess;
		Logger()->Log(EC_LOGLEVEL_DEBUG, "Message is not eligible for deletion.");
	}

	// Now see if we need to stub the message.
	if (m_ptrStubOp == nullptr)
		return hr;
	hr = m_ptrStubOp->VerifyRestriction(lpMessage);
	if (hr == hrSuccess) {
		Logger()->Log(EC_LOGLEVEL_DEBUG, "Executing stub operation.");
		hr = m_ptrStubOp->ProcessEntry(lpMessage);
		if (hr != hrSuccess)
			Logger()->pwarn("Stub operation failed, postponing next attempt", hr);
		else
			Logger()->Log(EC_LOGLEVEL_DEBUG, "Stub operation executed.");
	} else if (hr == MAPI_E_NOT_FOUND) {
		hr = hrSuccess;
		Logger()->Log(EC_LOGLEVEL_DEBUG, "Message is not eligible for stubbing.");
	}
	return hr;
}

HRESULT Copier::MoveToHistory(const SObjectEntry &sourceArchiveRoot, const SObjectEntry &sourceMsgEntry, TransactionPtr ptrTransaction, SObjectEntry *lpNewEntry, LPMESSAGE *lppNewMessage)
{
	ArchiveHelperPtr ptrArchiveHelper;
	MAPIFolderPtr ptrHistoryFolder;
	ULONG ulType;
	MsgStorePtr ptrArchiveStore;
	MessagePtr ptrArchive, ptrArchiveCopy;
	SPropValuePtr ptrEntryID;

	auto hr = ArchiveHelper::Create(m_ptrSession, sourceArchiveRoot, Logger(), &ptrArchiveHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveHelper->GetHistoryFolder(&~ptrHistoryFolder);
	if (hr != hrSuccess)
		return hr;
	hr = m_ptrSession->OpenStore(sourceMsgEntry.sStoreEntryId, &~ptrArchiveStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchiveStore->OpenEntry(sourceMsgEntry.sItemEntryId.size(), sourceMsgEntry.sItemEntryId, &iid_of(ptrArchive), 0, &ulType, &~ptrArchive);
	if (hr != hrSuccess)
		return hr;
	hr = ptrHistoryFolder->CreateMessage(&iid_of(ptrArchiveCopy), 0, &~ptrArchiveCopy);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrArchiveCopy, PR_ENTRYID, &~ptrEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = ptrArchive->CopyTo(0, NULL, NULL, 0, NULL, &iid_of(ptrArchiveCopy), ptrArchiveCopy, 0, NULL);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTransaction->Save(ptrArchiveCopy, true);
	if (hr == hrSuccess)
		hr = ptrTransaction->Delete(sourceMsgEntry, true);
	if (hr != hrSuccess)
		return hr;

	if (lppNewMessage) {
		hr = ptrArchiveCopy->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppNewMessage));
		if (hr != hrSuccess)
			return hr;
	}
	lpNewEntry->sStoreEntryId = sourceMsgEntry.sStoreEntryId;
	lpNewEntry->sItemEntryId = ptrEntryID->Value.bin;
	return hrSuccess;
}

HRESULT Copier::UpdateHistoryRefs(LPMESSAGE lpArchivedMsg, const SObjectEntry &refMsgEntry, TransactionPtr ptrTransaction)
{
	MAPIPropHelperPtr ptrPropHelper;
	MessagePtr ptrMessage;
	auto hr = MAPIPropHelper::Create(MAPIPropPtr(lpArchivedMsg, true), &ptrPropHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrPropHelper->OpenPrevious(m_ptrSession, &~ptrMessage);
	if (hr == MAPI_E_NOT_FOUND)
		return hrSuccess;
	else if (hr != hrSuccess)
		return hr;
	hr = MAPIPropHelper::Create(ptrMessage.as<MAPIPropPtr>(), &ptrPropHelper);
	if (hr != hrSuccess)
		return hr;
	hr = ptrPropHelper->SetReference(refMsgEntry);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTransaction->Save(ptrMessage, false);
	if (hr != hrSuccess)
		return hr;

	return UpdateHistoryRefs(ptrMessage, refMsgEntry, ptrTransaction);
}

}} /* namespace */
