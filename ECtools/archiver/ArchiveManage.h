/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <list>
#include <memory>
#include <ostream>
#include <string>
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

struct UserEntry {
	std::string UserName;
};

class ArchiveManage {
public:
	enum {
		UseIpmSubtree = 1,
		Writable = 2,
		ReadOnly = 4
	};

	virtual ~ArchiveManage(void) = default;
	KC_EXPORT static HRESULT Create(IMAPISession *, std::shared_ptr<ECLogger>, const TCHAR *user, std::unique_ptr<ArchiveManage> *manage);
	virtual eResult AttachTo(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder, unsigned int ulFlags) = 0;
	virtual eResult DetachFrom(const char *lpszArchiveServer, const TCHAR *lpszArchive, const TCHAR *lpszFolder) = 0;
	virtual eResult DetachFrom(unsigned int ulArchive) = 0;
	virtual eResult ListArchives(std::ostream &ostr) = 0;
	virtual eResult ListArchives(std::list<ArchiveEntry> *, const char *ipm_subtree_subst = nullptr) = 0;
	virtual eResult ListAttachedUsers(std::ostream &ostr) = 0;
	virtual eResult ListAttachedUsers(std::list<UserEntry> *) = 0;
	virtual eResult AutoAttach(unsigned int ulFlags) = 0;

protected:
	ArchiveManage(void) = default;
};

} /* namespace */
