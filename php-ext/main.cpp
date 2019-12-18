/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include "phpconfig.h"
#include <kopano/platform.h>
#include <kopano/ecversion.h>
#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <syslog.h>
#include <ctime>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <kopano/tie.hpp>
#include <kopano/MAPIErrors.h>
#include "ECRulesTableProxy.h"
#include <ICalToMAPI.h>
#include <MAPIToICal.h>
#include <libicalmapi/mapitovcf.hpp>
#include <libicalmapi/vcftomapi.hpp>

/*
 * Things to notice when reading/editing this source:
 *
 * - RETURN_... returns in the define, the rest of the function is skipped.
 * - RETVAL_... only sets the return value, the rest of the function is processed.
 *
 * - do not use E_ERROR as it cannot be fetched by the php script. Use E_WARNING.
 *
 * - do not create HRESULT variables, but use MAPI_G(hr) so the php script
 *   can retrieve the value with mapi_last_hresult()
 *
 * - all internal functions need to pass TSRMLS_CC in the end, so win32 version compiles
 *
 * - when using "l" in zend_parse_parameters(), use 'long' (64-bit) as variable type, not ULONG (32-bit)
 * - when using "s" in zend_parse_parameters(), the string length is "int" in PHP5 and "size_t" in PHP7. Use our php_stringsize_t typedef.
 *
 */

/*
 * Some information on objects, refcounts, and how PHP manages memory:
 *
 * Each ZVAL is allocated when doing MAKE_STD_ZVAL or INIT_ALLOC_ZVAL. These allocate the
 * space for the ZVAL struct and nothing else. When you put a value in it, INTs and other small
 * values are stored in the ZVAL itself. When you add a string, more memory is allocated
 * (eg through ZVAL_STRING()) to the ZVAL. Some goes for arrays and associated arrays.
 *
 * The data is freed by PHP by traversing the entire ZVAL and freeing each part of the ZVAL
 * it encounters. So, for an array or string ZVAL, it will be doing more frees than just the base
 * value. The freeing is done in zval_dtor().
 *
 * This system means that MAKE_STD_ZVAL(&zval); ZVAL_STRING(zval, "hello", 0); zval_dtor(zval) will
 * segfault. This is because it will try to free the "hello" string, which was not allocated by PHP.
 * So, always use the 'copy' flag when using constant strings (eg ZVAL_STRING(zval, "hello", 1)). This
 * wastes memory, but makes life a lot easier.
 *
 * There is also a referencing system in PHP that is basically the same as the AddRef() and Release()
 * COM scheme. When you MAKE_STD_ZVAL or ALLOC_INIT_ZVAL, the refcount is 1. You should then always
 * make sure that you never destroy the value with zval_dtor (directly calls the destructor and then
 * calls FREE_ZVAL) when somebody else *could* have a reference to it. Using zval_dtor on values that
 * users may have a reference to is a security hole. (see http://www.php-security.org/MOPB/MOPB-24-2007.html)
 * Although I think this should only happen when you're calling into PHP user space with call_user_function()
 *
 * What you *should* do is use zval_ptr_dtor(). This is not just the same as zval_dtor!!! It first decreases
 * the refcount, and *then* destroys the zval, but only if the refcount is 0. So zval_ptr_dtor is the
 * same as Release() in COM.
 *
 * This means that you should basically always use MAKE_STD_ZVAL and then zval_ptr_dtor(), and you
 * should always be safe. You can easily break things by calling ZVAL_DELREF() too many times. Worst thing is,
 * you won't notice that you've broken it until a while later in the PHP script ...
 *
 * We have one thing here which doesn't follow this pattern: some functions use RETVAL_ZVAL to copy the value
 * of one zval into the 'return_value' zval. Seen as we don't want to do a real copy and then a free, we use
 * RETVAL_ZVAL(val, 0, 0), specifying that the value of the zval should not be copied, but just referenced, and
 * we also specify 0 for the 'dtor' flag. This would cause PHP to destruct the old value, but the old value is
 * pointing at the same data als the return_value zval! So we want to release *only* the ZVAL itself, which we do
 * via FREE_ZVAL.
 *
 * I think.
 *
 *   Steve
 *
 */

// we need to include this in c++ space because php.h also includes it in
// 'extern "C"'-space which doesn't work in win32
#include <cmath>

extern "C" {
	// Remove these defines to remove warnings
	#undef PACKAGE_VERSION
	#undef PACKAGE_TARNAME
	#undef PACKAGE_NAME
	#undef PACKAGE_STRING
	#undef PACKAGE_BUGREPORT

	#include "php.h"
   	#include "php_globals.h"
   	#include "php_ini.h"
   	#include "zend_exceptions.h"
	#include "ext/standard/info.h"
	#include "ext/standard/php_string.h"
}

typedef int php_stringsize_t; /* cf. va_arg call in php/Zend/zend_API.c */

// Destructor functions needed for the PHP resources. 
static void _php_free_mapi_rowset(zend_rsrc_list_entry *rsrc TSRMLS_DC);

// Not defined anymore in PHP 5.3.0
#if ZEND_MODULE_API_NO >= 20071006
ZEND_BEGIN_ARG_INFO(first_arg_force_ref, 0)
        ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(second_arg_force_ref, 0)
        ZEND_ARG_PASS_INFO(0)
        ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(fourth_arg_force_ref, 0)
        ZEND_ARG_PASS_INFO(0)
        ZEND_ARG_PASS_INFO(0)
        ZEND_ARG_PASS_INFO(0)
        ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO()
#endif

#define LOG_BEGIN() do { \
	if (mapi_debug & 1) \
		php_error_docref(nullptr TSRMLS_CC, E_NOTICE, "[IN] %s", __FUNCTION__); \
} while (false)

#define LOG2_END(func) do { \
	if (mapi_debug & 2) { \
		HRESULT hrx =  MAPI_G(hr); \
		php_error_docref(nullptr TSRMLS_CC, E_NOTICE, "[OUT] %s: %s (%x)", func, GetMAPIErrorMessage(hrx), hrx); \
	} \
} while (false)
#define LOG_END() LOG2_END(__func__)

/*
 * PHP fails to apply do{}while(0), so we have to do it extra in some places.
 * Do not remove the do{} wrap.
 */
/* Only PHP >= 5.6 (or so) has the type of "resource_type_name" properly */
#define ZEND_FETCH_RESOURCE_C(rsrc, rsrc_type, passed_id, default_id, resource_type_name, resource_type) \
	do { \
		ZEND_FETCH_RESOURCE((rsrc), rsrc_type, (passed_id), (default_id), const_cast<char *>(resource_type_name), (resource_type)); \
	} while (false)

// A very, very nice PHP #define that causes link errors in MAPI when you have multiple
// files referencing MAPI....
#undef inline

#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapispi.h>
#include <mapitags.h>
#include <mapidefs.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/ECTags.h>
#include <kopano/ECDefs.h>

#define USES_IID_IMAPIProp
#define USES_IID_IMAPIContainer
#define USES_IID_IMsgStore
#define USES_IID_IMessage
#define USES_IID_IExchangeManageStore

#include <string>
#include "util.h"
#include "rtfutil.h"
#include <kopano/CommonUtil.h>
#include "ECImportContentsChangesProxy.h"
#include "ECImportHierarchyChangesProxy.h"
#include "ECMemStream.h"
#include <inetmapi/inetmapi.h>
#include <inetmapi/options.h>
#include <edkmdb.h>
#include <mapiguid.h>
#include <kopano/ECGuid.h>
#include <edkguid.h>
#include "ECFreeBusySupport.h"
#include "main.h"
#include "typeconversion.h"
#include "MAPINotifSink.h"
#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>
#include "charset/localeutil.h"

#define PMEASURE_FUNC pmeasure __pmobject(__PRETTY_FUNCTION__);
#define kphperr(m, hr) php_error_docref(nullptr TSRMLS_CC, E_WARNING, m ": %s (%x)", GetMAPIErrorMessage(hr), hr)

using namespace KC;

class pmeasure {
public:
	pmeasure(const std::string &);
	~pmeasure(void);

private:
	std::string what;
	time_point start_ts;
};

static std::shared_ptr<ECLogger> lpLogger;

static unsigned int mapi_debug;
static char *perf_measure_file;
int le_mapi_session, le_mapi_table, le_mapi_rowset, le_mapi_msgstore;
int le_mapi_addrbook, le_mapi_mailuser, le_mapi_distlist, le_mapi_abcont;
int le_mapi_folder, le_mapi_message, le_mapi_attachment;
int le_mapi_property, le_mapi_modifytable, le_istream;
int le_freebusy_support, le_freebusy_data;
int le_freebusy_update, le_freebusy_enumblock;
int le_mapi_exportchanges, le_mapi_importhierarchychanges;
int le_mapi_importcontentschanges, le_mapi_advisesink;

pmeasure::pmeasure(const std::string &whatIn)
{
	if (perf_measure_file == NULL || *perf_measure_file == '\0')
		return;
	what = whatIn;
	start_ts = decltype(start_ts)::clock::now();
}

pmeasure::~pmeasure(void)
{
	if (perf_measure_file == NULL || *perf_measure_file == '\0')
		return;
	auto end_ts = decltype(start_ts)::clock::now();
	FILE *fh = fopen(perf_measure_file, "a+");
	if (fh == NULL) {
		if (lpLogger != NULL)
			lpLogger->logf(EC_LOGLEVEL_ERROR, "~pmeasure: cannot open \"%s\": %s", perf_measure_file, strerror(errno));
		return;
	}
	using namespace std::chrono;
	static size_t rcount = 0;
	auto epd = end_ts.time_since_epoch();
	fprintf(fh, "%6d %9zu %llu.%03lu: %9lldµs %s\n", getpid(), ++rcount,
		static_cast<unsigned long long>(duration_cast<seconds>(epd).count()),
		duration_cast<milliseconds>(epd).count() % std::milli::den,
		static_cast<unsigned long long>(duration_cast<microseconds>(end_ts - start_ts).count()),
		what.c_str());
	fclose(fh);
}

/* function list so that the Zend engine will know what's here */
zend_function_entry mapi_functions[] =
{
	ZEND_FE(mapi_last_hresult, NULL)
	ZEND_FE(mapi_prop_type, NULL)
	ZEND_FE(mapi_prop_id, NULL)
	ZEND_FE(mapi_is_error, NULL)
	ZEND_FE(mapi_make_scode, NULL)
	ZEND_FE(mapi_prop_tag, NULL)
	ZEND_FE(mapi_createoneoff, NULL)
	ZEND_FE(mapi_parseoneoff, NULL)
	ZEND_FE(mapi_logon_zarafa, NULL)
	ZEND_FE(mapi_getmsgstorestable, NULL)
	ZEND_FE(mapi_openmsgstore, NULL)
	ZEND_FE(mapi_openprofilesection, NULL)

	ZEND_FE(mapi_openaddressbook, NULL)
	ZEND_FE(mapi_openentry, NULL)
	ZEND_FE(mapi_ab_openentry, NULL)
	// TODO: add other functions for le_mapi_mailuser and le_mapi_distlist
	ZEND_FE(mapi_ab_resolvename, NULL)
	ZEND_FE(mapi_ab_getdefaultdir, NULL)

	ZEND_FE(mapi_msgstore_createentryid, NULL)
	ZEND_FE(mapi_msgstore_getarchiveentryid, NULL)
	ZEND_FE(mapi_msgstore_openentry, NULL)
	ZEND_FE(mapi_msgstore_getreceivefolder, NULL)
	ZEND_FE(mapi_msgstore_entryidfromsourcekey, NULL)
	ZEND_FE(mapi_msgstore_openmultistoretable, NULL)
	ZEND_FE(mapi_msgstore_advise, NULL)
	ZEND_FE(mapi_msgstore_unadvise, NULL)
	ZEND_FE(mapi_msgstore_abortsubmit, nullptr)
	
	ZEND_FE(mapi_sink_create, NULL)
	ZEND_FE(mapi_sink_timedwait, NULL)

	ZEND_FE(mapi_table_queryallrows, NULL)
	ZEND_FE(mapi_table_queryrows, NULL)
	ZEND_FE(mapi_table_getrowcount, NULL)
	ZEND_FE(mapi_table_setcolumns, NULL)
	ZEND_FE(mapi_table_seekrow, NULL)
	ZEND_FE(mapi_table_sort, NULL)
	ZEND_FE(mapi_table_restrict, NULL)
	ZEND_FE(mapi_table_findrow, NULL)
	ZEND_FE(mapi_table_createbookmark, NULL)
	ZEND_FE(mapi_table_freebookmark, NULL)

	ZEND_FE(mapi_folder_gethierarchytable, NULL)
	ZEND_FE(mapi_folder_getcontentstable, NULL)
	ZEND_FE(mapi_folder_createmessage, NULL)
	ZEND_FE(mapi_folder_createfolder, NULL)
	ZEND_FE(mapi_folder_deletemessages, NULL)
	ZEND_FE(mapi_folder_copymessages, NULL)
	ZEND_FE(mapi_folder_emptyfolder, NULL)
	ZEND_FE(mapi_folder_copyfolder, NULL)
	ZEND_FE(mapi_folder_deletefolder, NULL)
	ZEND_FE(mapi_folder_setreadflags, NULL)
	ZEND_FE(mapi_folder_openmodifytable, NULL)
	ZEND_FE(mapi_folder_setsearchcriteria, NULL)
	ZEND_FE(mapi_folder_getsearchcriteria, NULL)

	ZEND_FE(mapi_message_getattachmenttable, NULL)
	ZEND_FE(mapi_message_getrecipienttable, NULL)
	ZEND_FE(mapi_message_openattach, NULL)
	ZEND_FE(mapi_message_createattach, NULL)
	ZEND_FE(mapi_message_deleteattach, NULL)
	ZEND_FE(mapi_message_modifyrecipients, NULL)
	ZEND_FE(mapi_message_submitmessage, NULL)
	ZEND_FE(mapi_message_setreadflag, NULL)

	ZEND_FE(mapi_openpropertytostream, NULL)
	ZEND_FE(mapi_stream_write, NULL)
	ZEND_FE(mapi_stream_read, NULL)
	ZEND_FE(mapi_stream_stat, NULL)
	ZEND_FE(mapi_stream_seek, NULL)
	ZEND_FE(mapi_stream_commit, NULL)
	ZEND_FE(mapi_stream_setsize, NULL)
	ZEND_FE(mapi_stream_create, NULL)

	ZEND_FE(mapi_attach_openobj, NULL)
	ZEND_FE(mapi_savechanges, NULL)
	ZEND_FE(mapi_getprops, NULL)
	ZEND_FE(mapi_setprops, NULL)
	ZEND_FE(mapi_copyto, NULL)
	ZEND_FE(mapi_openproperty, NULL)
	ZEND_FE(mapi_deleteprops, NULL)
	ZEND_FE(mapi_getnamesfromids, NULL)
	ZEND_FE(mapi_getidsfromnames, NULL)

	ZEND_FE(mapi_decompressrtf, NULL)

	ZEND_FE(mapi_rules_gettable, NULL)
	ZEND_FE(mapi_rules_modifytable, NULL)

	ZEND_FE(mapi_zarafa_createuser, NULL)
	ZEND_FE(mapi_zarafa_setuser, NULL)
	ZEND_FE(mapi_zarafa_getuser_by_id, NULL)
	ZEND_FE(mapi_zarafa_getuser_by_name, NULL)
	ZEND_FE(mapi_zarafa_deleteuser, NULL)
	ZEND_FE(mapi_zarafa_createstore, NULL)
	ZEND_FE(mapi_zarafa_getuserlist, NULL)
	ZEND_FE(mapi_zarafa_getquota, NULL)
	ZEND_FE(mapi_zarafa_setquota, NULL)
	ZEND_FE(mapi_zarafa_creategroup, NULL)
	ZEND_FE(mapi_zarafa_deletegroup, NULL)
	ZEND_FE(mapi_zarafa_addgroupmember, NULL)
	ZEND_FE(mapi_zarafa_deletegroupmember, NULL)
	ZEND_FE(mapi_zarafa_setgroup, NULL)
	ZEND_FE(mapi_zarafa_getgroup_by_id, NULL)
	ZEND_FE(mapi_zarafa_getgroup_by_name, NULL)
	ZEND_FE(mapi_zarafa_getgrouplist, NULL)
	ZEND_FE(mapi_zarafa_getgrouplistofuser, NULL)
	ZEND_FE(mapi_zarafa_getuserlistofgroup, NULL)
	ZEND_FE(mapi_zarafa_createcompany, NULL)
	ZEND_FE(mapi_zarafa_deletecompany, NULL)
	ZEND_FE(mapi_zarafa_getcompany_by_id, NULL)
	ZEND_FE(mapi_zarafa_getcompany_by_name, NULL)
	ZEND_FE(mapi_zarafa_getcompanylist, NULL)
	ZEND_FE(mapi_zarafa_add_company_remote_viewlist, NULL)
	ZEND_FE(mapi_zarafa_del_company_remote_viewlist, NULL)
	ZEND_FE(mapi_zarafa_get_remote_viewlist, NULL)
	ZEND_FE(mapi_zarafa_add_user_remote_adminlist, NULL)
	ZEND_FE(mapi_zarafa_del_user_remote_adminlist, NULL)
	ZEND_FE(mapi_zarafa_get_remote_adminlist, NULL)
	ZEND_FE(mapi_zarafa_add_quota_recipient, NULL)
	ZEND_FE(mapi_zarafa_del_quota_recipient, NULL)
	ZEND_FE(mapi_zarafa_get_quota_recipientlist, NULL)
	ZEND_FE(mapi_zarafa_getpermissionrules, NULL)
	ZEND_FE(mapi_zarafa_setpermissionrules, NULL)

	ZEND_FE(mapi_freebusysupport_open, NULL)
	ZEND_FE(mapi_freebusysupport_close, NULL)
	ZEND_FE(mapi_freebusysupport_loaddata, NULL)
	ZEND_FE(mapi_freebusysupport_loadupdate, NULL)

	ZEND_FE(mapi_freebusydata_enumblocks, NULL)
	ZEND_FE(mapi_freebusydata_getpublishrange, NULL)
	ZEND_FE(mapi_freebusydata_setrange, NULL)

	ZEND_FE(mapi_freebusyenumblock_reset, NULL)
	ZEND_FE(mapi_freebusyenumblock_next, NULL)
	ZEND_FE(mapi_freebusyenumblock_skip, NULL)
	ZEND_FE(mapi_freebusyenumblock_restrict, NULL)
	ZEND_FE(mapi_freebusyenumblock_ical, NULL)

	ZEND_FE(mapi_freebusyupdate_publish, NULL)
	ZEND_FE(mapi_freebusyupdate_reset, NULL)
	ZEND_FE(mapi_freebusyupdate_savechanges, NULL)

	ZEND_FE(mapi_exportchanges_config, NULL)
	ZEND_FE(mapi_exportchanges_synchronize, NULL)
	ZEND_FE(mapi_exportchanges_updatestate, NULL)
	ZEND_FE(mapi_exportchanges_getchangecount, NULL)

	ZEND_FE(mapi_importcontentschanges_config, NULL)
	ZEND_FE(mapi_importcontentschanges_updatestate, NULL)
	ZEND_FE(mapi_importcontentschanges_importmessagechange, fourth_arg_force_ref)
	ZEND_FE(mapi_importcontentschanges_importmessagedeletion, NULL)
	ZEND_FE(mapi_importcontentschanges_importperuserreadstatechange, NULL)
	ZEND_FE(mapi_importcontentschanges_importmessagemove, NULL)

	ZEND_FE(mapi_importhierarchychanges_config, NULL)
	ZEND_FE(mapi_importhierarchychanges_updatestate, NULL)
	ZEND_FE(mapi_importhierarchychanges_importfolderchange, NULL)
	ZEND_FE(mapi_importhierarchychanges_importfolderdeletion, NULL)

	ZEND_FE(mapi_wrap_importcontentschanges, first_arg_force_ref)
	ZEND_FE(mapi_wrap_importhierarchychanges, first_arg_force_ref)

	ZEND_FE(mapi_inetmapi_imtoinet, NULL)
	ZEND_FE(mapi_inetmapi_imtomapi, NULL)

	ZEND_FE(mapi_icaltomapi, nullptr)
	ZEND_FE(mapi_mapitoical, nullptr)

	ZEND_FE(mapi_vcftomapi, nullptr)
	ZEND_FE(mapi_mapitovcf, nullptr)

	ZEND_FE(mapi_enable_exceptions, NULL)

    ZEND_FE(mapi_feature, NULL)

	ZEND_FALIAS(mapi_attach_openbin, mapi_openproperty, NULL)
	ZEND_FALIAS(mapi_msgstore_getprops, mapi_getprops, NULL)
	ZEND_FALIAS(mapi_folder_getprops, mapi_getprops, NULL)
	ZEND_FALIAS(mapi_message_getprops, mapi_getprops, NULL)
	ZEND_FALIAS(mapi_message_setprops, mapi_setprops, NULL)
	ZEND_FALIAS(mapi_message_openproperty, mapi_openproperty, NULL)
	ZEND_FALIAS(mapi_attach_getprops, mapi_getprops, NULL)
	ZEND_FALIAS(mapi_attach_openproperty, mapi_openproperty, NULL)
	ZEND_FALIAS(mapi_message_savechanges, mapi_savechanges, NULL)

	ZEND_FE(kc_session_save, second_arg_force_ref)
	ZEND_FE(kc_session_restore, second_arg_force_ref)
	{NULL, NULL, NULL}
};

ZEND_DECLARE_MODULE_GLOBALS(mapi)
static void php_mapi_init_globals(zend_mapi_globals *mapi_globals) {
	// seems to be empty ..
}

/* module information */
zend_module_entry mapi_module_entry =
{
	STANDARD_MODULE_HEADER,
	"mapi",				/* name */
	mapi_functions,		/* functionlist */
	PHP_MINIT(mapi),	/* module startup function */
	PHP_MSHUTDOWN(mapi),/* module shutdown function */
	PHP_RINIT(mapi),	/* Request init function */
	PHP_RSHUTDOWN(mapi),/* Request shutdown function */
	PHP_MINFO(mapi),	/* Info function */
	PROJECT_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#if COMPILE_DL_MAPI
BEGIN_EXTERN_C()
	ZEND_DLEXPORT zend_module_entry *get_module(void);
	ZEND_GET_MODULE(mapi)
END_EXTERN_C()
#endif

/***************************************************************
* PHP Module functions
***************************************************************/
PHP_MINFO_FUNCTION(mapi)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "MAPI Support", "enabled");
	php_info_print_table_row(2, "Version", PROJECT_VERSION);
	php_info_print_table_end();
}

#define CE_PHP_MAPI_PERFORMANCE_TRACE_FILE "php_mapi_performance_trace_file"
#define CE_PHP_MAPI_DEBUG "php_mapi_debug"

