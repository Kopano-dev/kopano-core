/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <cassert>
#include <cctype>
#include <cstring>
#include <getopt.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <kopano/ECGetText.h>
#include <kopano/ECLogger.h>
#include <kopano/codepage.h>
#include <openssl/md5.h>
#include <mapidefs.h>
#include "ECACL.h"

namespace KC {

struct acl_right_name {
	/* The data in this array must be sorted on the n_right field. */
	unsigned int n_right;
	const char *tx_right;

	bool operator<(const acl_right_name &r) const { return n_right < r.n_right; }
};

struct acl_role_name {
	/* The data in this array must be sorted on the n_rights field. */
	unsigned int n_rights;
	const char *tx_role;

	bool operator<(const acl_role_name &r) const { return n_rights < r.n_rights; }
};

/**
 * This class performs the actual conversion and caching of the translated
 * messages. Results are cached based on the pointer value, not the string
 * content. This implies two assumptions:
 * 1. Gettext always returns the same pointer for a particular translation.
 * 2. If there is no translation, the original pointer is returned. So we
 *    assume that the compiler optimized string literals to have the same
 *    address if they are equal. If this assumption is false, this will lead to
 *    more conversions, and more memory usage by the cache.
 */
class gtconv final {
	public:
	static std::unique_ptr<gtconv> &get_instance()
	{
		scoped_lock locker(m_lock);
		if (m_instance == nullptr)
			m_instance.reset(new gtconv);
		return m_instance;
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
	static std::unique_ptr<gtconv> m_instance;
	static std::mutex m_lock;

	convert_context	m_converter;
	std::map<const char *, std::wstring> m_cache;
	std::mutex m_hCacheLock;
};

std::mutex gtconv::m_lock;
std::unique_ptr<gtconv> gtconv::m_instance;

/**
 * Performs a "regular" gettext and converts the result to a wide character string.
 * @domain:	The domain to use for the translation
 * @msg:	The msgid of the message to be translated.
 * Retruns the converted, translated string.
 */
const wchar_t *kopano_dcgettext_wide(const char *domain, const char *msg)
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
	return gtconv::get_instance()->convert(dcgettext(domain, msg, LC_MESSAGES));
}

std::string stringify_hex(unsigned int x)
{
	char b[33];
	snprintf(b, sizeof(b), "0x%08X", x);
	return b;
}

std::string stringify_signed(int x)
{
	char b[33];
	snprintf(b, sizeof(b), "%d", x);
	return b;
}

std::string stringify_int64(int64_t x, bool usehex) {
	std::ostringstream s;

	if (usehex) {
		s.flags(std::ios::showbase);
		s.setf(std::ios::hex, std::ios::basefield);	// showbase && basefield: add 0x prefix
		s.setf(std::ios::uppercase);
	}
	s << x;
	return s.str();
}

std::string stringify_float(double x)
{
	std::ostringstream s;
	s << x;
	return s.str();
}

std::string stringify_double(double x, int prec, bool bLocale) {
	std::ostringstream s;

	s.precision(prec);
	s.setf(std::ios::fixed,std::ios::floatfield);
	if (bLocale) {
		try {
			std::locale l("");
			s.imbue(l);
		} catch (const std::runtime_error &) {
			// locale not available, print in C
		}
		s << x;
	} else
		s << x;

	return s.str();
}

std::wstring wstringify_hex(unsigned int x)
{
	wchar_t b[33];
	swprintf(b, ARRAY_SIZE(b), L"0x%08X", x);
	return b;
}

int memsubstr(const void* haystack, size_t haystackSize, const void* needle, size_t needleSize)
{
	size_t pos = 0;
	size_t match = 0;
	auto searchbuf = static_cast<const BYTE *>(needle);
	auto databuf = static_cast<const BYTE *>(haystack);

	if(haystackSize < needleSize)
		return (haystackSize-needleSize);

	while(pos < haystackSize)
	{
		if(*databuf == *searchbuf){
			++searchbuf;
			++match;
			if(match == needleSize)
				return 0;
		}else{
			databuf -= match;
			pos -= match;
			searchbuf = (BYTE*)needle;
			match = 0;
		}
		++databuf;
		++pos;
	}
	return 1;
}

std::string str_storage(uint64_t ulBytes, bool bUnlimited) {
	static double MB = 1024.0 * 1024.0;

	if (ulBytes == 0 && bUnlimited)
		return "unlimited";
	return stringify_double((double)ulBytes / MB, 2) + " MB";
}

std::string GetServerNameFromPath(const char *szPath) {
	std::string path = szPath;
	size_t pos = 0;

	pos = path.find("://");
	if (pos != std::string::npos) {
		/* Remove prefixed type information */
		path.erase(0, pos + 3);
	}
	pos = path.find(':');
	if (pos != std::string::npos)
		path.erase(pos, std::string::npos);
	return path;
}

std::string GetServerPortFromPath(const char *szPath) {
	std::string path = szPath;
	size_t pos = 0;

	if (strncmp(path.c_str(), "http", 4) != 0)
		return std::string();
	pos = path.rfind(':');
	if (pos == std::string::npos)
		return std::string();
	pos += 1; /* Skip ':' */
	/* Remove all leading characters */
	path.erase(0, pos);
	/* Strip additional path */
	pos = path.rfind('/');
	if (pos != std::string::npos)
		path.erase(pos, std::string::npos);
	return path;
}

std::vector<std::wstring> tokenize(const std::wstring &strInput, const WCHAR sep, bool bFilterEmpty) {
	const WCHAR *begin, *end = NULL;
	std::vector<std::wstring> vct;

	begin = strInput.c_str();
	while (*begin != '\0') {
		end = wcschr(begin, sep);
		if (!end) {
			vct.emplace_back(begin);
			break;
		}
		if (!bFilterEmpty || std::distance(begin,end) > 0)
			vct.emplace_back(begin, end);
		begin = end+1;
	}
	return vct;
}

std::vector<std::string> tokenize(const std::string &strInput, const char sep, bool bFilterEmpty) {
	const char *begin, *last, *end = NULL;
	std::vector<std::string> vct;

	begin = strInput.c_str();
	last = begin + strInput.length();
	while (begin < last) {
		end = strchr(begin, sep);
		if (!end) {
			vct.emplace_back(begin);
			break;
		}
		if (!bFilterEmpty || std::distance(begin,end) > 0)
			vct.emplace_back(begin, end);
		begin = end+1;
	}
	return vct;
}

std::string trim(const std::string &strInput, const std::string &strTrim)
{
	std::string s = strInput;
	size_t pos;

	if (s.empty())
		return s;
	pos = s.find_first_not_of(strTrim);
	s.erase(0, pos);
	pos = s.find_last_not_of(strTrim);
	if (pos != std::string::npos)
		s.erase(pos + 1, std::string::npos);
	return s;
}

unsigned char x2b(char c)
{
	if (c >= '0' && c <= '9')
	// expects sensible input
		return c - '0';
	else if (c >= 'a')
		return c - 'a' + 10;
	return c - 'A' + 10;
}

std::string hex2bin(const std::string &input)
{
	std::string buffer;

	if (input.length() % 2 != 0)
		return buffer;
	buffer.reserve(input.length() / 2);
	for (unsigned int i = 0; i < input.length(); ) {
		unsigned char c;
		c = x2b(input[i++]) << 4;
		c |= x2b(input[i++]);
		buffer += c;
	}
	return buffer;
}

std::string hex2bin(const std::wstring &input)
{
	std::string buffer;

	if (input.length() % 2 != 0)
		return buffer;
	buffer.reserve(input.length() / 2);
	for (unsigned int i = 0; i < input.length(); ) {
		unsigned char c;
		c = x2b((char)input[i++]) << 4;
		c |= x2b((char)input[i++]);
		buffer += c;
	}
	return buffer;
}

std::string bin2hex(size_t inLength, const void *vinput)
{
	if (vinput == nullptr)
		return "";
	static constexpr const char digits[] = "0123456789ABCDEF";
	auto input = static_cast<const char *>(vinput);
	std::string buffer;
	buffer.resize(inLength * 2);
	for (size_t j = 0; inLength-- > 0; j += 2) {
		buffer[j]   = digits[(*input >> 4) & 0x0F];
		buffer[j+1] = digits[*input & 0x0F];
		++input;
	}
	return buffer;
}

std::string bin2hex(const std::string &input)
{
	return bin2hex(input.size(), input.c_str());
}

std::string bin2hex(const SBinary &b)
{
	return bin2hex(b.cb, b.lpb);
}

std::string bin2txt(const void *vdata, size_t len)
{
	auto data = static_cast<const unsigned char *>(vdata);
	std::string ret;
	char b[5];
	for (size_t i = 0; i < len; ++i) {
		if (isprint(data[i]) && data[i] != '"' && data[i] != '\\') {
			b[0] = data[i];
			b[1] = '\0';
		} else if (data[i] < 8) {
			b[0] = '\\';
			b[1] = '0' + (data[i] % 8);
			b[2] = '\0';
		} else if (data[i] < 32) {
			b[0] = '\\';
			b[1] = '0' + (data[i] / 8 % 8);
			b[2] = '0' + (data[i] % 8);
			b[3] = '\0';
		} else {
			b[0] = '\\';
			b[1] = '0' + (data[i] / 64 % 8);
			b[2] = '0' + (data[i] / 8 % 8);
			b[3] = '0' + (data[i] % 8);
			b[4] = '\0';
		}
		ret.append(b);
	}
	return ret;
}

std::string bin2txt(const SBinary &b)
{
	return bin2txt(b.lpb, b.cb);
}

/**
 * Encodes a string for inclusion into an url.
 *
 * @note this does not encode an url to another more valid url (since / would get encoded!)
 * @note watch the locale of the string, make sure it's the same as the rest of the url.
 *
 * @param[in] input string to encode
 *
 * @return encoded string valid to include in an url
 */
std::string urlEncode(const std::string &input)
{
	std::string output;
	static const char digits[] = "0123456789ABCDEF";
	/*
	 * Need at least this much, but otherwise have no strategy better
	 * than the default for reallocs.
	 */
	output.reserve(input.length());
	for (size_t i = 0; i < input.length(); ++i) {
		if (static_cast<unsigned char>(input[i]) <= 33 ||
		    static_cast<unsigned char>(input[i]) >= 128) {
			output += '%';
			output += digits[input[i] >> 4];
			output += digits[input[i] & 0x0F];
			continue;
		}
		switch (input[i]) {
		case ':':
		case '/':
		case '?':
		case '#':
		case '[':
		case ']':
		case '@':
		case '!':
		case '$':
		case '&':
		case '\'':
		case '(':
		case ')':
		case '*':
		case '+':
		case ',':
		case ';':
		case '=':
			output += '%';
			output += digits[input[i] >> 4];
			output += digits[input[i] & 0x0F];
			break;
		default:
			output += input[i];
		}
	}

	return output;
}

/**
 * encode an url part, input in wide char, and destination charset in encoded characters
 *
 * @param[in] input wide string to convert to valid url encoded ascii string
 * @param[in] charset non-ascii characters will be encoded for this charset
 *
 * @return url valid encoded string
 */
std::string urlEncode(const std::wstring &input, const char* charset)
{
	return urlEncode(convert_to<std::string>(charset, input, rawsize(input), CHARSET_WCHAR));
}

std::string urlEncode(const WCHAR* input, const char* charset)
{
	return urlEncode(convert_to<std::string>(charset, input, rawsize(input), CHARSET_WCHAR));
}

/**
 * replaces %## values by ascii values
 * i.e Amsterdam%2C -> Amsterdam,
 * @note 1. this can take a full url, since it just replaces the %##
 * @note 2. you need to handle the locale of the string yourself!
 *
 * @param[in] input url encoded string
 *
 * @return decoded url in the locale it was encoded in
 */
std::string urlDecode(const std::string &input)
{
	std::string output;

	output.reserve(input.length());
	for (size_t i = 0; i < input.length(); ++i) {
		if (input[i] == '%' && input.length() > i + 2)
		{
			unsigned char c;
			c = x2b(input[++i]) << 4;
			c |= x2b(input[++i]);
			output += c;
		}
		else
			output += input[i];
	}
	return output;
}

/**
 * Convert a memory buffer with strings with Unix \n enters to DOS
 * \r\n enters.
 *
 * @param[in] size length of the input
 * @param[in] input buffer containing strings with enters to convert
 * @param[out] output buffer with enough space to hold input + extra \r characters
 * @param[out] outsize number of characters written to output
 */
void BufferLFtoCRLF(size_t size, const char *input, char *output, size_t *outsize) {
	size_t j = 0;
	for (size_t i = 0; i < size; ++i) {
		if (input[i] == '\r') {
			if ((i+1) < size && input[i+1] == '\n') {
				output[j++] = '\r';
				output[j++] = '\n';
				++i;
			} else {
				output[j++] = '\r';
				output[j++] = '\n';
			}
		} else if (input[i] == '\n') {
			output[j++] = '\r';
			output[j++] = '\n';
		} else {
			output[j++] = input[i];
		}
	}
	output[j] = '\0';
	*outsize = j;
}

/**
 * converts Tabs in a string to spaces
 *
 * @param[in] 	strInput		input string to be converted
 * @param[out] 	strOutput		return converted string
 */
void StringTabtoSpaces(const std::wstring &strInput, std::wstring *lpstrOutput) {
	std::wstring strOutput;
	/*
	 * With this reservation, at worst, when every input char is a tab,
	 * at most two reallocs happen (with capacity doubling).
	 */
	strOutput.reserve(strInput.length());
	for (auto c : strInput)
		if (c == '\t')
			strOutput.append(4, ' ');
		else
			strOutput.append(1, c);
	*lpstrOutput = std::move(strOutput);
}

/**
 * converts CRLF in a string to LF
 *
 * @param[in] strInput		input string to be converted
 * @param[out] strOutput	return converted string
 */
void StringCRLFtoLF(const std::wstring &strInput, std::wstring *lpstrOutput) {
	std::wstring::const_iterator iInput(strInput.begin());
	std::wstring strOutput;

	strOutput.reserve(strInput.length());
	for (; iInput != strInput.end(); ++iInput) {
		// skips /r if /r/n found together in the text
		if (*iInput == '\r' && (iInput + 1 != strInput.end() && *(iInput + 1) == '\n'))
			continue;
		strOutput.append(1, *iInput);
	}
	*lpstrOutput = std::move(strOutput);
}

/**
 * converts a string inline from \n enters to \r\n
 *
 * @param strInOut string to edit
 */
void StringLFtoCRLF(std::string &strInOut)
{
	std::string strOutput;
	std::string::const_iterator i;
	/* Output at most double the size of input => one realloc normally */
	strOutput.reserve(strInOut.size());
	for (i = strInOut.begin(); i != strInOut.end(); ++i)
		if (*i == '\n' && i != strInOut.begin() && *(i-1) != '\r')
			strOutput.append("\r\n");
		else
			strOutput.append(1, *i);
	strInOut = std::move(strOutput);
}

std::string format(const char *const fmt, ...) {
        char *buffer = NULL;
        va_list ap;

        va_start(ap, fmt);
        (void)vasprintf(&buffer, fmt, ap);
        va_end(ap);

        std::string result = buffer;
        free(buffer);
        return result;
}

char *kc_strlcpy(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n);
	dest[n-1] = '\0';
	return dest;
}

