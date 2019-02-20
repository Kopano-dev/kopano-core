/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECMAILUSER
#define ECMAILUSER

#include <kopano/Util.h>
#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include "ECABContainer.h"

class ECABLogon;

class ECDistList KC_FINAL_OPG : public ECABContainer, public IDistList {
	public:
	static HRESULT Create(ECABLogon *prov, BOOL modify, ECDistList **);
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);

	// Override IMAPIProp
	virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	virtual HRESULT CopyTo(ULONG nexcl, const IID *excl, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;

	// override IUnknown
	virtual HRESULT	QueryInterface(const IID &, void **) override;

	protected:
	ECDistList(ECABLogon *prov, BOOL modify);
	ALLOC_WRAP_FRIEND;
};

class ECMailUser KC_FINAL_OPG : public ECABProp, public IMailUser {
private:
	ECMailUser(ECABLogon *prov, BOOL modify);

public:
	static HRESULT Create(ECABLogon *prov, BOOL modify, ECMailUser **);
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);
	static HRESULT DefaultGetProp(unsigned int tag, void *prov, unsigned int flags, SPropValue *, ECGenericProp *, void *base);

	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	virtual HRESULT CopyTo(ULONG nexcl, const IID *excl, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;
	virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;
	ALLOC_WRAP_FRIEND;
};

#endif // #ifndef ECMAILUSER
