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

#ifdef WIN32
extern HMODULE g_hLibMapi;
#endif

SCODE __stdcall OpenIMsgSession(LPMALLOC lpMalloc, ULONG ulFlags, LPMSGSESS *lppMsgSess)
{
	TRACE_MAPILIB(TRACE_ENTRY, "OpenIMsgSession", "");

	SCODE sc = MAPI_E_NO_SUPPORT;
#ifdef WIN32
	typedef SCODE (__stdcall MS_OpenIMsgSession)(LPMALLOC lpMalloc, ULONG ulFlags, LPMSGSESS *lppMsgSess);

	MS_OpenIMsgSession* lpOImsg = NULL;

	if (!g_hLibMapi) {
		sc = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	lpOImsg = (MS_OpenIMsgSession *)GetProcAddress(g_hLibMapi, "OpenIMsgSession@12");
	if(!lpOImsg) {
		goto exit;
	}

	sc = lpOImsg(lpMalloc, ulFlags, lppMsgSess);

exit:
	;
#endif
	TRACE_MAPILIB(TRACE_RETURN, "OpenIMsgSession", "sc=0x%08X", sc);
	return sc;
}

void __stdcall CloseIMsgSession(LPMSGSESS lpMsgSess)
{
	TRACE_MAPILIB(TRACE_ENTRY, "CloseIMsgSession", "");

#ifdef WIN32
	typedef void (__stdcall MS_CloseIMsgSession)(LPMSGSESS lpMsgSess);

	MS_CloseIMsgSession* lpCImsg = NULL;

	if (!g_hLibMapi) {
		goto exit;
	}

	lpCImsg = (MS_CloseIMsgSession *)GetProcAddress(g_hLibMapi, "CloseIMsgSession@4");
	if(!lpCImsg) {
		goto exit;
	}

	lpCImsg(lpMsgSess);

exit:

	;
#endif
	TRACE_MAPILIB(TRACE_RETURN, "CloseIMsgSession", "");
}

SCODE __stdcall OpenIMsgOnIStg(	LPMSGSESS lpMsgSess, LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore,
								LPFREEBUFFER lpFreeBuffer, LPMALLOC lpMalloc, LPVOID lpMapiSup, LPSTORAGE lpStg, 
								MSGCALLRELEASE *lpfMsgCallRelease, ULONG ulCallerData, ULONG ulFlags, LPMESSAGE *lppMsg )
{
	TRACE_MAPILIB(TRACE_ENTRY, "OpenIMsgOnIStg", "");
	
	SCODE sc = MAPI_E_NO_SUPPORT;

#ifdef WIN32
	typedef SCODE (__stdcall MS_OpenIMsgOnIStg)(	LPMSGSESS lpMsgSess, LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore,
								LPFREEBUFFER lpFreeBuffer, LPMALLOC lpMalloc, LPVOID lpMapiSup, LPSTORAGE lpStg, 
								MSGCALLRELEASE *lpfMsgCallRelease, ULONG ulCallerData, ULONG ulFlags, LPMESSAGE *lppMsg );

	MS_OpenIMsgOnIStg* lpMSOI = NULL;

	if (!g_hLibMapi) {
		sc = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	lpMSOI = (MS_OpenIMsgOnIStg *)GetProcAddress(g_hLibMapi, "OpenIMsgOnIStg@44");
	if(!lpMSOI) {
		goto exit;
	}

	sc = lpMSOI(lpMsgSess, lpAllocateBuffer, lpAllocateMore, lpFreeBuffer, lpMalloc, 
				lpMapiSup, lpStg, lpfMsgCallRelease, ulCallerData, ulFlags, lppMsg);
	
exit:
#endif
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
