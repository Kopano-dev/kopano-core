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

#include <iostream>
#include <algorithm>

#include <sys/stat.h>
#include <memory.h>
#include <cstdlib>
#include <cstdio>

using namespace std;

#include "ECConfigCheck.h"

ECConfigCheck::ECConfigCheck(const char *lpszName, const char *lpszConfigFile)
{
	m_lpszName = lpszName;
	m_lpszConfigFile = lpszConfigFile;
	m_bDirty = false;
	m_bHosted = false;
	m_bMulti = false;

	readConfigFile(lpszConfigFile);
}

static string clearCharacters(string s, const string &whitespaces)
{
	size_t pos = 0;

	/*
	 * The line is build up like this:
	 * config_name = bla bla
	 *
	 * Whe should clean it in such a way that it resolves to:
	 * config_name=bla bla
	 *
	 * Be careful _not_ to remove any whitespace characters
	 * within the configuration value itself.
	 */
	pos = s.find_first_not_of(whitespaces);
	s.erase(0, pos);

	pos = s.find_last_not_of(whitespaces);
	if (pos != string::npos)
		s.erase(pos + 1, string::npos);

	return s;
}

void ECConfigCheck::readConfigFile(const char *lpszConfigFile)
{
	FILE *fp = NULL;
	char cBuffer[1024];
	string strLine;
	string strName;
	string strValue;
	size_t pos;

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

		strLine = string(cBuffer);

		/* Skip empty lines any lines which start with # */
		if (strLine.empty() || strLine[0] == '#')
			continue;

		/* Get setting name */
		pos = strLine.find('=');
		if (pos != string::npos) {
			strName = strLine.substr(0, pos);
			strValue = strLine.substr(pos + 1);
		} else
			continue;

		strName = clearCharacters(strName, " \t\r\n");
		strValue = clearCharacters(strValue, " \t\r\n");

		if(!strName.empty())
			m_mSettings[strName] = strValue;
	}

	fclose(fp);
}

bool ECConfigCheck::isDirty()
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
	FILE *fp = NULL;

	/* When grepping iconv output, all lines have '//' appended,
	 * additionally all charsets are uppercase */
	std::string v1 = check->value1;
	std::transform(v1.begin(), v1.end(), v1.begin(), ::toupper);
	fp = popen(("iconv -l | grep -x \"" + v1 + "//\"").c_str(), "r");

	if (fp) {
		char buffer[50];
		string output;

		memset(buffer, 0, sizeof(buffer));

		fread(buffer, sizeof(buffer), 1, fp);
		output = buffer;

		pclose(fp);

		if (output.find(v1) == string::npos) {
			printError(check->option1, "contains unknown chartype \"" + v1 + "\"");
			return CHECK_ERROR;
		}
	} else {
		printWarning(check->option1, "Failed to validate charset");
		return CHECK_WARNING;
	}

	return CHECK_OK;
}

int ECConfigCheck::testBoolean(const config_check_t *check)
{
	std::string v1 = check->value1;
	std::transform(v1.begin(), v1.end(), v1.begin(), ::tolower);

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

	m_lChecks.push_back(check);
}

void ECConfigCheck::addCheck(const std::string &option, unsigned int flags,
    int (*check)(const config_check_t *check))
{
	config_check_t config_check;

	config_check.option1 = option;
	config_check.option2 = "";
	config_check.check = check;

	addCheck(config_check, flags);
}

void ECConfigCheck::addCheck(const std::string &option1,
    const std::string &option2, unsigned int flags,
    int (*check)(const config_check_t *check))
{
	config_check_t config_check;

	config_check.option1 = option1;
	config_check.option2 = option2;
	config_check.check = check;

	addCheck(config_check, flags);
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
