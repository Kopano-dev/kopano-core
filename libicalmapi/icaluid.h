/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __ICAL_UID_H
#define __ICAL_UID_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <string>

namespace KC {

extern _kc_export bool IsOutlookUid(const std::string &);
HRESULT HrGenerateUid(std::string *lpStrUid);
extern _kc_export HRESULT HrCreateGlobalID(ULONG named_tag, void *base, LPSPropValue *pv);
extern _kc_export HRESULT HrGetICalUidFromBinUid(const SBinary &, std::string *uid);
extern _kc_export HRESULT HrMakeBinUidFromICalUid(const std::string &uid, std::string *binuid);
extern _kc_export HRESULT HrMakeBinaryUID(const std::string &strUid, void *base, SPropValue *lpPropValue);

} /* namespace */

#endif
