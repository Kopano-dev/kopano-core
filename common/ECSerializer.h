/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSERIALIZER_H
#define ECSERIALIZER_H

#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>

namespace KC {

#ifdef KNOB144
#define STR_DEF_TIMEOUT 0
#else
#define STR_DEF_TIMEOUT 600000
#endif

class ECSerializer {
public:
	virtual ~ECSerializer(void) = default;
	virtual ECRESULT SetBuffer(void *lpBuffer) = 0;

	virtual ECRESULT Write(const void *ptr, size_t size, size_t nmemb) = 0;
	virtual ECRESULT Read(void *ptr, size_t size, size_t nmemb) = 0;

	virtual ECRESULT Skip(size_t size, size_t nmemb) = 0;
	virtual ECRESULT Flush() = 0;
	
	virtual ECRESULT Stat(ULONG *lpulRead, ULONG *lpulWritten) = 0;
};

} /* namespace */

#endif /* ECSERIALIZER_H */
