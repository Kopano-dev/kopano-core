/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <string>
#include <iostream>
#include <utility>
#include <clocale>
#include <cstring>
#include <unistd.h>
#include "localeutil.h"

namespace KC {

locale_t createUTF8Locale()
{
	/* this trick only works on newer distros */
	auto loc = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
	if (loc)
		return loc;

	std::string new_locale;
	char *cur_locale = setlocale(LC_CTYPE, NULL);
	char *dot = strchr(cur_locale, '.');
	if (dot) {
		if (strcmp(dot+1, "UTF-8") == 0 || strcmp(dot+1, "utf8") == 0) {
			loc = newlocale(LC_CTYPE_MASK, cur_locale, nullptr);
			goto exit;
		}
		// strip current charset selector, to be replaced
		*dot = '\0';
	}
	new_locale = std::string(cur_locale) + ".UTF-8";
	loc = newlocale(LC_CTYPE_MASK, new_locale.c_str(), nullptr);
	if (loc)
		return loc;
	loc = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", nullptr);
exit:
	// too bad, but I don't want to return an unusable object
	if (!loc)
		loc = newlocale(LC_CTYPE_MASK, "C", nullptr);
	return loc;
}

/**
 * Initializes the locale to the current language, forced in UTF-8.
 *
 * @param[in]	bOutput	Print errors during init to stderr
 * @param[out]	lpstrLocale Last locale trying to set (optional)
 * @retval	true	successfully initialized
 * @retval	false	error during initialization
 */
bool forceUTF8Locale(bool bOutput, std::string *lpstrLastSetLocale)
{
	const char *orig_locale = setlocale(LC_CTYPE, "");
	if (orig_locale == nullptr) {
		if (bOutput)
			std::cerr << "Unable to initialize locale" << std::endl;
		return false;
	}
	std::string lo = orig_locale;
	auto dot = lo.find_first_of('.');
	if (dot != lo.npos) {
		if (strcmp(&lo[dot+1], "UTF-8") == 0 || strcmp(&lo[dot+1], "utf8") == 0) {
			if (lpstrLastSetLocale)
				*lpstrLastSetLocale = std::move(lo);
			return true; // no need to force anything
		}
		lo.erase(dot);
	}
	if (bOutput && (isatty(STDOUT_FILENO) || isatty(STDERR_FILENO))) {
		std::cerr << "Warning: Terminal locale not UTF-8, but UTF-8 locale is being forced." << std::endl;
		std::cerr << "         Screen output may not be correctly printed." << std::endl;
	}
	lo += ".UTF-8";
	if (lpstrLastSetLocale)
		*lpstrLastSetLocale = lo;
	orig_locale = setlocale(LC_CTYPE, lo.c_str());
	if (orig_locale == nullptr) {
		lo = "en_US.UTF-8";
		if (lpstrLastSetLocale)
			*lpstrLastSetLocale = lo;
		orig_locale = setlocale(LC_CTYPE, lo.c_str());
	}
	if (orig_locale == nullptr) {
		if (bOutput)
			std::cerr << "Unable to set locale '" << lo << "'" << std::endl;
		return false;
	}
	return true;
}

} /* namespace */
