/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright 2018, Kopano and its licensors
 */
#include <string>
#include <climits>
#include <cstdlib>
#include <libHX/option.h>
#include <mapitags.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/mapiext.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>

using namespace KC;

struct ibr_parse_ctx {
	SRestriction rst{};
	std::vector<ACTION> actions;
	memory_ptr<char> memblk;
	std::string name;
	unsigned int seq = 0, rule_state = ST_ENABLED;
	bool have_seq = false, have_cond = false;
};

enum { IBR_NONE = 0, IBR_SHOW, IBR_ADD };
static unsigned int ibr_action, ibr_passpr;
static int ibr_delete_pos = -1;
static char *ibr_user, *ibr_pass, *ibr_host;
static constexpr const struct HXoption ibr_options[] = {
	{nullptr, 'A', HXTYPE_VAL, &ibr_action, nullptr, nullptr, IBR_ADD, "Add new rule to the table"},
	{nullptr, 'D', HXTYPE_INT, &ibr_delete_pos, nullptr, nullptr, 0, "Delete rule by position", "POS"},
	{nullptr, 'P', HXTYPE_NONE, &ibr_passpr, nullptr, nullptr, 0, "Prompt for plain password to use for login"},
	{nullptr, 'S', HXTYPE_VAL, &ibr_action, nullptr, nullptr, IBR_SHOW, "List rules for user"},
	{nullptr, 'h', HXTYPE_STRING, &ibr_host, nullptr, nullptr, 0, "URI for server"},
	{"user", 'u', HXTYPE_STRING, &ibr_user, nullptr, nullptr, 0, "User to inspect inbox of", "NAME"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static const char ibr_tree[] = " *  ";

static void ibr_rule_state(unsigned int flags)
{
#define S(f) if (flags & (f)) printf(" " #f);
	S(ST_ENABLED)
	S(ST_ERROR)
	S(ST_ONLY_WHEN_OOF)
	S(ST_KEEP_OOF_HIST)
	S(ST_EXIT_LEVEL)
	S(ST_SKIP_IF_SCL_IS_SAFE)
	S(ST_RULE_PARSE_ERROR)
	S(ST_CLEAR_OOF_HIST)
#undef S
}

static ACTTYPE ibr_atype_parse(const char *s)
{
	if (strcmp(s, "move") == 0) return OP_MOVE;
	if (strcmp(s, "copy") == 0) return OP_COPY;
	if (strcmp(s, "reply") == 0) return OP_REPLY;
	if (strcmp(s, "oof-reply") == 0) return OP_OOF_REPLY;
	if (strcmp(s, "defer-action") == 0) return OP_DEFER_ACTION;
	if (strcmp(s, "bounce") == 0) return OP_BOUNCE;
	if (strcmp(s, "forward") == 0) return OP_FORWARD;
	if (strcmp(s, "delegate") == 0) return OP_DELEGATE;
	if (strcmp(s, "tag") == 0) return OP_TAG;
	if (strcmp(s, "delete") == 0) return OP_DELETE;
	if (strcmp(s, "mark-as-read") == 0) return OP_MARK_AS_READ;
	return static_cast<ACTTYPE>(-1);
}

static const char *ibr_proptag_text(unsigned int tag)
{
#define S(s) case PROP_ID(s): return #s
	switch (PROP_ID(tag)) {
		S(PR_RULE_ID); S(PR_RULE_SEQUENCE);
		S(PR_RULE_NAME); S(PR_RULE_STATE); S(PR_RULE_LEVEL);
		S(PR_RULE_ACTIONS); S(PR_RULE_CONDITION);
		S(PR_RULE_PROVIDER); S(PR_RULE_PROVIDER_DATA);
		S(PR_BODY); S(PR_SUBJECT); S(PR_MESSAGE_CLASS);
		S(PR_MESSAGE_FLAGS); S(PR_MESSAGE_RECIPIENTS);
		S(PR_MESSAGE_ATTACHMENTS); S(PR_OBJECT_TYPE);
		S(PR_DISPLAY_NAME); S(PR_DISPLAY_TYPE); S(PR_EMAIL_ADDRESS);
		S(PR_SMTP_ADDRESS); S(PR_ADDRTYPE); S(PR_RECIPIENT_TYPE);
		S(PR_SEARCH_KEY); S(PR_ENTRYID);
	default: return nullptr;
	}
#undef S
}

static void ibr_show_proptag_text(unsigned int tag)
{
	auto n = ibr_proptag_text(tag);
	if (n != nullptr)
		printf("%s", n);
	else
		printf("tag 0x%08x", tag);
}

#define S(s) case s: return #s
static const char *ibr_atype_text(ACTTYPE t)
{
	switch (t) {
		S(OP_MOVE); S(OP_COPY); S(OP_REPLY); S(OP_OOF_REPLY);
		S(OP_DEFER_ACTION); S(OP_BOUNCE); S(OP_FORWARD);
		S(OP_DELEGATE); S(OP_TAG); S(OP_DELETE); S(OP_MARK_AS_READ);
	default: return "<UNKNOWN OP>";
	}
}

static const char *ibr_rtype_text(unsigned int type)
{
	switch (type) {
		S(RES_AND); S(RES_OR); S(RES_NOT); S(RES_CONTENT);
		S(RES_PROPERTY); S(RES_COMPAREPROPS); S(RES_BITMASK);
		S(RES_SIZE); S(RES_EXIST); S(RES_SUBRESTRICTION);
		S(RES_COMMENT);
	default: return "<UNKNOWN RES>";
	}
}
#undef S

static const char *ibr_relop_text(unsigned int op)
{
	switch (op) {
	case RELOP_LT: return "<";
	case RELOP_LE: return "<=";
	case RELOP_GT: return ">";
	case RELOP_GE: return ">=";
	case RELOP_EQ: return "==";
	case RELOP_NE: return "!=";
	case RELOP_RE: return "~";
	default: return "<UNKNOWN OP>";
	}
}

static int ibr_show_propval(const SPropValue &p)
{
	switch (PROP_TYPE(p.ulPropTag)) {
	case PT_UNSPECIFIED: return printf(" unspec");
	case PT_NULL: return printf(" null");
	case PT_SHORT: return printf(" %hd", p.Value.i);
	case PT_LONG: return printf(" %d", p.Value.l);
	case PT_FLOAT: return printf(" %f", p.Value.flt);
	case PT_DOUBLE: return printf(" %f", p.Value.dbl);
	case PT_BOOLEAN: return printf(" %u", p.Value.b);
	case PT_STRING8: return printf(" \"%s\"", p.Value.lpszA);
	case PT_UNICODE: return printf(" \"%s\"", convert_to<std::string>(p.Value.lpszW).c_str());
	case PT_BINARY: return printf(" binary(\"%s\")", bin2txt(p.Value.bin).c_str());
	case PT_LONGLONG: return printf(" %lld", static_cast<long long>(p.Value.li.QuadPart));
	default: return printf(" <PT_UNHANDLED:%04x>", PROP_TYPE(p.ulPropTag));
	}
}

static void ibr_show_adrentry(unsigned int level, const ADRENTRY &e)
{
	for (size_t i = 0; i < e.cValues; ++i) {
		printf("%-*s%s", level * 4, "", ibr_tree);
		ibr_show_proptag_text(e.rgPropVals[i].ulPropTag);
		ibr_show_propval(e.rgPropVals[i]);
		printf("\n");
	}

}

static void ibr_show_adrlist(unsigned int level, const ADRLIST &al)
{
	for (size_t i = 0; i < al.cEntries; ++i) {
		printf("%-*s *  Entry #%zu\n", level * 4, "", i);
		ibr_show_adrentry(level + 1, al.aEntries[i]);
	}
}

static void ibr_show_actions(unsigned int level, const ACTIONS &al)
{
	for (size_t i = 0; i < al.cActions; ++i) {
		const auto &a = al.lpAction[i];
		printf("%-*s%s", level * 4, "", ibr_tree);
		printf("Action #%zu: %s", i, ibr_atype_text(a.acttype));
		switch (a.acttype) {
		case OP_MOVE: case OP_COPY:
			printf(" -> store_eid \"%s\" folder_eid \"%s\"\n",
				bin2txt(a.actMoveCopy.lpStoreEntryId, a.actMoveCopy.cbStoreEntryId).c_str(),
				bin2txt(a.actMoveCopy.lpFldEntryId, a.actMoveCopy.cbFldEntryId).c_str());
			break;
		case OP_REPLY: case OP_OOF_REPLY:
			printf(" tpl_guid %s msg_eid \"%s\"\n",
				bin2hex(sizeof(a.actReply.guidReplyTemplate), &a.actReply.guidReplyTemplate).c_str(),
				bin2txt(a.actReply.lpEntryId, a.actReply.cbEntryId).c_str());
			break;
		case OP_DEFER_ACTION:
			printf(" binary(\"%s\")\n", bin2txt(a.actDeferAction.pbData, a.actDeferAction.cbData).c_str());
			break;
		case OP_BOUNCE:
			printf(" code %d\n", a.scBounceCode);
			break;
		case OP_FORWARD: case OP_DELEGATE:
			printf(" to ...\n");
			if (a.lpadrlist != nullptr)
				ibr_show_adrlist(level + 1, *a.lpadrlist);
			break;
		case OP_TAG:
			printf(" ");
			ibr_show_proptag_text(a.propTag.ulPropTag);
			printf(" ");
			ibr_show_propval(a.propTag);
			printf("\n");
			break;
		default:
			printf("\n");
			break;
		}
	}
}

static void ibr_show_res_content(unsigned int level, const SContentRestriction &ct)
{
	switch (ct.ulFuzzyLevel & 0xffff) {
	case FL_FULLSTRING: printf(" FL_FULLSTRING"); break;
	case FL_SUBSTRING: printf(" FL_SUBSTRING"); break;
	case FL_PREFIX: printf(" FL_PREFIX"); break;
	}
	if (ct.ulFuzzyLevel & FL_IGNORECASE)
		printf(" FL_IGNORECASE");
	if (ct.ulFuzzyLevel & FL_IGNORENONSPACE)
		printf(" FL_IGNORENONSPACE");
	if (ct.ulFuzzyLevel & FL_LOOSE)
		printf(" FL_LOOSE");
	printf(" 0x%08x", ct.ulPropTag);
	if (ct.lpProp != nullptr)
		ibr_show_propval(*ct.lpProp);
	printf("\n");
}

static void ibr_show_res_prop(unsigned int level, const SPropertyRestriction &r)
{
	printf(" 0x%08x %s", r.ulPropTag, ibr_relop_text(r.relop));
	if (r.lpProp != nullptr)
		ibr_show_propval(*r.lpProp);
	printf("\n");
}

static void ibr_show_res_bmr(unsigned int level, const SBitMaskRestriction &r)
{
	printf(" value(");
	ibr_show_proptag_text(r.ulPropTag);
	printf(") & 0x%x %s\n", r.ulMask, r.relBMR == BMR_EQZ ? "== 0" : "!= 0");
}

static void ibr_show_cond(unsigned int level, const SRestriction &r);
static void ibr_show_res_comment(unsigned int level, const SCommentRestriction &r)
{
	if (r.lpRes != nullptr) {
		printf("\n%-*s%sRestriction:\n", level * 4, "", ibr_tree);
		ibr_show_cond(level + 1, *r.lpRes);
	}
	if (r.lpProp != nullptr)
		for (size_t i = 0; i < r.cValues; ++i) {
			printf("%-*s%s", level * 4, "", ibr_tree);
			ibr_show_proptag_text(r.lpProp[i].ulPropTag);
			ibr_show_propval(r.lpProp[i]);
			printf("\n");
		}
}

static void ibr_show_cond(unsigned int level, const SRestriction &r)
{
	printf("%-*s%s%s", level * 4, "", ibr_tree, ibr_rtype_text(r.rt));
	switch (r.rt) {
	case RES_AND:
		printf("\n");
		for (size_t i = 0; i < r.res.resAnd.cRes; ++i)
			ibr_show_cond(level + 1, r.res.resAnd.lpRes[i]);
		break;
	case RES_OR:
		printf("\n");
		for (size_t i = 0; i < r.res.resOr.cRes; ++i)
			ibr_show_cond(level + 1, r.res.resOr.lpRes[i]);
		break;
	case RES_NOT:
		printf("\n");
		if (r.res.resNot.lpRes != nullptr)
			ibr_show_cond(level + 1, *r.res.resNot.lpRes);
		break;
	case RES_CONTENT:
		ibr_show_res_content(level + 1, r.res.resContent);
		break;
	case RES_PROPERTY:
		ibr_show_res_prop(level, r.res.resProperty);
		break;
	case RES_EXIST:
		printf(" ");
		ibr_show_proptag_text(r.res.resExist.ulPropTag);
		printf("\n");
		break;
	case RES_BITMASK:
		ibr_show_res_bmr(level, r.res.resBitMask);
		break;
	case RES_COMMENT:
		ibr_show_res_comment(level + 1, r.res.resComment);
		break;
	case RES_SUBRESTRICTION: {
		printf(" subobject ");
		ibr_show_proptag_text(r.res.resSub.ulSubObject);
		printf("\n");
		if (r.res.resSub.lpRes != nullptr)
			ibr_show_cond(level + 1, *r.res.resSub.lpRes);
		break;
	}
	default:
		printf(" <UNHANDLED>\n");
		break;
	}
}

static HRESULT ibr_show_rule(unsigned int level, const SRow &row)
{
	for (unsigned int i = 0; i < row.cValues; ++i) {
		const auto &prop = row.lpProps[i];
		if (PROP_TYPE(prop.ulPropTag) == PT_ERROR)
			continue;
		printf("%-*s%s", level * 4, "", ibr_tree);
		ibr_show_proptag_text(prop.ulPropTag);
		printf(":");

		switch (prop.ulPropTag) {
		case PR_RULE_STATE:
			ibr_rule_state(prop.Value.ul);
			printf("\n");
			break;
		case PR_RULE_ACTIONS: {
			printf("\n");
			auto a = reinterpret_cast<const ACTIONS *>(prop.Value.lpszA);
			if (a != nullptr)
				ibr_show_actions(level + 1, *a);
			break;
		}
		case PR_RULE_CONDITION: {
			printf("\n");
			auto c = reinterpret_cast<const SRestriction *>(prop.Value.lpszA);
			if (c != nullptr)
				ibr_show_cond(level + 1, *c);
			break;
		}
		default: {
			ibr_show_propval(prop);
			printf("\n");
			break;
		}
		}
	}
	return hrSuccess;
}

static HRESULT ibr_show(IExchangeModifyTable *emt)
{
	object_ptr<IMAPITable> tbl;
	auto ret = emt->GetTable(0, &~tbl);
	if (ret != hrSuccess)
		return kc_perrorf("GetTable", ret);
	memory_ptr<SPropTagArray> cols;
	ret = tbl->QueryColumns(TBL_ALL_COLUMNS, &~cols);
	if (ret != hrSuccess)
		return kc_perrorf("QueryColumns", ret);
	ret = tbl->SetColumns(cols, 0);
	if (ret != hrSuccess)
		return kc_perrorf("SetColumns", ret);
	rowset_ptr rs;
	ret = HrQueryAllRows(tbl, nullptr, nullptr, nullptr, UINT_MAX, &~rs);
	if (ret != hrSuccess)
		return kc_perrorf("HrQueryAllRows", ret);

	for (size_t i = 0; i < rs.size(); ++i) {
		printf("%sEntry #%zu\n", ibr_tree, i);
		ibr_show_rule(1, rs[i]);
	}
	return hrSuccess;
}

static bool ibr_parse_exists(int &argc, const char **&argv, void *base, SRestriction &r)
{
	if (argc < 1) {
		fprintf(stderr, "\"exists\" needs to be followed by a proptag or mnemonic\n");
		return false;
	}
	char *e = nullptr;
	r.rt = RES_EXIST;
	r.res.resExist.ulPropTag = strtoul(*argv, &e, 0);
	if (*e == '\0') {
		--argc; ++argv;
		return true;
	}
	if (strcmp(*argv, "message-class") == 0)
		r.res.resExist.ulPropTag = PR_MESSAGE_CLASS;
	else
		return false;
	--argc;
	++argv;
	return true;
}

static bool ibr_parse_rst(int &argc, const char **&argv, void *base, SRestriction &r)
{
	if (argc < 1) {
		fprintf(stderr, "\"cond\" needs to be followed by a restriction type\n");
		return false;
	}
	if (strcmp(argv[0], "exists") == 0)
		return ibr_parse_exists(--argc, ++argv, base, r);
	fprintf(stderr, "Unknown restriction type\n");
	return false;
}

static bool ibr_parse_act_move(int &argc, const char **&argv, void *base, ACTION &af)
{
	if (argc < 4 || strcmp(argv[0], "store") != 0 || strcmp(argv[2], "folder") != 0) {
		fprintf(stderr, "\"move\"/\"copy\" needs to be followed by \"store GUID folder GUID\"\n");
		return false;
	}
	auto &a = af.actMoveCopy;
	auto store = hex2bin(argv[1]);
	auto folder = hex2bin(argv[3]);
	if (MAPIAllocateMore(store.size(), base, reinterpret_cast<void **>(&a.lpStoreEntryId)) != hrSuccess ||
	    MAPIAllocateMore(folder.size(), base, reinterpret_cast<void **>(&a.lpFldEntryId)) != hrSuccess)
		return false;
	memcpy(a.lpStoreEntryId, store.c_str(), store.size());
	memcpy(a.lpFldEntryId, folder.c_str(), folder.size());
	a.cbStoreEntryId = store.size();
	a.cbFldEntryId = folder.size();
	argc -= 4;
	argv += 4;
	return true;
}

static bool ibr_parse_act(int &argc, const char **&argv, void *base, ACTION &a)
{
	if (argc == 0) {
		fprintf(stderr, "\"act\" must be followed by an action type (move, copy, delete)\n");
		return false;
	}
	a.acttype = ibr_atype_parse(*argv);
	--argc; ++argv;
	if (a.acttype == OP_DELETE || a.acttype == OP_MARK_AS_READ)
		return true; /* no extra arguments needed */
	if (a.acttype == OP_MOVE || a.acttype == OP_COPY)
		return ibr_parse_act_move(argc, argv, base, a);
	fprintf(stderr, "Unknown action type\n");
	return false;
}

static HRESULT ibr_parse(int argc, const char **argv, ibr_parse_ctx &ctx)
{
	ctx.actions.clear();
	if (MAPIAllocateBuffer(1, &~ctx.memblk) != hrSuccess)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	while (argc > 0) {
		if (strcmp(*argv, "seq") == 0) {
			if (argc < 2) {
				fprintf(stderr, "\"seq\" needs an argument\n");
				return MAPI_E_CALL_FAILED;
			}
			ctx.have_seq = true;
			ctx.seq = strtoul(argv[1], nullptr, 0);
			argc -= 2;
			argv += 2;
		} else if (strcmp(*argv, "name") == 0) {
			if (argc < 2) {
				fprintf(stderr, "\"name\" needs an argument\n");
				return MAPI_E_CALL_FAILED;
			}
			ctx.name = argv[1];
			argc -= 2;
			argv += 2;
		} else if (strcmp(*argv, "cond") == 0) {
			if (ctx.have_cond) {
				fprintf(stderr, "Only one (main) condition is allowed; use AND/OR logic instead.\n");
				return MAPI_E_CALL_FAILED;
			}
			if (!ibr_parse_rst(--argc, ++argv, ctx.memblk, ctx.rst))
				return MAPI_E_CALL_FAILED;
			ctx.have_cond = true;
		} else if (strcmp(*argv, "act") == 0) {
			ACTION a;
			memset(&a, 0, sizeof(a));
			if (!ibr_parse_act(--argc, ++argv, ctx.memblk, a))
				return MAPI_E_CALL_FAILED;
			ctx.actions.push_back(std::move(a));
		} else if (strcmp(*argv, "stop") == 0) {
			ctx.rule_state |= ST_EXIT_LEVEL;
			--argc;
			++argv;
		} else {
			fprintf(stderr, "Unrecognized keyword \"%s\"\n", *argv);
			return MAPI_E_CALL_FAILED;
		}
	}
	return hrSuccess;
}

static HRESULT ibr_add(IExchangeModifyTable *emt, ibr_parse_ctx &&ctx)
{
	ACTIONS ablk;
	ablk.ulVersion = 1;
	ablk.cActions = ctx.actions.size();
	ablk.lpAction = &ctx.actions[0];
	SPropValue pv[5];
	unsigned int pc = 0;
	if (ctx.have_seq) {
		pv[pc].ulPropTag = PR_RULE_SEQUENCE;
		pv[pc++].Value.ul = ctx.seq;
	}
	if (!ctx.name.empty()) {
		pv[pc].ulPropTag = CHANGE_PROP_TYPE(PR_RULE_NAME, PT_STRING8);
		pv[pc++].Value.lpszA = const_cast<char *>(ctx.name.c_str());
	}
	pv[pc].ulPropTag = PR_RULE_STATE;
	pv[pc++].Value.ul = ctx.rule_state;
	if (ctx.have_cond) {
		pv[pc].ulPropTag = PR_RULE_CONDITION;
		pv[pc++].Value.lpszA = reinterpret_cast<char *>(&ctx.rst);
	}
	if (ctx.actions.size() > 0) {
		pv[pc].ulPropTag = PR_RULE_ACTIONS;
		pv[pc++].Value.lpszA = reinterpret_cast<char *>(&ablk);
	}
	memory_ptr<ROWLIST> rl;
	auto ret = MAPIAllocateBuffer(CbNewROWLIST(1), &~rl);
	if (ret != hrSuccess)
		return kc_perrorf("MAPIAllocate", ret);
	rl->cEntries = 1;
	rl->aEntries[0] = {ROW_ADD, pc, pv};
	ret = emt->ModifyTable(0, rl.get());
	if (ret != hrSuccess)
		return kc_perrorf("ModifyTable", ret);
	return ret;
}

static HRESULT ibr_delete_rule(IExchangeModifyTable *emt, int seqno)
{
	SPropValue pv;
	pv.ulPropTag = PR_RULE_ID;
	pv.Value.l = seqno;
	memory_ptr<ROWLIST> rl;
	auto ret = MAPIAllocateBuffer(CbNewROWLIST(1), &~rl);
	if (ret != hrSuccess)
		return kc_perror("MAPIAllocateBuffer", ret);
	rl->cEntries = 1;
	rl->aEntries[0] = {ROW_REMOVE, 1, &pv};
	ret = emt->ModifyTable(0, rl.get());
	if (ret != hrSuccess)
		return kc_perrorf("ModifyTable", ret);
	return ret;
}

static HRESULT ibr_perform(int argc, const char **argv)
{
	KServerContext srvctx;
	srvctx.m_app_misc = "rules";
	srvctx.m_host = (ibr_host != nullptr) ? ibr_host : GetServerUnixSocket("default:");
	auto ret = srvctx.logon(ibr_user, ibr_pass);
	if (ret != hrSuccess)
		return kc_perror("logon", ret);
	object_ptr<IMAPIFolder> inbox;
	ret = srvctx.inbox(&~inbox);
	if (ret != hrSuccess)
		return kc_perror("GetReceiveFolder/OpenEntry", ret);

	object_ptr<IExchangeModifyTable> emt;
	ret = inbox->OpenProperty(PR_RULES_TABLE, &iid_of(emt), MAPI_MODIFY, 0, &~emt);
	if (ret != hrSuccess)
		return kc_perrorf("OpenProperty", ret);
	if (ibr_delete_pos >= 0)
		return ibr_delete_rule(emt, ibr_delete_pos);
	if (ibr_action == IBR_SHOW)
		return ibr_show(emt);

	ibr_parse_ctx ctx;
	ret = ibr_parse(argc - 1, argv + 1, ctx);
	if (ret != hrSuccess)
		return ret;
	if (ibr_action == IBR_ADD)
		return ibr_add(emt, std::move(ctx));
	fprintf(stderr, "No action selected\n");
	return MAPI_E_CALL_FAILED;
}

static HRESULT ibr_parse_options(int &argc, const char **&argv)
{
	if (HX_getopt(ibr_options, &argc, &argv, HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return MAPI_E_CALL_FAILED;
	if ((ibr_delete_pos >= 0) + (ibr_action != IBR_NONE) > 1) {
		fprintf(stderr, "Only one of -A, -D and -S may be specified.\n");
		return MAPI_E_CALL_FAILED;
	}
	if (ibr_user == nullptr) {
		fprintf(stderr, "No username specified.\n");
		return MAPI_E_CALL_FAILED;
	}
	char *p = nullptr;
	p = ibr_passpr ? get_password("Login password: ") : getenv("IBR_PASSWORD");
	if (p != nullptr)
		ibr_pass = strdup(p);
	return hrSuccess;
}

int main(int argc, const char **argv)
{
	setlocale(LC_ALL, "");
	auto ret = ibr_parse_options(argc, argv);
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	return ibr_perform(argc, argv) == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}
