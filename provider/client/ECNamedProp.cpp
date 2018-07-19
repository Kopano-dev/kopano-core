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
#include <kopano/platform.h>
#include <mapitags.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <list>
#include <kopano/ECLogger.h>
#include "Mem.h"
#include "ECNamedProp.h"
#include "WSTransport.h"

/*
 * How our named properties work
 *
 * Basically, named properties in objects with the same PR_MAPPING_SIGNATURE should have the same
 * mapping of named properties to property IDs and vice-versa. We can use this information, together
 * with the information that Outlook mainly uses a fixed set of named properties to speed up things
 * considerably;
 *
 * Normally, each GetIDsFromNames calls would have to consult the server for an ID, and then cache
 * and return the value to the client. This is a rather time-consuming thing to do as Outlook requests
 * quite a few different named properties at startup. We can make this much faster by hard-wiring 
 * a bunch of known named properties into the CLIENT-side DLL. This makes sure that most (say 90%) of
 * GetIDsFromNames calls can be handled locally without any reference to the server, while any other
 * (new) named properties can be handled in the standard way. This reduces client-server communications
 * dramatically, resulting in both a faster client as less datacommunication between client and server.
 *
 * In fact, most of the time, the named property mechanism will work even if the server is down...
 */

/*
 * Currently, serverside named properties are cached locally in a map<> object,
 * however, in the future, a bimap<> may be used to speed up reverse lookups (ie
 * getNamesFromIDs) but this is not used frequently so we can leave it like 
 * this for now
 *
 * For the most part, this implementation is rather fast, (possible faster than
 * Exchange) due to the fact that we know which named properties are likely to be
 * requested. This means that we have 
 */

/* Our local names
 *
 * It is VERY IMPORTANT that these values are NOT MODIFIED, otherwise the mapping of
 * named properties will change, which will BREAK THINGS BADLY
 *
 * Special constraints for this structure:
 * - The ulMappedIds must not overlap the previous row
 * - The ulMappedIds must be in ascending order
 */

