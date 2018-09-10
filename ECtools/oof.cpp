/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2018+, Kopano and its licensors
 */
#include <exception>
#include <memory>
#include <string>
#include <cerrno>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <json/writer.h>
#include <libHX/option.h>
#include <kopano/automapi.hpp>
#include <kopano/charset/convert.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/ecversion.h>
#include <kopano/hl.hpp>
#include <kopano/IECInterfaces.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include "common/fileutil.h"

using namespace KC;

static char *oof_from, *oof_until, *oof_subject, *oof_msgfile;
static char *oof_host, *oof_user, *oof_pass, *oof_sslkey, *oof_sslpass;
static struct tm oof_fromtm, oof_untiltm;
static int oof_mode = -1, oof_passpr, oof_sslpr;
static constexpr const struct HXoption oof_options[] = {
	{"user", 'u', HXTYPE_STRING, &oof_user, nullptr, nullptr, 0, "User to set out of office message for", "NAME"},
	{nullptr, 'x', HXTYPE_NONE, &oof_sslpr, nullptr, nullptr, 0, "Prompt for plain password to use for login"},
	{"mode", 'm', HXTYPE_INT, &oof_mode, nullptr, nullptr, 0, "0 to disable out of office, 1 to enable", "INT"},
	{"from", 0, HXTYPE_STRING, &oof_from, nullptr, nullptr, 0, "Date/time (Y-m-d H:M) when OOF should become active", "TIMESPEC"},
	{"until", 0, HXTYPE_STRING, &oof_until, nullptr, nullptr, 0, "Date/time or \"infinity\" when OOF should become inactive again", "TIMESPEC"},
	{"subject", 't', HXTYPE_STRING, &oof_subject, nullptr, nullptr, 0, "The subject to be set in the OOF message", "TEXT"},
	{"message", 'n', HXTYPE_STRING, &oof_msgfile, nullptr, nullptr, 0, "text file containing the body of the message", "FILENAME"},
	{"host", 'h', HXTYPE_STRING, &oof_host, nullptr, nullptr, 0, "Host to connect with (default: localhost)", "HOSTNAME"},
	{"sslkey-file", 's', HXTYPE_STRING, &oof_sslkey, nullptr, nullptr, 0, "SSL key file to authenticate as admin", "FILENAME"},
	{"sslkey-pass", 'p', HXTYPE_STRING, &oof_sslpass, nullptr, nullptr, 0, "Password for the SSL key file", "TEXT"},
	{nullptr, 'P', HXTYPE_NONE, &oof_sslpr, nullptr, nullptr, 0, "Prompt for SSL key password"},
	{"dump-json", 0, HXTYPE_STRING, nullptr, nullptr, nullptr, 0, "(Option is ignored for compatibility)", "VALUE"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static bool oof_infinite(const char *t)
{
	return t != nullptr && strcmp(t, "infinite") == 0;
}

static HRESULT oof_parse_options(int &argc, const char **&argv)
{
	if (HX_getopt(oof_options, &argc, &argv, HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return MAPI_E_CALL_FAILED;
	if (argc < 2 || oof_user == nullptr) {
		fprintf(stderr, "No username specified.\n");
		return -1;
	}
	static constexpr const char formula[] = "%Y-%m-%d %H:%M"; /* ISO 8601 */
	if (oof_from != nullptr && !oof_infinite(oof_from)) {
		auto p = strptime(oof_from, formula, &oof_fromtm);
		if (p == nullptr || *p != '\0') {
			fprintf(stderr, "Time specification \"%s\" does not match pattern \"%s\"\n", oof_from, formula);
			return -1;
		}
	}
	if (oof_until != nullptr && !oof_infinite(oof_until)) {
		auto p = strptime(oof_until, formula, &oof_untiltm);
		if (p == nullptr || *p != '\0') {
			fprintf(stderr, "Time specification \"%s\" does not match pattern \"%s\"\n", oof_until, formula);
			return -1;
		}
	}
	char *p = nullptr;
	p = oof_passpr ? get_password("Login password: ") : getenv("OOF_PASSWORD");
	if (p != nullptr)
		oof_pass = strdup(p);
	p = oof_sslpr ? get_password("SSL keyfile password: ") :
	    oof_sslpass ? oof_sslpass : getenv("OOF_SSLKEY_PASSWORD");
	if (p != nullptr)
		oof_sslpass = strdup(p);
	return hrSuccess;
}

static int oof_show(IMsgStore *store)
{
	static constexpr const SizedSPropTagArray(5, proplist) =
		{5, {PR_EC_OUTOFOFFICE, PR_EC_OUTOFOFFICE_FROM, PR_EC_OUTOFOFFICE_UNTIL,
		PR_EC_OUTOFOFFICE_SUBJECT, PR_EC_OUTOFOFFICE_MSG}};
	ULONG nprops = 0;
	memory_ptr<SPropValue> props;
	auto ret = store->GetProps(proplist, 0, &nprops, &~props);
	if (FAILED(ret) || nprops != 5)
		return kc_perror("GetProps", ret);

	std::string subject, message;
	Json::Value root;
	auto dump = make_scope_success([&]() { puts(Json::writeString(Json::StreamWriterBuilder(), root).c_str()); });
	if (props[0].ulPropTag != PR_EC_OUTOFOFFICE) {
		root["set"] = Json::Value::null;
		return hrSuccess;
	}
	root["set"] = static_cast<bool>(props[0].Value.b);

	char tbuf[64];
	bool active = props[0].Value.b;
	time_t now = time(nullptr), ts;
	if (props[1].ulPropTag != PR_EC_OUTOFOFFICE_FROM) {
		root["from"] = "infinite";
	} else {
		if (FileTimeToTimestamp(props[1].Value.ft, ts, tbuf, sizeof(tbuf)) < 0)
			root["from"] = Json::Value::null;
		else
			root["from"] = tbuf;
		active &= ts <= now;
	}
	if (props[2].ulPropTag != PR_EC_OUTOFOFFICE_UNTIL) {
		root["until"] = "infinite";
	} else {
		if (FileTimeToTimestamp(props[2].Value.ft, ts, tbuf, sizeof(tbuf)) < 0)
			root["until"] = Json::Value::null;
		else
			root["until"] = tbuf;
		active &= now <= ts;
	}
	/* whether dagent/spooler would consider OOF active at this very moment */
	root["active"] = active;

	if (props[3].ulPropTag == PR_EC_OUTOFOFFICE_SUBJECT) {
		subject = convert_to<std::string>("UTF-8", props[3].Value.lpszW, rawsize(props[3].Value.lpszW), CHARSET_WCHAR);
		root["subject"] = subject.c_str();
	}
	if (props[4].ulPropTag == PR_EC_OUTOFOFFICE_MSG) {
		message = convert_to<std::string>("UTF-8", props[4].Value.lpszW, rawsize(props[4].Value.lpszW), CHARSET_WCHAR);
		root["message"] = message.c_str();
	}
	return hrSuccess;
}

static int oof_delete(IMsgStore *store)
{
	SizedSPropTagArray(2, tags) = {0};
	if (oof_infinite(oof_from))
		tags.aulPropTag[tags.cValues++] = PR_EC_OUTOFOFFICE_FROM;
	if (oof_infinite(oof_until))
		tags.aulPropTag[tags.cValues++] = PR_EC_OUTOFOFFICE_UNTIL;
	auto ret = store->DeleteProps(tags, nullptr);
	if (ret != hrSuccess)
		return kc_perror("DeleteProps", ret);
	return ret;
}

static int oof_set(IMsgStore *store)
{
	KPropbuffer<5> pv;
	unsigned int c = 0;
	if (oof_mode >= 0) {
		pv[c].ulPropTag = PR_EC_OUTOFOFFICE;
		pv[c++].Value.b = oof_mode > 0;
	}
	if (oof_from != nullptr) {
		pv[c].ulPropTag  = PR_EC_OUTOFOFFICE_FROM;
		pv[c++].Value.ft = UnixTimeToFileTime(mktime(&oof_fromtm));
	}
	if (oof_until != nullptr) {
		pv[c].ulPropTag  = PR_EC_OUTOFOFFICE_UNTIL;
		pv[c++].Value.ft = UnixTimeToFileTime(mktime(&oof_untiltm));
	}
	if (oof_subject != nullptr)
		pv.set(c++, PR_EC_OUTOFOFFICE_SUBJECT, convert_to<std::wstring>(oof_subject));
	if (oof_msgfile != nullptr) {
		std::unique_ptr<FILE, file_deleter> fp(fopen(oof_msgfile, "r"));
		if (fp == nullptr) {
			fprintf(stderr, "Cannot open %s: %s", oof_msgfile, strerror(errno));
			return MAPI_E_CALL_FAILED;
		}
		std::string msg;
		auto ret = HrMapFileToString(fp.get(), &msg);
		if (ret != hrSuccess)
			return kc_perror("HrMapFileToString", ret);
		pv.set(c++, PR_EC_OUTOFOFFICE, convert_to<std::wstring>(msg));
	}
	if (c == 0)
		return hrSuccess;
	auto ret = store->SetProps(c, pv.get(), nullptr);
	if (ret != hrSuccess)
		return kc_perror("SetProps", ret);
	return oof_delete(store);
}

static HRESULT oof_login()
{
	AutoMAPI mapi;
	auto ret = mapi.Initialize();
	if (ret != hrSuccess)
		return kc_perror("MAPIInitialize", ret);
	object_ptr<IMAPISession> ses;
	if (oof_pass != nullptr || (oof_sslkey != nullptr && oof_sslpass != nullptr))
		ret = HrOpenECSession(&~ses, "oof", PROJECT_VERSION, oof_user,
		      oof_pass, oof_host, EC_PROFILE_FLAGS_NO_NOTIFICATIONS,
		      oof_sslkey, oof_sslpass);
	else
		ret = HrOpenECSession(&~ses, "oof", PROJECT_VERSION,
		      KOPANO_SYSTEM_USER, KOPANO_SYSTEM_USER, oof_host,
		      EC_PROFILE_FLAGS_NO_NOTIFICATIONS, oof_sslkey,
		      oof_sslpass);
	if (ret != hrSuccess)
		return kc_perror("OpenECSession", ret);
	object_ptr<IMsgStore> adm_store;
	ret = HrOpenDefaultStore(ses, &~adm_store);
	if (ret != hrSuccess)
		return kc_perror("HrOpenDefaultStore", ret);
	memory_ptr<SPropValue> props;
	ret = HrGetOneProp(adm_store, PR_EC_OBJECT, &~props);
	if (ret != hrSuccess)
		return kc_perror("HrGetOneProp PR_EC_OBJECT", ret);
	object_ptr<IUnknown> ecobj;
	ecobj.reset(reinterpret_cast<IUnknown *>(props->Value.lpszA));
	object_ptr<IECServiceAdmin> ecadm;
	ret = ecobj->QueryInterface(IID_IECServiceAdmin, &~ecadm);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface IECServiceAdmin", ret);
	ULONG xid_size = 0;
	memory_ptr<ENTRYID> xid;
	ret = ecadm->ResolveUserName(LPTSTR(oof_user), 0, &xid_size, &~xid);
	if (ret != hrSuccess)
		return kc_perror("ResolveUserName", ret);
	memory_ptr<ECUSER> ecuser;
	ret = ecadm->GetUser(xid_size, xid, 0, &~ecuser);
	if (ret != hrSuccess)
		return kc_perror("GetUser", ret);
	object_ptr<IExchangeManageStore> mgt;
	ret = ecobj->QueryInterface(IID_IExchangeManageStore, &~mgt);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface IExchangeManageStore", ret);
	ret = mgt->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(""), ecuser->lpszUsername, 0, &xid_size, &~xid);
	if (ret != hrSuccess)
		return kc_perror("User has no store?! CreateStoreEntryId", ret);
	object_ptr<IMsgStore> usr_store;
	ret = ses->OpenMsgStore(0, xid_size, xid, &iid_of(usr_store), MDB_WRITE, &~usr_store);
	if (ret != hrSuccess)
		return kc_perror("OpenMsgStore", ret);
	ret = oof_set(usr_store);
	if (ret != hrSuccess)
		return ret;
	return oof_show(usr_store);
}

int main(int argc, const char **argv) try
{
	setlocale(LC_ALL, "");
	auto ret = oof_parse_options(argc, argv);
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	return oof_login() == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
} catch (...) {
	std::terminate();
}
