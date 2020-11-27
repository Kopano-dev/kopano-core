/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/charset/convert.h>
#include <string>
#include <kopano/charset/utf8string.h>
#include <mapidefs.h>

namespace KC {

class KC_EXPORT convstring KC_FINAL {
public:
	KC_HIDDEN convstring() = default;
	KC_HIDDEN convstring(const convstring &) = delete;
	convstring(const TCHAR *lpsz, ULONG ulFlags);
	operator utf8string() const;
	operator std::string() const;

private:
	template<typename T> KC_HIDDEN T convert_to() const;

	const TCHAR *m_lpsz = nullptr;
	ULONG m_ulFlags = 0;
	tstring		m_str;

	mutable convert_context	m_converter;
};

} /* namespace */
