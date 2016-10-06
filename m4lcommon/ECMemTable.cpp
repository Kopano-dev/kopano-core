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

#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <kopano/lockhelper.hpp>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiutil.h>

#include <kopano/ECMemTable.h>
#include <kopano/ECKeyTable.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <kopano/Trace.h>
#include <kopano/CommonUtil.h>
#include <kopano/charset/convert.h>

#include <kopano/kcodes.h>

#include <kopano/ECDebug.h>
#include <algorithm>
// needed for htons()
#include <netdb.h>

using namespace std;

static SSortOrderSet sSortDefault = {0, 0};

class FixStringType _zcp_final {
public:
	FixStringType(ULONG ulFlags) : m_ulFlags(ulFlags) { assert((m_ulFlags & ~MAPI_UNICODE) == 0); }
	ULONG operator()(ULONG ulPropTag) const
	{
		if ((PROP_TYPE(ulPropTag) & 0x0ffe) == 0x1e) 	// Any string type
			return CHANGE_PROP_TYPE(ulPropTag, (((m_ulFlags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8) | (PROP_TYPE(ulPropTag) & MVI_FLAG)));
		else
			return ulPropTag;
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

ECMemTable::ECMemTable(SPropTagArray *lpsPropTags, ULONG ulRowPropTag) : ECUnknown("ECMemTable")
{
	this->lpsColumns = (LPSPropTagArray) new BYTE[CbSPropTagArray(lpsPropTags)];
	this->lpsColumns->cValues = lpsPropTags->cValues;
	memcpy(&this->lpsColumns->aulPropTag, &lpsPropTags->aulPropTag, lpsPropTags->cValues * sizeof(ULONG));
	this->ulRowPropTag = ulRowPropTag;
}

ECMemTable::~ECMemTable()
{
	HrClear();
	delete[] this->lpsColumns;
}

HRESULT ECMemTable::Create(LPSPropTagArray lpsColumns, ULONG ulRowPropTag, ECMemTable **lppECMemTable)
{
	ECMemTable *lpMemTable = NULL;
	
	if(PROP_TYPE(ulRowPropTag) != PT_I8 && PROP_TYPE(ulRowPropTag) != PT_LONG)
	{
		assert(false);
		return MAPI_E_INVALID_TYPE;
	}

	lpMemTable = new ECMemTable(lpsColumns, ulRowPropTag);
	return lpMemTable->QueryInterface(IID_ECMemTable,
	       reinterpret_cast<void **>(lppECMemTable));
}

HRESULT ECMemTable::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECMemTable, this);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

// Get all rowa tables in the table with status type
HRESULT ECMemTable::HrGetAllWithStatus(LPSRowSet *lppRowSet, LPSPropValue *lppIDs, LPULONG *lppulStatus)
{
	HRESULT hr = hrSuccess;
	LPSRowSet lpRowSet = NULL;
	LPSPropValue lpIDs = NULL;
	LPULONG lpulStatus = NULL;
	int n = 0;
	ulock_rec l_data(m_hDataMutex);

	hr = MAPIAllocateBuffer(CbNewSRowSet(mapRows.size()), (void **) &lpRowSet);
	if(hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(SPropValue) * mapRows.size(), (void **) &lpIDs);
	if(hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(ULONG) * mapRows.size(), (void **) &lpulStatus);
	if(hr != hrSuccess)
		goto exit;

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
			goto exit;

		if (rowp.second.lpsID != NULL) {
			hr = Util::HrCopyProperty(&lpIDs[n], rowp.second.lpsID, lpIDs);
			if(hr != hrSuccess)
				goto exit;

		} else {
			lpIDs[n].Value.bin.cb = 0;
			lpIDs[n].Value.bin.lpb = NULL;
		}
		++n;
	}
	lpRowSet->cRows = n;

	*lppRowSet = lpRowSet;
	*lppIDs = lpIDs;
	*lppulStatus = lpulStatus;

exit:
	l_data.unlock();
	if (hr != hrSuccess) {
		MAPIFreeBuffer(lpRowSet);
		MAPIFreeBuffer(lpIDs);
		MAPIFreeBuffer(lpulStatus);
	}
	return hr;
}

HRESULT ECMemTable::HrGetRowID(LPSPropValue lpRow, LPSPropValue *lppID)
{

	std::map<unsigned int, ECTableEntry>::const_iterator iterRows;
	scoped_rlock l_data(m_hDataMutex);

	if (lpRow->ulPropTag != this->ulRowPropTag)
		return MAPI_E_INVALID_PARAMETER;
	iterRows = mapRows.find(lpRow->Value.ul);
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
	HRESULT hr = hrSuccess;
	ULONG cValues = 0;
	LPSPropValue lpRowData = NULL;
	std::map<unsigned int, ECTableEntry>::const_iterator iterRows;
	ulock_rec l_data(m_hDataMutex);

	if(lpRow->ulPropTag != this->ulRowPropTag) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	iterRows = mapRows.find(lpRow->Value.ul);
	if (iterRows == mapRows.cend() || iterRows->second.lpsID == NULL) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	hr = Util::HrCopyPropertyArray(iterRows->second.lpsPropVal, iterRows->second.cValues, &lpRowData, &cValues);
	if(hr != hrSuccess)
		goto exit;

	*lpcValues = cValues;

	*lppRowData = lpRowData;
	lpRowData = NULL;

exit:
	l_data.unlock();
	MAPIFreeBuffer(lpRowData);
	return hr;
}

// This function is called to reset the add/modify/delete flags of all
// the rows. This is normally called after saving the data from the HrGet* functions
// to the storage.
HRESULT ECMemTable::HrSetClean()
{
	HRESULT hr = hrSuccess;

	map<unsigned int, ECTableEntry>::iterator iterRows;
	map<unsigned int, ECTableEntry>::iterator iterNext;
	scoped_rlock l_data(m_hDataMutex);

	for (iterRows = mapRows.begin(); iterRows != mapRows.end(); iterRows = iterNext) {
		iterNext = iterRows;
		++iterNext;

		if(iterRows->second.fDeleted) {
			MAPIFreeBuffer(iterRows->second.lpsID);
			MAPIFreeBuffer(iterRows->second.lpsPropVal);
			mapRows.erase(iterRows);
		} else {
			iterRows->second.fDeleted = false;
			iterRows->second.fDirty = false;
			iterRows->second.fNew = false;
		}
	}
	return hr;
}

HRESULT ECMemTable::HrUpdateRowID(LPSPropValue lpId, LPSPropValue lpProps, ULONG cValues)
{
    LPSPropValue lpUniqueProp = NULL;
	scoped_rlock l_data(m_hDataMutex);

	lpUniqueProp = PpropFindProp(lpProps, cValues, ulRowPropTag);
	if (lpUniqueProp == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto iterRows = mapRows.find(lpUniqueProp->Value.ul);
	if (iterRows == mapRows.cend())
		return MAPI_E_NOT_FOUND;
    
    MAPIFreeBuffer(iterRows->second.lpsID);
	HRESULT hr = MAPIAllocateBuffer(sizeof(SPropValue),
		reinterpret_cast<void **>(&iterRows->second.lpsID));
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

HRESULT ECMemTable::HrModifyRow(ULONG ulUpdateType, SPropValue *lpsID, SPropValue *lpPropVals, ULONG cValues)
{
	HRESULT hr = hrSuccess;
	ECTableEntry entry;
	LPSPropValue lpsRowID = NULL;
	std::map<unsigned int, ECTableEntry>::iterator iterRows;
	scoped_rlock l_data(m_hDataMutex);

	lpsRowID = PpropFindProp(lpPropVals, cValues, ulRowPropTag);
	if (lpsRowID == NULL)
		// you must specify a row number
		return MAPI_E_INVALID_PARAMETER;

	iterRows = mapRows.find(lpsRowID->Value.ul);
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
			// free the old row.

			LPSPropValue lpOldPropVal = iterRows->second.lpsPropVal;

			// Update new row
			hr = Util::HrCopyPropertyArray(lpPropVals, cValues, &iterRows->second.lpsPropVal, &iterRows->second.cValues, /* exclude PT_ERRORs */ true);
			
			if(hr != hrSuccess)
				return hr;

			// Free old row
			MAPIFreeBuffer(lpOldPropVal);
		}
	}

	if(ulUpdateType == ECKeyTable::TABLE_ROW_ADD) {

		hr = Util::HrCopyPropertyArray(lpPropVals, cValues, &entry.lpsPropVal, &entry.cValues);
		if(hr != hrSuccess)
			return hr;

		entry.fDeleted = FALSE;
		entry.fDirty = TRUE;
		entry.fNew = TRUE;
		
		if(lpsID) {
			hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&entry.lpsID);
			if(hr != hrSuccess)
				return hr;
			hr = Util::HrCopyProperty(entry.lpsID, lpsID, entry.lpsID);
			if(hr != hrSuccess)
				return hr;
		} else {
			entry.lpsID = NULL;
		}

		// Add the actual data
		mapRows[lpsRowID->Value.ul] =  entry;
	}

