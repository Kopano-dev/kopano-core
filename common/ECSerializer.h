/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>

namespace KC {

#define STR_DEF_TIMEOUT 600000

class ECSerializer {
public:
	virtual ~ECSerializer() = default;
	virtual ECRESULT SetBuffer(void *lpBuffer) = 0;
	virtual ECRESULT Write(const void *ptr, size_t size, size_t nmemb) = 0;
	virtual ECRESULT Read(void *ptr, size_t size, size_t nmemb) = 0;
	virtual ECRESULT Skip(size_t size, size_t nmemb) = 0;
	virtual ECRESULT Flush() = 0;
	virtual ECRESULT Stat(ULONG *lpulRead, ULONG *lpulWritten) = 0;
};

} /* namespace */
