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

#ifndef ARCHIVEHELPER_H_INCLUDED
#define ARCHIVEHELPER_H_INCLUDED

#include <boost/smart_ptr.hpp>
#include <kopano/archiver-common.h>
#include <kopano/mapi_ptr.h>
#include <kopano/CommonUtil.h>
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr

namespace za { namespace helpers {

class ArchiveHelper;
typedef boost::shared_ptr<ArchiveHelper> ArchiveHelperPtr;

enum ArchiveType {
	UndefArchive = 0,
	SingleArchive = 1,
	MultiArchive = 2
};

enum AttachType {
	ExplicitAttach = 0,
	ImplicitAttach = 1,
	UnknownAttach = 42
};

/**
 * The ArchiveHelper class is a utility class that operates on a message store that's used as
 * an archive.
 */
class ArchiveHelper
{
public:
	static HRESULT Create(LPMDB lpArchiveStore, const tstring &strFolder, const char *lpszServerPath, ArchiveHelperPtr *lpptrArchiveHelper);
	static HRESULT Create(LPMDB lpArchiveStore, LPMAPIFOLDER lpArchiveFolder, const char *lpszServerPath, ArchiveHelperPtr *lpptrArchiveHelper);
	static HRESULT Create(ArchiverSessionPtr ptrSession, const SObjectEntry &archiveEntry, ECLogger *lpLogger, ArchiveHelperPtr *lpptrArchiveHelper);
	~ArchiveHelper();

	HRESULT GetAttachedUser(abentryid_t *lpsUserEntryId);
	HRESULT SetAttachedUser(const abentryid_t &sUserEntryId);
	HRESULT GetArchiveEntry(bool bCreate, SObjectEntry *lpsObjectEntry);

	HRESULT GetArchiveType(ArchiveType *lparchType, AttachType *lpattachType);
	HRESULT SetArchiveType(ArchiveType archType, AttachType attachType);

	HRESULT SetPermissions(const abentryid_t &sUserEntryId, bool bWritable);

	HRESULT GetArchiveFolderFor(MAPIFolderPtr &ptrSourceFolder, ArchiverSessionPtr ptrSession, LPMAPIFOLDER *lppDestinationFolder);
	HRESULT GetHistoryFolder(LPMAPIFOLDER *lppHistoryFolder);
	HRESULT GetOutgoingFolder(LPMAPIFOLDER *lppOutgoingFolder);
	HRESULT GetDeletedItemsFolder(LPMAPIFOLDER *lppOutgoingFolder);
	HRESULT GetSpecialsRootFolder(LPMAPIFOLDER *lppSpecialsRootFolder);

	HRESULT GetArchiveFolder(bool bCreate, LPMAPIFOLDER *lppArchiveFolder);
	HRESULT IsArchiveFolder(LPMAPIFOLDER lpFolder, bool *lpbResult);

	MsgStorePtr GetMsgStore() const { return m_ptrArchiveStore; }

	HRESULT PrepareForFirstUse(ECLogger *lpLogger = NULL);

private:
	ArchiveHelper(LPMDB lpArchiveStore, const tstring &strFolder, const std::string &strServerPath);
	ArchiveHelper(LPMDB lpArchiveStore, LPMAPIFOLDER lpArchiveFolder, const std::string &strServerPath);
	HRESULT Init();

	enum eSpecFolder {
		sfBase = 0,			//< The root of the special folders, which is a child of the archive root
		sfHistory = 1,		//< The history folder, which is a child of the special root
		sfOutgoing = 2,		//< The outgoing folder, which is a child of the special root
		sfDeleted = 3		//< The deleted items folder, which is a child of the special root
	};
	HRESULT GetSpecialFolderEntryID(eSpecFolder sfWhich, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	HRESULT SetSpecialFolderEntryID(eSpecFolder sfWhich, ULONG cbEntryID, LPENTRYID lpEntryID);
	HRESULT GetSpecialFolder(eSpecFolder sfWhich, bool bCreate, LPMAPIFOLDER *lppSpecialFolder);
	HRESULT CreateSpecialFolder(eSpecFolder sfWhich, LPMAPIFOLDER *lppSpecialFolder);
	HRESULT IsSpecialFolder(eSpecFolder sfWhich, LPMAPIFOLDER lpFolder, bool *lpbResult);

private:
	MsgStorePtr	m_ptrArchiveStore;
	MAPIFolderPtr m_ptrArchiveFolder;
	tstring	m_strFolder;
	const std::string m_strServerPath;

	PROPMAP_START
	PROPMAP_DEF_NAMED_ID(ATTACHED_USER_ENTRYID)
	PROPMAP_DEF_NAMED_ID(ARCHIVE_TYPE)
	PROPMAP_DEF_NAMED_ID(ATTACH_TYPE)
	PROPMAP_DEF_NAMED_ID(SPECIAL_FOLDER_ENTRYIDS)
};

}} // Namespaces

#endif // !defined ARCHIVEHELPER_INCLUDED
