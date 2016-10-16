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

#pragma once

#include <kopano/zcdefs.h>
#include <string>
#include <map>
#include <stack>

class CHtmlToTextParser _kc_final {
public:
	CHtmlToTextParser(void);
	bool Parse(const WCHAR *lpwHTML);
	std::wstring& GetText();

protected:
	void Init();

	void parseTag(const WCHAR* &lpwHTML);
	bool parseEntity(const WCHAR* &lpwHTML);
	void parseAttributes(const WCHAR* &lpwHTML);

	void addChar(WCHAR c);
	void addNewLine(bool forceLine);
	bool addURLAttribute(const WCHAR *lpattr, bool bSpaces = false);
	void addSpace(bool force);

	//Parse tags
	void parseTagP();
	void parseTagBP();
	void parseTagBR();
	void parseTagTR();
	void parseTagBTR();
	void parseTagTDTH();
	void parseTagIMG();
	void parseTagA();
	void parseTagBA();
	void parseTagSCRIPT();
	void parseTagBSCRIPT();
	void parseTagSTYLE();
	void parseTagBSTYLE();
	void parseTagHEAD();
	void parseTagBHEAD();
	void parseTagNewLine();
	void parseTagHR();
	void parseTagHeading();
	void parseTagPRE();
	void parseTagBPRE();
	void parseTagOL();
	void parseTagUL();
	void parseTagLI();
	void parseTagPopList();
	void parseTagDL();
	void parseTagDT();
	void parseTagDD();

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

	struct tagParser {
		tagParser(){};
		tagParser(bool bParseAttrs, ParseMethodType parserMethod){
			this->bParseAttrs = bParseAttrs;
			this->parserMethod = parserMethod;
		};
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

	typedef std::map<std::wstring, tagParser>		MapParser;
	typedef std::map<std::wstring, std::wstring>	MapAttrs;

	typedef std::stack<MapAttrs>	StackMapAttrs;
	typedef std::stack<_TableRow>	StackTableRow;
	typedef std::stack<ListInfo>	ListInfoStack;
	
	StackTableRow	stackTableRow;
	MapParser		tagMap;
	StackMapAttrs	stackAttrs;
	ListInfo 		listInfo;
	ListInfoStack	listInfoStack;
};
