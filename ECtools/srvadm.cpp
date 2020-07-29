/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005-2016 Zarafa and its licensors
 * Copyright 2018, Kopano and its licensors
 */
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <mapidefs.h>
#include <libHX/option.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>

using namespace KC;
static int opt_purge_deferred, opt_purge_softdelete = -1, opt_ct_size = 1024;
static unsigned int opt_cache_bits, opt_ct_create, opt_ct_read;
static const char *opt_config_file, *opt_host, *opt_clear_cache;
static std::unique_ptr<ECConfig> adm_config;

static constexpr struct HXoption adm_options[] = {
	{"clear-cache", 0, HXTYPE_STRING, &opt_clear_cache, nullptr, nullptr, 0, "Clear one or more caches"},
	{"ct-create", 0, HXTYPE_UINT, &opt_ct_create, nullptr, nullptr, 0, "Cache testing: generate dummy messages", "AMOUNT"},
	{"ct-read", 0, HXTYPE_NONE, &opt_ct_read, nullptr, nullptr, 0, "Cache testing: readback dummy messages"},
	{"ct-size", 0, HXTYPE_UINT, &opt_ct_size, nullptr, nullptr, 0, "Cache testing: mail size", "BYTES"},
	{"purge-deferred", 0, HXTYPE_NONE, &opt_purge_deferred, nullptr, nullptr, 0, "Purge all items in the deferred update table"},
	{"purge-softdelete", 0, HXTYPE_INT, &opt_purge_softdelete, nullptr, nullptr, 0, "Purge softdeleted items older than N days"},
	{nullptr, 'c', HXTYPE_STRING, &opt_config_file, nullptr, nullptr, 0, "Specify alternate config file"},
	{nullptr, 'h', HXTYPE_STRING, &opt_host, nullptr, nullptr, 0, "URI for server"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static constexpr configsetting_t adm_config_defaults[] = {
	{"default_store_locale", ""},
	{"server_socket", "default:"},
	{"sslkey_file", ""},
	{"sslkey_pass", ""},
	{nullptr},
};

static HRESULT adm_clear_cache(IECServiceAdmin *svcadm)
{
	auto ret = svcadm->PurgeCache(opt_cache_bits);
	if (ret != hrSuccess)
		kc_perror("Clearing caches failed", ret);
	printf("Caches have been cleared.\n");
	return hrSuccess;
}

static HRESULT adm_purge_deferred(IECServiceAdmin *svcadm)
{
	using namespace std::chrono;
	auto last = steady_clock::now() - seconds(1);
	while (true) {
		ULONG rem;
		auto ret = svcadm->PurgeDeferredUpdates(&rem);
		if (ret == MAPI_E_NOT_FOUND)
			break;
		else if (ret != hrSuccess)
			return kc_perror("Purge failed", ret);
		auto now = decltype(last)::clock::now();
		if (last + seconds(1) > now)
			continue;
		last = now;
		if (isatty(STDERR_FILENO))
			fprintf(stderr, "\r\e[2K""Remaining deferred records: %u", rem); // ]
		else
			fprintf(stderr, "Remaining deferred records: %u\n", rem);
		fflush(stderr);
	}
	if (isatty(STDERR_FILENO))
		fprintf(stderr, "\r\e[2K"); // ]
	fprintf(stderr, "Deferred records processed.\n");
	return hrSuccess;
}

static HRESULT adm_purge_softdelete(IECServiceAdmin *svcadm)
{
	auto ret = svcadm->PurgeSoftDelete(opt_purge_softdelete);
	if (ret == MAPI_E_BUSY) {
		printf("Softdelete purge already running.\n");
		return hrSuccess;
	} else if (ret != hrSuccess) {
		return kc_perror("Softdelete purge failed", ret);
	}
	printf("Softdelete purge done.\n");
	return hrSuccess;
}

static HRESULT adm_ct_create(IMsgStore *store, unsigned int amt)
{
	object_ptr<IMAPIFolder> root;
	auto ret = store->OpenEntry(0, nullptr, &iid_of(root), MAPI_DEFERRED_ERRORS | MAPI_MODIFY, nullptr, &~root);
	if (ret != hrSuccess)
		return hr_lerrf(ret, "OpenEntry");
	std::unique_ptr<char[]> mdat(new char[opt_ct_size]);
	SPropValue pv[2];
	memset(mdat.get(), '!', opt_ct_size);
	mdat[opt_ct_size-1] = '\0';
	pv[0].ulPropTag   = PR_SUBJECT_A;
	pv[0].Value.lpszA = const_cast<char *>("DUMMY");
	pv[1].ulPropTag   = PR_BODY_A;
	pv[1].Value.lpszA = const_cast<char *>(mdat.get());
	auto cl = make_scope_success([]() { printf("\n"); });

	while (amt-- > 0) {
		object_ptr<IMessage> msg;
		ret = root->CreateMessage(&iid_of(msg), MAPI_DEFERRED_ERRORS, &~msg);
		if (ret != hrSuccess)
			return hr_lerrf(ret, "CreateMessage");
		ret = msg->SetProps(ARRAY_SIZE(pv), pv, nullptr);
		if (ret != hrSuccess)
			return hr_lerrf(ret, "SetProps");
		msg->SaveChanges(0);
		if (amt % 32 == 0) {
			printf("\r\e[2K" "%u left...", amt);
			fflush(stdout);
		}
	}
	return hrSuccess;
}

static HRESULT adm_ct_read(IMsgStore *store)
{
	object_ptr<IMAPIFolder> root;
	auto ret = store->OpenEntry(0, nullptr, &iid_of(root), MAPI_DEFERRED_ERRORS | MAPI_MODIFY, nullptr, &~root);
	if (ret != hrSuccess)
		return hr_lerrf(ret, "OpenEntry");
	object_ptr<IMAPITable> tbl;
	ret = root->GetContentsTable(MAPI_DEFERRED_ERRORS, &~tbl);
	if (ret != hrSuccess)
		return hr_lerrf(ret, "GetContentsTable");
	static constexpr SizedSPropTagArray(1, cols) = {1, {PR_ENTRYID}};
	ret = tbl->SetColumns(cols, TBL_ASYNC | TBL_BATCH);
	size_t total = 0;
	auto cl = make_scope_success([]() { printf("\n"); });

	do {
		rowset_ptr rset;
		ret = tbl->QueryRows(1024, 0, &~rset);
		if (ret != hrSuccess)
			return hr_lerrf(ret, "QueryRows");
		if (rset.size() == 0)
			break;
		for (unsigned int i = 0; i < rset.size(); ++i) {
			object_ptr<IMessage> msg;
			ret = store->OpenEntry(rset[i].lpProps[0].Value.bin.cb, reinterpret_cast<const ENTRYID *>(rset[i].lpProps[0].Value.bin.lpb),
			      &iid_of(msg), MAPI_DEFERRED_ERRORS, nullptr, &~msg);
			if (ret != hrSuccess)
				return hr_lerrf(ret, "OpenEntry");
			memory_ptr<SPropValue> pv;
			unsigned int pnum = 0;
			ret = HrGetAllProps(msg, 0, &pnum, &~pv);
			if (FAILED(ret))
				return hr_lerrf(ret, "GetAllProps");
		}
		printf("\r\e[2K%zu items...", total += rset.size());
		fflush(stdout);
	} while (true);
	return hrSuccess;
}

static HRESULT adm_perform()
{
	KServerContext srvctx;
	srvctx.m_app_misc = "srvadm";
	if (opt_host == nullptr)
		opt_host = GetServerUnixSocket(adm_config->GetSetting("server_socket"));
	srvctx.m_host = opt_host;
	srvctx.m_ssl_keyfile = adm_config->GetSetting("sslkey_file", "", nullptr);
	srvctx.m_ssl_keypass = adm_config->GetSetting("sslkey_pass", "", nullptr);
	auto ret = srvctx.logon();
	if (ret != hrSuccess)
		return kc_perror("KServerContext::logon", ret);
	if (opt_clear_cache)
		return adm_clear_cache(srvctx.m_svcadm);
	if (opt_purge_deferred)
		return adm_purge_deferred(srvctx.m_svcadm);
	if (opt_purge_softdelete >= 0)
		return adm_purge_softdelete(srvctx.m_svcadm);
	if (opt_ct_read)
		return adm_ct_read(srvctx.m_admstore);
	if (opt_ct_create > 0)
		return adm_ct_create(srvctx.m_admstore, opt_ct_create);
	return MAPI_E_CALL_FAILED;
}

static unsigned int adm_parse_cache(const char *arglist)
{
	if (*arglist == '\0') {
		fprintf(stderr, "No caches were selected, nothing will be done.\n");
		return 0;
	}
	static const std::map<std::string, unsigned int> map = {
		{"all", PURGE_CACHE_ALL},
		{"quota", PURGE_CACHE_QUOTA},
		{"quotadefault", PURGE_CACHE_QUOTADEFAULT},
		{"object", PURGE_CACHE_OBJECTS},
		{"store", PURGE_CACHE_STORES},
		{"acl", PURGE_CACHE_ACL},
		{"cell", PURGE_CACHE_CELL},
		{"index1", PURGE_CACHE_INDEX1},
		{"index2", PURGE_CACHE_INDEX2},
		{"indexedproperty", PURGE_CACHE_INDEXEDPROPERTIES},
		{"userobject", PURGE_CACHE_USEROBJECT},
		{"externid", PURGE_CACHE_EXTERNID},
		{"userdetail", PURGE_CACHE_USERDETAILS},
		{"server", PURGE_CACHE_SERVER},
	};
	unsigned int bits = 0;
	for (const auto &arg : tokenize(arglist, ",")) {
		auto e = map.find(arg);
		if (e == map.cend()) {
			fprintf(stderr, "Unknown cache: \"%s\"\n", arg.c_str());
			return 0;
		}
		bits |= e->second;
	}
	return bits;
}

static bool adm_parse_options(int &argc, const char **&argv)
{
	adm_config.reset(ECConfig::Create(adm_config_defaults));
	adm_config->LoadSettings(ECConfig::GetDefaultPath("admin.cfg"));

	if (HX_getopt(adm_options, &argc, &argv, HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return false;
	if (opt_config_file != nullptr) {
		adm_config->LoadSettings(opt_config_file);
		if (adm_config->HasErrors()) {
			/* Only complain when -c was used */
			fprintf(stderr, "Error reading config file %s\n", opt_config_file);
			LogConfigErrors(adm_config.get());
			return false;
		}
	}
	if (opt_clear_cache != nullptr) {
		opt_cache_bits = adm_parse_cache(opt_clear_cache);
		if (opt_cache_bits == 0)
			return false;
	}
	return true;
}

int main(int argc, const char **argv)
{
	setlocale(LC_ALL, "");
	ec_log_get()->SetLoglevel(EC_LOGLEVEL_INFO);
	if (!adm_parse_options(argc, argv))
		return EXIT_FAILURE;
	return adm_perform() == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}
