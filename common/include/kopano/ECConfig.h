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
/* value is still unchanged from hard-coded defaults (internal flag) */
#define CONFIGSETTING_MARK_DEFAULT 0x0080
#define CONFIGGROUP_PROPMAP			0x0001
};

static const char *const lpszDEFAULTDIRECTIVES[] = {"include", NULL};

class KC_EXPORT ECConfig {
public:
	static ECConfig *Create(const configsetting_t *defaults, const char *const *directives = lpszDEFAULTDIRECTIVES);
	static ECConfig *Create(const std::nothrow_t &, const configsetting_t *defaults, const char *const *directives = lpszDEFAULTDIRECTIVES);
	static const char *GetDefaultPath(const char *basename);
	KC_HIDDEN virtual ~ECConfig() = default;
	KC_HIDDEN virtual bool LoadSettings(const char *file, bool ignore_missing = false) = 0;
	KC_HIDDEN virtual int ParseParams(int argc, char **argv) = 0;
	KC_HIDDEN virtual const char *GetSettingsPath() const = 0;
	KC_HIDDEN virtual bool	ReloadSettings() = 0;
	KC_HIDDEN virtual bool	AddSetting(const char *name, const char *value, unsigned int group = 0) = 0;
	KC_HIDDEN virtual const char *GetSetting(const char *name) = 0;
	KC_HIDDEN virtual const char *GetSetting(const char *name, const char *equal, const char *other) = 0;
	KC_HIDDEN virtual std::list<configsetting_t> GetSettingGroup(unsigned int group) = 0;
	KC_HIDDEN virtual std::list<configsetting_t> GetAllSettings() = 0;
	KC_HIDDEN virtual bool HasWarnings() = 0;
	KC_HIDDEN virtual const std::list<std::string> *GetWarnings() = 0;
	KC_HIDDEN virtual bool	HasErrors() = 0;
	KC_HIDDEN virtual const std::list<std::string> *GetErrors() = 0;
	KC_HIDDEN virtual int dump_config(FILE *) = 0;
};

} /* namespace */

#endif // ECCONFIG_H
