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

#ifndef SOAPALLOC_H
#define SOAPALLOC_H

#include "soapH.h"

namespace KC {

// The automatic soap/non-soap allocator
template<typename Type>
Type* s_alloc(struct soap *soap, size_t size) {
	if(soap == NULL) {
		return new Type[size];
	} else {
		return (Type*)soap_malloc(soap, sizeof(Type) * size);
	}
}

template<typename Type>
Type* s_alloc(struct soap *soap) {
	if(soap == NULL) {
		return new Type;
	} else {
		return (Type*)soap_malloc(soap, sizeof(Type));
	}
}

inline char *s_strcpy(struct soap *soap, const char *str) {
	char *s = s_alloc<char>(soap, strlen(str)+1);

	strcpy(s, str);

	return s;
}

inline char *s_memcpy(struct soap *soap, const char *str, unsigned int len) {
	char *s = s_alloc<char>(soap, len);

	memcpy(s, str, len);

	return s;
}

template<typename Type>
inline void s_free(struct soap *soap, Type *p) {
	if(soap == NULL) {
		delete p;
	} else {
		soap_dealloc(soap, p);
	}
}

template<typename Type>
inline void s_free_array(struct soap *soap, Type *p) {
	if(soap == NULL) {
		delete [] p;
	} else {
		soap_dealloc(soap, p);
	}
}

} /* namespace */

#endif
