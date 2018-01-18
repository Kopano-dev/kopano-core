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

	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Advise(ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG * lpulConnection);
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT GetStatus(ULONG *lpulTableStatus, ULONG *lpulTableType);
	virtual HRESULT SetColumns(const SPropTagArray *lpPropTagArray, ULONG ulFlags);
	virtual HRESULT QueryColumns(ULONG ulFlags, LPSPropTagArray *lpPropTagArray);
	virtual HRESULT GetRowCount(ULONG ulFlags, ULONG *lpulCount);
	virtual HRESULT SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG *lplRowsSought) ;
	virtual HRESULT SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator);
	virtual HRESULT QueryPosition(ULONG *lpulRow, ULONG *lpulNumerator, ULONG *lpulDenominator);
	virtual HRESULT FindRow(const SRestriction *, BOOKMARK origin, ULONG flags) override;
	virtual HRESULT Restrict(const SRestriction *, ULONG flags) override;
	virtual HRESULT CreateBookmark(BOOKMARK* lpbkPosition);
	virtual HRESULT FreeBookmark(BOOKMARK bkPosition);
	virtual HRESULT SortTable(const SSortOrderSet *, ULONG flags);
	virtual HRESULT QuerySortOrder(LPSSortOrderSet *lppSortCriteria);
	virtual HRESULT QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet *lppRows);
	virtual HRESULT Abort();
	virtual HRESULT ExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet * lppRows, ULONG *lpulMoreRows);
	virtual HRESULT CollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount);
	virtual HRESULT WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG *lpulTableStatus);
	virtual HRESULT GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, LPBYTE lpbInstanceKey, ULONG *lpcbCollapseState, LPBYTE *lppbCollapseState);
	virtual HRESULT SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, LPBYTE pbCollapseState, BOOKMARK *lpbkLocation);

	static HRESULT Reload(void *lpParam);

private:
	std::recursive_mutex m_hLock;
	KC::object_ptr<WSTableView> lpTableOps;
	KC::object_ptr<ECNotifyClient> lpNotifyClient;
	KC::memory_ptr<SSortOrderSet> lpsSortOrderSet;
	ULONG				ulFlags; // Currently unused
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
