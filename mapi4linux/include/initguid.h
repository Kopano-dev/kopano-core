/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef M4L_INITGUID_H
#define M4L_INITGUID_H

/* Overwrite DEFINE_GUID to really create the guid data, not just declare. */
#include <kopano/platform.h>

#define INITGUID
#undef DEFINE_GUID
#define DEFINE_GUID(n,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
	GUID_EXT _kc_export constexpr const GUID n = \
		{cpu_to_le32(l), cpu_to_le16(w1), cpu_to_le16(w2), \
		{b1, b2, b3, b4, b5, b6, b7, b8}}

#endif
