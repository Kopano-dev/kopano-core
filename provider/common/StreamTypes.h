/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef STREAMTYPES_H
#define STREAMTYPES_H

#include <mapidefs.h>

namespace KC {

// State information
struct ECStreamInfo {
	unsigned long	ulStep;
	unsigned long	cbPropVals;
	LPSPropValue	lpsPropVals;
};

} /* namespace */

#endif // ndef STREAMTYPES_H
