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

#ifndef ECMEMTABLE_H
#define ECMEMTABLE_H

#include <kopano/zcdefs.h>
#include <vector>
#include <map>
#include <mapitags.h>
#include <mapidefs.h>
#include <pthread.h>

#include <kopano/ECKeyTable.h>
#include <kopano/ECUnknown.h>
#include <kopano/ustringutil.h>

typedef struct {
	LPSPropValue	lpsPropVal;
	BOOL			fDeleted;
	BOOL			fDirty;
	BOOL			fNew;
	LPSPropValue	lpsID;
	ULONG			cValues;
} ECTableEntry;

typedef struct {
	ULONG				ulEventMask;
	LPMAPIADVISESINK	lpAdviseSink;
	//ULONG				ulConnection;
} ECMEMADVISE;

typedef std::map<int, ECMEMADVISE *> ECMapMemAdvise;


/* Status returned in HrGetAllWithStatus() */
#define ECROW_NORMAL	0
#define ECROW_ADDED		1
#define ECROW_MODIFIED	2
#define ECROW_DELETED	3

/*
 * This is a client-side implementation of IMAPITable, based on 
 * SPropValue's. You can add/delete/modify data through HrModifyRow
 * and you can get the data from its IMAPITable interface
 *
 * We use the ECKeyTable engine for the actual cursor/sorting system
 */
class ECMemTableView;

class ECMemTable : public ECUnknown {
protected:
	ECMemTable(LPSPropTagArray lpsPropTagArray, ULONG ulRowPropTag);
	virtual ~ECMemTable();
public:
	static  HRESULT Create(LPSPropTagArray lpsPropTagArray, ULONG ulRowPropTag, ECMemTable **lppRecipTable);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;

	virtual HRESULT HrGetView(const ECLocale &locale, ULONG ulFlags, ECMemTableView **lpView);

	virtual HRESULT HrModifyRow(ULONG ulFlags, LPSPropValue lpId, LPSPropValue lpProps, ULONG cValues);
	
	virtual HRESULT HrUpdateRowID(LPSPropValue lpId, LPSPropValue lpProps, ULONG cValues);

	virtual HRESULT HrClear();

	virtual HRESULT HrDeleteAll();

	// Get the modified, deleted and added tables in the row
	virtual HRESULT HrGetAllWithStatus(LPSRowSet *lppRowSet, LPSPropValue *lppIDs, LPULONG *lppulStatus);
	virtual HRESULT HrGetRowID(LPSPropValue lpRow, LPSPropValue *lpID);
	virtual HRESULT HrGetRowData(LPSPropValue lpRow, ULONG *lpcValues, LPSPropValue *lppRowData);

	// Update all rows as being clean, remove deleted rows
	virtual HRESULT HrSetClean();

protected:
	// Data
	std::map<unsigned int, ECTableEntry>	mapRows;
	std::vector<ECMemTableView *>			lstViews;
	LPSPropTagArray							lpsColumns;
	ULONG									ulRowPropTag;

	pthread_mutex_t							m_hDataMutex;

	friend class ECMemTableView;
};

