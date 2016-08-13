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
#include "ECFreeBusySupport.h"

#include "ECFreeBusyUpdate.h"
#include "ECFreeBusyData.h"
#include <kopano/CommonUtil.h>
#include <kopano/mapiext.h>
#include <mapiutil.h>

#include "freebusyutil.h"
#include <kopano/mapi_ptr.h>

ECFreeBusySupport::ECFreeBusySupport(void)
{
	m_lpSession = NULL;
	m_lpPublicStore = NULL;
	m_lpUserStore = NULL;
	m_lpFreeBusyFolder = NULL;
	GetClientVersion(&m_ulOutlookVersion);
}

ECFreeBusySupport::~ECFreeBusySupport(void)
{
	if(m_lpFreeBusyFolder)
		m_lpFreeBusyFolder->Release();

	if(m_lpUserStore)
		m_lpUserStore->Release();

	if(m_lpPublicStore)
		m_lpPublicStore->Release();

	if(m_lpSession)
		m_lpSession->Release();
}

HRESULT ECFreeBusySupport::Create(ECFreeBusySupport **lppECFreeBusySupport)
{
	HRESULT				hr = hrSuccess;
	ECFreeBusySupport*	lpECFreeBusySupport = NULL;

	lpECFreeBusySupport = new ECFreeBusySupport();

	hr = lpECFreeBusySupport->QueryInterface(IID_ECFreeBusySupport, (void **)lppECFreeBusySupport);

	if(hr != hrSuccess)
		delete lpECFreeBusySupport;

	return hr;
}

HRESULT ECFreeBusySupport::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECFreeBusySupport, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	if (m_ulOutlookVersion == CLIENT_VERSION_OLK2000) {
		REGISTER_INTERFACE(IID_IFreeBusySupport, &this->m_xFreeBusySupportOutlook2000);
		REGISTER_INTERFACE(IID_IUnknown, &this->m_xFreeBusySupportOutlook2000);
	} else {
		REGISTER_INTERFACE(IID_IFreeBusySupport, &this->m_xFreeBusySupport);
		REGISTER_INTERFACE(IID_IUnknown, &this->m_xFreeBusySupport);
	}

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECFreeBusySupport::Open(IMAPISession* lpMAPISession, IMsgStore* lpMsgStore, BOOL bStore)
{
	HRESULT hr = hrSuccess;
	IMsgStore* lpPublicStore = NULL;

	if(lpMAPISession == NULL)
	{
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}

#ifdef DEBUG
	{
	LPSPropValue lpPropArray = NULL;

	if (lpMsgStore) {
		HrGetOneProp(lpMsgStore, PR_DISPLAY_NAME_A, &lpPropArray);
		TRACE_MAPI(TRACE_ENTRY, "ECFreeBusySupport::Open", "Storename=%s", (lpPropArray && lpPropArray->ulPropTag == PR_DISPLAY_NAME_A) ? lpPropArray->Value.lpszA : "Error");
		MAPIFreeBuffer(lpPropArray);
	}
	}
#endif

	// Hold the mapisession, the session will be released by function 'close' or 
	// on delete the class
	hr = lpMAPISession->QueryInterface(IID_IMAPISession, (void**)&m_lpSession);
	if(hr != hrSuccess)
		goto exit;

	// Open the public store for communicate with the freebusy information.
	hr = HrOpenECPublicStoreOnline(lpMAPISession, &lpPublicStore);
	if(hr != hrSuccess)
		goto exit;

	hr = lpPublicStore->QueryInterface(IID_IMsgStore, (void**)&m_lpPublicStore);
	if(hr != hrSuccess)
		goto exit;

	if(lpMsgStore) {
		//Hold the use store for update freebusy
		hr = lpMsgStore->QueryInterface(IID_IMsgStore, (void**)&m_lpUserStore);
		if(hr != hrSuccess)
			goto exit;
	}
exit:
	if(lpPublicStore)
		lpPublicStore->Release();

	return hr;
}

