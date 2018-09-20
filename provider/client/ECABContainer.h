/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECABCONTAINER_H
#define ECABCONTAINER_H

#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include <kopano/ECUnknown.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <mapispi.h>
#include "ECGenericProp.h"
#include "ECNotifyClient.h"

class WSTransport;

class ECABLogon final : public KC::ECUnknown, public IABLogon {
	protected:
	ECABLogon(IMAPISupport *, WSTransport *, ULONG profile_flags, const GUID *);
	virtual ~ECABLogon();

	public:
	static  HRESULT Create(IMAPISupport *, WSTransport *, ULONG profile_flags, const GUID *, ECABLogon **);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT GetLastError(HRESULT, ULONG flags, MAPIERROR **) override;
	virtual HRESULT Logoff(ULONG flags) override;
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) override;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) override;
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(ULONG conn) override;
	virtual HRESULT OpenStatusEntry(const IID *intf, ULONG flags, ULONG *objtype, IMAPIStatus **) override;
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) override;
	virtual HRESULT GetOneOffTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT PrepareRecips(ULONG flags, const SPropTagArray *, ADRLIST *recips) override;

	KC::object_ptr<IMAPISupport> m_lpMAPISup;
	KC::object_ptr<WSTransport> m_lpTransport;
	KC::object_ptr<ECNotifyClient> m_lpNotifyClient;
	GUID m_guid, m_ABPGuid;
	ALLOC_WRAP_FRIEND;
};

class ECABProp : public ECGenericProp {
	protected:
	ECABProp(ECABLogon *prov, ULONG obj_type, BOOL modify, const char *cls = nullptr);
	virtual ~ECABProp() = default;

	public:
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	static HRESULT DefaultABGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);
	ECABLogon *GetABStore() const { return static_cast<ECABLogon *>(lpProvider); }
};

class ECABContainer : public ECABProp, public IABContainer {
protected:
	ECABContainer(ECABLogon *prov, ULONG obj_type, BOOL modify, const char *cls);
	virtual ~ECABContainer() = default;
public:
	static HRESULT Create(ECABLogon *prov, ULONG obj_type, BOOL modify, ECABContainer **);
	static HRESULT	DefaultABContainerGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFLags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);

	// IUnknown
	virtual HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

	// IABContainer
	virtual HRESULT CreateEntry(ULONG eid_size, const ENTRYID *eid, ULONG flags, IMAPIProp **) override;
	virtual HRESULT CopyEntries(const ENTRYLIST *, ULONG ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT DeleteEntries(const ENTRYLIST *, ULONG flags) override;
	virtual HRESULT ResolveNames(const SPropTagArray *, ULONG flags, ADRLIST *, FlagList *) override;

	// From IMAPIContainer
	virtual HRESULT GetContentsTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT GetHierarchyTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) override;
	virtual HRESULT SetSearchCriteria(const SRestriction *, const ENTRYLIST *container, ULONG flags) override;
	virtual HRESULT GetSearchCriteria(ULONG flags, SRestriction **, ENTRYLIST **container, ULONG *state) override;

	// From IMAPIProp
	virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	virtual HRESULT CopyTo(ULONG nexcl, const IID *excl, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;

private:
	ALLOC_WRAP_FRIEND;
};

class ECABProvider final : public KC::ECUnknown, public IABProvider {
	protected:
	ECABProvider(ULONG ulFlags, const char *szClassName);
	virtual ~ECABProvider() = default;

	public:
	static  HRESULT Create(ECABProvider **lppECABProvider);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Shutdown(ULONG *flags) override;
	virtual HRESULT Logon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG flags, ULONG *sec_size, BYTE **sec, MAPIERROR **, IABLogon **) override;

	ULONG m_ulFlags;
	ALLOC_WRAP_FRIEND;
};

class ECABProviderSwitch final : public KC::ECUnknown, public IABProvider {
	protected:
	ECABProviderSwitch();

	public:
	static  HRESULT Create(ECABProviderSwitch **lppECABProvider);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Shutdown(ULONG *flags) override;
	virtual HRESULT Logon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG flags, ULONG *sec_size, BYTE **sec, MAPIERROR **, IABLogon **) override;
	ALLOC_WRAP_FRIEND;
};

#endif
