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
#include "ICalToMAPI.h"

using namespace KC;

enum {
	VTM_CALENDAR = 1,
	VTM_CONTACT,
};

static char *vtm_host, *vtm_user, *vtm_pass, *vtm_sslkey, *vtm_sslpass;
static int vtm_passpr, vtm_sslpr;
static unsigned int vtm_mode;
static constexpr const struct HXoption vtm_options[] = {
	{"calendar", 0, HXTYPE_VAL, &vtm_mode, nullptr, nullptr, VTM_CALENDAR, "Import calendar object (.ics file)"},
	{"contact", 0, HXTYPE_VAL, &vtm_mode, nullptr, nullptr, VTM_CONTACT, "Import contact object (.vcf file)"},
	{"user", 'u', HXTYPE_STRING, &vtm_user, nullptr, nullptr, 0, "User to set out of office message for", "NAME"},
	{nullptr, 'x', HXTYPE_NONE, &vtm_sslpr, nullptr, nullptr, 0, "Prompt for plain password to use for login"},
	{"host", 'h', HXTYPE_STRING, &vtm_host, nullptr, nullptr, 0, "Host to connect with (default: localhost)", "HOSTNAME"},
	{"sslkey-file", 's', HXTYPE_STRING, &vtm_sslkey, nullptr, nullptr, 0, "SSL key file to authenticate as admin", "FILENAME"},
	{"sslkey-pass", 'p', HXTYPE_STRING, &vtm_sslpass, nullptr, nullptr, 0, "Password for the SSL key file", "TEXT"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static int vtm_open_contacts(IMsgStore *store, IMAPIFolder **fld)
{
	object_ptr<IMAPIFolder> root;
	unsigned int type = 0;
	auto ret = store->OpenEntry(0, nullptr, &iid_of(root), 0, &type, &~root);
	if (ret != hrSuccess)
		return kc_perror("open root", ret);
	memory_ptr<SPropValue> contacts;
	ret = HrGetOneProp(root, PR_IPM_CONTACT_ENTRYID, &~contacts);
	if (ret != hrSuccess)
		return kc_perror("getoneprop", ret);
	ret = store->OpenEntry(contacts->Value.bin.cb, reinterpret_cast<ENTRYID *>(contacts->Value.bin.lpb),
	      &iid_of(*fld), MAPI_MODIFY, &type, &~root);
	if (ret != hrSuccess)
		return kc_perror("open contacts", ret);
	*fld = root.release();
	return hrSuccess;
}

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

static HRESULT vtm_import_calendar(IAddrBook *abk, IMAPIFolder *fld,
    std::string &&ics)
{
	std::unique_ptr<ICalToMapi> conv;
	size_t items = 0;

	auto ret = CreateICalToMapi(fld, abk, true, &unique_tie(conv));
	if (ret != hrSuccess)
		return kc_perrorf("CreateIcalToMapi", ret);
	if (conv == nullptr)
		return kc_perrorf("CreateIcalToMapi", MAPI_E_NOT_ENOUGH_MEMORY);
	ret = conv->ParseICal2(ics.c_str(), "UTF-8", "UTC", nullptr, 0);
	if (ret != hrSuccess)
		return kc_perrorf("ParseIcal", ret);

	for (size_t i = 0; i < conv->GetItemCount(); ++i) {
		object_ptr<IMessage> msg;
		ret = fld->CreateMessage(nullptr, MAPI_DEFERRED_ERRORS, &~msg);
		if (ret != hrSuccess)
			return kc_perror("create event", ret);
		ret = conv->GetItem(i, 0, msg);
		if (ret != hrSuccess)
			return kc_perror("get_item", ret);
		ret = msg->SaveChanges(0);
		if (ret != hrSuccess)
			kc_perror("SaveChanges(event)", ret);
		else
			++items;
	}
	printf("Processed %u blocks, imported %zu events.\n",
	       conv->GetItemCount(), items);
	return hrSuccess;
}

static HRESULT vtm_import_contact(IMAPIFolder *fld, std::string &&vcf)
{
	std::unique_ptr<vcftomapi> conv;
	size_t items = 0;

	auto ret = create_vcftomapi(fld, &unique_tie(conv));
	if (ret != hrSuccess)
		return kc_perrorf("create_vcftomapi", ret);
	if (conv == nullptr)
		return kc_perrorf("create_vcftomapi", MAPI_E_NOT_ENOUGH_MEMORY);
	ret = conv->parse_vcf(std::move(vcf));
	if (ret != hrSuccess)
		return kc_perrorf("parse_vcf", ret);
	for (size_t i = 0; i < conv->get_item_count(); ++i) {
		object_ptr<IMessage> msg;
		ret = fld->CreateMessage(nullptr, MAPI_DEFERRED_ERRORS, &~msg);
		if (ret != hrSuccess)
			return kc_perror("create contact", ret);
		ret = conv->get_item(msg, i);
		if (ret != hrSuccess) {
			kc_perrorf("get_item", ret);
			continue;
		}
		ret = msg->SaveChanges(0);
		if (ret != hrSuccess)
			kc_perror("SaveChanges(contact)", ret);
		++items;
	}
	printf("Processed %zu blocks, imported %zu contacts.\n",
	       conv->get_item_count(), items);
	return hrSuccess;
}

static HRESULT vtm_perform(IAddrBook *abk, IMAPIFolder *fld,
    std::string &&vcf)
{
	return vtm_mode == VTM_CALENDAR ?
	       vtm_import_calendar(abk, fld, std::move(vcf)) :
	       vtm_import_contact(fld, std::move(vcf));
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
	object_ptr<IAddrBook> abk;
	if (vtm_mode == VTM_CALENDAR) {
		ret = ses->OpenAddressBook(0, nullptr, AB_NO_DIALOG, &~abk);
		if (ret != hrSuccess)
			return kc_perror("OpenAddressBook", ret);
		ret = HrOpenDefaultCalendar(store, &~root);
		if (ret != hrSuccess)
			return kc_perror("OpenDefaultCalendar", ret);
	} else {
		ret = vtm_open_contacts(store, &~root);
		if (ret != hrSuccess)
			return kc_perror("vtm_open_contacts", ret);
	}

	if (argc == 0) {
		/* Read one from stdin */
		auto vcf = vtm_slurp(nullptr);
		if (vcf.empty())
			return hrSuccess;
		ret = vtm_perform(abk, root, std::move(vcf));
		if (ret != hrSuccess)
			return kc_perror("vtm_perform", ret);
		ret = root->SaveChanges(0);
		if (ret != hrSuccess)
			return kc_perror("SaveChanges(folder)", ret);
		return hrSuccess;
	}

	while (argc-- > 0) {
		auto vcf = vtm_slurp(*argv++);
		ret = vtm_perform(abk, root, std::move(vcf));
		if (ret != hrSuccess)
			return kc_perror("vtm_perform", ret);
		ret = root->SaveChanges(0);
		if (ret != hrSuccess)
			return kc_perror("SaveChanges(folder)", ret);
	}
	return hrSuccess;
}

static HRESULT vtm_parse_options(int &argc, const char **&argv)
{
	if (HX_getopt(vtm_options, &argc, &argv, HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return MAPI_E_CALL_FAILED;
	if (vtm_mode != VTM_CALENDAR && vtm_mode != VTM_CONTACT) {
		fprintf(stderr, "Must specify one of --calendar or --contact\n");
		return MAPI_E_CALL_FAILED;
	}
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
