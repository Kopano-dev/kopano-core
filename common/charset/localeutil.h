/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __CHARSET_UTIL_H
#define __CHARSET_UTIL_H

#include <kopano/zcdefs.h>

namespace KC {

extern _kc_export bool forceUTF8Locale(bool output, std::string *prev_lcoale = nullptr);
locale_t createUTF8Locale();

} /* namespace */

#endif