	for (auto viewp : lstViews) {
		hr = viewp->UpdateRow(ulUpdateType, lpsRowID->Value.ul);
		if(hr != hrSuccess)
			return hr;
	}
	return hr;
}

HRESULT ECMemTable::HrGetView(const ECLocale &locale, ULONG ulFlags, ECMemTableView **lppView)
{
	ECMemTableView *lpView = NULL;
	scoped_rlock l_data(m_hDataMutex);

	HRESULT hr = ECMemTableView::Create(this, locale, ulFlags, &lpView);
	if (hr != hrSuccess)
		return hr;

	lstViews.push_back(lpView);
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
	for (const auto &rowp : mapRows) {
		MAPIFreeBuffer(rowp.second.lpsPropVal);
		MAPIFreeBuffer(rowp.second.lpsID);
	}

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

ECMemTableView::ECMemTableView(ECMemTable *lpMemTable, const ECLocale &locale, ULONG ulFlags) : ECUnknown("ECMemTableView")
{
	this->lpsSortOrderSet = NULL;
	this->lpsRestriction = NULL;

	this->lpKeyTable = new ECKeyTable();

	this->lpMemTable = lpMemTable;

	this->lpsPropTags = (LPSPropTagArray) new BYTE[CbNewSPropTagArray(lpMemTable->lpsColumns->cValues)];

	lpsPropTags->cValues = lpMemTable->lpsColumns->cValues;
	std::transform(lpMemTable->lpsColumns->aulPropTag, lpMemTable->lpsColumns->aulPropTag + lpMemTable->lpsColumns->cValues, (ULONG*)lpsPropTags->aulPropTag, FixStringType(ulFlags & MAPI_UNICODE));

	SortTable(&sSortDefault, 0);

	m_ulConnection = 1;
	m_ulFlags = ulFlags & MAPI_UNICODE;

	m_locale = locale;
}

ECMemTableView::~ECMemTableView()
{
	ECMapMemAdvise::const_iterator iterAdvise, iterAdviseRemove;

	// Remove ourselves from the parent's view list
	for (auto iterViews = lpMemTable->lstViews.begin();
	     iterViews != lpMemTable->lstViews.cend(); ++iterViews)
		if(*iterViews == this) {
			lpMemTable->lstViews.erase(iterViews);
			break;
		}

	// Remove advises
	iterAdvise = m_mapAdvise.cbegin();
	while (iterAdvise != m_mapAdvise.cend()) {
		iterAdviseRemove = iterAdvise;
		++iterAdvise;
		Unadvise(iterAdviseRemove->first);
	}

	delete[] this->lpsPropTags;
	delete[] lpsSortOrderSet;
	delete lpKeyTable;
	MAPIFreeBuffer(lpsRestriction);
}

HRESULT ECMemTableView::Create(ECMemTable *lpMemTable, const ECLocale &locale, ULONG ulFlags, ECMemTableView **lppMemTableView)
{
	HRESULT hr = hrSuccess;

	ECMemTableView *lpView = new ECMemTableView(lpMemTable, locale, ulFlags);

	hr = lpView->QueryInterface(IID_ECMemTableView, (void **) lppMemTableView);

	if(hr != hrSuccess)
		delete lpView;

	return hr;
}

HRESULT ECMemTableView::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECMemTableView, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IMAPITable, &this->m_xMAPITable);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xMAPITable);
	
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMemTableView::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMemTableView::Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG * lpulConnection)
{
	ECMEMADVISE *lpMemAdvise = NULL;
	ULONG ulConnection = m_ulConnection++;

	if (lpAdviseSink == NULL || lpulConnection == NULL)
		return MAPI_E_INVALID_PARAMETER;
	lpAdviseSink->AddRef();

	lpMemAdvise = new ECMEMADVISE;
	lpMemAdvise->lpAdviseSink = lpAdviseSink;
	lpMemAdvise->ulEventMask = ulEventMask;

	m_mapAdvise.insert( ECMapMemAdvise::value_type( ulConnection, lpMemAdvise) );

	*lpulConnection = ulConnection;
	return hrSuccess;
}