static const struct _sLocalNames {
	GUID guid;
	LONG ulMin, ulMax;
	ULONG ulMappedId; // mapped ID of the FIRST property in the range
} sLocalNames[] = 	{{{ 0x62002, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8200, 0x826F, 0x8000 },
					{{ 0x62003, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8100, 0x813F, 0x8070 },
					{{ 0x62004, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8000, 0x80EF, 0x80B0 },
					{{ 0x62008, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8500, 0x85FF, 0x81A0 },
					{{ 0x6200A, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8700, 0x871F, 0x82A0 },
					{{ 0x6200B, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8800, 0x881F, 0x82C0 },
					{{ 0x6200E, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8B00, 0x8B1F, 0x82E0 },
					{{ 0x62013, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8D00, 0x8D1F, 0x8300 },
					{{ 0x62014, 0x0, 0x0, { 0xC0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46 } }, 0x8F00, 0x8F1F, 0x8320 },
					{{ 0x6ED8DA90, 0x450B, 0x101B, { 0x98, 0xDA, 0x00, 0xAA, 0x00, 0x3F, 0x13, 0x05} } , 0x0000, 0x003F, 0x8340}};

#define SERVER_NAMED_OFFSET	0x8500
#define SERVER_MAX_NPID (0xFFFF - SERVER_NAMED_OFFSET)

/**
 * Sort function
 *
 * It does not really matter *how* it sorts, as long as it is reproduceable.
 */
bool ltmap::operator()(const MAPINAMEID *a, const MAPINAMEID *b) const noexcept
{
	auto r = memcmp(a->lpguid, b->lpguid, sizeof(GUID));
	if (r < 0)
		return false;
	if (r > 0)
		return true;
	if (a->ulKind != b->ulKind)
		return a->ulKind > b->ulKind;
	switch (a->ulKind) {
	case MNID_ID:
		return a->Kind.lID > b->Kind.lID;
	case MNID_STRING:
		return wcscmp(a->Kind.lpwstrName, b->Kind.lpwstrName) < 0;
	default:
		return false;
	}
}

ECNamedProp::ECNamedProp(WSTransport *tp) :
	lpTransport(tp)
{}

ECNamedProp::~ECNamedProp()
{
	// Clear all the cached names
	for (const auto &p : mapNames)
		if (p.first)
			ECFreeBuffer(p.first);
}

HRESULT ECNamedProp::GetNamesFromIDs(SPropTagArray **lppPropTags,
    const GUID *lpPropSetGuid, ULONG ulFlags, ULONG *lpcPropNames,
    MAPINAMEID ***lpppPropNames)
{
	if (lppPropTags == nullptr || *lppPropTags == nullptr)
		/* Exchange does not support this, so neither do we. */
		return MAPI_E_TOO_BIG;

	ecmem_ptr<MAPINAMEID *> lppPropNames, lppResolved;
	ecmem_ptr<SPropTagArray> lpsUnresolved;
	unsigned int cResolved = 0, cUnresolved = 0;

	auto lpsPropTags = *lppPropTags;
	// Allocate space for properties
	auto hr = ECAllocateBuffer(sizeof(LPMAPINAMEID) * lpsPropTags->cValues, &~lppPropNames);
	if (hr != hrSuccess)
		return hr;

	// Pass 1, local reverse mapping (FAST)
	for (unsigned int i = 0; i < lpsPropTags->cValues; ++i)
		if (ResolveReverseLocal(PROP_ID(lpsPropTags->aulPropTag[i]),
		    lpPropSetGuid, ulFlags, lppPropNames,
		    &lppPropNames[i]) != hrSuccess)
			lppPropNames[i] = NULL;

	// Pass 2, cache reverse mapping (FAST)
	for (unsigned int i = 0; i < lpsPropTags->cValues; ++i) {
		if (lppPropNames[i] != NULL)
			continue;
		if (PROP_ID(lpsPropTags->aulPropTag[i]) > SERVER_NAMED_OFFSET)
			ResolveReverseCache(PROP_ID(lpsPropTags->aulPropTag[i]) - SERVER_NAMED_OFFSET, lpPropSetGuid, ulFlags, lppPropNames, &lppPropNames[i]);
		// else { Hmmm, so here is a named property, which is < SERVER_NAMED_OFFSET, but CANNOT be
		// resolved internally. Looks like somebody's pulling our leg ... We just leave it unknown }
	}

	hr = ECAllocateBuffer(CbNewSPropTagArray(lpsPropTags->cValues), &~lpsUnresolved);
	if (hr != hrSuccess)
		return hr;

	cUnresolved = 0;
	// Pass 3, server reverse lookup (SLOW)
	for (unsigned int i = 0; i < lpsPropTags->cValues; ++i)
		if (lppPropNames[i] == NULL)
			if(PROP_ID(lpsPropTags->aulPropTag[i]) > SERVER_NAMED_OFFSET) {
				lpsUnresolved->aulPropTag[cUnresolved] = PROP_ID(lpsPropTags->aulPropTag[i]) - SERVER_NAMED_OFFSET;
				++cUnresolved;
			}
	lpsUnresolved->cValues = cUnresolved;

	if(cUnresolved > 0) {
		hr = lpTransport->HrGetNamesFromIDs(lpsUnresolved, &~lppResolved, &cResolved);
		if(hr != hrSuccess)
			return hr;

		// Put the resolved values from the server into the cache
		if (cResolved != cUnresolved)
			return MAPI_E_CALL_FAILED;
		for (unsigned int i = 0; i < cResolved; ++i)
			if(lppResolved[i] != NULL)
				UpdateCache(lpsUnresolved->aulPropTag[i], lppResolved[i]);

		// re-scan the cache
		for (unsigned int i = 0; i < lpsPropTags->cValues; ++i)
			if (lppPropNames[i] == NULL)
				if (PROP_ID(lpsPropTags->aulPropTag[i]) > SERVER_NAMED_OFFSET)
					ResolveReverseCache(PROP_ID(lpsPropTags->aulPropTag[i]) - SERVER_NAMED_OFFSET, lpPropSetGuid, ulFlags, lppPropNames, &lppPropNames[i]);
	}

	// Check for errors
	for (unsigned int i = 0; i < lpsPropTags->cValues; ++i)
		if(lppPropNames[i] == NULL)
			hr = MAPI_W_ERRORS_RETURNED;

	*lpppPropNames = lppPropNames.release();
	*lpcPropNames = lpsPropTags->cValues;
	return hr;
}

HRESULT ECNamedProp::GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags)
{
	if (cPropNames == 0 || lppPropNames == nullptr)
		/* Exchange does not support this, so neither do we. */
		return MAPI_E_TOO_BIG;

	ecmem_ptr<SPropTagArray> lpsPropTagArray;
	std::unique_ptr<MAPINAMEID *[]> lppPropNamesUnresolved;
	ULONG			cUnresolved = 0;
	ecmem_ptr<ULONG> lpServerIDs;

	// Allocate memory for the return structure
	auto hr = ECAllocateBuffer(CbNewSPropTagArray(cPropNames), &~lpsPropTagArray);
	if(hr != hrSuccess)
		return hr;

	lpsPropTagArray->cValues = cPropNames;

	// Pass 1, resolve static (local) names (FAST)
	for (unsigned int i = 0; i < cPropNames; ++i)
		if(lppPropNames[i] == NULL || ResolveLocal(lppPropNames[i], &lpsPropTagArray->aulPropTag[i]) != hrSuccess)
			lpsPropTagArray->aulPropTag[i] = PROP_TAG(PT_ERROR, 0);

	// Pass 2, resolve names from local cache (FAST)
	for (unsigned int i = 0; i < cPropNames; ++i)
		if (lppPropNames[i] != NULL && lpsPropTagArray->aulPropTag[i] == PROP_TAG(PT_ERROR, 0))
			ResolveCache(lppPropNames[i], &lpsPropTagArray->aulPropTag[i]);

	// Pass 3, resolve names from server (SLOW, but decreases in frequency with store lifetime)
	lppPropNamesUnresolved.reset(new MAPINAMEID *[lpsPropTagArray->cValues]); // over-allocated

	// Get a list of unresolved names
	for (unsigned int i = 0; i < cPropNames; ++i)
		if(lpsPropTagArray->aulPropTag[i] == PROP_TAG(PT_ERROR, 0) && lppPropNames[i] != NULL ) {
			lppPropNamesUnresolved[cUnresolved] = lppPropNames[i];
			++cUnresolved;
		}

	if(cUnresolved) {
		// Let the server resolve these names 
		hr = lpTransport->HrGetIDsFromNames(lppPropNamesUnresolved.get(), cUnresolved, ulFlags, &~lpServerIDs);
		if(hr != hrSuccess)
			return hr;

		// Put the names into the local cache for all the IDs the server gave us
		for (unsigned int i = 0; i < cUnresolved; ++i)
			if(lpServerIDs[i] != 0)
				UpdateCache(lpServerIDs[i], lppPropNamesUnresolved[i]);

		// Pass 4, re-resolve from local cache (FAST)
		for (unsigned int i = 0; i < cPropNames; ++i)
			if (lppPropNames[i] != NULL &&
			    lpsPropTagArray->aulPropTag[i] == PROP_TAG(PT_ERROR, 0))
				ResolveCache(lppPropNames[i], &lpsPropTagArray->aulPropTag[i]);
	}
	
	// Finally, check for any errors left in the returned structure
	hr = hrSuccess;
	for (unsigned int i = 0; i < cPropNames; ++i)
		if(lpsPropTagArray->aulPropTag[i] == PROP_TAG(PT_ERROR, 0)) {
			hr = MAPI_W_ERRORS_RETURNED;
			break;
		}

	*lppPropTags = lpsPropTagArray.release();
	return hr;
}

HRESULT ECNamedProp::ResolveLocal(MAPINAMEID *lpName, ULONG *ulPropTag)
{
	// We can only locally resolve MNID_ID types of named properties
	if (lpName->ulKind != MNID_ID)
		return MAPI_E_NOT_FOUND;

	// Loop through our local names to see if the named property is in there
	for (size_t i = 0; i < ARRAY_SIZE(sLocalNames); ++i) {
		if(memcmp(&sLocalNames[i].guid,lpName->lpguid,sizeof(GUID))==0 && sLocalNames[i].ulMin <= lpName->Kind.lID && sLocalNames[i].ulMax >= lpName->Kind.lID) {
			// Found it, calculate the ID and return it.
			*ulPropTag = PROP_TAG(PT_UNSPECIFIED, sLocalNames[i].ulMappedId + lpName->Kind.lID - sLocalNames[i].ulMin);
			return hrSuccess;
		}
	}

	// Couldn't find it ...
	return MAPI_E_NOT_FOUND;
}

HRESULT ECNamedProp::ResolveReverseCache(ULONG ulId, const GUID *lpGuid,
    ULONG ulFlags, void *lpBase, MAPINAMEID **lppName)
{
	// Loop through the map to find the reverse-lookup of the named property. This could be speeded up by
	// used a bimap (bi-directional map)

	for (const auto &p : mapNames) {
		if (p.second >= SERVER_MAX_NPID)
			continue;
		if (p.second == ulId) { // FIXME match GUID
			if (lpGuid != nullptr)
				assert(memcmp(lpGuid, p.first->lpguid, sizeof(GUID)) == 0); // TEST michel
			// found it
			return HrCopyNameId(p.first, lppName, lpBase);
		}
	}
	return MAPI_E_NOT_FOUND;
}

HRESULT ECNamedProp::ResolveReverseLocal(ULONG ulId, const GUID *lpGuid,
    ULONG ulFlags, void *lpBase, MAPINAMEID **lppName)
{
	if (ulFlags & MAPI_NO_IDS)
		/* Local mapping is only for MNID_ID */
		return MAPI_E_NOT_FOUND;

	MAPINAMEID *lpName = nullptr;
	// Loop through the local names to see if we can reverse-map the id
	for (size_t i = 0; i < ARRAY_SIZE(sLocalNames); ++i) {
		bool y = (lpGuid == nullptr || memcmp(&sLocalNames[i].guid, lpGuid, sizeof(GUID)) == 0) &&
		         ulId >= sLocalNames[i].ulMappedId &&
		         ulId < sLocalNames[i].ulMappedId + (sLocalNames[i].ulMax - sLocalNames[i].ulMin + 1);
		if (!y)
			continue;
		// Found it !
		auto hr = ECAllocateMore(sizeof(MAPINAMEID), lpBase, reinterpret_cast<void **>(&lpName));
		if (hr != hrSuccess)
			return hr;
		hr = ECAllocateMore(sizeof(GUID), lpBase, reinterpret_cast<void **>(&lpName->lpguid));
		if (hr != hrSuccess)
			return hr;
		lpName->ulKind = MNID_ID;
		memcpy(lpName->lpguid, &sLocalNames[i].guid, sizeof(GUID));
		lpName->Kind.lID = sLocalNames[i].ulMin + (ulId - sLocalNames[i].ulMappedId);
		break;
	}
	if (lpName == NULL)
		return MAPI_E_NOT_FOUND;
	*lppName = lpName;
	return hrSuccess;
}

// Update the cache with the given data
HRESULT ECNamedProp::UpdateCache(ULONG ulId, MAPINAMEID *lpName)
{
	if (mapNames.find(lpName) != mapNames.end())
		/* Already in the cache! */
		return MAPI_E_NOT_FOUND;

	ecmem_ptr<MAPINAMEID> lpNameCopy;
	auto hr = HrCopyNameId(lpName, &~lpNameCopy, nullptr);
	if(hr != hrSuccess)
		return hr;
	mapNames[lpNameCopy.release()] = ulId;
	static bool warn_range_exceeded;
	if (ulId >= SERVER_MAX_NPID && !warn_range_exceeded) {
		warn_range_exceeded = true;
		ec_log_err("K-1222: Server returned a high namedpropid (0x%x) which this client cannot deal with.", ulId);
	}
	return hrSuccess;
}

HRESULT ECNamedProp::ResolveCache(MAPINAMEID *lpName, ULONG *lpulPropTag)
{
	auto iterMap = mapNames.find(lpName);
	if (iterMap == mapNames.cend())
		return MAPI_E_NOT_FOUND;
	if (iterMap->second >= SERVER_MAX_NPID) {
		*lpulPropTag = PROP_TAG(PT_ERROR, 0);
		return MAPI_W_ERRORS_RETURNED;
	}
	*lpulPropTag = PROP_TAG(PT_UNSPECIFIED, SERVER_NAMED_OFFSET + iterMap->second);
	return hrSuccess;
}

/* This copies a MAPINAMEID struct using ECAllocate* functions. Therefore, the
 * memory allocated here is *not* traced by the debug functions. Make sure you
 * release all memory allocated from this function! (or make sure the client application
 * does)
 */
HRESULT ECNamedProp::HrCopyNameId(LPMAPINAMEID lpSrc, LPMAPINAMEID *lppDst, void *lpBase)
{
	HRESULT			hr = hrSuccess;
	LPMAPINAMEID	lpDst = NULL;

	if(lpBase == NULL)
		hr = ECAllocateBuffer(sizeof(MAPINAMEID), (void **) &lpDst);
	else
		hr = ECAllocateMore(sizeof(MAPINAMEID), lpBase, (void **) &lpDst);

	if(hr != hrSuccess)
		return hr;

	lpDst->ulKind = lpSrc->ulKind;

	if(lpSrc->lpguid) {
		if(lpBase) 
			hr = ECAllocateMore(sizeof(GUID), lpBase, (void **) &lpDst->lpguid);
		else 
			hr = ECAllocateMore(sizeof(GUID), lpDst, (void **) &lpDst->lpguid);

		if(hr != hrSuccess)
			goto exit;

		memcpy(lpDst->lpguid, lpSrc->lpguid, sizeof(GUID));
	} else {
		lpDst->lpguid = NULL;
	}

	switch(lpSrc->ulKind) {
	case MNID_ID:
		lpDst->Kind.lID = lpSrc->Kind.lID;
		break;
	case MNID_STRING:
		if(lpBase)
			hr = ECAllocateMore(wcslen(lpSrc->Kind.lpwstrName) * sizeof(wchar_t) + sizeof(wchar_t),
			     lpBase, reinterpret_cast<void **>(&lpDst->Kind.lpwstrName));
		else
			hr = ECAllocateMore(wcslen(lpSrc->Kind.lpwstrName) * sizeof(wchar_t) + sizeof(wchar_t),
			     lpDst, reinterpret_cast<void **>(&lpDst->Kind.lpwstrName));
		if (hr != hrSuccess)
			return hr;
		wcscpy(lpDst->Kind.lpwstrName, lpSrc->Kind.lpwstrName);
		break;
	default:
		hr = MAPI_E_INVALID_TYPE;
		goto exit;
	}

	*lppDst = lpDst;

exit:
	if (hr != hrSuccess && lpBase == nullptr)
		ECFreeBuffer(lpDst);

	return hr;
}
