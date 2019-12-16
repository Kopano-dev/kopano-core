/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005-2016 Zarafa and its licensors
 * Copyright 2018, Kopano and its licensors
 */
#include <memory>
#include <stdexcept>
#include <string>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <mapidefs.h>
#include <json/writer.h>
#include <libHX/option.h>
#include <kopano/automapi.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/ECABEntryID.h>
#include <kopano/ECConfig.h>
#include <kopano/ECGetText.h>
#include <kopano/ECLogger.h>
#include <kopano/ECRestriction.h>
#include <kopano/EMSAbTag.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/ecversion.h>
#include <kopano/mapiext.h>
#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <kopano/timeutil.hpp>
#include <kopano/memory.hpp>
#include <kopano/charset/convert.h>
#include "ConsoleTable.h"
#include "kcore.hpp"

using namespace KC;

static int opt_create_store, opt_create_public, opt_detach_store;
static int opt_copytopublic, opt_list_orphan, opt_show_version;
static int opt_list_mbt, opt_localize_folders, opt_loglevel = 3;
static const char *opt_attach_store, *opt_remove_store;
static const char *opt_config_file, *opt_host;
static const char *opt_entity_name, *opt_entity_type;
static const char *opt_companyname, *opt_lang;
static std::unique_ptr<ECConfig> adm_config;