static int LoadSettingsFile(void)
{
	const char *const cfg_file = ECConfig::GetDefaultPath("php-mapi.cfg"); 
	struct stat st;
	if (stat(cfg_file, &st) == 0) {
		static const configsetting_t settings[] = {
			{"log_method", "syslog", CONFIGSETTING_NONEMPTY},
			{"log_file", "/var/log/kopano/php-mapi.log", CONFIGSETTING_NONEMPTY},
			{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
			{ "log_timestamp", "0" },
			{ "log_buffer_size", "0" },
			{ "log_buffer_size", "0" },
			{ CE_PHP_MAPI_PERFORMANCE_TRACE_FILE, "" },
			{ CE_PHP_MAPI_DEBUG, "0" },
			{ NULL, NULL }
		};

		auto cfg = ECConfig::Create(std::nothrow, settings);
                if (!cfg)
			return FAILURE;

                if (cfg->LoadSettings(cfg_file))
			lpLogger.reset(CreateLogger(cfg, "php-mapi", "PHPMapi"));

		const char *temp = cfg->GetSetting(CE_PHP_MAPI_PERFORMANCE_TRACE_FILE);
		if (temp != NULL) {
			perf_measure_file = strdup(temp);
			lpLogger->Log(EC_LOGLEVEL_INFO, "Performance measuring enabled");
		}

		temp = cfg->GetSetting(CE_PHP_MAPI_DEBUG);
		if (temp != NULL)
			mapi_debug = strtoul(temp, NULL, 0);

		delete cfg;
	}

	if (!lpLogger)
		lpLogger.reset(new(std::nothrow) ECLogger_Null);
	if (lpLogger == NULL)
		return FAILURE;
	lpLogger->Log(EC_LOGLEVEL_INFO, "php5-mapi " PROJECT_VERSION " instantiated");
	ec_log_set(lpLogger);
	if (mapi_debug)
		lpLogger->logf(EC_LOGLEVEL_INFO, "PHP-MAPI trace level set to %d", mapi_debug);
	return SUCCESS;
}

template<typename T> static void
_php_free_mapi_object(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
	if (rsrc->ptr != nullptr)
		static_cast<T *>(rsrc->ptr)->Release();
}

/**
* Initfunction for the module, will be called once at server startup
*/
PHP_MINIT_FUNCTION(mapi) {
	int ret = LoadSettingsFile();
	if (ret != SUCCESS)
		return ret;

	le_mapi_session = zend_register_list_destructors_ex(_php_free_mapi_object<IMAPISession>, nullptr, const_cast<char *>(name_mapi_session), module_number);
	le_mapi_table = zend_register_list_destructors_ex(_php_free_mapi_object<IMAPITable>, nullptr, const_cast<char *>(name_mapi_table), module_number);
	le_mapi_rowset = zend_register_list_destructors_ex(_php_free_mapi_rowset, NULL, const_cast<char *>(name_mapi_rowset), module_number);
	le_mapi_msgstore = zend_register_list_destructors_ex(_php_free_mapi_object<IMsgStore>, nullptr, const_cast<char *>(name_mapi_msgstore), module_number);
	le_mapi_addrbook = zend_register_list_destructors_ex(_php_free_mapi_object<IAddrBook>, nullptr, const_cast<char *>(name_mapi_addrbook), module_number);
	le_mapi_mailuser = zend_register_list_destructors_ex(_php_free_mapi_object<IMailUser>, nullptr, const_cast<char *>(name_mapi_mailuser), module_number);
	le_mapi_distlist = zend_register_list_destructors_ex(_php_free_mapi_object<IDistList>, nullptr, const_cast<char *>(name_mapi_distlist), module_number);
	le_mapi_abcont = zend_register_list_destructors_ex(_php_free_mapi_object<IABContainer>, nullptr, const_cast<char *>(name_mapi_abcont), module_number);
	le_mapi_folder = zend_register_list_destructors_ex(_php_free_mapi_object<IMAPIFolder>, nullptr, const_cast<char *>(name_mapi_folder), module_number);
	le_mapi_message = zend_register_list_destructors_ex(_php_free_mapi_object<IMessage>, nullptr, const_cast<char *>(name_mapi_message), module_number);
	le_mapi_attachment = zend_register_list_destructors_ex(_php_free_mapi_object<IAttach>, nullptr, const_cast<char *>(name_mapi_attachment), module_number);
	le_mapi_property = zend_register_list_destructors_ex(_php_free_mapi_object<IMAPIProp>, nullptr, const_cast<char *>(name_mapi_property), module_number);
	le_mapi_modifytable = zend_register_list_destructors_ex(_php_free_mapi_object<IExchangeModifyTable>, nullptr, const_cast<char *>(name_mapi_modifytable), module_number);
	le_mapi_advisesink = zend_register_list_destructors_ex(_php_free_mapi_object<IMAPIAdviseSink>, nullptr, const_cast<char *>(name_mapi_advisesink), module_number);
	le_istream = zend_register_list_destructors_ex(_php_free_mapi_object<IStream>, nullptr, const_cast<char *>(name_istream), module_number);

	// Freebusy functions
	le_freebusy_support = zend_register_list_destructors_ex(_php_free_mapi_object<IFreeBusySupport>, nullptr, const_cast<char *>(name_fb_support), module_number);
	le_freebusy_data = zend_register_list_destructors_ex(_php_free_mapi_object<IFreeBusyData>, nullptr, const_cast<char *>(name_fb_data), module_number);
	le_freebusy_update = zend_register_list_destructors_ex(_php_free_mapi_object<IFreeBusyUpdate>, nullptr, const_cast<char *>(name_fb_update), module_number);
	le_freebusy_enumblock = zend_register_list_destructors_ex(_php_free_mapi_object<IEnumFBBlock>, nullptr, const_cast<char *>(name_fb_enumblock), module_number);

	// ICS interfaces
	le_mapi_exportchanges = zend_register_list_destructors_ex(_php_free_mapi_object<IExchangeExportChanges>, nullptr, const_cast<char *>(name_mapi_exportchanges), module_number);
	le_mapi_importhierarchychanges = zend_register_list_destructors_ex(_php_free_mapi_object<IExchangeImportHierarchyChanges>, nullptr, const_cast<char *>(name_mapi_importhierarchychanges), module_number);
	le_mapi_importcontentschanges = zend_register_list_destructors_ex(_php_free_mapi_object<IExchangeImportContentsChanges>, nullptr, const_cast<char *>(name_mapi_importcontentschanges), module_number);
	MAPIINIT_0 MAPIINIT = { 0, MAPI_MULTITHREAD_NOTIFICATIONS };

	// There is also a MAPI_NT_SERVICE flag, see help page for MAPIInitialize
	if (MAPIInitialize(&MAPIINIT) != hrSuccess)
		return FAILURE;

	ZEND_INIT_MODULE_GLOBALS(mapi, php_mapi_init_globals, NULL);

	// force this program to use UTF-8, that way, we don't have to use lpszW and do all kinds of conversions from UTF-8 to WCHAR and back
	forceUTF8Locale(false);
	return SUCCESS;
}

#define DEFERRED_EPILOGUE \
	auto epilogue_handler = make_scope_success([&,func=__func__]() { \
		LOG_END(); \
		if (FAILED(MAPI_G(hr))) { \
			if (lpLogger) \
				lpLogger->logf(EC_LOGLEVEL_ERROR, "MAPI error: %s (%x) (method: %s, line: %d)", GetMAPIErrorMessage(MAPI_G(hr)), MAPI_G(hr), func, __LINE__); \
			\
			if (MAPI_G(exceptions_enabled)) \
				zend_throw_exception(MAPI_G(exception_ce), "MAPI error ", MAPI_G(hr)  TSRMLS_CC); \
		} \
	});
// Used at the end of each MAPI call to throw exceptions if mapi_enable_exceptions() has been called

/**
*
*
*/
PHP_MSHUTDOWN_FUNCTION(mapi)
{
	UNREGISTER_INI_ENTRIES();

	free(perf_measure_file);
	perf_measure_file = NULL;
    
	if (lpLogger)
		lpLogger->Log(EC_LOGLEVEL_INFO, "PHP-MAPI shutdown");

	MAPIUninitialize();
	lpLogger.reset();
	return SUCCESS;
}

/***************************************************************
* PHP Request functions
***************************************************************/
/**
* Request init function, will be called before every request.
*
*/
PHP_RINIT_FUNCTION(mapi) {
	MAPI_G(hr) = hrSuccess;
	MAPI_G(exception_ce) = NULL;
	MAPI_G(exceptions_enabled) = false;
	return SUCCESS;
}

/**
* Request shutdown function, will be called after every request.
*
*/
PHP_RSHUTDOWN_FUNCTION(mapi) {
	return SUCCESS;
}

/***************************************************************
* Resource Destructor functions
***************************************************************/

// This is called when our proxy object goes out of scope
static void _php_free_mapi_rowset(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
	auto pRowSet = static_cast<SRowSet *>(rsrc->ptr);
	if (pRowSet) FreeProws(pRowSet);
}

/***************************************************************
* Custom Code
***************************************************************/
ZEND_FUNCTION(mapi_last_hresult)
{
	RETURN_LONG((LONG)MAPI_G(hr));
}

/*
* PHP casts a variable to a signed long before bitshifting. So a C++ function
* is used.
*/
ZEND_FUNCTION(mapi_prop_type)
{
	long ulPropTag;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &ulPropTag) == FAILURE) return;

	RETURN_LONG(PROP_TYPE(ulPropTag));
}

/*
* PHP casts a variable to a signed long before bitshifting. So a C++ function
* is used.
*/
ZEND_FUNCTION(mapi_prop_id)
{
	long ulPropTag;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &ulPropTag) == FAILURE) return;

	RETURN_LONG(PROP_ID(ulPropTag));
}

/**
 * Checks if the severity of an errorCode is set to Fail
 */
ZEND_FUNCTION(mapi_is_error)
{
	long errorcode;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &errorcode) == FAILURE) return;

	RETURN_BOOL(IS_ERROR(errorcode));
}

/*
* Makes a mapi SCODE
* input:
*  long severity
*  long code
*
*/
ZEND_FUNCTION(mapi_make_scode)
{
	long sev, code;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ll", &sev, &code) == FAILURE) return;

	/*
	 * sev has two possible values: 0 for a warning, 1 for an error
	 * err is the error code for the specific error.
	 */
	RETURN_LONG(MAKE_MAPI_SCODE(sev & 1, FACILITY_ITF, code));
}

/*
* PHP casts a variable to a signed long before bitshifting. So a C++ function
* is used.
*/
ZEND_FUNCTION(mapi_prop_tag)
{
	long ulPropID, ulPropType;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ll", &ulPropType, &ulPropID) == FAILURE) return;

	// PHP uses variable type 'long' internally. If a number in a string as key is used in add_assoc_*(),
	// it is re-interpreted a a number when it's smaller than LONG_MAX.
	// however, LONG_MAX is 2147483647L on 32-bit systems, but 9223372036854775807L on 64-bit.
	// this make named props (0x80000000+) fit in the 'number' description, and so this breaks on 64-bit systems
	// .. well, it un-breaks on 64-bit .. so we cast the unsigned proptag to a signed number here, so
	// the compares within .php files can be correctly performed, so named props work.

	// maybe we need to rewrite this system a bit, so proptags are always a string, and never interpreted
	// e.g. by prepending the assoc keys with 'PROPTAG' or something...

	RETURN_LONG((LONG)PROP_TAG(ulPropType, ulPropID));
}

ZEND_FUNCTION(mapi_createoneoff)
{
	PMEASURE_FUNC;

	LOG_BEGIN();
	// params
	char *szDisplayName = NULL;
	char *szType = NULL;
	char *szEmailAddress = NULL;
	php_stringsize_t ulDisplayNameLen = 0, ulTypeLen = 0, ulEmailAddressLen = 0;
	long ulFlags = MAPI_SEND_NO_RICH_INFO;
	// return value
	memory_ptr<ENTRYID> lpEntryID;
	ULONG cbEntryID = 0;
	// local
	std::wstring name, type, email;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sss|l",
		&szDisplayName, &ulDisplayNameLen,
		&szType, &ulTypeLen,
		&szEmailAddress, &ulEmailAddressLen, &ulFlags) == FAILURE) return;

	MAPI_G(hr) = TryConvert(szDisplayName, name);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("CreateOneOff name conversion failed", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = TryConvert(szType, type);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("CreateOneOff type conversion failed", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = TryConvert(szEmailAddress, email);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("CreateOneOff address conversion failed", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = ECCreateOneOff((LPTSTR)name.c_str(), (LPTSTR)type.c_str(), (LPTSTR)email.c_str(), MAPI_UNICODE | ulFlags, &cbEntryID, &~lpEntryID);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("CreateOneOff failed", MAPI_G(hr));
		goto exit;
	}

	RETVAL_STRINGL(reinterpret_cast<const char *>(lpEntryID.get()), cbEntryID, 1);
exit:
	// using RETVAL_* not RETURN_*, otherwise php will instantly return itself, and we won't be able to free...
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_parseoneoff)
{
	PMEASURE_FUNC;

	LOG_BEGIN();
	// params
	LPENTRYID lpEntryID = NULL;
	php_stringsize_t cbEntryID = 0;
	// return value
	utf8string strDisplayName;
	utf8string strType;
	utf8string strAddress;
	std::wstring wstrDisplayName;
	std::wstring wstrType;
	std::wstring wstrAddress;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &lpEntryID, &cbEntryID) == FAILURE) return;

	MAPI_G(hr) = ECParseOneOff(lpEntryID, cbEntryID, wstrDisplayName, wstrType, wstrAddress);

	if(MAPI_G(hr) != hrSuccess) {
		kphperr("ParseOneOff failed", MAPI_G(hr));
		goto exit;
	}

	array_init(return_value);

	strDisplayName = convert_to<utf8string>(wstrDisplayName);
	strType = convert_to<utf8string>(wstrType);
	strAddress = convert_to<utf8string>(wstrAddress);

	add_assoc_string(return_value, "name", (char*)strDisplayName.c_str(), 1);
	add_assoc_string(return_value, "type", (char*)strType.c_str(), 1);
	add_assoc_string(return_value, "address", (char*)strAddress.c_str(), 1);
exit:
	DEFERRED_EPILOGUE;
}

/*
 * How sessions and stores work:
 * - mapi_logon_zarafa() creates a session and returns this
 * - mapi_getmsgstorestable() to get the entryid of the default and public store
 * - mapi_openmsgstore() to open the default user store + public store
 * - mapi_msgstore_createentryid() retuns a store entryid of requested user
 * - store entryid can be used with mapi_openmsgstore() to open
 *
 * Removed, how it did work in the far past:
 * - mapi_openmsgstore_zarafa() creates a session, opens the user's store, and the public
 *   and returns these store pointers in an array.
 * - mapi_msgstore_createentryid() is used to call Store->CreateStoreEntryID()
 * - mapi_openmsgstore_zarafa_other() with the prev acquired entryid opens the store of another user,
 * - mapi_openmsgstore_zarafa_other() is therefor called with the current user id and password (and maybe the server) as well!
 * Only with this info we can find the session again, to get the IMAPISession, and open another store.
 */

ZEND_FUNCTION(mapi_logon_zarafa)
{
	PMEASURE_FUNC;

	LOG_BEGIN();
	// params
	char		*username = NULL;
	char		*password = NULL;
	const char *server = NULL;
	const char *sslcert = "";
	const char *sslpass = "";
	const char *wa_version = "";
	const char *misc_version = "";
	php_stringsize_t username_len = 0, password_len = 0, server_len = 0;
	php_stringsize_t sslcert_len = 0, sslpass_len = 0, wa_version_len = 0;
	php_stringsize_t misc_version_len = 0;
	long		ulFlags = EC_PROFILE_FLAGS_NO_NOTIFICATIONS;
	// return value
	object_ptr<IMAPISession> lpMAPISession;
	// local
	ULONG		ulProfNum = rand_mt();
	char		szProfName[MAX_PATH];
	SPropValue	sPropOur[8];

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|ssslss",
		&username, &username_len, &password, &password_len, &server, &server_len,
		&sslcert, &sslcert_len, &sslpass, &sslpass_len, &ulFlags,
		&wa_version, &wa_version_len, &misc_version, &misc_version_len) == FAILURE) return;

	if (!server) {
		server = "http://localhost:236/";
		server_len = strlen(server);
	}

	snprintf(szProfName, MAX_PATH-1, "www-profile%010u", ulProfNum);

	sPropOur[0].ulPropTag = PR_EC_PATH;
	sPropOur[0].Value.lpszA = const_cast<char *>(server);
	sPropOur[1].ulPropTag = PR_EC_USERNAME_A;
	sPropOur[1].Value.lpszA = const_cast<char *>(username);
	sPropOur[2].ulPropTag = PR_EC_USERPASSWORD_A;
	sPropOur[2].Value.lpszA = const_cast<char *>(password);
	sPropOur[3].ulPropTag = PR_EC_FLAGS;
	sPropOur[3].Value.ul = ulFlags;

	// unused if PR_EC_PATH is not https
	sPropOur[4].ulPropTag = PR_EC_SSLKEY_FILE;
	sPropOur[4].Value.lpszA = const_cast<char *>(sslcert);
	sPropOur[5].ulPropTag = PR_EC_SSLKEY_PASS;
	sPropOur[5].Value.lpszA = const_cast<char *>(sslpass);

	sPropOur[6].ulPropTag = PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION;
	sPropOur[6].Value.lpszA = const_cast<char *>(wa_version);
	sPropOur[7].ulPropTag = PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC;
	sPropOur[7].Value.lpszA = const_cast<char *>(misc_version);

	MAPI_G(hr) = mapi_util_createprof(szProfName, "ZARAFA6", 8, sPropOur);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s", mapi_util_getlasterror().c_str());
		goto exit; // error already displayed in mapi_util_createprof
	}

	// Logon to our new profile
	MAPI_G(hr) = MAPILogonEx(0, reinterpret_cast<LPTSTR>(const_cast<char *>(szProfName)),
	             reinterpret_cast<LPTSTR>(const_cast<char *>("")),
	             MAPI_EXTENDED | MAPI_TIMEOUT_SHORT | MAPI_NEW_SESSION,
	             &~lpMAPISession);
	if (MAPI_G(hr) != hrSuccess) {
		mapi_util_deleteprof(szProfName);
		kphperr("Unable to logon to profile", MAPI_G(hr));
		goto exit;
	}

	// Delete the profile (it will be deleted when we close our session)
	MAPI_G(hr) = mapi_util_deleteprof(szProfName);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to delete profile", MAPI_G(hr));
		goto exit;
	}

	ZEND_REGISTER_RESOURCE(return_value, lpMAPISession.release(), le_mapi_session);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_openentry
* Opens the msgstore from the session
* @param Resource IMAPISession
* @param String EntryID
*/
ZEND_FUNCTION(mapi_openentry)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res;
	IMAPISession *lpSession = NULL;
	php_stringsize_t cbEntryID = 0;
	LPENTRYID	lpEntryID	= NULL;
	long		ulFlags = MAPI_BEST_ACCESS;
	// return value
	object_ptr<IUnknown> lpUnknown; // either folder or message
	// local
	ULONG		ulObjType;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|sl", &res, &lpEntryID, &cbEntryID, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpSession, IMAPISession *, &res, -1, name_mapi_session, le_mapi_session);
	MAPI_G(hr) = lpSession->OpenEntry(cbEntryID, lpEntryID,
	             &iid_of(lpUnknown), ulFlags, &ulObjType, &~lpUnknown);
	if (FAILED(MAPI_G(hr)))
		goto exit;

	if (ulObjType == MAPI_FOLDER) {
		object_ptr<IMAPIFolder> fld;
		MAPI_G(hr) = lpUnknown->QueryInterface(iid_of(fld), &~fld);
		if (FAILED(MAPI_G(hr)))
			goto exit;
		ZEND_REGISTER_RESOURCE(return_value, fld.release(), le_mapi_folder);
	}
	else if(ulObjType == MAPI_MESSAGE) {
		object_ptr<IMessage> msg;
		MAPI_G(hr) = lpUnknown->QueryInterface(iid_of(msg), &~msg);
		if (FAILED(MAPI_G(hr)))
			goto exit;
		ZEND_REGISTER_RESOURCE(return_value, msg.release(), le_mapi_message);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "EntryID is not a folder or a message.");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

exit:
	DEFERRED_EPILOGUE;
}

// IMAPISession::OpenAddressBook()
// must have valid session
ZEND_FUNCTION(mapi_openaddressbook)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res;
	IMAPISession *lpSession = NULL;
	// return value
	LPADRBOOK lpAddrBook;
	// local

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpSession, IMAPISession*, &res, -1, name_mapi_session, le_mapi_session);

	MAPI_G(hr) = lpSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, &lpAddrBook);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpAddrBook, le_mapi_addrbook);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_ab_openentry) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res;
	LPADRBOOK	lpAddrBook = NULL;
	php_stringsize_t cbEntryID = 0;
	LPENTRYID	lpEntryID = NULL;
	long		ulFlags = 0; //MAPI_BEST_ACCESS;
	// return value
	ULONG		ulObjType;
	object_ptr<IUnknown> lpUnknown;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|sl", &res, &lpEntryID, &cbEntryID, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpAddrBook, LPADRBOOK, &res, -1, name_mapi_addrbook, le_mapi_addrbook);

	MAPI_G(hr) = lpAddrBook->OpenEntry(cbEntryID, lpEntryID,
	             &iid_of(lpUnknown), ulFlags, &ulObjType, &~lpUnknown);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	if (ulObjType == MAPI_MAILUSER) {
		object_ptr<IMailUser> usr;
		MAPI_G(hr) = lpUnknown->QueryInterface(iid_of(usr), &~usr);
		if (FAILED(MAPI_G(hr)))
			goto exit;
		ZEND_REGISTER_RESOURCE(return_value, usr.release(), le_mapi_mailuser);
	} else if (ulObjType == MAPI_DISTLIST) {
		object_ptr<IDistList> dl;
		MAPI_G(hr) = lpUnknown->QueryInterface(iid_of(dl), &~dl);
		if (FAILED(MAPI_G(hr)))
			goto exit;
		ZEND_REGISTER_RESOURCE(return_value, dl.release(), le_mapi_distlist);
	} else if (ulObjType == MAPI_ABCONT) {
		object_ptr<IABContainer> ab;
		MAPI_G(hr) = lpUnknown->QueryInterface(iid_of(ab), &~ab);
		if (FAILED(MAPI_G(hr)))
			goto exit;
		ZEND_REGISTER_RESOURCE(return_value, ab.release(), le_mapi_abcont);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "EntryID is not an AddressBook item");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_ab_resolvename) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res;
	LPADRBOOK	lpAddrBook = NULL;
	zval		*array;
	zval		*rowset;
	long		ulFlags = 0;
	// return value

	// local
	adrlist_ptr lpAList;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|l", &res, &array, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpAddrBook, LPADRBOOK, &res, -1, name_mapi_addrbook, le_mapi_addrbook);

	MAPI_G(hr) = PHPArraytoAdrList(array, nullptr, &~lpAList TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = lpAddrBook->ResolveName(0, ulFlags, NULL, lpAList);
	switch (MAPI_G(hr)) {
	case hrSuccess:
		// parse back lpAList and return as array
		RowSettoPHPArray(reinterpret_cast<SRowSet *>(lpAList.get()), &rowset TSRMLS_CC); // binary compatible
		RETVAL_ZVAL(rowset, 0, 0);
		FREE_ZVAL(rowset);
		break;
	case MAPI_E_AMBIGUOUS_RECIP:
	case MAPI_E_NOT_FOUND:
	default:
		break;
	};

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_ab_getdefaultdir) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res;
	LPADRBOOK	lpAddrBook = NULL;
	// return value
	memory_ptr<ENTRYID> lpEntryID;
	ULONG cbEntryID = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpAddrBook, LPADRBOOK, &res, -1, name_mapi_addrbook, le_mapi_addrbook);

	MAPI_G(hr) = lpAddrBook->GetDefaultDir(&cbEntryID, &~lpEntryID);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Failed GetDefaultDir of addressbook", MAPI_G(hr));
		goto exit;
	}

	RETVAL_STRINGL(reinterpret_cast<const char *>(lpEntryID.get()), cbEntryID, 1);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_getstores
