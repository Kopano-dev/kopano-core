/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <map>
#include <stack>
#include <string>
#include <vector>

namespace KC {

class KC_EXPORT CHtmlToTextParser KC_FINAL {
public:
	CHtmlToTextParser(void);
	bool Parse(const wchar_t *lpwHTML);
	std::wstring& GetText();

protected:
	KC_HIDDEN void Init();
	KC_HIDDEN bool ll_parse(const wchar_t *);
	KC_HIDDEN void parseTag(const wchar_t *&);
	KC_HIDDEN bool parseEntity(const wchar_t *&);
	KC_HIDDEN void parseAttributes(const wchar_t *&);
	KC_HIDDEN void addChar(wchar_t);
	KC_HIDDEN void addNewLine(bool force_line);
	KC_HIDDEN bool addURLAttribute(const wchar_t *attr, bool spaces = false);
	KC_HIDDEN void addSpace(bool force);

	//Parse tags
	KC_HIDDEN void parseTagP();
	KC_HIDDEN void parseTagBP();
	KC_HIDDEN void parseTagBR();
	KC_HIDDEN void parseTagTR();
	KC_HIDDEN void parseTagBTR();
	KC_HIDDEN void parseTagTDTH();
	KC_HIDDEN void parseTagIMG();
	KC_HIDDEN void parseTagA();
	KC_HIDDEN void parseTagBA();
	KC_HIDDEN void parseTagSCRIPT();
	KC_HIDDEN void parseTagBSCRIPT();
	KC_HIDDEN void parseTagSTYLE();
	KC_HIDDEN void parseTagBSTYLE();
	KC_HIDDEN void parseTagHEAD();
	KC_HIDDEN void parseTagBHEAD();
	KC_HIDDEN void parseTagNewLine();
	KC_HIDDEN void parseTagHR();
	KC_HIDDEN void parseTagHeading();
	KC_HIDDEN void parseTagPRE();
	KC_HIDDEN void parseTagBPRE();
	KC_HIDDEN void parseTagOL();
	KC_HIDDEN void parseTagUL();
	KC_HIDDEN void parseTagLI();
	KC_HIDDEN void parseTagPopList();
	KC_HIDDEN void parseTagDL();
	KC_HIDDEN void parseTagDT();
	KC_HIDDEN void parseTagDD();

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

	struct KC_HIDDEN tagParser {
		tagParser(void) = default;
		tagParser(bool pa, ParseMethodType mt) :
			bParseAttrs(pa), parserMethod(mt)
		{}
		bool bParseAttrs = false;
		ParseMethodType parserMethod = nullptr;
	};

	struct TableRow {
		bool bFirstCol;
	};

	enum eListMode { lmDefinition, lmOrdered, lmUnordered };
	struct ListInfo {
		eListMode mode = lmDefinition;
		unsigned int count = 0;
	};

	typedef std::map<std::wstring, std::wstring>	MapAttrs;
	std::stack<TableRow> stackTableRow;
	std::map<std::wstring, tagParser> tagMap;
	std::stack<MapAttrs> stackAttrs;
	ListInfo 		listInfo;
	std::stack<ListInfo> listInfoStack;
};

extern KC_EXPORT bool rosie_clean_html(const std::string &in, std::string *out, std::vector<std::string> *err);

} /* namespace */
