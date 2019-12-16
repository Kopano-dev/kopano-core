/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2016 Zarafa and its licensors
 */
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
#include <kopano/stringutil.h>
#include "librosie.h"

namespace KC {

static std::set<std::string> rosie_good_tags = {
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

static std::map<std::string, std::set<std::string> > rosie_good_attrs = {
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
	return rosie_strip_nodes(tdoc, tidyGetHtml(tdoc));
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

} /* namespace */
