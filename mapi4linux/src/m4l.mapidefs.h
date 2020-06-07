/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <mapidefs.h>
#include <mapispi.h>
#include <list>
#include <map>
#include <string>
#include <kopano/ECUnknown.h>
#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>

class M4LMsgServiceAdmin;

class M4LMAPIProp : public KC::ECUnknown, public virtual IMAPIProp {
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

class M4LMailUser KC_FINAL_OPG : public M4LMAPIProp, public IMailUser {
	public:
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

class M4LProfSect KC_FINAL_OPG : public IProfSect, public M4LMAPIProp {
public:
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

class M4LProviderAdmin KC_FINAL_OPG : public KC::ECUnknown, public IProviderAdmin {
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
    public KC::ECUnknown, public IMAPIAdviseSink {
private:
    void *lpContext;
    LPNOTIFCALLBACK lpFn;

public:
	M4LMAPIAdviseSink(NOTIFCALLBACK *f, void *ctx) : lpContext(ctx), lpFn(f) {}
	virtual ULONG OnNotify(unsigned int nelem, NOTIFICATION *) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

struct abEntry {
	MAPIUID muid;
	std::string displayname;
	KC::object_ptr<IABProvider> lpABProvider;
	KC::object_ptr<IABLogon> lpABLogon;
};

class M4LABContainer final : public IABContainer, public M4LMAPIProp {
private:
	const std::list<abEntry> &m_lABEntries;

public:
	M4LABContainer(const std::list<abEntry> &lABEntries);
	virtual HRESULT CreateEntry(unsigned int eid_size, const ENTRYID *eid, unsigned int flags, IMAPIProp **) override;
	virtual HRESULT CopyEntries(const ENTRYLIST *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT DeleteEntries(const ENTRYLIST *, ULONG flags) override;
	virtual HRESULT ResolveNames(const SPropTagArray *, unsigned int flags, ADRLIST *, FlagList *) override;
	virtual HRESULT GetContentsTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT GetHierarchyTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(unsigned int flags, SRestriction **, ENTRYLIST **container, unsigned int *search_state) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};
