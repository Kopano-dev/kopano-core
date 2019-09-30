/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __M4L_MAPIDEFS_IMPL_H
#define __M4L_MAPIDEFS_IMPL_H

#include "m4l.common.h"
#include <mapidefs.h>
#include <mapispi.h>
#include <list>
#include <map>
#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>

class M4LMsgServiceAdmin;

class M4LMAPIProp : public M4LUnknown, public virtual IMailUser {
private:
    // variables
	std::list<LPSPropValue> properties;

public:
	virtual ~M4LMAPIProp(void);
	virtual HRESULT GetLastError(HRESULT, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT SaveChanges(unsigned int flags) override;
	virtual HRESULT GetProps(const SPropTagArray *proptag, unsigned int flags, unsigned int *nvals, SPropValue **prop) override;
	virtual HRESULT GetPropList(unsigned int flags, SPropTagArray **proptag) override;
	virtual HRESULT OpenProperty(unsigned int proptag, const IID *, unsigned int ifaceopts, unsigned int flags, IUnknown **) override __attribute__((nonnull(3)));
	virtual HRESULT SetProps(unsigned int nvals, const SPropValue *prop, SPropProblemArray **) override;
	virtual HRESULT DeleteProps(const SPropTagArray *proptag, SPropProblemArray **) override;
	virtual HRESULT CopyTo(unsigned int nexcl, const IID *excliid, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *iface, void *dest_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *iface, void *dest_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, unsigned int flags, unsigned int *nvals, MAPINAMEID ***names) override;
	virtual HRESULT GetIDsFromNames(unsigned int nelem, MAPINAMEID **names, unsigned int flags, SPropTagArray **proptag) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

class M4LProfSect KC_FINAL_OPG : public IProfSect, public M4LMAPIProp {
private:
	BOOL bGlobalProf;
public:
	M4LProfSect(BOOL gp = false) : bGlobalProf(gp) {}
	virtual HRESULT ValidateState(ULONG ulUIParam, ULONG ulFlags);
	virtual HRESULT SettingsDialog(ULONG ulUIParam, ULONG ulFlags);
	virtual HRESULT ChangePassword(const TCHAR *oldpw, const TCHAR *newpw, ULONG flags);
	virtual HRESULT FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags);
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

class M4LMAPITable final : public M4LUnknown, public IMAPITable {
public:
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
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

class M4LProviderAdmin KC_FINAL_OPG : public M4LUnknown, public IProviderAdmin {
private:
	M4LMsgServiceAdmin* msa;
	char *szService;

public:
	M4LProviderAdmin(M4LMsgServiceAdmin *, const char *service);
	virtual ~M4LProviderAdmin(void);
	virtual HRESULT GetLastError(HRESULT result, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT GetProviderTable(unsigned int flags, IMAPITable **table) override;
	virtual HRESULT CreateProvider(const TCHAR *name, ULONG nprops, const SPropValue *, ULONG ui_param, unsigned int flags, MAPIUID *uid) override;
	virtual HRESULT DeleteProvider(const MAPIUID *uid) override;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, const IID *intf, ULONG flags, IProfSect **) override;
	virtual HRESULT QueryInterface(const IID &, void **iface) override;
};

class M4LMAPIAdviseSink KC_FINAL_OPG :
    public M4LUnknown, public IMAPIAdviseSink {
private:
    void *lpContext;
    LPNOTIFCALLBACK lpFn;

public:
	M4LMAPIAdviseSink(NOTIFCALLBACK *f, void *ctx) : lpContext(ctx), lpFn(f) {}
	virtual ULONG OnNotify(unsigned int nelem, NOTIFICATION *) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};


/* for ABContainer */
class M4LMAPIContainer : public M4LMAPIProp, public virtual IMAPIContainer {
public:
	virtual HRESULT GetContentsTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT GetHierarchyTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(unsigned int flags, SRestriction **, ENTRYLIST **container, unsigned int *search_state) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

struct abEntry {
	MAPIUID muid;
	std::string displayname;
	KC::object_ptr<IABProvider> lpABProvider;
	KC::object_ptr<IABLogon> lpABLogon;
};

class M4LABContainer final : public IABContainer, public M4LMAPIContainer {
private:
	/*  */
	const std::list<abEntry> &m_lABEntries;

public:
	M4LABContainer(const std::list<abEntry> &lABEntries);
	virtual HRESULT CreateEntry(unsigned int eid_size, const ENTRYID *eid, unsigned int flags, IMAPIProp **) override;
	virtual HRESULT CopyEntries(const ENTRYLIST *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT DeleteEntries(const ENTRYLIST *, ULONG flags) override;
	virtual HRESULT ResolveNames(const SPropTagArray *, unsigned int flags, ADRLIST *, FlagList *) override;
	virtual HRESULT GetHierarchyTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

#endif
