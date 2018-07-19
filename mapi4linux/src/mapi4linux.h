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
