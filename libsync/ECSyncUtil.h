/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSYNCUTIL_H
#define ECSYNCUTIL_H

#include <string>
#include <set>
#include <kopano/zcdefs.h>
#include <mapidefs.h>

namespace KC {

typedef std::set<std::pair<unsigned int, std::string> > PROCESSEDCHANGESSET;
HRESULT ResetStream(LPSTREAM lpStream);
HRESULT CreateNullStatusStream(LPSTREAM *lppStream);

} /* namespace */

#endif // ndef ECSYNCUTIL_H
