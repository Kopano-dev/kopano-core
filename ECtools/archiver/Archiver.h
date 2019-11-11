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
	KC_HIDDEN static const configsetting_t *GetConfigDefaults();
	static eResult Create(std::unique_ptr<Archiver> *);
	KC_HIDDEN virtual ~Archiver() = default;
	KC_HIDDEN virtual eResult Init(const char *app_name, const char *config, const configsetting_t *extra_opts = nullptr, unsigned int flags = 0) = 0;
	KC_HIDDEN virtual eResult GetControl(ArchiveControlPtr *, bool force_cleanup = false) = 0;
	KC_HIDDEN virtual eResult GetManage(const TCHAR *user, ArchiveManagePtr *) = 0;
	KC_HIDDEN virtual eResult AutoAttach(unsigned int flags) = 0;
	KC_HIDDEN virtual ECConfig *GetConfig() const = 0;
	KC_HIDDEN virtual ECLogger *GetLogger(eLogType which = DefaultLog) const = 0;

protected:
	KC_HIDDEN Archiver() {}
};

} /* namespace */

#endif // !defined ARCHIVER_H_INCLUDED
