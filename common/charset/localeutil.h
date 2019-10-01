/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_CHARSET_UTIL_H
#define EC_CHARSET_UTIL_H

#include <kopano/zcdefs.h>

namespace KC {

extern KC_EXPORT bool forceUTF8Locale(bool output, std::string *prev_lcoale = nullptr);
locale_t createUTF8Locale();

} /* namespace */

#endif
