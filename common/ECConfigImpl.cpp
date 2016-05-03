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

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cassert>
#include <climits>
#include <kopano/stringutil.h>
#include "ECConfigImpl.h"

#include <kopano/charset/convert.h>
#include <kopano/boost_compat.h>

using namespace std;

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

const directive_t ECConfigImpl::s_sDirectives[] = {
	{ "include",	&ECConfigImpl::HandleInclude },
	{ "propmap",	&ECConfigImpl::HandlePropMap },
	{ NULL }
};

class PathCompare _zcp_final {
public:
	PathCompare(const fs::path &ref): m_ref(ref) {}
	bool operator()(const fs::path &other) const { return fs::equivalent(m_ref, other); }
private:
	const fs::path &m_ref;
};

// Configuration file parser

ECConfigImpl::ECConfigImpl(const configsetting_t *lpDefaults,
    const char *const *lpszDirectives)
{
	pthread_rwlock_init(&m_settingsRWLock, NULL);
	m_szConfigFile = NULL;
	m_lpDefaults = lpDefaults;

	// allowed directives in this config object
	for (int i = 0; lpszDirectives != NULL && lpszDirectives[i] != NULL; ++i)
		m_lDirectives.push_back(lpszDirectives[i]);

	InitDefaults(LOADSETTING_INITIALIZING | LOADSETTING_UNKNOWN | LOADSETTING_OVERWRITE);
}

bool ECConfigImpl::LoadSettings(const char *szFilename)
{
	m_szConfigFile = szFilename;	
	return InitConfigFile(LOADSETTING_OVERWRITE);
}

static int tounderscore(int c)
{
	return c == '-' ? '_' : c;
}

/**
 * Parse commandline parameters to override the values loaded from the
 * config files.
 * 
 * This function accepts only long options in the form
 * --option-name=value. All dashes in the option-name will be converted
 * to underscores. The option-name should then match a valid config option.
 * This config option will be set to value. No processing is done on value
 * except for removing leading and trailing whitespaces.
 * 
 * The aray in argv will be reordered so all non-long-option values will
 * be located after the long-options. On return *lpargidx will be the
 * index of the first non-long-option in the array.
 * 
 * @param[in]	argc		The number of arguments to parse.
 * @param[in]	argv		The parameters to parse. The size of the
 * 							array must be at least argc.
 * @param[out]	lpargidx	Pointer to an integer that will be set to
 * 							the index in argv where parsing stopped.
 * 							This parameter may be NULL.
 * @retval	true
 */
bool ECConfigImpl::ParseParams(int argc, char *argv[], int *lpargidx)
{
	for (int i = 0; i < argc; ++i) {
		char *arg = argv[i];
		if (arg && arg[0] == '-' && arg[1] == '-') {
			const char *eq = strchr(arg, '=');
			
			if (eq) {
				string strName(arg+2, eq-arg-2);
				string strValue(eq+1);
				
				strName = trim(strName, " \t\r\n");
				strValue = trim(strValue, " \t\r\n");
				
				std::transform(strName.begin(), strName.end(), strName.begin(), tounderscore);
				
				configsetting_t setting = { strName.c_str(), strValue.c_str(), 0, 0 };
				
				// Overwrite an existing setting, and make sure it is not reloadable during HUP
				AddSetting(&setting, LOADSETTING_OVERWRITE | LOADSETTING_CMDLINE_PARAM);
			} else {
				errors.push_back("Commandline option '" + string(arg+2) + "' cannot be empty!");
			}
		} else if (arg) {
			// Move non-long-option to end of list
			--argc;
			for (int j = i; j < argc; ++j)
				argv[j] = argv[j+1];
			argv[argc] = arg;
			--i;
		}
	}
	
	if (lpargidx)
		*lpargidx = argc;

	return true;
}

bool ECConfigImpl::ReloadSettings()
{
	// unsetting this value isn't possible
	if (!m_szConfigFile)
		return false;

	// Check if we can still open the main config file. Do not reset to Defaults
	FILE *fp = NULL;
	if(!(fp = fopen(m_szConfigFile, "rt"))) {
		return false;
	} else {
		fclose(fp);
	}

	// reset to defaults because unset items in config file should return to default values.
	InitDefaults(LOADSETTING_OVERWRITE_RELOAD);

	return InitConfigFile(LOADSETTING_OVERWRITE_RELOAD);
}

