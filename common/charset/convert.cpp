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
#include <kopano/charset/convert.h>

#include <mapicode.h>

#include <numeric>
#include <vector>
#include <string>
#include <kopano/stringutil.h>
#include <cerrno>
#define BUFSIZE 4096

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

namespace details {

	HRESULT HrFromException(const convert_exception &ce)
	{
		switch (ce.type()) {
			case convert_exception::eUnknownCharset:	return MAPI_E_NOT_FOUND;
			case convert_exception::eIllegalSequence:	return MAPI_E_INVALID_PARAMETER;
			default:									return MAPI_E_CALL_FAILED;
		}
	}

	// HACK: prototypes may differ depending on the compiler and/or system (the
	// second parameter may or may not be 'const'). This redeclaration is a hack
	// to have a common prototype "iconv_cast".
	class ICONV_HACK
	{
	public:
		ICONV_HACK(const char** ptr) : m_ptr(ptr) { }

		// the compiler will choose the right operator
		operator const char **(void) const { return m_ptr; }
		operator char**() { return const_cast <char**>(m_ptr); }

	private:
		const char** m_ptr;
	};
	
	
	/**
	 * Constructor for iconv_context_base
	 *
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
	iconv_context_base::iconv_context_base(const char* tocode, const char* fromcode)
	{
#ifdef FORCE_CHARSET_CONVERSION		
		// We now default to ignoring illegal sequences during conversion; this makes sure that we don't SIGABORT
		// when some bad input from a user fails to convert. This means that the 'IGNORE'
		// flag is on by default; specifying it is not useful.
		m_bForce = true;
#else
		// In debug builds, SIGABRT will be triggered in most cases due to the throw() 
		// in doconvert()
		m_bForce = false;
#endif
		m_bHTML = false;
		
        std::string strto = tocode;
        size_t pos = strto.find("//");

        if(pos != std::string::npos) {
            std::string options = strto.substr(pos+2);
            strto = strto.substr(0,pos);
            std::vector<std::string> vOptions = tokenize(options, ",");
            std::vector<std::string> vOptionsFiltered;
            std::vector<std::string>::const_iterator i;

            i = vOptions.begin();
            while(i != vOptions.end()) {
                if (*i == "IGNORE" || *i == "FORCE") {
                    m_bForce = true;
                } else if (*i == "NOIGNORE" || *i == "NOFORCE") {
                    m_bForce = false;
                } else if(*i == "HTMLENTITIES" && strcasecmp(fromcode, CHARSET_WCHAR) == 0) {
                	m_bHTML = true;
                } else vOptionsFiltered.push_back(*i);
				++i;
            }

			if(!vOptionsFiltered.empty()) {
	            strto += "//";
				strto += join(vOptionsFiltered.begin(), vOptionsFiltered.end(), std::string(","));
			}
        }

		m_cd = iconv_open(strto.c_str(), fromcode);
		if (m_cd == (iconv_t)(-1))
			throw unknown_charset_exception(strerror(errno));
	}

	iconv_context_base::~iconv_context_base()
	{
		if (m_cd != (iconv_t)(-1))
			iconv_close(m_cd);
	}

	void iconv_context_base::doconvert(const char *lpFrom, size_t cbFrom)
	{
		char buf[BUFSIZE];
		const char *lpSrc = NULL;
		char *lpDst = NULL;
		size_t cbSrc = 0;
		size_t cbDst = 0;
		size_t err;
		
		lpSrc = lpFrom;
		cbSrc = cbFrom;
		
		while(cbSrc) {
			lpDst = buf;
			cbDst = sizeof(buf);
			err = iconv(m_cd, ICONV_HACK(&lpSrc), &cbSrc, &lpDst, &cbDst);
			
			if (err == (size_t)(-1) && cbDst == sizeof(buf)) {
				if(m_bHTML) {
					if(cbSrc < sizeof(wchar_t)) {
						// Do what //IGNORE would have done
						++lpSrc;
						--cbSrc;
					} else {
						// Convert the codepoint to '&#12345;'
						std::wstring wstrEntity = L"&#";
						size_t cbEntity;
						wchar_t code;
						const char *lpEntity;
						
						memcpy(&code, lpSrc, sizeof(code));
						wstrEntity += wstringify(code);
						wstrEntity += L";";
						cbEntity = wstrEntity.size() * sizeof(wchar_t);
						lpEntity = (const char *)wstrEntity.c_str();
						
						// Since we don't know in what charset we are outputting, we have to send
						// the entity through iconv so that it can convert it to the target charset.
						
						err = iconv(m_cd, ICONV_HACK(&lpEntity), &cbEntity, &lpDst, &cbDst);
						
						if(err == (size_t)(-1)) {
							ASSERT(false); // This will should never fail
						}
						
						lpSrc += sizeof(wchar_t);
						cbSrc -= sizeof(wchar_t);
					}
				} else if(m_bForce) {
					// Force conversion by skipping this character
					if(cbSrc) {
						++lpSrc;
						--cbSrc;
					}
				} else {
					throw illegal_sequence_exception(strerror(errno));
				}
			}			
			// buf now contains converted chars, append them to output
			append(buf, sizeof(buf) - cbDst);
		}

		// Finalize (needed for stateful conversion)	
		lpDst = buf;
		cbDst = sizeof(buf);
		err = iconv(m_cd, NULL, NULL, &lpDst, &cbDst);
		append(buf, sizeof(buf) - cbDst);
	}
	
} // namespace details

convert_context::convert_context()
{}

convert_context::~convert_context()
{
	context_map::iterator iContext;
	for (iContext = m_contexts.begin(); iContext != m_contexts.end(); ++iContext)
		delete iContext->second;
		
	code_set::iterator iCode;
	for (iCode = m_codes.begin(); iCode != m_codes.end(); ++iCode)
		delete[] *iCode;
}

void convert_context::persist_code(context_key &key, unsigned flags)
{
	if (flags & pfToCode) {
		code_set::const_iterator iCode = m_codes.find(key.tocode);
		if (iCode == m_codes.end()) {
			char *tocode = new char[strlen(key.tocode) + 1];
			memcpy(tocode, key.tocode, strlen(key.tocode) + 1);
			iCode = m_codes.insert(tocode).first;
		}
		key.tocode = *iCode;
	}
	if (flags & pfFromCode) {
		code_set::const_iterator iCode = m_codes.find(key.fromcode);
		if (iCode == m_codes.end()) {
			char *fromcode = new char[strlen(key.fromcode) + 1];
			memcpy(fromcode, key.fromcode, strlen(key.fromcode) + 1);
			iCode = m_codes.insert(fromcode).first;
		}
		key.fromcode = *iCode;
	}
}

char* convert_context::persist_string(const std::string &strValue)
{
	m_lstStrings.push_back(strValue);
	return const_cast<char*>(m_lstStrings.back().c_str());
}

wchar_t* convert_context::persist_string(const std::wstring &wstrValue)
{
	m_lstWstrings.push_back(wstrValue);
	return const_cast<wchar_t*>(m_lstWstrings.back().c_str());
}