* Gets the table with the messagestores available
*
*/
ZEND_FUNCTION(mapi_getmsgstorestable)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
	IMAPISession *lpSession = NULL;
	// return value
	LPMAPITABLE	lpTable = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpSession, IMAPISession *, &res, -1, name_mapi_session, le_mapi_session);

	MAPI_G(hr) = lpSession->GetMsgStoresTable(0, &lpTable);

	if (FAILED(MAPI_G(hr))) {
		kphperr("Unable to fetch the message store table", MAPI_G(hr));
		goto exit;
	}
	ZEND_REGISTER_RESOURCE(return_value, lpTable, le_mapi_table);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_openmsgstore
* Opens the messagestore for the entryid
* @param String with the entryid
* @return RowSet with messages
*/
ZEND_FUNCTION(mapi_openmsgstore)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	php_stringsize_t cbEntryID = 0;
	LPENTRYID	lpEntryID	= NULL;
	zval *res = NULL;
	IMAPISession * lpSession = NULL;
	// return value
	LPMDB	pMDB = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs",
		&res, (char *)&lpEntryID, &cbEntryID) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpSession, IMAPISession *, &res, -1, name_mapi_session, le_mapi_session);

	MAPI_G(hr) = lpSession->OpenMsgStore(0, cbEntryID, lpEntryID, 0, MAPI_BEST_ACCESS | MDB_NO_DIALOG, &pMDB);

	if (FAILED(MAPI_G(hr))) {
		kphperr("Unable to open message store", MAPI_G(hr));
		goto exit;
	}
	ZEND_REGISTER_RESOURCE(return_value, pMDB, le_mapi_msgstore);
exit:
	DEFERRED_EPILOGUE;
}

/** 
 * Open the profile section of given guid, and returns the php-usable IMAPIProp object
 * 
 * @param[in] Resource mapi session
 * @param[in] String mapi uid of profile section
 * 
 * @return IMAPIProp interface of IProfSect object
 */
ZEND_FUNCTION(mapi_openprofilesection)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res;
	IMAPISession *lpSession = NULL;
	php_stringsize_t uidlen;
	LPMAPIUID lpUID = NULL;
	// return value
	IMAPIProp *lpProfSectProp = NULL;
	// local

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpUID, &uidlen) == FAILURE) return;

	if (uidlen != sizeof(MAPIUID))
		goto exit;

	ZEND_FETCH_RESOURCE_C(lpSession, IMAPISession*, &res, -1, name_mapi_session, le_mapi_session);

	// yes, you can request any compatible interface, but the return pointer is LPPROFSECT .. ohwell.
	MAPI_G(hr) = lpSession->OpenProfileSection(lpUID, &IID_IMAPIProp, 0, (LPPROFSECT*)&lpProfSectProp);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpProfSectProp, le_mapi_property);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_folder_gethierarchtable
* Opens the hierarchytable from a folder
* @param Resource mapifolder
*
*/
ZEND_FUNCTION(mapi_folder_gethierarchytable)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval	*res;
	long	ulFlags	= 0;
	// return value
	LPMAPITABLE	lpTable = NULL;
	int type = -1;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &res, &ulFlags) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_folder) {
		IMAPIFolder *fld = nullptr;
		ZEND_FETCH_RESOURCE_C(fld, decltype(fld), &res, -1, name_mapi_folder, le_mapi_folder);
		MAPI_G(hr) = fld->GetHierarchyTable(ulFlags, &lpTable);
	} else if (type == le_mapi_abcont) {
		IABContainer *ab = nullptr;
		ZEND_FETCH_RESOURCE_C(ab, decltype(ab), &res, -1, name_mapi_abcont, le_mapi_abcont);
		MAPI_G(hr) = ab->GetHierarchyTable(ulFlags, &lpTable);
	} else if (type == le_mapi_distlist) {
		IDistList *dl = nullptr;
		ZEND_FETCH_RESOURCE_C(dl, decltype(dl), &res, -1, name_mapi_distlist, le_mapi_distlist);
		MAPI_G(hr) = dl->GetHierarchyTable(ulFlags, &lpTable);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Resource is not a valid IMAPIFolder or derivative");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// return the returncode
	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpTable, le_mapi_table);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_msgstore_getcontentstable
*
* @param Resource MAPIMSGStore
* @param long Flags
* @return MAPIFolder
*/
ZEND_FUNCTION(mapi_folder_getcontentstable)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res	= NULL;
	long			ulFlags	= 0;
	// return value
	LPMAPITABLE		pTable	= NULL;
	// local
	int type = -1;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &res, &ulFlags) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_folder) {
		IMAPIFolder *fld = nullptr;
		ZEND_FETCH_RESOURCE_C(fld, decltype(fld), &res, -1, name_mapi_folder, le_mapi_folder);
		MAPI_G(hr) = fld->GetContentsTable(ulFlags, &pTable);
	} else if (type == le_mapi_abcont) {
		IABContainer *ab = nullptr;
		ZEND_FETCH_RESOURCE_C(ab, decltype(ab), &res, -1, name_mapi_abcont, le_mapi_abcont);
		MAPI_G(hr) = ab->GetContentsTable(ulFlags, &pTable);
	} else if( type == le_mapi_distlist) {
		IDistList *dl = nullptr;
		ZEND_FETCH_RESOURCE_C(dl, decltype(dl), &res, -1, name_mapi_distlist, le_mapi_distlist);
		MAPI_G(hr) = dl->GetContentsTable(ulFlags, &pTable);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Resource is not a valid IMAPIContainer or derivative");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, pTable, le_mapi_table);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_folder_createmessage
*
*
*/
ZEND_FUNCTION(mapi_folder_createmessage)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval * res;
	LPMAPIFOLDER	pFolder	= NULL;
	long ulFlags = 0;
	// return value
	LPMESSAGE pMessage;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &res, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pFolder, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = pFolder->CreateMessage(NULL, ulFlags, &pMessage);

	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, pMessage, le_mapi_message);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_folder_deletemessage
*
*
*/
ZEND_FUNCTION(mapi_folder_deletemessages)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	LPMAPIFOLDER	pFolder = NULL;
	zval			*res = NULL;
	zval			*entryid_array = NULL;
	long			ulFlags = 0;
	// local
	memory_ptr<ENTRYLIST> lpEntryList;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|l", &res, &entryid_array, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pFolder, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = PHPArraytoSBinaryArray(entryid_array, NULL, &~lpEntryList TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Bad message list", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = pFolder->DeleteMessages(lpEntryList, 0, NULL, ulFlags);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

/**
* Function to copy message from the source folder to the destination folder. A folder must be opened with openentry.
*
* @param SourceFolder, Message List, DestinationFolder, flags (optional)
*/
ZEND_FUNCTION(mapi_folder_copymessages)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	LPMAPIFOLDER	lpSrcFolder = NULL, lpDestFolder = NULL;
	zval			*srcFolder = NULL, *destFolder = NULL;
	zval			*msgArray = NULL;
	long			flags = 0;
	// local
	memory_ptr<ENTRYLIST> lpEntryList;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rar|l", &srcFolder, &msgArray, &destFolder, &flags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpSrcFolder, LPMAPIFOLDER, &srcFolder, -1, name_mapi_folder, le_mapi_folder);
	ZEND_FETCH_RESOURCE_C(lpDestFolder, LPMAPIFOLDER, &destFolder, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = PHPArraytoSBinaryArray(msgArray, NULL, &~lpEntryList TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Bad message list", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = lpSrcFolder->CopyMessages(lpEntryList, NULL, lpDestFolder, 0, NULL, flags);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

/*
* Function to set the read flags for a folder.
*
*
*/
ZEND_FUNCTION(mapi_folder_setreadflags)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL, *entryArray = NULL;
	long			flags = 0;
	// local
	LPMAPIFOLDER	lpFolder = NULL;
	memory_ptr<ENTRYLIST> lpEntryList;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|l", &res, &entryArray, &flags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFolder, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = PHPArraytoSBinaryArray(entryArray, NULL, &~lpEntryList TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Bad message list", MAPI_G(hr));
		goto exit;
	}

	// Special case: if an empty array was passed, treat it as NULL
	if(lpEntryList->cValues != 0)
       	MAPI_G(hr) = lpFolder->SetReadFlags(lpEntryList, 0, NULL, flags);
    else
        MAPI_G(hr) = lpFolder->SetReadFlags(NULL, 0, NULL, flags);

	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_folder_createfolder) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	LPMAPIFOLDER lpSrcFolder = NULL;
	zval *srcFolder = NULL;
	long folderType = FOLDER_GENERIC, ulFlags = 0;
	const char *lpszFolderName = "", *lpszFolderComment = "";
	php_stringsize_t FolderNameLen = 0, FolderCommentLen = 0;
	// return value
	LPMAPIFOLDER lpNewFolder = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs|sll", &srcFolder, &lpszFolderName, &FolderNameLen, &lpszFolderComment, &FolderCommentLen, &ulFlags, &folderType) == FAILURE) return;

	if (FolderNameLen == 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Foldername cannot be empty");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	if (FolderCommentLen == 0)
		lpszFolderComment = NULL;

	ZEND_FETCH_RESOURCE_C(lpSrcFolder, LPMAPIFOLDER, &srcFolder, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = lpSrcFolder->CreateFolder(folderType, (LPTSTR)lpszFolderName, (LPTSTR)lpszFolderComment, NULL, ulFlags & ~MAPI_UNICODE, &lpNewFolder);
	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpNewFolder, le_mapi_folder);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_folder_deletefolder)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	ENTRYID			*lpEntryID = NULL;
	php_stringsize_t cbEntryID = 0;
	long			ulFlags = 0;
	zval			*res = NULL;
	LPMAPIFOLDER	lpFolder = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs|l", &res, &lpEntryID, &cbEntryID, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFolder, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = lpFolder->DeleteFolder(cbEntryID, lpEntryID, 0, NULL, ulFlags);
	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_folder_emptyfolder)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res;
	LPMAPIFOLDER	lpFolder = NULL;
	long			ulFlags = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &res, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFolder, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = lpFolder->EmptyFolder(0, NULL, ulFlags);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

/**
* Function that copies (or moves) a folder to another folder.
*
*
*/
ZEND_FUNCTION(mapi_folder_copyfolder)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*zvalSrcFolder, *zvalDestFolder;
	LPMAPIFOLDER	lpSrcFolder = NULL, lpDestFolder = NULL;
	ENTRYID			*lpEntryID = NULL;
	php_stringsize_t cbEntryID = 0, cbNewFolderNameLen = 0;
	long			ulFlags = 0;
	LPTSTR			lpszNewFolderName = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	// Params: (SrcFolder, entryid, DestFolder, (opt) New foldername, (opt) Flags)
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsr|sl", &zvalSrcFolder, &lpEntryID, &cbEntryID, &zvalDestFolder, &lpszNewFolderName, &cbNewFolderNameLen, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpSrcFolder, LPMAPIFOLDER, &zvalSrcFolder, -1, name_mapi_folder, le_mapi_folder);
	ZEND_FETCH_RESOURCE_C(lpDestFolder, LPMAPIFOLDER, &zvalDestFolder, -1, name_mapi_folder, le_mapi_folder);

	if (lpEntryID == NULL) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "EntryID must not be empty.");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// If php input is NULL, lpszNewFolderName ="" but you expect a NULL string
	if(cbNewFolderNameLen == 0)
		lpszNewFolderName = NULL;

	MAPI_G(hr) = lpSrcFolder->CopyFolder(cbEntryID, lpEntryID, NULL, lpDestFolder, lpszNewFolderName, 0, NULL, ulFlags);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_msgstore_createentryid
* Creates an EntryID to open a store with mapi_openmsgstore.
* @param Resource IMsgStore
*        String   username
*
* return value: EntryID or FALSE when there's no permission...?
*/
ZEND_FUNCTION(mapi_msgstore_createentryid)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res;
	LPMDB		pMDB		= NULL;
	LPSTR		sMailboxDN = NULL;
	php_stringsize_t lMailboxDN = 0;
	// return value
	ULONG		cbEntryID	= 0;
	memory_ptr<ENTRYID> lpEntryID;
	// local
	object_ptr<IExchangeManageStore> lpEMS;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &sMailboxDN, &lMailboxDN) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMDB, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);

	MAPI_G(hr) = pMDB->QueryInterface(IID_IExchangeManageStore, &~lpEMS);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("IExchangeManageStore interface was not supported by given store", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = lpEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(""), reinterpret_cast<const TCHAR *>(sMailboxDN), 0, &cbEntryID, &~lpEntryID);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	RETVAL_STRINGL(reinterpret_cast<const char *>(lpEntryID.get()), cbEntryID, 1);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_msgstore_getearchiveentryid
* Creates an EntryID to open an archive store with mapi_openmsgstore.
* @param Resource IMsgStore
*        String   username
*        String   servername (server containing the archive)
*
* return value: EntryID or FALSE when there's no permission...?
*/
ZEND_FUNCTION(mapi_msgstore_getarchiveentryid)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res;
	LPMDB		pMDB		= NULL;
	LPSTR		sUser = NULL;
	LPSTR		sServer = NULL;
	php_stringsize_t lUser = 0, lServer = 0;
	// return value
	ULONG		cbEntryID	= 0;
	EntryIdPtr	ptrEntryID;
	// local
	ECServiceAdminPtr ptrSA;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", &res, &sUser, &lUser, &sServer, &lServer) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMDB, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);

	MAPI_G(hr) = pMDB->QueryInterface(iid_of(ptrSA), &~ptrSA);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("IECServiceAdmin interface was not supported by given store", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = ptrSA->GetArchiveStoreEntryID((LPTSTR)sUser, (LPTSTR)sServer, 0, &cbEntryID, &~ptrEntryID);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_STRINGL((char *)ptrEntryID.get(), cbEntryID, 1);

exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_openentry
* Opens the msgstore to get the root folder.
* @param Resource IMsgStore
* @param String EntryID
*/
ZEND_FUNCTION(mapi_msgstore_openentry)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res;
	LPMDB		pMDB		= NULL;
	php_stringsize_t cbEntryID = 0;
	LPENTRYID	lpEntryID	= NULL;
	long		ulFlags = MAPI_BEST_ACCESS;
	// return value
	object_ptr<IUnknown> lpUnknown; // either folder or message
	// local
	ULONG		ulObjType;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|sl", &res, &lpEntryID, &cbEntryID, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMDB, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);

	// returns a folder
	MAPI_G(hr) = pMDB->OpenEntry(cbEntryID, lpEntryID, &iid_of(lpUnknown),
	             ulFlags, &ulObjType, &~lpUnknown);
	if (FAILED(MAPI_G(hr)))
		goto exit;

	if (ulObjType == MAPI_FOLDER) {
		object_ptr<IMAPIFolder> fld;
		MAPI_G(hr) = lpUnknown->QueryInterface(iid_of(fld), &~fld);
		if (FAILED(MAPI_G(hr)))
			goto exit;
		ZEND_REGISTER_RESOURCE(return_value, fld.release(), le_mapi_folder);
	}
	else if(ulObjType == MAPI_MESSAGE) {
		object_ptr<IMessage> msg;
		MAPI_G(hr) = lpUnknown->QueryInterface(iid_of(msg), &~msg);
		if (FAILED(MAPI_G(hr)))
			goto exit;
		ZEND_REGISTER_RESOURCE(return_value, msg.release(), le_mapi_message);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "EntryID is not a folder or a message.");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_msgstore_entryidfromsourcekey)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval	*resStore = NULL;
	BYTE 	*lpSourceKeyMessage = NULL;
	php_stringsize_t cbSourceKeyMessage = 0, cbSourceKeyFolder = 0;
	LPMDB	lpMsgStore = NULL;
	BYTE	*lpSourceKeyFolder = NULL;
	memory_ptr<ENTRYID> lpEntryId;
	ULONG		cbEntryId = 0;
	object_ptr<IExchangeManageStore> lpIEMS;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs|s", &resStore, &lpSourceKeyFolder, &cbSourceKeyFolder, &lpSourceKeyMessage, &cbSourceKeyMessage) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &resStore, -1, name_mapi_msgstore, le_mapi_msgstore);

	MAPI_G(hr) = lpMsgStore->QueryInterface(IID_IExchangeManageStore, &~lpIEMS);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = lpIEMS->EntryIDFromSourceKey(cbSourceKeyFolder, lpSourceKeyFolder, cbSourceKeyMessage, lpSourceKeyMessage, &cbEntryId, &~lpEntryId);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;
	RETVAL_STRINGL(reinterpret_cast<const char *>(lpEntryId.get()), cbEntryId, 1);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_msgstore_advise)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval	*resStore = NULL;
	zval	*resSink = NULL;
	LPMDB	lpMsgStore = NULL;
	IMAPIAdviseSink *lpSink = NULL;
	LPENTRYID lpEntryId = NULL;
	php_stringsize_t cbEntryId = 0;
	long	ulMask = 0;
	ULONG 	ulConnection = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rslr", &resStore, &lpEntryId, &cbEntryId, &ulMask, &resSink) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &resStore, -1, name_mapi_msgstore, le_mapi_msgstore);
	ZEND_FETCH_RESOURCE_C(lpSink, MAPINotifSink *, &resSink, -1, name_mapi_advisesink, le_mapi_advisesink);

	// Sanitize NULL entryids
	if(cbEntryId == 0) lpEntryId = NULL;

	MAPI_G(hr) = lpMsgStore->Advise(cbEntryId, lpEntryId, ulMask, lpSink, &ulConnection);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_LONG(ulConnection);

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_msgstore_unadvise)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval	*resStore = NULL;
	LPMDB	lpMsgStore = NULL;
	long 	ulConnection = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &resStore, &ulConnection) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &resStore, -1, name_mapi_msgstore, le_mapi_msgstore);

	MAPI_G(hr) = lpMsgStore->Unadvise(ulConnection);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_sink_create)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	MAPINotifSink *lpSink = NULL;
	RETVAL_FALSE;
    
	MAPI_G(hr) = MAPINotifSink::Create(&lpSink);
	ZEND_REGISTER_RESOURCE(return_value, lpSink, le_mapi_advisesink);
	LOG_END();
}

ZEND_FUNCTION(mapi_sink_timedwait)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *resSink = NULL;
	zval *notifications = NULL;
	long ulTime = 0;
	MAPINotifSink *lpSink = NULL;
	ULONG cNotifs = 0;
	memory_ptr<NOTIFICATION> lpNotifs;
    
	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &resSink, &ulTime) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpSink, MAPINotifSink *, &resSink, -1, name_mapi_advisesink, le_mapi_advisesink);
	
	MAPI_G(hr) = lpSink->GetNotifications(&cNotifs, &~lpNotifs, false, ulTime);
	if(MAPI_G(hr) != hrSuccess)
	    goto exit;
	    
	MAPI_G(hr) = NotificationstoPHPArray(cNotifs, lpNotifs, &notifications TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("The notifications could not be converted to a PHP array", MAPI_G(hr));
		goto exit;
	}

	RETVAL_ZVAL(notifications, 0, 0);
	FREE_ZVAL(notifications);

exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_table_hrqueryallrows
* Execute htqueryallrows on a table
* @param Resource MAPITable
* @return Array
*/
ZEND_FUNCTION(mapi_table_queryallrows)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res				= NULL;
	zval			*tagArray			= NULL;
	zval			*restrictionArray	= NULL;
	zval			*rowset				= NULL;
	LPMAPITABLE		lpTable				= NULL;
	// return value
	// locals
	memory_ptr<SPropTagArray> lpTagArray;
	memory_ptr<SRestriction> lpRestrict;
	rowset_ptr pRowSet;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|aa", &res, &tagArray, &restrictionArray) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	if (restrictionArray != NULL) {
		// create restrict array
		MAPI_G(hr) = MAPIAllocateBuffer(sizeof(SRestriction), &~lpRestrict);
		if (MAPI_G(hr) != hrSuccess)
			goto exit;

		MAPI_G(hr) = PHPArraytoSRestriction(restrictionArray, /* result */lpRestrict, /* Base */lpRestrict TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess) {
			kphperr("Failed to convert the PHP srestriction array", MAPI_G(hr));
			goto exit;
		}
	}

	if (tagArray != NULL) {
		// create proptag array
		MAPI_G(hr) = PHPArraytoPropTagArray(tagArray, NULL, &~lpTagArray TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess) {
			kphperr("Failed to convert the PHP proptag array", MAPI_G(hr));
			goto exit;
		}
	}

	// Execute
	MAPI_G(hr) = HrQueryAllRows(lpTable, lpTagArray, lpRestrict, nullptr, 0, &~pRowSet);

	// return the returncode
	if (FAILED(MAPI_G(hr)))
		goto exit;
	MAPI_G(hr) = RowSettoPHPArray(pRowSet.get(), &rowset TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("The resulting rowset could not be converted to a PHP array", MAPI_G(hr));
		goto exit;
	}
	RETVAL_ZVAL(rowset, 0, 0);
	FREE_ZVAL(rowset);

exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_table_queryrows
* Execute queryrows on a table
* @param Resource MAPITable
* @param long
* @param array
* @return array
*/
ZEND_FUNCTION(mapi_table_queryrows)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res	= NULL;
	LPMAPITABLE	lpTable = NULL;
	zval		*tagArray = NULL;
	zval		*rowset = NULL;
	memory_ptr<SPropTagArray> lpTagArray;
	long		lRowCount = 0, start = 0;
	// local
	rowset_ptr pRowSet;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|a!ll", &res, &tagArray, &start, &lRowCount) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	if (tagArray != NULL) {
		MAPI_G(hr) = PHPArraytoPropTagArray(tagArray, NULL, &~lpTagArray TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess) {
			kphperr("Failed to convert the PHP array", MAPI_G(hr));
			goto exit;
		}

		MAPI_G(hr) = lpTable->SetColumns(lpTagArray, TBL_BATCH);

		if (FAILED(MAPI_G(hr))) {
			kphperr("SetColumns failed", MAPI_G(hr));
			goto exit;
		}
	}

	// move to the starting row if there is one
	if (start != 0) {
		MAPI_G(hr) = lpTable->SeekRow(BOOKMARK_BEGINNING, start, NULL);

		if (FAILED(MAPI_G(hr))) {
			kphperr("SeekRow failed", MAPI_G(hr));
			goto exit;
		}
	}

	MAPI_G(hr) = lpTable->QueryRows(lRowCount, 0, &~pRowSet);
	if (FAILED(MAPI_G(hr)))
		goto exit;
	MAPI_G(hr) = RowSettoPHPArray(pRowSet.get(), &rowset TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("The resulting rowset could not be converted to a PHP array", MAPI_G(hr));
		goto exit;
	}

	RETVAL_ZVAL(rowset, 0, 0);
	FREE_ZVAL(rowset);

exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_table_setcolumns
* Execute setcolumns on a table
* @param Resource MAPITable
* @param array		column set
* @return true/false
*/
ZEND_FUNCTION(mapi_table_setcolumns)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res	= NULL;
	LPMAPITABLE	lpTable = NULL;
	zval		*tagArray = NULL;
	long		lFlags = 0;
	// local
	memory_ptr<SPropTagArray> lpTagArray;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|l", &res, &tagArray, &lFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	MAPI_G(hr) = PHPArraytoPropTagArray(tagArray, NULL, &~lpTagArray TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Failed to convert the PHP array", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = lpTable->SetColumns(lpTagArray, lFlags);

	if (FAILED(MAPI_G(hr))) {
		kphperr("SetColumns failed", MAPI_G(hr));
		goto exit;
	}

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

/**
 * mapi_table_seekrow
 * Execute seekrow on a table
 * @param Resource MAPITable
 * @param long bookmark
 * @param long Flags
 * @return long RowsSought
 */
ZEND_FUNCTION(mapi_table_seekrow)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res	= NULL;
	LPMAPITABLE	lpTable = NULL;
	long		lRowCount = 0, lbookmark = BOOKMARK_BEGINNING;
	// return
	long lRowsSought = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rll", &res, &lbookmark, &lRowCount) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	MAPI_G(hr) = lpTable->SeekRow((BOOKMARK)lbookmark, lRowCount, (LONG*)&lRowsSought);

	if (FAILED(MAPI_G(hr))) {
		kphperr("SeekRow failed", MAPI_G(hr));
		goto exit;
	}

	RETVAL_LONG(lRowsSought);
	
exit:
	DEFERRED_EPILOGUE;
}

/**
*
*
*
*/
ZEND_FUNCTION(mapi_table_sort)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval * res;
	zval * sortArray;
	long ulFlags = 0;
	// local
	LPMAPITABLE	lpTable				= NULL;
	memory_ptr<SSortOrderSet> lpSortCriteria;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|l", &res, &sortArray, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	MAPI_G(hr) = PHPArraytoSortOrderSet(sortArray, NULL, &~lpSortCriteria TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess)
		kphperr("Unable to convert sort order set from the PHP array", MAPI_G(hr));
	MAPI_G(hr) = lpTable->SortTable(lpSortCriteria, ulFlags);
	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

/**
*
*
*
*/
ZEND_FUNCTION(mapi_table_getrowcount)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res;
	LPMAPITABLE	lpTable = NULL;
	// return value
	ULONG count = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	MAPI_G(hr) = lpTable->GetRowCount(0, &count);
	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_LONG(count);
exit:
	DEFERRED_EPILOGUE;
}

/**
*
*
*
*/
ZEND_FUNCTION(mapi_table_restrict)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res;
	zval			*restrictionArray;
	ulong			ulFlags = 0;
	// local
	LPMAPITABLE		lpTable = NULL;
	memory_ptr<SRestriction> lpRestrict;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|l", &res, &restrictionArray, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	if (!restrictionArray || zend_hash_num_elements(Z_ARRVAL_P(restrictionArray)) == 0) {
		// reset restriction
		lpRestrict.reset();
	} else {
		// create restrict array
		MAPI_G(hr) = PHPArraytoSRestriction(restrictionArray, NULL, &~lpRestrict TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess) {
			kphperr("Failed to convert the PHP srestriction array", MAPI_G(hr));
			goto exit;
		}
	}

	MAPI_G(hr) = lpTable->Restrict(lpRestrict, ulFlags);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_table_findrow)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res;
	zval			*restrictionArray;
	ulong			bkOrigin = BOOKMARK_BEGINNING;
	ulong			ulFlags = 0;
	// local
	LPMAPITABLE		lpTable = NULL;
	memory_ptr<SRestriction> lpRestrict;
	ULONG ulRow = 0;
	ULONG ulNumerator = 0;
	ULONG ulDenominator = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|ll", &res, &restrictionArray, &bkOrigin, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	if (!restrictionArray || zend_hash_num_elements(Z_ARRVAL_P(restrictionArray)) == 0) {
		// reset restriction
		lpRestrict.reset();
	} else {
		// create restrict array
		MAPI_G(hr) = PHPArraytoSRestriction(restrictionArray, NULL, &~lpRestrict TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess) {
			kphperr("Failed to convert the PHP srestriction array", MAPI_G(hr));
			goto exit;
		}
	}

	MAPI_G(hr) = lpTable->FindRow(lpRestrict, bkOrigin, ulFlags);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = lpTable->QueryPosition(&ulRow, &ulNumerator, &ulDenominator);
	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_LONG(ulRow);

exit:
	DEFERRED_EPILOGUE;
}

/**
 * mapi_table_createbookmark
 * Execute create bookmark on a table
 * @param Resource MAPITable
 * @return long bookmark
 */
ZEND_FUNCTION(mapi_table_createbookmark)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res	= NULL;
	LPMAPITABLE	lpTable = NULL;
	// return
	long lbookmark = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	MAPI_G(hr) = lpTable->CreateBookmark((BOOKMARK*)&lbookmark);

	if (FAILED(MAPI_G(hr))) {
		kphperr("Create bookmark failed", MAPI_G(hr));
		goto exit;
	}

	RETVAL_LONG(lbookmark);
	
exit:
	DEFERRED_EPILOGUE;
}

/**
 * mapi_table_freebookmark
 * Execute free bookmark on a table
 * @param Resource MAPITable
 * @param long bookmark
 * @return true/false
 */
ZEND_FUNCTION(mapi_table_freebookmark)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res	= NULL;
	LPMAPITABLE	lpTable = NULL;
	long lbookmark = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &res, &lbookmark) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpTable, LPMAPITABLE, &res, -1, name_mapi_table, le_mapi_table);

	MAPI_G(hr) = lpTable->FreeBookmark((BOOKMARK)lbookmark);

	if (FAILED(MAPI_G(hr))) {
		kphperr("Free bookmark failed", MAPI_G(hr));
		goto exit;
	}

	RETVAL_TRUE;	
exit:
	DEFERRED_EPILOGUE;
}
/**
* mapi_msgstore_getreceivefolder
*
* @param Resource MAPIMSGStore
* @return MAPIFolder
*/
ZEND_FUNCTION(mapi_msgstore_getreceivefolder)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res;
	LPMDB			pMDB		= NULL;
	// return value
	object_ptr<IMAPIFolder> lpFolder;
	// locals
	ULONG			cbEntryID	= 0;
	memory_ptr<ENTRYID> lpEntryID;
	ULONG			ulObjType	= 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMDB, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);

	MAPI_G(hr) = pMDB->GetReceiveFolder(NULL, 0, &cbEntryID, &~lpEntryID, NULL);
	if(FAILED(MAPI_G(hr)))
		goto exit;
	MAPI_G(hr) = pMDB->OpenEntry(cbEntryID, lpEntryID, &iid_of(lpFolder),
	             MAPI_BEST_ACCESS, &ulObjType, &~lpFolder);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpFolder.release(), le_mapi_folder);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_msgstore_openmultistoretable
*
* @param Resource MAPIMSGStore
* @param Array EntryIdList
* @param Long Flags
* @return MAPITable
*/
ZEND_FUNCTION(mapi_msgstore_openmultistoretable)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res;
	zval			*entryid_array = NULL;
	LPMDB			lpMDB		= NULL;
	long			ulFlags		= 0;
	// return value
	LPMAPITABLE		lpMultiTable = NULL;
	// locals
	object_ptr<IECMultiStoreTable> lpECMST;
	memory_ptr<ENTRYLIST> lpEntryList;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|l", &res, &entryid_array, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMDB, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);

	MAPI_G(hr) = PHPArraytoSBinaryArray(entryid_array, NULL, &~lpEntryList TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Bad message list", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = GetECObject(lpMDB, iid_of(lpECMST), &~lpECMST);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Specified object is not a Kopano object", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = lpECMST->OpenMultiStoreTable(lpEntryList, ulFlags, &lpMultiTable);
	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpMultiTable, le_mapi_table);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_message_modifyrecipients
* @param resource
* @param flags
* @param array
*/
ZEND_FUNCTION(mapi_message_modifyrecipients)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res, *adrlist;
	LPMESSAGE		pMessage = NULL;
	long			flags = MODRECIP_ADD;		// flags to use, default to ADD
	// local
	adrlist_ptr lpListRecipients;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rla", &res, &flags, &adrlist) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMessage, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = PHPArraytoAdrList(adrlist, nullptr, &~lpListRecipients TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to parse recipient list", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = pMessage->ModifyRecipients(flags, lpListRecipients);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_message_submitmessage
*
*
*/
ZEND_FUNCTION(mapi_message_submitmessage)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval * res;
	LPMESSAGE		pMessage	= NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMessage, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = pMessage->SubmitMessage(0);
	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_message_getattachmenttable
* @return MAPITable
*
*/
ZEND_FUNCTION(mapi_message_getattachmenttable)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval * res = NULL;
	LPMESSAGE	pMessage	= NULL;
	// return value
	LPMAPITABLE	pTable		= NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMessage, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = pMessage->GetAttachmentTable(0, &pTable);

	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, pTable, le_mapi_table);
exit:
	DEFERRED_EPILOGUE;
}

/**
* Opens a attachment
* @param Message resource
* @param Attachment number
* @return Attachment resource
*/
ZEND_FUNCTION(mapi_message_openattach)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res		= NULL;
	LPMESSAGE	pMessage	= NULL;
	long		attach_num	= 0;
	// return value
	LPATTACH	pAttach		= NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &res, &attach_num) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMessage, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = pMessage->OpenAttach(attach_num, NULL, MAPI_BEST_ACCESS, &pAttach);

	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, pAttach, le_mapi_attachment);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_message_createattach)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*zvalMessage = NULL;
	LPMESSAGE	lpMessage = NULL;
	long		ulFlags = 0;
	// return value
	LPATTACH	lpAttach = NULL;
	// local
	ULONG		attachNum = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &zvalMessage, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMessage, LPMESSAGE, &zvalMessage, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = lpMessage->CreateAttach(NULL, ulFlags, &attachNum, &lpAttach);

	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpAttach, le_mapi_attachment);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_message_deleteattach)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*zvalMessage = NULL;
	LPMESSAGE	lpMessage = NULL;
	long		ulFlags = 0, attachNum = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl|l", &zvalMessage, &attachNum, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMessage, LPMESSAGE, &zvalMessage, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = lpMessage->DeleteAttach(attachNum, 0, NULL, ulFlags);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_stream_read)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res	= NULL;
	LPSTREAM	pStream	= NULL;
	unsigned long		lgetBytes = 0;
	// return value
	std::unique_ptr<char[]> buf;
	ULONG		actualRead = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &res, &lgetBytes) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pStream, LPSTREAM, &res, -1, name_istream, le_istream);

	buf.reset(new char[lgetBytes]);
	MAPI_G(hr) = pStream->Read(buf.get(), lgetBytes, &actualRead);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	RETVAL_STRINGL(buf.get(), actualRead, 1);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_stream_seek)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res = NULL;
	LPSTREAM	pStream = NULL;
	long		moveBytes = 0, seekFlag = STREAM_SEEK_CUR;
	// local
	LARGE_INTEGER	move;
	ULARGE_INTEGER	newPos;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl|l", &res, &moveBytes, &seekFlag) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pStream, LPSTREAM, &res, -1, name_istream, le_istream);

	move.QuadPart = moveBytes;
	MAPI_G(hr) = pStream->Seek(move, seekFlag, &newPos);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_stream_setsize)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res = NULL;
	LPSTREAM	pStream = NULL;
	long		newSize = 0;
	// local
	ULARGE_INTEGER libNewSize = { { 0 } };

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &res, &newSize) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pStream, LPSTREAM, &res, -1, name_istream, le_istream);

	libNewSize.QuadPart = newSize;

	MAPI_G(hr) = pStream->SetSize(libNewSize);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_stream_commit)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res = NULL;
	LPSTREAM	pStream = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pStream, LPSTREAM, &res, -1, name_istream, le_istream);

	MAPI_G(hr) = pStream->Commit(0);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_stream_write)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res = NULL;
	LPSTREAM	pStream = NULL;
	char		*pv = NULL;
	php_stringsize_t cb = 0;
	// return value
	ULONG		pcbWritten = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &pv, &cb) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pStream, LPSTREAM, &res, -1, name_istream, le_istream);

	MAPI_G(hr) = pStream->Write(pv, cb, &pcbWritten);

	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_LONG(pcbWritten);
exit:
	DEFERRED_EPILOGUE;
}

// FIXME: Add more output in the array
ZEND_FUNCTION(mapi_stream_stat)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res = NULL;
	LPSTREAM	pStream = NULL;
	// return value
	ULONG		cb = 0;
	// local
	STATSTG		stg = { 0 };

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pStream, LPSTREAM, &res, -1, name_istream, le_istream);

	MAPI_G(hr) = pStream->Stat(&stg,STATFLAG_NONAME);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	cb = stg.cbSize.LowPart;

	array_init(return_value);
	add_assoc_long(return_value, "cb", cb);

exit:
	DEFERRED_EPILOGUE;
}

/* Create a new in-memory IStream interface. Useful only to write stuff to and then
 * read it back again. Kind of defeats the purpose of the memory usage limits in PHP though ..
 */
ZEND_FUNCTION(mapi_stream_create)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	object_ptr<ECMemStream> lpStream;
	IStream *lpIStream =  NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	MAPI_G(hr) = ECMemStream::Create(nullptr, 0, STGM_WRITE | STGM_SHARE_EXCLUSIVE, nullptr, nullptr, nullptr, &~lpStream);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to instantiate new stream object", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = lpStream->QueryInterface(IID_IStream, (void**)&lpIStream);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	ZEND_REGISTER_RESOURCE(return_value, lpIStream, le_istream);

exit:
	DEFERRED_EPILOGUE;
}

/*
	Opens property to a stream

	THIS FUNCTION IS DEPRECATED. USE mapi_openproperty() INSTEAD

*/

ZEND_FUNCTION(mapi_openpropertytostream)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res		= NULL;
	LPMAPIPROP	lpMapiProp	= NULL;
	long		proptag		= 0, flags = 0; // open default readable
	char		*guidStr	= NULL; // guid is given as a char array
	php_stringsize_t guidLen = 0;
	// return value
	LPSTREAM	pStream		= NULL;
	// local
	LPGUID		lpGuid;			// pointer to string param or static guid
	int			type = -1;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	php_error_docref("mapi_openpropertytostream" TSRMLS_CC, E_DEPRECATED, "Use of mapi_openpropertytostream is deprecated, use mapi_openproperty");

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl|ls", &res, &proptag, &flags, &guidStr, &guidLen) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown resource type");
		goto exit;
	}

	if (guidStr == NULL) {
		// when no guidstring is provided default to IStream
		lpGuid = (LPGUID)&IID_IStream;
	} else if (guidLen == sizeof(GUID)) { // assume we have a guid if the length is right
		lpGuid = (LPGUID)guidStr;
	} else {
		php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Using the default GUID because the given GUIDs length is not right");
		lpGuid = (LPGUID)&IID_IStream;
	}

	MAPI_G(hr) = lpMapiProp->OpenProperty(proptag, lpGuid, 0, flags, (LPUNKNOWN *) &pStream);

	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	ZEND_REGISTER_RESOURCE(return_value, pStream, le_istream);
exit:
	DEFERRED_EPILOGUE;
}

/**
* mapi_message_getreceipenttable
* @return MAPI Table
*
*/
ZEND_FUNCTION(mapi_message_getrecipienttable)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res;
	LPMESSAGE		pMessage = NULL;
	// return value
	LPMAPITABLE		pTable = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMessage, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = pMessage->GetRecipientTable(0, &pTable);

	if (FAILED(MAPI_G(hr)))
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, pTable, le_mapi_table);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_message_setreadflag)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
	LPMESSAGE	pMessage	= NULL;
	long		flag		= 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &res, &flag) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMessage, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = pMessage->SetReadFlag(flag);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

/**
* Read the data from an attachment.
* @param Attachment resource
* @return Message
*/
ZEND_FUNCTION(mapi_attach_openobj)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res	= NULL;
	LPATTACH	pAttach	= NULL;
	long		ulFlags = 0;
	// return value
	LPMESSAGE	lpMessage = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &res, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pAttach, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);

	MAPI_G(hr) = pAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, ulFlags, (LPUNKNOWN *) &lpMessage);

	if (FAILED(MAPI_G(hr)))
		kphperr("Fetching attachmentdata as object failed", MAPI_G(hr));
	else
		ZEND_REGISTER_RESOURCE(return_value, lpMessage, le_mapi_message);

	DEFERRED_EPILOGUE;
}

/**
* Function to get a Property ID from the name. The function expects an array
* with the property names or IDs.
*
*
*/
ZEND_FUNCTION(mapi_getidsfromnames)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval	*messageStore	= NULL;
	LPMDB	lpMessageStore	= NULL;
	zval	*propNameArray	= NULL;
	zval	*guidArray		= NULL;
	// return value
	memory_ptr<SPropTagArray> lpPropTagArray;
	memory_ptr<MAPINAMEID *> lppNamePropId;
	zval		**entry = NULL, **guidEntry = NULL;
	HashTable	*targetHash	= NULL,	*guidHash = NULL;
	GUID guidOutlook = { 0x00062002, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
	int multibytebufferlen = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|a", &messageStore, &propNameArray, &guidArray) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMessageStore, LPMDB, &messageStore, -1, name_mapi_msgstore, le_mapi_msgstore);

	targetHash	= Z_ARRVAL_P(propNameArray);

	if(guidArray)
		guidHash = Z_ARRVAL_P(guidArray);

	// get the number of items in the array
	auto hashTotal = zend_hash_num_elements(targetHash);
	if (guidHash && hashTotal != zend_hash_num_elements(guidHash))
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "The array with the guids is not of the same size as the array with the ids");

	// allocate memory to use
	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(LPMAPINAMEID) * hashTotal, &~lppNamePropId);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	HashPosition thpos, ghpos;
	zend_hash_internal_pointer_reset_ex(targetHash, &thpos);
	if(guidHash)
		zend_hash_internal_pointer_reset_ex(guidHash, &ghpos);
	for (unsigned int i = 0; i < hashTotal; ++i, zend_hash_move_forward_ex(targetHash, &thpos),
	     (guidHash != nullptr ? zend_hash_move_forward_ex(guidHash, &ghpos) : 0)) {
		zend_hash_get_current_data_ex(targetHash, reinterpret_cast<void **>(&entry), &thpos);
		if(guidHash)
			zend_hash_get_current_data_ex(guidHash, reinterpret_cast<void **>(&guidEntry), &ghpos);
		MAPI_G(hr) = MAPIAllocateMore(sizeof(MAPINAMEID),lppNamePropId,(void **) &lppNamePropId[i]);
		if (MAPI_G(hr) != hrSuccess)
			goto exit;

		// fall back to a default GUID if the passed one is not good ..
		lppNamePropId[i]->lpguid = &guidOutlook;

		if(guidHash) {
			if (guidEntry[0]->type != IS_STRING || sizeof(GUID) != guidEntry[0]->value.str.len) {
				php_error_docref(nullptr TSRMLS_CC, E_WARNING, "The GUID with index number %u that is passed is not of the right length, cannot convert to GUID", i);
			} else {
				MAPI_G(hr) = KAllocCopy(guidEntry[0]->value.str.val, sizeof(GUID), reinterpret_cast<void **>(&lppNamePropId[i]->lpguid), lppNamePropId);
				if (MAPI_G(hr) != hrSuccess)
					goto exit;
			}
		}

		switch(entry[0]->type)
		{
		case IS_LONG:
			lppNamePropId[i]->ulKind = MNID_ID;
			lppNamePropId[i]->Kind.lID = entry[0]->value.lval;
			break;
		case IS_STRING:
			multibytebufferlen = mbstowcs(NULL, entry[0]->value.str.val, 0);
			MAPI_G(hr) = MAPIAllocateMore((multibytebufferlen + 1) * sizeof(WCHAR), lppNamePropId,
				  (void **)&lppNamePropId[i]->Kind.lpwstrName);
			if (MAPI_G(hr) != hrSuccess)
				goto exit;
			mbstowcs(lppNamePropId[i]->Kind.lpwstrName, entry[0]->value.str.val, multibytebufferlen + 1);
			lppNamePropId[i]->ulKind = MNID_STRING;
			break;
		case IS_DOUBLE:
			lppNamePropId[i]->ulKind = MNID_ID;
			lppNamePropId[i]->Kind.lID = (LONG) entry[0]->value.dval;
			break;
		default:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Entry is of an unknown type: %08X", entry[0]->type);
			break;
		}
	}

	MAPI_G(hr) = lpMessageStore->GetIDsFromNames(hashTotal, lppNamePropId, MAPI_CREATE, &~lpPropTagArray);
	if (FAILED(MAPI_G(hr))) {
		kphperr("GetIDsFromNames failed", MAPI_G(hr));
		goto exit;
	} else {
		array_init(return_value);
		for (unsigned int i = 0; i < lpPropTagArray->cValues; ++i)
			add_next_index_long(return_value, (LONG)lpPropTagArray->aulPropTag[i]);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_setprops)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL, *propValueArray = NULL;
	LPMAPIPROP lpMapiProp	= NULL;
	// local
	int type = -1;
	ULONG	cValues = 0;
	memory_ptr<SPropValue> pPropValueArray;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &res, &propValueArray) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else if (type == le_mapi_property) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIPROP, &res, -1, name_mapi_property, le_mapi_property);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown resource type");
		goto exit;
	}

	MAPI_G(hr) = PHPArraytoPropValueArray(propValueArray, NULL, &cValues, &~pPropValueArray TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to convert PHP property to MAPI", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = lpMapiProp->SetProps(cValues, pPropValueArray, NULL);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_copyto)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	memory_ptr<SPropTagArray> lpExcludeProps;
	LPMAPIPROP lpSrcObj = NULL;
	LPVOID lpDstObj = NULL;
	LPCIID lpInterface = NULL;
	memory_ptr<IID> lpExcludeIIDs;
	ULONG cExcludeIIDs = 0;

	// params
	zval *srcres = NULL;
	zval *dstres = NULL;
	zval *excludeprops = NULL;
	zval *excludeiid = NULL;
	long flags = 0;

	// local
	int type = -1;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "raar|l", &srcres, &excludeiid, &excludeprops, &dstres, &flags) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(srcres), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpSrcObj, LPMESSAGE, &srcres, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpSrcObj, LPMAPIFOLDER, &srcres, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpSrcObj, LPATTACH, &srcres, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpSrcObj, LPMDB, &srcres, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown resource type");
		goto exit;
	}

	MAPI_G(hr) = PHPArraytoGUIDArray(excludeiid, NULL, &cExcludeIIDs, &~lpExcludeIIDs TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to parse IIDs", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = PHPArraytoPropTagArray(excludeprops, NULL, &~lpExcludeProps TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to parse property tag array", MAPI_G(hr));
		goto exit;
	}

	zend_list_find(Z_RESVAL_P(dstres), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpDstObj, LPMESSAGE, &dstres, -1, name_mapi_message, le_mapi_message);
		lpInterface = &IID_IMessage;
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpDstObj, LPMAPIFOLDER, &dstres, -1, name_mapi_folder, le_mapi_folder);
		lpInterface = &IID_IMAPIFolder;
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpDstObj, LPATTACH, &dstres, -1, name_mapi_attachment, le_mapi_attachment);
		lpInterface = &IID_IAttachment;
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpDstObj, LPMDB, &dstres, -1, name_mapi_msgstore, le_mapi_msgstore);
		lpInterface = &IID_IMsgStore;
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown resource type");
		goto exit;
	}

	MAPI_G(hr) = lpSrcObj->CopyTo(cExcludeIIDs, lpExcludeIIDs, lpExcludeProps, 0, NULL, lpInterface, lpDstObj, flags, NULL);

	if (FAILED(MAPI_G(hr)))
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_savechanges)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res = NULL;
	LPMAPIPROP	lpMapiProp = NULL;
	long		flags = KEEP_OPEN_READWRITE;
	// local
	int type = -1;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &res, &flags) == FAILURE) return;

	if (res->type != IS_RESOURCE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unsupported case !IS_RESOURCE.");
		goto exit;
	}
	zend_list_find(Z_RESVAL_P(res), &type);
	if (type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else if (type == le_mapi_property) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIPROP, &res, -1, name_mapi_property, le_mapi_property);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Resource does not exist...");
		goto exit;
	}

	MAPI_G(hr) = lpMapiProp->SaveChanges(flags);

	if (FAILED(MAPI_G(hr)))
		kphperr("Failed to save object", MAPI_G(hr));
	else
		RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_deleteprops)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res = NULL, *propTagArray = NULL;
	LPMAPIPROP	lpMapiProp = NULL;
	memory_ptr<SPropTagArray> lpTagArray;
	// local
	int type = -1;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &res, &propTagArray) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Resource does not exist...");
		goto exit;
	}

	MAPI_G(hr) = PHPArraytoPropTagArray(propTagArray, NULL, &~lpTagArray TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Failed to convert the PHP array", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = lpMapiProp->DeleteProps(lpTagArray, NULL);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_openproperty)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res		= NULL;
	LPMAPIPROP	lpMapiProp	= NULL;
	long		proptag		= 0, flags = 0, interfaceflags = 0; // open default readable
	char		*guidStr	= NULL; // guid is given as a char array
	php_stringsize_t guidLen = 0;
	// return value
	IUnknown*	lpUnk		= NULL;
	// local
	LPGUID		lpGUID		= NULL;			// pointer to string param or static guid
	int			type 		= -1;
	bool		bBackwardCompatible = false;
	object_ptr<IStream> lpStream;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(ZEND_NUM_ARGS() == 2) {
		// BACKWARD COMPATIBILITY MODE.. this means that we just read the entire stream and return it as a string
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &res, &proptag) == FAILURE) return;

		bBackwardCompatible = true;

		guidStr = (char *)&IID_IStream;
		guidLen = sizeof(GUID);
		interfaceflags = 0;
		flags = 0;
	} else if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rlsll", &res, &proptag, &guidStr, &guidLen, &interfaceflags, &flags) == FAILURE) {
		return;
	}

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else {
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Resource is not a valid MAPI resource");
		goto exit;
	}

	if (guidLen != sizeof(GUID)) {
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified interface is not a valid interface identifier (wrong size)");
		goto exit;
	}

	lpGUID = (LPGUID) guidStr;

	MAPI_G(hr) = lpMapiProp->OpenProperty(proptag, lpGUID, interfaceflags, flags, (LPUNKNOWN *) &lpUnk);

	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	if (*lpGUID == IID_IStream) {
		if(bBackwardCompatible) {
			STATSTG stat;
			char *data = NULL;
			ULONG cRead;

			// do not use queryinterface, since we don't return the stream, but the contents
			lpStream.reset(reinterpret_cast<IStream *>(lpUnk), false);
			MAPI_G(hr) = lpStream->Stat(&stat, STATFLAG_NONAME);
			if(MAPI_G(hr) != hrSuccess)
				goto exit;

			// Use emalloc so that it can be returned directly to PHP without copying
			data = (char *)emalloc(stat.cbSize.LowPart);
			if (data == NULL) {
				kphperr("Unable to allocate memory", MAPI_G(hr));
				MAPI_G(hr) = MAPI_E_NOT_ENOUGH_MEMORY;
				goto exit;
			}

			MAPI_G(hr) = lpStream->Read(data, (ULONG)stat.cbSize.LowPart, &cRead);
			if(MAPI_G(hr)) {
				kphperr("Unable to read the data", MAPI_G(hr));
				goto exit;
			}

			RETVAL_STRINGL(data, cRead, 0);
		} else {
			ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_istream);
		}
	} else if(*lpGUID == IID_IMAPITable) {
		ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_mapi_table);
	} else if(*lpGUID == IID_IMessage) {
		ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_mapi_message);
	} else if(*lpGUID == IID_IMAPIFolder) {
		ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_mapi_folder);
	} else if(*lpGUID == IID_IMsgStore) {
		ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_mapi_msgstore);
	} else if(*lpGUID == IID_IExchangeModifyTable) {
		ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_mapi_modifytable);
	} else if(*lpGUID == IID_IExchangeExportChanges) {
		ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_mapi_exportchanges);
	} else if(*lpGUID == IID_IExchangeImportHierarchyChanges) {
		ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_mapi_importhierarchychanges);
	} else if(*lpGUID == IID_IExchangeImportContentsChanges) {
		ZEND_REGISTER_RESOURCE(return_value, lpUnk, le_mapi_importcontentschanges);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "The openproperty call succeeded, but the PHP extension is unable to handle the requested interface");
		lpUnk->Release();
		MAPI_G(hr) = MAPI_E_NO_SUPPORT;
		goto exit;
	}

