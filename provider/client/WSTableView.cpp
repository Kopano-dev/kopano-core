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
#include <new>
#include <kopano/platform.h>
#include "WSTableView.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/charset/convert.h>
#include "soapKCmdProxy.h"

/*
 * TableView operations for WS transport
 */
#define START_SOAP_CALL retry:
#define END_SOAP_CALL 	\
	if (er == KCERR_END_OF_SESSION && m_lpTransport->HrReLogon() == hrSuccess) \
		goto retry; \
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND); \
	if(hr != hrSuccess) \
		goto exit;

WSTableView::WSTableView(ULONG ty, ULONG fl, KCmdProxy *cmd,
    std::recursive_mutex &data_lock, ECSESSIONID sid, ULONG cbEntryId,
    const ENTRYID *lpEntryId, WSTransport *lpTransport,
    const char *szClassName) :
	ECUnknown(szClassName), lpCmd(cmd), lpDataLock(data_lock),
	ecSessionId(sid), m_lpTransport(lpTransport), ulFlags(fl), ulType(ty)
{
	m_lpTransport->AddSessionReloadCallback(this, Reload, &m_ulSessionReloadCallback);

	CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &m_sEntryId);
}

WSTableView::~WSTableView()
{
	m_lpTransport->RemoveSessionReloadCallback(m_ulSessionReloadCallback);

	// if the table was still open it will now be closed in the server too
	this->HrCloseTable();
	delete[] m_lpsPropTagArray;
	delete[] m_lpsSortOrderSet;
	FreeEntryId(&m_sEntryId, false);
}

