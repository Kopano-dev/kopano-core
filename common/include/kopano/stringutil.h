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

#ifndef _STRINGUTIL_H
#define _STRINGUTIL_H

#include <kopano/zcdefs.h>
#include <cstdarg>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <kopano/platform.h>
#include <openssl/md5.h>

struct SBinary;

namespace KC {

/*
 * Comparison handler for case-insensitive keys in maps
 */
struct strcasecmp_comparison {
	bool operator()(const std::string &left, const std::string &right) const
	{
		return left.size() < right.size() || (left.size() == right.size() && strcasecmp(left.c_str(), right.c_str()) < 0);
	}
};

struct wcscasecmp_comparison {
	bool operator()(const std::wstring &left, const std::wstring &right) const
	{
		return left.size() < right.size() || (left.size() == right.size() && wcscasecmp(left.c_str(), right.c_str()) < 0);
	}
};

static inline std::string strToUpper(std::string f) {
	transform(f.begin(), f.end(), f.begin(), ::toupper);
	return f;
}

static inline std::string strToLower(std::string f) {
	transform(f.begin(), f.end(), f.begin(), ::tolower);
	return f;
}

static inline std::wstring strToUpper(std::wstring f)
{
	std::transform(f.begin(), f.end(), f.begin(), ::towupper);
	return f;
}

static inline std::string stringify(unsigned int x)
{
	/* (w)stringify(-1) has a different result than to_(w)string(-1), so do not subtitute! */
	return std::to_string(x);
}

static inline std::wstring wstringify(unsigned int x)
{
	return std::to_wstring(x);
}

extern _kc_export std::string stringify(unsigned int x, bool usehex, bool _signed = false);
extern _kc_export std::string stringify_int64(int64_t, bool usehex = false);
extern _kc_export std::string stringify_float(float);
extern _kc_export std::string stringify_double(double, int prec = 18, bool locale = false);

extern _kc_export std::wstring wstringify(unsigned int x, bool usehex, bool _signed = false);

#define tstringify			wstringify

static inline unsigned int atoui(const char *s) { return strtoul(s, nullptr, 10); }
static inline unsigned int xtoi(const char *s) { return strtoul(s, nullptr, 16); }
extern _kc_export int memsubstr(const void *haystack, size_t hsize, const void *needle, size_t nsize);
extern _kc_export std::string str_storage(uint64_t bytes, bool unlimited = true);
extern _kc_export std::string GetServerNameFromPath(const char *);
extern _kc_export std::string GetServerPortFromPath(const char *);

static inline bool parseBool(const char *s)
{
	return s == nullptr || (strcmp(s, "0") != 0 &&
	       strcmp(s, "false") != 0 && strcmp(s, "no") != 0);
}

extern _kc_export std::vector<std::wstring> tokenize(const std::wstring &, const wchar_t sep, bool filter_empty = false);
extern _kc_export std::vector<std::string> tokenize(const std::string &, const char sep, bool filter_empty = false);
extern _kc_export std::string trim(const std::string &input, const std::string &trim = " ");
extern _kc_export unsigned char x2b(char);
extern _kc_export std::string hex2bin(const std::string &);
extern _kc_export std::string hex2bin(const std::wstring &);
extern _kc_export std::string bin2hex(const std::string &);
extern _kc_export std::string bin2hex(size_t len, const void *input);
extern _kc_export std::string bin2hex(const SBinary &);
extern _kc_export std::string urlEncode(const std::string &);
extern _kc_export std::string urlEncode(const std::wstring &, const char *charset);
extern _kc_export std::string urlEncode(const wchar_t *input, const char *charset);

extern _kc_export std::string urlDecode(const std::string &);
extern _kc_export void BufferLFtoCRLF(size_t size, const char *input, char *output, size_t *outsize);
extern _kc_export void StringCRLFtoLF(const std::wstring &in, std::wstring *out);
extern _kc_export void StringLFtoCRLF(std::string &inout);
extern _kc_export void StringTabtoSpaces(const std::wstring &in, std::wstring *out);

template<typename T>
std::vector<T> tokenize(const T &str, const T &delimiters)
{
	std::vector<T> tokens;

	// skip delimiters at beginning.
   	typename T::size_type lastPos = str.find_first_not_of(delimiters, 0);

	// find first "non-delimiter".
   	typename T::size_type pos = str.find_first_of(delimiters, lastPos);

   	while (std::string::npos != pos || std::string::npos != lastPos)
   	{
       	// found a token, add it to the std::vector.
		tokens.emplace_back(str.substr(lastPos, pos - lastPos));

       	// skip delimiters.  Note the "not_of"
       	lastPos = str.find_first_not_of(delimiters, pos);

       	// find next "non-delimiter"
       	pos = str.find_first_of(delimiters, lastPos);
   	}

	return tokens;
}

/**
 * Notes on use: Iff the program part to be edited can cope with duplicates in
 * a vector already, do not bother with the conversion to set if @v has few
 * elements.
 */
template<typename T> std::set<T> vector_to_set(std::vector<T> &&v)
{
	return std::set<T>(std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
}

template<typename T>
std::vector<T> tokenize(const T &str, const typename T::value_type *delimiters)
{
	return tokenize(str, (T)delimiters);
}

template<typename T>
std::vector<std::basic_string<T> > tokenize(const T* str, const T* delimiters)
{
	return tokenize(std::basic_string<T>(str), std::basic_string<T>(delimiters));
}

template<typename InputIterator, typename Tp>
Tp join(InputIterator __first, InputIterator __last, Tp __sep)
{
	Tp s;
    for (; __first != __last; ++__first) {
        if(!s.empty())
            s += __sep;
        s += *__first;
    }
    return s;
}

extern _kc_export std::string format(const char *fmt, ...) KC_LIKE_PRINTF(1, 2);
extern _kc_export char *kc_strlcpy(char *dst, const char *src, size_t n);
extern _kc_export bool kc_starts_with(const std::string &, const std::string &);
extern _kc_export bool kc_istarts_with(const std::string &, const std::string &);
extern _kc_export bool kc_ends_with(const std::string &, const std::string &);

template<typename T> std::string kc_join(const T &v, const char *sep)
{
	/* This is faster than std::copy(,,ostream_iterator(stringstream)); on glibc */
	std::string s;
	size_t z = 0;
	for (const auto i : v)
		z += i.size() + 1;
	s.reserve(z);
	for (const auto i : v) {
		s += i;
		s += sep;
	}
	z = strlen(sep);
	if (s.size() > z)
		s.erase(s.size() - z, z);
	return s;
}

extern _kc_export std::string base64_encode(const void *, unsigned int);
extern _kc_export std::string base64_decode(const std::string &);
extern _kc_export std::string zcp_md5_final_hex(MD5_CTX *);
extern _kc_export std::wstring string_strip_nuls(const std::wstring &);
extern _kc_export std::string string_strip_crlf(const char *);

} /* namespace */

#endif