bool kc_starts_with(const std::string &full, const std::string &prefix)
{
	return full.compare(0, prefix.size(), prefix) == 0;
}

bool kc_istarts_with(const std::string &full, const std::string &needle)
{
	return kc_starts_with(strToLower(full), strToLower(needle));
}

bool kc_ends_with(const std::string &full, const std::string &prefix)
{
	size_t fz = full.size(), pz = prefix.size();
	if (fz < pz)
		 return false;
	return full.compare(fz - pz, pz, prefix) == 0;
}

static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline bool is_base64(unsigned char c)
{
	return isalnum(c) || c == '+' || c == '/';
}

std::string base64_encode(const void *bte, unsigned int in_len)
{
	auto bytes_to_encode = static_cast<const unsigned char *>(bte);
	unsigned char char_array_3[3], char_array_4[4];
	int i = 0, j = 0;
	std::string ret;
	ret.reserve((in_len + 2) / 3 * 4);

	while (in_len--) {
		char_array_3[i++] = *(bytes_to_encode++);
		if (i != 3)
			continue;
		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;
		for (i = 0; i < 4; ++i)
			ret += base64_chars[char_array_4[i]];
		i = 0;
	}

	if (i == 0)
		return ret;
	for (j = i; j < 3; ++j)
		char_array_3[j] = '\0';
	char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
	char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
	char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
	char_array_4[3] = char_array_3[2] & 0x3f;
	for (j = 0; j < i + 1; ++j)
		ret += base64_chars[char_array_4[j]];
	while ((i++ < 3))
		ret += '=';
	return ret;
}

