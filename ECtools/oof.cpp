/*
 * Copyright 2018+, Kopano and its licensors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <memory>
#include <string>
#include <cerrno>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <popt.h>
#include <json/writer.h>
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
static const struct poptOption oof_options[] = {
	{"user", 'u', POPT_ARG_STRING, &oof_user, 0, "User to set out of office message for"},
	{nullptr, 'x', POPT_ARG_NONE, &oof_sslpr, 0, "Prompt for plain password to use for login"},
	{"mode", 'm', POPT_ARG_INT, &oof_mode, 0, "0 to disable out of office, 1 to enable"},
	{"from", 0, POPT_ARG_STRING, &oof_from, 0, "Date/time (Y-m-d H:M) when OOF should become active"},
	{"until", 0, POPT_ARG_STRING, &oof_until, 0, "Date/time or \"infinity\" when OOF should become inactive again"},
	{"subject", 't', POPT_ARG_STRING, &oof_subject, 0, "The subject to be set in the OOF message"},
	{"message", 'n', POPT_ARG_STRING, &oof_msgfile, 0, "text file containing the body of the message"},
	{"host", 'h', POPT_ARG_STRING, &oof_host, 0, "Host to connect with (default: localhost)"},
	{"sslkey-file", 's', POPT_ARG_STRING, &oof_sslkey, 0, "SSL key file to authenticate as admin"},
	{"sslkey-pass", 'p', POPT_ARG_STRING, &oof_sslpass, 0, "Password for the SSL key file"},
	{nullptr, 'P', POPT_ARG_NONE, &oof_sslpr, 0, "Prompt for SSL key password"},
	{"dump-json", 0, POPT_ARG_STRING, nullptr, 0, "(Option is ignored for compatibility)"},
	POPT_AUTOHELP
	{nullptr}
};

static bool oof_infinite(const char *t)
{
	return t != nullptr && strcmp(t, "infinite") == 0;
}

static HRESULT oof_parse_options(int &argc, char **&argv)
{
	auto optctx = poptGetContext(nullptr, argc, const_cast<const char **>(argv), oof_options, 0);
	while (poptGetNextOpt(optctx) >= 0)
		/* just auto-parse */;
	if (argc < 2 || oof_user == nullptr) {
		fprintf(stderr, "No username specified.\n");
		poptPrintUsage(optctx, stderr, 0);
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
		auto ret = HrMapFileToString(fp.get(), &msg, nullptr);
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

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	auto ret = oof_parse_options(argc, argv);
	if (ret != hrSuccess)
		return EXIT_FAILURE;
	return oof_login() == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}
