/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <kopano/platform.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiutil.h>
#include <kopano/ECMemTable.h>
#include <kopano/ECKeyTable.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <kopano/CommonUtil.h>
#include <kopano/memory.hpp>
#include <kopano/charset/convert.h>
#include <kopano/kcodes.h>
#include <algorithm>
#include <netdb.h> /* htons */

namespace KC {

static constexpr const SizedSSortOrderSet(1, sSortDefault) = { 0, 0, 0, {} } ;

class FixStringType final {
public:
	FixStringType(ULONG ulFlags) : m_ulFlags(ulFlags) { assert((m_ulFlags & ~MAPI_UNICODE) == 0); }
	ULONG operator()(ULONG ulPropTag) const
	{
		if ((PROP_TYPE(ulPropTag) & 0x0ffe) != 0x1e)
			return ulPropTag;
		/* Any string type */
		return CHANGE_PROP_TYPE(ulPropTag, (((m_ulFlags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8) | (PROP_TYPE(ulPropTag) & MVI_FLAG)));
	}
private:
	ULONG m_ulFlags;
};

/*
 * This is a complete table data / table view implementation, comparable to
 * ITableData / IMAPITable. The ECMemTable object holds the information, while
 * ECMemTableView is the actual view into the object, implementing the IMAPITable
 * interface.
 */

ECMemTable::ECMemTable(const SPropTagArray *lpsPropTags, ULONG rpt) :
	ECUnknown("ECMemTable"), ulRowPropTag(rpt)
{
	if (MAPIAllocateBuffer(CbSPropTagArray(lpsPropTags), &~lpsColumns) != hrSuccess)
		throw std::bad_alloc();
	lpsColumns->cValues = lpsPropTags->cValues;
	memcpy(&lpsColumns->aulPropTag, &lpsPropTags->aulPropTag, lpsPropTags->cValues * sizeof(ULONG));
}

ECMemTable::~ECMemTable()
{
	HrClear();
}

HRESULT ECMemTable::Create(const SPropTagArray *lpsColumns, ULONG ulRowPropTag,
    ECMemTable **lppECMemTable)
{
	if(PROP_TYPE(ulRowPropTag) != PT_I8 && PROP_TYPE(ulRowPropTag) != PT_LONG)
	{
		assert(false);
		return MAPI_E_INVALID_TYPE;
	}
	return alloc_wrap<ECMemTable>(lpsColumns, ulRowPropTag)
	       .put(lppECMemTable);
}

HRESULT ECMemTable::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMemTable, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

// Get all rowa tables in the table with status type
HRESULT ECMemTable::HrGetAllWithStatus(LPSRowSet *lppRowSet, LPSPropValue *lppIDs, LPULONG *lppulStatus)
{
	rowset_ptr lpRowSet;
	memory_ptr<SPropValue> lpIDs;
	memory_ptr<ULONG> lpulStatus;
	int n = 0;
	ulock_rec l_data(m_hDataMutex);

	auto hr = MAPIAllocateBuffer(CbNewSRowSet(mapRows.size()), &~lpRowSet);
	if(hr != hrSuccess)
		return hr;
	lpRowSet->cRows = 0;
	hr = MAPIAllocateBuffer(sizeof(SPropValue) * mapRows.size(), &~lpIDs);
	if(hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(ULONG) * mapRows.size(), &~lpulStatus);
	if(hr != hrSuccess)
		return hr;

	for (const auto &rowp : mapRows) {
		if (rowp.second.fNew)
			lpulStatus[n] = ECROW_ADDED;
		else if (rowp.second.fDeleted)
			lpulStatus[n] = ECROW_DELETED;
		else if (rowp.second.fDirty)
			lpulStatus[n] = ECROW_MODIFIED;
		else
			lpulStatus[n] = ECROW_NORMAL;

		lpRowSet->aRow[n].cValues = rowp.second.cValues;
		hr = Util::HrCopyPropertyArrayByRef(rowp.second.lpsPropVal, rowp.second.cValues, &lpRowSet->aRow[n].lpProps, &lpRowSet->aRow[n].cValues);
		if(hr != hrSuccess)
			return hr;
		++lpRowSet->cRows;
		if (rowp.second.lpsID != NULL) {
			hr = Util::HrCopyProperty(&lpIDs[n], rowp.second.lpsID, lpIDs);
			if(hr != hrSuccess)
				return hr;
		} else {
			lpIDs[n].Value.bin.cb = 0;
			lpIDs[n].Value.bin.lpb = NULL;
		}
		++n;
	}
	*lppRowSet = lpRowSet.release();
	*lppIDs = lpIDs.release();
	*lppulStatus = lpulStatus.release();
	return hrSuccess;
}

HRESULT ECMemTable::HrGetRowID(LPSPropValue lpRow, LPSPropValue *lppID)
{
	scoped_rlock l_data(m_hDataMutex);

	if (lpRow->ulPropTag != ulRowPropTag)
		return MAPI_E_INVALID_PARAMETER;
	auto iterRows = mapRows.find(lpRow->Value.ul);
	if (iterRows == mapRows.cend() || iterRows->second.lpsID == NULL)
		return MAPI_E_NOT_FOUND;
	LPSPropValue lpID = NULL;
	HRESULT hr = MAPIAllocateBuffer(sizeof(SPropValue),
		reinterpret_cast<void **>(&lpID));
	if(hr != hrSuccess)
		return hr;
	hr = Util::HrCopyProperty(lpID, iterRows->second.lpsID, lpID);
	if(hr != hrSuccess)
		return hr;

	*lppID = lpID;
	return hrSuccess;
}

HRESULT ECMemTable::HrGetRowData(LPSPropValue lpRow, ULONG *lpcValues, LPSPropValue *lppRowData)
{
	ULONG cValues = 0;
	memory_ptr<SPropValue> lpRowData;
	ulock_rec l_data(m_hDataMutex);

	if (lpRow->ulPropTag != ulRowPropTag)
		return MAPI_E_INVALID_PARAMETER;
	auto iterRows = mapRows.find(lpRow->Value.ul);
	if (iterRows == mapRows.cend() || iterRows->second.lpsID == NULL)
		return MAPI_E_NOT_FOUND;
	auto hr = Util::HrCopyPropertyArray(iterRows->second.lpsPropVal, iterRows->second.cValues, &~lpRowData, &cValues);
	if(hr != hrSuccess)
		return hr;
	*lpcValues = cValues;
	*lppRowData = lpRowData.release();
	return hrSuccess;
}

// This function is called to reset the add/modify/delete flags of all
// the rows. This is normally called after saving the data from the HrGet* functions
// to the storage.
HRESULT ECMemTable::HrSetClean()
{
	scoped_rlock l_data(m_hDataMutex);

	for (auto iterRows = mapRows.begin(); iterRows != mapRows.end(); ) {
		if(iterRows->second.fDeleted) {
			iterRows = mapRows.erase(iterRows);
			continue;
		}
		iterRows->second.fDeleted = false;
		iterRows->second.fDirty = false;
		iterRows->second.fNew = false;
		++iterRows;
	}
	return hrSuccess;
}

HRESULT ECMemTable::HrUpdateRowID(LPSPropValue lpId, LPSPropValue lpProps, ULONG cValues)
{
	scoped_rlock l_data(m_hDataMutex);

	auto lpUniqueProp = PCpropFindProp(lpProps, cValues, ulRowPropTag);
	if (lpUniqueProp == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto iterRows = mapRows.find(lpUniqueProp->Value.ul);
	if (iterRows == mapRows.cend())
		return MAPI_E_NOT_FOUND;
	auto hr = MAPIAllocateBuffer(sizeof(SPropValue), &~iterRows->second.lpsID);
	if(hr != hrSuccess)
		return hr;
	return Util::HrCopyProperty(iterRows->second.lpsID, lpId, iterRows->second.lpsID);
}

/*
 * This is not as easy as it seems. This is how we handle updates:
 *
 * There are really only two operations, ADD and DELETE, in which ADD can either add a 
 * row or modify an existing row, and DELETE can delete a row. We basically handle TABLE_ROW_ADD
 * and TABLE_ROW_MODIFY exactly the same:
 *
 * - If there is an index column in the row:
 *   - Existing row, then modify the existing row
 *   - Non-existing row, then add a new row
 * - If there is no index column, exit with an error
 *
 */

HRESULT ECMemTable::HrModifyRow(ULONG ulUpdateType, const SPropValue *lpsID,
    const SPropValue *lpPropVals, ULONG cValues)
{
	scoped_rlock l_data(m_hDataMutex);

	auto lpsRowID = PCpropFindProp(lpPropVals, cValues, ulRowPropTag);
	if (lpsRowID == NULL)
		// you must specify a row number
		return MAPI_E_INVALID_PARAMETER;
	auto iterRows = mapRows.find(lpsRowID->Value.ul);
	if (ulUpdateType == ECKeyTable::TABLE_ROW_ADD && iterRows != mapRows.cend())
		// Row is already in here, move to modify
		ulUpdateType = ECKeyTable::TABLE_ROW_MODIFY;
	if (ulUpdateType == ECKeyTable::TABLE_ROW_MODIFY && iterRows == mapRows.cend())
		// Row not found, move to add
		ulUpdateType = ECKeyTable::TABLE_ROW_ADD;
	if (ulUpdateType == ECKeyTable::TABLE_ROW_DELETE && iterRows == mapRows.cend())
		// Row to delete is nonexistent
		return MAPI_E_NOT_FOUND;

	if(ulUpdateType == ECKeyTable::TABLE_ROW_DELETE) {
		iterRows->second.fDeleted = TRUE;
		iterRows->second.fDirty = FALSE;
		iterRows->second.fNew = FALSE;
	}

	if(ulUpdateType == ECKeyTable::TABLE_ROW_MODIFY) {
		iterRows->second.fDeleted = FALSE;
		iterRows->second.fDirty = TRUE;

		if(lpPropVals) {
			// We want to support users calling ModifyRow with data that itself
			// was retrieved from this table. So, the data passed in lpPropVals may
			// refer to data that is allocated from iterRows->second.lpsPropVal.
			//
			// We therefore save the old value, copy the new properties, and THEN
			// (auto-)free the old row.

			auto lpOldPropVal = std::move(iterRows->second.lpsPropVal);
			// Update new row
			auto hr = Util::HrCopyPropertyArray(lpPropVals, cValues, &~iterRows->second.lpsPropVal, &iterRows->second.cValues, /* exclude PT_ERRORs */ true);
			if(hr != hrSuccess)
				return hr;
		}
	}

	if(ulUpdateType == ECKeyTable::TABLE_ROW_ADD) {
		ECTableEntry entry;
		auto hr = Util::HrCopyPropertyArray(lpPropVals, cValues, &~entry.lpsPropVal, &entry.cValues);
		if(hr != hrSuccess)
			return hr;

		entry.fDeleted = FALSE;
		entry.fDirty = TRUE;
		entry.fNew = TRUE;
		
		if(lpsID) {
			hr = MAPIAllocateBuffer(sizeof(SPropValue), &~entry.lpsID);
			if(hr != hrSuccess)
				return hr;
			hr = Util::HrCopyProperty(entry.lpsID, lpsID, entry.lpsID);
			if(hr != hrSuccess)
				return hr;
		} else {
			entry.lpsID.reset();
		}

		// Add the actual data
		mapRows[lpsRowID->Value.ul] = std::move(entry);
	}

	for (auto viewp : lstViews) {
		auto hr = viewp->UpdateRow(ulUpdateType, lpsRowID->Value.ul);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT ECMemTable::HrGetView(const ECLocale &locale, ULONG ulFlags, ECMemTableView **lppView)
{
	ECMemTableView *lpView = NULL;
	scoped_rlock l_data(m_hDataMutex);

	HRESULT hr = ECMemTableView::Create(this, locale, ulFlags, &lpView);
	if (hr != hrSuccess)
		return hr;
	lstViews.emplace_back(lpView);
	AddChild(lpView);
	*lppView = lpView;
	return hrSuccess;
}

HRESULT ECMemTable::HrDeleteAll()
{
	scoped_rlock l_data(m_hDataMutex);
	for (auto &rowp : mapRows) {
		rowp.second.fDeleted = TRUE;
		rowp.second.fDirty = FALSE;
		rowp.second.fNew = FALSE;
	}
	for (auto viewp : lstViews)
		viewp->Clear();
	return hrSuccess;
}

HRESULT ECMemTable::HrClear()
{
	scoped_rlock l_data(m_hDataMutex);
	// Clear list
	mapRows.clear();

	// Update views
	for (auto viewp : lstViews)
		viewp->Clear();
	return hrSuccess;
}

/*
 * This is the IMAPITable-compatible view section of the ECMemTable. It holds hardly any
 * data and is therefore very lightweight. We use the fast ECKeyTable keying system to do
 * the actual sorting, etc.
 */

ECMemTableView::ECMemTableView(ECMemTable *mt, const ECLocale &locale,
    ULONG ulFlags) :
	ECUnknown("ECMemTableView"), lpMemTable(mt),
	m_locale(locale), m_ulConnection(1), m_ulFlags(ulFlags & MAPI_UNICODE)
{
	if (MAPIAllocateBuffer(CbNewSPropTagArray(lpMemTable->lpsColumns->cValues), &~lpsPropTags) != hrSuccess)
		throw std::bad_alloc();
	lpsPropTags->cValues = lpMemTable->lpsColumns->cValues;
	std::transform(lpMemTable->lpsColumns->aulPropTag, lpMemTable->lpsColumns->aulPropTag + lpMemTable->lpsColumns->cValues, (ULONG*)lpsPropTags->aulPropTag, FixStringType(ulFlags & MAPI_UNICODE));

	SortTable(sSortDefault, 0);
}

ECMemTableView::~ECMemTableView()
{
	// Remove ourselves from the parent's view list
	for (auto iterViews = lpMemTable->lstViews.begin();
	     iterViews != lpMemTable->lstViews.cend(); ++iterViews)
		if(*iterViews == this) {
			lpMemTable->lstViews.erase(iterViews);
			break;
		}

	// Remove advises
	auto iterAdvise = m_mapAdvise.cbegin();
	while (iterAdvise != m_mapAdvise.cend()) {
		auto iterAdviseRemove = iterAdvise;
		++iterAdvise;
		Unadvise(iterAdviseRemove->first);
	}
}

HRESULT ECMemTableView::Create(ECMemTable *lpMemTable, const ECLocale &locale, ULONG ulFlags, ECMemTableView **lppMemTableView)
{
	return alloc_wrap<ECMemTableView>(lpMemTable, locale, ulFlags)
	       .put(lppMemTableView);
}

HRESULT ECMemTableView::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMemTableView, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMAPITable, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMemTableView::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMemTableView::Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG * lpulConnection)
{
	ULONG ulConnection = m_ulConnection++;

	if (lpAdviseSink == NULL || lpulConnection == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto lpMemAdvise = new ECMEMADVISE;
	lpMemAdvise->lpAdviseSink.reset(lpAdviseSink);
	lpMemAdvise->ulEventMask = ulEventMask;
	m_mapAdvise.emplace(ulConnection, lpMemAdvise);
	*lpulConnection = ulConnection;
	return hrSuccess;
}

HRESULT ECMemTableView::Unadvise(ULONG ulConnection)
{
	// Remove notify from list
	ECMapMemAdvise::const_iterator iterAdvise = m_mapAdvise.find(ulConnection);
	if (iterAdvise == m_mapAdvise.cend()) {
		assert(false);
		return hrSuccess;
	}
	delete iterAdvise->second;
	m_mapAdvise.erase(iterAdvise);
	return hrSuccess;
}

HRESULT ECMemTableView::Notify(ULONG ulTableEvent, sObjectTableKey* lpsRowItem, sObjectTableKey* lpsPrevRow)
{
	memory_ptr<NOTIFICATION> lpNotification;
	rowset_ptr lpRows;
	ECObjectTableList sRowList;
	auto hr = MAPIAllocateBuffer(sizeof(NOTIFICATION), &~lpNotification);
	if(hr != hrSuccess)
		return hr;

	memset(lpNotification, 0, sizeof(NOTIFICATION));

	lpNotification->ulEventType = fnevTableModified;
	auto &tab = lpNotification->info.tab;
	tab.ulTableEvent = ulTableEvent;

	if (lpsPrevRow == NULL || lpsPrevRow->ulObjId == 0) {
		tab.propPrior.ulPropTag = PR_NULL;
	} else {
		tab.propPrior.ulPropTag = PR_INSTANCE_KEY;
		auto &bin = tab.propPrior.Value.bin;
		bin.cb = sizeof(ULONG) * 2;
		hr = MAPIAllocateMore(bin.cb, lpNotification, reinterpret_cast<void **>(&bin.lpb));
		if(hr != hrSuccess)
			return hr;
		uint32_t tmp4 = cpu_to_le32(lpsPrevRow->ulObjId);
		memcpy(bin.lpb, &tmp4, sizeof(tmp4));
		tmp4 = cpu_to_le32(lpsPrevRow->ulOrderId);
		memcpy(bin.lpb + sizeof(tmp4), &tmp4, sizeof(tmp4));
	}

	if (lpsRowItem == NULL || lpsRowItem->ulObjId == 0) {
		tab.propIndex.ulPropTag = PR_NULL;
	} else {
		tab.propIndex.ulPropTag = PR_INSTANCE_KEY;
		auto &bin = tab.propIndex.Value.bin;
		bin.cb = sizeof(ULONG) * 2;
		hr = MAPIAllocateMore(bin.cb, lpNotification, reinterpret_cast<void **>(&bin.lpb));
		if(hr != hrSuccess)
			return hr;
		uint32_t tmp4 = cpu_to_le32(lpsRowItem->ulObjId);
		memcpy(bin.lpb, &tmp4, sizeof(tmp4));
		tmp4 = cpu_to_le32(lpsRowItem->ulOrderId);
		memcpy(bin.lpb + sizeof(tmp4), &tmp4, sizeof(tmp4));
	}
	
	switch(ulTableEvent) {
	case TABLE_ROW_ADDED:
	case TABLE_ROW_MODIFIED:
		if (lpsRowItem == nullptr)
			return MAPI_E_INVALID_PARAMETER;
		sRowList.emplace_back(*lpsRowItem);
		hr = QueryRowData(&sRowList, &~lpRows);
		if(hr != hrSuccess)
			return hr;
		tab.row.cValues = lpRows[0].cValues;
		tab.row.lpProps = lpRows[0].lpProps;
		break;
	default:
		break;// no row needed
	}

	// Push the notifications
	for (const auto &adv : m_mapAdvise)
		//FIXME: maybe thought the MAPISupport ?
		adv.second->lpAdviseSink->OnNotify(1, lpNotification);
	return hrSuccess;
}

HRESULT ECMemTableView::GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType)
{
	*lpulTableStatus = TBLSTAT_COMPLETE;
	*lpulTableType = TBLTYPE_DYNAMIC;
	return hrSuccess;
}

HRESULT ECMemTableView::SetColumns(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags)
{
	if (MAPIAllocateBuffer(CbNewSPropTagArray(lpPropTagArray->cValues), &~lpsPropTags) != hrSuccess)
		throw std::bad_alloc();
	lpsPropTags->cValues = lpPropTagArray->cValues;
	memcpy(&lpsPropTags->aulPropTag, &lpPropTagArray->aulPropTag, lpPropTagArray->cValues * sizeof(ULONG));

	Notify(TABLE_SETCOL_DONE, NULL, NULL);
	return hrSuccess;
}

HRESULT ECMemTableView::QueryColumns(ULONG ulFlags, LPSPropTagArray *lppPropTagArray)
{
	LPSPropTagArray lpsPropTagArray = NULL;
	std::list<ULONG> lstTags;

	if ((ulFlags & ~TBL_ALL_COLUMNS) != 0)
		return MAPI_E_UNKNOWN_FLAGS;

	if(ulFlags & TBL_ALL_COLUMNS) {
		FixStringType fix(m_ulFlags);

		// All columns is an aggregate of .. 1. Our column list of columns that we always support and 2. 
		// any other columns in our data set that we can find

		// Our standard set
		for (unsigned int i = 0; i < lpMemTable->lpsColumns->cValues; ++i)
			// Return the string tags based on m_ulFlags (passed when the ECMemTable was created).
			lstTags.emplace_back(fix(lpMemTable->lpsColumns->aulPropTag[i]));

		// All other property tags of all rows
		for (const auto &rowp : lpMemTable->mapRows)
			for (unsigned int i = 0; i < rowp.second.cValues; ++i)
				if (PROP_TYPE(rowp.second.lpsPropVal[i].ulPropTag) != PT_ERROR &&
				    PROP_TYPE(rowp.second.lpsPropVal[i].ulPropTag) != PT_NULL)
					// Return the string tags based on m_ulFlags (passed when the ECMemTable was created).
					lstTags.emplace_back(fix(rowp.second.lpsPropVal[i].ulPropTag));

		// Remove doubles
		lstTags.sort();
		lstTags.unique();
		auto hr = MAPIAllocateBuffer(CbNewSPropTagArray(lstTags.size()), reinterpret_cast<void **>(&lpsPropTagArray));
		if(hr != hrSuccess)
			return hr;
		lpsPropTagArray->cValues = lstTags.size();
		unsigned int i = 0;
		for (auto tag : lstTags)
			lpsPropTagArray->aulPropTag[i++] = tag;
	} else if (lpsPropTags != nullptr) {
		auto hr = MAPIAllocateBuffer(CbNewSPropTagArray(lpsPropTags->cValues), reinterpret_cast<void **>(&lpsPropTagArray));
		if(hr != hrSuccess)
			return hr;
		lpsPropTagArray->cValues = lpsPropTags->cValues;
		memcpy(&lpsPropTagArray->aulPropTag, &lpsPropTags->aulPropTag, sizeof(ULONG) * lpsPropTags->cValues);
	} else {
		return MAPI_E_NOT_FOUND;
	}

	*lppPropTagArray = lpsPropTagArray;
	return hrSuccess;
}

HRESULT ECMemTableView::GetRowCount(ULONG ulFlags, ULONG *lpulCount)
{
	unsigned int ulCount, ulCurrentRow;
	if(lpulCount == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto er = lpKeyTable.GetRowCount(&ulCount, &ulCurrentRow);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	*lpulCount = ulCount;
	return hrSuccess;
}

HRESULT ECMemTableView::SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought)
{
	int lRowsSought;
	auto er = lpKeyTable.SeekRow(static_cast<unsigned int>(bkOrigin),
	              lRowCount, &lRowsSought);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	if(lplRowsSought)
		*lplRowsSought = lRowsSought;
	return hrSuccess;
}

HRESULT ECMemTableView::SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator)
{
	unsigned int ulRows = 0, ulCurrentRow = 0;
	auto er = lpKeyTable.GetRowCount(&ulRows, &ulCurrentRow);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	return SeekRow(BOOKMARK_BEGINNING, static_cast<ULONG>(static_cast<double>(ulRows) * (static_cast<double>(ulNumerator) / ulDenominator)), NULL);
}

HRESULT ECMemTableView::QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator)
{
	unsigned int ulRows = 0, ulCurrentRow = 0;
	if (lpulRow == NULL || lpulNumerator == NULL || lpulDenominator == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto er = lpKeyTable.GetRowCount(&ulRows, &ulCurrentRow);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	*lpulRow = ulCurrentRow;
	*lpulNumerator = ulCurrentRow;
	*lpulDenominator = ulRows;
	return hrSuccess;
}

HRESULT ECMemTableView::FindRow(const SRestriction *lpRestriction,
    BOOKMARK bkOrigin, ULONG ulFlags)
{
	ECRESULT er;
	ECObjectTableList sRowList;
	sObjectTableKey sRowItem;

	if (lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if(	lpRestriction->rt == RES_PROPERTY && 
		lpRestriction->res.resProperty.lpProp->ulPropTag == lpMemTable->ulRowPropTag &&
		bkOrigin == BOOKMARK_BEGINNING) 
	{
		sRowItem.ulObjId = lpRestriction->res.resContent.lpProp->Value.ul;
		sRowItem.ulOrderId = 0; // FIXME:mvprops ?
		return kcerr_to_mapierr(lpKeyTable.SeekId(&sRowItem));
	}
	if (bkOrigin == BOOKMARK_END && ulFlags & DIR_BACKWARD)
		// Loop through the rows
		er = SeekRow(bkOrigin, -1, NULL);
	else
		er = SeekRow(bkOrigin, 0, NULL);
	auto hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;

	// Loop through the rows until one matches.
	while(1) {
		er = lpKeyTable.QueryRows(1, &sRowList, ulFlags & DIR_BACKWARD, 0);
		hr = kcerr_to_mapierr(er);
		if(hr != hrSuccess)
			return hr;
		if (sRowList.empty())
			return MAPI_E_NOT_FOUND;
		if(TestRestriction(lpRestriction, 
		    lpMemTable->mapRows[sRowList.front().ulObjId].cValues,
		    lpMemTable->mapRows[sRowList.front().ulObjId].lpsPropVal,
		    m_locale) == hrSuccess) {
			if (ulFlags & DIR_BACKWARD)
				er = SeekRow(BOOKMARK_CURRENT, 1, NULL);
			else
				er = SeekRow(BOOKMARK_CURRENT, -1, NULL);
			hr = kcerr_to_mapierr(er);
			break;
		}
		sRowList.clear();
	}
	return hr;
}

HRESULT ECMemTableView::Restrict(const SRestriction *lpRestriction, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	if(lpRestriction)
		hr = Util::HrCopySRestriction(&~lpsRestriction, lpRestriction);
	else
		lpsRestriction.reset();
	if(hr != hrSuccess)
		return hr;
	hr = UpdateSortOrRestrict();
	if (hr == hrSuccess)
		Notify(TABLE_RESTRICT_DONE, NULL, NULL);
	return hr;
}

HRESULT ECMemTableView::CreateBookmark(BOOKMARK* lpbkPosition)
{
	unsigned int bkPosition = 0;

	if (lpbkPosition == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto er = lpKeyTable.CreateBookmark(&bkPosition);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	*lpbkPosition = bkPosition;
	return hrSuccess;
}

HRESULT ECMemTableView::FreeBookmark(BOOKMARK bkPosition)
{
	return kcerr_to_mapierr(lpKeyTable.FreeBookmark(static_cast<unsigned int>(bkPosition)));
}

// This is the client version of ECGenericObjectTable::GetBinarySortKey()
HRESULT ECMemTableView::GetBinarySortKey(const SPropValue *lpsPropVal, ECSortCol &sc)
{
#define R(x) reinterpret_cast<const char *>(x)
	switch(PROP_TYPE(lpsPropVal->ulPropTag)) {
	case PT_BOOLEAN:
	case PT_I2: {
		unsigned short tmp = htons(lpsPropVal->Value.b);
		sc.key.assign(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_LONG: {
		unsigned int tmp = htonl(lpsPropVal->Value.ul);
		sc.key.assign(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_R4: {
		double tmp = lpsPropVal->Value.flt;
		sc.key.assign(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_DOUBLE:
		sc.key.assign(R(&lpsPropVal->Value.dbl), sizeof(double));
		break;
	case PT_APPTIME:
		sc.key.assign(R(&lpsPropVal->Value.at), sizeof(double));
		break;
	case PT_CURRENCY:// FIXME: unsortable
		sc.isnull = true;
		break;
	case PT_SYSTIME: {
		unsigned int tmp = htonl(lpsPropVal->Value.ft.dwHighDateTime);
		sc.key.reserve(sizeof(tmp) * 2);
		sc.key.assign(R(&tmp), sizeof(tmp));
		tmp = htonl(lpsPropVal->Value.ft.dwLowDateTime);
		sc.key.append(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_I8: {
		unsigned int tmp = htonl(static_cast<unsigned int>(lpsPropVal->Value.li.QuadPart >> 32));
		sc.key.reserve(sizeof(tmp) * 2);
		sc.key.assign(R(&tmp), sizeof(tmp));
		tmp = htonl(static_cast<unsigned int>(lpsPropVal->Value.li.QuadPart));
		sc.key.append(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_STRING8: 
	case PT_UNICODE:
		if (!lpsPropVal->Value.lpszA) {	// Checks both lpszA and lpszW
			sc.key.clear();
			sc.isnull = true;
			break;
		}
		if (PROP_TYPE(lpsPropVal->ulPropTag) == PT_STRING8)
			sc.key = createSortKeyData(lpsPropVal->Value.lpszA, 255, m_locale);
		else
			sc.key = createSortKeyData(lpsPropVal->Value.lpszW, 255, m_locale);
		break;
	case PT_CLSID:
	case PT_BINARY:
		sc.key.assign(R(lpsPropVal->Value.bin.lpb), lpsPropVal->Value.bin.cb);
		break;
	case PT_ERROR:
		sc.isnull = true;
		break;
	default:
		return MAPI_E_INVALID_TYPE;
	}
	return hrSuccess;
#undef R
}

HRESULT ECMemTableView::SortTable(const SSortOrderSet *lpSortCriteria,
    ULONG ulFlags)
{
	if (!lpSortCriteria)
		lpSortCriteria = sSortDefault;
	auto hr = MAPIAllocateBuffer(CbSSortOrderSet(lpSortCriteria), &~lpsSortOrderSet);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpsSortOrderSet.get(), lpSortCriteria, CbSSortOrderSet(lpSortCriteria));
	hr = UpdateSortOrRestrict();
	if (hr == hrSuccess)
		Notify(TABLE_SORT_DONE, NULL, NULL);

	return hr;
}

HRESULT ECMemTableView::UpdateSortOrRestrict() {
	sObjectTableKey sRowItem;

	// Clear the keytable
	lpKeyTable.Clear();

	// Add the columns into the keytable, which does the actual sorting, etc.
	for (const auto &recip : lpMemTable->mapRows) {
		if (recip.second.fDeleted)
			continue;
		sRowItem.ulObjId = recip.first;
		sRowItem.ulOrderId = 0;
		ModifyRowKey(&sRowItem, NULL, NULL);
	}

	// Seek to start of table (FIXME should this seek to the previously selected row ?)
	lpKeyTable.SeekRow(ECKeyTable::EC_SEEK_SET, 0, nullptr);
	return hrSuccess;
}

HRESULT ECMemTableView::ModifyRowKey(sObjectTableKey *lpsRowItem, sObjectTableKey* lpsPrevRow, ULONG *lpulAction)
{
	// FIXME: mvprops?
	// now is only ulObjId used from lpsRowItem
	if (lpsRowItem == NULL)
		return MAPI_E_INVALID_PARAMETER;

	auto iterData = lpMemTable->mapRows.find(lpsRowItem->ulObjId);
	if (iterData == lpMemTable->mapRows.cend())
		return MAPI_E_NOT_FOUND;

	// Check if there is a restriction in place, and if so, apply it
	if (lpsRestriction != nullptr &&
	    TestRestriction(lpsRestriction, iterData->second.cValues, iterData->second.lpsPropVal, m_locale) != hrSuccess) {
		// no match
		// Remove the row, ignore error
		lpKeyTable.UpdateRow(ECKeyTable::TABLE_ROW_DELETE, lpsRowItem, {}, lpsPrevRow, false, reinterpret_cast<ECKeyTable::UpdateType *>(lpulAction));
		return hrSuccess;
	}

	if (lpsSortOrderSet == nullptr)
		return hrSuccess;

	// Get all the sort columns and package them as binary keys
	std::vector<ECSortCol> sortcols(lpsSortOrderSet->cSorts);
	for (unsigned int j = 0; j < lpsSortOrderSet->cSorts; ++j) {
		auto lpsSortID = PCpropFindProp(iterData->second.lpsPropVal, iterData->second.cValues, lpsSortOrderSet->aSort[j].ulPropTag);
		if (lpsSortID == nullptr || GetBinarySortKey(lpsSortID, sortcols[j]) != hrSuccess)
			continue;
		// Mark as descending if required
		if(lpsSortOrderSet->aSort[j].ulOrder == TABLE_SORT_DESCEND)
			sortcols[j].flags |= TABLEROW_FLAG_DESC;
	}
	lpKeyTable.UpdateRow(ECKeyTable::TABLE_ROW_ADD, lpsRowItem,
		std::move(sortcols), lpsPrevRow, false,
		reinterpret_cast<ECKeyTable::UpdateType *>(lpulAction));
	return hrSuccess;
}

HRESULT ECMemTableView::QuerySortOrder(LPSSortOrderSet *lppSortCriteria)
{
	LPSSortOrderSet lpSortCriteria = NULL;
	auto hr = KAllocCopy(lpsSortOrderSet.get(), CbSSortOrderSet(lpsSortOrderSet.get()), reinterpret_cast<void **>(&lpSortCriteria));
	if(hr != hrSuccess)
		return hr;
	*lppSortCriteria = lpSortCriteria;
	return hrSuccess;
}

HRESULT ECMemTableView::Abort()
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMemTableView::ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet * lppRows, ULONG *lpulMoreRows)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMemTableView::CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMemTableView::WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG *lpulTableStatus)
{
	*lpulTableStatus = S_OK;
	return hrSuccess;
}

HRESULT ECMemTableView::GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMemTableView::SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMemTableView::QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet *lppRows)
{
	ECObjectTableList	sRowList;
	auto hr = kcerr_to_mapierr(lpKeyTable.QueryRows(lRowCount, &sRowList, false, ulFlags));
	if(hr != hrSuccess)
		return hr;
	return QueryRowData(&sRowList, lppRows);
}

HRESULT ECMemTableView::QueryRowData(const ECObjectTableList *lpsRowList,
    SRowSet **lppRows)
{
	rowset_ptr lpRows;
	convert_context converter;

	if (lpsRowList == NULL || lppRows == NULL) {
		assert(false);
		return MAPI_E_INVALID_PARAMETER;
	}

	// We have the rows we need, just copy them into a new rowset
	auto hr = MAPIAllocateBuffer(CbNewSRowSet(lpsRowList->size()), &~lpRows);
	if(hr != hrSuccess)
		return hr;
	lpRows->cRows = 0;

	// Copy the rows into the rowset
	unsigned int i = 0, j = 0;
	for (auto rowlist : *lpsRowList) {
		auto iterRows = lpMemTable->mapRows.find(rowlist.ulObjId);
		if (iterRows == lpMemTable->mapRows.cend())
			// FIXME this could happen during multi-threading
			return MAPI_E_NOT_FOUND;

		lpRows->aRow[i].cValues = lpsPropTags->cValues;
		lpRows->aRow[i].ulAdrEntryPad = 0;
		hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpsPropTags->cValues, reinterpret_cast<void **>(&lpRows->aRow[i].lpProps));
		if(hr != hrSuccess)
			return hr;
		++lpRows->cRows;

		for (j = 0; j < lpsPropTags->cValues; ++j) {
			auto &prop = lpRows[i].lpProps[j];
			// Handle some fixed properties
			if(lpsPropTags->aulPropTag[j] == lpMemTable->ulRowPropTag) {
				prop.ulPropTag = lpMemTable->ulRowPropTag;
				//FIXME: now only support 32 bits of the PT_I8
				if(PROP_TYPE(lpMemTable->ulRowPropTag) == PT_I8)
					prop.Value.li.QuadPart = 0; //empty memory
				prop.Value.ul = rowlist.ulObjId;
				continue;
			}
			if (PROP_TYPE(lpsPropTags->aulPropTag[j]) == PT_NULL) {
				prop.Value.ul = 0;
				prop.ulPropTag = lpsPropTags->aulPropTag[j];
				continue;
			} else if(lpsPropTags->aulPropTag[j] == PR_INSTANCE_KEY) {
				prop.ulPropTag = PR_INSTANCE_KEY;
				prop.Value.bin.cb = sizeof(ULONG)*2;
				hr = MAPIAllocateMore(prop.Value.bin.cb, lpRows[i].lpProps, reinterpret_cast<void **>(&prop.Value.bin.lpb));
				if(hr != hrSuccess)
					return hr;
				uint32_t tmp4 = cpu_to_le32(rowlist.ulObjId);
				memcpy(prop.Value.bin.lpb, &tmp4, sizeof(tmp4));
				tmp4 = cpu_to_le32(rowlist.ulOrderId);
				memcpy(prop.Value.bin.lpb + sizeof(tmp4), &tmp4, sizeof(tmp4));
				continue;
			}
			// Find the property in the data (locate the property by property ID, since we may need conversion)
			auto lpsProp = PCpropFindProp(iterRows->second.lpsPropVal, iterRows->second.cValues, CHANGE_PROP_TYPE(lpsPropTags->aulPropTag[j], PT_UNSPECIFIED));
			if (lpsProp == nullptr) {
				/* Not found */
				prop.ulPropTag = CHANGE_PROP_TYPE(lpsPropTags->aulPropTag[j], PT_ERROR);
				prop.Value.err = MAPI_E_NOT_FOUND;
				continue;
			}
			if (PROP_TYPE(lpsPropTags->aulPropTag[j]) == PT_UNICODE && PROP_TYPE(lpsProp->ulPropTag) == PT_STRING8) {
				// PT_UNICODE requested, and PT_STRING8 provided. Do conversion.
				prop.ulPropTag = CHANGE_PROP_TYPE(lpsPropTags->aulPropTag[j], PT_UNICODE);
				const auto strTmp = converter.convert_to<std::wstring>(lpsProp->Value.lpszA);
				hr = KAllocCopy(strTmp.c_str(), (strTmp.size() + 1) * sizeof(std::wstring::value_type), reinterpret_cast<void **>(&prop.Value.lpszW), lpRows[i].lpProps);
				if (hr != hrSuccess)
					return hr;
				continue; // Finished with this property
			} else if (PROP_TYPE(lpsPropTags->aulPropTag[j]) == PT_STRING8 && PROP_TYPE(lpsProp->ulPropTag) == PT_UNICODE) {
				// PT_STRING8 requested, and PT_UNICODE provided. Do conversion.
				prop.ulPropTag = CHANGE_PROP_TYPE(lpsPropTags->aulPropTag[j], PT_STRING8);
				const auto strTmp = converter.convert_to<std::string>(lpsProp->Value.lpszW);
				hr = KAllocCopy(strTmp.c_str(), strTmp.size() + 1, reinterpret_cast<void **>(&prop.Value.lpszA), lpRows[i].lpProps);
				if (hr != hrSuccess)
					return hr;
				continue; // Finished with this property
			} else if (lpsPropTags->aulPropTag[j] == lpsProp->ulPropTag) {
				// Exact property requested that we have
				hr = Util::HrCopyProperty(&prop, lpsProp, lpRows[i].lpProps);
				if (hr != hrSuccess) {
					prop.ulPropTag = CHANGE_PROP_TYPE(lpsPropTags->aulPropTag[j], PT_ERROR);
					prop.Value.err = MAPI_E_NOT_FOUND;
					hr = hrSuccess; // ignore this error for the return code
				}
				continue;
			}
			// Not found
			prop.ulPropTag = CHANGE_PROP_TYPE(lpsPropTags->aulPropTag[j], PT_ERROR);
			prop.Value.err = MAPI_E_NOT_FOUND;
		}
		++i;
	}
	*lppRows = lpRows.release();
	return hr;
}

HRESULT ECMemTableView::UpdateRow(ULONG ulUpdateType, ULONG ulId)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = hrSuccess;
	sObjectTableKey sRowItem, sPrevRow;
	ULONG ulTableEvent = 0;

	sRowItem.ulObjId = ulId;
	sRowItem.ulOrderId = 0;

	// Optimisation: no sort columns, no restriction, don't need to query the DB
	if (((lpsSortOrderSet == nullptr || lpsSortOrderSet->cSorts == 0) && lpsRestriction == nullptr) ||
		ulUpdateType == ECKeyTable::TABLE_ROW_DELETE)
	{
		er = lpKeyTable.UpdateRow(static_cast<ECKeyTable::UpdateType>(ulUpdateType), &sRowItem, {}, &sPrevRow, false, reinterpret_cast<ECKeyTable::UpdateType *>(&ulTableEvent));
		hr = kcerr_to_mapierr(er);
	} else {
		hr = ModifyRowKey(&sRowItem, &sPrevRow, &ulTableEvent);
	}

	if (hr == hrSuccess)
		Notify(ulTableEvent, &sRowItem, &sPrevRow);

	return hr;
}

HRESULT ECMemTableView::Clear()
{
	auto hr = kcerr_to_mapierr(lpKeyTable.Clear());
	// FIXME: Outlook gives a TABLE_ROW_DELETE or TABLE_CHANGE?
	if (hr == hrSuccess)
		Notify(TABLE_CHANGED, NULL, NULL);

	return hr;
}

} /* namespace */
