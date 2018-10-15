/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef copier_INCLUDED
#define copier_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include "operations.h"
#include "postsaveaction.h"
#include "transaction_fwd.h"
#include "instanceidmapper_fwd.h"
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr
#include <kopano/archiver-common.h>
#include <map>

namespace KC {

class ECConfig;
class ECLogger;

namespace operations {

/**
 * Performs the copy part of the archive operation.
 */
class _kc_export Copier final : public ArchiveOperationBaseEx {
public:
	_kc_hidden Copier(ArchiverSessionPtr, ECConfig *, std::shared_ptr<ECArchiverLogger>, const ObjectEntryList &archives, const SPropTagArray *exclprop, int age, bool process_unread);
	_kc_hidden ~Copier(void);

	/**
	 * Override ArchiveOperationBaseEx's GetRestriction to add some more
	 * magic.
	 */
	_kc_hidden HRESULT GetRestriction(LPMAPIPROP p, LPSRestriction *ret);

	/**
	 * Set the operation that will perform the deletion if required.
	 * @param[in]	ptrDeleteOp		The delete operation.
	 */
	_kc_hidden void SetDeleteOperation(DeleterPtr);

	/**
	 * Set the operation that will perform the stubbing if required.
	 * @param[in]	ptrStubOp		The stub operation.
	 */
	_kc_hidden void SetStubOperation(StubberPtr);

	class _kc_export Helper { // For lack of a better name
	public:
		Helper(ArchiverSessionPtr, std::shared_ptr<ECLogger>, const InstanceIdMapperPtr &, const SPropTagArray *exclprop, LPMAPIFOLDER folder);

		/**
		 * Create a copy of a message in the archive, effectively archiving the message.
		 * @param[in]	lpSource		The message to archive.
		 * @param[in]	archiveEntry	SObjectEntry specifying the archive to archive in.
		 * @param[in]	refMsgEntry		SObejctEntry referencing the original message (used as a back reference from the archive).
		 * @param[out]	lppArchivedMsg	The new message.
		 */
		HRESULT CreateArchivedMessage(LPMESSAGE lpSource, const SObjectEntry &archiveEntry, const SObjectEntry &refMsgEntry, LPMESSAGE *lppArchivedMsg, PostSaveActionPtr *lpptrPSAction);

		/**
		 * Get the folder that acts as root for an archive.
		 * @param[in]	archiveEntry		SObjectEntry specifying the archive to archive.
		 * @param[out]	lppArchiveFolder	The archive root folder.
		 */
		_kc_hidden HRESULT GetArchiveFolder(const SObjectEntry &arc_entry, LPMAPIFOLDER *ret);

		/**
		 * Copy the message to the archive and setup the special properties.
		 * @param[in]	lpSource		The message to archive.
		 * @param[in]	lpMsgEntry		SObejctEntry referencing the original message (used as a back reference from the archive).
		 * @param[in]	lpDest			The message to archive to.
		 */
		HRESULT ArchiveMessage(LPMESSAGE lpSource, const SObjectEntry *lpMsgEntry, LPMESSAGE lpDest, PostSaveActionPtr *lpptrPSAction);

		/**
		 * Update the single instance IDs of the destination message based on
		 * existing mappings of instance IDs stored in previous runs.
		 * @param[in]	lpSource		The reference message.
		 * @param[in]	lpDest			The message to update.
		 */
		_kc_hidden HRESULT UpdateIIDs(LPMESSAGE src, LPMESSAGE dst, PostSaveActionPtr *);

		/**
		 * Get the Session instance associated with this instance.
		 */
		_kc_hidden ArchiverSessionPtr &GetSession(void) { return m_ptrSession; }

	private:
		typedef std::map<entryid_t,MAPIFolderPtr> ArchiveFolderMap;
		ArchiveFolderMap m_mapArchiveFolders;

		ArchiverSessionPtr m_ptrSession;
		std::shared_ptr<ECLogger> m_lpLogger;
		const SPropTagArray *m_lpExcludeProps;
		MAPIFolderPtr m_ptrFolder;
		InstanceIdMapperPtr m_ptrMapper;
	};

private:
	_kc_hidden HRESULT EnterFolder(IMAPIFolder *) override;
	_kc_hidden HRESULT LeaveFolder() override;
	_kc_hidden HRESULT DoProcessEntry(const SRow &proprow) override;

	/**
	 * Perform an initial archive of a message. This will be used to archive
	 * a message that has not been archived before.
	 *
	 * @param[in]	lpMessage			The message to archive
	 * @param[in]	archiveRootEntry	The SObjectEntry describing the archive root folder
	 * @param[in]	refMsgEntry			The SObjectEntry describing the message to be archived
	 * @param[out]	lpptrTransaction	A Transaction object used to save and delete the proper messages when everything is setup
	 */
	_kc_hidden HRESULT DoInitialArchive(LPMESSAGE, const SObjectEntry &arc_root_entry, const SObjectEntry &ref_msg_entry, TransactionPtr *);

