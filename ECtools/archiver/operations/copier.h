/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include "operations.h"
#include "postsaveaction.h"
#include <kopano/archiver-common.h>
#include <map>

namespace KC {

class ArchiverSession;
class Config;
class Logger;

namespace operations {

class Deleter;
class InstanceIdMapper;
class Stubber;
class Transaction;

/**
 * Performs the copy part of the archive operation.
 */
class KC_EXPORT Copier final : public ArchiveOperationBaseEx {
public:
	KC_HIDDEN Copier(std::shared_ptr<ArchiverSession>, Config *, std::shared_ptr<ArchiverLogger>, const std::list<SObjectEntry> &archives, const SPropTagArray *exclprop, int age, bool process_unread);
	KC_HIDDEN ~Copier();

	/**
	 * Override ArchiveOperationBaseEx's GetRestriction to add some more
	 * magic.
	 */
	KC_HIDDEN HRESULT GetRestriction(IMAPIProp *, SRestriction **ret) override;

	/**
	 * Set the operation that will perform the deletion if required.
	 * @param[in]	ptrDeleteOp		The delete operation.
	 */
	KC_HIDDEN void SetDeleteOperation(std::shared_ptr<Deleter>);

	/**
	 * Set the operation that will perform the stubbing if required.
	 * @param[in]	ptrStubOp		The stub operation.
	 */
	KC_HIDDEN void SetStubOperation(std::shared_ptr<Stubber>);

	class KC_EXPORT Helper { // For lack of a better name
	public:
		Helper(std::shared_ptr<ArchiverSession>, std::shared_ptr<KC::Logger>, const std::shared_ptr<InstanceIdMapper> &, const SPropTagArray *exclprop, IMAPIFolder *);

		/**
		 * Create a copy of a message in the archive, effectively archiving the message.
		 * @param[in]	lpSource		The message to archive.
		 * @param[in]	archiveEntry	SObjectEntry specifying the archive to archive in.
		 * @param[in]	refMsgEntry		SObejctEntry referencing the original message (used as a back reference from the archive).
		 * @param[out]	lppArchivedMsg	The new message.
		 */
		HRESULT CreateArchivedMessage(IMessage *src, const SObjectEntry &arc_entry, const SObjectEntry &ref_msgentry, IMessage **arc_msg, std::shared_ptr<IPostSaveAction> *);

		/**
		 * Get the folder that acts as root for an archive.
		 * @param[in]	archiveEntry		SObjectEntry specifying the archive to archive.
		 * @param[out]	lppArchiveFolder	The archive root folder.
		 */
		KC_HIDDEN HRESULT GetArchiveFolder(const SObjectEntry &arc_entry, IMAPIFolder **);

		/**
		 * Copy the message to the archive and setup the special properties.
		 * @param[in]	lpSource		The message to archive.
		 * @param[in]	lpMsgEntry		SObejctEntry referencing the original message (used as a back reference from the archive).
		 * @param[in]	lpDest			The message to archive to.
		 */
		HRESULT ArchiveMessage(IMessage *src, const SObjectEntry *msgentry, IMessage *dst, std::shared_ptr<IPostSaveAction> *);

		/**
		 * Update the single instance IDs of the destination message based on
		 * existing mappings of instance IDs stored in previous runs.
		 * @param[in]	lpSource		The reference message.
		 * @param[in]	lpDest			The message to update.
		 */
		KC_HIDDEN HRESULT UpdateIIDs(IMessage *src, IMessage *dst, std::shared_ptr<IPostSaveAction> *);

		/**
		 * Get the Session instance associated with this instance.
		 */
		KC_HIDDEN std::shared_ptr<ArchiverSession> &GetSession() { return m_ptrSession; }

	private:
		typedef std::map<entryid_t, object_ptr<IMAPIFolder>> ArchiveFolderMap;
		ArchiveFolderMap m_mapArchiveFolders;

		std::shared_ptr<ArchiverSession> m_ptrSession;
		std::shared_ptr<KC::Logger> m_lpLogger;
		const SPropTagArray *m_lpExcludeProps;
		object_ptr<IMAPIFolder> m_ptrFolder;
		std::shared_ptr<InstanceIdMapper> m_ptrMapper;
	};

private:
	KC_HIDDEN HRESULT EnterFolder(IMAPIFolder *) override;
	KC_HIDDEN HRESULT LeaveFolder() override;
	KC_HIDDEN HRESULT DoProcessEntry(const SRow &proprow) override;

