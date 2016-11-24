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
#include <kopano/ECDebugPrint.h>
#include <kopano/ECDebug.h>
#include <kopano/charset/convert.h>
#include <kopano/stringutil.h>

using namespace std;

namespace KC {

namespace details {
	string conversion_helpers<string>::convert_from(const wstring &s) {
		return convert_to<string>(s);
	}

	string conversion_helpers<string>::stringify(LPCVOID lpVoid) {
		if(!lpVoid) return "NULL";

		char szBuff[33];
		sprintf(szBuff, "0x%p", lpVoid);
		return szBuff;
	}

	const string conversion_helpers<string>::strNULL = "NULL";
	const string conversion_helpers<string>::strCOMMA= ",";

	wstring conversion_helpers<wstring>::convert_from(const string &s) {
		return convert_to<wstring>(s);
	}

	wstring conversion_helpers<wstring>::stringify(LPCVOID lpVoid) {
		if(!lpVoid) return L"NULL";

		wchar_t szBuff[33];
		swprintf(szBuff, ARRAY_SIZE(szBuff), L"0x%p", lpVoid);
		return szBuff;
	}

	const wstring conversion_helpers<wstring>::strNULL = L"NULL";
	const wstring conversion_helpers<wstring>::strCOMMA= L",";
} // namespace details

} /* namespace */
