/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <shared_mutex>
#include <string>
#include <utility>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <sys/stat.h>
#include <libHX/string.h>
#include <kopano/ECConfig.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>
#include <kopano/fileutil.hpp>
#include <kopano/charset/convert.h>
#define MAXLINELEN 4096

/* Flags for the InitDefaults & InitConfigFile functions */
enum {
	LOADSETTING_INITIALIZING     = 1 << 0, /* ECConfig is initializing, turns on extra debug information */
	LOADSETTING_UNKNOWN          = 1 << 1, /* Allow adding new configuration options */
	LOADSETTING_OVERWRITE        = 1 << 2, /* Allow overwriting predefined configuration options */
	LOADSETTING_OVERWRITE_GROUP  = 1 << 3, /* Same as CONFIG_LOAD_OVERWRITE but only if options are in the same group */
	LOADSETTING_OVERWRITE_RELOAD = 1 << 4, /* Same as CONFIG_LOAD_OVERWRITE but only if option is marked reloadable */
	LOADSETTING_CMDLINE_PARAM    = 1 << 5, /* This setting is being set from commandline parameters. Sets the option non-reloadable */
	LOADSETTING_MARK_DEFAULT     = 1 << 6, /* This setting is at its default value */
};

namespace KC {

class ECConfigImpl;

struct settingkey_t {
	char s[256];
	unsigned short cs_flags, ulGroup;
	bool operator<(const settingkey_t &o) const noexcept { return strcmp(s, o.s) < 0; }
};

struct directive_t {
	const char *lpszDirective;
	bool (ECConfigImpl::*fExecute)(const char *, unsigned int);
};

/* Note: char* in map is allocated ONCE to 1024, and GetSetting will always return the same pointer to this buffer */
typedef std::map<settingkey_t, char *> settingmap_t;

class ECConfigImpl KC_FINAL : public ECConfig {
	public:
	ECConfigImpl(const configsetting_t *defaults, const char *const *directives);
	~ECConfigImpl();
	virtual bool LoadSettings(const char *file, bool ignore_missing = false) override;
	virtual int ParseParams(int argc, char **argv) override;
	const char *GetSettingsPath() const override { return m_szConfigFile; }
	bool ReloadSettings() override;
	bool AddSetting(const char *name, const char *value, const unsigned int group = 0) override;
	const char *GetSetting(const char *name) override;
	const char *GetSetting(const char *name, const char *equal, const char *other) override;
	std::list<configsetting_t> GetSettingGroup(unsigned int group) override;
	std::list<configsetting_t> GetAllSettings() override;
	bool HasWarnings() override;
	const std::list<std::string> *GetWarnings() override { return &warnings; }
	bool HasErrors() override;
	const std::list<std::string> *GetErrors() override { return &errors; }
	int dump_config(FILE *) override;

	private:
	bool InitDefaults(unsigned int flags);
	bool InitConfigFile(unsigned int flags);
	bool ReadConfigFile(const std::string &file, unsigned int flags, unsigned int group = 0);
	bool HandleDirective(const std::string &line, unsigned int flags);
	bool HandleInclude(const char *args, unsigned int flags);
	bool HandlePropMap(const char *args, unsigned int fags);
	void InsertOrReplace(settingmap_t *, const settingkey_t &s, const char *value, bool is_size);
	const char *GetMapEntry(const settingmap_t *, const char *name);
	const char *GetAlias(const char *alias);
	bool AddSetting(const configsetting_t &, unsigned int flags);
	void AddAlias(const configsetting_t &alias);
	static bool CopyConfigSetting(const configsetting_t &, settingkey_t *);
	static bool CopyConfigSetting(const settingkey_t *, const char *value, configsetting_t *);

	const configsetting_t *m_lpDefaults = nullptr;
	const char *m_szConfigFile = nullptr;
	std::list<std::string> m_lDirectives;

