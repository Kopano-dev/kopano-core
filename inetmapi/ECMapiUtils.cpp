/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <pthread.h>
#include <mapix.h>
#include <ctime>
#include <iostream>
#include <kopano/timeutil.hpp>
#include "ECMapiUtils.h"

namespace KC {

// Returns the FILETIME as GM-time
FILETIME vmimeDatetimeToFiletime(const vmime::datetime &dt)
{
	struct tm when;
	int iYear, iMonth, iDay, iHour, iMinute, iSecond, iZone;

	dt.getDate(iYear, iMonth, iDay);
	dt.getTime( iHour, iMinute, iSecond, iZone );

	when.tm_hour	= iHour;
	when.tm_min		= iMinute - iZone; // Zone is expressed in minutes. mktime() will normalize negative values or values over 60
	when.tm_sec		= iSecond;
	when.tm_mon	= iMonth - 1;
	when.tm_mday	= iDay;
	when.tm_year	= iYear - 1900;
	when.tm_isdst	= -1;		// ignore dst
	auto lTmpTime = timegm(&when);
	return UnixTimeToFileTime(lTmpTime);
}

vmime::datetime FiletimeTovmimeDatetime(const FILETIME &ft)
{
	struct tm convert;
	gmtime_safe(FileTimeToUnixTime(ft), &convert);
	return vmime::datetime(convert.tm_year + 1900, convert.tm_mon + 1, convert.tm_mday, convert.tm_hour, convert.tm_min, convert.tm_sec);
}

static constexpr const struct {
	const char *ext, *mime_type;
} mime_types[] = {
	{"bin", "application/octet-stream"},
	{"exe", "application/octet-stream"},
	{"ai", "application/postscript"},
	{"eps", "application/postscript"},
	{"ps", "application/postscript"},
	{"pdf", "application/pdf"},
	{"rtf", "application/rtf"},
	{"zip", "application/zip"},
	{"doc", "application/msword"},
	{"dot", "application/msword"},
	{"mdb", "application/x-msaccess"},
	{"xla", "application/vnd.ms-excel"},
	{"xls", "application/vnd.ms-excel"},
	{"xlt", "application/vnd.ms-excel"},
	{"xlw", "application/vnd.ms-excel"},
	{"pot", "application/vnd.ms-powerpoint"},
	{"ppt", "application/vnd.ms-powerpoint"},
	{"pps", "application/vnd.ms-powerpoint"},
	{"mpp", "application/vnd.ms-project"},
	{"edi", "application/edifact"},
	{"docm", "application/vnd.ms-word.document.macroEnabled.12"},
	{"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
	{"dotm", "application/vnd.ms-word.template.macroEnabled.12"},
	{"dotx", "application/vnd.openxmlformats-officedocument.wordprocessingml.template"},
	{"potm", "application/vnd.ms-powerpoint.template.macroEnabled.12"},
	{"potx", "application/vnd.openxmlformats-officedocument.presentationml.template"},
	{"ppam", "application/vnd.ms-powerpoint.addin.macroEnabled.12"},
	{"ppsm", "application/vnd.ms-powerpoint.slideshow.macroEnabled.12"},
	{"ppsx", "application/vnd.openxmlformats-officedocument.presentationml.slideshow"},
	{"pptm", "application/vnd.ms-powerpoint.presentation.macroEnabled.12"},
	{"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
	{"xlam", "application/vnd.ms-excel.addin.macroEnabled.12"},
	{"xlsb", "application/vnd.ms-excel.sheet.binary.macroEnabled.12"},
	{"xlsm", "application/vnd.ms-excel.sheet.macroEnabled.12"},
	{"xltm", "application/vnd.ms-excel.template.macroEnabled.12"},
	{"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
	{"xltx", "application/vnd.openxmlformats-officedocument.spreadsheetml.template"},
	{"ua", "audio/basic"},
	{"wav", "audio/x-wav"},
	{"mid", "audio/x-midi"},
	{"gif", "image/gif"},
	{"jpg", "image/jpeg"},
	{"jpe", "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"png", "image/png"},
	{"bmp", "image/x-ms-bmp"},
	{"tiff", "image/tiff"},
	{"xbm", "image/xbm"},
	{"mpg", "video/mpeg"},
	{"mpe", "video/mpeg"},
	{"mpeg", "video/mpeg"},
	{"qt", "video/quicktime"},
	{"mov", "video/quicktime"},
	{"avi", "video/x-msvideo"},
	{"ics", "text/calendar"},
	{"html", "text/html"},
};

const char *ext_to_mime_type(const char *ext, const char *def)
{
	for (const auto &elem : mime_types)
		if (strcasecmp(ext, elem.ext) == 0)
			return elem.mime_type;
	return def;
}

const char *mime_type_to_ext(const char *mime_type, const char *def)
{
	for (const auto &elem : mime_types)
		if (strcasecmp(mime_type, elem.mime_type) == 0)
			return elem.ext;
	return def;
}

} /* namespace */
