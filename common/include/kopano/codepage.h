/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef CODEPAGE_H
#define CODEPAGE_H

#include <kopano/zcdefs.h>

namespace KC {

extern _kc_export HRESULT HrGetCharsetByCP(ULONG cp, const char **ret);
extern _kc_export HRESULT HrGetCPByCharset(const char *cset, ULONG *cp);

} /* namespace */

#endif
