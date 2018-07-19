/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef VALARM_H
#define VALARM_H

#include "icalitem.h"
#include <libical/ical.h>

namespace KC {

/* valarm.cpp/h contains functions to convert reminder <-> valarm */

HRESULT HrParseReminder(LONG lRemindBefore, time_t ttReminderTime, bool bTask, icalcomponent **lppAlarm);
HRESULT HrParseVAlarm(icalcomponent *lpComp, LONG *lpulRemindBefore, time_t *lpttReminderTime, bool *lpbReminderSet);

} /* namespace */

#endif
