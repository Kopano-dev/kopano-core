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

class WSTransport;

// Sort function
//
// Doesn't really matter *how* it sorts, as long as it's reproduceable
struct ltmap {
	bool operator()(const MAPINAMEID *a, const MAPINAMEID *b) const
	{
	    int r = memcmp(a->lpguid, b->lpguid, sizeof(GUID));
	    
        if(r<0)
            return false;
        else if(r>0)
            return true;
        else {	    
            if(a->ulKind != b->ulKind)
                return a->ulKind > b->ulKind;

            switch(a->ulKind) {
            case MNID_ID:
                return a->Kind.lID > b->Kind.lID;
            case MNID_STRING:
                return wcscmp(a->Kind.lpwstrName, b->Kind.lpwstrName) < 0;
            default:
                return false;
            }
        }
	}
};

class ECNamedProp _kc_final {
public:
	ECNamedProp(WSTransport *lpTransport);
	virtual ~ECNamedProp();

	virtual HRESULT GetNamesFromIDs(LPSPropTagArray FAR * lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG FAR * lpcPropNames, LPMAPINAMEID FAR * FAR * lpppPropNames);
	virtual HRESULT GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID FAR * lppPropNames, ULONG ulFlags, LPSPropTagArray FAR * lppPropTags);

private:
	std::map<MAPINAMEID *,ULONG,ltmap>		mapNames;
	WSTransport	*							lpTransport;

	HRESULT			ResolveLocal(MAPINAMEID *lpName, ULONG *ulId);
	HRESULT			ResolveCache(MAPINAMEID *lpName, ULONG *ulId);

	HRESULT			ResolveReverseLocal(ULONG ulId, LPGUID lpGuid, ULONG ulFlags, void *lpBase, MAPINAMEID **lppName);
	HRESULT			ResolveReverseCache(ULONG ulId, LPGUID lpGuid, ULONG ulFlags, void *lpBase, MAPINAMEID **lppName);

	HRESULT			UpdateCache(ULONG ulId, MAPINAMEID *lpName);
	HRESULT			HrCopyNameId(LPMAPINAMEID lpSrc, LPMAPINAMEID *lppDst, void *lpBase);

};

#endif // ECNAMEDPROP_H
