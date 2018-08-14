/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2016, Kopano and its licensors */
/*
 *	Test routine for RFC5322 input message character set recognition,
 *	derivation and transformation.
 */
#define _GNU_SOURCE 1 /* memmem */
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <inetmapi/inetmapi.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <edkmdb.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/CommonUtil.h>
#include <kopano/automapi.hpp>
#include <kopano/codepage.h>
#include <kopano/hl.hpp>
#include <inetmapi/options.h>
#include "tbi.hpp"

using namespace KC;

enum {
	TEST_OK = 0,
	TEST_FAIL = 1
};

static const char *t_ascii_upgrade = "utf-8";
ECLogger_File t_logger(EC_LOGLEVEL_DEBUG, false, "-", 0);

class ictx _kc_final {
	public:
	void complete_init(void);
	const SPropValue *find(unsigned int) const;

	memory_ptr<SPropValue> props;
	ULONG count;
	const char *file;

	unsigned int cpid;
	const char *codepage;
	wchar_t *data;
	IMessage *imsg;
};

class t_base {
	protected:
	int (*m_analyze)(const struct ictx &);

	public:
	delivery_options m_dopt;

	t_base(int (*func)(const struct ictx &) = NULL)
	{
		imopt_default_delivery_options(&m_dopt);
		m_dopt.ascii_upgrade = t_ascii_upgrade;
		m_analyze = func;
	}
	virtual void setup(void) {};
	virtual int verify(const struct ictx &ctx)
	{
		return (m_analyze != NULL) ? (*m_analyze)(ctx) : TEST_OK;
	}
};

const SPropValue *ictx::find(unsigned int tag) const
{
	return PCpropFindProp(props, count, tag);
}

void ictx::complete_init(void)
{
	auto prop = find(PR_INTERNET_CPID);
	cpid = (prop != NULL) ? prop->Value.ul : 0;
	if (HrGetCharsetByCP(cpid, &codepage) != hrSuccess)
		codepage = "<unknown>";
	prop = find(PR_BODY_W);
	data = (prop != NULL) ? prop->Value.lpszW : NULL;
}

static int slurp_file(const char *file, std::string &msg)
{
	std::ifstream fp(file);
	if (fp.fail()) {
		fprintf(stderr, "Failed to open %s: %s\n", file, strerror(errno));
		return -errno;
	}
	fp.seekg(0, std::ios::end);
	if (fp.good()) {
		long res = fp.tellg();
		if (res > 0)
			msg.reserve(res);
		fp.seekg(0, std::ios::beg);
	}
	msg.assign(std::istreambuf_iterator<char>(fp),
	           std::istreambuf_iterator<char>());
	return 1;
}

/**
 * pfile - read and process a file
 * @file:	path to file containing RFC 5322 message
 * @analyze:	a function to analyze the MAPI message resulting from
 * 		VMIMEToMAPI conversion
 */
static int dofile(const char *file, t_base *cls)
{
	std::string rfcmsg;
	auto ret = slurp_file(file, rfcmsg);
	if (ret <= 0)
		return TEST_FAIL;

	struct ictx ictx;
	HRESULT hr = hrSuccess;

	/*
	 * Sucky libinetmapi requires that we have a session open
	 * just to test VMIMEToMAPI's functions :-(
	 */
	ictx.props = NULL;
	cls->setup();
	ec_log_get()->SetLoglevel(EC_LOGLEVEL_DEBUG);
	auto imsg = KSession().open_default_store().open_root(MAPI_MODIFY).create_message();
	hr = IMToMAPI(NULL, NULL, NULL, imsg, rfcmsg, cls->m_dopt);
	if (hr != hrSuccess) {
		fprintf(stderr, "IMToMAPI: %s\n", GetMAPIErrorMessage(hr));
		return TEST_FAIL;
	}
	hr = HrGetAllProps(imsg, MAPI_UNICODE, &ictx.count, &~ictx.props);
	if (hr == MAPI_W_ERRORS_RETURNED) {
	} else if (hr != hrSuccess) {
		fprintf(stderr, "GetAllProps: %s\n", GetMAPIErrorMessage(hr));
		return TEST_FAIL;
	}
	ictx.file = file;
	ictx.imsg = imsg;
	ictx.complete_init();
	return cls->verify(ictx);
}