	/**
	 * Track an existing archive and create a new archive of a message. This is used for the
	 * track_history option and will move the old archive to the history folder while creating
	 * a new archive message that will reference that old archive.
	 *
	 * @param[in]	lpMessage			The message to archive
	 * @param[in]	archiveRootEntry	The SObjectEntry describing the archive root folder
	 * @param[in]	archiveMsgEntry		The SObjectEntry describing the existing archive message
	 * @param[in]	refMsgEntry			The SObjectEntry describing the message to be archived
	 * @param[in]	bUpdateHistory		If true, update the history references
	 * @param[out]	lpptrTransaction	A Transaction object used to save and delete the proper messages when everything is setup
	 */
	_kc_hidden HRESULT DoTrackAndRearchive(LPMESSAGE, const SObjectEntry &arc_root_entry, const SObjectEntry &arc_msg_entry, const SObjectEntry &ref_msg_entry, bool update_history, TransactionPtr *);

	/**
	 * Update an existing archive of a message. This is used for dirty messages when the track_history
	 * option is disabled.
	 *
	 * @param[in]	lpMessage			The message to archive
	 * @param[in]	archiveMsgEntry		The SObjectEntry describing the existing archive message
	 * @param[in]	refMsgEntry			The SObjectEntry describing the message to be archived
	 * @param[out]	lpptrTransaction	A Transaction object used to save and delete the proper messages when everything is setup
	 */
	_kc_hidden HRESULT DoUpdateArchive(LPMESSAGE, const SObjectEntry &arc_msg_entry, const SObjectEntry &ref_msg_entry, TransactionPtr *);

	/**
	 * Move an archived message from one archive folder to another. This is only needed when the
	 * message in the primary store has been moved. The references in old archives will automatically
	 * be updated.
	 *
	 * @note This function actually creates a copy of the message and delete the old message through
	 *       the transaction system. Otherwise no rollback can be guaranteed.
	 *
	 * @param[in]	archiveRootEntry	The SObjectEntry describing the archive root folder
	 * @param[in]	archiveMsgEntry		The SObjectEntry describing the existing archive message
	 * @param[in]	refMsgEntry			The SObjectEntry describing the message to be archived
	 * @param[out]	lpptrTransaction	A Transaction object used to save and delete the proper messages when everything is setup
	 */
	_kc_hidden HRESULT DoMoveArchive(const SObjectEntry &arc_root_entry, const SObjectEntry &arc_msg_entry, const SObjectEntry &ref_msg_entry, TransactionPtr *);

	/**
	 * Execute the delete or stub operation if available. If both are set the delete operation
	 * has precedence if the message matches the restriction set for it. If non of the operations
	 * restriction match for the message, nothing will be done.
	 *
	 * @param[in]	lpMessage		The message to process
	 * @param[in]	lpFolder		The parent folder
	 * @param[in]	row			A list of properties containing the information to open the correct message.
	 */
	_kc_hidden HRESULT ExecuteSubOperations(IMessage *, IMAPIFolder *, const SRow &);

	/**
	 * Move an archive message to the special history folder.
	 *
	 * @param[in]	sourceArchiveRoot	The SObjectEntry describing the archive root folder
	 * @param[in]	sourceMsgEntry		The SObjectEntry describing the archive message to move
	 * @param[in]	ptrTransaction		A Transaction object used to save and delete the proper messages when everything is setup
	 * @param[out]	lpNewEntry			The SObjectEntry describing the moved message.
	 * @param[out]	lppNewMessage		The newly created message. This argument is allowed to be NULL
	 */
	_kc_hidden HRESULT MoveToHistory(const SObjectEntry &src_arc_root, const SObjectEntry &src_msg_entry, TransactionPtr, SObjectEntry *new_entry, LPMESSAGE *new_msg);

	/**
	 * Open the history message referenced by lpArchivedMsg and update its reference. Continue doing that for all
	 * history messages.
	 *
	 * @param[in]	lpArchivedMsg		The archived message whose predecessor to update.
	 * @param[in]	refMsgEntry			The SObjectEntry describing to reference
	 * @param[in]	ptrTransaction		A Transaction object used to save and delete the proper messages when everything is setup
	 */
	_kc_hidden HRESULT UpdateHistoryRefs(LPMESSAGE arc_msg, const SObjectEntry &ref_msg_entry, TransactionPtr);

	ArchiverSessionPtr m_ptrSession;
	ECConfig *m_lpConfig;
	ObjectEntryList m_lstArchives;
	SPropTagArrayPtr m_ptrExcludeProps;

	DeleterPtr m_ptrDeleteOp;
	StubberPtr m_ptrStubOp;
	std::unique_ptr<Helper> m_ptrHelper;
	TransactionPtr m_ptrTransaction;
	InstanceIdMapperPtr m_ptrMapper;
};

}} /* namespace */

#endif // ndef copier_INCLUDED
