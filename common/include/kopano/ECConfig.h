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

#ifndef ECCONFIG_H
#define ECCONFIG_H

#include <list>
#include <string>

struct configsetting_t {
	const char *szName;
	const char *szValue;
	unsigned short ulFlags;
#define CONFIGSETTING_ALIAS			0x0001
#define CONFIGSETTING_RELOADABLE	0x0002
#define CONFIGSETTING_UNUSED		0x0004
#define CONFIGSETTING_NONEMPTY		0x0008
#define CONFIGSETTING_EXACT			0x0010
#define CONFIGSETTING_SIZE			0x0020
	unsigned short ulGroup;
#define CONFIGGROUP_PROPMAP			0x0001
};

#ifdef UNICODE
#define GetConfigSetting(_config, _name) ((_config)->GetSettingW(_name))
#else
#define GetConfigSetting(_config, _name) ((_config)->GetSetting(_name))
#endif

static const char *const lpszDEFAULTDIRECTIVES[] = {"include", NULL};

class ECConfig {
public:
	static ECConfig *Create(const configsetting_t *lpDefaults, const char *const *lpszDirectives = lpszDEFAULTDIRECTIVES);
	static const char* GetDefaultPath(const char* lpszBasename);

	virtual ~ECConfig(void) {};

	virtual bool	LoadSettings(const char *szFilename) = 0;
	virtual bool	LoadSettings(const wchar_t *szFilename);
	virtual bool    ParseParams(int argc, char *argv[], int *lpargidx) = 0;
	virtual const char*	GetSettingsPath() = 0;
	virtual bool	ReloadSettings() = 0;

	virtual bool	AddSetting(const char *szName, const char *szValue, const unsigned int ulGroup = 0) = 0;

	virtual const char *GetSetting(const char *szName) = 0;
	virtual const char *GetSetting(const char *szName, const char *equal, const char *other) = 0;
	virtual const wchar_t *GetSettingW(const char *szName) = 0;
	virtual const wchar_t *GetSettingW(const char *szName, const wchar_t *equal, const wchar_t *other) = 0;

	virtual std::list<configsetting_t> GetSettingGroup(unsigned int ulGroup) = 0;
	virtual std::list<configsetting_t> GetAllSettings() = 0;

	virtual bool	HasWarnings() = 0;
	virtual const std::list<std::string> *GetWarnings(void) = 0;
	virtual bool	HasErrors() = 0;
	virtual const std::list<std::string> *GetErrors(void) = 0;
};

#endif // ECCONFIG_H
