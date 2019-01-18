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
#include <cstdint>
#include <kopano/platform.h>
#include <kopano/ECABEntryID.h>
#include <kopano/ECGuid.h>

#include <mapicode.h>

namespace KC {

/* This is a copy from the definition in kcore.hpp. It's for internal use only as we
 * don't want to expose the format of the entry id. */
struct ABEID {
	BYTE abFlags[4]{};
	GUID guid{};
	uint32_t ulVersion = 0, ulType = 0, ulId = 0;
	char szExId[];

	ABEID(ABEID &&) = delete;
};

struct ABEID_FIXED {
	BYTE abFlags[4]{};
	GUID guid{};
	uint32_t ulVersion = 0, ulType = 0, ulId = 0;
	char pad[4]{};

	constexpr ABEID_FIXED(unsigned int type, const GUID &g, unsigned int id) :
		guid(g), ulType(type), ulId(id)
	{}
};

static const ABEID_FIXED g_sDefaultEid(MAPI_MAILUSER, MUIDECSAB, 0);
unsigned char		*g_lpDefaultEid = (unsigned char*)&g_sDefaultEid;
const unsigned int	g_cbDefaultEid = sizeof(g_sDefaultEid);

static const ABEID_FIXED g_sEveryOneEid(MAPI_DISTLIST, MUIDECSAB, 1);
unsigned char		*g_lpEveryoneEid = (unsigned char*)&g_sEveryOneEid;
const unsigned int	g_cbEveryoneEid = sizeof(g_sEveryOneEid);

static const ABEID_FIXED g_sSystemEid(MAPI_MAILUSER, MUIDECSAB, 2);
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
	else if (lpEid->ulVersion == 1 && cbEntryId > sizeof(ABEID) && lpEid->szExId[0] != '\0')
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
