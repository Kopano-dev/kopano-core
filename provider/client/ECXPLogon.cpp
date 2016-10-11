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
 */
#include <kopano/platform.h>
#include <chrono>
#include <condition_variable>
#include <kopano/lockhelper.hpp>
#include <mapi.h>
#include <mapispi.h>
#include <mapiutil.h>
#include <kopano/mapiguidext.h>
#include <kopano/ECGuid.h>
#include <kopano/ECInterfaceDefs.h>
#include <kopano/mapiext.h>
#include <edkmdb.h>
#include <edkguid.h>

#include "kcore.hpp"
#include "ECXPLogon.h"
#include "ECXPProvider.h"
#include "WSTransport.h"
#include "Mem.h"
#include "ClientUtil.h"
#include <kopano/CommonUtil.h>
#include "ECMsgStore.h"
#include "ECMessage.h"

#include <kopano/kcodes.h>

#include <kopano/ECDebug.h>
#include <kopano/ECRestriction.h>
#include <kopano/mapi_ptr.h>

static HRESULT HrGetECMsgStore(IMAPIProp *lpProp, ECMsgStore **lppECMsgStore)
{
	HRESULT hr;
	LPSPropValue lpPropVal = NULL;
	ECMAPIProp *lpECMAPIProp = NULL;

	hr = HrGetOneProp(lpProp, PR_EC_OBJECT, &lpPropVal);
	if(hr != hrSuccess)
		return hr;

	lpECMAPIProp = (ECMAPIProp *)lpPropVal->Value.lpszA;

	*lppECMsgStore = lpECMAPIProp->GetMsgStore();
	(*lppECMsgStore)->AddRef();
	return hrSuccess;
}

ECXPLogon::ECXPLogon(const std::string &strProfileName, BOOL bOffline, ECXPProvider *lpXPProvider, LPMAPISUP lpMAPISup) : ECUnknown("IXPLogon")
{
	m_lppszAdrTypeArray = NULL;
	m_ulTransportStatus = 0;
	m_lpMAPISup = lpMAPISup;
	m_lpXPProvider = lpXPProvider;
	m_lpMAPISup->AddRef();
	m_bCancel = false;
	m_bOffline = bOffline;
}

ECXPLogon::~ECXPLogon()
{
	if(m_lppszAdrTypeArray)
		ECFreeBuffer(m_lppszAdrTypeArray);

	if(m_lpMAPISup)
		m_lpMAPISup->Release();
}

HRESULT ECXPLogon::Create(const std::string &strProfileName, BOOL bOffline, ECXPProvider *lpXPProvider, LPMAPISUP lpMAPISup, ECXPLogon **lppECXPLogon)
{
	HRESULT hr = hrSuccess;

	ECXPLogon *lpXPLogon = new ECXPLogon(strProfileName, bOffline, lpXPProvider, lpMAPISup);

	hr = lpXPLogon->QueryInterface(IID_ECXPLogon, (void **)lppECXPLogon);

	if(hr != hrSuccess)
		delete lpXPLogon;

	return hr;
}

