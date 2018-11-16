/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include "ECFreeBusySupport.h"
#include "ECFreeBusyUpdate.h"
#include "ECFreeBusyData.h"
#include <kopano/CommonUtil.h>
#include <kopano/mapiext.h>
#include <kopano/ECLogger.h>
#include <mapiutil.h>
#include "freebusyutil.h"
#include <kopano/mapi_ptr.h>

namespace KC {

ECFreeBusySupport::ECFreeBusySupport(void)
{
	GetClientVersion(&m_ulOutlookVersion);
}

HRESULT ECFreeBusySupport::Create(ECFreeBusySupport **lppECFreeBusySupport)
{
	return alloc_wrap<ECFreeBusySupport>().put(lppECFreeBusySupport);
}

HRESULT ECFreeBusySupport::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECFreeBusySupport, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IFreeBusySupport, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECFreeBusySupport::Open(IMAPISession* lpMAPISession, IMsgStore* lpMsgStore, BOOL bStore)
{
	object_ptr<IMsgStore> lpPublicStore;

	if(lpMAPISession == NULL)
		return MAPI_E_INVALID_OBJECT;
#ifdef KNOB144
	if (lpMsgStore) {
		memory_ptr<SPropValue> lpPropArray;
		HrGetOneProp(lpMsgStore, PR_DISPLAY_NAME_A, &~lpPropArray);
		ec_log_debug("ECFreeBusySupport::Open Storename=%s", (lpPropArray && lpPropArray->ulPropTag == PR_DISPLAY_NAME_A) ? lpPropArray->Value.lpszA : "Error");
	}
#endif

	// Hold the mapisession, the session will be released by function 'close' or 
	// on delete the class
	auto hr = lpMAPISession->QueryInterface(IID_IMAPISession, &~m_lpSession);
	if(hr != hrSuccess)
		return hr;

	// Open the public store for communicate with the freebusy information.
	hr = HrOpenECPublicStoreOnline(lpMAPISession, &~lpPublicStore);
	if(hr != hrSuccess)
		return hr;
	hr = lpPublicStore->QueryInterface(IID_IMsgStore, &~m_lpPublicStore);
	if(hr != hrSuccess)
		return hr;
	if (lpMsgStore != nullptr)
		//Hold the use store for update freebusy
		hr = lpMsgStore->QueryInterface(IID_IMsgStore, &~m_lpUserStore);
	return hr;
}

HRESULT ECFreeBusySupport::Close()
{
	m_lpSession.reset();
	m_lpPublicStore.reset();
	m_lpUserStore.reset();
	return S_OK;
}

HRESULT ECFreeBusySupport::LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead)
{
	ULONG			ulFindUsers = 0;
	ECFBBlockList	fbBlockList;
	LONG			rtmStart = 0;
	LONG			rtmEnd = 0;
	ULONG			i;

	if((cMax > 0 && rgfbuser == NULL) || prgfbdata == NULL)
		return MAPI_E_INVALID_PARAMETER;

	for (i = 0; i < cMax; ++i) {
		object_ptr<IMessage> lpMessage;
		if (GetFreeBusyMessage(m_lpSession, m_lpPublicStore, nullptr, rgfbuser[i].m_cbEid, rgfbuser[i].m_lpEid, false, &~lpMessage) != hrSuccess) {
			/* No free busy information, gives the empty class. */
			prgfbdata[i] = nullptr;
			continue;
		}
		fbBlockList.Clear();
		auto hr = GetFreeBusyMessageData(lpMessage, &rtmStart, &rtmEnd, &fbBlockList);
		if (FAILED(hr))
			return hr;
		// Add fbdata
		object_ptr<ECFreeBusyData> lpECFreeBusyData;
		ECFreeBusyData::Create(rtmStart, rtmEnd, fbBlockList, &~lpECFreeBusyData);
		hr = lpECFreeBusyData->QueryInterface(IID_IFreeBusyData, (void**)&prgfbdata[i]);
		if (hr != hrSuccess)
			return hr;
		++ulFindUsers;
	}

	if(pcRead)
		*pcRead = ulFindUsers;
	return S_OK;
}

HRESULT ECFreeBusySupport::LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4)
{
	HRESULT				hr = hrSuccess;
	ULONG				cFBUpdate = 0;

	if((cUsers > 0 && lpUsers == NULL) || lppFBUpdate == NULL)
		return MAPI_E_INVALID_PARAMETER;

	for (unsigned int i = 0; i < cUsers; ++i) {
		object_ptr<IMessage> lpMessage;

		// Get the FB message, is not exist create them
		hr = GetFreeBusyMessage(m_lpSession, m_lpPublicStore, m_lpUserStore, lpUsers[i].m_cbEid, lpUsers[i].m_lpEid, true, &~lpMessage);
		if (hr != hrSuccess)
		{
			lppFBUpdate[i] = NULL;//FIXME: what todo with this?
			continue;
		}

		object_ptr<ECFreeBusyUpdate> lpECFBUpdate;
		hr = ECFreeBusyUpdate::Create(lpMessage, &~lpECFBUpdate);
		if(hr != hrSuccess)
			return hr;
		hr = lpECFBUpdate->QueryInterface(IID_IFreeBusyUpdate, (void**)&lppFBUpdate[i]);
		if(hr != hrSuccess)
			return hr;
		++cFBUpdate;
	}

	if(lpcFBUpdate)
		*lpcFBUpdate = cFBUpdate;
	return hr;
}

} /* namespace */
