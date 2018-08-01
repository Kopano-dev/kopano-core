/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

// From http://www.wischik.com/lu/programmer/mapi_utils.html
// Parts rewritten by Zarafa
#include <kopano/platform.h>
#include <iostream>
#include <map>
#include <utility>
#include <kopano/codepage.h>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include <kopano/charset/convert.h>
#include <kopano/stringutil.h>
#include "HtmlEntity.h"
#include "rtfutil.h"
#include <string>
#include <sstream>

namespace KC {

static const char szHex[] = "0123456789ABCDEF";

// Charsets used in \fcharsetXXX (from http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dnrtfspec/html/rtfspec_6.asp )
// charset "" is the ANSI codepage specified in \ansicpg
// charset NULL means 'no conversion', ie direct 1-to-1 translation to UNICODE
static const struct _rtfcharset {
	int id;
	const char *charset;
} RTFCHARSET[] = {
	{0, ""}, // This is actually the codepage specified in \ansicpg
	{1, ""}, // default charset, probably also the codepage in \ansicpg
	{2, ""}, // This is SYMBOL, but in practice, we can just send the data
	// one-on-one down the line, as the actual character codes
	// don't change when converted to unicode (because the other
	// side will also be using the MS Symbol font to display)
	{3, NULL}, // 'Invalid'
	{77, "MAC"}, // Unsure if this is the correct charset
	{128,"SJIS"}, // OR cp 932 ?
	{129,"euc-kr"}, // 'Hangul' korean
	{130,"JOHAB"},
	{134,"GB2312"},
	{136,"BIG5"},
	{161,"windows-1253"},
	{162,"windows-1254"}, // 'Turkish'
	{163,"windows-1258"}, // Vietnamese
	{177,"windows-1255"}, // Hebrew
	{178,"windows-1256"}, // Arabic
	{179,"windows-1256"}, // Arabic traditional
	{180,"windows-1256"}, // Arabic user
	{181,"windows-1255"}, // Hebrew user
	{186,"windows-1257"},
	{204,"windows-1251"}, // Cyrillic for russian
	{222,NULL},		// Thai ?
	{238,"windows-1250"}, // Eastern European
	{254,"IBM437"},
	{255,NULL} 		// OEM
};

struct RTFSTATE {
	int ulFont;
	const char *szCharset;
	bool bInFontTbl;
	bool bInColorTbl;
	bool bInSkipTbl;
	std::string output;			// text in current szCharset
	bool bRTFOnly;
	int ulUnicodeSkip;			// number of characters to skip after a unicode character
	int ulSkipChars;
};

#define RTF_MAXSTATE 	256
#define RTF_MAXCMD		64
typedef std::map<int, int> fontmap_t;

/**
 * Converts RTF \ansicpgN <N> number to normal charset string.
 *
 * @param[in]	id	RTF codepage number
 * @param[out]	lpszCharset static charset string
 * @retval		MAPI_E_NOT_FOUND if id was unknown
 */
static HRESULT HrGetCharsetByRTFID(int id, const char **lpszCharset)
{
	for (size_t i = 0; i < ARRAY_SIZE(RTFCHARSET); ++i) {
		if(RTFCHARSET[i].id == id) {
			*lpszCharset = RTFCHARSET[i].charset;
			return hrSuccess;
		}
	}
	return MAPI_E_NOT_FOUND;
}

class rtfstringcomp {
	public:
	bool operator()(const char *a, const char *b) const { return strcmp(a, b) < 0; }
};

static bool isRTFIgnoreCommand(const char *lpCommand)
{
	static const std::set<const char *, rtfstringcomp> rtf_ignore_cmds = {
		"stylesheet", "revtbl", "xmlnstbl", "rsidtbl", "fldinst",
		"shpinst", "wgrffmtfilter", "pnseclvl", "atrfstart", "atrfend",
		"atnauthor", "annotation", "sp", "atnid", "xmlopen",
		// "fldrslt"
	};
	return lpCommand != nullptr &&
	       rtf_ignore_cmds.find(lpCommand) != rtf_ignore_cmds.cend();
}

/**
 * Initializes an RTFState struct to default values.
 *
 * @param[in/out]	sState	pointer to RTFState struct to init
 */
static void InitRTFState(RTFSTATE *sState)
{
	sState->bInSkipTbl = false;
	sState->bInFontTbl = false;
	sState->bInColorTbl = false;
	sState->szCharset = "us-ascii";
	sState->bRTFOnly = false;
	sState->ulFont = 0;
	sState->ulUnicodeSkip = 1;
	sState->ulSkipChars = 0;
}

static std::wstring RTFFlushStateOutput(convert_context &convertContext,
    RTFSTATE *sState, ULONG ulState)
{
	std::wstring wstrUnicode;

	if (sState[ulState].output.empty())
		return wstrUnicode;
	TryConvert(convertContext, sState[ulState].output, rawsize(sState[ulState].output), sState[ulState].szCharset, wstrUnicode);
	sState[ulState].output.clear();
	return wstrUnicode;
}

/**
 * Converts RTF text into HTML text. It will return an HTML string in
 * the given codepage.
 *
 * To convert between the RTF text and HTML codepage text, we use a
 * WCHAR string as intermediate.
 *
 * @param[in]	lpStrRTFIn		RTF input string that contains \fromtext
 * @param[out]	lpStrHTMLOut	HTML output in requested ulCodepage
 * @param[out]	ulCodepage		codepage for HTML output
 */
HRESULT HrExtractHTMLFromRTF(const std::string &lpStrRTFIn,
    std::string &lpStrHTMLOut, ULONG ulCodepage)
{
	HRESULT hr;
	const char *szInput = lpStrRTFIn.c_str();
	auto rtfend = szInput + lpStrRTFIn.size();
	const char *szANSICharset = "us-ascii";
	const char *szHTMLCharset;
	std::string strConvertCharset;
	std::wstring strOutput;
	int ulState = 0;
	RTFSTATE sState[RTF_MAXSTATE];
	fontmap_t mapFontToCharset;
	convert_context convertContext;

	// Find \\htmltag, if there is none we can't extract HTML
	if (strstr(szInput, "{\\*\\htmltag") == NULL)
		return MAPI_E_NOT_FOUND;

	// select output charset
	hr = HrGetCharsetByCP(ulCodepage, &szHTMLCharset);
	if (hr != hrSuccess) {
		szHTMLCharset = "us-ascii";
		hr = hrSuccess;
	}
	strConvertCharset = szHTMLCharset + std::string("//HTMLENTITIES");
	InitRTFState(&sState[0]);

	while (szInput < rtfend) {
		if(strncmp(szInput,"\\*",2) == 0) {
			szInput+=2;
		} else if(*szInput == '\\') {
			// Command
			char szCommand[RTF_MAXCMD];
			char *szCmdOutput;
			int lArg = -1;
			bool bNeg = false;

			++szInput;
			if(isalpha(*szInput)) {
				szCmdOutput = szCommand;

				while (isalpha(*szInput) && szCmdOutput < szCommand + RTF_MAXCMD - 1)
					*szCmdOutput++ = *szInput++;
				*szCmdOutput = 0;

				if(*szInput == '-') {
					bNeg = true;
					++szInput;
				}
				if(isdigit(*szInput)) {
					lArg = 0;
					while (isdigit(*szInput)) {
						lArg = lArg * 10 + *szInput - '0';
						++szInput;
					}
					if(bNeg) lArg = -lArg;
				}
				if(*szInput == ' ')
					++szInput;

				// szCommand is the command, lArg is the argument.
				if(strcmp(szCommand,"fonttbl") == 0) {
					sState[ulState].bInFontTbl = true;
				} else if(strcmp(szCommand,"colortbl") == 0) {
					sState[ulState].bInColorTbl = true;
				} else if(strcmp(szCommand,"pntext") == 0) { // pntext is the plaintext alternative, ignore it.
					sState[ulState].bRTFOnly = true;
				} else if(strcmp(szCommand,"ansicpg") == 0) {
					if(HrGetCharsetByCP(lArg, &szANSICharset) != hrSuccess)
						szANSICharset = "us-ascii";
					sState[ulState].szCharset = szANSICharset;
				} else if(strcmp(szCommand,"fcharset") == 0) {
					if (sState[ulState].bInFontTbl)
						mapFontToCharset.emplace(sState[ulState].ulFont, lArg);
				} else if(strcmp(szCommand,"htmltag") == 0) {
				} else if(strcmp(szCommand,"mhtmltag") == 0) {
				} else if (strcmp(szCommand,"pard") == 0) {
				} else if (strcmp(szCommand,"par") == 0) {
					if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl) {
						sState[ulState].output.append(1,'\r');
						sState[ulState].output.append(1,'\n');
					}
				} else if(strcmp(szCommand,"tab") == 0) {
					if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl) {
						sState[ulState].output.append(1,' ');
						sState[ulState].output.append(1,' ');
						sState[ulState].output.append(1,' ');
					}
				} else if (strcmp(szCommand,"uc") == 0) {
					sState[ulState].ulUnicodeSkip = lArg;
				} else if(strcmp(szCommand,"f") == 0) {
					sState[ulState].ulFont = lArg;

					if(!sState[ulState].bInFontTbl) {
						fontmap_t::const_iterator i = mapFontToCharset.find(lArg);
						if (i == mapFontToCharset.cend())
							continue;
						// Output any data before this point
						strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
						// Set new charset
						HrGetCharsetByRTFID(i->second, &sState[ulState].szCharset);
						if (sState[ulState].szCharset == nullptr)
							sState[ulState].szCharset = "us-ascii";
						else if (sState[ulState].szCharset[0] == 0)
							sState[ulState].szCharset = szANSICharset;
					}
					// ignore error
				}
				else if (strcmp(szCommand,"u") == 0) {
					// unicode character, in signed short WCHAR
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					if (!sState[ulState].bRTFOnly)
						strOutput.append(1, (unsigned short)lArg); // add as literal character
					sState[ulState].ulSkipChars += sState[ulState].ulUnicodeSkip;
				}
				else if(strcmp(szCommand,"htmlrtf") == 0) {
					sState[ulState].bRTFOnly = lArg != 0;
				}else if(isRTFIgnoreCommand(szCommand)) {
					sState[ulState].bInSkipTbl = true;
				}
			}
			// Non-alnum after '\'
			else if(*szInput == '\\') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'\\');
			}
			else if(*szInput == '{') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'{');
			}
			else if(*szInput == '}') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'}');
			}
			else if(*szInput == '\'') {
				unsigned int ulChar;

				while(*szInput == '\'')
				{
					ulChar = 0;
					++szInput;

					if(*szInput) {
						ulChar = (unsigned int) (strchr(szHex, toupper(*szInput)) == NULL ? 0 : (strchr(szHex, toupper(*szInput)) - szHex));
						ulChar = ulChar << 4;
						++szInput;
					}
					if(*szInput) {
						ulChar += (unsigned int) (strchr(szHex, toupper(*szInput)) == NULL ? 0 : (strchr(szHex, toupper(*szInput)) - szHex));
						++szInput;
					}

					if (!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl && !sState[ulState].ulSkipChars)
						sState[ulState].output.append(1,ulChar);
					else if (sState[ulState].ulSkipChars)
						--sState[ulState].ulSkipChars;

					if(*szInput == '\\' && *(szInput+1) == '\'')
						++szInput;
					else
						break;
				}
			} else {
				++szInput; // skip single character after '\'
			}
		} // Non-command
		else if(*szInput == '{') {
			// Dump output data
			strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
			++ulState;
			if (ulState >= RTF_MAXSTATE)
				return MAPI_E_NOT_ENOUGH_MEMORY;
			sState[ulState] = sState[ulState-1];
			sState[ulState].output.clear();
			++szInput;
		} else if(*szInput == '}') {
			// Dump output data
			strOutput += RTFFlushStateOutput(convertContext, sState, ulState);

			if(ulState > 0)
				--ulState;
			++szInput;
		} else if (*szInput == '\r' || *szInput == '\n' || *szInput == '\0') {
			++szInput;
		} else {
			if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl && !sState[ulState].ulSkipChars) {
				sState[ulState].output.append(1,*szInput);
			} else if (sState[ulState].ulSkipChars)
				--sState[ulState].ulSkipChars;
			++szInput;
		}
	}

	strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
	try {
		lpStrHTMLOut = convertContext.convert_to<std::string>(strConvertCharset.c_str(), strOutput, rawsize(strOutput), CHARSET_WCHAR);
	} catch (const convert_exception &ce) {
		hr = HrFromException(ce);
	}
	return hr;
}