HRESULT ECXPLogon::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECXPLogon, this);

	REGISTER_INTERFACE(IID_IXPLogon, &this->m_xXPLogon);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECXPLogon::AddressTypes(ULONG * lpulFlags, ULONG * lpcAdrType, LPTSTR ** lpppszAdrTypeArray, ULONG * lpcMAPIUID, LPMAPIUID ** lpppUIDArray)
{
	HRESULT hr;

	if(m_lppszAdrTypeArray == NULL) {

		hr = ECAllocateBuffer(sizeof(TCHAR *) * 3, (LPVOID *)&m_lppszAdrTypeArray);
		if(hr != hrSuccess)
			return hr;

		hr = ECAllocateMore((_tcslen(TRANSPORT_ADDRESS_TYPE_SMTP)+1) * sizeof(TCHAR), m_lppszAdrTypeArray, (LPVOID *)&m_lppszAdrTypeArray[0]);
		if(hr != hrSuccess)
			return hr;

		_tcscpy(m_lppszAdrTypeArray[0], TRANSPORT_ADDRESS_TYPE_SMTP);

		hr = ECAllocateMore((_tcslen(TRANSPORT_ADDRESS_TYPE_ZARAFA)+1) * sizeof(TCHAR), m_lppszAdrTypeArray, (LPVOID *)&m_lppszAdrTypeArray[1]);
		if(hr != hrSuccess)
			return hr;

		_tcscpy(m_lppszAdrTypeArray[1], TRANSPORT_ADDRESS_TYPE_ZARAFA);

		hr = ECAllocateMore((_tcslen(TRANSPORT_ADDRESS_TYPE_FAX)+1) * sizeof(TCHAR), m_lppszAdrTypeArray, (LPVOID *)&m_lppszAdrTypeArray[2]);
		if(hr != hrSuccess)
			return hr;

		_tcscpy(m_lppszAdrTypeArray[2], TRANSPORT_ADDRESS_TYPE_FAX);
	}

	*lpulFlags = fMapiUnicode;
	*lpcMAPIUID = 0;
	*lpppUIDArray = NULL; // We could specify the Kopano addressbook's UID here to stop the MAPI spooler doing expansions on them (IE EntryID -> Email address)
	*lpcAdrType = 3;
	*lpppszAdrTypeArray = m_lppszAdrTypeArray;
	return hrSuccess;
}

HRESULT ECXPLogon::RegisterOptions(ULONG * lpulFlags, ULONG * lpcOptions, LPOPTIONDATA * lppOptions)
{
	*lpulFlags = 0;//fMapiUnicode ?
	*lpcOptions = 0;
	*lppOptions = NULL;
	return hrSuccess;
}

HRESULT ECXPLogon::TransportNotify(ULONG * lpulFlags, LPVOID * lppvData)
{
	if(*lpulFlags & NOTIFY_ABORT_DEFERRED)
		//FIXME: m_ulTransportStatus 
		// doe iets met lppvData
		// Remove item, out the spooler list (outgoing queue ???)
		/* nothing */;
	if (*lpulFlags & NOTIFY_BEGIN_INBOUND)
		m_ulTransportStatus |= STATUS_INBOUND_ENABLED;
	if (*lpulFlags & NOTIFY_BEGIN_INBOUND_FLUSH)
		m_ulTransportStatus |= STATUS_INBOUND_FLUSH;
	if (*lpulFlags & NOTIFY_BEGIN_OUTBOUND)
		m_ulTransportStatus |= STATUS_OUTBOUND_ENABLED;
	if (*lpulFlags & NOTIFY_BEGIN_OUTBOUND_FLUSH)
		m_ulTransportStatus |= STATUS_OUTBOUND_FLUSH;
	if (*lpulFlags & NOTIFY_CANCEL_MESSAGE) {
		scoped_lock lock(m_hExitMutex);
		m_bCancel = true;
		m_hExitSignal.notify_one();
	}
	if (*lpulFlags & NOTIFY_END_INBOUND)
		m_ulTransportStatus &= ~STATUS_INBOUND_ENABLED;
	if (*lpulFlags & NOTIFY_END_INBOUND_FLUSH)
		m_ulTransportStatus &= ~STATUS_INBOUND_FLUSH;
	if (*lpulFlags & NOTIFY_END_OUTBOUND)
		m_ulTransportStatus &= ~STATUS_OUTBOUND_ENABLED;
	if (*lpulFlags & NOTIFY_END_OUTBOUND_FLUSH)
		m_ulTransportStatus &= ~STATUS_OUTBOUND_FLUSH;
	return HrUpdateTransportStatus();
}

HRESULT ECXPLogon::Idle(ULONG ulFlags)
{
	// The MAPI spooler periodically calls the IXPLogon::Idle method during times when the system is idle
	// We do nothing ..
	return hrSuccess;
}

