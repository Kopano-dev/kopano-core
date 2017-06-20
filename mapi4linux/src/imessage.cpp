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

#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>

#include <imessage.h>

#include <kopano/Util.h>
#include <kopano/ECDebug.h>

SCODE OpenIMsgSession(LPMALLOC lpMalloc, ULONG ulFlags, LPMSGSESS *lppMsgSess)
{
	return MAPI_E_NO_SUPPORT;
}

void CloseIMsgSession(LPMSGSESS lpMsgSess)
{
}

SCODE OpenIMsgOnIStg(LPMSGSESS lpMsgSess, LPALLOCATEBUFFER lpAllocateBuffer,
    LPALLOCATEMORE lpAllocateMore, LPFREEBUFFER lpFreeBuffer, LPMALLOC lpMalloc,
    LPVOID lpMapiSup, LPSTORAGE lpStg, MSGCALLRELEASE *lpfMsgCallRelease,
    ULONG ulCallerData, ULONG ulFlags, LPMESSAGE *lppMsg)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT GetAttribIMsgOnIStg(LPVOID lpObject,
    const SPropTagArray *lpPropTagArray, SPropAttrArray **lppPropAttrArray)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT SetAttribIMsgOnIStg(void *object, const SPropTagArray *,
    const SPropAttrArray *, SPropProblemArray **)
{
	return MAPI_E_NO_SUPPORT;
}

SCODE MapStorageSCode(SCODE StgSCode)
{
	return StgSCode;
}
