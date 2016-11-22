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

#include <kopano/platform.h>
#include <pthread.h>
#include <mapix.h>
#include <ctime>
#include <iostream>

#include "ECMapiUtils.h"

// Returns the FILETIME as GM-time
FILETIME vmimeDatetimeToFiletime(vmime::datetime dt) {
	FILETIME sFiletime;
	struct tm when;
	int iYear, iMonth, iDay, iHour, iMinute, iSecond, iZone;
	time_t lTmpTime;

	dt.getDate( iYear, iMonth, iDay );	
	dt.getTime( iHour, iMinute, iSecond, iZone );

	when.tm_hour	= iHour;	
	when.tm_min		= iMinute - iZone; // Zone is expressed in minutes. mktime() will normalize negative values or values over 60
	when.tm_sec		= iSecond;
	when.tm_mon		= iMonth - 1;	
	when.tm_mday	= iDay;	
	when.tm_year	= iYear - 1900;
	when.tm_isdst	= -1;		// ignore dst

	lTmpTime = timegm(&when);

	UnixTimeToFileTime(lTmpTime, &sFiletime);
	return sFiletime;
}

vmime::datetime FiletimeTovmimeDatetime(FILETIME ft) {
	time_t tmp;
	struct tm convert;

	FileTimeToUnixTime(ft, &tmp);
	gmtime_safe(&tmp, &convert);
	return vmime::datetime(convert.tm_year + 1900, convert.tm_mon + 1, convert.tm_mday, convert.tm_hour, convert.tm_min, convert.tm_sec);
}