HRESULT ECFreeBusySupport::Close()
{
	if(m_lpSession)
	{
		m_lpSession->Release();
		m_lpSession = NULL;
	}

	if(m_lpPublicStore)
	{
		m_lpPublicStore->Release();
		m_lpPublicStore = NULL;
	}

	if(m_lpUserStore)
	{
		m_lpUserStore->Release();
		m_lpUserStore = NULL;
	}

	return S_OK;
}

HRESULT ECFreeBusySupport::LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead)
{
	HRESULT			hr = S_OK;
	ECFreeBusyData*	lpECFreeBusyData = NULL;
	ULONG			ulFindUsers = 0;
	IMessage*		lpMessage = NULL;

	ECFBBlockList	fbBlockList;
	LONG			rtmStart = 0;
	LONG			rtmEnd = 0;
	ULONG			i;

	if((cMax > 0 && rgfbuser == NULL) || prgfbdata == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	for (i = 0; i < cMax; ++i) {
		if(GetFreeBusyMessage(m_lpSession, m_lpPublicStore, NULL, rgfbuser[i].m_cbEid, rgfbuser[i].m_lpEid, false, &lpMessage) == hrSuccess)
		{
			ECFreeBusyData::Create(&lpECFreeBusyData);

			fbBlockList.Clear();

			hr = GetFreeBusyMessageData(lpMessage, &rtmStart, &rtmEnd, &fbBlockList);
			if(hr != hrSuccess)
			{
				//FIXME: ?
			}

			// Add fbdata
			lpECFreeBusyData->Init(rtmStart, rtmEnd, &fbBlockList);
			hr = lpECFreeBusyData->QueryInterface(IID_IFreeBusyData, (void**)&prgfbdata[i]);
			if(hr != hrSuccess)
				goto exit;
			
			++ulFindUsers;
			lpECFreeBusyData->Release();
			lpECFreeBusyData = NULL;
			
			lpMessage->Release();
			lpMessage = NULL;
		}// else No free busy information, gives the empty class
		else
			prgfbdata[i] = NULL;

		//if(phrStatus)
		//	phrStatus[i] = hr;
	}

	if(pcRead)
		*pcRead = ulFindUsers;
exit:
	if(lpECFreeBusyData)
		lpECFreeBusyData->Release();
	
	if(lpMessage)
		lpMessage->Release();

	return S_OK;
}

HRESULT ECFreeBusySupport::LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4)
{
	TRACE_MAPI(TRACE_ENTRY, "ECFreeBusySupport::LoadFreeBusyUpdate", "cUsers=%d", cUsers);

	HRESULT				hr = hrSuccess;
	ECFreeBusyUpdate*	lpECFBUpdate = NULL;
	ULONG				cFBUpdate = 0;
	IMessage*			lpMessage = NULL;

	if((cUsers > 0 && lpUsers == NULL) || lppFBUpdate == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	for (unsigned int i = 0; i < cUsers; ++i) {
		lpMessage = NULL;

		// Get the FB message, is not exist create them
		hr = GetFreeBusyMessage(m_lpSession, m_lpPublicStore, m_lpUserStore, lpUsers[i].m_cbEid, lpUsers[i].m_lpEid, true, &lpMessage);
		if (hr != hrSuccess)
		{
			lppFBUpdate[i] = NULL;//FIXME: what todo with this?
			continue;
		}

		hr = ECFreeBusyUpdate::Create(lpMessage, &lpECFBUpdate);
		if(hr != hrSuccess)
			goto exit;

		hr = lpECFBUpdate->QueryInterface(IID_IFreeBusyUpdate, (void**)&lppFBUpdate[i]);
		if(hr != hrSuccess)
			goto exit;
		
		lpECFBUpdate->Release();
		lpECFBUpdate = NULL;
		
		lpMessage->Release();
		lpMessage = NULL;
		++cFBUpdate;
	}

	if(lpcFBUpdate)
		*lpcFBUpdate = cFBUpdate;

exit:
	if(lpECFBUpdate)
		lpECFBUpdate->Release();

	if(lpMessage)
		lpMessage->Release();

	return hr;
}

HRESULT ECFreeBusySupport::GetDelegateInfoEx(FBUser sFBUser, unsigned int *lpulStatus, unsigned int *lpulStart, unsigned int *lpulEnd)
{
	HRESULT hr = hrSuccess;
	IFreeBusyData *lpFBData = NULL;
	HRESULT ulStatus = 0;
	ULONG ulRead = 0;

	struct StatusOL2K3 {
		ULONG ulResourceType;		/* 0x68410003 PR_SCHDINFO_RESOURCE_TYPE always 0*/
		ULONG ulReserved1;			/* always 1? olk 2007 = 0 */

		char **lppszFullname;		/* PR_MAILBOX_OWNER_NAME, but allocation method is unknown */
		SBinary *lpUserEntryID;		/* PR_MAILBOX_OWNER_ENTRYID, but allocation method is unknown  */
		ULONG *lpUnknown4;			/* pointer to NULL, -- not present in OL2K */
		ULONG ulBossWantsCopy;		/* 0x6842000B PR_SCHDINFO_BOSS_WANTS_COPY always 1 */

		ULONG ulBossWantsInfo;		/* 0x684B000B PR_SCHDINFO_BOSS_WANTS_INFO always 1 */
		ULONG ulDontEmailDelegates;	/* 0x6843000B PR_SCHDINFO_DONT_MAIL_DELEGATES always 1 */

		ULONG fDoesAutoAccept;
		ULONG fDoesRejectRecurring;
		ULONG fDoesRejectConflict;

		ULONG ulReserved11;			/* always 0 -- unknown -- not present in OL2K */
	} *lpStatusOlk2k3;

	struct StatusOL2K7 {
		ULONG ulResourceType;		/* 0x68410003 PR_SCHDINFO_RESOURCE_TYPE always 0*/
		ULONG ulReserved1;			/* always 1? olk 2007 = 0 */

		char **lppszFullname;		/* PR_MAILBOX_OWNER_NAME, but allocation method is unknown */
		SBinary *lpUserEntryID;		/* PR_MAILBOX_OWNER_ENTRYID, but allocation method is unknown  */
		ULONG *lpUnknown4;			/* pointer to NULL, -- not present in OL2K */
		ULONG ulBossWantsCopy;		/* 0x6842000B PR_SCHDINFO_BOSS_WANTS_COPY always 1 */

		ULONG ulBossWantsInfo;		/* 0x684B000B PR_SCHDINFO_BOSS_WANTS_INFO always 1 */
		ULONG ulDontEmailDelegates;	/* 0x6843000B PR_SCHDINFO_DONT_MAIL_DELEGATES always 1 */

		ULONG ulReserved11;			/* always 0 -- unknown -- not present in OL2K */
		ULONG fDoesAutoAccept;
		ULONG fDoesRejectRecurring;
		ULONG fDoesRejectConflict;

	} *lpStatus;

	struct StatusOL2K {
		ULONG ulResourceType;		/* always 0 */
		ULONG ulReserved1;			/* always 1 */

		char **lppszFullname;		/* PR_MAILBOX_OWNER_NAME, but allocation method is unknown */
		SBinary *lpUserEntryID;		/* PR_MAILBOX_OWNER_ENTRYID, but allocation method is unknown  */
		ULONG ulBossWantsCopy;		/* always 1 */

		ULONG ulBossWantsInfo;		/* always 1 */
		ULONG ulDontEmailDelegates;	/* always 1 */

		ULONG fDoesAutoAccept;
		ULONG fDoesRejectRecurring;
		ULONG fDoesRejectConflict;
	} *lpStatusOlk2K;

	bool bAutoAccept = true, bDeclineConflict = true, bDeclineRecurring = true;
	ULONG ulObjType = 0;
	MailUserPtr ptrUser;
	MsgStorePtr ptrStore;
	SPropValuePtr ptrName;

	if (m_lpSession->OpenEntry(sFBUser.m_cbEid, sFBUser.m_lpEid, NULL, 0, &ulObjType, &ptrUser) == hrSuccess && 
		HrGetOneProp(ptrUser, PR_ACCOUNT, &ptrName) == hrSuccess &&
		HrOpenUserMsgStore(m_lpSession, ptrName->Value.LPSZ, &ptrStore) == hrSuccess)
	{
		GetAutoAcceptSettings(ptrStore, &bAutoAccept, &bDeclineConflict, &bDeclineRecurring);
		// ignore error, default true.
		hr = hrSuccess;
	}

	switch (m_ulOutlookVersion) {
	case CLIENT_VERSION_OLK2000:
	case CLIENT_VERSION_OLK2002:
		lpStatusOlk2K = (StatusOL2K*)lpulStatus;
		memset(lpStatusOlk2K, 0, sizeof(StatusOL2K));

		lpStatusOlk2K->ulReserved1 = 1;
		lpStatusOlk2K->ulBossWantsCopy = 0; // WARNING Outlook will crash if it will be enabled (1)!
		lpStatusOlk2K->ulBossWantsInfo = 1;
		lpStatusOlk2K->ulDontEmailDelegates = 1;

		// They don't seem to have much effect, as outlook will always plan the resource.
		lpStatusOlk2K->fDoesAutoAccept = bAutoAccept;
		lpStatusOlk2K->fDoesRejectConflict = bDeclineConflict;
		lpStatusOlk2K->fDoesRejectRecurring = bDeclineRecurring;

		break;
	case CLIENT_VERSION_OLK2003:
		lpStatusOlk2k3 = (StatusOL2K3*)lpulStatus;
		memset(lpStatusOlk2k3, 0, sizeof(StatusOL2K3));

		lpStatusOlk2k3->ulReserved1 = 0;
		lpStatusOlk2k3->ulBossWantsCopy = 0; // WARNING Outlook will crash if it will be enabled (1)!
		lpStatusOlk2k3->ulBossWantsInfo = 1;
		lpStatusOlk2k3->ulDontEmailDelegates = 1;

		lpStatusOlk2k3->fDoesAutoAccept = bAutoAccept;
		lpStatusOlk2k3->fDoesRejectConflict = bDeclineConflict;
		lpStatusOlk2k3->fDoesRejectRecurring = bDeclineRecurring;

		break;
	default:
		lpStatus = (StatusOL2K7*)lpulStatus;
		memset(lpStatus, 0, sizeof(StatusOL2K7));

		lpStatus->ulReserved1 = 0;
		lpStatus->ulBossWantsCopy = 0; // WARNING Outlook will crash if it will be enabled (1)!
		lpStatus->ulBossWantsInfo = 1;
		lpStatus->ulDontEmailDelegates = 1;

		// Atleast Outlook 2007 should be able to correctly use these, if you restart outlook.
		lpStatus->fDoesAutoAccept = bAutoAccept;
		lpStatus->fDoesRejectConflict = bDeclineConflict;
		lpStatus->fDoesRejectRecurring = bDeclineRecurring;
		break;
	};

	// These two dates seem to be the published range in RTimes for the specified user. However, just specifying zero
	// doesn't seem to matter when booking resources, so it looks like these values are ignored.

	// We'll get the values anyway just to be sure.

	hr = LoadFreeBusyData(1, &sFBUser, &lpFBData, &ulStatus, &ulRead);
	if(hr != hrSuccess)
		goto exit;

	if(ulRead != 1) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	hr = lpFBData->GetFBPublishRange((LONG *)lpulStart, (LONG *)lpulEnd);

exit:
	if(lpFBData)
		lpFBData->Release();

	// if an error is returned, outlook will send an email to the resource.
	// PR_LAST_VERB_EXECUTED (ulong) will be set to 516, so outlook knows modifications need to be mailed too.
	return hr;
}

// Interfaces
//		IUnknown
//		IFreeBusySupport
// IUnknown
HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::QueryInterface(REFIID refiid, void** lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::QueryInterface", "");
	METHOD_PROLOGUE_(ECFreeBusySupport, FreeBusySupport);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG __stdcall ECFreeBusySupport::xFreeBusySupport::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::AddRef", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	return pThis->AddRef();
}

ULONG __stdcall ECFreeBusySupport::xFreeBusySupport::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::Release", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	return pThis->Release();
}

