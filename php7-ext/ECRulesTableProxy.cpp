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
#include "ECRulesTableProxy.h"
#include <kopano/ECGuid.h>
#include <kopano/mapi_ptr.h>
#include <kopano/ECInterfaceDefs.h>
#include <kopano/Trace.h>
#include <kopano/charset/convert.h>

#include <mapix.h>

/* conversion from unicode to string8 for rules table data */
static HRESULT ConvertUnicodeToString8(LPSRestriction lpRes, void *base, convert_context &converter);
static HRESULT ConvertUnicodeToString8(const ACTIONS *lpActions, void *base, convert_context &converter);

ECRulesTableProxy::ECRulesTableProxy(LPMAPITABLE lpTable)
: m_lpTable(lpTable)
{
	m_lpTable->AddRef();
}

ECRulesTableProxy::~ECRulesTableProxy()
{
	m_lpTable->Release();
}

HRESULT ECRulesTableProxy::Create(LPMAPITABLE lpTable, ECRulesTableProxy **lppRulesTableProxy)
{
	mapi_object_ptr<ECRulesTableProxy> ptrRulesTableProxy;

	if (lpTable == NULL || lppRulesTableProxy == NULL)
		return MAPI_E_INVALID_PARAMETER;
	try {
		ptrRulesTableProxy.reset(new ECRulesTableProxy(lpTable));
	} catch (const std::bad_alloc &) {
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}

	*lppRulesTableProxy = ptrRulesTableProxy.release();
	return hrSuccess;
}

HRESULT ECRulesTableProxy::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECUnknown, this);
	REGISTER_INTERFACE(IID_IMAPITable, &this->m_xMAPITable);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xMAPITable);
	
	return MAPI_E_INTERFACE_NOT_SUPPORTED;

}

HRESULT ECRulesTableProxy::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return m_lpTable->GetLastError(hResult, ulFlags, lppMAPIError);
}

HRESULT ECRulesTableProxy::Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection)
{
	return m_lpTable->Advise(ulEventMask, lpAdviseSink, lpulConnection);
}

HRESULT ECRulesTableProxy::Unadvise(ULONG ulConnection)
{
	return m_lpTable->Unadvise(ulConnection);
}

HRESULT ECRulesTableProxy::GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType)
{
	return m_lpTable->GetStatus(lpulTableStatus, lpulTableType);
}

HRESULT ECRulesTableProxy::SetColumns(LPSPropTagArray lpPropTagArray, ULONG ulFlags)
{
	return m_lpTable->SetColumns(lpPropTagArray, ulFlags);
}

HRESULT ECRulesTableProxy::QueryColumns(ULONG ulFlags, LPSPropTagArray *lpPropTagArray)
{
	return m_lpTable->QueryColumns(ulFlags, lpPropTagArray);
}

HRESULT ECRulesTableProxy::GetRowCount(ULONG ulFlags, ULONG *lpulCount)
{
	return m_lpTable->GetRowCount(ulFlags, lpulCount);
}

HRESULT ECRulesTableProxy::SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought)
{
	return m_lpTable->SeekRow(bkOrigin, lRowCount, lplRowsSought);
}

HRESULT ECRulesTableProxy::SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator)
{
	return m_lpTable->SeekRowApprox(ulNumerator, ulDenominator);
}

HRESULT ECRulesTableProxy::QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator)
{
	return m_lpTable->QueryPosition(lpulRow, lpulNumerator, lpulDenominator);
}

HRESULT ECRulesTableProxy::FindRow(LPSRestriction lpRestriction, BOOKMARK bkOrigin, ULONG ulFlags)
{
	return m_lpTable->FindRow(lpRestriction, bkOrigin, ulFlags);
}

HRESULT ECRulesTableProxy::Restrict(LPSRestriction lpRestriction, ULONG ulFlags)
{
	return m_lpTable->Restrict(lpRestriction, ulFlags);
}

HRESULT ECRulesTableProxy::CreateBookmark(BOOKMARK* lpbkPosition)
{
	return m_lpTable->CreateBookmark(lpbkPosition);
}