std::string base64_decode(const std::string &encoded_string)
{
	int in_len = encoded_string.size(), i = 0, j = 0, in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	std::string ret;
	ret.reserve((in_len + 1) / 4 * 3);

	while (in_len-- && encoded_string[in_] != '=' && is_base64(encoded_string[in_])) {
		char_array_4[i++] = encoded_string[in_++];
		if (i != 4)
			continue;
		for (i = 0; i < 4; ++i)
			char_array_4[i] = base64_chars.find(char_array_4[i]);
		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
		for (i = 0; i < 3; ++i)
			ret += char_array_3[i];
		i = 0;
	}

	if (i == 0)
		return ret;
	for (j = i; j < 4; ++j)
		char_array_4[j] = 0;
	for (j = 0; j < 4; ++j)
		char_array_4[j] = base64_chars.find(char_array_4[j]);
	char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
	char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
	char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
	for (j = 0; j < i - 1; ++j)
		ret += char_array_3[j];
	return ret;
}

std::string zcp_md5_final_hex(MD5_CTX *ctx)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char md[MD5_DIGEST_LENGTH];
	std::string s;
	s.reserve(2 * sizeof(md));

	MD5_Final(md, ctx);
	for (size_t z = 0; z < sizeof(md); ++z) {
		s.push_back(hex[(md[z] & 0xF0) >> 4]);
		s.push_back(hex[md[z] & 0xF]);
	}
	return s;
}

