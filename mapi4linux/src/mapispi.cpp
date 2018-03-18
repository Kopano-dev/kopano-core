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
#include <kopano/memory.hpp>
#include "m4l.mapispi.h"
#include "m4l.mapix.h"
#include <mapi.h>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include <kopano/ECGuid.h>

#include <algorithm>
#include <kopano/mapi_ptr.h>

using namespace KC;

M4LMAPIGetSession::M4LMAPIGetSession(IMAPISession *new_session) :
	session(new_session)
{
	assert(new_session != NULL);
}

HRESULT M4LMAPIGetSession::GetMAPISession(LPUNKNOWN *lppSession)
{
	return session->QueryInterface(IID_IMAPISession, reinterpret_cast<void **>(lppSession));
}

HRESULT M4LMAPIGetSession::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IMAPIGetSession) {
		AddRef();
		*lpvoid = static_cast<IMAPIGetSession *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
    } else
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	return hrSuccess;
}

M4LMAPISupport::M4LMAPISupport(LPMAPISESSION new_session, LPMAPIUID lpUid,
    SVCService *lpService) :
	session(new_session), service(lpService)
{
	if(lpUid) {
		lpsProviderUID.reset(new MAPIUID);
		memcpy(lpsProviderUID.get(), lpUid, sizeof(MAPIUID));
	}
}

M4LMAPISupport::~M4LMAPISupport() {
	for (const auto &i : m_advises)
		MAPIFreeBuffer(i.second.lpKey);
}

HRESULT M4LMAPISupport::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR * lppMAPIError) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::GetMemAllocRoutines(LPALLOCATEBUFFER * lpAllocateBuffer, LPALLOCATEMORE * lpAllocateMore,
											LPFREEBUFFER * lpFreeBuffer) {
	*lpAllocateBuffer = MAPIAllocateBuffer;
	*lpAllocateMore   = MAPIAllocateMore;
	*lpFreeBuffer     = MAPIFreeBuffer;
	return hrSuccess;
}

HRESULT M4LMAPISupport::Subscribe(const NOTIFKEY *lpKey, ULONG ulEventMask,
    ULONG ulFlags, IMAPIAdviseSink *lpAdviseSink, ULONG *lpulConnection)
{
	LPNOTIFKEY lpNewKey = NULL;
	ulock_normal l_adv(m_advises_mutex, std::defer_lock_t());

	/* Copy key (this should prevent deletion of the key while it is still in the list */
	auto hr = MAPIAllocateBuffer(CbNewNOTIFKEY(sizeof(GUID)), reinterpret_cast<void **>(&lpNewKey));
	if (hr != hrSuccess)
		return hr;

	memcpy(lpNewKey, lpKey, sizeof(*lpKey));
	l_adv.lock();
	++m_connections;
	m_advises.emplace(m_connections, M4LSUPPORTADVISE(lpNewKey, ulEventMask, ulFlags, lpAdviseSink));
	*lpulConnection = m_connections;
	return hrSuccess;
}

HRESULT M4LMAPISupport::Unsubscribe(ULONG ulConnection) {
	M4LSUPPORTADVISES::iterator i;
	scoped_lock l_adv(m_advises_mutex);

	i = m_advises.find(ulConnection);
	if (i == m_advises.cend())
		return MAPI_E_NOT_FOUND;
	MAPIFreeBuffer(i->second.lpKey);
	m_advises.erase(i);
	return hrSuccess;
}

