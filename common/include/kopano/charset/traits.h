/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <string>
#include <cstring>
#if KC_USES_CXX17
#	include <string_view>
#endif

namespace KC {

template<typename Type> class iconv_charset KC_FINAL {
};

#define CHARSET_CHAR "//TRANSLIT"
/*
 * glibc iconv is missing support for wchar_t->wchar_t conversions
 * (https://sourceware.org/bugzilla/show_bug.cgi?id=20804 ), and
 * iconv_charset<> does not capture that special case either.
 * UTF-32BE/LE needs to be used, because conversion to unspecified UTF-32
 * leads to BOMs (and an upset testsuite).
 */
#ifdef KC_BIGENDIAN
#	define CHARSET_WCHAR "UTF-32BE"
#else
#	define CHARSET_WCHAR "UTF-32LE"
#endif
#define CHARSET_TCHAR (iconv_charset<TCHAR*>::name())

// Multibyte character specializations
template<> class iconv_charset<std::string> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_CHAR;	// Current locale
	}
	static const char *rawptr(const std::string &from) {
		return from.c_str();
	}
	static size_t rawsize(const std::string &from) {
		return from.size();
	}
};

#if KC_USES_CXX17
template<> class iconv_charset<std::string_view> KC_FINAL {
public:
	static const char *name() { return CHARSET_CHAR; }
	static const char *rawptr(const std::string_view &from) { return from.data(); }
	static size_t rawsize(const std::string_view &from) { return from.size(); }
};
#endif

template<> class iconv_charset<char *> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_CHAR;	// Current locale
	}
	static const char *rawptr(const char *from) {
		return from;
	}
	static size_t rawsize(const char *from) {
		return strlen(from);
	}
};

template<> class iconv_charset<const char *> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_CHAR;	// Current locale
	}
	static const char *rawptr(const char *from) {
		return from;
	}
	static size_t rawsize(const char *from) {
		return strlen(from);
	}
};

template<size_t N> class iconv_charset<char[N]> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_CHAR;	// Current locale
	}
	static const char *rawptr(const char (&from)[N])
	{
		return from;
	}
	static size_t rawsize(const char (&)[N])
	{
		return N - 1;
	}
};

template<size_t N> class iconv_charset<const char[N]> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_CHAR;	// Current locale
	}
	static const char *rawptr(const char (&from)[N])
	{
		return from;
	}
	static size_t rawsize(const char (&)[N])
	{
		return N - 1;
	}
};


// Wide character specializations
template<> class iconv_charset<std::wstring> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_WCHAR;
	}
	static const char *rawptr(const std::wstring &from) {
		return reinterpret_cast<const char*>(from.c_str());
	}
	static size_t rawsize(const std::wstring &from) {
		return from.size() * sizeof(std::wstring::value_type);
	}
};

template<> class iconv_charset<wchar_t *> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_WCHAR;
	}
	static const char *rawptr(const wchar_t *from) {
		return reinterpret_cast<const char*>(from);
	}
	static size_t rawsize(const wchar_t *from) {
		return wcslen(from) * sizeof(wchar_t);
	}
};

template<> class iconv_charset<const wchar_t *> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_WCHAR;
	}
	static const char *rawptr(const wchar_t *from) {
		return reinterpret_cast<const char*>(from);
	}
	static size_t rawsize(const wchar_t *from) {
		return wcslen(from) * sizeof(wchar_t);
	}
};

template<size_t N> class iconv_charset<wchar_t[N]> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_WCHAR;	// Current locale
	}
	static const char *rawptr(const wchar_t (&from)[N])
	{
		return reinterpret_cast<const char*>(from);
	}
	static size_t rawsize(const wchar_t (&)[N])
	{
		return (N - 1) * sizeof(wchar_t);
	}
};

template<size_t N> class iconv_charset<const wchar_t[N]> KC_FINAL {
public:
	static const char *name() {
		return CHARSET_WCHAR;	// Current locale
	}
	static const char *rawptr(const wchar_t (&from)[N])
	{
		return reinterpret_cast<const char*>(from);
	}
	static size_t rawsize(const wchar_t (&)[N])
	{
		return (N - 1) * sizeof(wchar_t);
	}
};

template<> class iconv_charset<std::u16string> KC_FINAL {
public:
	static const char *name() {
#ifdef KC_BIGENDIAN
		return "UTF-16BE";
#else
		return "UTF-16LE";
#endif
	}
	static const char *rawptr(const std::u16string &from)
	{
		return reinterpret_cast<const char *>(from.c_str());
	}
	static size_t rawsize(const std::u16string &from)
	{
		return from.size() * sizeof(std::u16string::value_type);
	}
};

template<typename Type> size_t rawsize(const Type &x)
{
	return iconv_charset<Type>::rawsize(x);
}

} /* namespace */
