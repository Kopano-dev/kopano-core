/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <algorithm>
#include <exception>
#include <iostream>
#include <list>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <getopt.h>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <mapispi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/automapi.hpp>
#include <kopano/IECInterfaces.hpp>
#include "SSLUtil.h"
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ECTags.h>
#include <kopano/EMSAbTag.h>
#include <kopano/ECGuid.h>
#include <kopano/ECABEntryID.h>
#include <kopano/CommonUtil.h>
#include <kopano/stringutil.h>
#include <kopano/ecversion.h>
#include <kopano/mapiext.h>
#include <kopano/timeutil.hpp>
#include <kopano/Util.h>
#include <kopano/ECRestriction.h>
#include <kopano/charset/convert.h>
#include "ConsoleTable.h"
#include <kopano/mapi_ptr.h>
#include <kopano/ECFeatures.hpp>
#include "ECACL.h"
#include "charset/localeutil.h"
#include <kopano/MAPIErrors.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <libintl.h>
#include "Archiver.h"
#include <kopano/MAPIErrors.h> // for declaration of GetMAPIErrorMessage()

using namespace KC;
using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::string;
using std::wstring;

static int forcedExitCode = 0;

enum modes {
	MODE_INVALID = 0, MODE_LIST_USERS, MODE_CREATE_PUBLIC,
	MODE_CREATE_USER, MODE_CREATE_STORE, MODE_HOOK_STORE, MODE_UNHOOK_STORE,
	MODE_DELETE_STORE, MODE_REMOVE_STORE, MODE_UPDATE_USER, MODE_DELETE_USER,
	MODE_CREATE_GROUP, MODE_UPDATE_GROUP, MODE_DELETE_GROUP,
	MODE_LIST_GROUP, MODE_ADDUSER_GROUP, MODE_DELETEUSER_GROUP,
	MODE_CREATE_COMPANY, MODE_UPDATE_COMPANY, MODE_DELETE_COMPANY, MODE_LIST_COMPANY,
	MODE_ADD_VIEW, MODE_DEL_VIEW, MODE_LIST_VIEW,
	MODE_ADD_ADMIN, MODE_DEL_ADMIN, MODE_LIST_ADMIN,
	MODE_ADD_USERQUOTA_RECIPIENT, MODE_DEL_USERQUOTA_RECIPIENT, MODE_LIST_USERQUOTA_RECIPIENT,
	MODE_ADD_COMPANYQUOTA_RECIPIENT, MODE_DEL_COMPANYQUOTA_RECIPIENT, MODE_LIST_COMPANYQUOTA_RECIPIENT,
	MODE_SYNC_USERS, MODE_DETAILS, MODE_LIST_SENDAS, MODE_HELP,
	MODE_SYSTEM_ADMIN,
	MODE_FORCE_RESYNC, MODE_USER_COUNT, MODE_RESET_FOLDER_COUNT
};

enum {
	OPT_CREATE_STORE = UCHAR_MAX + 1, // high to avoid clashes with modes
	OPT_DELETE_STORE,
	OPT_HOOK_STORE,
	OPT_UNHOOK_STORE,
	OPT_REMOVE_STORE,
	OPT_COPYTO_PUBLIC,
	OPT_HELP,
	OPT_HOST,
	OPT_SYNC_USERS,
	OPT_DETAILS,
	OPT_DETAILS_TYPE,
	OPT_USER_QUOTA_HARD,
	OPT_USER_QUOTA_SOFT,
	OPT_USER_QUOTA_WARN,
	OPT_USER_QUOTA_OVERRIDE,
	OPT_USER_DEFAULT_QUOTA_HARD,
	OPT_USER_DEFAULT_QUOTA_SOFT,
	OPT_USER_DEFAULT_QUOTA_WARN,
	OPT_USER_DEFAULT_QUOTA_OVERRIDE,
	OPT_LANG,
	OPT_MR_ACCEPT,
	OPT_MR_DECLINE_CONFLICT,
	OPT_MR_DECLINE_RECURRING,
	OPT_ADD_SENDAS,
	OPT_DEL_SENDAS,
	OPT_LIST_SENDAS,
	OPT_UPDATE_GROUP,
	OPT_CREATE_COMPANY,
	OPT_UPDATE_COMPANY,
	OPT_DELETE_COMPANY,
	OPT_LIST_COMPANY,
	OPT_ADD_VIEW,
	OPT_DEL_VIEW,
	OPT_LIST_VIEW,
	OPT_ADD_ADMIN,
	OPT_DEL_ADMIN,
	OPT_LIST_ADMIN,
	OPT_SYSTEM_ADMIN,
	OPT_ADD_UQUOTA_RECIPIENT,
	OPT_DEL_UQUOTA_RECIPIENT,
	OPT_LIST_UQUOTA_RECIPIENT,
	OPT_ADD_CQUOTA_RECIPIENT,
	OPT_DEL_CQUOTA_RECIPIENT,
	OPT_LIST_CQUOTA_RECIPIENT,
	OPT_PURGE_SOFTDELETE,
	OPT_PURGE_DEFERRED,
	OPT_CLEAR_CACHE,
	OPT_LIST_ORPHANS,
	OPT_CONFIG,
	OPT_UTF8,
	OPT_FORCE_RESYNC,
	OPT_USER_COUNT,
	OPT_ENABLE_FEATURE,
	OPT_DISABLE_FEATURE,
	OPT_SELECT_NODE,
	OPT_RESET_FOLDER_COUNT,
	OPT_VERBOSITY,
	OPT_VERSION,
	OPT_LIST_USERS,
	OPT_LIST_GROUPS,
	OPT_PASSWORD,
	OPT_PASSWORD_PROMPT,
	OPT_FULLNAME,
	OPT_EMAIL,
};

static const struct option long_options[] = {
	{ "create-store", 1, NULL, OPT_CREATE_STORE },
	{ "delete-store", 1, NULL, OPT_DELETE_STORE },
	{ "hook-store", 1, NULL, OPT_HOOK_STORE },
	{ "unhook-store", 1, NULL, OPT_UNHOOK_STORE },
	{ "remove-store", 1, NULL, OPT_REMOVE_STORE },
	{ "copyto-public", 0, NULL, OPT_COPYTO_PUBLIC },
	{ "list-orphans", 0, NULL, OPT_LIST_ORPHANS },
	{ "details", 1, NULL, OPT_DETAILS },
	{ "type", 1, NULL, OPT_DETAILS_TYPE },
	{ "help", 0, NULL, OPT_HELP },
	{ "host", 1, NULL, OPT_HOST },
	{ "sync", 0, NULL, OPT_SYNC_USERS },
	{ "qh", 1, NULL, OPT_USER_QUOTA_HARD },
	{ "qs", 1, NULL, OPT_USER_QUOTA_SOFT },
	{ "qw", 1, NULL, OPT_USER_QUOTA_WARN },
	{ "qo", 1, NULL, OPT_USER_QUOTA_OVERRIDE },
	{ "udqh", 1, NULL, OPT_USER_DEFAULT_QUOTA_HARD },
	{ "udqs", 1, NULL, OPT_USER_DEFAULT_QUOTA_SOFT },
	{ "udqw", 1, NULL, OPT_USER_DEFAULT_QUOTA_WARN },
	{ "udqo", 1, NULL, OPT_USER_DEFAULT_QUOTA_OVERRIDE },
	{ "lang", 1, NULL, OPT_LANG },
	{ "mr-accept", 1, NULL, OPT_MR_ACCEPT },
	{ "mr-decline-conflict", 1, NULL, OPT_MR_DECLINE_CONFLICT },
	{ "mr-decline-recurring", 1, NULL, OPT_MR_DECLINE_RECURRING },
	{ "add-sendas", 1, NULL, OPT_ADD_SENDAS },
	{ "del-sendas", 1, NULL, OPT_DEL_SENDAS },
	{ "list-sendas", 1, NULL, OPT_LIST_SENDAS },
	{ "update-group", 1, NULL, OPT_UPDATE_GROUP },
	{ "create-company", 1, NULL, OPT_CREATE_COMPANY },
	{ "update-company", 1, NULL, OPT_UPDATE_COMPANY },
	{ "delete-company", 1, NULL, OPT_DELETE_COMPANY },
	{ "list-companies", 0, NULL, OPT_LIST_COMPANY },
	{ "add-to-viewlist", 1, NULL, OPT_ADD_VIEW },
	{ "del-from-viewlist", 1, NULL, OPT_DEL_VIEW },
	{ "list-view", 0, NULL, OPT_LIST_VIEW },
	{ "add-to-adminlist", 1, NULL, OPT_ADD_ADMIN },
	{ "del-from-adminlist", 1, NULL, OPT_DEL_ADMIN },
	{ "list-admin", 0, NULL, OPT_LIST_ADMIN },
	{ "set-system-admin", 1, NULL, OPT_SYSTEM_ADMIN },
	{ "add-userquota-recipient", 1, NULL, OPT_ADD_UQUOTA_RECIPIENT },
	{ "del-userquota-recipient", 1, NULL, OPT_DEL_UQUOTA_RECIPIENT },
	{ "list-userquota-recipients", 0, NULL, OPT_LIST_UQUOTA_RECIPIENT },
	{ "add-companyquota-recipient", 1, NULL, OPT_ADD_CQUOTA_RECIPIENT },
	{ "del-companyquota-recipient", 1, NULL, OPT_DEL_CQUOTA_RECIPIENT },
	{ "list-companyquota-recipients", 0, NULL, OPT_LIST_CQUOTA_RECIPIENT },
	{ "purge-softdelete", 1, NULL, OPT_PURGE_SOFTDELETE },
	{ "purge-deferred", 0, NULL, OPT_PURGE_DEFERRED },
	{ "clear-cache", 2, NULL, OPT_CLEAR_CACHE },
	{ "config", 1, NULL, OPT_CONFIG },
	{ "utf8", 0, NULL, OPT_UTF8 },
	{ "force-resync", 0, NULL, OPT_FORCE_RESYNC },
	{ "user-count", 0, NULL, OPT_USER_COUNT },
	{ "enable-feature", 1, NULL, OPT_ENABLE_FEATURE },
	{ "disable-feature", 1, NULL, OPT_DISABLE_FEATURE },
	{ "node", 1, NULL, OPT_SELECT_NODE },
	{ "reset-folder-count", 1, NULL, OPT_RESET_FOLDER_COUNT },
	{ "verbose", required_argument, NULL, OPT_VERBOSITY },
	{ "version", no_argument, NULL, OPT_VERSION },
	{"list-users", no_argument, nullptr, OPT_LIST_USERS},
	{"list-groups", no_argument, nullptr, OPT_LIST_GROUPS},
	{"password", no_argument, nullptr, OPT_PASSWORD},
	{"password-prompt", no_argument, nullptr, OPT_PASSWORD_PROMPT},
	{"fullname", no_argument, nullptr, OPT_FULLNAME},
	{"email", no_argument, nullptr, OPT_EMAIL},
	{ NULL, 0, NULL, 0 }
};

/**
 * Prints all options on screen. This should always be in sync with reality.
 *
 * @param[in]	name	The name of the program (arg[0])
 */
static void print_help(const char *name)
{
	ConsoleTable ct(0,0);
	cout << "Usage:" << endl;
	cout << name << " [action] [options]" << endl << endl;
	cout << "Actions: [-s] | [[-c|-u|-d|-b|-B|--details] username] | [[-g|-G] groupname] | [-l|-L]" << endl;
	ct.Resize(15, 2);
	ct.AddColumn(0, "-s");		ct.AddColumn(1, "Create public store.");
	ct.AddColumn(0, "--sync");	ct.AddColumn(1, "Synchronize users and groups with external source.");
	ct.AddColumn(0, "--clear-cache");			ct.AddColumn(1, "Clear all caches in the server.");
	ct.AddColumn(0, "--purge-softdelete N");	ct.AddColumn(1, "Purge items in marked as softdeleted that are older than N days.");
	ct.AddColumn(0, "--purge-deferred"); 		ct.AddColumn(1, "Purge all items in the deferred update table.");
	ct.AddColumn(0, "-l");		ct.AddColumn(1, "List users. Use -I to list users of a specific company, if applicable.");
	ct.AddColumn(0, "-L");		ct.AddColumn(1, "List groups. Use -I to list groups of a specific company, if applicable.");
	ct.AddColumn(0, "--list-sendas name");	ct.AddColumn(1, "List all users who are allowed to send-as 'name'. Use --type to indicate the object type.");
	ct.AddColumn(0, "--list-companies");	ct.AddColumn(1, "List all companies.");
	ct.AddColumn(0, "--list-view");			ct.AddColumn(1, "List all companies in the remote-view list.");
	ct.AddColumn(0, "--list-admin");		ct.AddColumn(1, "List all users in the remote-admin list.");
	ct.AddColumn(0, "--list-userquota-recipients");		ct.AddColumn(1, "List all additional recipients for a userquota warning email.");
	ct.AddColumn(0, "--list-companyquota-recipients");	ct.AddColumn(1, "List all additional recipients for a companyquota warning email.");
	ct.AddColumn(0, "--details");	ct.AddColumn(1, "Show object details, use --type to indicate the object type.");
	ct.AddColumn(0, "--type type");	ct.AddColumn(1, "Set object type for --details. Values can be \"user\", \"group\" or \"company\".");
	ct.AddColumn(0, "--user-count");	ct.AddColumn(1, "Output the system users counts.");
	ct.PrintTable();
	cout << endl;
	cout << "Additional Actions when using the DB user plugin:" << endl;
	ct.Resize(8,2);
	ct.AddColumn(0, "-c user");	ct.AddColumn(1, "Create user, -p, -f, -e options required, -a and -n are optional. Quota options are optional.");
	ct.AddColumn(0, "-u user");	ct.AddColumn(1, "Update user, -U, -p, -f, -e, -n and -a optional. Quota options are optional.");
	ct.AddColumn(0, "-d user");	ct.AddColumn(1, "Delete user.");
	ct.AddColumn(0, "-g group");	ct.AddColumn(1, "Create group, -e options optional.");
	ct.AddColumn(0, "--update-group group");	ct.AddColumn(1, "Update group, -e optional.");
	ct.AddColumn(0, "-G group");	ct.AddColumn(1, "Delete group.");
	ct.AddColumn(0, "-b user");	ct.AddColumn(1, "Add user to a group, -i required.");
	ct.AddColumn(0, "-B user");	ct.AddColumn(1, "Delete user from a group, -i required.");
	ct.PrintTable();
	cout << endl;
	cout << "Additional Actions when using the Unix user plugin:" << endl;
	ct.Resize(2,2);
	ct.AddColumn(0, "-u user");	ct.AddColumn(1, "Update user, -e and -a optional. Quota options are optional.");
	ct.AddColumn(0, "--update-group group");	ct.AddColumn(1, "Update group, -e optional.");
	ct.PrintTable();
	cout << endl;
	cout << "Additional Actions when using the DB or Unix user plugin:" << endl;
	ct.Resize(2,2);
	ct.AddColumn(0, "--enable-feature feature");	ct.AddColumn(1, "Update user to explicitly enable a feature.");
	ct.AddColumn(0, "--disable-feature feature");	ct.AddColumn(1, "Update user to explicitly disable a feature.");
	ct.PrintTable();
	cout << endl;
	cout << "Additional Actions when using the DB user plugin in hosted mode:" << endl;
	ct.Resize(12,2);
	ct.AddColumn(0, "--create-company name");	ct.AddColumn(1, "Create company space");
	ct.AddColumn(0, "--update-company name");	ct.AddColumn(1, "Update company space");
	ct.AddColumn(0, "--delete-company name");	ct.AddColumn(1, "Delete company space");
	ct.AddColumn(0, "--set-system-admin name");	ct.AddColumn(1, "Set system administrator for the company specified by -I (does not grant Admin privileges)");
	ct.AddColumn(0, "--add-to-viewlist name");	ct.AddColumn(1, "Add company 'name' to remote-view list of company specified by -I");
	ct.AddColumn(0, "--del-from-viewlist name");	ct.AddColumn(1, "Delete company 'name' from remote-view list of company specified by -I");
	ct.AddColumn(0, "--add-to-adminlist name");	ct.AddColumn(1, "Add user 'name' to remote-admin list of company specified by -I");
	ct.AddColumn(0, "--del-from-adminlist name");	ct.AddColumn(1, "Delete user 'name' from remote-admin list of company specified by -I");
	ct.AddColumn(0, "--add-userquota-recipient user");	ct.AddColumn(1, "Add 'user' as recipient to userquota warning emails.");
	ct.AddColumn(0, "--del-userquota-recipient user");	ct.AddColumn(1, "Delete 'user' as recipient to userquota warning emails.");
	ct.AddColumn(0, "--add-companyquota-recipient user");	ct.AddColumn(1, "Add 'user' as recipient to companyquota warning emails.");
	ct.AddColumn(0, "--del-companyquota-recipient user");	ct.AddColumn(1, "Delete 'user' as recipient to companyquota warning emails.");
	ct.PrintTable();
	cout << endl;
	cout << "Options: [-U new username] [-P|-p password] [-f fullname] [-e emailaddress]" << endl;
	cout << "         [-a [y|n]] [-n [y|n]] [-h path] [-i group] [--qo [y|n]] [--qh value] [--qs value] [--qw value]" << endl;
	ct.Resize(22,2);
	ct.AddColumn(0, "-U new username"); ct.AddColumn(1, "Rename username to new username");
	ct.AddColumn(0, "-P"); ct.AddColumn(1, "Prompt for password, can be substituted by '-p pass'");
	ct.AddColumn(0, "-p pass"); ct.AddColumn(1, "Set password to pass, can be substituted by '-P'");
	ct.AddColumn(0, "-f full"); ct.AddColumn(1, "Set fullname to full");
	ct.AddColumn(0, "-e addr"); ct.AddColumn(1, "Set email address to addr");
	ct.AddColumn(0, "-a [y|n]"); ct.AddColumn(1, "Set administrator level for user. yes/no, y/n or 2/1/0.");
	ct.AddColumn(0, "-n [y|n]"); ct.AddColumn(1, "set user to non-active. yes/no, y/n or 1/0.");
	ct.AddColumn(0, "--qo [y|n]"); ct.AddColumn(1, "Override server quota limits. yes/no, y/n or 1/0.");
	ct.AddColumn(0, "--qh hardquota"); ct.AddColumn(1, "Set hardquota limit in Mb");
	ct.AddColumn(0, "--qs softquota"); ct.AddColumn(1, "Set softquota limit in Mb");
	ct.AddColumn(0, "--qw warnquota"); ct.AddColumn(1, "Set warnquota limit in Mb");
	ct.AddColumn(0, "--udqo [y|n]"); ct.AddColumn(1, "Override user default server quota limits for specific company. yes/no, y/n or 1/0.");
	ct.AddColumn(0, "--udqh hardquota"); ct.AddColumn(1, "Set user default hardquota limit for specific company in Mb");
	ct.AddColumn(0, "--udqs softquota"); ct.AddColumn(1, "Set user default softquota limit for specific company in Mb");
	ct.AddColumn(0, "--udqw warnquota"); ct.AddColumn(1, "Set user default warnquota limit for specific company in Mb");
	ct.AddColumn(0, "-i group"); ct.AddColumn(1, "Name of the group");
	ct.AddColumn(0, "-I company"); ct.AddColumn(1, "Name of the company");
	ct.AddColumn(0, "--mr-accept"); ct.AddColumn(1, "(resource) auto-accept meeting requests. yes/no");
	ct.AddColumn(0, "--mr-decline-conflict"); ct.AddColumn(1, "(resource) decline meeting requests for conflicting times. yes/no");
	ct.AddColumn(0, "--mr-decline-recurring"); ct.AddColumn(1, "(resource) decline meeting requests for all recurring items. yes/no");
	ct.AddColumn(0, "--add-sendas name"); ct.AddColumn(1, "Add user 'name' to send-as list of user specified by -u or --update-group");
	ct.AddColumn(0, "--del-sendas name"); ct.AddColumn(1, "Delete user 'name' from send-as list of user specified by -u or --update-group");
	ct.PrintTable();
	cout << endl;
	cout << "The following functions are to control stores of users:" << endl;
	ct.Resize(10,2);
	ct.AddColumn(0, "--list-orphans"); ct.AddColumn(1, "List all users without stores and stores without users.");
	ct.AddColumn(0, "--remove-store storeguid"); ct.AddColumn(1, "Delete orphaned store of user that is deleted from external source.");
	ct.AddColumn(0, "--hook-store storeguid"); ct.AddColumn(1, "Hook orphaned store to a user or copy to a public.");
	ct.AddColumn(0, "  -u username"); ct.AddColumn(1, "Update user to received orphaned store given in --hook-store.");
	ct.AddColumn(0, "  --type"); ct.AddColumn(1, "Type of the user to hook. Defaults to 'user', can be 'group' or 'company' for public store. Use 'archive' for archive stores.");
	ct.AddColumn(0, "  --copyto-public"); ct.AddColumn(1, "Copy the orphan store to the public folder.");
	ct.AddColumn(0, "--unhook-store username"); ct.AddColumn(1, "Unhook store from user.");
	ct.AddColumn(0, "  --type"); ct.AddColumn(1, "Type of the user to hook. Defaults to 'user', can be 'group' or 'company' for public store. Use 'archive' for archive stores.");
	ct.AddColumn(0, ""); ct.AddColumn(1, "Use 'Everyone' as username with type 'group' to unhook the public store, or use the company name and type 'company'.");
	ct.AddColumn(0, "--force-resync [username [username [...]]]"); ct.AddColumn(1, "Force a resynchronisation of offline profiles for specified users.");
	ct.AddColumn(0, "--reset-folder-count username"); ct.AddColumn(1, "Reset the counters on all folder in the store.");
	ct.PrintTable();
	cout << endl;
	cout << "The following functions are for use from the create/delete user/group scripts:" << endl;
	ct.Resize(2,2);
	ct.AddColumn(0, "--create-store user"); ct.AddColumn(1, "Create store for user that exists in external source.");
	ct.AddColumn(0, "--lang language"); ct.AddColumn(1, "Create folders in a new store in this language (e.g. en_EN.UTF-8).");
	ct.PrintTable();
	cout << endl;
	cout << "Note: the list-orphans and create/remove/hook/unhook-store functions only work on the server you're connected to. The commands will not be redirected in a multi-server environment." << endl;
	cout << endl;
	cout << "Global options: [-h|--host path]" << endl;
	ct.Resize(4,2);
	ct.AddColumn(0, "--config file"); ct.AddColumn(1, "Use a configuration file");
	ct.AddColumn(0, "-h path"); ct.AddColumn(1, "Connect through <path>, e.g. file:///var/run/socket");
	ct.AddColumn(0, "--node name"); ct.AddColumn(1, "Execute the command on cluster node <name>");
	ct.AddColumn(0, "--utf8"); ct.AddColumn(1, "Force the current locale to UTF-8");
	ct.AddColumn(0, "-v"); ct.AddColumn(1, "Increase verbosity. A maximum of 7 is possible where 1=fatal errors only, 6=debug and 7=everything.");
	ct.AddColumn(0, "--verbosity x"); ct.AddColumn(1, "Set verbosity to value 'x': 0...7 (0 = disable)");
	ct.AddColumn(0, "-V"); ct.AddColumn(1, "Print version info.");
	ct.AddColumn(0, "--version"); ct.AddColumn(1, "Print version info.");
	ct.AddColumn(0, "--help"); ct.AddColumn(1, "Show this help text.");
	ct.PrintTable();
	cout << endl;
}