/**
 * Extracts the Plain text that was encapsulated in an RTF text, and
 * writes out HTML. It will return an HTML string in the given
 * codepage.
 *
 * To convert between the RTF text and HTML codepage text, we use a
 * WCHAR string as intermediate.
 *
 * @param[in]	lpStrRTFIn		RTF input string that contains \fromtext
 * @param[out]	lpStrHTMLOut	HTML output in requested ulCodepage
 * @param[out]	ulCodepage		codepage for HTML output
 */
HRESULT HrExtractHTMLFromTextRTF(const std::string &lpStrRTFIn,
    std::string &lpStrHTMLOut, ULONG ulCodepage)
{
	HRESULT hr;
	std::wstring wstrUnicodeTmp;
	const char *szInput = lpStrRTFIn.c_str();
	auto rtfend = szInput + lpStrRTFIn.size();
	const char *szANSICharset = "us-ascii";
	const char *szHTMLCharset;
	std::string strConvertCharset, tmp;
	std::wstring strOutput;
	int ulState = 0;
	bool bPar = false;
	int nLineChar=0;
	RTFSTATE sState[RTF_MAXSTATE];
	fontmap_t mapFontToCharset;
	convert_context convertContext;

	// select output charset
	hr = HrGetCharsetByCP(ulCodepage, &szHTMLCharset);
	if (hr != hrSuccess) {
		szHTMLCharset = "us-ascii";
		hr = hrSuccess;
	}
	strConvertCharset = szHTMLCharset + std::string("//HTMLENTITIES");

	tmp =	"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\r\n" \
		 "<HTML>\r\n" \
		 "<HEAD>\r\n" \
		 "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=";
	tmp += szHTMLCharset;
	tmp += 	"\">\r\n"												\
		 "<META NAME=\"Generator\" CONTENT=\"Kopano HrExtractHTMLFromTextRTF\">\r\n" \
		 "<TITLE></TITLE>\r\n" \
		 "</HEAD>\r\n" \
		 "<BODY>\r\n" \
		 "<!-- Converted from text/plain format -->\r\n" \
		 "\r\n"; //FIXME create title on the fly ?

	wstrUnicodeTmp.resize(0,0);
	TryConvert(convertContext, tmp, rawsize(tmp), "us-ascii", wstrUnicodeTmp);
	strOutput.append(wstrUnicodeTmp);
	InitRTFState(&sState[0]);

	while (szInput < rtfend) {
		if(strncmp(szInput,"\\*",2) == 0) {
			szInput+=2;
		} else if(*szInput == '\\') {
			// Command
			char szCommand[RTF_MAXCMD];
			char *szCmdOutput;
			int lArg = -1;
			bool bNeg = false;

			++szInput;
			if(isalpha(*szInput)) {
				szCmdOutput = szCommand;

				while (isalpha(*szInput) && szCmdOutput < szCommand + RTF_MAXCMD - 1)
					*szCmdOutput++ = *szInput++;
				*szCmdOutput = 0;

				if(*szInput == '-') {
					bNeg = true;
					++szInput;
				}
				if(isdigit(*szInput)) {
					lArg = 0;
					while (isdigit(*szInput)) {
						lArg = lArg * 10 + *szInput - '0';
						++szInput;
					}
					if(bNeg) lArg = -lArg;
				}
				if(*szInput == ' ')
					++szInput;

				// szCommand is the command, lArg is the argument.
				if(strcmp(szCommand,"fonttbl") == 0) {
					sState[ulState].bInFontTbl = true;
				} else if(strcmp(szCommand,"colortbl") == 0) {
					sState[ulState].bInColorTbl = true;
				} else if(strcmp(szCommand,"pntext") == 0) { // pntext is the plaintext alternative, ignore it.
					sState[ulState].bRTFOnly = true;
				} else if(strcmp(szCommand,"ansicpg") == 0) {
					if(HrGetCharsetByCP(lArg, &szANSICharset) != hrSuccess)
						szANSICharset = "us-ascii";
					sState[ulState].szCharset = szANSICharset;
				} else if(strcmp(szCommand,"fcharset") == 0) {
					if (sState[ulState].bInFontTbl)
						mapFontToCharset.emplace(sState[ulState].ulFont, lArg);
				} else if(strcmp(szCommand,"htmltag") == 0) {
				} else if(strcmp(szCommand,"mhtmltag") == 0) {
				} else if (strcmp(szCommand, "line") == 0) {
					sState[ulState].output.append("<br>\r\n");
				} else if (strcmp(szCommand, "par") == 0 &&
				    !sState[ulState].bInFontTbl &&
				    !sState[ulState].bRTFOnly &&
				    !sState[ulState].bInColorTbl &&
				    !sState[ulState].bInSkipTbl &&
				    bPar) {
					sState[ulState].output.append("</P>\r\n\r\n");
					bPar = false;
					nLineChar = 0;
				} else if(strcmp(szCommand,"tab") == 0) {
					if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl) {
						sState[ulState].output.append(1,' ');
						sState[ulState].output.append(1,' ');
						sState[ulState].output.append(1,' ');
					}
				} else if (strcmp(szCommand,"uc") == 0) {
					sState[ulState].ulUnicodeSkip = lArg;
				} else if(strcmp(szCommand,"f") == 0) {
					sState[ulState].ulFont = lArg;

					if(!sState[ulState].bInFontTbl) {
						fontmap_t::const_iterator i = mapFontToCharset.find(lArg);
						if (i == mapFontToCharset.cend())
							continue;
						// Output any data before this point
						strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
						// Set new charset
						HrGetCharsetByRTFID(i->second, &sState[ulState].szCharset);
						if (sState[ulState].szCharset == nullptr)
							sState[ulState].szCharset = "us-ascii";
						else if (sState[ulState].szCharset[0] == 0)
							sState[ulState].szCharset = szANSICharset;
					}
					// ignore error
				}
				else if (strcmp(szCommand,"u") == 0) {
					if (!bPar) {
						sState[ulState].output.append("<p>");
						bPar = true;
					}
					// unicode character, in signed short WCHAR
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					if (!sState[ulState].bRTFOnly)
						strOutput.append(1, (unsigned short)lArg); // add as literal character
					sState[ulState].ulSkipChars += sState[ulState].ulUnicodeSkip;
				}
				else if(strcmp(szCommand,"htmlrtf") == 0) {
					sState[ulState].bRTFOnly = lArg != 0;
				} else if(isRTFIgnoreCommand(szCommand)) {
					sState[ulState].bInSkipTbl = true;
				}
			}
			// Non-alnum after '\'
			else if(*szInput == '\\') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'\\');
			}
			else if(*szInput == '{') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'{');
			}
			else if(*szInput == '}') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'}');
			}
			else if(*szInput == '\'') {
				unsigned int ulChar;

				if (!bPar) {
					sState[ulState].output.append("<p>");
					bPar = true;
				}
				// Dump output data until now, if we're switching charsets
				if (szANSICharset == nullptr || strcmp(sState[ulState].szCharset, szANSICharset) != 0)
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);

				while(*szInput == '\'')
				{
					ulChar = 0;
					++szInput;

					if(*szInput) {
						ulChar = (unsigned int) (strchr(szHex, toupper(*szInput)) == NULL ? 0 : (strchr(szHex, toupper(*szInput)) - szHex));
						ulChar = ulChar << 4;
						++szInput;
					}
					if(*szInput) {
						ulChar += (unsigned int) (strchr(szHex, toupper(*szInput)) == NULL ? 0 : (strchr(szHex, toupper(*szInput)) - szHex));
						++szInput;
					}

					if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].ulSkipChars) {
						sState[ulState].output.append(1,ulChar);
					} else if (sState[ulState].ulSkipChars)
						--sState[ulState].ulSkipChars;

					if(*szInput == '\\' && *(szInput+1) == '\'')
						++szInput;
					else
						break;
				}

				// Dump escaped data in charset 0 (ansicpg), if we had to switch charsets
				if (szANSICharset == nullptr || strcmp(sState[ulState].szCharset, szANSICharset) != 0)
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
			} else {
				++szInput; // skip single character after '\'
			}
		} // Non-command
		else if(*szInput == '{') {
			// Dump output data
			if (!sState[ulState].output.empty())
				strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
			++ulState;
			if (ulState >= RTF_MAXSTATE)
				return MAPI_E_NOT_ENOUGH_MEMORY;
			sState[ulState] = sState[ulState-1];
			++szInput;
		} else if(*szInput == '}') {
			// Dump output data
			strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
			if(ulState > 0)
				--ulState;
			++szInput;
		} else if (*szInput == '\r' || *szInput == '\n' || *szInput == '\0') {
			++szInput;
		} else if (!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl && !sState[ulState].ulSkipChars) {
			if (!bPar) {
				sState[ulState].output.append("<P>");
				bPar = true;
			}
			// Change space to &nbsp; . The last space is a real space like "&nbsp;&nbsp; " or " "
			if (*szInput == ' ') {
				++szInput;
				while (*szInput == ' ') {
					sState[ulState].output.append("&nbsp;");
					++szInput;
				}
				sState[ulState].output.append(1, ' ');
			} else {
				std::wstring entity;

				if (!CHtmlEntity::CharToHtmlEntity((WCHAR)*szInput, entity))
					sState[ulState].output.append(1, *szInput);
				else
					sState[ulState].output.append(entity.begin(), entity.end());
				++szInput;
			}
			++nLineChar;
		} else {
			if (sState[ulState].ulSkipChars)
				--sState[ulState].ulSkipChars;
			++szInput;
		}
	}

	strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
	if (bPar)
		strOutput += L"</p>\r\n";
	strOutput += L"\r\n" \
		     L"</BODY>\r\n" \
		     L"</HTML>\r\n";
	try {
		lpStrHTMLOut = convertContext.convert_to<std::string>(strConvertCharset.c_str(), strOutput, rawsize(strOutput), CHARSET_WCHAR);
	} catch (const convert_exception &ce) {
		hr = HrFromException(ce);
	}
	return hr;
}