bool ECConfigImpl::AddSetting(const char *szName, const char *szValue, const unsigned int ulGroup)
{
	configsetting_t sSetting;

	sSetting.szName = szName;
	sSetting.szValue = szValue;
	sSetting.ulFlags = 0;
	sSetting.ulGroup = ulGroup;

	return AddSetting(&sSetting, ulGroup ? LOADSETTING_OVERWRITE_GROUP : LOADSETTING_OVERWRITE);
}

static void freeSettings(settingmap_t::value_type entry)
{
	// see InsertOrReplace
	delete [] entry.second;
}

void ECConfigImpl::CleanupMap(settingmap_t *lpMap)
{
	if (!lpMap->empty())
		for_each(lpMap->begin(), lpMap->end(), freeSettings);
}

ECConfigImpl::~ECConfigImpl()
{
	pthread_rwlock_wrlock(&m_settingsRWLock);

	CleanupMap(&m_mapSettings);
	CleanupMap(&m_mapAliases);

	pthread_rwlock_unlock(&m_settingsRWLock);

	pthread_rwlock_destroy(&m_settingsRWLock);
}

/** 
 * Returns the size in bytes for a size marked config value
 * 
 * @param szValue input value from config file
 * 
 * @return size in bytes
 */
size_t ECConfigImpl::GetSize(const char *szValue)
{
	size_t rv = 0;
	if (szValue) {
		char *end = NULL;
		rv = strtoul(szValue, &end, 10);
		if (rv && end > szValue && *end != '\0') {
			while (*end != '\0' && (*end == ' ' || *end == '\t'))
				++end;
			switch (tolower(*end)) {
				case 'k': rv *= 1024; break;
				case 'm': rv *= 1024*1024; break;
				case 'g': rv *= 1024*1024*1024; break;
			}
		}
	}
	return rv;
}

/** 
 * Adds a new setting to the map, or replaces the current data.
 * Only the first 1024 bytes of the value are saved, longer values are truncated.
 * The map must be locked by the m_settingsRWLock.
 * 
 * @param lpMap settings map to set value in
 * @param s key to access map point
 * @param szValue new value to set in map
 */
void ECConfigImpl::InsertOrReplace(settingmap_t *lpMap, const settingkey_t &s, const char* szValue, bool bIsSize)
{
	char* data = NULL;
	size_t len = std::min((size_t)1023, strlen(szValue));

	settingmap_t::iterator i = lpMap->find(s);

	if(i == lpMap->end()) {
		// Insert new value
		data = new char[1024];
		lpMap->insert(make_pair(s, data));
	} else {
		// Actually remove and re-insert the map entry since we may be modifying
		// ulFlags in the key (this is a bit of a hack, since you shouldn't be modifying
		// stuff in the key, but this is the easiest)
		data = i->second;
		lpMap->erase(i);
		lpMap->insert(make_pair(s, data));
	}
	
	if (bIsSize)
		len = snprintf(data, 1024, "%lu", GetSize(szValue));
	else
		strncpy(data, szValue, len);
	data[len] = '\0';
}

const char *ECConfigImpl::GetMapEntry(const settingmap_t *lpMap,
    const char *szName)
{
	const char *retval = NULL;
	if (szName == NULL)
		return NULL;

	settingkey_t key = {""};
	if (strlen(szName) >= sizeof(key.s))
		return NULL;

	strcpy(key.s, szName);
	pthread_rwlock_rdlock(&m_settingsRWLock);

	settingmap_t::const_iterator itor = lpMap->find(key);
	if (itor != lpMap->end())
		retval = itor->second;
	pthread_rwlock_unlock(&m_settingsRWLock);
	return retval;
}

const char *ECConfigImpl::GetSetting(const char *szName)
{
	return GetMapEntry(&m_mapSettings, szName);
}

const char *ECConfigImpl::GetAlias(const char *szName)
{
	return GetMapEntry(&m_mapAliases, szName);
}

