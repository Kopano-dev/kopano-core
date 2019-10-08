/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright 2019 Kopano B.V.
 */
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <cstdio>
#include <cstring>
#include <libHX/option.h>
#include <kopano/automapi.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/ecversion.h>
#include <kopano/fileutil.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <kopano/platform.h>
#include <kopano/tie.hpp>
#include <libicalmapi/vcftomapi.hpp>

using namespace KC;

static char *vtm_host, *vtm_user, *vtm_pass, *vtm_sslkey, *vtm_sslpass;
static int vtm_passpr, vtm_sslpr;
static constexpr const struct HXoption vtm_options[] = {
	{"user", 'u', HXTYPE_STRING, &vtm_user, nullptr, nullptr, 0, "User to set out of office message for", "NAME"},
	{nullptr, 'x', HXTYPE_NONE, &vtm_sslpr, nullptr, nullptr, 0, "Prompt for plain password to use for login"},
	{"host", 'h', HXTYPE_STRING, &vtm_host, nullptr, nullptr, 0, "Host to connect with (default: localhost)", "HOSTNAME"},
	{"sslkey-file", 's', HXTYPE_STRING, &vtm_sslkey, nullptr, nullptr, 0, "SSL key file to authenticate as admin", "FILENAME"},
	{"sslkey-pass", 'p', HXTYPE_STRING, &vtm_sslpass, nullptr, nullptr, 0, "Password for the SSL key file", "TEXT"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static std::string vtm_slurp(const char *file)
{
	std::string content;
	if (file == nullptr) {
		HrMapFileToString(stdin, &content);
		return content;
	}
	std::unique_ptr<FILE, file_deleter> fp(fopen(file, "r"));
	if (fp == nullptr) {
		ec_log_err("Could not read %s: %s", file, strerror(errno));
		return content;
	}
	auto ret = HrMapFileToString(fp.get(), &content);
	if (ret != hrSuccess)
		ec_log_err("HrMapFileToString %s: %s", file, GetMAPIErrorMessage(ret));
	return content;
}

static HRESULT vtm_perform(IMsgStore *store, IMessage *msg, std::string &&vcf)
{
	std::unique_ptr<vcftomapi> conv;

	auto ret = create_vcftomapi(store, &unique_tie(conv));
	if (ret != hrSuccess)
		return kc_perrorf("create_vcftomapi", ret);
	if (conv == nullptr)
		return kc_perrorf("create_vcftomapi", MAPI_E_NOT_ENOUGH_MEMORY);
	ret = conv->parse_vcf(std::move(vcf));
	if (ret != hrSuccess)
		return kc_perrorf("parse_vcf", ret);
	ret = conv->get_item(msg);
	if (ret != hrSuccess)
		return kc_perrorf("get_item", ret);
	return hrSuccess;
}

static HRESULT vtm_login(int argc, const char **argv)
{
	AutoMAPI mapi;
	auto ret = mapi.Initialize();
	if (ret != hrSuccess)
		return kc_perror("MAPIInitialize", ret);
	object_ptr<IMAPISession> ses;
	if (vtm_pass != nullptr || (vtm_sslkey != nullptr && vtm_sslpass != nullptr))
		ret = HrOpenECSession(&~ses, PROJECT_VERSION, "vtm", vtm_user,
		      vtm_pass, vtm_host, EC_PROFILE_FLAGS_NO_NOTIFICATIONS,
		      vtm_sslkey, vtm_sslpass);
	else
		ret = HrOpenECSession(&~ses, PROJECT_VERSION, "vtm",
		      KOPANO_SYSTEM_USER, KOPANO_SYSTEM_USER, vtm_host,
		      EC_PROFILE_FLAGS_NO_NOTIFICATIONS, vtm_sslkey,
		      vtm_sslpass);
	if (ret != hrSuccess)
		return kc_perror("OpenECSession", ret);
	object_ptr<IMsgStore> store;
	ret = HrOpenDefaultStore(ses, &~store);
	if (ret != hrSuccess)
		return kc_perror("HrOpenDefaultStore", ret);
	object_ptr<IMAPIFolder> root;
	unsigned int type = 0;
	ret = store->OpenEntry(0, nullptr, &iid_of(root), 0, &type, &~root);
	if (ret != hrSuccess)
		return kc_perror("open root", ret);
	memory_ptr<SPropValue> contacts;
	ret = HrGetOneProp(root, PR_IPM_CONTACT_ENTRYID, &~contacts);
	if (ret != hrSuccess)
		return kc_perror("getoneprop", ret);
	ret = store->OpenEntry(contacts->Value.bin.cb, reinterpret_cast<ENTRYID *>(contacts->Value.bin.lpb),
	      &iid_of(root), MAPI_MODIFY, &type, &~root);
	if (ret != hrSuccess)
		return kc_perror("open contacts", ret);

	if (argc == 0) {
		/* Read one from stdin */
		object_ptr<IMessage> msg;
		ret = root->CreateMessage(nullptr, MAPI_DEFERRED_ERRORS, &~msg);
		if (ret != hrSuccess)
			return kc_perror("create contact", ret);
		auto vcf = vtm_slurp(nullptr);
		if (vcf.empty())
			return hrSuccess;
		ret = vtm_perform(store, msg, std::move(vcf));
		if (ret != hrSuccess)
			return kc_perror("vtm_perform", ret);
		msg->SaveChanges(0);
		root->SaveChanges(0);
		return hrSuccess;
	}

	while (argc-- > 0) {
		object_ptr<IMessage> msg;
		ret = root->CreateMessage(nullptr, MAPI_DEFERRED_ERRORS, &~msg);
		if (ret != hrSuccess)
			return kc_perror("create contact", ret);
		auto vcf = vtm_slurp(*argv++);
		ret = vtm_perform(store, msg, std::move(vcf));
		if (ret != hrSuccess)
			return kc_perror("vtm_perform", ret);
		msg->SaveChanges(0);
		root->SaveChanges(0);
	}
	return hrSuccess;
}

static HRESULT vtm_parse_options(int &argc, const char **&argv)
{
	if (HX_getopt(vtm_options, &argc, &argv, HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return MAPI_E_CALL_FAILED;
	if (vtm_user == nullptr) {
		fprintf(stderr, "No username specified.\n");
		return MAPI_E_CALL_FAILED;
	}
	char *p = nullptr;
	p = vtm_passpr ? get_password("Login password: ") : getenv("PASSWORD");
	if (p != nullptr)
		vtm_pass = strdup(p);
	p = vtm_sslpr ? get_password("SSL keyfile password: ") :
	    vtm_sslpass ? vtm_sslpass : getenv("SSLKEY_PASSWORD");
	if (p != nullptr)
		vtm_sslpass = strdup(p);
	return hrSuccess;
}

int main(int argc, const char **argv) try
{
	setlocale(LC_ALL, "");
	auto ret = vtm_parse_options(argc, argv);
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	return vtm_login(--argc, ++argv) == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
} catch (...) {
	std::terminate();
}
