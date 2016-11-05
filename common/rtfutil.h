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

#ifndef __RTFUTIL_H
#define __RTFUTIL_H

#include <kopano/zcdefs.h>
#include <kopano/CommonUtil.h>
#include <string>

extern "C" {

extern _kc_export bool isrtfhtml(const char *, unsigned int);
extern _kc_export bool isrtftext(const char *, unsigned int);

extern _kc_export HRESULT HrExtractHTMLFromRTF(const std::string &rtf, std::string &html, ULONG codepage);
extern _kc_export HRESULT HrExtractHTMLFromTextRTF(const std::string &rtf, std::string &html, ULONG codepage);
extern _kc_export HRESULT HrExtractHTMLFromRealRTF(const std::string &rtf, std::string &html, ULONG codepage);
extern _kc_export HRESULT HrExtractBODYFromTextRTF(const std::string &rtf, std::wstring &bodyout);

}

#endif