const char *ECConfigImpl::GetSetting(const char *szName, const char *equal,
    const char *other)
{
	const char *value = this->GetSetting(szName);
	if (value == equal || (value && equal && !strcmp(value, equal)))
		return other;
	else
		return value;
}

const wchar_t *ECConfigImpl::GetSettingW(const char *szName)
{
	const char *value = GetSetting(szName);
	pair<ConvertCache::iterator, bool> result = m_convertCache.insert(ConvertCache::value_type(value, L""));
	ConvertCache::iterator iter = result.first;

	if (result.second)
		iter->second = convert_to<wstring>(value);

	return iter->second.c_str();
}

const wchar_t *ECConfigImpl::GetSettingW(const char *szName,
    const wchar_t *equal, const wchar_t *other)
{
	const wchar_t *value = this->GetSettingW(szName);
	if (value == equal || (value && equal && !wcscmp(value, equal)))
		return other;
	else
		return value;
}

list<configsetting_t> ECConfigImpl::GetSettingGroup(unsigned int ulGroup)
{
	list<configsetting_t> lGroup;
	configsetting_t sSetting;

	for (const auto &s : m_mapSettings)
		if ((s.first.ulGroup & ulGroup) == ulGroup &&
		    CopyConfigSetting(&s.first, s.second, &sSetting))
			lGroup.push_back(sSetting);
	return lGroup;
}

std::list<configsetting_t> ECConfigImpl::GetAllSettings()
{
	list<configsetting_t> lSettings;
	configsetting_t sSetting;

	for (const auto &s : m_mapSettings)
		if (CopyConfigSetting(&s.first, s.second, &sSetting))
			lSettings.push_back(sSetting);
	return lSettings;
}

bool ECConfigImpl::InitDefaults(unsigned int ulFlags)
{
	unsigned int i = 0;

	/* throw error? this is unacceptable! useless object, since it won't set new settings */
	if (!m_lpDefaults)
		return false;

	while (m_lpDefaults[i].szName != NULL) {
		if (m_lpDefaults[i].ulFlags & CONFIGSETTING_ALIAS) {
			/* Aliases are only initialized once */
			if (ulFlags & LOADSETTING_INITIALIZING)
				AddAlias(&m_lpDefaults[i]);
		} else
			AddSetting(&m_lpDefaults[i], ulFlags);
		++i;
	}

	return true;
}

bool ECConfigImpl::InitConfigFile(unsigned int ulFlags)
{
	bool bResult = false;

	assert(m_readFiles.empty());

	if (!m_szConfigFile)
		return false;

	bResult = ReadConfigFile(m_szConfigFile, ulFlags);

	m_readFiles.clear();

	return bResult;
}

bool ECConfigImpl::ReadConfigFile(const path_type &file, unsigned int ulFlags, unsigned int ulGroup)
{
	FILE *fp = NULL;
	bool bReturn = false;
	char cBuffer[MAXLINELEN] = {0};
	string strFilename;
	string strLine;
	string strName;
	string strValue;
	size_t pos;

	// Store the path of the previous file in case we're recursively processing files.
	fs::path prevFile = m_currentFile;
	
	// We need to keep track of the current path so we can handle relative includes in HandleInclude
	m_currentFile = file;

	if (!exists(file)) {
		errors.push_back("Config file '" + path_to_string(file) + "' does not exist.");
		goto exit;
	}
	if (is_directory(file)) {
		errors.push_back("Config file '" + path_to_string(file) + "' is a directory.");
		goto exit;
	}

	/* Check if we read this file before. */
	if (find_if(m_readFiles.begin(), m_readFiles.end(), PathCompare(file)) != m_readFiles.end()) {
		bReturn = true;
		goto exit;
	}

    m_readFiles.insert(file);

	if(!(fp = fopen(path_to_string(file).c_str(), "rt"))) {
		errors.push_back("Unable to open config file '" + path_to_string(file) + "'");
		goto exit;
	}

	while (!feof(fp)) {
		memset(&cBuffer, 0, sizeof(cBuffer));

		if (!fgets(cBuffer, sizeof(cBuffer), fp))
			continue;

		strLine = string(cBuffer);

		/* Skip empty lines any lines which start with # */
		if (strLine.empty() || strLine[0] == '#')
 			continue;

		/* Handle special directives which start with '!' */
		if (strLine[0] == '!') {
			if (!HandleDirective(strLine, ulFlags))
				goto exit;
			continue;
		}

		/* Get setting name */
		pos = strLine.find('=');
		if (pos != string::npos) {
			strName = strLine.substr(0, pos);
			strValue = strLine.substr(pos + 1);
		} else
			continue;

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
		strName = trim(strName, " \t\r\n");
		strValue = trim(strValue, " \t\r\n");

		if(!strName.empty()) {
			// Save it
			configsetting_t setting = { strName.c_str(), strValue.c_str(), 0, static_cast<unsigned short int>(ulGroup) };
			AddSetting(&setting, ulFlags);
		}
	}

	bReturn = true;

exit:
	if(fp) 
		fclose(fp);

	// Restore the path of the previous file.
	m_currentFile.swap(prevFile);

	return bReturn;
}

