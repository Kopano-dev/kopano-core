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

#ifndef KC_PCUTIL_HPP
#define KC_PCUTIL_HPP 1

// All functions which used in storage server and client
#include "kcore.hpp"
#include <kopano/kcodes.h>
#include "soapH.h"
#include <kopano/ECDefs.h>
#include "SOAPUtils.h"
#include <mapidefs.h>

#include <string>

bool IsKopanoEntryId(ULONG cb, LPBYTE lpEntryId);
bool ValidateZEntryId(ULONG cb, LPBYTE lpEntryId, unsigned int ulCheckType);
bool ValidateZEntryList(LPENTRYLIST lpMsgList, unsigned int ulCheckType);
ECRESULT ABEntryIDToID(ULONG cb, LPBYTE lpEntryId, unsigned int* lpulID, objectid_t* lpsExternId, unsigned int* lpulMapiType);
ECRESULT SIEntryIDToID(ULONG cb, LPBYTE lpInstanceId, LPGUID guidServer, unsigned int *lpulInstanceId, unsigned int *lpulPropId = NULL);
int SortCompareABEID(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2);
bool CompareABEID(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2);

ECRESULT ParseKopanoVersion(const std::string &strVersion, unsigned int *lpulVersion);

//Clientside functions
HRESULT HrGetStoreGuidFromEntryId(ULONG cb, LPBYTE lpEntryId, LPGUID lpguidStore);
HRESULT HrGetObjTypeFromEntryId(ULONG cb, LPBYTE lpEntryId, unsigned int* lpulObjType);
HRESULT HrSIEntryIDToID(ULONG cb, LPBYTE lpInstanceId, LPGUID guidServer, unsigned int *lpulID, unsigned int *lpulPropId = NULL);

// Serverside functions
ECRESULT GetStoreGuidFromEntryId(ULONG cb, LPBYTE lpEntryId, LPGUID guidStore);
ECRESULT GetObjTypeFromEntryId(ULONG cb, LPBYTE lpEntryId, unsigned int* lpulObjType);
ECRESULT GetStoreGuidFromEntryId(entryId sEntryId, LPGUID guidStore);
ECRESULT GetObjTypeFromEntryId(entryId sEntryId, unsigned int* lpulObjType);
ECRESULT ABEntryIDToID(entryId* lpsEntryId, unsigned int* lpulID, objectid_t* lpsExternId, unsigned int* lpulMapiType);
ECRESULT SIEntryIDToID(entryId* sInstanceId, LPGUID guidServer, unsigned int *lpulInstanceId, unsigned int *lpulPropId = NULL);
ECRESULT ABIDToEntryID(struct soap *soap, unsigned int ulID, const objectid_t& strExternId, entryId *lpsEntryId);
ECRESULT SIIDToEntryID(struct soap *soap, LPGUID guidServer, unsigned int ulInstanceId, unsigned int ulPropId, entryId *lpsInstanceId);
ECRESULT MAPITypeToType(ULONG ulMAPIType, objectclass_t *lpsUserObjClass);
ECRESULT TypeToMAPIType(objectclass_t sUserObjClass, ULONG *lpulMAPIType);

#endif /* KC_PCUTIL_HPP */
