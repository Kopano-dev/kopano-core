/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECCONFIG_H
#define ECCONFIG_H

#include <kopano/zcdefs.h>
#include <list>
#include <string>

namespace KC {

struct configsetting_t {
	const char *szName, *szValue;
	unsigned short cs_flags, ulGroup;
#define CONFIGSETTING_ALIAS			0x0001
#define CONFIGSETTING_RELOADABLE	0x0002
#define CONFIGSETTING_UNUSED		0x0004
#define CONFIGSETTING_NONEMPTY		0x0008
#define CONFIGSETTING_EXACT			0x0010
#define CONFIGSETTING_SIZE			0x0020
#define CONFIGSETTING_OBSOLETE 0x0040
/* value is still unchanged from hard-coded defautls (internal flag) */
#define CONFIGSETTING_MARK_DEFAULT 0x0080
#define CONFIGGROUP_PROPMAP			0x0001
};

#define GetConfigSetting(_config, _name) ((_config)->GetSettingW(_name))

static const char *const lpszDEFAULTDIRECTIVES[] = {"include", NULL};

class _kc_export ECConfig {
public:
	static ECConfig *Create(const configsetting_t *defaults, const char *const *directives = lpszDEFAULTDIRECTIVES);
	static ECConfig *Create(const std::nothrow_t &, const configsetting_t *defaults, const char *const *directives = lpszDEFAULTDIRECTIVES);
	static const char *GetDefaultPath(const char *basename);
	_kc_hidden virtual ~ECConfig(void) = default;
	_kc_hidden virtual bool LoadSettings(const char *file, bool ignore_missing = false) = 0;
	_kc_hidden virtual int ParseParams(int argc, char **argv) = 0;
	_kc_hidden virtual const char *GetSettingsPath() const = 0;
	_kc_hidden virtual bool	ReloadSettings(void) = 0;
	_kc_hidden virtual bool	AddSetting(const char *name, const char *value, unsigned int group = 0) = 0;
	_kc_hidden virtual const char *GetSetting(const char *name) = 0;
	_kc_hidden virtual const char *GetSetting(const char *name, const char *equal, const char *other) = 0;
	_kc_hidden virtual const wchar_t *GetSettingW(const char *name) = 0;
	_kc_hidden virtual const wchar_t *GetSettingW(const char *name, const wchar_t *equal, const wchar_t *other) = 0;
	_kc_hidden virtual std::list<configsetting_t> GetSettingGroup(unsigned int group) = 0;
	_kc_hidden virtual std::list<configsetting_t> GetAllSettings(void) = 0;
	_kc_hidden virtual bool	HasWarnings(void) = 0;
	_kc_hidden virtual const std::list<std::string> *GetWarnings(void) = 0;
	_kc_hidden virtual bool	HasErrors(void) = 0;
	_kc_hidden virtual const std::list<std::string> *GetErrors(void) = 0;
	_kc_hidden virtual int dump_config(FILE *) = 0;
};

} /* namespace */

#endif // ECCONFIG_H
