/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/ECGetText.h>
#include <kopano/charset/convert.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <cassert>

namespace KC {

/**
 * This class performs the actual conversion and caching of the translated messages.
 * Results are cached based on the pointer value, not the string content. This implies
 * two assumptions:
 * 1. Gettext always returns the same pointer for a particular translation.
 * 2. If there's no translation, the original pointer is returned. So we assume that the
 *    compiler optimized string literals to have the same address if they're equal. If
 *    this assumption is false, this will lead to more conversions, and more memory usage
 *    by the cache.
 */
class converter final {
	public:
	static std::unique_ptr<converter> &getInstance()
	{
		scoped_lock locker(s_hInstanceLock);
		if (s_lpInstance == nullptr)
			s_lpInstance.reset(new converter);
		return s_lpInstance;
	}

	/**
	 * Perform the actual cache lookup or conversion.
	 *
	 * @param[in]	lpsz	The string to convert.
	 * @return	The converted string.
	 */
	const wchar_t *convert(const char *lpsz) {
		scoped_lock l_cache(m_hCacheLock);
		auto insResult = m_cache.emplace(lpsz, L"");
		if (insResult.second) /* successful insert, so not found in cache */
			insResult.first->second.assign(m_converter.convert_to<std::wstring>(lpsz, strlen(lpsz), "UTF-8"));
		return insResult.first->second.c_str();
	}

	private:
	static std::unique_ptr<converter> s_lpInstance;
	static std::mutex s_hInstanceLock;

	convert_context	m_converter;
	std::map<const char *, std::wstring> m_cache;
	std::mutex m_hCacheLock;
};

std::mutex converter::s_hInstanceLock;
std::unique_ptr<converter> converter::s_lpInstance;

/**
 * Performs a 'regular' gettext and converts the result to a wide character string.
 *
 * @param[in]	domainname	The domain to use for the translation
 * @param[in]	msgid		The msgid of the message to be translated.
 *
 * @return	The converted, translated string.
 */
const wchar_t *kopano_dcgettext_wide(const char *domainname, const char *msgid)
{
	static bool init;
	if (!init) {
		/*
		 * Avoid gettext doing the downconversion to LC_CTYPE and
		 * killing all the Unicode characters before we had a chance of
		 * seeing them.
		 */
		bind_textdomain_codeset("kopano", "utf-8");
		init = true;
	}
	return converter::getInstance()->convert(dcgettext(domainname, msgid, LC_MESSAGES));
}

} /* namespace */
