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

/* ArchiveManage.h
 * Declaration of class ArchiveManage
 */
#ifndef ARCHIVEMANAGE_H_INCLUDED
#define ARCHIVEMANAGE_H_INCLUDED

#include <kopano/platform.h>
#include <list>
#include <mapix.h>
#include <kopano/ArchiveControl.h>

class ECLogger;

struct ArchiveEntry {
	std::string StoreName;
	std::string FolderName;
	std::string StoreOwner;
	unsigned Rights;
	std::string StoreGuid;
};
typedef std::list<ArchiveEntry> ArchiveList;

struct UserEntry {
	std::string UserName;
};
typedef std::list<UserEntry> UserList;

class ArchiveManage {
public:
	enum {
		UseIpmSubtree = 1,
		Writable = 2,
		ReadOnly = 4
	};

	typedef std::auto_ptr<ArchiveManage>	auto_ptr_type;

	virtual ~ArchiveManage() {};

	static HRESULT Create(LPMAPISESSION lpSession, ECLogger *lpLogger, const TCHAR *lpszUser, auto_ptr_type *lpptrManage);

	virtual eResult AttachTo(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder, unsigned int ulFlags) = 0;
	virtual eResult DetachFrom(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder) = 0;
	virtual eResult DetachFrom(unsigned int ulArchive) = 0;
	virtual eResult ListArchives(std::ostream &ostr) = 0;
	virtual eResult ListArchives(ArchiveList *lplstArchives, const char *lpszIpmSubtreeSubstitude = NULL) = 0;
	virtual eResult ListAttachedUsers(std::ostream &ostr) = 0;
	virtual eResult ListAttachedUsers(UserList *lplstUsers) = 0;
	virtual eResult AutoAttach(unsigned int ulFlags) = 0;

protected:
	ArchiveManage() {};
};

typedef ArchiveManage::auto_ptr_type	ArchiveManagePtr;

#endif // !defined ARCHIVEMANAGE_H_INCLUDED
