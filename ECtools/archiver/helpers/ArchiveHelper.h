/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ARCHIVEHELPER_H_INCLUDED
#define ARCHIVEHELPER_H_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/archiver-common.h>
#include <kopano/mapi_ptr.h>
#include <kopano/CommonUtil.h>
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr

namespace KC {

class ECLogger;

namespace helpers {

class ArchiveHelper;
typedef std::shared_ptr<ArchiveHelper> ArchiveHelperPtr;

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
class _kc_export ArchiveHelper _kc_final {
public:
	_kc_hidden static HRESULT Create(LPMDB arc_store, const tstring &folder, const char *server_path, ArchiveHelperPtr *);
	_kc_hidden static HRESULT Create(LPMDB arc_store, LPMAPIFOLDER arc_folder, const char *server_path, ArchiveHelperPtr *);
	static HRESULT Create(ArchiverSessionPtr ptrSession, const SObjectEntry &archiveEntry, std::shared_ptr<ECLogger>, ArchiveHelperPtr *lpptrArchiveHelper);
	_kc_hidden HRESULT GetAttachedUser(abentryid_t *user_eid);
	_kc_hidden HRESULT SetAttachedUser(const abentryid_t &user_eid);
	_kc_hidden HRESULT GetArchiveEntry(bool create, SObjectEntry *obj_entry);
	_kc_hidden HRESULT GetArchiveType(ArchiveType *arc_type, AttachType *att_type);
	_kc_hidden HRESULT SetArchiveType(ArchiveType arc_type, AttachType att_type);
	_kc_hidden HRESULT SetPermissions(const abentryid_t &user_eid, bool writable);
	HRESULT GetArchiveFolderFor(MAPIFolderPtr &ptrSourceFolder, ArchiverSessionPtr ptrSession, LPMAPIFOLDER *lppDestinationFolder);
	HRESULT GetHistoryFolder(LPMAPIFOLDER *lppHistoryFolder);
	HRESULT GetOutgoingFolder(LPMAPIFOLDER *lppOutgoingFolder);
	HRESULT GetDeletedItemsFolder(LPMAPIFOLDER *lppOutgoingFolder);
	HRESULT GetSpecialsRootFolder(LPMAPIFOLDER *lppSpecialsRootFolder);

	HRESULT GetArchiveFolder(bool bCreate, LPMAPIFOLDER *lppArchiveFolder);
	_kc_hidden HRESULT IsArchiveFolder(LPMAPIFOLDER, bool *res);
	_kc_hidden MsgStorePtr GetMsgStore(void) const { return m_ptrArchiveStore; }
	_kc_hidden HRESULT PrepareForFirstUse(ECLogger * = nullptr);

private:
	_kc_hidden ArchiveHelper(LPMDB arc_store, const tstring &folder, const std::string &server_path);
	_kc_hidden ArchiveHelper(LPMDB arc_store, LPMAPIFOLDER arc_folder, const std::string &server_path);
	_kc_hidden HRESULT Init(void);

	enum eSpecFolder {
		sfBase = 0,			//< The root of the special folders, which is a child of the archive root
		sfHistory = 1,		//< The history folder, which is a child of the special root
		sfOutgoing = 2,		//< The outgoing folder, which is a child of the special root
		sfDeleted = 3		//< The deleted items folder, which is a child of the special root
	};
	_kc_hidden HRESULT GetSpecialFolderEntryID(eSpecFolder sf_which, ULONG *eid_size, LPENTRYID *eid);
	_kc_hidden HRESULT SetSpecialFolderEntryID(eSpecFolder sf_which, ULONG eid_size, LPENTRYID eid);
	_kc_hidden HRESULT GetSpecialFolder(eSpecFolder sf_which, bool create, LPMAPIFOLDER *spc_folder);
	_kc_hidden HRESULT CreateSpecialFolder(eSpecFolder sf_which, LPMAPIFOLDER *spc_folder);
	_kc_hidden HRESULT IsSpecialFolder(eSpecFolder sf_which, LPMAPIFOLDER, bool *res);

	MsgStorePtr	m_ptrArchiveStore;
	MAPIFolderPtr m_ptrArchiveFolder;
	tstring	m_strFolder;
	const std::string m_strServerPath;

	PROPMAP_DECL()
	PROPMAP_DEF_NAMED_ID(ATTACHED_USER_ENTRYID)
	PROPMAP_DEF_NAMED_ID(ARCHIVE_TYPE)
	PROPMAP_DEF_NAMED_ID(ATTACH_TYPE)
	PROPMAP_DEF_NAMED_ID(SPECIAL_FOLDER_ENTRYIDS)
};

}} /* namespace */

#endif // !defined ARCHIVEHELPER_INCLUDED
