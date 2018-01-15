/*
 * Copyright 2005-2016 Zarafa and its licensors
 * Copyright 2018, Kopano and its licensors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 3 of the License.
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
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <mapidefs.h>
#include <popt.h>
#include <kopano/automapi.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/ECABEntryID.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ECRestriction.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <kopano/memory.hpp>
#include <kopano/charset/convert.h>
#include "ConsoleTable.h"
#include "kcore.hpp"

using namespace KCHL;

static int opt_create_store, opt_create_public, opt_detach_store;
static const char *opt_attach_store, *opt_remove_store;
static const char *opt_config_file, *opt_host;
static const char *opt_entity_name, *opt_entity_type;
static const char *opt_companyname;
static std::unique_ptr<ECConfig> adm_config;

static constexpr const struct poptOption adm_options[] = {
	{nullptr, 'A', POPT_ARG_STRING, &opt_attach_store, 0, "Attach an orphaned store by GUID to a user account (with -n)"},
	{nullptr, 'C', POPT_ARG_NONE, &opt_create_store, 0, "Create a store and attach it to a user account (with -n)"},
	{nullptr, 'D', POPT_ARG_NONE, &opt_detach_store, 0, "Detach a user's store (with -n) and make it orphan"},
	{nullptr, 'P', POPT_ARG_NONE, &opt_create_public, 0, "Create a public store"},
	{nullptr, 'R', POPT_ARG_STRING, &opt_remove_store, 0, "Remove an orphaned store by GUID"},
	{nullptr, 'c', POPT_ARG_STRING, &opt_config_file, 'c', "Specify alternate config file"},
	{nullptr, 'h', POPT_ARG_STRING, &opt_host, 0, "URI for server"},
	{nullptr, 'k', POPT_ARG_STRING, &opt_companyname, 0, "Name of the company for creating a public store in a multi-tenant setup"},
	{nullptr, 'n', POPT_ARG_STRING, &opt_entity_name, 0, "User/group/company account to work on for -A,-C,-D"},
	{nullptr, 't', POPT_ARG_STRING, &opt_entity_type, 0, "Store type for the -n argument (user, archive, group, company)"},
	POPT_AUTOHELP
	{nullptr}
};

static constexpr const configsetting_t adm_config_defaults[] = {
	{"server_socket", "default:"},
	{"sslkey_file", ""},
	{"sslkey_pass", ""},
	{nullptr},
};

static HRESULT adm_hex2bin(const char *x, GUID &out)
{
	auto s = hex2bin(x);
	if (s.size() != sizeof(out)) {
		ec_log_err("GUID must be exactly %zu bytes long (%zu characters in hex representation)",
			sizeof(out), 2 * sizeof(out));
		return MAPI_E_INVALID_PARAMETER;
	}
	memcpy(&out, s.c_str(), s.size());
	return hrSuccess;
}

static HRESULT adm_resolve_entity(IECServiceAdmin *svcadm,
    unsigned int &store_type, ULONG &eid_size, memory_ptr<ENTRYID> &eid)
{
	auto entity = reinterpret_cast<const TCHAR *>(opt_entity_name);
	if (opt_entity_type == nullptr)
		opt_entity_type = "user";
	HRESULT ret = hrSuccess;
	if (strcmp(opt_entity_type, "user") == 0) {
		store_type = ECSTORE_TYPE_PRIVATE;
		ret = svcadm->ResolveUserName(entity, 0, &eid_size, &~eid);
	} else if (strcmp(opt_entity_type, "archive") == 0) {
		store_type = ECSTORE_TYPE_ARCHIVE;
		ret = svcadm->ResolveUserName(entity, 0, &eid_size, &~eid);
	} else if (strcmp(opt_entity_type, "group") == 0) {
		store_type = ECSTORE_TYPE_PUBLIC;
		ret = svcadm->ResolveGroupName(entity, 0, &eid_size, &~eid);
	} else if (strcmp(opt_entity_type, "company") == 0) {
		store_type = ECSTORE_TYPE_PUBLIC;
		ret = svcadm->ResolveCompanyName(entity, 0, &eid_size, &~eid);
	} else {
		fprintf(stderr, "Unknown store type \"%s\"\n", opt_entity_type);
		return MAPI_E_CALL_FAILED;
	}
	if (ret != hrSuccess)
		ec_log_err("Unable to find store for %s \"%s\": %s",
			opt_entity_type, opt_entity_name, GetMAPIErrorMessage(ret));
	return ret;
}

/**
 * Get the public store from a company or just the default public store. If a
 * company name is given it will try to open the companies store. if it fails
 * it will not fall back to the default store.
 *
 * @ses:	Current MAPI session.
 * @store:	Random store for opening the ExchangeManageStore object.
 * @company:	Owner of the public store. If empty, opens the default public store.
 * @pub:	Pointer to the public store
 */