bool ECConfigImpl::HandleDirective(const string &strLine, unsigned int ulFlags)
{
	size_t pos = strLine.find_first_of(" \t", 1);
	string strName = strLine.substr(1, pos - 1);

	/* Check if this directive is known */
	for (int i = 0; s_sDirectives[i].lpszDirective != NULL; ++i) {
		if (strName.compare(s_sDirectives[i].lpszDirective) == 0) {
			/* Check if this directive is supported */
			list<string>::iterator f = find(m_lDirectives.begin(), m_lDirectives.end(), strName);
			if (f != m_lDirectives.end())
				return (this->*s_sDirectives[i].fExecute)(strLine.substr(pos).c_str(), ulFlags);

			warnings.push_back("Unsupported directive '" + strName + "' found!");
			return true;
		}
	}

	warnings.push_back("Unknown directive '" + strName + "' found!");
	return true;
}

bool ECConfigImpl::HandleInclude(const char *lpszArgs, unsigned int ulFlags)
{
	string strValue;
	path_type file;
	
	file = (strValue = trim(lpszArgs, " \t\r\n"));
	if (!file.is_complete()) {
		// Rebuild the path
		file = remove_filename_from_path(m_currentFile);
		file /= strValue;
	}
	
	return ReadConfigFile(file, ulFlags);
}

bool ECConfigImpl::HandlePropMap(const char *lpszArgs, unsigned int ulFlags)
{
	string	strValue;
	bool	bResult;

	strValue = trim(lpszArgs, " \t\r\n");
	bResult = ReadConfigFile(strValue.c_str(), LOADSETTING_UNKNOWN | LOADSETTING_OVERWRITE_GROUP, CONFIGGROUP_PROPMAP);

	return bResult;
}

bool ECConfigImpl::CopyConfigSetting(const configsetting_t *lpsSetting, settingkey_t *lpsKey)
{
	if (lpsSetting->szName == NULL || lpsSetting->szValue == NULL)
		return false;

	memset(lpsKey, 0, sizeof(*lpsKey));
	kc_strlcpy(lpsKey->s, lpsSetting->szName, sizeof(lpsKey->s));
	lpsKey->ulFlags = lpsSetting->ulFlags;
	lpsKey->ulGroup = lpsSetting->ulGroup;

	return true;
}

bool ECConfigImpl::CopyConfigSetting(const settingkey_t *lpsKey, const char *szValue, configsetting_t *lpsSetting)
{
	if (strlen(lpsKey->s) == 0 || szValue == NULL)
		return false;

	lpsSetting->szName = lpsKey->s;
	lpsSetting->szValue = szValue;
	lpsSetting->ulFlags = lpsKey->ulFlags;
	lpsSetting->ulGroup = lpsKey->ulGroup;

	return true;
}