std::string string_strip_crlf(const char *s)
{
	std::string o;
	/*
	 * Expectation: 2 bytes (CRLF) every 78 bytes; and an input usually not
	 * longer than 480 bytes. Allocating the same length is therefore
	 * acceptable.
	 */
	o.reserve(strlen(s));
	std::copy_if(s, s + strlen(s), std::back_inserter(o),
		[](char c) { return c != '\n' && c != '\r'; });
	return o;
}

/**
 * Check if the provided password is crypted.
 *
 * Crypted passwords have the format "{N}:<crypted password>, with N being the encryption algorithm.
 * Currently only algorithm number 1 and 2 are supported:
 * 1: base64-of-XOR-A5 of windows-1252 encoded data
 * 2: base64-of-XOR-A5 of UTF-8 encoded data
 *
 * @param[in]	strCrypted	The string to test.
 * @return	boolean
 * @retval	true	The provided string was encrypted.
 * @retval	false 	The provided string was not encrypted.
 */
bool SymmetricIsCrypted(const char *c)
{
	return strncmp(c, "{1}:", 4) == 0 || strncmp(c, "{2}:", 4) == 0;
}

/**
 * Decrypt the crypt data.
 *
 * Depending on the N value, the password is decrypted using algorithm 1 or 2.
 *
 * @param[in]	ulAlg	The number selecting the algorithm. (1 or 2)
 * @param[in]	strXORed	The binary data to decrypt.
 * @return	The decrypted password encoded in UTF-8.
 */
