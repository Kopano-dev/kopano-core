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
#include "transaction.h"
#include "ArchiverSession.h"

namespace za { namespace operations {

Transaction::Transaction(const SObjectEntry &objectEntry): m_objectEntry(objectEntry) 
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
			ULONG ulType;

			hrTmp = lpSession->OpenEntry(obj.objectEntry.sItemEntryId.size(), obj.objectEntry.sItemEntryId, &ptrMessage.iid, 0, &ulType, &ptrMessage);
			if (hrTmp == MAPI_E_NOT_FOUND) {
				MsgStorePtr ptrStore;

				// Try to open the message on the store
				hrTmp = ptrSession->OpenStore(obj.objectEntry.sStoreEntryId, &ptrStore);
				if (hrTmp == hrSuccess)
					hrTmp = ptrStore->OpenEntry(obj.objectEntry.sItemEntryId.size(), obj.objectEntry.sItemEntryId, &ptrMessage.iid, 0, &ulType, &ptrMessage);
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
	m_lstSave.push_back(se);
	return hrSuccess;
}

HRESULT Transaction::Delete(const SObjectEntry &objectEntry, bool bDeferredDelete)
{
	DelEntry de;
	de.objectEntry = objectEntry;
	de.bDeferredDelete = bDeferredDelete;
	m_lstDelete.push_back(de);
	return hrSuccess;
}

HRESULT Rollback::Delete(ArchiverSessionPtr ptrSession, IMessage *lpMessage)
{
	HRESULT hr;
	SPropArrayPtr ptrMsgProps;
	ULONG cMsgProps;
	ULONG ulType;
	DelEntry entry;

	SizedSPropTagArray(2, sptaMsgProps) = {2, {PR_ENTRYID, PR_PARENT_ENTRYID}};
	enum {IDX_ENTRYID, IDX_PARENT_ENTRYID};

	if (lpMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;

	hr = lpMessage->GetProps((LPSPropTagArray)&sptaMsgProps, 0, &cMsgProps, &ptrMsgProps);
	if (hr != hrSuccess)
		return hr;
	hr = ptrSession->GetMAPISession()->OpenEntry(ptrMsgProps[IDX_PARENT_ENTRYID].Value.bin.cb, (LPENTRYID)ptrMsgProps[IDX_PARENT_ENTRYID].Value.bin.lpb, &entry.ptrFolder.iid, MAPI_MODIFY, &ulType, &entry.ptrFolder);
	if (hr != hrSuccess)
		return hr;

	entry.eidMessage.assign(ptrMsgProps[IDX_ENTRYID].Value.bin);
	m_lstDelete.push_back(entry);
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

}} // namespace operations, za
