/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <stdexcept>
#include <kopano/platform.h>
#include "WSABPropStorage.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/charset/convert.h>
#include "soapKCmdProxy.h"

#define START_SOAP_CALL retry:
#define END_SOAP_CALL   \
	if (er == KCERR_END_OF_SESSION && m_lpTransport->HrReLogon() == hrSuccess) \
		goto retry; \
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND); \
    if(hr != hrSuccess) \
        goto exit;
                    

/*
 * This is a PropStorage object for use with the WebServices storage platform
 */

WSABPropStorage::WSABPropStorage(ULONG cbEntryId, const ENTRYID *lpEntryId,
    ECSESSIONID sid, WSTransport *lpTransport) :
	ECUnknown("WSABPropStorage"), ecSessionId(sid),
	m_lpTransport(lpTransport)
{
	auto ret = CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &m_sEntryId);
	if (ret != hrSuccess)
		throw std::runtime_error("CopyMAPIEntryIdToSOAPEntryId");
    lpTransport->AddSessionReloadCallback(this, Reload, &m_ulSessionReloadCallback);
}

WSABPropStorage::~WSABPropStorage()
{
    m_lpTransport->RemoveSessionReloadCallback(m_ulSessionReloadCallback);
    
	FreeEntryId(&m_sEntryId, false);
}

HRESULT WSABPropStorage::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(WSABPropStorage, this);
	REGISTER_INTERFACE2(IECPropStorage, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSABPropStorage::Create(ULONG cbEntryId, const ENTRYID *lpEntryId,
    ECSESSIONID ecSessionId, WSTransport *lpTransport,
    WSABPropStorage **lppPropStorage)
{
	return alloc_wrap<WSABPropStorage>(cbEntryId, lpEntryId, ecSessionId,
	       lpTransport).put(lppPropStorage);
}

HRESULT WSABPropStorage::HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT WSABPropStorage::HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject)
{
	return MAPI_E_NO_SUPPORT;
	// TODO: this should be supported eventually
}

HRESULT WSABPropStorage::HrLoadObject(MAPIOBJECT **lppsMapiObject)
{
	HRESULT		hr = hrSuccess;
	ECRESULT	er = hrSuccess;
	MAPIOBJECT  *mo = NULL;
	ecmem_ptr<SPropValue> lpProp;
	struct readPropsResponse sResponse;
	convert_context	converter;
	soap_lock_guard spg(*m_lpTransport);

	START_SOAP_CALL
	{
    	// Read the properties from the server
		if (m_lpTransport->m_lpCmd->readABProps(ecSessionId, m_sEntryId, &sResponse) != SOAP_OK)
    		er = KCERR_NETWORK_ERROR;
    	else
    		er = sResponse.er;
    }
    END_SOAP_CALL
    
	// Convert the property tags to a MAPIOBJECT
	//(type,objectid)
	mo = new MAPIOBJECT;

	/*
	 * This is only done to have a base for AllocateMore, otherwise a local
	 * automatic variable would have sufficed.
	 */
	hr = ECAllocateBuffer(sizeof(SPropValue), &~lpProp);
	if (hr != hrSuccess)
		goto exit;

	for (gsoap_size_t i = 0; i < sResponse.aPropTag.__size; ++i)
		mo->lstAvailable.emplace_back(sResponse.aPropTag.__ptr[i]);

	for (gsoap_size_t i = 0; i < sResponse.aPropVal.__size; ++i) {
		/* can call AllocateMore on lpProp */
		hr = CopySOAPPropValToMAPIPropVal(lpProp, &sResponse.aPropVal.__ptr[i], lpProp, &converter);
		if (hr != hrSuccess)
			goto exit;
		/*
		 * The ECRecipient ctor makes a deep copy of *lpProp, so it is
		 * ok to have *lpProp overwritten on the next iteration.
		 */
		mo->lstProperties.emplace_back(lpProp);
	}

	*lppsMapiObject = mo;

exit:
	spg.unlock();
	if (hr != hrSuccess)
		delete mo;
	return hr;
}

// Called when the session ID has changed
HRESULT WSABPropStorage::Reload(void *lpParam, ECSESSIONID sessionId) {
	static_cast<WSABPropStorage *>(lpParam)->ecSessionId = sessionId;
	return hrSuccess;
}

WSABTableView::WSABTableView(ULONG type, ULONG flags, ECSESSIONID sid,
    ULONG cbEntryId, const ENTRYID *lpEntryId, ECABLogon *lpABLogon,
    WSTransport *lpTransport) :
	WSTableView(type, flags, sid, cbEntryId, lpEntryId, lpTransport,
	    "WSABTableView")
{
	m_lpProvider = lpABLogon;
	m_ulTableType = TABLETYPE_AB;
}

HRESULT WSABTableView::Create(ULONG ulType, ULONG ulFlags,
    ECSESSIONID ecSessionId, ULONG cbEntryId, const ENTRYID *lpEntryId,
    ECABLogon *lpABLogon, WSTransport *lpTransport, WSTableView **lppTableView)
{
	return alloc_wrap<WSABTableView>(ulType, ulFlags, ecSessionId,
	       cbEntryId, lpEntryId, lpABLogon, lpTransport)
	       .as(IID_ECTableView, lppTableView);
}

HRESULT WSABTableView::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE3(ECTableView, WSTableView, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}