static std::string SymmetricDecryptBlob(unsigned int ulAlg, const std::string &strXORed)
{
	std::string strRaw = strXORed;
	size_t z = strRaw.size();

	assert(ulAlg == 1 || ulAlg == 2);
	for (unsigned int i = 0; i < z; ++i)
		strRaw[i] ^= 0xA5;
	/*
	 * Check the encoding algorithm. If it equals 1, the raw data is windows-1252.
	 * Otherwise, it must be 2, which means it is already UTF-8.
	 */
	if (ulAlg == 1)
		strRaw = convert_to<std::string>("UTF-8", strRaw, rawsize(strRaw), "WINDOWS-1252");
	return strRaw;
}

/**
 * Decrypt an encrypted password.
 *
 * Depending on the N value, the password is decrypted using algorithm 1 or 2.
 *
 * @param[in]	strCrypted	The UTF-8 encoded encrypted password to decrypt.
 * @return	THe decrypted password encoded in UTF-8.
 */
std::string SymmetricDecrypt(const char *strCrypted)
{
	if (!SymmetricIsCrypted(strCrypted))
		return "";
	/* Length has been guaranteed to be >= 4. */
	return SymmetricDecryptBlob(strCrypted[1] - '0',
		base64_decode(convert_to<std::string>(strCrypted + 4)));
}

