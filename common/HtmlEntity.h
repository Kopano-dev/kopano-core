/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>

namespace KC {

class _kc_export CHtmlEntity KC_FINAL {
public:
	_kc_hidden static wchar_t toChar(const wchar_t *);
	_kc_hidden static const wchar_t *toName(wchar_t);
	static bool CharToHtmlEntity(wchar_t c, std::wstring &html);
	static bool validateHtmlEntity(const std::wstring &strEntity);
	static wchar_t HtmlEntityToChar(const std::wstring &entity);
};

} /* namespace */
