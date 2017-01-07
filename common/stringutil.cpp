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
#include <algorithm>
#include <string>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <sstream>

#include <kopano/ECIConv.h>

std::string stringify(unsigned int x, bool usehex, bool _signed) {
	char szBuff[33];

	if(usehex)
		sprintf(szBuff, "0x%08X", x);
	else {
		if(_signed)
			sprintf(szBuff, "%d", x);
		else
			sprintf(szBuff, "%u", x);
	}
	
	return szBuff;
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

std::string stringify_float(float x) {
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
		} catch (std::runtime_error &) {
			// locale not available, print in C
		}
		s << x;
	} else
		s << x;

	return s.str();
}

/* Convert time_t to string representation
 *
 * String is in format YYYY-MM-DD mm:hh in the local timezone
 *
 * @param time_t x Timestamp to convert
 * @return string String representation
 */
std::string stringify_datetime(time_t x) {
	char date[128];
	struct tm *tm;
	
	tm = localtime(&x);
	if(!tm){
		x = 0;
		tm = localtime(&x);
	}
	
	//strftime(date, 128, "%Y-%m-%d %H:%M:%S", tm);
	snprintf(date,128,"%d-%02d-%02d %.2d:%.2d:%.2d",tm->tm_year+1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	
	return date;
}

// FIXME support only unsigned int!!!
std::wstring wstringify(unsigned int x, bool usehex, bool _signed)
{
	std::wostringstream s;

	if (usehex) {
		s.flags(std::ios::showbase);
		s.setf(std::ios::hex, std::ios::basefield); // showbase && basefield: add 0x prefix
		s.setf(std::ios::uppercase);
	}
	s << x;

	return s.str();
}

std::wstring wstringify_int64(int64_t x, bool usehex)
{
	std::wostringstream s;

	if (usehex) {
		s.flags(std::ios::showbase);
		s.setf(std::ios::hex, std::ios::basefield);	// showbase && basefield: add 0x prefix
		s.setf(std::ios::uppercase);
	}
	s << x;

	return s.str();
}

std::wstring wstringify_uint64(uint64_t x, bool usehex)
{
	std::wostringstream s;

	if (usehex) {
		s.flags(std::ios::showbase);
		s.setf(std::ios::hex, std::ios::basefield);	// showbase && basefield: add 0x prefix
		s.setf(std::ios::uppercase);
	}
	s << x;

	return s.str();
}

std::wstring wstringify_float(float x)
{
	std::wostringstream s;

	s << x;

	return s.str();
}

std::wstring wstringify_double(double x, int prec)
{
	std::wostringstream s;

	s.precision(prec);
	s << x;

	return s.str();
}

unsigned int xtoi(const char *lpszHex)
{
	unsigned int ulHex = 0;

	sscanf(lpszHex, "%X", &ulHex);

	return ulHex;
}

int memsubstr(const void* haystack, size_t haystackSize, const void* needle, size_t needleSize)
{
	size_t pos = 0;
	size_t match = 0;
	BYTE* searchbuf = (BYTE*)needle;
	BYTE* databuf = (BYTE*)haystack;

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

	return path.c_str();
}

std::string shell_escape(const std::string &str)
{
	std::string::const_iterator start;
	std::string::const_iterator ptr;
	std::string escaped;

	start = ptr = str.begin();
	while (ptr != str.end()) {
		while (ptr != str.end() && *ptr != '\'')
			++ptr;

		escaped += std::string(start, ptr);
		if (ptr == str.end())
			break;

		start = ++ptr;          // skip single quote
		escaped += "'\\''";     // shell escape sequence
	}

	return escaped;
}

std::string shell_escape(const std::wstring &wstr)
{
	std::string strLocale = convert_to<std::string>(wstr);
	return shell_escape(strLocale);
}

std::vector<std::wstring> tokenize(const std::wstring &strInput, const WCHAR sep, bool bFilterEmpty) {
	const WCHAR *begin, *end = NULL;
	std::vector<std::wstring> vct;

	begin = strInput.c_str();
	while (*begin != '\0') {
		end = wcschr(begin, sep);
		if (!end) {
			vct.push_back(begin);
			break;
		}
		if (!bFilterEmpty || std::distance(begin,end) > 0)
			vct.push_back(std::wstring(begin,end));
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
			vct.push_back(begin);
			break;
		}
		if (!bFilterEmpty || std::distance(begin,end) > 0)
			vct.push_back(std::string(begin,end));
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

std::string bin2hex(unsigned int inLength, const unsigned char *input)
{
	static const char digits[] = "0123456789ABCDEF";
	std::string buffer;

	if (!input)
		return buffer;

	buffer.reserve(inLength * 2);
	for (unsigned int i = 0; i < inLength; ++i) {
		buffer += digits[input[i]>>4];
		buffer += digits[input[i]&0x0F];
	}

	return buffer;
}

std::string bin2hex(const std::string &input)
{
    return bin2hex((unsigned int)input.size(), (const unsigned char*)input.c_str());
}

std::wstring bin2hexw(unsigned int inLength, const unsigned char *input)
{
	static const wchar_t digits[] = L"0123456789ABCDEF";
	std::wstring buffer;

	if (!input)
		return buffer;

	buffer.reserve(inLength * 2);
	for (unsigned int i = 0; i < inLength; ++i) {
		buffer += digits[input[i]>>4];
		buffer += digits[input[i]&0x0F];
	}

	return buffer;
}

std::wstring bin2hexw(const std::string &input)
{
    return bin2hexw((unsigned int)input.size(), (const unsigned char*)input.c_str());
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

	output.reserve(input.length());
	for (size_t i = 0; i < input.length(); ++i) {
		if (input[i] <= 127) {
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
				output += digits[input[i]>>4];
				output += digits[input[i]&0x0F];
				break;
			default:
				output += input[i];
			}
		} else {
			output += '%';
			output += digits[input[i]>>4];
			output += digits[input[i]&0x0F];
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
	std::string output = convert_to<std::string>(charset, input, rawsize(input), CHARSET_WCHAR);
	return urlEncode(output);
}

std::string urlEncode(const WCHAR* input, const char* charset)
{
	std::string output = convert_to<std::string>(charset, input, rawsize(input), CHARSET_WCHAR);
	return urlEncode(output);
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

	strOutput.reserve(strInput.length());

	for (auto c : strInput)
		if (c == '\t')
			strOutput.append(4, ' ');
		else
			strOutput.append(1, c);

	lpstrOutput->swap(strOutput);
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
		else
			strOutput.append(1, *iInput);
		
	}
	lpstrOutput->swap(strOutput);
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

	strOutput.reserve(strInOut.size());

	for (i = strInOut.begin(); i != strInOut.end(); ++i)
		if (*i == '\n' && i != strInOut.begin() && *(i-1) != '\r')
			strOutput.append("\r\n");
		else
			strOutput.append(1, *i);

	swap(strInOut, strOutput);
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
	std::string h = full, n = needle;
	std::transform(h.begin(), h.end(), h.begin(), ::tolower);
	std::transform(n.begin(), n.end(), n.begin(), ::tolower);
	return kc_starts_with(h, n);
}

bool kc_ends_with(const std::string &full, const std::string &prefix)
{
	size_t fz = full.size(), pz = prefix.size();
	if (fz < pz)
		 return false;
	return full.compare(fz - pz, pz, prefix);
}