// IFreeBusySupport
HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::Open(IMAPISession* lpMAPISession, IMsgStore* lpMsgStore, BOOL bStore)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::Open", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->Open(lpMAPISession, lpMsgStore, bStore);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::Open", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::Close()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::Close", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->Close();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::Close", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::LoadFreeBusyData", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->LoadFreeBusyData(cMax, rgfbuser, prgfbdata, phrStatus, pcRead);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::LoadFreeBusyData", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::LoadFreeBusyUpdate", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->LoadFreeBusyUpdate(cUsers, lpUsers, lppFBUpdate, lpcFBUpdate, lpData4);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::LoadFreeBusyUpdate", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::CommitChanges()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::CommitChanges", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->CommitChanges();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::CommitChanges", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::GetDelegateInfo(FBUser fbUser, void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::GetDelegateInfo", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->GetDelegateInfo(fbUser, lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::GetDelegateInfo", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::SetDelegateInfo(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::SetDelegateInfo", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->SetDelegateInfo(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::SetDelegateInfo", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::AdviseFreeBusy(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::AdviseFreeBusy", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->AdviseFreeBusy(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::AdviseFreeBusy", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::Reload(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::Reload", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->Reload(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::Reload", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::GetFBDetailSupport(void **lppData, BOOL bData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::GetFBDetailSupport", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->GetFBDetailSupport(lppData, bData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::GetFBDetailSupport", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::HrHandleServerSched(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::HrHandleServerSched", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->HrHandleServerSched(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::HrHandleServerSched", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::HrHandleServerSchedAccess()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::HrHandleServerSchedAccess", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->HrHandleServerSchedAccess();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::HrHandleServerSchedAccess", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

BOOL __stdcall ECFreeBusySupport::xFreeBusySupport::FShowServerSched(BOOL bData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::FShowServerSched", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	BOOL b = pThis->FShowServerSched(bData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::FShowServerSched", "%s", (b == TRUE)?"TRUE":"FALSE");
	return b;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::HrDeleteServerSched()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::HrDeleteServerSched", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->HrDeleteServerSched();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::HrDeleteServerSched", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::GetFReadOnly(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::GetFReadOnly", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->GetFReadOnly(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::GetFReadOnly", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::SetLocalFB(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::SetLocalFB", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->SetLocalFB(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::SetLocalFB", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::PrepareForSync()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::PrepareForSync", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->PrepareForSync();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::PrepareForSync", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::GetFBPublishMonthRange(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::GetFBPublishMonthRange", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->GetFBPublishMonthRange(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::GetFBPublishMonthRange", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::PublishRangeChanged()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::PublishRangeChanged", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->PublishRangeChanged();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::PublishRangeChanged", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::CleanTombstone()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::CleanTombstone", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->CleanTombstone();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::CleanTombstone", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::GetDelegateInfoEx(FBUser fbUser, unsigned int *lpData1, unsigned int *lpData2, unsigned int *lpData3)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::GetDelegateInfoEx", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->GetDelegateInfoEx(fbUser, lpData1, lpData2, lpData3);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::GetDelegateInfoEx", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::PushDelegateInfoToWorkspace()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::PushDelegateInfoToWorkspace", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->PushDelegateInfoToWorkspace();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::PushDelegateInfoToWorkspace", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::Placeholder21(void *lpData, HWND hwnd, BOOL bData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::Placeholder21", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->Placeholder21(lpData, hwnd, bData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::Placeholder21", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupport::Placeholder22()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupport::Placeholder22", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupport);
	HRESULT hr = pThis->Placeholder22();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupport::Placeholder22", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

// Interfaces
//		IUnknown
//		IFreeBusySupportOutlook2000
// IUnknown
HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::QueryInterface(REFIID refiid, void** lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::QueryInterface", "");
	METHOD_PROLOGUE_(ECFreeBusySupport, FreeBusySupportOutlook2000);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::AddRef", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	return pThis->AddRef();
}

ULONG __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::Release", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	return pThis->Release();
}

// IFreeBusySupport
HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::Open(IMAPISession* lpMAPISession, IMsgStore* lpMsgStore, BOOL bStore)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::Open", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->Open(lpMAPISession, lpMsgStore, bStore);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::Open", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::Close()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::Close", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->Close();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::Close", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::LoadFreeBusyData", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->LoadFreeBusyData(cMax, rgfbuser, prgfbdata, phrStatus, pcRead);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::LoadFreeBusyData", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::LoadFreeBusyUpdate", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->LoadFreeBusyUpdate(cUsers, lpUsers, lppFBUpdate, lpcFBUpdate, lpData4);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::LoadFreeBusyUpdate", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::CommitChanges()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::CommitChanges", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->CommitChanges();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::CommitChanges", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::GetDelegateInfo(FBUser fbUser, void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::GetDelegateInfo", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->GetDelegateInfo(fbUser, lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::GetDelegateInfo", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::SetDelegateInfo(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::SetDelegateInfo", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->SetDelegateInfo(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::SetDelegateInfo", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::AdviseFreeBusy(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::AdviseFreeBusy", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->AdviseFreeBusy(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::AdviseFreeBusy", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::Reload(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::Reload", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->Reload(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::Reload", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::GetFBDetailSupport(void **lppData, BOOL bData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::GetFBDetailSupport", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->GetFBDetailSupport(lppData, bData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::GetFBDetailSupport", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::HrHandleServerSched(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::HrHandleServerSched", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->HrHandleServerSched(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::HrHandleServerSched", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::HrHandleServerSchedAccess()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::HrHandleServerSchedAccess", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->HrHandleServerSchedAccess();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::HrHandleServerSchedAccess", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

BOOL __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::FShowServerSched(BOOL bData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::FShowServerSched", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	BOOL b = pThis->FShowServerSched(bData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::FShowServerSched", "%s", (b == TRUE)?"TRUE":"FALSE");
	return b;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::HrDeleteServerSched()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::HrDeleteServerSched", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->HrDeleteServerSched();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::HrDeleteServerSched", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::GetFReadOnly(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::GetFReadOnly", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->GetFReadOnly(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::GetFReadOnly", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::SetLocalFB(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::SetLocalFB", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->SetLocalFB(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::SetLocalFB", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::PrepareForSync()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::PrepareForSync", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->PrepareForSync();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::PrepareForSync", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::GetFBPublishMonthRange(void *lpData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::GetFBPublishMonthRange", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->GetFBPublishMonthRange(lpData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::GetFBPublishMonthRange", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::PublishRangeChanged()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::PublishRangeChanged", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->PublishRangeChanged();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::PublishRangeChanged", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}
/*
HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::CleanTombstone()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::CleanTombstone", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->CleanTombstone();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::CleanTombstone", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}
*/
/*
HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::PlaceholderRemoved1()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::PlaceholderRemoved1", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = E_NOTIMPL; // skip call
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::PlaceholderRemoved1", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}
*/
HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::GetDelegateInfoEx(FBUser fbUser, unsigned int *lpData1, unsigned int *lpData2, unsigned int *lpData3)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::GetDelegateInfoEx", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->GetDelegateInfoEx(fbUser, lpData1, lpData2, lpData3);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::GetDelegateInfoEx", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::PushDelegateInfoToWorkspace()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::PushDelegateInfoToWorkspace", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->PushDelegateInfoToWorkspace();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::PushDelegateInfoToWorkspace", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::Placeholder21(void *lpData, HWND hwnd, BOOL bData)
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::Placeholder21", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->Placeholder21(lpData, hwnd, bData);
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::Placeholder21", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECFreeBusySupport::xFreeBusySupportOutlook2000::Placeholder22()
{
	TRACE_MAPI(TRACE_ENTRY, "IFreeBusySupportOutlook2000::Placeholder22", "");
	METHOD_PROLOGUE_(ECFreeBusySupport , FreeBusySupportOutlook2000);
	HRESULT hr = pThis->Placeholder22();
	TRACE_MAPI(TRACE_RETURN, "IFreeBusySupportOutlook2000::Placeholder22", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}