HRESULT M4LMAPISupport::Notify(const NOTIFKEY *lpKey, ULONG cNotification,
    NOTIFICATION *lpNotifications, ULONG *lpulFlags)
{
	object_ptr<IMAPIAdviseSink> lpAdviseSink;
	ulock_normal l_adv(m_advises_mutex);

	auto iter = find_if(m_advises.cbegin(), m_advises.cend(),
		[=](const M4LSUPPORTADVISES::value_type &entry) {
			return entry.second.lpKey->cb == lpKey->cb &&
			       memcmp(entry.second.lpKey->ab, lpKey->ab, lpKey->cb) == 0;
		});
	if (iter == m_advises.cend())
		/* Should this be reported as error? */
		return hrSuccess;
	lpAdviseSink.reset(iter->second.lpAdviseSink);
	l_adv.unlock();
	return lpAdviseSink->OnNotify(cNotification, lpNotifications);
}

HRESULT M4LMAPISupport::ModifyStatusRow(ULONG cValues,
    const SPropValue *lpColumnVals, ULONG ulFlags)
{
	return static_cast<M4LMAPISession *>(this->session)->setStatusRow(cValues, lpColumnVals);
}

HRESULT M4LMAPISupport::OpenProfileSection(const MAPIUID *lpUid, ULONG ulFlags,
    IProfSect **lppProfileObj)
{
	if (lpUid == NULL)
		lpUid = lpsProviderUID.get();
	return session->OpenProfileSection(lpUid, nullptr, ulFlags, lppProfileObj);
}

HRESULT M4LMAPISupport::RegisterPreprocessor(const MAPIUID *,
    const TCHAR *addrtype, const TCHAR *dllname, const char *preprocess,
    const char *remove_pp_info, ULONG flags)
{
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::NewUID(LPMAPIUID lpMuid) {
	return CoCreateGuid(reinterpret_cast<GUID *>(lpMuid));
}

HRESULT M4LMAPISupport::MakeInvalid(ULONG ulFlags, LPVOID lpObject, ULONG ulRefCount, ULONG cMethods) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::SpoolerYield(ULONG ulFlags) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::SpoolerNotify(ULONG ulFlags, LPVOID lpvData) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::CreateOneOff(const TCHAR *lpszName,
    const TCHAR *lpszAdrType, const TCHAR *lpszAddress, ULONG ulFlags,
    ULONG *lpcbEntryID, ENTRYID **lppEntryID)
{
	// although it's called EC... the return value is HRESULT :)
	return ECCreateOneOff(lpszName, lpszAdrType, lpszAddress, ulFlags,
	       lpcbEntryID, lppEntryID);
}

HRESULT M4LMAPISupport::SetProviderUID(const MAPIUID *, ULONG flags)
{
    return hrSuccess;
}

HRESULT M4LMAPISupport::CompareEntryIDs(ULONG cbEntry1, const ENTRYID *lpEntry1,
    ULONG cbEntry2, const ENTRYID *lpEntry2, ULONG ulCompareFlags,
    ULONG *lpulResult)
{
	if (cbEntry1 != cbEntry2)
		*lpulResult = FALSE;
	else if (!lpEntry1 || !lpEntry2)
		return MAPI_E_INVALID_ENTRYID;
	else if (memcmp(lpEntry1, lpEntry2, cbEntry1) != 0)
		*lpulResult = FALSE;
	else
		*lpulResult = TRUE;
	return hrSuccess;
}

HRESULT M4LMAPISupport::OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid,
    ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew,
    IMAPIProp *sibling)
{
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulOpenFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	return session->OpenEntry(cbEntryID, lpEntryID, lpInterface,
	       ulOpenFlags, lpulObjType, lppUnk);
}

