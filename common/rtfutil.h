/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __RTFUTIL_H
#define __RTFUTIL_H

#include <kopano/zcdefs.h>
#include <kopano/CommonUtil.h>
#include <string>

namespace KC {

extern _kc_export bool isrtfhtml(const char *, unsigned int);
extern _kc_export bool isrtftext(const char *, unsigned int);

extern _kc_export HRESULT HrExtractHTMLFromRTF(const std::string &rtf, std::string &html, ULONG codepage);
extern _kc_export HRESULT HrExtractHTMLFromTextRTF(const std::string &rtf, std::string &html, ULONG codepage);
extern _kc_export HRESULT HrExtractHTMLFromRealRTF(const std::string &rtf, std::string &html, ULONG codepage);

} /* namespace */

#endif