/**
 * Determine character set from a possibly broken Content-Type value.
 * @in:		string in the form of m{^text/foo\s*(;?\s*key=value)*}
 * @cset:	the default return value if no charset= is to be found
 *
 * Attempt to extract the character set parameter, e.g. from a HTML <meta> tag,
 * or from a Content-Type MIME header (though we do not use it for MIME headers
 * currently).
 */
std::string content_type_get_charset(const char *in, const char *cset)
{
	const char *cset_end = cset + strlen(cset);

	while (!isspace(*in) && *in != '\0')	/* skip type */
		++in;
	while (*in != '\0') {
		while (isspace(*in))
			++in; /* skip possible whitespace before ';' */
		if (*in == ';') {
			++in;
			while (isspace(*in))	/* skip WS after ';' */
				++in;
		}
		if (strncasecmp(in, "charset=", 8) == 0) {
			in += 8;
			if (*in == '"') {
				cset = ++in;
				while (*in != '\0' && *in != '"')
					++in;
				cset_end = in;
			} else {
				cset = in;
				while (!isspace(*in) && *in != ';' && *in != '\0')
					++in;	/* skip value */
				cset_end = in;
			}
			continue;
			/* continue parsing for more charset= values */
		}
		while (!isspace(*in) && *in != ';' && *in != '\0')
			++in;
	}
	return std::string(cset, cset_end - cset);
}

