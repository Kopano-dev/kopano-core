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
#include <kopano/ECABEntryID.h>
#include <kopano/ECGuid.h>

#include <mapicode.h>

/* This is a copy from the definition in kcore.hpp. It's for internal use only as we
 * don't want to expose the format of the entry id. */
typedef struct ABEID {
	BYTE	abFlags[4];
	GUID	guid;
	ULONG	ulVersion;
	ULONG	ulType;
	ULONG	ulId;
	char	szExId[1];
	char	szPadding[3];

	ABEID(ULONG ulType, GUID guid, ULONG ulId) {
		memset(this, 0, sizeof(ABEID));
		this->ulType = ulType;
		this->guid = guid;
		this->ulId = ulId;
	}
} ABEID;

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

HRESULT EntryIdIsDefault(unsigned int cbEntryId, const ENTRYID *lpEntryId,
    bool *lpbResult)
{
	return CheckEntryId(cbEntryId, lpEntryId, 0, MAPI_MAILUSER, lpbResult);
}

HRESULT EntryIdIsSystem(unsigned int cbEntryId, const ENTRYID *lpEntryId,
    bool *lpbResult)
{
	return CheckEntryId(cbEntryId, lpEntryId, 2, MAPI_MAILUSER, lpbResult);
}

HRESULT EntryIdIsEveryone(unsigned int cbEntryId, const ENTRYID *lpEntryId,
    bool *lpbResult)
{
	return CheckEntryId(cbEntryId, lpEntryId, 1, MAPI_DISTLIST, lpbResult);
}

HRESULT GetNonPortableObjectId(unsigned int cbEntryId,
    const ENTRYID *lpEntryId, unsigned int *lpulObjectId)
{
	if (cbEntryId < sizeof(ABEID) || lpEntryId == NULL || lpulObjectId == NULL)
		return MAPI_E_INVALID_PARAMETER;
	*lpulObjectId = reinterpret_cast<const ABEID *>(lpEntryId)->ulId;
	return hrSuccess;
}

HRESULT GetNonPortableObjectType(unsigned int cbEntryId,
    const ENTRYID *lpEntryId, ULONG *lpulObjectType)
{
	if (cbEntryId < sizeof(ABEID) || lpEntryId == NULL || lpulObjectType == NULL)
		return MAPI_E_INVALID_PARAMETER;
	*lpulObjectType = reinterpret_cast<const ABEID *>(lpEntryId)->ulType;
	return hrSuccess;
}

HRESULT GeneralizeEntryIdInPlace(unsigned int cbEntryId, ENTRYID *lpEntryId)
{
	if (cbEntryId < sizeof(ABEID) || lpEntryId == NULL)
		return MAPI_E_INVALID_PARAMETER;

	auto lpAbeid = reinterpret_cast<ABEID *>(lpEntryId);
	switch (lpAbeid->ulVersion) {
		// A version 0 entry id is generalized by nature as it's not used to be shared
		// between servers.
		case 0:
			break;

		// A version 1 entry id can be understood by all servers in a cluster, but they
		// cannot be compared with memcpy because the ulId field is server specific.
		// However we can zero this field as the server doesn't need it to locate the
		// object referenced with the entry id.
		// An exception on this rule are SYSTEM en EVERYONE as they don't have an external
		// id. However their ulId fields are specified as 1 and 2 respectively. So every
		// server will understand the entry id, regardless of the version number. We will
		// downgrade anyway be as compatible as possible in that case.
		case 1:
			if (lpAbeid->szExId[0])	// remove the 'legacy ulId field'
				lpAbeid->ulId = 0;			
			else {								// downgrade to version 0
				assert(cbEntryId == sizeof(ABEID));
				lpAbeid->ulVersion = 0;
			}
			break;

		default:
			assert(false);
			break;
	}
	return hrSuccess;
}
