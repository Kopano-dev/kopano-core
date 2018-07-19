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
#include <mapispi.h>
#include <mapicode.h>
#include "Mem.h"

LPMALLOC			_pmalloc;
LPALLOCATEBUFFER	_pfnAllocBuf;
LPALLOCATEMORE		_pfnAllocMore;
LPFREEBUFFER		_pfnFreeBuf;
HINSTANCE			_hInstance;

// This is the same as client-side MAPIFreeBuffer, but uses
// the linked memory routines passed in during MSProviderInit()

// Use the EC* functions to allocate memory that will be
// passed back to the caller through MAPI

HRESULT ECFreeBuffer(void *lpvoid) {
	if(_pfnFreeBuf == NULL)
		return MAPI_E_CALL_FAILED;
	else return _pfnFreeBuf(lpvoid);
}

HRESULT ECAllocateBuffer(ULONG cbSize, void **lpvoid) {
	if(_pfnAllocBuf == NULL)
		return MAPI_E_CALL_FAILED;
	else return _pfnAllocBuf(cbSize, lpvoid);
}

HRESULT ECAllocateMore(ULONG cbSize, void *lpBase, void **lpvoid) {
	if(_pfnAllocMore == NULL)
		return MAPI_E_CALL_FAILED;
	else return _pfnAllocMore(cbSize, lpBase, lpvoid);
}

MAPIOBJECT::~MAPIOBJECT()
{
	for (auto &obj : lstChildren)
		delete obj;
	if (lpInstanceID != nullptr)
		ECFreeBuffer(lpInstanceID);
}
