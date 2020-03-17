/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>

namespace KC {

extern KC_EXPORT HRESULT HrGetCharsetByCP(unsigned int cp, const char **ret);
extern KC_EXPORT HRESULT HrGetCPByCharset(const char *cset, unsigned int *cp);

} /* namespace */