HRESULT ECXPLogon::TransportLogoff(ULONG ulFlags)
{
	return hrSuccess;
}

/**
 * Clear not deleted submit message
 *
 * The messages older than 10 days will be deleted. This function delete 
 * only the first 50 messages. Normally it should be zero!
 *
 * @param[in] lpFolder Folder with submitted messages
 *
 * @return MAPI error code
 */
HRESULT ECXPLogon::ClearOldSubmittedMessages(LPMAPIFOLDER lpFolder)
{
	HRESULT hr = hrSuccess;
	SizedSPropTagArray(1, sptDelete) = {1,{PR_ENTRYID} };
	MAPITablePtr	ptrContentsTable;
	ECAndRestriction resDelete;
	SRestrictionPtr ptrRestriction;
	LPENTRYLIST		lpDeleteItemEntryList = NULL;
	SPropValue		sPropDelAfterSubmit = {0};
	SPropValue		sPropxDaysBefore = {0};
	SRowSetPtr		ptrRows;
	time_t tNow = 0;

	hr = lpFolder->GetContentsTable(0, &ptrContentsTable);
	if (hr != hrSuccess)
		goto exit;
	hr = ptrContentsTable->SetColumns(sptDelete, MAPI_DEFERRED_ERRORS);
	if(hr != hrSuccess)
		goto exit;

	// build restriction where we search for messages which must deleted after the submit
	sPropDelAfterSubmit.ulPropTag = PR_DELETE_AFTER_SUBMIT;
	sPropDelAfterSubmit.Value.b = TRUE;

	sPropxDaysBefore.ulPropTag = PR_CREATION_TIME;
	time(&tNow);
	UnixTimeToFileTime(tNow - (10 * 24 * 60 * 60), &sPropxDaysBefore.Value.ft);

	resDelete =	ECAndRestriction(
					ECAndRestriction(
							ECExistRestriction(PR_DELETE_AFTER_SUBMIT) +
							ECPropertyRestriction(RELOP_EQ, PR_DELETE_AFTER_SUBMIT, &sPropDelAfterSubmit, ECRestriction::Cheap)
					) + 
					ECPropertyRestriction(RELOP_LE, PR_CREATION_TIME, &sPropxDaysBefore, ECRestriction::Cheap)
				);

	hr = resDelete.CreateMAPIRestriction(&ptrRestriction, ECRestriction::Cheap);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrContentsTable->Restrict(ptrRestriction, MAPI_DEFERRED_ERRORS);
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), (void**)&lpDeleteItemEntryList);
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateMore(50 * sizeof(SBinary), lpDeleteItemEntryList, (void**)&lpDeleteItemEntryList->lpbin);
	if (hr != hrSuccess)
		goto exit;

	lpDeleteItemEntryList->cValues = 0;

	// Get only the first 50 items
	hr = ptrContentsTable->QueryRows(50, 0, &ptrRows);
	if (hr != hrSuccess)
		goto exit;

	for (unsigned int i = 0; i < ptrRows.size(); ++i)
		if(ptrRows[i].lpProps[0].ulPropTag == PR_ENTRYID)
			lpDeleteItemEntryList->lpbin[lpDeleteItemEntryList->cValues++] = ptrRows[i].lpProps[0].Value.bin;

	if(lpDeleteItemEntryList->cValues > 0)
		hr = lpFolder->DeleteMessages(lpDeleteItemEntryList, 0, NULL, 0); //Delete message on the server

exit:
	MAPIFreeBuffer(lpDeleteItemEntryList);
	return hr;
}

