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

#ifndef _M4L_IMESSAGE_H_
#define _M4L_IMESSAGE_H_
#define _IMESSAGE_H_

extern "C"
{

typedef struct _MSGSESS *LPMSGSESS;

typedef void (__stdcall MSGCALLRELEASE)( ULONG ulCallerData, LPMESSAGE lpMessage );

STDAPI_(SCODE) OpenIMsgSession(LPMALLOC lpMalloc, ULONG ulFlags, LPMSGSESS *lppMsgSess);

STDAPI_(void) CloseIMsgSession(LPMSGSESS lpMsgSess);

STDAPI_(SCODE) OpenIMsgOnIStg(LPMSGSESS lpMsgSess, LPALLOCATEBUFFER lpAllocateBuffer,
								LPALLOCATEMORE lpAllocateMore, LPFREEBUFFER lpFreeBuffer, LPMALLOC lpMalloc,
								LPVOID lpMapiSup, LPSTORAGE lpStg, MSGCALLRELEASE *lpfMsgCallRelease,
								ULONG ulCallerData, ULONG ulFlags, LPMESSAGE *lppMsg);

#define IMSG_NO_ISTG_COMMIT		((ULONG) 0x00000001)

#define PROPATTR_MANDATORY		((ULONG) 0x00000001)
#define PROPATTR_READABLE		((ULONG) 0x00000002)
#define PROPATTR_WRITEABLE		((ULONG) 0x00000004)

#define PROPATTR_NOT_PRESENT	((ULONG) 0x00000008)

typedef struct _SPropAttrArray
{
	ULONG	cValues;							
	ULONG	aPropAttr[MAPI_DIM];
} SPropAttrArray, * LPSPropAttrArray;

#define CbNewSPropAttrArray(_cattr) \
	(offsetof(SPropAttrArray,aPropAttr) + (_cattr)*sizeof(ULONG))
#define CbSPropAttrArray(_lparray) \
	(offsetof(SPropAttrArray,aPropAttr) + \
	(UINT)((_lparray)->cValues)*sizeof(ULONG))

#define SizedSPropAttrArray(_cattr, _name) \
struct _SPropAttrArray_ ## _name \
{ \
	ULONG	cValues; \
	ULONG	aPropAttr[_cattr]; \
} _name

STDAPI_(HRESULT) GetAttribIMsgOnIStg(LPVOID lpObject, LPSPropTagArray lpPropTagArray, 
						   LPSPropAttrArray *lppPropAttrArray );

STDAPI_(HRESULT) SetAttribIMsgOnIStg(LPVOID lpObject, LPSPropTagArray lpPropTags, 
						   LPSPropAttrArray lpPropAttrs, LPSPropProblemArray *lppPropProblems);

STDAPI_(SCODE) MapStorageSCode( SCODE StgSCode );

} //extern "C"

#endif	/* _M4L_IMESSAGE_H_ */