HRESULT ECMemTableView::Unadvise(ULONG ulConnection)
{
	HRESULT hr = hrSuccess;

	// Remove notify from list
	ECMapMemAdvise::const_iterator iterAdvise = m_mapAdvise.find(ulConnection);
	if (iterAdvise != m_mapAdvise.cend()) {
		if (iterAdvise->second->lpAdviseSink != NULL)
			iterAdvise->second->lpAdviseSink->Release();

		delete iterAdvise->second;
		
		m_mapAdvise.erase(iterAdvise);
	} else {
		assert(false);
	}
	
	return hr;
}

HRESULT ECMemTableView::Notify(ULONG ulTableEvent, sObjectTableKey* lpsRowItem, sObjectTableKey* lpsPrevRow)
{
	HRESULT hr = hrSuccess;
	LPNOTIFICATION lpNotification = NULL;
	LPSRowSet lpRows = NULL;
	ECObjectTableList sRowList;

	hr = MAPIAllocateBuffer(sizeof(NOTIFICATION), (void**)&lpNotification);
	if(hr != hrSuccess)
		goto exit;

	memset(lpNotification, 0, sizeof(NOTIFICATION));

	lpNotification->ulEventType = fnevTableModified;
	lpNotification->info.tab.ulTableEvent = ulTableEvent;

	if (lpsPrevRow == NULL || lpsPrevRow->ulObjId == 0) {
		lpNotification->info.tab.propPrior.ulPropTag = PR_NULL;
	} else {
		lpNotification->info.tab.propPrior.ulPropTag = PR_INSTANCE_KEY;

		lpNotification->info.tab.propPrior.Value.bin.cb = sizeof(ULONG)*2;
		hr = MAPIAllocateMore(lpNotification->info.tab.propPrior.Value.bin.cb, lpNotification, (void**)&lpNotification->info.tab.propPrior.Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;

		memcpy(lpNotification->info.tab.propPrior.Value.bin.lpb, &lpsPrevRow->ulObjId, sizeof(ULONG));
		memcpy(lpNotification->info.tab.propPrior.Value.bin.lpb+sizeof(ULONG), &lpsPrevRow->ulOrderId, sizeof(ULONG));

	}

	if (lpsRowItem == NULL || lpsRowItem->ulObjId == 0) {
		lpNotification->info.tab.propIndex.ulPropTag = PR_NULL;
	} else {
		lpNotification->info.tab.propIndex.ulPropTag = PR_INSTANCE_KEY;
		lpNotification->info.tab.propIndex.Value.bin.cb = sizeof(ULONG)*2;
		hr = MAPIAllocateMore(lpNotification->info.tab.propIndex.Value.bin.cb, lpNotification, (void**)&lpNotification->info.tab.propIndex.Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;

		memcpy(lpNotification->info.tab.propIndex.Value.bin.lpb, &lpsRowItem->ulObjId, sizeof(ULONG));
		memcpy(lpNotification->info.tab.propIndex.Value.bin.lpb+sizeof(ULONG), &lpsRowItem->ulOrderId, sizeof(ULONG));
	}
	
	switch(ulTableEvent) {
		case TABLE_ROW_ADDED:
		case TABLE_ROW_MODIFIED:
			if (lpsRowItem == NULL) {
				hr = MAPI_E_INVALID_PARAMETER;
				goto exit;
			}

			sRowList.push_back(*lpsRowItem);

			hr = QueryRowData(&sRowList, &lpRows);
			if(hr != hrSuccess)
				goto exit;

			lpNotification->info.tab.row.cValues = lpRows->aRow[0].cValues;
			lpNotification->info.tab.row.lpProps = lpRows->aRow[0].lpProps;
			break;
		default:
			break;// no row needed
	}

	// Push the notifications
	for (const auto &adv : m_mapAdvise)
		//FIXME: maybe thought the MAPISupport ?
		adv.second->lpAdviseSink->OnNotify(1, lpNotification);
exit:
	MAPIFreeBuffer(lpNotification);
	if (lpRows)
		FreeProws(lpRows);

	return hr;
}

HRESULT ECMemTableView::GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType)
{
	HRESULT hr = hrSuccess;

	*lpulTableStatus = TBLSTAT_COMPLETE;
	*lpulTableType = TBLTYPE_DYNAMIC;

	return hr;
}

HRESULT ECMemTableView::SetColumns(LPSPropTagArray lpPropTagArray, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;

	delete[] this->lpsPropTags;
	lpsPropTags = (LPSPropTagArray) new BYTE[CbNewSPropTagArray(lpPropTagArray->cValues)];

	lpsPropTags->cValues = lpPropTagArray->cValues;
	memcpy(&lpsPropTags->aulPropTag, &lpPropTagArray->aulPropTag, lpPropTagArray->cValues * sizeof(ULONG));

	Notify(TABLE_SETCOL_DONE, NULL, NULL);

	return hr;
}

HRESULT ECMemTableView::QueryColumns(ULONG ulFlags, LPSPropTagArray *lppPropTagArray)
{
	HRESULT hr;
	LPSPropTagArray lpsPropTagArray = NULL;
	std::list<ULONG> lstTags;
	unsigned int i = 0;

	if ((ulFlags & ~TBL_ALL_COLUMNS) != 0)
		return MAPI_E_UNKNOWN_FLAGS;

	if(ulFlags & TBL_ALL_COLUMNS) {
		FixStringType fix(m_ulFlags);

		// All columns is an aggregate of .. 1. Our column list of columns that we always support and 2. 
		// any other columns in our data set that we can find

		// Our standard set
		for (i = 0; i < lpMemTable->lpsColumns->cValues; ++i)
			// Return the string tags based on m_ulFlags (passed when the ECMemTable was created).
			lstTags.push_back(fix(lpMemTable->lpsColumns->aulPropTag[i]));

		// All other property tags of all rows
		for (const auto &rowp : lpMemTable->mapRows)
			for (i = 0; i < rowp.second.cValues; ++i)
				if (PROP_TYPE(rowp.second.lpsPropVal[i].ulPropTag) != PT_ERROR &&
				    PROP_TYPE(rowp.second.lpsPropVal[i].ulPropTag) != PT_NULL)
					// Return the string tags based on m_ulFlags (passed when the ECMemTable was created).
					lstTags.push_back(fix(rowp.second.lpsPropVal[i].ulPropTag));

		// Remove doubles
		lstTags.sort();
		lstTags.unique();
		
		hr = MAPIAllocateBuffer(CbNewSPropTagArray(lstTags.size()), (void **)&lpsPropTagArray);

		if(hr != hrSuccess)
			return hr;
		lpsPropTagArray->cValues = lstTags.size();
		i = 0;
		for (auto tag : lstTags)
			lpsPropTagArray->aulPropTag[i++] = tag;

	} else if(this->lpsPropTags) {
		hr = MAPIAllocateBuffer(CbNewSPropTagArray(lpsPropTags->cValues),(void **)&lpsPropTagArray);

		if(hr != hrSuccess)
			return hr;
		lpsPropTagArray->cValues = this->lpsPropTags->cValues;
		memcpy(&lpsPropTagArray->aulPropTag, &this->lpsPropTags->aulPropTag, sizeof(ULONG) * this->lpsPropTags->cValues);
	} else {
		return MAPI_E_NOT_FOUND;
	}

	*lppPropTagArray = lpsPropTagArray;
	return hrSuccess;
}

HRESULT ECMemTableView::GetRowCount(ULONG ulFlags, ULONG *lpulCount)
{
	unsigned int ulCount;
	unsigned int ulCurrentRow;

	if(lpulCount == NULL)
		return MAPI_E_INVALID_PARAMETER;
	ECRESULT er = this->lpKeyTable->GetRowCount(&ulCount, &ulCurrentRow);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	*lpulCount = ulCount;
	return hrSuccess;
}

HRESULT ECMemTableView::SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought)
{
	int lRowsSought;

	ECRESULT er = this->lpKeyTable->SeekRow(static_cast<unsigned int>(bkOrigin),
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
	unsigned int ulRows = 0;
	unsigned int ulCurrentRow = 0;

	ECRESULT er = lpKeyTable->GetRowCount(&ulRows, &ulCurrentRow);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	return SeekRow(BOOKMARK_BEGINNING, static_cast<ULONG>(static_cast<double>(ulRows) * (static_cast<double>(ulNumerator) / ulDenominator)), NULL);
}

HRESULT ECMemTableView::QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator)
{
	unsigned int ulRows = 0;
	unsigned int ulCurrentRow = 0;

	if (lpulRow == NULL || lpulNumerator == NULL || lpulDenominator == NULL)
		return MAPI_E_INVALID_PARAMETER;
	ECRESULT er = lpKeyTable->GetRowCount(&ulRows, &ulCurrentRow);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	*lpulRow = ulCurrentRow;
	*lpulNumerator = ulCurrentRow;
	*lpulDenominator = ulRows;
	return hrSuccess;
}