/**
 * Reads a password twice from stdin, and doesn't print on stdout.
 *
 * @return	char	The typed password if ok, or NULL when failed.
 */
static char *get_password(void)
{
	static char password[80] = {0};
	auto s = get_password("Type password:");
	if(s == NULL)
		return NULL;
	strncpy(password, s, sizeof(password)-1);
	s = get_password("Retype password:");
	if (s == NULL)
		return NULL;
	if (strcmp(password, s) != 0)
		return NULL;
	return password;
}

/**
 * Parse given string to return 1 for true (yes) or 0 for false (no).
 *
 * @note Does not accept uppercase 'yes'
 *
 * @param[in]	char*	String containing a boolean
 * @return	int	1 for true (yes) or 0 for false (no).
 */
static int parse_yesno(const char *opt)
{
	return opt[0] == 'y' || opt[0] == '1';
}

/**
 * Print quota levels and/or store size.
 *
 * @param[in]	lpQuota			Optional ECQuota object with a users quota settings
 * @param[in]	lpQuotaStatus	Optional ECQuotaStatus object with the storesize of a user
 */
static void print_quota(const ECQUOTA *lpQuota,
    const ECQUOTASTATUS *lpQuotaStatus, bool isPublic = false)
{
	if (lpQuota) {
		// watch the not:
		if (!isPublic)
			cout << "Current user store quota settings:" << endl;
		else
			cout << "Current public store quota settings:" << endl;

		cout << " Quota overrides:\t" << (!lpQuota->bUseDefaultQuota?"yes":"no") << endl;
		cout << " Warning level:\t\t" << str_storage(lpQuota->llWarnSize) << endl;
		if(!isPublic) {
			cout << " Soft level:\t\t" << str_storage(lpQuota->llSoftSize) << endl;
			cout << " Hard level:\t\t" << str_storage(lpQuota->llHardSize) << endl;
		}
	}

	if (lpQuotaStatus == nullptr)
		return;
	if (!isPublic)
		cout << "Current store size:\t";
	else
		cout << "Public store size:\t";
	cout << str_storage(lpQuotaStatus->llStoreSize, false) << endl;
}

/**
 * Set new quota levels for a given user (EntryID). This only works
 * for DB and Unix plugin.
 *
 * @param[in]	lpServiceAdmin	IECServiceAdmin object on the Admin store
 * @param[in]	cbEid		Size of lpEid
 * @param[in]	lpEid		EntryID of a user
 * @param[in]	quota		New yes/no quota override flag setting, or -1 for default quota settings for user
 * @param[in]	udefault	IsUserDefaultQuota setting (Default quota for users within company)
 * @param[in]	warn		New warning level for user, or -1 for default system settings
 * @param[in]	soft		New soft level for user, or -1 for default system settings
 * @param[in]	hard		New hard level for user, or -1 for default system settings
 * @param[in]	print		Prints old and new quota settings for a user (optional, default false)
 */
