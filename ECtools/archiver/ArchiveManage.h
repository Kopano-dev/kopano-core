/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/* ArchiveManage.h
 * Declaration of class ArchiveManage
 */
#ifndef ARCHIVEMANAGE_H_INCLUDED
#define ARCHIVEMANAGE_H_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <list>
#include <memory>
#include <mapix.h>
#include <kopano/ArchiveControl.h>

namespace KC {

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

	virtual ~ArchiveManage(void) = default;
	_kc_export static HRESULT Create(LPMAPISESSION, ECLogger *, const TCHAR *user, std::unique_ptr<ArchiveManage> *manage);
	virtual eResult AttachTo(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder, unsigned int ulFlags) = 0;
	virtual eResult DetachFrom(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder) = 0;
	virtual eResult DetachFrom(unsigned int ulArchive) = 0;
	virtual eResult ListArchives(std::ostream &ostr) = 0;
	virtual eResult ListArchives(ArchiveList *lplstArchives, const char *lpszIpmSubtreeSubstitude = NULL) = 0;
	virtual eResult ListAttachedUsers(std::ostream &ostr) = 0;
	virtual eResult ListAttachedUsers(UserList *lplstUsers) = 0;
	virtual eResult AutoAttach(unsigned int ulFlags) = 0;

protected:
	ArchiveManage(void) = default;
};

typedef std::unique_ptr<ArchiveManage> ArchiveManagePtr;

} /* namespace */

#endif // !defined ARCHIVEMANAGE_H_INCLUDED