/**
 * Extracts the HTML text that was encapsulated in an RTF text. It
 * will return an HTML string in the given codepage.
 *
 * To convert between the RTF text and HTML codepage text, we use a
 * WCHAR string as intermediate.
 *
 * @param[in]	lpStrRTFIn		RTF input string that contains \fromhtml
 * @param[out]	lpStrHTMLOut	HTML output in requested ulCodepage
 * @param[out]	ulCodepage		codepage for HTML output
 *
 * @todo Export the right HTML tags, now only plain stuff
 */
HRESULT HrExtractHTMLFromRealRTF(const std::string &lpStrRTFIn,
    std::string &lpStrHTMLOut, ULONG ulCodepage)
{
	HRESULT hr;
	std::wstring wstrUnicodeTmp;
	const char *szInput = lpStrRTFIn.c_str();
	auto rtfend = szInput + lpStrRTFIn.size();
	const char *szANSICharset = "us-ascii";
	const char *szHTMLCharset;
	std::string strConvertCharset, tmp;
	std::wstring strOutput;
	int ulState = 0;
	RTFSTATE sState[RTF_MAXSTATE];
	convert_context convertContext;
	fontmap_t mapFontToCharset;
	bool bPar = false;

	// select output charset
	hr = HrGetCharsetByCP(ulCodepage, &szHTMLCharset);
	if (hr != hrSuccess) {
		szHTMLCharset = "us-ascii";
		hr = hrSuccess;
	}
	strConvertCharset = szHTMLCharset + std::string("//HTMLENTITIES");

	tmp =	"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\r\n" \
		 "<HTML>\r\n" \
		 "<HEAD>\r\n" \
		 "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=";
	tmp += szHTMLCharset;
	tmp +=	"\">\r\n"															\
		 "<META NAME=\"Generator\" CONTENT=\"Kopano HrExtractHTMLFromRealRTF\">\r\n" \
		 "<TITLE></TITLE>\r\n" \
		 "</HEAD>\r\n" \
		 "<BODY>\r\n" \
		 "<!-- Converted from text/rtf format -->\r\n" \
		 "\r\n"; //FIXME create title on the fly ?

	TryConvert(convertContext, tmp, rawsize(tmp), "us-ascii", wstrUnicodeTmp);
	strOutput.append(wstrUnicodeTmp);
	InitRTFState(&sState[0]);

	while (szInput < rtfend) {
		if(strncmp(szInput,"\\*",2) == 0) {
			szInput+=2;
		} else if(*szInput == '\\') {
			// Command
			char szCommand[RTF_MAXCMD];
			char *szCmdOutput;
			int lArg = -1;
			bool bNeg = false;

			++szInput;
			if(isalpha(*szInput)) {
				szCmdOutput = szCommand;

				while (isalpha(*szInput) && szCmdOutput < szCommand + RTF_MAXCMD - 1)
					*szCmdOutput++ = *szInput++;
				*szCmdOutput = 0;

				if(*szInput == '-') {
					bNeg = true;
					++szInput;
				}
				if(isdigit(*szInput)) {
					lArg = 0;
					while (isdigit(*szInput)) {
						lArg = lArg * 10 + *szInput - '0';
						++szInput;
					}
					if(bNeg) lArg = -lArg;
				}
				if(*szInput == ' ')
					++szInput;

				// szCommand is the command, lArg is the argument.
				if(strcmp(szCommand,"fonttbl") == 0) {
					sState[ulState].bInFontTbl = true;
				} else if(strcmp(szCommand,"colortbl") == 0) {
					sState[ulState].bInColorTbl = true;
				} else if(strcmp(szCommand,"listtable") == 0) {
					sState[ulState].bInSkipTbl = true;
				} else if(strcmp(szCommand,"pntext") == 0) { // pntext is the plaintext alternative, ignore it.
					sState[ulState].bRTFOnly = true;
				} else if(strcmp(szCommand,"ansicpg") == 0) {
					if(HrGetCharsetByCP(lArg, &szANSICharset) != hrSuccess)
						szANSICharset = "us-ascii";
					sState[ulState].szCharset = szANSICharset;
				} else if(strcmp(szCommand,"fcharset") == 0) {
					if (sState[ulState].bInFontTbl)
						mapFontToCharset.emplace(sState[ulState].ulFont, lArg);
				} else if(strcmp(szCommand,"htmltag") == 0) {
				} else if(strcmp(szCommand,"latentstyles") == 0) {
					sState[ulState].bRTFOnly = true;
				} else if(strcmp(szCommand,"datastore") == 0) {
					sState[ulState].bRTFOnly = true;
				} else if(strcmp(szCommand,"mhtmltag") == 0) {
				} else if (strcmp(szCommand, "line") == 0) {
					sState[ulState].output.append("<br>\r\n");
				} else if (strcmp(szCommand,"par") == 0) {
					if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl) {
						if (bPar)
							sState[ulState].output.append("</p>\r\n\r\n");
						sState[ulState].output.append("<p>");
						bPar = true;
					}
				} else if(strcmp(szCommand,"tab") == 0) {
					if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl) {
						sState[ulState].output.append(1,' ');
						sState[ulState].output.append(1,' ');
						sState[ulState].output.append(1,' ');
					}
				} else if (strcmp(szCommand,"bin") == 0) {
					if (lArg > 0)
						szInput += lArg; // skip all binary bytes here.
				} else if (strcmp(szCommand,"uc") == 0) {
					sState[ulState].ulUnicodeSkip = lArg;
				} else if(strcmp(szCommand,"f") == 0) {
					sState[ulState].ulFont = lArg;

					if(!sState[ulState].bInFontTbl) {
						fontmap_t::const_iterator i = mapFontToCharset.find(lArg);
						if (i == mapFontToCharset.cend())
							continue;
						// Output any data before this point
						if (!sState[ulState].output.empty()) {
							strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
						}
						// Set new charset
						HrGetCharsetByRTFID(i->second, &sState[ulState].szCharset);
						if (sState[ulState].szCharset == nullptr)
							sState[ulState].szCharset = "us-ascii";
						else if (sState[ulState].szCharset[0] == 0)
							sState[ulState].szCharset = szANSICharset;
					}
					// ignore error
				}
				else if (strcmp(szCommand,"u") == 0) {
					if (!bPar) {
						sState[ulState].output.append("<p>");
						bPar = true;
					}
					// unicode character, in signed short WCHAR
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl) {
						std::wstring entity;

						if (! CHtmlEntity::CharToHtmlEntity((WCHAR)*szInput, entity))
							strOutput.append(1, (unsigned short)lArg);	// add as literal character
						else
							strOutput.append(entity.begin(), entity.end());
					}
					sState[ulState].ulSkipChars += sState[ulState].ulUnicodeSkip;
				}
				else if(strcmp(szCommand,"htmlrtf") == 0) {
					sState[ulState].bRTFOnly = lArg != 0;
				}
				/*else if(strcmp(szCommand,"b") == 0) {
				  if( lArg == -1)
				  sState[ulState].output.append("<b>", 3);
				  else
				  sState[ulState].output.append("</b>", 4);
				  }else if(strcmp(szCommand,"i") == 0) {
				  if( lArg == -1)
				  sState[ulState].output.append("<i>", 3);
				  else
				  sState[ulState].output.append("</i>", 4);
				  }else if(strcmp(szCommand,"ul") == 0) {
				  if( lArg == -1)
				  sState[ulState].output.append("<u>", 3);
				  else
				  sState[ulState].output.append("</u>", 4);
				  }else if(strcmp(szCommand,"ulnone") == 0) {
				  sState[ulState].output.append("</u>", 4);
				  }*/
				else if(strcmp(szCommand,"generator") == 0){
					while (*szInput != ';' && *szInput != '}' && *szInput)
						++szInput;
					if(*szInput == ';')
						++szInput;
				}
				else if(strcmp(szCommand,"bkmkstart") == 0 || strcmp(szCommand,"bkmkend") == 0){
					// skip bookmark name
					while (*szInput && isalnum(*szInput))
						++szInput;
					sState[ulState].bInSkipTbl = true;
				} else if (strcmp(szCommand, "endash") == 0) {
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					// windows-1252: 0x96, unicode 0x2013
					strOutput += 0x2013;
				} else if (strcmp(szCommand, "emdash") == 0) {
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					// windows-1252: 0x97, unicode 0x2014
					strOutput += 0x2014;
				} else if (strcmp(szCommand, "lquote") == 0) {
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					// windows-1252: 0x91, unicode 0x2018
					strOutput += 0x2018;
				} else if (strcmp(szCommand, "rquote") == 0) {
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					// windows-1252: 0x92, unicode 0x2019
					strOutput += 0x2019;
				} else if (strcmp(szCommand, "ldblquote") == 0) {
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					// windows-1252: 0x93, unicode 0x201C
					strOutput += 0x201C;
				} else if (strcmp(szCommand, "rdblquote") == 0) {
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					// windows-1252: 0x94, unicode 0x201D
					strOutput += 0x201D;
				} else if (strcmp(szCommand, "bullet") == 0) {
					strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
					// windows-1252: 0x95, unicode 0x2022
					strOutput += 0x2022;
				} else if(isRTFIgnoreCommand(szCommand)) {
					sState[ulState].bInSkipTbl = true;
				}
			}
			// Non-alnum after '\'
			else if(*szInput == '\\') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'\\');
			}
			else if(*szInput == '{') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'{');
			}
			else if(*szInput == '}') {
				++szInput;
				if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl)
					sState[ulState].output.append(1,'}');
			}
			else if(*szInput == '\'') {
				unsigned int ulChar;
				std::wstring wstrUnicode;

				if (!bPar) {
					sState[ulState].output.append("<p>");
					bPar = true;
				}
				while(*szInput == '\'')
				{
					ulChar = 0;
					++szInput;

					if(*szInput) {
						ulChar = (unsigned int) (strchr(szHex, toupper(*szInput)) == NULL ? 0 : (strchr(szHex, toupper(*szInput)) - szHex));
						ulChar = ulChar << 4;
						++szInput;
					}
					if(*szInput) {
						ulChar += (unsigned int) (strchr(szHex, toupper(*szInput)) == NULL ? 0 : (strchr(szHex, toupper(*szInput)) - szHex));
						++szInput;
					}

					if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl && !sState[ulState].ulSkipChars) {
						sState[ulState].output.append(1,ulChar);
					} else if (sState[ulState].ulSkipChars)
						--sState[ulState].ulSkipChars;

					if(*szInput == '\\' && *(szInput+1) == '\'')
						++szInput;
					else
						break;
				}
			} else {
				++szInput; // skip single character after '\'
			}
		} // Non-command
		else if(*szInput == '{') {
			// Dump output data
			strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
			++ulState;
			if (ulState >= RTF_MAXSTATE)
				return MAPI_E_NOT_ENOUGH_MEMORY;
			sState[ulState] = sState[ulState-1];
			++szInput;
		} else if(*szInput == '}') {
			// Dump output data
			strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
			if(ulState > 0)
				--ulState;
			++szInput;
		} else if (*szInput == '\r' || *szInput == '\n' || *szInput == '\0') {
			++szInput;
		} else {
			if(!sState[ulState].bInFontTbl && !sState[ulState].bRTFOnly && !sState[ulState].bInColorTbl && !sState[ulState].bInSkipTbl && !sState[ulState].ulSkipChars) {
				if (!bPar) {
					sState[ulState].output.append("<p>");
					bPar = true;
				}
				// basic html escaping only
				if (*szInput == '&')
					sState[ulState].output.append("&amp;");
				else if (*szInput == '<')
					sState[ulState].output.append("&lt;");
				else if (*szInput == '>')
					sState[ulState].output.append("&gt;");
				else
					sState[ulState].output.append(1,*szInput);
			} else if (sState[ulState].ulSkipChars)
				--sState[ulState].ulSkipChars;
			++szInput;
		}
	}

	strOutput += RTFFlushStateOutput(convertContext, sState, ulState);
	if (bPar)
		strOutput += L"</p>\r\n";
	strOutput += L"\r\n" \
		     L"</BODY>\r\n" \
		     L"</HTML>\r\n";
	try {
		lpStrHTMLOut = convertContext.convert_to<std::string>(strConvertCharset.c_str(), strOutput, rawsize(strOutput), CHARSET_WCHAR);
	} catch (const convert_exception &ce) {
		hr = HrFromException(ce);
	}
	return hr;
}

