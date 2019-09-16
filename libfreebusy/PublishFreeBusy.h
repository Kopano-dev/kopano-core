/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef FB_PUBLISHFREEBUSY_H
#define FB_PUBLISHFREEBUSY_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <ctime>

class IMAPISession;
class IMsgStore;

namespace KC {

extern _kc_export HRESULT HrPublishDefaultCalendar(IMAPISession *, IMsgStore *, time_t start, ULONG months);

} /* namespace */

#endif