HRESULT ECMemTableView::FindRow(LPSRestriction lpRestriction, BOOKMARK bkOrigin, ULONG ulFlags)
{
	HRESULT hr;
	ECRESULT er;
	ECObjectTableList sRowList;
	sObjectTableKey sRowItem;

	if (lpRestriction == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if(	lpRestriction->rt == RES_PROPERTY && 
		lpRestriction->res.resProperty.lpProp->ulPropTag == this->lpMemTable->ulRowPropTag &&
		bkOrigin == BOOKMARK_BEGINNING) 
	{
		sRowItem.ulObjId = lpRestriction->res.resContent.lpProp->Value.ul;
		sRowItem.ulOrderId = 0; // FIXME:mvprops ?
		return kcerr_to_mapierr(this->lpKeyTable->SeekId(&sRowItem));
	}

	if(bkOrigin == BOOKMARK_END && ulFlags & DIR_BACKWARD) {
		// Loop through the rows
		er = SeekRow(bkOrigin, -1, NULL);
	} else {
		er = SeekRow(bkOrigin, 0, NULL);
	}
	hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;

	// Loop through the rows until one matches.
	while(1) {
		er = this->lpKeyTable->QueryRows(1, &sRowList, ulFlags & DIR_BACKWARD, 0);
		hr = kcerr_to_mapierr(er);
		if(hr != hrSuccess)
			return hr;
		if (sRowList.empty())
			return MAPI_E_NOT_FOUND;
		if(TestRestriction(lpRestriction, 
							this->lpMemTable->mapRows[sRowList.begin()->ulObjId].cValues,
							this->lpMemTable->mapRows[sRowList.begin()->ulObjId].lpsPropVal,
							m_locale) == hrSuccess)  {
			if(ulFlags & DIR_BACKWARD) {
				er = SeekRow(BOOKMARK_CURRENT, 1, NULL);
			} else {
				er = SeekRow(BOOKMARK_CURRENT, -1, NULL);
			}
			hr = kcerr_to_mapierr(er);
			break;
		}
		sRowList.clear();
	}
	return hr;
}

HRESULT ECMemTableView::Restrict(LPSRestriction lpRestriction, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;

	MAPIFreeBuffer(this->lpsRestriction);
	this->lpsRestriction = NULL;

	if(lpRestriction)
		hr = Util::HrCopySRestriction(&this->lpsRestriction, lpRestriction);
	else
		this->lpsRestriction = NULL;

	if(hr != hrSuccess)
		return hr;

	hr = this->UpdateSortOrRestrict();

	if (hr == hrSuccess)
		Notify(TABLE_RESTRICT_DONE, NULL, NULL);
	return hr;
}

HRESULT ECMemTableView::CreateBookmark(BOOKMARK* lpbkPosition)
{
	unsigned int bkPosition = 0;

	if (lpbkPosition == NULL)
		return MAPI_E_INVALID_PARAMETER;
	ECRESULT er = this->lpKeyTable->CreateBookmark(&bkPosition);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	*lpbkPosition = bkPosition;
	return hrSuccess;
}

HRESULT ECMemTableView::FreeBookmark(BOOKMARK bkPosition)
{
	ECRESULT er = this->lpKeyTable->FreeBookmark(static_cast<unsigned int>(bkPosition));
	return kcerr_to_mapierr(er);
}

// This is the client version of ECGenericObjectTable::GetBinarySortKey()
HRESULT ECMemTableView::GetBinarySortKey(LPSPropValue lpsPropVal, unsigned int *lpSortLen, unsigned char *lpFlags, unsigned char **lppSortData)
{
	unsigned char	*lpSortData = NULL;
	unsigned int	ulSortLen = 0;
	unsigned char	ulFlags = 0;

	switch(PROP_TYPE(lpsPropVal->ulPropTag)) {
	case PT_BOOLEAN:
	case PT_I2:
		ulSortLen = 2;
		lpSortData = new unsigned char[2];
		*(unsigned short *)lpSortData = htons(lpsPropVal->Value.b);
		break;
	case PT_LONG:
		ulSortLen = 4;
		lpSortData = new unsigned char[4];
		*(unsigned int *)lpSortData = htonl(lpsPropVal->Value.ul);
		break;
	case PT_R4:
		ulSortLen = sizeof(double);
		lpSortData = new unsigned char[sizeof(double)];
		*(double *)lpSortData = lpsPropVal->Value.flt;
		break;
	case PT_DOUBLE:
		ulSortLen = sizeof(double);
		lpSortData = new unsigned char[sizeof(double)];
		*(double *)lpSortData = lpsPropVal->Value.dbl;
		break;
	case PT_APPTIME:
		ulSortLen = sizeof(double);
		lpSortData = new unsigned char[sizeof(double)];
		*(double *)lpSortData = lpsPropVal->Value.at;
		break;
	case PT_CURRENCY:// FIXME: unsortable
		ulSortLen = 0;
		lpSortData = NULL;
		break;
	case PT_SYSTIME:
		ulSortLen = 8;
		lpSortData = new unsigned char[8];
		*(unsigned int *)lpSortData = htonl(lpsPropVal->Value.ft.dwHighDateTime);
		*(unsigned int *)(lpSortData+4) = htonl(lpsPropVal->Value.ft.dwLowDateTime);
		break;
	case PT_I8:
		ulSortLen = 8;
		lpSortData = new unsigned char[8];
		*(unsigned int *)lpSortData = htonl((unsigned int)(lpsPropVal->Value.li.QuadPart >> 32));
		*(unsigned int *)(lpSortData+4) = htonl((unsigned int)lpsPropVal->Value.li.QuadPart);
		break;
	case PT_STRING8: 
	case PT_UNICODE: {
			if (!lpsPropVal->Value.lpszA) {	// Checks both lpszA and lpszW
				ulSortLen = 0;
				lpSortData = NULL;
				break;
			}

			if (PROP_TYPE(lpsPropVal->ulPropTag) == PT_STRING8)
				createSortKeyData(lpsPropVal->Value.lpszA, 255, m_locale, &ulSortLen, &lpSortData);
			else
				createSortKeyData(lpsPropVal->Value.lpszW, 255, m_locale, &ulSortLen, &lpSortData);
		}
		break;
	case PT_CLSID:
	case PT_BINARY:
		ulSortLen = lpsPropVal->Value.bin.cb;
		lpSortData = new unsigned char [ulSortLen];
		memcpy(lpSortData, lpsPropVal->Value.bin.lpb, ulSortLen); // could be optimized to one func
		break;
	case PT_ERROR:
		ulSortLen = 0;
		lpSortData = NULL;
		break;
	default:
		return MAPI_E_INVALID_TYPE;
	}
	*lpSortLen = ulSortLen;
	*lppSortData = lpSortData;
	*lpFlags = ulFlags;
	return hrSuccess;
}

HRESULT ECMemTableView::SortTable(LPSSortOrderSet lpSortCriteria, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;

	if (!lpSortCriteria)
		lpSortCriteria = &sSortDefault;

	delete[] lpsSortOrderSet;
	lpsSortOrderSet = (LPSSortOrderSet) new BYTE[CbSSortOrderSet(lpSortCriteria)];

	memcpy(lpsSortOrderSet, lpSortCriteria, CbSSortOrderSet(lpSortCriteria));

	hr = UpdateSortOrRestrict();

	if (hr == hrSuccess)
		Notify(TABLE_SORT_DONE, NULL, NULL);

	return hr;
}

HRESULT ECMemTableView::UpdateSortOrRestrict() {
	HRESULT hr = hrSuccess;
	sObjectTableKey sRowItem;

	// Clear the keytable
	lpKeyTable->Clear();

	// Add the columns into the keytable, which does the actual sorting, etc.
	for (const auto &recip : lpMemTable->mapRows) {
		if (recip.second.fDeleted)
			continue;
		sRowItem.ulObjId = recip.first;
		sRowItem.ulOrderId = 0;
		ModifyRowKey(&sRowItem, NULL, NULL);
	}

	// Seek to start of table (FIXME should this seek to the previously selected row ?)
	lpKeyTable->SeekRow(ECKeyTable::EC_SEEK_SET, 0 , NULL);

	return hr;
}

HRESULT ECMemTableView::ModifyRowKey(sObjectTableKey *lpsRowItem, sObjectTableKey* lpsPrevRow, ULONG *lpulAction)
{
	HRESULT hr = hrSuccess;
	unsigned int	*lpulSortLen = NULL;
	unsigned char	**lpSortKeys = NULL;
	unsigned char	*lpFlags = NULL;
	LPSPropValue lpsSortID = NULL;
	ULONG j;

	// FIXME: mvprops?
	// now is only ulObjId used from lpsRowItem
	if (lpsRowItem == NULL)
		return MAPI_E_INVALID_PARAMETER;

	auto iterData = lpMemTable->mapRows.find(lpsRowItem->ulObjId);
	if (iterData == lpMemTable->mapRows.cend())
		return MAPI_E_NOT_FOUND;

	if (lpsSortOrderSet && lpsSortOrderSet->cSorts > 0){
		lpulSortLen = new unsigned int [lpsSortOrderSet->cSorts];
		lpFlags = new unsigned char [lpsSortOrderSet->cSorts];
		lpSortKeys = new unsigned char * [lpsSortOrderSet->cSorts];
	}

	// Check if there is a restriction in place, and if so, apply it
	if(this->lpsRestriction) {
		if(TestRestriction(this->lpsRestriction, iterData->second.cValues, iterData->second.lpsPropVal, m_locale) != hrSuccess) {
			// no match
			
			// Remove the row, ignore error
			lpKeyTable->UpdateRow(ECKeyTable::TABLE_ROW_DELETE, lpsRowItem, 0, NULL, NULL, NULL, lpsPrevRow, false, (ECKeyTable::UpdateType*)lpulAction);
			goto exit;
		}
	}

	// Get all the sort columns and package them as binary keys
	for (j = 0; j < lpsSortOrderSet->cSorts; ++j) {
		lpsSortID = PpropFindProp(iterData->second.lpsPropVal, iterData->second.cValues, lpsSortOrderSet->aSort[j].ulPropTag);
		if (lpsSortID == NULL || GetBinarySortKey(lpsSortID, &lpulSortLen[j], &lpFlags[j], &lpSortKeys[j]) != hrSuccess) {
			lpulSortLen[j] = 0;
			lpSortKeys[j] = NULL;
			lpFlags[j] = 0;
			continue;
		}

		// Mark as descending if required
		if(lpsSortOrderSet->aSort[j].ulOrder == TABLE_SORT_DESCEND)
			lpFlags[j] |= TABLEROW_FLAG_DESC;
	}

	lpKeyTable->UpdateRow(ECKeyTable::TABLE_ROW_ADD, lpsRowItem, lpsSortOrderSet->cSorts, lpulSortLen, lpFlags, lpSortKeys, lpsPrevRow, false, (ECKeyTable::UpdateType*)lpulAction);

	// clean up GetBinarySortKey() allocs
	for (j = 0; j < lpsSortOrderSet->cSorts; ++j)
		delete[] lpSortKeys[j];

exit:
	delete[] lpulSortLen;
	delete[] lpSortKeys;
	delete[] lpFlags;
	return hr;
}

HRESULT ECMemTableView::QuerySortOrder(LPSSortOrderSet *lppSortCriteria)
{
	LPSSortOrderSet lpSortCriteria = NULL;

	HRESULT hr = MAPIAllocateBuffer(CbSSortOrderSet(lpsSortOrderSet),
	             reinterpret_cast<void **>(&lpSortCriteria));
	if(hr != hrSuccess)
		return hr;

	memcpy(lpSortCriteria, lpsSortOrderSet, CbSSortOrderSet(lpsSortOrderSet));
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

	HRESULT er = lpKeyTable->QueryRows(lRowCount, &sRowList, false, ulFlags);
	HRESULT hr = kcerr_to_mapierr(er);
	if(hr != hrSuccess)
		return hr;
	return QueryRowData(&sRowList, lppRows);
}

HRESULT ECMemTableView::QueryRowData(ECObjectTableList *lpsRowList, LPSRowSet *lppRows)
{
	HRESULT hr = hrSuccess;

	LPSPropValue lpsProp = NULL;
	unsigned int i=0,j=0;

	LPSRowSet lpRows = NULL;
	convert_context converter;

	if (lpsRowList == NULL || lppRows == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		assert(false);
		goto exit;
	}

	// We have the rows we need, just copy them into a new rowset
	hr = MAPIAllocateBuffer(CbNewSRowSet(lpsRowList->size()), (void **) &lpRows);
	if(hr != hrSuccess)
		goto exit;

	// Copy the rows into the rowset
	i = 0;
	for (auto rowlist : *lpsRowList) {
		auto iterRows = this->lpMemTable->mapRows.find(rowlist.ulObjId);
		if (iterRows == this->lpMemTable->mapRows.cend()) {
			hr = MAPI_E_NOT_FOUND;
			goto exit; // FIXME this could happen during multi-threading
		}

		lpRows->aRow[i].cValues = lpsPropTags->cValues;
		lpRows->aRow[i].ulAdrEntryPad = 0;

		hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpsPropTags->cValues, (void **)&lpRows->aRow[i].lpProps);
		if(hr != hrSuccess)
			goto exit;

		for (j = 0; j < lpsPropTags->cValues; ++j) {
			// Handle some fixed properties
			if(lpsPropTags->aulPropTag[j] == lpMemTable->ulRowPropTag) {
				lpRows->aRow[i].lpProps[j].ulPropTag = lpMemTable->ulRowPropTag;
				
				//FIXME: now only support 32bits of the PT_I8
				if(PROP_TYPE(lpMemTable->ulRowPropTag) == PT_I8)
					lpRows->aRow[i].lpProps[j].Value.li.QuadPart = 0; //empty memory
				
				lpRows->aRow[i].lpProps[j].Value.ul = rowlist.ulObjId;
			} else {
				if(PROP_TYPE(lpsPropTags->aulPropTag[j]) == PT_NULL) {
					lpRows->aRow[i].lpProps[j].Value.ul = 0;
					lpRows->aRow[i].lpProps[j].ulPropTag = lpsPropTags->aulPropTag[j];
				} else if(lpsPropTags->aulPropTag[j] == PR_INSTANCE_KEY) {

					lpRows->aRow[i].lpProps[j].ulPropTag = PR_INSTANCE_KEY;
					lpRows->aRow[i].lpProps[j].Value.bin.cb = sizeof(ULONG)*2;
					hr = MAPIAllocateMore(lpRows->aRow[i].lpProps[j].Value.bin.cb, lpRows->aRow[i].lpProps, (void**)&lpRows->aRow[i].lpProps[j].Value.bin.lpb);
					if(hr != hrSuccess)
						goto exit;

					memcpy(lpRows->aRow[i].lpProps[j].Value.bin.lpb, &rowlist.ulObjId, sizeof(ULONG));
					memcpy(lpRows->aRow[i].lpProps[j].Value.bin.lpb + sizeof(ULONG), &rowlist.ulOrderId, sizeof(ULONG));
				} else {
					// Find the property in the data (locate the property by property ID, since we may need conversion)
					lpsProp = PpropFindProp(iterRows->second.lpsPropVal, iterRows->second.cValues, PROP_TAG(PT_UNSPECIFIED, PROP_ID(lpsPropTags->aulPropTag[j])));

					if(lpsProp) {
						if(PROP_TYPE(lpsPropTags->aulPropTag[j]) == PT_UNICODE && PROP_TYPE(lpsProp->ulPropTag) == PT_STRING8) {
							// PT_UNICODE requested, and PT_STRING8 provided. Do conversion.
							lpRows->aRow[i].lpProps[j].ulPropTag = PROP_TAG(PT_UNICODE, PROP_ID(lpsPropTags->aulPropTag[j]));
							
							const wstring strTmp = converter.convert_to<wstring>(lpsProp->Value.lpszA);
							if ((hr = MAPIAllocateMore((strTmp.size() + 1) * sizeof(wstring::value_type), lpRows->aRow[i].lpProps, (void**)&lpRows->aRow[i].lpProps[j].Value.lpszW)) != hrSuccess)
								goto exit;
							memcpy(lpRows->aRow[i].lpProps[j].Value.lpszW, strTmp.c_str(), (strTmp.size() + 1) * sizeof(wstring::value_type));
							
							continue; // Finished with this property
						} else if (PROP_TYPE(lpsPropTags->aulPropTag[j]) == PT_STRING8 && PROP_TYPE(lpsProp->ulPropTag) == PT_UNICODE) {
							// PT_STRING8 requested, and PT_UNICODE provided. Do conversion.
							lpRows->aRow[i].lpProps[j].ulPropTag = PROP_TAG(PT_STRING8, PROP_ID(lpsPropTags->aulPropTag[j]));

							const string strTmp = converter.convert_to<string>(lpsProp->Value.lpszW);
							if ((hr = MAPIAllocateMore(strTmp.size() + 1, lpRows->aRow[i].lpProps, (void**)&lpRows->aRow[i].lpProps[j].Value.lpszA)) != hrSuccess)
								goto exit;
							memcpy(lpRows->aRow[i].lpProps[j].Value.lpszA, strTmp.c_str(), strTmp.size() + 1);
							
							continue; // Finished with this property
						} else if(lpsPropTags->aulPropTag[j] == lpsProp->ulPropTag) {
							// Exact property requested that we have
							hr = Util::HrCopyProperty(&lpRows->aRow[i].lpProps[j], lpsProp,(void *)lpRows->aRow[i].lpProps);

							if(hr != hrSuccess) {
								lpRows->aRow[i].lpProps[j].ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(lpsPropTags->aulPropTag[j]));
								lpRows->aRow[i].lpProps[j].Value.err = MAPI_E_NOT_FOUND;

								hr = hrSuccess; // ignore this error for the return code
							}
							continue;
						}
					} 

					// Not found
					lpRows->aRow[i].lpProps[j].ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(lpsPropTags->aulPropTag[j]));
					lpRows->aRow[i].lpProps[j].Value.err = MAPI_E_NOT_FOUND;
				}
			}
		}
		++i;
	}

	lpRows->cRows = lpsRowList->size();

	*lppRows = lpRows;

exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(lpRows);

	return hr;
}

HRESULT ECMemTableView::UpdateRow(ULONG ulUpdateType, ULONG ulId)
{
	HRESULT hr = hrSuccess;
	ECRESULT er = hrSuccess;
	sObjectTableKey sRowItem;
	sObjectTableKey sPrevRow;
	ULONG ulTableEvent = 0;

	sRowItem.ulObjId = ulId;
	sRowItem.ulOrderId = 0;

	// Optimisation: no sort columns, no restriction, don't need to query the DB
	if(((this->lpsSortOrderSet == NULL  || this->lpsSortOrderSet->cSorts == 0) && this->lpsRestriction == NULL) ||
		ulUpdateType == ECKeyTable::TABLE_ROW_DELETE)
	{
		er = lpKeyTable->UpdateRow((ECKeyTable::UpdateType)ulUpdateType, &sRowItem, 0, NULL, NULL, NULL, &sPrevRow, false, (ECKeyTable::UpdateType*)&ulTableEvent);
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
	HRESULT hr = hrSuccess;
	ECRESULT er = hrSuccess;

	er = lpKeyTable->Clear();

	hr = kcerr_to_mapierr(er);

	// FIXME: Outlook gives a TABLE_ROW_DELETE or TABLE_CHANGE?
	if (hr == hrSuccess)
		Notify(TABLE_CHANGED, NULL, NULL);

	return hr;
}

ULONG ECMemTableView::xMAPITable::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::AddRef", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->AddRef();
}

