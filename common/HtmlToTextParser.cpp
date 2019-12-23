/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif
#include <kopano/platform.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>
#include <cwctype>
#include <pthread.h>
#include "HtmlToTextParser.h"
#include "HtmlEntity.h"
#include <kopano/charset/convert.h>
#include <kopano/stringutil.h>
#ifdef HAVE_TIDYBUFFIO_H
#	include <tidy.h>
#	include <tidybuffio.h>
#endif

namespace KC {

#ifdef HAVE_TIDYBUFFIO_H
static std::set<std::string, std::less<>> rosie_good_tags = {
	"a", "abbr", "address", "area", "article", "aside", "b", "blockquote",
	"body", "br", "caption", "center", "cite", "code", "col", "datalist",
	"details", "div", "em", "figcaption", "figure", "font", "footer",
	"form", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr",
	"html", "i", "img", "input", "ins", "label", "legend", "li",
	"link", // FIXME css?
	"mark", "meter", "nav", "noscript", "ol", "optgroup", "output", "p",
	"pre", "q", "samp", "section", "small", "span", "strong", "style",
	"sub", "summary", "sup", "table", "tbody", "td", "textarea", "tfoot",
	"th", "thead", "time", "title", "tr", "u", "ul", "var", "wbr",
};

static std::map<std::string, std::set<std::string, std::less<>>, std::less<>> rosie_good_attrs = {
	{"a", {"class", "href", "name", "title"}},
	{"div", {"class", "id"}},
	{"font", {"size"}},
	{"form", {"id"}},
	{"img", {"alt", "border", "height", "src", "width"}},
	{"input", {"class", "id", "size", "type", "width", "for"}},
	{"label", {"for"}},
	{"li", {"class"}},
	{"li", {"id"}},
	{"link", {"href", "media", "rel", "type"}},
	{"meta", {"charset"}},
	{"table", {"cellpadding", "cellspacing", "class", "height", "width"}},
	{"td", {"align", "height", "valign", "width"}},
	{"tr", {"align", "height", "valign", "width"}},
};

static pthread_mutex_t rosie_initlock = PTHREAD_MUTEX_INITIALIZER;
static bool rosie_inited = false;
#endif

CHtmlToTextParser::CHtmlToTextParser(void)
{
	tagMap[L"head"] = tagParser(false, &CHtmlToTextParser::parseTagHEAD);
	tagMap[L"/head"] = tagParser(false, &CHtmlToTextParser::parseTagBHEAD);
	tagMap[L"style"] = tagParser(false, &CHtmlToTextParser::parseTagSTYLE);
	tagMap[L"/style"] = tagParser(false, &CHtmlToTextParser::parseTagBSTYLE);
	tagMap[L"script"] = tagParser(false, &CHtmlToTextParser::parseTagSCRIPT);
	tagMap[L"/script"] = tagParser(false, &CHtmlToTextParser::parseTagBSCRIPT);
	tagMap[L"pre"] = tagParser(false, &CHtmlToTextParser::parseTagPRE);
	tagMap[L"/pre"] = tagParser(false, &CHtmlToTextParser::parseTagBPRE);
	tagMap[L"p"] = tagParser(false, &CHtmlToTextParser::parseTagP);
	tagMap[L"/p"] = tagParser(false, &CHtmlToTextParser::parseTagBP);
	tagMap[L"a"] = tagParser(true, &CHtmlToTextParser::parseTagA);
	tagMap[L"/a"] = tagParser(false, &CHtmlToTextParser::parseTagBA);
	tagMap[L"br"] = tagParser(false, &CHtmlToTextParser::parseTagBR);
	tagMap[L"tr"] = tagParser(false, &CHtmlToTextParser::parseTagTR);
	tagMap[L"/tr"] = tagParser(false, &CHtmlToTextParser::parseTagBTR);
	tagMap[L"td"] = tagParser(false, &CHtmlToTextParser::parseTagTDTH);
	tagMap[L"th"] = tagParser(false, &CHtmlToTextParser::parseTagTDTH);
	tagMap[L"img"] = tagParser(true, &CHtmlToTextParser::parseTagIMG);
	tagMap[L"div"] = tagParser(false, &CHtmlToTextParser::parseTagNewLine);
	tagMap[L"/div"] = tagParser(false, &CHtmlToTextParser::parseTagNewLine);
	tagMap[L"hr"] = tagParser(false, &CHtmlToTextParser::parseTagHR);
	tagMap[L"h1"] = tagParser(false, &CHtmlToTextParser::parseTagHeading);
	tagMap[L"h2"] = tagParser(false, &CHtmlToTextParser::parseTagHeading);
	tagMap[L"h3"] = tagParser(false, &CHtmlToTextParser::parseTagHeading);
	tagMap[L"h4"] = tagParser(false, &CHtmlToTextParser::parseTagHeading);
	tagMap[L"h5"] = tagParser(false, &CHtmlToTextParser::parseTagHeading);
	tagMap[L"h6"] = tagParser(false, &CHtmlToTextParser::parseTagHeading);
	tagMap[L"ol"] = tagParser(false, &CHtmlToTextParser::parseTagOL);
	tagMap[L"/ol"] = tagParser(false, &CHtmlToTextParser::parseTagPopList);
	tagMap[L"ul"] = tagParser(false, &CHtmlToTextParser::parseTagUL);
	tagMap[L"/ul"] = tagParser(false, &CHtmlToTextParser::parseTagPopList);
	tagMap[L"li"] = tagParser(false, &CHtmlToTextParser::parseTagLI);
	tagMap[L"/dl"] = tagParser(false, &CHtmlToTextParser::parseTagPopList);
	tagMap[L"dt"] = tagParser(false, &CHtmlToTextParser::parseTagDT);
	tagMap[L"dd"] = tagParser(false, &CHtmlToTextParser::parseTagDD);
	tagMap[L"dl"] = tagParser(false, &CHtmlToTextParser::parseTagDL);
	// @todo check span
}

void CHtmlToTextParser::Init()
{
	fScriptMode = false;
	fHeadMode = false;
	cNewlines = 0;
	fStyleMode = false;
	fTDTHMode = false;
	fPreMode = false;
	fTextMode = false;
	fAddSpace = false;
	strText.clear();
}

bool CHtmlToTextParser::Parse(const wchar_t *lpwHTML)
{
#ifdef HAVE_TIDYBUFFIO_H
	TidyBuffer output, errbuf;
	tidyBufInit(&output);
	tidyBufInit(&errbuf);
	auto tdoc = tidyCreate();
	tidyOptSetBool(tdoc, TidyHideComments, yes);
	tidyOptSetBool(tdoc, TidyPreserveEntities, yes);
	tidySetCharEncoding(tdoc, "utf8");
	tidySetErrorBuffer(tdoc, &errbuf);
	tidyParseString(tdoc, convert_to<std::string>("UTF-8", reinterpret_cast<const char *>(lpwHTML), rawsize(lpwHTML), CHARSET_WCHAR).c_str());
	tidyCleanAndRepair(tdoc);
	tidyOptSetBool(tdoc, TidyForceOutput, yes);
	tidySaveBuffer(tdoc, &output);
	auto ret = ll_parse(convert_to<std::wstring>(reinterpret_cast<const char *>(output.bp), output.size, "UTF-8").c_str());
	tidyBufFree(&errbuf);
	tidyBufFree(&output);
	tidyRelease(tdoc);
	return ret;
#else
	return ll_parse(lpwHTML);
#endif
}

bool CHtmlToTextParser::ll_parse(const wchar_t *lpwHTML)
{
	Init();

	while(*lpwHTML != 0)
	{
		if((*lpwHTML == '\n' || *lpwHTML == '\r' || *lpwHTML == '\t') && !fPreMode) {// ignore tabs and newlines
			if(fTextMode && !fTDTHMode && !fScriptMode && !fHeadMode && !fStyleMode && (*lpwHTML == '\n' || *lpwHTML == '\r'))
				fAddSpace = true;
			else
				fAddSpace = false;
			++lpwHTML;
			continue;
		} else if(*lpwHTML == '<' && *lpwHTML+1 != ' ') { // The next char can not be a space!
			++lpwHTML;
			parseTag(lpwHTML);
			continue;
		} else if(*lpwHTML == ' ' && !fPreMode) {
			fTextMode = true;
			addSpace(false);
			++lpwHTML;
			continue;
		}
		if (fTextMode && fAddSpace)
			addSpace(false);
		fAddSpace = false;
		fTextMode = true;

		// if (skippable and not parsed)
		if (!(fScriptMode || fHeadMode || fStyleMode)) {
			if (parseEntity(lpwHTML))
				continue;
			addChar(*lpwHTML);
		}
		++lpwHTML;
	}

	return true;
}

std::wstring& CHtmlToTextParser::GetText() {
	/*
	 * Remove all trailing whitespace, but remember if there was the usual
	 * final newline (since it too counts as whitespace) and retain/restore
	 * it afterwards.
	 */
	bool lf = false;
	auto r = strText.rbegin();
	for (; r != strText.rend() && iswspace(*r); ++r)
		if (*r == L'\n')
			/* \n is sufficient — no need to test for \r too */
			lf = true;
	strText.erase(r.base(), strText.end());
	if (lf)
		strText += L"\r\n";
	return strText;
}

void CHtmlToTextParser::addNewLine(bool forceLine) {
	if (strText.empty())
		return;
	if (forceLine || cNewlines == 0)
		strText += L"\r\n";
	++cNewlines;
}

void CHtmlToTextParser::addChar(wchar_t c)
{
	if (fScriptMode || fHeadMode || fStyleMode)
		return;
	strText.push_back(c);
	cNewlines = 0;
	fTDTHMode = false;
}

void CHtmlToTextParser::addSpace(bool force) {
	if (force || (!strText.empty() && strText.back() != ' '))
		addChar(' ');
}

/**
 * @todo validate the entity!!
 */
bool CHtmlToTextParser::parseEntity(const wchar_t *&lpwHTML)
{
	std::wstring entity;

	if(*lpwHTML != '&')
		return false;
	++lpwHTML;

	if (*lpwHTML == '#') {
		int base = 10;

		++lpwHTML;
		if (*lpwHTML == 'x') {
			++lpwHTML;
			base = 16;
		}
		for (int i = 0; iswxdigit(*lpwHTML) && *lpwHTML != ';' && i < 10; ++i) {
			entity += *lpwHTML;
			++lpwHTML;
		}
		strText.push_back(wcstoul(entity.c_str(), nullptr, base));
	} else {
		for (int i = 0; *lpwHTML != ';' && *lpwHTML != 0 && i < 10; ++i) {
			entity += *lpwHTML;
			++lpwHTML;
		}
		auto code = CHtmlEntity::toChar(entity.c_str());
		if (code > 0)
			strText.push_back(code);
	}

	if(*lpwHTML == ';')
		++lpwHTML;
	return true;
}

void CHtmlToTextParser::parseTag(const wchar_t *&lpwHTML)
{
	bool bTagName = true, bTagEnd = false, bParseAttrs = false;
	decltype(tagMap)::const_iterator iterTag;
	std::wstring tagName;

	while (*lpwHTML != 0 && !bTagEnd)
	{
		if (bTagName && *lpwHTML == '!') {
			// HTML comment or doctype detect, ignore all the text
			bool fCommentMode = false;
			++lpwHTML;

			if (*lpwHTML == '-' && *(lpwHTML+1) == '-') {
				fCommentMode = true;
				lpwHTML += 2; // Skip over the initial "<!--"
			}

			while (*lpwHTML != 0) {
				if (*lpwHTML != '>') {
					++lpwHTML;
					continue;
				}
				if (!fCommentMode) {
					++lpwHTML; // all others end on the first >
					return;
				}
				if (*(lpwHTML-1) == '-' && *(lpwHTML-2) == '-' ) {
					++lpwHTML; // comment ends with -->
					return;
				}
				++lpwHTML;
			}
		} else if (*lpwHTML == '>') {
			if(!bTagEnd){
				iterTag = tagMap.find(tagName);
				bTagEnd = true;
				bTagName = false;
			}
		} else if (*lpwHTML == '<') {
			return; // Possible broken HTML, ignore data before
		} else if (bTagName) {
			if (*lpwHTML == ' ') {
				bTagName = false;
				iterTag = tagMap.find(tagName);
				if (iterTag != tagMap.cend())
					bParseAttrs = iterTag->second.bParseAttrs;
			} else {
				tagName.push_back(towlower(*lpwHTML));
			}
		} else if (bParseAttrs) {
			parseAttributes(lpwHTML);
			break;
		}

		++lpwHTML;
	}

	// Parse tag
	if (!bTagName && iterTag != tagMap.cend()) {
		(this->*iterTag->second.parserMethod)();
		fTextMode = false;
	}
}

void CHtmlToTextParser::parseAttributes(const wchar_t *&lpwHTML)
{
	std::wstring attrName, attrValue;
	bool bAttrName = true, bAttrValue = false, bEndTag = false;
	MapAttrs mapAttrs;
	wchar_t firstQuote = 0;

	while(*lpwHTML != 0 && !bEndTag) {
		if(*lpwHTML == '>' && bAttrValue) {
				bAttrValue = false;
				bEndTag = true;
		} else if(*lpwHTML == '>' && bAttrName) {
			++lpwHTML;
			break; // No attributes or broken attribute detect
		} else if(*lpwHTML == '=' && bAttrName) {
			bAttrName = false;
			bAttrValue = true;
		} else if(*lpwHTML == ' ' && bAttrValue && firstQuote == 0) {
			if (!attrValue.empty())
				bAttrValue = false;
			// ignore space
		} else if (bAttrValue) {
			if(*lpwHTML == '\'' || *lpwHTML == '\"') {
				if (firstQuote == 0) {
					firstQuote = *lpwHTML++;
					continue; // Don't add the quote!
				} else if (firstQuote == *lpwHTML) {
					bAttrValue = false;
				}
			}
			if(bAttrValue)
				attrValue.push_back(*lpwHTML);
		} else if (bAttrName) {
			attrName.push_back(towlower(*lpwHTML));
		}

		if(!bAttrName && !bAttrValue) {
			mapAttrs[std::move(attrName)] = std::move(attrValue);
			firstQuote = 0;
			bAttrName = true;
			bAttrValue = false;
			attrValue.clear();
			attrName.clear();
		}
		++lpwHTML;
	}
	stackAttrs.push(std::move(mapAttrs));
}

void CHtmlToTextParser::parseTagP()
{
	if (cNewlines < 2 && !fTDTHMode) {
		addNewLine( false );
		addNewLine( true );
	}
}

void CHtmlToTextParser::parseTagBP() {
	addNewLine( false );
	addNewLine( true );
}

void CHtmlToTextParser::parseTagBR()
{
	addNewLine( true );
}

void CHtmlToTextParser::parseTagTR()
{
	TableRow t;
	t.bFirstCol = true;
	addNewLine( false );
	stackTableRow.push(t);
}

void CHtmlToTextParser::parseTagBTR()
{
	if(!stackTableRow.empty())
		stackTableRow.pop();
}

void CHtmlToTextParser::parseTagTDTH()
{
	if (!stackTableRow.empty() && stackTableRow.top().bFirstCol)
		 stackTableRow.top().bFirstCol = false;
	else
		addChar('\t');
	fTDTHMode = true;
}

void CHtmlToTextParser::parseTagIMG()
{
	if (addURLAttribute(L"src", true)) {
		cNewlines = 0;
		fTDTHMode = false;
	}
	if (!stackAttrs.empty())
		stackAttrs.pop();
}

void CHtmlToTextParser::parseTagA() {
	// nothing todo, only because we want to parse the tag A attributes
}

void CHtmlToTextParser::parseTagBA()
{
	if (addURLAttribute(L"href")) {
		cNewlines = 0;
		fTDTHMode = false;
	}
	if(!stackAttrs.empty())
		stackAttrs.pop();
}

bool CHtmlToTextParser::addURLAttribute(const wchar_t *lpattr, bool bSpaces)
{
	if (stackAttrs.empty())
		return false;
	auto iter = stackAttrs.top().find(lpattr);
	if (iter == stackAttrs.top().cend())
		return false;
	if (wcsncasecmp(iter->second.c_str(), L"http:", 5) != 0 &&
	    wcsncasecmp(iter->second.c_str(), L"ftp:", 4) != 0 &&
	    wcsncasecmp(iter->second.c_str(), L"mailto:", 7) != 0)
		return false;
	addSpace(false);
	strText.append(L"<");
	strText.append(iter->second);
	strText.append(L">");
	addSpace(false);
	return true;
}

void CHtmlToTextParser::parseTagSCRIPT() {
	fScriptMode = true;
}

void CHtmlToTextParser::parseTagBSCRIPT() {
	fScriptMode = false;
}

void CHtmlToTextParser::parseTagSTYLE() {
	fStyleMode = true;
}

void CHtmlToTextParser::parseTagBSTYLE() {
	fStyleMode = false;
}

void CHtmlToTextParser::parseTagHEAD() {
	fHeadMode = true;
}

void CHtmlToTextParser::parseTagBHEAD() {
	fHeadMode = false;
}

void CHtmlToTextParser::parseTagNewLine() {
	addNewLine( false );
}

void CHtmlToTextParser::parseTagHR() {
	addNewLine( false );
	strText.append(L"--------------------------------");
	addNewLine( true );
}
void CHtmlToTextParser::parseTagHeading() {
	addNewLine( false );
	addNewLine( true );
}

void CHtmlToTextParser::parseTagPopList() {
	if (!listInfoStack.empty())
		listInfoStack.pop();
	addNewLine( false );
}

void CHtmlToTextParser::parseTagOL() {
	listInfo.mode = lmOrdered;
	listInfo.count = 1;
	listInfoStack.push(listInfo);
}

void CHtmlToTextParser::parseTagUL() {
	listInfo.mode = lmUnordered;
	listInfo.count = 1;
	listInfoStack.push(listInfo);
}

static std::wstring inttostring(unsigned int x) {
	wchar_t buf[33];
	swprintf(buf, 33, L"%u", x);
	return buf;
}

void CHtmlToTextParser::parseTagLI() {
	addNewLine( false );
	if (listInfoStack.empty())
		return;
	for (size_t i = 0; i < listInfoStack.size() - 1; ++i)
		strText.append(L"\t");
	if (listInfoStack.top().mode == lmOrdered)
		strText += inttostring(listInfoStack.top().count++) + L".";
	else
		strText.append(L"*");
	strText.append(L"\t");
	cNewlines = 0;
	fTDTHMode = false;
}

void CHtmlToTextParser::parseTagDT() {
	addNewLine( false );
	if (listInfoStack.empty())
		return;
	for (size_t i = 0; i < listInfoStack.size() - 1; ++i)
		strText.append(L"\t");
}

void CHtmlToTextParser::parseTagDD() {
	addNewLine( false );
	if (listInfoStack.empty())
		return;
	for (size_t i = 0; i < listInfoStack.size(); ++i)
		strText.append(L"\t");
}

void CHtmlToTextParser::parseTagDL() {
	listInfo.mode = lmDefinition;
	listInfo.count = 1;
	listInfoStack.push(listInfo);
}

void CHtmlToTextParser::parseTagPRE() {
	fPreMode = true;
    addNewLine( false );
    addNewLine( true );
}

void CHtmlToTextParser::parseTagBPRE() {
	fPreMode = false;
	addNewLine( false );
	addNewLine( true );
}

#ifdef HAVE_TIDYBUFFIO_H
static void rosie_init(void)
{
	for (const auto &p : rosie_good_attrs) {
		auto it = rosie_good_attrs.find(p.first);
		if (it != rosie_good_attrs.end())
			continue;
		rosie_good_tags.emplace(p.first);
	}
}

static bool rosie_reject_tag(const char *const tag)
{
	bool r = rosie_good_tags.find(tag) == rosie_good_tags.end();
#ifdef _DEBUG
	if (r)
		fprintf(stderr, "%s ", tag);
#endif
	return r;
}

static bool rosie_reject_attr(const char *tag, const char *const attr)
{
	auto it = rosie_good_attrs.find(tag);
	if (it == rosie_good_attrs.end())
		return true;

	bool r = it->second.find(attr) == it->second.end();
#ifdef _DEBUG
	if (r)
		fprintf(stderr, "%s(%s) ", tag, attr);
#endif
	return r;
}

static void rosie_strip_attrs(TidyDoc tdoc, TidyNode tnod)
{
	ctmbstr tname = tidyNodeGetName(tnod);

	for (TidyAttr attribute = tidyAttrFirst(tnod); attribute != NULL; ) {
		ctmbstr aname = tidyAttrName(attribute);

		if (aname != NULL && rosie_reject_attr(tname, aname)) {
			TidyAttr next = tidyAttrNext(attribute);
			tidyAttrDiscard(tdoc, tnod, attribute);
			attribute = next;
		} else {
			attribute = tidyAttrNext(attribute);
		}
	}
}

static bool rosie_strip_nodes(TidyDoc tdoc)
{
	TidyNode next_node = nullptr, curr_node = tidyGetHtml(tdoc);
	size_t nesting = 0;

	while (curr_node != nullptr) {
		ctmbstr name = tidyNodeGetName(curr_node);
		auto parent = tidyGetParent(curr_node);

		if (name != NULL && rosie_reject_tag(name)) {
			next_node = tidyDiscardElement(tdoc, curr_node);
			curr_node = nullptr;
		} else {
			rosie_strip_attrs(tdoc, curr_node);
			next_node = tidyGetChild(curr_node);
			if (next_node != nullptr)
				++nesting;
			else
				next_node = tidyGetNext(curr_node);
		}
		if (next_node != nullptr) {
			curr_node = next_node;
			continue;
		}

		while (next_node == nullptr) {
			next_node = parent;
			if (next_node == nullptr)
				break;
			--nesting;
			curr_node = next_node;
			next_node = tidyGetNext(curr_node);
			parent    = tidyGetParent(curr_node);
		}
		curr_node = next_node;
		if (nesting == 0)
			break;
	}
	return true;
}

/*
 * @in:		Input HTML, which must be encoded as UTF-8. (<meta> is ignored.)
 * @output:	Output HTML, also UTF-8.
 */
bool rosie_clean_html(const std::string &in, std::string *const out,
    std::vector<std::string> *const errors)
{
	pthread_mutex_lock(&rosie_initlock);
	if (!rosie_inited) {
		rosie_inited = true;
		rosie_init();
	}
	pthread_mutex_unlock(&rosie_initlock);

	TidyBuffer output;
	TidyBuffer errbuf;
	int rc = -1;

	out->clear();
	tidyBufInit(&output);
	tidyBufInit(&errbuf);

	TidyDoc tdoc = tidyCreate();

	tidyOptSetBool(tdoc, TidyHideComments, yes); /* they don't help */
	tidyOptSetBool(tdoc, TidyReplaceColor, yes);
	tidyOptSetBool(tdoc, TidyPreserveEntities, yes);
	tidySetCharEncoding(tdoc, "utf8");

	rc = tidySetErrorBuffer(tdoc, &errbuf); /* capture diagnostics */
	if (rc != 0 && errors != NULL)
		errors->emplace_back(format("tidySetErrorBuffer(%d) ", rc));

	if (rc >= 0)
		rc = tidyParseString(tdoc, in.c_str());
	if (rc != 0 && errors != NULL)
		errors->emplace_back(format("tidyParseString(%d) ", rc));

	if (rc >= 0)
		rc = tidyCleanAndRepair(tdoc);
	if (rc != 0 && errors != NULL)
		errors->emplace_back(format("tidyCleanAndRepair(%d) ", rc));

	if (rc >= 0)
		rc = rosie_strip_nodes(tdoc) ? 0 : -1;
	if (rc != 0 && errors != NULL)
		errors->emplace_back(format("RemoveBadHtml(%d) ", rc));

	if (rc >= 0)
		rc = tidyRunDiagnostics(tdoc); /* kvetch */
	if (rc != 0 && errors != NULL)
		errors->emplace_back(format("tidyRunDiagnostics(%d) ", rc));

	tidyOptSetBool(tdoc, TidyForceOutput, yes);

	if (rc >= 0)
		rc = tidySaveBuffer(tdoc, &output); /* pretty print */
	if (rc != 0 && errors != NULL)
		errors->emplace_back(format("tidySaveBuffer(%d) ", rc));

	out->assign(reinterpret_cast<const char *>(output.bp));
	if (rc == 0 || rc == 1) {
		/* rc==1: warnings emitted */
		if (rc == 1 && errors != nullptr)
			errors->emplace_back(format("%s: libtidy warning: %s",
				__PRETTY_FUNCTION__,
				reinterpret_cast<const char *>(errbuf.bp)));
	} else if (errors != nullptr) {
		errors->emplace_back(format("%s: libtidy failed: %s",
			__PRETTY_FUNCTION__,
			reinterpret_cast<const char *>(errbuf.bp)));
	}

	if (rc >= 0)
		tidyBufFree(&output);

	tidyBufFree(&errbuf);
	tidyRelease(tdoc);
	return rc >= 0;
}
#endif /* HAVE_TIDYBUFFIO_H */

} /* namespace */