	/**
	 * Perform an initial archive of a message. This will be used to archive
	 * a message that has not been archived before.
	 *
	 * @param[in]	lpMessage			The message to archive
	 * @param[in]	archiveRootEntry	The SObjectEntry describing the archive root folder
	 * @param[in]	refMsgEntry			The SObjectEntry describing the message to be archived
	 * @param[out]	lpptrTransaction	A Transaction object used to save and delete the proper messages when everything is setup
	 */
	KC_HIDDEN HRESULT DoInitialArchive(IMessage *, const SObjectEntry &arc_root_entry, const SObjectEntry &ref_msg_entry, std::shared_ptr<Transaction> *);

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
	KC_HIDDEN HRESULT DoTrackAndRearchive(IMessage *, const SObjectEntry &arc_root_entry, const SObjectEntry &arc_msg_entry, const SObjectEntry &ref_msg_entry, bool update_history, std::shared_ptr<Transaction> *);

	/**
	 * Update an existing archive of a message. This is used for dirty messages when the track_history
	 * option is disabled.
	 *
	 * @param[in]	lpMessage			The message to archive
	 * @param[in]	archiveMsgEntry		The SObjectEntry describing the existing archive message
	 * @param[in]	refMsgEntry			The SObjectEntry describing the message to be archived
	 * @param[out]	lpptrTransaction	A Transaction object used to save and delete the proper messages when everything is setup
	 */
	KC_HIDDEN HRESULT DoUpdateArchive(IMessage *, const SObjectEntry &arc_msg_entry, const SObjectEntry &ref_msg_entry, std::shared_ptr<Transaction> *);

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
	KC_HIDDEN HRESULT DoMoveArchive(const SObjectEntry &arc_root_entry, const SObjectEntry &arc_msg_entry, const SObjectEntry &ref_msg_entry, std::shared_ptr<Transaction> *);

	/**
	 * Execute the delete or stub operation if available. If both are set the delete operation
	 * has precedence if the message matches the restriction set for it. If non of the operations
	 * restriction match for the message, nothing will be done.
	 *
	 * @param[in]	lpMessage		The message to process
	 * @param[in]	lpFolder		The parent folder
	 * @param[in]	row			A list of properties containing the information to open the correct message.
	 */
	KC_HIDDEN HRESULT ExecuteSubOperations(IMessage *, IMAPIFolder *, const SRow &);

	/**
	 * Move an archive message to the special history folder.
	 *
	 * @param[in]	sourceArchiveRoot	The SObjectEntry describing the archive root folder
	 * @param[in]	sourceMsgEntry		The SObjectEntry describing the archive message to move
	 * @param[in]	ptrTransaction		A Transaction object used to save and delete the proper messages when everything is setup
	 * @param[out]	lpNewEntry			The SObjectEntry describing the moved message.
	 * @param[out]	lppNewMessage		The newly created message. This argument is allowed to be NULL
	 */
	KC_HIDDEN HRESULT MoveToHistory(const SObjectEntry &src_arc_root, const SObjectEntry &src_msg_entry, std::shared_ptr<Transaction>, SObjectEntry *new_entry, IMessage **new_msg);

	/**
	 * Open the history message referenced by lpArchivedMsg and update its reference. Continue doing that for all
	 * history messages.
	 *
	 * @param[in]	lpArchivedMsg		The archived message whose predecessor to update.
	 * @param[in]	refMsgEntry			The SObjectEntry describing to reference
	 * @param[in]	ptrTransaction		A Transaction object used to save and delete the proper messages when everything is setup
	 */
	KC_HIDDEN HRESULT UpdateHistoryRefs(IMessage *arc_msg, const SObjectEntry &ref_msg_entry, std::shared_ptr<Transaction>);

	std::shared_ptr<ArchiverSession> m_ptrSession;
	Config *m_lpConfig;
	std::list<SObjectEntry> m_lstArchives;
	memory_ptr<SPropTagArray> m_ptrExcludeProps;

	std::shared_ptr<Deleter> m_ptrDeleteOp;
	std::shared_ptr<Stubber> m_ptrStubOp;
	std::unique_ptr<Helper> m_ptrHelper;
	std::shared_ptr<Transaction> m_ptrTransaction;
	std::shared_ptr<InstanceIdMapper> m_ptrMapper;
};

}} /* namespace */
