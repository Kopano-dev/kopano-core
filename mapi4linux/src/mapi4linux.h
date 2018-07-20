/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __MAPI4LINUX_H
#define __MAPI4LINUX_H

/* common.cpp */
#define USES_IID_IUnknown
#define USES_IID_IStream		/* well, not exactly, but hey .. should be? */
#define USES_IID_ISequentialStream

/* mapix.cpp */
#define USES_IID_IProfAdmin
#define USES_IID_IMsgServiceAdmin
#define USES_IID_IMAPISession

/* mapidefs.cpp */
#define USES_IID_IMAPITable
#define USES_IID_IMAPIProp
#define USES_IID_IProfSect
#define USES_IID_IProviderAdmin

/* mapispi.cpp */
#define USES_IID_IMAPISupport
#define USES_IID_IMAPIGetSession
#define USES_IID_IMAPIFolder

#include <mapiguid.h>
#include "platform.linux.h"
#include "m4l.common.h"
#include "m4l.mapix.h"
#include "m4l.mapidefs.h"

#endif
