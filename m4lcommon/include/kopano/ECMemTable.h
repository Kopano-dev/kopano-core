/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECMEMTABLE_H
#define ECMEMTABLE_H

#include <kopano/zcdefs.h>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <mapitags.h>
#include <mapidefs.h>
#include <kopano/ECKeyTable.h>
#include <kopano/ECUnknown.h>
#include <kopano/memory.hpp>
#include <kopano/ustringutil.h>
#include <kopano/Util.h>

namespace KC {

struct ECTableEntry {
	memory_ptr<SPropValue> lpsPropVal, lpsID;
	BOOL fDeleted, fDirty, fNew;
	ULONG			cValues;
};

struct ECMEMADVISE {
	ULONG				ulEventMask;
	object_ptr<IMAPIAdviseSink> lpAdviseSink;
	//ULONG				ulConnection;
};

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

class KC_EXPORT ECMemTable : public ECUnknown {
protected:
	ECMemTable(const SPropTagArray *lpsPropTagArray, ULONG ulRowPropTag);
	virtual ~ECMemTable();
public:
	static HRESULT Create(const SPropTagArray *lpsPropTagArray, ULONG ulRowPropTag, ECMemTable **lppRecipTable);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT HrGetView(const ECLocale &locale, ULONG ulFlags, ECMemTableView **lpView);
	virtual HRESULT HrModifyRow(ULONG flags, const SPropValue *id, const SPropValue *prop, ULONG n);
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
	ULONG									ulRowPropTag;
	memory_ptr<SPropTagArray> lpsColumns;
	std::recursive_mutex m_hDataMutex;

	friend class ECMemTableView;
	ALLOC_WRAP_FRIEND;
};

class KC_EXPORT ECMemTableView KC_FINAL_OPG :
    public ECUnknown, public IMAPITable {
protected:
	_kc_hidden ECMemTableView(ECMemTable *, const ECLocale &, ULONG flags);
	_kc_hidden virtual ~ECMemTableView(void);
public:
	_kc_hidden static HRESULT Create(ECMemTable *, const ECLocale &, ULONG flags, ECMemTableView **ret);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	_kc_hidden virtual HRESULT UpdateRow(ULONG update_type, ULONG id);
	_kc_hidden virtual HRESULT Clear(void);
	_kc_hidden virtual HRESULT GetLastError(HRESULT, unsigned int flags, MAPIERROR **ret) override;
	_kc_hidden virtual HRESULT Advise(unsigned int event_mask, IMAPIAdviseSink *, unsigned int *conn) override;
	_kc_hidden virtual HRESULT Unadvise(unsigned int conn) override;
	_kc_hidden virtual HRESULT GetStatus(unsigned int *table_status, unsigned int *table_type) override;
	virtual HRESULT SetColumns(const SPropTagArray *, unsigned int flags) override;
	virtual HRESULT QueryColumns(unsigned int flags, SPropTagArray **) override;
	_kc_hidden virtual HRESULT GetRowCount(unsigned int flags, unsigned int *count) override;
	_kc_hidden virtual HRESULT SeekRow(BOOKMARK origin, int row_count, int *rows_sought) override;
	_kc_hidden virtual HRESULT SeekRowApprox(unsigned int numerator, unsigned int denominator) override;
	_kc_hidden virtual HRESULT QueryPosition(unsigned int *row, unsigned int *numerator, unsigned int *denominator) override;
	_kc_hidden virtual HRESULT FindRow(const SRestriction *, BOOKMARK origin, unsigned int flags) override;
	_kc_hidden virtual HRESULT Restrict(const SRestriction *, unsigned int flags) override;
	_kc_hidden virtual HRESULT CreateBookmark(BOOKMARK *pos) override;
	_kc_hidden virtual HRESULT FreeBookmark(BOOKMARK pos) override;
	_kc_hidden virtual HRESULT SortTable(const SSortOrderSet *sort_crit, unsigned int flags) override;
	_kc_hidden virtual HRESULT QuerySortOrder(SSortOrderSet **sort_crit) override;
	virtual HRESULT QueryRows(int row_count, unsigned int flags, SRowSet **) override;
	_kc_hidden virtual HRESULT Abort() override;
	_kc_hidden virtual HRESULT ExpandRow(unsigned int ikey_size, BYTE *ikey, unsigned int row_count, unsigned int flags, SRowSet **rows, unsigned int *more_rows) override;
	_kc_hidden virtual HRESULT CollapseRow(unsigned int ikey_size, BYTE *ikey, unsigned int flags, unsigned int *row_count) override;
	_kc_hidden virtual HRESULT WaitForCompletion(unsigned int flags, unsigned int timeout, unsigned int *table_status) override;
	_kc_hidden virtual HRESULT GetCollapseState(unsigned int flags, unsigned int ikey_size, BYTE *ikey, unsigned int *collapse_size, BYTE **collapse_state) override;
	_kc_hidden virtual HRESULT SetCollapseState(unsigned int flags, unsigned int collapse_size, BYTE *collapse_state, BOOKMARK *location) override;

private:
	_kc_hidden HRESULT GetBinarySortKey(const SPropValue *pv, ECSortCol &);
	_kc_hidden HRESULT ModifyRowKey(sObjectTableKey *row_item, sObjectTableKey *prev_row, ULONG *action);
	_kc_hidden HRESULT QueryRowData(const ECObjectTableList *row_list, SRowSet **rows);
	_kc_hidden HRESULT Notify(ULONG table_event, sObjectTableKey *row_item, sObjectTableKey *prev_row);

	ECKeyTable lpKeyTable;
	ECMemTable *			lpMemTable;
	ECMapMemAdvise			m_mapAdvise;
	memory_ptr<SSortOrderSet> lpsSortOrderSet;
	memory_ptr<SPropTagArray> lpsPropTags; /* columns */
	memory_ptr<SRestriction> lpsRestriction;
	ECLocale				m_locale;
	ULONG m_ulConnection = 1; // Next advise id
	ULONG					m_ulFlags;

	_kc_hidden virtual HRESULT UpdateSortOrRestrict(void);
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif // ECMemTable_H
