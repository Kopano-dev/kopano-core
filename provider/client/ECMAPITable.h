/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECMAPITABLE_H
#define ECMAPITABLE_H

#include <kopano/zcdefs.h>
#include <mutex>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include "WSTransport.h"
#include "ECNotifyClient.h"
#include <set>
#include <kopano/memory.hpp>

using namespace KC;

/*
 * This is the superclass which contains common code for the Hierarchy and Contents
 * tables implementations
 */

class ECMAPITable _kc_final : public ECUnknown, public IMAPITable {
protected:
	ECMAPITable(const std::string &name, ECNotifyClient *, ULONG flags);
	virtual ~ECMAPITable();


public:
	static HRESULT Create(const std::string &name, ECNotifyClient *, ULONG flags, ECMAPITable **);
	virtual HRESULT HrSetTableOps(WSTableView *lpTableOps, bool fLoad);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual BOOL IsDeferred();
	virtual HRESULT FlushDeferred(LPSRowSet *lppRowSet = NULL);
	virtual HRESULT GetLastError(HRESULT, ULONG flags, MAPIERROR **) override;
	virtual HRESULT Advise(ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(ULONG conn) override;
	virtual HRESULT GetStatus(ULONG *tbl_status, ULONG *tbl_type) override;
	virtual HRESULT SetColumns(const SPropTagArray *, ULONG flags) override;
	virtual HRESULT QueryColumns(ULONG flags, SPropTagArray **) override;
	virtual HRESULT GetRowCount(ULONG flags, ULONG *count) override;
	virtual HRESULT SeekRow(BOOKMARK origin, LONG rows, LONG *sought) override;
	virtual HRESULT SeekRowApprox(ULONG num, ULONG denom) override;
	virtual HRESULT QueryPosition(ULONG *row, ULONG *num, ULONG *denom) override;
	virtual HRESULT FindRow(const SRestriction *, BOOKMARK origin, ULONG flags) override;
	virtual HRESULT Restrict(const SRestriction *, ULONG flags) override;
	virtual HRESULT CreateBookmark(BOOKMARK *pos) override;
	virtual HRESULT FreeBookmark(BOOKMARK pos) override;
	virtual HRESULT SortTable(const SSortOrderSet *, ULONG flags) override;
	virtual HRESULT QuerySortOrder(SSortOrderSet **crit) override;
	virtual HRESULT QueryRows(LONG rows, ULONG flags, SRowSet **) override;
	virtual HRESULT Abort() override;
	virtual HRESULT ExpandRow(ULONG ik_size, BYTE *instance_key, ULONG rows, ULONG flags, SRowSet **, ULONG *more) override;
	virtual HRESULT CollapseRow(ULONG ik_size, BYTE *instance_key, ULONG flags, ULONG *rows) override;
	virtual HRESULT WaitForCompletion(ULONG flags, ULONG timeout, ULONG *tbl_status) override;
	virtual HRESULT GetCollapseState(ULONG flags, ULONG ik_size, BYTE *instance_key, ULONG *state_size, BYTE **state) override;
	virtual HRESULT SetCollapseState(ULONG flags, ULONG state_size, BYTE *state, BOOKMARK *loc) override;
	static HRESULT Reload(void *lpParam);

private:
	std::recursive_mutex m_hLock;
	KC::object_ptr<WSTableView> lpTableOps;
	KC::object_ptr<ECNotifyClient> lpNotifyClient;
	KC::memory_ptr<SSortOrderSet> lpsSortOrderSet;
	std::set<ULONG>		m_ulConnectionList;
	std::recursive_mutex m_hMutexConnectionList;
	
	// Deferred calls
	KC::memory_ptr<SPropTagArray> m_lpSetColumns;
	KC::memory_ptr<SRestriction> m_lpRestrict;
	KC::memory_ptr<SSortOrderSet> m_lpSortTable;
	ULONG m_ulDeferredFlags = 0, m_ulRowCount = 0;
	ULONG m_ulFlags = 0; /* Flags from queryrows */
	std::string			m_strName;
	ALLOC_WRAP_FRIEND;
};

#endif // ECMAPITABLE_H
