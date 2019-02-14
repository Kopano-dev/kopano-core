/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECATTACH_H
#define ECATTACH_H

#include <mapidefs.h>
#include <kopano/Util.h>
#include "ECMessage.h"
#include "ECMAPIProp.h"
#include "Mem.h"

class ECMsgStore;

class ECAttach : public ECMAPIProp, public IAttach {
protected:
	ECAttach(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root);
	virtual ~ECAttach(void) = default;
public:
	virtual HRESULT QueryInterface(const IID &, void **) override;
	static HRESULT Create(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root, ECAttach **);

	// Override for SaveChanges
	virtual HRESULT SaveChanges(ULONG flags) override;

	// Override for OpenProperty
	virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	static HRESULT GetPropHandler(unsigned int tag, void *prov, unsigned int flags, SPropValue *, ECGenericProp *lpParam, void *base);
	static HRESULT SetPropHandler(unsigned int tag, void *prov, const SPropValue *, ECGenericProp *);

	// Override for CopyTo
	virtual HRESULT CopyTo(ULONG nexcl, const IID *excl, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, ULONG flags, SPropProblemArray **) override;

	// Override for HrSetRealProp - should reset instance ID when changed
	virtual HRESULT HrSetRealProp(const SPropValue *) override;
	virtual HRESULT HrSaveChild(ULONG flags, MAPIOBJECT *) override;

private:
	ULONG ulAttachNum;
	ALLOC_WRAP_FRIEND;
};

class ECAttachFactory final : public IAttachFactory {
public:
	HRESULT Create(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *, ECAttach **) const;
};

#endif // ECATTACH_H
