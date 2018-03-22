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
#include <json/writer.h>
#include <kopano/automapi.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/ECABEntryID.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ECRestriction.h>
#include <kopano/EMSAbTag.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <kopano/memory.hpp>
#include <kopano/charset/convert.h>
#include "ConsoleTable.h"
#include "kcore.hpp"

using namespace KC;

static int opt_create_store, opt_create_public, opt_detach_store;
static int opt_copytopublic, opt_list_orphan, opt_show_version;
static int opt_list_mbt;
static const char *opt_attach_store, *opt_remove_store;
static const char *opt_config_file, *opt_host;
static const char *opt_entity_name, *opt_entity_type;
static const char *opt_companyname, *opt_lang;
static std::unique_ptr<ECConfig> adm_config;

static constexpr const struct poptOption adm_options[] = {
	{nullptr, 'A', POPT_ARG_STRING, &opt_attach_store, 0, "Attach an orphaned store by GUID to a user account (with -n)"},
	{nullptr, 'C', POPT_ARG_NONE, &opt_create_store, 0, "Create a store and attach it to a user account (with -n)"},
	{nullptr, 'D', POPT_ARG_NONE, &opt_detach_store, 0, "Detach a user's store (with -n) and make it orphan"},
	{nullptr, 'M', POPT_ARG_NONE, &opt_list_mbt, 0, "Show the so-called mailbox table"},
	{nullptr, 'O', POPT_ARG_NONE, &opt_list_orphan, 0, "List orphaned stores"},
	{nullptr, 'P', POPT_ARG_NONE, &opt_create_public, 0, "Create a public store"},
	{nullptr, 'R', POPT_ARG_STRING, &opt_remove_store, 0, "Remove an orphaned store by GUID"},
	{nullptr, 'V', POPT_ARG_NONE, &opt_show_version, 0, "Show the program version"},
	{nullptr, 'c', POPT_ARG_STRING, &opt_config_file, 'c', "Specify alternate config file"},
	{nullptr, 'h', POPT_ARG_STRING, &opt_host, 0, "URI for server"},
	{nullptr, 'k', POPT_ARG_STRING, &opt_companyname, 0, "Name of the company for creating a public store in a multi-tenant setup"},
	{nullptr, 'l', POPT_ARG_STRING, &opt_lang, 0, "Use given locale for selecting folder names"},
	{nullptr, 'n', POPT_ARG_STRING, &opt_entity_name, 0, "User/group/company account to work on for -A,-C,-D"},
	{nullptr, 'p', POPT_ARG_NONE, &opt_copytopublic, 0, "Copy an orphaned store's root to a subfolder in the public store"},
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

static std::string time_to_rel2(double n)
{
	if (n < 120)
		return stringify(n) + "s";
	if (n < 7200)
		return stringify(n / 60) + "m";
	if (n < 172800)
		return stringify(n / 3600) + "h";
	if (n < 63115200)
		return stringify(n / 86400) + "d";
	return stringify(n / 63115200) + "y";
}

static std::string time_to_rel(time_t then)
{
	auto delta = difftime(time(nullptr), then);
	if (delta >= 0)
		return time_to_rel2(delta) + " ago";
	return "in " + time_to_rel2(-delta);
}

static const char *store_type_string(unsigned int t)
{
	if (t == ECSTORE_TYPE_PRIVATE)
		return "private";
	if (t == ECSTORE_TYPE_ARCHIVE)
		return "archive";
	if (t == ECSTORE_TYPE_PUBLIC)
		return "public";
	return "<unrecognized>";
}

/**
 * List users without a store, and stores without a user.
 *
 * Gets a list of users and stores. (The server only returns users/stores which
 * are home to the chosen server, thereby excluding archives located
 * elsewhere.) Because of the sorting chosen, stores without a user will be
 * printed first, until the first user without a store is found. Then those are
 * printed, until the first user with a store is found.
 */
static HRESULT adm_list_orphans(IECServiceAdmin *svcadm)
{
	bool listing_orphans = true;
	ConsoleTable ct(50, 5);
	static constexpr const SizedSSortOrderSet(2, sort_order) =
		{2, 0, 0, {
			{PR_EC_USERNAME, TABLE_SORT_ASCEND},
			{PR_EC_STOREGUID, TABLE_SORT_ASCEND},
		}};

	object_ptr<IMAPITable> table;
	auto ret = svcadm->OpenUserStoresTable(0, &~table);
	if (ret != hrSuccess)
		return kc_perror("OpenUserStoresTable", ret);
	ret = table->SortTable(sort_order, 0);
	if (ret != hrSuccess)
		return kc_perror("SortTable", ret);
	ct.set_lead("");
	ct.SetHeader(0, "Store GUID");
	ct.SetHeader(1, "Guessed username");
	ct.SetHeader(2, "Last login");
	ct.SetHeader(3, "Store size");
	ct.SetHeader(4, "Store type");
	printf("Stores without users:\n");

	while (true) {
		rowset_ptr rowset;
		ret = table->QueryRows(-1, 0, &~rowset);
		if (ret != hrSuccess)
			return kc_perror("QueryRows", ret);
		if (rowset.size() == 0)
			break;

		for (unsigned int i = 0; i < rowset->cRows; ++i) {
			auto guid  = rowset[i].cfind(PR_EC_STOREGUID);
			auto userp = rowset[i].cfind(PR_EC_USERNAME_A);
			if (guid != nullptr && userp != nullptr)
				continue;
			auto mtime = rowset[i].cfind(PR_LAST_MODIFICATION_TIME);
			auto ssize = rowset[i].cfind(PR_MESSAGE_SIZE_EXTENDED);
			auto stype = rowset[i].cfind(PR_EC_STORETYPE);
			std::string user;
			if (userp == nullptr) {
				userp = rowset[i].cfind(PR_DISPLAY_NAME_A);
				user = userp != nullptr ? userp->Value.lpszA : "<unknown>";
			} else {
				if (listing_orphans) {
					listing_orphans = false;
					ct.PrintTable();
					ct.Resize(50, 1);
					ct.SetHeader(0, "Username");
					printf("\nUsers without stores:\n");
				}
				user = userp->Value.lpszA;
			}
			if (guid == nullptr) {
				ct.AddColumn(0, user);
				continue;
			}
			ct.AddColumn(0, strToLower(bin2hex(guid->Value.bin)));
			ct.AddColumn(1, user);
			ct.AddColumn(2, mtime != nullptr ? time_to_rel(FileTimeToUnixTime(mtime->Value.ft)) : "<unknown>");
			ct.AddColumn(3, ssize != nullptr ? str_storage(ssize->Value.li.QuadPart, false) : "<unknown>");
			ct.AddColumn(4, stype != nullptr ? store_type_string(stype->Value.ul) : "<unknown>");
		}
	}
	ct.PrintTable();
	return hrSuccess;
}

static HRESULT adm_list_mbt(KServerContext &srvctx)
{
	/* Unlike ECUserStoreTable, the MBT is the real thing. */
	object_ptr<IExchangeManageStore> ms;
	auto ret = srvctx.m_admstore->QueryInterface(IID_IExchangeManageStore, &~ms);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface", ret);
	object_ptr<IMAPITable> table;
	ret = ms->GetMailboxTable(nullptr, &~table, MAPI_DEFERRED_ERRORS);
	if (ret != hrSuccess)
		return ret;
	static constexpr const SizedSPropTagArray(5, sp) =
		{5, {PR_MAILBOX_OWNER_ENTRYID, PR_EC_STORETYPE,
		PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W, PR_LAST_MODIFICATION_TIME}};
	ret = table->SetColumns(sp, TBL_BATCH);
	if (ret != hrSuccess)
		return ret;

	while (true) {
		rowset_ptr rowset;
		ret = table->QueryRows(-1, 0, &~rowset);
		if (ret != hrSuccess)
			return kc_perror("QueryRows", ret);
		if (rowset.size() == 0)
			break;

		for (unsigned int i = 0; i < rowset->cRows; ++i) {
			Json::Value outrow;
			auto p = rowset[i].cfind(PR_MAILBOX_OWNER_ENTRYID);
			if (p != nullptr)
				outrow["owner"] = bin2hex(p->Value.bin);
			p = rowset[i].cfind(PR_EC_STORETYPE);
			if (p != nullptr)
				outrow["type"] = store_type_string(p->Value.ul);
			p = rowset[i].cfind(PR_DISPLAY_NAME_A);
			if (p != nullptr)
				outrow["display_name"] = p->Value.lpszA;
			p = rowset[i].cfind(PR_DISPLAY_NAME_W);
			if (p != nullptr)
				outrow["display_name_w"] = p->Value.lpszW;
			p = rowset[i].cfind(PR_LAST_MODIFICATION_TIME);
			if (p != nullptr)
				outrow["mtime"] = static_cast<Json::Value::Int64>(FileTimeToUnixTime(p->Value.ft));
			puts(Json::writeString(Json::StreamWriterBuilder(), outrow).c_str());
		}
	}
	return hrSuccess;
}

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
 * Parse a server store entryid to client store entryid.
 *
 * This is a hack to open an orphan store. It will convert a server store
 * entryid, which does not include a server url and is not wrapped by the
 * support object, to a client side store entryid.
 *
 * @url:	ServerURL for open the orphan store.
 * @eid_size:	Size of the unwrapped orphan store entryid.
 * @eid:	Unwrapped orphan store entryid without server URL.
 * @wrapeid_size:	Size of the wrapped orphan store entryid.
 * @wrapeid:	Pointer to the wrapped entryid from the orphan store entryid.
 */
static HRESULT adm_create_orphan_eid(const char *url, const ENTRYID *eid,
    ULONG eid_size, ENTRYID **wrapeid, ULONG *wrapeid_size)
{
	if (url == nullptr || eid == nullptr || wrapeid == nullptr ||
	    wrapeid_size == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ULONG url_len = strlen(url), neweid_size = eid_size + url_len;
	memory_ptr<ENTRYID> neweid;
	auto ret = MAPIAllocateBuffer(neweid_size, &~neweid);
	if (ret != hrSuccess)
		return ret;
	memcpy(neweid, eid, eid_size);
	memcpy(reinterpret_cast<unsigned char *>(neweid.get()) + eid_size - 4, url, url_len + 4);
	return WrapStoreEntryID(0, reinterpret_cast<const TCHAR *>("zarafa6client.dll"),
	       neweid_size, neweid, wrapeid_size, wrapeid);
}

static HRESULT adm_orphan_store_info(IECServiceAdmin *svcadm, const GUID &store,
    const char *url, std::wstring &user, std::wstring &company,
    ULONG *eid_size, ENTRYID **eid)
{
	object_ptr<IMAPITable> table;
	rowset_ptr rowset;
	auto ret = svcadm->OpenUserStoresTable(MAPI_UNICODE, &~table);
	if (ret != hrSuccess)
		return ret;
	SPropValue stguid;
	stguid.ulPropTag     = PR_EC_STOREGUID;
	stguid.Value.bin.cb  = sizeof(GUID);
	stguid.Value.bin.lpb = reinterpret_cast<BYTE *>(const_cast<GUID *>(&store));
	ret = ECPropertyRestriction(RELOP_EQ, PR_EC_STOREGUID, &stguid, ECRestriction::Cheap)
	      .FindRowIn(table, BOOKMARK_BEGINNING, 0);
	if (ret != hrSuccess)
		return ret;
	ret = table->QueryRows(1, 0, &~rowset);
	if (ret != hrSuccess)
		return ret;
	if (rowset.empty())
		return MAPI_E_NOT_FOUND;
	auto name = rowset[0].cfind(PR_DISPLAY_NAME_W);
	if (name != nullptr)
		user = name->Value.lpszW;
	name = rowset[0].cfind(PR_EC_COMPANY_NAME_W);
	if (name != nullptr)
		company = name->Value.lpszW;
	auto eidprop = rowset[0].cfind(PR_STORE_ENTRYID);
	if (eidprop == nullptr)
		return MAPI_E_NOT_FOUND;
	return adm_create_orphan_eid(url, reinterpret_cast<ENTRYID *>(eidprop->Value.bin.lpb),
	       eidprop->Value.bin.cb, eid, eid_size);
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

/**
 * Open/create deleted stores folder in the public store.
 *
 * Open the deleted admin folder in a public store. If the folder does not exist, it
 * will be created. First, it creates a folder called "Admin" in the top-level
 * tree (IPM_SUBTREE). The permissions on the folder are set to "Everyone" -WTF- can
 * not read the folder except an admin. A second folder 'Deleted stores' will
 * create without permissions because the inheritance of the permissions.
 *
 * @store:	Public store where to open/create the "Deleted Stores" folder
 * @dsfld:	Pointer to a pointer of folder 'Deleted stores'.
 */
static HRESULT adm_open_dsfolder(IMsgStore *store,
    IMAPIFolder **dsfld)
{
	if (store == nullptr || dsfld == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<SPropValue> pv;
	auto ret = HrGetOneProp(store, PR_MDB_PROVIDER, &~pv);
	if (ret != hrSuccess)
		return kc_perror("HrGetOneProp", ret);

	/* Workaround for companies, because a company is a delegate store! */
	auto pt = pv->Value.bin.cb == sizeof(MAPIUID) &&
	          memcmp(pv->Value.bin.lpb, &KOPANO_STORE_PUBLIC_GUID, sizeof(MAPIUID)) == 0 ?
	          PR_IPM_PUBLIC_FOLDERS_ENTRYID : PR_IPM_SUBTREE_ENTRYID;
	ret = HrGetOneProp(store, pt, &~pv);
	if (ret != hrSuccess)
		return kc_perror("HrGetOneProp", ret);
	ULONG obj_type = 0;
	object_ptr<IMAPIFolder> ipm;
	ret = store->OpenEntry(pv->Value.bin.cb, reinterpret_cast<ENTRYID *>(pv->Value.bin.lpb),
	      &iid_of(ipm), MAPI_MODIFY, &obj_type, &~ipm);
	if (ret != hrSuccess)
		return kc_perror("OpenEntry", ret);
	/*
	 * Create/open a folder called "Admin", and below that, one called
	 * "Deleted Stores".
	 */
	object_ptr<IMAPIFolder> adm_folder, ds_folder;
	ret = ipm->CreateFolder(FOLDER_GENERIC, reinterpret_cast<const TCHAR *>("Admin"), nullptr, nullptr, 0, &~adm_folder);
	if (ret == hrSuccess) {
		/* Set permissions */
		object_ptr<IECSecurity> sec;
		ret = GetECObject(adm_folder, iid_of(sec), &~sec);
		if (ret != hrSuccess)
			return kc_perror("GetECObject", ret);
		ECPERMISSION perm = {0};
		perm.ulRights = 0; /* No rights, only for admin */
		perm.sUserId.lpb = g_lpEveryoneEid; /* group: everyone */
		perm.sUserId.cb = g_cbEveryoneEid;
		perm.ulState = RIGHT_NEW | RIGHT_AUTOUPDATE_DENIED;
		perm.ulType = ACCESS_TYPE_GRANT;
		ret = sec->SetPermissionRules(1, &perm);
	} else if (ret == MAPI_E_COLLISION) {
		ret = ipm->CreateFolder(FOLDER_GENERIC, reinterpret_cast<const TCHAR *>("Admin"),
		      nullptr, nullptr, OPEN_IF_EXISTS, &~adm_folder);
	}
	if (ret != hrSuccess)
		return ret;
	return adm_folder->CreateFolder(FOLDER_GENERIC, reinterpret_cast<const TCHAR *>("Deleted stores"),
	       nullptr, &iid_of(*dsfld), OPEN_IF_EXISTS, dsfld);
}

static HRESULT adm_copy_to_public(KServerContext &kadm,
    const std::string &hexguid, const GUID &binguid)
{
	/* Find store entryid */
	ULONG eid_size = 0;
	memory_ptr<ENTRYID> eid;
	std::wstring user, company;
	auto ret = adm_orphan_store_info(kadm.m_svcadm, binguid, opt_host,
	           user, company, &eid_size, &~eid);
	if (ret != hrSuccess)
		return kc_perror("GetOrphanStoreInfo", ret);

	/* Open the orphan store */
	object_ptr<IMsgStore> usr_store;
	ret = kadm.m_session->OpenMsgStore(0, eid_size, eid.get(),
	      nullptr, MAPI_BEST_ACCESS, &~usr_store);
	if (ret != hrSuccess)
		return kc_perror("Unable to open orphaned store", ret);

	/* Open the root container for copying folders */
	object_ptr<IMAPIFolder> root_fld;
	ULONG root_type = 0;
	ret = usr_store->OpenEntry(0, nullptr, &iid_of(root_fld),
	      MAPI_BEST_ACCESS, &root_type, &~root_fld);
	if (ret != hrSuccess)
		return kc_perror("Unable to open root folder of the orphaned store", ret);
	memory_ptr<SPropValue> pv;
	ret = HrGetOneProp(usr_store, PR_IPM_SUBTREE_ENTRYID, &~pv);
	if (ret != hrSuccess)
		return kc_perror("Orphan store has no IPM_SUBTREE", ret);

	/* Open the public store */
	object_ptr<IMsgStore> pub_store;
	ret = adm_get_public_store(kadm.m_session, usr_store, company, &~pub_store);
	if (ret != hrSuccess)
		return kc_perror("Unable to open the public store", ret);

	/* Open/create folders admin/stores */
	object_ptr<IMAPIFolder> ds_fld;
	ret = adm_open_dsfolder(pub_store, &~ds_fld);
	if (ret != hrSuccess)
		return kc_perror("Unable to open the folder \"Deleted Stores\"", ret);

	/* Copy everything to public */
	std::wstring store_name = L"Deleted User - ";
	if (store_name.empty())
		store_name += convert_to<std::wstring>(hexguid);
	else
		store_name += user;

	auto stname_tmp = store_name;
	unsigned int folder_id = 0;
	printf("Copying the orphan store to the public store folder \"%s\"\n",
	       convert_to<std::string>(store_name).c_str());
	while (true) {
		ret = root_fld->CopyFolder(pv->Value.bin.cb,
		      reinterpret_cast<const ENTRYID *>(pv->Value.bin.lpb), nullptr, ds_fld,
		      reinterpret_cast<const TCHAR *>(stname_tmp.c_str()), 0,
		      nullptr, COPY_SUBFOLDERS | MAPI_UNICODE);
		if (ret == MAPI_E_COLLISION) {
			if (folder_id >= 1000) {
				fprintf(stderr, "Unable to copy the store to the public, maximum folder collisions exceeded\n");
				return MAPI_E_CALL_FAILED;
			}
			stname_tmp = store_name + std::to_wstring(++folder_id);
			fprintf(stderr, "Folder already exists, retrying with foldername \"%s\"\n",
			        convert_to<std::string>(stname_tmp).c_str());
			continue;
		} else if (FAILED(ret)) {
			return kc_perror("Unable to copy the store to public", ret);
		} else if (ret != hrSuccess) {
			fprintf(stderr, "The copy succeeded, but not all entries "
			"were copied (%s)\n", GetMAPIErrorMessage(ret));
			break;
		}
		printf("Copy succeeded\n");
		break;
	}
	return hrSuccess;
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
	    *reinterpret_cast<const char *>(user->lpszServername) == '\0')
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
	if (opt_copytopublic)
		return adm_copy_to_public(kadm, hexguid, binguid);
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
	if (opt_show_version) {
		printf("kopano-storeadm " PROJECT_VERSION "\n");
		return hrSuccess;
	}
	KServerContext srvctx;
	srvctx.m_app_misc = "storeadm";
	if (opt_host == nullptr)
		opt_host = GetServerUnixSocket(adm_config->GetSetting("server_socket"));
	srvctx.m_host = opt_host;
	srvctx.m_ssl_keyfile = adm_config->GetSetting("sslkey_file", "", nullptr);
	srvctx.m_ssl_keypass = adm_config->GetSetting("sslkey_pass", "", nullptr);
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
	if (opt_list_orphan)
		return adm_list_orphans(srvctx.m_svcadm);
	if (opt_list_mbt)
		return adm_list_mbt(srvctx);
	return MAPI_E_CALL_FAILED;
}

static bool adm_parse_options(int &argc, char **&argv)
{
	adm_config.reset(ECConfig::Create(adm_config_defaults));
	opt_config_file = ECConfig::GetDefaultPath("admin.cfg");
	adm_config->LoadSettings(opt_config_file);

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
	           !!opt_remove_store + !!opt_create_public + !!opt_list_orphan +
	           !!opt_list_mbt + !!opt_show_version;
	if (act > 1) {
		fprintf(stderr, "-A, -C, -D, -M, -O, -P, -R and -V are mutually exclusive.\n");
		return false;
	} else if (act == 0) {
		fprintf(stderr, "One of -A, -C, -D, -M, -O, -P, -R, -V or -? must be specified.\n");
		return false;
	} else if (opt_attach_store != nullptr && ((opt_entity_name != nullptr) == !!opt_copytopublic)) {
		fprintf(stderr, "-A needs exactly one of -n or -p\n");
		return false;
	} else if ((opt_create_store || opt_detach_store) && opt_entity_name == nullptr) {
		fprintf(stderr, "-C/-D need the -n option\n");
		return false;
	} else if (opt_lang != nullptr && !opt_create_store && !opt_create_public) {
		fprintf(stderr, "-l can only be used with -C or -P.\n");
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
	if (opt_lang != nullptr && setlocale(LC_MESSAGES, opt_lang) == nullptr) {
		fprintf(stderr, "Your system does not have the \"%s\" locale available.\n", opt_lang);
		return EXIT_FAILURE;
	}
	return adm_perform() == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}