int my_getopt_long_permissive(int argc, char **argv, const char *shortopts,
    const struct option *longopts, int *longind)
{
	int opterr_save = opterr, saved_optind = optind;
	opterr = 0;

	int c = getopt_long(argc, argv, shortopts, longopts, longind);
	if (c == '?') {
		// Move this parameter to the end of the list if it a long option
		if (argv[optind - 1][0] == '-' && argv[optind - 1][1] == '-' && argv[optind - 1][2] != '\0') {
			int i = optind - 1;
			/*
			 * Continue parsing at the next argument before moving the unknown
			 * option to the end, otherwise a potentially endless loop could
			 * ensue.
			 */
			c = getopt_long(argc, argv, shortopts, longopts, longind);
			char *tmp = argv[i];
			int move_count = (argc - i) - i;
			if (move_count > 0)
				memmove(&argv[i], &argv[i + 1], move_count * sizeof(char *));
			argv[i] = tmp;
			--optind;
			--saved_optind;
		}
	}

	opterr = opterr_save;
	// Show error
	if (c == '?') {
		optind = saved_optind;
		if (getopt_long(argc, argv, shortopts, longopts, longind) != 0)
			/* ignore return value */;
	}
	return c;
}

static const acl_right_name acl_rights[] = {
	{RIGHTS_READ_ITEMS, "item read"},
	{RIGHTS_CREATE_ITEMS, "item create"},
	{RIGHTS_EDIT_OWN, "edit own"},
	{RIGHTS_DELETE_OWN, "delete own"},
	{RIGHTS_EDIT_ALL, "edit all"},
	{RIGHTS_DELETE_ALL, "delete all"},
	{RIGHTS_CREATE_SUBFOLDERS, "create sub"},
	{RIGHTS_FOLDER_OWNER, "own"},
	{RIGHTS_FOLDER_CONTACT, "contact"},
	{RIGHTS_FOLDER_VISIBLE, "view"}
};

static const acl_role_name acl_roles[] = {
	{RIGHTS_NONE, "none"}, /* Actually a right, but not seen as such by is_right */
	{ROLE_NONE, "none"}, /* This might be confusing */
	{ROLE_REVIEWER, "reviewer"},
	{ROLE_CONTRIBUTOR, "contributor"},
	{ROLE_NONEDITING_AUTHOR, "non-editting author"},
	{ROLE_AUTHOR, "author"},
	{ROLE_EDITOR, "editor"},
	{ROLE_PUBLISH_EDITOR, "publish editor"},
	{ROLE_PUBLISH_AUTHOR, "publish author"},
	{ROLE_OWNER, "owner"}
};

static inline bool is_right(unsigned int ror)
{
	/* A right has exactly 1 bit set. Otherwise, it is a role. */
	return (ror ^ (ror - 1)) == 0;
}

static const struct acl_right_name *find_acl_right(unsigned int rightnum)
{
	const struct acl_right_name k = {rightnum, nullptr};
	auto e = std::lower_bound(acl_rights, ARRAY_END(acl_rights), k);
	if (e != ARRAY_END(acl_rights) && e->n_right == rightnum)
		return e;
	return nullptr;
}

static const struct acl_role_name *find_acl_role(unsigned int rolenum)
{
	const struct acl_role_name k = {rolenum, nullptr};
	auto e = std::lower_bound(acl_roles, ARRAY_END(acl_roles), k);
	if (e != ARRAY_END(acl_roles) && e->n_rights == rolenum)
		return e;
	return nullptr;
}

std::string AclRightsToString(unsigned int ror)
{
	if (ror == static_cast<unsigned int>(-1))
		return "missing or invalid";
	if (is_right(ror)) {
		auto r = find_acl_right(ror);
		if (r == nullptr)
			return stringify_hex(ror);
		return r->tx_right;
	}

	auto role = find_acl_role(ror);
	if (role != nullptr)
		return role->tx_role;

	std::ostringstream ostr;
	bool empty = true;
	for (unsigned bit = 0, mask = 1; bit < 32; ++bit, mask <<= 1) {
		if (ror & mask) {
			if (!empty)
				ostr << ",";
			empty = false;
			ostr << AclRightsToString(mask);
		}
	}
	return ostr.str();
}

/*
 * These charset should all be supported by iconv.
 * See http://msdn.microsoft.com/en-us/library/dd317756(VS.85).aspx
 */
