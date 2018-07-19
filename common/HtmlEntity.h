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
#pragma once
#include <kopano/zcdefs.h>

namespace KC {

class _kc_export CHtmlEntity _kc_final {
public:
	_kc_hidden static wchar_t toChar(const wchar_t *);
	_kc_hidden static const wchar_t *toName(wchar_t);
	static bool CharToHtmlEntity(WCHAR c, std::wstring &strHTML);
	static bool validateHtmlEntity(const std::wstring &strEntity);
	static WCHAR HtmlEntityToChar(const std::wstring &strEntity);
};

} /* namespace */
