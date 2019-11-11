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
class KC_EXPORT ArchiveHelper final {
public:
	KC_HIDDEN static HRESULT Create(IMsgStore *arc_store, const tstring &folder, const char *server_path, ArchiveHelperPtr *);
	KC_HIDDEN static HRESULT Create(IMsgStore *arc_store, IMAPIFolder *arc_folder, const char *server_path, ArchiveHelperPtr *);
	static HRESULT Create(ArchiverSessionPtr ptrSession, const SObjectEntry &archiveEntry, std::shared_ptr<ECLogger>, ArchiveHelperPtr *lpptrArchiveHelper);
	KC_HIDDEN HRESULT GetAttachedUser(abentryid_t *user_eid);
	KC_HIDDEN HRESULT SetAttachedUser(const abentryid_t &user_eid);
	KC_HIDDEN HRESULT GetArchiveEntry(bool create, SObjectEntry *obj_entry);
	KC_HIDDEN HRESULT GetArchiveType(ArchiveType *arc_type, AttachType *att_type);
	KC_HIDDEN HRESULT SetArchiveType(ArchiveType arc_type, AttachType att_type);
	KC_HIDDEN HRESULT SetPermissions(const abentryid_t &user_eid, bool writable);
	HRESULT GetArchiveFolderFor(MAPIFolderPtr &ptrSourceFolder, ArchiverSessionPtr ptrSession, LPMAPIFOLDER *lppDestinationFolder);
	HRESULT GetHistoryFolder(LPMAPIFOLDER *lppHistoryFolder);
	HRESULT GetOutgoingFolder(LPMAPIFOLDER *lppOutgoingFolder);
	HRESULT GetDeletedItemsFolder(LPMAPIFOLDER *lppOutgoingFolder);
	HRESULT GetSpecialsRootFolder(LPMAPIFOLDER *lppSpecialsRootFolder);

	HRESULT GetArchiveFolder(bool bCreate, LPMAPIFOLDER *lppArchiveFolder);
	KC_HIDDEN HRESULT IsArchiveFolder(IMAPIFolder *, bool *res);
	KC_HIDDEN MsgStorePtr GetMsgStore() const { return m_ptrArchiveStore; }
	KC_HIDDEN HRESULT PrepareForFirstUse(ECLogger * = nullptr);

private:
	KC_HIDDEN ArchiveHelper(IMsgStore *arc_store, const tstring &folder, const std::string &server_path);
	KC_HIDDEN ArchiveHelper(IMsgStore *arc_store, IMAPIFolder *arc_folder, const std::string &server_path);
	KC_HIDDEN HRESULT Init();

	enum eSpecFolder {
		sfBase = 0,			//< The root of the special folders, which is a child of the archive root
		sfHistory = 1,		//< The history folder, which is a child of the special root
		sfOutgoing = 2,		//< The outgoing folder, which is a child of the special root
		sfDeleted = 3		//< The deleted items folder, which is a child of the special root
	};
	KC_HIDDEN HRESULT GetSpecialFolderEntryID(eSpecFolder sf_which, unsigned int *eid_size, ENTRYID **eid);
	KC_HIDDEN HRESULT SetSpecialFolderEntryID(eSpecFolder sf_which, unsigned int eid_size, ENTRYID *eid);
	KC_HIDDEN HRESULT GetSpecialFolder(eSpecFolder sf_which, bool create, IMAPIFolder **spc_folder);
	KC_HIDDEN HRESULT CreateSpecialFolder(eSpecFolder sf_which, IMAPIFolder **spc_folder);
	KC_HIDDEN HRESULT IsSpecialFolder(eSpecFolder sf_which, IMAPIFolder *, bool *res);

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
