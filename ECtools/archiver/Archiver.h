/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ARCHIVER_H_INCLUDED
#define ARCHIVER_H_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include "ArchiveManage.h" // for ArchiveManagePtr

namespace KC {

class ECConfig;

#define ARCHIVE_RIGHTS_ERROR	(unsigned)-1
#define ARCHIVE_RIGHTS_ABSENT	(unsigned)-2

struct configsetting_t;

class KC_EXPORT Archiver {
public:
	enum {
		RequireConfig		= 0x00000001,
		AttachStdErr		= 0x00000002,
		DumpConfig              = 0x00000004,
		InhibitErrorLogging	= 0x40000000	// To silence Init errors in the unit test.
	};

	enum eLogType {
		DefaultLog		= 0,
		LogOnly			= 1
	};

	static const char *GetConfigPath(void);
	_kc_hidden static const configsetting_t *GetConfigDefaults(void);
	static eResult Create(std::unique_ptr<Archiver> *);
	_kc_hidden virtual ~Archiver(void) = default;
	_kc_hidden virtual eResult Init(const char *app_name, const char *config, const configsetting_t *extra_opts = nullptr, unsigned int flags = 0) = 0;
	_kc_hidden virtual eResult GetControl(ArchiveControlPtr *, bool force_cleanup = false) = 0;
	_kc_hidden virtual eResult GetManage(const TCHAR *user, ArchiveManagePtr *) = 0;
	_kc_hidden virtual eResult AutoAttach(unsigned int flags) = 0;
	_kc_hidden virtual ECConfig *GetConfig(void) const = 0;
	_kc_hidden virtual ECLogger *GetLogger(eLogType which = DefaultLog) const = 0;

protected:
	_kc_hidden Archiver(void) {};
};

} /* namespace */

#endif // !defined ARCHIVER_H_INCLUDED
