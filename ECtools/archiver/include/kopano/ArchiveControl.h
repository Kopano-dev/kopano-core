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

/* ArchiveControl.h
 * Declaration of class ArchiveControl
 */
#ifndef ARCHIVECONTROL_H_INCLUDED
#define ARCHIVECONTROL_H_INCLUDED

#include <memory>
#include <kopano/tstring.h>

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

class ArchiveControl 
{
public:
	typedef std::auto_ptr<ArchiveControl>	auto_ptr_type;

	virtual ~ArchiveControl() {};

	virtual eResult ArchiveAll(bool bLocalOnly, bool bAutoAttach, unsigned int ulFlags) = 0;
	virtual eResult Archive(const tstring& strUser, bool bAutoAttach, unsigned int ulFlags) = 0;
	virtual eResult CleanupAll(bool bLocalOnly) = 0;
	virtual eResult Cleanup(const tstring& strUser) = 0;

protected:
	ArchiveControl() {};
};

typedef ArchiveControl::auto_ptr_type	ArchiveControlPtr;

#endif // !defined ARCHIVECONTROL_H_INCLUDED
