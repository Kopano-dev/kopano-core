/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/charset/convert.h>
#include <mapicode.h>
#include <numeric>
#include <vector>
#include <stdexcept>
#include <string>
#include <kopano/stringutil.h>
#include <cerrno>
#include <cstring>
#define BUFSIZE 4096

using namespace std::string_literals;

namespace KC {

convert_exception::convert_exception(enum exception_type type, const std::string &message)
	: std::runtime_error(message)
	, m_type(type)
{}

unknown_charset_exception::unknown_charset_exception(const std::string &message)
	: convert_exception(eUnknownCharset, message)
{}

illegal_sequence_exception::illegal_sequence_exception(const std::string &message)
	: convert_exception(eIllegalSequence, message)
{}

HRESULT HrFromException(const convert_exception &ce)
{
	switch (ce.type()) {
	case convert_exception::eUnknownCharset:	return MAPI_E_NOT_FOUND;
	case convert_exception::eIllegalSequence:	return MAPI_E_INVALID_PARAMETER;
	default:					return MAPI_E_CALL_FAILED;
	}
}

// HACK: prototypes may differ depending on the compiler and/or system (the
// second parameter may or may not be 'const'). This redeclaration is a hack
// to have a common prototype "iconv_cast".
class ICONV_HACK {
public:
	ICONV_HACK(const char** ptr) : m_ptr(ptr) { }
	// the compiler will choose the right operator
	operator const char **() const { return m_ptr; }
	operator char**() { return const_cast <char**>(m_ptr); }

private:
	const char** m_ptr;
};

/**
 * The conversion context for iconv charset conversions takes a fromcode and a tocode,
 * which are the source and destination charsets, respectively. The 'tocode' may take
 * some extra options, separated with '//' from the charset, and then separated by commas
 *
 * This function accepts values accepted by GNU iconv:
 *
 * iso-8859-1//TRANSLIT,IGNORE
 * windows-1252//TRANSLIT
 *
 * The 'fromcode' can also take modifiers but they are ignored by iconv.
 *
 * Also, instead of IGNORE, the HTMLENTITY modifier can be used, eg:
 *
 * iso-8859-1//HTMLENTITY
 *
 * This works much like TRANSLIT, except that characters that cannot be represented in the
 * output character set are not represented by '?' but by the HTML entity '&#xxxx;'. This is useful
 * for generating HTML in which as many characters as possible are directly represented, but
 * other characters are represented by an HTML entity. Note: the HTMLENTITY modifier may only
 * be applied when the fromcode is CHARSET_WCHAR (this is purely an implementation limitation)
 *
 * Release builds default to //IGNORE (due to -DFORCE_CHARSET_CONVERSION
 * added by ./configure --enable-release), while debug builds default
 * to //NOIGNORE.
 *
 * @param tocode Destination charset
 * @param fromcode Source charset
 */
iconv_context::iconv_context(const char *tocode, const char *fromorig)
{
	std::string strfrom = fromorig;
	auto pos = strfrom.find("//");
	if (pos != strfrom.npos)
		/* // only meaningful for tocode */
		strfrom.erase(pos);
	std::string strto = tocode;
	pos = strto.find("//");

	if (pos != std::string::npos) {
		std::string options = strto.substr(pos+2);
		strto.erase(pos);
		std::vector<std::string> vOptions = tokenize(options, ",");
		std::vector<std::string>::const_iterator i;

		i = vOptions.begin();
		while (i != vOptions.end()) {
			if (*i == "IGNORE" || *i == "FORCE")
				m_bForce = true;
			else if (*i == "NOIGNORE" || *i == "NOFORCE")
				m_bForce = false;
			else if (*i == "HTMLENTITIES" &&
			    strcasecmp(strfrom.c_str(), CHARSET_WCHAR) == 0)
				m_bHTML = true;
			else if (*i == "TRANSLIT")
				m_translit_run = true;
			++i;
		}
	}

	if (m_translit_run) {
		m_cd = iconv_open((strto + "//TRANSLIT").c_str(), strfrom.c_str());
		if (m_cd != (iconv_t)(-1)) {
			/* Looks like GNU iconv */
			m_translit_run = false;
			return;
		}
		/* Skip accordingly many bytes for unconvertible characters */
		if (strcasecmp(strfrom.c_str(), "wchar_t") == 0)
			m_translit_adv = sizeof(wchar_t);
		else if (strcasecmp(strfrom.c_str(), "utf-16") == 0 ||
		    strcasecmp(strfrom.c_str(), "utf-16le") == 0 ||
		    strcasecmp(strfrom.c_str(), "utf-16be") == 0)
			m_translit_adv = sizeof(uint16_t);
		else if (strcasecmp(strfrom.c_str(), "utf-32") == 0 ||
		    strcasecmp(strfrom.c_str(), "utf-32le") == 0 ||
		    strcasecmp(strfrom.c_str(), "utf-32be") == 0)
			m_translit_adv = sizeof(uint32_t);
	}

	m_cd = iconv_open(strto.c_str(), strfrom.c_str());
	if (m_cd == (iconv_t)(-1))
		throw unknown_charset_exception(strfrom + " -> "s + strto +
		      ": " + strerror(errno));
}

iconv_context::~iconv_context()
{
	if (m_cd != (iconv_t)(-1)) {
		iconv_close(m_cd);
	}
}

iconv_context::iconv_context(iconv_context &&rhs) :
	m_cd(rhs.m_cd), m_bForce(rhs.m_bForce), m_bHTML(rhs.m_bHTML),
	m_translit_run(rhs.m_translit_run), m_translit_adv(rhs.m_translit_adv)
{
	rhs.m_cd = iconv_t(-1);
}

void iconv_context::doconvert(
	const char *lpFrom,
	size_t cbFrom,
	void *obj,
	const std::function<void(void *, const char *, std::size_t)>& appendFunc)
{
	char buf[BUFSIZE];
	const char *lpSrc = NULL;
	char *lpDst = NULL;
	size_t cbSrc = 0;
	size_t cbDst = 0;

	lpSrc = lpFrom;
	cbSrc = cbFrom;

	while (cbSrc) {
		lpDst = buf;
		cbDst = sizeof(buf);
		auto err = iconv(m_cd, ICONV_HACK(&lpSrc), &cbSrc, &lpDst, &cbDst);
		if (err != static_cast<size_t>(-1) || cbDst != sizeof(buf)) {
			// buf now contains converted chars, append them to output
			appendFunc(obj, buf, sizeof(buf) - cbDst);
			continue;
		}
		if (m_bHTML) {
			if (cbSrc < sizeof(wchar_t)) {
				// Do what //IGNORE would have done
				++lpSrc;
				--cbSrc;
				continue;
			}
			// Convert the codepoint to '&#12345;'
			wchar_t code;

			memcpy(&code, lpSrc, sizeof(code));
			auto wstrEntity = L"&#" + std::to_wstring(code) + L";";
			auto cbEntity = wstrEntity.size() * sizeof(wchar_t);
			auto lpEntity = reinterpret_cast<const char *>(wstrEntity.c_str());
			// Since we don't know in what charset we are outputting, we have to send
			// the entity through iconv so that it can convert it to the target charset.
			err = iconv(m_cd, ICONV_HACK(&lpEntity), &cbEntity, &lpDst, &cbDst);
			if (err == static_cast<size_t>(-1))
				assert(false); // This will should never fail
			lpSrc += sizeof(wchar_t);
			cbSrc -= sizeof(wchar_t);
		} else if (m_translit_run) {
			if (cbSrc >= m_translit_adv) {
				lpSrc += m_translit_adv;
				cbSrc -= m_translit_adv;
				buf[0] = '?';
				--cbDst;
			}
		} else if (m_bForce) {
			// Force conversion by skipping this character
			if (cbSrc) {
				++lpSrc;
				--cbSrc;
			}
		} else {
			throw illegal_sequence_exception(strerror(errno));
		}
		// buf now contains converted chars, append them to output
		appendFunc(obj, buf, sizeof(buf) - cbDst);
	}

	// Finalize (needed for stateful conversion)
	lpDst = buf;
	cbDst = sizeof(buf);
	if (iconv(m_cd, nullptr, nullptr, &lpDst, &cbDst) != static_cast<size_t>(-1)) {
		appendFunc(obj, buf, sizeof(buf) - cbDst);
	}
}

} /* namespace */