/**
 * Checks if input is HTML "wrapped" in RTF.
 *
 * We look for the words "\fromhtml" somewhere in the file.  If the
 * rtf encodes text rather than html, then instead it will only find
 * "\fromtext".
 *
 * @param[in]	buf	character buffer containing RTF text
 * @param[in]	len	length of input buffer
 * @return true if buf is html wrapped in rtf
 */
bool isrtfhtml(const char *buf, unsigned int len)
{
	if (len < 9)
		return false;
	assert(buf != nullptr);
	for (const char *c = buf; c < buf + len - 9; ++c)
		if (memcmp(c, "\\fromhtml", 9) == 0)
			return true;
	return false;
}

/**
 * Checks if input is Text "wrapped" in RTF.
 *
 * We look for the words "\fromtext" somewhere in the file.  If the
 * rtf encodes text rather than text, then instead it will only find
 * "\fromhtml".
 *
 * @param[in]	buf	character buffer containing RTF text
 * @param[in]	len	length of input buffer
 * @return true if buf is html wrapped in rtf
 */
bool isrtftext(const char *buf, unsigned int len)
{
	if (len < 9)
		return false;
	assert(buf != nullptr);
	for (const char *c = buf; c < buf + len - 9; ++c)
		if (memcmp(c, "\\fromtext", 9) == 0)
			return true;
	return false;
}

} /* namespace */
