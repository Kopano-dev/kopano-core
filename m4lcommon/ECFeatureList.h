/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef EC_FEATURELIST_H
#define EC_FEATURELIST_H

#include <kopano/platform.h>
#include <string>
#include <set>

namespace KC {

///< all kopano features that are checked for access before allowing it
static const char *const kopano_features[] = {
	"imap", "pop3", "mobile", "outlook", "webapp"
};

} /* namespace */

#endif
