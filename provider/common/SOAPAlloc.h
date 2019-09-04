/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef SOAPALLOC_H
#define SOAPALLOC_H

#include <new>
#include <cstdlib>
#include "soapH.h"

namespace KC {

inline void soap_del_PointerTosearchCriteria(struct searchCriteria **a)
{
	if (a == nullptr || *a == nullptr)
		return;
	soap_del_searchCriteria(*a);
	SOAP_DELETE(nullptr, *a, struct searchCriteria);
}

// The automatic soap/non-soap allocator
template<typename Type> Type *s_alloc_nothrow(struct soap *soap, size_t size)
{
	auto p = static_cast<Type *>(soap_malloc(soap, sizeof(Type) * size));
	if (p != nullptr)
		memset(p, 0, sizeof(Type) * size);
	return p;
}

template<typename Type> Type *s_alloc_nothrow(struct soap *soap)
{
	auto p = static_cast<Type *>(soap_malloc(soap, sizeof(Type)));
	if (p != nullptr)
		memset(p, 0, sizeof(Type));
	return p;
}

template<typename Type> Type *s_alloc(struct soap *soap, size_t size)
{
	auto p = s_alloc_nothrow<Type>(soap, size);
	if (p == nullptr)
		throw std::bad_alloc();
	return p;
}

template<typename Type> Type *s_alloc(struct soap *soap)
{
	auto p = s_alloc_nothrow<Type>(soap);
	if (p == nullptr)
		throw std::bad_alloc();
	return p;
}

} /* namespace */

#endif
