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

inline unsigned char *s_memcpy(struct soap *soap, const void *str, unsigned int len)
{
	auto s = s_alloc<unsigned char>(soap, len);
	memcpy(s, str, len);
	return s;
}

template<typename Type>
inline void s_free(struct soap *soap, Type *p) {
	/*
	 * Horrible implementation detail because gsoap does not expose
	 * a proper function that is completely symmetric to soap_malloc.
	 */
	if (soap == NULL)
		SOAP_FREE(soap, p);
	else
		soap_dealloc(soap, p);
}

} /* namespace */

#endif
