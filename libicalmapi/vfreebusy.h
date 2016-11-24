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