bool ECConfigImpl::AddSetting(const configsetting_t *lpsConfig, unsigned int ulFlags)
{
	settingmap_t::iterator iterSettings;
	settingkey_t s;
	char *valid = NULL;
	const char *szAlias = NULL;
	bool bReturnValue = true;

	if (!CopyConfigSetting(lpsConfig, &s))
		return false;

	// Lookup name as alias
	szAlias = GetAlias(lpsConfig->szName);
	if (szAlias) {
		if (!(ulFlags & LOADSETTING_INITIALIZING))
			warnings.push_back("Option '" + string(lpsConfig->szName) + "' is deprecated! New name for option is '" + szAlias + "'.");
		kc_strlcpy(s.s, szAlias, sizeof(s.s));
	}

	pthread_rwlock_wrlock(&m_settingsRWLock);

	iterSettings = m_mapSettings.find(s);

	if (iterSettings == m_mapSettings.end()) {
		// new items from file are illegal, add error
		if (!(ulFlags & LOADSETTING_UNKNOWN)) {
			errors.push_back("Unknown option '" + string(lpsConfig->szName) + "' found!");
			goto exit;
		}
	} else {
		// Check for permissions before overwriting
		if (ulFlags & LOADSETTING_OVERWRITE_GROUP) {
			if (iterSettings->first.ulGroup != lpsConfig->ulGroup) {
				errors.push_back("option '" + string(lpsConfig->szName) + "' cannot be overridden (different group)!");
				bReturnValue = false;
				goto exit;
			}
		} else if (ulFlags & LOADSETTING_OVERWRITE_RELOAD) {
			if (!(iterSettings->first.ulFlags & CONFIGSETTING_RELOADABLE)) {
				bReturnValue = false;
				goto exit;
			}
		} else if (!(ulFlags & LOADSETTING_OVERWRITE)) {
			errors.push_back("option '" + string(lpsConfig->szName) + "' cannot be overridden!");
			bReturnValue = false;
			goto exit;
		}

		if (!(ulFlags & LOADSETTING_INITIALIZING) &&
			(iterSettings->first.ulFlags & CONFIGSETTING_UNUSED))
				warnings.push_back("Option '" + string(lpsConfig->szName) + "' is not used anymore.");

		s.ulFlags = iterSettings->first.ulFlags;

		// If this is a commandline parameter, mark the setting as non-reloadable since you do not want to
		// change the value after a HUP
		if (ulFlags & LOADSETTING_CMDLINE_PARAM)
			s.ulFlags &= ~ CONFIGSETTING_RELOADABLE;

	}

	if (lpsConfig->szValue[0] == '$' && (s.ulFlags & CONFIGSETTING_EXACT) == 0) {
		const char *szValue = getenv(lpsConfig->szValue + 1);
		if (szValue == NULL) {
			warnings.push_back("'" + string(lpsConfig->szValue + 1) + "' not found in environment, using '" + lpsConfig->szValue + "' for options '" + lpsConfig->szName + "'.");
			szValue = lpsConfig->szValue;
		}

		if (s.ulFlags & CONFIGSETTING_SIZE) {
			strtoul(szValue, &valid, 10);
			if (valid == szValue) {
				errors.push_back("Option '" + string(lpsConfig->szName) + "' must be a size value (number + optional k/m/g multiplier).");
				bReturnValue = false;
				goto exit;
			}
		}

		InsertOrReplace(&m_mapSettings, s, szValue, lpsConfig->ulFlags & CONFIGSETTING_SIZE);
	} else {
		if (s.ulFlags & CONFIGSETTING_SIZE) {
			strtoul(lpsConfig->szValue, &valid, 10);
			if (valid == lpsConfig->szValue) {
				errors.push_back("Option '" + string(lpsConfig->szName) + "' must be a size value (number + optional k/m/g multiplier).");
				bReturnValue = false;
				goto exit;
			}
		}

		InsertOrReplace(&m_mapSettings, s, lpsConfig->szValue, s.ulFlags & CONFIGSETTING_SIZE);
	}

exit:
	pthread_rwlock_unlock(&m_settingsRWLock);
	return bReturnValue;
}

void ECConfigImpl::AddAlias(const configsetting_t *lpsAlias)
{
	settingkey_t s;

	if (!CopyConfigSetting(lpsAlias, &s))
		return;

	pthread_rwlock_wrlock(&m_settingsRWLock);
	InsertOrReplace(&m_mapAliases, s, lpsAlias->szValue, false);
	pthread_rwlock_unlock(&m_settingsRWLock);
}

bool ECConfigImpl::HasWarnings() {
	return !warnings.empty();
}

