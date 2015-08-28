#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <map>
#include <set>
#include <string>
#include <vector>
#include <stdio.h>
#include <tidy.h>
#include <tidybuffio.h>

std::set<std::string> htmlTagsWhitelists;
std::map<std::string, std::set<std::string> > htmlAttributesWhitelists;

pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
bool initted = false;

static void InitLibRosie()
{
	static const char *const tags[] = {
		"a",
		"abbr",
		"address",
		"area",
		"article",
		"aside",
		"b",
		"blockquote",
		"body",
		"br",
		"caption",
		"center",
		"cite",
		"code",
		"col",
		"datalist",
		"details",
		"div",
		"em",
		"figcaption",
		"figure",
		"font",
		"footer",
		"form",
		"h1",
		"h2",
		"h3",
		"h4",
		"h5",
		"h6",
		"head",
		"header",
		"hr",
		"html",
		"i",
		"img",
		"input",
		"ins",
		"label",
		"legend",
		"li",
		"link", // FIXME css?
		"mark",
		"meter",
		"nav",
		"noscript",
		"ol",
		"optgroup",
		"output",
		"p",
		"pre",
		"q",
		"samp",
		"section",
		"small",
		"span",
		"strong",
		"style",
		"sub",
		"summary",
		"sup",
		"table",
		"tbody",
		"td",
		"textarea",
		"tfoot",
		"th",
		"thead",
		"time",
		"title",
		"tr",
		"u",
		"ul",
		"var",
		"wbr",
		NULL,
	};

	for (int i = 0; tags[i] != NULL; ++i)
		htmlTagsWhitelists.insert(tags[i]);

	typedef struct {
		const char *tag, *attribute;
	} tag_pair_t;

	static const tag_pair_t attributes[] = {
		{"a", "class"},
		{"a", "href"},
		{"a", "name"},
		{"a", "title"},
		{"div", "class"},
		{"div", "id"},
		{"font", "size"},
		{"form", "id"},
		{"img", "alt"},
		{"img", "border"},
		{"img", "height"},
		{"img", "src"},
		{"img", "width"},
		{"input", "class"},
		{"input", "id"},
		{"input", "size"},
		{"input", "type"},
		{"input", "width"},
		{"label", "for"},
		{"li", "class"},
		{"li", "id"},
		{"link", "href"},
		{"link", "media"},
		{"link", "rel"},
		{"link", "type"},
		{"meta", "charset"},
		{"table", "cellpadding"},
		{"table", "cellspacing"},
		{"table", "class"},
		{"table", "height"},
		{"table", "width"},
		{"td", "align"},
		{"td", "height"},
		{"td", "valign"},
		{"td", "width"},
		{"tr", "align"},
		{"tr", "height"},
		{"tr", "valign"},
		{"tr", "width"},
		{NULL},
	};

	for (int i = 0; attributes[i].tag != NULL; ++i) {
		std::map<std::string, std::set<std::string> >::iterator it = htmlAttributesWhitelists.find(attributes[i].tag);

		if (it != htmlAttributesWhitelists.end()) {
			it->second.insert(attributes[i].attribute);
		} else {
			std::set<std::string> attrs;
			attrs.insert(attributes[i].attribute);

			htmlAttributesWhitelists.insert(std::pair<std::string, std::set<std::string> >(attributes[i].tag, attrs));
		}
	}
}

static std::string format(const char *const fmt, ...)
{
        char *buffer = NULL;

	std::string result;

        va_list ap;
        va_start(ap, fmt);
        if (vasprintf(&buffer, fmt, ap) >= 0)
		result = buffer;
        va_end(ap);

	free(buffer);

        return result;
}

static bool IsInHtmlTagWhitelist(const char *const tag)
{
#ifdef _DEBUG
	if (htmlTagsWhitelists.find(tag) == htmlTagsWhitelists.end())
		fprintf(stderr, "%s ", tag);
#endif

	return htmlTagsWhitelists.find(tag) != htmlTagsWhitelists.end();
}

