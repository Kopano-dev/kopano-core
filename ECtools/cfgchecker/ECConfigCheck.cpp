/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#include <iostream>
#include <algorithm>
#include <string>
#include <utility>
#include <sys/stat.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iconv.h>
#include <kopano/stringutil.h>
#include "ECConfigCheck.h"

using std::cerr;
using std::cout;
using std::endl;

ECConfigCheck::ECConfigCheck(const char *lpszName, const char *lpszConfigFile) :
	m_lpszName(lpszName), m_lpszConfigFile(lpszConfigFile)
{
	readConfigFile(lpszConfigFile);
}

static void clearCharacters(std::string &s, const std::string &whitespaces)
{
	/*
	 * The line is build up like this:
	 * config_name = bla bla
	 *
	 * We should clean it in such a way that it resolves to:
	 * config_name=bla bla
	 *
	 * Be careful _not_ to remove any whitespace characters
	 * within the configuration value itself.
	 */
	auto pos = s.find_first_not_of(whitespaces);
	s.erase(0, pos);

	pos = s.find_last_not_of(whitespaces);
	if (pos != std::string::npos)
		s.erase(pos + 1, std::string::npos);
}

void ECConfigCheck::readConfigFile(const char *lpszConfigFile)
{
	FILE *fp = NULL;
	char cBuffer[1024];

	if (!lpszConfigFile) {
		m_bDirty = true;
		return;
	}

	if(!(fp = fopen(lpszConfigFile, "rt"))) {
		m_bDirty = true;
		return;
	}

	while (!feof(fp)) {
		memset(&cBuffer, 0, sizeof(cBuffer));

		if (!fgets(cBuffer, sizeof(cBuffer), fp))
			continue;

		std::string strLine = cBuffer, strName, strValue;

		/* Skip empty lines any lines which start with # */
		if (strLine.empty() || strLine[0] == '#')
			continue;

		/* Get setting name */
		auto pos = strLine.find('=');
		if (pos != std::string::npos) {
			strName = strLine.substr(0, pos);
			strValue = strLine.substr(pos + 1);
		} else
			continue;

		clearCharacters(strName, " \t\r\n");
		clearCharacters(strValue, " \t\r\n");
		if(!strName.empty())
			m_mSettings[strName] = strValue;
	}

	fclose(fp);
}

bool ECConfigCheck::isDirty() const
{
	if (m_bDirty)
		cerr << "Validation of " << m_lpszName << " failed: file could not be read (" << m_lpszConfigFile << ")" << endl;
	return m_bDirty;
}

void ECConfigCheck::setHosted(bool hosted)
{
	m_bHosted = hosted;
}

void ECConfigCheck::setMulti(bool multi)
{
	m_bMulti = multi;
}

void ECConfigCheck::validate()
{
	int warnings = 0;
	int errors = 0;

	cout << "Starting configuration validation of " << m_lpszName << endl;

	for (auto &c : m_lChecks) {
		c.hosted = m_bHosted;
		c.multi = m_bMulti;
		c.value1 = getSetting(c.option1);
		c.value2 = getSetting(c.option2);
		int retval = 0;
		if (c.check)
			retval = c.check(&c);
		warnings += (retval == CHECK_WARNING);
		errors += (retval == CHECK_ERROR);
	}

	cout << "Validation of " << m_lpszName << " ended with " << warnings << " warnings and " << errors << " errors" << endl;
}

int ECConfigCheck::testMandatory(const config_check_t *check)
{
	if (!check->value1.empty())
		return CHECK_OK;

	printError(check->option1, "required option is empty");
	return CHECK_ERROR;
}

int ECConfigCheck::testDirectory(const config_check_t *check)
{
	struct stat statfile;

	if (check->value1.empty())
		return CHECK_OK;

	// check if path exists, and is a directory (not a symlink)
	if (stat(check->value1.c_str(), &statfile) == 0 && S_ISDIR(statfile.st_mode))
		return CHECK_OK;

	printError(check->option1, "does not point to existing direcory: \"" + check->value1 + "\"");
	return CHECK_ERROR;
}