static size_t chkfile(const std::string &fs, int (*analyze)(const struct ictx &))
{
	auto file = fs.c_str();
	t_base cls(analyze);
	fprintf(stderr, "=== %s ===\n", file);
	int ret = dofile(file, &cls);
	if (ret != TEST_OK) {
		fprintf(stderr, "FAILED: %s\n\n", file);
		return ret;
	}
	fprintf(stderr, "\n");
	return 0;
}

static size_t chkfile(const std::string &fs, t_base &&cls)
{
	auto file = fs.c_str();
	fprintf(stderr, "=== %s ===\n", file);
	int ret = dofile(file, &cls);
	if (ret != TEST_OK) {
		fprintf(stderr, "FAILED: %s\n\n", file);
		return ret;
	}
	fprintf(stderr, "\n");
	return 0;
}

/*
 * Important properties one may want to inspect: PR_SUBJECT_W,
 * (PR_SUBJECT_PREFIX_W), PR_BODY_W, PR_BODY_HTML, PR_INTERNET_CPID,
 * (PR_RTF_COMPRESSED)
 */

static int test_mimecset01(const struct ictx &ctx)
{
	return (strcmp(ctx.codepage, "utf-8") == 0 &&
	        wcscmp(ctx.data, L"t\xE6st") == 0) ? TEST_OK : TEST_FAIL;
}

class test_mimecset03 _kc_final : public t_base {
	public:
	void setup(void) { m_dopt.ascii_upgrade = nullptr; }
	int verify(const struct ictx &ctx);
};

int test_mimecset03::verify(const struct ictx &ctx)
{
	return strcmp(ctx.codepage, "us-ascii") == 0 ? TEST_OK : TEST_FAIL;
}

static int test_encword_split(const struct ictx &ctx)
{
	const SPropValue *prop = ctx.find(PR_SUBJECT_W);
	if (prop == NULL)
		return TEST_FAIL;
	static const wchar_t exp[] =
		{0x263A, 0x20, 0x76, 0x173, 0x2E, 0x20, 0x3F, 0xBB98, '\0'};
	if (wcscmp(prop->Value.lpszW, exp) == 0)
		return TEST_OK;
	for (const wchar_t *subj = prop->Value.lpszW; *subj != L'\0'; ++subj)
		fprintf(stderr, " %04x", *subj);
	fprintf(stderr, "\n");
	return TEST_FAIL;
}

static int test_iconvonly(const struct ictx &ctx)
{
	/*
	 * iconvonly01 has a charset for which no Win32 CPID exists (at
	 * least in our codepage.cpp), and so gets reconverted by inetmapi
	 * to UTF-8 so Windows has something to display.
	 */
	return strcasecmp(ctx.codepage, "utf-8") == 0 ?
	       TEST_OK : TEST_FAIL;
}

static int test_cset_upgrade(const struct ictx &ctx)
{
	return strcasecmp(ctx.codepage, t_ascii_upgrade) == 0 ?
	       TEST_OK : TEST_FAIL;
}

static int test_rfc2045_sec6_4(const struct ictx &ctx)
{
	/*
	 * On unknown Content-Transfer-Encodings, the MIME part needs to be
	 * read raw and tagged application/octet-stream (RFC 2045 §6.4 pg 17).
	 */
	object_ptr<IMAPITable> table;
	rowset_ptr allrows;
	SPropValue *prop;
	object_ptr<IAttach> at;

	if (ctx.imsg->GetAttachmentTable(MAPI_UNICODE, &~table) != S_OK)
		return TEST_FAIL;
	if (HrQueryAllRows(table, nullptr, nullptr, nullptr, 1, &~allrows) != S_OK)
		return TEST_FAIL;
	prop = PpropFindProp(allrows->aRow[0].lpProps, allrows->aRow[0].cValues, PR_ATTACH_NUM);
	if (prop == NULL)
		return TEST_FAIL;
	if (ctx.imsg->OpenAttach(prop->Value.ul, nullptr, MAPI_BEST_ACCESS, &~at) != S_OK)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_MIME_TAG_A, &prop) != S_OK)
		return TEST_FAIL;
	if (strcmp(prop->Value.lpszA, "application/octet-stream") != 0)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_METHOD, &prop) != S_OK)
		return TEST_FAIL;
	if (prop->Value.ul != ATTACH_BY_VALUE)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_DATA_BIN, &prop) != S_OK)
		return TEST_FAIL;
	if (strncmp(reinterpret_cast<const char *>(prop->Value.bin.lpb),
	    "=E2=98=BA", prop->Value.bin.cb) != 0)
		return TEST_FAIL;
	return TEST_OK;
}