HRESULT WSTableView::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE3(ECTableView, WSTableView, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSTableView::HrQueryRows(ULONG ulRowCount, ULONG ulFlags, LPSRowSet *lppRowSet)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct tableQueryRowsResponse sResponse;

	LockSoap();
	
	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableQueryRows(ecSessionId, ulTableId, ulRowCount, ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = CopySOAPRowSetToMAPIRowSet(m_lpProvider, &sResponse.sRowSet, lppRowSet, this->ulType);

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrCloseTable()
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;

	LockSoap();

	if(ulTableId == 0)
		goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableClose(ecSessionId, this->ulTableId, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;

		if(er == KCERR_END_OF_SESSION)
			er = erSuccess; // Don't care about end of session 
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrSetColumns(const SPropTagArray *lpsPropTagArray)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct propTagArray sColumns;
	LPSPropTagArray lpsOld = m_lpsPropTagArray;

	// Save the columns so that we can restore the column state when reconnecting
	m_lpsPropTagArray = (LPSPropTagArray) new char[CbNewSPropTagArray(lpsPropTagArray->cValues)];
	memcpy(&m_lpsPropTagArray->aulPropTag, &lpsPropTagArray->aulPropTag, sizeof(ULONG) * lpsPropTagArray->cValues);
	m_lpsPropTagArray->cValues = lpsPropTagArray->cValues;

	sColumns.__ptr = (unsigned int *)&lpsPropTagArray->aulPropTag;
	sColumns.__size = lpsPropTagArray->cValues;

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableSetColumns(ecSessionId, ulTableId, &sColumns, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	delete[] lpsOld;
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrQueryColumns(ULONG ulFlags, LPSPropTagArray *lppsPropTags)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct tableQueryColumnsResponse sResponse;
	LPSPropTagArray lpsPropTags = NULL;
	
	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableQueryColumns(ecSessionId, ulTableId, ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = ECAllocateBuffer(CbNewSPropTagArray(sResponse.sPropTagArray.__size),(void **)&lpsPropTags);

	if(hr != hrSuccess)
		goto exit;
	for (gsoap_size_t i = 0; i < sResponse.sPropTagArray.__size; ++i)
		lpsPropTags->aulPropTag[i] = sResponse.sPropTagArray.__ptr[i];

	lpsPropTags->cValues = sResponse.sPropTagArray.__size;

	*lppsPropTags = lpsPropTags;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrSortTable(const SSortOrderSet *lpsSortOrderSet)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	sortOrderArray sSort;
	unsigned int i=0;
	LPSSortOrderSet lpOld = m_lpsSortOrderSet;
	
	// Remember sort order for reconnect
	m_lpsSortOrderSet = (LPSSortOrderSet)new char [CbSSortOrderSet(lpsSortOrderSet)];
	memcpy(m_lpsSortOrderSet, lpsSortOrderSet, CbSSortOrderSet(lpsSortOrderSet));

	sSort.__size = lpsSortOrderSet->cSorts;
	sSort.__ptr = s_alloc<sortOrder>(nullptr, lpsSortOrderSet->cSorts);

	for (i = 0; i < lpsSortOrderSet->cSorts; ++i) {
		sSort.__ptr[i].ulOrder = lpsSortOrderSet->aSort[i].ulOrder;
		sSort.__ptr[i].ulPropTag = lpsSortOrderSet->aSort[i].ulPropTag;
	}

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableSort(ecSessionId, ulTableId, &sSort, lpsSortOrderSet->cCategories, lpsSortOrderSet->cExpanded, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();
	delete[] lpOld;
	s_free(nullptr, sSort.__ptr);
	return hr;
}

HRESULT WSTableView::HrOpenTable()
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct tableOpenResponse sResponse;

	LockSoap();

	if(this->ulTableId != 0)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableOpen(ecSessionId, m_sEntryId, m_ulTableType, ulType, this->ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	this->ulTableId = sResponse.ulTableId;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrGetRowCount(ULONG *lpulRowCount, ULONG *lpulCurrentRow)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct tableGetRowCountResponse sResponse;
	
	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableGetRowCount(ecSessionId, ulTableId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulRowCount = sResponse.ulCount;
	*lpulCurrentRow = sResponse.ulRow;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrFindRow(const SRestriction *lpsRestriction,
    BOOKMARK bkOrigin, ULONG ulFlags)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;

	struct restrictTable *lpRestrict = NULL;

	LockSoap();

	er = CopyMAPIRestrictionToSOAPRestriction(&lpRestrict, lpsRestriction);

	if(er != erSuccess) {
		hr = MAPI_E_INVALID_PARAMETER;

		goto exit;
	}
	
	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableFindRow(ecSessionId, ulTableId, static_cast<unsigned int>(bkOrigin), ulFlags, lpRestrict, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	if(lpRestrict)
		FreeRestrictTable(lpRestrict);

	return hr;
}

HRESULT WSTableView::HrSeekRow(BOOKMARK bkOrigin, LONG lRows, LONG *lplRowsSought)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;

	struct tableSeekRowResponse sResponse;

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableSeekRow(ecSessionId, ulTableId, static_cast<unsigned int>(bkOrigin), lRows, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if(lplRowsSought)
		*lplRowsSought = sResponse.lRowsSought;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::CreateBookmark(BOOKMARK* lpbkPosition)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	tableBookmarkResponse	sResponse;

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	if(lpbkPosition == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	START_SOAP_CALL
	{
		if (lpCmd->tableCreateBookmark(ecSessionId, ulTableId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpbkPosition = sResponse.ulbkPosition;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::FreeBookmark(BOOKMARK bkPosition)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableFreeBookmark(ecSessionId, ulTableId, bkPosition, &er) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
	}
	END_SOAP_CALL

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet * lppRows, ULONG *lpulMoreRows)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	xsd__base64Binary sInstanceKey;
	struct tableExpandRowResponse sResponse;

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	sInstanceKey.__size = cbInstanceKey;
	sInstanceKey.__ptr = pbInstanceKey;

	START_SOAP_CALL
	{
		if (lpCmd->tableExpandRow(ecSessionId, this->ulTableId, sInstanceKey, ulRowCount, ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if(lppRows)
		hr = CopySOAPRowSetToMAPIRowSet(m_lpProvider, &sResponse.rowSet, lppRows, this->ulType);	

	if(lpulMoreRows)
		*lpulMoreRows = sResponse.ulMoreRows;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrCollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	xsd__base64Binary sInstanceKey;
	struct tableCollapseRowResponse sResponse;

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	sInstanceKey.__size = cbInstanceKey;
	sInstanceKey.__ptr = pbInstanceKey;

	START_SOAP_CALL
	{
		if (lpCmd->tableCollapseRow(ecSessionId, this->ulTableId, sInstanceKey, ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	*lpulRowCount = sResponse.ulRows;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrGetCollapseState(BYTE **lppCollapseState, ULONG *lpcbCollapseState, BYTE *lpInstanceKey, ULONG cbInstanceKey)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	struct tableGetCollapseStateResponse sResponse;
	struct xsd__base64Binary sBookmark;

	sBookmark.__size = cbInstanceKey;
	sBookmark.__ptr = lpInstanceKey;

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableGetCollapseState(ecSessionId, this->ulTableId, sBookmark, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = KAllocCopy(sResponse.sCollapseState.__ptr, sResponse.sCollapseState.__size, reinterpret_cast<void **>(lppCollapseState));
	if (hr != hrSuccess)
		goto exit;
	*lpcbCollapseState = sResponse.sCollapseState.__size;
		
exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrSetCollapseState(BYTE *lpCollapseState, ULONG cbCollapseState, BOOKMARK *lpbkPosition)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = erSuccess;
	xsd__base64Binary sState;
	struct tableSetCollapseStateResponse sResponse;

	sState.__ptr = lpCollapseState;
	sState.__size = cbCollapseState;

	LockSoap();

	hr = HrOpenTable();
	if(hr != erSuccess)
	    goto exit;

	START_SOAP_CALL
	{
		if (lpCmd->tableSetCollapseState(ecSessionId, this->ulTableId, sState, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		goto exit;

	if(lpbkPosition)
		*lpbkPosition = sResponse.ulBookmark;

exit:
	UnLockSoap();

	return hr;
}

HRESULT WSTableView::HrMulti(ULONG ulDeferredFlags, LPSPropTagArray lpsPropTagArray, LPSRestriction lpsRestriction, LPSSortOrderSet lpsSortOrderSet, ULONG ulRowCount, ULONG ulFlags, LPSRowSet *lppRowSet)
{
    HRESULT hr = hrSuccess;
    ECRESULT er = erSuccess;
	struct propTagArray sColumns;
	struct tableMultiRequest sRequest;
	struct tableMultiResponse sResponse;
	struct restrictTable *lpsRestrictTable = NULL;
	struct tableQueryRowsRequest sQueryRows;
	struct tableSortRequest sSort;
	struct tableOpenRequest sOpen;
	unsigned int i;
	
	if(ulTableId == 0) {
	    sOpen.sEntryId = m_sEntryId;
	    sOpen.ulTableType = m_ulTableType;
	    sOpen.ulType = this->ulType;
	    sOpen.ulFlags = this->ulFlags;
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
    
	if(lpsRestriction) {
		hr = CopyMAPIRestrictionToSOAPRestriction(&lpsRestrictTable, lpsRestriction);

		if(hr != hrSuccess)
			goto exit;
			
        sRequest.lpRestrict = lpsRestrictTable;
	}
	
	if(lpsSortOrderSet) {
		// Remember sort order for reconnect
		delete[] m_lpsSortOrderSet;
		m_lpsSortOrderSet = (LPSSortOrderSet)new char [CbSSortOrderSet(lpsSortOrderSet)];
		memcpy(m_lpsSortOrderSet, lpsSortOrderSet, CbSSortOrderSet(lpsSortOrderSet));

		// Copy sort order for call
        sSort.sSortOrder.__size = lpsSortOrderSet->cSorts;
		sSort.sSortOrder.__ptr = s_alloc<sortOrder>(nullptr, lpsSortOrderSet->cSorts);

        for (i = 0; i < lpsSortOrderSet->cSorts; ++i) {
            sSort.sSortOrder.__ptr[i].ulOrder = lpsSortOrderSet->aSort[i].ulOrder;
            sSort.sSortOrder.__ptr[i].ulPropTag = lpsSortOrderSet->aSort[i].ulPropTag;
        }
        
        sSort.ulExpanded = lpsSortOrderSet->cExpanded;
        sSort.ulCategories = lpsSortOrderSet->cCategories;
        
        sRequest.lpSort = &sSort;
	}
	
	if(ulRowCount > 0) {
	    sQueryRows.ulCount = ulRowCount;
	    sQueryRows.ulFlags = ulFlags;
	    
	    sRequest.lpQueryRows = &sQueryRows;
	}

	LockSoap();

	START_SOAP_CALL
	{
		if (lpCmd->tableMulti(ecSessionId, sRequest, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	if (sResponse.ulTableId != 0)
		ulTableId = sResponse.ulTableId;
	if (lppRowSet)
		hr = CopySOAPRowSetToMAPIRowSet(m_lpProvider, &sResponse.sRowSet, lppRowSet, this->ulType);

exit:
	UnLockSoap();
	s_free(nullptr, sSort.sSortOrder.__ptr);
	if(lpsRestrictTable)
		FreeRestrictTable(lpsRestrictTable);
        
    return hr;    
}

//FIXME: one lock/unlock function
HRESULT WSTableView::LockSoap()
{
	lpDataLock.lock();
	return erSuccess;
}

HRESULT WSTableView::UnLockSoap()
{
	//Clean up data create with soap_malloc
	if(lpCmd->soap) {
		soap_destroy(lpCmd->soap);
		soap_end(lpCmd->soap);
	}
	lpDataLock.unlock();
	return erSuccess;
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
	this->m_lpCallback = callback;
	this->m_lpParam = lpParam;
	return hrSuccess;
}

// WSTableOutGoingQueue view
WSTableOutGoingQueue::WSTableOutGoingQueue(KCmdProxy *lpCmd,
    std::recursive_mutex &lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId,
    const ENTRYID *lpEntryId, ECMsgStore *lpMsgStore,
    WSTransport *lpTransport) :
	WSStoreTableView(MAPI_MESSAGE, 0, lpCmd, lpDataLock, ecSessionId,
	    cbEntryId, lpEntryId, lpMsgStore, lpTransport)
{
}

HRESULT WSTableOutGoingQueue::Create(KCmdProxy *lpCmd,
    std::recursive_mutex &lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId,
    const ENTRYID *lpEntryId, ECMsgStore *lpMsgStore, WSTransport *lpTransport,
    WSTableOutGoingQueue **lppTableOutGoingQueue)
{
	return alloc_wrap<WSTableOutGoingQueue>(lpCmd, lpDataLock, ecSessionId,
	       cbEntryId, lpEntryId, lpMsgStore, lpTransport).put(lppTableOutGoingQueue);
}

HRESULT	WSTableOutGoingQueue::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE3(ECTableOutGoingQueue, WSTableOutGoingQueue, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSTableOutGoingQueue::HrOpenTable()
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;

	struct tableOpenResponse sResponse;

	LockSoap();

	if(this->ulTableId != 0)
	    goto exit;

	START_SOAP_CALL
	{
		//m_sEntryId is the id of a store
		if (lpCmd->tableOpen(ecSessionId, m_sEntryId, TABLETYPE_SPOOLER, 0, this->ulFlags, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	this->ulTableId = sResponse.ulTableId;

exit:
	UnLockSoap();

	return hr;
}