int ECConfigCheck::testFile(const config_check_t *check)
{
	struct stat statfile;

	if (check->value1.empty())
		return CHECK_OK;

	// check if file exists, and is a normal file (not a symlink, or a directory
	if (stat(check->value1.c_str(), &statfile) == 0 && S_ISREG(statfile.st_mode))
		return CHECK_OK;

	printError(check->option1, "does not point to existing file: \"" + check->value1 + "\"");
	return CHECK_ERROR;
}

int ECConfigCheck::testUsedWithHosted(const config_check_t *check)
{
	if (check->hosted)
		return CHECK_OK;

	if (check->value1.empty())
		return CHECK_OK;

	printWarning(check->option1, "Multi-company disabled: this option will be ignored");
	return CHECK_WARNING;
}

int ECConfigCheck::testUsedWithoutHosted(const config_check_t *check)
{
	if (!check->hosted)
		return CHECK_OK;

	if (check->value1.empty())
		return CHECK_OK;

	printWarning(check->option1, "Multi-company enabled: this option will be ignored");
	return CHECK_WARNING;
}

int ECConfigCheck::testUsedWithMultiServer(const config_check_t *check)
{
	if (check->multi)
		return CHECK_OK;

	if (check->value1.empty())
		return CHECK_OK;

	printWarning(check->option1, "Multi-server disabled: this option will be ignored");
	return CHECK_WARNING;
}

int ECConfigCheck::testUsedWithoutMultiServer(const config_check_t *check)
{
	if (!check->multi)
		return CHECK_OK;

	if (check->value1.empty())
		return CHECK_OK;

	printWarning(check->option1, "Multi-server enabled: this option will be ignored");
	return CHECK_WARNING;
}

int ECConfigCheck::testCharset(const config_check_t *check)
{
	iconv_t cd = iconv_open("WCHAR_T", check->value1.c_str());
	if (cd == reinterpret_cast<iconv_t>(-1)) {
		printError(check->option1, "contains unknown charset \"" + check->value1 + "\"");
		iconv_close(cd);
		return CHECK_ERROR;
	}

	iconv_close(cd);
	return CHECK_OK;
}

int ECConfigCheck::testBoolean(const config_check_t *check)
{
	auto v1 = KC::strToLower(check->value1);
	if (v1.empty() || v1 == "true" || v1 == "false" || v1 == "yes" ||
	    v1 == "no")
		return CHECK_OK;

	printError(check->option1, "does not contain boolean value: \"" + v1 + "\"");
	return CHECK_ERROR;
}

int ECConfigCheck::testNonZero(const config_check_t *check)
{
	if (check->value1.empty() || atoi(check->value1.c_str()))
		return CHECK_OK;
	printError(check->option1, "must contain a positive (non-zero) value: " + check->value1);
	return CHECK_ERROR;
}

void ECConfigCheck::addCheck(const config_check_t &check, unsigned int flags)
{
	if (flags & CONFIG_MANDATORY) {
		if (!check.option1.empty())
			addCheck(check.option1, 0, &testMandatory);
		if (!check.option2.empty())
			addCheck(check.option2, 0, &testMandatory);
	}

	if (flags & CONFIG_HOSTED_UNUSED) {
		if (!check.option1.empty())
			addCheck(check.option1, 0, &testUsedWithoutHosted);
		if (!check.option2.empty())
			addCheck(check.option2, 0, &testUsedWithoutHosted);
	}
	if (flags & CONFIG_HOSTED_USED) {
		if (!check.option1.empty())
			addCheck(check.option1, 0, &testUsedWithHosted);
		if (!check.option2.empty())
			addCheck(check.option2, 0, &testUsedWithHosted);
	}

	if (flags & CONFIG_MULTI_UNUSED) {
		if (!check.option1.empty())
			addCheck(check.option1, 0, &testUsedWithoutMultiServer);
		if (!check.option2.empty())
			addCheck(check.option2, 0, &testUsedWithoutMultiServer);
	}
	if (flags & CONFIG_MULTI_USED) {
		if (!check.option1.empty())
			addCheck(check.option1, 0, &testUsedWithMultiServer);
		if (!check.option2.empty())
			addCheck(check.option2, 0, &testUsedWithMultiServer);
	}

	m_lChecks.emplace_back(check);
}