static int test_unknown_text_charset(const struct ictx &ctx)
{
	/* Mails with unknown charsets get stored as attachments. */
	object_ptr<IMAPITable> table;
	rowset_ptr allrows;
	SPropValue *prop;
	object_ptr<IAttach> at;

	if (ctx.imsg->GetAttachmentTable(MAPI_UNICODE, &~table) != S_OK)
		return TEST_FAIL;
	if (HrQueryAllRows(table, nullptr, nullptr, nullptr, 1, &~allrows) != S_OK)
		return TEST_FAIL;
	prop = PpropFindProp(allrows->aRow[0].lpProps, allrows->aRow[0].cValues, PR_ATTACH_NUM);
	if (prop == NULL)
		return TEST_FAIL;
	if (ctx.imsg->OpenAttach(prop->Value.ul, nullptr, MAPI_BEST_ACCESS, &~at) != S_OK)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_MIME_TAG_A, &prop) != S_OK)
		return TEST_FAIL;
	if (strcmp(prop->Value.lpszA, "text/plain") != 0)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_METHOD, &prop) != S_OK)
		return TEST_FAIL;
	if (prop->Value.ul != ATTACH_BY_VALUE)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_DATA_BIN, &prop) != S_OK)
		return TEST_FAIL;
	if (prop->Value.bin.cb != 3 ||
	    memcmp(prop->Value.bin.lpb, "\xE2\x98\xBA", 3) != 0)
		return TEST_FAIL;
	return TEST_OK;
}

static int test_html_cset_01(const struct ictx &ctx)
{
	object_ptr<IMAPITable> table;
	rowset_ptr allrows;
	SPropValue *prop;
	object_ptr<IAttach> at;

	if (ctx.imsg->GetAttachmentTable(MAPI_UNICODE, &~table) != S_OK)
		return TEST_FAIL;
	if (HrQueryAllRows(table, nullptr, nullptr, nullptr, 1, &~allrows) != S_OK)
		return TEST_FAIL;
	prop = PpropFindProp(allrows->aRow[0].lpProps, allrows->aRow[0].cValues, PR_ATTACH_NUM);
	if (prop == NULL)
		return TEST_FAIL;
	if (ctx.imsg->OpenAttach(prop->Value.ul, nullptr, MAPI_BEST_ACCESS, &~at) != S_OK)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_MIME_TAG_A, &prop) != S_OK)
		return TEST_FAIL;
	if (strcmp(prop->Value.lpszA, "text/html") != 0)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_METHOD, &prop) != S_OK)
		return TEST_FAIL;
	if (prop->Value.ul != ATTACH_BY_VALUE)
		return TEST_FAIL;
	if (HrGetOneProp(at, PR_ATTACH_DATA_BIN, &prop) != S_OK)
		return TEST_FAIL;
	if (prop->Value.bin.cb < 6 ||
	    memcmp(prop->Value.bin.lpb, "body\xD0\xA7", 6) != 0)
		return TEST_FAIL;
	return TEST_OK;
}

class test_html_cset_02 _kc_final : public t_base {
	public:
	void setup(void) { m_dopt.charset_strict_rfc = false; };
	int verify(const struct ictx &);
};

int test_html_cset_02::verify(const struct ictx &ctx)
{
	return strcasecmp(ctx.codepage, "iso-8859-1") == 0 ?
	       TEST_OK : TEST_FAIL;
}

class test_cset_big5 _kc_final : public t_base {
	public:
	void setup(void) { m_dopt.ascii_upgrade = "big5"; };
	int verify(const struct ictx &);
};

int test_cset_big5::verify(const struct ictx &ctx)
{
	const SPropValue *prop = ctx.find(PR_SUBJECT_W);
	if (prop == NULL)
		return TEST_FAIL;
	const wchar_t *subj = prop->Value.lpszW;
	if (wcscmp(subj, L"?i???h?u?W?q??") != 0)
		return TEST_FAIL;
	return TEST_OK;
}

