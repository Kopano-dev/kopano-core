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
#include <kopano/Trace.h>
#include "Mem.h"

// We don't want that here
#undef ECAllocateBuffer

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

HRESULT AllocNewMapiObject(ULONG ulUniqueId, ULONG ulObjId, ULONG ulObjType, MAPIOBJECT **lppMapiObject)
{
	MAPIOBJECT *sMapiObject;

	sMapiObject = new MAPIOBJECT;
	sMapiObject->lstChildren = new ECMapiObjects;
	sMapiObject->lstDeleted = new std::list<ULONG>;
	sMapiObject->lstAvailable = new std::list<ULONG>;
	sMapiObject->lstModified = new std::list<ECProperty>;
	sMapiObject->lstProperties = new std::list<ECProperty>;
	sMapiObject->lpInstanceID = NULL;
	sMapiObject->cbInstanceID = 0;
	sMapiObject->bChangedInstance = false;
	sMapiObject->bChanged = false;
	sMapiObject->bDelete = false;
	sMapiObject->ulUniqueId = ulUniqueId;
	sMapiObject->ulObjId = ulObjId;
	sMapiObject->ulObjType = ulObjType;
	*lppMapiObject = sMapiObject;

	return hrSuccess;
}

HRESULT FreeMapiObject(MAPIOBJECT *lpsObject)
{
	delete lpsObject->lstAvailable;
	delete lpsObject->lstDeleted;
	delete lpsObject->lstModified;
	delete lpsObject->lstProperties;

	for (const auto &obj : *lpsObject->lstChildren)
		FreeMapiObject(obj);
	delete lpsObject->lstChildren;

	if (lpsObject->lpInstanceID)
		ECFreeBuffer(lpsObject->lpInstanceID);

	delete lpsObject;

	return hrSuccess;
}