static constexpr const struct HXoption adm_options[] = {
	{nullptr, 'A', HXTYPE_STRING, &opt_attach_store, nullptr, nullptr, 0, "Attach an orphaned store by GUID to a user account (with -n)", "GUID"},
	{nullptr, 'C', HXTYPE_NONE, &opt_create_store, nullptr, nullptr, 0, "Create a store and attach it to a user account (with -n)"},
	{nullptr, 'D', HXTYPE_NONE, &opt_detach_store, nullptr, nullptr, 0, "Detach a user's store (with -n) and make it orphan"},
	{nullptr, 'M', HXTYPE_NONE, &opt_list_mbt, nullptr, nullptr, 0, "Show the so-called mailbox table"},
	{nullptr, 'O', HXTYPE_NONE, &opt_list_orphan, nullptr, nullptr, 0, "List orphaned stores"},
	{nullptr, 'P', HXTYPE_NONE, &opt_create_public, nullptr, nullptr, 0, "Create a public store"},
	{nullptr, 'R', HXTYPE_STRING, &opt_remove_store, nullptr, nullptr, 0, "Remove an orphaned store by GUID", "GUID"},
	{nullptr, 'V', HXTYPE_NONE, &opt_show_version, nullptr, nullptr, 0, "Show the program version"},
	{nullptr, 'Y', HXTYPE_NONE, &opt_localize_folders, nullptr, nullptr, 0, "Re-localize standard folders"},
	{nullptr, 'c', HXTYPE_STRING, &opt_config_file, nullptr, nullptr, 0, "Specify alternate config file", "FILENAME"},
	{nullptr, 'h', HXTYPE_STRING, &opt_host, nullptr, nullptr, 0, "URI for server", "URI"},
	{nullptr, 'k', HXTYPE_STRING, &opt_companyname, nullptr, nullptr, 0, "Name of the company for creating a public store in a multi-tenant setup", "NAME"},
	{nullptr, 'l', HXTYPE_STRING, &opt_lang, nullptr, nullptr, 0, "Use given locale for selecting folder names", "LOCALE"},
	{nullptr, 'n', HXTYPE_STRING, &opt_entity_name, nullptr, nullptr, 0, "User/group/company account to work on for -A,-C,-D", "NAME"},
	{nullptr, 'p', HXTYPE_NONE, &opt_copytopublic, nullptr, nullptr, 0, "Copy an orphaned store's root (with -A) to a subfolder in the public store"},
	{nullptr, 't', HXTYPE_STRING, &opt_entity_type, nullptr, nullptr, 0, "Store type for the -n argument (user, archive, group, company)", "TYPE"},
	{nullptr, 'v', HXTYPE_NONE | HXOPT_INC, &opt_loglevel, nullptr, nullptr, 0, "Raise loglevel by one count"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static constexpr const configsetting_t adm_config_defaults[] = {
	{"default_store_locale", ""},
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

static inline std::string objclass_to_str(const SPropValue *p)
{
	if (p == nullptr)
		return "(undefined)";
	switch (p->Value.ul) {
	case ACTIVE_USER: return "User";
	case NONACTIVE_USER: return "Shared store";
	case NONACTIVE_ROOM: return "Room";
	case NONACTIVE_EQUIPMENT: return "Equipment";
	case NONACTIVE_CONTACT: return "Contact";
	case DISTLIST_GROUP: return "Group";
	case DISTLIST_SECURITY: return "Security group";
	case DISTLIST_DYNAMIC: return "Dynamic group";
	case CONTAINER_COMPANY: return "Company";
	case CONTAINER_ADDRESSLIST: return "Address list";
	default: return std::to_string(p->Value.ul);
	}
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
	ct.SetHeader(1, "Guessed owner");
	ct.SetHeader(2, "Last login");
	ct.SetHeader(3, "Store size");
	ct.SetHeader(4, "Store type");
	printf("Stores without an owner:\n");

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
			auto objcls = rowset[i].cfind(PR_OBJECT_TYPE);
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
					ct.Clear();
					ct.Resize(50, 2);
					ct.SetHeader(0, "Type");
					ct.SetHeader(1, "Name");
					printf("\nEntities without stores:\n");
				}
				user = userp->Value.lpszA;
			}
			if (guid == nullptr) {
				ct.AddColumn(0, objclass_to_str(objcls));
				ct.AddColumn(1, user);
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
	static constexpr const SizedSPropTagArray(6, sp) =
		{6, {PR_MAILBOX_OWNER_ENTRYID, PR_EC_STORETYPE,
		PR_DISPLAY_NAME_A, PR_DISPLAY_NAME_W, PR_LAST_MODIFICATION_TIME,
		PR_MESSAGE_SIZE_EXTENDED}};
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
			auto &p = rowset[i].lpProps;
			if (p[0].ulPropTag == PR_MAILBOX_OWNER_ENTRYID)
				outrow["owner"] = bin2hex(p[0].Value.bin);
			if (p[1].ulPropTag == PR_EC_STORETYPE)
				outrow["type"] = store_type_string(p[1].Value.ul);
			if (p[2].ulPropTag == PR_DISPLAY_NAME_A)
				outrow["display_name"] = p[2].Value.lpszA;
			if (p[3].ulPropTag == PR_DISPLAY_NAME_W)
				outrow["display_name_w"] = p[3].Value.lpszW;
			if (p[4].ulPropTag == PR_LAST_MODIFICATION_TIME)
				outrow["mtime"] = static_cast<Json::Value::Int64>(FileTimeToUnixTime(p[4].Value.ft));
			if (p[5].ulPropTag == PR_MESSAGE_SIZE_EXTENDED)
				outrow["size"] = static_cast<Json::Value::Int64>(p[5].Value.li.QuadPart);
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
			ret = kadm.m_admstore->QueryInterface(IID_IExchangeManageStore, &~ms);
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
		return kc_perror("User store already exists", ret);
	if (ret != hrSuccess)
		return kc_perror("Unable to create store", ret);
	if (store_size == sizeof(EID_FIXED))
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
	if (store_size == sizeof(EID_FIXED))
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

static HRESULT adm_snreport(HRESULT ret, const char *old, const wchar_t *nw)
{
	if (ret == MAPI_E_NOT_FOUND)
		ec_log_info("Skipping %s (no such proptag/folder)", old);
	else if (ret != hrSuccess)
		ec_log_err("Error during lookup of %s: %s", old, GetMAPIErrorMessage(ret));
	else
		ec_log_notice("Renamed %s -> \"%s\"", old, convert_to<std::string>(nw).c_str());
	return ret;
}

static HRESULT adm_setname(IMAPIProp *fld, const char *plain)
{
	SPropValue pv;
	pv.ulPropTag   = PR_DISPLAY_NAME_W;
	pv.Value.lpszW = KC_TX(plain);
	return adm_snreport(HrSetOneProp(fld, &pv), plain, pv.Value.lpszW);
}

template<typename C> static HRESULT
adm_setname(C &parent, unsigned int tag, const char *plain)
{
	memory_ptr<SPropValue> pv;
	auto ret = HrGetOneProp(parent, tag, &~pv);
	if (ret != hrSuccess)
		return adm_snreport(ret, plain, L"");
	object_ptr<IMAPIFolder> fld;
	unsigned int type = 0;
	ret = parent->OpenEntry(pv->Value.bin.cb, reinterpret_cast<const ENTRYID *>(pv->Value.bin.lpb),
	      &iid_of(fld), MAPI_MODIFY, &type, &~fld);
	if (ret != hrSuccess)
		return adm_snreport(ret, plain, L"");
	return adm_setname(fld, plain);
}

static HRESULT adm_setname_ren(IMsgStore *store, IMAPIContainer *parent,
    unsigned int atag, unsigned int mvpos, const char *plain)
{
	memory_ptr<SPropValue> pv;
	auto ret = HrGetOneProp(parent, PR_ADDITIONAL_REN_ENTRYIDS, &~pv);
	if (ret != hrSuccess)
		return adm_snreport(ret, plain, L"");
	if (mvpos >= pv->Value.MVbin.cValues)
		return adm_snreport(MAPI_E_NOT_FOUND, plain, L"");
	object_ptr<IMAPIFolder> fld;
	unsigned int type = 0;
	const auto &eid = pv->Value.MVbin.lpbin[mvpos];
	ret = store->OpenEntry(eid.cb, reinterpret_cast<const ENTRYID *>(eid.lpb),
	      &iid_of(fld), MAPI_MODIFY, &type, &~fld);
	if (ret != hrSuccess)
		return adm_snreport(ret, plain, L"");
	return adm_setname(fld, plain);
}

static HRESULT adm_setname_rsf(IMsgStore *store, IMAPIContainer *parent,
    unsigned int search_type, const char *plain)
{
	struct elbuf {
		uint16_t type, unknown1, block_type, eid_size;
	} elb;
	memory_ptr<SPropValue> pv;
	auto ret = HrGetOneProp(parent, PR_IPM_OL2007_ENTRYIDS, &~pv);
	if (ret != hrSuccess)
		return adm_snreport(ret, plain, L"");
	auto &rem = pv->Value.bin.cb;
	for (auto ptr = pv->Value.bin.lpb; rem >= sizeof(elbuf); ) {
		memcpy(&elb, ptr, sizeof(elb));
		elb.type = le16_to_cpu(elb.type);
		elb.eid_size = le16_to_cpu(elb.eid_size);
		if (elb.type == 0 || rem < elb.eid_size + sizeof(elb))
			break;
		if (elb.type == search_type) {
			object_ptr<IMAPIFolder> fld;
			unsigned int type = 0;
			ret = store->OpenEntry(elb.eid_size, reinterpret_cast<const ENTRYID *>(ptr + sizeof(elb)),
			      &iid_of(fld), MAPI_MODIFY, &type, &~fld);
			if (ret != hrSuccess)
				break;
			return adm_setname(fld, plain);
		}
		ptr += elb.eid_size + sizeof(elb);
		rem -= elb.eid_size + sizeof(elb);
	}
	return adm_snreport(MAPI_E_NOT_FOUND, plain, L"");
}

static HRESULT adm_localize(KServerContext &srvctx)
{
	object_ptr<IExchangeManageStore> ms;
	auto ret = srvctx.m_admstore->QueryInterface(IID_IExchangeManageStore, &~ms);
	if (ret != hrSuccess)
		return kc_perrorf("QueryInterface", ret);
	unsigned int type = 0, pv_size = 0;
	memory_ptr<ENTRYID> pv_eid;
	ret = ms->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(""), reinterpret_cast<const TCHAR *>(opt_entity_name),
	      0, &pv_size, &~pv_eid);
	if (ret != hrSuccess)
		return kc_perrorf("CreateStoreEntryID", ret);
	object_ptr<IMsgStore> ustore;
	ret = srvctx.m_session->OpenMsgStore(0, pv_size, pv_eid, &iid_of(ustore), MDB_WRITE, &~ustore);
	if (ret != hrSuccess)
		return kc_perrorf("OpenMsgStore", ret);
	object_ptr<IMAPIFolder> root, inbox;
	ret = ustore->OpenEntry(0, nullptr, &iid_of(root), MAPI_BEST_ACCESS | MAPI_MODIFY, &type, &~root);
	if (ret != hrSuccess)
		return kc_perrorf("open_root", ret);

	/* keep in sync with ECMsgStore.cpp and ECExchangeImportContentsChanges.cpp */
	adm_setname(ustore, PR_COMMON_VIEWS_ENTRYID, "IPM_COMMON_VIEWS");
	adm_setname(ustore, PR_VIEWS_ENTRYID, "IPM_VIEWS");
	adm_setname(ustore, PR_FINDER_ENTRYID, "FINDER_ROOT");
	adm_setname(ustore, PR_IPM_FAVORITES_ENTRYID, "Shortcut");
	adm_setname(ustore, PR_SCHEDULE_FOLDER_ENTRYID, "Schedule");
	adm_setname(ustore, PR_IPM_OUTBOX_ENTRYID, "Outbox");
	adm_setname(ustore, PR_IPM_WASTEBASKET_ENTRYID, "Deleted Items");
	adm_setname(ustore, PR_IPM_SENTMAIL_ENTRYID, "Sent Items");

	ret = ustore->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &pv_size, &~pv_eid, nullptr);
	if (ret == MAPI_E_NOT_FOUND)
		return hrSuccess;
	else if (ret != hrSuccess)
		return kc_perrorf("GetReceiveFolder", ret);
	ret = root->OpenEntry(pv_size, reinterpret_cast<const ENTRYID *>(pv_eid.get()),
	      &iid_of(inbox), MAPI_BEST_ACCESS | MAPI_MODIFY, &type, &~inbox);
	if (ret == MAPI_E_NOT_FOUND)
		return hrSuccess;
	else if (ret != hrSuccess)
		return ret;

	adm_setname(inbox, "Inbox");
	adm_setname(inbox, PR_IPM_CONTACT_ENTRYID, "Contacts");
	adm_setname(inbox, PR_IPM_APPOINTMENT_ENTRYID, "Calendar");
	adm_setname(inbox, PR_IPM_DRAFTS_ENTRYID, "Drafts");
	adm_setname(inbox, PR_IPM_JOURNAL_ENTRYID, "Journal");
	adm_setname(inbox, PR_IPM_NOTE_ENTRYID, "Notes");
	adm_setname(inbox, PR_IPM_TASK_ENTRYID, "Tasks");
	adm_setname_ren(ustore, inbox, PR_ADDITIONAL_REN_ENTRYIDS, 0, "Conflicts");
	adm_setname_ren(ustore, inbox, PR_ADDITIONAL_REN_ENTRYIDS, 1, "Sync Issues");
	adm_setname_ren(ustore, inbox, PR_ADDITIONAL_REN_ENTRYIDS, 2, "Local Failures");
	adm_setname_ren(ustore, inbox, PR_ADDITIONAL_REN_ENTRYIDS, 3, "Server Failures");
	adm_setname_ren(ustore, inbox, PR_ADDITIONAL_REN_ENTRYIDS, 4, "Junk E-mail");
	adm_setname_rsf(ustore, inbox, RSF_PID_RSS_SUBSCRIPTION, "RSS Feeds");
	adm_setname_rsf(ustore, inbox, RSF_PID_CONV_ACTIONS, "Conversation Action Settings");
	adm_setname_rsf(ustore, inbox, RSF_PID_COMBINED_ACTIONS, "Quick Step Settings");
	adm_setname_rsf(ustore, inbox, RSF_PID_SUGGESTED_CONTACTS, "Suggested Contacts");
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
	if (opt_localize_folders)
		return adm_localize(srvctx);
	return MAPI_E_CALL_FAILED;
}

static bool adm_parse_options(int &argc, const char **&argv)
{
	adm_config.reset(ECConfig::Create(adm_config_defaults));
	adm_config->LoadSettings(ECConfig::GetDefaultPath("admin.cfg"));

	if (HX_getopt(adm_options, &argc, &argv, HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return false;
	if (opt_loglevel > EC_LOGLEVEL_DEBUG)
		opt_loglevel = EC_LOGLEVEL_ALWAYS;
	ec_log_get()->SetLoglevel(opt_loglevel);
	if (opt_config_file != nullptr) {
		adm_config->LoadSettings(opt_config_file);
		if (adm_config->HasErrors()) {
			fprintf(stderr, "Error reading config file %s\n", opt_config_file);
			LogConfigErrors(adm_config.get());
			return false;
		}
	}
	auto act = !!opt_attach_store + !!opt_detach_store + !!opt_create_store +
	           !!opt_remove_store + !!opt_create_public + !!opt_list_orphan +
	           !!opt_list_mbt + !!opt_show_version + !!opt_localize_folders;
	if (act > 1) {
		fprintf(stderr, "-A, -C, -D, -M, -O, -P, -R, -V and -Y are mutually exclusive.\n");
		return false;
	} else if (act == 0) {
		fprintf(stderr, "One of -A, -C, -D, -M, -O, -P, -R, -V, -Y or -? must be specified.\n");
		return false;
	} else if (opt_attach_store != nullptr && ((opt_entity_name != nullptr) == !!opt_copytopublic)) {
		fprintf(stderr, "-A needs exactly one of -n or -p.\n");
		return false;
	} else if ((opt_create_store || opt_detach_store) && opt_entity_name == nullptr) {
		fprintf(stderr, "-C/-D also need the -n option.\n");
		return false;
	} else if (opt_companyname != nullptr && !opt_create_public) {
		fprintf(stderr, "-k can only be used with -P.\n");
		return false;
	} else if (opt_lang != nullptr && !opt_create_store && !opt_create_public && !opt_localize_folders) {
		fprintf(stderr, "-l can only be used with -C/-P/-Y.\n");
		return false;
	} else if (opt_entity_name != nullptr && !opt_attach_store && !opt_create_store &&
	    !opt_detach_store && !opt_localize_folders) {
		fprintf(stderr, "-n can only be used with -A/-C/-D/-Y.\n");
		return false;
	} else if (opt_copytopublic && !opt_attach_store) {
		fprintf(stderr, "-p can only be used with -A.\n");
		return false;
	} else if (opt_entity_type && opt_entity_name == nullptr) {
		fprintf(stderr, "-t can only be used with -n.\n");
		return false;
	}
	if (opt_lang == nullptr) {
		opt_lang = adm_config->GetSetting("default_store_locale");
		if (opt_localize_folders)
			fprintf(stderr, "The -l option was not specified; "
			        "\"%s\" will be used as language.\n", opt_lang);
	}
	return true;
}

static bool adm_setlocale(const char *lang)
{
	if (lang == nullptr || *opt_lang == '\0')
		return true;
	if (setlocale(LC_MESSAGES, opt_lang) != nullptr)
		return true;
	auto uloc = opt_lang + std::string(".UTF-8");
	if (strchr(opt_lang, '.') == nullptr &&
	    setlocale(LC_MESSAGES, uloc.c_str()) != nullptr)
		return true;
	fprintf(stderr, "Your system does not appear have the \"%s\" or \"%s\" "
	        "locale available.\n", opt_lang, uloc.c_str());
	return false;
}

int main(int argc, const char **argv) try
{
	setlocale(LC_ALL, "");
	ec_log_get()->SetLoglevel(EC_LOGLEVEL_INFO);
	if (!adm_parse_options(argc, argv) || !adm_setlocale(opt_lang))
		return EXIT_FAILURE;
	return adm_perform() == hrSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
} catch (...) {
	std::terminate();
}
