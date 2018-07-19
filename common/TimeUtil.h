/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef TIMEZONE_UTIL_H
#define TIMEZONE_UTIL_H

#include <kopano/zcdefs.h>

namespace KC {

/* MAPI TimeZoneStruct named property */
struct TIMEZONE_STRUCT {
	// The bias values (bias, stdbias and dstbias) are the opposite of what you expect.
	// Thus +1 hour becomes -60, +2 hours becomes -120, -3 becomes +180

	LONG lBias;					/* nl: -1*60, jp: -9*60 */
	LONG lStdBias;				/* nl: 0, jp: 0 (wintertijd) */
	LONG lDstBias;				/* nl: -1*60: jp: 0 (zomertijd) */

	WORD wStdYear;
	SYSTEMTIME stStdDate;		/* 2->3, dus 3 in wHour */

	WORD wDstYear;
	SYSTEMTIME stDstDate;		/* 3->2, dus 2 in wHour */

	void le_to_cpu()
	{
		lBias = le32_to_cpu(lBias);
		lStdBias = le32_to_cpu(lStdBias);
		lDstBias = le32_to_cpu(lDstBias);
		wStdYear = le16_to_cpu(wStdYear);
		KC::le_to_cpu(stStdDate);
		wDstYear = le16_to_cpu(wDstYear);
		KC::le_to_cpu(stDstDate);
	}
};

extern _kc_export time_t LocalToUTC(time_t local, const TIMEZONE_STRUCT &);
extern _kc_export time_t UTCToLocal(time_t utc, const TIMEZONE_STRUCT &);

} /* namespace */

#endif
