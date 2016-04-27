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

#ifndef ARCHIVER_H_INCLUDED
#define ARCHIVER_H_INCLUDED

#include "ArchiveManage.h" // for ArchiveManagePtr

class ECConfig;

#define ARCHIVE_RIGHTS_ERROR	(unsigned)-1
#define ARCHIVE_RIGHTS_ABSENT	(unsigned)-2

#define ARCHIVER_API

struct configsetting_t;

class Archiver {
public:
	typedef std::auto_ptr<Archiver>		auto_ptr_type;

	enum {
		RequireConfig		= 0x00000001,
		AttachStdErr		= 0x00000002,
		InhibitErrorLogging	= 0x40000000	// To silence Init errors in the unit test.
	};

	enum eLogType {
		DefaultLog		= 0,
		LogOnly			= 1
	};

	static const char* ARCHIVER_API GetConfigPath();
	static const configsetting_t* ARCHIVER_API GetConfigDefaults();
	static eResult ARCHIVER_API Create(auto_ptr_type *lpptrArchiver);

	virtual ~Archiver() {};

	virtual eResult Init(const char *lpszAppName, const char *lpszConfig, const configsetting_t *lpExtraSettings = NULL, unsigned int ulFlags = 0) = 0;

	virtual eResult GetControl(ArchiveControlPtr *lpptrControl, bool bForceCleanup = false) = 0;
	virtual eResult GetManage(const TCHAR *lpszUser, ArchiveManagePtr *lpptrManage) = 0;
	virtual eResult AutoAttach(unsigned int ulFlags) = 0;

	virtual ECConfig* GetConfig() const = 0;
	virtual ECLogger* GetLogger(eLogType which = DefaultLog) const = 0;

protected:
	Archiver() {};
};

typedef Archiver::auto_ptr_type		ArchiverPtr;

#endif // !defined ARCHIVER_H_INCLUDED