static HRESULT adm_get_public_store(IMAPISession *ses,
    IMsgStore *store, const std::wstring &company, IMsgStore **pub)
{
	if (company.empty())
		return HrOpenECPublicStore(ses, pub);
	object_ptr<IExchangeManageStore> ms;
	auto ret = store->QueryInterface(IID_IExchangeManageStore, &~ms);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface", ret);
	ULONG eid_size = 0;
	memory_ptr<ENTRYID> eid;
	ret = ms->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(L""),
	      reinterpret_cast<const TCHAR *>(company.c_str()),
	      MAPI_UNICODE, &eid_size, &~eid);
	if (ret != hrSuccess)
		return ret;
	return ses->OpenMsgStore(0, eid_size, eid, &iid_of(*pub), MDB_WRITE, pub);
}

static HRESULT adm_hook_check_server(IECServiceAdmin *svcadm,
    unsigned int user_size, const ENTRYID *user_eid)
{
	if (strcmp(opt_entity_type, "user") != 0)
		return hrSuccess;
	/* Check if this user should exist on the connected server */
	memory_ptr<ECUSER> user;
	auto ret = svcadm->GetUser(user_size, user_eid, 0, &~user);
	if (ret != hrSuccess)
		return kc_perror("Unable to load details with GetUser", ret);

	/* Home server on single server installations is empty */
	if (user->lpszServername == nullptr ||
	    *reinterpret_cast<TCHAR *>(user->lpszServername) == '\0')
		return hrSuccess;
	/* GetServerDetails uses AllocateMore, so this needs MAPIAllocate. */
	memory_ptr<ECSVRNAMELIST> srvlist;
	ret = MAPIAllocateBuffer(sizeof(ECSVRNAMELIST), &~srvlist);
	if (ret != hrSuccess)
		return kc_perror("MAPIAllocate", ret);
	ret = MAPIAllocateMore(sizeof(TCHAR *), srvlist, reinterpret_cast<void **>(&srvlist->lpszaServer));
	if (ret != hrSuccess)
		return kc_perror("MAPIAllocate", ret);
	srvlist->cServers = 1;
	srvlist->lpszaServer[0] = user->lpszServername;
	memory_ptr<ECSERVERLIST> details;
	ret = svcadm->GetServerDetails(srvlist, 0, &~details);
	if (ret != hrSuccess) {
		ec_log_err("Unable to load server details for \"%s\": %s",
			reinterpret_cast<const char *>(user->lpszServername), GetMAPIErrorMessage(ret));
		return ret;
	}
	if (!(details->lpsaServer[0].ulFlags & EC_SDFLAG_IS_PEER))
		/* Since we do not know which server is connected, do not print a server name. */
		ec_log_warn("Hooking store of non-homeserver of \"%s\"", opt_entity_name);
	return hrSuccess;
}

static HRESULT adm_hook_to_normal(IECServiceAdmin *svcadm, const GUID &guid)
{
	unsigned int store_type = 0;
	ULONG user_size = 0;
	memory_ptr<ENTRYID> user_eid;
	HRESULT ret = hrSuccess;

	ret = adm_resolve_entity(svcadm, store_type, user_size, user_eid);
	if (ret != hrSuccess)
		return ret;
	ret = adm_hook_check_server(svcadm, user_size, user_eid);
	if (ret != hrSuccess)
		return ret;
	/* The server will not let you hook public stores to users and vice-versa. */
	ret = svcadm->HookStore(store_type, user_size, user_eid, &guid);
	if (ret != hrSuccess)
		return kc_perror("Unable to hook store", ret);
	printf("The store has been hooked.\n");
	return hrSuccess;
}

static HRESULT adm_attach_store(KServerContext &kadm, const char *hexguid)
{
	GUID binguid;
	auto ret = adm_hex2bin(hexguid, binguid);
	if (ret != hrSuccess)
		return ret;
	return adm_hook_to_normal(kadm.m_svcadm, binguid);
}