ULONG ECMemTableView::xMAPITable::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::Release", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->Release();
}

HRESULT ECMemTableView::xMAPITable::QueryInterface(REFIID refiid, void **lppInterface)
{
 	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::QueryInterface", "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", refiid.Data1, refiid.Data2, refiid.Data3, refiid.Data4[0], refiid.Data4[1], refiid.Data4[2], refiid.Data4[3], refiid.Data4[4], refiid.Data4[5], refiid.Data4[6], refiid.Data4[7]);
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::GetLastError", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->GetLastError(hResult, ulFlags, lppMAPIError);
}

HRESULT ECMemTableView::xMAPITable::Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG * lpulConnection)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::Advise", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->Advise(ulEventMask, lpAdviseSink, lpulConnection);
}

HRESULT ECMemTableView::xMAPITable::Unadvise(ULONG ulConnection)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::Unadvise", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->Unadvise(ulConnection);
}

HRESULT ECMemTableView::xMAPITable::GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::GetStatus", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->GetStatus(lpulTableStatus, lpulTableType);
}

HRESULT ECMemTableView::xMAPITable::SetColumns(LPSPropTagArray lpPropTagArray, ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::SetColumns", "%s", PropNameFromPropTagArray(lpPropTagArray).c_str());
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->SetColumns(lpPropTagArray, ulFlags);
}