static HRESULT setQuota(IECServiceAdmin *lpServiceAdmin, ULONG cbEid,
    LPENTRYID lpEid, int quota, bool udefault, long long warn, long long soft,
    long long hard, bool print = false, bool company = false)
{
	memory_ptr<ECQUOTASTATUS> lpsQuotaStatus;
	memory_ptr<ECQUOTA> lpsQuota;
	ECQUOTA sQuota;

	if (lpEid == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpServiceAdmin->GetQuota(cbEid, lpEid, false, &~lpsQuota);
	if (hr != hrSuccess) {
		cerr << "Unable to update quota, probably not found." << endl;
		return hr;
	}
	if (print) {
		cout << "Old quota settings:" << endl;
		print_quota(lpsQuota, NULL, company);
		cout << endl;
	}

	if (quota == -1)
		sQuota.bUseDefaultQuota = lpsQuota->bUseDefaultQuota;
	else
		sQuota.bUseDefaultQuota = !quota;
	sQuota.bIsUserDefaultQuota = udefault;
	sQuota.llHardSize = (hard >= 0) ? hard : lpsQuota->llHardSize;
	sQuota.llSoftSize = (soft >= 0) ? soft : lpsQuota->llSoftSize;
	sQuota.llWarnSize = (warn >= 0) ? warn : lpsQuota->llWarnSize;
	hr = lpServiceAdmin->SetQuota(cbEid, lpEid, &sQuota);
	if(hr != hrSuccess) {
		cerr << "Unable to update quota information." << endl;
		return hr;
	}

	if (!print)
		return hrSuccess;
	hr = lpServiceAdmin->GetQuotaStatus(cbEid, lpEid, &~lpsQuotaStatus);
	if(hr != hrSuccess) {
		cerr << "Unable to request updated quota information: " <<
			GetMAPIErrorMessage(hr) << " (" <<
			stringify_hex(hr) << ")" << endl;
		return hr;
	}
	cout << "New quota settings:" << endl;
	print_quota(&sQuota, lpsQuotaStatus, company);
	cout << endl;
	return hrSuccess;
}

/**
 * Returns the string PR_* name for a set of addressbook properties,
 * not using the type of the tag to compare.
 *
 * @param[in]	ulPropTag	A MAPI proptag
 * @return		string		The PR_ string of a property, or the number.
 */
static string getMapiPropertyString(ULONG ulPropTag)
{
#define PROP_TO_STRING(__proptag) \
	case PROP_ID(__proptag): return #__proptag

	switch (PROP_ID(ulPropTag))
	{
		 PROP_TO_STRING(PR_MANAGER_NAME);
		 PROP_TO_STRING(PR_GIVEN_NAME);
		 PROP_TO_STRING(PR_INITIALS);
		 PROP_TO_STRING(PR_SURNAME);
		 PROP_TO_STRING(PR_DISPLAY_NAME);
		 PROP_TO_STRING(PR_ACCOUNT);
		 PROP_TO_STRING(PR_STREET_ADDRESS);
		 PROP_TO_STRING(PR_LOCALITY);
		 PROP_TO_STRING(PR_STATE_OR_PROVINCE);
		 PROP_TO_STRING(PR_POSTAL_CODE);
		 PROP_TO_STRING(PR_COUNTRY);
		 PROP_TO_STRING(PR_TITLE);
		 PROP_TO_STRING(PR_COMPANY_NAME);
		 PROP_TO_STRING(PR_DEPARTMENT_NAME);
		 PROP_TO_STRING(PR_OFFICE_LOCATION);
		 PROP_TO_STRING(PR_ASSISTANT);
		 PROP_TO_STRING(PR_BUSINESS_TELEPHONE_NUMBER);
		 PROP_TO_STRING(PR_BUSINESS2_TELEPHONE_NUMBER);
		 PROP_TO_STRING(PR_BUSINESS_FAX_NUMBER);
		 PROP_TO_STRING(PR_HOME_TELEPHONE_NUMBER);
		 PROP_TO_STRING(PR_HOME2_TELEPHONE_NUMBER);
		 PROP_TO_STRING(PR_MOBILE_TELEPHONE_NUMBER);
		 PROP_TO_STRING(PR_PAGER_TELEPHONE_NUMBER);
		 PROP_TO_STRING(PR_PRIMARY_FAX_NUMBER);
		 PROP_TO_STRING(PR_COMMENT);
		 PROP_TO_STRING(PR_EMS_AB_MANAGER);
		 PROP_TO_STRING(PR_EMS_AB_REPORTS);
		 PROP_TO_STRING(PR_EMS_AB_IS_MEMBER_OF_DL);
		 PROP_TO_STRING(PR_EMS_AB_PROXY_ADDRESSES);
		 PROP_TO_STRING(PR_EMS_AB_OWNER);
		 PROP_TO_STRING(PR_EMS_AB_MEMBER);
		 PROP_TO_STRING(PR_EMS_AB_X509_CERT);
		 PROP_TO_STRING(PR_EC_ENABLED_FEATURES);
		 PROP_TO_STRING(PR_EC_DISABLED_FEATURES);
		 PROP_TO_STRING(PR_EC_ARCHIVE_SERVERS);
		 PROP_TO_STRING(PR_EC_ARCHIVE_COUPLINGS);
		 default:
		 return stringify_hex(ulPropTag);
	}
}

/**
 * Prints a list of companies, enter or comma separated.
 *
 * @param[in]	cCompanies		Number of companies in lpECCompanies
 * @param[in]	lpECCompanies	Array of ECCompany structs
 * @param[in]	bList			true to list with comma separation, otherwise enters are used.
 */
static void print_companies(unsigned int cCompanies,
    const ECCOMPANY *lpECCompanies, bool bList)
{
	for (unsigned int i = 0; i< cCompanies; ++i) {
		if (!bList)
			cout << ((i > 0) ? ", " : "");
		else
			cout << "\t";
		cout << (LPSTR)lpECCompanies[i].lpszCompanyname;
		if (bList)
			cout << endl;
	}
}

/**
 * Prints a list of groups, enter or comma separated.
 *
 * @param[in]	cGroups		Number of groups in lpECGroups
 * @param[in]	lpECGroups	Array of ECGroup structs
 * @param[in]	bList		true to list with comma's separation, otherwise enters are used.
 */
static void print_groups(unsigned int cGroups, const ECGROUP *lpECGroups,
    bool bList)
{
	for (unsigned int i = 0; i < cGroups; ++i) {
		if (!bList)
			cout << ((i > 0) ? ", " : "");
		else
			cout << "\t";
		cout << (LPSTR)lpECGroups[i].lpszGroupname;
		if (bList)
			cout << endl;
	}
}

/**
 * Prints a list of users, enter or comma separated.
 *
 * @param[in]	cUsers		Number of users in lpECUsers
 * @param[in]	lpECUsers	Array of ECUser structs
 * @param[in]	bShowHomeServer	true to print home server in multiserver environment if available
 */
static void print_users(unsigned int cUsers, const ECUSER *lpECUsers,
    bool bShowHomeServer = false)
{
	ConsoleTable ct(cUsers, bShowHomeServer?3:2);

	ct.SetHeader(0, "Username");
	ct.SetHeader(1, "Fullname");
	if (bShowHomeServer)
		ct.SetHeader(2, "Homeserver");

	for (unsigned int i = 0; i < cUsers; ++i) {
		ct.SetColumn(i, 0, (LPSTR)lpECUsers[i].lpszUsername);
		ct.SetColumn(i, 1, (LPSTR)lpECUsers[i].lpszFullName);
		if (!bShowHomeServer)
			continue;
		if (lpECUsers[i].lpszServername != NULL && *reinterpret_cast<LPSTR>(lpECUsers[i].lpszServername) != '\0')
			ct.SetColumn(i, 2, (LPSTR)lpECUsers[i].lpszServername);
		else
			// make sure we fill in all table parts. not using "<unknown>" tag,
			// since bShowHomeServer can be set to true even on non-multiserver environments
			ct.SetColumn(i, 2, string());
	}
	ct.PrintTable();
}

/**
 * Prints extra addressbook properties of an addressbook object, if present.
 *
 * @param[in]	lpPropmap	SPROPMAP struct, custom addressbook properties
 * @param[in]	lpMVPropmap	MVSPROPMAP struct, custom addressbook multi-valued properties
 * @param[in]	group	Indicate if the caller of this function is print_group_settings
 */
static void print_extra_settings(const SPROPMAP *lpPropmap,
    const MVPROPMAP *lpMVPropmap, bool is_group = false)
{
	unsigned int c = 0;

	if (!lpPropmap->cEntries && !lpMVPropmap->cEntries)
		return;

	ConsoleTable ct(lpPropmap->cEntries + lpMVPropmap->cEntries, 2);
	for (unsigned int i = 0; i < lpPropmap->cEntries; ++i) {
		ct.SetColumn(c, 0, getMapiPropertyString(lpPropmap->lpEntries[i].ulPropId));
		if (PROP_TYPE(lpPropmap->lpEntries[i].ulPropId) == PT_BINARY)
			ct.SetColumn(c, 1, stringify(strlen((LPSTR)lpPropmap->lpEntries[i].lpszValue)) + " bytes");
		else
			ct.SetColumn(c, 1, (LPSTR)lpPropmap->lpEntries[i].lpszValue);
		++c;
	}
	for (unsigned int i = 0; i < lpMVPropmap->cEntries; ++i) {
		string strMVValues;

		if (is_group && (lpMVPropmap->lpEntries[i].ulPropId == PR_EC_ENABLED_FEATURES_A ||
		    lpMVPropmap->lpEntries[i].ulPropId == PR_EC_DISABLED_FEATURES_A))
			continue;

		ct.SetColumn(c, 0, getMapiPropertyString(lpMVPropmap->lpEntries[i].ulPropId));

		if (PROP_TYPE(lpMVPropmap->lpEntries[i].ulPropId) == PT_MV_BINARY) {
			strMVValues = stringify(lpMVPropmap->lpEntries[i].cValues) + " values";
		} else {
			for (int j = 0; j < lpMVPropmap->lpEntries[i].cValues; ++j) {
				if (!strMVValues.empty())
					strMVValues += "; ";

				strMVValues += (LPSTR)lpMVPropmap->lpEntries[i].lpszValues[j];
			}
		}
		ct.SetColumn(c, 1, strMVValues);
		++c;
	}
	if (c == 0)
		return;
	cout << "Mapped properties:" << endl;
	ct.PrintTable();
}

/**
 * Prints company details
 *
 * @param[in]	lpECCompany			ECCompany struct
 * @param[in]	lpECAdministrator	ECUser struct with the administrator of this company
 */
static void print_company_settings(const ECCOMPANY *lpECCompany,
    const ECUSER *lpECAdministrator)
{
	cout << "Companyname:\t\t" << (LPSTR)lpECCompany->lpszCompanyname << endl;
	cout << "Sysadmin:\t\t" << (LPSTR)lpECAdministrator->lpszUsername << endl;
	if (lpECCompany->lpszServername != NULL && *reinterpret_cast<LPSTR>(lpECCompany->lpszServername) != '\0')
		cout << "Home server:\t\t" << (LPSTR)lpECCompany->lpszServername << endl;
	cout << "Address book:\t\t" << (lpECCompany->ulIsABHidden ? "Hidden" : "Visible") << endl;
	print_extra_settings(&lpECCompany->sPropmap, &lpECCompany->sMVPropmap);
}

/**
 * Prints group details
 *
 * @param[in]	lpECGroups	ECGroup struct
 */
static void print_group_settings(const ECGROUP *lpECGroup)
{
	cout << "Groupname:\t\t" << (LPSTR)lpECGroup->lpszGroupname << endl;
	cout << "Fullname:\t\t" << (LPSTR)lpECGroup->lpszFullname << endl;
	cout << "Emailaddress:\t\t" << (LPSTR)lpECGroup->lpszFullEmail << endl;
	cout << "Address book:\t\t" << (lpECGroup->ulIsABHidden ? "Hidden" : "Visible") << endl;
	print_extra_settings(&lpECGroup->sPropmap, &lpECGroup->sMVPropmap, true);
}

/**
 * Converts an objectclass_t (common/ECDefs.h) to a string.
 *
 * @param[in]	eClass	Returns a user readable string for this objectclass
 * @return		string
 */
static string ClassToString(objectclass_t eClass)
{
	switch (eClass) {
	case ACTIVE_USER: return "User";
	case NONACTIVE_USER: return "Shared store";
	case NONACTIVE_ROOM: return "Room";
	case NONACTIVE_EQUIPMENT: return "Equipment";
	case NONACTIVE_CONTACT: return "Contact";
	case DISTLIST_GROUP: return "Group";
	case DISTLIST_SECURITY: return "Security group";
	case DISTLIST_DYNAMIC: return "Dynamic group";
	case CONTAINER_COMPANY: return "Company";
	case CONTAINER_ADDRESSLIST: return "Addresslist";
	default: return "Unknown";
	};
}

static void adm_oof_status(const SPropValue *const prop)
{
	char start_buf[64] = {'\0'}, end_buf[64] = {'\0'};
	time_t ts, now = time(NULL);
	/* o = out of office marked as switched on */
	bool o = prop[2].ulPropTag == PR_EC_OUTOFOFFICE && prop[2].Value.b;
	/* a = is it currently active with respect to time interval */
	bool a = o;

	if (prop[3].ulPropTag == PR_EC_OUTOFOFFICE_FROM) {
		if (FileTimeToTimestamp(prop[3].Value.ft, ts, start_buf, sizeof(start_buf)) == -1)
			return;
		a &= ts <= now;
	}
	if (prop[4].ulPropTag == PR_EC_OUTOFOFFICE_UNTIL) {
		if (FileTimeToTimestamp(prop[4].Value.ft, ts, end_buf, sizeof(start_buf)) == -1)
			return;
		a &= now <= ts;
	}
	if (!o) {
		printf("Out Of Office:          disabled\n");
		return;
	}
	if (start_buf[0] == '\0' && end_buf[0] == '\0') {
		printf("Out Of Office:          enabled\n");
		return;
	}
	printf("Out Of Office:          ");
	if (start_buf[0] != '\0')
		printf("from %s ", start_buf);
	if (end_buf[0] != '\0')
		printf("until %s ", end_buf);
	printf("(currently %s)\n", a ? "active" : "inactive");
}

/**
 * Print user details
 *
 * @param[in]	lpStore				Store of the user
 * @param[in]	lpECUser			ECUser struct with user details
 * @param[in]	bAutoAccept			Meeting request settings of user
 * @param[in]	bDeclineConflict	Meeting request settings of user
 * @param[in]	bDeclineRecurring	Meeting request settings of user
 * @param[in]	lstArchives			List of attached archives
 */
static void print_user_settings(IMsgStore *lpStore, const ECUSER *lpECUser,
    bool bAutoAccept, bool bDeclineConflict, bool bDeclineRecur,
    const ArchiveList &lstArchives)
{
	memory_ptr<SPropValue> lpProps;
	static constexpr const SizedSPropTagArray(6, sptaProps) =
		{6, {PR_LAST_LOGON_TIME, PR_LAST_LOGOFF_TIME,
		PR_EC_OUTOFOFFICE, PR_EC_OUTOFOFFICE_FROM,
		PR_EC_OUTOFOFFICE_UNTIL, CHANGE_PROP_TYPE(PR_EC_SERVER_VERSION, PT_STRING8)}};
	ULONG cValues = 0;

	if (lpStore)
		lpStore->GetProps(sptaProps, 0, &cValues, &~lpProps);

	cout << "Username:\t\t" << (LPSTR)lpECUser->lpszUsername << endl;
	cout << "Fullname:\t\t" << (LPSTR)lpECUser->lpszFullName << endl;
	cout << "Emailaddress:\t\t" << (LPSTR)lpECUser->lpszMailAddress << endl;
	cout << "Active:\t\t\t" << ((lpECUser->ulObjClass==ACTIVE_USER) ? "yes" : "no") << endl;
	if (lpECUser->ulObjClass != ACTIVE_USER)
		cout << "Non-active type:\t" << ClassToString(static_cast<objectclass_t>(lpECUser->ulObjClass)) << endl;
	if (lpECUser->ulObjClass == NONACTIVE_ROOM || lpECUser->ulObjClass == NONACTIVE_EQUIPMENT)
		cout << "Resource capacity:\t" << lpECUser->ulCapacity << endl;
	cout << "Administrator:\t\t" << ((lpECUser->ulIsAdmin >= 1) ? "yes" : "no") << ((lpECUser->ulIsAdmin == 2) ? " (system)" : "") << endl;
	cout << "Address book:\t\t" << (lpECUser->ulIsABHidden ? "Hidden" : "Visible") << endl;
	cout << "Auto-accept meeting req:" << (bAutoAccept ? "yes" : "no") << endl;
	if (bAutoAccept) {
		cout << "Decline dbl meetingreq:\t" << (bDeclineConflict ? "yes" : "no") << endl;
		cout << "Decline recur meet.req:\t" << (bDeclineRecur ? "yes" : "no") << endl;
	}
	if (lpECUser->lpszServername != NULL && *reinterpret_cast<LPSTR>(lpECUser->lpszServername) != '\0')
		cout << "Home server:\t\t" << (LPSTR)lpECUser->lpszServername << endl;

	if (lpProps) {
		time_t logon = 0, logoff = 0;
		char d[64];

		adm_oof_status(lpProps);
		if(lpProps[0].ulPropTag == PR_LAST_LOGON_TIME)
			logon = FileTimeToUnixTime(lpProps[0].Value.ft);
		if(lpProps[1].ulPropTag == PR_LAST_LOGOFF_TIME)
			logoff = FileTimeToUnixTime(lpProps[1].Value.ft);
		if(logon) {
			strftime(d, sizeof(d), "%x %X", localtime(&logon));
			cout << "Last logon:\t\t" << d << std::endl;
		}
		if(logoff) {
			strftime(d, sizeof(d), "%x %X", localtime(&logoff));
			cout << "Last logoff:\t\t" << d << std::endl;
		}
		if (lpProps[5].ulPropTag == sptaProps.aulPropTag[5])
			cout << "Server version:\t\t" << lpProps[5].Value.lpszA << endl;
	}

	print_extra_settings(&lpECUser->sPropmap, &lpECUser->sMVPropmap);

	if (!lstArchives.empty()) {
		cout << "Attached archives:\t" << lstArchives.size() << endl;
		for (const auto &arc : lstArchives) {
			cout << "\t" << arc.FolderName << " in " << arc.StoreName << " (" << arc.StoreGuid << ")";

			if (arc.Rights != ARCHIVE_RIGHTS_ABSENT) {
				if (arc.Rights == ROLE_OWNER)
					cout << " [Read Write]";
				else if (arc.Rights == ROLE_REVIEWER)
					cout << " [Read Only]";
				else
					cout << " [Modified: " << AclRightsToString(arc.Rights) << "]";
			}
			cout << endl;
		}
	}
}

/**
 * Print archive store details on local server
 *
 * @param[in]	lpSession		MAPI session of the internal Kopano System administrator user
 * @param[in]	lpECMsgStore	The IUnknown PR_EC_OBJECT pointer, used as IECServiceAdmin and IExchangeManageStore interface
 * @param[in]	lpszName		Name to resolve, using type in ulClass
 * @return		MAPI error code
 */
static HRESULT print_archive_details(LPMAPISESSION lpSession,
    IECServiceAdmin *ptrServiceAdmin, const char *lpszName)
{
	ULONG cbArchiveId = 0;
	EntryIdPtr ptrArchiveId;
	MsgStorePtr ptrArchive;
	SPropValuePtr ptrArchiveSize;

	auto hr = ptrServiceAdmin->GetArchiveStoreEntryID(LPCTSTR(lpszName), nullptr, 0, &cbArchiveId, &~ptrArchiveId);
	if (hr != hrSuccess) {
		cerr << "No archive found for user '" << lpszName << "'." << endl;
		return hr;
	}
	printf("%-23s %s\n", "Archive store id:", bin2hex(cbArchiveId, ptrArchiveId.get()).c_str());
	hr = lpSession->OpenMsgStore(0, cbArchiveId, ptrArchiveId, &iid_of(ptrArchive), 0, &~ptrArchive);
	if (hr != hrSuccess) {
		cerr << "Unable to open archive." << endl;
		return hr;
	}
	hr = HrGetOneProp(ptrArchive, PR_MESSAGE_SIZE_EXTENDED, &~ptrArchiveSize);
	if (hr != hrSuccess) {
		cerr << "Unable to get archive store size." << endl;
		return hr;
	}

	cout << "Current store size:\t";
	cout << stringify_double((double)ptrArchiveSize->Value.li.QuadPart /1024.0 /1024.0, 2, true) << " MiB" << endl;
	return hrSuccess;
}

static LPMVPROPMAPENTRY FindMVPropmapEntry(ECUSER *lpUser, ULONG ulPropTag)
{
	for (unsigned i = 0; i < lpUser->sMVPropmap.cEntries; ++i)
		if (lpUser->sMVPropmap.lpEntries[i].ulPropId == ulPropTag)
			return &lpUser->sMVPropmap.lpEntries[i];
	return NULL;
}

/**
 * Print the defaults of any user object (user/group/company)
 *
 * Depending on the input ulClass, find the object on the server, and
 * print the details of the object if found.
 *
 * @param[in]	lpSession		MAPI session of the internal Kopano System administrator user
 * @param[in]	lpECMsgStore	The IUnknown PR_EC_OBJECT pointer, used as IECServiceAdmin and IExchangeManageStore interface
 * @param[in]	ulClass			addressbook objectclass of input lpszName
 * @param[in]	lpszName		Name to resolve, using type in ulClass
 * @return		MAPI error code
 */
static HRESULT print_details(LPMAPISESSION lpSession,
    IECServiceAdmin *lpServiceAdmin, objectclass_t ulClass,
    const char *lpszName)
{
	memory_ptr<ECUSER> lpECUser;
	memory_ptr<ECGROUP> lpECGroup, lpECGroups;
	memory_ptr<ECCOMPANY> lpECCompany, lpECViews;
	memory_ptr<ECQUOTASTATUS> lpsQuotaStatus;
	memory_ptr<ECQUOTA> lpsQuota;
	memory_ptr<ECUSER> lpECUsers, lpECAdmins;
	ULONG cGroups = 0, cUsers = 0, cAdmins = 0, cViews = 0, cbEntryID = 0;
	memory_ptr<ENTRYID> lpEntryID;
	object_ptr<IMsgStore> lpStore;
	object_ptr<IExchangeManageStore> lpIEMS;
	bool bAutoAccept = false, bDeclineConflict = false, bDeclineRecurring = false;
	ULONG cbObjectId = 0;
	memory_ptr<ENTRYID> lpObjectId;
	ArchiveManagePtr ptrArchiveManage;
	ArchiveList lstArchives;
	convert_context converter;
	HRESULT hr = hrSuccess;

	switch (ulClass) {
	case OBJECTCLASS_CONTAINER:
	case CONTAINER_COMPANY:
		hr = lpServiceAdmin->ResolveCompanyName((LPTSTR)lpszName, 0, &cbObjectId, &~lpObjectId);
		if (hr != hrSuccess) {
			cerr << "Unable to resolve company: " << getMapiCodeString(hr, lpszName) << endl;
			return hr;
		}
		printf("Company object id: %s\n", bin2hex(cbObjectId, lpObjectId).c_str());
		hr = lpServiceAdmin->GetCompany(cbObjectId, lpObjectId, 0, &~lpECCompany);
		if (hr != hrSuccess) {
			cerr << "Unable to show company details: " << getMapiCodeString(hr) << endl;
			return hr;
		}
		hr = lpServiceAdmin->QueryInterface(IID_IExchangeManageStore, &~lpIEMS);
		if (hr != hrSuccess) {
			cerr << "Unable to get admin interface." << endl;
			return hr;
		}
		hr = lpIEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(""), lpECCompany->lpszCompanyname, 0, &cbEntryID, &~lpEntryID);
		if (hr != hrSuccess) {
			cerr << "Unable to get company store entry id. Company possibly has no store." << endl;
			return hr;
		}
		printf("%-23s %s\n", "Company object id:", bin2hex(cbEntryID, lpEntryID).c_str());
		hr = lpSession->OpenMsgStore(0, cbEntryID, lpEntryID, &IID_IMsgStore, MDB_WRITE, &~lpStore);
		if (hr != hrSuccess) {
			cerr << "Unable to open company store." << endl;
			return hr;
		}
		hr = lpServiceAdmin->GetUser(lpECCompany->sAdministrator.cb, (LPENTRYID)lpECCompany->sAdministrator.lpb, 0, &~lpECUser);
		if (hr != hrSuccess) {
			cerr << "Unable to resolve company administrator: " << getMapiCodeString(hr) << endl;
			return hr;
		}
		hr = lpServiceAdmin->GetRemoteAdminList(cbObjectId, lpObjectId, 0, &cAdmins, &~lpECAdmins);
		if (hr != hrSuccess) {
			cerr << "Unable to display remote-admin list: " << getMapiCodeString(hr) << endl;
			hr = hrSuccess; /* Don't make error fatal */
		}
		hr = lpServiceAdmin->GetRemoteViewList(cbObjectId, lpObjectId, 0, &cViews, &~lpECViews);
		if (hr != hrSuccess) {
			cerr << "Unable to display remote-view list: " << getMapiCodeString(hr) << endl;
			hr = hrSuccess; /* Don't make error fatal */
		}
		print_company_settings(lpECCompany, lpECUser);
		break;
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		hr = lpServiceAdmin->ResolveGroupName((LPTSTR)lpszName, 0, &cbObjectId, &~lpObjectId);
		if (hr != hrSuccess) {
			cerr << "Unable to resolve group: " << getMapiCodeString(hr, lpszName) << endl;
			return hr;
		}
		printf("%-23s %s\n", "Group object id:", bin2hex(cbObjectId, lpObjectId).c_str());
		hr = lpServiceAdmin->GetGroup(cbObjectId, lpObjectId, 0, &~lpECGroup);
		if (hr != hrSuccess) {
			cerr << "Unable to show group details: " << getMapiCodeString(hr) << endl;
			return hr;
		}
		hr = lpServiceAdmin->GetUserListOfGroup(cbObjectId, lpObjectId, 0, &cUsers, &~lpECUsers);
		if (hr != hrSuccess) {
			cerr << "Unable to request users for group: " << getMapiCodeString(hr) << endl;
			hr = hrSuccess; /* Don't make error fatal */
		}
		print_group_settings(lpECGroup);
		break;
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
	default:
		hr = lpServiceAdmin->ResolveUserName((LPTSTR)lpszName, 0, &cbObjectId, &~lpObjectId);
		if (hr != hrSuccess) {
			cerr << "Unable to resolve user: " << getMapiCodeString(hr, lpszName) << endl;
			return hr;
		}
		printf("%-23s %s\n", "User object id:", bin2hex(cbObjectId, lpObjectId).c_str());
		hr = lpServiceAdmin->GetUser(cbObjectId, lpObjectId, 0, &~lpECUser);
		if (hr != hrSuccess) {
			cerr << "Unable to show user details: " << getMapiCodeString(hr) << endl;
			return hr;
		}
		hr = lpServiceAdmin->QueryInterface(IID_IExchangeManageStore, &~lpIEMS);
		if (hr != hrSuccess) {
			cerr << "Unable to get admin interface." << endl;
			return hr;
		}
		hr = lpIEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(""), lpECUser->lpszUsername, 0, &cbEntryID, &~lpEntryID);
		if (hr != hrSuccess) {
			cerr << "WARNING: Unable to get user store entry id. User possibly has no store." << endl << endl;
			lpStore.reset();
			forcedExitCode = 1;
		}
		else {
			hr = lpSession->OpenMsgStore(0, cbEntryID, lpEntryID, &IID_IMsgStore, MDB_WRITE, &~lpStore);
			if (hr != hrSuccess) {
				cerr << "Unable to open user store." << endl;
				return hr;
			}
			GetAutoAcceptSettings(lpStore, &bAutoAccept, &bDeclineConflict, &bDeclineRecurring);
			/* Ignore return value */
		}
		printf("%-23s %s\n", "User store id:", bin2hex(cbEntryID, lpEntryID).c_str());
		hr = lpServiceAdmin->GetGroupListOfUser(cbObjectId, lpObjectId, 0, &cGroups, &~lpECGroups);
		if (hr != hrSuccess) {
			cerr << "Unable to request groups for user: " << getMapiCodeString(hr) << endl;
			hr = hrSuccess; /* Don't make error fatal */
		}
		hr = ArchiveManage::Create(lpSession, NULL, converter.convert_to<LPTSTR>(lpszName), &ptrArchiveManage);
		if (hr != hrSuccess) {
			if (hr != MAPI_E_NOT_FOUND)
				cerr << "Error while obtaining archive details: " << getMapiCodeString(hr) << endl;
			hr = hrSuccess; /* Don't make error fatal */
		}
		if (ptrArchiveManage.get() != NULL) {
			hr = ptrArchiveManage->ListArchives(&lstArchives, "Root Folder");
			if (hr != hrSuccess) {
				cerr << "Error while obtaining archive list: " << getMapiCodeString(hr) << endl;
				hr = hrSuccess; /* Don't make error fatal */
			}
		}
		print_user_settings(lpStore, lpECUser, bAutoAccept, bDeclineConflict, bDeclineRecurring, lstArchives);
		break;
	}

	/* Group quota is not completely implemented at this time on the server... */
	if (ulClass != DISTLIST_GROUP) {
		hr = lpServiceAdmin->GetQuota(cbObjectId, lpObjectId, false, &~lpsQuota);
		if (hr != hrSuccess) {
			cerr << "Unable to show object quota: " << getMapiCodeString(hr) << endl;
			hr = hrSuccess; /* Don't make error fatal */
		} else {
			hr = Util::HrGetQuotaStatus(lpStore, lpsQuota, &~lpsQuotaStatus);
			if (hr != hrSuccess) {
				cerr << "Unable to show object quota information: " << getMapiCodeString(hr) << endl;
				hr = hrSuccess; /* Don't make error fatal */
			} else
				print_quota(lpsQuota, lpsQuotaStatus, (ulClass == CONTAINER_COMPANY));
		}
	}

	if (ulClass == CONTAINER_COMPANY) {
		hr = lpServiceAdmin->GetQuota(cbObjectId, lpObjectId, true, &~lpsQuota);
		if (hr != hrSuccess) {
			cerr << "Unable to get user default quota for company: " << getMapiCodeString(hr) << endl;
			hr = hrSuccess; /* not fatal */
		} else
			print_quota(lpsQuota, NULL, false);
	}

	if (cUsers) {
		cout << "Users (" << cUsers << "):" << endl;
		print_users(cUsers, lpECUsers, true);
		cout << endl;
	}
	if (cGroups) {
		cout << "Groups (" << cGroups << "):" << endl;
		print_groups(cGroups, lpECGroups, true);
		cout << endl;
	}
	if (cAdmins) {
		cout << "Remote admins (" << cAdmins << "):" << endl;
		print_users(cAdmins, lpECAdmins);
		cout << endl;
	}
	if (cViews) {
		cout << "Remote viewers (" << cViews << "):" << endl;
		print_companies(cViews, lpECViews, true);
		cout << endl;
	}
	if (lpECUser == nullptr)
		return hr;

	LPMVPROPMAPENTRY lpArchiveServers = FindMVPropmapEntry(lpECUser, PR_EC_ARCHIVE_SERVERS_A);
	if (lpArchiveServers == nullptr || lpArchiveServers->cValues == 0)
		return hr;

	MsgStorePtr ptrAdminStore;
	hr = lpServiceAdmin->QueryInterface(IID_IMsgStore, &~ptrAdminStore);
	if (hr != hrSuccess)
		return hr;

	for (int i = 0; i < lpArchiveServers->cValues; ++i) {
		MsgStorePtr ptrRemoteAdminStore;
		SPropValuePtr ptrPropValue;

		cout << "Archive details on node '" << (LPSTR)lpArchiveServers->lpszValues[i] << "':" << endl;
		auto hrTmp = HrGetRemoteAdminStore(lpSession, ptrAdminStore, lpArchiveServers->lpszValues[i], 0, &~ptrRemoteAdminStore);
		if (FAILED(hrTmp)) {
			cerr << "Unable to access node '" <<
				(LPSTR)lpArchiveServers->lpszValues[i] <<
				"': " << GetMAPIErrorMessage(hr) <<
				"(" << stringify_hex(hrTmp) <<
				")" << endl;
			continue;
		}

		object_ptr<IECServiceAdmin> svcadm;
		hr = GetECObject(ptrRemoteAdminStore, iid_of(svcadm), &~svcadm);
		if (hr != hrSuccess) {
			cerr << "Admin object not found." << endl;
			return hr;
		}
		print_archive_details(lpSession, svcadm, lpszName);
		cout << endl;
	}
	return hr;
}