HRESULT ECXPLogon::SubmitMessage(ULONG ulFlags, LPMESSAGE lpMessage, ULONG * lpulMsgRef, ULONG * lpulReturnParm)
{
	HRESULT hr = hrSuccess;
	LPMAPITABLE lpRecipTable = NULL;
	LPSRowSet lpRecipRows = NULL;
	
	ULONG ulRow = 0;
	ULONG ulRowCount = 0;

	LPSPropValue lpEntryID = NULL;
	LPSPropValue lpECObject = NULL;
	IMsgStore *lpOnlineStore = NULL;
	ECMsgStore *lpOnlineECMsgStore = NULL;
	ULONG ulObjType;
	ECMsgStore *lpECMsgStore = NULL;
	LPMAPIFOLDER lpSubmitFolder = NULL;
	LPMESSAGE lpSubmitMessage = NULL;
	SPropValue sDeleteAfterSubmitProp;
	ULONG ulOnlineAdviseConnection = 0;
	ENTRYLIST sDelete;
	IMsgStore *lpMsgStore = NULL;
	ULONG ulType = 0;

	SizedSPropTagArray(6, sptExcludeProps) = {6,{PR_SENTMAIL_ENTRYID, PR_SOURCE_KEY, PR_CHANGE_KEY, PR_PREDECESSOR_CHANGE_LIST, PR_ENTRYID, PR_SUBMIT_FLAGS}};

	// Un-cancel
	ulock_normal l_exit(m_hExitMutex);
	m_bCancel = false;
	l_exit.unlock();

	// Save some outgoing properties for the server
	hr = SetOutgoingProps(lpMessage);
	if (hr != erSuccess)
		goto exit;

	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != erSuccess)
		goto exit;

	// Get the recipient table from the message
	hr = lpMessage->GetRecipientTable(fMapiUnicode, &lpRecipTable);
	if (hr != hrSuccess)
		goto exit;

	// The spooler marks all the message recipients this transport has to
	// handle with PR_RESPONSIBILITY set to FALSE
	SPropValue spvRecipUnsent;
	spvRecipUnsent.ulPropTag                       = PR_RESPONSIBILITY;
	spvRecipUnsent.Value.b                         = FALSE;

	SRestriction srRecipientUnhandled;
	srRecipientUnhandled.rt                        = RES_PROPERTY;
	srRecipientUnhandled.res.resProperty.relop     = RELOP_EQ;
	srRecipientUnhandled.res.resProperty.ulPropTag = PR_RESPONSIBILITY;
	srRecipientUnhandled.res.resProperty.lpProp    = &spvRecipUnsent;

	hr = lpRecipTable->Restrict(&srRecipientUnhandled, 0);
	if (hr != hrSuccess)
		goto exit;

	hr = lpRecipTable->GetRowCount(0, &ulRowCount);
	if (hr != hrSuccess)
		goto exit;

	if(ulRowCount == 0) {
		hr = MAPI_E_NOT_ME;
		goto exit;
	}

	if (HrGetECMsgStore(lpMessage, &lpECMsgStore) != hrSuccess) {
		hr = m_lpMAPISup->OpenEntry(this->m_lpXPProvider->m_lpIdentityProps[XPID_STORE_EID].Value.bin.cb, (LPENTRYID)this->m_lpXPProvider->m_lpIdentityProps[XPID_STORE_EID].Value.bin.lpb, NULL, MAPI_MODIFY, &ulType, (IUnknown **)&lpMsgStore);
		if (hr != hrSuccess)
			goto exit;

		hr = HrGetOneProp(lpMsgStore, PR_EC_OBJECT, &lpECObject);
		if (hr != hrSuccess)
			goto exit;

		lpECMsgStore = (ECMsgStore*)lpECObject->Value.lpszA;
		lpECMsgStore->AddRef();
	}

	hr = lpECMsgStore->QueryInterface(IID_ECMsgStoreOnline, (LPVOID*)&lpOnlineStore);
	if (hr != hrSuccess)
		goto exit;

	hr = HrGetECMsgStore(lpOnlineStore, &lpOnlineECMsgStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpOnlineStore->OpenEntry(0, NULL, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, (LPUNKNOWN*)&lpSubmitFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = ClearOldSubmittedMessages(lpSubmitFolder);
	if (FAILED(hr))
		goto exit;

	hr = lpSubmitFolder->CreateMessage(&IID_IMessage, 0, &lpSubmitMessage);
	if (hr != hrSuccess)
		goto exit;
	hr = lpMessage->CopyTo(0, NULL, sptExcludeProps, 0, NULL,
	     &IID_IMessage, lpSubmitMessage, 0, NULL);
	if (hr != hrSuccess)
		goto exit;
	
	sDeleteAfterSubmitProp.ulPropTag = PR_DELETE_AFTER_SUBMIT;
	sDeleteAfterSubmitProp.Value.b = true;
	hr = HrSetOneProp(lpSubmitMessage, &sDeleteAfterSubmitProp);
	if (hr != hrSuccess)
		goto exit;

	hr = lpSubmitMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		goto exit;
	
	hr = HrGetOneProp(lpSubmitMessage, PR_ENTRYID, &lpEntryID);
	if (hr != hrSuccess)
		goto exit;

	sDelete.cValues = 1;
	sDelete.lpbin = &lpEntryID->Value.bin;

	// Add the message to the master outgoing queue on the server
	l_exit.lock();

	hr = lpOnlineStore->Advise(lpEntryID->Value.bin.cb, (LPENTRYID)lpEntryID->Value.bin.lpb, fnevObjectDeleted, &this->m_xMAPIAdviseSink, &ulOnlineAdviseConnection);
	if (hr != hrSuccess) {
		lpSubmitFolder->DeleteMessages(&sDelete, 0, NULL, 0); //Delete message on the server
		l_exit.unlock();
		goto exit;
	}

	hr = lpOnlineECMsgStore->lpTransport->HrSubmitMessage(lpEntryID->Value.bin.cb, (LPENTRYID)lpEntryID->Value.bin.lpb, EC_SUBMIT_MASTER | EC_SUBMIT_DOSENTMAIL);
	if (hr != hrSuccess) {
		lpSubmitFolder->DeleteMessages(&sDelete, 0, NULL, 0); //Delete message on the server
		l_exit.unlock();
		goto exit;
	}
	if (m_hExitSignal.wait_for(l_exit, std::chrono::minutes(5)) == std::cv_status::timeout)
		m_bCancel = true;

	lpOnlineStore->Unadvise(ulOnlineAdviseConnection);

	if(m_bCancel){
		l_exit.unlock();
		hr = MAPI_E_CANCEL;

		lpOnlineECMsgStore->lpTransport->HrFinishedMessage(lpEntryID->Value.bin.cb, (LPENTRYID)lpEntryID->Value.bin.lpb, EC_SUBMIT_MASTER);
		
		sDelete.cValues = 1;
		sDelete.lpbin = &lpEntryID->Value.bin;
		lpSubmitFolder->DeleteMessages(&sDelete, 0, NULL, 0);

		// Message still in queue (other error occurred or still in queue)
		if(lpulReturnParm)
			*lpulReturnParm = 60;
		
		goto exit;
	}
	l_exit.unlock();
	if(lpulMsgRef)
		*lpulMsgRef = rand_mt();

	// Update the recipient table because we sent the message OK
	hr = HrQueryAllRows (lpRecipTable, NULL, NULL, NULL, 0, &lpRecipRows);
	if (hr != erSuccess)
		goto exit;

	for (ulRow = 0; ulRow < lpRecipRows->cRows; ++ulRow) {
		LPSPropValue lpsPropValue = PpropFindProp(lpRecipRows->aRow[ulRow].lpProps, lpRecipRows->aRow[ulRow].cValues, PR_ADDRTYPE);
		LPSPropValue lpsResponsibility = PpropFindProp(lpRecipRows->aRow[ulRow].lpProps, lpRecipRows->aRow[ulRow].cValues, PR_RESPONSIBILITY);

		if(lpsPropValue == NULL || lpsResponsibility == NULL)
			continue;

		// Accept all SMTP-type addresses and set PR_RESPONSIBILITY set to TRUE
		if (_tcsicmp(lpsPropValue->Value.LPSZ, TRANSPORT_ADDRESS_TYPE_SMTP) == 0 ||
		    _tcsicmp(lpsPropValue->Value.LPSZ, TRANSPORT_ADDRESS_TYPE_ZARAFA) == 0 ||
		    _tcsicmp(lpsPropValue->Value.LPSZ, TRANSPORT_ADDRESS_TYPE_FAX) == 0)
			lpsResponsibility->Value.b = TRUE;
	}

	hr = lpMessage->ModifyRecipients(MODRECIP_MODIFY, (LPADRLIST )lpRecipRows);

	if (hr != erSuccess)
		goto exit;

	// Note that these modifications are *not* saved. This is correct, because they are
	// only important for other transports running on the same lpMessage.

exit:
	if (lpMsgStore)
		lpMsgStore->Release();
	MAPIFreeBuffer(lpECObject);
	if (lpOnlineStore)
		lpOnlineStore->Release();

	if (lpECMsgStore)
		lpECMsgStore->Release();

	if (lpOnlineECMsgStore)
		lpOnlineECMsgStore->Release();

	if (lpSubmitMessage)
		lpSubmitMessage->Release();

	if (lpSubmitFolder)
		lpSubmitFolder->Release();
	MAPIFreeBuffer(lpEntryID);
	if(lpRecipRows)
		FreeProws (lpRecipRows);

	if(lpRecipTable)
		lpRecipTable->Release();
	lpMessage->Release();
	return hr;
}

#define OUT_MSG_PROPS 2
static const SizedSPropTagArray(OUT_MSG_PROPS, sptOutMsgProps) =
{
    OUT_MSG_PROPS,
    {
        PR_SENDER_ENTRYID,
        PR_SENT_REPRESENTING_NAME
    }
};

HRESULT ECXPLogon::SetOutgoingProps (LPMESSAGE lpMessage)
{
	LPSPropValue lpspvSender = NULL;
	ULONG ulValues;
	HRESULT hr = erSuccess;
	#define NUM_OUTGOING_PROPS  12
	SPropValue spvProps[NUM_OUTGOING_PROPS] = {{0}};
	ULONG i = 0;
	FILETIME ft;

	hr = lpMessage->GetProps(sptOutMsgProps, 0, &ulValues, &lpspvSender);
	if (FAILED(hr))
		lpspvSender = NULL; // So that we may recover and continue using default values

    assert(ulValues == 2);
    // If no sender has been stamped on the message use the identity of the transport
    if (!lpspvSender || PR_SENDER_ENTRYID != lpspvSender[0].ulPropTag)
    {
        spvProps[i].ulPropTag = PR_SENDER_NAME;
        spvProps[i++].Value.lpszA = m_lpXPProvider->m_lpIdentityProps[XPID_NAME].Value.lpszA;

        spvProps[i].ulPropTag = PR_SENDER_EMAIL_ADDRESS;
        spvProps[i++].Value.lpszA = m_lpXPProvider->m_lpIdentityProps[XPID_ADDRESS].Value.lpszA;

        spvProps[i].ulPropTag = PR_SENDER_ADDRTYPE;
        spvProps[i++].Value.lpszA = m_lpXPProvider->m_lpIdentityProps[XPID_ADDRTYPE].Value.lpszA;

        spvProps[i].ulPropTag = PR_SENDER_ENTRYID;
        spvProps[i++].Value.bin =  m_lpXPProvider->m_lpIdentityProps[XPID_EID].Value.bin;

        spvProps[i].ulPropTag = PR_SENDER_SEARCH_KEY;
        spvProps[i++].Value.bin = m_lpXPProvider->m_lpIdentityProps[XPID_SEARCH_KEY].Value.bin;
		
    }
    // The MS Exchange mail viewer requires these properties
   if (!lpspvSender || PR_SENT_REPRESENTING_NAME != lpspvSender[1].ulPropTag)
    {
        spvProps[i].ulPropTag = PR_SENT_REPRESENTING_NAME;
        spvProps[i++].Value.lpszA = m_lpXPProvider->m_lpIdentityProps[XPID_NAME].Value.lpszA;
        spvProps[i].ulPropTag = PR_SENT_REPRESENTING_SEARCH_KEY;
        spvProps[i++].Value.bin = m_lpXPProvider->m_lpIdentityProps[XPID_SEARCH_KEY].Value.bin;
        spvProps[i].ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
        spvProps[i++].Value.bin = m_lpXPProvider->m_lpIdentityProps[XPID_EID].Value.bin;
        spvProps[i].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE;
        spvProps[i++].Value.lpszA = m_lpXPProvider->m_lpIdentityProps[XPID_ADDRTYPE].Value.lpszA;
        spvProps[i].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS;
        spvProps[i++].Value.lpszA = m_lpXPProvider->m_lpIdentityProps[XPID_ADDRESS].Value.lpszA;
		
    }
    
	GetSystemTimeAsFileTime(&ft);

    // Set the time when this transport actually transmitted the message
    spvProps[i].ulPropTag = PR_MESSAGE_DELIVERY_TIME;
    spvProps[i++].Value.ft = ft;
    spvProps[i].ulPropTag = PR_PROVIDER_SUBMIT_TIME;
    spvProps[i++].Value.ft = ft;

    assert (i <= NUM_OUTGOING_PROPS);
    hr = lpMessage->SetProps (i, spvProps, NULL);

	if(lpspvSender)
		ECFreeBuffer(lpspvSender);

	return hr;
}

HRESULT ECXPLogon::EndMessage(ULONG ulMsgRef, ULONG * lpulFlags)
{
	return hrSuccess;
}

HRESULT ECXPLogon::Poll(ULONG * lpulIncoming)
{
	*lpulIncoming = 0;
	//lpulIncoming [out] Value indicating the existence of inbound messages. 
	//A nonzero value indicates that there are inbound messages.
	return hrSuccess;
}

HRESULT ECXPLogon::StartMessage(ULONG ulFlags, LPMESSAGE lpMessage, ULONG * lpulMsgRef)
{
	*lpulMsgRef = 0;
	return hrSuccess;
}

HRESULT ECXPLogon::OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG * lpulObjType, LPMAPISTATUS * lppEntry)
{
	return MAPI_E_CALL_FAILED;
}

