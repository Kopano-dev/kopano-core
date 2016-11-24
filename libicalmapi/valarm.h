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
