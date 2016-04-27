/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef TIMEZONE_UTIL_H
#define TIMEZONE_UTIL_H



/* MAPI TimeZoneStruct named property */
typedef struct _TIMEZONE_STRUCT {
	// The bias values (bias, stdbias and dstbias) are the opposite of what you expect.
	// Thus +1 hour becomes -60, +2 hours becomes -120, -3 becomes +180

	LONG lBias;					/* nl: -1*60, jp: -9*60 */
	LONG lStdBias;				/* nl: 0, jp: 0 (wintertijd) */
	LONG lDstBias;				/* nl: -1*60: jp: 0 (zomertijd) */

	WORD wStdYear;
	SYSTEMTIME stStdDate;		/* 2->3, dus 3 in wHour */

	WORD wDstYear;
	SYSTEMTIME stDstDate;		/* 3->2, dus 2 in wHour */
} TIMEZONE_STRUCT;

time_t getDateByYearMonthWeekDayHour(WORD year, WORD month, WORD week, WORD day, WORD hour);
LONG getTZOffset(time_t date, TIMEZONE_STRUCT sTimeZone);

time_t LocalToUTC(time_t local, TIMEZONE_STRUCT sTimeZone);
time_t UTCToLocal(time_t utc, TIMEZONE_STRUCT sTimeZone);


#endif
