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
#ifndef WSTABLEVIEW_H
#define WSTABLEVIEW_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <mutex>
#include "soapH.h"
#include "kcore.hpp"
#include <kopano/kcodes.h>
#include <mapi.h>
#include <mapispi.h>

class WSTransport;

typedef HRESULT (*RELOADCALLBACK)(void *lpParam);
using namespace KC;

class WSTableView : public ECUnknown {
protected:
	WSTableView(ULONG type, ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *eid, WSTransport *, const char *cls_name = nullptr);
	virtual ~WSTableView();

public:
	virtual	HRESULT	QueryInterface(REFIID refiid, void **lppInstanceID) _kc_override;
	virtual HRESULT HrOpenTable();
	virtual HRESULT HrCloseTable();

	// You must call HrOpenTable before calling the following methods
	virtual HRESULT HrSetColumns(const SPropTagArray *lpsPropTagArray);
	virtual HRESULT HrFindRow(const SRestriction *lpsRestriction, BOOKMARK bkOrigin, ULONG ulFlags);
	virtual HRESULT HrQueryColumns(ULONG ulFlags, LPSPropTagArray *lppsPropTags);
	virtual HRESULT HrSortTable(const SSortOrderSet *lpsSortOrderSet);
	virtual HRESULT HrQueryRows(ULONG ulRowCount, ULONG ulFlags, LPSRowSet *lppRowSet);
	virtual HRESULT HrGetRowCount(ULONG *lpulRowCount, ULONG *lpulCurrentRow);
	virtual HRESULT HrSeekRow(BOOKMARK bkOrigin, LONG ulRows, LONG *lplRowsSought);
	virtual HRESULT HrExpandRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulRowCount, ULONG ulFlags, LPSRowSet * lppRows, ULONG *lpulMoreRows);
	virtual HRESULT HrCollapseRow(ULONG cbInstanceKey, LPBYTE pbInstanceKey, ULONG ulFlags, ULONG *lpulRowCount);
	virtual HRESULT HrGetCollapseState(BYTE **lppCollapseState, ULONG *lpcbCollapseState, BYTE *lpbInstanceKey, ULONG cbInstanceKey);
	virtual HRESULT HrSetCollapseState(BYTE *lpCollapseState, ULONG cbCollapseState, BOOKMARK *lpbkPosition);

	virtual HRESULT HrMulti(ULONG ulDeferredFlags, LPSPropTagArray lpsPropTagArray, LPSRestriction lpsRestriction, LPSSortOrderSet lpsSortOrderSet, ULONG ulRowCount, ULONG ulFlags, LPSRowSet *lppRowSet);

	virtual HRESULT FreeBookmark(BOOKMARK bkPosition);
	virtual HRESULT CreateBookmark(BOOKMARK* lpbkPosition);

	static HRESULT Reload(void *lpParam, ECSESSIONID sessionID);
	virtual HRESULT SetReloadCallback(RELOADCALLBACK callback, void *lpParam);

	ULONG ulTableId = 0;

protected:
	ECSESSIONID		ecSessionId;
	entryId			m_sEntryId;
	void *			m_lpProvider;
	unsigned int m_ulTableType, m_ulSessionReloadCallback;
	WSTransport*	m_lpTransport;
	SPropTagArray *m_lpsPropTagArray = nullptr;
	SSortOrderSet *m_lpsSortOrderSet = nullptr;
	SRestriction *m_lpsRestriction = nullptr;
	unsigned int ulFlags, ulType;

	void *m_lpParam = nullptr;
	RELOADCALLBACK m_lpCallback = nullptr;
};

#endif