static HRESULT adm_detach_store(KServerContext &kadm)
{
	unsigned int store_type = 0;
	ULONG user_size = 0, unwrap_size = 0;
	memory_ptr<ENTRYID> user_eid, unwrap_eid;
	auto ret = adm_resolve_entity(kadm.m_svcadm, store_type, user_size, user_eid);
	if (ret != hrSuccess)
		return ret;

	if (store_type == ECSTORE_TYPE_PUBLIC) {
		/*
		 * ns__resolveUserStore (CreateStoreEntryID) does not work with
		 * normal (non-company) public store.
		 */
		object_ptr<IMsgStore> pub_store;
		std::wstring company;
		if (opt_companyname != nullptr)
			company = convert_to<std::wstring>(opt_companyname);
		ret = adm_get_public_store(kadm.m_session, kadm.m_admstore, company, &~pub_store);
		if (ret != hrSuccess)
			return kc_perror("Unable to open public store", ret);
		memory_ptr<SPropValue> pv;
		ret = HrGetOneProp(pub_store, PR_STORE_ENTRYID, &~pv);
		if (ret != hrSuccess)
			return kc_perror("Unable to get public store entryid", ret);
		ret = UnWrapStoreEntryID(pv->Value.bin.cb, reinterpret_cast<ENTRYID *>(pv->Value.bin.lpb), &unwrap_size, &~unwrap_eid);
		if (ret != hrSuccess)
			return kc_perror("Unable to unhook store. Unable to unwrap the store entryid", ret);
	} else {
		ULONG cbstore = 0;
		memory_ptr<ENTRYID> lpstore;
		if (store_type == ECSTORE_TYPE_ARCHIVE) {
			ret = kadm.m_svcadm->GetArchiveStoreEntryID(reinterpret_cast<const TCHAR *>(opt_entity_name), nullptr, 0, &cbstore, &~lpstore);
			if (ret != hrSuccess)
				return kc_perror("Unable to retrieve store entryid", ret);
		} else {
			object_ptr<IExchangeManageStore> ms;
			auto ret = kadm.m_admstore->QueryInterface(IID_IExchangeManageStore, &~ms);
			if (ret != hrSuccess)
				return kc_perror("QueryInterface", ret);
			/*
			 * Do not redirect to another server, unhook works on
			 * the server it is connected to.
			 */
			ret = ms->CreateStoreEntryID(nullptr, reinterpret_cast<const TCHAR *>(opt_entity_name), OPENSTORE_OVERRIDE_HOME_MDB, &cbstore, &~lpstore);
			if (ret == MAPI_E_NOT_FOUND) {
				fprintf(stderr, "Unable to unhook store. User \"%s\" has no store attached.\n", opt_entity_name);
				return ret;
			}
			if (ret != hrSuccess)
				return kc_perror("Unable to unhook store. Can not create store entryid", ret);
		}
		ret = UnWrapStoreEntryID(cbstore, lpstore, &unwrap_size, &~unwrap_eid);
		if (ret != hrSuccess)
			return kc_perror("UnwrapStoreEntryID", ret);
	}

	ret = kadm.m_svcadm->UnhookStore(store_type, user_size, user_eid);
	if (ret != hrSuccess)
		return kc_perror("Unable to unhook store", ret);
	printf("The store has been unhooked.\nStore GUID is %s\n",
	       strToLower(bin2hex(sizeof(GUID), unwrap_eid->ab)).c_str());
	return hrSuccess;
}

static HRESULT adm_create_store(IECServiceAdmin *svcadm)
{
	ULONG user_size = 0, store_size = 0, root_size = 0;
	memory_ptr<ENTRYID> user_eid, store_eid, root_fld;
	auto ret = svcadm->ResolveUserName(reinterpret_cast<const TCHAR *>(opt_entity_name), 0, &user_size, &~user_eid);
	if (ret != hrSuccess)
		return kc_perror("Failed to resolve user", ret);
	ret = svcadm->CreateStore(ECSTORE_TYPE_PRIVATE, user_size, user_eid,
	      &store_size, &~store_eid, &root_size, &~root_fld);
	if (ret == MAPI_E_COLLISION)
		return kc_perror("Public store already exists", ret);
	if (ret != hrSuccess)
		return kc_perror("Unable to create store", ret);
	if (store_size == sizeof(EID))
		printf("Store GUID is %s\n", strToLower(bin2hex(sizeof(GUID), &reinterpret_cast<EID *>(store_eid.get())->guid)).c_str());
	else
		printf("Store EID is %s\n", strToLower(bin2hex(store_size, store_eid->ab)).c_str());
	return hrSuccess;
}

