/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_RTFUTIL_H
#define EC_RTFUTIL_H

#include <kopano/zcdefs.h>
#include <kopano/CommonUtil.h>
#include <string>

namespace KC {

extern KC_EXPORT bool isrtfhtml(const char *, unsigned int);
extern KC_EXPORT bool isrtftext(const char *, unsigned int);
extern KC_EXPORT HRESULT HrExtractHTMLFromRTF(const std::string &rtf, std::string &html, unsigned int codepage);
extern KC_EXPORT HRESULT HrExtractHTMLFromTextRTF(const std::string &rtf, std::string &html, unsigned int codepage);
extern KC_EXPORT HRESULT HrExtractHTMLFromRealRTF(const std::string &rtf, std::string &html, unsigned int codepage);

} /* namespace */

#endif
