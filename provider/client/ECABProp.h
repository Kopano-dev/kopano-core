/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECABPROP_H
#define ECABPROP_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include "ECGenericProp.h"
#include "ECABLogon.h"
#include "WSTransport.h"

class ECABLogon;

class ECABProp : public ECGenericProp {
protected:
	ECABProp(ECABLogon *prov, ULONG obj_type, BOOL modify, const char *cls = nullptr);
	virtual ~ECABProp(void) = default;
public:
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	static HRESULT DefaultABGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);
	ECABLogon *GetABStore() const { return static_cast<ECABLogon *>(lpProvider); }
};

#endif
