/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/scope.hpp>
#include "WSTableView.h"
#include <kopano/ECGuid.h>
#include "SOAPSock.h"
#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/charset/convert.h>

/*
 * TableView operations for WS transport
 */
#define START_SOAP_CALL retry: \
	if (m_lpTransport->m_lpCmd == nullptr) \
		return MAPI_E_NETWORK_ERROR;
#define END_SOAP_CALL 	\
	if (er == KCERR_END_OF_SESSION && m_lpTransport->HrReLogon() == hrSuccess) \
		goto retry; \
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND); \
	if(hr != hrSuccess) \
		return hr;

using namespace KC;

WSTableView::WSTableView(ULONG ty, ULONG fl, ECSESSIONID sid, ULONG cbEntryId,
    const ENTRYID *lpEntryId, WSTransport *lpTransport) :
	ecSessionId(sid), m_lpTransport(lpTransport), ulFlags(fl), ulType(ty)
{
	m_lpTransport->AddSessionReloadCallback(this, Reload, &m_ulSessionReloadCallback);
	CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &m_sEntryId);
}

WSTableView::~WSTableView()
{
	m_lpTransport->RemoveSessionReloadCallback(m_ulSessionReloadCallback);
	/* If the table was still open, it will now be closed in the server too. */
	HrCloseTable();
	delete[] m_lpsPropTagArray;
	delete[] m_lpsSortOrderSet;
	soap_del_entryId(&m_sEntryId);
}

