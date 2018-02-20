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
#include <kopano/ECConfig.h>

#include <map>
#include <set>
#include <list>
#include <string>
#include <cstring>
#include <pthread.h>
#include <iostream>
#include <fstream>

namespace KC {

struct settingkey_t {
	char s[256];
	unsigned short ulFlags, ulGroup;
	bool operator<(const settingkey_t &o) const noexcept { return strcmp(s, o.s) < 0; }
};

#define MAXLINELEN 4096

class ECConfigImpl;

/* Note: char* in map is allocated ONCE to 1024, and GetSetting will always return the same pointer to this buffer */
typedef std::map<settingkey_t, char *> settingmap_t;
typedef bool (ECConfigImpl::*directive_func_t)(const char *, unsigned int);
struct directive_t {
	const char			*lpszDirective;
	directive_func_t	fExecute;
};

/*
 * Flags for the InitDefaults & InitConfigFile functions
 */
#define LOADSETTING_INITIALIZING		0x0001	/* ECConfig is initializing, turns on extra debug information */
#define LOADSETTING_UNKNOWN				0x0002	/* Allow adding new configuration options */
#define LOADSETTING_OVERWRITE			0x0004	/* Allow overwriting predefined configuration options */
#define LOADSETTING_OVERWRITE_GROUP		0x0008	/* Same as CONFIG_LOAD_OVERWRITE but only if options are in the same group */
#define LOADSETTING_OVERWRITE_RELOAD	0x0010	/* Same as CONFIG_LOAD_OVERWRITE but only if option is marked reloadable */
#define LOADSETTING_CMDLINE_PARAM		0x0020	/* This setting is being set from commandline parameters. Sets the option non-reloadable */
#define LOADSETTING_MARK_DEFAULT                0x0040  /* This setting is at its default value */
#define LOADSETTING_MARK_UNUSED                 0x0080  /* This setting has no effect */

class ECConfigImpl _kc_final : public ECConfig {
public:
	ECConfigImpl(const configsetting_t *lpDefaults, const char *const *lpszDirectives);
	~ECConfigImpl();
	virtual bool LoadSettings(const char *file, bool ignore_missing = false) _kc_override;
	virtual int ParseParams(int argc, char **argv) _kc_override;
	const char *GetSettingsPath() const _kc_override { return m_szConfigFile; }
	bool ReloadSettings(void) _kc_override;
	bool AddSetting(const char *name, const char *value, const unsigned int group = 0) _kc_override;
	const char *GetSetting(const char *name) _kc_override;
	const char *GetSetting(const char *name, const char *equal, const char *other) _kc_override;
	const wchar_t *GetSettingW(const char *name) _kc_override;
	const wchar_t *GetSettingW(const char *name, const wchar_t *equal, const wchar_t *other) _kc_override;
	std::list<configsetting_t> GetSettingGroup(unsigned int group) _kc_override;
	std::list<configsetting_t> GetAllSettings(void) _kc_override;
	bool HasWarnings(void) _kc_override;
	const std::list<std::string> *GetWarnings(void) _kc_override { return &warnings; }
	bool HasErrors(void) _kc_override;
	const std::list<std::string> *GetErrors(void) _kc_override { return &errors; }
	int dump_config(FILE *) override;

private:
	bool	InitDefaults(unsigned int ulFlags);
	bool	InitConfigFile(unsigned int ulFlags);
	bool	ReadConfigFile(const std::string &file, unsigned int ulFlags, unsigned int ulGroup = 0);
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
	static bool CopyConfigSetting(const configsetting_t *, settingkey_t *);
	static bool CopyConfigSetting(const settingkey_t *, const char *value, configsetting_t *);

	const configsetting_t	*m_lpDefaults;
	const char *m_szConfigFile = nullptr;
	std::list<std::string>	m_lDirectives;

	/* m_mapSettings & m_mapAliases are protected by m_settingsLock */
	KC::shared_mutex m_settingsRWLock;
	settingmap_t			m_mapSettings;
	settingmap_t			m_mapAliases;
	std::list<std::string>	warnings;
	std::list<std::string>	errors;
	std::string m_currentFile;
	std::set<std::string> m_readFiles;

	typedef std::map<const char*, std::wstring>	ConvertCache;
	ConvertCache		m_convertCache;

	static const directive_t	s_sDirectives[];
};

} /* namespace */

#endif // ECCONFIGIMPL_H
