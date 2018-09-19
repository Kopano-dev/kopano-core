/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECABCONTAINER_H
#define ECABCONTAINER_H

#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/memory.hpp>
#include "ECABLogon.h"
#include "ECABProp.h"

class ECABLogon;

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

#endif