/**
 * Print a list of all users within a company.
 *
 * @param[in]	lpServiceAdmin	IECServiceAdmin on SYSTEM store
 * @param[in]	lpCompany		The company to request users from. NULL EntryID in a non-hosted environment
 * @return		MAPI Error code
 */
static HRESULT ListUsers(IECServiceAdmin *lpServiceAdmin, ECCOMPANY *lpCompany)
{
	memory_ptr<ECUSER> lpECUsers;
	ULONG		cUsers = 0;

	auto hr = lpServiceAdmin->GetUserList(lpCompany->sCompanyId.cb, (LPENTRYID)lpCompany->sCompanyId.lpb, 0, &cUsers, &~lpECUsers);
	if (hr != hrSuccess) {
		cerr << "Unable to list users: " << getMapiCodeString(hr) << endl;
		return hr;
	}

	cout << "User list for " << (LPSTR)lpCompany->lpszCompanyname << "("<< cUsers <<"):" << endl;
	print_users(cUsers, lpECUsers, true);
	cout << endl;
	return hrSuccess;
}

/**
 * Print a list of all groups within a company.
 *
 * @param[in]	lpServiceAdmin	IECServiceAdmin on SYSTEM store
 * @param[in]	lpCompany		The company to request users from. NULL EntryID in a non-hosted environment
 * @return		HRESULT			MAPI Error code
 */
static HRESULT ListGroups(IECServiceAdmin *lpServiceAdmin,
    ECCOMPANY *lpCompany)
{
	memory_ptr<ECGROUP> lpECGroups;
	ULONG		cGroups = 0;

	auto hr = lpServiceAdmin->GetGroupList(lpCompany->sCompanyId.cb, (LPENTRYID)lpCompany->sCompanyId.lpb, 0, &cGroups, &~lpECGroups);
	if (hr != hrSuccess) {
		cerr << "Unable to list groups: " << getMapiCodeString(hr) << endl;
		return hr;
	}

	cout << "Group list for " << (LPSTR)lpCompany->lpszCompanyname << "("<< cGroups <<"):" << endl;
	cout << "\t" << "groupname" << "" << endl;
	cout << "\t-------------------------------------" << endl;
	print_groups(cGroups, lpECGroups, true);
	cout << endl;
	return hrSuccess;
}

/**
 * Call the sync function to flush user cache on the server.
 *
 * @param[in]	lpServiceAdmin	IECServiceAdmin on SYSTEM store
 * @return		HRESULT			MAPI Error code
 */
static HRESULT SyncUsers(IECServiceAdmin *lpServiceAdmin)
{
	// we don't sync one company, since the complete cache is flushed in the server
	auto hr = lpServiceAdmin->SyncUsers(0, nullptr);
	if (hr != hrSuccess)
		cerr << "User/group synchronization failed: " << getMapiCodeString(hr) << endl;
	return hr;
}

/**
 * Loop a function over one or a list of companies.
 *
 * @param[in]	lpServiceAdmin	IECServiceAdmin on SYSTEM store
 * @param[in]	lpszCompanyname	Only work on given company. NULL to work on all companies available.
 * @param[in]	lpWork			Function to call given any company found in this function.
 * @return		HRESULT			MAPI Error code
 */
static HRESULT ForEachCompany(IECServiceAdmin *lpServiceAdmin,
    const char *lpszCompanyName,
    HRESULT (*lpWork)(IECServiceAdmin *, ECCOMPANY *))
{
	HRESULT hr = hrSuccess;
	ULONG cbCompanyId = 0;
	memory_ptr<ENTRYID> lpCompanyId;
	ULONG cCompanies = 0;
	ECCOMPANY *lpECCompanies = NULL;
	memory_ptr<ECCOMPANY> lpECCompaniesAlloc;
	ECCOMPANY sRootCompany = {{g_cbSystemEid, g_lpSystemEid}, (LPTSTR)"Default", NULL, {0, NULL}};

	if (lpszCompanyName) {
		hr = lpServiceAdmin->ResolveCompanyName((LPTSTR)lpszCompanyName, 0, &cbCompanyId, &~lpCompanyId);
		if (hr != hrSuccess) {
			cerr << "Failed to resolve company name: " << getMapiCodeString(hr, lpszCompanyName) << endl;
			return hr;
		}

		cCompanies = 1;
		sRootCompany.sCompanyId.cb = cbCompanyId;
		sRootCompany.sCompanyId.lpb = reinterpret_cast<unsigned char *>(lpCompanyId.get());
		sRootCompany.lpszCompanyname = (LPTSTR)lpszCompanyName;
		lpECCompanies = &sRootCompany;
	} else {
		hr = lpServiceAdmin->GetCompanyList(0, &cCompanies, &~lpECCompaniesAlloc);
		if (hr != hrSuccess) {
			cCompanies = 1;
			lpECCompanies = &sRootCompany;
			hr = hrSuccess;
		} else {
			lpECCompanies = lpECCompaniesAlloc;
		}
	}

	if (cCompanies == 0) {
		cerr << "No companies found." << endl;
		return hr;
	}
	for (unsigned int i = 0; i < cCompanies; ++i) {
		hr = lpWork(lpServiceAdmin, &lpECCompanies[i]);
		if (hr != hrSuccess)
			return hr;
	}
	return hr;
}

static HRESULT ForceResyncFor(LPMAPISESSION lpSession, LPMDB lpAdminStore,
    const char *lpszAccount, const char *lpszHomeMDB)
{
	ExchangeManageStorePtr ptrEMS;
	ULONG cbEntryID = 0;
	EntryIdPtr ptrEntryID;
	MsgStorePtr ptrUserStore;
	MAPIFolderPtr ptrRoot;
	SPropValuePtr ptrPropResyncID;
	ULONG ulType = 0;

	auto hr = lpAdminStore->QueryInterface(iid_of(ptrEMS), &~ptrEMS);
	if (hr != hrSuccess)
		return hr;
	hr = ptrEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(lpszHomeMDB), reinterpret_cast<const TCHAR *>(lpszAccount), 0, &cbEntryID, &~ptrEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = lpSession->OpenMsgStore(0, cbEntryID, ptrEntryID, NULL, MDB_WRITE|MAPI_DEFERRED_ERRORS, &~ptrUserStore);
	if (hr != hrSuccess)
		return hr;
	hr = ptrUserStore->OpenEntry(0, nullptr, &iid_of(ptrRoot), MAPI_MODIFY, &ulType, &~ptrRoot);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrRoot, PR_EC_RESYNC_ID, &~ptrPropResyncID);
	if (hr == MAPI_E_NOT_FOUND) {
		SPropValue sPropResyncID;
		sPropResyncID.ulPropTag = PR_EC_RESYNC_ID;
		sPropResyncID.Value.ul = 1;
		return HrSetOneProp(ptrRoot, &sPropResyncID);
	} else if (hr != hrSuccess) {
		return hr;
	}
	++ptrPropResyncID->Value.ul;
	return HrSetOneProp(ptrRoot, ptrPropResyncID);
}