HRESULT ECRulesTableProxy::FreeBookmark(BOOKMARK bkPosition)
{
	return m_lpTable->FreeBookmark(bkPosition);
}

HRESULT ECRulesTableProxy::SortTable(LPSSortOrderSet lpSortCriteria, ULONG ulFlags)
{
	return m_lpTable->SortTable(lpSortCriteria, ulFlags);
}

HRESULT ECRulesTableProxy::QuerySortOrder(LPSSortOrderSet *lppSortCriteria)
{
	return m_lpTable->QuerySortOrder(lppSortCriteria);
}

HRESULT ECRulesTableProxy::QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet *lppRows)
{
	HRESULT hr = hrSuccess;
	SRowSetPtr ptrRows;
	convert_context converter;

	hr = m_lpTable->QueryRows(lRowCount, ulFlags, &ptrRows);
	if (hr != hrSuccess)
		return hr;
	
	// table PR_RULE_ACTIONS and PR_RULE_CONDITION contain PT_UNICODE data, which we must convert to local charset PT_STRING8
	// so we update the rows before we return them to the caller.
	for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
		LPSPropValue lpRuleProp = NULL;

		lpRuleProp = PpropFindProp(ptrRows[i].lpProps, ptrRows[i].cValues, PR_RULE_CONDITION);
		if (lpRuleProp)
			hr = ConvertUnicodeToString8((LPSRestriction)lpRuleProp->Value.lpszA, ptrRows[i].lpProps, converter);
		if (hr != hrSuccess)
			return hr;
		lpRuleProp = PpropFindProp(ptrRows[i].lpProps, ptrRows[i].cValues, PR_RULE_ACTIONS);
		if (lpRuleProp)
			hr = ConvertUnicodeToString8((ACTIONS*)lpRuleProp->Value.lpszA, ptrRows[i].lpProps, converter);
		if (hr != hrSuccess)
			return hr;
	}
	
	*lppRows = ptrRows.release();
	return hr;
}

HRESULT ECRulesTableProxy::Abort()
{
	return m_lpTable->Abort();
}

HRESULT ECRulesTableProxy::ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet * lppRows, ULONG *lpulMoreRows)
{
	return m_lpTable->ExpandRow(cbInstanceKey, pbInstanceKey, ulRowCount, ulFlags, lppRows, lpulMoreRows);
}

HRESULT ECRulesTableProxy::CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount)
{
	return m_lpTable->CollapseRow(cbInstanceKey, pbInstanceKey, ulFlags, lpulRowCount);
}

HRESULT ECRulesTableProxy::WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG *lpulTableStatus)
{
	return m_lpTable->WaitForCompletion(ulFlags, ulTimeout, lpulTableStatus);
}

HRESULT ECRulesTableProxy::GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState)
{
	return m_lpTable->GetCollapseState(ulFlags, cbInstanceKey, lpbInstanceKey, lpcbCollapseState, lppbCollapseState);
}

HRESULT ECRulesTableProxy::SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation)
{
	return m_lpTable->SetCollapseState(ulFlags, cbCollapseState, pbCollapseState, lpbkLocation);
}

ULONG ECRulesTableProxy::xMAPITable::AddRef()
{
	METHOD_PROLOGUE_(ECRulesTableProxy, MAPITable);
	return pThis->AddRef();
}
			
ULONG ECRulesTableProxy::xMAPITable::Release()
{
	METHOD_PROLOGUE_(ECRulesTableProxy, MAPITable);
	return pThis->Release();
}

DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, GetLastError, (HRESULT, hResult), (ULONG, ulFlags), (LPMAPIERROR *, lppMAPIError))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, Advise, (ULONG, ulEventMask), (LPMAPIADVISESINK, lpAdviseSink), (ULONG *, lpulConnection))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, Unadvise, (ULONG, ulConnection))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, GetStatus, (ULONG *, lpulTableStatus), (ULONG *, lpulTableType))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, SetColumns, (LPSPropTagArray, lpPropTagArray), (ULONG, ulFlags))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, QueryColumns, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, GetRowCount, (ULONG, ulFlags), (ULONG *, lpulCount))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, SeekRow, (BOOKMARK, bkOrigin), (LONG, lRowCount), (LONG *, lplRowsSought))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, SeekRowApprox, (ULONG, ulNumerator), (ULONG, ulDenominator))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, QueryPosition, (ULONG *, lpulRow), (ULONG *, lpulNumerator), (ULONG *, lpulDenominator))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, FindRow, (LPSRestriction, lpRestriction), (BOOKMARK, bkOrigin), (ULONG, ulFlags))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, Restrict, (LPSRestriction, lpRestriction), (ULONG, ulFlags))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, CreateBookmark, (BOOKMARK *, lpbkPosition))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, FreeBookmark, (BOOKMARK, bkPosition))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, SortTable, (LPSSortOrderSet, lpSortCriteria), (ULONG, ulFlags))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, QuerySortOrder, (LPSSortOrderSet *, lppSortCriteria))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, QueryRows, (LONG, lRowCount), (ULONG, ulFlags), (LPSRowSet *, lppRows))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, Abort, (void))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, ExpandRow, (ULONG, cbInstanceKey, LPBYTE, pbInstanceKey), (ULONG, ulRowCount), (ULONG, ulFlags), (LPSRowSet *, lppRows), (ULONG *, lpulMoreRows))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, CollapseRow, (ULONG, cbInstanceKey, LPBYTE, pbInstanceKey), (ULONG, ulFlags), (ULONG *, lpulRowCount))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, WaitForCompletion, (ULONG, ulFlags), (ULONG, ulTimeout), (ULONG *, lpulTableStatus))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, GetCollapseState, (ULONG, ulFlags), (ULONG, cbInstanceKey, LPBYTE, lpbInstanceKey), (ULONG *, lpcbCollapseState, LPBYTE *, lppbCollapseState))
DEF_HRMETHOD(TRACE_MAPI, ECRulesTableProxy, MAPITable, SetCollapseState, (ULONG, ulFlags), (ULONG, cbCollapseState, LPBYTE, pbCollapseState), (BOOKMARK *, lpbkLocation))

static HRESULT ConvertUnicodeToString8(const WCHAR *lpszW, char **lppszA,
    void *base, convert_context &converter)
{
	std::string local;
	char *lpszA = NULL;

	if (lpszW == NULL || lppszA == NULL)
		return MAPI_E_INVALID_PARAMETER;
	TryConvert(lpszW, local);
	HRESULT hr = MAPIAllocateMore((local.length() +1) * sizeof(std::string::value_type),
		base, reinterpret_cast<void **>(&lpszA));
	if (hr != hrSuccess)
		return hr;
	strcpy(lpszA, local.c_str());
	*lppszA = lpszA;
	return hrSuccess;
}

