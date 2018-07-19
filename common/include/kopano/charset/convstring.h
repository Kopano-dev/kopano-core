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

#ifndef convstring_INCLUDED
#define convstring_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/charset/convert.h>
#include <string>
#include <kopano/charset/utf8string.h>
#include <mapidefs.h>

namespace KC {

class _kc_export convstring _kc_final {
public:
	static convstring from_SPropValue(const SPropValue *lpsPropVal);
	_kc_hidden static convstring from_SPropValue(const SPropValue &);
	_kc_hidden convstring(void) = default;
	_kc_hidden convstring(const convstring &);
	_kc_hidden convstring(const char *);
	convstring(const wchar_t *lpsz);
	convstring(const TCHAR *lpsz, ULONG ulFlags);
	
	bool null_or_empty() const;
	
	operator utf8string() const;
	operator std::string(void) const;
	operator std::wstring(void) const;
	const char *c_str() const;
	const char *u8_str() const;

private:
	template<typename T> _kc_hidden T convert_to(void) const;
	template<typename T> _kc_hidden T convert_to(const char *tocode) const;

	const TCHAR *m_lpsz = nullptr;
	ULONG m_ulFlags = 0;
	tstring		m_str;

	mutable convert_context	m_converter;
};

} /* namespace */

#endif // ndef convstring_INCLUDED
