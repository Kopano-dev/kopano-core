/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <list>

#include <cerrno>
#include <cassert>
#include <kopano/EMSAbTag.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ECPluginSharedData.h>
#include <kopano/lockhelper.hpp>
#include <kopano/memory.hpp>
#include "ECStatsCollector.h"
#include <kopano/stringutil.h>

using namespace KC;

#include "LDAPUserPlugin.h"
#include "ldappasswords.h"
#include <kopano/ecversion.h>

#ifndef PROP_ID
// from mapidefs.h
	#define PROP_ID(ulPropTag)		(((ULONG)(ulPropTag))>>16)
#endif

extern "C" {

UserPlugin *getUserPluginInstance(std::mutex &pluginlock,
    ECPluginSharedData *shareddata)
{
	return new LDAPUserPlugin(pluginlock, shareddata);
}

	void deleteUserPluginInstance(UserPlugin *up) {
		delete up;
	}

unsigned long getUserPluginVersion()
{
	return PROJECT_VERSION_REVISION;
}

} /* extern "C" */

namespace KC {

using std::list;
using std::runtime_error;
using std::string;

class ldap_delete {
	public:
	void operator()(void *x) const { ldap_memfree(x); }
	void operator()(BerElement *x) const { ber_free(x, 0); }
	void operator()(LDAPMessage *x) const { ldap_msgfree(x); }
	void operator()(LDAPControl *x) const { ldap_control_free(x); }
	void operator()(LDAPControl **x) const { ldap_controls_free(x); }
	void operator()(struct berval **x) const { ldap_value_free_len(x); }
};

typedef KCHL::memory_ptr<char, ldap_delete> auto_free_ldap_attribute;
typedef KCHL::memory_ptr<BerElement, ldap_delete> auto_free_ldap_berelement;
typedef KCHL::memory_ptr<LDAPMessage, ldap_delete> auto_free_ldap_message;
typedef KCHL::memory_ptr<LDAPControl, ldap_delete> auto_free_ldap_control;
typedef KCHL::memory_ptr<LDAPControl *, ldap_delete> auto_free_ldap_controls;
typedef KCHL::memory_ptr<struct berval *, ldap_delete> auto_free_ldap_berval;

#define LDAP_DATA_TYPE_DN			"dn"	// data in attribute like cn=piet,cn=user,dc=localhost,dc=com
#define LDAP_DATA_TYPE_BINARY		"binary"

#define FETCH_ATTR_VALS 0
#define DONT_FETCH_ATTR_VALS 1

#if HAVE_LDAP_CREATE_PAGE_CONTROL
#define FOREACH_PAGING_SEARCH(basedn, scope, filter, attrs, flags, res) \
{ \
	bool morePages = false; \
	struct berval sCookie = {0, NULL};									\
	int ldap_page_size = strtoul(m_config->GetSetting("ldap_page_size"), NULL, 10); \
	LDAPControl *serverControls[2] = { NULL, NULL }; \
	auto_free_ldap_control pageControl; \
	auto_free_ldap_controls returnedControls; \
	int rc; \
	\
	ldap_page_size = (ldap_page_size == 0) ? 1000 : ldap_page_size; \
	do { \
		if (m_ldap == NULL) \
			/* this either returns a connection or throws an exception */ \
			m_ldap = ConnectLDAP(m_config->GetSetting("ldap_bind_user"), m_config->GetSetting("ldap_bind_passwd")); \
		/* set critical to 'F' to not force paging? @todo find an ldap server without support. */ \
		rc = ldap_create_page_control(m_ldap, ldap_page_size, &sCookie, 0, &~pageControl); \
		if (rc != LDAP_SUCCESS) {										\
			/* 'F' ? */ \
			throw ldap_error(string("ldap_create_page_control: ") + ldap_err2string(rc), rc); \
		} \
		serverControls[0] = pageControl; \
		\
		/* search like normal, throws on error */ \
		my_ldap_search_s(basedn, scope, filter, attrs, flags, &~res, serverControls); \
		\
		/* get paged result */ \
		rc = ldap_parse_result(m_ldap, res, nullptr, nullptr, nullptr, nullptr, &~returnedControls, 0); \
		if (rc != LDAP_SUCCESS) { \
			/* @todo, whoops do we really need to unbind? */ \
			/* ldap_unbind(m_ldap); */ \
			/* m_ldap = NULL; */ \
			throw ldap_error(string("ldap_parse_result: ") + ldap_err2string(rc), rc); \
		} \
		\
		if (sCookie.bv_val != NULL) { \
			ber_memfree(sCookie.bv_val); \
			sCookie.bv_val = NULL; \
			sCookie.bv_len = 0; \
		} \
		if (!!returnedControls) {										\
			rc = ldap_parse_pageresponse_control(m_ldap, returnedControls[0], NULL, &sCookie); \
			if (rc != LDAP_SUCCESS) {										\
				throw ldap_error(string("ldap_parse_pageresponse_control: ") + ldap_err2string(rc), rc); \
			}																\
			morePages = sCookie.bv_len > 0; \
		} else { \
			morePages = false; \
		}

#define END_FOREACH_LDAP_PAGING	\
	} \
	while (morePages == true); \
	\
	if (sCookie.bv_val != NULL) { \
		ber_memfree(sCookie.bv_val); \
		sCookie.bv_val = NULL; \
		sCookie.bv_len = 0; \
	} \
}
#else
// non paged support, revert to normal search
#define FOREACH_PAGING_SEARCH(basedn, scope, filter, attrs, flags, res) \
{ \
	my_ldap_search_s(basedn, scope, filter, attrs, flags, &~res);

#define END_FOREACH_LDAP_PAGING	\
	}

#endif

#define FOREACH_ENTRY(res) \
{ \
	LDAPMessage *entry = NULL; \
	for (entry = ldap_first_entry(m_ldap, res); \
		 entry != NULL; \
		 entry = ldap_next_entry(m_ldap, entry)) {

#define END_FOREACH_ENTRY \
	} \
}

#define FOREACH_ATTR(entry) \
{ \
	auto_free_ldap_attribute att; \
	auto_free_ldap_berelement ber; \
	for (att.reset(ldap_first_attribute(m_ldap, entry, &~ber)); \
		 att != NULL; \
		 att.reset(ldap_next_attribute(m_ldap, entry, ber))) {

#define END_FOREACH_ATTR \
	}\
}

class attrArray _kc_final {
public:
	attrArray(unsigned int ulSize) :
		ulAttrs(0), ulMaxAttrs(ulSize),
		lpAttrs(new const char *[ulMaxAttrs+1])
	{
		/* +1 for NULL entry */
		assert(lpAttrs != NULL);
		/* Make sure all entries are NULL */
		memset(lpAttrs.get(), 0, sizeof(const char *) * ulSize);
	}

	void add(const char *lpAttr)
	{
		assert(ulAttrs < ulMaxAttrs);
		lpAttrs[ulAttrs++] = lpAttr;
		lpAttrs[ulAttrs] = NULL;
	}

	void add(const char **lppAttr)
	{
		unsigned int i = 0;

		while (lppAttr[i])
			add(lppAttr[i++]);
	}

	bool empty(void) const
	{
		return lpAttrs[0] == NULL;
	}

	const char **get(void) const
	{
		return lpAttrs.get();
	}

private:
	unsigned int ulAttrs;
	unsigned int ulMaxAttrs;
	std::unique_ptr<const char *[]> lpAttrs;
};

/* Make life a bit less boring */
#define CONFIG_TO_ATTR(__attrs, __var, __config)			\
	const char *__var = m_config->GetSetting(__config, "", NULL);	\
	if (__var)												\
		(__attrs)->add(__var);

std::unique_ptr<LDAPCache> LDAPUserPlugin::m_lpCache(new LDAPCache());

template<typename T> static constexpr inline LONGLONG dur2ms(const T &t)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
}

