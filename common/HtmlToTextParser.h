/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <string>
#include <map>
#include <stack>

namespace KC {

class _kc_export CHtmlToTextParser KC_FINAL {
public:
	CHtmlToTextParser(void);
	bool Parse(const wchar_t *lpwHTML);
	std::wstring& GetText();

protected:
	_kc_hidden void Init(void);
	_kc_hidden bool ll_parse(const wchar_t *);
	_kc_hidden void parseTag(const wchar_t *&);
	_kc_hidden bool parseEntity(const wchar_t *&);
	_kc_hidden void parseAttributes(const wchar_t *&);
	_kc_hidden void addChar(wchar_t);
	_kc_hidden void addNewLine(bool force_line);
	_kc_hidden bool addURLAttribute(const wchar_t *attr, bool spaces = false);
	_kc_hidden void addSpace(bool force);

	//Parse tags
	_kc_hidden void parseTagP(void);
	_kc_hidden void parseTagBP(void);
	_kc_hidden void parseTagBR(void);
	_kc_hidden void parseTagTR(void);
	_kc_hidden void parseTagBTR(void);
	_kc_hidden void parseTagTDTH(void);
	_kc_hidden void parseTagIMG(void);
	_kc_hidden void parseTagA(void);
	_kc_hidden void parseTagBA(void);
	_kc_hidden void parseTagSCRIPT(void);
	_kc_hidden void parseTagBSCRIPT(void);
	_kc_hidden void parseTagSTYLE(void);
	_kc_hidden void parseTagBSTYLE(void);
	_kc_hidden void parseTagHEAD(void);
	_kc_hidden void parseTagBHEAD(void);
	_kc_hidden void parseTagNewLine(void);
	_kc_hidden void parseTagHR(void);
	_kc_hidden void parseTagHeading(void);
	_kc_hidden void parseTagPRE(void);
	_kc_hidden void parseTagBPRE(void);
	_kc_hidden void parseTagOL(void);
	_kc_hidden void parseTagUL(void);
	_kc_hidden void parseTagLI(void);
	_kc_hidden void parseTagPopList(void);
	_kc_hidden void parseTagDL(void);
	_kc_hidden void parseTagDT(void);
	_kc_hidden void parseTagDD(void);

	std::wstring strText;
	bool fScriptMode = false;
	bool fHeadMode = false;
	short cNewlines = 0;
	bool fStyleMode = false;
	bool fTDTHMode = false;
	bool fPreMode = false;
	bool fTextMode = false;
	bool fAddSpace = false;

	typedef void ( CHtmlToTextParser::*ParseMethodType )( void );

	struct _kc_hidden tagParser {
		tagParser(void) = default;
		tagParser(bool pa, ParseMethodType mt) :
			bParseAttrs(pa), parserMethod(mt)
		{}
		bool bParseAttrs = false;
		ParseMethodType parserMethod = nullptr;
	};

	struct _TableRow {
		bool bFirstCol;
	};

	enum eListMode { lmDefinition, lmOrdered, lmUnordered };
	struct ListInfo {
		eListMode mode = lmDefinition;
		unsigned int count = 0;
	};

	typedef std::map<std::wstring, std::wstring>	MapAttrs;
	std::stack<_TableRow> stackTableRow;
	std::map<std::wstring, tagParser> tagMap;
	std::stack<MapAttrs> stackAttrs;
	ListInfo 		listInfo;
	std::stack<ListInfo> listInfoStack;
};

} /* namespace */
