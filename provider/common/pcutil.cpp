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
#include <utility>
#include "pcutil.hpp"
#include <mapicode.h>
#include <kopano/stringutil.h>
#include <mapidefs.h>
#include <kopano/ECGuid.h>
#include "versions.h"

namespace KC {

bool IsKopanoEntryId(ULONG cb, const BYTE *lpEntryId)
{
	if(lpEntryId == NULL)
		return false;
	auto peid = reinterpret_cast<const EID *>(lpEntryId);
	/* TODO: maybe also a check on objType */
	return (cb == sizeof(EID) && peid->ulVersion == 1) ||
	       (cb == sizeof(EID_V0) && peid->ulVersion == 0);
}

bool ValidateZEntryId(ULONG cb, const BYTE *lpEntryId, unsigned int ulCheckType)
{
	if(lpEntryId == NULL)
		return false;
	auto peid = reinterpret_cast<const EID *>(lpEntryId);
	return ((cb == sizeof(EID) && peid->ulVersion == 1) ||
	       (cb == sizeof(EID_V0) && peid->ulVersion == 0)) &&
	       peid->usType == ulCheckType;
}

/**
 * Validate a kopano entryid list on a specific mapi object type
 * 
 * @param lpMsgList		Pointer to an ENTRYLIST structure that contains the number 
 *						of items to validate and an array of ENTRYID structures identifying the items.
 * @param ulCheckType	Contains the type of the objects in the lpMsgList. 
 *
 * @return bool			true if all the items in the lpMsgList matches with the object type
 */
bool ValidateZEntryList(const ENTRYLIST *lpMsgList, unsigned int ulCheckType)
{
	if (lpMsgList == NULL)
		return false;

	for (ULONG i = 0; i < lpMsgList->cValues; ++i) {
		auto peid = reinterpret_cast<const EID *>(lpMsgList->lpbin[i].lpb);
		if( !(((lpMsgList->lpbin[i].cb == sizeof(EID) && peid->ulVersion == 1) ||
			 (lpMsgList->lpbin[i].cb == sizeof(EID_V0) && peid->ulVersion == 0 ) ) &&
			 peid->usType == ulCheckType))
			return false;
	}
	return true;
}

ECRESULT GetStoreGuidFromEntryId(ULONG cb, const BYTE *lpEntryId,
    GUID *lpguidStore)
{
	if(lpEntryId == NULL || lpguidStore == NULL)
		return KCERR_INVALID_PARAMETER;
	auto peid = reinterpret_cast<const EID *>(lpEntryId);
	if(!((cb == sizeof(EID) && peid->ulVersion == 1) ||
		 (cb == sizeof(EID_V0) && peid->ulVersion == 0 )) )
		return KCERR_INVALID_ENTRYID;

	memcpy(lpguidStore, &peid->guid, sizeof(GUID));
	return erSuccess;
}

ECRESULT GetObjTypeFromEntryId(ULONG cb, const BYTE *lpEntryId,
    unsigned int *lpulObjType)
{
	if (lpEntryId == NULL || lpulObjType == NULL)
		return KCERR_INVALID_PARAMETER;
	if (cb == sizeof(EID)) {
		EID eid;
		memcpy(&eid, lpEntryId, sizeof(eid));
		if (eid.ulVersion != 1)
			return KCERR_INVALID_ENTRYID;
		*lpulObjType = eid.usType;
		return erSuccess;
	} else if (cb == sizeof(EID_V0)) {
		EID_V0 eid;
		memcpy(&eid, lpEntryId, sizeof(eid));
		if (eid.ulVersion != 0)
			return KCERR_INVALID_ENTRYID;
		*lpulObjType = eid.usType;
		return erSuccess;
	}
	return KCERR_INVALID_ENTRYID;
}

ECRESULT GetObjTypeFromEntryId(const entryId &sEntryId,
    unsigned int *lpulObjType)
{
    return GetObjTypeFromEntryId(sEntryId.__size, sEntryId.__ptr, lpulObjType);
}

ECRESULT GetStoreGuidFromEntryId(const entryId &sEntryId, GUID *lpguidStore)
{
    return GetStoreGuidFromEntryId(sEntryId.__size, sEntryId.__ptr, lpguidStore);
}

HRESULT HrGetStoreGuidFromEntryId(ULONG cb, const BYTE *lpEntryId,
    GUID *lpguidStore)
{
	return kcerr_to_mapierr(GetStoreGuidFromEntryId(cb, lpEntryId, lpguidStore));
}

HRESULT HrGetObjTypeFromEntryId(ULONG cb, const BYTE *lpEntryId,
    unsigned int *lpulObjType)
{
	return kcerr_to_mapierr(GetObjTypeFromEntryId(cb, lpEntryId, lpulObjType));
}

ECRESULT ABEntryIDToID(ULONG cb, const BYTE *lpEntryId, unsigned int *lpulID,
    objectid_t *lpsExternId, unsigned int *lpulMapiType)
{
	if (lpEntryId == nullptr || lpulID == nullptr || cb < CbNewABEID(""))
		return KCERR_INVALID_PARAMETER;
	auto lpABEID = reinterpret_cast<const ABEID *>(lpEntryId);
	if (memcmp(&lpABEID->guid, &MUIDECSAB, sizeof(GUID)) != 0)
		return KCERR_INVALID_ENTRYID;

	unsigned int ulID = lpABEID->ulId;
	objectid_t		sExternId;
	objectclass_t	sClass = ACTIVE_USER;
	MAPITypeToType(lpABEID->ulType, &sClass);

	if (lpABEID->ulVersion == 1)
		sExternId = objectid_t(base64_decode(reinterpret_cast<const char *>(lpABEID->szExId)), sClass);

	*lpulID = ulID;

	if (lpsExternId)
		*lpsExternId = std::move(sExternId);
	if (lpulMapiType)
		*lpulMapiType = lpABEID->ulType;
	return erSuccess;
}

ECRESULT ABEntryIDToID(const entryId *lpsEntryId, unsigned int *lpulID,
    objectid_t *lpsExternId, unsigned int *lpulMapiType)
{
	if (lpsEntryId == NULL)
		return KCERR_INVALID_PARAMETER;
	return ABEntryIDToID(lpsEntryId->__size, lpsEntryId->__ptr, lpulID, lpsExternId, lpulMapiType);
}

ECRESULT SIEntryIDToID(ULONG cb, const BYTE *lpInstanceId, GUID *guidServer,
    unsigned int *lpulInstanceId, unsigned int *lpulPropId)
{
	if (lpInstanceId == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto lpInstanceEid = reinterpret_cast<const SIEID *>(lpInstanceId);
	if (guidServer)
		memcpy(guidServer, (LPBYTE)lpInstanceEid + sizeof(SIEID), sizeof(GUID));
	if (lpulInstanceId)
		*lpulInstanceId = lpInstanceEid->ulId;
	if (lpulPropId)
		*lpulPropId = lpInstanceEid->ulType;
	return erSuccess;
}

template<typename T> static int twcmp(T a, T b)
{
	/* see elsewhere for raison d'être */
	return (a < b) ? -1 : (a == b) ? 0 : 1;
}

/**
 * Compares ab entryids and returns an int, can be used for sorting algorithms.
 * <0 left first
 *  0 same, or invalid
 * >0 right first
 */
int SortCompareABEID(ULONG cbEntryID1, const ENTRYID *lpEntryID1,
    ULONG cbEntryID2, const ENTRYID *lpEntryID2)
{
	int rv = 0;
	auto peid1 = reinterpret_cast<const ABEID *>(lpEntryID1);
	auto peid2 = reinterpret_cast<const ABEID *>(lpEntryID2);

	if (lpEntryID1 == NULL || lpEntryID2 == NULL)
		return 0;
	if (peid1->ulVersion != peid2->ulVersion)
		return twcmp(peid1->ulVersion, peid2->ulVersion);

	// sort: user(6), group(8), company(4)
	if (peid1->ulType != peid2->ulType)  {
		if (peid1->ulType == MAPI_ABCONT)
			return -1;
		else if (peid2->ulType == MAPI_ABCONT)
			return 1;
		else
			rv = twcmp(peid1->ulType, peid2->ulType);
		if (rv != 0)
			return rv;
	}

	if (peid1->ulVersion == 0)
		rv = twcmp(peid1->ulId, peid2->ulId);
	else
		rv = strcmp((char*)peid1->szExId, (char*)peid2->szExId);
	if (rv != 0)
		return rv;
	rv = memcmp(&peid1->guid, &peid2->guid, sizeof(GUID));
	if (rv != 0)
		return rv;
	return 0;
}

bool CompareABEID(ULONG cbEntryID1, const ENTRYID *lpEntryID1,
    ULONG cbEntryID2, const ENTRYID *lpEntryID2)
{
	auto peid1 = reinterpret_cast<const ABEID *>(lpEntryID1);
	auto peid2 = reinterpret_cast<const ABEID *>(lpEntryID2);

	if (lpEntryID1 == NULL || lpEntryID2 == NULL)
		return false;

	if (peid1->ulVersion != peid2->ulVersion) {
		if (cbEntryID1 < CbNewABEID("") || cbEntryID2 < CbNewABEID("") ||
		    peid1->ulId != peid2->ulId)
			return false;
	} else if (cbEntryID1 != cbEntryID2 || cbEntryID1 < CbNewABEID("")) {
		return false;
	} else if (peid1->ulVersion == 0) {
		if (peid1->ulId != peid2->ulId)
			return false;
	} else if (strcmp(reinterpret_cast<const char *>(peid1->szExId), reinterpret_cast<const char *>(peid2->szExId))) {
		return false;
	}
	return peid1->guid == peid2->guid && peid1->ulType == peid2->ulType;
}

HRESULT HrSIEntryIDToID(ULONG cb, const BYTE *lpInstanceId, GUID *guidServer,
    unsigned int *lpulInstanceId, unsigned int *lpulPropId)
{
	if(lpInstanceId == NULL)
		return MAPI_E_INVALID_PARAMETER;
	return kcerr_to_mapierr(SIEntryIDToID(cb, lpInstanceId, guidServer, lpulInstanceId, lpulPropId));
}

ECRESULT ABIDToEntryID(struct soap *soap, unsigned int ulID, const objectid_t& sExternId, entryId *lpsEntryId)
{
	if (lpsEntryId == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto strEncExId = base64_encode(sExternId.id.c_str(), sExternId.id.size());
	unsigned int ulLen = CbNewABEID(strEncExId.c_str());
	auto lpUserEid = reinterpret_cast<ABEID *>(s_alloc<char>(soap, ulLen));
	memset(lpUserEid, 0, ulLen);
	lpUserEid->ulId = ulID;
	auto er = TypeToMAPIType(sExternId.objclass, &lpUserEid->ulType);
	if (er != erSuccess) {
		s_free(soap, lpUserEid);
		return er; /* or make default type user? */
	}

	memcpy(&lpUserEid->guid, &MUIDECSAB, sizeof(GUID));

	// If the externid is non-empty, we'll create a V1 entry id.
	if (!sExternId.id.empty())
	{
		lpUserEid->ulVersion = 1;
		// avoid FORTIFY_SOURCE checks in strcpy to an address that the compiler thinks is 1 size large
		memcpy(lpUserEid->szExId, strEncExId.c_str(), strEncExId.length()+1);
	}

	lpsEntryId->__size = ulLen;
	lpsEntryId->__ptr = (unsigned char*)lpUserEid;
	return erSuccess;
}

ECRESULT SIIDToEntryID(struct soap *soap, const GUID *guidServer,
    unsigned int ulInstanceId, unsigned int ulPropId, entryId *lpsInstanceId)
{
	assert(ulPropId < 0x0000FFFF);
	if (lpsInstanceId == nullptr)
		return KCERR_INVALID_PARAMETER;

	ULONG ulSize = sizeof(SIEID) + sizeof(GUID);
	auto lpInstanceEid = reinterpret_cast<SIEID *>(s_alloc<char>(soap, ulSize));
	memset(lpInstanceEid, 0, ulSize);

	lpInstanceEid->ulId = ulInstanceId;
	lpInstanceEid->ulType = ulPropId;
	memcpy(&lpInstanceEid->guid, MUIDECSI_SERVER, sizeof(GUID));

	memcpy((char *)lpInstanceEid + sizeof(SIEID), guidServer, sizeof(GUID));

	lpsInstanceId->__size = ulSize;
	lpsInstanceId->__ptr = (unsigned char *)lpInstanceEid;
	return erSuccess;
}

ECRESULT SIEntryIDToID(const entryId *sInstanceId, GUID *guidServer,
    unsigned int *lpulInstanceId, unsigned int *lpulPropId)
{
	if (sInstanceId == NULL)
		return KCERR_INVALID_PARAMETER;
	return SIEntryIDToID(sInstanceId->__size, sInstanceId->__ptr, guidServer, lpulInstanceId, lpulPropId);
}

// NOTE: when using this function, we can never be sure that we return the actual objectclass_t.
// MAPI_MAILUSER can also be any type of nonactive user, groups can be security groups etc...
// This can only be used as a hint. You should really look the user up since you should either know the
// users table id, or extern id of the user too!
ECRESULT MAPITypeToType(ULONG ulMAPIType, objectclass_t *lpsUserObjClass)
{
	if (lpsUserObjClass == nullptr)
		return KCERR_INVALID_PARAMETER;

	objectclass_t		sUserObjClass = OBJECTCLASS_UNKNOWN;
	switch (ulMAPIType) {
	case MAPI_MAILUSER:
		sUserObjClass = OBJECTCLASS_USER;
		break;
	case MAPI_DISTLIST:
		sUserObjClass = OBJECTCLASS_DISTLIST;
		break;
	case MAPI_ABCONT:
		sUserObjClass = OBJECTCLASS_CONTAINER;
		break;
	default:
		return KCERR_INVALID_TYPE;
	}

	*lpsUserObjClass = std::move(sUserObjClass);
	return erSuccess;
}

ECRESULT TypeToMAPIType(objectclass_t sUserObjClass, ULONG *lpulMAPIType)
{
	if (lpulMAPIType == nullptr)
		return KCERR_INVALID_PARAMETER;

	ULONG ulMAPIType = MAPI_MAILUSER;
	// Check for correctness of mapping!
	switch (OBJECTCLASS_TYPE(sUserObjClass))
	{
	case OBJECTTYPE_MAILUSER:
		ulMAPIType = MAPI_MAILUSER;
		break;
	case OBJECTTYPE_DISTLIST:
		ulMAPIType = MAPI_DISTLIST;
		break;
	case OBJECTTYPE_CONTAINER:
		ulMAPIType = MAPI_ABCONT;
		break;
	default:
		return KCERR_INVALID_TYPE;
	}

	*lpulMAPIType = ulMAPIType;
	return erSuccess;
}

/**
 * Parse a Kopano version string in the form [0,]<general>,<major>,<minor>[,<svn_revision>] and
 * place the result in a 32 bit unsigned integer.
 * The format of the result is 1 byte general, 1 bytes major and 2 bytes minor.
 * The svn_revision is optional and ignored in any case.
 *
 * @param[in]	strVersion		The version string to parse
 * @param[out]	lpulVersion		Pointer to the unsigned integer in which the result is stored.
 *
 * @retval		KCERR_INVALID_PARAMETER	The version string could not be parsed.
 */
ECRESULT ParseKopanoVersion(const std::string &strVersion, std::string *seg,
    unsigned int *lpulVersion)
{
	const char *lpszStart = strVersion.c_str();
	char *lpszEnd = NULL;
	unsigned int ulGeneral, ulMajor, ulMinor;

	if (seg != nullptr)
		seg->clear();
	// For some reason the server returns its version prefixed with "0,". We'll
	// just ignore that.
	// We assume that there's no actual live server running 0,x,y,z
	if (strncmp(lpszStart, "0,", 2) == 0)
		lpszStart += 2;

	ulGeneral = strtoul(lpszStart, &lpszEnd, 10);
	if (lpszEnd == NULL || lpszEnd == lpszStart || *lpszEnd != ',')
		return KCERR_INVALID_PARAMETER;

	lpszStart = lpszEnd + 1;
	ulMajor = strtoul(lpszStart, &lpszEnd, 10);
	if (lpszEnd == NULL || lpszEnd == lpszStart || *lpszEnd != ',')
		return KCERR_INVALID_PARAMETER;

	lpszStart = lpszEnd + 1;
	ulMinor = strtoul(lpszStart, &lpszEnd, 10);
	if (lpszEnd == NULL || lpszEnd == lpszStart || (*lpszEnd != ',' && *lpszEnd != '\0'))
		return KCERR_INVALID_PARAMETER;
	if (seg != nullptr)
		*seg = std::to_string(ulGeneral) + "." + std::to_string(ulMajor) + "." + std::to_string(ulMinor);
	if (lpulVersion)
		*lpulVersion = MAKE_KOPANO_VERSION(ulGeneral, ulMajor, ulMinor);
	return erSuccess;
}

} /* namespace */
