/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef M4L_INITGUID_H
#define M4L_INITGUID_H

/* Overwrite DEFINE_GUID to really create the guid data, not just declare. */
#include <kopano/zcdefs.h>
#include <kopano/platform.h>

#define INITGUID
#undef DEFINE_GUID
#define DEFINE_GUID KC_DEFINE_GUID
#endif