static bool IsInHtmlAttributeWhitelist(const char *tag, const char *const attr)
{
	std::map<std::string, std::set<std::string> >::iterator it = htmlAttributesWhitelists.find(tag);

	if (it == htmlAttributesWhitelists.end())
		return false;
#ifdef _DEBUG
	if (it->second.find(attr) == it->second.end())
		fprintf(stderr, "%s(%s) ", tag, attr);
#endif

	return it->second.find(attr) != it->second.end();
}

static void CleanAttributes(TidyDoc tdoc, TidyNode tnod)
{
	ctmbstr tname = tidyNodeGetName(tnod);

	for (TidyAttr attribute = tidyAttrFirst(tnod); attribute != NULL; ) {
		ctmbstr aname = tidyAttrName(attribute);

		if (aname != NULL &&
		    !IsInHtmlAttributeWhitelist(tname, aname)) {
			TidyAttr next = tidyAttrNext(attribute);
			tidyAttrDiscard(tdoc, tnod, attribute);
			attribute = next;
		} else {
			attribute = tidyAttrNext(attribute);
		}
	}
}

static bool BadHTMLCleaner(TidyDoc tdoc, TidyNode tnod)
{
	for (TidyNode child = tidyGetChild(tnod); child != NULL; ) {
		ctmbstr name = tidyNodeGetName(child);

		if (name != NULL && !IsInHtmlTagWhitelist(name)) {
			child = tidyDiscardElement(tdoc, child);
		} else {
			CleanAttributes(tdoc, tnod);

			BadHTMLCleaner(tdoc, child);
			child = tidyGetNext(child);
		}
	}

	return true;
}

static bool RemoveBadHtml(TidyDoc tdoc)
{
	return BadHTMLCleaner(tdoc, tidyGetRoot(tdoc));
}

bool CleanHtml(const std::string &in, std::string *const out,
    std::vector<std::string> *const errors)
{
	pthread_mutex_lock(&init_lock);
	if (!initted) {
		initted = true;
		InitLibRosie();
	}
	pthread_mutex_unlock(&init_lock);

	TidyBuffer output = {0};
	TidyBuffer errbuf = {0};
	int rc = -1;

	out->clear();

	TidyDoc tdoc = tidyCreate();

	tidyOptSetBool(tdoc, TidyHideComments, yes); /* they don't help */
	tidyOptSetBool(tdoc, TidyReplaceColor, yes);
	tidyOptSetBool(tdoc, TidyPreserveEntities, yes);

	rc = tidySetErrorBuffer(tdoc, &errbuf); /* capture diagnostics */
	if (rc != 0 && errors != NULL)
		errors->push_back(format("tidySetErrorBuffer(%d) ", rc));

	if (rc >= 0)
		rc = tidyParseString(tdoc, in.c_str());
	if (rc != 0 && errors != NULL)
		errors->push_back(format("tidyParseString(%d) ", rc));

	if (rc >= 0)
		rc = tidyCleanAndRepair(tdoc);
	if (rc != 0 && errors != NULL)
		errors->push_back(format("tidyCleanAndRepair(%d) ", rc));

	if (rc >= 0)
		rc = RemoveBadHtml(tdoc) ? 0 : -1;
	if (rc != 0 && errors != NULL)
		errors->push_back(format("RemoveBadHtml(%d) ", rc));

	if (rc >= 0)
		rc = tidyRunDiagnostics(tdoc); /* kvetch */
	if (rc != 0 && errors != NULL)
		errors->push_back(format("tidyRunDiagnostics(%d) ", rc));

	tidyOptSetBool(tdoc, TidyForceOutput, yes);

	if (rc >= 0)
		rc = tidySaveBuffer(tdoc, &output); /* pretty print */
	if (rc != 0 && errors != NULL)
		errors->push_back(format("tidySaveBuffer(%d) ", rc));

	if (rc == 0 || rc == 1) {
		/* rc==1: warnings emitted */
		out->assign((const char *)output.bp);

		if (rc == 1)
			errors->push_back(format("CleanHtml(): libtidy warning: %s", (const char *)errbuf.bp));
	}
	else {
		errors->push_back(format("CleanHtml(): libtidy failed: %s", (const char *)errbuf.bp));
	}

	if (rc >= 0)
		tidyBufFree(&output);

	tidyBufFree(&errbuf);
	tidyRelease(tdoc);
	return rc >= 0;
}