static int test_gb2312_18030(const struct ictx &ctx)
{
	/*
	 * kcinetmapi has an upgrade strategy defined for gb2312; this also
	 * happens to fix mistagged gb18030 words.
	 * See if this works (it can break if libvmime filters bad charsets
	 * first — cf. https://github.com/kisli/vmime/issues/149 )
	 */
	auto p = ctx.find(PR_SENDER_NAME_W);
	bool s = true;
	if (p != nullptr)
		s &= wcscmp(p->Value.lpszW, L"\x6881\x7950\x665f") == 0;
	p = ctx.find(PR_DISPLAY_TO_W);
	if (p != nullptr)
		s &= wcscmp(p->Value.lpszW, L"'\x6797\x6176\x7f8e'") == 0;
	p = ctx.find(PR_SUBJECT_W);
	if (p != nullptr)
		s &= wcscmp(p->Value.lpszW, L"\x8b80\x53d6: ??\x6392??\x751f?a") == 0;
	return s ? TEST_OK : TEST_FAIL;
}

static int test_zcp_11581(const struct ictx &ctx)
{
	if (wcsstr(ctx.data, L"RFC-compliant") != NULL)
		return TEST_OK;
	fputws(ctx.data, stderr);
	return TEST_FAIL;
}

static int test_zcp_11699(const struct ictx &ctx)
{
	const SPropValue *prop = ctx.find(PR_SUBJECT_W);
	if (prop == NULL)
		return TEST_FAIL;
	const wchar_t *subj = prop->Value.lpszW;
	static const wchar_t matchsubj[] = L"\x263A dum";
	static const wchar_t matchbody[] = L"\x263A dummy \x263B";
	if (wcscmp(subj, matchsubj) != 0 || wcscmp(ctx.data, matchbody) != 0)
		return TEST_FAIL;
	return TEST_OK;
}

class test_zcp_11713 _kc_final : public t_base {
	public:
	void setup(void) { m_dopt.charset_strict_rfc = false; }
	int verify(const struct ictx &ctx);
};

int test_zcp_11713::verify(const struct ictx &ctx)
{
	/*
	 * ISO-2022-JP (50220, 50222) is a valid outcome of any decoder.
	 * SHIFT_JIS (932) is a possible outcome of ZCP's IMToMAPI, but not
	 * strictly RFC-conformant.
	 */
	if (strcmp(ctx.codepage, "iso-2022-jp") != 0 &&
	    strcmp(ctx.codepage, "shift-jis") != 0) {
		fprintf(stderr, "zcp-11713: unexpected charset %s (%d)\n",
		        ctx.codepage, ctx.cpid);
		return TEST_FAIL;
	}
	/* "メッセージ" */
	if (wcsstr(ctx.data, L"\x30E1\x30C3\x30BB\x30FC\x30B8") == NULL) {
		fprintf(stderr, "zcp-11713: expected text piece not found\n");
		return TEST_FAIL;
	}
	return TEST_OK;
}

class test_zcp_12930 _kc_final : public t_base {
	public:
	void setup(void) { m_dopt.ascii_upgrade = nullptr; }
	int verify(const struct ictx &ctx);
};

int test_zcp_12930::verify(const struct ictx &ctx)
{
	if (strcmp(ctx.codepage, "us-ascii") != 0) {
		fprintf(stderr, "zcp-12930: expected us-ascii, got %s\n", ctx.codepage);
		return TEST_FAIL;
	}
	if (wcsstr(ctx.data, L"simply dummy t ext") == NULL) {
		fprintf(stderr, "zcp-12930: verbatim body extraction incorrect\n");
		return TEST_FAIL;
	}
	return TEST_OK;
}

static int test_zcp_13036_0d(const struct ictx &ctx)
{
	return (strcmp(ctx.codepage, "utf-8") == 0 &&
	        wcsstr(ctx.data, L"zg\x142osze\x144") != NULL) ?
	        TEST_OK : TEST_FAIL;
}

static int test_zcp_13036_69(const struct ictx &ctx)
{
	return (strcmp(ctx.codepage, "iso-8859-1") == 0 &&
	        wcsstr(ctx.data, L"J\xE4nner") != NULL) ? TEST_OK : TEST_FAIL;
}

static int test_zcp_13036_lh(const struct ictx &ctx)
{
	return (strcmp(ctx.codepage, "utf-8") == 0 &&
	        wcsstr(ctx.data, L"k\xF6nnen, \xF6" L"ffnen") != NULL) ?
	        TEST_OK : TEST_FAIL;
}

class test_zcp_13175 _kc_final : public t_base {
	public:
	void setup(void) { m_dopt.charset_strict_rfc = false; }
	int verify(const struct ictx &);
};

