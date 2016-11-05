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

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <string>
#include "icalmapi.h"

extern "C" {

extern _kc_export bool IsOutlookUid(const std::string &);
HRESULT ICALMAPI_API HrGenerateUid(std::string *lpStrUid);
extern _kc_export HRESULT HrCreateGlobalID(ULONG named_tag, void *base, LPSPropValue *pv);
extern _kc_export HRESULT HrGetICalUidFromBinUid(SBinary &, std::string *uid);
extern _kc_export HRESULT HrMakeBinUidFromICalUid(const std::string &uid, std::string *binuid);

} /* extern "C" */

#endif
