/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/* ArchiveControl.h
 * Declaration of class ArchiveControl
 */
#ifndef ARCHIVECONTROL_H_INCLUDED
#define ARCHIVECONTROL_H_INCLUDED

#include <kopano/zcdefs.h>
#include <memory>

namespace KC {

enum eResult {
	Success = 0,
	Failure,
	Uninitialized,
	OutOfMemory,
	InvalidParameter,
	FileNotFound,
	InvalidConfig,
	PartialCompletion
};

class ArchiveControl {
public:
	virtual ~ArchiveControl(void) = default;
	virtual eResult ArchiveAll(bool bLocalOnly, bool bAutoAttach, unsigned int ulFlags) = 0;
	virtual eResult Archive(const tstring& strUser, bool bAutoAttach, unsigned int ulFlags) = 0;
	virtual eResult CleanupAll(bool bLocalOnly) = 0;
	virtual eResult Cleanup(const tstring& strUser) = 0;

protected:
	ArchiveControl(void) = default;
};

typedef std::unique_ptr<ArchiveControl> ArchiveControlPtr;

} /* namespace */

#endif // !defined ARCHIVECONTROL_H_INCLUDED
