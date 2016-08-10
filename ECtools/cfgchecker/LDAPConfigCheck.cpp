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

#include "LDAPConfigCheck.h"

LDAPConfigCheck::LDAPConfigCheck(const char *lpszConfigFile) : ECConfigCheck("LDAP Configuration file", lpszConfigFile)
{
}

void LDAPConfigCheck::loadChecks()
{
	// TODO: add check for ldap_host is resolveable ip address
	// TODO: add check for ldap_port on ldap_host is reachable
	addCheck("ldap_bind_user", CONFIG_MANDATORY);
	addCheck("ldap_last_modification_attribute", CONFIG_MANDATORY);

	addCheck("ldap_server_charset", 0, &testCharset);

	addCheck("ldap_search_base", CONFIG_MANDATORY);
	addCheck("ldap_object_type_attribute", CONFIG_MANDATORY);
	addCheck("ldap_user_type_attribute_value", CONFIG_MANDATORY);
	addCheck("ldap_group_type_attribute_value", CONFIG_MANDATORY);
	addCheck("ldap_company_type_attribute_value", CONFIG_MANDATORY | CONFIG_HOSTED_USED);
	addCheck("ldap_contact_type_attribute_value", 0);
	addCheck("ldap_addresslist_type_attribute_value", 0);
	addCheck("ldap_dynamicgroup_type_attribute_value", 0);
	addCheck("ldap_server_type_attribute_value", CONFIG_MULTI_USED);

	addCheck("ldap_user_search_filter", 0, &testLdapQuery);
	addCheck("ldap_group_search_filter", 0, &testLdapQuery);
	addCheck("ldap_company_search_filter", 0, &testLdapQuery);

	addCheck("ldap_user_unique_attribute", CONFIG_MANDATORY);
	addCheck("ldap_user_unique_attribute_type", 0, &testLdapType);
	addCheck("ldap_group_unique_attribute", CONFIG_MANDATORY);
	addCheck("ldap_group_unique_attribute_type", 0, &testLdapType);
	addCheck("ldap_groupmembers_attribute_type", 0, &testLdapType);
	addCheck("ldap_company_unique_attribute", CONFIG_HOSTED_USED | CONFIG_MANDATORY);
	addCheck("ldap_company_unique_attribute_type", CONFIG_HOSTED_USED, &testLdapType);
	addCheck("ldap_company_view_attribute_type", CONFIG_HOSTED_USED, &testLdapType);
	addCheck("ldap_company_admin_attribute_type", CONFIG_HOSTED_USED, &testLdapType);
	addCheck("ldap_company_system_admin_attribute_type", CONFIG_HOSTED_USED, &testLdapType);
	addCheck("ldap_quota_userwarning_recipients_attribute_type", 0, &testLdapType);
	addCheck("ldap_quota_companywarning_recipients_attribute_type", 0, &testLdapType);
}

int LDAPConfigCheck::testLdapScope(const config_check_t *check)
{
	if (check->value1.empty() ||
		check->value1 == "base" ||
		check->value1 == "one" ||
		check->value1 == "sub")
			return CHECK_OK;

	printError(check->option1, "contains unknown scope \"" + check->value1 + "\"");
	return CHECK_ERROR;
}

int LDAPConfigCheck::testLdapType(const config_check_t *check)
{
	if (check->value1.empty() ||
		check->value1 == "text" ||
		check->value1 == "binary" ||
		check->value1 == "dn" ||
		check->value1 == "attribute")
			return CHECK_OK;

	printError(check->option1, "contains unknown type \"" + check->value1 + "\"");
	return CHECK_ERROR;
}

int LDAPConfigCheck::testLdapQuery(const config_check_t *check)
{
	std::string stack;
	bool contains_data;     /* '(' was followed by attribute comparison */
	bool contains_check;    /* '=' found */

	/* Empty queries are always correct */
	if (check->value1.empty())
		return CHECK_OK;

	/* If the query contains any of the following characters it is broken */
	if (check->value1.find_first_of("{}") != std::string::npos) {
		printError(check->option1, "contains invalid character: \"" + check->value1 + "\"");
		return CHECK_ERROR;
	}

	/* Queries _must_ always be enclosed by '(' and ')',
	 * note that this check will pass '(a=1)(b=2)' as correct,
	 * we will block this in the for loop.
	 */
	if (*check->value1.begin() != '(' || *check->value1.rbegin() != ')')
		goto error_exit;

	/* Since we already checked the first character, we can add it to the stack */
	stack += check->value1[0];
	contains_data = false;
	contains_check = false;

	/* Loop through the string, the following queries must be marked broken:
	 *      (a=1)(a=2)      => Must be enclosed by '(' and ')'
	 *      ((a=1)          => Requires equal number '(' and ')'
	 *      (a=1))          => Requires equal number '(' and ')'
	 *      (a)             => A check consists of attribute=value comparison
	 *      ((a=1)(a=2))    => Requires '|' or '&' to combine the 2 queries
	 *      (&a=1)          => '|' and '&' should only be present in front of '('
	 */
	for (std::string::const_iterator i = check->value1.begin() + 1; i != check->value1.end(); ++i) {
		if (stack.empty())
			goto error_exit;

		switch (*i) {
		case '(':
			if (contains_data)
				goto error_exit;
			if (*stack.rbegin() != '|' &&
				*stack.rbegin() != '&')
					goto error_exit;
			stack += *i;
			break;
		case ')':
			if (!contains_data || !contains_check)
				goto error_exit;

			stack.erase(stack.end() - 1);
			contains_data = false;
			contains_check = false;
			break;
		case '|':
		case '&':
			if (contains_data)
				goto error_exit;
			stack += *i;
			break;
		case '=':
			if (!contains_data)
				goto error_exit;
			contains_check = true;
			break;
		default:
			if (*stack.rbegin() == '|' ||
				*stack.rbegin() == '&')
					goto error_exit;
			contains_data = true;
			break;
		}
	}

	return CHECK_OK;

error_exit:
	printError(check->option1, "contains malformatted string: \"" + check->value1 + "\"");
	return CHECK_ERROR;
}

