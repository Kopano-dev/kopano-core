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
#include <kopano/Trace.h>

SCODE __stdcall OpenIMsgSession(LPMALLOC lpMalloc, ULONG ulFlags, LPMSGSESS *lppMsgSess)
{
	TRACE_MAPILIB(TRACE_ENTRY, "OpenIMsgSession", "");

	SCODE sc = MAPI_E_NO_SUPPORT;
	TRACE_MAPILIB(TRACE_RETURN, "OpenIMsgSession", "sc=0x%08X", sc);
	return sc;
}

void __stdcall CloseIMsgSession(LPMSGSESS lpMsgSess)
{
	TRACE_MAPILIB(TRACE_ENTRY, "CloseIMsgSession", "");
	TRACE_MAPILIB(TRACE_RETURN, "CloseIMsgSession", "");
}

SCODE __stdcall OpenIMsgOnIStg(	LPMSGSESS lpMsgSess, LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore,
								LPFREEBUFFER lpFreeBuffer, LPMALLOC lpMalloc, LPVOID lpMapiSup, LPSTORAGE lpStg, 
								MSGCALLRELEASE *lpfMsgCallRelease, ULONG ulCallerData, ULONG ulFlags, LPMESSAGE *lppMsg )
{
	TRACE_MAPILIB(TRACE_ENTRY, "OpenIMsgOnIStg", "");
	
	SCODE sc = MAPI_E_NO_SUPPORT;
	TRACE_MAPILIB(TRACE_RETURN, "OpenIMsgOnIStg", "sc=0x%08x", sc);
	return sc;
}

HRESULT __stdcall GetAttribIMsgOnIStg(LPVOID lpObject, LPSPropTagArray lpPropTagArray, LPSPropAttrArray *lppPropAttrArray)
{
	TRACE_MAPILIB(TRACE_ENTRY, "GetAttribIMsgOnIStg", "");
	TRACE_MAPILIB(TRACE_RETURN, "GetAttribIMsgOnIStg", "");
	return MAPI_E_NO_SUPPORT;
}

HRESULT __stdcall SetAttribIMsgOnIStg(LPVOID lpObject, LPSPropTagArray lpPropTags, LPSPropAttrArray lpPropAttrs,
									  LPSPropProblemArray *lppPropProblems)
{
	TRACE_MAPILIB(TRACE_ENTRY, "SetAttribIMsgOnIStg", "");
	TRACE_MAPILIB(TRACE_RETURN, "SetAttribIMsgOnIStg", "");
	return MAPI_E_NO_SUPPORT;
}

SCODE __stdcall MapStorageSCode( SCODE StgSCode )
{
	TRACE_MAPILIB(TRACE_ENTRY, "MapStorageSCode", "");
	return StgSCode;
}
