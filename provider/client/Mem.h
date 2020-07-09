/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/memory.hpp>
#define ECAllocateBuffer MAPIAllocateBuffer
#define ECAllocateMore MAPIAllocateMore
#define ECFreeBuffer MAPIFreeBuffer
template<typename T> using ecmem_ptr = KC::memory_ptr<T>;
