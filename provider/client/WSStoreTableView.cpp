/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "ECMsgStore.h"
#include "WSStoreTableView.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include "SOAPSock.h"
#include "SOAPUtils.h"
#include "WSUtil.h"

using namespace KC;

WSStoreTableView::WSStoreTableView(ULONG type, ULONG flags, ECSESSIONID sid,
    ULONG cbEntryId, const ENTRYID *lpEntryId, ECMsgStore *lpMsgStore,
    WSTransport *lpTransport) :
	WSTableView(type, flags, sid, cbEntryId, lpEntryId, lpTransport)
{
	// OK, this is ugly, but the static row-wrapper routines need this object
	// to get the guid and other information that has to be inlined into the table row. Really,
	// the whole transport layer should have no references to the ECMAPI* classes, because that's
	// upside-down in the layer model, but having the transport layer first deliver the properties,
	// and have a different routine then go through all the properties is more memory intensive AND
	// slower as we have 2 passes to fill the queried rows.
	m_lpProvider = lpMsgStore;
	m_ulTableType = TABLETYPE_MS;
}

HRESULT WSStoreTableView::Create(ULONG ulType, ULONG ulFlags,
    ECSESSIONID ecSessionId, ULONG cbEntryId, const ENTRYID *lpEntryId,
    ECMsgStore *lpMsgStore, WSTransport *lpTransport,
    WSTableView **lppTableView)
{
	return alloc_wrap<WSStoreTableView>(ulType, ulFlags, ecSessionId,
	       cbEntryId, lpEntryId, lpMsgStore, lpTransport)
	       .as(IID_ECTableView, lppTableView);
}

/*
  Miscellaneous tables are not really store tables, but the is the same, so it inherits from the store table
  Supported tables are the stats tables, and userstores table.
*/
WSTableMisc::WSTableMisc(ULONG ulTableType, ULONG flags, ECSESSIONID sid,
    ULONG cbEntryId, const ENTRYID *lpEntryId, ECMsgStore *lpMsgStore,
    WSTransport *lpTransport) :
	// is MAPI_STATUS even valid here?
	WSStoreTableView(MAPI_STATUS, flags, sid, cbEntryId, lpEntryId,
	    lpMsgStore, lpTransport)
{
	m_ulTableType = ulTableType;
	ulTableId = 0;
}

HRESULT WSTableMisc::Create(ULONG ulTableType, ULONG ulFlags,
    ECSESSIONID ecSessionId, ULONG cbEntryId, const ENTRYID *lpEntryId,
    ECMsgStore *lpMsgStore, WSTransport *lpTransport,
    WSTableMisc **lppTableMisc)
{
	return alloc_wrap<WSTableMisc>(ulTableType, ulFlags, ecSessionId,
	       cbEntryId, lpEntryId, lpMsgStore, lpTransport)
	       .put(lppTableMisc);
}

HRESULT WSTableMisc::HrOpenTable()
{
	if (ulTableId != 0)
		return hrSuccess;

	ECRESULT er = erSuccess;
	struct tableOpenResponse sResponse;
	soap_lock_guard spg(*m_lpTransport);

	// the class is actually only to call this function with the correct ulTableType .... hmm.
	if (m_lpTransport->m_lpCmd == nullptr ||
	    m_lpTransport->m_lpCmd->tableOpen(ecSessionId, m_sEntryId,
	    m_ulTableType, ulType, ulFlags, &sResponse) != SOAP_OK)
		er = KCERR_NETWORK_ERROR;
	else
		er = sResponse.er;

	auto hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	ulTableId = sResponse.ulTableId;
	return hrSuccess;
}

// WSTableMailBox view
WSTableMailBox::WSTableMailBox(ULONG flags, ECSESSIONID sid,
    ECMsgStore *lpMsgStore, WSTransport *lpTransport) :
	WSStoreTableView(MAPI_STORE, flags, sid, 0, nullptr, lpMsgStore,
	    lpTransport)
{
	m_ulTableType = TABLETYPE_MAILBOX;
}

HRESULT WSTableMailBox::Create(ULONG ulFlags, ECSESSIONID ecSessionId,
    ECMsgStore *lpMsgStore, WSTransport *lpTransport, WSTableMailBox **lppTable)
{
	return alloc_wrap<WSTableMailBox>(ulFlags, ecSessionId, lpMsgStore,
	       lpTransport).put(lppTable);
}