static const struct CPMAP {
	const char *charset;
	unsigned int codepage;
} CPMAP[] = {
	{"DIN_66003", 20106},
	{"NS_4551-1", 20108},
	{"SEN_850200_B", 20107},
	{"big5", 950},
	{"csISO2022JP", 50221},
	{"euc-jp", 51932},
	{"euc-cn", 51936},
	{"euc-kr", 51949},
	{"euc-kr", 949}, /* euc-kr is compatible with cp949 according to some sources (some horde tickets say this) */
	{"cp949", 949},
	{"ks_c_5601-1987", 949}, /* ks_c_5601-1987 == cp949, but this charset is not recognized by iconv */
	{"gb18030", 936}, /* was gb2312, but cp936 is gb3212 + more, which is superseded by gb18030 (is codepage 54936?) */
	{"gb2312", 936}, /* entry for reverse lookup */
	{"GBK", 936}, /* entry for reverse lookup */
	{"csgb2312", 52936}, // not sure, hz-cn-2312 according to MS, iconv has this one
	{"ibm852", 852},
	{"ibm866", 866},
	{"iso-2022-jp", 50220},
	{"iso-2022-jp", 50222},
	{"iso-2022-kr", 50225},
	{"windows-1252", 1252},
	{"iso-8859-1", 28591},
	{"iso-8859-2", 28592},
	{"iso-8859-3", 28593},
	{"iso-8859-4", 28594},
	{"iso-8859-5", 28595},
	{"iso-8859-6", 28596},
	{"iso-8859-7", 28597},
	{"iso-8859-8", 28598},
	{"iso-8859-8-i", 28598},
	{"iso-8859-9", 28599},
	{"iso-8859-13", 28603},
	{"iso-8859-15", 28605},
	{"koi8-r", 20866},
	{"koi8-u", 21866},
	{"shift-jis", 932},
	{"shift_jis", 932},
	{"unicode", 1200}, /* UTF-16LE and BMP-only */
	{"unicodebig", 1201}, /* UTF-16BE and BMP-only */
	{"utf-7", 65000},
	{"utf-8", 65001},
	{"windows-1250", 1250},
	{"windows-1251", 1251},
	{"windows-1253", 1253},
	{"windows-1254", 1254},
	{"windows-1255", 1255},
	{"windows-1256", 1256},
	{"windows-1257", 1257},
	{"windows-1258", 1258},
	{"windows-874", 874},
	{"us-ascii", 20127},
};

/**
 * Converts a Windows codepage to a valid iconv charset string.
 *
 * @codepage:	Windows codepage number (e.g. from PR_INTERNET_CPID)
 * @cs:		Pointer to internal structure containing iconv charset string
 *
 * Returns %MAPI_E_NOT_FOUND on unknown codepage and cs will be unchanged.
 */
HRESULT HrGetCharsetByCP(unsigned int id, const char **cs)
{
	for (size_t i = 0; i < ARRAY_SIZE(CPMAP); ++i)
		if (CPMAP[i].codepage == id) {
			*cs = CPMAP[i].charset;
			return hrSuccess;
		}
	return MAPI_E_NOT_FOUND;
}

/**
 * Converts a Windows codepage to a valid iconv charset string.
 *
 * @codepage:	Windows codepage number (e.g. from PR_INTERNET_CPID)
 * @cs:		Pointer to internal structure containing iconv charset string
 *
 * Returns %MAPI_E_NOT_FOUND on unknown codepage and @cs will be unchanged.
 */
HRESULT HrGetCPByCharset(const char *cs, unsigned int *id)
{
	for (size_t i = 0; i < ARRAY_SIZE(CPMAP); ++i)
		if (strcasecmp(CPMAP[i].charset, cs) == 0) {
			*id = CPMAP[i].codepage;
			return hrSuccess;
		}
	return MAPI_E_NOT_FOUND;
}

} /* namespace */
