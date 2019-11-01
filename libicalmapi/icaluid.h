/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_ICAL_UID_H
#define EC_ICAL_UID_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <string>

namespace KC {

extern KC_EXPORT bool IsOutlookUid(const std::string &);
HRESULT HrGenerateUid(std::string *lpStrUid);
extern KC_EXPORT HRESULT HrCreateGlobalID(unsigned int named_tag, void *base, SPropValue **pv);
extern KC_EXPORT HRESULT HrGetICalUidFromBinUid(const SBinary &, std::string *uid);
extern KC_EXPORT HRESULT HrMakeBinUidFromICalUid(const std::string &uid, std::string *binuid);
extern KC_EXPORT HRESULT HrMakeBinaryUID(const std::string &strUid, void *base, SPropValue *lpPropValue);

} /* namespace */

#endif
