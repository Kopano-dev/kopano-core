/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECNAMEDPROP_H
#define ECNAMEDPROP_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <map>
#include <kopano/memory.hpp>

class WSTransport;

struct ltmap {
	bool operator()(const MAPINAMEID *, const MAPINAMEID *) const noexcept;
};

class ECNamedProp _kc_final {
public:
	ECNamedProp(WSTransport *lpTransport);
	virtual ~ECNamedProp();
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names);
	virtual HRESULT GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags);

private:
	KC::object_ptr<WSTransport> lpTransport;
	std::map<MAPINAMEID *, unsigned int, ltmap> mapNames;

	HRESULT			ResolveLocal(MAPINAMEID *lpName, ULONG *ulId);
	HRESULT			ResolveCache(MAPINAMEID *lpName, ULONG *ulId);
	HRESULT ResolveReverseLocal(ULONG ulId, const GUID *, ULONG flags, void *base, MAPINAMEID **name);
	HRESULT ResolveReverseCache(ULONG ulId, const GUID *, ULONG flags, void *base, MAPINAMEID **name);
	HRESULT			UpdateCache(ULONG ulId, MAPINAMEID *lpName);
	HRESULT			HrCopyNameId(LPMAPINAMEID lpSrc, LPMAPINAMEID *lppDst, void *lpBase);
};

#endif // ECNAMEDPROP_H
