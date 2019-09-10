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

} /* namespace */

#endif