HRESULT ECMemTableView::xMAPITable::QueryColumns(ULONG ulFlags, LPSPropTagArray *lpPropTagArray)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::QueryColumns", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->QueryColumns(ulFlags, lpPropTagArray);
}

HRESULT ECMemTableView::xMAPITable::GetRowCount(ULONG ulFlags, ULONG *lpulCount)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::GetRowCount", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->GetRowCount(ulFlags, lpulCount);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::GetRowCount", "%s, count=%d", GetMAPIErrorDescription(hr).c_str(), (lpulCount)?*lpulCount:0);
	return hr;
}

HRESULT ECMemTableView::xMAPITable::SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::SeekRow", "%d %d", bkOrigin, lRowCount);
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->SeekRow(bkOrigin, lRowCount, lplRowsSought);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::SeekRow", "%s, lRowsSought= %d", GetMAPIErrorDescription(hr).c_str(), lplRowsSought ? *lplRowsSought : -1);
	return hr;
}

HRESULT ECMemTableView::xMAPITable::SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::SeekRowApprox", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->SeekRowApprox(ulNumerator, ulDenominator);
}

HRESULT ECMemTableView::xMAPITable::QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::QueryPosition", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->QueryPosition(lpulRow, lpulNumerator, lpulDenominator);
}

HRESULT ECMemTableView::xMAPITable::FindRow(LPSRestriction lpRestriction, BOOKMARK bkOrigin, ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::FindRow", "Restrict=%sbookmark=%d,flags=%d", RestrictionToString(lpRestriction).c_str(), bkOrigin, ulFlags);
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->FindRow(lpRestriction, bkOrigin, ulFlags);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::FindRow", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::Restrict(LPSRestriction lpRestriction, ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::Restrict", "flags=0x%08x  Restrict=%s", ulFlags, RestrictionToString(lpRestriction).c_str());
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->Restrict(lpRestriction, ulFlags);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::Restrict", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::CreateBookmark(BOOKMARK* lpbkPosition)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::CreateBookmak", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->CreateBookmark(lpbkPosition);
}

