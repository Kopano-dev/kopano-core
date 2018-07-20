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
HRESULT HrFbBlock2ICal(FBBlock_1 *lpsFbblk, LONG ulBlocks, time_t tStart, time_t tEnd, const std::string &strOrganiser, const std::string &strUser, const std::string &strUID, icalcomponent **lpicFbComponent);

} /* namespace */

#endif