class ECMemTableView _zcp_final : public ECUnknown {
protected:
	ECMemTableView(ECMemTable *lpMemTable, const ECLocale &locale, ULONG ulFlags);
	virtual ~ECMemTableView();
public:
	static HRESULT	Create(ECMemTable *lpMemTable, const ECLocale &locale, ULONG ulFlags, ECMemTableView **lppMemTableView);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
	virtual HRESULT UpdateRow(ULONG ulUpdateType, ULONG ulId);
	virtual HRESULT Clear();

	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG * lpulConnection);
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType);
	virtual HRESULT SetColumns(LPSPropTagArray lpPropTagArray, ULONG ulFlags);
	virtual HRESULT QueryColumns(ULONG ulFlags, LPSPropTagArray *lpPropTagArray);
	virtual HRESULT GetRowCount(ULONG ulFlags, ULONG *lpulCount);
	virtual HRESULT SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought) ;
	virtual HRESULT SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator);
	virtual HRESULT QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator);
	virtual HRESULT FindRow(LPSRestriction lpRestriction, BOOKMARK bkOrigin, ULONG ulFlags);
	virtual HRESULT Restrict(LPSRestriction lpRestriction, ULONG ulFlags);
	virtual HRESULT CreateBookmark(BOOKMARK* lpbkPosition);
	virtual HRESULT FreeBookmark(BOOKMARK bkPosition);
	virtual HRESULT SortTable(LPSSortOrderSet lpSortCriteria, ULONG ulFlags);
	virtual HRESULT QuerySortOrder(LPSSortOrderSet *lppSortCriteria);
	virtual HRESULT QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet *lppRows);
	virtual HRESULT Abort();
	virtual HRESULT ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet * lppRows, ULONG *lpulMoreRows);
	virtual HRESULT CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount);
	virtual HRESULT WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG *lpulTableStatus);
	virtual HRESULT GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState);
	virtual HRESULT SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation);

	class xMAPITable _zcp_final : public IMAPITable {
		// IUnknown
		virtual ULONG __stdcall AddRef(void) _zcp_override;
		virtual ULONG __stdcall Release(void) _zcp_override;
		virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;

		// From IMAPITable
		virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _zcp_override;
		virtual HRESULT __stdcall Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection) _zcp_override;
		virtual HRESULT __stdcall Unadvise(ULONG ulConnection) _zcp_override;
		virtual HRESULT __stdcall GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType) _zcp_override;
		virtual HRESULT __stdcall SetColumns(LPSPropTagArray lpPropTagArray, ULONG ulFlags) _zcp_override;
		virtual HRESULT __stdcall QueryColumns(ULONG ulFlags, LPSPropTagArray *lpPropTagArray) _zcp_override;
		virtual HRESULT __stdcall GetRowCount(ULONG ulFlags, ULONG *lpulCount) _zcp_override;
		virtual HRESULT __stdcall SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought) _zcp_override;
		virtual HRESULT __stdcall SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator) _zcp_override;
		virtual HRESULT __stdcall QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator) _zcp_override;
		virtual HRESULT __stdcall FindRow(LPSRestriction lpRestriction, BOOKMARK bkOrigin, ULONG ulFlags) _zcp_override;
		virtual HRESULT __stdcall Restrict(LPSRestriction lpRestriction, ULONG ulFlags) _zcp_override;
		virtual HRESULT __stdcall CreateBookmark(BOOKMARK *lpbkPosition) _zcp_override;
		virtual HRESULT __stdcall FreeBookmark(BOOKMARK bkPosition) _zcp_override;
		virtual HRESULT __stdcall SortTable(LPSSortOrderSet lpSortCriteria, ULONG ulFlags) _zcp_override;
		virtual HRESULT __stdcall QuerySortOrder(LPSSortOrderSet *lppSortCriteria) _zcp_override;
		virtual HRESULT __stdcall QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet *lppRows) _zcp_override;
		virtual HRESULT __stdcall Abort() _zcp_override;
		virtual HRESULT __stdcall ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet *lppRows, ULONG *lpulMoreRows) _zcp_override;
		virtual HRESULT __stdcall CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount) _zcp_override;
		virtual HRESULT __stdcall WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG *lpulTableStatus) _zcp_override;
		virtual HRESULT __stdcall GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState) _zcp_override;
		virtual HRESULT __stdcall SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation) _zcp_override;
	} m_xMAPITable;

private:
	HRESULT __stdcall GetBinarySortKey(LPSPropValue lpsPropVal, unsigned int *lpSortLen, unsigned char *lpFlags, unsigned char **lppSortData);

	HRESULT ModifyRowKey(sObjectTableKey *lpsRowItem, sObjectTableKey* lpsPrevRow, ULONG *lpulAction);
	HRESULT QueryRowData(ECObjectTableList *lpsRowList, LPSRowSet *lppRows);
	HRESULT Notify(ULONG ulTableEvent, sObjectTableKey* lpsRowItem, sObjectTableKey* lpsPrevRow);

	ECKeyTable *			lpKeyTable;
	LPSSortOrderSet			lpsSortOrderSet;
	LPSPropTagArray			lpsPropTags;		// Columns
	LPSRestriction			lpsRestriction;
	ECMemTable *			lpMemTable;
	ECMapMemAdvise			m_mapAdvise;
	ULONG					m_ulConnection; // Next advise id
	ECLocale				m_locale;
	ULONG					m_ulFlags;

	virtual HRESULT UpdateSortOrRestrict();

};


#endif // ECMemTable_H
