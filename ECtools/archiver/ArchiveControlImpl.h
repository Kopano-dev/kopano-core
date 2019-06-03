/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ARCHIVECONTROLIMPL_H_INCLUDED
#define ARCHIVECONTROLIMPL_H_INCLUDED

#include <memory>
#include <set>
#include <kopano/memory.hpp>
#include "operations/operations_fwd.h"
#include "helpers/ArchiveHelper.h"

namespace KC {

class ECConfig;
class ECLogger;
class ECArchiverLogger;

/**
 * This class is the entry point to the archiving system. It's responsible for executing the actual archive
 * operation.
 *
 * There are three steps involved in archiving:
 * 1. Copying - Copy a message to the archive.
 * 2. Stubbing - Stub a message in the primary store and have it reference the archive.
 * 3. Deleting - Delete a message from the primary store (but leave the archived copy).
 *
 * These three steps are executed in the following order: 1, 3, 2. This is done because
 * if a message can be deleted from the primary store, there's no need to stub it. So
 * by deleting all messages that may be deleted first, we make sure we're not doing more
 * work than needed.
 *
 * The primary way how this is performed is with the use of searchfolders. There are
 * three searchfolders for this purpose:
 *
 * The first searchfolder contains all messages of the right message class that have
 * not yet been archived (completely). This means that they do not have a copy in all
 * attached archives yet. All messages in this searchfolder are processed in the step
 * 1 if they match the restriction for this step.
 *
 * The second searchfolder contains all messages that have been archived but are not
 * yet stubbed. These messages are processed in step 2 if they match the restriction
 * for this step.
 *
 * The third searchfolder contains all message that have been archived, regardless if
 * they're stubbed or not. These messages are processed in step 3 if they match the
 * restriction for this step.
 *
 * After a message is processed in a particular step, its state will change, causing
 * it to be removed from the searchfolder for that step and be added to the searchfolder
 * for the next step(s). A message processed in step 1 will be added to the searchfolder
 * for both step 2 and step 3. After step 3 has been processed, the message will be
 * removed from the searchfolder for step 3, but not from the searchfolder for step 2.
 *
 * By going through the three searchfolders and executing the appropriate steps, all
 * messages will be processed as they're supposed to.
 *
 *
 * There's a catch though: Searchfolders are asynchronous and the transition of a
 * message from one searchfolder to another might take longer than the operation the
 * archiver is executing. This causes messages to be unprocces because at the time
 * the archiver was looking in the searchfolder the message wasn't there yet. The
 * message will be processed on the next run, but it is hard to explain to admins
 * that they just have to wait for the next run before something they expect to
 * happen really happens.
 *
 * For this reason there's a second approach to execute the three steps:
 * Logically speaking there are just a few possibilities in what steps are executed
 * on a message:
 * 1. copy
 * 2. copy, delete
 * 3. copy, stub
 * 4. stub
 * 5, delete
 *
 * Option 1, 4 and 5 will be handled fine by the primary approach as they consist of
 * one operation only. Option 2 and 3 however depend on the timing of the searchfolders
 * for the second step to be executed or not.
 *
 * Since we know that if a message is copied to all attached archives, it would be
 * moved to the searchfolders for the delete and stub steps. Therefore we know that
 * if the searchfolders were instant, the message would be processed in these steps
 * if it would match the restriction defined for those steps.
 *
 * The alternate approach is thus to take the following actions at a successful
 * completion of step 1:
 * 1. Check if deletion is enabled
 * 2. If so, check if the message matches the restriction for the deletion step
 * 3. If it matched, execute the deletion step and quit
 * 4. Check if stubbing is enabled
 * 5. If so, check if the message matches the restriction for the stubbing step
 * 6. If it matches, execute the stubbing step.
 *
 * This way necessary steps will be executed on a message, regardless of searchfolder
 * timing.
 */
class ArchiveControlImpl final : public ArchiveControl {
public:
	static HRESULT Create(ArchiverSessionPtr ptrSession, ECConfig *lpConfig, std::shared_ptr<ECLogger>, bool bForceCleanup, ArchiveControlPtr *lpptrArchiveControl);
	eResult ArchiveAll(bool local_only, bool auto_attach, unsigned int flags) override;
	HRESULT Archive2(const tstring &user, bool auto_attach, unsigned int flags);
	eResult Archive(const tstring &user, bool auto_attach, unsigned int flags) override;
	eResult CleanupAll(bool local_only) override;
	eResult Cleanup(const tstring &user) override;

private:
	class ReferenceLessCompare {
	public:
		typedef std::pair<entryid_t, entryid_t> value_type;
		bool operator()(const value_type &lhs, const value_type &rhs) const
		{
			return lhs.second < rhs.second;
		}
	};

