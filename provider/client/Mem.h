/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef MEM_H
#define MEM_H

#include <kopano/memory.hpp>
#include "IECPropStorage.h"

// Linked memory routines
HRESULT ECFreeBuffer(void *lpvoid);
HRESULT ECAllocateBuffer(ULONG cbSize, void **lpvoid);
HRESULT ECAllocateMore(ULONG cbSize, void *lpBase, void **lpvoid);

class client_delete {
	public:
	void operator()(void *x) const { ECFreeBuffer(x); }
};

template<typename T> using ecmem_ptr = KC::memory_ptr<T, client_delete>;

#endif // MEM_H
