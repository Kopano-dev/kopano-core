/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECRULESTABLEPROXY_INCLUDED
#define ECRULESTABLEPROXY_INCLUDED

#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <mapidefs.h>

class ECRulesTableProxy final : public KC::ECUnknown, public IMAPITable {
protected:
	ECRulesTableProxy(LPMAPITABLE lpTable);
	virtual ~ECRulesTableProxy();
    
public:
	static  HRESULT Create(LPMAPITABLE lpTable, ECRulesTableProxy **lppRulesTableProxy);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT GetLastError(HRESULT result, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT Advise(unsigned int evt_mask, IMAPIAdviseSink *, unsigned int *conn) override;
	virtual HRESULT Unadvise(unsigned int conn) override;
	virtual HRESULT GetStatus(unsigned int *table_status, unsigned int *table_type) override;
	virtual HRESULT SetColumns(const SPropTagArray *, unsigned int flags) override;
	virtual HRESULT QueryColumns(unsigned int flags, SPropTagArray **) override;
	virtual HRESULT GetRowCount(unsigned int flags, unsigned int *count) override;
	virtual HRESULT SeekRow(BOOKMARK origin, int row_count, int *rows_sought) override;
	virtual HRESULT SeekRowApprox(unsigned int num, unsigned int denom) override;
	virtual HRESULT QueryPosition(unsigned int *row, unsigned int *num, unsigned int *denom) override;
	virtual HRESULT FindRow(const SRestriction *, BOOKMARK origin, unsigned int flags) override;
	virtual HRESULT Restrict(const SRestriction *, unsigned int flags) override;
	virtual HRESULT CreateBookmark(BOOKMARK *pos) override;
	virtual HRESULT FreeBookmark(BOOKMARK pos) override;
	virtual HRESULT SortTable(const SSortOrderSet *sort_crit, unsigned int flags) override;
	virtual HRESULT QuerySortOrder(SSortOrderSet **sort_crit) override;
	virtual HRESULT QueryRows(int row_count, unsigned int flags, SRowSet **) override;
	virtual HRESULT Abort() override;
	virtual HRESULT ExpandRow(unsigned int ik_size, BYTE *inst_key, unsigned int row_count, unsigned int flags, SRowSet **, unsigned int *more_rows) override;
	virtual HRESULT CollapseRow(unsigned int ik_size, BYTE *inst_key, unsigned int flags, unsigned int *row_count) override;
	virtual HRESULT WaitForCompletion(unsigned int flags, unsigned int timeout, unsigned int *table_status) override;
	virtual HRESULT GetCollapseState(unsigned int flags, unsigned int ik_size, BYTE *inst_key, unsigned int *coll_size, BYTE **collapse_state) override;
	virtual HRESULT SetCollapseState(unsigned int flags, unsigned int coll_size, BYTE *collapse_state, BOOKMARK *loc) override;

private:
	LPMAPITABLE m_lpTable;
	ALLOC_WRAP_FRIEND;
};

#endif // ndef ECRULESTABLEPROXY_INCLUDED