exit:
	DEFERRED_EPILOGUE;
}

/*
Function that get a resource and check what type it is, when the type is a subclass from IMAPIProp
it will use it to do a getProps.
*/
ZEND_FUNCTION(mapi_getprops)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	LPMAPIPROP lpMapiProp = NULL;
	zval *res = NULL, *tagArray = NULL;
	ULONG cValues = 0;
	memory_ptr<SPropValue> lpPropValues;
	memory_ptr<SPropTagArray> lpTagArray;
	// return value
	zval *zval_prop_value = NULL;
	// local
	int type = -1; // list entry number of the resource.

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|a", &res, &tagArray) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else if( type == le_mapi_mailuser) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAILUSER, &res, -1, name_mapi_mailuser, le_mapi_mailuser);
	} else if( type == le_mapi_distlist) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPDISTLIST, &res, -1, name_mapi_distlist, le_mapi_distlist);
	} else if( type == le_mapi_abcont) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPABCONT, &res, -1, name_mapi_abcont, le_mapi_abcont);
	} else if( type == le_mapi_property) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIPROP, &res, -1, name_mapi_property, le_mapi_property);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Resource is not a valid MAPI resource");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if(tagArray) {
		MAPI_G(hr) = PHPArraytoPropTagArray(tagArray, NULL, &~lpTagArray TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess) {
			kphperr("Unable to parse property tag array", MAPI_G(hr));
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
	} else {
		lpTagArray = NULL;
	}

	MAPI_G(hr) = lpMapiProp->GetProps(lpTagArray, 0, &cValues, &~lpPropValues);
	if (FAILED(MAPI_G(hr)))
		goto exit;
	MAPI_G(hr) = spv_postload_large_props(lpMapiProp, lpTagArray, cValues, lpPropValues);
	if (FAILED(MAPI_G(hr)))
		goto exit;

	MAPI_G(hr) = PropValueArraytoPHPArray(cValues, lpPropValues, &zval_prop_value TSRMLS_CC);

	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to convert properties to PHP values", MAPI_G(hr));
		goto exit;
	}

	RETVAL_ZVAL(zval_prop_value, 0, 0);
	FREE_ZVAL(zval_prop_value);

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_getnamesfromids)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval	*res, *array;
	LPMDB	pMDB = NULL;
	memory_ptr<SPropTagArray> lpPropTags;
	// return value
	// local
	ULONG				cPropNames = 0;
	memory_ptr<MAPINAMEID *> pPropNames;
	ULONG				count = 0;
	zval *prop;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &res, &array) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(pMDB, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);

	MAPI_G(hr) = PHPArraytoPropTagArray(array, NULL, &~lpPropTags TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to convert proptag array from PHP array", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = pMDB->GetNamesFromIDs(&+lpPropTags, NULL, 0, &cPropNames, &~pPropNames);
	if(FAILED(MAPI_G(hr)))
		goto exit;

	// This code should be moved to typeconversions.cpp
	array_init(return_value);
	for (count = 0; count < lpPropTags->cValues; ++count) {
		if (pPropNames[count] == NULL)
			continue;			// FIXME: the returned array is smaller than the passed array!

		char buffer[20];
		snprintf(buffer, 20, "%i", lpPropTags->aulPropTag[count]);

		MAKE_STD_ZVAL(prop);
		array_init(prop);

		add_assoc_stringl(prop, "guid", (char*)pPropNames[count]->lpguid, sizeof(GUID), 1);

		if (pPropNames[count]->ulKind == MNID_ID) {
			add_assoc_long(prop, "id", pPropNames[count]->Kind.lID);
		} else {
			int slen;
			char *buffer;
			slen = wcstombs(NULL, pPropNames[count]->Kind.lpwstrName, 0);	// find string size
			++slen;															// add terminator
			buffer = new char[slen];										// alloc
			wcstombs(buffer, pPropNames[count]->Kind.lpwstrName, slen);		// convert & terminate
			add_assoc_string(prop, "name", buffer, 1);
			delete [] buffer;
		}

		add_assoc_zval(return_value, buffer, prop);
	}

exit:
	DEFERRED_EPILOGUE;
}

/**
* Receives a string of compressed RTF. First turn this string into a Stream and than
* decompres.
*
*/
ZEND_FUNCTION(mapi_decompressrtf)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	char * rtfBuffer = NULL;
	php_stringsize_t rtfBufferLen = 0, bufsize = 10240;
	// return value
	std::unique_ptr<char[]> htmlbuf;
	// local
	ULONG actualWritten = 0;
	ULONG cbRead = 0;
	object_ptr<IStream> pStream, deCompressedStream;
	LARGE_INTEGER begin = { { 0, 0 } };
	std::string strUncompressed;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &rtfBuffer, &rtfBufferLen) == FAILURE) return;

	// make and fill the stream
	MAPI_G(hr) = CreateStreamOnHGlobal(nullptr, true, &~pStream);
	if (MAPI_G(hr) != hrSuccess || pStream == NULL) {
		kphperr("Unable to CreateStreamOnHGlobal", MAPI_G(hr));
		goto exit;
	}

	pStream->Write(rtfBuffer, rtfBufferLen, &actualWritten);
	pStream->Commit(0);
	pStream->Seek(begin, SEEK_SET, NULL);
	MAPI_G(hr) = WrapCompressedRTFStream(pStream, 0, &~deCompressedStream);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to wrap uncompressed stream", MAPI_G(hr));
		goto exit;
	}

	// bufsize is the size of the buffer we've allocated, and htmlsize is the
	// amount of text we've read in so far. If our buffer wasn't big enough,
	// we enlarge it and continue. We have to do this, instead of allocating
	// it up front, because Stream::Stat() doesn't work for the unc.stream
	bufsize = std::max(rtfBufferLen * 2, bufsize);
	htmlbuf.reset(new char[bufsize]);

	while(1) {
		MAPI_G(hr) = deCompressedStream->Read(htmlbuf.get(), bufsize, &cbRead);
		if (MAPI_G(hr) != hrSuccess) {
			kphperr("Read from uncompressed stream failed", MAPI_G(hr));
			goto exit;
		}

		if (cbRead == 0)
		    break;
		strUncompressed.append(htmlbuf.get(), cbRead);
	}

	RETVAL_STRINGL((char *)strUncompressed.c_str(), strUncompressed.size(), 1);

exit:
	DEFERRED_EPILOGUE;
}

/**
*
* Opens the rules table from the default INBOX of the given store
*
*/
ZEND_FUNCTION(mapi_folder_openmodifytable) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res;
	LPMAPIFOLDER lpInbox = NULL;
	// return value
	LPEXCHANGEMODIFYTABLE lpRulesTable = NULL;
	// locals

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpInbox, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = lpInbox->OpenProperty(PR_RULES_TABLE, &IID_IExchangeModifyTable, 0, 0, (LPUNKNOWN *)&lpRulesTable);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpRulesTable, le_mapi_modifytable);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_folder_getsearchcriteria) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
	zval *restriction = NULL;
	zval *folderlist = NULL;
	LPMAPIFOLDER lpFolder = NULL;
	long ulFlags = 0;
	// local
	memory_ptr<SRestriction> lpRestriction;
	memory_ptr<ENTRYLIST> lpFolderList;
	ULONG ulSearchState = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|l", &res, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFolder, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = lpFolder->GetSearchCriteria(ulFlags, &~lpRestriction, &~lpFolderList, &ulSearchState);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = SRestrictiontoPHPArray(lpRestriction, 0, &restriction TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = SBinaryArraytoPHPArray(lpFolderList, &folderlist TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);

	add_assoc_zval(return_value, "restriction", restriction);
	add_assoc_zval(return_value, "folderlist", folderlist);
	add_assoc_long(return_value, "searchstate", ulSearchState);

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_folder_setsearchcriteria) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// param
	zval *res = NULL;
	zval *restriction = NULL;
	zval *folderlist = NULL;
	long ulFlags = 0;
	// local
	LPMAPIFOLDER lpFolder = NULL;
	memory_ptr<ENTRYLIST> lpFolderList;
	memory_ptr<SRestriction> lpRestriction;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "raal", &res, &restriction, &folderlist, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFolder, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);

	MAPI_G(hr) = PHPArraytoSRestriction(restriction, NULL, &~lpRestriction TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = PHPArraytoSBinaryArray(folderlist, NULL, &~lpFolderList TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = lpFolder->SetSearchCriteria(lpRestriction, lpFolderList, ulFlags);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

/**
*
* returns a read-only view on the rules table
*
*/
ZEND_FUNCTION(mapi_rules_gettable) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res;
	LPEXCHANGEMODIFYTABLE lpRulesTable = NULL;
	// return value
	object_ptr<IMAPITable> lpRulesView;
	// locals
	static constexpr const SizedSPropTagArray(11, sptaRules) =
		{11, {PR_RULE_ID, PR_RULE_IDS, PR_RULE_SEQUENCE, PR_RULE_STATE,
		PR_RULE_USER_FLAGS, PR_RULE_CONDITION, PR_RULE_ACTIONS,
		PR_RULE_PROVIDER, PR_RULE_NAME, PR_RULE_LEVEL,
		PR_RULE_PROVIDER_DATA}};
	static constexpr const SizedSSortOrderSet(1, sosRules) =
		{1, 0, 0, {{PR_RULE_SEQUENCE, TABLE_SORT_ASCEND}}};
	ECRulesTableProxy *lpRulesTableProxy = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpRulesTable, LPEXCHANGEMODIFYTABLE, &res, -1, name_mapi_modifytable, le_mapi_modifytable);

	MAPI_G(hr) = lpRulesTable->GetTable(0, &~lpRulesView);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = lpRulesView->SetColumns(sptaRules, 0);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = lpRulesView->SortTable(sosRules, 0);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	
	MAPI_G(hr) = ECRulesTableProxy::Create(lpRulesView, &lpRulesTableProxy);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = lpRulesTableProxy->QueryInterface(IID_IMAPITable, &~lpRulesView);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpRulesView.release(), le_mapi_table);
exit:
	DEFERRED_EPILOGUE;
}

/**
 * Adds, modifies or deletes rows from the rules table
*/
ZEND_FUNCTION(mapi_rules_modifytable) {
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res;
	LPEXCHANGEMODIFYTABLE lpRulesTable = NULL;
	zval *rows;
	LPROWLIST lpRowList = NULL;
	long ulFlags = 0;
	// return value
	// locals

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra|l", &res, &rows, &ulFlags) == FAILURE) return;
	ZEND_FETCH_RESOURCE_C(lpRulesTable, LPEXCHANGEMODIFYTABLE, &res, -1, name_mapi_modifytable, le_mapi_modifytable);

	MAPI_G(hr) = PHPArraytoRowList(rows, NULL, &lpRowList TSRMLS_CC);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to parse rowlist");
		goto exit;
	}

	MAPI_G(hr) = lpRulesTable->ModifyTable(ulFlags, lpRowList);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	if (lpRowList)
		FreeProws((LPSRowSet)lpRowList);
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_createuser)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	char			*lpszUsername = NULL;
	char			*lpszFullname = NULL;
	char			*lpszEmail = NULL;
	char			*lpszPassword = NULL;
	php_stringsize_t ulUsernameLen = 0, ulFullname = 0, ulEmail = 0, ulPassword = 0;
	long			ulIsNonactive = false;
	long			ulIsAdmin = false;

	// return value
	ULONG			cbUserId = 0;
	memory_ptr<ENTRYID> lpUserId;

	// local
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	ECUSER			sUser = { 0 };

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rssss|ll", &res,
							&lpszUsername, &ulUsernameLen,
							&lpszPassword, &ulPassword,
							&lpszFullname, &ulFullname,
							&lpszEmail, &ulEmail,
							&ulIsNonactive, &ulIsAdmin) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}

	//FIXME: Check the values

	sUser.lpszUsername	= (TCHAR*)lpszUsername;
	sUser.lpszMailAddress = (TCHAR*)lpszEmail;
	sUser.lpszPassword	= (TCHAR*)lpszPassword;
	sUser.ulObjClass	= (ulIsNonactive ? NONACTIVE_USER : ACTIVE_USER);
	sUser.lpszFullName	= (TCHAR*)lpszFullname;
	sUser.ulIsAdmin		= ulIsAdmin;
	MAPI_G(hr) = lpServiceAdmin->CreateUser(&sUser, 0, &cbUserId, &~lpUserId);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_setuser)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params

	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	LPENTRYID		lpUserId = NULL;
	char			*lpszUsername = NULL;
	char			*lpszFullname = NULL;
	char			*lpszEmail = NULL;
	char			*lpszPassword = NULL;
	php_stringsize_t cbUserId = 0, ulUsername = 0, ulFullname = 0, ulEmail = 0, ulPassword = 0;
	long			ulIsNonactive = 0;
	long			ulIsAdmin = 0;

	// local
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	ECUSER			sUser;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsssssll", &res, &lpUserId, &cbUserId, &lpszUsername, &ulUsername, &lpszFullname, &ulFullname, &lpszEmail, &ulEmail, &lpszPassword, &ulPassword, &ulIsNonactive, &ulIsAdmin) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}

	//FIXME: Check the values

	memset(&sUser, 0, sizeof(ECUSER));

	sUser.lpszUsername		= (TCHAR*)lpszUsername;
	sUser.lpszPassword		= (TCHAR*)lpszPassword;
	sUser.lpszMailAddress		= (TCHAR*)lpszEmail;
	sUser.lpszFullName		= (TCHAR*)lpszFullname;
	sUser.sUserId.lpb		= (unsigned char*)lpUserId;
	sUser.sUserId.cb		= cbUserId;
	sUser.ulObjClass		= (ulIsNonactive ? NONACTIVE_USER : ACTIVE_USER);
	sUser.ulIsAdmin			= ulIsAdmin;
	// no support for hidden, capacity and propmaps

	MAPI_G(hr) = lpServiceAdmin->SetUser(&sUser, 0);

	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_deleteuser)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	ULONG			cbUserId = 0;
	memory_ptr<ENTRYID> lpUserId;
	char*			lpszUserName = NULL;
	php_stringsize_t ulUserName;

	// local
	object_ptr<IECServiceAdmin> lpServiceAdmin;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpszUserName, &ulUserName) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->ResolveUserName((TCHAR*)lpszUserName, 0, &cbUserId, &~lpUserId);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Cannot resolve/delete user", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = lpServiceAdmin->DeleteUser(cbUserId, lpUserId);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to delete user", MAPI_G(hr));
		goto exit;
	}

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_createstore)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	long			ulStoreType;
	LPENTRYID		lpUserId = NULL;
	php_stringsize_t cbUserId = 0;
	// local
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ENTRYID> lpStoreID, lpRootID;
	ULONG			cbStoreID = 0;
	ULONG			cbRootID = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rls", &res, &ulStoreType, &lpUserId, &cbUserId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->CreateStore(ulStoreType, cbUserId, lpUserId, &cbStoreID, &~lpStoreID, &cbRootID, &~lpRootID);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to modify user", MAPI_G(hr));
		goto exit;
	}

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