int test_zcp_13175::verify(const struct ictx &ctx)
{
	return (strcmp(ctx.codepage, "utf-8") == 0 &&
	        wcsstr(ctx.data, L"extrem \xFC" L"berh\xF6ht") != NULL) ?
	        TEST_OK : TEST_FAIL;
}

static int test_zcp_13337(const struct ictx &ctx)
{
	return (strcmp(ctx.codepage, "utf-8") == 0 &&
	        wcsstr(ctx.data, L"\xA0") != NULL) ?
		TEST_OK : TEST_FAIL;
}

static int test_zcp_13439_nl(const struct ictx &ctx)
{
	if (strcmp(ctx.codepage, "utf-8") != 0 ||
	    wcsstr(ctx.data, L"f\xFCr") == NULL)
		return TEST_FAIL;
	const SPropValue *prop = ctx.find(PR_SUBJECT_W);
	if (prop == NULL)
		return TEST_FAIL;
	const wchar_t *subj = prop->Value.lpszW;
	if (subj == NULL ||
	    wcscmp(subj, L"\xc4\xe4 \xd6\xf6 \xdc\xfc \xdf \x2013 Umlautetest, UMLAUTETEST 2") != 0)
		 return TEST_FAIL;
	return TEST_OK;
}

class test_zcp_13449_meca _kc_final : public t_base {
	public:
	void setup(void) { m_dopt.charset_strict_rfc = false; }
	int verify(const struct ictx &ctx);
};

int test_zcp_13449_meca::verify(const struct ictx &ctx)
{
	/* body codepage and content */
	if (strcmp(ctx.codepage, "windows-1252") != 0)
		return TEST_FAIL;
	if (wcsstr(ctx.data, L"M\xE9" L"canique") == NULL)
		return TEST_FAIL;

	/*
	 * The subject specifies an invalid charset (windows-1252http-equiv…).
	 * RFC 2049 proposed (a) display raw string as-is, (b) 
	 * do something else.
	 */
	const SPropValue *prop = ctx.find(PR_SUBJECT_W);
	if (prop == NULL)
		return TEST_FAIL;
	const wchar_t *subj = prop->Value.lpszW;
	if (wcscmp(subj, L"=?windows-1252http-equivContent-Type?Q?Orange_m=E9?=canique") == 0)
		/* RFC 2047 §6.2 ¶5 (a) */
		return TEST_OK;
	if (wcscmp(subj, L"Orange m?canique") == 0 ||
	    wcscmp(subj, L"Orange mcanique") == 0)
		/*
		 * RFC 2047 §6.2 ¶5 (b);
		 * possible outcomes from vmime and iconv.
		 */
		return TEST_OK;

	/* RFC 2047 §6.2 ¶5 (c) is not such a nice outcome. */
	return TEST_FAIL;
}

class test_zcp_13449_na _kc_final : public t_base {
	public:
	void setup(void) { m_dopt.ascii_upgrade = nullptr; }
	int verify(const struct ictx &ctx);
};

int test_zcp_13449_na::verify(const struct ictx &ctx)
{
	if (strcmp(ctx.codepage, "us-ascii") != 0)
		return TEST_FAIL;
	/* All non-ASCII is stripped, and the '!' is the leftover. */
	if (wcscmp(ctx.data, L"!") != 0)
		return TEST_FAIL;
	const SPropValue *prop = ctx.find(PR_SUBJECT_W);
	if (prop == NULL)
		return TEST_FAIL;
	const wchar_t *subj = prop->Value.lpszW;
	/*
	 * May need rework depending on how unreadable characters
	 * are transformed (decoder dependent).
	 */
	if (wcscmp(subj, L"N??t ASCII??????") != 0)
		return TEST_FAIL;
	return TEST_OK;
}

static int test_zcp_13473(const struct ictx &ctx)
{
	return (strcmp(ctx.codepage, "utf-8") == 0) ? TEST_OK : TEST_FAIL;
}

static int test_kc_138_1(const struct ictx &ctx)
{
	/* Ensure later bodies do not override iCal object description. */
	return wcsstr(ctx.data, L"part1") != nullptr ? TEST_OK : TEST_FAIL;
}

static int test_kc_138_2(const struct ictx &ctx)
{
	/* Ensure iCal object description does not override earlier bodies. */
	return wcsstr(ctx.data, L"part1") == nullptr &&
	       wcsstr(ctx.data, L"part2") == nullptr ? TEST_FAIL : TEST_OK;
}