HRESULT M4LMAPISupport::GetOneOffTable(ULONG ulFlags, LPMAPITABLE * lppTable) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::Address(ULONG * lpulUIParam, LPADRPARM lpAdrParms, LPADRLIST * lppAdrList) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::Details(ULONG_PTR *ui_param, DISMISSMODELESS *dsfunc,
    void *dismiss_ctx, ULONG cbEntryID, const ENTRYID *lpEntryID,
    LPFNBUTTON callback, void *btn_ctx, const TCHAR *btn_text, ULONG flags)
{
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::NewEntry(ULONG_PTR ui_param, ULONG flags,
    ULONG eid_size, const ENTRYID *eid_cont, ULONG tpl_size, const ENTRYID *tpl,
    ULONG *new_size, ENTRYID **new_eid)
{
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::DoConfigPropsheet(ULONG_PTR ui_param, ULONG flags,
    const TCHAR *title, IMAPITable *disp_tbl, IMAPIProp *cfg_data,
    ULONG top_page)
{
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::CopyMessages(const IID *lpSrcInterface,
    void *lpSrcFolder, const ENTRYLIST *lpMsgList, const IID *lpDestInterface,
    void *lpDestFolder, ULONG_PTR ulUIParam, IMAPIProgress *lpProgress,
    ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	LPMAPIFOLDER lpSource = NULL;
	LPMAPIFOLDER lpDest = NULL;
	ULONG ulObjType;
	memory_ptr<ENTRYLIST> lpDeleteEntries;
	bool bPartial = false;
	ULONG i;

	if (lpSrcInterface == nullptr || lpSrcFolder == nullptr ||
	    lpDestFolder == nullptr || lpMsgList == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (*lpSrcInterface != IID_IMAPIFolder ||
	    (lpDestInterface != nullptr && *lpDestInterface != IID_IMAPIFolder))
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	lpSource = (LPMAPIFOLDER)lpSrcFolder;
	lpDest = (LPMAPIFOLDER)lpDestFolder;
	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~lpDeleteEntries);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(sizeof(SBinary)*lpMsgList->cValues, lpDeleteEntries, (void**)&lpDeleteEntries->lpbin);
	if (hr != hrSuccess)
		return hr;

	lpDeleteEntries->cValues = 0;

	for (i = 0; i < lpMsgList->cValues; ++i) {
		object_ptr<IMessage> lpSrcMessage, lpDestMessage;

		hr = lpSource->OpenEntry(lpMsgList->lpbin[i].cb,
		     reinterpret_cast<ENTRYID *>(lpMsgList->lpbin[i].lpb),
		     &IID_IMessage, 0, &ulObjType, &~lpSrcMessage);
		if (hr != hrSuccess) {
			// partial, or error to calling client?
			bPartial = true;
			continue;
		}
		hr = lpDest->CreateMessage(&IID_IMessage, MAPI_MODIFY, &~lpDestMessage);
		if (hr != hrSuccess) {
			bPartial = true;
			continue;
		}

		hr = this->DoCopyTo(&IID_IMessage, lpSrcMessage, 0, NULL, NULL, ulUIParam, lpProgress, &IID_IMessage, lpDestMessage, 0, NULL);
		if (FAILED(hr)) {
			return hr;
		} else if (hr != hrSuccess) {
			bPartial = true;
			continue;
		}

		hr = lpDestMessage->SaveChanges(0);
		if (hr != hrSuccess) {
			bPartial = true;
		} else if (ulFlags & MAPI_MOVE) {
			lpDeleteEntries->lpbin[lpDeleteEntries->cValues].cb = lpMsgList->lpbin[i].cb;
			lpDeleteEntries->lpbin[lpDeleteEntries->cValues].lpb = lpMsgList->lpbin[i].lpb;
			++lpDeleteEntries->cValues;
		}
	}

	if ((ulFlags & MAPI_MOVE) && lpDeleteEntries->cValues > 0 &&
	    lpSource->DeleteMessages(lpDeleteEntries, 0, NULL, 0) != hrSuccess)
		bPartial = true;
	if (bPartial)
		hr = MAPI_W_PARTIAL_COMPLETION;
	return hr;
}

HRESULT M4LMAPISupport::CopyFolder(const IID *lpSrcInterface, void *lpSrcFolder,
    ULONG cbEntryID, const ENTRYID *lpEntryID, const IID *lpDestInterface,
    void *lpDestFolder, const TCHAR *lpszNewFolderName, ULONG_PTR ulUIParam,
    IMAPIProgress *lpProgress, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpSource, *lpDest;
	object_ptr<IMAPIFolder> lpFolder, lpSubFolder;
	memory_ptr<SPropValue> lpSourceName;
	ULONG ulObjType  = 0;
	ULONG ulFolderFlags = 0;
	static constexpr const SizedSPropTagArray (1, sExcludeProps) = {1, {PR_DISPLAY_NAME_A}};

	if (lpSrcInterface == nullptr || lpSrcFolder == nullptr ||
	    cbEntryID == 0 || lpEntryID == nullptr || lpDestFolder == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (*lpSrcInterface != IID_IMAPIFolder)
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	/*
	 * lpDestInterface == NULL or IID_IMAPIFolder compatible.
	 * [Since IMAPIFolder has no known I* descendants, there is
	 * just this one class to handle.]
	 */
	if (lpDestInterface != nullptr && *lpDestInterface != IID_IMAPIFolder)
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	lpSource = static_cast<IMAPIFolder *>(lpSrcFolder);
	lpDest = static_cast<IMAPIFolder *>(lpDestFolder);
	hr = lpSource->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, 0, &ulObjType, &~lpFolder);
	if (hr != hrSuccess)
		return hr;
	if (!lpszNewFolderName) {
		hr = HrGetOneProp(lpFolder, PR_DISPLAY_NAME_W, &~lpSourceName);
		if (hr != hrSuccess)
			return hr;
		ulFolderFlags |= MAPI_UNICODE;
		lpszNewFolderName = (LPTSTR)lpSourceName->Value.lpszW;
	} else
		ulFolderFlags |= (ulFlags & MAPI_UNICODE);

	hr = lpDest->CreateFolder(FOLDER_GENERIC, lpszNewFolderName, nullptr, &IID_IMAPIFolder, ulFolderFlags, &~lpSubFolder);
	if (hr != hrSuccess)
		return hr;
	hr = this->DoCopyTo(&IID_IMAPIFolder, lpFolder, 0, NULL, sExcludeProps,
	     ulUIParam, lpProgress, &IID_IMAPIFolder, lpSubFolder, ulFlags, NULL);
	if (hr != hrSuccess)
		return hr;
	if (ulFlags & MAPI_MOVE)
		lpSource->DeleteFolder(cbEntryID, (LPENTRYID)lpEntryID, 0, NULL, DEL_FOLDERS | DEL_MESSAGES);
	return hrSuccess;
}

HRESULT M4LMAPISupport::DoCopyTo(LPCIID lpSrcInterface, LPVOID lpSrcObj,
    ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpDestInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyTo(lpSrcInterface, lpSrcObj, ciidExclude,
	       rgiidExclude, lpExcludeProps, ulUIParam, lpProgress,
	       lpDestInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT M4LMAPISupport::DoCopyProps(LPCIID lpSrcInterface, void *lpSrcObj,
    const SPropTagArray *lpIncludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpDestInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyProps(lpSrcInterface, lpSrcObj, lpIncludeProps,
	       ulUIParam, lpProgress, lpDestInterface, lpDestObj, ulFlags,
	       lppProblems);
}

HRESULT M4LMAPISupport::DoProgressDialog(ULONG ulUIParam, ULONG ulFlags, LPMAPIPROGRESS * lppProgress) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::ReadReceipt(ULONG ulFlags, LPMESSAGE lpReadMessage, LPMESSAGE * lppEmptyMessage) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::PrepareSubmit(LPMESSAGE lpMessage, ULONG * lpulFlags) {
	// for every recipient,
	// if mapi_submitted flag, clear flag, responsibility == false
	// else set recipient type P1, responsibility == true
    return MAPI_E_NO_SUPPORT;
}

/** 
 * Should perform the following tasks:
 *
 * 1 Expand certain personal distribution lists to their component recipients.
 * 2 Replace all display names that have been changed with the original names.
 * 3 Mark any duplicate entries.
 * 4 Resolve all one-off addresses. 
 * 5 Check whether the message needs preprocessing and, if it does,
 *   set the flag pointed to by lpulFlags to NEEDS_PREPROCESSING.
 * 
 * Currently we only do step 1. The rest is done by the spooler and inetmapi.
 *
 * @param[in] lpMessage The message to process
 * @param[out] lpulFlags Return flags for actions required by the caller.
 * 
 * @return MAPI Error code
 */
HRESULT M4LMAPISupport::ExpandRecips(LPMESSAGE lpMessage, ULONG * lpulFlags) {
	HRESULT hr = hrSuccess;
	MAPITablePtr ptrRecipientTable;
	SRowSetPtr ptrRow;
	AddrBookPtr ptrAddrBook;
	std::set<std::string> setFilter;
	SPropTagArrayPtr ptrColumns;

	hr = session->OpenAddressBook(0, NULL, AB_NO_DIALOG, &~ptrAddrBook);
	if (hr != hrSuccess)
		return hr;
	hr = lpMessage->GetRecipientTable(fMapiUnicode | MAPI_DEFERRED_ERRORS, &~ptrRecipientTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRecipientTable->QueryColumns(TBL_ALL_COLUMNS, &~ptrColumns);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRecipientTable->SetColumns(ptrColumns, 0);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		const SPropValue *lpAddrType = NULL;
		const SPropValue *lpDLEntryID = NULL;
		ULONG ulObjType;
		object_ptr<IDistList> ptrDistList;
		MAPITablePtr ptrMemberTable;
		SRowSetPtr ptrMembers;

		hr = ptrRecipientTable->QueryRows(1, 0L, &~ptrRow);
		if (hr != hrSuccess)
			return hr;
		if (ptrRow.size() == 0)
			break;
		lpAddrType = ptrRow[0].cfind(PR_ADDRTYPE);
		if (!lpAddrType)
			continue;
		if (_tcscmp(lpAddrType->Value.LPSZ, KC_T("MAPIPDL")))
			continue;
		lpDLEntryID = ptrRow[0].cfind(PR_ENTRYID);
		if (!lpDLEntryID)
			continue;

		if (setFilter.find(std::string(lpDLEntryID->Value.bin.lpb, lpDLEntryID->Value.bin.lpb + lpDLEntryID->Value.bin.cb)) != setFilter.end()) {
			// already expanded this group so continue without opening
			hr = lpMessage->ModifyRecipients(MODRECIP_REMOVE, (LPADRLIST)ptrRow.get());
			if (hr != hrSuccess)
				return hr;
			continue;
		}
		setFilter.emplace(lpDLEntryID->Value.bin.lpb, lpDLEntryID->Value.bin.lpb + lpDLEntryID->Value.bin.cb);
		hr = ptrAddrBook->OpenEntry(lpDLEntryID->Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(lpDLEntryID->Value.bin.lpb),
		     &iid_of(ptrDistList), 0, &ulObjType, &~ptrDistList);
		if (hr != hrSuccess)
			continue;

		// remove MAPIPDL entry when distlist is opened
		hr = lpMessage->ModifyRecipients(MODRECIP_REMOVE, (LPADRLIST)ptrRow.get());
		if (hr != hrSuccess)
			return hr;
		hr = ptrDistList->GetContentsTable(fMapiUnicode, &~ptrMemberTable);
		if (hr != hrSuccess)
			continue;

		// Same columns as recipient table
		hr = ptrMemberTable->SetColumns(ptrColumns, 0);
		if (hr != hrSuccess)
			return hr;

		// Get all recipients in distlist, and add to message.
		// If another distlist is here, it will expand in the next loop.
		hr = ptrMemberTable->QueryRows(-1, fMapiUnicode, &~ptrMembers);
		if (hr != hrSuccess)
			continue;

		// find all unknown properties in the rows, reference-copy those from the original recipient
		// ModifyRecipients() will actually copy the data
		for (ULONG c = 0; c < ptrMembers.size(); ++c) {
			for (ULONG i = 0; i < ptrMembers[c].cValues; ++i) {
				if (PROP_TYPE(ptrMembers[c].lpProps[i].ulPropTag) != PT_ERROR)
					continue;

				// prop is unknown, find prop in recip, and copy value
				auto lpRecipProp = ptrRow[0].cfind(CHANGE_PROP_TYPE(ptrMembers[c].lpProps[i].ulPropTag, PT_UNSPECIFIED));
				if (lpRecipProp)
					ptrMembers[c].lpProps[i] = *lpRecipProp;
				// else: leave property unknown
			}
		}

		hr = lpMessage->ModifyRecipients(MODRECIP_ADD, (LPADRLIST)ptrMembers.get());
		if (hr != hrSuccess)
			return hr;
	}
	// Return 0 (no spooler needed, no preprocessing needed)
	if(lpulFlags)
		*lpulFlags = 0;
	return hrSuccess;
}

HRESULT M4LMAPISupport::UpdatePAB(ULONG ulFlags, LPMESSAGE lpMessage) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::DoSentMail(ULONG ulFlags, LPMESSAGE lpMessage) {
	return ::DoSentMail(session, nullptr, ulFlags,
	       object_ptr<IMessage>(lpMessage)); /* from CommonUtil */
}

HRESULT M4LMAPISupport::OpenAddressBook(LPCIID lpInterface, ULONG ulFlags, LPADRBOOK * lppAdrBook) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::Preprocess(ULONG flags, ULONG eid_size, const ENTRYID *)
{
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::CompleteMsg(ULONG flags, ULONG eid_size, const ENTRYID *)
{
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::StoreLogoffTransports(ULONG * lpulFlags) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::StatusRecips(IMessage *, const ADRLIST *recips)
{
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::WrapStoreEntryID(ULONG cbOrigEntry,
    const ENTRYID *lpOrigEntry, ULONG *lpcbWrappedEntry,
    ENTRYID **lppWrappedEntry)
{
	// get the dll name from SVCService
	const SPropValue *lpDLLName = NULL;

	if (service == nullptr)
		// addressbook provider doesn't have the SVCService object
		return MAPI_E_CALL_FAILED;
	lpDLLName = service->GetProp(PR_SERVICE_DLL_NAME_A);
	if (lpDLLName == nullptr || lpDLLName->Value.lpszA == nullptr)
		return MAPI_E_NOT_FOUND;
	// call mapiutil version
	return ::WrapStoreEntryID(0, reinterpret_cast<TCHAR *>(lpDLLName->Value.lpszA),
	       cbOrigEntry, lpOrigEntry, lpcbWrappedEntry, lppWrappedEntry);
}

HRESULT M4LMAPISupport::ModifyProfile(ULONG ulFlags) {
	return hrSuccess;
}

HRESULT M4LMAPISupport::IStorageFromStream(LPUNKNOWN lpUnkIn, LPCIID lpInterface, ULONG ulFlags, LPSTORAGE * lppStorageOut) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::GetSvcConfigSupportObj(ULONG ulFlags, LPMAPISUP * lppSvcSupport) {
    return MAPI_E_NO_SUPPORT;
}

HRESULT M4LMAPISupport::QueryInterface(REFIID refiid, void **lpvoid) {
	if (refiid == IID_IMAPISup) {
		AddRef();
		*lpvoid = static_cast<IMAPISupport *>(this);
	} else if (refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = static_cast<IUnknown *>(this);
	} else if (refiid == IID_IMAPIGetSession) {
		IMAPIGetSession *lpGetSession = new M4LMAPIGetSession(session);
		lpGetSession->AddRef();
		*lpvoid = lpGetSession;
    } else
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	return hrSuccess;
}