	typedef HRESULT(ArchiveControlImpl::*fnProcess_t)(const tstring&);
	typedef std::set<entryid_t> EntryIDSet;
	typedef std::set<std::pair<entryid_t, entryid_t>, ReferenceLessCompare> ReferenceSet;

	ArchiveControlImpl(ArchiverSessionPtr ptrSession, ECConfig *lpConfig, std::shared_ptr<ECLogger>, bool bForceCleanup);
	HRESULT Init();
	HRESULT DoArchive(const tstring& strUser);
	HRESULT DoCleanup(const tstring& strUser);
	HRESULT ProcessFolder2(object_ptr<IMAPIFolder> &, std::shared_ptr<operations::IArchiveOperation>, bool &);
	HRESULT ProcessFolder(MAPIFolderPtr &ptrFolder, operations::ArchiveOperationPtr ptrArchiveOperation);
	HRESULT ProcessAll(bool bLocalOnly, fnProcess_t fnProcess);
	HRESULT PurgeArchives(const ObjectEntryList &lstArchives);
	HRESULT PurgeArchiveFolder(MsgStorePtr &ptrArchive, const entryid_t &folderEntryID, const LPSRestriction lpRestriction);
	HRESULT CleanupArchive(const SObjectEntry &archiveEntry, IMsgStore* lpUserStore, LPSRestriction lpRestriction);
	HRESULT GetAllReferences(IMsgStore *store, const GUID *archive, EntryIDSet *refs);
	HRESULT AppendAllReferences(IMAPIFolder *root, const GUID *archive, EntryIDSet *refs);
	HRESULT GetAllEntries(helpers::ArchiveHelperPtr, LPMAPIFOLDER arc, LPSRestriction, EntryIDSet *entries);
	HRESULT AppendAllEntries(LPMAPIFOLDER lpArchive, LPSRestriction lpRestriction, EntryIDSet *lpMsgEntries);
	HRESULT CleanupHierarchy(helpers::ArchiveHelperPtr, LPMAPIFOLDER arc_root, LPMDB user_store);
	HRESULT MoveAndDetachMessages(helpers::ArchiveHelperPtr, LPMAPIFOLDER arc_folder, const EntryIDSet &);
	HRESULT MoveAndDetachFolder(helpers::ArchiveHelperPtr, LPMAPIFOLDER arc_folder);
	HRESULT DeleteMessages(LPMAPIFOLDER lpArchiveFolder, const EntryIDSet &setEIDs);
	HRESULT DeleteFolder(LPMAPIFOLDER lpArchiveFolder);
	HRESULT AppendFolderEntries(LPMAPIFOLDER lpBase, EntryIDSet *lpEntries);
	HRESULT CheckSafeCleanupSettings();

	enum eCleanupAction { caDelete, caStore, caNone };

	ArchiverSessionPtr m_ptrSession;
	ECConfig *m_lpConfig = nullptr;
	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	FILETIME m_ftCurrent = {0, 0};
	bool m_bArchiveEnable = true;
	int m_ulArchiveAfter = 30;
	bool m_bDeleteEnable = false;
	bool m_bDeleteUnread = false;
	int m_ulDeleteAfter = 0;
	bool m_bStubEnable = false;
	bool m_bStubUnread = false;
	int m_ulStubAfter = 0;
	bool m_bPurgeEnable = false;
	int m_ulPurgeAfter = 2555;
	eCleanupAction m_cleanupAction;
	bool m_bCleanupFollowPurgeAfter = false;
	bool m_bForceCleanup;

	PROPMAP_DECL()
	PROPMAP_DEF_NAMED_ID(ARCHIVE_STORE_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ORIGINAL_SOURCEKEY)
	PROPMAP_DEF_NAMED_ID(STUBBED)
	PROPMAP_DEF_NAMED_ID(DIRTY)
};

} /* namespace */

#endif // !defined ARCHIVECONTROLIMPL_H_INCLUDED