/**
* Retrieve a list of users
* @param  logged on msgstore
* @param  company entryid
* @return array(username => array(fullname, emaladdress, userid, admin));
*/
ZEND_FUNCTION(mapi_zarafa_getuserlist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	zval*			zval_data_value = NULL;
	LPMDB			lpMsgStore = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbCompanyId = 0;
	// return value

	// local
	ULONG		nUsers, i;
	memory_ptr<ECUSER> lpUsers;
	object_ptr<IECSecurity> lpSecurity;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|s", &res, &lpCompanyId, &cbCompanyId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpSecurity), &~lpSecurity);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpSecurity->GetUserList(cbCompanyId, lpCompanyId, 0, &nUsers, &~lpUsers);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (i = 0; i < nUsers; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "userid", (char*)lpUsers[i].sUserId.lpb, lpUsers[i].sUserId.cb, 1);
		add_assoc_string(zval_data_value, "username", (char*)lpUsers[i].lpszUsername, 1);
		add_assoc_string(zval_data_value, "fullname", (char*)lpUsers[i].lpszFullName, 1);
		add_assoc_string(zval_data_value, "emailaddress", (char*)lpUsers[i].lpszMailAddress, 1);
		add_assoc_long(zval_data_value, "admin", lpUsers[i].ulIsAdmin);
		add_assoc_long(zval_data_value, "nonactive", (lpUsers[i].ulObjClass == ACTIVE_USER ? 0 : 1));

		add_assoc_zval(return_value, (char*)lpUsers[i].lpszUsername, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

/**
 * Retrieve quota values of a users
 * @param  logged on msgstore
 * @param  user entryid to get quota information from
 * @return array(usedefault, isuserdefault, warnsize, softsize, hardsize);
 */
ZEND_FUNCTION(mapi_zarafa_getquota)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval            *res = NULL;
	LPMDB           lpMsgStore = NULL;
	LPENTRYID		lpUserId = NULL;
	php_stringsize_t cbUserId = 0;
	// return value

	// local
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ECQUOTA> lpQuota;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpUserId, &cbUserId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetQuota(cbUserId, lpUserId, false, &~lpQuota);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	add_assoc_bool(return_value, "usedefault", lpQuota->bUseDefaultQuota);
	add_assoc_bool(return_value, "isuserdefault", lpQuota->bIsUserDefaultQuota);
	add_assoc_long(return_value, "warnsize", lpQuota->llWarnSize);
	add_assoc_long(return_value, "softsize", lpQuota->llSoftSize);
	add_assoc_long(return_value, "hardsize", lpQuota->llHardSize);
       
exit:
	DEFERRED_EPILOGUE;
}

/**
 * Update quota values for a users
 * @param  logged on msgstore
 * @param  userid to get quota information from
 * @param  array(usedefault, isuserdefault, warnsize, softsize, hardsize)
 * @return true/false
 */
ZEND_FUNCTION(mapi_zarafa_setquota)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval            *res = NULL;
	LPMDB           lpMsgStore = NULL;
	LPENTRYID		lpUserId = NULL;
	php_stringsize_t cbUserId = 0;
	zval			*array = NULL;
	// return value

	// local
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ECQUOTA> lpQuota;
	HashTable		*data = NULL;
	zval			**value = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsa", &res, &lpUserId, &cbUserId, &array) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetQuota(cbUserId, lpUserId, false, &~lpQuota);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	data = HASH_OF(array);
	if (zend_hash_find(data, "usedefault", sizeof("usedefault"), reinterpret_cast<void **>(&value)) == SUCCESS)
		lpQuota->bUseDefaultQuota = zval_is_true(*value);
	if (zend_hash_find(data, "isuserdefault", sizeof("isuserdefault"), reinterpret_cast<void **>(&value)) == SUCCESS)
		lpQuota->bIsUserDefaultQuota = zval_is_true(*value);
	if (zend_hash_find(data, "warnsize", sizeof("warnsize"), (void**)&value) == SUCCESS) {
		SEPARATE_ZVAL(value);
		convert_to_long_ex(value);
		lpQuota->llWarnSize = Z_LVAL_PP(value);
	}

	if (zend_hash_find(data, "softsize", sizeof("softsize"), (void**)&value) == SUCCESS) {
		SEPARATE_ZVAL(value);
		convert_to_long_ex(value);
		lpQuota->llSoftSize = Z_LVAL_PP(value);
	}

	if (zend_hash_find(data, "hardsize", sizeof("hardsize"), (void**)&value) == SUCCESS) {
		SEPARATE_ZVAL(value);
		convert_to_long_ex(value);
		lpQuota->llHardSize = Z_LVAL_PP(value);
	}

	MAPI_G(hr) = lpServiceAdmin->SetQuota(cbUserId, lpUserId, lpQuota);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

/**
* Retrieve user information
* @param  logged on msgstore
* @param  username
* @return array(fullname, emailaddress, userid, admin);
*/
ZEND_FUNCTION(mapi_zarafa_getuser_by_name)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval		*res = NULL;
	LPMDB		lpMsgStore = NULL;
	char			*lpszUsername;
	php_stringsize_t ulUsername;
	// return value

	// local
	memory_ptr<ECUSER> lpUsers;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ENTRYID> lpUserId;
	unsigned int	cbUserId = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpszUsername, &ulUsername) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->ResolveUserName((TCHAR*)lpszUsername, 0, (ULONG*)&cbUserId, &~lpUserId);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to resolve user", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetUser(cbUserId, lpUserId, 0, &~lpUsers);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to get user", MAPI_G(hr));
		goto exit;
	}

	array_init(return_value);

	add_assoc_stringl(return_value, "userid", (char*)lpUsers->sUserId.lpb, lpUsers->sUserId.cb, 1);
	add_assoc_string(return_value, "username", (char*)lpUsers->lpszUsername, 1);
	add_assoc_string(return_value, "fullname", (char*)lpUsers->lpszFullName, 1);
	add_assoc_string(return_value, "emailaddress", (char*)lpUsers->lpszMailAddress, 1);
	add_assoc_long(return_value, "admin", lpUsers->ulIsAdmin);

exit:
	DEFERRED_EPILOGUE;
}

/**
* Retrieve user information
* @param  logged on msgstore
* @param  userid
* @return array(fullname, emailaddress, userid, admin);
*/
ZEND_FUNCTION(mapi_zarafa_getuser_by_id)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	LPENTRYID		lpUserId = NULL;
	php_stringsize_t cbUserId = 0;
	// return value

	// local
	memory_ptr<ECUSER> lpUsers;
	object_ptr<IECServiceAdmin> lpServiceAdmin;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpUserId, &cbUserId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetUser(cbUserId, lpUserId, 0, &~lpUsers);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to get user", MAPI_G(hr));
		goto exit;
	}

	array_init(return_value);

	add_assoc_stringl(return_value, "userid", (char*)lpUsers->sUserId.lpb, lpUsers->sUserId.cb, 1);
	add_assoc_string(return_value, "username", (char*)lpUsers->lpszUsername, 1);
	add_assoc_string(return_value, "fullname", (char*)lpUsers->lpszFullName, 1);
	add_assoc_string(return_value, "emailaddress", (char*)lpUsers->lpszMailAddress, 1);
	add_assoc_long(return_value, "admin", lpUsers->ulIsAdmin);

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_creategroup)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	ECGROUP			sGroup;
	php_stringsize_t cbGroupname;
	// return value
	memory_ptr<ENTRYID> lpGroupId;
	unsigned int	cbGroupId = 0;
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &sGroup.lpszGroupname, &cbGroupname) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	sGroup.lpszFullname = sGroup.lpszGroupname;
	MAPI_G(hr) = lpServiceAdmin->CreateGroup(&sGroup, 0, (ULONG*)&cbGroupId, &~lpGroupId);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to create group", MAPI_G(hr));
		goto exit;
	}
	RETVAL_STRINGL(reinterpret_cast<const char *>(lpGroupId.get()), cbGroupId, 1);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_deletegroup)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	char			*lpszGroupname;
	php_stringsize_t cbGroupname;
	// return value
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ENTRYID> lpGroupId;
	unsigned int	cbGroupId = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpszGroupname, &cbGroupname) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->ResolveGroupName((TCHAR*)lpszGroupname, 0, (ULONG*)&cbGroupId, &~lpGroupId);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Group not found", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = lpServiceAdmin->DeleteGroup(cbGroupId, lpGroupId);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_addgroupmember)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval 			*res = NULL;
	LPENTRYID		lpGroupId = NULL;
	LPENTRYID		lpUserId = NULL;
	php_stringsize_t cbGroupId = 0, cbUserId = 0;
	// return value
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore	*lpMsgStore = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", &res, &lpGroupId, &cbGroupId, &lpUserId, &cbUserId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->AddGroupUser(cbGroupId, lpGroupId, cbUserId, lpUserId);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_deletegroupmember)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval 			*res = NULL;
	LPENTRYID		lpGroupId = NULL;
	LPENTRYID		lpUserId = NULL;
	php_stringsize_t cbGroupId = 0, cbUserId = 0;
	// return value
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore	*lpMsgStore = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", &res, &lpGroupId, &cbGroupId, &lpUserId, &cbUserId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->DeleteGroupUser(cbGroupId, lpGroupId, cbUserId, lpUserId);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_setgroup)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	char			*lpszGroupname;
	LPENTRYID		*lpGroupId = NULL;
	php_stringsize_t cbGroupname, cbGroupId = 0;

	// return value
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	ECGROUP			sGroup;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", &res, &lpGroupId, &cbGroupId, &lpszGroupname, &cbGroupname) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	sGroup.sGroupId.cb = cbGroupId;
	sGroup.sGroupId.lpb = (unsigned char*)lpGroupId;
	sGroup.lpszGroupname = (TCHAR*)lpszGroupname;

	MAPI_G(hr) = lpServiceAdmin->SetGroup(&sGroup, 0);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getgroup_by_id)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	LPENTRYID		lpGroupId = NULL;
	php_stringsize_t cbGroupId = 0;
	// return value
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ECGROUP> lpsGroup;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpGroupId, &cbGroupId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetGroup(cbGroupId, lpGroupId, 0, &~lpsGroup);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	add_assoc_stringl(return_value, "groupid", (char*)lpGroupId, cbGroupId, 1);
	add_assoc_string(return_value, "groupname", (char*)lpsGroup->lpszGroupname, 1);

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getgroup_by_name)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	char			*lpszGroupname = NULL;
	php_stringsize_t ulGroupname;
	// locals
	LPMDB lpMsgStore = NULL;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ECGROUP> lpsGroup;
	// return value
	memory_ptr<ENTRYID> lpGroupId;
	unsigned int cbGroupId = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpszGroupname, &ulGroupname) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->ResolveGroupName((TCHAR*)lpszGroupname, 0, (ULONG*)&cbGroupId, &~lpGroupId);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to resolve group", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetGroup(cbGroupId, lpGroupId, 0, &~lpsGroup);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	add_assoc_stringl(return_value, "groupid", reinterpret_cast<char *>(lpGroupId.get()), cbGroupId, 1);
	add_assoc_string(return_value, "groupname", (char*)lpsGroup->lpszGroupname, 1);

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getgrouplist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbCompanyId = 0;
	// return value
	// locals
	zval			*zval_data_value  = NULL;
	LPMDB			lpMsgStore = NULL;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	ULONG			ulGroups;
	memory_ptr<ECGROUP> lpsGroups;
	unsigned int	i;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|s", &res, &lpCompanyId, &cbCompanyId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetGroupList(cbCompanyId, lpCompanyId, 0, &ulGroups, &~lpsGroups);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (i = 0; i < ulGroups; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "groupid", (char*)lpsGroups[i].sGroupId.lpb, lpsGroups[i].sGroupId.cb, 1);
		add_assoc_string(zval_data_value, "groupname", (char*)lpsGroups[i].lpszGroupname, 1);

		add_assoc_zval(return_value, (char*)lpsGroups[i].lpszGroupname, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getgrouplistofuser)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPENTRYID		lpUserId = NULL;
	php_stringsize_t cbUserId = 0;
	// return value
	// locals
	zval			*zval_data_value  = NULL;
	LPMDB			lpMsgStore = NULL;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	ULONG			ulGroups;
	memory_ptr<ECGROUP> lpsGroups;
	unsigned int	i;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpUserId, &cbUserId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetGroupListOfUser(cbUserId, lpUserId, 0, &ulGroups, &~lpsGroups);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (i = 0; i < ulGroups; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "groupid", (char*)lpsGroups[i].sGroupId.lpb, lpsGroups[i].sGroupId.cb, 1);
		add_assoc_string(zval_data_value, "groupname", (char*)lpsGroups[i].lpszGroupname, 1);

		add_assoc_zval(return_value, (char*)lpsGroups[i].lpszGroupname, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getuserlistofgroup)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPENTRYID		lpGroupId = NULL;
	php_stringsize_t cbGroupId = 0;
	// return value
	// locals
	zval			*zval_data_value  = NULL;
	LPMDB			lpMsgStore = NULL;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	ULONG			ulUsers;
	memory_ptr<ECUSER> lpsUsers;
	unsigned int	i;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpGroupId, &cbGroupId) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetUserListOfGroup(cbGroupId, lpGroupId, 0, &ulUsers, &~lpsUsers);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (i = 0; i < ulUsers; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "userid", (char*)lpsUsers[i].sUserId.lpb, lpsUsers[i].sUserId.cb, 1);
		add_assoc_string(zval_data_value, "username", (char*)lpsUsers[i].lpszUsername, 1);
		add_assoc_string(zval_data_value, "fullname", (char*)lpsUsers[i].lpszFullName, 1);
		add_assoc_string(zval_data_value, "emailaddress", (char*)lpsUsers[i].lpszMailAddress, 1);
		add_assoc_long(zval_data_value, "admin", lpsUsers[i].ulIsAdmin);

		add_assoc_zval(return_value, (char*)lpsUsers[i].lpszUsername, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_createcompany)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
	LPMDB lpMsgStore = NULL;
	ECCOMPANY sCompany;
	php_stringsize_t cbCompanyname;
	// return value
	memory_ptr<ENTRYID> lpCompanyId;
	unsigned int	cbCompanyId = 0;
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &sCompany.lpszCompanyname, &cbCompanyname) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->CreateCompany(&sCompany, 0, (ULONG*)&cbCompanyId, &~lpCompanyId);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to create company", MAPI_G(hr));
		goto exit;
	}
	RETVAL_STRINGL(reinterpret_cast<const char *>(lpCompanyId.get()), cbCompanyId, 1);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_deletecompany)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
	LPMDB lpMsgStore = NULL;
	char *lpszCompanyname;
	php_stringsize_t cbCompanyname = 0;
	// return value
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ENTRYID> lpCompanyId;
	unsigned int	cbCompanyId = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpszCompanyname, &cbCompanyname) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		 php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		 goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->ResolveCompanyName((TCHAR*)lpszCompanyname, 0, (ULONG*)&cbCompanyId, &~lpCompanyId);
	if (MAPI_G(hr) != hrSuccess) {
		kphperr("Company not found", MAPI_G(hr));
		goto exit;
	}

	MAPI_G(hr) = lpServiceAdmin->DeleteCompany(cbCompanyId, lpCompanyId);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getcompany_by_id)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval			*res = NULL;
	LPMDB			lpMsgStore = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbCompanyId = 0;
	// return value
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ECCOMPANY> lpsCompany;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpCompanyId, &cbCompanyId) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetCompany(cbCompanyId, lpCompanyId, 0, &~lpsCompany);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	add_assoc_stringl(return_value, "companyid", (char*)lpCompanyId, cbCompanyId, 1);
	add_assoc_string(return_value, "companyname", (char*)lpsCompany->lpszCompanyname, 1);

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getcompany_by_name)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
	LPMDB lpMsgStore = NULL;
	char *lpszCompanyname = NULL;
	php_stringsize_t ulCompanyname;
	memory_ptr<ENTRYID> lpCompanyId;
	unsigned int cbCompanyId = 0;
	// return value
	// locals
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ECCOMPANY> lpsCompany;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpszCompanyname, &ulCompanyname) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->ResolveCompanyName((TCHAR*)lpszCompanyname, 0, (ULONG*)&cbCompanyId, &~lpCompanyId);
	if(MAPI_G(hr) != hrSuccess) {
		kphperr("Unable to resolve company", MAPI_G(hr));
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetCompany(cbCompanyId, lpCompanyId, 0, &~lpsCompany);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	add_assoc_stringl(return_value, "companyid", reinterpret_cast<char *>(lpCompanyId.get()), cbCompanyId, 1);
	add_assoc_string(return_value, "companyname", (char*)lpsCompany->lpszCompanyname, 1);

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getcompanylist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
 	zval *zval_data_value = NULL;
	LPMDB lpMsgStore = NULL;
	// return value
	// local
	ULONG nCompanies, i;
	memory_ptr<ECCOMPANY> lpCompanies;
	object_ptr<IECSecurity> lpSecurity;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpSecurity), &~lpSecurity);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpSecurity->GetCompanyList(0, &nCompanies, &~lpCompanies);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (i = 0; i < nCompanies; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "companyid", (char*)lpCompanies[i].sCompanyId.lpb, lpCompanies[i].sCompanyId.cb, 1);
		add_assoc_string(zval_data_value, "companyname", (char*)lpCompanies[i].lpszCompanyname, 1);
		add_assoc_zval(return_value, (char*)lpCompanies[i].lpszCompanyname, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_add_company_remote_viewlist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpSetCompanyId = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbSetCompanyId = 0, cbCompanyId = 0;

	/* Locals */
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", &res, &lpSetCompanyId, &cbSetCompanyId, &lpCompanyId, &cbCompanyId) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->AddCompanyToRemoteViewList(cbSetCompanyId, lpSetCompanyId, cbCompanyId, lpCompanyId);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_del_company_remote_viewlist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpSetCompanyId = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbSetCompanyId = 0, cbCompanyId = 0;

	/* Locals */
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", &res, &lpSetCompanyId, &cbSetCompanyId, &lpCompanyId, &cbCompanyId) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->DelCompanyFromRemoteViewList(cbSetCompanyId, lpSetCompanyId, cbCompanyId, lpCompanyId);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_get_remote_viewlist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbCompanyId = 0;

	/* Locals */
	zval			*zval_data_value  = NULL;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;
	ULONG			ulCompanies = 0;
	memory_ptr<ECCOMPANY> lpsCompanies;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpCompanyId, &cbCompanyId) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetRemoteViewList(cbCompanyId, lpCompanyId, 0, &ulCompanies, &~lpsCompanies);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (unsigned int i = 0; i < ulCompanies; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "companyid", (char*)lpsCompanies[i].sCompanyId.lpb, lpsCompanies[i].sCompanyId.cb, 1);
		add_assoc_string(zval_data_value, "companyname", (char*)lpsCompanies[i].lpszCompanyname, 1);
		add_assoc_zval(return_value, (char*)lpsCompanies[i].lpszCompanyname, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_add_user_remote_adminlist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpUserId = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbUserId = 0, cbCompanyId = 0;

	/* Locals */
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", &res, &lpUserId, &cbUserId, &lpCompanyId, &cbCompanyId) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->AddUserToRemoteAdminList(cbUserId, lpUserId, cbCompanyId, lpCompanyId);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_del_user_remote_adminlist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpUserId = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbUserId = 0, cbCompanyId = 0;

	/* Locals */
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rss", &res, &lpUserId, &cbUserId, &lpCompanyId, &cbCompanyId) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->DelUserFromRemoteAdminList(cbUserId, lpUserId, cbCompanyId, lpCompanyId);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_get_remote_adminlist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbCompanyId = 0;

	/* Locals */
	zval			*zval_data_value  = NULL;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;
	ULONG			ulUsers = 0;
	memory_ptr<ECUSER> lpsUsers;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpCompanyId, &cbCompanyId) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetRemoteAdminList(cbCompanyId, lpCompanyId, 0, &ulUsers, &~lpsUsers);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (unsigned int i = 0; i < ulUsers; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "userid", (char*)lpsUsers[i].sUserId.lpb, lpsUsers[i].sUserId.cb, 1);
		add_assoc_string(zval_data_value, "username", (char*)lpsUsers[i].lpszUsername, 1);
		add_assoc_zval(return_value, (char*)lpsUsers[i].lpszUsername, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_add_quota_recipient)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpRecipientId = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbRecipientId = 0, cbCompanyId = 0;
	long			ulType = 0;

	/* Locals */
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rssl", &res, &lpCompanyId, &cbCompanyId, &lpRecipientId, &cbRecipientId, &ulType) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->AddQuotaRecipient(cbCompanyId, lpCompanyId, cbRecipientId, lpRecipientId, ulType);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_del_quota_recipient)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpRecipientId = NULL;
	LPENTRYID		lpCompanyId = NULL;
	php_stringsize_t cbRecipientId = 0, cbCompanyId = 0;
	long			ulType = 0;

	/* Locals */
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rssl", &res, &lpCompanyId, &cbCompanyId, &lpRecipientId, &cbRecipientId, &ulType) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->DeleteQuotaRecipient(cbCompanyId, lpCompanyId, cbRecipientId, lpRecipientId, ulType);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_get_quota_recipientlist)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval			*res = NULL;
	LPENTRYID		lpObjectId = NULL;
	php_stringsize_t cbObjectId = 0;

	/* Locals */
	zval			*zval_data_value  = NULL;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	IMsgStore		*lpMsgStore = NULL;
	ULONG			ulUsers = 0;
	memory_ptr<ECUSER> lpsUsers;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &res, &lpObjectId, &cbObjectId) == FAILURE)
	return;

	ZEND_FETCH_RESOURCE_C(lpMsgStore, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = GetECObject(lpMsgStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano store");
		goto exit;
	}
	MAPI_G(hr) = lpServiceAdmin->GetQuotaRecipients(cbObjectId, lpObjectId, 0, &ulUsers, &~lpsUsers);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (unsigned int i = 0; i < ulUsers; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "userid", (char*)lpsUsers[i].sUserId.lpb, lpsUsers[i].sUserId.cb, 1);
		add_assoc_string(zval_data_value, "username", (char*)lpsUsers[i].lpszUsername, 1);
		add_assoc_zval(return_value, (char*)lpsUsers[i].lpszUsername, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_getpermissionrules)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
	LPMAPIPROP lpMapiProp = NULL;
	long ulType;

	// return value
	zval *zval_data_value = NULL;
	ULONG cPerms = 0;
	memory_ptr<ECPERMISSION> lpECPerms;

	// local
	int type = -1;
	object_ptr<IECSecurity> lpSecurity;
	ULONG i;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &res, &ulType) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Resource is not a valid MAPI resource");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	MAPI_G(hr) = GetECObject(lpMapiProp, iid_of(lpSecurity), &~lpSecurity);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano object");
		goto exit;
	}
	MAPI_G(hr) = lpSecurity->GetPermissionRules(ulType, &cPerms, &~lpECPerms);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);
	for (i = 0; i < cPerms; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);

		add_assoc_stringl(zval_data_value, "userid", (char*)lpECPerms[i].sUserId.lpb, lpECPerms[i].sUserId.cb, 1);
		add_assoc_long(zval_data_value, "type", lpECPerms[i].ulType);
		add_assoc_long(zval_data_value, "rights", lpECPerms[i].ulRights);
		add_assoc_long(zval_data_value, "state", lpECPerms[i].ulState);

		add_index_zval(return_value, i, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_zarafa_setpermissionrules)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval *res = NULL;
	zval *perms = NULL;
	LPMAPIPROP lpMapiProp = NULL;

	// local
	int type = -1;
	object_ptr<IECSecurity> lpSecurity;
	ULONG cPerms = 0;
	memory_ptr<ECPERMISSION> lpECPerms;
	HashTable *target_hash = NULL;
	ULONG i, j;
	zval **entry = NULL, **value = NULL;
	HashTable *data = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &res, &perms) == FAILURE) return;

	zend_list_find(Z_RESVAL_P(res), &type);
	if(type == le_mapi_message) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMESSAGE, &res, -1, name_mapi_message, le_mapi_message);
	} else if (type == le_mapi_folder) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMAPIFOLDER, &res, -1, name_mapi_folder, le_mapi_folder);
	} else if (type == le_mapi_attachment) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPATTACH, &res, -1, name_mapi_attachment, le_mapi_attachment);
	} else if (type == le_mapi_msgstore) {
		ZEND_FETCH_RESOURCE_C(lpMapiProp, LPMDB, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Resource is not a valid MAPI resource");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	MAPI_G(hr) = GetECObject(lpMapiProp, iid_of(lpSecurity), &~lpSecurity);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Specified object is not a Kopano object");
		goto exit;
	}
	target_hash = HASH_OF(perms);
	if (!target_hash) {
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// The following code should be in typeconversion.cpp
	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	cPerms = zend_hash_num_elements(target_hash);
	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(ECPERMISSION)*cPerms, &~lpECPerms);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	memset(lpECPerms, 0, sizeof(ECPERMISSION)*cPerms);
	
	for (j = 0, i = 0; i < cPerms; ++i, zend_hash_move_forward_ex(target_hash, &hpos)) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos);
		// null pointer returned if perms was not array(array()).
		data = HASH_OF(entry[0]);
		if (zend_hash_find(data, "userid", sizeof("userid"), (void **)&value) != SUCCESS)
			continue;
		SEPARATE_ZVAL(value);
		convert_to_string_ex(value);
		lpECPerms[j].sUserId.cb = Z_STRLEN_PP(value);
		lpECPerms[j].sUserId.lpb = (unsigned char*)Z_STRVAL_PP(value);

		if (zend_hash_find(data, "type", sizeof("type"), (void **)&value) != SUCCESS)
			continue;
		SEPARATE_ZVAL(value);
		convert_to_long_ex(value);
		lpECPerms[j].ulType = Z_LVAL_PP(value);

		if (zend_hash_find(data, "rights", sizeof("rights"), (void **)&value) != SUCCESS)
			continue;
		SEPARATE_ZVAL(value);
		convert_to_long_ex(value);
		lpECPerms[j].ulRights = Z_LVAL_PP(value);

		if (zend_hash_find(data, "state", sizeof("state"), (void **)&value) == SUCCESS) {
			SEPARATE_ZVAL(value);
		    convert_to_long_ex(value);
			lpECPerms[j].ulState = Z_LVAL_PP(value);
		} else {
			lpECPerms[j].ulState = RIGHT_NEW|RIGHT_AUTOUPDATE_DENIED;
		}
		++j;
	}

	MAPI_G(hr) = lpSecurity->SetPermissionRules(j, lpECPerms);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusysupport_open)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// local
	object_ptr<ECFreeBusySupport> lpecFBSupport;

	// extern
	zval*				resSession = NULL;
	zval*				resStore = NULL;
	IMAPISession*		lpSession = NULL;
	IMsgStore*			lpUserStore = NULL;

	// return
	object_ptr<IFreeBusySupport> lpFBSupport;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|r", &resSession, &resStore) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpSession, IMAPISession*, &resSession, -1, name_mapi_session, le_mapi_session);
	if (resStore != nullptr)
		ZEND_FETCH_RESOURCE_C(lpUserStore, LPMDB, &resStore, -1, name_mapi_msgstore, le_mapi_msgstore);

	// Create the freebusy support object
	MAPI_G(hr) = ECFreeBusySupport::Create(&~lpecFBSupport);
	if( MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = lpecFBSupport->QueryInterface(IID_IFreeBusySupport, &~lpFBSupport);
	if( MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = lpFBSupport->Open(lpSession, lpUserStore, (lpUserStore)?TRUE:FALSE);
	if( MAPI_G(hr) != hrSuccess)
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpFBSupport.release(), le_freebusy_support);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusysupport_close)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// Extern
	IFreeBusySupport*	lpFBSupport = NULL;
	zval*				resFBSupport = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &resFBSupport) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBSupport, IFreeBusySupport*, &resFBSupport, -1, name_fb_support, le_freebusy_support);

	MAPI_G(hr) = lpFBSupport->Close();
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusysupport_loaddata)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	HashTable*			target_hash = NULL;
	zval**				entry = NULL;
	int					rid = 0;
	memory_ptr<FBUser> lpUsers;
	IFreeBusySupport*	lpFBSupport = NULL;
	zval*				resFBSupport = NULL;
	zval*				resUsers = NULL;
	ULONG				cUsers = 0;
	memory_ptr<IFreeBusyData *> lppFBData;
	std::vector<object_ptr<IFreeBusyData>> fbdata;
	ULONG				cFBData = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &resFBSupport,&resUsers) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBSupport, IFreeBusySupport*, &resFBSupport, -1, name_fb_support, le_freebusy_support);

	target_hash = HASH_OF(resUsers);
	if (!target_hash) {
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	cUsers = zend_hash_num_elements(target_hash);
	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(FBUser)*cUsers, &~lpUsers);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	// Get the user entryids
	for (unsigned int j = 0, i = 0; i < cUsers; ++i, zend_hash_move_forward_ex(target_hash, &hpos)) {
		if (zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos) == FAILURE) {
			MAPI_G(hr) = MAPI_E_INVALID_ENTRYID;
			goto exit;
		}

		lpUsers[j].m_cbEid = Z_STRLEN_PP(entry);
		lpUsers[j].m_lpEid = (LPENTRYID)Z_STRVAL_PP(entry);
		++j;
	}

	fbdata.resize(cUsers);
	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(IFreeBusyData *) * cUsers, &~lppFBData);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = lpFBSupport->LoadFreeBusyData(cUsers, lpUsers, lppFBData, NULL, &cFBData);
	for (size_t i = 0; i < cUsers; ++i) {
		fbdata[i].reset(lppFBData[i]);
		lppFBData[i] = nullptr;
	}
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	//Return an array of IFreeBusyData interfaces
	array_init(return_value);
	for (unsigned int i = 0; i < cUsers; ++i) {
		if (fbdata[i] != nullptr) {
			// Set resource relation
			rid = ZEND_REGISTER_RESOURCE(NULL, fbdata[i], le_freebusy_data);
			fbdata[i].release();
			// Add item to return list
			add_next_index_resource(return_value, rid);
		} else {
			// Add empty item to return list
			add_next_index_null(return_value);
		}
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusysupport_loadupdate)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	HashTable*			target_hash = NULL;
	zval**				entry = NULL;
	int					rid = 0;
	memory_ptr<FBUser> lpUsers;
	IFreeBusySupport*	lpFBSupport = NULL;
	zval*				resFBSupport = NULL;
	zval*				resUsers = NULL;
	ULONG				cUsers = 0;
	memory_ptr<IFreeBusyUpdate *> lppFBUpdate;
	std::vector<object_ptr<IFreeBusyUpdate>> fbupdate;
	ULONG				cFBUpdate = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &resFBSupport,&resUsers) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBSupport, IFreeBusySupport*, &resFBSupport, -1, name_fb_support, le_freebusy_support);

	target_hash = HASH_OF(resUsers);
	if (!target_hash) {
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	cUsers = zend_hash_num_elements(target_hash);
	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(FBUser)*cUsers, &~lpUsers);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	// Get the user entryids
	for (unsigned int j = 0, i = 0; i < cUsers; ++i, zend_hash_move_forward_ex(target_hash, &hpos)) {
		if (zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos) == FAILURE)
		{
			MAPI_G(hr) = MAPI_E_INVALID_ENTRYID;
			goto exit;
		}

		lpUsers[j].m_cbEid = Z_STRLEN_PP(entry);
		lpUsers[j].m_lpEid = (LPENTRYID)Z_STRVAL_PP(entry);
		++j;
	}

	fbupdate.resize(cUsers);
	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(IFreeBusyUpdate*)*cUsers, &~lppFBUpdate);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = lpFBSupport->LoadFreeBusyUpdate(cUsers, lpUsers, lppFBUpdate, &cFBUpdate, NULL);
	for (size_t i = 0; i < cUsers; ++i) {
		fbupdate[i].reset(lppFBUpdate[i]);
		lppFBUpdate[i] = nullptr;
	}
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	//Return an array of IFreeBusyUpdate interfaces
	array_init(return_value);
	for (unsigned int i = 0; i < cUsers; ++i) {
		if (fbupdate[i] != nullptr) {
			// Set resource relation
			rid = ZEND_REGISTER_RESOURCE(NULL, fbupdate[i], le_freebusy_update);
			fbupdate[i].release();
			// Add item to return list
			add_next_index_resource(return_value, rid);
		} else {
			// Add empty item to return list
			add_next_index_null(return_value);
		}
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusydata_enumblocks)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IFreeBusyData*		lpFBData = NULL;
	zval*				resFBData = NULL;

	FILETIME			ftmStart;
	FILETIME			ftmEnd;
	time_t				ulUnixStart = 0;
	time_t				ulUnixEnd = 0;
	IEnumFBBlock*		lpEnumBlock = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rll", &resFBData, &ulUnixStart, &ulUnixEnd) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBData, IFreeBusyData*, &resFBData, -1, name_fb_data, le_freebusy_data);

	ftmStart = UnixTimeToFileTime(ulUnixStart);
	ftmEnd   = UnixTimeToFileTime(ulUnixEnd);

	MAPI_G(hr) = lpFBData->EnumBlocks(&lpEnumBlock, ftmStart, ftmEnd);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;
	ZEND_REGISTER_RESOURCE(return_value, lpEnumBlock, le_freebusy_enumblock);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusydata_getpublishrange)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IFreeBusyData*		lpFBData = NULL;
	zval*				resFBData = NULL;

	LONG				rtmStart;
	LONG				rtmEnd;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &resFBData) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBData, IFreeBusyData*, &resFBData, -1, name_fb_data, le_freebusy_data);

	MAPI_G(hr) = lpFBData->GetFBPublishRange(&rtmStart, &rtmEnd);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;
	array_init(return_value);
	add_assoc_long(return_value, "start", RTimeToUnixTime(rtmStart));
	add_assoc_long(return_value, "end", RTimeToUnixTime(rtmEnd));
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusydata_setrange)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IFreeBusyData*		lpFBData = NULL;
	zval*				resFBData = NULL;
	time_t				ulUnixStart = 0;
	time_t				ulUnixEnd = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rll", &resFBData, &ulUnixStart, &ulUnixEnd) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBData, IFreeBusyData*, &resFBData, -1, name_fb_data, le_freebusy_data);
	MAPI_G(hr) = lpFBData->SetFBRange(UnixTimeToRTime(ulUnixStart), UnixTimeToRTime(ulUnixEnd));
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusyenumblock_reset)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IEnumFBBlock*		lpEnumBlock = NULL;
	zval*				resEnumBlock = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &resEnumBlock) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpEnumBlock, IEnumFBBlock*, &resEnumBlock, -1, name_fb_enumblock, le_freebusy_enumblock);

	MAPI_G(hr) = lpEnumBlock->Reset();
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusyenumblock_next)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IEnumFBBlock*		lpEnumBlock = NULL;
	zval*				resEnumBlock = NULL;
	long				cElt = 0;
	LONG				cFetch = 0;
	LONG				i;
	memory_ptr<FBBlock_1> lpBlk;
	zval				*zval_data_value  = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &resEnumBlock, &cElt) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpEnumBlock, IEnumFBBlock*, &resEnumBlock, -1, name_fb_enumblock, le_freebusy_enumblock);

	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(FBBlock_1)*cElt, &~lpBlk);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = lpEnumBlock->Next(cElt, lpBlk, &cFetch);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	array_init(return_value);

	for (i = 0; i < cFetch; ++i) {
		MAKE_STD_ZVAL(zval_data_value);
		array_init(zval_data_value);
		add_assoc_long(zval_data_value, "start", RTimeToUnixTime(lpBlk[i].m_tmStart));
		add_assoc_long(zval_data_value, "end", RTimeToUnixTime(lpBlk[i].m_tmEnd));
		add_assoc_long(zval_data_value, "status", (LONG)lpBlk[i].m_fbstatus);

		add_next_index_zval(return_value, zval_data_value);
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusyenumblock_skip)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IEnumFBBlock*		lpEnumBlock = NULL;
	zval*				resEnumBlock = NULL;
	long				ulSkip = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rl", &resEnumBlock, &ulSkip) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpEnumBlock, IEnumFBBlock*, &resEnumBlock, -1, name_fb_enumblock, le_freebusy_enumblock);

	MAPI_G(hr) = lpEnumBlock->Skip(ulSkip);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusyenumblock_restrict)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IEnumFBBlock*		lpEnumBlock = NULL;
	zval*				resEnumBlock = NULL;

	FILETIME			ftmStart;
	FILETIME			ftmEnd;
	time_t				ulUnixStart = 0;
	time_t				ulUnixEnd = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rll", &resEnumBlock, &ulUnixStart, &ulUnixEnd) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpEnumBlock, IEnumFBBlock*, &resEnumBlock, -1, name_fb_enumblock, le_freebusy_enumblock);

	ftmStart = UnixTimeToFileTime(ulUnixStart);
	ftmEnd   = UnixTimeToFileTime(ulUnixEnd);

	MAPI_G(hr) = lpEnumBlock->Restrict(ftmStart, ftmEnd);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusyenumblock_ical)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	DEFERRED_EPILOGUE;
	long req_count = 0;
	php_stringsize_t organizer_len, user_len, uid_len;
	char *organizer_cstr = nullptr, *user_cstr = nullptr;
	char *uid_cstr = nullptr;
	zval *res_addrbook = nullptr, *res_enumblock = nullptr;
	time_t start = 0, end = 0;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rrlllsss", &res_addrbook, &res_enumblock, &req_count, &start, &end, &organizer_cstr, &organizer_len, &user_cstr, &user_len, &uid_cstr, &uid_len) == FAILURE)
		return;

	IAddrBook *addrbook = nullptr;
	ZEND_FETCH_RESOURCE_C(addrbook, IAddrBook *, &res_addrbook, -1, name_mapi_addrbook, le_mapi_addrbook);

	IEnumFBBlock *enumblock = nullptr;
	ZEND_FETCH_RESOURCE_C(enumblock, IEnumFBBlock*, &res_enumblock, -1, name_fb_enumblock, le_freebusy_enumblock);

	memory_ptr<FBBlock_1> blk;
	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(FBBlock_1)*req_count, &~blk);
	if (MAPI_G(hr) != hrSuccess)
		return;

	LONG count = 0;
	MAPI_G(hr) = enumblock->Next(req_count, blk, &count);
	if (MAPI_G(hr) != hrSuccess)
		return;

	std::unique_ptr<MapiToICal> mapiical;
	MAPI_G(hr) = CreateMapiToICal(addrbook, "utf-8", &unique_tie(mapiical));
	if (MAPI_G(hr) != hrSuccess)
		return;

	std::string organizer(organizer_cstr, organizer_len);
	std::string user(user_cstr, user_len);
	std::string uid(uid_cstr, uid_len);
	MAPI_G(hr) = mapiical->AddBlocks(blk, count, start, end, organizer, user, uid);
	if (MAPI_G(hr) != hrSuccess)
		return;

	std::string strical, method;
	MAPI_G(hr) = mapiical->Finalize(0, &method, &strical);
	RETVAL_STRING(strical.c_str(), sizeof(strical.c_str()));
}