HRESULT ECXPLogon::ValidateState(ULONG ulUIParam, ULONG ulFlags)
{
	return hrSuccess;
}

HRESULT ECXPLogon::FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags)
{
	//The outbound message queue or queues should be flushed. 
	if (ulFlags & FLUSH_UPLOAD)
		m_ulTransportStatus |= STATUS_OUTBOUND_FLUSH;
	//The inbound message queue or queues should be flushed. 
	if (ulFlags & FLUSH_DOWNLOAD)
		m_ulTransportStatus |= STATUS_INBOUND_FLUSH;
	return HrUpdateTransportStatus();
}

ULONG ECXPLogon::OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifs){
	for (unsigned int i = 0; i < cNotif; ++i)
		if(lpNotifs[i].ulEventType == fnevObjectDeleted) {
			scoped_lock lock(m_hExitMutex);
			m_hExitSignal.notify_one();
		}
	return S_OK;
}

static const TCHAR *GetStatusString(ULONG ulFlags)
{
	const TCHAR *lpszStatus = NULL;

	if (ulFlags & STATUS_INBOUND_ACTIVE)
		lpszStatus = _T("Uploading messages...");
	else if (ulFlags & STATUS_OUTBOUND_ACTIVE)
		lpszStatus = _T("Downloading messages...");
	else if (ulFlags & STATUS_INBOUND_FLUSH)
		lpszStatus = _T("Inbound Flushing...");
	else if (ulFlags & STATUS_OUTBOUND_FLUSH)
		lpszStatus = _T("Outbound Flushing...");
	else if ((ulFlags & STATUS_AVAILABLE) &&
			((ulFlags & STATUS_INBOUND_ENABLED) ||
			(ulFlags & STATUS_OUTBOUND_ENABLED)))
			lpszStatus = _T("On-Line");
	else if (ulFlags & STATUS_AVAILABLE)
		lpszStatus = _T("Available");
	else
		lpszStatus = _T("Off-Line");

	return lpszStatus;
}