static HRESULT ForceResyncAll(LPMAPISESSION lpSession, LPMDB lpAdminStore)
{
	AddrBookPtr		ptrAdrBook;
	ABContainerPtr	ptrABContainer;
	MAPITablePtr	ptrTable;
	SRowSetPtr	ptrRows;
	ULONG			ulType = 0;
	bool			bFail = false;
	static constexpr const SizedSPropTagArray(1, sGALProps) = {1, {PR_ENTRYID}};
	SPropValue			  sGALPropVal;
	static constexpr const SizedSPropTagArray(2, sContentsProps) =
		{2, {PR_ACCOUNT, PR_EMS_AB_HOME_MDB}};
	SPropValue			  sObjTypePropVal;
	SPropValue			  sDispTypePropVal;

	auto hr = lpSession->OpenAddressBook(0, &iid_of(ptrAdrBook), AB_NO_DIALOG, &~ptrAdrBook);
	if (hr != hrSuccess)
		return hr;
	hr = ptrAdrBook->OpenEntry(0, nullptr, &iid_of(ptrABContainer), 0, &ulType, &~ptrABContainer);
	if (hr != hrSuccess)
		return hr;
	hr = ptrABContainer->GetHierarchyTable(0, &~ptrTable);
	if (hr != hrSuccess)
		return hr;

	sGALPropVal.ulPropTag = PR_AB_PROVIDER_ID;
	sGALPropVal.Value.bin.cb = sizeof(GUID);
	sGALPropVal.Value.bin.lpb = (LPBYTE)&MUIDECSAB;

	hr = ptrTable->SetColumns(sGALProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ECPropertyRestriction(RELOP_EQ, PR_AB_PROVIDER_ID, &sGALPropVal, ECRestriction::Cheap)
	     .RestrictTable(ptrTable, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->QueryRows(1, 0, &~ptrRows);
	if (hr != hrSuccess)
		return hr;
	if (ptrRows.size() != 1 || ptrRows[0].lpProps[0].ulPropTag != PR_ENTRYID)
		return MAPI_E_NOT_FOUND;
	hr = ptrAdrBook->OpenEntry(ptrRows[0].lpProps[0].Value.bin.cb, reinterpret_cast<ENTRYID *>(ptrRows[0].lpProps[0].Value.bin.lpb),
	     &iid_of(ptrABContainer), MAPI_BEST_ACCESS, &ulType, &~ptrABContainer);
	if (hr != hrSuccess)
		return hr;
	hr = ptrABContainer->GetContentsTable(0, &~ptrTable);
	if (hr != hrSuccess)
		return hr;

	sObjTypePropVal.ulPropTag = PR_OBJECT_TYPE;
	sObjTypePropVal.Value.l = MAPI_MAILUSER;
	sDispTypePropVal.ulPropTag = PR_DISPLAY_TYPE;
	sDispTypePropVal.Value.l = DT_MAILUSER;

	hr = ptrTable->SetColumns(sContentsProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ECAndRestriction(
			ECPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, &sObjTypePropVal, ECRestriction::Cheap) +
			ECPropertyRestriction(RELOP_EQ, PR_DISPLAY_TYPE, &sDispTypePropVal, ECRestriction::Cheap)
		).RestrictTable(ptrTable, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		hr = ptrTable->QueryRows(50, 0, &~ptrRows);
		if (hr != hrSuccess)
			goto exit;
		if (ptrRows.empty())
			break;

		for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
			if (PROP_TYPE(ptrRows[i].lpProps[0].ulPropTag) == PT_ERROR ||
			    PROP_TYPE(ptrRows[i].lpProps[1].ulPropTag) == PT_ERROR)
			{
				cerr << "Ignoring incomplete entry." << endl;
				continue;
			}

			hr = ForceResyncFor(lpSession, lpAdminStore, ptrRows[i].lpProps[0].Value.lpszA, ptrRows[i].lpProps[1].Value.lpszA);
			if (hr == hrSuccess)
				continue;
			cerr << "Failed to force resync for user " <<
				ptrRows[i].lpProps[0].Value.lpszA <<
				": " << GetMAPIErrorMessage(hr) <<
				" (" << stringify_hex(hr) << ")" <<
				endl;
			bFail = true;
		}
	}

exit:
	if (!FAILED(hr) && bFail)
		hr = MAPI_W_ERRORS_RETURNED;
	return hr;
}

static HRESULT ForceResync(LPMAPISESSION lpSession, LPMDB lpAdminStore,
    const std::list<std::string> &lstUsernames)
{
	HRESULT hr = hrSuccess;
	bool bFail = false;

	for (const auto &user : lstUsernames) {
		hr = ForceResyncFor(lpSession, lpAdminStore, user.c_str(), NULL);
		if (hr == hrSuccess)
			continue;
		cerr << "Failed to force resync for user " <<
			user << ": " << GetMAPIErrorMessage(hr) <<
			" (" << stringify_hex(hr) << ")" << endl;
		bFail = true;
	}

	if (!FAILED(hr) && bFail)
		return MAPI_W_ERRORS_RETURNED;
	return hr;
}

static HRESULT DisplayUserCount(LPMDB lpAdminStore)
{
	MAPITablePtr ptrSystemTable;
	SPropValue sPropDisplayName;
	SRowSetPtr ptrRows;
	ULONG ulLicensedUsers = 0;
	ULONG ulActiveUsers = (ULONG)-1;	//!< used active users
	ULONG ulNonActiveTotal = (ULONG)-1;	//!< used non-active users
	ULONG ulNonActiveUsers = (ULONG)-1;	//!< used shared stores, subset of used non-active users
	ULONG ulRooms = (ULONG)-1;			//!< used rooms, subset of used non-active users
	ULONG ulEquipment = (ULONG)-1;		//!< used equipment, subset of used non-active users
	ULONG ulMaxTotal = 0;				//!< complete total of user objects allowed by license, aka ulNonActiveHigh limit
	ULONG ulNonActiveLow = 0; //!< at least non-active users allowed
	ULONG ulActiveAsNonActive = 0;		//!< non-active users taken from active count
	ConsoleTable ct(3, 4);
	ULONG ulExtraRow = 0;
	ULONG ulExtraRows = 0;
	static constexpr const SizedSPropTagArray(2, sptaStatsProps) =
		{2, {PR_DISPLAY_NAME_A, PR_EC_STATS_SYSTEM_VALUE}};
	enum {IDX_DISPLAY_NAME_A, IDX_EC_STATS_SYSTEM_VALUE};
	enum {COL_ALLOWED=1, COL_USED, COL_AVAILABLE};

	auto hr = lpAdminStore->OpenProperty(PR_EC_STATSTABLE_SYSTEM, &iid_of(ptrSystemTable), 0, 0, &~ptrSystemTable);
	if (hr != hrSuccess)
		return hr;

	sPropDisplayName.ulPropTag = PR_DISPLAY_NAME_A;
	sPropDisplayName.Value.lpszA = const_cast<char *>("usercnt_");

	hr = ECContentRestriction(FL_PREFIX, PR_DISPLAY_NAME_A, &sPropDisplayName, ECRestriction::Cheap)
	     .RestrictTable(ptrSystemTable, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ptrSystemTable->SetColumns(sptaStatsProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	hr = ptrSystemTable->QueryRows(0xffff, 0, &~ptrRows);
	if (hr != hrSuccess)
		return hr;
	// We expect at least the first 3
	if (ptrRows.size() < 3)
		return MAPI_E_NOT_FOUND;

	for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
		const char *lpszDisplayName = ptrRows[i].lpProps[IDX_DISPLAY_NAME_A].Value.lpszA;

		if (strcmp(lpszDisplayName, "usercnt_licensed") == 0)
			ulLicensedUsers = atoui(ptrRows[i].lpProps[IDX_EC_STATS_SYSTEM_VALUE].Value.lpszA);
		else if (strcmp(lpszDisplayName, "usercnt_active") == 0)
			ulActiveUsers = atoui(ptrRows[i].lpProps[IDX_EC_STATS_SYSTEM_VALUE].Value.lpszA);
		else if (strcmp(lpszDisplayName, "usercnt_nonactive") == 0)
			ulNonActiveTotal = atoui(ptrRows[i].lpProps[IDX_EC_STATS_SYSTEM_VALUE].Value.lpszA);
		else if (strcmp(lpszDisplayName, "usercnt_na_user") == 0)
			ulNonActiveUsers = atoui(ptrRows[i].lpProps[IDX_EC_STATS_SYSTEM_VALUE].Value.lpszA);
		else if (strcmp(lpszDisplayName, "usercnt_room") == 0)
			ulRooms = atoui(ptrRows[i].lpProps[IDX_EC_STATS_SYSTEM_VALUE].Value.lpszA);
		else if (strcmp(lpszDisplayName, "usercnt_equipment") == 0)
			ulEquipment = atoui(ptrRows[i].lpProps[IDX_EC_STATS_SYSTEM_VALUE].Value.lpszA);
	}

	if (ulLicensedUsers == static_cast<ULONG>(-1) ||
	    ulActiveUsers == static_cast<ULONG>(-1) ||
	    ulNonActiveTotal == static_cast<ULONG>(-1))
		return MAPI_E_NOT_FOUND;
	if (ulNonActiveUsers != (ULONG)-1)
		++ulExtraRows;
	if (ulRooms != (ULONG)-1)
		++ulExtraRows;
	if (ulEquipment != (ULONG)-1)
		++ulExtraRows;
	if (ulExtraRows > 0)
		ct.Resize(3 + ulExtraRows, 4);

	ulMaxTotal = std::max(ulLicensedUsers + 25, (ulLicensedUsers *5)/ 2);
	ulNonActiveLow = ulMaxTotal - ulLicensedUsers;
	ulActiveAsNonActive = (ulNonActiveTotal > ulNonActiveLow) ? ulNonActiveTotal - ulNonActiveLow : 0;

	cout << "User counts:" << endl;
	ct.SetHeader(COL_ALLOWED, "Allowed");
	ct.SetHeader(COL_USED, "Used");
	ct.SetHeader(COL_AVAILABLE, "Available");
	ct.SetColumn(0, 0, "Active");
	ct.SetColumn(0, COL_USED, stringify(ulActiveUsers));
	if (ulLicensedUsers == 0) {
		ct.SetColumn(0, COL_ALLOWED, "no limit");
		ct.SetColumn(0, COL_AVAILABLE, "-");
	} else {
		ct.SetColumn(0, COL_ALLOWED, stringify(ulLicensedUsers));
		ct.SetColumn(0, COL_AVAILABLE, stringify_signed(ulLicensedUsers - ulActiveUsers - ulActiveAsNonActive));
	}

	ct.SetColumn(1, 0, "Non-active");
	if (ulNonActiveTotal > ulNonActiveLow)
		ct.SetColumn(1, COL_USED, stringify(ulNonActiveLow) + " + " + stringify(ulActiveAsNonActive));
	else
		ct.SetColumn(1, COL_USED, stringify(ulNonActiveTotal));
	if (ulLicensedUsers == 0) {
		ct.SetColumn(1, COL_ALLOWED, "no limit");
		ct.SetColumn(1, COL_AVAILABLE, "-");
	} else {
		ct.SetColumn(1, COL_ALLOWED, stringify_signed(ulMaxTotal - ulLicensedUsers));
		if (ulNonActiveTotal > ulNonActiveLow)
			ct.SetColumn(1, COL_AVAILABLE, "0 (+" + stringify_signed(ulLicensedUsers - ulActiveUsers - ulActiveAsNonActive) + ")");
		else
			ct.SetColumn(1, COL_AVAILABLE, stringify_signed(ulNonActiveLow - ulNonActiveTotal) +
					" (+" + stringify_signed(ulLicensedUsers - ulActiveUsers - ulActiveAsNonActive) + ")");
	}

	if (ulNonActiveUsers != (ULONG)-1) {
		ct.SetColumn(2 + ulExtraRow, 0, "  Users");
		ct.SetColumn(2 + ulExtraRow, COL_USED, stringify(ulNonActiveUsers));
		++ulExtraRow;
	}
	if (ulRooms != (ULONG)-1) {
		ct.SetColumn(2 + ulExtraRow, 0, "  Rooms");
		ct.SetColumn(2 + ulExtraRow, COL_USED, stringify(ulRooms));
		++ulExtraRow;
	}
	if (ulEquipment != (ULONG)-1) {
		ct.SetColumn(2 + ulExtraRow, 0, "  Equipment");
		ct.SetColumn(2 + ulExtraRow, COL_USED, stringify(ulEquipment));
		++ulExtraRow;
	}

	ct.SetColumn(2 + ulExtraRows, 0, "Total");
	ct.SetColumn(2 + ulExtraRows, COL_USED, stringify(ulActiveUsers + ulNonActiveTotal));
	// available & allowed columns are too confusing in totals field.
	ct.SetColumn(2 + ulExtraRows, COL_AVAILABLE, string()); // add empty last column to make sure we print this row
	ct.PrintTable();
	return hrSuccess;
}

static HRESULT ResetFolderCount(LPMAPISESSION lpSession, LPMDB lpAdminStore,
    const char *lpszAccount)
{
	ExchangeManageStorePtr ptrEMS;
	ULONG cbEntryID;
	EntryIdPtr ptrEntryID;
	ULONG ulType = 0;
	MsgStorePtr ptrUserStore;
	MAPIFolderPtr ptrRoot;
	ECServiceAdminPtr ptrServiceAdmin;
	SPropValuePtr ptrPropEntryID;
	ULONG ulUpdates = 0;
	ULONG bFailures = false;
	ULONG ulTotalUpdates = 0;
	MAPITablePtr ptrTable;
	SRowSetPtr ptrRows;
	static constexpr const SizedSPropTagArray(2, sptaTableProps) =
		{2, {PR_DISPLAY_NAME_A, PR_ENTRYID}};
	enum {IDX_DISPLAY_NAME, IDX_ENTRYID};

	auto hr = lpAdminStore->QueryInterface(iid_of(ptrEMS), &~ptrEMS);
	if (hr != hrSuccess)
		return hr;
	hr = ptrEMS->CreateStoreEntryID(nullptr, reinterpret_cast<const TCHAR *>(lpszAccount), 0, &cbEntryID, &~ptrEntryID);
	if (hr != hrSuccess) {
		cerr << "Unable to resolve store for '" << lpszAccount << "'." << endl;
		return hr;
	}

	hr = lpSession->OpenMsgStore(0, cbEntryID, ptrEntryID, nullptr, MDB_WRITE, &~ptrUserStore);
	if (hr != hrSuccess) {
		cerr << "Unable to open store for '" << lpszAccount << "'." << endl;
		return hr;
	}
	hr = ptrUserStore->QueryInterface(iid_of(ptrServiceAdmin), &~ptrServiceAdmin);
	if (hr != hrSuccess)
		return hr;
	hr = ptrUserStore->OpenEntry(0, nullptr, &iid_of(ptrRoot), 0, &ulType, &~ptrRoot);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrRoot, PR_ENTRYID, &~ptrPropEntryID);
	if (hr != hrSuccess)
		return hr;

	hr = ptrServiceAdmin->ResetFolderCount(ptrPropEntryID->Value.bin.cb, (LPENTRYID)ptrPropEntryID->Value.bin.lpb, &ulUpdates);
	if (hr != hrSuccess) {
		cerr << "Failed to update counters in the root folder." << endl;
		bFailures = true;
	} else if (ulUpdates) {
		cerr << "Updated " << ulUpdates << " counters in the root folder." << endl;
		ulTotalUpdates += ulUpdates;
	}

	hr = ptrRoot->GetHierarchyTable(CONVENIENT_DEPTH, &~ptrTable);
	if (hr != hrSuccess)
		goto exit;
	hr = HrQueryAllRows(ptrTable, sptaTableProps, nullptr, nullptr, 0, &~ptrRows);
	if (hr != hrSuccess)
		goto exit;

	for (SRowSetPtr::size_type i = 0; i < ptrRows.size(); ++i) {
		const SRow &row = ptrRows[i];
		const char* lpszName = "<Unknown>";

		if (PROP_TYPE(row.lpProps[IDX_DISPLAY_NAME].ulPropTag) != PT_ERROR)
			lpszName = row.lpProps[IDX_DISPLAY_NAME].Value.lpszA;
		hr = ptrServiceAdmin->ResetFolderCount(row.lpProps[IDX_ENTRYID].Value.bin.cb,
				(LPENTRYID)row.lpProps[IDX_ENTRYID].Value.bin.lpb,
				&ulUpdates);
		if (hr != hrSuccess) {
			cerr << "Failed to update counters in folder '" << lpszName << "'." << endl;
			bFailures = true;
			hr = hrSuccess;
		} else if (ulUpdates) {
			cerr << "Updated " << ulUpdates << " counters in folder '" << lpszName << "'." << endl;
			ulTotalUpdates += ulUpdates;
		}
	}

	if (ulTotalUpdates == 0)
		cerr << "No counters needed to be updated." << endl;
exit:
	if (hr == hrSuccess && bFailures)
		hr = MAPI_W_ERRORS_RETURNED;
	return hr;
}

class InputValidator {
	public:
	bool failed = false;

		/**
		 * Checks for 'invalid' input from the command prompt. Any
		 * non-printable or control ascii character is not allowed.
		 *
		 * @param[in] szInput command line input string
		 *
		 * @return validated input or NULL
		 */
		char* operator()(char *szInput) {
			wstring strInput;
			failed = szInput == nullptr || TryConvert(szInput, strInput) != hrSuccess ||
			       !std::all_of(strInput.cbegin(), strInput.cend(), iswprint);
			return failed ? nullptr : szInput;
		}
};

static HRESULT fillMVPropmap(ECUSER &sECUser, ULONG ulPropTag, int index,
    std::set<std::string, strcasecmp_comparison> &sFeatures, void *lpBase)
{
	sECUser.sMVPropmap.lpEntries[index].ulPropId = ulPropTag;
	sECUser.sMVPropmap.lpEntries[index].cValues = sFeatures.size();
	sECUser.sMVPropmap.lpEntries[index].lpszValues = NULL;
	if (sFeatures.size() == 0)
		return hrSuccess;
	auto hr = MAPIAllocateMore(sizeof(LPTSTR) * sFeatures.size(), lpBase,
	          reinterpret_cast<void **>(&sECUser.sMVPropmap.lpEntries[index].lpszValues));
	if (hr != hrSuccess) {
		cerr << "Memory error" << endl;
		return hr;
	}
	auto i = sFeatures.cbegin();
	// @note we store char* data in a LPTSTR (whcar_t by -DUNICODE) pointer.
	for (unsigned int n = 0; i != sFeatures.cend(); ++i, ++n)
		sECUser.sMVPropmap.lpEntries[index].lpszValues[n] = (TCHAR*)i->c_str();
	return hrSuccess;
}

static void missing_quota(int hard, int warn, int soft)
{
	if (hard == -1)
		cerr << " hard quota (--qh)";
	if (warn == -1)
		cerr << " warn quota (--qw)";
	if (soft == -1)
		cerr << " soft quota (--qs)";
}

static int fexec(const std::string &admin, std::vector<std::string> &&cmd)
{
	/*
	 * Run @cmd[0] with the directory contained in @admin (if any),
	 * so that the redirect also works in just-built trees.
	 */
	auto pos = admin.rfind('/');
	if (pos != std::string::npos)
		cmd[0] = admin.substr(0, pos + 1) + cmd[0];
	cerr << "The selected option is deprecated in this utility.\n";
	cerr << "\e[1;33m""Forwarding call to: `" << kc_join(cmd, " ") << "`.\e[0m\n";
	auto argv = make_unique_nt<const char *[]>(cmd.size() + 1);
	int argc = 0;
	if (argv == nullptr) {
		perror("new");
		return EXIT_FAILURE;
	}
	for (const auto &e : cmd)
		argv[argc++] = e.c_str();
	argv[argc] = nullptr;
	execvp(argv[0], const_cast<char * const *>(argv.get()));
	return EXIT_FAILURE;
}

static int fexech(const std::string &prog, std::vector<std::string> &&cmd, const char *path)
{
	if (path != nullptr && *path != '\0' && cmd.size() > 0)
		cmd.insert(std::next(cmd.begin()), {"-h", path});
	return fexec(prog, std::move(cmd));
}

static int clearcache(const char *arg0, const char *path, const char *s_mode)
{
	unsigned int mode = PURGE_CACHE_ALL;
	std::vector<std::string> v;
	if (s_mode != nullptr)
		mode = strtoul(s_mode, nullptr, 0);
	if (mode == PURGE_CACHE_ALL) {
		v.push_back("all");
	} else {
#define E(m, s) if (mode & m) v.push_back(s)
		E(PURGE_CACHE_QUOTA, "quota");
		E(PURGE_CACHE_QUOTADEFAULT, "quotadefault");
		E(PURGE_CACHE_OBJECTS, "object");
		E(PURGE_CACHE_STORES, "store");
		E(PURGE_CACHE_ACL, "acl");
		E(PURGE_CACHE_CELL, "cell");
		E(PURGE_CACHE_INDEX1, "index1");
		E(PURGE_CACHE_INDEX2, "index2");
		E(PURGE_CACHE_INDEXEDPROPERTIES, "indexedproperty");
		E(PURGE_CACHE_USEROBJECT, "userobject");
		E(PURGE_CACHE_EXTERNID, "externid");
		E(PURGE_CACHE_USERDETAILS, "userdetail");
		E(PURGE_CACHE_SERVER, "server");
#undef E
	}
	return fexech(arg0, {"kopano-srvadm", "--clear-cache", kc_join(v, ",").c_str()}, path);
}

int main(int argc, char **argv) try
{
	AutoMAPI mapiinit;
	object_ptr<IMAPISession> lpSession;
	object_ptr<IMsgStore> lpMsgStore, lpUserStore;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	unsigned int cbUserId = 0, cbEntryID = 0;
	memory_ptr<ENTRYID> lpUserId, lpSenderId, lpGroupId, lpCompanyId;
	memory_ptr<ENTRYID> lpSetCompanyId, lpEntryID, lpUnWrappedEntry;
	unsigned int cbGroupId = 0, cbSenderId = 0, cbCompanyId = 0, cbSetCompanyId = 0;
	ECUSER sECUser;
	memory_ptr<ECUSER> lpECUser, lpSenders;
	ECGROUP		sECGroup;
	unsigned int cCompanies = 0, cUsers = 0, cSenders = 0;
	ECCOMPANY sECCompany, *lpECCompanies = nullptr;
	objectclass_t ulClass = OBJECTCLASS_UNKNOWN;
	const char *detailstype = nullptr, *path = nullptr;
	char *username = nullptr, *groupname = nullptr, *new_username = nullptr;
	char *companyname = nullptr, *set_companyname = nullptr;
	char *password = nullptr, *emailadr = nullptr, *fullname = nullptr;
	char *storeguid = nullptr, *lang = nullptr, *feature = nullptr;
	char *node = nullptr, *sendas_user = nullptr;
	bool bFeature = true, bCopyToPublic = false, bExplicitConfig = false;
	std::set<std::string, strcasecmp_comparison> sEnabled, sDisabled;
	int quota = -1, ud_quota = -1, isadmin = -1, isnonactive = -1;
	long long quotahard = -1, quotasoft = -1, quotawarn = -1;
	long long ud_quotahard = -1, ud_quotasoft = -1, ud_quotawarn = -1;
	int mr_accept = -1, mr_decline_conflict = -1, mr_decline_recurring = -1;
	int sendas_action = -1, passprompt = 0;
	modes mode = MODE_INVALID;
	std::list<std::string> lstUsernames;
	bool bAutoAccept = false, bDeclineConflict = false, bDeclineRecurring = false;
	object_ptr<IExchangeManageStore> lpIEMS;
	unsigned int loglevel = EC_LOGLEVEL_NONE;
	std::shared_ptr<ECLogger> lpLogger;
	const configsetting_t lpDefaults[] = {
		{"default_store_locale", ""}, /* ignored here; creation is in storeadm */
		{ "server_socket", "default:" },
		{ "sslkey_file", "" },
		{ "sslkey_pass", "", CONFIGSETTING_EXACT },
		{ NULL, NULL },
	};
	std::unique_ptr<ECConfig> lpsConfig(ECConfig::Create(lpDefaults));
	ConsoleTable ct(0,0);
	const char *szConfig = ECConfig::GetDefaultPath("admin.cfg");

	// Set locale to system variables
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	setlocale(LC_TIME, "");

	if(argc < 2) {
		print_help(argv[0]);
		return 1;
	}

	while (1) {
		InputValidator validateInput;
		auto c = getopt_long(argc, argv, "VlLsc:u:d:U:Pp:f:e:a:h:g:G:b:B:i:I:n:v", long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case OPT_VERBOSITY:
			loglevel = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			if (loglevel < EC_LOGLEVEL_DEBUG + 1)
				++loglevel;
			break;
		case 'l':
		case OPT_LIST_USERS:
			mode = MODE_LIST_USERS;
			break;
		case 's':
			mode = MODE_CREATE_PUBLIC;
			break;
		case 'c':
			mode = MODE_CREATE_USER;
			username = validateInput(optarg);
			break;
		case 'u':
			if (mode != MODE_HOOK_STORE)
				mode = MODE_UPDATE_USER;
			username = validateInput(optarg);
			break;
		case 'd':
			mode = MODE_DELETE_USER;
			username = validateInput(optarg);
			break;
		case 'g':
			mode = MODE_CREATE_GROUP;
			groupname = validateInput(optarg);
			break;
		case 'G':
			mode = MODE_DELETE_GROUP;
			groupname = validateInput(optarg);
			break;
		case 'L':
		case OPT_LIST_GROUPS:
			mode = MODE_LIST_GROUP;
			break;
		case 'b':
			mode = MODE_ADDUSER_GROUP;
			username = validateInput(optarg);
			break;
		case 'B':
			mode = MODE_DELETEUSER_GROUP;
			username = validateInput(optarg);
			break;
		case 'U':
			new_username = validateInput(optarg);
			break;
		case 'P':
		case OPT_PASSWORD:
			passprompt = 1;
			break;
		case 'p':
		case OPT_PASSWORD_PROMPT:
			password = validateInput(optarg);
			break;
		case 'f':
		case OPT_FULLNAME:
			fullname = validateInput(optarg);
			break;
		case 'e':
		case OPT_EMAIL:
			emailadr = validateInput(optarg);
			break;
		case 'a':
			isadmin = atoi(optarg);
			if (isadmin == 0)
				isadmin = parse_yesno(optarg);
			else
				isadmin = std::min(2, isadmin);
			break;
		case 'n':
			isnonactive = parse_yesno(optarg);
			break;
		case 'i':
			groupname = validateInput(optarg);
			break;
		case 'I':
			companyname = validateInput(optarg);
			break;
			// error handling?
		case '?':
			break;
		case OPT_HOST:
		case 'h':
			path = validateInput(optarg);
			break;
		case OPT_HELP:
			mode = MODE_HELP;
			break;
		case OPT_CREATE_STORE:
			mode = MODE_CREATE_STORE;
			username = validateInput(optarg);
			break;
		case OPT_DELETE_STORE:
			mode = MODE_DELETE_STORE;
			username = validateInput(optarg);
			break;
		case OPT_HOOK_STORE:
			mode = MODE_HOOK_STORE;
			storeguid = validateInput(optarg);
			break;
		case OPT_UNHOOK_STORE:
			mode = MODE_UNHOOK_STORE;
			username = validateInput(optarg);
			break;
		case OPT_REMOVE_STORE:
			mode = MODE_REMOVE_STORE;
			storeguid = validateInput(optarg);
			break;
		case OPT_COPYTO_PUBLIC:
			bCopyToPublic = true;
			break;
		case OPT_SYNC_USERS:
			mode = MODE_SYNC_USERS;
			break;
		case OPT_DETAILS:
			mode = MODE_DETAILS;
			username = validateInput(optarg);
			break;
		case OPT_DETAILS_TYPE:
			detailstype = validateInput(optarg);
			break;
			// Make values from Mb to bytes which the server wants
		case OPT_USER_QUOTA_HARD:
			quotahard = atoll(optarg) *1024*1024;
			break;
		case OPT_USER_QUOTA_SOFT:
			quotasoft = atoll(optarg) *1024*1024;
			break;
		case OPT_USER_QUOTA_WARN:
			quotawarn = atoll(optarg) *1024*1024;
			break;
		case OPT_USER_QUOTA_OVERRIDE:
			quota = parse_yesno(optarg);
			break;
		case OPT_USER_DEFAULT_QUOTA_HARD:
			ud_quotahard = atoll(optarg) * 1024 * 1024;
			break;
		case OPT_USER_DEFAULT_QUOTA_SOFT:
			ud_quotasoft = atoll(optarg) * 1024 * 1024;
			break;
		case OPT_USER_DEFAULT_QUOTA_WARN:
			ud_quotawarn = atoll(optarg) * 1024 * 1024;
			break;
		case OPT_USER_DEFAULT_QUOTA_OVERRIDE:
			ud_quota = parse_yesno(optarg);
			break;
		case OPT_LANG:
			// Use alternate language
			lang = validateInput(optarg);
			break;
		case OPT_MR_ACCEPT:
			mr_accept = parse_yesno(optarg);
			break;
		case OPT_MR_DECLINE_CONFLICT:
			mr_decline_conflict = parse_yesno(optarg);
			break;
		case OPT_MR_DECLINE_RECURRING:
			mr_decline_recurring = parse_yesno(optarg);
			break;
		case OPT_LIST_SENDAS:
			mode = MODE_LIST_SENDAS;
			username = validateInput(optarg);
			break;
		case OPT_ADD_SENDAS:
			sendas_user = validateInput(optarg);
			sendas_action = 1;
			break;
		case OPT_DEL_SENDAS:
			sendas_user = validateInput(optarg);
			sendas_action = 0;
			break;
		case OPT_UPDATE_GROUP:
			mode = MODE_UPDATE_GROUP;
			groupname = validateInput(optarg);
			break;
		case OPT_CREATE_COMPANY:
			mode = MODE_CREATE_COMPANY;
			companyname = validateInput(optarg);
			break;
		case OPT_UPDATE_COMPANY:
			mode = MODE_UPDATE_COMPANY;
			companyname = validateInput(optarg);
			break;
		case OPT_DELETE_COMPANY:
			mode = MODE_DELETE_COMPANY;
			companyname = validateInput(optarg);
			break;
		case OPT_LIST_COMPANY:
			mode = MODE_LIST_COMPANY;
			break;
		case OPT_ADD_VIEW:
			mode = MODE_ADD_VIEW;
			set_companyname = validateInput(optarg);
			break;
		case OPT_DEL_VIEW:
			mode = MODE_DEL_VIEW;
			set_companyname = validateInput(optarg);
			break;
		case OPT_LIST_VIEW:
			mode = MODE_LIST_VIEW;
			break;
		case OPT_ADD_ADMIN:
			mode = MODE_ADD_ADMIN;
			username = validateInput(optarg);
			break;
		case OPT_DEL_ADMIN:
			mode = MODE_DEL_ADMIN;
			username = validateInput(optarg);
			break;
		case OPT_LIST_ADMIN:
			mode = MODE_LIST_ADMIN;
			break;
		case OPT_SYSTEM_ADMIN:
			mode = MODE_SYSTEM_ADMIN;
			username = validateInput(optarg);
			break;
		case OPT_ADD_UQUOTA_RECIPIENT:
			mode = MODE_ADD_USERQUOTA_RECIPIENT;
			username = validateInput(optarg);
			break;
		case OPT_DEL_UQUOTA_RECIPIENT:
			mode = MODE_DEL_USERQUOTA_RECIPIENT;
			username = validateInput(optarg);
			break;
		case OPT_LIST_UQUOTA_RECIPIENT:
			mode = MODE_LIST_USERQUOTA_RECIPIENT;
			break;
		case OPT_ADD_CQUOTA_RECIPIENT:
			mode = MODE_ADD_COMPANYQUOTA_RECIPIENT;
			username = validateInput(optarg);
			break;
		case OPT_DEL_CQUOTA_RECIPIENT:
			mode = MODE_DEL_COMPANYQUOTA_RECIPIENT;
			username = validateInput(optarg);
			break;
		case OPT_LIST_CQUOTA_RECIPIENT:
			mode = MODE_LIST_COMPANYQUOTA_RECIPIENT;
			break;
		case OPT_PURGE_SOFTDELETE:
			return fexech(argv[0], {"kopano-srvadm", "--purge-softdelete", optarg}, path);

		case OPT_CLEAR_CACHE:
			return clearcache(argv[0], path, optarg);
		case OPT_PURGE_DEFERRED:
			return fexech(argv[0], {"kopano-srvadm", "--purge-deferred"}, path);
		case OPT_LIST_ORPHANS:
			return fexech(argv[0], {"kopano-storeadm", "-O"}, path);
		case OPT_CONFIG:
			szConfig = validateInput(optarg);
			bExplicitConfig = true;
			break;
		case OPT_UTF8: {
			// set early, so other arguments are parsed in this charset.
			std::string locale;
			if (!forceUTF8Locale(false, &locale)) {
				cerr << "Your system does not have the '" << locale << "' locale installed." << endl;
				cerr << "Please install this locale before creating new users." << endl;
				return 1;
			}
			break;
		}
		case OPT_FORCE_RESYNC:
			mode = MODE_FORCE_RESYNC;
			break;
		case OPT_USER_COUNT:
			mode = MODE_USER_COUNT;
			break;
		case OPT_ENABLE_FEATURE:
		case OPT_DISABLE_FEATURE:
			if (feature) {
			       cerr << "Only one feature can be enabled/disabled at a time" << endl;
			       break;
			}
			if (!isFeature(optarg)) {
			       cerr << optarg << " is not a valid kopano feature" << endl;
			       break;
			}
			feature = optarg;
			bFeature = (c == OPT_ENABLE_FEATURE);
			break;
		case OPT_VERSION:
		case 'V':
			cout << "kopano-admin " PROJECT_VERSION << endl;
			return EXIT_SUCCESS;
		case OPT_SELECT_NODE:
			node = validateInput(optarg);
			break;
		case OPT_RESET_FOLDER_COUNT:
			mode = MODE_RESET_FOLDER_COUNT;
			username = validateInput(optarg);
			break;
		default:
			break;
		};
		if (validateInput.failed) {
			cerr << "Invalid input '" << optarg << "' found." << endl;
			// no need to return, later input checking will print an error too
		}
	}

	// check empty input
	if (username && username[0] == 0x00) {
		cerr << "Username (-u) cannot be empty" << endl;
		return 1;
	}
	if (username && strcasecmp(username, "SYSTEM")==0) {
		cerr << "Username (-u) cannot be SYSTEM" << endl;
		return 1;
	}
	if (password && password[0] == 0x00) {
		cerr << "Password (-p) cannot be empty" << endl;
		return 1;
	}
	if (companyname && companyname[0] == 0x00){
		cerr << "Companyname (-I) cannot be empty" << endl;
		return 1;
	}
	if (groupname && groupname[0] == 0x00) {
		cerr << "Groupname cannot be empty" << endl;
		return 1;
	}
	if (fullname && fullname[0] == 0x00) {
		cerr << "Fullname (-f) cannot be empty" << endl;
		return 1;
	}
	if (emailadr && emailadr[0] == 0x00) {
		cerr << "Email address (-e) cannot be empty" << endl;
		return 1;
	}

	// --force-resync takes all left over arguments as usernames
	if (mode == MODE_FORCE_RESYNC) {
		assert(optind <= argc);
		std::copy(argv + optind, argv + argc, std::back_inserter(lstUsernames));
		optind = argc;
	}

	// check parameters
	if (optind < argc) {
		cerr << "Too many options given." << endl;
		return 1;
	}
	if (mode == MODE_INVALID) {
		cerr << "No correct command (e.g. -c for create user) given." << endl;
		return 1;
	}
	if (mode == MODE_HELP) {
		print_help(argv[0]);
		cout << endl << "Please read kopano-admin(8) for detailed information. Enter `man kopano-admin` to view it." << endl << endl;
		return 0;
	}
	// For the following modes we need a company name.
	if (!companyname &&
			(mode == MODE_ADD_VIEW || mode == MODE_DEL_VIEW || mode == MODE_LIST_VIEW ||
			 mode == MODE_ADD_ADMIN || mode == MODE_DEL_ADMIN || mode == MODE_LIST_ADMIN ||
			 mode == MODE_SYSTEM_ADMIN)){
		cerr << "Missing companyname to perform action" << endl;
		return 1;
	}
	if (mode == MODE_DETAILS && username == NULL) {
		cerr << "Missing information to show user details." << endl;
		return 1;
	}
	if (mode == MODE_CREATE_USER) {
		bool has_username = username != NULL;
		bool has_password = !(password == NULL && passprompt == 0 && isnonactive < 1);
		bool has_emailaddr = emailadr != NULL;
		bool has_fullname = fullname != NULL;

		if (!has_username || !has_password || !has_emailaddr || !has_fullname) {
			cerr << "Missing information to create user:";

			if (!has_username)
				cerr << " username (-u)";
			if (!has_password)
				cerr << " password (-p)";
			if (!has_emailaddr)
				cerr << " email address (-e)";
			if (!has_fullname)
				cerr << " full name (-f)";

			cerr << endl;
			return EXIT_FAILURE;
		}
	}
	if (mode == MODE_CREATE_USER && quota == 1 && (quotahard == -1 || quotawarn == -1 || quotasoft == -1)) {
		cerr << "Not all user specific quota levels are given." << endl;
		cerr << "Missing information to create user:";
		missing_quota(quotahard, quotawarn, quotasoft);
		cerr << endl;
		return 1;
	}
	if (mode == MODE_CREATE_COMPANY &&
			((quota == 1 && quotawarn == -1) ||
			 (ud_quota == 1 && (ud_quotahard == -1 || ud_quotasoft == -1 || ud_quotawarn == -1)))) {
		cerr << "Not all company specific quota levels are given." << endl;
		cerr << "Missing information to create company:";

		if (quota == 1 && quotawarn == -1)
			cerr << " warn quota (--qw)";
		if (ud_quota == 1)
			missing_quota(ud_quotahard, ud_quotawarn, ud_quotasoft);
		cerr << endl;
		return 1;
	}
	if (mode == MODE_CREATE_STORE && username == NULL) {
		cerr << "Missing username (-u) to be able to create store." << endl;
		return 1;
	}
	if (mode == MODE_DELETE_STORE) {
		cerr << "Delete store action is not available anymore. Use --remove-store to remove a store from the database." << endl;
		return 1;
	}
	if (mode == MODE_HOOK_STORE && (storeguid == nullptr || (username == nullptr && !bCopyToPublic))) {
		cerr << "Missing information to hook store:";
		if (storeguid == NULL)
			cerr << " store GUID (--hook-store)";
		if (username == nullptr && !bCopyToPublic)
			cerr << " username (-u)";
		cerr << endl;
		return 1;
	}
	if (mode == MODE_UNHOOK_STORE && username == NULL) {
		cerr << "Missing username (-u) to unhook store for." << endl;
		return 1;
	}
	if (mode == MODE_REMOVE_STORE && storeguid == NULL) {
		cerr << "Missing guid (--remove-store) to remove store for." << endl;
		return 1;
	}
	if (mode == MODE_UPDATE_USER && password == NULL && passprompt == 0 &&
			emailadr == NULL && fullname == NULL && new_username == NULL && isadmin == -1 &&
			quota == -1 && quotahard == -1 && quotasoft == -1 && quotawarn == -1 &&
			mr_accept == -1 && mr_decline_conflict == -1 && mr_decline_recurring == -1 &&
			sendas_user == NULL && isnonactive == -1 && feature == NULL) {
		cerr << "Missing information to update user (e.g. password, quota, see --help)." << endl;
		return 1;
	}
	if (mode == MODE_DELETE_USER && username == NULL) {
		cerr << "Missing username (-u) to delete." << endl;
		return 1;
	}
	if (mode == MODE_CREATE_GROUP && groupname == NULL) {
		cerr << "Missing name of group (-g) to create." << endl;
		return 1;
	}
	if (mode == MODE_UPDATE_GROUP && (groupname == NULL || (emailadr == NULL && sendas_user == NULL) ) ) {
		cerr << "Missing information to update group:";
		if (!groupname)
			cerr << " group name";
		if (!emailadr && !sendas_user)
			cerr << " either e-mail address (-e) or \"send-as user\" (--add-sendas)";
		cerr << endl;
		return 1;
	}
	if (mode == MODE_DELETE_GROUP && groupname == NULL) {
		cerr << "Missing name of group (-G) to delete." << endl;
		return 1;
	}
	if (mode == MODE_ADDUSER_GROUP && (groupname == NULL || username == NULL)) {
		cerr << "Missing information to add user to group:";
		if (!groupname)
			cerr << " group name (-i)";
		if (!username)
			cerr << " user name";
		cerr << endl;
		return 1;
	}
	if (mode == MODE_DELETEUSER_GROUP && (groupname == NULL || username == NULL)) {
		cerr << "Missing information to remove user from group:";
		if (!groupname)
			cerr << " group name (-i)";
		if (!username)
			cerr << " user name";
		cerr << endl;
		return 1;
	}
	if (mode == MODE_CREATE_COMPANY && companyname == NULL) {
		cerr << "Missing name of company to create." << endl;
		return 1;
	}
	if (mode == MODE_UPDATE_COMPANY &&
			((quota == 1 && quotawarn == -1) ||
			 (ud_quota == 1 && (ud_quotahard == -1 || ud_quotasoft == -1 || ud_quotawarn == -1)))) {
		cerr << "Missing information to update company:";
		if (quota == 1 && quotawarn == -1)
			cerr << " warn quota (--qw)";
		if (ud_quota == 1)
			missing_quota(ud_quotahard, ud_quotawarn, ud_quotasoft);
		cerr << endl;
		return 1;
	}
	if (mode == MODE_DELETE_COMPANY && companyname == NULL) {
		cerr << "Missing name of company to delete." << endl;
		return 1;
	}
	if (mode == MODE_ADD_VIEW && set_companyname == NULL) {
		cerr << "Missing company name to add remote view privilege to." << endl;
		return 1;
	}
	if (mode == MODE_DEL_VIEW && set_companyname == NULL) {
		cerr << "Missing company name to delete remote view privilege to." << endl;
		return 1;
	}
	if (mode == MODE_ADD_ADMIN && username == NULL) {
		cerr << "Missing username to add remote administrator to." << endl;
		return 1;
	}
	if (mode == MODE_DEL_ADMIN && username == NULL) {
		cerr << "Missing username to delete remote administration privilege for." << endl;
		return 1;
	}
	if (mode == MODE_SYSTEM_ADMIN && username == NULL) {
		cerr << "Missing username to set system administrator privilege for." << endl;
		return 1;
	}
	if ((mode == MODE_ADD_USERQUOTA_RECIPIENT || mode == MODE_DEL_USERQUOTA_RECIPIENT ||
				mode == MODE_ADD_COMPANYQUOTA_RECIPIENT || mode == MODE_DEL_COMPANYQUOTA_RECIPIENT) &&
			username == NULL) {
		cerr << "Missing username to edit quota recipients for." << endl;
		return 1;
	}
	if (mode == MODE_RESET_FOLDER_COUNT && username == NULL) {
		cerr << "Missing username to reset folder counts for." << endl;
		return 1;
	}
	if (lang && mode != MODE_CREATE_STORE) {
		cerr << "You can only use the --lang option in combination with --create-store." << endl;
		return 1;
	}

	// check warnings
	if (new_username != NULL && mode != MODE_UPDATE_USER) {
		cerr << "WARNING: new username \"" << new_username << "\" will be ignored (only used for -U)."  << endl;
	}
	if ((quota == 0 && (quotawarn >= 0 || quotasoft >= 0 || quotahard >= 0)) ||
			(ud_quota == 0 && (ud_quotawarn >= 0 || ud_quotasoft >= 0 || ud_quotahard >= 0))) {
		cerr << "Disabling quota override, but quota levels are provided." << endl;
		cerr << "By disabling quota overrides the existing values will be reset," << endl;
		cerr << "and these new values will be ignored." << endl;
	}
	if ((quota == -1 && (quotawarn >= 0 || quotasoft >= 0 || quotahard >= 0)) ||
			(ud_quota == -1 && (ud_quotawarn >= 0 || ud_quotasoft >= 0 || ud_quotahard >= 0))) {
		cerr << "Quota levels are provided, but not quota level override" << endl;
		cerr << "Without an explicit quota override value the quota levels will be ignored." << endl;
	}

	// confirmations
	if (mode == MODE_FORCE_RESYNC && lstUsernames.empty()) {
		string response;

		cout << "You requested a forced resync without arguments, are you sure you want" << endl;
		cout << "force a resync of all offline profiles for all users? [y/N]: ";
		cin >> response;
		if (response.empty() || strcasecmp(response.c_str(), "n") == 0 || strcasecmp(response.c_str(), "no") == 0)
			return 0;
		if (strcasecmp(response.c_str(), "y") != 0 && strcasecmp(response.c_str(), "yes") != 0) {
			cout << "Invalid response." << endl;
			return 1;
		}
	}

	bool bHaveConfig = lpsConfig->LoadSettings(szConfig);
	/* Special case on complaining errors in config file:
	 * - explicit config file given, but was not found
	 * - default config file loaded, but contains errors
	 * This makes that we do not complain for errors when an "invalid" config was given but did load ok.
	 * This is a trick that people can use to give the spooler or dagent a config for its client SSL settings,
	 * which are the only settings used by the admin program.
	 */
	if ((!bHaveConfig && bExplicitConfig) || (bHaveConfig && !bExplicitConfig && lpsConfig->HasErrors())) {
		cerr << "Error while reading configuration file " << szConfig << endl;
		// create fatal logger without a timestamp to stderr
		lpLogger = std::make_shared<ECLogger_File>(EC_LOGLEVEL_FATAL, 0, "-", false);
		ec_log_set(lpLogger);
		LogConfigErrors(lpsConfig.get());
		return 1;
	}
	if(lang) {
		char* locale = setlocale(LC_MESSAGES, lang);
		if (!locale) {
			cerr << "Your system does not have the '" << lang << "' locale installed." << endl;
			cerr << "Please install this locale before creating new users." << endl;
			// do not create store in wrong language, give admin a chance to make store in right locale
			return 1;
		}
	}

	if (loglevel > EC_LOGLEVEL_DEBUG)
		loglevel = EC_LOGLEVEL_ALWAYS;
	if (loglevel > EC_LOGLEVEL_NONE)
		lpLogger = std::make_shared<ECLogger_File>(loglevel, 0, "-", false);
	else
		lpLogger = std::make_shared<ECLogger_Null>();
	ec_log_set(lpLogger);

	//Init mapi
	auto hr = mapiinit.Initialize();
	if (hr != hrSuccess) {
		cerr << "Unable to initialize" << endl;
		goto exit;
	}

	/*
	 * server path sequence is:
	 * 1. -h option from command line
	 * 2. KOPANO_SOCKET environment variable
	 * 3. config setting option
	 */
	if (!path) {
		path = lpsConfig->GetSetting("server_socket");
		// environment variable may override setting variable
		path = GetServerUnixSocket(path);
	}
	hr = HrOpenECAdminSession(&~lpSession, PROJECT_VERSION, "admin",
	     path, EC_PROFILE_FLAGS_NO_NOTIFICATIONS,
	     lpsConfig->GetSetting("sslkey_file", "", NULL),
	     lpsConfig->GetSetting("sslkey_pass", "", NULL));
	if(hr != hrSuccess) {
		cerr << "Unable to open Admin session: " <<
			GetMAPIErrorMessage(hr) << " (" <<
			stringify_hex(hr) << ")" << endl;
		switch (hr) {
		case MAPI_E_NETWORK_ERROR:
			cerr << "The server is not running, or not accessible";
			if (path != NULL && *path != '\0')
				cerr << " through \"" << path << "\"";
			cerr << "." << endl;
			break;
		case MAPI_E_LOGON_FAILED:
		case MAPI_E_NO_ACCESS:
			cerr << "Access was denied on " << path << "." << endl;
			break;
		default:
			break;
		};
		goto exit;
	}

	hr = HrOpenDefaultStore(lpSession, &~lpMsgStore);
	if(hr != hrSuccess) {
		cerr << "Unable to open Admin store: " <<
			GetMAPIErrorMessage(hr) << " (" <<
			stringify_hex(hr) << ")" << endl;
		goto exit;
	}

	if (node != NULL && *node != '\0') {
		MsgStorePtr ptrRemoteStore;

		hr = HrGetRemoteAdminStore(lpSession, lpMsgStore, (LPTSTR)node, 0, &~ptrRemoteStore);
		if (hr != hrSuccess) {
			cerr << "Unable to connect to node '" << node << "':" <<
				GetMAPIErrorMessage(hr) << " (" <<
				stringify_hex(hr) << ")" << endl;
			switch (hr) {
			case MAPI_E_NETWORK_ERROR:
				cerr << "The server is not running, or not accessible." << endl;
				break;
			case MAPI_E_LOGON_FAILED:
			case MAPI_E_NO_ACCESS:
				cerr << "Access was denied." << endl;
				break;
			case MAPI_E_NOT_FOUND:
				cerr << "Node '" << node << "' is unknown." << endl;
				break;
			default:
				break;
			}
			goto exit;
		}
		lpMsgStore.reset(ptrRemoteStore.release(), false);
	}

	hr = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (hr != hrSuccess) {
		cerr << "Admin object not found." << endl;
		goto exit;
	}

	switch (mode) {
	case MODE_UPDATE_COMPANY:
	case MODE_DELETE_COMPANY:
	case MODE_ADD_VIEW:
	case MODE_DEL_VIEW:
	case MODE_LIST_VIEW:
	case MODE_ADD_ADMIN:
	case MODE_DEL_ADMIN:
	case MODE_LIST_ADMIN:
	case MODE_SYSTEM_ADMIN:
	case MODE_ADD_USERQUOTA_RECIPIENT:
	case MODE_DEL_USERQUOTA_RECIPIENT:
	case MODE_LIST_USERQUOTA_RECIPIENT:
	case MODE_ADD_COMPANYQUOTA_RECIPIENT:
	case MODE_DEL_COMPANYQUOTA_RECIPIENT:
	case MODE_LIST_COMPANYQUOTA_RECIPIENT:
		hr = lpServiceAdmin->ResolveCompanyName((LPTSTR)companyname, 0, &cbCompanyId, &~lpCompanyId);
		if (hr != hrSuccess) {
			fprintf(stderr, "Failed to resolve company: %s\n", getMapiCodeString(hr, companyname).c_str());
			goto exit;
		}
		break;
	default:
		break;
	}

	switch (mode) {
	case MODE_DELETE_USER:
	case MODE_UPDATE_USER:
	case MODE_ADDUSER_GROUP:
	case MODE_DELETEUSER_GROUP:
	case MODE_ADD_ADMIN:
	case MODE_DEL_ADMIN:
	case MODE_SYSTEM_ADMIN:
	case MODE_ADD_USERQUOTA_RECIPIENT:
	case MODE_DEL_USERQUOTA_RECIPIENT:
	case MODE_ADD_COMPANYQUOTA_RECIPIENT:
	case MODE_DEL_COMPANYQUOTA_RECIPIENT:
		if (username == nullptr)
			break;
		hr = lpServiceAdmin->ResolveUserName(reinterpret_cast<LPTSTR>(username), 0, &cbUserId, &~lpUserId);
		if (hr != hrSuccess) {
			fprintf(stderr, "Failed to resolve user: %s\n", getMapiCodeString(hr, username).c_str());
			goto exit;
		}
		break;
	default:
		break;
	}

	// fully logged on, action!
	switch(mode) {
	case MODE_LIST_USERS:
		hr = ForEachCompany(lpServiceAdmin, companyname, ListUsers);
		if (hr != hrSuccess)
			goto exit;
		break;
	case MODE_DETAILS:
		if (detailstype == NULL || strcasecmp(detailstype, "user") == 0)
			ulClass = ACTIVE_USER;
		else if (strcasecmp(detailstype, "group") == 0)
			ulClass = DISTLIST_GROUP;
		else if (strcasecmp(detailstype, "company") == 0)
			ulClass = CONTAINER_COMPANY;
		else if (strcasecmp(detailstype, "archive") != 0) {
			hr = MAPI_E_INVALID_TYPE;
			cerr << "Unknown userobject type \"" << detailstype << "\"" << endl;
			goto exit;
		}
		if (detailstype && strcasecmp(detailstype, "archive") == 0)
			hr = print_archive_details(lpSession, lpServiceAdmin, username);
		else
			hr = print_details(lpSession, lpServiceAdmin, ulClass, username);
		if (hr != hrSuccess)
			goto exit;
		break;
	case MODE_CREATE_PUBLIC:
		if (companyname == nullptr)
			return fexech(argv[0], {"kopano-storeadm", "-P"}, path);
		return fexech(argv[0], {"kopano-storeadm", "-Pk", companyname}, path);
	case MODE_CREATE_USER:
		memset(&sECUser, 0, sizeof(sECUser));
		sECUser.sUserId.cb = g_cbDefaultEid;
		sECUser.sUserId.lpb = g_lpDefaultEid;
		sECUser.lpszUsername = (LPTSTR)username;

		if (passprompt && isnonactive != 1) {
			sECUser.lpszPassword = (LPTSTR)get_password();
			if (sECUser.lpszPassword == NULL) {
				cerr << "Passwords don't match" << endl;
				return 1;
			}
			if (sECUser.lpszPassword[0] == 0x00) {
				cerr << "Password cannot be empty" << endl;
				return 1;
			}
		}
		else if (isnonactive == 1)
			sECUser.lpszPassword = NULL;
		else
			sECUser.lpszPassword = (LPTSTR)password;

		sECUser.lpszMailAddress = (LPTSTR)emailadr;
		sECUser.lpszFullName = (LPTSTR)fullname;
		sECUser.ulIsAdmin = (isadmin != -1)?isadmin:0;
		// FIXME: create user, room or equipment!
		sECUser.ulObjClass = ACTIVE_USER;
		if (isnonactive == 1)
			sECUser.ulObjClass = NONACTIVE_USER; // NONACTIVE_ROOM, NONACTIVE_EQUIPMENT, (NONACTIVE_CONTACT?)

		hr = lpServiceAdmin->CreateUser(&sECUser, 0, &cbUserId, &~lpUserId);
		if (hr != hrSuccess) {
			cerr << "Unable to create user: " << getMapiCodeString(hr, username) << endl;
			cerr << "Check server.log for details." << endl;
			goto exit;
		}
		// set quota data
		if (quota != -1 || quotahard != -1 || quotasoft != -1 || quotawarn != -1) {
			hr = setQuota(lpServiceAdmin, cbUserId, lpUserId, quota, false, quotawarn, quotasoft, quotahard);
			if(hr != hrSuccess)
				goto exit;
		}
		cout << "User created." << endl;
		break;
	case MODE_CREATE_STORE:
		if (lang == nullptr)
			return fexech(argv[0], {"kopano-storeadm", "-Cn", username}, path);
		return fexech(argv[0], {"kopano-storeadm", "-Cn", username, "-l", lang}, path);
	case MODE_DELETE_USER:
		hr = lpServiceAdmin->DeleteUser(cbUserId, lpUserId);
		if (hr != hrSuccess) {
			cerr << "Unable to delete user: " << getMapiCodeString(hr, username) << endl;
			goto exit;
		}
		cout << "User deleted." << endl;
		break;
	case MODE_DELETE_STORE:
		// happy compiler
		break;
	case MODE_HOOK_STORE:
		if (bCopyToPublic)
			return fexech(argv[0], {"kopano-storeadm", "-A", storeguid, "-p"}, path);
		return fexech(argv[0], {"kopano-storeadm", "-A", storeguid, "-n", username}, path);
	case MODE_UNHOOK_STORE:
		if (detailstype == NULL)
			return fexech(argv[0], {"kopano-storeadm", "-Dn", username}, path);
		return fexech(argv[0], {"kopano-storeadm", "-Dn", username, "-t", detailstype}, path);
	case MODE_REMOVE_STORE:
		return fexech(argv[0], {"kopano-storeadm", "-R", storeguid}, path);
	case MODE_UPDATE_USER:
		if (new_username) {
			memory_ptr<ENTRYID> userid;
			ULONG usize;
			hr = lpServiceAdmin->ResolveUserName(reinterpret_cast<LPTSTR>(new_username), 0, &usize, &~userid);
			if (hr == hrSuccess) {
				cerr << "User with name '" << new_username << "' is already present." << endl;
				hr = MAPI_E_COLLISION;
				goto exit;
			}
		}
		// get old features. we need these, because not setting them would mean: remove
		hr = lpServiceAdmin->GetUser(cbUserId, lpUserId, 0, &~lpECUser);
		if (hr != hrSuccess) {
			cerr << "Unable to get user details: " << getMapiCodeString(hr, username) << endl;
			goto exit;
		}

		// lpECUser memory will be kept alive to let the SetUser() call work
		for (ULONG i = 0; i < lpECUser->sMVPropmap.cEntries; ++i) {
			if (lpECUser->sMVPropmap.lpEntries[i].ulPropId == PR_EC_ENABLED_FEATURES_A)
				sEnabled.insert((char**)lpECUser->sMVPropmap.lpEntries[i].lpszValues,
						(char**)lpECUser->sMVPropmap.lpEntries[i].lpszValues + lpECUser->sMVPropmap.lpEntries[i].cValues);
			else if (lpECUser->sMVPropmap.lpEntries[i].ulPropId == PR_EC_DISABLED_FEATURES_A)
				sDisabled.insert((char**)lpECUser->sMVPropmap.lpEntries[i].lpszValues,
						(char**)lpECUser->sMVPropmap.lpEntries[i].lpszValues + lpECUser->sMVPropmap.lpEntries[i].cValues);
		}

		if (feature) {
			if (bFeature) {
				sEnabled.emplace(feature);
				sDisabled.erase(feature);
			} else {
				sEnabled.erase(feature);
				sDisabled.emplace(feature);
			}
		}

		if (new_username || password || passprompt || emailadr || fullname || isadmin != -1 || isnonactive != -1 || feature) {
			memset(&sECUser, 0, sizeof(sECUser));

			// if the user did not select an active/inactive state on the command-line,
			// then check what the status was before admin started; that status
			// will then be re-used
			if (isnonactive == -1)
				isnonactive = lpECUser -> ulObjClass == NONACTIVE_USER;

			// copy static info
			sECUser.sUserId.cb = cbUserId;
			sECUser.sUserId.lpb = reinterpret_cast<unsigned char *>(lpUserId.get());
			// possibly set new values
			sECUser.lpszUsername = (LPTSTR)(new_username ? new_username : username);
			if (passprompt) {
				sECUser.lpszPassword = (LPTSTR)get_password();
				if (sECUser.lpszPassword == NULL)
				{
					cerr << "Passwords don't match" << endl;
					return 1;
				}
				if (sECUser.lpszPassword[0] == 0)
				{
					cerr << "Password cannot be empty" << endl;
					return 1;
				}
			} else
				sECUser.lpszPassword = (LPTSTR)password;
			sECUser.lpszMailAddress = (LPTSTR)emailadr;
			sECUser.lpszFullName = (LPTSTR)fullname;
			sECUser.ulIsAdmin = isadmin;
			sECUser.ulObjClass = ACTIVE_USER;
			if (isnonactive == 1)
				sECUser.ulObjClass = NONACTIVE_USER;

			// sEnabled to sECUser.sMVPropmap ergens
			sECUser.sMVPropmap.cEntries = 2; // @note: if we have more mv props than the feature lists, adjust this value!
			// mapi allocate more on lpECUser, so this will be freed automatically at exit.
			hr = MAPIAllocateMore(sizeof(MVPROPMAPENTRY) * sECUser.sMVPropmap.cEntries, lpECUser,
			     reinterpret_cast<void **>(&sECUser.sMVPropmap.lpEntries));
			if (hr != hrSuccess) {
				cerr << "Memory error" << endl;
				goto exit;
			}
			if (fillMVPropmap(sECUser, PR_EC_ENABLED_FEATURES_A, 0, sEnabled, lpECUser) != hrSuccess ||
			    fillMVPropmap(sECUser, PR_EC_DISABLED_FEATURES_A, 1, sDisabled, lpECUser) != hrSuccess)
				goto exit;
			hr = lpServiceAdmin->SetUser(&sECUser, 0);
			if (hr != hrSuccess) {
				cerr << "Unable to update user information: " << getMapiCodeString(hr) << endl;
				goto exit;
			}
		}

		// update quota data
		if (quota != -1 || quotahard != -1 || quotasoft != -1 || quotawarn != -1) {
			hr = setQuota(lpServiceAdmin, cbUserId, lpUserId, quota, false, quotawarn, quotasoft, quotahard, true);
			if (hr != hrSuccess)
				goto exit;
		}

		if (mr_accept != -1 || mr_decline_conflict != -1 || mr_decline_recurring != -1)
		{
			hr = lpServiceAdmin->QueryInterface(IID_IExchangeManageStore, &~lpIEMS);
			if (hr != hrSuccess) {
				cerr << "Unable to get admin interface." << endl;
				goto exit;
			}
			hr = lpIEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(""), reinterpret_cast<const TCHAR *>(username), 0, &cbEntryID, &~lpEntryID);
			if (hr != hrSuccess) {
				cerr << "Unable to get user store entry id. User has possibly has not store." << endl;
				goto exit;
			}
			hr = lpSession->OpenMsgStore(0, cbEntryID, lpEntryID, &IID_IMsgStore, MDB_WRITE, &~lpUserStore);
			if (hr != hrSuccess) {
				cerr << "Unable to open user store." << endl;
				goto exit;
			}
			hr = GetAutoAcceptSettings(lpUserStore, &bAutoAccept, &bDeclineConflict, &bDeclineRecurring);
			if (hr != hrSuccess)
				// ignore and assume 'false' for all values
				hr = hrSuccess;
			if (mr_accept != -1)
				bAutoAccept = mr_accept;
			if (mr_decline_conflict != -1)
				bDeclineConflict = mr_decline_conflict;
			if (mr_decline_recurring != -1)
				bDeclineRecurring = mr_decline_recurring;

			hr = SetAutoAcceptSettings(lpUserStore, bAutoAccept, bDeclineConflict, bDeclineRecurring);
			if (hr != hrSuccess) {
				cerr << "Unable to set auto-accept settings." << endl;
				goto exit;
			}
		}

		if (sendas_user) {
			hr = lpServiceAdmin->ResolveUserName((LPTSTR)sendas_user, 0, &cbSenderId, &~lpSenderId);
			if (hr != hrSuccess) {
				cerr << "Unable to update user, sendas user not available: " << getMapiCodeString(hr, sendas_user) << endl;
				goto exit;
			}
			if (sendas_action == 0)
				// delete sendas user
				hr = lpServiceAdmin->DelSendAsUser(cbUserId, lpUserId, cbSenderId, lpSenderId);
			else if (sendas_action == 1)
				// add sendas user
				hr = lpServiceAdmin->AddSendAsUser(cbUserId, lpUserId, cbSenderId, lpSenderId);

			switch (hr) {
			case MAPI_E_NOT_FOUND:
				// on del, not in list
				if (sendas_action == 0)
					cerr << "Unable to update user, sender " << sendas_user << " not found in sendas list" << endl;
				else
					cerr << "Unable to update user, sender " << sendas_user << " not allowed in sendas list" << endl;
				goto exit;
			case MAPI_E_COLLISION:
				// on add, already added ... too bad an insert collision does not return this error :|
				cerr << "Unable to update user, sender " << sendas_user << " already in sendas list" << endl;
				goto exit;
			default:
				if (hr != hrSuccess) {
					cerr << "Unable to update user, unable to update sendas list: " << getMapiCodeString(hr, username) << endl;
					goto exit;
				}
				break;
			};
		}
		cout << "User information updated." << endl;
		break;
	case MODE_CREATE_COMPANY:
		memset(&sECCompany, 0, sizeof(sECCompany));
		sECCompany.lpszCompanyname = (LPTSTR)companyname;
		hr = lpServiceAdmin->CreateCompany(&sECCompany, 0, &cbCompanyId, &~lpCompanyId);
		if (hr != hrSuccess) {
			cerr << "Unable to create company: " << getMapiCodeString(hr, companyname) << endl;
			goto exit;
		}
		if (quota != -1) {
			// this is the company public quota, and only contains a warning, nothing more.
			hr = setQuota(lpServiceAdmin, cbCompanyId, lpCompanyId, quota, false, quotawarn, 0, 0, false, true);
			if(hr != hrSuccess)
				goto exit;
		}
		if (ud_quota != -1) {
			hr = setQuota(lpServiceAdmin, cbCompanyId, lpCompanyId, ud_quota, true, ud_quotawarn, ud_quotasoft, ud_quotahard);
			if (hr != hrSuccess)
				goto exit;
		}
		cout << "Company created" << endl;
		break;
	case MODE_UPDATE_COMPANY:
		if (quota != -1 || quotahard != -1 || quotasoft != -1 || quotawarn != -1) {
			hr = setQuota(lpServiceAdmin, cbCompanyId, lpCompanyId, quota, false, quotawarn, quotasoft, quotahard, true, true);
			if (hr != hrSuccess)
				goto exit;
		}
		if (ud_quota != -1 || ud_quotahard != -1 || ud_quotasoft != -1 || ud_quotawarn != -1) {
			hr = setQuota(lpServiceAdmin, cbCompanyId, lpCompanyId, ud_quota, true, ud_quotawarn, ud_quotasoft, ud_quotahard);
			if (hr != hrSuccess)
				goto exit;
		}
		break;
	case MODE_DELETE_COMPANY:
		hr = lpServiceAdmin->DeleteCompany(cbCompanyId, lpCompanyId);
		if (hr != hrSuccess) {
			cerr << "Unable to delete company: " << getMapiCodeString(hr, companyname) << endl;
			goto exit;
		}
		cout << "Company deleted" << endl;
		break;
	case MODE_LIST_COMPANY:
		hr = lpServiceAdmin->GetCompanyList(0, &cCompanies, &lpECCompanies);
		if(hr != hrSuccess) {
			cerr << "Unable to list companies: " << getMapiCodeString(hr) << endl;
			goto exit;
		}
		cout << "Company list ("<< cCompanies <<"):" << endl;
		ct.Resize(cCompanies, 2);
		ct.SetHeader(0, "Companyname");
		ct.SetHeader(1, "System administrator");
		for (unsigned int i = 0; i < cCompanies; ++i) {
			ct.AddColumn(0, (LPSTR)lpECCompanies[i].lpszCompanyname);
			hr = lpServiceAdmin->GetUser(lpECCompanies[i].sAdministrator.cb, reinterpret_cast<ENTRYID *>(lpECCompanies[i].sAdministrator.lpb), 0, &~lpECUser);
			if (hr != hrSuccess) {
				cerr << "Unable to get administrator details: " << getMapiCodeString(hr, "administrator") << endl;
				goto exit;
			}
			ct.AddColumn(1, (LPSTR)lpECUser->lpszUsername);
		}
		ct.PrintTable();
		break;
	case MODE_CREATE_GROUP:
		memset(&sECGroup, 0, sizeof(sECGroup));
		sECGroup.lpszGroupname = (LPTSTR)groupname;
		sECGroup.lpszFullname = (LPTSTR)groupname;
		sECGroup.lpszFullEmail = (LPTSTR)emailadr;
		hr = lpServiceAdmin->CreateGroup(&sECGroup, 0, &cbGroupId, &~lpGroupId);
		if (hr != hrSuccess) {
			cerr << "Unable to create group: " << getMapiCodeString(hr, groupname) << endl;
			goto exit;
		}
		cout << "Group created." << endl;
		break;
	case MODE_UPDATE_GROUP:
		hr = lpServiceAdmin->ResolveGroupName(reinterpret_cast<LPTSTR>(groupname), 0, &cbGroupId, &~lpGroupId);
		if (hr != hrSuccess) {
			cerr << "Unable to update group: " << getMapiCodeString(hr, groupname) << endl;
			goto exit;
		}

		if (emailadr) {
			memset(&sECGroup, 0, sizeof(sECGroup));
			// copy static info
			sECGroup.sGroupId.cb = cbGroupId;
			sECGroup.sGroupId.lpb = reinterpret_cast<unsigned char *>(lpGroupId.get());
			// possibly set new values
			sECGroup.lpszFullEmail = (LPTSTR)emailadr;
			sECGroup.lpszGroupname = (LPTSTR)groupname;
			sECGroup.lpszFullname = (LPTSTR)groupname;

			hr = lpServiceAdmin->SetGroup(&sECGroup, 0);
			if (hr != hrSuccess) {
				cerr << "Unable to update group information: " << getMapiCodeString(hr) << endl;
				goto exit;
			}
		}

		if (sendas_user) {
			hr = lpServiceAdmin->ResolveUserName(reinterpret_cast<LPTSTR>(sendas_user), 0, &cbSenderId, &~lpSenderId);
			if (hr != hrSuccess) {
				cerr << "Unable to update group, sendas user not available: " << getMapiCodeString(hr, sendas_user) << endl;
				goto exit;
			}
			if (sendas_action == 0)
				// delete sendas user
				hr = lpServiceAdmin->DelSendAsUser(cbGroupId, lpGroupId, cbSenderId, lpSenderId);
			else if (sendas_action == 1)
				// add sendas user
				hr = lpServiceAdmin->AddSendAsUser(cbGroupId, lpGroupId, cbSenderId, lpSenderId);

			switch (hr) {
			case MAPI_E_NOT_FOUND:
				// on del, not in list
				if (sendas_action == 0)
					cerr << "Unable to update group, sender " << sendas_user << " not found in sendas list" << endl;
				else
					cerr << "Unable to update group, sender " << sendas_user << " not allowed in sendas list" << endl;
				goto exit;
			case MAPI_E_COLLISION:
				// on add, already added ... too bad an insert collision does not return this error :|
				cerr << "Unable to update group, sender " << sendas_user << " already in sendas list" << endl;
				goto exit;
			default:
				if (hr != hrSuccess) {
					cerr << "Unable to update group, unable to update sendas list: " << getMapiCodeString(hr, username) << endl;
					goto exit;
				}
				break;
			};
		}
		cout << "Group information updated." << endl;
		break;
	case MODE_DELETE_GROUP:
		hr = lpServiceAdmin->ResolveGroupName(reinterpret_cast<LPTSTR>(groupname), 0, &cbGroupId, &~lpGroupId);
		if (hr != hrSuccess) {
			cerr << "Unable to delete group: " << getMapiCodeString(hr, groupname) << endl;
			goto exit;
		}
		hr = lpServiceAdmin->DeleteGroup(cbGroupId, lpGroupId);
		if (hr != hrSuccess) {
			cerr << "Unable to delete group." << endl;
			goto exit;
		}
		cout << "Group deleted." << endl;
		break;
	case MODE_LIST_GROUP:
		hr = ForEachCompany(lpServiceAdmin, companyname, ListGroups);
		if (hr != hrSuccess)
			goto exit;
		break;
	case MODE_ADDUSER_GROUP:
		hr = lpServiceAdmin->ResolveGroupName(reinterpret_cast<LPTSTR>(groupname), 0, &cbGroupId, &~lpGroupId);
		if (hr != hrSuccess) {
			cerr << "Unable to add user to group: " << getMapiCodeString(hr, groupname) << endl;
			goto exit;
		}
		hr = lpServiceAdmin->AddGroupUser(cbGroupId, lpGroupId, cbUserId, lpUserId);
		if (hr != hrSuccess) {
			cerr << "Unable to add user to group." << endl;
			goto exit;
		}
		cout << "User added to group." << endl;
		break;
	case MODE_DELETEUSER_GROUP:
		hr = lpServiceAdmin->ResolveGroupName(reinterpret_cast<LPTSTR>(groupname), 0, &cbGroupId, &~lpGroupId);
		if (hr != hrSuccess) {
			cerr << "Unable to remove user from group: " << getMapiCodeString(hr, groupname) << endl;
			goto exit;
		}
		hr = lpServiceAdmin->DeleteGroupUser(cbGroupId, lpGroupId, cbUserId, lpUserId);
		if (hr != hrSuccess) {
			cerr << "Unable to remove user from group." << endl;
			goto exit;
		}
		cout << "User removed from group." << endl;
		break;
	case MODE_SYNC_USERS:
		hr = SyncUsers(lpServiceAdmin);
		if (hr != hrSuccess)
			goto exit;
		cout << "Users and groups synchronized." << endl;
		break;
	case MODE_ADD_VIEW:
		hr = lpServiceAdmin->ResolveCompanyName(reinterpret_cast<LPTSTR>(set_companyname), 0, &cbSetCompanyId, &~lpSetCompanyId);
		if (hr != hrSuccess) {
			cerr << "Failed to resolve company name: " << getMapiCodeString(hr, set_companyname) << endl;
			goto exit;
		}
		hr = lpServiceAdmin->AddCompanyToRemoteViewList(cbSetCompanyId, lpSetCompanyId, cbCompanyId, lpCompanyId);
		if (hr != hrSuccess) {
			cerr << "Failed to add company to remote-view list" << endl;
			goto exit;
		}
		cout << "Company " << set_companyname << " added to the remote-view list of " << companyname << endl;
		break;
	case MODE_DEL_VIEW:
		hr = lpServiceAdmin->ResolveCompanyName(reinterpret_cast<LPTSTR>(set_companyname), 0, &cbSetCompanyId, &~lpSetCompanyId);
		if (hr != hrSuccess) {
			cerr << "Failed to resolve company name: " << getMapiCodeString(hr, set_companyname) << endl;
			goto exit;
		}
		hr = lpServiceAdmin->DelCompanyFromRemoteViewList(cbSetCompanyId, lpSetCompanyId, cbCompanyId, lpCompanyId);
		if (hr != hrSuccess) {
			cerr << "Failed to remove company from remote-view list: " << getMapiCodeString(hr) << endl;
			goto exit;
		}
		cout << "Company " << set_companyname << " removed from the remote-view list of " << companyname << endl;
		break;
	case MODE_LIST_VIEW:
		hr = lpServiceAdmin->GetRemoteViewList(cbCompanyId, lpCompanyId, 0, &cCompanies, &lpECCompanies);
		if (hr != hrSuccess) {
			cerr << "Unable to display remote-view list: " << getMapiCodeString(hr) << endl;
			goto exit;
		}
		cout << "remote-view list ("<< cCompanies <<"):" << endl;
		cout << "\t" << "companyname" << "" << endl;
		cout << "\t-------------------------------------" << endl;
		print_companies(cCompanies, lpECCompanies, true);
		break;
	case MODE_ADD_ADMIN:
		hr = lpServiceAdmin->AddUserToRemoteAdminList(cbUserId, lpUserId, cbCompanyId, lpCompanyId);
		if (hr != hrSuccess) {
			cerr << "Failed to add user to remote-admin list: " << getMapiCodeString(hr) << endl;
			goto exit;
		}
		cout << "User " << username << " added to the remote-admin list of " << companyname << endl;
		break;
	case MODE_DEL_ADMIN:
		hr = lpServiceAdmin->DelUserFromRemoteAdminList(cbUserId, lpUserId, cbCompanyId, lpCompanyId);
		if (hr != hrSuccess) {
			cerr << "Failed to delete user from remote-admin list: " << getMapiCodeString(hr) << endl;
			goto exit;
		}
		cout << "User " << username << " removed from the remote-admin list of " << companyname << endl;
		break;
	case MODE_LIST_ADMIN:
		hr = lpServiceAdmin->GetRemoteAdminList(cbCompanyId, lpCompanyId, 0, &cUsers, &~lpECUser);
		if (hr != hrSuccess) {
			cerr << "Unable to display remote-admin list: " << getMapiCodeString(hr) << endl;
			goto exit;
		}
		cout << "remote-admin list ("<< cUsers <<"):" << endl;
		print_users(cUsers, lpECUser);
		break;
	case MODE_SYSTEM_ADMIN:
		memset(&sECCompany, 0, sizeof(sECCompany));
		sECCompany.sAdministrator.cb = cbUserId;
		sECCompany.sAdministrator.lpb = reinterpret_cast<unsigned char *>(lpUserId.get());
		sECCompany.lpszCompanyname = (LPTSTR)companyname;
		sECCompany.sCompanyId.cb = cbCompanyId;
		sECCompany.sCompanyId.lpb = reinterpret_cast<unsigned char *>(lpCompanyId.get());
		hr = lpServiceAdmin->SetCompany(&sECCompany, 0);
		if (hr != hrSuccess) {
			cerr << "Failed to set company system administrator" << endl;
			goto exit;
		}
		cout << "User " << username << " is set as admin of company " << companyname << endl;
		break;
	case MODE_ADD_USERQUOTA_RECIPIENT:
		hr = lpServiceAdmin->AddQuotaRecipient(cbCompanyId, lpCompanyId, cbUserId, lpUserId, ACTIVE_USER);
		if (hr != hrSuccess) {
			cerr << "Failed to add recipient to quota list." << endl;
			goto exit;
		}
		cout << "User " << username << " added to user quota recipients list for company " << companyname << endl;
		break;
	case MODE_DEL_USERQUOTA_RECIPIENT:
		hr = lpServiceAdmin->DeleteQuotaRecipient(cbCompanyId, lpCompanyId, cbUserId, lpUserId, ACTIVE_USER);
		if (hr != hrSuccess) {
			cerr << "Failed to remove company from quota list." << endl;
			goto exit;
		}
		cout << "User " << username << " removed from user quota recipients list for company " << companyname << endl;
		break;
	case MODE_LIST_USERQUOTA_RECIPIENT:
		/* HACK: request a user from the specified company, and request the recipients for that user. */
		hr = lpServiceAdmin->GetUserList(cbCompanyId, lpCompanyId, 0, &cUsers, &~lpECUser);
		if (hr != hrSuccess || cUsers <= 1) /* First user is always SYSTEM */ {
			cerr << "Failed to get quota recipient list" << endl;
			goto exit;
		}
		cbUserId = lpECUser[1].sUserId.cb;
		hr = KAllocCopy(lpECUser[1].sUserId.lpb, cbUserId, &~lpUserId);
		if (hr != hrSuccess)
			goto exit;
		hr = lpServiceAdmin->GetQuotaRecipients(cbUserId, lpUserId, 0, &cUsers, &~lpECUser);
		if (hr != hrSuccess) {
			cerr << "Failed to get quota recipient list" << endl;
			goto exit;
		}
		cout << "Recipient list ("<< cUsers-1 <<"):" << endl;
		/* Skip the dummy entry we used to obtain the list */
		print_users(cUsers - 1, &lpECUser[1]);
		break;
	case MODE_ADD_COMPANYQUOTA_RECIPIENT:
		hr = lpServiceAdmin->AddQuotaRecipient(cbCompanyId, lpCompanyId, cbUserId, lpUserId, CONTAINER_COMPANY);
		if (hr != hrSuccess) {
			cerr << "Failed to add recipient to quota list." << endl;
			goto exit;
		}
		cout << "User " << username << " added to company quota recipients list for company " << companyname << endl;
		break;
	case MODE_DEL_COMPANYQUOTA_RECIPIENT:
		hr = lpServiceAdmin->DeleteQuotaRecipient(cbCompanyId, lpCompanyId, cbUserId, lpUserId, CONTAINER_COMPANY);
		if (hr != hrSuccess) {
			cerr << "Failed to delete recipient to quota list." << endl;
			goto exit;
		}
		cout << "User " << username << " removed from company quota recipients list for company " << companyname << endl;
		break;
	case MODE_LIST_COMPANYQUOTA_RECIPIENT:
		hr = lpServiceAdmin->GetQuotaRecipients(cbCompanyId, lpCompanyId, 0, &cUsers, &~lpECUser);
		if (hr != hrSuccess) {
			cerr << "Failed to get quota recipient list." << endl;
			goto exit;
		}
		cout << "Recipient list ("<< cUsers-1 <<"):" << endl;
		/* Skipt first entry, that is the company itself which will not get the mail */
		print_users(cUsers - 1, &lpECUser[1]);
		break;
	case MODE_LIST_SENDAS:
		if (detailstype == NULL || strcasecmp(detailstype, "user") == 0) {
			hr = lpServiceAdmin->ResolveUserName(reinterpret_cast<LPTSTR>(username), 0, &cbUserId, &~lpUserId);
			detailstype = "user";
		} else if (strcasecmp(detailstype, "group") == 0) {
			hr = lpServiceAdmin->ResolveGroupName(reinterpret_cast<LPTSTR>(username), 0, &cbUserId, &~lpUserId);
		} else {
			hr = MAPI_E_INVALID_TYPE;
			cerr << "Unknown object type \"" << detailstype << "\"" << endl;
			goto exit;
		}

		if (hr != hrSuccess) {
			cerr << "Failed to resolve "<< detailstype <<" name: " << getMapiCodeString(hr, username) << endl;
			goto exit;
		}
		hr = lpServiceAdmin->GetSendAsList(cbUserId, lpUserId, 0, &cSenders, &~lpSenders);
		if (hr != hrSuccess) {
			cerr << "Failed to retrieve send-as list for " << detailstype << " " << username << endl;
			goto exit;
		}
		cout << "Send-as list ("<< cSenders <<") for " << detailstype << " " << username << ":" << endl;
		print_users(cSenders, lpSenders);
		break;
	case MODE_FORCE_RESYNC:
		if (lstUsernames.empty())
			hr = ForceResyncAll(lpSession, lpMsgStore);
		else
			hr = ForceResync(lpSession, lpMsgStore, lstUsernames);
		if (hr != hrSuccess) {
			cerr << "Failed to force resync." << endl;
			goto exit;
		}
		cerr << "Successfully forced resync." << endl;
		break;
	case MODE_USER_COUNT:
		hr = DisplayUserCount(lpMsgStore);
		if (hr != hrSuccess) {
			cerr << "Failed to get user statistics." << endl;
			goto exit;
		}
		break;
	case MODE_RESET_FOLDER_COUNT:
		hr = ResetFolderCount(lpSession, lpMsgStore, username);
		if (FAILED(hr)) {
			cerr << "Failed to reset folder counters." << endl;
			goto exit;
		} else if (hr != hrSuccess) {
			cerr << "Some folder counters could not be reset." << endl;
		} else {
			cerr << "Successfully reset folder counters." << endl;
		}
	case MODE_INVALID:
	case MODE_HELP:
		// happy compiler
		break;
	};

exit:
	SSL_library_cleanup();
	if (forcedExitCode > 0)
		return forcedExitCode;
	if (hr == hrSuccess)
		return 0;
	cerr << "Using the -v option (possibly multiple times) may "
	     << "give more hints." << endl;
	return 1;
} catch (...) {
	std::terminate();
}