LDAPUserPlugin::LDAPUserPlugin(std::mutex &pluginlock,
    ECPluginSharedData *shareddata) :
	UserPlugin(pluginlock, shareddata), m_ldap(NULL), ldapServerIndex(0)
{
	// LDAP Defaults
	const configsetting_t lpDefaults[] = {
		{ "ldap_user_sendas_relation_attribute", "ldap_sendas_relation_attribute", CONFIGSETTING_ALIAS },
		{ "ldap_user_sendas_attribute_type", "ldap_sendas_attribute_type", CONFIGSETTING_ALIAS },
		{ "ldap_user_sendas_attribute", "ldap_sendas_attribute", CONFIGSETTING_ALIAS },
		{ "ldap_host","localhost" },
		{ "ldap_port","389" },
		{ "ldap_uri","" },
		{ "ldap_protocol", "ldap" },
		{ "ldap_server_charset", "UTF-8" },
		{ "ldap_bind_user","" },
		{ "ldap_bind_passwd","", CONFIGSETTING_EXACT | CONFIGSETTING_RELOADABLE },
		{ "ldap_search_base","", CONFIGSETTING_RELOADABLE },
		{ "ldap_object_type_attribute", "objectClass", CONFIGSETTING_RELOADABLE },
		{ "ldap_user_type_attribute_value", "", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE },
		{ "ldap_group_type_attribute_value", "", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE },
		{ "ldap_contact_type_attribute_value", "", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_type_attribute_value", "", static_cast<unsigned short>((m_bHosted ? CONFIGSETTING_NONEMPTY : 0) | CONFIGSETTING_RELOADABLE)},
		{ "ldap_addresslist_type_attribute_value", "", CONFIGSETTING_RELOADABLE },
		{ "ldap_dynamicgroup_type_attribute_value", "", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_type_attribute_value", "", static_cast<unsigned short>((m_bDistributed ? CONFIGSETTING_NONEMPTY : 0) | CONFIGSETTING_RELOADABLE)},
		{ "ldap_user_search_base","", CONFIGSETTING_UNUSED },
		{ "ldap_user_search_filter","", CONFIGSETTING_RELOADABLE },
		{ "ldap_user_unique_attribute","cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_user_unique_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_user_unique_attribute_name","objectClass", CONFIGSETTING_RELOADABLE },
		{ "ldap_group_search_base","", CONFIGSETTING_UNUSED },
		{ "ldap_group_search_filter","", CONFIGSETTING_RELOADABLE },
		{ "ldap_group_unique_attribute","cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_group_unique_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_group_security_attribute","kopanoSecurityGroup", CONFIGSETTING_RELOADABLE },
		{ "ldap_group_security_attribute_type","boolean", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_search_base","", CONFIGSETTING_UNUSED },
		{ "ldap_company_search_filter","", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_unique_attribute","ou", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_unique_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_fullname_attribute","cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_loginname_attribute","uid", CONFIGSETTING_RELOADABLE },
		{ "ldap_password_attribute","userPassword", CONFIGSETTING_RELOADABLE },
		{ "ldap_nonactive_attribute","kopanoSharedStoreOnly", CONFIGSETTING_RELOADABLE },
		{ "ldap_resource_type_attribute","kopanoResourceType", CONFIGSETTING_RELOADABLE },
		{ "ldap_resource_capacity_attribute","kopanoResourceCapacity", CONFIGSETTING_RELOADABLE },
		{ "ldap_user_certificate_attribute", "userCertificate", CONFIGSETTING_RELOADABLE },
		{ "ldap_emailaddress_attribute","mail", CONFIGSETTING_RELOADABLE },
		{ "ldap_emailaliases_attribute","kopanoAliases", CONFIGSETTING_RELOADABLE },
		{ "ldap_groupname_attribute","cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_groupmembers_attribute","member", CONFIGSETTING_RELOADABLE },
		{ "ldap_groupmembers_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_companyname_attribute","ou", CONFIGSETTING_RELOADABLE },
		{ "ldap_isadmin_attribute","kopanoAdmin", CONFIGSETTING_RELOADABLE },
		{ "ldap_sendas_attribute","kopanoSendAsPrivilege", CONFIGSETTING_RELOADABLE },
		{ "ldap_sendas_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_sendas_relation_attribute","", CONFIGSETTING_RELOADABLE },
		{ "ldap_user_exchange_dn_attribute", "0x6788001E", CONFIGSETTING_ALIAS },
		{ "ldap_user_telephone_attribute", "0x3A08001E", CONFIGSETTING_ALIAS },
		{ "ldap_user_department_attribute", "0x3A23001E", CONFIGSETTING_ALIAS },
		{ "ldap_user_location_attribute", "0x3A18001E", CONFIGSETTING_ALIAS},
		{ "ldap_user_fax_attribute", "0x3A19001E", CONFIGSETTING_ALIAS },
		{ "ldap_company_view_attribute","kopanoViewPrivilege", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_view_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_view_relation_attribute","", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_admin_attribute","kopanoAdminPrivilege", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_admin_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_admin_relation_attribute","", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_system_admin_attribute","kopanoSystemAdmin", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_system_admin_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_system_admin_relation_attribute","", CONFIGSETTING_RELOADABLE },
		{ "ldap_authentication_method","bind", CONFIGSETTING_RELOADABLE },
		{ "ldap_quotaoverride_attribute","kopanoQuotaOverride", CONFIGSETTING_RELOADABLE },
		{ "ldap_warnquota_attribute","kopanoQuotaWarn", CONFIGSETTING_RELOADABLE },
		{ "ldap_softquota_attribute","kopanoQuotaSoft", CONFIGSETTING_RELOADABLE },
		{ "ldap_hardquota_attribute","kopanoQuotaHard", CONFIGSETTING_RELOADABLE },
		{ "ldap_userdefault_quotaoverride_attribute","kopanoUserDefaultQuotaOverride", CONFIGSETTING_RELOADABLE },
		{ "ldap_userdefault_warnquota_attribute","kopanoUserDefaultQuotaWarn", CONFIGSETTING_RELOADABLE },
		{ "ldap_userdefault_softquota_attribute","kopanoUserDefaultQuotaSoft", CONFIGSETTING_RELOADABLE },
		{ "ldap_userdefault_hardquota_attribute","kopanoUserDefaultQuotaHard", CONFIGSETTING_RELOADABLE },
		{ "ldap_quota_userwarning_recipients_attribute","kopanoQuotaUserWarningRecipients", CONFIGSETTING_RELOADABLE },
		{ "ldap_quota_userwarning_recipients_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_quota_userwarning_recipients_relation_attribute","", CONFIGSETTING_RELOADABLE },
		{ "ldap_quota_companywarning_recipients_attribute","kopanoQuotaCompanyWarningRecipients", CONFIGSETTING_RELOADABLE },
		{ "ldap_quota_companywarning_recipients_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_quota_companywarning_recipients_relation_attribute","", CONFIGSETTING_RELOADABLE },
		{ "ldap_quota_multiplier","1", CONFIGSETTING_RELOADABLE },
		{ "ldap_user_scope","", CONFIGSETTING_UNUSED },
		{ "ldap_group_scope","", CONFIGSETTING_UNUSED },
		{ "ldap_company_scope","", CONFIGSETTING_UNUSED },
		{ "ldap_groupmembers_relation_attribute", "", CONFIGSETTING_RELOADABLE },
		{ "ldap_last_modification_attribute", "modifyTimestamp", CONFIGSETTING_RELOADABLE },
		{ "ldap_user_server_attribute", "kopanoUserServer", CONFIGSETTING_RELOADABLE },
		{ "ldap_company_server_attribute", "kopanoCompanyServer", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_address_attribute", "", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_http_port_attribute", "kopanoHttpPort", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_ssl_port_attribute", "kopanoSslPort", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_file_path_attribute", "kopanoFilePath", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_proxy_path_attribute", "kopanoProxyURL", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_contains_public_attribute", "kopanoContainsPublic", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_scope", "", CONFIGSETTING_UNUSED },
		{ "ldap_server_search_base", "", CONFIGSETTING_UNUSED },
		{ "ldap_server_search_filter", "", CONFIGSETTING_RELOADABLE },
		{ "ldap_server_unique_attribute", "cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_addresslist_search_base","", CONFIGSETTING_UNUSED },
		{ "ldap_addresslist_scope","", CONFIGSETTING_UNUSED },
		{ "ldap_addresslist_search_filter","", CONFIGSETTING_RELOADABLE },
		{ "ldap_addresslist_unique_attribute","cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_addresslist_unique_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_addresslist_filter_attribute","kopanoFilter", CONFIGSETTING_RELOADABLE },
		{ "ldap_addresslist_search_base_attribute","kopanoBase", CONFIGSETTING_RELOADABLE },
		{ "ldap_addresslist_name_attribute","cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_dynamicgroup_search_filter","", CONFIGSETTING_RELOADABLE },
		{ "ldap_dynamicgroup_unique_attribute","cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_dynamicgroup_unique_attribute_type","text", CONFIGSETTING_RELOADABLE },
		{ "ldap_dynamicgroup_filter_attribute","kopanoFilter", CONFIGSETTING_RELOADABLE },
		{ "ldap_dynamicgroup_search_base_attribute","kopanoBase", CONFIGSETTING_RELOADABLE },
		{ "ldap_dynamicgroup_name_attribute","cn", CONFIGSETTING_RELOADABLE },
		{ "ldap_addressbook_hide_attribute","kopanoHidden", CONFIGSETTING_RELOADABLE },
		{ "ldap_network_timeout", "30", CONFIGSETTING_RELOADABLE },
		{ "ldap_object_search_filter", "", CONFIGSETTING_RELOADABLE },
		{ "ldap_filter_cutoff_elements", "1000", CONFIGSETTING_RELOADABLE },
		{ "ldap_page_size", "1000", CONFIGSETTING_RELOADABLE }, // MaxPageSize in ADS defaults to 1000

		/* Aliases, they should be loaded through the propmap directive */
		{ "0x6788001E", "", 0, CONFIGGROUP_PROPMAP },								/* PR_EC_EXCHANGE_DN */
		{ "0x3A08001E", "telephoneNumber", 0, CONFIGGROUP_PROPMAP },				/* PR_BUSINESS_TELEPHONE_NUMBER */
		{ "0x3A23001E", "facsimileTelephoneNumber", 0, CONFIGGROUP_PROPMAP },		/* PR_PRIMARY_FAX_NUMBER */
		{ "0x3A18001E", "department", 0, CONFIGGROUP_PROPMAP },						/* PR_DEPARTMENT_NAME */
		{ "0x3A19001E", "physicalDeliveryOfficeName", 0, CONFIGGROUP_PROPMAP },		/* PR_OFFICE_LOCATION */

		{ NULL, NULL },
	};
	static const char *const lpszAllowedDirectives[] = {
		"include",
		"propmap",
		NULL,
	};

	m_config = shareddata->CreateConfig(lpDefaults, lpszAllowedDirectives);
	if (!m_config)
		throw runtime_error(string("Not a valid configuration file."));

	// get the list of ldap urls and split them
	const char *const ldap_uri = m_config->GetSetting("ldap_uri");

	if (ldap_uri && strlen(ldap_uri))
		ldap_servers = tokenize(std::string(ldap_uri), ' ', true);
	else {
		const char *const ldap_host = m_config->GetSetting("ldap_host");
		const char *const ldap_port = m_config->GetSetting("ldap_port");
		const char *const ldap_proto = m_config->GetSetting("ldap_protocol");

		std::string url;

		if (ldap_proto != NULL && strcmp(ldap_proto, "ldaps") == 0)
			url = format("ldaps://%s:%s", ldap_host, ldap_port);
		else
			url = format("ldap://%s:%s", ldap_host, ldap_port);
		ldap_servers.emplace_back(std::move(url));
	}

	if (ldap_servers.empty())
		throw ldap_error(string("No LDAP servers configured in ldap.cfg"));
	m_timeout.tv_sec = atoui(m_config->GetSetting("ldap_network_timeout"));
	m_timeout.tv_usec = 0;
}

void LDAPUserPlugin::InitPlugin()
{
	const char *ldap_binddn = m_config->GetSetting("ldap_bind_user");
	const char *ldap_bindpw = m_config->GetSetting("ldap_bind_passwd");

	/* FIXME encode the user and password, now it depends on which charset the config is saved in */
	m_ldap = ConnectLDAP(ldap_binddn, ldap_bindpw);

	const char *ldap_server_charset = m_config->GetSetting("ldap_server_charset");
	m_iconv.reset(new ECIConv("UTF-8", ldap_server_charset));
	if (!m_iconv -> canConvert())
		throw ldap_error(format("Cannot convert %s to UTF8", ldap_server_charset));
	m_iconvrev.reset(new ECIConv(m_config->GetSetting("ldap_server_charset"), "UTF-8"));
	if (!m_iconvrev -> canConvert())
		throw ldap_error(format("Cannot convert UTF-8 to %s", ldap_server_charset));
}

LDAP *LDAPUserPlugin::ConnectLDAP(const char *bind_dn, const char *bind_pw) {
	int rc = -1;
	LDAP *ld = NULL;
	LONGLONG llelapsedtime = 0;
	auto tstart = std::chrono::steady_clock::now();

	if ((bind_dn && bind_dn[0] != 0) && (bind_pw == NULL || bind_pw[0] == 0)) {
		// Username specified, but no password. Apparently, OpenLDAP will attempt
		// an anonymous bind when this is attempted. We therefore disallow this
		// to make sure you can authenticate a user's password with this function
		throw ldap_error(string("Disallowing NULL password for user ") + bind_dn);
	}

	// Initialize LDAP struct
	for (unsigned long int loop = 0; loop < ldap_servers.size(); ++loop) {
		const int limit = 0;
		const int version = LDAP_VERSION3;
		std::string currentServer = ldap_servers.at(ldapServerIndex);

		ulock_normal biglock(m_plugin_lock);
		rc = ldap_initialize(&ld, currentServer.c_str());
		biglock.unlock();

		if (rc != LDAP_SUCCESS) {
			m_lpStatsCollector->Increment(SCN_LDAP_CONNECT_FAILED);

			ec_log_crit("Failed to initialize LDAP for \"%s\": %s", currentServer.c_str(), ldap_err2string(rc));
			goto fail2;
		}

		LOG_PLUGIN_DEBUG("Trying to connect to %s", currentServer.c_str());

		if ((rc = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version)) != LDAP_OPT_SUCCESS) {
			ec_log_err("LDAP_OPT_PROTOCOL_VERSION failed: %s", ldap_err2string(rc));
			goto fail;
		}

		// Disable response message size restrictions (but the server's
		// restrictions still apply)
		if ((rc = ldap_set_option(ld, LDAP_OPT_SIZELIMIT, &limit)) != LDAP_OPT_SUCCESS) {
			ec_log_err("LDAP_OPT_SIZELIMIT failed: %s", ldap_err2string(rc));
			goto fail;
		}

		// Search referrals are never accepted  - FIXME maybe needs config option
		if ((rc = ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF)) != LDAP_OPT_SUCCESS) {
			ec_log_err("LDAP_OPT_REFERRALS failed: %s", ldap_err2string(rc));
			goto fail;
		}

		// Set network timeout (for connect)
		if ((rc = ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &m_timeout)) != LDAP_OPT_SUCCESS) {
			ec_log_err("LDAP_OPT_NETWORK_TIMEOUT failed: %s", ldap_err2string(rc));
			goto fail;
		}

		// Bind
		// For these two values: if they are both NULL, anonymous bind
		// will be used (ldap_binddn, ldap_bindpw)
		LOG_PLUGIN_DEBUG("Issuing LDAP bind");
		if ((rc = ldap_simple_bind_s(ld, (char *)bind_dn, (char *)bind_pw)) == LDAP_SUCCESS)
			break;
		ec_log_warn("LDAP (simple) bind on %s failed: %s", bind_dn, ldap_err2string(rc));
	fail:
		if (ldap_unbind_s(ld) == -1)
			ec_log_err("LDAP unbind failed");
	fail2:
		// see if another (if any) server does work
		++ldapServerIndex;
		if (ldapServerIndex >= ldap_servers.size())
			ldapServerIndex = 0;
		m_lpStatsCollector->Increment(SCN_LDAP_CONNECT_FAILED);

		ld = NULL;

		if (loop == ldap_servers.size() - 1)
			throw ldap_error("Failure connecting any of the LDAP servers");
	}

	llelapsedtime = dur2ms(decltype(tstart)::clock::now() - tstart);
	m_lpStatsCollector->Increment(SCN_LDAP_CONNECTS);
	m_lpStatsCollector->Increment(SCN_LDAP_CONNECT_TIME, llelapsedtime);
	m_lpStatsCollector->Max(SCN_LDAP_CONNECT_TIME_MAX, llelapsedtime);

	LOG_PLUGIN_DEBUG("ldaptiming [%08.2f] connected to ldap", llelapsedtime / 1000000.0);

	return ld;
}

LDAPUserPlugin::~LDAPUserPlugin() {
	// Disconnect from the LDAP server
	if (m_ldap) {
		LOG_PLUGIN_DEBUG("%s", "Disconnecting from LDAP since unloading plugin instance");

		if (ldap_unbind_s(m_ldap) == -1)
			ec_log_err("LDAP unbind failed");
	}
}

void LDAPUserPlugin::my_ldap_search_s(char *base, int scope, char *filter, char *attrs[], int attrsonly, LDAPMessage **lppres, LDAPControl **serverControls)
{
	int result=LDAP_SUCCESS;
	string req;
	LONGLONG llelapsedtime;
	auto_free_ldap_message res;
	auto tstart = std::chrono::steady_clock::now();

	if (attrs != NULL)
		for (unsigned int i = 0; attrs[i] != NULL; ++i)
			req += string(attrs[i]) + " ";

	// filter must be NULL to request everything (becomes (objectClass=*) in ldap library)
	if (filter[0] == '\0') {
		assert(scope == LDAP_SCOPE_BASE);
		filter = NULL;
	}

	/*
	 * When an m_ldap connection object already exists, use it to make
	 * a query, and, if that fails for any reason, reconnect-and-retry
	 * exactly once.
	 * When no m_ldap connection object existed, this boils down to
	 * one standard connect plus query.
	 */
	if (m_ldap != NULL)
		result = ldap_search_ext_s(m_ldap, base, scope, filter, attrs,
		         attrsonly, serverControls, nullptr, &m_timeout, 0, &~res);

	if (m_ldap == NULL || LDAP_API_ERROR(result)) {
		const char *ldap_binddn = m_config->GetSetting("ldap_bind_user");
		const char *ldap_bindpw = m_config->GetSetting("ldap_bind_passwd");

		if (m_ldap != NULL) {
			ec_log_err("LDAP search error: %s. Will unbind, reconnect and retry.", ldap_err2string(result));
			if (ldap_unbind_s(m_ldap) == -1)
				ec_log_err("LDAP unbind failed");
			m_ldap = NULL;
		}

		/// @todo encode the user and password, now it's depended in which charset the config is saved
		m_ldap = ConnectLDAP(ldap_binddn, ldap_bindpw);

		m_lpStatsCollector->Increment(SCN_LDAP_RECONNECTS);
		result = ldap_search_ext_s(m_ldap, base, scope, filter, attrs,
		          attrsonly, serverControls, nullptr, nullptr, 0, &~res);
	}

	if(result != LDAP_SUCCESS) {
		ec_log_err("LDAP query in \"%s\" failed: %s (result=0x%02x, %s)", base, filter, result, ldap_err2string(result));

		if(LDAP_API_ERROR(result)) {
		    // Some kind of API error occurred (error is not from the server). Unbind the connection so any next try will re-bind
		    // which will possibly connect to a different (failed over) server.
			if (m_ldap != NULL) {
				ec_log_err("Unbinding from LDAP because of continued error (%s)", ldap_err2string(result));
				if (ldap_unbind_s(m_ldap) == -1)
					ec_log_err("LDAP unbind failed");
				m_ldap = NULL;
			}
		}
		goto exit;
	}

	llelapsedtime = dur2ms(decltype(tstart)::clock::now() - tstart);
	LOG_PLUGIN_DEBUG("ldaptiming [%08.2f] (\"%s\" \"%s\" %s), results: %d", llelapsedtime/1000000.0, base, filter, req.c_str(), ldap_count_entries(m_ldap, res));

	*lppres = res.release(); // deref the pointer from object

	m_lpStatsCollector->Increment(SCN_LDAP_SEARCH);
	m_lpStatsCollector->Increment(SCN_LDAP_SEARCH_TIME, llelapsedtime);
	m_lpStatsCollector->Max(SCN_LDAP_SEARCH_TIME_MAX, llelapsedtime);

exit:
	if (result != LDAP_SUCCESS) {
		m_lpStatsCollector->Increment(SCN_LDAP_SEARCH_FAILED);

		// throw ldap error
		throw ldap_error(string("ldap_search_ext_s: ") + ldap_err2string(result), result);
	}

	// In rare situations ldap_search_s can return LDAP_SUCCESS, but leave res at NULL. This
	// seems to happen when the connection to the server is lost at a very specific time.
	// The problem is that libldap checks its input parameters with an assert. So the next
	// call to ldap_find_first will cause an assertion to kick in because the result from
	// ldap_search_s is inconsistent.
	// The easiest way around this is to net let this function return with a NULL result.
	else if (*lppres == NULL) {
		m_lpStatsCollector->Increment(SCN_LDAP_SEARCH_FAILED);
		throw ldap_error("ldap_search_ext_s: spurious NULL result");
	}
}

std::list<std::string> LDAPUserPlugin::GetClasses(const char *lpszClasses)
{
	std::list<std::string> lstClasses;
	for (const auto &s : tokenize(lpszClasses, ','))
		lstClasses.emplace_back(trim(s));
	return lstClasses;
}

bool LDAPUserPlugin::MatchClasses(std::set<std::string> setClasses, std::list<std::string> lstClasses)
{
	for (const auto &cls : lstClasses)
		if (setClasses.find(strToUpper(cls)) == setClasses.cend())
			return false;
	return true;
}

std::string LDAPUserPlugin::GetObjectClassFilter(const char *lpszObjectClassAttr, const char *lpszClasses)
{
	std::list<std::string> lstObjectClasses = GetClasses(lpszClasses);
	if(lstObjectClasses.size() == 0) {
		return "";
	}
	else if(lstObjectClasses.size() == 1) {
		return (std::string)"(" + lpszObjectClassAttr + "=" + lstObjectClasses.front() + ")";
	}
	else {
		std::string filter = "(&";
		for (const auto &cls : lstObjectClasses)
			filter += (std::string)"(" + lpszObjectClassAttr + "=" + cls + ")";
		filter += ")";
		return filter;
	}
}

objectid_t LDAPUserPlugin::GetObjectIdForEntry(LDAPMessage *entry)
{
	list<string>	objclasses;
	std::string nonactive_type, resource_type, security_type;
	std::string user_unique, group_unique, company_unique;
	std::string addresslist_unique, dynamicgroup_unique, object_uid;

	const char *class_attr = m_config->GetSetting("ldap_object_type_attribute");
	const char *nonactive_attr = m_config->GetSetting("ldap_nonactive_attribute");
	const char *resource_attr = m_config->GetSetting("ldap_resource_type_attribute");
	const char *security_attr = m_config->GetSetting("ldap_group_security_attribute");
	const char *security_attr_type = m_config->GetSetting("ldap_group_security_attribute_type");

	const char *user_unique_attr = m_config->GetSetting("ldap_user_unique_attribute");
	const char *group_unique_attr = m_config->GetSetting("ldap_group_unique_attribute");
	const char *company_unique_attr = m_config->GetSetting("ldap_company_unique_attribute");
	const char *addresslist_unique_attr = m_config->GetSetting("ldap_addresslist_unique_attribute");
	const char *dynamicgroup_unique_attr = m_config->GetSetting("ldap_dynamicgroup_unique_attribute");

	const char *class_user_type = m_config->GetSetting("ldap_user_type_attribute_value");
	const char *class_contact_type = m_config->GetSetting("ldap_contact_type_attribute_value");
	const char *class_group_type = m_config->GetSetting("ldap_group_type_attribute_value");
	const char *class_company_type = m_config->GetSetting("ldap_company_type_attribute_value");
	const char *class_address_type = m_config->GetSetting("ldap_addresslist_type_attribute_value");
	const char *class_dynamic_type = m_config->GetSetting("ldap_dynamicgroup_type_attribute_value");

	FOREACH_ATTR(entry) {
		if (class_attr && strcasecmp(att, class_attr) == 0)
			objclasses = getLDAPAttributeValues(att, entry);

		if (nonactive_attr != nullptr && strcasecmp(att, nonactive_attr) == 0)
			nonactive_type = getLDAPAttributeValue(att, entry);
		if (resource_attr && strcasecmp(att, resource_attr) == 0)
			resource_type = getLDAPAttributeValue(att, entry);
		if (security_attr != nullptr && strcasecmp(att, security_attr) == 0)
			security_type = getLDAPAttributeValue(att, entry);
		if (user_unique_attr && strcasecmp(att, user_unique_attr) == 0)
			user_unique = getLDAPAttributeValue(att, entry);

		if (group_unique_attr && strcasecmp(att, group_unique_attr) == 0)
			group_unique = getLDAPAttributeValue(att, entry);

		if (company_unique_attr && strcasecmp(att, company_unique_attr) == 0)
			company_unique = getLDAPAttributeValue(att, entry);

		if (addresslist_unique_attr && strcasecmp(att, addresslist_unique_attr) == 0)
			addresslist_unique = getLDAPAttributeValue(att, entry);

		if (dynamicgroup_unique_attr && strcasecmp(att, dynamicgroup_unique_attr) == 0)
			dynamicgroup_unique = getLDAPAttributeValue(att, entry);
	}
	END_FOREACH_ATTR

	// All object classes for a certain object
	std::set<std::string> setObjectClasses;

	// List of matching Kopano object classes
	std::list<std::pair<unsigned int, objectclass_t> > lstMatches;
	/*
	 * Find the Kopano object type by looking at the object classes.
	 *
	 * We do this by checking the best-match of objectclasses; if an LDAP objectClass is
	 * listed for multiple Kopano object classes, we use the following rules:
	 *
	 * First, the Kopano object class with the most matching LDAP objectClasses is selected.
	 * If that does not resolve the ambiguity, then object classes with higher numerical values
	 * override those with lower numerical values (most importantly, contacts override users)
	 */
	for (const auto &i : objclasses)
		setObjectClasses.emplace(strToUpper(i));

	auto lstLDAPObjectClasses = GetClasses(class_user_type);
	if(MatchClasses(setObjectClasses, lstLDAPObjectClasses))
		lstMatches.emplace_back(lstLDAPObjectClasses.size(), OBJECTCLASS_USER); // Could still be active or nonactive, will resolve later

	lstLDAPObjectClasses = GetClasses(class_contact_type);
	if(MatchClasses(setObjectClasses, lstLDAPObjectClasses))
		lstMatches.emplace_back(lstLDAPObjectClasses.size(), NONACTIVE_CONTACT);

	lstLDAPObjectClasses = GetClasses(class_group_type);
	if(MatchClasses(setObjectClasses, lstLDAPObjectClasses))
		lstMatches.emplace_back(lstLDAPObjectClasses.size(), OBJECTCLASS_DISTLIST); // Could be permission or distribution group, will resolve later

	lstLDAPObjectClasses = GetClasses(class_dynamic_type);
	if(MatchClasses(setObjectClasses, lstLDAPObjectClasses))
		lstMatches.emplace_back(lstLDAPObjectClasses.size(), DISTLIST_DYNAMIC);

	lstLDAPObjectClasses = GetClasses(class_company_type);
	if(MatchClasses(setObjectClasses, lstLDAPObjectClasses))
		lstMatches.emplace_back(lstLDAPObjectClasses.size(), CONTAINER_COMPANY);

	lstLDAPObjectClasses = GetClasses(class_address_type);
	if(MatchClasses(setObjectClasses, lstLDAPObjectClasses))
		lstMatches.emplace_back(lstLDAPObjectClasses.size(), CONTAINER_ADDRESSLIST);

	// lstMatches now contains all the kopano object classes that the object COULD be, now sort by number of object classes

	if(lstMatches.empty())
		throw data_error("Unable to detect object class for object: " + GetLDAPEntryDN(entry));

	lstMatches.sort();
	lstMatches.reverse();

	// Class we want is now at the top of the list (since we sorted by number of matches, then by class id)
	auto objclass = lstMatches.front().second;

	// Subspecify some generic types now
	if (objclass == OBJECTCLASS_USER) {
		if (atoi(nonactive_type.c_str()) != 0)
			objclass = NONACTIVE_USER;
		else
			objclass = ACTIVE_USER;

		if (objclass == NONACTIVE_USER && !resource_type.empty()) {
			/* Overwrite objectclass, a resource is allowed to overwrite the nonactive type */
			if (strcasecmp(resource_type.c_str(), "room") == 0)
				objclass = NONACTIVE_ROOM;
			else if (strcasecmp(resource_type.c_str(), "equipment") == 0)
				objclass = NONACTIVE_EQUIPMENT;
		}

		object_uid = user_unique;

	}

	if (objclass == NONACTIVE_CONTACT)
		object_uid = user_unique;
	if (objclass == OBJECTCLASS_DISTLIST) {
		if (!strcasecmp(security_attr_type, "ads")) {
			if(atoi(security_type.c_str()) & 0x80000000)
				objclass = DISTLIST_SECURITY;
			else
				objclass = DISTLIST_GROUP;
		} else {
			if (parseBool(security_type.c_str()))
				objclass = DISTLIST_SECURITY;
			else
				objclass = DISTLIST_GROUP;
		}
		object_uid = group_unique;
	}

	if (objclass == DISTLIST_DYNAMIC)
		object_uid = dynamicgroup_unique;
	if (objclass == CONTAINER_COMPANY)
		object_uid = company_unique;
	if (objclass == CONTAINER_ADDRESSLIST)
		object_uid = addresslist_unique;
	return objectid_t(object_uid, objclass);
}

signatures_t LDAPUserPlugin::getAllObjectsByFilter(const std::string &basedn,
    int scope, const std::string &search_filter,
    const std::string &strCompanyDN, bool bCache)
{
	signatures_t signatures;
	objectid_t				objectid;
	string					signature;
	std::map<objectclass_t, dn_cache_t> mapDNCache;
	dn_list_t dnFilter;
	auto_free_ldap_message res;

	/*
	 * When working in multi-company mode we need to determine if the found object
	 * is really a member of the requested company and not turned up in the
	 * result because he is member of a subcompany.
	 * Create a filter by requesting all subcompanies for he given company,
	 * and remove any returned objects which are member of these subcompanies.
	 */
	if (m_bHosted && !strCompanyDN.empty())
		dnFilter = m_lpCache->getChildrenForDN(m_lpCache->getObjectDNCache(this, CONTAINER_COMPANY), strCompanyDN);

	std::unique_ptr<attrArray> request_attrs(new attrArray(15));

	/* Needed for GetObjectIdForEntry() */
	CONFIG_TO_ATTR(request_attrs, class_attr, "ldap_object_type_attribute");
	CONFIG_TO_ATTR(request_attrs, nonactive_attr, "ldap_nonactive_attribute");
	CONFIG_TO_ATTR(request_attrs, resource_attr, "ldap_resource_type_attribute");
	CONFIG_TO_ATTR(request_attrs, security_attr, "ldap_group_security_attribute");
	CONFIG_TO_ATTR(request_attrs, user_unique_attr, "ldap_user_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, group_unique_attr, "ldap_group_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, company_unique_attr, "ldap_company_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, addresslist_unique_attr, "ldap_addresslist_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, dynamicgroup_unique_attr, "ldap_dynamicgroup_unique_attribute");
	/* Needed for cache */
	CONFIG_TO_ATTR(request_attrs, modify_attr, "ldap_last_modification_attribute");

	FOREACH_PAGING_SEARCH((char *)basedn.c_str(), scope,
						  (char *)search_filter.c_str(), (char **)request_attrs->get(),
						  FETCH_ATTR_VALS, res)
	{
		FOREACH_ENTRY(res) {
			auto dn = GetLDAPEntryDN(entry);

			/* Make sure the DN isn't filtered because it is located in the subcontainer */
			if (m_bHosted && !strCompanyDN.empty() &&
			    m_lpCache->isDNInList(dnFilter, dn))
				continue;

			FOREACH_ATTR(entry) {
				if (modify_attr && strcasecmp(att, modify_attr) == 0)
					signature = getLDAPAttributeValue(att, entry);
			}
			END_FOREACH_ATTR

			try {
				objectid = GetObjectIdForEntry(entry);
			} catch(data_error &e) {
				ec_log_warn("Unable to get object id: %s", e.what());
				continue;
			}

			if (objectid.id.empty()) {
				ec_log_warn("Unique id not found for DN: %s", dn.c_str());
				continue;
			}
			signatures.emplace_back(objectid, signature);
			if (bCache) {
				auto retval = mapDNCache.emplace(objectid.objclass, dn_cache_t());
				auto iterDNCache = retval.first;
				iterDNCache->second.emplace(objectid, dn);
			}
		}
		END_FOREACH_ENTRY
	}
	END_FOREACH_LDAP_PAGING

	/* Update cache */
	for (auto &p : mapDNCache)
		m_lpCache->setObjectDNCache(p.first, std::move(p.second));
	return signatures;
}

string LDAPUserPlugin::getSearchBase(const objectid_t &company)
{
	const char *lpszSearchBase = m_config->GetSetting("ldap_search_base");

	if (lpszSearchBase == nullptr)
		/* GetSetting returns "" for all options it knows.. */
		throw std::logic_error("getSearchBase: unexpected nullptr");
	if (!m_bHosted || company.id.empty())
		return lpszSearchBase;

	// find company DN, and use as search_base
	auto search_base = m_lpCache->getDNForObject(m_lpCache->getObjectDNCache(this, company.objclass), company);
	// CHECK: should not be possible to not already know the company
	if (!search_base.empty())
		return search_base;
	ec_log_crit("No search base found for company \"%s\"", company.id.c_str());
	return lpszSearchBase;
}

string LDAPUserPlugin::getServerSearchFilter()
{
	const char *objecttype = m_config->GetSetting("ldap_object_type_attribute", "", NULL);
	const char *servertype = m_config->GetSetting("ldap_server_type_attribute_value", "", NULL);
	const char *serverfilter = m_config->GetSetting("ldap_server_search_filter");

	if (!objecttype)
		throw runtime_error("No object type attribute defined");

	if (!servertype)
		throw runtime_error("No server type attribute value defined");

	std::string filter = serverfilter;
	auto subfilter = "(" + string(objecttype) + "=" + servertype + ")";

	if (!filter.empty())
		return "(&(|" + filter + ")" + subfilter + ")";
	return subfilter;
}

string LDAPUserPlugin::getSearchFilter(objectclass_t objclass)
{
	string filter, subfilter;
	const char *objecttype = m_config->GetSetting("ldap_object_type_attribute", "", NULL);
	const char *usertype = m_config->GetSetting("ldap_user_type_attribute_value", "", NULL);
	const char *contacttype = m_config->GetSetting("ldap_contact_type_attribute_value", "", NULL);
	const char *grouptype = m_config->GetSetting("ldap_group_type_attribute_value", "", NULL);
	const char *companytype = m_config->GetSetting("ldap_company_type_attribute_value", "", NULL);
	const char *addresslisttype = m_config->GetSetting("ldap_addresslist_type_attribute_value", "", NULL);
	const char *dynamicgrouptype = m_config->GetSetting("ldap_dynamicgroup_type_attribute_value", "", NULL);
	const char *userfilter = m_config->GetSetting("ldap_user_search_filter");
	const char *groupfilter = m_config->GetSetting("ldap_group_search_filter");
	const char *companyfilter = m_config->GetSetting("ldap_company_search_filter");
	const char *addresslistfilter = m_config->GetSetting("ldap_addresslist_search_filter");
	const char *dynamicgroupfilter = m_config->GetSetting("ldap_dynamicgroup_search_filter");

	if (!objecttype)
		throw runtime_error("No object type attribute defined");

	switch (objclass) {
	case OBJECTCLASS_UNKNOWN:
		/* No objectclass is given, merge all filters together */
		subfilter = getSearchFilter(OBJECTCLASS_USER);
		if (contacttype)
			subfilter += getSearchFilter(NONACTIVE_CONTACT);
		subfilter += getSearchFilter(OBJECTCLASS_DISTLIST);
		subfilter += getSearchFilter(OBJECTCLASS_CONTAINER);
		subfilter = "(|" + subfilter + ")";
		break;
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
		if (!usertype)
			throw runtime_error("No user type attribute value defined");
		filter = userfilter;
		subfilter += "(|";
		subfilter += GetObjectClassFilter(objecttype, usertype);
		/* Generic user type should not exclude Contacts */
		if (objclass == OBJECTCLASS_USER && contacttype)
			subfilter += GetObjectClassFilter(objecttype, contacttype);
		subfilter += ")";
		break;
	case NONACTIVE_CONTACT:
		if (!contacttype)
			throw runtime_error("No contact type attribute value defined");
		filter = userfilter;
		subfilter = GetObjectClassFilter(objecttype, contacttype);
		break;
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		if ((grouptype || (groupfilter && groupfilter[0] != '\0')) && (dynamicgrouptype || (dynamicgroupfilter && dynamicgroupfilter[0] != '\0')))
			subfilter = "(|";

		if (grouptype && groupfilter && groupfilter[0] != '\0')
			subfilter += string("(&") + GetObjectClassFilter(objecttype, grouptype) + groupfilter + ")";
		else if (grouptype)
			subfilter += GetObjectClassFilter(objecttype, grouptype);
		else if (groupfilter && groupfilter[0] != '\0')
			subfilter += groupfilter;

		if (dynamicgrouptype && dynamicgroupfilter && dynamicgroupfilter[0] != '\0')
			subfilter += string("(&") + GetObjectClassFilter(objecttype, dynamicgrouptype) + dynamicgroupfilter + ")";
		else if (dynamicgrouptype)
			subfilter += GetObjectClassFilter(objecttype, dynamicgrouptype);
		else if (dynamicgroupfilter && dynamicgroupfilter[0] != '\0')
			subfilter += dynamicgroupfilter;

		if ((grouptype || (groupfilter && groupfilter[0] != '\0')) && (dynamicgrouptype || (dynamicgroupfilter && dynamicgroupfilter[0] != '\0')))
			subfilter += ")";
		break;
	case OBJECTCLASS_CONTAINER:
		subfilter = "(|";
		if (m_bHosted) {
			if (!companytype)
				throw runtime_error("No company type attribute value defined");
			subfilter += string("(&") + companyfilter + GetObjectClassFilter(objecttype, companytype) + ")";
		}
		if (addresslisttype)
			subfilter += string("(&") + addresslistfilter + GetObjectClassFilter(objecttype, addresslisttype) + ")";
		else
			subfilter += addresslistfilter;
		subfilter += ")";
		break;
	case CONTAINER_COMPANY:
		if (!m_bHosted)
			throw runtime_error("Searching for companies is not supported in singlecompany server");
		if (!companytype)
			throw runtime_error("No company type attribute value defined");
		filter = companyfilter;
		subfilter = GetObjectClassFilter(objecttype, companytype);
		break;
	case CONTAINER_ADDRESSLIST:
		if (!addresslisttype)
			throw runtime_error("No addresslist type attribute value defined");
		filter = addresslistfilter;
		subfilter = GetObjectClassFilter(objecttype, addresslisttype);
		break;
	default:
		throw runtime_error("Unknown object type " + stringify(objclass));
	}

	if (!filter.empty())
		return "(&" + filter + subfilter + ")";
	return subfilter;
}

string LDAPUserPlugin::getSearchFilter(const string &data, const char *attr, const char *attr_type)
{
	string search_data;

	// Set binary uniqueid to escaped string
	if(attr_type && strcasecmp(attr_type, LDAP_DATA_TYPE_BINARY) == 0)
		BintoEscapeSequence(data.c_str(), data.size(), &search_data);
	else
		search_data = StringEscapeSequence(data);

	if (attr)
		return "(" + string(attr) + "=" + search_data + ")";
	return "";
}

string LDAPUserPlugin::getObjectSearchFilter(const objectid_t &id, const char *attr, const char *attr_type)
{
	if (attr)
		return "(&" + getSearchFilter(id.objclass) + getSearchFilter(id.id, attr, attr_type) + ")";

	switch (id.objclass) {
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		return getObjectSearchFilter(id,
			m_config->GetSetting("ldap_user_unique_attribute"),
			m_config->GetSetting("ldap_user_unique_attribute_type"));
	case OBJECTCLASS_DISTLIST:
		return
			"(&" +
				getSearchFilter(id.objclass) +
				"(|" +
					getSearchFilter(id.id,
						m_config->GetSetting("ldap_group_unique_attribute"),
						m_config->GetSetting("ldap_group_unique_attribute_type")) +
					getSearchFilter(id.id,
						m_config->GetSetting("ldap_dynamicgroup_unique_attribute"),
						m_config->GetSetting("ldap_dynamicgroup_unique_attribute_type")) +
				"))";
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
		return getObjectSearchFilter(id,
			m_config->GetSetting("ldap_group_unique_attribute"),
			m_config->GetSetting("ldap_group_unique_attribute_type"));
	case DISTLIST_DYNAMIC:
		return getObjectSearchFilter(id,
			m_config->GetSetting("ldap_dynamicgroup_unique_attribute"),
			m_config->GetSetting("ldap_dynamicgroup_unique_attribute_type"));
	case OBJECTCLASS_CONTAINER:
		return
			"(&" +
				getSearchFilter(id.objclass) +
				"(|" +
					getSearchFilter(id.id,
						m_config->GetSetting("ldap_company_unique_attribute"),
						m_config->GetSetting("ldap_company_unique_attribute_type")) +
					getSearchFilter(id.id,
						m_config->GetSetting("ldap_addresslist_unique_attribute"),
						m_config->GetSetting("ldap_addresslist_unique_attribute_type")) +
				"))";
	case CONTAINER_COMPANY:
		return getObjectSearchFilter(id,
			m_config->GetSetting("ldap_company_unique_attribute"),
			m_config->GetSetting("ldap_company_unique_attribute_type"));
	case CONTAINER_ADDRESSLIST:
		return getObjectSearchFilter(id,
			m_config->GetSetting("ldap_addresslist_unique_attribute"),
			m_config->GetSetting("ldap_addresslist_unique_attribute_type"));
	case OBJECTCLASS_UNKNOWN:
	default:
		throw runtime_error("Object is wrong type");
	}
}

string LDAPUserPlugin::objectUniqueIDtoAttributeData(const objectid_t &uniqueid, const char* lpAttr)
{
	auto_free_ldap_message res;
	string			strData;
	bool			bDataAttrFound = false;

	string ldap_basedn = getSearchBase();
	string ldap_filter = getObjectSearchFilter(uniqueid);

	char *request_attrs[] = {
		(char*)lpAttr,
		NULL
	};

	if (lpAttr == NULL)
		throw runtime_error("Cannot convert uniqueid to unknown attribute");

	my_ldap_search_s(
			(char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
			(char *)ldap_filter.c_str(),
			request_attrs, FETCH_ATTR_VALS, &~res);

	switch(ldap_count_entries(m_ldap, res)) {
	case 0:
		throw objectnotfound(ldap_filter);
	case 1:
		break;
	default:
		throw toomanyobjects(string("More than one object returned in search ") + ldap_filter);
	}

	auto entry = ldap_first_entry(m_ldap, res);
	if (entry == nullptr)
		throw runtime_error("ldap_dn: broken.");

	FOREACH_ATTR(entry) {
		if (strcasecmp(att, lpAttr) == 0) {
			strData = getLDAPAttributeValue(att, entry);
			bDataAttrFound = true;
		}
	}
	END_FOREACH_ATTR

	if(bDataAttrFound == false)
		throw data_error(string(lpAttr)+" attribute not found");

	return strData;
}

string LDAPUserPlugin::objectUniqueIDtoObjectDN(const objectid_t &uniqueid, bool cache)
{
	auto lpCache = m_lpCache->getObjectDNCache(this, uniqueid.objclass);
	auto_free_ldap_message res;
	string			dn;
	LDAPMessage*	entry = NULL;

	/*
	 * The cache should actually contain this entry, search for the uniqueid in there first.
	 * In the rare case that the cache didn't contain the entry, check LDAP.
	 */
	if (cache) {
		dn = m_lpCache->getDNForObject(lpCache, uniqueid);
		if (!dn.empty())
			return dn;
	}

	/*
	 * That's odd, the cache didn't contain the uniqueid. Perform a LDAP query
	 * to search for the object, but chances are high the object doesn't exist at all.
	 *
	 * Except if we skipped the cache as per ZCP-11720, where we always
	 * want to issue an LDAP query.
	 */
	string			ldap_basedn = getSearchBase();
	string			ldap_filter = getObjectSearchFilter(uniqueid);
	std::unique_ptr<attrArray> request_attrs(new attrArray(1));

	request_attrs->add("dn");

	my_ldap_search_s(
			 (char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
			 (char *)ldap_filter.c_str(), (char **)request_attrs->get(),
			 DONT_FETCH_ATTR_VALS, &~res);

	switch(ldap_count_entries(m_ldap, res)) {
	case 0:
		throw objectnotfound(ldap_filter);
	case 1:
		break;
	default:
		throw toomanyobjects(string("More than one object returned in search ") + ldap_filter);
	}

	entry = ldap_first_entry(m_ldap, res);
	if (entry == nullptr)
		throw runtime_error("ldap_dn: broken.");
	dn = GetLDAPEntryDN(entry);

	return dn;
}

objectsignature_t LDAPUserPlugin::objectDNtoObjectSignature(objectclass_t objclass, const string &dn)
{
	auto ldap_filter = getSearchFilter(objclass);
	auto signatures = getAllObjectsByFilter(dn, LDAP_SCOPE_BASE, ldap_filter, string(), false);
	if (signatures.empty())
		throw objectnotfound(dn);
	else if (signatures.size() != 1)
		throw toomanyobjects("More than one object returned in search for DN " + dn);
	return signatures.front();
}

signatures_t
LDAPUserPlugin::objectDNtoObjectSignatures(objectclass_t objclass,
    const std::list<std::string> &dn)
{
	signatures_t signatures;

	for (const auto &i : dn) {
		try {
			signatures.emplace_back(objectDNtoObjectSignature(objclass, i));
		} catch (objectnotfound &e) {
			// resolve failed, drop entry
			continue;
		} catch (ldap_error &e) {
			if(LDAP_NAME_ERROR(e.GetLDAPError()))
				continue;
			throw;
		} catch (std::exception &e) {
			// query failed, drop entry
			continue;
		}
	}

	return signatures;
}

signatures_t
LDAPUserPlugin::resolveObjectsFromAttribute(objectclass_t objclass,
    const std::list<std::string> &objects, const char *lpAttr,
    const objectid_t &company)
{
	const char *lpAttrs[2] = {
		lpAttr,
		NULL,
	};

	return resolveObjectsFromAttributes(objclass, objects, lpAttrs, company);
}

signatures_t
LDAPUserPlugin::resolveObjectsFromAttributes(objectclass_t objclass,
    const std::list<std::string> &objects, const char **lppAttr,
    const objectid_t &company)
{
	string companyDN;

	if (lppAttr == NULL || lppAttr[0] == NULL)
		throw runtime_error("Unable to search for unknown attribute");

	auto ldap_basedn = getSearchBase(company);
	auto ldap_filter = getSearchFilter(objclass);
	if (!company.id.empty())
		companyDN = ldap_basedn; // in hosted, companyDN is the same as searchbase?

	ldap_filter = "(&" + ldap_filter + "(|";
	for (const auto &i : objects)
		for (unsigned int j = 0; lppAttr[j] != NULL; ++j)
			ldap_filter += "(" + string(lppAttr[j]) + "=" + StringEscapeSequence(i) + ")";
	ldap_filter += "))";

	return getAllObjectsByFilter(ldap_basedn, LDAP_SCOPE_SUBTREE, ldap_filter, companyDN, false);
}

objectsignature_t LDAPUserPlugin::resolveObjectFromAttributeType(objectclass_t objclass, const string &object, const char* lpAttr, const char* lpAttrType, const objectid_t &company)
{
	auto signatures = resolveObjectsFromAttributeType(objclass,
		std::list<std::string>{object}, lpAttr, lpAttrType, company);
	if (signatures.empty())
		throw objectnotfound(object+" not found in LDAP");
	return signatures.front();
}

signatures_t
LDAPUserPlugin::resolveObjectsFromAttributeType(objectclass_t objclass,
    const std::list<std::string> &objects, const char *lpAttr,
    const char *lpAttrType, const objectid_t &company)
{
	const char *lpAttrs[2] = {
		lpAttr,
		NULL,
	};

	return resolveObjectsFromAttributesType(objclass, objects, lpAttrs, lpAttrType, company);
}

signatures_t
LDAPUserPlugin::resolveObjectsFromAttributesType(objectclass_t objclass,
    const std::list<std::string> &objects, const char **lppAttr,
    const char *lpAttrType, const objectid_t &company)
{
	/* When the relation attribute the is the DN, we cannot perform any optimizations
	 * and we must incur the penalty of having to resolve each entry one by one.
	 * When the relation attribute is not the DN, we can optimize the lookup
	 * by creating a single query that obtains all the required data in a single query. */
	if (lpAttrType && strcasecmp(lpAttrType, LDAP_DATA_TYPE_DN) == 0)
		return objectDNtoObjectSignatures(objclass, objects);
	/* We have the full member list, create a new query that
	 * will request the unique modification attributes for all
	 * members in a single shot. With this data we can construct
	 * the list of object signatures */
	return resolveObjectsFromAttributes(objclass, objects, lppAttr, company);
}

objectsignature_t LDAPUserPlugin::resolveName(objectclass_t objclass, const string &name, const objectid_t &company)
{
	std::unique_ptr<attrArray> attrs(new attrArray(6));
	const char *loginname_attr = m_config->GetSetting("ldap_loginname_attribute", "", NULL);
	const char *groupname_attr = m_config->GetSetting("ldap_groupname_attribute", "", NULL);
	const char *dyngroupname_attr = m_config->GetSetting("ldap_dynamicgroupname_attribute", "", NULL);
	const char *companyname_attr = m_config->GetSetting("ldap_companyname_attribute", "", NULL);
	const char *addresslistname_attr = m_config->GetSetting("ldap_addresslist_name_attribute", "", NULL);

	if (company.id.empty())
		LOG_PLUGIN_DEBUG("%s Class %x, Name %s", __FUNCTION__, objclass, name.c_str());
	else
		LOG_PLUGIN_DEBUG("%s Class %x, Name %s, Company %s", __FUNCTION__, objclass, name.c_str(), company.id.c_str());

	switch (objclass) {
	case OBJECTCLASS_UNKNOWN:
		if (loginname_attr)
			attrs->add(loginname_attr);
		if (groupname_attr)
			attrs->add(groupname_attr);
		if (dyngroupname_attr)
			attrs->add(dyngroupname_attr);
		if (companyname_attr)
			attrs->add(companyname_attr);
		if (addresslistname_attr)
			attrs->add(addresslistname_attr);
		break;
	case OBJECTCLASS_USER:
		if (loginname_attr)
			attrs->add(loginname_attr);
		break;
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		if (loginname_attr)
			attrs->add(loginname_attr);
		break;
	case OBJECTCLASS_DISTLIST:
		if (groupname_attr)
			attrs->add(groupname_attr);
		if (dyngroupname_attr)
			attrs->add(dyngroupname_attr);
		break;
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
		if (groupname_attr)
			attrs->add(groupname_attr);
		break;
	case DISTLIST_DYNAMIC:
		if (dyngroupname_attr)
			attrs->add(dyngroupname_attr);
		break;
	case OBJECTCLASS_CONTAINER:
		if (companyname_attr)
			attrs->add(companyname_attr);
		if (addresslistname_attr)
			attrs->add(addresslistname_attr);
		break;
	case CONTAINER_COMPANY:
		if (companyname_attr)
			attrs->add(companyname_attr);
		break;
	case CONTAINER_ADDRESSLIST:
		if (addresslistname_attr)
			attrs->add(addresslistname_attr);
		break;
	default:
		throw runtime_error(string("resolveName: request for unknown object type"));
	}

	if (attrs->empty())
		throw runtime_error(string("Unable to resolve name with no attributes"));
	auto signatures = resolveObjectsFromAttributes(objclass,
		std::list<std::string>{m_iconvrev->convert(name)},
		attrs->get(), company);
	if (signatures.empty())
		throw objectnotfound(name+" not found in LDAP");

	// we can only resolve one name. caller should be more specific
	if (signatures.size() > 1)
		throw collision_error(name + " found " + stringify(signatures.size()) + " times in LDAP");
	if (!OBJECTCLASS_COMPARE(signatures.front().id.objclass, objclass))
		throw objectnotfound("No object has been found with name " + name);
	return signatures.front();
}

objectsignature_t LDAPUserPlugin::authenticateUser(const string &username, const string &password, const objectid_t &company)
{
	const char *authmethod = m_config->GetSetting("ldap_authentication_method");
	objectsignature_t id;
	auto tstart = std::chrono::steady_clock::now();

	try {
		if (strcasecmp(authmethod, "password") == 0)
			id = authenticateUserPassword(username, password, company);
		else
			id = authenticateUserBind(username, password, company);
	} catch (...) {
		m_lpStatsCollector->Increment(SCN_LDAP_AUTH_DENIED);
		throw;
	}

	auto llelapsedtime = dur2ms(decltype(tstart)::clock::now() - tstart);
	m_lpStatsCollector->Increment(SCN_LDAP_AUTH_LOGINS);
	m_lpStatsCollector->Increment(SCN_LDAP_AUTH_TIME, llelapsedtime);
	m_lpStatsCollector->Max(SCN_LDAP_AUTH_TIME_MAX, llelapsedtime);
	m_lpStatsCollector->Avg(SCN_LDAP_AUTH_TIME_AVG, llelapsedtime);

	return id;
}

objectsignature_t LDAPUserPlugin::authenticateUserBind(const string &username, const string &password, const objectid_t &company)
{
	LDAP*		ld = NULL;
	objectsignature_t	signature;

	try {
		signature = resolveName(ACTIVE_USER, username, company);
		/*
		 * ZCP-11720: When looking for users, explicitly request
		 * skipping the cache.
		 */
		auto dn = objectUniqueIDtoObjectDN(signature.id, false);
		ld = ConnectLDAP(dn.c_str(), m_iconvrev->convert(password).c_str());
	} catch (std::exception &e) {
		throw login_error((string)"Trying to authenticate failed: " + e.what() + (string)"; username = " + username);
	}
	if (ld == nullptr)
		throw runtime_error("Trying to authenticate failed: connection failed");
	if (ldap_unbind_s(ld) == -1)
		ec_log_err("LDAP unbind failed");

	return signature;
}

objectsignature_t LDAPUserPlugin::authenticateUserPassword(const string &username, const string &password, const objectid_t &company)
{
	auto_free_ldap_message res;
	objectdetails_t	d;
	objectsignature_t signature;

	std::unique_ptr<attrArray> request_attrs(new attrArray(4));
	CONFIG_TO_ATTR(request_attrs, loginname_attr, "ldap_loginname_attribute" );
	CONFIG_TO_ATTR(request_attrs, password_attr, "ldap_password_attribute");
	CONFIG_TO_ATTR(request_attrs, unique_attr, "ldap_user_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, nonactive_attr, "ldap_nonactive_attribute");
	auto ldap_basedn = getSearchBase(company);
	auto ldap_filter = getObjectSearchFilter(objectid_t(m_iconvrev->convert(username), ACTIVE_USER), loginname_attr);

	/* LDAP filter does not exist, user does not exist, user cannot login */
	if (ldap_filter.empty())
		throw objectnotfound("LDAP filter is empty");

	my_ldap_search_s(
			(char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
			(char *)ldap_filter.c_str(), (char **)request_attrs->get(),
			FETCH_ATTR_VALS, &~res);

	switch(ldap_count_entries(m_ldap, res)) {
	case 0:
		throw login_error("Trying to authenticate failed: wrong username or password");
	case 1:
		break;
	default:
		throw login_error("Trying to authenticate failed: unknown error");
	}

	auto entry = ldap_first_entry(m_ldap, res);
	if (entry == nullptr)
		throw runtime_error("ldap_dn: broken.");

	FOREACH_ATTR(entry) {
		if (loginname_attr != nullptr && strcasecmp(att, loginname_attr) == 0)
			d.SetPropString(OB_PROP_S_LOGIN, m_iconv->convert(getLDAPAttributeValue(att, entry)));
		else if (password_attr != nullptr && strcasecmp(att, password_attr) == 0)
			d.SetPropString(OB_PROP_S_PASSWORD, getLDAPAttributeValue(att, entry));

		if (unique_attr && !strcasecmp(att, unique_attr)) {
			signature.id.id = getLDAPAttributeValue(att, entry);
			signature.id.objclass = ACTIVE_USER; // only users can authenticate
		}
		if (nonactive_attr != nullptr && strcasecmp(att, nonactive_attr) == 0 &&
		    parseBool(getLDAPAttributeValue(att, entry).c_str()))
			throw login_error("Cannot login as nonactive user");
	}
	END_FOREACH_ATTR

	auto strCryptedpw = d.GetPropString(OB_PROP_S_PASSWORD);
	auto strPasswordConverted = m_iconvrev->convert(password);
	if (strCryptedpw.empty())
		throw login_error("Trying to authenticate failed: password field is empty or unreadable");
	if (signature.id.id.empty())
		throw login_error("Trying to authenticate failed: uniqueid is empty or unreadable");

	if (!strncasecmp("{CRYPT}", strCryptedpw.c_str(), 7)) {
		if(checkPassword(PASSWORD_CRYPT, strPasswordConverted.c_str(), &(strCryptedpw.c_str()[7])) != 0)
			throw login_error("Trying to authenticate failed: wrong username or password");
	} else if (!strncasecmp("{MD5}", strCryptedpw.c_str(), 5)) {
		if(checkPassword(PASSWORD_MD5, strPasswordConverted.c_str(), &(strCryptedpw.c_str()[5])) != 0)
			throw login_error("Trying to authenticate failed: wrong username or password");
	} else if (!strncasecmp("{SMD5}", strCryptedpw.c_str(), 6)) {
		if(checkPassword(PASSWORD_SMD5, strPasswordConverted.c_str(), &(strCryptedpw.c_str()[6])) != 0)
			throw login_error("Trying to authenticate failed: wrong username or password");
	} else if (!strncasecmp("{SSHA}", strCryptedpw.c_str(), 6)) {
		if(checkPassword(PASSWORD_SSHA, strPasswordConverted.c_str(), &(strCryptedpw.c_str()[6])) != 0)
			throw login_error("Trying to authenticate failed: wrong username or password");
	} else if (!strncasecmp("{SHA}", strCryptedpw.c_str(), 5)) {
		if(checkPassword(PASSWORD_SHA, strPasswordConverted.c_str(), &(strCryptedpw.c_str()[5])) != 0)
			throw login_error("Trying to authenticate failed: wrong username or password");
	} else if(!strncasecmp("{MD5CRYPT}", strCryptedpw.c_str(), 10)) {
		throw login_error("Trying to authenticate failed: unsupported encryption scheme");
	} else {
		if (strcmp(strCryptedpw.c_str(), strPasswordConverted.c_str()) != 0) //Plain password
			throw login_error("Trying to authenticate failed: wrong username or password");
	}

	return signature;
}

signatures_t
LDAPUserPlugin::getAllObjects(const objectid_t &company, objectclass_t objclass)
{
	string companyDN;
	if (!company.id.empty()) {
		LOG_PLUGIN_DEBUG("%s Company %s, Class %x", __FUNCTION__, company.id.c_str(), objclass);
		companyDN = getSearchBase(company);
	} else {
		LOG_PLUGIN_DEBUG("%s Class %x", __FUNCTION__, objclass);
	}
	return getAllObjectsByFilter(getSearchBase(company), LDAP_SCOPE_SUBTREE,
	       getSearchFilter(objclass), companyDN, true);
}

string LDAPUserPlugin::getLDAPAttributeValue(char *attribute, LDAPMessage *entry) {
	list<string> l = getLDAPAttributeValues(attribute, entry);
	return !l.empty() ? l.front() : std::string();
}

list<string> LDAPUserPlugin::getLDAPAttributeValues(char *attribute, LDAPMessage *entry) {
	list<string> r;
	string s;
	auto_free_ldap_berval berval(ldap_get_values_len(m_ldap, entry, attribute));

	if (berval == NULL)
		return r;
	for (int i = 0; berval[i] != NULL; ++i) {
		s.assign(berval[i]->bv_val, berval[i]->bv_len);
		r.emplace_back(std::move(s));
	}
	return r;
}

std::string LDAPUserPlugin::GetLDAPEntryDN(LDAPMessage *entry)
{
	auto_free_ldap_attribute ptrDN(ldap_get_dn(m_ldap, entry));
	if (*ptrDN != '\0')
		return ptrDN.get();
	return {};
}

struct postaction {
	objectid_t objectid;		//!< object to act on in the resolved map
	objectclass_t objclass;		//!< resolveObject(s)FromAttributeType 1st parameter
	string ldap_attr;			//!< resolveObjectFromAttributeType 2nd parameter
	list<string> ldap_attrs;	//!< resolveObjectsFromAttributeType 2nd parameter
	const char *relAttr;		//!< resolveObject(s)FromAttributeType 3rd parameter
	const char *relAttrType;	//!< resolveObject(s)FromAttributeType 4th parameter
	property_key_t propname;	//!< object prop to add/set from the result
	std::string result_attr;	//!< optional: attribute to use from resulting object(s), if none then unique id
};

std::map<objectid_t, objectdetails_t>
LDAPUserPlugin::getObjectDetails(const std::list<objectid_t> &objectids)
{
	std::map<objectid_t, objectdetails_t> mapdetails;
	auto_free_ldap_message res;

	LOG_PLUGIN_DEBUG("%s N=%d", __FUNCTION__, (int)objectids.size() );

	/* That was easy ... */
	if (objectids.empty())
		return mapdetails;

	bool			bCutOff = false;
	dn_cache_t lpCompanyCache;
	std::string ldap_filter;
	string						strDN;

	list<postaction> lPostActions;
	std::set<objectid_t> setObjectIds;
	list<configsetting_t>	lExtraAttrs = m_config->GetSettingGroup(CONFIGGROUP_PROPMAP);
	std::unique_ptr<attrArray> request_attrs(new attrArray(33 + lExtraAttrs.size()));

	CONFIG_TO_ATTR(request_attrs, object_attr, "ldap_object_type_attribute");
	CONFIG_TO_ATTR(request_attrs, user_unique_attr, "ldap_user_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, user_unique_attr_type, "ldap_user_unique_attribute_type");
	CONFIG_TO_ATTR(request_attrs, user_fullname_attr, "ldap_fullname_attribute");
	CONFIG_TO_ATTR(request_attrs, loginname_attr, "ldap_loginname_attribute");
	CONFIG_TO_ATTR(request_attrs, password_attr, "ldap_password_attribute");
	CONFIG_TO_ATTR(request_attrs, emailaddress_attr, "ldap_emailaddress_attribute");
	CONFIG_TO_ATTR(request_attrs, emailaliases_attr, "ldap_emailaliases_attribute");
	CONFIG_TO_ATTR(request_attrs, isadmin_attr, "ldap_isadmin_attribute");
	CONFIG_TO_ATTR(request_attrs, nonactive_attr, "ldap_nonactive_attribute");
	CONFIG_TO_ATTR(request_attrs, resource_type_attr, "ldap_resource_type_attribute");
	CONFIG_TO_ATTR(request_attrs, resource_capacity_attr, "ldap_resource_capacity_attribute");
	CONFIG_TO_ATTR(request_attrs, usercert_attr, "ldap_user_certificate_attribute");
	CONFIG_TO_ATTR(request_attrs, sendas_attr, "ldap_sendas_attribute");
	CONFIG_TO_ATTR(request_attrs, group_unique_attr, "ldap_group_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, group_unique_attr_type, "ldap_group_unique_attribute_type");
	CONFIG_TO_ATTR(request_attrs, group_fullname_attr, "ldap_groupname_attribute");
	CONFIG_TO_ATTR(request_attrs, group_security_attr, "ldap_group_security_attribute");
	CONFIG_TO_ATTR(request_attrs, company_unique_attr, "ldap_company_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, company_unique_attr_type, "ldap_company_unique_attribute_type");
	CONFIG_TO_ATTR(request_attrs, company_fullname_attr, "ldap_companyname_attribute");
	CONFIG_TO_ATTR(request_attrs, sysadmin_attr, "ldap_company_system_admin_attribute");
	CONFIG_TO_ATTR(request_attrs, sysadmin_attr_type, "ldap_company_system_admin_attribute_type");
	CONFIG_TO_ATTR(request_attrs, sysadmin_attr_rel, "ldap_company_system_admin_relation_attribute");
	CONFIG_TO_ATTR(request_attrs, addresslist_unique_attr, "ldap_addresslist_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, addresslist_unique_attr_type, "ldap_addresslist_unique_attribute_type");
	CONFIG_TO_ATTR(request_attrs, addresslist_name_attr, "ldap_addresslist_name_attribute");
	CONFIG_TO_ATTR(request_attrs, dynamicgroup_unique_attr, "ldap_dynamicgroup_unique_attribute");
	CONFIG_TO_ATTR(request_attrs, dynamicgroup_unique_attr_type, "ldap_dynamicgroup_unique_attribute_type");
	CONFIG_TO_ATTR(request_attrs, dynamicgroup_name_attr, "ldap_dynamicgroup_name_attribute");
	CONFIG_TO_ATTR(request_attrs, ldap_addressbook_hide_attr, "ldap_addressbook_hide_attribute");
	CONFIG_TO_ATTR(request_attrs, user_server_attr, "ldap_user_server_attribute");
	CONFIG_TO_ATTR(request_attrs, company_server_attr, "ldap_company_server_attribute");
	if (!m_bDistributed) {
		user_server_attr = NULL;
		company_server_attr = NULL;
	}

	for (const auto &c : lExtraAttrs)
		request_attrs->add(c.szValue);
	unsigned int ulCutoff = atoui(m_config->GetSetting("ldap_filter_cutoff_elements"));

	/*
	 * When working in multi-company mode we need to determine to which company
	 * this object belongs. To do this efficiently we are using the cache for
	 * resolving the company based on the DN.
	 */
	if (m_bHosted)
		lpCompanyCache = m_lpCache->getObjectDNCache(this, CONTAINER_COMPANY);

	auto ldap_basedn = getSearchBase();

	/*
	 * Optimize the filter, by sorting the objectids we made sure that all
	 * object classes are grouped together. We then only need to create the
	 * objectclass filter once for each class and add the id filter after that.
	 * This makes the entire ldap filter much shorter and can be be handled much
	 * better by LDAP.
	 */
	setObjectIds.insert(objectids.begin(), objectids.end());

    /*
     * This is purely an optimization: to make sure we don't send huge 5000-item filters
     * to the LDAP Server (which may be slow), we have a cutoff point at which we simply
     * retrieve all LDAP data. This is sometimes (depending on the type of LDAP Server)
     * much, much faster (factor of 40 has been seen), with the minor expense of receiving
     * more data than actually needed. This means we're trading minor local processing time
     * and network activity for a much lower LDAP-server processing overhead.
     */

    if (ulCutoff != 0 && setObjectIds.size() >= ulCutoff) {
		// find all the different object classes in the objectids list, and make an or filter based on that
		objectclass_t objclass = (objectclass_t)-1; // set to something invalid
		ldap_filter = "(|";
		for (const auto &id : setObjectIds)
			if (objclass != id.objclass) {
				ldap_filter += getSearchFilter(id.objclass);
				objclass = id.objclass;
			}
		ldap_filter += ")";

		bCutOff = true;
	} else {
		ldap_filter = "(|";
		for (auto iter = setObjectIds.cbegin(); iter != setObjectIds.cend(); /* no increment */) {
			objectclass_t objclass = iter->objclass;

			ldap_filter += "(&" + getSearchFilter(iter->objclass) + "(|";
			while (iter != setObjectIds.cend() && objclass == iter->objclass) {
				switch (objclass) {
				case OBJECTCLASS_USER:
				case ACTIVE_USER:
				case NONACTIVE_USER:
				case NONACTIVE_ROOM:
				case NONACTIVE_EQUIPMENT:
				case NONACTIVE_CONTACT:
					ldap_filter += getSearchFilter(iter->id, user_unique_attr, user_unique_attr_type);
					break;
				case OBJECTCLASS_DISTLIST:
					ldap_filter += getSearchFilter(iter->id, group_unique_attr, group_unique_attr_type);
					ldap_filter += getSearchFilter(iter->id, dynamicgroup_unique_attr, dynamicgroup_unique_attr_type);
					break;
				case DISTLIST_GROUP:
				case DISTLIST_SECURITY:
					ldap_filter += getSearchFilter(iter->id, group_unique_attr, group_unique_attr_type);
					break;
				case DISTLIST_DYNAMIC:
					ldap_filter += getSearchFilter(iter->id, dynamicgroup_unique_attr, dynamicgroup_unique_attr_type);
					break;
				case OBJECTCLASS_CONTAINER:
					ldap_filter += getSearchFilter(iter->id, company_unique_attr, company_unique_attr_type);
					ldap_filter += getSearchFilter(iter->id, addresslist_unique_attr, addresslist_unique_attr_type);
					break;
				case CONTAINER_COMPANY:
					ldap_filter += getSearchFilter(iter->id, company_unique_attr, company_unique_attr_type);
					break;
				case CONTAINER_ADDRESSLIST:
					ldap_filter += getSearchFilter(iter->id, addresslist_unique_attr, addresslist_unique_attr_type);
					break;
				case OBJECTCLASS_UNKNOWN:
					ldap_filter += getSearchFilter(iter->id, user_unique_attr, user_unique_attr_type);
					ldap_filter += getSearchFilter(iter->id, group_unique_attr, group_unique_attr_type);
					ldap_filter += getSearchFilter(iter->id, dynamicgroup_unique_attr, dynamicgroup_unique_attr_type);
					ldap_filter += getSearchFilter(iter->id, company_unique_attr, company_unique_attr_type);
					ldap_filter += getSearchFilter(iter->id, addresslist_unique_attr, addresslist_unique_attr_type);
					break;
				default:
					ec_log_crit("Incorrect object class %d for item \"%s\"", iter->objclass, iter->id.c_str());
					continue;
				}
				++iter;
			}
			ldap_filter += "))";
		}
		ldap_filter += ")";
	}

	FOREACH_PAGING_SEARCH((char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
						  (char *)ldap_filter.c_str(), (char **)request_attrs->get(),
						  FETCH_ATTR_VALS, res)
	{
	FOREACH_ENTRY(res) {
		strDN = GetLDAPEntryDN(entry);
		objectid_t objectid = GetObjectIdForEntry(entry);
		objectdetails_t	sObjDetails = objectdetails_t(objectid.objclass);

		/* Default value, will be overridden later */
		if (sObjDetails.GetClass() == CONTAINER_COMPANY)
			sObjDetails.SetPropObject(OB_PROP_O_SYSADMIN, objectid_t("SYSTEM", ACTIVE_USER));

		// when cutoff is used, filter only the requested entries.
		if(bCutOff == true && setObjectIds.find(objectid) == setObjectIds.end())
			continue;

		FOREACH_ATTR(entry) {
			/*
			 * We check the attribute for every match possible,
			 * because an attribute can be used in multiple config options
			 */
			if (ldap_addressbook_hide_attr && !strcasecmp(att, ldap_addressbook_hide_attr)) {
				auto ldap_attr = getLDAPAttributeValue(att, entry);
				sObjDetails.SetPropBool(OB_PROP_B_AB_HIDDEN, parseBool(ldap_attr.c_str()));
			}

			for (const auto &se : lExtraAttrs) {
				unsigned int ulPropTag;

				/*
				 * The value should be set to something, as protection to make sure
				 * the name is a property tag all names should be prefixed with '0x'.
				 */
				if (strcasecmp(se.szValue, att) != 0 || strncasecmp(se.szName, "0x", strlen("0x")) != 0)
					continue;

				ulPropTag = xtoi(se.szName);

				/* Handle property actions */
				switch (PROP_ID(ulPropTag)) {
					// this property is only supported on ADS and OpenLDAP 2.4+ with slapo-memberof enabled
				case 0x8008:	/* PR_EMS_AB_IS_MEMBER_OF_DL */
				case 0x800E:	/* PR_EMS_AB_REPORTS */
				{
					/*
					 * These properties should contain the DN of the object,
					 * resolve them to the objectid and them as the PT_MV_BINARY
					 * version, the server will create the EntryIDs so the client
					 * can use the openEntry() to get the correct references.
					 */
					ulPropTag = (ulPropTag & 0xffff0000) | 0x1102;
					postaction p;

					p.objectid = objectid;

					if(ulPropTag == 0x800E1102)
    					p.objclass = OBJECTCLASS_USER;
                    else
                        p.objclass = OBJECTCLASS_DISTLIST;
                        
					p.ldap_attrs = getLDAPAttributeValues(att, entry);

					p.relAttr = "dn";
					p.relAttrType = "dn";

					p.propname = (property_key_t)ulPropTag;
					lPostActions.emplace_back(std::move(p));
					break;
                }
				case 0x3A4E:	/* PR_MANAGER_NAME */
					/* Rename to PR_EMS_AB_MANAGER */
					ulPropTag = 0x8005001E;
					/* fallthru */
				case 0x8005:	/* PR_EMS_AB_MANAGER */
				case 0x800C:	/* PR_EMS_AB_OWNER */
				{
					/*
					 * These properties should contain the DN of the object,
					 * resolve it to the objectid and store it as the PT_BINARY
					 * version, the server will create the EntryID so the client
					 * can use OpenEntry() to get the correct reference.
					 */
					ulPropTag = (ulPropTag & 0xffff0000) | 0x0102;
					postaction p;

					p.objectid = objectid;
                    p.objclass = OBJECTCLASS_USER;
					p.ldap_attr = getLDAPAttributeValue(att, entry);
					p.relAttr = "dn";
					p.relAttrType = "dn";

					p.propname = (property_key_t)ulPropTag;
					lPostActions.emplace_back(std::move(p));
					break;
                }
				case 0x3A30:	/* PR_ASSISTANT */
				{
					/*
					 * These properties should contain the full name of the object,
					 * the client won't need to resolve them to a Address Book entry
					 * so a fullname will be sufficient.
					 */
					ulPropTag = 0x3A30001E;
					postaction p;

					p.objectid = objectid;
                    p.objclass = OBJECTCLASS_USER;
					p.ldap_attr = getLDAPAttributeValue(att, entry);
					p.relAttr = "dn";
					p.relAttrType = "dn";
					p.result_attr = m_config->GetSetting("ldap_fullname_attribute");
					
					p.propname = (property_key_t)ulPropTag;
					lPostActions.emplace_back(std::move(p));
					break;
                }
				default: {
					auto ldap_attrs = getLDAPAttributeValues(att, entry);

					if ((ulPropTag & 0xFFFE) == 0x1E) // if (PROP_TYPE(ulPropTag) == PT_STRING8 || PT_UNICODE)
						for (auto &i : ldap_attrs)
							i = m_iconv->convert(i);

					if (ulPropTag & 0x1000) /* MV_FLAG */
						sObjDetails.SetPropListString((property_key_t)ulPropTag, ldap_attrs);
					else
						sObjDetails.SetPropString((property_key_t)ulPropTag, ldap_attrs.front());
					break;
				}
				}
			}

			/* Objectclass specific properties */
			switch (sObjDetails.GetClass()) {
			case ACTIVE_USER:
			case NONACTIVE_USER:
			case NONACTIVE_ROOM:
			case NONACTIVE_EQUIPMENT:
			case NONACTIVE_CONTACT:
				if (loginname_attr && !strcasecmp(att, loginname_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_LOGIN, ldap_attr);
				}

				if (user_fullname_attr && !strcasecmp(att, user_fullname_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_FULLNAME, ldap_attr);
				}

				if (password_attr && !strcasecmp(att, password_attr)) {
					auto ldap_attr = getLDAPAttributeValue(att, entry);
					sObjDetails.SetPropString(OB_PROP_S_PASSWORD, ldap_attr);
				}

				if (emailaddress_attr && !strcasecmp(att, emailaddress_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_EMAIL, ldap_attr);
				}

				if (emailaliases_attr && !strcasecmp(att, emailaliases_attr)) {
					auto ldap_attrs = getLDAPAttributeValues(att, entry);
					sObjDetails.SetPropListString(OB_PROP_LS_ALIASES, ldap_attrs);
				}

				if (isadmin_attr && !strcasecmp(att, isadmin_attr)) {
					auto ldap_attr = getLDAPAttributeValue(att, entry);
					sObjDetails.SetPropInt(OB_PROP_I_ADMINLEVEL, std::min(2, atoi(ldap_attr.c_str())));
				}

				if (resource_type_attr && !strcasecmp(att, resource_type_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_RESOURCE_DESCRIPTION, ldap_attr);
				}

				if (resource_capacity_attr && !strcasecmp(att, resource_capacity_attr)) {
					auto ldap_attr = getLDAPAttributeValue(att, entry);
					sObjDetails.SetPropInt(OB_PROP_I_RESOURCE_CAPACITY, atoi(ldap_attr.c_str()));
				}

				if (usercert_attr && !strcasecmp(att, usercert_attr)) {
					auto ldap_attrs = getLDAPAttributeValues(att, entry);
					sObjDetails.SetPropListString(OB_PROP_LS_CERTIFICATE, ldap_attrs);
				}

				if (sendas_attr && !strcasecmp(att, sendas_attr)) {
					postaction p;

					p.objectid = objectid;

					p.objclass = OBJECTCLASS_UNKNOWN;
					p.ldap_attrs = getLDAPAttributeValues(att, entry);

					p.relAttr = m_config->GetSetting("ldap_sendas_relation_attribute");
					p.relAttrType = m_config->GetSetting("ldap_sendas_attribute_type");

					if (p.relAttr == NULL || p.relAttr[0] == '\0')
						p.relAttr = m_config->GetSetting("ldap_user_unique_attribute");

					p.propname = OB_PROP_LO_SENDAS;
					lPostActions.emplace_back(std::move(p));
				}
				if (user_server_attr && !strcasecmp(att, user_server_attr)) {
					auto ldap_attr = getLDAPAttributeValue(att, entry);
					sObjDetails.SetPropString(OB_PROP_S_SERVERNAME, ldap_attr);
				}
				break;
			case DISTLIST_GROUP:
			case DISTLIST_SECURITY:
				if (group_fullname_attr && !strcasecmp(att, group_fullname_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_LOGIN, ldap_attr);
					sObjDetails.SetPropString(OB_PROP_S_FULLNAME, ldap_attr);
				}

				if (emailaddress_attr && !strcasecmp(att, emailaddress_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_EMAIL, ldap_attr);
				}

				if (emailaliases_attr && !strcasecmp(att, emailaliases_attr)) {
					auto ldap_attrs = getLDAPAttributeValues(att, entry);
					sObjDetails.SetPropListString(OB_PROP_LS_ALIASES, ldap_attrs);
				}

				if (sendas_attr && !strcasecmp(att, sendas_attr)) {
					postaction p;

					p.objectid = objectid;

					p.objclass = OBJECTCLASS_UNKNOWN;
					p.ldap_attrs = getLDAPAttributeValues(att, entry);

					p.relAttr = m_config->GetSetting("ldap_sendas_relation_attribute");
					p.relAttrType = m_config->GetSetting("ldap_sendas_attribute_type");

					if (p.relAttr == NULL || p.relAttr[0] == '\0')
						p.relAttr = m_config->GetSetting("ldap_user_unique_attribute");

					p.propname = OB_PROP_LO_SENDAS;
					lPostActions.emplace_back(std::move(p));
				}
				break;
			case DISTLIST_DYNAMIC:
				if (dynamicgroup_name_attr && !strcasecmp(att, dynamicgroup_name_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_LOGIN, ldap_attr);
					sObjDetails.SetPropString(OB_PROP_S_FULLNAME, ldap_attr);
				}

				if (emailaddress_attr && !strcasecmp(att, emailaddress_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_EMAIL, ldap_attr);
				}

				if (emailaliases_attr && !strcasecmp(att, emailaliases_attr)) {
					auto ldap_attrs = getLDAPAttributeValues(att, entry);
					sObjDetails.SetPropListString(OB_PROP_LS_ALIASES, ldap_attrs);
				}
				break;
			case CONTAINER_COMPANY:
				if (company_fullname_attr && !strcasecmp(att, company_fullname_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_LOGIN, ldap_attr);
					sObjDetails.SetPropString(OB_PROP_S_FULLNAME, ldap_attr);
				}
				if (company_server_attr && !strcasecmp(att, company_server_attr)) {
					auto ldap_attr = getLDAPAttributeValue(att, entry);
					sObjDetails.SetPropString(OB_PROP_S_SERVERNAME, ldap_attr);
				}
				if (sysadmin_attr && !strcasecmp(att, sysadmin_attr)) {
					postaction p;

					p.objectid = objectid;

					p.objclass = OBJECTCLASS_USER;
					p.ldap_attr = getLDAPAttributeValue(att, entry);
					p.relAttr = sysadmin_attr_rel ? sysadmin_attr_rel : user_unique_attr;
					p.relAttrType = sysadmin_attr_type;
					p.propname = OB_PROP_O_SYSADMIN;
					lPostActions.emplace_back(std::move(p));
				}

				break;
			case CONTAINER_ADDRESSLIST:
				if (addresslist_name_attr && !strcasecmp(att, addresslist_name_attr)) {
					auto ldap_attr = m_iconv->convert(getLDAPAttributeValue(att, entry));
					sObjDetails.SetPropString(OB_PROP_S_LOGIN, ldap_attr);
					sObjDetails.SetPropString(OB_PROP_S_FULLNAME, ldap_attr);
				}
				break;
			case OBJECTCLASS_UNKNOWN:
			case OBJECTCLASS_USER:
			case OBJECTCLASS_DISTLIST:
			case OBJECTCLASS_CONTAINER:
				throw runtime_error("getObjectDetails: found unknown object type");
			default:
				break;
			}
		}
		END_FOREACH_ATTR

		if (m_bHosted && sObjDetails.GetClass() != CONTAINER_COMPANY) {
			objectid_t company = m_lpCache->getParentForDN(lpCompanyCache, strDN);
			sObjDetails.SetPropObject(OB_PROP_O_COMPANYID, company);
		}

		if (!objectid.id.empty())
			mapdetails[objectid] = sObjDetails;
	}
	END_FOREACH_ENTRY
	}
	END_FOREACH_LDAP_PAGING

	// paged loop ended, so now we can process the postactions.
	for (const auto &p : lPostActions) {
		auto o = mapdetails.find(p.objectid);
		if (o == mapdetails.cend()) {
			// this should never happen, but only some details will be missing, not the end of the world.
			ec_log_crit("No object \"%s\" found for postaction", p.objectid.id.c_str());
			continue;
		}

		if (p.ldap_attr.empty()) {
			// list, so use AddPropObject()
			try {
			    // Currently not supported for multivalued arrays. This would require multiple calls to objectUniqueIDtoAttributeData
			    // which is inefficient, and it is currently unused.
				assert(p.result_attr.empty());
				auto lstSignatures = resolveObjectsFromAttributeType(p.objclass, p.ldap_attrs, p.relAttr, p.relAttrType);
				if (lstSignatures.size() != p.ldap_attrs.size()) {
					// try to rat out the object causing the failed ldap query
					ec_log_err("Not all objects in relation found for object \"%s\"", o->second.GetPropString(OB_PROP_S_LOGIN).c_str());
				}
				for (const auto &sig : lstSignatures)
					o->second.AddPropObject(p.propname, sig.id);
			} catch (ldap_error &e) {
				if(!LDAP_NAME_ERROR(e.GetLDAPError()))
					throw;
			} catch (std::exception &e) {
				ec_log_err("Unable to resolve object from relational attribute type \"%s\"", p.relAttr);
			}
		} else {
			// string, so use SetPropObject
			try {
				objectsignature_t signature;
				signature = resolveObjectFromAttributeType(p.objclass, p.ldap_attr, p.relAttr, p.relAttrType);
				if (!p.result_attr.empty()) {
				    // String type
				    try {
						o->second.SetPropString(p.propname, objectUniqueIDtoAttributeData(signature.id, p.result_attr.c_str()));
                    } catch (ldap_error &e) {
                        if(!LDAP_NAME_ERROR(e.GetLDAPError()))
                            throw;
                    } catch (std::exception &e) {
                        ec_log_err("Unable to get attribute \"%s\" for relation \"%s\" for object \"%s\"", p.result_attr.c_str(), p.ldap_attr.c_str(), o->second.GetPropString(OB_PROP_S_LOGIN).c_str());
                    }
				} else {
				    // ID type
    				if (!signature.id.id.empty())
					o->second.SetPropObject(p.propname, signature.id);
	    			else
					ec_log_err("Unable to find relation \"%s\" in attribute \"%s\"", p.ldap_attr.c_str(), p.relAttr);
                }
			} catch (ldap_error &e) {
				if(!LDAP_NAME_ERROR(e.GetLDAPError()))
					throw;
			} catch (std::exception &e) {
				ec_log_err("Unable to resolve object from relational attribute type \"%s\"", p.relAttr);
			}
		}
	}

	return mapdetails;
}

objectdetails_t LDAPUserPlugin::getObjectDetails(const objectid_t &id)
{
	auto mapDetails = getObjectDetails(std::list<objectid_t>{id});
	auto iterDetails = mapDetails.find(id);
	if (iterDetails == mapDetails.cend())
		throw objectnotfound("No details for "+id.id);
	return iterDetails->second;
}

static LDAPMod *newLDAPModification(char *attribute, const list<string> &values) {
	auto mod = static_cast<LDAPMod *>(calloc(1, sizeof(LDAPMod)));

	// The only type of modification allowed here is replace.  It
	// should be enough for our needs, but if an addition or removal
	// is needed, this value has to be changed.
	mod->mod_op = LDAP_MOD_REPLACE;
	mod->mod_type = attribute;
	mod->mod_vals.modv_strvals = (char**) calloc(values.size() + 1, sizeof(char*));
	int idx = 0;
	for (const auto &i : values)
		// A strdup is necessary to be able to call free on it in the
		// method changeAttribute, below.
		mod->mod_vals.modv_strvals[idx++] = strdup(i.c_str());
	mod->mod_vals.modv_strvals[idx] = NULL;
	return mod;
}

static LDAPMod *newLDAPModification(char *attribute, const char *value) {
	// Pretty lame function, all it does is use newLDAPModification
	// with a list with only one element.
	return newLDAPModification(attribute, std::list<std::string>{value});
}

int LDAPUserPlugin::changeAttribute(const char *dn, char *attribute, const char *value) {
	LDAPMod *mods[] = {newLDAPModification(attribute, value), nullptr};

	// Actual LDAP call
	int rc = ldap_modify_s(m_ldap, const_cast<char *>(dn), mods);
	if (rc != LDAP_SUCCESS)
		return 1;

	// Free all calloced memory
	free(mods[0]->mod_vals.modv_strvals[0]);
	free(mods[0]->mod_vals.modv_strvals);
	free(mods[0]);

	return 0;
}

int LDAPUserPlugin::changeAttribute(const char *dn, char *attribute, const std::list<std::string> &values) {
	LDAPMod *mods[] = {newLDAPModification(attribute, values), nullptr};

	// Actual LDAP call
	int rc = ldap_modify_s(m_ldap, const_cast<char *>(dn), mods);
	if (rc != LDAP_SUCCESS)
		return 1;

	// Free all calloced / strduped memory
	for (int i = 0; mods[0]->mod_vals.modv_strvals[i] != NULL; ++i)
		free(mods[0]->mod_vals.modv_strvals[i]);
	free(mods[0]->mod_vals.modv_strvals);
	free(mods[0]);

	return 0;
}

void LDAPUserPlugin::changeObject(const objectid_t &id, const objectdetails_t &details, const std::list<std::string> *lpDelProps) {
	throw notimplemented("Change object is not supported when using the LDAP user plugin.");
}

objectsignature_t LDAPUserPlugin::createObject(const objectdetails_t &details) {
	throw notimplemented("Creating objects is not supported when using the LDAP user plugin.");
}

void LDAPUserPlugin::deleteObject(const objectid_t &id) {
	throw notimplemented("Deleting users is not supported when using the LDAP user plugin.");
}

void LDAPUserPlugin::modifyObjectId(const objectid_t &oldId, const objectid_t &newId)
{
	throw notimplemented("Modifying objectid is not supported when using the LDAP user plugin.");
}

/**
 * @todo speedup: read group info on the user (with ADS you know the groups if you have the user dn)
 * @note Missing one group: with linux you have one group id on the user and other objectid on every possible group.
 *  this function checks all groups from searchfilter for an existing objectid.
 */
signatures_t
LDAPUserPlugin::getParentObjectsForObject(userobject_relation_t relation,
    const objectid_t &childobject)
{
	string				member_data;
	objectclass_t		parentobjclass = OBJECTCLASS_UNKNOWN;
	const char *unique_attr = nullptr, *member_attr = nullptr;
	const char *member_attr_type = nullptr, *member_attr_rel = nullptr;

	switch (childobject.objclass) {
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		unique_attr = m_config->GetSetting("ldap_user_unique_attribute");
		break;
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
		unique_attr = m_config->GetSetting("ldap_group_unique_attribute");
		break;
	case DISTLIST_DYNAMIC:
		unique_attr = m_config->GetSetting("ldap_dynamicgroup_unique_attribute");
		break;
	case CONTAINER_COMPANY:
		unique_attr = m_config->GetSetting("ldap_company_unique_attribute");
		break;
	case CONTAINER_ADDRESSLIST:
		unique_attr = m_config->GetSetting("ldap_addresslist_unique_attribute");
		break;
	case OBJECTCLASS_UNKNOWN:
	case OBJECTCLASS_CONTAINER:
	default:
		throw runtime_error("Object is wrong type");
	}

	switch (relation) {
	case OBJECTRELATION_GROUP_MEMBER:
		LOG_PLUGIN_DEBUG("%s Relation: Group member", __FUNCTION__);
		parentobjclass = OBJECTCLASS_DISTLIST;
		member_attr = m_config->GetSetting("ldap_groupmembers_attribute");
		member_attr_type = m_config->GetSetting("ldap_groupmembers_attribute_type");
		member_attr_rel = m_config->GetSetting("ldap_groupmembers_relation_attribute");
		break;
	case OBJECTRELATION_COMPANY_VIEW:
		LOG_PLUGIN_DEBUG("%s Relation: Company view", __FUNCTION__);
		parentobjclass = CONTAINER_COMPANY;
		member_attr = m_config->GetSetting("ldap_company_view_attribute");
		member_attr_type = m_config->GetSetting("ldap_company_view_attribute_type");
		member_attr_rel = m_config->GetSetting("ldap_company_view_relation_attribute", "", NULL);
		// restrict to have only companies, nevery anything else
		if (!member_attr_rel)
			member_attr_rel = m_config->GetSetting("ldap_company_unique_attribute");
		break;
	case OBJECTRELATION_COMPANY_ADMIN:
		LOG_PLUGIN_DEBUG("%s Relation: Company admin", __FUNCTION__);
		parentobjclass = CONTAINER_COMPANY;
		member_attr = m_config->GetSetting("ldap_company_admin_attribute");
		member_attr_type = m_config->GetSetting("ldap_company_admin_attribute_type");
		member_attr_rel = m_config->GetSetting("ldap_company_admin_relation_attribute");
		break;
	case OBJECTRELATION_QUOTA_USERRECIPIENT:
		LOG_PLUGIN_DEBUG("%s Relation: Quota user recipient", __FUNCTION__);
		parentobjclass = CONTAINER_COMPANY;
		member_attr = m_config->GetSetting("ldap_quota_userwarning_recipients_attribute");
		member_attr_type = m_config->GetSetting("ldap_quota_userwarning_recipients_attribute_type");
		member_attr_rel = m_config->GetSetting("ldap_quota_userwarning_recipients_relation_attribute");
		break;
	case OBJECTRELATION_QUOTA_COMPANYRECIPIENT:
		LOG_PLUGIN_DEBUG("%s Relation: Quota company recipient", __FUNCTION__);
		parentobjclass = CONTAINER_COMPANY;
		member_attr = m_config->GetSetting("ldap_quota_companywarning_recipients_attribute");
		member_attr_type = m_config->GetSetting("ldap_quota_companywarning_recipients_attribute_type");
		member_attr_rel = m_config->GetSetting("ldap_quota_companywarning_recipients_relation_attribute");
		break;
	default:
		LOG_PLUGIN_DEBUG("%s Relation: Unhandled %x", __FUNCTION__, relation);
		throw runtime_error("Cannot obtain parents for relation " + stringify(relation));
		break;
	}

	auto ldap_basedn = getSearchBase();
	auto ldap_filter = getSearchFilter(parentobjclass);

	if(member_attr_rel == NULL  || strlen(member_attr_rel) == 0)
		member_attr_rel = unique_attr;

	if (member_attr_type && strcasecmp(member_attr_type, LDAP_DATA_TYPE_DN) == 0) {
		//ads
		member_data = objectUniqueIDtoObjectDN(childobject);
	} else { //LDAP_DATA_TYPE_ATTRIBUTE
		//posix ldap
		// FIXME: No binary support for member_attr_rel
		if (strcasecmp(member_attr_rel, unique_attr) == 0)
			member_data = childobject.id;
		else
			member_data = objectUniqueIDtoAttributeData(childobject, member_attr_rel);
	}

	ldap_filter = "(&" + ldap_filter + "(" + member_attr + "=" + StringEscapeSequence(member_data) + "))";
	return getAllObjectsByFilter(ldap_basedn, LDAP_SCOPE_SUBTREE,
	       ldap_filter, string(), false);
}

signatures_t
LDAPUserPlugin::getSubObjectsForObject(userobject_relation_t relation,
    const objectid_t &sExternId)
{
	enum LISTTYPE { MEMBERS, FILTER } ulType = MEMBERS;
	signatures_t members;
	list<string>			memberlist;

	auto_free_ldap_message res;
	std::string dn, ldap_member_filter, companyDN;
	objectid_t		companyid;
	objectclass_t	childobjclass = OBJECTCLASS_UNKNOWN;
	const char *unique_attr = nullptr, *unique_attr_type = nullptr;
	const char *member_attr = nullptr, *member_attr_type = nullptr;
	const char *base_attr = NULL;

	std::unique_ptr<attrArray> member_attr_rel(new attrArray(5));
	std::unique_ptr<attrArray> child_unique_attr(new attrArray(5));

	CONFIG_TO_ATTR(child_unique_attr, user_unique_attr, "ldap_user_unique_attribute");
	CONFIG_TO_ATTR(child_unique_attr, group_unique_attr, "ldap_group_unique_attribute");
	CONFIG_TO_ATTR(child_unique_attr, company_unique_attr, "ldap_company_unique_attribute");
	CONFIG_TO_ATTR(child_unique_attr, addresslist_unique_attr, "ldap_addresslist_unique_attribute");
	CONFIG_TO_ATTR(child_unique_attr, dynamicgroup_unique_attr, "ldap_dynamicgroup_unique_attribute");

	switch (relation) {
	case OBJECTRELATION_GROUP_MEMBER: {
		LOG_PLUGIN_DEBUG("%s Relation: Group member", __FUNCTION__);
		childobjclass = OBJECTCLASS_UNKNOWN; /* Support users & groups */
		if (sExternId.objclass != DISTLIST_DYNAMIC) {
			unique_attr = group_unique_attr;
			unique_attr_type = m_config->GetSetting("ldap_group_unique_attribute_type");
			member_attr = m_config->GetSetting("ldap_groupmembers_attribute");
			member_attr_type = m_config->GetSetting("ldap_groupmembers_attribute_type");
			CONFIG_TO_ATTR(member_attr_rel, group_rel_attr, "ldap_groupmembers_relation_attribute");
			if (member_attr_rel->empty()) {
				member_attr_rel->add(user_unique_attr);
				member_attr_rel->add(group_unique_attr);
			}
		} else {
			// dynamic group
			unique_attr = dynamicgroup_unique_attr;
			unique_attr_type = m_config->GetSetting("ldap_dynamicgroup_unique_attribute_type");
			member_attr = m_config->GetSetting("ldap_dynamicgroup_filter_attribute");
			base_attr = m_config->GetSetting("ldap_dynamicgroup_search_base_attribute");

			member_attr_rel->add(child_unique_attr->get());
			ulType = FILTER;
		}
		break;
	}
	case OBJECTRELATION_COMPANY_VIEW: {
		LOG_PLUGIN_DEBUG("%s Relation: Company view", __FUNCTION__);
		childobjclass = CONTAINER_COMPANY;
		unique_attr = company_unique_attr;
		unique_attr_type = m_config->GetSetting("ldap_company_unique_attribute_type");
		member_attr = m_config->GetSetting("ldap_company_view_attribute");
		member_attr_type = m_config->GetSetting("ldap_company_view_attribute_type");
		CONFIG_TO_ATTR(member_attr_rel, view_rel_attr, "ldap_company_view_relation_attribute");
		if (member_attr_rel->empty())
			member_attr_rel->add(company_unique_attr);
		break;
	}
	case OBJECTRELATION_COMPANY_ADMIN: {
		LOG_PLUGIN_DEBUG("%s Relation: Company admin", __FUNCTION__);
		childobjclass = ACTIVE_USER; /* Only active users can perform administrative tasks */
		unique_attr = company_unique_attr;
		unique_attr_type = m_config->GetSetting("ldap_company_unique_attribute_type");
		member_attr = m_config->GetSetting("ldap_company_admin_attribute");
		member_attr_type = m_config->GetSetting("ldap_company_admin_attribute_type");
		CONFIG_TO_ATTR(member_attr_rel, admin_rel_attr, "ldap_company_admin_relation_attribute");
		if (member_attr_rel->empty())
			member_attr_rel->add(user_unique_attr);
		break;
	}
	case OBJECTRELATION_QUOTA_USERRECIPIENT: {
		LOG_PLUGIN_DEBUG("%s Relation: Quota user recipient", __FUNCTION__);
		childobjclass = OBJECTCLASS_USER;
		unique_attr = company_unique_attr;
		unique_attr_type = m_config->GetSetting("ldap_company_unique_attribute_type");
		member_attr = m_config->GetSetting("ldap_quota_userwarning_recipients_attribute");
		member_attr_type = m_config->GetSetting("ldap_quota_userwarning_recipients_attribute_type");
		CONFIG_TO_ATTR(member_attr_rel, qur_rel_attr, "ldap_quota_userwarning_recipients_relation_attribute");
		if (member_attr_rel->empty()) {
			member_attr_rel->add(user_unique_attr);
			member_attr_rel->add(group_unique_attr);
		}
		break;
	}
	case OBJECTRELATION_QUOTA_COMPANYRECIPIENT: {
		LOG_PLUGIN_DEBUG("%s Relation: Quota company recipient", __FUNCTION__);
		childobjclass = OBJECTCLASS_USER;
		unique_attr = company_unique_attr;
		unique_attr_type = m_config->GetSetting("ldap_company_unique_attribute_type");
		member_attr = m_config->GetSetting("ldap_quota_companywarning_recipients_attribute");
		member_attr_type = m_config->GetSetting("ldap_quota_companywarning_recipients_attribute_type");
		CONFIG_TO_ATTR(member_attr_rel, qcr_rel_attr, "ldap_quota_companywarning_recipients_relation_attribute");
		if (member_attr_rel->empty()) {
			member_attr_rel->add(user_unique_attr);
			member_attr_rel->add(group_unique_attr);
		}
		break;
	}
	case OBJECTRELATION_ADDRESSLIST_MEMBER:
		LOG_PLUGIN_DEBUG("%s Relation: Addresslist member", __FUNCTION__);
		childobjclass = OBJECTCLASS_UNKNOWN;
		unique_attr = addresslist_unique_attr;
		unique_attr_type = m_config->GetSetting("ldap_addresslist_unique_attribute_type");
		member_attr = m_config->GetSetting("ldap_addresslist_filter_attribute");
		base_attr = m_config->GetSetting("ldap_addresslist_search_base_attribute");

		member_attr_rel->add(child_unique_attr->get());
		ulType = FILTER;
		break;
	default:
		LOG_PLUGIN_DEBUG("%s Relation: Unhandled %x", __FUNCTION__, relation);
		throw runtime_error("Cannot obtain children for relation " + stringify(relation));
		break;
	}

	const char *request_attrs[] = {
		member_attr,
		base_attr,
		NULL
	};

	//Get group DN from unique id
	auto ldap_basedn = getSearchBase();
	auto ldap_filter = getObjectSearchFilter(sExternId, unique_attr, unique_attr_type);

	/* LDAP filter empty, parent does not exist */
	if (ldap_filter.empty())
		throw objectnotfound("ldap filter is empty");

	my_ldap_search_s(
			(char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
			(char *)ldap_filter.c_str(), (char **)request_attrs,
			FETCH_ATTR_VALS, &~res);

	if(ulType == MEMBERS) {
		// Get the DN for each of the returned entries
		FOREACH_ENTRY(res) {
			FOREACH_ATTR(entry) {
				if (member_attr && !strcasecmp(att, member_attr))
					memberlist = getLDAPAttributeValues(att, entry);
			}
			END_FOREACH_ATTR
		}
		END_FOREACH_ENTRY

		if (!memberlist.empty())
			members = resolveObjectsFromAttributesType(childobjclass, memberlist, member_attr_rel->get(), member_attr_type);
		return members;
	}

	// Members are specified by a filter
	FOREACH_ENTRY(res) {
		// We need the DN of the addresslist so that we can later find out which company it is in
		dn = GetLDAPEntryDN(entry);

		FOREACH_ATTR(entry) {
			if (member_attr && !strcasecmp(att, member_attr))
				ldap_member_filter = getLDAPAttributeValue(att, entry);
			if (base_attr && !strcasecmp(att, base_attr))
				ldap_basedn = getLDAPAttributeValue(att, entry);
		}
		END_FOREACH_ATTR
	}
	END_FOREACH_ENTRY

	if (ldap_member_filter.empty())
		return members;
	if (m_bHosted) {
		auto lpCompanyCache = m_lpCache->getObjectDNCache(this, CONTAINER_COMPANY);
		companyid = m_lpCache->getParentForDN(lpCompanyCache, dn);
		companyDN = m_lpCache->getDNForObject(lpCompanyCache, companyid);
	}

	// Use the filter to get all members matching the specified search filter
	if (ldap_basedn.empty())
		ldap_basedn = getSearchBase();
	ldap_filter = "(&" + getSearchFilter(childobjclass) + ldap_member_filter + ")";
	members = getAllObjectsByFilter(ldap_basedn, LDAP_SCOPE_SUBTREE, ldap_filter, companyDN, false);
	return members;
}

void LDAPUserPlugin::addSubObjectRelation(userobject_relation_t relation, const objectid_t &id, const objectid_t &member) {
	throw notimplemented("add object relations is not supported when using the LDAP user plugin.");
}

void LDAPUserPlugin::deleteSubObjectRelation(userobject_relation_t relation, const objectid_t &id, const objectid_t &member) {
	throw notimplemented("Delete object relations is not supported when using the LDAP user plugin.");
}

signatures_t
LDAPUserPlugin::searchObject(const std::string &match, unsigned int ulFlags)
{
	string search_filter;
	size_t pos;

	LOG_PLUGIN_DEBUG("%s %s flags:%x", __FUNCTION__, match.c_str(), ulFlags);

	auto ldap_basedn = getSearchBase();
	auto ldap_filter = getSearchFilter();

	// Escape match string
	auto escMatch = StringEscapeSequence(m_iconvrev->convert(match));

	if (! (ulFlags & EMS_AB_ADDRESS_LOOKUP)) {
		try {
			// the admin should place %s* in the search filter
			search_filter = m_config->GetSetting("ldap_object_search_filter");
			// search/replace %s -> escMatch
			while ((pos = search_filter.find("%s")) != string::npos)
				search_filter.replace(pos, 2, escMatch);
		} catch (...) {};
		// custom filter was empty, add * for a full search
		if (search_filter.empty())
			escMatch += "*";
	}

	if (search_filter.empty()) {
		// if building the filter failed, use our search filter

		// @todo optimize filter, a lot of attributes can be the same.
		search_filter =
			"(|"
				"(" + string(m_config->GetSetting("ldap_loginname_attribute")) + "=" + escMatch + ")"
				"(" + string(m_config->GetSetting("ldap_fullname_attribute")) + "=" + escMatch + ")"
				"(" + string(m_config->GetSetting("ldap_emailaddress_attribute")) + "=" + escMatch + ")"
				"(" + string(m_config->GetSetting("ldap_emailaliases_attribute")) + "=" + escMatch + ")"
				"(" + string(m_config->GetSetting("ldap_groupname_attribute")) + "=" + escMatch + ")"
				"(" + string(m_config->GetSetting("ldap_companyname_attribute")) + "=" + escMatch + ")"
				"(" + string(m_config->GetSetting("ldap_addresslist_name_attribute")) + "=" + escMatch + ")"
				"(" + string(m_config->GetSetting("ldap_dynamicgroup_name_attribute")) + "=" + escMatch + ")"
			")";
	}
	ldap_filter = "(&" + ldap_filter + search_filter + ")";
	auto signatures = getAllObjectsByFilter(ldap_basedn, LDAP_SCOPE_SUBTREE,
	                  ldap_filter, string(), false);
	if (signatures.empty())
		throw objectnotfound(ldap_filter);

	return signatures;
}

objectdetails_t LDAPUserPlugin::getPublicStoreDetails()
{
	auto_free_ldap_message res;
	objectdetails_t details(CONTAINER_COMPANY);

	if (!m_bDistributed)
		throw objectnotfound("public store");

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	const char *publicstore_attr = m_config->GetSetting("ldap_server_contains_public_attribute", "", NULL);
	auto ldap_basedn = getSearchBase();
	auto search_filter = getServerSearchFilter();

	if (publicstore_attr)
		search_filter = "(&" + search_filter + "(" + publicstore_attr + "=1))";

	std::unique_ptr<attrArray> request_attrs(new attrArray(1));
	CONFIG_TO_ATTR(request_attrs, unique_attr, "ldap_server_unique_attribute");

	// Do a search request to get all attributes for this user. The
	// attributes requested should be passed as the fifth argument, if
	// necessary
	my_ldap_search_s(
			 (char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
			 (char *)search_filter.c_str(), (char **)request_attrs->get(),
			 FETCH_ATTR_VALS, &~res);

	switch (ldap_count_entries(m_ldap, res)) {
	case 0:
		throw objectnotfound("public store server");
	case 1:
		break;
	default:
		throw toomanyobjects("public store server");
	}

	auto entry = ldap_first_entry(m_ldap, res);
	if (entry == nullptr)
		throw runtime_error("ldap_dn: broken.");

	FOREACH_ATTR(entry) {
		if (unique_attr && !strcasecmp(att, unique_attr))
			details.SetPropString(OB_PROP_S_SERVERNAME, m_iconv->convert(getLDAPAttributeValue(att, entry)));
	}
	END_FOREACH_ATTR

	return details;
}

serverlist_t LDAPUserPlugin::getServers()
{
	auto_free_ldap_message res;
	serverlist_t serverlist;

	if (!m_bDistributed)
		throw objectnotfound("Distributed not enabled");

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);
	auto ldap_basedn = getSearchBase();
	auto search_filter = "(&" + getServerSearchFilter() + ")";

	std::unique_ptr<attrArray> request_attrs(new attrArray(1));
	CONFIG_TO_ATTR(request_attrs, name_attr, "ldap_server_unique_attribute");

	my_ldap_search_s(
			 (char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
			 (char *)search_filter.c_str(), (char **)request_attrs->get(),
			 FETCH_ATTR_VALS, &~res);

    FOREACH_ENTRY(res)
    {
    	FOREACH_ATTR(entry) {
			if (name_attr != nullptr && strcasecmp(att, name_attr) == 0)
				serverlist.emplace_back(m_iconv->convert(getLDAPAttributeValue(att, entry)));
        }
        END_FOREACH_ATTR
    }
    END_FOREACH_ENTRY

	return serverlist;
}

serverdetails_t LDAPUserPlugin::getServerDetails(const std::string &server)
{
	auto_free_ldap_message res;

	if (!m_bDistributed)
		throw objectnotfound("Distributed not enabled for" +server);

	LOG_PLUGIN_DEBUG("%s for server %s", __FUNCTION__, server.c_str());
	serverdetails_t serverDetails(server);
	std::string strAddress, strHttpPort, strSslPort, strFilePath, strProxyPath;
	auto ldap_basedn = getSearchBase();
	auto search_filter =
		"(&" +
		getServerSearchFilter() +
		getSearchFilter(server, m_config->GetSetting("ldap_server_unique_attribute")) +
		")";

	std::unique_ptr<attrArray> request_attrs(new attrArray(5));
	CONFIG_TO_ATTR(request_attrs, address_attr, "ldap_server_address_attribute");
	CONFIG_TO_ATTR(request_attrs, http_port_attr, "ldap_server_http_port_attribute");
	CONFIG_TO_ATTR(request_attrs, ssl_port_attr, "ldap_server_ssl_port_attribute");
	CONFIG_TO_ATTR(request_attrs, file_path_attr, "ldap_server_file_path_attribute");
	CONFIG_TO_ATTR(request_attrs, proxy_path_attr, "ldap_server_proxy_path_attribute");

	if (request_attrs->empty())
		throw runtime_error("no attributes defined");

	// Do a search request to get all attributes for this server. The
	// attributes requested should be passed as the fifth argument, if
	// necessary
	my_ldap_search_s(
			 (char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
			 (char *)search_filter.c_str(), (char **)request_attrs->get(),
			 FETCH_ATTR_VALS, &~res);

	switch (ldap_count_entries(m_ldap, res)) {
	case 0:
		throw objectnotfound("No results from ldap for "+server);
	case 1:
		break;
	default:
		throw toomanyobjects(server);
	}

	auto entry = ldap_first_entry(m_ldap, res);
	if (entry == nullptr)
		throw runtime_error("ldap_dn: broken.");

	FOREACH_ATTR(entry) {
		if (address_attr != nullptr && strcasecmp(att, address_attr) == 0)
			strAddress = m_iconv->convert(getLDAPAttributeValue(att, entry));
		if (http_port_attr != nullptr && strcasecmp(att, http_port_attr) == 0)
			strHttpPort = m_iconv->convert(getLDAPAttributeValue(att, entry));
		if (ssl_port_attr != nullptr && strcasecmp(att, ssl_port_attr) == 0)
			strSslPort = m_iconv->convert(getLDAPAttributeValue(att, entry));
		if (file_path_attr != nullptr && strcasecmp(att, file_path_attr) == 0)
			strFilePath = m_iconv->convert(getLDAPAttributeValue(att, entry));
		if (proxy_path_attr != nullptr && strcasecmp(att, proxy_path_attr) == 0)
		    strProxyPath = m_iconv->convert(getLDAPAttributeValue(att, entry));
	}
	END_FOREACH_ATTR

	if (strAddress.empty())
		throw runtime_error("obligatory address missing for server '" + server + "'");
	if (strHttpPort.empty())
		throw runtime_error("obligatory http port missing for server '" + server + "'");

	serverDetails.SetHostAddress(strAddress);
	serverDetails.SetHttpPort(atoi(strHttpPort.c_str()));
	serverDetails.SetSslPort(atoi(strSslPort.c_str()));
	serverDetails.SetFilePath(strFilePath);
	serverDetails.SetProxyPath(strProxyPath);
	return serverDetails;
}

// Convert unsigned char to 2-char hex string
static std::string toHex(unsigned char n)
{
	std::string s;
	static const char digits[] = "0123456789ABCDEF";

	s += digits[n >> 4];
	s += digits[n & 0xf];

	return s;
}

HRESULT LDAPUserPlugin::BintoEscapeSequence(const char* lpdata, size_t size, string* lpEscaped)
{
	lpEscaped->clear();
	for (size_t t = 0; t < size; ++t)
		lpEscaped->append("\\"+toHex(lpdata[t]));
	return 0;
}

std::string LDAPUserPlugin::StringEscapeSequence(const string &strData)
{
	return StringEscapeSequence(strData.c_str(), strData.size());
}

std::string LDAPUserPlugin::StringEscapeSequence(const char* lpdata, size_t size)
{
	std::string strEscaped;

	for (size_t t = 0; t < size; ++t)
		if (lpdata[t] != ' ' && (lpdata[t] < '0' || lpdata[t] > 122 ||
		    (lpdata[t] >= 58 /*:*/ && lpdata[t] <= 64 /*:;<=>?@*/) ||
		    (lpdata[t] >= 91 && lpdata[t] <= 96) /* [\]^_`*/))
			strEscaped.append("\\"+toHex(lpdata[t]));
		else
			strEscaped.append((char*)&lpdata[t], 1);
	return strEscaped;
}

quotadetails_t LDAPUserPlugin::getQuota(const objectid_t &id,
    bool bGetUserDefault)
{
	auto_free_ldap_message res;
	quotadetails_t quotaDetails;

	const char *multiplier_s = m_config->GetSetting("ldap_quota_multiplier");
	long long multiplier = 1L;
	if (multiplier_s != nullptr)
		multiplier = fromstring<const char *, long long>(multiplier_s);

	std::unique_ptr<attrArray> request_attrs(new attrArray(4));
	CONFIG_TO_ATTR(request_attrs, usedefaults_attr,
		bGetUserDefault ?
			"ldap_userdefault_quotaoverride_attribute" :
			 "ldap_quotaoverride_attribute");
	CONFIG_TO_ATTR(request_attrs, warnquota_attr,
		bGetUserDefault ?
			"ldap_userdefault_warnquota_attribute" :
			"ldap_warnquota_attribute");
	CONFIG_TO_ATTR(request_attrs, softquota_attr,
		bGetUserDefault ?
			"ldap_userdefault_softquota_attribute" :
			"ldap_softquota_attribute");
	CONFIG_TO_ATTR(request_attrs, hardquota_attr,
		bGetUserDefault ?
			"ldap_userdefault_hardquota_attribute" :
			"ldap_hardquota_attribute");

	string ldap_basedn = getSearchBase();
	string ldap_filter = getObjectSearchFilter(id);

	/* LDAP filter empty, object does not exist */
	if (ldap_filter.empty())
		throw objectnotfound("no ldap filter for "+id.id);

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);

	// Do a search request to get all attributes for this user. The
	// attributes requested should be passed as the fifth argument, if
	// necessary
	my_ldap_search_s(
			 (char *)ldap_basedn.c_str(), LDAP_SCOPE_SUBTREE,
			 (char *)ldap_filter.c_str(), (char **)request_attrs->get(),
			 FETCH_ATTR_VALS, &~res);
	quotaDetails.bIsUserDefaultQuota = bGetUserDefault;

	// Get the DN for each of the returned entries
	// ldap_first_message would work too, but that needs much more
	// work parsing the results from the ber structs.
	FOREACH_ENTRY(res) {
		FOREACH_ATTR(entry) {
			if (usedefaults_attr != nullptr && strcasecmp(att, usedefaults_attr) == 0)
				// Workarround quotaoverride == !usedefaultquota
				quotaDetails.bUseDefaultQuota = !parseBool(getLDAPAttributeValue(att, entry).c_str());
			else if (warnquota_attr != nullptr && strcasecmp(att, warnquota_attr) == 0)
				quotaDetails.llWarnSize = fromstring<std::string, long long>(getLDAPAttributeValue(att, entry)) * multiplier;
			else if (id.objclass != CONTAINER_COMPANY && softquota_attr != nullptr && strcasecmp(att, softquota_attr) == 0)
				quotaDetails.llSoftSize = fromstring<std::string, long long>(getLDAPAttributeValue(att, entry)) * multiplier;
			else if (id.objclass != CONTAINER_COMPANY && hardquota_attr != nullptr && strcasecmp(att, hardquota_attr) == 0)
				quotaDetails.llHardSize = fromstring<std::string, long long>(getLDAPAttributeValue(att, entry)) * multiplier;
		}
		END_FOREACH_ATTR
	}
	END_FOREACH_ENTRY

	return quotaDetails;
}

void LDAPUserPlugin::setQuota(const objectid_t &id, const quotadetails_t &quotadetails)
{
	throw notimplemented("set quota is not supported when using the LDAP user plugin.");
}

abprops_t LDAPUserPlugin::getExtraAddressbookProperties()
{
	abprops_t lProps;

	LOG_PLUGIN_DEBUG("%s", __FUNCTION__);
	for (const auto &cs : m_config->GetSettingGroup(CONFIGGROUP_PROPMAP))
		lProps.emplace_back(xtoi(cs.szName));
	return lProps;
}

void LDAPUserPlugin::removeAllObjects(objectid_t except)
{
    throw notimplemented("removeAllObjects is not implemented in the LDAP user plugin.");
}

} /* namespace */
