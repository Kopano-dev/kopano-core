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

#ifndef ECCONFIGIMPL_H
#define ECCONFIGIMPL_H

#include <kopano/zcdefs.h>

using namespace std;

#include <kopano/ECConfig.h>

#include <map>
#include <set>
#include <list>
#include <string>
#include <cstring>
#include <pthread.h>

#include <boost/filesystem/path.hpp>

#include <iostream>
#include <fstream>

struct settingkey_t {
	char s[256];
	unsigned short ulFlags;
	unsigned short ulGroup;
};

struct settingcompare
{
	bool operator()(const settingkey_t &a, const settingkey_t &b) const
	{
		return strcmp(a.s, b.s) < 0;
	}
};

#define MAXLINELEN 4096

class ECConfigImpl;

/* Note: char* in map is allocated ONCE to 1024, and GetSetting will always return the same pointer to this buffer */
typedef std::map<settingkey_t, char*, settingcompare> settingmap_t;
typedef bool (ECConfigImpl::*directive_func_t)(const char *, unsigned int);
typedef struct {
	const char			*lpszDirective;
	directive_func_t	fExecute;
} directive_t;

/*
 * Flags for the InitDefaults & InitConfigFile functions
 */
#define LOADSETTING_INITIALIZING		0x0001	/* ECConfig is initializing, turns on extra debug information */
#define LOADSETTING_UNKNOWN				0x0002	/* Allow adding new configuration options */
#define LOADSETTING_OVERWRITE			0x0004	/* Allow overwriting predefined configuration options */
#define LOADSETTING_OVERWRITE_GROUP		0x0008	/* Same as CONFIG_LOAD_OVERWRITE but only if options are in the same group */
#define LOADSETTING_OVERWRITE_RELOAD	0x0010	/* Same as CONFIG_LOAD_OVERWRITE but only if option is marked reloadable */
#define LOADSETTING_CMDLINE_PARAM		0x0020	/* This setting is being set from commandline parameters. Sets the option non-reloadable */

class ECConfigImpl _zcp_final : public ECConfig {
public:
	ECConfigImpl(const configsetting_t *lpDefaults, const char *const *lpszDirectives);
	~ECConfigImpl();

	bool LoadSettings(const char *szFilename) _zcp_override;
	virtual bool ParseParams(int argc, char *argv[], int *lpargidx) _zcp_override;
	const char *GetSettingsPath(void) _kc_override { return m_szConfigFile; }
	bool ReloadSettings(void) _zcp_override;

	bool AddSetting(const char *szName, const char *szValue, const unsigned int ulGroup = 0) _zcp_override;
	const char *GetSetting(const char *szName) _zcp_override;
	const char *GetSetting(const char *szName, const char *equal, const char *other) _zcp_override;
	const wchar_t *GetSettingW(const char *szName) _zcp_override;
	const wchar_t *GetSettingW(const char *szName, const wchar_t *equal, const wchar_t *other) _zcp_override;

	std::list<configsetting_t> GetSettingGroup(unsigned int ulGroup) _zcp_override;
	std::list<configsetting_t> GetAllSettings(void) _zcp_override;

	bool HasWarnings(void) _zcp_override;
	const std::list<std::string> *GetWarnings(void) _kc_override { return &warnings; }
	bool HasErrors(void) _zcp_override;
	const std::list<std::string> *GetErrors(void) _kc_override { return &errors; }

private:
	typedef boost::filesystem::path path_type;

	bool	InitDefaults(unsigned int ulFlags);
	bool	InitConfigFile(unsigned int ulFlags);
	bool	ReadConfigFile(const path_type &file, unsigned int ulFlags, unsigned int ulGroup = 0);

	bool	HandleDirective(const std::string &strLine, unsigned int ulFlags);
	bool	HandleInclude(const char *lpszArgs, unsigned int ulFlags);
	bool	HandlePropMap(const char *lpszArgs, unsigned int ulFlags);

	size_t  GetSize(const char *szValue);
	void	InsertOrReplace(settingmap_t *lpMap, const settingkey_t &s, const char* szValue, bool bIsSize);

	const char *GetMapEntry(const settingmap_t *lpMap, const char *szName);
	const char *GetAlias(const char *szAlias);

	bool	AddSetting(const configsetting_t *lpsConfig, unsigned int ulFlags);
	void	AddAlias(const configsetting_t *lpsAlias);

	void	CleanupMap(settingmap_t *lpMap);
	bool	CopyConfigSetting(const configsetting_t *lpsSetting, settingkey_t *lpsKey);
	bool	CopyConfigSetting(const settingkey_t *lpsKey, const char *szValue, configsetting_t *lpsSetting);

private:
	const configsetting_t	*m_lpDefaults;
	const char*				m_szConfigFile;
	std::list<std::string>	m_lDirectives;

	/* m_mapSettings & m_mapAliases are protected by m_settingsLock */
	pthread_rwlock_t m_settingsRWLock;

	settingmap_t			m_mapSettings;
	settingmap_t			m_mapAliases;
	std::list<std::string>	warnings;
	std::list<std::string>	errors;
	
	path_type			m_currentFile;
	std::set<path_type>	m_readFiles;

	typedef std::map<const char*, std::wstring>	ConvertCache;
	ConvertCache		m_convertCache;

	static const directive_t	s_sDirectives[];
};

#endif // ECCONFIGIMPL_H