HRESULT ECXPLogon::HrUpdateTransportStatus()
{
    HRESULT hResult;
    ULONG cProps = 2;
    SPropValue rgProps[2];
	const TCHAR *lpszStatus = NULL;
	
    //  Store the new Transport Provider Status Code. 

    rgProps[0].ulPropTag = PR_STATUS_CODE;
	// Set the STATUS_OFFLINE flag if the store is offline. This causes the following:
	// Outlook 2000: Disables 'send all data at outlook exit'
	// Outlook XP: 
	//
	// Outlook 2007: No effect

	rgProps[0].Value.ul = m_ulTransportStatus | (m_bOffline ? STATUS_OFFLINE : 0);

    // Set the Status String according to ulStatus
	lpszStatus = GetStatusString(m_ulTransportStatus);

    if (!lpszStatus)
    {
        rgProps[1].ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(PR_STATUS_STRING));
        rgProps[1].Value.err = MAPI_E_NOT_FOUND;
    }
    else
    {
        rgProps[1].ulPropTag = PR_STATUS_STRING;
		rgProps[1].Value.lpszA = (LPSTR)lpszStatus;	// @todo: Check if this 'hack' actually works for wide character strings.
    }
	
    //  OK. Notify the Spooler. It will tell MAPI. 
    hResult = m_lpMAPISup->ModifyStatusRow(cProps, rgProps, STATUSROW_UPDATE);

    return hResult;
}

DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, AddressTypes, (ULONG *, lpulFlags), (ULONG *, lpcAdrType), (LPTSTR **, lpppszAdrTypeArray), (ULONG *, lpcMAPIUID), (LPMAPIUID **, lpppUIDArray))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, RegisterOptions, (ULONG *, lpulFlags), (ULONG *, lpcOptions), (LPOPTIONDATA *, lppOptions))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, TransportNotify, (ULONG *, lpulFlags), (LPVOID *, lppvData))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, Idle, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, TransportLogoff, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, SubmitMessage, (ULONG, ulFlags), (LPMESSAGE, lpMessage), (ULONG *, lpulMsgRef), (ULONG *, lpulReturnParm))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, EndMessage, (ULONG, ulMsgRef), (ULONG *, lpulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, Poll, (ULONG *, lpulIncoming))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, StartMessage, (ULONG, ulFlags), (LPMESSAGE, lpMessage), (ULONG *, lpulMsgRef))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, OpenStatusEntry, (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPMAPISTATUS *, lppEntry))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, ValidateState, (ULONG, ulUIParam), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECXPLogon, XPLogon, FlushQueues, (ULONG, ulUIParam), (ULONG, cbTargetTransport), (LPENTRYID, lpTargetTransport), (ULONG, ulFlags))

DEF_ULONGMETHOD1(TRACE_MAPI, ECXPLogon, MAPIAdviseSink, OnNotify, (ULONG, cNotif), (LPNOTIFICATION, lpNotifs))
DEF_HRMETHOD_NOSUPPORT(TRACE_MAPI, ECXPLogon, MAPIAdviseSink, QueryInterface, (REFIID, refiid), (void **, lppInterface))

ULONG __stdcall ECXPLogon::xMAPIAdviseSink::AddRef(){
	return 1;
}

ULONG __stdcall ECXPLogon::xMAPIAdviseSink::Release(){
	return 1;
}
