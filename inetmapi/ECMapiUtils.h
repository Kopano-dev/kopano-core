/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECMAPIUTILS
#define ECMAPIUTILS

#include <string>
#include <vmime/dateTime.hpp>

namespace KC {

extern FILETIME vmimeDatetimeToFiletime(const vmime::datetime &dt);
extern vmime::datetime FiletimeTovmimeDatetime(const FILETIME &ft);
const char *ext_to_mime_type(const char *ext, const char *def = "application/octet-stream");
const char *mime_type_to_ext(const char *mime_type, const char *def = "txt");

} /* namespace */

#endif