ZEND_FUNCTION(mapi_freebusyupdate_publish)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval*				resFBUpdate = NULL;
	zval*				aBlocks = NULL;
	IFreeBusyUpdate*	lpFBUpdate = NULL;
	// local
	memory_ptr<FBBlock_1> lpBlocks;
	ULONG				cBlocks = 0;
	HashTable*			target_hash = NULL;
	zval**				entry = NULL;
	zval**				value = NULL;
	HashTable*			data = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &resFBUpdate, &aBlocks) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBUpdate, IFreeBusyUpdate*, &resFBUpdate, -1, name_fb_update, le_freebusy_update);

	target_hash = HASH_OF(aBlocks);
	if (!target_hash) {
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	HashPosition hpos;
	zend_hash_internal_pointer_reset_ex(target_hash, &hpos);
	cBlocks = zend_hash_num_elements(target_hash);
	MAPI_G(hr) = MAPIAllocateBuffer(sizeof(FBBlock_1)*cBlocks, &~lpBlocks);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	for (unsigned int i = 0; i < cBlocks; ++i, zend_hash_move_forward_ex(target_hash, &hpos)) {
		zend_hash_get_current_data_ex(target_hash, reinterpret_cast<void **>(&entry), &hpos);
		data = HASH_OF(entry[0]);
		if (zend_hash_find(data, "start", sizeof("start"), reinterpret_cast<void **>(&value)) != SUCCESS) {
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
		lpBlocks[i].m_tmStart = UnixTimeToRTime(Z_LVAL_PP(value));
		if (zend_hash_find(data, "end", sizeof("end"), reinterpret_cast<void **>(&value)) != SUCCESS) {
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
		lpBlocks[i].m_tmEnd = UnixTimeToRTime(Z_LVAL_PP(value));
		if (zend_hash_find(data, "status", sizeof("status"), reinterpret_cast<void **>(&value)) != SUCCESS) {
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
		lpBlocks[i].m_fbstatus = (enum FBStatus)Z_LVAL_PP(value);
	}

	MAPI_G(hr) = lpFBUpdate->PublishFreeBusy(lpBlocks, cBlocks);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusyupdate_reset)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IFreeBusyUpdate*	lpFBUpdate = NULL;
	zval*				resFBUpdate = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &resFBUpdate) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBUpdate, IFreeBusyUpdate*, &resFBUpdate, -1, name_fb_update, le_freebusy_update);

	MAPI_G(hr) = lpFBUpdate->ResetPublishedFreeBusy();
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_freebusyupdate_savechanges)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	// params
	zval*				resFBUpdate = NULL;
	time_t				ulUnixStart = 0;
	time_t				ulUnixEnd = 0;
	IFreeBusyUpdate*	lpFBUpdate = NULL;
	// local
	FILETIME			ftmStart;
	FILETIME			ftmEnd;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rll", &resFBUpdate, &ulUnixStart, &ulUnixEnd) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpFBUpdate, IFreeBusyUpdate*, &resFBUpdate, -1, name_fb_update, le_freebusy_update);

	ftmStart = UnixTimeToFileTime(ulUnixStart);
	ftmEnd   = UnixTimeToFileTime(ulUnixEnd);

	MAPI_G(hr) = lpFBUpdate->SaveChanges(ftmStart, ftmEnd);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

/*
 ***********************************************************************************
 * ICS interfaces
 ***********************************************************************************eight
 */

