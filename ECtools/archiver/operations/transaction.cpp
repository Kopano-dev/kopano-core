/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <utility>
#include <kopano/Util.h>
#include "transaction.h"
#include "ArchiverSession.h"

namespace KC { namespace operations {

Transaction::Transaction(const SObjectEntry &objectEntry) : m_objectEntry(objectEntry)
{ }

HRESULT Transaction::SaveChanges(ArchiverSessionPtr ptrSession, RollbackPtr *lpptrRollback)
{
	HRESULT hr = hrSuccess;
	RollbackPtr ptrRollback(new Rollback());
	bool bPSAFailure = false;

	if (lpptrRollback == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	for (const auto &msg : m_lstSave) {
		if (msg.bDeleteOnFailure) {
			hr = ptrRollback->Delete(ptrSession, msg.ptrMessage);
			if (hr == hrSuccess)
				goto exit;
		}
		hr = msg.ptrMessage->SaveChanges(0);
		if (hr != hrSuccess)
			goto exit;
		if (msg.ptrPSAction != NULL && msg.ptrPSAction->Execute() != hrSuccess)
			bPSAFailure = true;
	}

	*lpptrRollback = ptrRollback;

exit:
	if (hr != hrSuccess)
		ptrRollback->Execute(ptrSession);

	if (hr == hrSuccess && bPSAFailure)
		hr = MAPI_W_ERRORS_RETURNED;

	return hr;
}

HRESULT Transaction::PurgeDeletes(ArchiverSessionPtr ptrSession, TransactionPtr ptrDeferredTransaction)
{
	HRESULT hr = hrSuccess;
	MessagePtr ptrMessage;
	IMAPISession *lpSession = ptrSession->GetMAPISession();

	for (const auto &obj : m_lstDelete) {
		HRESULT hrTmp;
		if (obj.bDeferredDelete && ptrDeferredTransaction != NULL)
			hrTmp = ptrDeferredTransaction->Delete(obj.objectEntry);

		else {
			hrTmp = lpSession->OpenEntry(obj.objectEntry.sItemEntryId.size(), obj.objectEntry.sItemEntryId,
			        &iid_of(ptrMessage), 0, nullptr, &~ptrMessage);
			if (hrTmp == MAPI_E_NOT_FOUND) {
				MsgStorePtr ptrStore;

				// Try to open the message on the store
				hrTmp = ptrSession->OpenStore(obj.objectEntry.sStoreEntryId, &~ptrStore);
				if (hrTmp == hrSuccess)
					hrTmp = ptrStore->OpenEntry(obj.objectEntry.sItemEntryId.size(),
					        obj.objectEntry.sItemEntryId, &iid_of(ptrMessage), 0,
					        nullptr, &~ptrMessage);
			}
			if (hrTmp == hrSuccess)
				hrTmp = Util::HrDeleteMessage(lpSession, ptrMessage);
		}
		if (hrTmp != hrSuccess)
			hr = MAPI_W_ERRORS_RETURNED;
	}

	return hr;
}

HRESULT Transaction::Save(IMessage *lpMessage, bool bDeleteOnFailure, const PostSaveActionPtr &ptrPSAction)
{
	SaveEntry se;
	lpMessage->AddRef();
	se.bDeleteOnFailure = bDeleteOnFailure;
	se.ptrMessage.reset(lpMessage, false);
	se.ptrPSAction = ptrPSAction;
	m_lstSave.emplace_back(std::move(se));
	return hrSuccess;
}

HRESULT Transaction::Delete(const SObjectEntry &objectEntry, bool bDeferredDelete)
{
	DelEntry de;
	de.objectEntry = objectEntry;
	de.bDeferredDelete = bDeferredDelete;
	m_lstDelete.emplace_back(std::move(de));
	return hrSuccess;
}

HRESULT Rollback::Delete(ArchiverSessionPtr ptrSession, IMessage *lpMessage)
{
	SPropArrayPtr ptrMsgProps;
	unsigned int cMsgProps;
	DelEntry entry;
	static constexpr const SizedSPropTagArray(2, sptaMsgProps) =
		{2, {PR_ENTRYID, PR_PARENT_ENTRYID}};
	enum {IDX_ENTRYID, IDX_PARENT_ENTRYID};

	if (lpMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpMessage->GetProps(sptaMsgProps, 0, &cMsgProps, &~ptrMsgProps);
	if (hr != hrSuccess)
		return hr;
	hr = ptrSession->GetMAPISession()->OpenEntry(ptrMsgProps[IDX_PARENT_ENTRYID].Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(ptrMsgProps[IDX_PARENT_ENTRYID].Value.bin.lpb),
	     &iid_of(entry.ptrFolder), MAPI_MODIFY, nullptr, &~entry.ptrFolder);
	if (hr != hrSuccess)
		return hr;
	entry.eidMessage = ptrMsgProps[IDX_ENTRYID].Value.bin;
	m_lstDelete.emplace_back(std::move(entry));
	return hrSuccess;
}

HRESULT Rollback::Execute(ArchiverSessionPtr ptrSession)
{
	HRESULT hr = hrSuccess;
	SBinary entryID = {0, NULL};
	ENTRYLIST entryList = {1, &entryID};

	for (const auto &obj : m_lstDelete) {
		entryID.cb  = obj.eidMessage.size();
		entryID.lpb = obj.eidMessage;
		if (obj.ptrFolder->DeleteMessages(&entryList, 0, NULL, 0) != hrSuccess)
			hr = MAPI_W_ERRORS_RETURNED;
	}

	return hr;
}

}} /* namespace */
