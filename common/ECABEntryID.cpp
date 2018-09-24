/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/ECABEntryID.h>
#include <kopano/ECGuid.h>
#include <mapicode.h>
#include "../provider/include/kcore.hpp"

namespace KC {

static ABEID		g_sDefaultEid(MAPI_MAILUSER, MUIDECSAB, 0);
unsigned char		*g_lpDefaultEid = (unsigned char*)&g_sDefaultEid;
const unsigned int	g_cbDefaultEid = sizeof(g_sDefaultEid);

static ABEID		g_sEveryOneEid(MAPI_DISTLIST, MUIDECSAB, 1);
unsigned char		*g_lpEveryoneEid = (unsigned char*)&g_sEveryOneEid;
const unsigned int	g_cbEveryoneEid = sizeof(g_sEveryOneEid);

static ABEID		g_sSystemEid(MAPI_MAILUSER, MUIDECSAB, 2);
unsigned char		*g_lpSystemEid = (unsigned char*)&g_sSystemEid;
const unsigned int	g_cbSystemEid = sizeof(g_sSystemEid);

static HRESULT CheckEntryId(unsigned int cbEntryId, const ENTRYID *lpEntryId,
    unsigned int ulId, unsigned int ulType, bool *lpbResult)
{
	bool	bResult = true;

	if (cbEntryId < sizeof(ABEID) || lpEntryId == NULL || lpbResult == NULL)
		return MAPI_E_INVALID_PARAMETER;

	auto lpEid = reinterpret_cast<const ABEID *>(lpEntryId);
	if (lpEid->ulId != ulId)
		bResult = false;
	else if (lpEid->ulType != ulType)
		bResult = false;
	else if (lpEid->ulVersion == 1 && lpEid->szExId[0])
		bResult = false;
	*lpbResult = bResult;
	return hrSuccess;
}

HRESULT EntryIdIsEveryone(unsigned int cbEntryId, const ENTRYID *lpEntryId,
    bool *lpbResult)
{
	return CheckEntryId(cbEntryId, lpEntryId, 1, MAPI_DISTLIST, lpbResult);
}

HRESULT GetNonPortableObjectType(unsigned int cbEntryId,
    const ENTRYID *lpEntryId, ULONG *lpulObjectType)
{
	if (cbEntryId < sizeof(ABEID) || lpEntryId == NULL || lpulObjectType == NULL)
		return MAPI_E_INVALID_PARAMETER;
	*lpulObjectType = reinterpret_cast<const ABEID *>(lpEntryId)->ulType;
	return hrSuccess;
}

} /* namespace */
