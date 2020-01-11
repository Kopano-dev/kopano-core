/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_STRINGUTIL_H
#define EC_STRINGUTIL_H 1

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
#include <kopano/platform.h>
#include <kopano/zcdefs.h>
#include <openssl/md5.h>

struct SBinary;
struct option;

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
	std::transform(f.begin(), f.end(), f.begin(), ::toupper);
	return f;
}

static inline std::string strToLower(std::string f) {
	std::transform(f.begin(), f.end(), f.begin(), ::tolower);
	return f;
}

static inline std::wstring strToUpper(std::wstring f)
{
	std::transform(f.begin(), f.end(), f.begin(), ::towupper);
	return f;
}

static inline std::string stringify(unsigned int x)
{
	/* (w)stringify(-1) has a different result than to_(w)string(-1), so do not substitute! */
	return std::to_string(x);
}

static inline std::wstring wstringify(unsigned int x)
{
	return std::to_wstring(x);
}

extern KC_EXPORT std::string stringify_hex(unsigned int);
extern KC_EXPORT std::string stringify_signed(int);
extern KC_EXPORT std::string stringify_int64(int64_t, bool usehex = false);
extern KC_EXPORT std::string stringify_float(double);
extern KC_EXPORT std::string stringify_double(double, int prec = 18, bool locale = false);
extern KC_EXPORT std::wstring wstringify_hex(unsigned int);

#define tstringify			wstringify
#define tstringify_hex wstringify_hex

static inline unsigned int atoui(const char *s) { return strtoul(s, nullptr, 10); }
static inline unsigned int xtoi(const char *s) { return strtoul(s, nullptr, 16); }
extern KC_EXPORT int memsubstr(const void *haystack, size_t hsize, const void *needle, size_t nsize);
extern KC_EXPORT std::string str_storage(uint64_t bytes, bool unlimited = true);
extern KC_EXPORT std::string GetServerNameFromPath(const char *);
extern KC_EXPORT std::string GetServerPortFromPath(const char *);

static inline bool parseBool(const char *s)
{
	return s == nullptr || (strcmp(s, "0") != 0 &&
	       strcmp(s, "false") != 0 && strcmp(s, "no") != 0);
}

extern KC_EXPORT std::vector<std::wstring> tokenize(const std::wstring &, const wchar_t sep, bool filter_empty = false);
extern KC_EXPORT std::vector<std::string> tokenize(const std::string &, const char sep, bool filter_empty = false);
extern KC_EXPORT std::string trim(const std::string &input, const std::string &trim = " ");
extern KC_EXPORT unsigned char x2b(char);
extern KC_EXPORT std::string hex2bin(const std::string &);
extern KC_EXPORT std::string hex2bin(const std::wstring &);
extern KC_EXPORT std::string bin2hex(const std::string &);
extern KC_EXPORT std::string bin2hex(size_t len, const void *input);
extern KC_EXPORT std::string bin2hex(const SBinary &);
extern KC_EXPORT std::string bin2txt(const void *, size_t);
extern KC_EXPORT std::string bin2txt(const SBinary &);
inline std::string bin2txt(const std::string &s) { return bin2txt(s.data(), s.size()); }
extern KC_EXPORT std::string urlEncode(const std::string &);
extern KC_EXPORT std::string urlEncode(const std::wstring &, const char *charset);
extern KC_EXPORT std::string urlEncode(const wchar_t *input, const char *charset);
extern KC_EXPORT std::string urlDecode(const std::string &);
extern KC_EXPORT void BufferLFtoCRLF(size_t size, const char *input, char *output, size_t *outsize);
extern KC_EXPORT void StringCRLFtoLF(const std::wstring &in, std::wstring *out);
extern KC_EXPORT void StringLFtoCRLF(std::string &inout);
extern KC_EXPORT void StringTabtoSpaces(const std::wstring &in, std::wstring *out);

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
template<typename T, typename C = std::less<T>> std::set<T, C>
vector_to_set(std::vector<T> &&v)
{
	return std::set<T, C>(std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
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
Tp join(InputIterator first, InputIterator last, Tp sep)
{
	Tp s;
	for (; first != last; ++first) {
		if (!s.empty())
			s += sep;
		s += *first;
	}
	return s;
}

extern KC_EXPORT std::string format(const char *fmt, ...) KC_LIKE_PRINTF(1, 2);
extern KC_EXPORT char *kc_strlcpy(char *dst, const char *src, size_t n);
extern KC_EXPORT bool kc_starts_with(const std::string &, const std::string &);
extern KC_EXPORT bool kc_istarts_with(const std::string &, const std::string &);
extern KC_EXPORT bool kc_ends_with(const std::string &, const std::string &);

template<typename Iter> std::string kc_join(Iter cur, Iter end, const char *sep)
{
	/* This is faster than std::copy(,,ostream_iterator(stringstream)); on gcc libstdc++ */
	std::string s;
	using fr = std::remove_cv_t<std::remove_reference_t<decltype(*cur)>>;
	static_assert(std::is_same<fr, std::string>::value ||
		std::is_same<fr, const char *>::value ||
		std::is_same<fr, char *>::value, "container thing must be some string");
	if (cur != end)
		s += *cur++;
	while (cur != end) {
		s += sep;
		s += *cur++;
	}
	return s;
}

template<typename Container> std::string kc_join(const Container &v, const char *sep)
{
	return kc_join(cbegin(v), cend(v), sep);
}

template<typename C, typename F> std::string
kc_join(const C &v, const char *sep, F &&func)
{
	std::string result;
	auto it = v.cbegin();
	using fr = std::remove_reference_t<decltype(func(*it))>;
	static_assert(std::is_same<fr, std::string>::value ||
		std::is_same<fr, const char *>::value ||
		std::is_same<fr, char *>::value, "func must return some string");
	if (it == v.cend())
		return result;
	result += func(*it++);
	while (it != v.cend())
		result += sep + func(*it++);
	return result;
}

extern KC_EXPORT std::string base64_encode(const void *, unsigned int);
extern KC_EXPORT std::string base64_decode(const std::string &);
extern KC_EXPORT std::string zcp_md5_final_hex(MD5_CTX *);
extern KC_EXPORT std::string string_strip_crlf(const char *);
extern KC_EXPORT bool SymmetricIsCrypted(const char *);
extern KC_EXPORT std::string SymmetricDecrypt(const char *);
extern KC_EXPORT std::string content_type_get_charset(const char *in, const char *dflt);
/* Permit unknown long options, move them to end of argv like arguments */
extern KC_EXPORT int my_getopt_long_permissive(int, char **, const char *, const struct option *, int *);
extern KC_EXPORT std::string number_to_humansize(uint64_t);
extern KC_EXPORT uint64_t humansize_to_number(const char *);
extern KC_EXPORT std::string kc_wstr_to_punyaddr(const wchar_t *);
extern KC_EXPORT std::string kc_utf8_to_punyaddr(const char *);

} /* namespace */

#endif
