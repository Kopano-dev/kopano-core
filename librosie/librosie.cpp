#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdio>
#include <pthread.h>
#include <tidy.h>
#include <tidybuffio.h>
#include <kopano/platform.h>
#include "librosie.h"

typedef std::map<std::string, std::set<std::string> > attr_map_type;

static std::set<std::string> rosie_good_tags;
static attr_map_type rosie_good_attrs;

static pthread_mutex_t rosie_initlock = PTHREAD_MUTEX_INITIALIZER;
static bool rosie_inited = false;

static void rosie_init(void)
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
	};

	for (size_t i = 0; i < ARRAY_SIZE(tags); ++i)
		rosie_good_tags.insert(tags[i]);

	struct tag_pair {
		const char *tag, *attribute;
	};

	static const struct tag_pair attributes[] = {
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
	};

	for (size_t i = 0; i < ARRAY_SIZE(attributes); ++i) {
		attr_map_type::iterator it = rosie_good_attrs.find(attributes[i].tag);

		if (it != rosie_good_attrs.end()) {
			it->second.insert(attributes[i].attribute);
		} else {
			std::set<std::string> attrs;
			attrs.insert(attributes[i].attribute);

			rosie_good_attrs.insert(std::pair<std::string, std::set<std::string> >(attributes[i].tag, attrs));
		}
	}
}

/* Same as common/stringutil.cpp, consider merging. */
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
	attr_map_type::iterator it = rosie_good_attrs.find(tag);

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

static bool rosie_strip_nodes(TidyDoc tdoc, TidyNode tnod)
{
	for (TidyNode child = tidyGetChild(tnod); child != NULL; ) {
		ctmbstr name = tidyNodeGetName(child);

		if (name != NULL && rosie_reject_tag(name)) {
			child = tidyDiscardElement(tdoc, child);
		} else {
			rosie_strip_attrs(tdoc, tnod);
			rosie_strip_nodes(tdoc, child);
			child = tidyGetNext(child);
		}
	}

	return true;
}

static bool rosie_strip_nodes(TidyDoc tdoc)
{
	return rosie_strip_nodes(tdoc, tidyGetRoot(tdoc));
}

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
		rc = rosie_strip_nodes(tdoc) ? 0 : -1;
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
		out->assign(reinterpret_cast<const char *>(output.bp));

		if (rc == 1)
			errors->push_back(format("%s: libtidy warning: %s",
				__PRETTY_FUNCTION__,
				reinterpret_cast<const char *>(errbuf.bp)));
	}
	else {
		errors->push_back(format("%s: libtidy failed: %s",
			__PRETTY_FUNCTION__,
			reinterpret_cast<const char *>(errbuf.bp)));
	}

	if (rc >= 0)
		tidyBufFree(&output);

	tidyBufFree(&errbuf);
	tidyRelease(tdoc);
	return rc >= 0;
}
