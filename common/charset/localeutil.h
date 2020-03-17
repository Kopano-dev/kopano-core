/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>

namespace KC {

extern KC_EXPORT bool forceUTF8Locale(bool output, std::string *prev_lcoale = nullptr);
locale_t createUTF8Locale();

} /* namespace */