	/* m_mapSettings & m_mapAliases are protected by m_settingsLock */
	KC::shared_mutex m_settingsRWLock;
	settingmap_t m_mapSettings, m_mapAliases;
	std::list<std::string> warnings, errors;
	std::string m_currentFile;
	std::set<std::string> m_readFiles;
	std::map<const char *, std::wstring> m_convertCache;
	static const directive_t s_sDirectives[];
};

using std::string;

const directive_t ECConfigImpl::s_sDirectives[] = {
	{ "include",	&ECConfigImpl::HandleInclude },
	{ "propmap",	&ECConfigImpl::HandlePropMap },
	{ NULL }
};

ECConfig *ECConfig::Create(const configsetting_t *dfl,
    const char *const *direc)
{
	return new ECConfigImpl(dfl, direc);
}

ECConfig *ECConfig::Create(const std::nothrow_t &,
    const configsetting_t *dfl, const char *const *direc)
{
	return new(std::nothrow) ECConfigImpl(dfl, direc);
}

/**
 * Get the default path for the configuration file specified with lpszBasename.
 * Usually this will return '/etc/kopano/<lpszBasename>'. However, the path to
 * the configuration files can be altered by setting the 'KOPANO_CONFIG_PATH'
 * environment variable.
 *
 * @param[in]	lpszBasename
 * 						The basename of the requested configuration file. Passing
 * 						NULL or an empty string will result in the default path
 * 						to be returned.
 *
 * @returns		The full path to the requested configuration file. Memory for
 * 				the returned data is allocated in this function and will be freed
 * 				at program termination.
 *
 * @warning This function is not thread safe!
 */
const char *ECConfig::GetDefaultPath(const char *base)
{
	static std::map<std::string, std::string> s_mapPaths;

	if (base == nullptr)
		base = "";
	auto result = s_mapPaths.emplace(base, "");
	if (result.second) { /* New item added, so create the actual path */
		const char *dir = getenv("KOPANO_CONFIG_PATH");
		if (dir == nullptr || *dir == '\0')
			dir = "/etc/kopano";
		result.first->second = std::string(dir) + "/" + base;
	}
	return result.first->second.c_str();
}

// Configuration file parser

ECConfigImpl::ECConfigImpl(const configsetting_t *lpDefaults,
    const char *const *lpszDirectives) :
	m_lpDefaults(lpDefaults)
{
	// allowed directives in this config object
	for (int i = 0; lpszDirectives != NULL && lpszDirectives[i] != NULL; ++i)
		m_lDirectives.emplace_back(lpszDirectives[i]);
	InitDefaults(LOADSETTING_INITIALIZING | LOADSETTING_UNKNOWN | LOADSETTING_OVERWRITE);
}

bool ECConfigImpl::LoadSettings(const char *szFilename, bool ignore_missing)
{
	struct stat sb;
	if (stat(szFilename, &sb) != 0 && errno == ENOENT && ignore_missing)
		return true;
	m_szConfigFile = szFilename;
	return InitConfigFile(LOADSETTING_OVERWRITE);
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
 * The array in argv will be reordered so all non-long-option values will
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
int ECConfigImpl::ParseParams(int argc, char **argv)
{
	for (int i = 0; i < argc; ++i) {
		char *arg = argv[i];
		if (arg == nullptr)
			continue;
		if (arg[0] != '-' || arg[1] != '-') {
			// Move non-long-option to end of list
			--argc;
			for (int j = i; j < argc; ++j)
				argv[j] = argv[j+1];
			argv[argc] = arg;
			--i;
			continue;
		}
		const char *eq = strchr(arg, '=');
		if (eq == nullptr) {
			errors.emplace_back("Commandline option '" + std::string(arg + 2) + "' cannot be empty!");
			continue;
		}
		auto strName = trim(std::string(arg + 2, eq - arg - 2), " \t\r\n");
		auto strValue = trim(std::string(eq + 1), " \t\r\n");
		std::transform(strName.begin(), strName.end(), strName.begin(),
			[](int c) { return c == '-' ? '_' : c; });
		// Overwrite an existing setting, and make sure it is not reloadable during HUP
		AddSetting({strName.c_str(), strValue.c_str()}, LOADSETTING_OVERWRITE | LOADSETTING_CMDLINE_PARAM);
	}
	return argc;
}

bool ECConfigImpl::ReloadSettings()
{
	// unsetting this value isn't possible
	if (!m_szConfigFile)
		return false;

	// Check if we can still open the main config file. Do not reset to Defaults
	FILE *fp = fopen(m_szConfigFile, "rt");
	if (fp == nullptr)
		return false;
	fclose(fp);
	// reset to defaults because unset items in config file should return to default values.
	InitDefaults(LOADSETTING_OVERWRITE_RELOAD);
	return InitConfigFile(LOADSETTING_OVERWRITE_RELOAD);
}

bool ECConfigImpl::AddSetting(const char *szName, const char *szValue, const unsigned int ulGroup)
{
	return AddSetting({szName, szValue, 0, static_cast<unsigned short>(ulGroup)},
	       ulGroup ? LOADSETTING_OVERWRITE_GROUP : LOADSETTING_OVERWRITE);
}

ECConfigImpl::~ECConfigImpl()
{
	std::lock_guard<KC::shared_mutex> lset(m_settingsRWLock);
	for (auto &e : m_mapSettings)
		delete[] e.second;
	for (auto &e : m_mapAliases)
		delete[] e.second;
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
	size_t len = std::min(static_cast<size_t>(1023), strlen(szValue));

	auto i = lpMap->find(s);
	if (i == lpMap->cend()) {
		// Insert new value
		data = new char[1024];
		lpMap->insert({s, data});
	} else if (i->second == szValue) {
		/* Our pointer was handed back to us (AddSetting("x", GetSetting("x"))) */
		return;
	} else {
		// Actually remove and re-insert the map entry since we may be modifying
		// cs_flags in the key (this is a bit of a hack, since you shouldn't be modifying
		// stuff in the key, but this is the easiest)
		data = i->second;
		lpMap->erase(i);
		lpMap->insert({s, data});
	}

	if (bIsSize)
		len = snprintf(data, 1024, "%zu", static_cast<size_t>(humansize_to_number(szValue)));
	else
		strncpy(data, szValue, len);
	data[len] = '\0';
}

const char *ECConfigImpl::GetMapEntry(const settingmap_t *lpMap,
    const char *szName)
{
	if (szName == NULL)
		return NULL;

	settingkey_t key = {""};
	if (strlen(szName) >= sizeof(key.s))
		return NULL;
	strcpy(key.s, szName);
	std::shared_lock<KC::shared_mutex> lset(m_settingsRWLock);
	auto itor = lpMap->find(key);
	if (itor != lpMap->cend())
		return itor->second;
	return nullptr;
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
	auto value = GetSetting(szName);
	if (value == equal || (value && equal && !strcmp(value, equal)))
		return other;
	else
		return value;
}

std::list<configsetting_t> ECConfigImpl::GetSettingGroup(unsigned int ulGroup)
{
	std::list<configsetting_t> lGroup;
	configsetting_t sSetting;

	for (const auto &s : m_mapSettings)
		if ((s.first.ulGroup & ulGroup) == ulGroup &&
		    CopyConfigSetting(&s.first, s.second, &sSetting))
			lGroup.emplace_back(std::move(sSetting));
	return lGroup;
}

std::list<configsetting_t> ECConfigImpl::GetAllSettings()
{
	std::list<configsetting_t> lSettings;
	configsetting_t sSetting;

	for (const auto &s : m_mapSettings)
		if (CopyConfigSetting(&s.first, s.second, &sSetting))
			lSettings.emplace_back(std::move(sSetting));
	return lSettings;
}

bool ECConfigImpl::InitDefaults(unsigned int ls_flags)
{
	unsigned int i = 0;

	/* throw error? this is unacceptable! useless object, since it won't set new settings */
	if (!m_lpDefaults)
		return false;

	while (m_lpDefaults[i].szName != NULL) {
		if (m_lpDefaults[i].cs_flags & CONFIGSETTING_ALIAS) {
			/* Aliases are only initialized once */
			if (ls_flags & LOADSETTING_INITIALIZING)
				AddAlias(m_lpDefaults[i]);
			++i;
			continue;
		}
		auto f = ls_flags | LOADSETTING_MARK_DEFAULT;
		AddSetting(m_lpDefaults[i++], f);
	}
	return true;
}

bool ECConfigImpl::InitConfigFile(unsigned int ls_flags)
{
	assert(m_readFiles.empty());

	if (!m_szConfigFile)
		return false;
	auto bResult = ReadConfigFile(m_szConfigFile, ls_flags);
	m_readFiles.clear();
	return bResult;
}

bool ECConfigImpl::ReadConfigFile(const std::string &file,
    unsigned int ls_flags, unsigned int ulGroup)
{
	char cBuffer[MAXLINELEN]{};
	std::string strFilename, strLine, strName, strValue;
	size_t pos;

	std::unique_ptr<char[], cstdlib_deleter> normalized_file(realpath(file.c_str(), nullptr));
	if (normalized_file == nullptr) {
		errors.emplace_back("Cannot normalize path \"" + file + "\": " + strerror(errno));
		return false;
	}
	struct stat sb;
	if (stat(normalized_file.get(), &sb) < 0) {
		errors.emplace_back("Config file \"" + file + "\" cannot be read: " + strerror(errno));
		return false;
	}
	if (!S_ISREG(sb.st_mode)) {
		errors.emplace_back("Config file \"" + file + "\" is not a file");
		return false;
	}

	// Store the path of the previous file in case we're recursively processing files.
	// We need to keep track of the current path so we can handle relative includes in HandleInclude
	std::string prevFile = m_currentFile;
	auto cleanup = make_scope_success([&]() { m_currentFile = std::move(prevFile); });
	m_currentFile = normalized_file.get();

	/* Check if we read this file before. */
	if (std::find(m_readFiles.cbegin(), m_readFiles.cend(), m_currentFile) != m_readFiles.cend())
		return true;
	m_readFiles.emplace(m_currentFile);
	std::unique_ptr<FILE, file_deleter> fp(fopen(file.c_str(), "rt"));
	if (fp == nullptr) {
		errors.emplace_back(format("Unable to open config file \"%s\": %s", file.c_str(), strerror(errno)));
		return false;
	}

	while (!feof(fp.get())) {
		memset(&cBuffer, 0, sizeof(cBuffer));
		if (!fgets(cBuffer, sizeof(cBuffer), fp.get()))
			continue;

		strLine = string(cBuffer);
		/* Skip empty lines any lines which start with # */
		if (strLine.empty() || strLine[0] == '#')
 			continue;
		/* Handle special directives which start with '!' */
		if (strLine[0] == '!') {
			if (!HandleDirective(strLine, ls_flags))
				return false;
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
		 * We should clean it in such a way that it resolves to:
		 * config_name=bla bla
		 *
		 * Be careful _not_ to remove any whitespace characters
		 * within the configuration value itself.
		 */
		strName = trim(strName, " \t\r\n");
		strValue = trim(strValue, " \t\r\n");
		if (!strName.empty())
			// Save it
			AddSetting({strName.c_str(), strValue.c_str(), 0, static_cast<unsigned short>(ulGroup)}, ls_flags);
	}
	return true;
}

bool ECConfigImpl::HandleDirective(const string &strLine, unsigned int ls_flags)
{
	size_t pos = strLine.find_first_of(" \t", 1);
	string strName = strLine.substr(1, pos - 1);

	/* Check if this directive is known */
	for (int i = 0; s_sDirectives[i].lpszDirective != NULL; ++i) {
		if (strName.compare(s_sDirectives[i].lpszDirective) != 0)
			continue;
		/* Check if this directive is supported */
		auto f = std::find(m_lDirectives.cbegin(), m_lDirectives.cend(), strName);
		if (f != m_lDirectives.cend())
			return (this->*s_sDirectives[i].fExecute)(strLine.substr(pos).c_str(), ls_flags);
		warnings.emplace_back("Unsupported directive '" + strName + "' found!");
		return true;
	}

	warnings.emplace_back("Unknown directive '" + strName + "' found!");
	return true;
}

bool ECConfigImpl::HandleInclude(const char *lpszArgs, unsigned int ls_flags)
{
	auto strValue = trim(lpszArgs, " \t\r\n");
	auto file = strValue;

	if (file[0] != PATH_SEPARATOR) {
		// Rebuild the path. m_currentFile is always a normalized path.
		auto pos = m_currentFile.find_last_of(PATH_SEPARATOR);
		file = (pos != std::string::npos) ? m_currentFile.substr(0, pos) : ".";
		file += PATH_SEPARATOR;
		file += strValue;
	}
	return ReadConfigFile(file, ls_flags);
}

bool ECConfigImpl::HandlePropMap(const char *lpszArgs, unsigned int ls_flags)
{
	return ReadConfigFile(trim(lpszArgs, " \t\r\n").c_str(),
	       LOADSETTING_UNKNOWN | LOADSETTING_OVERWRITE_GROUP, CONFIGGROUP_PROPMAP);
}

bool ECConfigImpl::CopyConfigSetting(const configsetting_t &lpsSetting,
    settingkey_t *lpsKey)
{
	if (lpsSetting.szName == nullptr || lpsSetting.szValue == nullptr)
		return false;
	memset(lpsKey, 0, sizeof(*lpsKey));
	HX_strlcpy(lpsKey->s, lpsSetting.szName, sizeof(lpsKey->s));
	lpsKey->cs_flags = lpsSetting.cs_flags;
	lpsKey->ulGroup = lpsSetting.ulGroup;
	return true;
}

bool ECConfigImpl::CopyConfigSetting(const settingkey_t *lpsKey, const char *szValue, configsetting_t *lpsSetting)
{
	if (strlen(lpsKey->s) == 0 || szValue == NULL)
		return false;
	lpsSetting->szName = lpsKey->s;
	lpsSetting->szValue = szValue;
	lpsSetting->cs_flags = lpsKey->cs_flags;
	lpsSetting->ulGroup = lpsKey->ulGroup;
	return true;
}

bool ECConfigImpl::AddSetting(const configsetting_t &lpsConfig,
    unsigned int ls_flags)
{
	settingkey_t s;
	char *valid = NULL;
	if (!CopyConfigSetting(lpsConfig, &s))
		return false;

	// Lookup name as alias
	auto szAlias = GetAlias(lpsConfig.szName);
	if (szAlias) {
		if (!(ls_flags & LOADSETTING_INITIALIZING))
			warnings.emplace_back("Option \"" + std::string(lpsConfig.szName) + "\" is deprecated! New name for option is \"" + szAlias + "\".");
		HX_strlcpy(s.s, szAlias, sizeof(s.s));
	}

	std::lock_guard<KC::shared_mutex> lset(m_settingsRWLock);
	auto iterSettings = m_mapSettings.find(s);
	if (iterSettings == m_mapSettings.cend()) {
		// new items from file are illegal, add error
		if (!(ls_flags & LOADSETTING_UNKNOWN)) {
			errors.emplace_back("Unknown option \"" + std::string(lpsConfig.szName) + "\" found!");
			return true;
		}
	} else {
		// Check for permissions before overwriting
		if (ls_flags & LOADSETTING_OVERWRITE_GROUP) {
			if (iterSettings->first.ulGroup != lpsConfig.ulGroup) {
				errors.emplace_back("option \"" + std::string(lpsConfig.szName) + "\" cannot be overridden (different group)!");
				return false;
			}
		} else if (ls_flags & LOADSETTING_OVERWRITE_RELOAD) {
			if (!(iterSettings->first.cs_flags & CONFIGSETTING_RELOADABLE))
				return false;
		} else if (!(ls_flags & LOADSETTING_OVERWRITE)) {
			errors.emplace_back("option \"" + std::string(lpsConfig.szName) + "\" cannot be overridden!");
			return false;
		}

		if (!(ls_flags & LOADSETTING_INITIALIZING) &&
		    (iterSettings->first.cs_flags & CONFIGSETTING_OBSOLETE))
			warnings.emplace_back("Option \"" + std::string(lpsConfig.szName) + "\" is recognized but obsolete, and will be removed in a future release.");
		if (!(ls_flags & LOADSETTING_INITIALIZING) &&
		    (iterSettings->first.cs_flags & CONFIGSETTING_UNUSED))
			warnings.emplace_back("Option \"" + std::string(lpsConfig.szName) + "\" has no effect anymore, and will be removed in a future release.");
		s.cs_flags = iterSettings->first.cs_flags;

		// If this is a commandline parameter, mark the setting as non-reloadable since you do not want to
		// change the value after a HUP
		if (ls_flags & LOADSETTING_CMDLINE_PARAM)
			s.cs_flags &= ~CONFIGSETTING_RELOADABLE;
	}

	if (lpsConfig.szValue[0] == '$' && !(s.cs_flags & CONFIGSETTING_EXACT)) {
		const char *szValue = getenv(lpsConfig.szValue + 1);
		if (szValue == NULL) {
			warnings.emplace_back("\"" + std::string(lpsConfig.szValue + 1) + "\" not found in the environment, using \"" + lpsConfig.szValue + "\" for options \"" + lpsConfig.szName + "\".");
			szValue = lpsConfig.szValue;
		}
		if (s.cs_flags & CONFIGSETTING_SIZE) {
			strtoul(szValue, &valid, 10);
			if (valid == szValue) {
				errors.emplace_back("Option \"" + std::string(lpsConfig.szName) + "\" must be a size value (number + optional k/m/g multiplier).");
				return false;
			}
		}
		InsertOrReplace(&m_mapSettings, s, szValue, lpsConfig.cs_flags & CONFIGSETTING_SIZE);
		return true;
	}
	if (s.cs_flags & CONFIGSETTING_SIZE) {
		strtoul(lpsConfig.szValue, &valid, 10);
		if (valid == lpsConfig.szValue) {
			errors.emplace_back("Option \"" + std::string(lpsConfig.szName) + "\" must be a size value (number + optional k/m/g multiplier).");
			return false;
		}
	}
	if (ls_flags & LOADSETTING_MARK_DEFAULT)
		s.cs_flags |= CONFIGSETTING_MARK_DEFAULT;
	else
		s.cs_flags &= ~CONFIGSETTING_MARK_DEFAULT;
	InsertOrReplace(&m_mapSettings, s, lpsConfig.szValue, s.cs_flags & CONFIGSETTING_SIZE);
	return true;
}

void ECConfigImpl::AddAlias(const configsetting_t &lpsAlias)
{
	settingkey_t s;

	if (!CopyConfigSetting(lpsAlias, &s))
		return;

	std::lock_guard<KC::shared_mutex> lset(m_settingsRWLock);
	InsertOrReplace(&m_mapAliases, s, lpsAlias.szValue, false);
}

bool ECConfigImpl::HasWarnings() {
	return !warnings.empty();
}

bool ECConfigImpl::HasErrors() {
	/* First validate the configuration settings */
	std::shared_lock<KC::shared_mutex> lset(m_settingsRWLock);
	for (const auto &s : m_mapSettings)
		if (s.first.cs_flags & CONFIGSETTING_NONEMPTY)
			if (!s.second || strlen(s.second) == 0)
				errors.emplace_back("Option '" + std::string(s.first.s) + "' cannot be empty!");
	return !errors.empty();
}

int ECConfigImpl::dump_config(FILE *fp)
{
	std::lock_guard<KC::shared_mutex> lset(m_settingsRWLock);
	for (const auto &p : m_mapSettings) {
		if (p.first.cs_flags & CONFIGSETTING_UNUSED)
			continue;
		if (p.first.cs_flags & CONFIGSETTING_MARK_DEFAULT)
			fprintf(fp, "# ");
		auto ret = fprintf(fp, "%s = %s\n", p.first.s, p.second);
		if (ret < 0)
			return ret;
	}
	return 0;
}

} /* namespace */