static HRESULT ConvertUnicodeToString8(LPSRestriction lpRestriction,
    void *base, convert_context &converter)
{
	HRESULT hr = hrSuccess;
	ULONG i;

	if (lpRestriction == NULL)
		return hrSuccess;

	switch (lpRestriction->rt) {
	case RES_OR:
		for (i = 0; i < lpRestriction->res.resOr.cRes; ++i) {
			hr = ConvertUnicodeToString8(&lpRestriction->res.resOr.lpRes[i], base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	case RES_AND:
		for (i = 0; i < lpRestriction->res.resAnd.cRes; ++i) {
			hr = ConvertUnicodeToString8(&lpRestriction->res.resAnd.lpRes[i], base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	case RES_NOT:
		hr = ConvertUnicodeToString8(lpRestriction->res.resNot.lpRes, base, converter);
		if (hr != hrSuccess)
			return hr;
		break;
	case RES_COMMENT:
		if (lpRestriction->res.resComment.lpRes) {
			hr = ConvertUnicodeToString8(lpRestriction->res.resComment.lpRes, base, converter);
			if (hr != hrSuccess)
				return hr;
		}
		for (i = 0; i < lpRestriction->res.resComment.cValues; ++i)
			if (PROP_TYPE(lpRestriction->res.resComment.lpProp[i].ulPropTag) == PT_UNICODE) {
				hr = ConvertUnicodeToString8(lpRestriction->res.resComment.lpProp[i].Value.lpszW, &lpRestriction->res.resComment.lpProp[i].Value.lpszA, base, converter);
				if (hr != hrSuccess)
					return hr;
				lpRestriction->res.resComment.lpProp[i].ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resComment.lpProp[i].ulPropTag, PT_STRING8);
			}
		break;
	case RES_COMPAREPROPS:
		break;
	case RES_CONTENT:
		if (PROP_TYPE(lpRestriction->res.resContent.ulPropTag) == PT_UNICODE) {
			hr = ConvertUnicodeToString8(lpRestriction->res.resContent.lpProp->Value.lpszW, &lpRestriction->res.resContent.lpProp->Value.lpszA, base, converter);
			if (hr != hrSuccess)
				return hr;
			lpRestriction->res.resContent.lpProp->ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resContent.lpProp->ulPropTag, PT_STRING8);
			lpRestriction->res.resContent.ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resContent.ulPropTag, PT_STRING8);
		}
		break;
	case RES_PROPERTY:
		if (PROP_TYPE(lpRestriction->res.resProperty.ulPropTag) == PT_UNICODE) {
			hr = ConvertUnicodeToString8(lpRestriction->res.resProperty.lpProp->Value.lpszW, &lpRestriction->res.resProperty.lpProp->Value.lpszA, base, converter);
			if (hr != hrSuccess)
				return hr;
			lpRestriction->res.resProperty.lpProp->ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resProperty.lpProp->ulPropTag, PT_STRING8);
			lpRestriction->res.resProperty.ulPropTag = CHANGE_PROP_TYPE(lpRestriction->res.resProperty.ulPropTag, PT_STRING8);
		}
		break;
	case RES_SUBRESTRICTION:
		hr = ConvertUnicodeToString8(lpRestriction->res.resSub.lpRes, base, converter);
		if (hr != hrSuccess)
			return hr;
		break;
	}
	return hr;
}

static HRESULT ConvertUnicodeToString8(const SRow *lpRow, void *base,
    convert_context &converter)
{
	if (lpRow == NULL)
		return hrSuccess;
	for (ULONG c = 0; c < lpRow->cValues; ++c) {
		if (PROP_TYPE(lpRow->lpProps[c].ulPropTag) == PT_UNICODE) {
			HRESULT hr = ConvertUnicodeToString8(lpRow->lpProps[c].Value.lpszW,
				&lpRow->lpProps[c].Value.lpszA, base, converter);
			if (hr != hrSuccess)
				return hr;
			lpRow->lpProps[c].ulPropTag = CHANGE_PROP_TYPE(lpRow->lpProps[c].ulPropTag, PT_STRING8);
		}
	}
	return hrSuccess;
}

static HRESULT ConvertUnicodeToString8(const ADRLIST *lpAdrList, void *base,
    convert_context &converter)
{
	if (lpAdrList == NULL)
		return hrSuccess;
	for (ULONG c = 0; c < lpAdrList->cEntries; ++c) {
		// treat as row
		HRESULT hr = ConvertUnicodeToString8(reinterpret_cast<const SRow *>(&lpAdrList->aEntries[c]),
			base, converter);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

static HRESULT ConvertUnicodeToString8(const ACTIONS *lpActions, void *base, convert_context &converter)
{
	if (lpActions == NULL)
		return hrSuccess;
	for (ULONG c = 0; c < lpActions->cActions; ++c)
		if (lpActions->lpAction[c].acttype == OP_FORWARD || lpActions->lpAction[c].acttype == OP_DELEGATE) {
			HRESULT hr = ConvertUnicodeToString8(lpActions->lpAction[c].lpadrlist, base, converter);
			if (hr != hrSuccess)
				return hr;
		}
	return hrSuccess;
}
