/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
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
	KCHL::object_ptr<WSTransport> lpTransport;
	std::map<MAPINAMEID *, unsigned int, ltmap> mapNames;

	HRESULT			ResolveLocal(MAPINAMEID *lpName, ULONG *ulId);
	HRESULT			ResolveCache(MAPINAMEID *lpName, ULONG *ulId);
	HRESULT ResolveReverseLocal(ULONG ulId, const GUID *, ULONG flags, void *base, MAPINAMEID **name);
	HRESULT ResolveReverseCache(ULONG ulId, const GUID *, ULONG flags, void *base, MAPINAMEID **name);
	HRESULT			UpdateCache(ULONG ulId, MAPINAMEID *lpName);
	HRESULT			HrCopyNameId(LPMAPINAMEID lpSrc, LPMAPINAMEID *lppDst, void *lpBase);

};

#endif // ECNAMEDPROP_H
