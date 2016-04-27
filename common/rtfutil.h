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

#include <kopano/CommonUtil.h>
#include <string>

bool isrtfhtml(const char *buf, unsigned int len);
bool isrtftext(const char *buf, unsigned int len);

HRESULT HrExtractHTMLFromRTF(const std::string &lpStrRTFIn, std::string &lpStrHTMLOut, ULONG ulCodepage);
HRESULT HrExtractHTMLFromTextRTF(const std::string &lpStrRTFIn, std::string &lpStrHTMLOut, ULONG ulCodepage);
HRESULT HrExtractHTMLFromRealRTF(const std::string &lpStrRTFIn, std::string &lpStrHTMLOut, ULONG ulCodepage);
HRESULT HrExtractBODYFromTextRTF(const std::string &lpStrRTFIn, std::wstring &strBodyOut);

#endif