static int test_html_multipart_related(const struct ictx &ctx)
{
	const SPropValue *prop = ctx.find(PR_HTML);
	if (prop == NULL)
		return TEST_FAIL;
	/*
	 * Currently, we want that multipart/related thing to end up as an
	 * attachment, rather than as body text. So make sure we _don't_ find
	 * the Postbus needle in the parsed body.
	 */
	void *z = memmem(prop->Value.bin.lpb, prop->Value.bin.cb,
	          "Postbus 61", 10);
	if (z == NULL)
		return TEST_OK;
	fprintf(stderr, "Postbus 61 found at position %td\n",
	        reinterpret_cast<LPBYTE>(z) - prop->Value.bin.lpb);
	return TEST_FAIL;
}

static size_t runtests(std::string &&d)
{
	size_t err = 0;
	d += "/";
	err += chkfile(d + "gb2312_18030.eml", test_gb2312_18030);
	err += chkfile(d + "zcp-11581.eml", test_zcp_11581);
	err += chkfile(d + "multipart html", test_html_multipart_related);
	err += chkfile(d + "big5.eml", test_cset_big5());
	err += chkfile(d + "html-charset-01.eml", test_html_cset_01);
	err += chkfile(d + "html-charset-02.eml", test_html_cset_02());
	err += chkfile(d + "zcp-13449-na.eml", test_zcp_13449_na());
	err += chkfile(d + "iconvonly01.eml", test_iconvonly);
	err += chkfile(d + "iconvonly02.eml", test_iconvonly);
	err += chkfile(d + "no-content-type.eml", test_cset_upgrade);
	err += chkfile(d + "no-content-type-alt.eml", test_cset_upgrade);
	err += chkfile(d + "unknown-transfer-enc.eml", test_rfc2045_sec6_4);
	err += chkfile(d + "unknown-text-charset.eml", test_unknown_text_charset);
	err += chkfile(d + "no-charset-01.eml", test_cset_upgrade);
	err += chkfile(d + "no-charset-02.eml", test_cset_upgrade);
	err += chkfile(d + "no-charset-03.eml", test_cset_upgrade);
	err += chkfile(d + "no-charset-07.eml", test_cset_upgrade);
	err += chkfile(d + "encoded-word-split.eml", test_encword_split);
	err += chkfile(d + "mime_charset_01.eml", test_mimecset01);
	err += chkfile(d + "mime_charset_02.eml", test_mimecset01);
	err += chkfile(d + "mime_charset_03.eml", test_mimecset03());
	err += chkfile(d + "zcp-11699-ub.eml", test_zcp_11699);
	err += chkfile(d + "zcp-11699-utf8.eml", test_zcp_11699);
	err += chkfile(d + "zcp-11699-p.eml", test_zcp_11699);
	err += chkfile(d + "zcp-11713.eml", test_zcp_11713());
	err += chkfile(d + "zcp-12930.eml", test_zcp_12930());
	err += chkfile(d + "zcp-13036-6906a338.eml", test_zcp_13036_69);
	err += chkfile(d + "zcp-13036-0db504a2.eml", test_zcp_13036_0d);
	err += chkfile(d + "zcp-13036-lh.eml", test_zcp_13036_lh);
	err += chkfile(d + "zcp-13175.eml", test_zcp_13175());
	err += chkfile(d + "zcp-13337.eml", test_zcp_13337);
	err += chkfile(d + "zcp-13439-nl.eml", test_zcp_13439_nl);
	err += chkfile(d + "zcp-13449-meca.eml", test_zcp_13449_meca());
	err += chkfile(d + "zcp-13473.eml", test_zcp_13473);
	err += chkfile(d + "kc-138-1.eml", test_kc_138_1);
	err += chkfile(d + "kc-138-2.eml", test_kc_138_2);
	return err;
#undef TMDIR
}

int main(int argc, const char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Need the directory with test mails as first argument.\n");
		return EXIT_FAILURE;
	}
	AutoMAPI automapi;
	size_t err = 0;
	auto hr = automapi.Initialize();
	if (hr != hrSuccess) {
		fprintf(stderr, "MAPIInitialize: %s\n",
		        GetMAPIErrorMessage(hr));
		return EXIT_FAILURE;
	}

	try {
		err = runtests(argv[1]);
		fprintf(stderr, (err == 0) ? "Overall success\n" : "Overall FAILURE\n");
	} catch (const KMAPIError &e) {
		fprintf(stderr, "Aborted because of exception: %s\n", e.what());
	}
	return (err == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
