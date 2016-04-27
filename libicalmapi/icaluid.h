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

#ifndef __ICAL_UID_H
#define __ICAL_UID_H

#include <mapidefs.h>
#include <string>
#include "icalmapi.h"

bool ICALMAPI_API IsOutlookUid(const std::string &strUid);
HRESULT ICALMAPI_API HrGenerateUid(std::string *lpStrUid);
HRESULT ICALMAPI_API HrCreateGlobalID(ULONG ulNamedTag, void *base, LPSPropValue *lppPropVal);
HRESULT ICALMAPI_API HrGetICalUidFromBinUid(SBinary &sBin, std::string *lpStrUid);
HRESULT ICALMAPI_API HrMakeBinUidFromICalUid(const std::string &strUid, std::string *lpStrBinUid);

#endif