void ECConfigCheck::addCheck(const std::string &option, unsigned int flags,
    int (*check)(const config_check_t *check))
{
	config_check_t config_check;

	config_check.option1 = option;
	config_check.option2.clear();
	config_check.check = check;
	addCheck(std::move(config_check), flags);
}

void ECConfigCheck::addCheck(const std::string &option1,
    const std::string &option2, unsigned int flags,
    int (*check)(const config_check_t *check))
{
	config_check_t config_check;

	config_check.option1 = option1;
	config_check.option2 = option2;
	config_check.check = check;
	addCheck(std::move(config_check), flags);
}

const std::string &ECConfigCheck::getSetting(const std::string &option)
{
	return m_mSettings[option];
}

void ECConfigCheck::printError(const std::string &option,
    const std::string &message)
{
	cerr << "[ERROR] " << option << ": " << message << endl;
}

void ECConfigCheck::printWarning(const std::string &option,
    const std::string &message)
{
	cerr << "[WARNING] " << option << ": " << message << endl;
}

DAgentConfigCheck::DAgentConfigCheck(const char *lpszConfigFile) : ECConfigCheck("DAgent Configuration file", lpszConfigFile)
{
}

void DAgentConfigCheck::loadChecks()
{
	addCheck("lmtp_max_threads", 0, &testNonZero);
}

MonitorConfigCheck::MonitorConfigCheck(const char *lpszConfigFile) :
	ECConfigCheck("Monitor Configuration file", lpszConfigFile)
{}

void MonitorConfigCheck::loadChecks()
{
	addCheck("companyquota_warning_template", CONFIG_MANDATORY | CONFIG_HOSTED_USED, &testFile);
	addCheck("userquota_warning_template", CONFIG_MANDATORY, &testFile);
	addCheck("userquota_soft_template", CONFIG_MANDATORY, &testFile);
	addCheck("userquota_hard_template", CONFIG_MANDATORY, &testFile);
}

SpoolerConfigCheck::SpoolerConfigCheck(const char *lpszConfigFile) :
	ECConfigCheck("Spooler Configuration file", lpszConfigFile)
{}

void SpoolerConfigCheck::loadChecks()
{
	addCheck("max_threads", 0, &testNonZero);
	addCheck("always_send_delegates", 0, &testBoolean);
	addCheck("allow_redirect_spoofing", 0, &testBoolean);
	addCheck("copy_delegate_mails", 0, &testBoolean);
	addCheck("always_send_tnef", 0, &testBoolean);
}

UnixConfigCheck::UnixConfigCheck(const char *lpszConfigFile) :
	ECConfigCheck("Unix Configuration file", lpszConfigFile)
{}

void UnixConfigCheck::loadChecks()
{
	addCheck("default_domain", CONFIG_MANDATORY);
	addCheck("fullname_charset", 0, &testCharset);
	addCheck("min_user_uid", "max_user_uid", CONFIG_MANDATORY, &testId);
	addCheck("min_group_gid", "max_group_gid", CONFIG_MANDATORY, &testId);
}

int UnixConfigCheck::testId(const config_check_t *check)
{
	if (atoi(check->value1.c_str()) < atoi(check->value2.c_str()))
		return CHECK_OK;
	printError(check->option1, "is equal or greater then \"" + check->option2 +
		"\" (" + check->value1 + ">=" + check->value2 + ")");
	return CHECK_ERROR;
}
