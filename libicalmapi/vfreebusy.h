/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ICALMAPI_VFREEBUSY_H
#define ICALMAPI_VFREEBUSY_H

#include "freebusy.h"
#include <libical/ical.h>
#include <list>
#include <string>

namespace KC {

HRESULT HrGetFbInfo(icalcomponent *lpFbical, time_t *tStart, time_t *tEnd, std::string *lpstrUID, std::list<std::string> *lstUsers);
extern HRESULT HrFbBlock2ICal(const FBBlock_1 *, int blocks, time_t start, time_t end, const std::string &organiser, const std::string &user, const std::string &uid, icalcomponent **out);

} /* namespace */

#endif