HRESULT ECMemTableView::xMAPITable::FreeBookmark(BOOKMARK bkPosition)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::FreeBookmark", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->FreeBookmark(bkPosition);
}

HRESULT ECMemTableView::xMAPITable::SortTable(LPSSortOrderSet lpSortCriteria, ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::SortTable", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->SortTable(lpSortCriteria, ulFlags);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::SortTable", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::QuerySortOrder(LPSSortOrderSet *lppSortCriteria)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::QuerySortOrder", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	return pThis->QuerySortOrder(lppSortCriteria);
}

HRESULT ECMemTableView::xMAPITable::QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet *lppRows)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::QueryRows", "rowcount=%d, Flags=0x%08x", lRowCount, ulFlags);
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->QueryRows(lRowCount, ulFlags, lppRows);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::QueryRows", "%s , %d rows returned %s", GetMAPIErrorDescription(hr).c_str(), (*lppRows) ? (*lppRows)->cRows : -1, RowSetToString(*lppRows).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::Abort()
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::Abort", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->Abort();
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::Abort", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet * lppRows, ULONG *lpulMoreRows)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::ExpandRow", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->ExpandRow(cbInstanceKey, pbInstanceKey, ulRowCount, ulFlags, lppRows, lpulMoreRows);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::ExpandRow", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::CollapseRow", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->CollapseRow(cbInstanceKey, pbInstanceKey, ulFlags, lpulRowCount);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::CollapseRow", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG *lpulTableStatus)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::WaitForCompletion", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->WaitForCompletion(ulFlags, ulTimeout, lpulTableStatus);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::WaitForCompletion", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::GetCollapseState", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->GetCollapseState(ulFlags, cbInstanceKey, lpbInstanceKey, lpcbCollapseState, lppbCollapseState);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::GetCollapseState", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMemTableView::xMAPITable::SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation)
{
	TRACE_MAPI(TRACE_ENTRY, "IMemTableView::SetCollapseState", "");
	METHOD_PROLOGUE_(ECMemTableView, MAPITable);
	HRESULT hr = pThis->SetCollapseState(ulFlags, cbCollapseState, pbCollapseState, lpbkLocation);
	TRACE_MAPI(TRACE_RETURN, "IMemTableView::SetCollapseState", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