bool ECConfigImpl::HasErrors() {
	/* First validate the configuration settings */
	pthread_rwlock_rdlock(&m_settingsRWLock);

	for (const auto &s : m_mapSettings)
		if (s.first.ulFlags & CONFIGSETTING_NONEMPTY)
			if (!s.second || strlen(s.second) == 0)
				errors.push_back("Option '" + string(s.first.s) + "' cannot be empty!");
	
	pthread_rwlock_unlock(&m_settingsRWLock);

	return !errors.empty();
}

bool ECConfigImpl::WriteSettingToFile(const char *szName, const char *szValue, const char* szFileName)
{
	string strOutFileName;
	string strLine;
	string strName;

	strOutFileName = "outfile.cfg";

	ifstream in(szFileName);

	if( !in.is_open())
	{
		cout << "Input confif file failed to open creating it\n";
		std::ofstream createFile(szFileName);
		createFile.close();

		in.open(szFileName);
		if ( !in.is_open())
		{
			cout << "Input file failed to open after trying to create it" << endl;
			return false;
		}
	}

	// open temp output file
	ofstream out(strOutFileName.c_str());

	WriteLinesToFile(szName, szValue, in, out, true);

	in.close();
	out.close();

	// delete the original file
	remove(szFileName);

	// rename out file to new
	rename(strOutFileName.c_str(),szFileName);

	return true;
}

bool ECConfigImpl::WriteSettingsToFile(const char* szFileName)
{
	string strName;
	fs::path pathOutFile;
	fs::path pathBakFile;

	pathOutFile = pathBakFile = szFileName;
	remove_filename_from_path(pathOutFile) /= "config_out.cfg";
	remove_filename_from_path(pathBakFile) /= "config_bak.cfg";

	ifstream in(szFileName);

	if( !in.is_open())
	{
		cout << "Input config file failed to open creating it\n";
		std::ofstream createFile(szFileName);
		createFile.close();

		in.open(szFileName);
		if ( !in.is_open())
		{
			cout << "Input file failed to open after trying to create it" << endl;
			return false;
		}
	}

	// open temp output file
	ofstream out(path_to_string(pathOutFile.string()).c_str());
	const char* szName = NULL;
	const char* szValue = NULL;

	for (const auto &s : m_mapSettings) {
		szName  = s.first.s;
		szValue = s.second;
		this->WriteLinesToFile(szName, szValue, in, out, false);
	}
	in.close();
	out.close();

// the stdio functions does not work in win release mode in some cases
	remove(szFileName);
	rename(path_to_string(pathOutFile).c_str(),szFileName);
	return true;
}

void ECConfigImpl::WriteLinesToFile(const char* szName, const char* szValue, ifstream& in, ofstream& out, bool bWriteAll)
{
	string strLine;
	string strTmp;
	bool bFound = false;

	if (bWriteAll == true)
	{
		// This is done in order to keep comments and config order if there already exist a config
		while( getline(in,strLine) )
		{
			/* Skip empty lines any lines which start with # */
			if (strLine.empty() || strLine[0] == '#'){
				out << strLine << "\n";
				continue;
			}

			/* Handle special directives which start with '!' */
			if (strLine[0] == '!') {
				out << strLine << "\n";
				continue;
			}

			/* Get setting name */
			size_t pos = strLine.find('=');
			if (pos != string::npos) {
				string strName = strLine.substr(0, pos);
				strName = trim(strName, " \t\r\n");

				// Found it, set new value for setting
				if (strName == szName)
				{
					//This is a empty config add comment in front
					if (string(szValue) == "")
						strTmp = "#" + strName + " = " + szValue + "\n";	
					else
						strTmp = strName + " = " + szValue + "\n";	

					out << strTmp;
					bFound = true;
				}
				else
				{
					out << strLine << "\n";
				}
			} else
				continue;
		}
	}

	if (!bFound || bWriteAll == false) // Not found in original config, this is still valid, add it to output
	{				
		if (string(szValue) == "")
			strTmp = "#" + string(szName) + " = " + szValue + "\n";	
		else
			strTmp = string(szName) + " = " + szValue + "\n";	

		out << strTmp;
	}
}