static HRESULT adm_create_public(IECServiceAdmin *svcadm, const char *cname)
{
	ULONG cmpeid_size = 0;
	memory_ptr<ENTRYID> cmpeid;
	if (opt_companyname == nullptr) {
		cmpeid_size = g_cbEveryoneEid;
		auto ret = KAllocCopy(g_lpEveryoneEid, g_cbEveryoneEid, &~cmpeid);
		if (ret != hrSuccess)
			return kc_perror("KAllocCopy", ret);
	} else {
		auto ret = svcadm->ResolveCompanyName(reinterpret_cast<const TCHAR *>(cname), 0, &cmpeid_size, &~cmpeid);
		if (ret == MAPI_E_NO_SUPPORT)
			ec_log_info("Multi-tenancy not enabled in server.");
		if (ret != hrSuccess)
			return kc_perror("ResolveCompanyName", ret);
	}
	memory_ptr<ENTRYID> store_eid, root_fld;
	ULONG store_size = 0, root_size = 0;
	auto ret = svcadm->CreateStore(ECSTORE_TYPE_PUBLIC, cmpeid_size,
	           cmpeid, &store_size, &~store_eid, &root_size, &~root_fld);
	if (ret == MAPI_E_COLLISION)
		return kc_perror("Public store already exists", ret);
	if (ret != hrSuccess)
		return kc_perror("Unable to create public store", ret);
	printf("The store has been created.\n");
	if (store_size == sizeof(EID))
		printf("Store GUID is %s\n", strToLower(bin2hex(sizeof(GUID), &reinterpret_cast<EID *>(store_eid.get())->guid)).c_str());
	else
		printf("Store EID is %s\n", strToLower(bin2hex(store_size, store_eid->ab)).c_str());
	return hrSuccess;
}

static HRESULT adm_remove_store(IECServiceAdmin *svcadm, const char *hexguid)
{
	GUID binguid;
	auto ret = adm_hex2bin(hexguid, binguid);
	if (ret != hrSuccess)
		return ret;
	ret = svcadm->RemoveStore(&binguid);
	if (ret != hrSuccess)
		return kc_perror("RemoveStore", ret);
	printf("The store has been removed.\n");
	return hrSuccess;
}

static HRESULT adm_perform()
{
	KServerContext srvctx;
	srvctx.m_app_misc = "storeadm";
	auto ret = srvctx.logon();
	if (ret != hrSuccess)
		return kc_perror("KServerContext::logon", ret);
	if (opt_create_public)
		return adm_create_public(srvctx.m_svcadm, opt_companyname);
	if (opt_create_store)
		return adm_create_store(srvctx.m_svcadm);
	if (opt_detach_store)
		return adm_detach_store(srvctx);
	if (opt_remove_store != nullptr)
		return adm_remove_store(srvctx.m_svcadm, opt_remove_store);
	if (opt_attach_store != nullptr)
		return adm_attach_store(srvctx, opt_attach_store);
	return MAPI_E_CALL_FAILED;
}

static bool adm_parse_options(int &argc, char **&argv)
{
	adm_config.reset(ECConfig::Create(adm_config_defaults));
	opt_config_file = ECConfig::GetDefaultPath("admin.cfg");
	auto ctx = poptGetContext(nullptr, argc, const_cast<const char **>(argv), adm_options, 0);
	int c;
	while ((c = poptGetNextOpt(ctx)) >= 0) {
		if (c == 'c') {
			adm_config->LoadSettings(opt_config_file);
			if (adm_config->HasErrors()) {
				fprintf(stderr, "Error reading config file %s\n", opt_config_file);
				return false;
			}
		}
	}
	if (c < -1) {
		fprintf(stderr, "%s\n", poptStrerror(c));
		poptPrintHelp(ctx, stderr, 0);
		return false;
	}
	auto act = !!opt_attach_store + !!opt_detach_store + !!opt_create_store +
	           !!opt_remove_store + !!opt_create_public;
	if (act > 1) {
		fprintf(stderr, "-A, -C, -D, -P and -R are mutually exclusive.\n");
		return false;
	} else if (act == 0) {
		fprintf(stderr, "One of -A, -C, -D, -P, -R or -? must be specified.\n");
		return false;
	} else if (opt_attach_store != nullptr && opt_entity_name != nullptr) {
		fprintf(stderr, "-A needs -n\n");
		return false;
	} else if ((opt_create_store || opt_detach_store) && opt_entity_name == nullptr) {
		fprintf(stderr, "-C/-D need the -n option\n");
		return false;
	}
	return true;
}

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	ec_log_get()->SetLoglevel(EC_LOGLEVEL_INFO);
	if (!adm_parse_options(argc, argv))
		return EXIT_FAILURE;
	return adm_perform() == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}
