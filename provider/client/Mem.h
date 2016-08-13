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

#ifndef MEM_H
#define MEM_H

#include "IECPropStorage.h"

// Linked memory routines
HRESULT ECFreeBuffer(void *lpvoid);
HRESULT ECAllocateBuffer(ULONG cbSize, void **lpvoid);
HRESULT ECAllocateBufferDbg(ULONG cbSize, void **lpvoid, char *szFile, int line);
HRESULT ECAllocateMore(ULONG cbSize, void *lpBase, void **lpvoid);

HRESULT AllocNewMapiObject(ULONG ulUniqueId, ULONG ulObjId, ULONG ulObjType, MAPIOBJECT **lppMapiObject);
HRESULT FreeMapiObject(MAPIOBJECT *lpsObject);

#endif // MEM_H