HRESULT WSTableView::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE3(ECTableView, WSTableView, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSTableView::HrQueryRows(ULONG ulRowCount, ULONG flags, SRowSet **lppRowSet)
{
	ECRESULT er = erSuccess;
	struct tableQueryRowsResponse sResponse;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableQueryRows(ecSessionId,
		    ulTableId, ulRowCount, flags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL
	return CopySOAPRowSetToMAPIRowSet(m_lpProvider, &sResponse.sRowSet,
	       lppRowSet, ulType);
}

HRESULT WSTableView::HrCloseTable()
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	soap_lock_guard spg(*m_lpTransport);

	if(ulTableId == 0)
		return hrSuccess;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableClose(ecSessionId, ulTableId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;

		if(er == KCERR_END_OF_SESSION)
			er = erSuccess; // Don't care about end of session
	}
	END_SOAP_CALL
	return hrSuccess;
}

HRESULT WSTableView::HrSetColumns(const SPropTagArray *lpsPropTagArray)
{
	ECRESULT er = erSuccess;
	struct propTagArray sColumns;
	LPSPropTagArray lpsOld = m_lpsPropTagArray;

	// Save the columns so that we can restore the column state when reconnecting
	m_lpsPropTagArray = (LPSPropTagArray) new char[CbNewSPropTagArray(lpsPropTagArray->cValues)];
	auto cleanup = make_scope_exit([&]() { delete[] lpsOld; });
	memcpy(&m_lpsPropTagArray->aulPropTag, &lpsPropTagArray->aulPropTag, sizeof(ULONG) * lpsPropTagArray->cValues);
	m_lpsPropTagArray->cValues = lpsPropTagArray->cValues;

	sColumns.__ptr = (unsigned int *)&lpsPropTagArray->aulPropTag;
	sColumns.__size = lpsPropTagArray->cValues;

	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableSetColumns(ecSessionId,
		    ulTableId, &sColumns, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
	return hrSuccess;
}

HRESULT WSTableView::HrQueryColumns(ULONG flags, SPropTagArray **lppsPropTags)
{
	ECRESULT er = erSuccess;
	struct tableQueryColumnsResponse sResponse;
	LPSPropTagArray lpsPropTags = NULL;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableQueryColumns(ecSessionId,
		    ulTableId, flags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = MAPIAllocateBuffer(CbNewSPropTagArray(sResponse.sPropTagArray.__size), reinterpret_cast<void **>(&lpsPropTags));
	if(hr != hrSuccess)
		return hr;
	for (gsoap_size_t i = 0; i < sResponse.sPropTagArray.__size; ++i)
		lpsPropTags->aulPropTag[i] = sResponse.sPropTagArray.__ptr[i];
	lpsPropTags->cValues = sResponse.sPropTagArray.__size;
	*lppsPropTags = lpsPropTags;
	return hrSuccess;
}

HRESULT WSTableView::HrSortTable(const SSortOrderSet *lpsSortOrderSet)
{
	ECRESULT er = erSuccess;
	sortOrderArray sSort;
	LPSSortOrderSet lpOld = m_lpsSortOrderSet;

	// Remember sort order for reconnect
	m_lpsSortOrderSet = (LPSSortOrderSet)new char [CbSSortOrderSet(lpsSortOrderSet)];
	auto cleanup = make_scope_exit([&]() {
		delete[] lpOld;
		soap_del_sortOrderArray(&sSort);
	});
	memcpy(m_lpsSortOrderSet, lpsSortOrderSet, CbSSortOrderSet(lpsSortOrderSet));

	sSort.__size = lpsSortOrderSet->cSorts;
	sSort.__ptr  = soap_new_sortOrder(nullptr, lpsSortOrderSet->cSorts);
	for (unsigned int i = 0; i < lpsSortOrderSet->cSorts; ++i) {
		sSort.__ptr[i].ulOrder = lpsSortOrderSet->aSort[i].ulOrder;
		sSort.__ptr[i].ulPropTag = lpsSortOrderSet->aSort[i].ulPropTag;
	}

	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableSort(ecSessionId, ulTableId,
		    &sSort, lpsSortOrderSet->cCategories,
		    lpsSortOrderSet->cExpanded, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
	return hrSuccess;
}

HRESULT WSTableView::HrOpenTable()
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct tableOpenResponse sResponse;
	soap_lock_guard spg(*m_lpTransport);
	if (ulTableId != 0)
		return hrSuccess;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableOpen(ecSessionId, m_sEntryId,
		    m_ulTableType, ulType, ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL
	ulTableId = sResponse.ulTableId;
	return hrSuccess;
}

HRESULT WSTableView::HrGetRowCount(ULONG *lpulRowCount, ULONG *lpulCurrentRow)
{
	ECRESULT er = erSuccess;
	struct tableGetRowCountResponse sResponse;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableGetRowCount(ecSessionId,
		    ulTableId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulRowCount = sResponse.ulCount;
	*lpulCurrentRow = sResponse.ulRow;
	return hrSuccess;
}

HRESULT WSTableView::HrFindRow(const SRestriction *lpsRestriction,
    BOOKMARK bkOrigin, ULONG flags)
{
	ECRESULT er = erSuccess;
	struct restrictTable *lpRestrict = NULL;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = CopyMAPIRestrictionToSOAPRestriction(&lpRestrict, lpsRestriction);
	auto cleanup = make_scope_exit([&]() { soap_del_PointerTorestrictTable(&lpRestrict); });
	if (hr != hrSuccess)
		return hr;
	hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableFindRow(ecSessionId,
		    ulTableId, static_cast<unsigned int>(bkOrigin),
		    flags, lpRestrict, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
	return hrSuccess;
}

HRESULT WSTableView::HrSeekRow(BOOKMARK bkOrigin, LONG lRows, LONG *lplRowsSought)
{
	ECRESULT er = erSuccess;
	struct tableSeekRowResponse sResponse;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableSeekRow(ecSessionId,
		    ulTableId, static_cast<unsigned int>(bkOrigin),
		    lRows, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if(lplRowsSought)
		*lplRowsSought = sResponse.lRowsSought;
	return hrSuccess;
}

HRESULT WSTableView::CreateBookmark(BOOKMARK* lpbkPosition)
{
	if (lpbkPosition == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	tableBookmarkResponse	sResponse;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableCreateBookmark(ecSessionId,
		    ulTableId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpbkPosition = sResponse.ulbkPosition;
	return hrSuccess;
}

HRESULT WSTableView::FreeBookmark(BOOKMARK bkPosition)
{
	ECRESULT er = erSuccess;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableFreeBookmark(ecSessionId,
		    ulTableId, bkPosition, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL
	return hrSuccess;
}

HRESULT WSTableView::HrExpandRow(ULONG cbInstanceKey, BYTE *pbInstanceKey,
    ULONG ulRowCount, ULONG flags, SRowSet **lppRows, ULONG *lpulMoreRows)
{
	ECRESULT er = erSuccess;
	xsd__base64Binary sInstanceKey;
	struct tableExpandRowResponse sResponse;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	sInstanceKey.__size = cbInstanceKey;
	sInstanceKey.__ptr = pbInstanceKey;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableExpandRow(ecSessionId,
		    ulTableId, sInstanceKey, ulRowCount, flags,
		    &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if(lppRows)
		hr = CopySOAPRowSetToMAPIRowSet(m_lpProvider, &sResponse.rowSet, lppRows, ulType);
	if(lpulMoreRows)
		*lpulMoreRows = sResponse.ulMoreRows;
	return hr;
}

HRESULT WSTableView::HrCollapseRow(ULONG cbInstanceKey, BYTE *pbInstanceKey,
    ULONG flags, ULONG *lpulRowCount)
{
	ECRESULT er = erSuccess;
	xsd__base64Binary sInstanceKey;
	struct tableCollapseRowResponse sResponse;
	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	sInstanceKey.__size = cbInstanceKey;
	sInstanceKey.__ptr = pbInstanceKey;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableCollapseRow(ecSessionId,
		    ulTableId, sInstanceKey, flags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulRowCount = sResponse.ulRows;
	return hrSuccess;
}

HRESULT WSTableView::HrGetCollapseState(BYTE **lppCollapseState, ULONG *lpcbCollapseState, BYTE *lpInstanceKey, ULONG cbInstanceKey)
{
	ECRESULT er = erSuccess;
	struct tableGetCollapseStateResponse sResponse;
	struct xsd__base64Binary sBookmark;

	sBookmark.__size = cbInstanceKey;
	sBookmark.__ptr = lpInstanceKey;

	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableGetCollapseState(ecSessionId,
		    ulTableId, sBookmark, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = KAllocCopy(sResponse.sCollapseState.__ptr, sResponse.sCollapseState.__size, reinterpret_cast<void **>(lppCollapseState));
	if (hr != hrSuccess)
		return hr;
	*lpcbCollapseState = sResponse.sCollapseState.__size;
	return hrSuccess;
}

HRESULT WSTableView::HrSetCollapseState(BYTE *lpCollapseState, ULONG cbCollapseState, BOOKMARK *lpbkPosition)
{
	ECRESULT er = erSuccess;
	xsd__base64Binary sState;
	struct tableSetCollapseStateResponse sResponse;

	sState.__ptr = lpCollapseState;
	sState.__size = cbCollapseState;

	soap_lock_guard spg(*m_lpTransport);
	auto hr = HrOpenTable();
	if (hr != hrSuccess)
		return hr;

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableSetCollapseState(ecSessionId,
		    ulTableId, sState, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	if(lpbkPosition)
		*lpbkPosition = sResponse.ulBookmark;
	return hrSuccess;
}

HRESULT WSTableView::HrMulti(ULONG ulDeferredFlags,
    SPropTagArray *lpsPropTagArray, SRestriction *lpsRestriction,
    SSortOrderSet *lpsSortOrderSet, ULONG ulRowCount, ULONG flags,
    SRowSet **lppRowSet)
{
	struct propTagArray sColumns;
	struct tableMultiRequest sRequest;
	struct tableMultiResponse sResponse;
	struct restrictTable *lpsRestrictTable = NULL;
	struct tableQueryRowsRequest sQueryRows;
	struct tableSortRequest sSort;
	struct tableOpenRequest sOpen;

	if(ulTableId == 0) {
	    sOpen.sEntryId = m_sEntryId;
	    sOpen.ulTableType = m_ulTableType;
		sOpen.ulType = ulType;
		sOpen.ulFlags = ulFlags;
	    sRequest.lpOpen = &sOpen;
    } else {
        sRequest.ulTableId = ulTableId;
    }

	sRequest.ulFlags = ulDeferredFlags;

	if(lpsPropTagArray) {
		// Save the proptag set for if we need to reload
		delete[] m_lpsPropTagArray;
		m_lpsPropTagArray = (LPSPropTagArray) new char[CbNewSPropTagArray(lpsPropTagArray->cValues)];
		memcpy(&m_lpsPropTagArray->aulPropTag, &lpsPropTagArray->aulPropTag, sizeof(ULONG) * lpsPropTagArray->cValues);
		m_lpsPropTagArray->cValues = lpsPropTagArray->cValues;

		// Copy the setcolumns for the call
		sColumns.__size = lpsPropTagArray->cValues;
    	sColumns.__ptr = (unsigned int *)&lpsPropTagArray->aulPropTag; // Don't copy, just reference their data
    	sRequest.lpSetColumns = &sColumns;
	}

	auto cleanup = make_scope_exit([&]() {
		soap_del_sortOrderArray(&sSort.sSortOrder);
		soap_del_PointerTorestrictTable(&lpsRestrictTable);
	});
	soap_lock_guard spg(*m_lpTransport);
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	if(lpsRestriction) {
		hr = CopyMAPIRestrictionToSOAPRestriction(&lpsRestrictTable, lpsRestriction);
		if(hr != hrSuccess)
			return hr;
        sRequest.lpRestrict = lpsRestrictTable;
	}

	if(lpsSortOrderSet) {
		// Remember sort order for reconnect
		delete[] m_lpsSortOrderSet;
		m_lpsSortOrderSet = (LPSSortOrderSet)new char [CbSSortOrderSet(lpsSortOrderSet)];
		memcpy(m_lpsSortOrderSet, lpsSortOrderSet, CbSSortOrderSet(lpsSortOrderSet));

		// Copy sort order for call
        sSort.sSortOrder.__size = lpsSortOrderSet->cSorts;
		sSort.sSortOrder.__ptr  = soap_new_sortOrder(nullptr, lpsSortOrderSet->cSorts);
		for (unsigned int i = 0; i < lpsSortOrderSet->cSorts; ++i) {
            sSort.sSortOrder.__ptr[i].ulOrder = lpsSortOrderSet->aSort[i].ulOrder;
            sSort.sSortOrder.__ptr[i].ulPropTag = lpsSortOrderSet->aSort[i].ulPropTag;
        }

        sSort.ulExpanded = lpsSortOrderSet->cExpanded;
        sSort.ulCategories = lpsSortOrderSet->cCategories;
        sRequest.lpSort = &sSort;
	}

	if(ulRowCount > 0) {
	    sQueryRows.ulCount = ulRowCount;
		sQueryRows.ulFlags = flags;
	    sRequest.lpQueryRows = &sQueryRows;
	}

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->tableMulti(ecSessionId, sRequest, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if (sResponse.ulTableId != 0)
		ulTableId = sResponse.ulTableId;
	if (lppRowSet)
		hr = CopySOAPRowSetToMAPIRowSet(m_lpProvider, &sResponse.sRowSet, lppRowSet, ulType);
	return hr;
}

HRESULT WSTableView::Reload(void *lpParam, ECSESSIONID sessionId)
{
	auto lpThis = static_cast<WSTableView *>(lpParam);

	lpThis->ecSessionId = sessionId;
	// Since we've switched sessions, our table is no longer open or valid
	lpThis->ulTableId = 0;

	// Restore state
	if (lpThis->m_lpsPropTagArray != nullptr)
		// ignore error
		lpThis->HrSetColumns(lpThis->m_lpsPropTagArray);
	if (lpThis->m_lpsSortOrderSet != nullptr)
		// ignore error
		lpThis->HrSortTable(lpThis->m_lpsSortOrderSet);

	// Call the reload callback if necessary
	if(lpThis->m_lpCallback)
		lpThis->m_lpCallback(lpThis->m_lpParam);
	return hrSuccess;
}

HRESULT WSTableView::SetReloadCallback(RELOADCALLBACK callback, void *lpParam)
{
	m_lpCallback = callback;
	m_lpParam = lpParam;
	return hrSuccess;
}

// WSTableOutGoingQueue view
WSTableOutGoingQueue::WSTableOutGoingQueue(ECSESSIONID sid, ULONG cbEntryId,
    const ENTRYID *lpEntryId, ECMsgStore *lpMsgStore, WSTransport *tp) :
	WSStoreTableView(MAPI_MESSAGE, 0, sid, cbEntryId, lpEntryId,
	    lpMsgStore, tp)
{
}

HRESULT WSTableOutGoingQueue::Create(ECSESSIONID ecSessionId, ULONG cbEntryId,
    const ENTRYID *lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport,
    WSTableOutGoingQueue **lppTableOutGoingQueue)
{
	return alloc_wrap<WSTableOutGoingQueue>(ecSessionId, cbEntryId,
	       lpEntryId, lpMsgStore, lpTransport).put(lppTableOutGoingQueue);
}

HRESULT	WSTableOutGoingQueue::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE3(ECTableOutGoingQueue, WSTableOutGoingQueue, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSTableOutGoingQueue::HrOpenTable()
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	struct tableOpenResponse sResponse;
	soap_lock_guard spg(*m_lpTransport);
	if (ulTableId != 0)
		return hrSuccess;

	START_SOAP_CALL
	{
		//m_sEntryId is the id of a store
		if (m_lpTransport->m_lpCmd->tableOpen(ecSessionId, m_sEntryId,
		    TABLETYPE_SPOOLER, 0, ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL
	ulTableId = sResponse.ulTableId;
	return hrSuccess;
}