ZEND_FUNCTION(mapi_exportchanges_config)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	IUnknown *			lpImportChanges = NULL; // may be contents or hierarchy
	IExchangeExportChanges *lpExportChanges = NULL;
	IStream *			lpStream = NULL;
	zval *				resStream = NULL;
	long				ulFlags = 0;
	long				ulBuffersize = 0;
	zval *				resImportChanges = NULL;
	zval *				resExportChanges = NULL;
	zval *				aRestrict = NULL;
	zval *				aIncludeProps = NULL;
	zval *				aExcludeProps = NULL;
	int					type = -1;
	memory_ptr<SRestriction> lpRestrict;
	memory_ptr<SPropTagArray> lpIncludeProps, lpExcludeProps;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rrlzzzzl", &resExportChanges, &resStream, &ulFlags, &resImportChanges, &aRestrict, &aIncludeProps, &aExcludeProps, &ulBuffersize) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpExportChanges, IExchangeExportChanges *, &resExportChanges, -1, name_mapi_exportchanges, le_mapi_exportchanges);

	if(Z_TYPE_P(resImportChanges) == IS_RESOURCE) {
		zend_list_find(Z_RESVAL_P(resImportChanges), &type);
		if(type == le_mapi_importcontentschanges) {
			ZEND_FETCH_RESOURCE_C(lpImportChanges, IUnknown *, &resImportChanges, -1, name_mapi_importcontentschanges, le_mapi_importcontentschanges);
		} else if(type == le_mapi_importhierarchychanges) {
			ZEND_FETCH_RESOURCE_C(lpImportChanges, IUnknown *, &resImportChanges, -1, name_mapi_importhierarchychanges, le_mapi_importhierarchychanges);
		} else {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "The importer must be either a contents importer or a hierarchy importer object");
			MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}
	} else if(Z_TYPE_P(resImportChanges) == IS_BOOL && !resImportChanges->value.lval) {
		lpImportChanges = NULL;
	} else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "The importer must be an actual importer resource, or FALSE");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	ZEND_FETCH_RESOURCE_C(lpStream, IStream *, &resStream, -1, name_istream, le_istream);

	if(Z_TYPE_P(aRestrict) == IS_ARRAY) {
		MAPI_G(hr) = MAPIAllocateBuffer(sizeof(SRestriction), &~lpRestrict);
		if (MAPI_G(hr) != hrSuccess)
			goto exit;

		MAPI_G(hr) = PHPArraytoSRestriction(aRestrict, lpRestrict, lpRestrict TSRMLS_CC);
		if (MAPI_G(hr) != hrSuccess)
			goto exit;
	}

	if(Z_TYPE_P(aIncludeProps) == IS_ARRAY) {
		MAPI_G(hr) = PHPArraytoPropTagArray(aIncludeProps, NULL, &~lpIncludeProps TSRMLS_CC);
		if(MAPI_G(hr) != hrSuccess) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to parse includeprops array");
			goto exit;
		}
	}

	if(Z_TYPE_P(aExcludeProps) == IS_ARRAY) {
		MAPI_G(hr) = PHPArraytoPropTagArray(aExcludeProps, NULL, &~lpExcludeProps TSRMLS_CC);
		if(MAPI_G(hr) != hrSuccess) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to parse excludeprops array");
			goto exit;
		}
	}

	MAPI_G(hr) = lpExportChanges->Config(lpStream, ulFlags, lpImportChanges, lpRestrict, lpIncludeProps, lpExcludeProps, ulBuffersize);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_exportchanges_synchronize)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *					resExportChanges = NULL;
	IExchangeExportChanges *lpExportChanges = NULL;
	ULONG					ulSteps = 0;
	ULONG					ulProgress = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &resExportChanges) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpExportChanges, IExchangeExportChanges *, &resExportChanges, -1, name_mapi_exportchanges, le_mapi_exportchanges);

	MAPI_G(hr) = lpExportChanges->Synchronize(&ulSteps, &ulProgress);
	if(MAPI_G(hr) == SYNC_W_PROGRESS) {
		array_init(return_value);

		add_next_index_long(return_value, ulSteps);
		add_next_index_long(return_value, ulProgress);
	} else if(MAPI_G(hr) != hrSuccess) {
		goto exit;
	} else {
		// hr == hrSuccess
		RETVAL_TRUE;
	}

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_exportchanges_updatestate)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *					resExportChanges = NULL;
	zval *					resStream = NULL;
	IExchangeExportChanges *lpExportChanges = NULL;
	IStream *				lpStream = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rr", &resExportChanges, &resStream) == FAILURE) return;

    ZEND_FETCH_RESOURCE_C(lpExportChanges, IExchangeExportChanges *, &resExportChanges, -1, name_mapi_exportchanges, le_mapi_exportchanges);
    ZEND_FETCH_RESOURCE_C(lpStream, IStream *, &resStream, -1, name_istream, le_istream);

	MAPI_G(hr) = lpExportChanges->UpdateState(lpStream);

	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_exportchanges_getchangecount)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *					resExportChanges = NULL;
	IExchangeExportChanges *lpExportChanges = NULL;
	object_ptr<IECExportChanges> lpECExportChanges;
	ULONG					ulChanges;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &resExportChanges) == FAILURE) return;

    ZEND_FETCH_RESOURCE_C(lpExportChanges, IExchangeExportChanges *, &resExportChanges, -1, name_mapi_exportchanges, le_mapi_exportchanges);

	MAPI_G(hr) = lpExportChanges->QueryInterface(IID_IECExportChanges, &~lpECExportChanges);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "ExportChanges does not support IECExportChanges interface which is required for the getchangecount call");
		goto exit;
	}

	MAPI_G(hr) = lpECExportChanges->GetChangeCount(&ulChanges);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_LONG(ulChanges);
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importcontentschanges_config)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *					resImportContentsChanges = NULL;
	zval *					resStream = NULL;
	IExchangeImportContentsChanges *lpImportContentsChanges = NULL;
	IStream	*				lpStream = NULL;
	long					ulFlags = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rrl", &resImportContentsChanges, &resStream, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpImportContentsChanges, IExchangeImportContentsChanges *, &resImportContentsChanges, -1, name_mapi_importcontentschanges, le_mapi_importcontentschanges);
	ZEND_FETCH_RESOURCE_C(lpStream, IStream *, &resStream, -1, name_istream, le_istream);

	MAPI_G(hr) = lpImportContentsChanges->Config(lpStream, ulFlags);

	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importcontentschanges_updatestate)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *							resImportContentsChanges = NULL;
	zval *							resStream = NULL;
	IExchangeImportContentsChanges	*lpImportContentsChanges = NULL;
	IStream *						lpStream = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|r", &resImportContentsChanges, &resStream) == FAILURE) return;

    ZEND_FETCH_RESOURCE_C(lpImportContentsChanges, IExchangeImportContentsChanges *, &resImportContentsChanges, -1, name_mapi_importcontentschanges, le_mapi_importcontentschanges);
	if (resStream != NULL) {
		ZEND_FETCH_RESOURCE_C(lpStream, IStream *, &resStream, -1, name_istream, le_istream);
	}

	MAPI_G(hr) = lpImportContentsChanges->UpdateState(lpStream);

	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importcontentschanges_importmessagechange)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *					resImportContentsChanges = NULL;
	zval *					resProps = NULL;
	long					ulFlags = 0;
	zval *					resMessage = NULL;
	memory_ptr<SPropValue> lpProps;
	ULONG					cValues = 0;
	IMessage *				lpMessage = NULL;
	IExchangeImportContentsChanges * lpImportContentsChanges = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ralz", &resImportContentsChanges, &resProps, &ulFlags, &resMessage) == FAILURE) return;

    ZEND_FETCH_RESOURCE_C(lpImportContentsChanges, IExchangeImportContentsChanges *, &resImportContentsChanges, -1, name_mapi_importcontentschanges, le_mapi_importcontentschanges);

	MAPI_G(hr) = PHPArraytoPropValueArray(resProps, NULL, &cValues, &~lpProps TSRMLS_CC);
    if(MAPI_G(hr) != hrSuccess) {
    	php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to parse property array");
    	goto exit;
	}

	MAPI_G(hr) = lpImportContentsChanges->ImportMessageChange(cValues, lpProps, ulFlags, &lpMessage);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	ZEND_REGISTER_RESOURCE(resMessage, lpMessage, le_mapi_message);
	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importcontentschanges_importmessagedeletion)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *			resMessages;
	zval *			resImportContentsChanges;
	IExchangeImportContentsChanges *lpImportContentsChanges = NULL;
	memory_ptr<SBinaryArray> lpMessages;
	long			ulFlags = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rla", &resImportContentsChanges, &ulFlags, &resMessages) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpImportContentsChanges, IExchangeImportContentsChanges *, &resImportContentsChanges, -1, name_mapi_importcontentschanges, le_mapi_importcontentschanges);

	MAPI_G(hr) = PHPArraytoSBinaryArray(resMessages, NULL, &~lpMessages TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to parse message list");
	    goto exit;
	}

	MAPI_G(hr) = lpImportContentsChanges->ImportMessageDeletion(ulFlags, lpMessages);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importcontentschanges_importperuserreadstatechange)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *			resReadStates;
	zval *			resImportContentsChanges;
	IExchangeImportContentsChanges *lpImportContentsChanges = NULL;
	memory_ptr<READSTATE> lpReadStates;
	ULONG			cValues = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &resImportContentsChanges, &resReadStates) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpImportContentsChanges, IExchangeImportContentsChanges *, &resImportContentsChanges, -1, name_mapi_importcontentschanges, le_mapi_importcontentschanges);

	MAPI_G(hr) = PHPArraytoReadStateArray(resReadStates, NULL, &cValues, &~lpReadStates TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to parse readstates");
	    goto exit;
	}

	MAPI_G(hr) = lpImportContentsChanges->ImportPerUserReadStateChange(cValues, lpReadStates);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importcontentschanges_importmessagemove)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	php_stringsize_t cbSourceKeySrcFolder = 0;
	php_stringsize_t cbSourceKeySrcMessage = 0;
	php_stringsize_t cbPCLMessage = 0;
	php_stringsize_t cbSourceKeyDestMessage = 0;
	php_stringsize_t cbChangeNumDestMessage = 0;
	BYTE *			pbSourceKeySrcFolder = NULL;
	BYTE *			pbSourceKeySrcMessage = NULL;
	BYTE *			pbPCLMessage = NULL;
	BYTE *			pbSourceKeyDestMessage = NULL;
	BYTE *			pbChangeNumDestMessage = NULL;

	zval *			resImportContentsChanges;
	IExchangeImportContentsChanges *lpImportContentsChanges = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsssss", 	&resImportContentsChanges,
																	&pbSourceKeySrcFolder, &cbSourceKeySrcFolder,
																	&pbSourceKeySrcMessage, &cbSourceKeySrcMessage,
																	&pbPCLMessage, &cbPCLMessage,
																	&pbSourceKeyDestMessage, &cbSourceKeyDestMessage,
																	&pbChangeNumDestMessage, &cbChangeNumDestMessage) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpImportContentsChanges, IExchangeImportContentsChanges *, &resImportContentsChanges, -1, name_mapi_importcontentschanges, le_mapi_importcontentschanges);

	MAPI_G(hr) = lpImportContentsChanges->ImportMessageMove(cbSourceKeySrcFolder, pbSourceKeySrcFolder, cbSourceKeySrcMessage, pbSourceKeySrcMessage, cbPCLMessage, pbPCLMessage, cbSourceKeyDestMessage, pbSourceKeyDestMessage, cbChangeNumDestMessage, pbChangeNumDestMessage);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importhierarchychanges_config)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *					resImportHierarchyChanges = NULL;
	zval *					resStream = NULL;
	IExchangeImportHierarchyChanges *lpImportHierarchyChanges = NULL;
	IStream	*				lpStream = NULL;
	long					ulFlags = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rrl", &resImportHierarchyChanges, &resStream, &ulFlags) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpImportHierarchyChanges, IExchangeImportHierarchyChanges *, &resImportHierarchyChanges, -1, name_mapi_importhierarchychanges, le_mapi_importhierarchychanges);
	ZEND_FETCH_RESOURCE_C(lpStream, IStream *, &resStream, -1, name_istream, le_istream);

	MAPI_G(hr) = lpImportHierarchyChanges->Config(lpStream, ulFlags);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importhierarchychanges_updatestate)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *							resImportHierarchyChanges = NULL;
	zval *							resStream = NULL;
	IExchangeImportHierarchyChanges	*lpImportHierarchyChanges = NULL;
	IStream *						lpStream = NULL;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|r", &resImportHierarchyChanges, &resStream) == FAILURE) return;

    ZEND_FETCH_RESOURCE_C(lpImportHierarchyChanges, IExchangeImportHierarchyChanges *, &resImportHierarchyChanges, -1, name_mapi_importhierarchychanges, le_mapi_importhierarchychanges);
	if (resStream != NULL) {
		ZEND_FETCH_RESOURCE_C(lpStream, IStream *, &resStream, -1, name_istream, le_istream);
	}

	MAPI_G(hr) = lpImportHierarchyChanges->UpdateState(lpStream);

	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importhierarchychanges_importfolderchange)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *					resImportHierarchyChanges = NULL;
	zval *					resProps = NULL;
	IExchangeImportHierarchyChanges *lpImportHierarchyChanges = NULL;
	memory_ptr<SPropValue> lpProps;
	ULONG 					cValues = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ra", &resImportHierarchyChanges, &resProps) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpImportHierarchyChanges, IExchangeImportHierarchyChanges *, &resImportHierarchyChanges, -1, name_mapi_importhierarchychanges, le_mapi_importhierarchychanges);

	MAPI_G(hr) = PHPArraytoPropValueArray(resProps, NULL, &cValues, &~lpProps TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to convert properties in properties array");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	MAPI_G(hr) = lpImportHierarchyChanges->ImportFolderChange(cValues, lpProps);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_importhierarchychanges_importfolderdeletion)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *					resImportHierarchyChanges = NULL;
	zval *					resFolders = NULL;
	IExchangeImportHierarchyChanges *lpImportHierarchyChanges = NULL;
	memory_ptr<SBinaryArray> lpFolders;
	long					ulFlags = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rla", &resImportHierarchyChanges, &ulFlags, &resFolders) == FAILURE) return;

	ZEND_FETCH_RESOURCE_C(lpImportHierarchyChanges, IExchangeImportHierarchyChanges *, &resImportHierarchyChanges, -1, name_mapi_importhierarchychanges, le_mapi_importhierarchychanges);

	MAPI_G(hr) = PHPArraytoSBinaryArray(resFolders, NULL, &~lpFolders TSRMLS_CC);
	if(MAPI_G(hr) != hrSuccess) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to parse folder list");
		MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	MAPI_G(hr) = lpImportHierarchyChanges->ImportFolderDeletion(ulFlags, lpFolders);
	if(MAPI_G(hr) != hrSuccess)
		goto exit;

	RETVAL_TRUE;

exit:
	DEFERRED_EPILOGUE;
}

/*
 * This function needs some explanation as it is not just a one-to-one MAPI function. This function
 * accepts an object from PHP, and returns a resource. The resource is the MAPI equivalent of the passed
 * object, which can then be passed to mapi_exportchanges_config(). This basically can be seen as a callback
 * system whereby mapi_exportchanges_synchronize() calls back to PHP-space to give it data.
 *
 * The way we do this is to create a real IExchangeImportChanges class that calls back to its PHP equivalent
 * in each method implementation. If the function cannot be found, we simply return an appropriate error.
 *
 * We also have to make sure that we do good refcounting here, as the user may wrap a PHP object, and then
 * delete references to that object. We still hold an internal reference though, so we have to tell Zend
 * that we still have a pointer to the object. We do this with the standard internal Zend refcounting system.
 */

ZEND_FUNCTION(mapi_wrap_importcontentschanges)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
    zval *							objImportContentsChanges = NULL;
    ECImportContentsChangesProxy *	lpImportContentsChanges = NULL;

    RETVAL_FALSE;
    MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "o", &objImportContentsChanges) == FAILURE) return;

    lpImportContentsChanges = new ECImportContentsChangesProxy(objImportContentsChanges TSRMLS_CC);

    // Simply return the wrapped object
	ZEND_REGISTER_RESOURCE(return_value, lpImportContentsChanges, le_mapi_importcontentschanges);
	MAPI_G(hr) = hrSuccess;
	DEFERRED_EPILOGUE;
}

// Same for IExchangeImportHierarchyChanges
ZEND_FUNCTION(mapi_wrap_importhierarchychanges)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
    zval *							objImportHierarchyChanges = NULL;
    ECImportHierarchyChangesProxy *	lpImportHierarchyChanges = NULL;

    RETVAL_FALSE;
    MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;

    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "o", &objImportHierarchyChanges) == FAILURE) return;

    lpImportHierarchyChanges = new ECImportHierarchyChangesProxy(objImportHierarchyChanges TSRMLS_CC);

    // Simply return the wrapped object
	ZEND_REGISTER_RESOURCE(return_value, lpImportHierarchyChanges, le_mapi_importhierarchychanges);
	MAPI_G(hr) = hrSuccess;
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_inetmapi_imtoinet)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
    zval *resSession;
    zval *resAddrBook;
    zval *resMessage;
    zval *resOptions;
    sending_options sopt;
	object_ptr<ECMemStream> lpMemStream = NULL;
    IStream *lpStream = NULL;
	std::unique_ptr<char[]> lpBuffer;
    
    imopt_default_sending_options(&sopt);
    sopt.no_recipients_workaround = true;
    
    IMAPISession *lpMAPISession = NULL;
    IAddrBook *lpAddrBook = NULL;
    IMessage *lpMessage = NULL;
    
    RETVAL_FALSE;
    MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
    
    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rrra", &resSession, &resAddrBook, &resMessage, &resOptions) == FAILURE) return;
    
    ZEND_FETCH_RESOURCE_C(lpMAPISession, IMAPISession *, &resSession, -1, name_mapi_session, le_mapi_session);
    ZEND_FETCH_RESOURCE_C(lpAddrBook, IAddrBook *, &resAddrBook, -1, name_mapi_addrbook, le_mapi_addrbook);
    ZEND_FETCH_RESOURCE_C(lpMessage, IMessage *, &resMessage, -1, name_mapi_message, le_mapi_message);

    MAPI_G(hr) = PHPArraytoSendingOptions(resOptions, &sopt);
    if(MAPI_G(hr) != hrSuccess)
        goto exit;
    
    MAPI_G(hr) = IMToINet(lpMAPISession, lpAddrBook, lpMessage, &unique_tie(lpBuffer), sopt);
    if(MAPI_G(hr) != hrSuccess)
        goto exit;
    MAPI_G(hr) = ECMemStream::Create(lpBuffer.get(), strlen(lpBuffer.get()), 0, nullptr, nullptr, nullptr, &~lpMemStream);
    if(MAPI_G(hr) != hrSuccess)
        goto exit;
        
    MAPI_G(hr) = lpMemStream->QueryInterface(IID_IStream, (void **)&lpStream);
    if(MAPI_G(hr) != hrSuccess)
        goto exit;
        
    ZEND_REGISTER_RESOURCE(return_value, lpStream, le_istream);
    
exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_inetmapi_imtomapi)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
    zval *resSession;
    zval *resStore;
    zval *resAddrBook;
    zval *resMessage;
    zval *resOptions;
    delivery_options dopt;
	php_stringsize_t cbString = 0;
    char *szString = NULL;
    
    imopt_default_delivery_options(&dopt);
    
    IMAPISession *lpMAPISession = NULL;
    IAddrBook *lpAddrBook = NULL;
    IMessage *lpMessage = NULL;
    IMsgStore *lpMsgStore = NULL;
    
    RETVAL_FALSE;
    MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
    
    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rrrrsa", &resSession, &resStore, &resAddrBook, &resMessage, &szString, &cbString, &resOptions) == FAILURE) return;
    
    ZEND_FETCH_RESOURCE_C(lpMAPISession, IMAPISession *, &resSession, -1, name_mapi_session, le_mapi_session);
    ZEND_FETCH_RESOURCE_C(lpMsgStore, IMsgStore *, &resStore, -1, name_mapi_msgstore, le_mapi_msgstore);
    ZEND_FETCH_RESOURCE_C(lpAddrBook, IAddrBook *, &resAddrBook, -1, name_mapi_addrbook, le_mapi_addrbook);
    ZEND_FETCH_RESOURCE_C(lpMessage, IMessage *, &resMessage, -1, name_mapi_message, le_mapi_message);

    std::string strInput(szString, cbString);

	MAPI_G(hr) = PHPArraytoDeliveryOptions(resOptions, &dopt);
    if(MAPI_G(hr) != hrSuccess)
        goto exit; 
   
    MAPI_G(hr) = IMToMAPI(lpMAPISession, lpMsgStore, lpAddrBook, lpMessage, strInput, dopt);

    if(MAPI_G(hr) != hrSuccess)
        goto exit;
        
    RETVAL_TRUE;
    
exit:
	DEFERRED_EPILOGUE;
}    

ZEND_FUNCTION(mapi_icaltomapi)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *resSession;
	zval *resStore;
	zval *resAddrBook;
	zval *resMessage;
	zend_bool noRecipients = false;
	php_stringsize_t cbString = 0;
	char *szString = nullptr;
	IMAPISession *lpMAPISession = nullptr;
	IAddrBook *lpAddrBook = nullptr;
	IMessage *lpMessage = nullptr;
	IMsgStore *lpMsgStore = nullptr;
	std::unique_ptr<ICalToMapi> lpIcalToMapi;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rrrrsb",
	    &resSession, &resStore, &resAddrBook, &resMessage, &szString,
	    &cbString, &noRecipients) == FAILURE)
		return;
	ZEND_FETCH_RESOURCE_C(lpMAPISession, IMAPISession *, &resSession, -1, name_mapi_session, le_mapi_session);
	ZEND_FETCH_RESOURCE_C(lpMsgStore, IMsgStore *, &resStore, -1, name_mapi_msgstore, le_mapi_msgstore);
	ZEND_FETCH_RESOURCE_C(lpAddrBook, IAddrBook *, &resAddrBook, -1, name_mapi_addrbook, le_mapi_addrbook);
	ZEND_FETCH_RESOURCE_C(lpMessage, IMessage *, &resMessage, -1, name_mapi_message, le_mapi_message);

	std::string icalMsg(szString, cbString);
	memory_ptr<SPropValue> prop;
	object_ptr<IMailUser> mailuser;
	ULONG objtype;
	MAPI_G(hr) = HrGetOneProp(lpMsgStore, PR_MAILBOX_OWNER_ENTRYID, &~prop);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	MAPI_G(hr) = lpMAPISession->OpenEntry(prop->Value.bin.cb, reinterpret_cast<ENTRYID *>(prop->Value.bin.lpb), &iid_of(mailuser), MAPI_BEST_ACCESS, &objtype, &~mailuser);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;

	// noRecpients, skip recipients from ical.
	// Used for DAgent, which uses the mail recipients
	MAPI_G(hr) = CreateICalToMapi(lpMessage, lpAddrBook, noRecipients, &unique_tie(lpIcalToMapi));
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	// Set the default timezone to UTC if none is set, replicating the
	// behaviour of VMIMEToMAPI.
	MAPI_G(hr) = lpIcalToMapi->ParseICal(icalMsg, "utf-8", "UTC", mailuser, 0);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	if (lpIcalToMapi->GetItemCount() == 0) {
		MAPI_G(hr) = MAPI_E_TABLE_EMPTY;
		goto exit;
	}
	MAPI_G(hr) = lpIcalToMapi->GetItem(0, 0, lpMessage);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	RETVAL_TRUE;
 exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_mapitoical)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *zvignore, *resAddrBook;
	zval *resMessage;
	zval *resOptions;
	IAddrBook *lpAddrBook = nullptr;
	IMessage *lpMessage = nullptr;
	std::unique_ptr<MapiToICal> lpMtIcal;
	std::string strical("");
	std::string method("");

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zrra",
	    &zvignore, &resAddrBook, &resMessage, &resOptions) == FAILURE)
		return;
	ZEND_FETCH_RESOURCE_C(lpAddrBook, IAddrBook *, &resAddrBook, -1, name_mapi_addrbook, le_mapi_addrbook);
	ZEND_FETCH_RESOURCE_C(lpMessage, IMessage *, &resMessage, -1, name_mapi_message, le_mapi_message);

	// set HR
	MAPI_G(hr) = CreateMapiToICal(lpAddrBook, "utf-8", &unique_tie(lpMtIcal));
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = lpMtIcal->AddMessage(lpMessage, "", 0);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = lpMtIcal->Finalize(0, &method, &strical);
	RETVAL_STRING(strical.c_str(), sizeof(strical.c_str()));
 exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_vcftomapi)
{
	zval *zvignore, *resMessage;
	ULONG cbString = 0;
	char *szString = nullptr;
	IMessage *lpMessage = nullptr;
	std::unique_ptr<vcftomapi> conv;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zzrs",
	    &zvignore, &zvignore, &resMessage, &szString,
	    &cbString) == FAILURE)
		return;
	ZEND_FETCH_RESOURCE_C(lpMessage, IMessage *, &resMessage, -1, name_mapi_message, le_mapi_message);

	std::string vcfMsg(szString, cbString);
	MAPI_G(hr) = create_vcftomapi(lpMessage, &unique_tie(conv));
	if (MAPI_G(hr) != hrSuccess)
		return;
	MAPI_G(hr) = conv->parse_vcf(vcfMsg);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = conv->get_item(lpMessage);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	RETVAL_TRUE;
 exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_mapitovcf)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *zvignore, *resAddrBook;
	zval *resMessage;
	zval *resOptions;
	IMessage *lpMessage = nullptr;
	std::unique_ptr<mapitovcf> conv;
	std::string vcf;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zrra",
	    &zvignore, &resAddrBook, &resMessage, &resOptions) == FAILURE)
		return;
	ZEND_FETCH_RESOURCE_C(lpMessage, IMessage *, &resMessage, -1, name_mapi_message, le_mapi_message);

	MAPI_G(hr) = create_mapitovcf(&unique_tie(conv));
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = conv->add_message(lpMessage);
	if (MAPI_G(hr) != hrSuccess)
		goto exit;
	MAPI_G(hr) = conv->finalize(&vcf);
	RETVAL_STRING(vcf.c_str(), vcf.size());
 exit:
	DEFERRED_EPILOGUE;
}

ZEND_FUNCTION(mapi_enable_exceptions)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	ULONG			cbExClass = 0;
	char *			szExClass = NULL;
	zend_class_entry **ce = NULL;

	RETVAL_FALSE;
	
    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &szExClass, &cbExClass) == FAILURE) return;
    
    if (zend_hash_find(CG(class_table), szExClass, cbExClass+1, (void **) &ce) == SUCCESS) {
        MAPI_G(exceptions_enabled) = true;
        MAPI_G(exception_ce) = *ce;
        RETVAL_TRUE;
    }
    
	LOG_END();
}

// Can be queried by client applications to check whether certain API features are supported or not.
ZEND_FUNCTION(mapi_feature)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	static const char *const features[] =
		{"LOGONFLAGS", "NOTIFICATIONS", "INETMAPI_IMTOMAPI", "ST_ONLY_WHEN_OOF"};
	const char *szFeature = NULL;
	php_stringsize_t cbFeature = 0;
    
    RETVAL_FALSE;
    
    if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &szFeature, &cbFeature) == FAILURE) return;
	for (size_t i = 0; i < ARRAY_SIZE(features); ++i)
        if(strcasecmp(features[i], szFeature) == 0) {
            RETVAL_TRUE;
            break;
	}
    LOG_END();
}

ZEND_FUNCTION(kc_session_save)
{
	PMEASURE_FUNC;
	zval *res = nullptr, *outstr = nullptr;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rz", &res, &outstr) == FAILURE)
		return;
	IMAPISession *ses;
	ZEND_FETCH_RESOURCE_C(ses, IMAPISession *, &res, -1, name_mapi_session, le_mapi_session);
	std::string data;
	MAPI_G(hr) = kc_session_save(ses, data);
	if (MAPI_G(hr) == hrSuccess)
		ZVAL_STRINGL(outstr, data.c_str(), data.size(), 1);
	RETVAL_LONG(MAPI_G(hr));
	LOG_END();
}

ZEND_FUNCTION(kc_session_restore)
{
	PMEASURE_FUNC;
	zval *data, *res;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zz", &data, &res) == FAILURE)
		return;
	if (Z_TYPE_P(data) != IS_STRING) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "kc_session_restore() expects parameter 1 to be string, but something else was given");
		RETVAL_LONG(MAPI_G(hr) = MAPI_E_INVALID_PARAMETER);
		LOG_END();
		return;
	}
	object_ptr<IMAPISession> ses = nullptr;
	MAPI_G(hr) = kc_session_restore(std::string(Z_STRVAL_P(data), Z_STRLEN_P(data)), &~ses);
	if (MAPI_G(hr) == hrSuccess)
		ZEND_REGISTER_RESOURCE(res, ses.release(), le_mapi_session);
	RETVAL_LONG(MAPI_G(hr));
	LOG_END();
}

ZEND_FUNCTION(mapi_msgstore_abortsubmit)
{
	PMEASURE_FUNC;
	LOG_BEGIN();
	zval *res;
	IMsgStore *store = nullptr;
	ENTRYID *eid = nullptr;
	size_t eid_size = 0;

	RETVAL_FALSE;
	MAPI_G(hr) = MAPI_E_INVALID_PARAMETER;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r|s", &res, &eid, &eid_size) == FAILURE)
		return;
	ZEND_FETCH_RESOURCE_C(store, IMsgStore *, &res, -1, name_mapi_msgstore, le_mapi_msgstore);
	MAPI_G(hr) = store->AbortSubmit(eid_size, eid, 0);
	if (FAILED(MAPI_G(hr)))
		kphperr("Unable to abort submit", MAPI_G(hr));
	else
		RETVAL_TRUE;
	DEFERRED_EPILOGUE;
}
