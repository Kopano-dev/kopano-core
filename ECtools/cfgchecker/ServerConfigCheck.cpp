/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "ECConfigCheck.h"
#include <kopano/stringutil.h>

using namespace KC;

ServerConfigCheck::ServerConfigCheck(const char *lpszConfigFile) : ECConfigCheck("Server Configuration file", lpszConfigFile)
{
	std::string setting = getSetting("enable_hosted_kopano");
	if (!setting.empty())
		setHosted(parseBool(setting.c_str()));
	setting = getSetting("enable_distributed_kopano");
	if (!setting.empty())
		setMulti(parseBool(setting.c_str()));
}

void ServerConfigCheck::loadChecks()
{
	addCheck("attachment_storage", 0, &testAttachment);
	addCheck("attachment_storage", "attachment_path", 0, &testAttachmentPath);

	addCheck("user_plugin", 0, &testPlugin);
	addCheck("user_plugin", "user_plugin_config", 0, &testPluginConfig);
	addCheck("user_plugin", "plugin_path", 0, &testPluginPath);

	addCheck("createuser_script", 0, &testFile);
	addCheck("deleteuser_script", 0, &testFile);
	addCheck("creategroup_script", 0, &testFile);
	addCheck("deletegroup_script", 0, &testFile);
	addCheck("createcompany_script", CONFIG_HOSTED_USED, &testFile);
	addCheck("deletecompany_script", CONFIG_HOSTED_USED, &testFile);

	addCheck("enable_hosted_kopano", 0, &testBoolean);
	addCheck("storename_format", 0, &testStorename);
	addCheck("user_plugin", "loginname_format", 0, &testLoginname);

	addCheck("enable_distributed_kopano", 0, &testBoolean);
	addCheck("server_name", CONFIG_MULTI_USED);
	addCheck("enable_gab", 0, &testBoolean);
	addCheck("enable_sso_ntlmauth", 0, &testBoolean);
	addCheck("client_update_enabled", 0, &testBoolean);
	addCheck("hide_everyone", 0, &testBoolean);
	addCheck("enable_enhanced_ics", 0, &testBoolean);

	addCheck("softdelete_lifetime", 0, &testNonZero);

	addCheck("auth_method", 0, &testAuthMethod);
}

int ServerConfigCheck::testAttachment(const config_check_t *check)
{
	if (check->value1.empty())
		return CHECK_OK;

	if (check->value1 == "database" || check->value1 == "files" ||
	    check->value1 == "files_v2")
		return CHECK_OK;

	printError(check->option1, "contains unknown storage type: \"" + check->value1 + "\"");
	return CHECK_ERROR;
}

int ServerConfigCheck::testAttachmentPath(const config_check_t *check)
{
	if (check->value1 != "files" && check->value1 != "files_v2")
		return CHECK_OK;

	config_check_t check2;
	check2.hosted = check->hosted;
	check2.multi = check->multi;
	check2.option1 = check->option2;
	check2.value1 = check->value2;

	return testDirectory(&check2);
}

int ServerConfigCheck::testPlugin(const config_check_t *check)
{
	if (check->value1.empty()) {
		printWarning(check->option1, "Plugin not set, defaulting to 'db' plugin");
		return CHECK_OK;
	}

	if (check->hosted && check->value1 == "unix") {
		printError(check->option1, "Unix plugin does not support multi-tenancy");
		return CHECK_ERROR;
	}
	if (check->multi && check->value1 == "ldap") {
		printError(check->option1, "Unix plugin does not support multiserver");
		return CHECK_ERROR;
	}

	if (check->value1 == "ldap" || check->value1 == "ldapms" || check->value1 == "unix" || check->value1 == "db")
		return CHECK_OK;

	printError(check->option1, "contains unknown plugin: \"" + check->value1 + "\"");
	return CHECK_ERROR;
}

int ServerConfigCheck::testPluginConfig(const config_check_t *check)
{
	if (check->value1 != "ldap" && check->value1 != "unix")
		return CHECK_OK;

	config_check_t check2;
	check2.hosted = check->hosted;
	check2.option1 = check->option2;
	check2.value1 = check->value2;

	return testFile(&check2);
}

int ServerConfigCheck::testPluginPath(const config_check_t *check)
{
	if (check->value1 != "ldap" && check->value1 != "unix")
		return CHECK_OK;

	config_check_t check2;
	check2.hosted = check->hosted;
	check2.option1 = check->option2;
	check2.value1 = check->value2;

	return testDirectory(&check2);
}

int ServerConfigCheck::testStorename(const config_check_t *check)
{
	if (check->hosted)
		return CHECK_OK;
	if (check->value1.find("%c") != std::string::npos) {
		printError(check->option1, "multi-tenancy disabled, but value contains %c: " + check->value1);
		return CHECK_ERROR;
	}
	return CHECK_OK;
}

int ServerConfigCheck::testLoginname(const config_check_t *check)
{
	/* LDAP has no rules for loginname */
	if (check->value1 == "ldap")
		return CHECK_OK;

	if (check->value1 == "unix") {
		if (check->value2.find("%c") != std::string::npos) {
			printError(check->option2, "contains %c but this is not supported by Unix plugin");
			return CHECK_ERROR;
		}
		return CHECK_OK;
	}

	/* DB plugin, must have %c in loginname format when hosted is enabled */
	if (check->hosted) {
		if (check->value2.find("%c") == std::string::npos) {
			printError(check->option2, "multi-tenancy enabled, but value does not contain %c: \"" + check->value2 + "\"");
			return CHECK_ERROR;
		}
	} else if (check->value2.find("%c") != std::string::npos) {
		printError(check->option2, "multi-tenancy disabled, but value contains %c: \"" + check->value2 + "\"");
		return CHECK_ERROR;
	}

	return CHECK_OK;
}

int ServerConfigCheck::testAuthMethod(const config_check_t *check)
{
	if (check->value1.empty() || check->value1 == "plugin" || check->value1 == "pam" || check->value1 == "kerberos")
		return CHECK_OK;
	printError(check->option1, "must be one of 'plugin', 'pam' or 'kerberos': " + check->value1);
	return CHECK_ERROR;
}
