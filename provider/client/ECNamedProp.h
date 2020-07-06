/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <map>
#include <kopano/memory.hpp>

class WSTransport;

struct ltmap {
	bool operator()(const MAPINAMEID *, const MAPINAMEID *) const noexcept;
};

class ECNamedProp final {
public:
	ECNamedProp(WSTransport *lpTransport);
	virtual ~ECNamedProp();
	virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names);
	virtual HRESULT GetIDsFromNames(unsigned int num, MAPINAMEID **names, unsigned int flags, SPropTagArray **proptags);

private:
	KC::object_ptr<WSTransport> lpTransport;
	std::map<MAPINAMEID *, unsigned int, ltmap> mapNames;

	HRESULT			ResolveLocal(MAPINAMEID *lpName, ULONG *ulId);
	HRESULT			ResolveCache(MAPINAMEID *lpName, ULONG *ulId);
	HRESULT ResolveReverseLocal(ULONG ulId, const GUID *, ULONG flags, void *base, MAPINAMEID **name);
	HRESULT ResolveReverseCache(ULONG ulId, const GUID *, ULONG flags, void *base, MAPINAMEID **name);
	HRESULT			UpdateCache(ULONG ulId, MAPINAMEID *lpName);
	HRESULT HrCopyNameId(MAPINAMEID *src, MAPINAMEID **dst, void *base);
};
