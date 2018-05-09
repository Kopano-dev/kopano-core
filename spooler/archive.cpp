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
#include <iostream>
#include <new>
#include <utility>
#include "archive.h"

#include <kopano/ECLogger.h>
#include <kopano/ECGetText.h>
#include <kopano/MAPIErrors.h>
#include <kopano/charset/convert.h>
#include <kopano/mapi_ptr.h>
#include <kopano/scope.hpp>
#include "helpers/StoreHelper.h"
#include "operations/copier.h"
#include "operations/instanceidmapper.h"
#include "ArchiverSession.h"
#include "helpers/ArchiveHelper.h"

#include <list>
#include <sstream>

#include <kopano/Util.h>
#include "PyMapiPlugin.h"

using namespace KC;
using namespace KC::helpers;
using namespace KC::operations;
using std::endl;
using std::list;
using std::pair;
using std::string;

typedef std::wostringstream tostringstream;

void ArchiveResult::AddMessage(MessagePtr ptrMessage) {
	m_lstMessages.emplace_back(ptrMessage);
}

void ArchiveResult::Undo(IMAPISession *lpSession) {
	for (const auto i : m_lstMessages)
		Util::HrDeleteMessage(lpSession, i);
}

HRESULT Archive::Create(IMAPISession *lpSession, ArchivePtr *lpptrArchive)
{
	if (lpSession == NULL || lpptrArchive == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto x = new(std::nothrow) Archive(lpSession);
	if (x == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	lpptrArchive->reset(x);
	return hrSuccess;
}

Archive::Archive(IMAPISession *lpSession)
: m_ptrSession(lpSession, true)
{
}

HRESULT Archive::HrArchiveMessageForDelivery(IMessage *lpMessage)
{
	HRESULT hr = hrSuccess;
	ULONG cMsgProps;
	SPropArrayPtr ptrMsgProps;
	MsgStorePtr ptrStore;
	ULONG ulType;
	MAPIFolderPtr ptrFolder;
	StoreHelperPtr ptrStoreHelper;
	SObjectEntry refMsgEntry;
	ObjectEntryList lstArchives;
	ArchiverSessionPtr ptrSession;
	InstanceIdMapperPtr ptrMapper;
	std::unique_ptr<Copier::Helper> ptrHelper;
	list<pair<MessagePtr,PostSaveActionPtr> > lstArchivedMessages;
	ArchiveResult result;
	ObjectEntryList lstReferences;
	MAPIPropHelperPtr ptrMsgHelper;
	static constexpr const SizedSPropTagArray(3, sptaMessageProps) =
		{3, {PR_ENTRYID, PR_STORE_ENTRYID, PR_PARENT_ENTRYID}};
	enum {IDX_ENTRYID, IDX_STORE_ENTRYID, IDX_PARENT_ENTRYID};

	auto cleanup = make_scope_success([&]() {
		/* On error, delete all saved archives. */
		if (FAILED(hr))
			result.Undo(m_ptrSession);
	});
	if (lpMessage == NULL) {
		ec_log_warn("Archive::HrArchiveMessageForDelivery(): invalid parameter");
		return MAPI_E_INVALID_PARAMETER;
	}
	hr = lpMessage->GetProps(sptaMessageProps, 0, &cMsgProps, &~ptrMsgProps);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForDelivery(): GetProps failed", hr);
	refMsgEntry.sStoreEntryId = ptrMsgProps[IDX_STORE_ENTRYID].Value.bin;
	refMsgEntry.sItemEntryId = ptrMsgProps[IDX_ENTRYID].Value.bin;
	hr = m_ptrSession->OpenMsgStore(0, ptrMsgProps[IDX_STORE_ENTRYID].Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(ptrMsgProps[IDX_STORE_ENTRYID].Value.bin.lpb),
	     &iid_of(ptrStore), MDB_WRITE, &~ptrStore);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForDelivery(): OpenMsgStore failed", hr);
	hr = StoreHelper::Create(ptrStore, &ptrStoreHelper);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForDelivery(): StoreHelper::Create failed", hr);
	hr = ptrStoreHelper->GetArchiveList(&lstArchives);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForDelivery(): StoreHelper::GetArchiveList failed", hr);
	if (lstArchives.empty()) {
		ec_log_debug("No archives attached to store");
		return hrSuccess;
	}
	hr = ptrStore->OpenEntry(ptrMsgProps[IDX_PARENT_ENTRYID].Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(ptrMsgProps[IDX_PARENT_ENTRYID].Value.bin.lpb),
	     &iid_of(ptrFolder), MAPI_MODIFY, &ulType, &~ptrFolder);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForDelivery(): StoreHelper::OpenEntry failed", hr);
	hr = ArchiverSession::Create(m_ptrSession, ec_log_get(), &ptrSession);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForDelivery(): ArchiverSession::Create failed", hr);
	/**
	 * @todo: Create an archiver config object globally in the calling application to
	 *        avoid the creation of the configuration for each message to be archived.
	 */
	hr = InstanceIdMapper::Create(ec_log_get(), NULL, &ptrMapper);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForDelivery(): InstanceIdMapper::Create failed", hr);
	// First create all (mostly one) the archive messages without saving them.
	ptrHelper.reset(new(std::nothrow) Copier::Helper(ptrSession,
		ec_log_get(), ptrMapper, nullptr, ptrFolder));
	if (ptrHelper == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	for (const auto &arc : lstArchives) {
		MessagePtr ptrArchivedMsg;
		PostSaveActionPtr ptrPSAction;

		hr = ptrHelper->CreateArchivedMessage(lpMessage, arc, refMsgEntry, &~ptrArchivedMsg, &ptrPSAction);
		if (hr != hrSuccess)
			return kc_pwarn("Archive::HrArchiveMessageForDelivery(): CreateArchivedMessage failed", hr);
		lstArchivedMessages.emplace_back(ptrArchivedMsg, ptrPSAction);
	}

	// Now save the messages one by one. On failure all saved messages need to be deleted.
	for (const auto &msg : lstArchivedMessages) {
		ULONG cArchivedMsgProps;
		SPropArrayPtr ptrArchivedMsgProps;
		SObjectEntry refArchiveEntry;

		hr = msg.first->GetProps(sptaMessageProps, 0,
		     &cArchivedMsgProps, &~ptrArchivedMsgProps);
		if (hr != hrSuccess)
			return kc_pwarn("Archive::HrArchiveMessageForDelivery(): ArchivedMessage GetProps failed", hr);
		refArchiveEntry.sItemEntryId = ptrArchivedMsgProps[IDX_ENTRYID].Value.bin;
		refArchiveEntry.sStoreEntryId = ptrArchivedMsgProps[IDX_STORE_ENTRYID].Value.bin;
		lstReferences.emplace_back(refArchiveEntry);
		hr = msg.first->SaveChanges(KEEP_OPEN_READWRITE);
		if (hr != hrSuccess)
			return kc_pwarn("Archive::HrArchiveMessageForDelivery(): ArchivedMessage SaveChanges failed", hr);
		if (msg.second) {
			HRESULT hrTmp = msg.second->Execute();
			if (hrTmp != hrSuccess)
				kc_pwarn("Failed to execute post save action", hrTmp);
		}

		result.AddMessage(msg.first);
	}

	// Now add the references to the original message.
	lstReferences.sort();
	lstReferences.unique();

	hr = MAPIPropHelper::Create(MAPIPropPtr(lpMessage, true), &ptrMsgHelper);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForDelivery(): failed creating reference to original message", hr);
	return ptrMsgHelper->SetArchiveList(lstReferences, true);
}

HRESULT Archive::HrArchiveMessageForSending(IMessage *lpMessage, ArchiveResult *lpResult)
{
	HRESULT hr = hrSuccess;
	ULONG cMsgProps;
	SPropArrayPtr ptrMsgProps;
	MsgStorePtr ptrStore;
	StoreHelperPtr ptrStoreHelper;
	ObjectEntryList lstArchives;
	ArchiverSessionPtr ptrSession;
	InstanceIdMapperPtr ptrMapper;
	std::unique_ptr<Copier::Helper> ptrHelper;
	list<pair<MessagePtr,PostSaveActionPtr> > lstArchivedMessages;
	ArchiveResult result;
	static constexpr const SizedSPropTagArray(2, sptaMessageProps) = {1, {PR_STORE_ENTRYID}};
	enum {IDX_STORE_ENTRYID};

	auto cleanup = make_scope_success([&]() {
		/* On error, delete all saved archives. */
		if (FAILED(hr))
			result.Undo(m_ptrSession);
	});
	if (lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	hr = lpMessage->GetProps(sptaMessageProps, 0, &cMsgProps, &~ptrMsgProps);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForSending(): GetProps failed", hr);
	hr = m_ptrSession->OpenMsgStore(0, ptrMsgProps[IDX_STORE_ENTRYID].Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(ptrMsgProps[IDX_STORE_ENTRYID].Value.bin.lpb),
	     &iid_of(ptrStore), 0, &~ptrStore);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForSending(): OpenMsgStore failed", hr);
	hr = StoreHelper::Create(ptrStore, &ptrStoreHelper);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForSending(): StoreHelper::Create failed", hr);
	hr = ptrStoreHelper->GetArchiveList(&lstArchives);
	if (hr != hrSuccess) {
		SetErrorMessage(hr, _("Unable to obtain list of attached archives."));
		return kc_perror("Unable to obtain list of attached archives", hr);
	}

	if (lstArchives.empty()) {
		ec_log_debug("No archives attached to store");
		return hr;
	}

	hr = ArchiverSession::Create(m_ptrSession, ec_log_get(), &ptrSession);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForSending(): ArchiverSession::Create failed", hr);
	/**
	 * @todo: Create an archiver config object globally in the calling application to
	 *        avoid the creation of the configuration for each message to be archived.
	 */
	hr = InstanceIdMapper::Create(ec_log_get(), NULL, &ptrMapper);
	if (hr != hrSuccess)
		return kc_pwarn("Archive::HrArchiveMessageForSending(): InstanceIdMapper::Create failed", hr);
	// First create all (mostly one) the archive messages without saving them.
	// We pass an empty MAPIFolderPtr here!
	ptrHelper.reset(new(std::nothrow) Copier::Helper(ptrSession,
		ec_log_get(), ptrMapper, nullptr, MAPIFolderPtr()));
	if (ptrHelper == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	for (const auto &arc : lstArchives) {
		ArchiveHelperPtr ptrArchiveHelper;
		MAPIFolderPtr ptrArchiveFolder;
		MessagePtr ptrArchivedMsg;
		PostSaveActionPtr ptrPSAction;

		hr = ArchiveHelper::Create(ptrSession, arc, ec_log_get(), &ptrArchiveHelper);
		if (hr != hrSuccess) {
			SetErrorMessage(hr, _("Unable to open archive."));
			return hr;
		}
		hr = ptrArchiveHelper->GetOutgoingFolder(&~ptrArchiveFolder);
		if (hr != hrSuccess) {
			SetErrorMessage(hr, _("Unable to get outgoing archive folder."));
			return kc_perror("Failed to get outgoing archive folder", hr);
		}
		hr = ptrArchiveFolder->CreateMessage(&iid_of(ptrArchivedMsg), 0, &~ptrArchivedMsg);
		if (hr != hrSuccess) {
			SetErrorMessage(hr, _("Unable to create archive message in outgoing archive folder."));
			return kc_perror("Failed to create message in outgoing archive folder", hr);
		}

		hr = ptrHelper->ArchiveMessage(lpMessage, NULL, ptrArchivedMsg, &ptrPSAction);
		if (hr != hrSuccess) {
			SetErrorMessage(hr, _("Unable to copy message data."));
			return hr;
		}

		ec_log_info("Stored message in archive");
		lstArchivedMessages.emplace_back(ptrArchivedMsg, ptrPSAction);
	}

	// Now save the messages one by one. On failure all saved messages need to be deleted.
	for (const auto &msg : lstArchivedMessages) {
		hr = msg.first->SaveChanges(KEEP_OPEN_READONLY);
		if (hr != hrSuccess) {
			SetErrorMessage(hr, _("Unable to save archived message."));
			return kc_perror("Failed to save message in archive", hr);
		}

		if (msg.second) {
			HRESULT hrTmp = msg.second->Execute();
			if (hrTmp != hrSuccess)
				kc_pwarn("Failed to execute post save action", hrTmp);
		}

		result.AddMessage(msg.first);
	}

	if (lpResult)
		std::swap(result, *lpResult);
	return hrSuccess;
}

void Archive::SetErrorMessage(HRESULT hr, LPCTSTR lpszMessage)
{
	tostringstream	oss;
	LPTSTR lpszDesc;

	oss << lpszMessage << endl;
	oss << _("Error code:") << KC_T(" ") << convert_to<tstring>(GetMAPIErrorMessage(hr))
		<< KC_T(" (") << tstringify(hr, true) << KC_T(")") << endl;
	if (Util::HrMAPIErrorToText(hr, &lpszDesc) == hrSuccess)
		oss << _("Error description:") << KC_T(" ") << lpszDesc << endl;
	m_strErrorMessage.assign(oss.str());
}

#ifndef ENABLE_PYTHON
PyMapiPluginFactory::PyMapiPluginFactory() {}
PyMapiPluginFactory::~PyMapiPluginFactory() {}

HRESULT PyMapiPluginFactory::create_plugin(ECConfig *,
    const char *, pym_plugin_intf **ret)
{
	*ret = new(std::nothrow) pym_plugin_intf;
	return *ret != nullptr ? hrSuccess : MAPI_E_NOT_ENOUGH_MEMORY;
}
#endif
