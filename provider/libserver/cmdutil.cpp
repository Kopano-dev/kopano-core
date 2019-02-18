/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <list>
#include <map>
#include <utility>
#include <mapidefs.h>
#include <mapitags.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <kopano/EMSAbTag.h>
#include <kopano/scope.hpp>
#include <edkmdb.h>
#include "ECMAPI.h"
#include "soapH.h"
#include "ECSessionManager.h"
#include "ECSecurity.h"
#include "ics.h"
#include "ECICS.h"
#include "StorageUtil.h"
#include "ECAttachmentStorage.h"
#include "StatsClient.h"
#include "ECTPropsPurge.h"
#include "cmdutil.hpp"

#define FIELD_NR_NAMEID		(FIELD_NR_MAX + 1)
#define FIELD_NR_NAMESTR	(FIELD_NR_MAX + 2)
#define FIELD_NR_NAMEGUID	(FIELD_NR_MAX + 3)

namespace KC {

static void FreeDeleteItem(DELETEITEM *);

ECRESULT GetSourceKey(unsigned int ulObjId, SOURCEKEY *lpSourceKey)
{
	unsigned char *lpData = NULL;
	unsigned int cbData = 0;
	auto er = g_lpSessionManager->GetCacheManager()->GetPropFromObject(PROP_ID(PR_SOURCE_KEY),
	          ulObjId, nullptr, &cbData, &lpData);
	if (er == erSuccess)
		*lpSourceKey = SOURCEKEY(cbData, lpData);
	s_free(nullptr, lpData);
	return er;
}

/*
 * This is a generic delete function that is called from
 *
 * ns__deleteObjects
 * ns__emptyFolder
 * ns__deleteFolder
 * purgeSoftDelete
 *
 * Functions which using sub set of the delete system are:
 * ns__saveObject
 * importMessageFromStream
 *
 * It does a recursive deletion of objects in the hierarchytable, according to
 * the flags given, which is any combination of:
 *
 * EC_DELETE_FOLDERS		- Delete subfolders
 * EC_DELETE_MESSAGES		- Delete messages
 * EC_DELETE_RECIPIENTS		- Delete recipients of messages
 * EC_DELETE_ATTACHMENTS	- Delete attachments of messages
 * EC_DELETE_CONTAINER		- Delete the container specified in the first place (otherwise only subobjects)
 *
 * This is done by first recusively retrieving the object IDs, then checking the types of objects in that
 * list. If there is any subobject that has *not* been specified for deletion, the function fails. Else,
 * all properties in the subobjects and the subobjects themselves are deleted. If EC_DELETE_CONTAINER
 * is specified, then the objects passed in lpEntryList are also deleted (together with their properties).
 *
 */

/**
 * Validate permissions and match object type
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] bCheckPermission Check if the object folder or message has delete permissions.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] sItem Reference to a DELETEITEM structure that contains object information which identifying the folder, message, reciptient and attachment.
 *
 * @return Kopano error code
 */
static ECRESULT ValidateDeleteObject(ECSession *lpSession,
    bool bCheckPermission, unsigned int ulFlags, const DELETEITEM &sItem)
{
	if (lpSession == NULL)
		return KCERR_INVALID_PARAMETER;
	// Check permission for each folder and messages
	if (bCheckPermission && ((sItem.ulObjType == MAPI_MESSAGE && sItem.ulParentType == MAPI_FOLDER) || sItem.ulObjType == MAPI_FOLDER)) {
		auto er = lpSession->GetSecurity()->CheckPermission(sItem.ulId, ecSecurityDelete);
		if(er != erSuccess)
			return er;
	}
	if (sItem.fRoot)
		return erSuccess; // Not for a root

	switch(RealObjType(sItem.ulObjType, sItem.ulParentType)) {
	case MAPI_MESSAGE:
		if (!(ulFlags & EC_DELETE_MESSAGES))
			return KCERR_HAS_MESSAGES;
		break;
	case MAPI_FOLDER:
		if (!(ulFlags & EC_DELETE_FOLDERS))
			return KCERR_HAS_FOLDERS;
		break;
	case MAPI_MAILUSER:
	case MAPI_DISTLIST:
		if (!(ulFlags & EC_DELETE_RECIPIENTS))
			return KCERR_HAS_RECIPIENTS;
		break;
	case MAPI_ATTACH:
		if (!(ulFlags & EC_DELETE_ATTACHMENTS))
			return KCERR_HAS_ATTACHMENTS;
		break;
	case MAPI_STORE: // only admins can delete a store, rights checked in ns__removeStore
		if (!(ulFlags & EC_DELETE_STORE))
			return KCERR_NOT_FOUND;
		break;
	default:
		// Unknown object type? We'll delete it anyway so we don't get frustrating non-deletable items
		assert(false); // Only frustrating developers!
		break;
	}
	return erSuccess;
}

/**
 * Expand a list of objects, validate permissions and object types
 * This function returns a list of all items that need to be
 * deleted. This may or may not include the given list in
 * lpsObjectList, because of EC_DELETE_CONTAINER.
 *
 * If the ulFLags includes EC_DELETE_NOT_ASSOCIATED_MSG only the associated messages for the container folder is
 * not deleted. If ulFlags EC_DELETE_CONTAINER included, the EC_DELETE_NOT_ASSOCIATED_MSG flag will be ignored.
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] lpDatabase Reference to a database object; cannot be NULL.
 * @param[in] lpsObjectList Reference to a list of objects that contains itemid and must be expanded.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] bCheckPermission Check the objects delete permissions.
 * @param[out] lplstDeleteItems Recursive list with objects
 *
 * @return Kopano error code
 */
ECRESULT ExpandDeletedItems(ECSession *lpSession, ECDatabase *lpDatabase, ECListInt *lpsObjectList, unsigned int ulFlags, bool bCheckPermission, ECListDeleteItems *lplstDeleteItems)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	std::set<unsigned int> setIDs;
	ECListDeleteItems lstDeleteItems, lstContainerItems;
	DELETEITEM sItem;
	unsigned int ulParent = 0;

	if (lpSession == nullptr || lpDatabase == nullptr ||
	    lpsObjectList == nullptr || lplstDeleteItems == nullptr)
		return KCERR_INVALID_PARAMETER;

	auto lpSessionManager = lpSession->GetSessionManager();
	auto lpCacheManager = lpSessionManager->GetCacheManager();
	auto cleanup = make_scope_success([&]() { FreeDeletedItems(&lstDeleteItems); });

	// First, put all the root objects in the list
	for (const auto &elem : *lpsObjectList) {
		sItem.fRoot = true;
		/* Lock the root records' parent counter to maintain locking order (counters/content/storesize/committimemax) */
		auto er = lpCacheManager->GetObject(elem, &ulParent, nullptr, nullptr, nullptr);
		if(er != erSuccess)
			return er;
        er = lpDatabase->DoSelect("SELECT properties.val_ulong FROM properties WHERE hierarchyid = " + stringify(ulParent) + " FOR UPDATE", NULL);
        if(er != erSuccess)
			return er;
        // Lock the root records to make sure that we don't interfere with modifies or deletes on the same record
		er = lpDatabase->DoSelect("SELECT hierarchy.flags FROM hierarchy WHERE id = " + stringify(elem) + " FOR UPDATE", nullptr);
        if(er != erSuccess)
			return er;

		auto strQuery = "SELECT h.id, h.parent, h.type, h.flags, p.type, properties.val_ulong, (SELECT hierarchy_id FROM outgoingqueue WHERE outgoingqueue.hierarchy_id = h.id LIMIT 1) FROM hierarchy as h LEFT JOIN properties ON properties.hierarchyid=h.id AND properties.tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " AND properties.type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) + " LEFT JOIN hierarchy AS p ON h.parent=p.id WHERE ";
		if((ulFlags & EC_DELETE_CONTAINER) == 0)
			strQuery += "h.parent=" + stringify(elem);
		else
			strQuery += "h.id=" + stringify(elem);
		if((ulFlags & EC_DELETE_HARD_DELETE) != EC_DELETE_HARD_DELETE)
			strQuery += " AND (h.flags&"+stringify(MSGFLAG_DELETED)+") !="+stringify(MSGFLAG_DELETED);
		if ((ulFlags & (EC_DELETE_CONTAINER | EC_DELETE_NOT_ASSOCIATED_MSG)) == EC_DELETE_NOT_ASSOCIATED_MSG)
			strQuery += " AND (h.flags&"+stringify(MSGFLAG_ASSOCIATED)+") !="+stringify(MSGFLAG_ASSOCIATED);
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			return er;

		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			// No type or flags exist
			if(lpDBRow[2] == NULL || lpDBRow[3] == NULL) {
				//er = KCERR_DATABASE_ERROR;
				//goto exit;
				continue;
			}
			// When you delete a store the parent id is NULL, object type must be MAPI_STORE
			if(lpDBRow[1] == NULL && atoi(lpDBRow[2]) != MAPI_STORE) {
				//er = KCERR_DATABASE_ERROR;
				//goto exit;
				continue;
			}
			// Loop protection, don't insert duplicates.
			if (!setIDs.emplace(atoui(lpDBRow[0])).second)
				continue;

			sItem.ulId = atoui(lpDBRow[0]);
			sItem.ulParent = (lpDBRow[1])?atoui(lpDBRow[1]) : 0;
			sItem.ulObjType = atoi(lpDBRow[2]);
			sItem.ulFlags = atoui(lpDBRow[3]);
			sItem.ulObjSize = 0;
			sItem.ulStoreId = 0;
			sItem.ulParentType = (lpDBRow[4])?atoui(lpDBRow[4]) : 0;
			sItem.sEntryId.__size = 0;
			sItem.sEntryId.__ptr = NULL;
			sItem.ulMessageFlags = lpDBRow[5] ? atoui(lpDBRow[5]) : 0;
			sItem.fInOGQueue = lpDBRow[6] != nullptr;
			// Validate deleted object, if not valid, break directly
			er = ValidateDeleteObject(lpSession, bCheckPermission, ulFlags, sItem);
			if (er != erSuccess)
				return er;

			// Get extended data
			if(sItem.ulObjType == MAPI_STORE || sItem.ulObjType == MAPI_FOLDER || sItem.ulObjType == MAPI_MESSAGE) {
				lpCacheManager->GetStore(sItem.ulId, &sItem.ulStoreId , NULL); //CHECKme:"oude gaf geen errors
				if (!(sItem.ulFlags & MSGFLAG_DELETED))
					GetObjectSize(lpDatabase, sItem.ulId, &sItem.ulObjSize);
				lpCacheManager->GetEntryIdFromObject(sItem.ulId, NULL, 0, &sItem.sEntryId);//CHECKme:"oude gaf geen errors

				GetSourceKey(sItem.ulId, &sItem.sSourceKey);
				GetSourceKey(sItem.ulParent, &sItem.sParentSourceKey);
			}
			lstDeleteItems.emplace_back(sItem);
		}
	}

	// Now, run through the list, adding children to the bottom of the list. This means
	// we're actually running width-first, and don't have to do anything recursive.
	for (const auto &di : lstDeleteItems) {
		auto strQuery = "SELECT id, type, flags, (SELECT hierarchy_id FROM outgoingqueue WHERE outgoingqueue.hierarchy_id = hierarchy.id LIMIT 1) FROM hierarchy WHERE parent=" +
			stringify(di.ulId);
		if((ulFlags & EC_DELETE_HARD_DELETE) != EC_DELETE_HARD_DELETE)
			strQuery += " AND (flags&"+stringify(MSGFLAG_DELETED)+") !="+stringify(MSGFLAG_DELETED);
		auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			return er;

		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			// No id, type or flags exist
			if(lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL)
				continue;
			// Loop protection, don't insert duplicates.
			if (!setIDs.emplace(atoui(lpDBRow[0])).second)
				continue;

			// Add this object as a node to the end of the list
			sItem.fRoot = false;
			sItem.ulObjSize = 0;
			sItem.ulStoreId = 0;
			sItem.sEntryId.__size = 0;
			sItem.sEntryId.__ptr = NULL;
			sItem.ulId = atoui(lpDBRow[0]);
			sItem.ulParent = di.ulId;
			sItem.ulParentType = di.ulObjType;
			sItem.ulObjType = atoi(lpDBRow[1]);
			// Add the parent delete flag, because only the top-level object is marked for deletion
			sItem.ulFlags = atoui(lpDBRow[2]) | (di.ulFlags & MSGFLAG_DELETED);
			sItem.fInOGQueue = lpDBRow[3] != nullptr;
			// Validate deleted object, if no valid, break directly
			er = ValidateDeleteObject(lpSession, bCheckPermission, ulFlags, sItem);
			if (er != erSuccess)
				return er;

			if(sItem.ulObjType == MAPI_STORE || sItem.ulObjType == MAPI_FOLDER || (sItem.ulObjType == MAPI_MESSAGE && sItem.ulParentType == MAPI_FOLDER) ) {
				lpCacheManager->GetStore(sItem.ulId, &sItem.ulStoreId , NULL); //CHECKme:"oude gaf geen errors
				if (!(sItem.ulFlags & MSGFLAG_DELETED))
					GetObjectSize(lpDatabase, sItem.ulId, &sItem.ulObjSize);
				lpCacheManager->GetEntryIdFromObject(sItem.ulId, NULL, 0, &sItem.sEntryId);//CHECKme:"oude gaf geen errors

				GetSourceKey(sItem.ulId, &sItem.sSourceKey);
				GetSourceKey(sItem.ulParent, &sItem.sParentSourceKey);
			}
			lstDeleteItems.emplace_back(sItem);
		}
	}

	// Move list
	std::swap(lstDeleteItems, *lplstDeleteItems);
	return erSuccess;
}

/*
 * Add changes into the ICS system.
 *
 * This adds a change for each removed folder and message. The change could be a soft- or hard delete.
 * It is possible to gives a list with different deleted objects, all not supported object types will be skipped.
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] lstDeleted List with deleted objects
 * @param[in] ulSyncId ???
 *
 */
static ECRESULT DeleteObjectUpdateICS(ECSession *lpSession,
    unsigned int ulFlags, ECListDeleteItems &lstDeleted, unsigned int ulSyncId)
{
	for (const auto &di : lstDeleted)
		// ICS update
		if (di.ulObjType == MAPI_MESSAGE &&
		    di.ulParentType == MAPI_FOLDER)
			AddChange(lpSession, ulSyncId, di.sSourceKey, di.sParentSourceKey, ulFlags & EC_DELETE_HARD_DELETE ? ICS_MESSAGE_HARD_DELETE : ICS_MESSAGE_SOFT_DELETE);
		else if (di.ulObjType == MAPI_FOLDER &&
		    !(di.ulFlags & FOLDER_SEARCH))
			AddChange(lpSession, ulSyncId, di.sSourceKey, di.sParentSourceKey, ulFlags & EC_DELETE_HARD_DELETE ? ICS_FOLDER_HARD_DELETE : ICS_FOLDER_SOFT_DELETE);
	return erSuccess;
}

/**
 * Check if the deletion of the object should actually occur in the sync
 * scope. Checks the syncedmessages table.
 *
 * @param lpDatabase Database object
 * @param lstDeleted Expanded list of all objects to check
 * @param ulSyncId syncid to check with
 *
 * @return Kopano error code
 */
static ECRESULT CheckICSDeleteScope(ECDatabase *lpDatabase,
    ECListDeleteItems &lstDeleted, unsigned int ulSyncId)
{
	for (auto iterDeleteItems = lstDeleted.begin();
	     iterDeleteItems != lstDeleted.end(); ) {
		auto er = CheckWithinLastSyncedMessagesSet(lpDatabase, ulSyncId, iterDeleteItems->sSourceKey);
		if (er == KCERR_NOT_FOUND) {
			/* Ignore deletion of message. */
			ec_log_debug("Message not in sync scope, ignoring delete");
			FreeDeleteItem(&(*iterDeleteItems));
			iterDeleteItems = lstDeleted.erase(iterDeleteItems);
		} else if (er != erSuccess)
			return er;
		else
			++iterDeleteItems;
	}
	return erSuccess;
}

/*
 * Calculate and update the store size for deleted items
 *
 * The DeleteObjectStoreSize method calculate and update the store size. Only top-level messages will
 * be calculate, all other objects are not supported and will be skipped. If a message has the
 * MSGFLAG_DELETED flag, the size will ignored because it is already subtract from the store size.
 * The deleted object list may include more than one store.
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] lpDatabase Reference to a database object; cannot be NULL.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] lstDeleted List with deleted objects
 */
ECRESULT DeleteObjectStoreSize(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulFlags, ECListDeleteItems &lstDeleted)
{
	ECRESULT er = erSuccess;
	std::map<unsigned int, long long> mapStoreSize;
//TODO: check or foldersize also is used

	for (const auto &di : lstDeleted) {
		// Get size of all the messages
		bool k = di.ulObjType == MAPI_MESSAGE &&
			di.ulParentType == MAPI_FOLDER &&
			(di.ulFlags & MSGFLAG_DELETED) != MSGFLAG_DELETED;
		if (!k)
			continue;
		assert(di.ulStoreId != 0);
		if (mapStoreSize.find(di.ulStoreId) != mapStoreSize.end() )
			mapStoreSize[di.ulStoreId] += di.ulObjSize;
		else
			mapStoreSize[di.ulStoreId] = di.ulObjSize;
	}

	// Update store size for each store
	for (auto i = mapStoreSize.cbegin();
	     i != mapStoreSize.cend() && er == erSuccess; ++i)
		if (i->second > 0)
			er = UpdateObjectSize(lpDatabase, i->first, MAPI_STORE, UPDATE_SUB, i->second);
	return er;
}

/*
 * Soft delete objects, mark the root objects as deleted
 *
 * Mark the root objects as deleted, add deleted on date on the root objects.
 * Since this is a fairly simple operation, we are doing soft deletes in a single transaction. Once the SQL has gone OK,
 * we know that all items were successfully mark as deleted and we can therefore add all soft-deleted items into
 * the 'lstDeleted' list at once
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] lpDatabase Reference to a database object; cannot be NULL.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] lstDeleteItems List with objecta which must be deleted.
 * @param[out] lstDeleted List with deleted objects.
 *
 */
static ECRESULT DeleteObjectSoft(ECSession *lpSession, ECDatabase *lpDatabase,
    unsigned int ulFlags, ECListDeleteItems &lstDeleteItems,
    ECListDeleteItems &lstDeleted)
{
	FILETIME ft;
	std::string strInclauseOQQ, strInclause;
	std::map<unsigned int, PARENTINFO> mapFolderCounts;

	// Build where condition
	for (const auto &di : lstDeleteItems) {
		bool k = (di.ulObjType == MAPI_MESSAGE &&
			di.ulParentType == MAPI_FOLDER) ||
			di.ulObjType == MAPI_FOLDER ||
			di.ulObjType == MAPI_STORE;
		if (!k)
			continue;
		if (di.fInOGQueue) {
			if(!strInclauseOQQ.empty())
				strInclauseOQQ += ",";
			strInclauseOQQ += stringify(di.ulId);
		}
		if (!di.fRoot)
			continue;
		if (!strInclause.empty())
			strInclause += ",";
		strInclause += stringify(di.ulId);
		// Track counter changes
		if (di.ulParentType != MAPI_FOLDER)
			continue;
		// Ignore already-softdeleted items
		if ((di.ulFlags & MSGFLAG_DELETED) != 0)
			continue;
		if (di.ulObjType == MAPI_MESSAGE) {
			if (di.ulFlags & MAPI_ASSOCIATED) {
				--mapFolderCounts[di.ulParent].lAssoc;
				++mapFolderCounts[di.ulParent].lDeletedAssoc;
			} else {
				--mapFolderCounts[di.ulParent].lItems;
				++mapFolderCounts[di.ulParent].lDeleted;
				if ((di.ulMessageFlags & MSGFLAG_READ) == 0)
					--mapFolderCounts[di.ulParent].lUnread;
			}
		}
		if (di.ulObjType == MAPI_FOLDER) {
			--mapFolderCounts[di.ulParent].lFolders;
			++mapFolderCounts[di.ulParent].lDeletedFolders;
		}
	}

	// Mark all items as deleted, if a item in the outgoingqueue and remove the submit flag
	if (!strInclauseOQQ.empty())
	{
		// Remove any entries in the outgoing queue for deleted items
		auto strQuery = "DELETE FROM outgoingqueue WHERE hierarchy_id IN ( " + strInclauseOQQ + ")";
		auto er = lpDatabase->DoDelete(strQuery);
		if(er!= erSuccess)
			return er;
		// Remove the submit flag
		strQuery = "UPDATE properties SET val_ulong=val_ulong&~" + stringify(MSGFLAG_SUBMIT)+" WHERE hierarchyid IN(" + strInclauseOQQ + ") AND tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + " and type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS));
		er = lpDatabase->DoUpdate(strQuery);
		if(er!= erSuccess)
			return er;
	}

	if(!strInclause.empty())
	{
		// Mark item as deleted
		auto strQuery = "UPDATE hierarchy SET flags=flags|" + stringify(MSGFLAG_DELETED) + " WHERE id IN(" + strInclause + ")";
		auto er = lpDatabase->DoUpdate(strQuery);
		if(er!= erSuccess)
			return er;
		// Remove the MSGSTATUS_DELMARKED flag (IMAP gateway set)
		strQuery = "UPDATE properties SET val_ulong=val_ulong&~" + stringify(MSGSTATUS_DELMARKED) +
			" WHERE hierarchyid IN(" + strInclause + ") AND tag = " + stringify(PROP_ID(PR_MSG_STATUS)) + " and type = " + stringify(PROP_TYPE(PR_MSG_STATUS));
		er = lpDatabase->DoUpdate(strQuery);
		if(er!= erSuccess)
			return er;
	}

	auto er = ApplyFolderCounts(lpDatabase, mapFolderCounts);
	if(er != erSuccess)
		return er;

	// Add properties: PR_DELETED_ON
	GetSystemTimeAsFileTime(&ft);
	for (const auto &di : lstDeleteItems) {
		bool k = di.fRoot &&
			((di.ulObjType == MAPI_MESSAGE &&
			di.ulParentType == MAPI_FOLDER) ||
			di.ulObjType == MAPI_FOLDER ||
			di.ulObjType == MAPI_STORE);
		if (!k)
			continue;
		auto strQuery = "INSERT INTO properties(hierarchyid, tag, type, val_lo, val_hi) VALUES(" +
			stringify(di.ulId) + "," +
			stringify(PROP_ID(PR_DELETED_ON)) + "," +
			stringify(PROP_TYPE(PR_DELETED_ON)) + "," +
			stringify(ft.dwLowDateTime) + "," +
			stringify(ft.dwHighDateTime) +
			") ON DUPLICATE KEY UPDATE val_lo=" +
			stringify(ft.dwLowDateTime) + ",val_hi=" +
			stringify(ft.dwHighDateTime);
		er = lpDatabase->DoUpdate(strQuery);
		if (er!= erSuccess)
			return er;
		er = ECTPropsPurge::AddDeferredUpdateNoPurge(lpDatabase,
		     di.ulParent, 0, di.ulId);
		if (er != erSuccess)
			return er;
	}

	lstDeleted = lstDeleteItems;
	return erSuccess;
}

/**
 * Hard delete objects, remove the data from storage
 *
 * This means we should be really deleting the actual data from the database and storage. This will be done in
 * bachtches of 32 items each because deleting records is generally fairly slow. Also, very large delete batches
 * can taking up to more than an hour to process. We don't want to have a transaction lasting an hour because it
 * would cause lots of locking problems. Also, each item successfully deleted and committed to the database will
 * added into a list. So, If something fails we notify the items in the 'deleted items list' only.
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] lpDatabase Reference to a database object; cannot be NULL.
 * @param[in] lpAttachmentStorage Reference to an Attachment object. could not NULL if bNoTransaction is true.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] lstDeleteItems List with objects which must be deleted.
 * @param[in] bNoTransaction Disable the database transactions.
 * @param[in] lstDeleted List with deleted objects.
 *
 * @return Kopano error code
 */
ECRESULT DeleteObjectHard(ECSession *lpSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, unsigned int ulFlags, ECListDeleteItems &lstDeleteItems, bool bNoTransaction, ECListDeleteItems &lstDeleted)
{
	ECRESULT er = erSuccess;
	std::shared_ptr<ECAttachmentStorage> lpInternalAttachmentStorage;
	std::list<ULONG> lstDeleteAttachments;
	std::string strInclause;
	std::string strOGQInclause;
	std::string strQuery;
	ECListDeleteItems lstToBeDeleted;
	PARENTINFO pi;
	std::map<unsigned int, PARENTINFO> mapFolderCounts;
	int i;

	if (!(ulFlags & EC_DELETE_HARD_DELETE))
		return er = KCERR_INVALID_PARAMETER;
	if (bNoTransaction && lpAttachmentStorage == NULL) {
		assert(false);
		return KCERR_INVALID_PARAMETER;
	}
	if (!lpAttachmentStorage) {
		lpInternalAttachmentStorage.reset(g_lpSessionManager->get_atxconfig()->new_handle(lpDatabase));
		lpAttachmentStorage = lpInternalAttachmentStorage.get();
	}

	for (auto iterDeleteItems = lstDeleteItems.crbegin();
	     iterDeleteItems != lstDeleteItems.crend(); ) {
		strInclause.clear();
		strOGQInclause.clear();
		lstDeleteAttachments.clear();
		lstToBeDeleted.clear();
		i = 0;

		// Delete max 32 items per query
		while (i < 32 && iterDeleteItems != lstDeleteItems.crend()) {
			if(!strInclause.empty())
				strInclause += ",";
			strInclause += stringify(iterDeleteItems->ulId);
			if(iterDeleteItems->fInOGQueue) {
				if(!strOGQInclause.empty())
					strOGQInclause += ",";
				strOGQInclause += stringify(iterDeleteItems->ulId);
			}

			// make new list for attachment deletes. messages can have imap "attachment".
			if (iterDeleteItems->ulObjType == MAPI_ATTACH || (iterDeleteItems->ulObjType == MAPI_MESSAGE && iterDeleteItems->ulParentType == MAPI_FOLDER))
				lstDeleteAttachments.emplace_back(iterDeleteItems->ulId);
			lstToBeDeleted.emplace_front(*iterDeleteItems);

			if(!(ulFlags&EC_DELETE_STORE) && iterDeleteItems->ulParentType == MAPI_FOLDER && iterDeleteItems->fRoot) {
				// Track counter changes
				pi = decltype(pi)();
				pi.ulStoreId = iterDeleteItems->ulStoreId;
				mapFolderCounts.emplace(iterDeleteItems->ulParent, pi);

				if(iterDeleteItems->ulObjType == MAPI_MESSAGE) {
					if(iterDeleteItems->ulFlags == MAPI_ASSOCIATED) {
						// Delete associated
						--mapFolderCounts[iterDeleteItems->ulParent].lAssoc;
					} else if(iterDeleteItems->ulFlags == 0) {
						// Deleting directly from normal item, count normal and unread items
						--mapFolderCounts[iterDeleteItems->ulParent].lItems;
						if((iterDeleteItems->ulMessageFlags & MSGFLAG_READ) == 0)
							--mapFolderCounts[iterDeleteItems->ulParent].lUnread;
					} else if(iterDeleteItems->ulFlags == (MAPI_ASSOCIATED | MSGFLAG_DELETED)) {
						// Deleting softdeleted associated item
						--mapFolderCounts[iterDeleteItems->ulParent].lDeletedAssoc;
					} else {
						// Deleting normal softdeleted item
						--mapFolderCounts[iterDeleteItems->ulParent].lDeleted;
					}
				}
				if(iterDeleteItems->ulObjType == MAPI_FOLDER) {
					if ((iterDeleteItems->ulFlags & MSGFLAG_DELETED) == 0)
						--mapFolderCounts[iterDeleteItems->ulParent].lFolders;
					else
						--mapFolderCounts[iterDeleteItems->ulParent].lDeletedFolders;
				}
			}
			++i;
			++iterDeleteItems;
		}

		// Start transaction
		kd_trans atx, dtx;
		if (!bNoTransaction) {
			atx = lpAttachmentStorage->Begin(er);
			if (er != erSuccess)
				return er;
			dtx = lpDatabase->Begin(er);
			if (er != erSuccess)
				return er;
		}

		if(!strInclause.empty()) {
			// First, Remove any entries in the outgoing queue for deleted items
			if(!strOGQInclause.empty()) {
				strQuery = "DELETE FROM outgoingqueue WHERE hierarchy_id IN ( " + strOGQInclause + ")";
				er = lpDatabase->DoDelete(strQuery);
				if(er!= erSuccess)
					return er;
			}

			// Then, the hierarchy entries of all the objects
			strQuery = "DELETE FROM hierarchy WHERE id IN (" + strInclause + ")";
			er = lpDatabase->DoDelete(strQuery);
			if(er!= erSuccess)
				return er;
			// Then, the table properties for the objects we just deleted
			strQuery = "DELETE FROM tproperties WHERE hierarchyid IN (" + strInclause + ")";
			er = lpDatabase->DoDelete(strQuery);
			if(er!= erSuccess)
				return er;
			// Then, the properties for the objects we just deleted
			strQuery = "DELETE FROM properties WHERE hierarchyid IN (" + strInclause + ")";
			er = lpDatabase->DoDelete(strQuery);
			if(er!= erSuccess)
				return er;
			// Then, the MVproperties for the objects we just deleted
			strQuery = "DELETE FROM mvproperties WHERE hierarchyid IN (" + strInclause + ")";
			er = lpDatabase->DoDelete(strQuery);
			if(er!= erSuccess)
				return er;
			// Then, the acls for the objects we just deleted (if exist)
			strQuery = "DELETE FROM acl WHERE hierarchy_id IN (" + strInclause + ")";
			er = lpDatabase->DoDelete(strQuery);
			if(er != erSuccess)
				return er;
			// remove indexedproperties
			strQuery = "DELETE FROM indexedproperties WHERE hierarchyid IN (" + strInclause + ")";
			er = lpDatabase->DoDelete(strQuery);
			if(er != erSuccess)
				return er;
			// remove deferred table updates
			strQuery = "DELETE FROM deferredupdate WHERE hierarchyid IN (" + strInclause + ")";
			er = lpDatabase->DoDelete(strQuery);
			if(er != erSuccess)
				return er;
		}
		// list may contain non-attachment object IDs!
		if (!lstDeleteAttachments.empty()) {
			er = lpAttachmentStorage->DeleteAttachments(lstDeleteAttachments);
			if (er != erSuccess)
				return er;
		}

		er = ApplyFolderCounts(lpDatabase, mapFolderCounts);
		if(er != erSuccess)
			return er;
		// Clear map for next round
		mapFolderCounts.clear();
		// Commit the transaction
		if (!bNoTransaction) {
			er = atx.commit();
			if (er != erSuccess)
				return er;
			er = dtx.commit();
			if(er != erSuccess)
				return er;
		}
		// Deletes have been committed, add the deleted items to the list of items we have deleted
		lstDeleted.splice(lstDeleted.begin(),lstToBeDeleted);
	} // while iterDeleteItems != end()
	return erSuccess;
}

/*
 * Deleted object cache updates
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] lstDeleted List with deleted objects.
 */
ECRESULT DeleteObjectCacheUpdate(ECSession *lpSession, unsigned int ulFlags, ECListDeleteItems &lstDeleted)
{
	if (lpSession == NULL)
		return KCERR_INVALID_PARAMETER;
	auto lpSessionManager = lpSession->GetSessionManager();
	auto lpCacheManager = lpSessionManager->GetCacheManager();

	// Remove items from cache and update the outgoing queue
	for (const auto &di : lstDeleted) {
		// update the cache
		lpCacheManager->Update(fnevObjectDeleted, di.ulId);
		if (di.fRoot)
			lpCacheManager->Update(fnevObjectModified, di.ulParent);
		// Update cache, Remove index properties
		if (ulFlags & EC_DELETE_HARD_DELETE)
			lpCacheManager->RemoveIndexData(di.ulId);
	}
	return erSuccess;
}

/*
 * Deleted object notifications
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] lstDeleted List with deleted objects.
 */
static ECRESULT DeleteObjectNotifications(ECSession *lpSession,
    unsigned int ulFlags, ECListDeleteItems &lstDeleted)
{
	std::list<unsigned int> lstParent;
	ECMapTableChangeNotifications mapTableChangeNotifications;
	//std::set<unsigned int>	setFolderParents;
	size_t cDeleteditems = lstDeleted.size();
	unsigned int ulGrandParent = 0;

	if (lpSession == NULL)
		return KCERR_INVALID_PARAMETER;
	auto lpSessionManager = lpSession->GetSessionManager();

	// Now, send the notifications for MAPI_MESSAGE and MAPI_FOLDER types
	for (auto &di : lstDeleted) {
		// Update the outgoing queue
		// Remove the item from both the local and master outgoing queues
		if ((di.ulFlags & MSGFLAG_SUBMIT) &&
		    di.ulParentType == MAPI_FOLDER &&
		    di.ulObjType == MAPI_MESSAGE) {
			lpSessionManager->UpdateOutgoingTables(ECKeyTable::TABLE_ROW_DELETE, di.ulStoreId, di.ulId, EC_SUBMIT_LOCAL, MAPI_MESSAGE);
			lpSessionManager->UpdateOutgoingTables(ECKeyTable::TABLE_ROW_DELETE, di.ulStoreId, di.ulId, EC_SUBMIT_MASTER, MAPI_MESSAGE);
		}

		bool k = (di.ulParentType == MAPI_FOLDER &&
			di.ulObjType == MAPI_MESSAGE) ||
			di.ulObjType == MAPI_FOLDER;
		if (!k)
			continue;
		// Notify that the message has been deleted
		lpSessionManager->NotificationDeleted(di.ulObjType, di.ulId,
			di.ulStoreId, &di.sEntryId, di.ulParent,
			di.ulFlags & (MSGFLAG_ASSOCIATED | MSGFLAG_DELETED));
		// Update all tables viewing this message
		if (cDeleteditems < EC_TABLE_CHANGE_THRESHOLD) {
			lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_DELETE,
				di.ulFlags & (MSGFLAG_ASSOCIATED | MSGFLAG_DELETED),
				di.ulParent, di.ulId, di.ulObjType);
			if ((ulFlags & EC_DELETE_HARD_DELETE) != EC_DELETE_HARD_DELETE)
				// Update all tables viewing this message
				lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD,
					MSGFLAG_DELETED, di.ulParent, di.ulId, di.ulObjType);
		} else {
			// We need to send a table change notifications later on
			mapTableChangeNotifications[di.ulParent].emplace(di.ulObjType, di.ulFlags & MSGFLAG_NOTIFY_FLAGS);
			if ((ulFlags & EC_DELETE_HARD_DELETE) != EC_DELETE_HARD_DELETE)
				mapTableChangeNotifications[di.ulParent].emplace(di.ulObjType, (di.ulFlags & MSGFLAG_NOTIFY_FLAGS) | MSGFLAG_DELETED);
		}
		// @todo: Is this correct ???
		if (di.fRoot)
			 lstParent.emplace_back(di.ulParent);
	}

	// We have a list of all the folders in which something was deleted, so get a unique list
	lstParent.sort();
	lstParent.unique();

	// Now, send each parent folder a notification that it has been altered and send
	// its parent a notification (ie the grandparent of the deleted object) that its
	// hierarchy table has been changed.
	for (auto pa_id : lstParent) {
		if(cDeleteditems >= EC_TABLE_CHANGE_THRESHOLD) {
			// Find the set of notifications to send for the current parent.
			auto pn = mapTableChangeNotifications.find(pa_id);
			if (pn != mapTableChangeNotifications.cend())
				// Iterate the set and send notifications.
				for (auto n = pn->second.cbegin();
				     n != pn->second.cend(); ++n)
					lpSessionManager->UpdateTables(ECKeyTable::TABLE_CHANGE,
						n->ulFlags, pa_id, 0, n->ulType);
		}
		lpSessionManager->NotificationModified(MAPI_FOLDER, pa_id, 0, true);
		if (lpSessionManager->GetCacheManager()->GetParent(pa_id, &ulGrandParent) == erSuccess)
			lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY,
				0, ulGrandParent, pa_id, MAPI_FOLDER);
	}
	return erSuccess;
}

/**
 * Mark a store as deleted
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] lpDatabase Reference to a database object; cannot be NULL.
 * @param[in] ulStoreHierarchyId Store id to be delete
 * @param[in] ulSyncId ??????
 *
 * @return Kopano error code
 */
ECRESULT MarkStoreAsDeleted(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulStoreHierarchyId, unsigned int ulSyncId)
{
	FILETIME ft;

	if (lpSession == NULL || lpDatabase == NULL)
		return KCERR_INVALID_PARAMETER;
	auto lpSessionManager = lpSession->GetSessionManager();
	auto lpSearchFolders = lpSessionManager->GetSearchFolders();
	auto lpCacheManager = lpSessionManager->GetCacheManager();

	// Remove search results for deleted store
	lpSearchFolders->RemoveSearchFolder(ulStoreHierarchyId);
	// Mark item as deleted
	auto strQuery = "UPDATE hierarchy SET flags=flags|"+stringify(MSGFLAG_DELETED)+" WHERE id="+stringify(ulStoreHierarchyId);
	auto er = lpDatabase->DoUpdate(strQuery);
	if(er!= erSuccess)
		return er;

	// Add properties: PR_DELETED_ON
	GetSystemTimeAsFileTime(&ft);
	strQuery = "INSERT INTO properties(hierarchyid, tag, type, val_lo, val_hi) VALUES("+stringify(ulStoreHierarchyId)+","+stringify(PROP_ID(PR_DELETED_ON))+","+stringify(PROP_TYPE(PR_DELETED_ON))+","+stringify(ft.dwLowDateTime)+","+stringify(ft.dwHighDateTime)+") ON DUPLICATE KEY UPDATE val_lo="+stringify(ft.dwLowDateTime)+",val_hi="+stringify(ft.dwHighDateTime);
	er = lpDatabase->DoUpdate(strQuery);
	if(er!= erSuccess)
		return er;
	lpCacheManager->Update(fnevObjectDeleted, ulStoreHierarchyId);
	return erSuccess;
}

/*
 * Delete objects from different stores.
 *
 * Delete a store, folders, messages, recipients and attachments.
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] lpDatabase Reference to a database object; cannot be NULL.
 * @param[in] ulObjectId Itemid which must be expand.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] ulSyncId ??????
 * @param[in] bNoTransaction Disable the database transactions.
 * @param[in] bCheckPermission Check the objects delete permissions.
 *
 * @return Kopano error code
 */
ECRESULT DeleteObjects(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulObjectId, unsigned int ulFlags, unsigned int ulSyncId, bool bNoTransaction, bool bCheckPermission)
{
	ECListInt sObjectList = {ulObjectId};
	return DeleteObjects(lpSession, lpDatabase, &sObjectList, ulFlags, ulSyncId, bNoTransaction, bCheckPermission);
}

/*
 * Delete objects from different stores.
 *
 * Delete a store, folders, messages, recipients and attachments.
 *
 * @param[in] lpSession Reference to a session object; cannot be NULL.
 * @param[in] lpDatabase Reference to a database object; cannot be NULL.
 * @param[in] lpsObjectList Reference to a list of objects that contains itemid and must be expanded.
 * @param[in] ulFlags Bitmask of flags that controls how the objects will deleted.
 * @param[in] ulSyncId ??????
 * @param[in] bNoTransaction Disable the database transactions.
 * @param[in] bCheckPermission Check the objects delete permissions.
 *
 * @return Kopano error code
 */
ECRESULT DeleteObjects(ECSession *lpSession, ECDatabase *lpDatabase, ECListInt *lpsObjectList, unsigned int ulFlags, unsigned int ulSyncId, bool bNoTransaction, bool bCheckPermission)
{
	ECRESULT er = erSuccess;
	ECListDeleteItems lstDeleteItems, lstDeleted;
	auto cleanup = make_scope_success([&]() { FreeDeletedItems(&lstDeleteItems); });

	if (lpSession == nullptr || lpDatabase == nullptr || lpsObjectList == nullptr)
		return er = KCERR_INVALID_PARAMETER;

	// Make sure we're only deleting things once
	lpsObjectList->sort();
	lpsObjectList->unique();
	auto lpSessionManager = lpSession->GetSessionManager();
	auto lpSearchFolders = lpSessionManager->GetSearchFolders();

	if ((bNoTransaction && (ulFlags & EC_DELETE_HARD_DELETE)) ||
		(bNoTransaction && (ulFlags&EC_DELETE_STORE)) ) {
		assert(false); // This means that the caller has a transaction but that's not allowed
		return er = KCERR_INVALID_PARAMETER;
	}

	kd_trans dtx;
	if(!(ulFlags & EC_DELETE_HARD_DELETE) && !bNoTransaction) {
		dtx = lpDatabase->Begin(er);
		if (er != erSuccess)
			return er;
	}

	// Collect recursive parent objects, validate item and check the permissions
	er = ExpandDeletedItems(lpSession, lpDatabase, lpsObjectList, ulFlags, bCheckPermission, &lstDeleteItems);
	if (er != erSuccess) {
		ec_log_info("Error while expanding delete item list, error code %u", er);
		return er;
	}

	// Remove search results for deleted folders
	if (ulFlags & EC_DELETE_STORE)
		lpSearchFolders->RemoveSearchFolder(lpsObjectList->front());
	else
		for (const auto &di : lstDeleteItems)
			if (di.ulObjType == MAPI_FOLDER &&
			    di.ulFlags == FOLDER_SEARCH)
				lpSearchFolders->RemoveSearchFolder(di.ulStoreId, di.ulId);

	// before actual delete check items which are outside the sync scope
	if (ulSyncId != 0)
		CheckICSDeleteScope(lpDatabase, lstDeleteItems, ulSyncId);

	// Mark or delete objects
	if(ulFlags & EC_DELETE_HARD_DELETE)
		er = DeleteObjectHard(lpSession, lpDatabase, NULL, ulFlags, lstDeleteItems, bNoTransaction, lstDeleted);
	else
		er = DeleteObjectSoft(lpSession, lpDatabase, ulFlags, lstDeleteItems, lstDeleted);
	if (er != erSuccess) {
		ec_log_info("Error while deleting expanded item list, error code %u", er);
		return er;
	}

	if (!(ulFlags&EC_DELETE_STORE)) {
		//Those functions are not called with a store delete
		// Update store size
		er = DeleteObjectStoreSize(lpSession, lpDatabase, ulFlags, lstDeleted);
		if(er!= erSuccess) {
			ec_log_info("Error while updating store sizes after delete, error code %u", er);
			return er;
		}

		// Update ICS
		er = DeleteObjectUpdateICS(lpSession, ulFlags, lstDeleted, ulSyncId);
		if (er != erSuccess) {
			ec_log_info("Error while updating ICS after delete, error code %u", er);
			return er;
		}

		// Update local commit time on top level folders
		for (const auto &di : lstDeleteItems) {
			bool k = di.fRoot && di.ulParentType == MAPI_FOLDER &&
				di.ulObjType == MAPI_MESSAGE;
			if (!k)
				continue;
			er = WriteLocalCommitTimeMax(NULL, lpDatabase, di.ulParent, NULL);
			if (er != erSuccess) {
				ec_log_info("Error while updating folder access time after delete, error code %u", er);
				return er;
			}
			// the folder will receive a changed notification anyway, since items are being deleted from it
		}
	}

	// Finish transaction
	if(!(ulFlags & EC_DELETE_HARD_DELETE) && !bNoTransaction) {
		er = dtx.commit();
		if (er != erSuccess)
			return er;
	}

	// Update cache
	DeleteObjectCacheUpdate(lpSession, ulFlags, lstDeleted);
	// Send notifications
	if (!(ulFlags&EC_DELETE_STORE))
		DeleteObjectNotifications(lpSession, ulFlags, lstDeleted);
	return erSuccess;
}

/**
 * Update PR_LOCAL_COMMIT_TIME_MAX property on a folder which contents has changed.
 *
 * This function should be called when the contents of a folder change
 * Affected: saveObject, emptyFolder, deleteObjects, (not done: copyObjects, copyFolder)
 *
 * @param[in]  soap soap struct used for allocating memory for return value, can be NULL
 * @param[in]  lpDatabase database pointer, should be in transaction already
 * @param[in]  ulFolderId folder to update property in
 * @param[out] ppvTime time property that was written on the folder, can be NULL
 *
 * @return Kopano error code
 * @retval KCERR_DATABASE_ERROR database could not be updated
 */
// @todo add parameter to pass ulFolderIdType, to check that it contains MAPI_FOLDER.
ECRESULT WriteLocalCommitTimeMax(struct soap *soap, ECDatabase *lpDatabase, unsigned int ulFolderId, propVal **ppvTime)
{
	FILETIME ftNow;
	propVal *pvTime = NULL;

	GetSystemTimeAsFileTime(&ftNow);

	if (soap && ppvTime) {
		pvTime = s_alloc<propVal>(soap);
		pvTime->ulPropTag = PR_LOCAL_COMMIT_TIME_MAX;
		pvTime->__union = SOAP_UNION_propValData_hilo;
		pvTime->Value.hilo = s_alloc<hiloLong>(soap);
		pvTime->Value.hilo->hi = ftNow.dwHighDateTime;
		pvTime->Value.hilo->lo = ftNow.dwLowDateTime;
	}

	auto strQuery = "INSERT INTO properties (hierarchyid, tag, type, val_hi, val_lo) VALUES ("
		+stringify(ulFolderId)+","+stringify(PROP_ID(PR_LOCAL_COMMIT_TIME_MAX))+","+stringify(PROP_TYPE(PR_LOCAL_COMMIT_TIME_MAX))+","
		+stringify(ftNow.dwHighDateTime)+","+stringify(ftNow.dwLowDateTime)+")"+
		" ON DUPLICATE KEY UPDATE val_hi="+stringify(ftNow.dwHighDateTime)+",val_lo="+stringify(ftNow.dwLowDateTime);
	auto er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		return er;
	if (ppvTime)
		*ppvTime = std::move(pvTime);
	return erSuccess;
}

static void FreeDeleteItem(DELETEITEM *src)
{
	s_free(nullptr, src->sEntryId.__ptr);
}

void FreeDeletedItems(ECListDeleteItems *lplstDeleteItems)
{
	for (auto &di : *lplstDeleteItems)
		FreeDeleteItem(&di);
	lplstDeleteItems->clear();
}

/**
 * Update value in tproperties by taking value from properties for a list of objects
 *
 * This should be called whenever a value is changed in the 'properties' table outside WriteProps(). It updates the value
 * in tproperties if necessary (it may not be in tproperties at all).
 *
 * @param[in] lpDatabase Database handle
 * @param[in] ulPropTag Property tag to update in tproperties
 * @param[in] ulFolderId Folder ID for all objects in lpObjectIDs
 * @param[in] lpObjectIDs List of object IDs to update
 * @return result
 */
ECRESULT UpdateTProp(ECDatabase *lpDatabase, unsigned int ulPropTag, unsigned int ulFolderId, ECListInt *lpObjectIDs) {
    if(lpObjectIDs->empty())
		return erSuccess; // Nothing to do

    // Update tproperties by taking value from properties
	auto strQuery = "UPDATE tproperties JOIN properties on properties.hierarchyid=tproperties.hierarchyid AND properties.tag=tproperties.tag AND properties.type=tproperties.type SET tproperties.val_ulong = properties.val_ulong "
	                "WHERE properties.tag = " + stringify(PROP_ID(ulPropTag)) + " AND properties.type = " + stringify(PROP_TYPE(ulPropTag)) + " AND tproperties.folderid = " + stringify(ulFolderId) + " AND properties.hierarchyid IN (" +
	                kc_join(*lpObjectIDs, ",", stringify) + ")";
	return lpDatabase->DoUpdate(strQuery);
}

/**
 * Update value in tproperties by taking value from properties for a single object
 *
 * This should be called whenever a value is changed in the 'properties' table outside WriteProps(). It updates the value
 * in tproperties if necessary (it may not be in tproperties at all).
 *
 * @param[in] lpDatabase Database handle
 * @param[in] ulPropTag Property tag to update in tproperties
 * @param[in] ulFolderId Folder ID for all objects in lpObjectIDs
 * @param[in] ulObjId Object ID to update
 * @return result
 */
ECRESULT UpdateTProp(ECDatabase *lpDatabase, unsigned int ulPropTag, unsigned int ulFolderId, unsigned int ulObjId) {
    ECListInt list;
	list.emplace_back(ulObjId);
    return UpdateTProp(lpDatabase, ulPropTag, ulFolderId, &list);
}

/**
 * Update folder count for a folder by adding or removing a certain amount
 *
 * This function can be used to incrementally update a folder count of a folder. The lDelta can be positive (counter increases)
 * or negative (counter decreases)
 *
 * @param lpDatabase Database handle
 * @param ulFolderId Folder ID to update
 * @param ulPropTag Counter property to update (must be type that uses val_ulong (PT_LONG or PT_BOOLEAN))
 * @param lDelta Signed integer change
 * @return result
 */
ECRESULT UpdateFolderCount(ECDatabase *lpDatabase, unsigned int ulFolderId, unsigned int ulPropTag, int lDelta)
{
	unsigned int ulParentId, ulType;

	if(lDelta == 0)
		return erSuccess; // No change
	auto er = g_lpSessionManager->GetCacheManager()->GetObject(ulFolderId, &ulParentId, NULL, NULL, &ulType);
	if(er != erSuccess)
		return er;
	if (ulType != MAPI_FOLDER) {
		ec_log_info("Not updating folder count %d for non-folder object %d type %d", lDelta, ulFolderId, ulType);
		assert(ulType == MAPI_FOLDER);
		return erSuccess;
	}

	std::string strQuery = "UPDATE properties SET val_ulong = ";
	// make sure val_ulong stays a positive number
	if (lDelta < 0)
		strQuery += "IF (val_ulong >= " + stringify_signed(abs(lDelta)) + ", val_ulong + " + stringify_signed(lDelta) + ", 0)";
	else
		strQuery += "val_ulong + " + stringify_signed(lDelta);
	strQuery += " WHERE hierarchyid = " + stringify(ulFolderId) + " AND tag = " + stringify(PROP_ID(ulPropTag)) + " AND type = " + stringify(PROP_TYPE(ulPropTag));
	er = lpDatabase->DoUpdate(strQuery);
	if(er != erSuccess)
		return er;
	return UpdateTProp(lpDatabase, ulPropTag, ulParentId, ulFolderId);
}

ECRESULT CheckQuota(ECSession *lpecSession, ULONG ulStoreId)
{
	long long llStoreSize = 0;
	eQuotaStatus QuotaStatus = QUOTA_OK;
	auto sec = lpecSession->GetSecurity();
	auto er = sec->GetStoreSize(ulStoreId, &llStoreSize);
	if (er != erSuccess)
		return er;
	er = sec->CheckQuota(ulStoreId, llStoreSize, &QuotaStatus);
	if (er != erSuccess)
		return er;
	if (QuotaStatus == QUOTA_HARDLIMIT)
		return KCERR_STORE_FULL;
	return erSuccess;
}

ECRESULT MapEntryIdToObjectId(ECSession *lpecSession, ECDatabase *lpDatabase, ULONG ulObjId, const entryId &sEntryId)
{
	auto er = RemoveStaleIndexedProp(lpDatabase, PR_ENTRYID, sEntryId.__ptr, sEntryId.__size);
	if(er != erSuccess) {
		ec_log_crit("ERROR: Collision detected while setting entryid. objectid=%u, entryid=%s, user=%u", ulObjId, bin2hex(sEntryId.__size, sEntryId.__ptr).c_str(), lpecSession->GetSecurity()->GetUserId());
		return KCERR_DATABASE_ERROR;
	}
	auto strQuery = "INSERT INTO indexedproperties (hierarchyid,tag,val_binary) VALUES(" + stringify(ulObjId) + ", 4095, " + lpDatabase->EscapeBinary(sEntryId.__ptr, sEntryId.__size) + ")";
	er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		return er;
	g_lpSessionManager->GetCacheManager()->SetObjectEntryId((entryId*)&sEntryId, ulObjId);
	return erSuccess;
}

ECRESULT UpdateFolderCounts(ECDatabase *lpDatabase, ULONG ulParentId, ULONG ulFlags, propValArray *lpModProps)
{
	ECRESULT er = erSuccess;

	if (ulFlags & MAPI_ASSOCIATED)
		return UpdateFolderCount(lpDatabase, ulParentId, PR_ASSOC_CONTENT_COUNT, 1);
	er = UpdateFolderCount(lpDatabase, ulParentId, PR_CONTENT_COUNT, 1);
	struct propVal *lpPropMessageFlags = NULL;
	lpPropMessageFlags = FindProp(lpModProps, PR_MESSAGE_FLAGS);
	if (er == erSuccess && (!lpPropMessageFlags || (lpPropMessageFlags->Value.ul & MSGFLAG_READ) == 0))
		er = UpdateFolderCount(lpDatabase, ulParentId, PR_CONTENT_UNREAD, 1);
	return er;
}

/**
 * Handles the outgoingqueue table according to the PR_MESSAGE_FLAGS
 * of a message. This function does not do transactions, so you must
 * already be in a database transaction.
 *
 * @param[in] lpDatabase Database object
 * @param[in] ulSyncId syncid of the message
 * @param[in] ulStoreId storeid of the message
 * @param[in] ulObjId hierarchyid of the message
 * @param[in] bNewItem message is new
 * @param[in] lpModProps properties of the message
 *
 * @return Kopano error code
 */
ECRESULT ProcessSubmitFlag(ECDatabase *lpDatabase, ULONG ulSyncId, ULONG ulStoreId, ULONG ulObjId, bool bNewItem, propValArray *lpModProps)
{
	DB_RESULT lpDBResult;
	ULONG ulPrevSubmitFlag = 0;

	// If the messages was saved by an ICS syncer, then we need to sync the PR_MESSAGE_FLAGS for MSGFLAG_SUBMIT if it
	// was included in the save.
	auto lpPropMessageFlags = FindProp(lpModProps, PR_MESSAGE_FLAGS);
	if (ulSyncId == 0 || lpPropMessageFlags == nullptr)
		return erSuccess;
	if (bNewItem) {
		// Item is new, so it's not in the queue at the moment
		ulPrevSubmitFlag = 0;
	} else {
		// Existing item. Check its current submit flag by looking at the outgoing queue.
		auto strQuery = "SELECT hierarchy_id FROM outgoingqueue WHERE hierarchy_id=" + stringify(ulObjId) + " AND flags & " + stringify(EC_SUBMIT_MASTER) + " = 0 LIMIT 1";
		auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess)
			return er;
		// Item is (1)/is not (0) in the outgoing queue at the moment
		ulPrevSubmitFlag = lpDBResult.get_num_rows() > 0;
	}

	if ((lpPropMessageFlags->Value.ul & MSGFLAG_SUBMIT) && ulPrevSubmitFlag == 0) {
		// Message was previously not submitted, but it is now, so add it to the outgoing queue and set the correct flags.
		auto strQuery = "INSERT INTO outgoingqueue (store_id, hierarchy_id, flags) VALUES("+stringify(ulStoreId)+", "+stringify(ulObjId)+"," + stringify(EC_SUBMIT_LOCAL) + ")";
		auto er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			return er;
		strQuery = "UPDATE properties SET val_ulong = val_ulong | " + stringify(MSGFLAG_SUBMIT) +
			" WHERE hierarchyid = " + stringify(ulObjId) +
			" AND type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) +
			" AND tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS));
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			return er;
		// The object has changed, update the cache.
		g_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, ulObjId);
		// Update in-memory outgoing tables
		g_lpSessionManager->UpdateOutgoingTables(ECKeyTable::TABLE_ROW_ADD, ulStoreId, ulObjId, EC_SUBMIT_LOCAL, MAPI_MESSAGE);
	} else if ((lpPropMessageFlags->Value.ul & MSGFLAG_SUBMIT) == 0 && ulPrevSubmitFlag == 1) {
		// Message was previously submitted, but is not submitted any more. Remove it from the outgoing queue and remove the flags.
		auto strQuery = "DELETE FROM outgoingqueue WHERE hierarchy_id = " + stringify(ulObjId);
		auto er = lpDatabase->DoDelete(strQuery);
		if (er != erSuccess)
			return er;
		strQuery = "UPDATE properties SET val_ulong = val_ulong & ~" + stringify(MSGFLAG_SUBMIT) +
			" WHERE hierarchyid = " + stringify(ulObjId) +
			" AND type = " + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) +
			" AND tag = " + stringify(PROP_ID(PR_MESSAGE_FLAGS));
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			return er;
		// The object has changed, update the cache.
		g_lpSessionManager->GetCacheManager()->Update(fnevObjectModified, ulObjId);
		// Update in-memory outgoing tables
		g_lpSessionManager->UpdateOutgoingTables(ECKeyTable::TABLE_ROW_DELETE, ulStoreId, ulObjId, EC_SUBMIT_LOCAL, MAPI_MESSAGE);
	}
	return erSuccess;
}

ECRESULT CreateNotifications(ULONG ulObjId, ULONG ulObjType, ULONG ulParentId, ULONG ulGrandParentId, bool bNewItem, propValArray *lpModProps, struct propVal *lpvCommitTime)
{
	unsigned int ulObjFlags = 0, ulParentFlags = 0;
	if (ulObjType != MAPI_MESSAGE && ulObjType != MAPI_FOLDER &&
	    ulObjType != MAPI_STORE)
		return erSuccess;
	auto cache = g_lpSessionManager->GetCacheManager();
	cache->GetObjectFlags(ulObjId, &ulObjFlags);
	// update PR_LOCAL_COMMIT_TIME_MAX in cache for disconnected clients who want to know if the folder contents changed
	if (lpvCommitTime) {
		sObjectTableKey key(ulParentId, 0);
		cache->SetCell(&key, PR_LOCAL_COMMIT_TIME_MAX, lpvCommitTime);
	}

	if (bNewItem) {
		// Notify that the message has been created
		g_lpSessionManager->NotificationCreated(ulObjType, ulObjId, ulParentId);
		g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_ADD, ulObjFlags & MSGFLAG_NOTIFY_FLAGS, ulParentId, ulObjId, ulObjType);
		// Notify that the folder in which the message resided has changed (PR_CONTENT_COUNT, PR_CONTENT_UNREAD)
		if (ulObjFlags & MAPI_ASSOCIATED)
			cache->UpdateCell(ulParentId, PR_ASSOC_CONTENT_COUNT, 1);
		else {
			cache->UpdateCell(ulParentId, PR_CONTENT_COUNT, 1);
			struct propVal *lpPropMessageFlags = FindProp(lpModProps, PR_MESSAGE_FLAGS);
			if (lpPropMessageFlags && (lpPropMessageFlags->Value.ul & MSGFLAG_READ) == 0)
				// Unread message
				cache->UpdateCell(ulParentId, PR_CONTENT_UNREAD, 1);
		}
		g_lpSessionManager->NotificationModified(MAPI_FOLDER, ulParentId, 0, true);
		if (ulGrandParentId) {
			cache->GetObjectFlags(ulParentId, &ulParentFlags);
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, ulParentFlags & MSGFLAG_NOTIFY_FLAGS, ulGrandParentId, ulParentId, MAPI_FOLDER);
		}
	} else if (ulObjType == MAPI_STORE) {
		g_lpSessionManager->NotificationModified(ulObjType, ulObjId);
	} else {
		// Notify that the message has been modified
		if (ulObjType == MAPI_MESSAGE)
			g_lpSessionManager->NotificationModified(ulObjType, ulObjId, ulParentId);
		else
			g_lpSessionManager->NotificationModified(ulObjType, ulObjId);
		if (ulParentId)
			g_lpSessionManager->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY, ulObjFlags & MSGFLAG_NOTIFY_FLAGS, ulParentId, ulObjId, ulObjType);
	}
	return erSuccess;
}

ECRESULT WriteSingleProp(ECDatabase *lpDatabase, unsigned int ulObjId,
    unsigned int ulFolderId, const struct propVal *lpPropVal, bool bColumnProp,
    unsigned int ulMaxQuerySize, std::string &strInsertQuery, bool replace)
{
	std::string strColData, strQueryAppend;
	unsigned int ulColId = 0;

	assert(PROP_TYPE(lpPropVal->ulPropTag) != PT_UNICODE);
	auto er = CopySOAPPropValToDatabasePropVal(lpPropVal, &ulColId, strColData, lpDatabase, bColumnProp);
	if(er != erSuccess)
		return erSuccess; // Data from client was bogus, ignore it.
	if (!strInsertQuery.empty())
		strQueryAppend = ",";
	else if (bColumnProp)
		strQueryAppend = std::string(replace ? "REPLACE" : "INSERT") + " INTO tproperties (hierarchyid,tag,type,folderid," + (std::string)PROPCOLVALUEORDER(tproperties) + ") VALUES";
	else
		strQueryAppend = std::string(replace ? "REPLACE" : "INSERT") + " INTO properties (hierarchyid,tag,type," + (std::string)PROPCOLVALUEORDER(properties) + ") VALUES";

	strQueryAppend += "(" + stringify(ulObjId) + "," +
							stringify(PROP_ID(lpPropVal->ulPropTag)) + "," +
							stringify(PROP_TYPE(lpPropVal->ulPropTag)) + ",";
	if (bColumnProp)
		strQueryAppend += stringify(ulFolderId) + ",";

	for (unsigned int k = 0; k < VALUE_NR_MAX; ++k) {
		if (k == ulColId)
			strQueryAppend += strColData;
		else if (k == VALUE_NR_HILO)
			strQueryAppend += "null,null";
		else
			strQueryAppend += "null";
		if (k != VALUE_NR_MAX-1)
			strQueryAppend += ",";
	}

	strQueryAppend += ")";
	if (ulMaxQuerySize > 0 && strInsertQuery.size() + strQueryAppend.size() > ulMaxQuerySize)
		return KCERR_TOO_BIG;
	strInsertQuery.append(strQueryAppend);
	return erSuccess;
}

ECRESULT WriteProp(ECDatabase *lpDatabase, unsigned int ulObjId,
    unsigned int ulParentId, const struct propVal *lpPropVal, bool replace)
{
	std::string strQuery;

	strQuery.clear();
	WriteSingleProp(lpDatabase, ulObjId, ulParentId, lpPropVal, false, 0, strQuery, replace);
	auto er = lpDatabase->DoInsert(strQuery);
	if(er != erSuccess)
		return er;
	if (ulParentId == 0)
		return erSuccess;
	strQuery.clear();
	WriteSingleProp(lpDatabase, ulObjId, ulParentId, lpPropVal, true, 0, strQuery, replace);
	return lpDatabase->DoInsert(strQuery);
}

/**
 * Batch WriteProp calls for the same ulObjID and ulParentID.
 */
ECRESULT InsertProps(ECDatabase *database, unsigned int objId,
    unsigned int parentId, const std::list<propVal> &propList, bool replace)
{
	std::string query, colquery;

	for (const auto &i : propList) {
		WriteSingleProp(database, objId, parentId, &i, false, 0, query, replace);
		WriteSingleProp(database, objId, parentId, &i, true, 0, colquery, replace);
	}

	auto er = database->DoInsert(query);
	if (er != erSuccess)
		return er;
	if (parentId == 0)
		return erSuccess;
	return database->DoInsert(colquery);
}

ECRESULT GetNamesFromIDs(struct soap *soap, ECDatabase *lpDatabase, struct propTagArray *lpPropTags, struct namedPropArray *lpsNames)
{
	DB_RESULT lpDBResult;
	// Allocate memory for return object
	lpsNames->__ptr = s_alloc<namedProp>(soap, lpPropTags->__size);
	lpsNames->__size = lpPropTags->__size;
	memset(lpsNames->__ptr, 0, sizeof(struct namedProp) * lpPropTags->__size);

	for (gsoap_size_t i = 0; i < lpPropTags->__size; ++i) {
		auto strQuery = "SELECT nameid, namestring, guid FROM names WHERE id=" + stringify(lpPropTags->__ptr[i]-1) + " LIMIT 1";
		auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			return er;
		if (lpDBResult.get_num_rows() != 1) {
			// No entry
			lpsNames->__ptr[i].lpguid = NULL;
			lpsNames->__ptr[i].lpId = NULL;
			lpsNames->__ptr[i].lpString = NULL;
			continue;
		}

		auto lpDBRow = lpDBResult.fetch_row();
		auto lpDBLen = lpDBResult.fetch_row_lengths();
		if (lpDBRow == nullptr) {
			ec_log_crit("GetNamesFromIDs(): row/col NULL");
			return KCERR_DATABASE_ERROR;
		}
		if (lpDBRow[0] != NULL) {
			// It's an ID type
			lpsNames->__ptr[i].lpId = s_alloc<unsigned int>(soap);
			*lpsNames->__ptr[i].lpId = atoi(lpDBRow[0]);
		} else if (lpDBRow[1] != NULL) {
			// It's a String type
			lpsNames->__ptr[i].lpString = s_alloc<char>(soap, strlen(lpDBRow[1]) + 1);
			strcpy(lpsNames->__ptr[i].lpString, lpDBRow[1]);
		}
		if (lpDBRow[2] == nullptr)
			continue;
		// Got a GUID (should always do so ...)
		lpsNames->__ptr[i].lpguid = s_alloc<struct xsd__base64Binary>(soap);
		lpsNames->__ptr[i].lpguid->__size = lpDBLen[2];
		lpsNames->__ptr[i].lpguid->__ptr = s_alloc<unsigned char>(soap, lpDBLen[2]);
		memcpy(lpsNames->__ptr[i].lpguid->__ptr, lpDBRow[2], lpDBLen[2]);
	}
	return erSuccess;
}

/**
 * Resets the folder counts of a folder
 *
 * This function resets the counts of a folder by recalculating them from the actual
 * database child entries. If any of the current counts is out-of-date, they are updated to the
 * correct value and the foldercount_reset counter is increased. Note that in theory, foldercount_reset
 * should always remain at 0. If not, this means that a bug somewhere has failed to update the folder
 * count correctly at some point.
 *
 * WARNING this function creates its own transaction!
 *
 * @param[in] lpSession Session to get database handle, etc
 * @param[in] ulObjId ID of the folder to recalc
 * @param[out] lpulUpdates Will be set to number of counters that were updated (may be NULL)
 * @return result
 */
ECRESULT ResetFolderCount(ECSession *lpSession, unsigned int ulObjId, unsigned int *lpulUpdates)
{
	ECRESULT er = erSuccess;
	DB_RESULT lpDBResult;
	unsigned int ulAffected = 0, ulParent = 0;
	auto sesmgr = lpSession->GetSessionManager();
	auto cache = sesmgr->GetCacheManager();
	ECDatabase *lpDatabase = NULL;
	auto cleanup = make_scope_success([&]() {
		if (er == erSuccess && lpulUpdates != nullptr)
			*lpulUpdates = ulAffected;
	});

	er = lpSession->GetDatabase(&lpDatabase);
	if(er != erSuccess)
		return er;
	auto dtx = lpDatabase->Begin(er);
	if(er != erSuccess)
		return er;

    // Lock the counters now since the locking order is normally counters/foldercontent/storesize/localcommittimemax. So our lock order
    // is now counters/foldercontent/counters which is compatible (*cough* in theory *cough*)
	auto strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid = " + stringify(ulObjId) + " FOR UPDATE";
	er = lpDatabase->DoSelect(strQuery, NULL); // don't care about the result
	if (er != erSuccess)
		return er;

	// Gets counters from hierarchy: cc, acc, dmc, dac, cfc, dfc
	// use for update, since the update query below must see the same values, mysql should already block here.
	strQuery = "SELECT count(if(flags & 0x440 = 0 && type = 5, 1, null)) AS cc, count(if(flags & 0x440 = 0x40 and type = 5, 1, null)) AS acc, count(if(flags & 0x440 = 0x400 and type = 5, 1, null)) AS dmc, count(if(flags & 0x440 = 0x440 and type = 5, 1, null)) AS dac, count(if(flags & 0x400 = 0 and type = 3, 1, null)) AS cfc, count(if(flags & 0x400 and type = 3, 1, null)) AS dfc from hierarchy where parent=" + stringify(ulObjId) + " FOR UPDATE";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();
	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL || lpDBRow[4] == NULL) {
		ec_log_crit("ResetFolderCount(): row/col NULL (1)");
		return er = KCERR_DATABASE_ERROR;
	}
	/*
	 * Content count, assoc. content count, deleted message count,
	 * child folder count, deleted folder count, content unread.
	 */
	std::string strCC = lpDBRow[0], strACC = lpDBRow[1];
	std::string strDMC = lpDBRow[2], strDAC = lpDBRow[3];
	std::string strCFC = lpDBRow[4], strDFC = lpDBRow[5];

	// Gets unread counters from hierarchy / properties / tproperties
	strQuery = "SELECT "
	          // Part one, unread count from non-deferred rows (get the flags from tproperties)
	          "(SELECT count(if(tproperties.val_ulong & 1,null,1)) from hierarchy left join tproperties on tproperties.folderid=" + stringify(ulObjId) + " and tproperties.tag = 0x0e07 and tproperties.type = 3 and tproperties.hierarchyid=hierarchy.id left join deferredupdate on deferredupdate.hierarchyid=hierarchy.id where parent=" + stringify(ulObjId) + " and hierarchy.type=5 and hierarchy.flags = 0 and deferredupdate.hierarchyid is null FOR UPDATE)"
	          " + "
	          // Part two, unread count from deferred rows (get the flags from properties)
	          "(SELECT count(if(properties.val_ulong & 1,null,1)) from hierarchy left join properties on properties.tag = 0x0e07 and properties.type = 3 and properties.hierarchyid=hierarchy.id join deferredupdate on deferredupdate.hierarchyid=hierarchy.id and deferredupdate.folderid = parent where parent=" + stringify(ulObjId) + " and hierarchy.type=5 and hierarchy.flags = 0 FOR UPDATE)"
	          ;
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	if(lpDBRow == NULL || lpDBRow[0] == NULL) {
		ec_log_crit("ResetFolderCount(): row/col NULL (2)");
		return er = KCERR_DATABASE_ERROR;
	}

	std::string strCU = lpDBRow[0];
    strQuery = "UPDATE properties SET val_ulong = CASE tag "
      " WHEN " + stringify(PROP_ID(PR_CONTENT_COUNT)) + " THEN + " + strCC +
      " WHEN " + stringify(PROP_ID(PR_ASSOC_CONTENT_COUNT)) + " THEN + " + strACC +
      " WHEN " + stringify(PROP_ID(PR_DELETED_MSG_COUNT)) + " THEN + " + strDMC +
      " WHEN " + stringify(PROP_ID(PR_DELETED_ASSOC_MSG_COUNT)) + " THEN + " + strDAC +
      " WHEN " + stringify(PROP_ID(PR_FOLDER_CHILD_COUNT)) + " THEN + " + strCFC +
      " WHEN " + stringify(PROP_ID(PR_SUBFOLDERS)) + " THEN + " + strCFC +
      " WHEN " + stringify(PROP_ID(PR_DELETED_FOLDER_COUNT)) + " THEN + " + strDFC +
      " WHEN " + stringify(PROP_ID(PR_CONTENT_UNREAD)) + " THEN + " + strCU +
      " END WHERE hierarchyid = " + stringify(ulObjId) + " AND TAG in (" +
          stringify(PROP_ID(PR_CONTENT_COUNT)) + "," +
          stringify(PROP_ID(PR_ASSOC_CONTENT_COUNT)) + "," +
          stringify(PROP_ID(PR_DELETED_MSG_COUNT)) + "," +
          stringify(PROP_ID(PR_DELETED_ASSOC_MSG_COUNT)) + "," +
          stringify(PROP_ID(PR_FOLDER_CHILD_COUNT)) + "," +
          stringify(PROP_ID(PR_SUBFOLDERS)) + "," +
          stringify(PROP_ID(PR_DELETED_FOLDER_COUNT)) + "," +
          stringify(PROP_ID(PR_CONTENT_UNREAD)) +
      ")";
    er = lpDatabase->DoUpdate(strQuery, &ulAffected);
    if(er != erSuccess)
		return er;
    if (ulAffected == 0)
        // Nothing updated
		return er = erSuccess;

	g_lpSessionManager->m_stats->inc(SCN_DATABASE_COUNTER_RESYNCS);
	er = cache->GetParent(ulObjId, &ulParent);
	if (er != erSuccess)
		// No parent -> root folder. Nothing else we need to do now.
		return er = erSuccess;

    // Update tprops
    strQuery = "REPLACE INTO tproperties (folderid, hierarchyid, tag, type, val_ulong) "
        "SELECT " + stringify(ulParent) + ", properties.hierarchyid, properties.tag, properties.type, properties.val_ulong "
        "FROM properties "
        "WHERE tag IN (" +
          stringify(PROP_ID(PR_CONTENT_COUNT)) + "," +
          stringify(PROP_ID(PR_ASSOC_CONTENT_COUNT)) + "," +
          stringify(PROP_ID(PR_DELETED_MSG_COUNT)) + "," +
          stringify(PROP_ID(PR_DELETED_ASSOC_MSG_COUNT)) + "," +
          stringify(PROP_ID(PR_FOLDER_CHILD_COUNT)) + "," +
          stringify(PROP_ID(PR_SUBFOLDERS)) + "," +
          stringify(PROP_ID(PR_DELETED_FOLDER_COUNT)) + "," +
          stringify(PROP_ID(PR_CONTENT_UNREAD)) +
        ") AND hierarchyid = " + stringify(ulObjId);
    er = lpDatabase->DoInsert(strQuery);
    if(er != erSuccess)
		return er;

    // Clear cache and update table entries. We do not send an object notification since the object hasn't really changed and
    // this is normally called just before opening an entry anyway, so the counters retrieved there will be ok.
	cache->Update(fnevObjectModified, ulObjId);
	return er = sesmgr->UpdateTables(ECKeyTable::TABLE_ROW_MODIFY,
	       0, ulParent, ulObjId, MAPI_FOLDER);
}

/**
 * Removes stale indexed properties
 *
 * In some cases, the database can contain stale (old) indexed properties. One example is when
 * you replicate a store onto a server, then remove that store with kopano-admin --remove-store
 * and then do the replication again. The second replication will attempt to create items with
 * equal entryids and sourcekeys. Since the softdelete purge will not have removed the data from
 * the system yet, we check to see if the indexedproperty that is in the database is actually in
 * use by checking if the store it belongs to is deleted. If so, we remove the entry. If the
 * item is used by a non-deleted store, then an error occurs since you cannot use the same indexed
 * property for two items.
 *
 * @param[in] lpDatabase Database handle
 * @param[in] ulPropTag Property tag to scan for
 * @param[in] lpData Data of the indexed property
 * @param[in] cbSize Bytes in lpData
 * @return result
 */
ECRESULT RemoveStaleIndexedProp(ECDatabase *lpDatabase, unsigned int ulPropTag,
    const unsigned char *lpData, unsigned int cbSize)
{
	DB_RESULT lpDBResult;
	unsigned int ulObjId = 0, ulStoreId = 0;
	bool bStale = false;

	auto strQuery = "SELECT hierarchyid FROM indexedproperties WHERE tag= " + stringify(PROP_ID(ulPropTag)) + " AND val_binary=" + lpDatabase->EscapeBinary(lpData, cbSize) + " LIMIT 1";
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();
    if(!lpDBRow || lpDBRow[0] == NULL)
		return er; /* Nothing there, no need to do anything */
    ulObjId = atoui(lpDBRow[0]);

    // Check if the found item is in a deleted store
    if(g_lpSessionManager->GetCacheManager()->GetStore(ulObjId, &ulStoreId, NULL) == erSuccess) {
        // Find the store
        strQuery = "SELECT hierarchy_id FROM stores WHERE hierarchy_id = " + stringify(ulStoreId) + " LIMIT 1";
        er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if(er != erSuccess)
			return er;
        lpDBRow = lpDBResult.fetch_row();
        if (lpDBRow == nullptr || lpDBRow[0] == nullptr)
            bStale = true;
    } else {
        // The item has no store. This means it's safe to re-use the indexed prop. Possibly the store is half-deleted at this time.
        bStale = true;
    }

    if(bStale) {
        // Item is in a deleted store. This means we can delete it
        er = lpDatabase->DoDelete("DELETE FROM indexedproperties WHERE hierarchyid = " + stringify(ulObjId));
        if(er != erSuccess)
			return er;
        // Remove it from the cache
        g_lpSessionManager->GetCacheManager()->RemoveIndexData(ulPropTag, cbSize, lpData);
    }
	else {
		ec_log_crit("RemoveStaleIndexedProp(): caller wanted to remove the entry, but we cannot since it is in use");
		return KCERR_COLLISION;
	}
	return erSuccess;
}

static ECRESULT ApplyFolderCounts(ECDatabase *lpDatabase,
    unsigned int ulFolderId, const PARENTINFO &pi)
{
	auto er = UpdateFolderCount(lpDatabase, ulFolderId, PR_CONTENT_COUNT, pi.lItems);
	if (er == erSuccess)
		er = UpdateFolderCount(lpDatabase, ulFolderId, PR_CONTENT_UNREAD,   		pi.lUnread);
	if (er == erSuccess)
		er = UpdateFolderCount(lpDatabase, ulFolderId, PR_ASSOC_CONTENT_COUNT,   	pi.lAssoc);
	if (er == erSuccess)
		er = UpdateFolderCount(lpDatabase, ulFolderId, PR_DELETED_MSG_COUNT, 		pi.lDeleted);
	if (er == erSuccess)
		er = UpdateFolderCount(lpDatabase, ulFolderId, PR_DELETED_ASSOC_MSG_COUNT, 	pi.lDeletedAssoc);
	if (er == erSuccess)
		er = UpdateFolderCount(lpDatabase, ulFolderId, PR_SUBFOLDERS,  				pi.lFolders);
	if (er == erSuccess)
		er = UpdateFolderCount(lpDatabase, ulFolderId, PR_FOLDER_CHILD_COUNT,		pi.lFolders);
	if (er == erSuccess)
		er = UpdateFolderCount(lpDatabase, ulFolderId, PR_DELETED_FOLDER_COUNT,		pi.lDeletedFolders);
	return er;
}

ECRESULT ApplyFolderCounts(ECDatabase *lpDatabase, const std::map<unsigned int, PARENTINFO> &mapFolderCounts) {
	// Update folder counts
	for (const auto &p : mapFolderCounts) {
		auto er = ApplyFolderCounts(lpDatabase, p.first, p.second);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

static ECRESULT LockFolders(ECDatabase *lpDatabase, bool bShared,
    const std::set<unsigned int> &setParents)
{
    if(setParents.empty())
		return erSuccess;
	auto strQuery = "SELECT 1 FROM properties WHERE hierarchyid IN(" +
		kc_join(setParents, ",", [](std::set<unsigned int>::key_type p) { return stringify(p); }) + ")";
	if (bShared)
		strQuery += " LOCK IN SHARE MODE";
	else
		strQuery += " FOR UPDATE";
	return lpDatabase->DoSelect(strQuery, NULL);
}

static ECRESULT BeginLockFolders(ECDatabase *lpDatabase, unsigned int ulTag,
    const std::set<std::string> &setIds, unsigned int ulFlags, kd_trans &dtx,
    ECRESULT &dtxerr)
{
    ECRESULT er = erSuccess;
	DB_RESULT lpDBResult;
    DB_ROW lpDBRow = NULL;
    std::set<unsigned int> setMessages, setFolders, setUncachedMessages;
    std::set<std::string> setUncached;
    unsigned int ulId;
    std::string strQuery;

    // See if we can get the object IDs for the passed objects from the cache
    for (const auto &s : setIds) {
		if (g_lpSessionManager->GetCacheManager()->QueryObjectFromProp(ulTag, s.size(),
		    reinterpret_cast<unsigned char *>(const_cast<char *>(s.data())), &ulId) != erSuccess) {
			setUncached.emplace(s);
			continue;
		}
		if (ulTag == PROP_ID(PR_SOURCE_KEY)) {
			setFolders.emplace(ulId);
			continue;
		} else if (ulTag != PROP_ID(PR_ENTRYID)) {
			assert(false);
			continue;
		}

		EntryId eid(s);
		try {
			if (eid.type() == MAPI_FOLDER)
				setFolders.emplace(ulId);
			else if (eid.type() == MAPI_MESSAGE)
				setMessages.emplace(ulId);
			else
				assert(false);
		} catch (const std::runtime_error &e) {
			ec_log_err("eid.type(): %s", e.what());
			return MAPI_E_CORRUPT_DATA;
		}
    }

    if(!setUncached.empty()) {
        // For the items that were uncached, go directly to their parent (or the item itself for folders) in the DB
        strQuery = "SELECT hierarchyid, hierarchy.type, hierarchy.parent, hierarchy.owner, hierarchy.flags FROM indexedproperties JOIN hierarchy ON hierarchy.id=indexedproperties.hierarchyid WHERE tag = " + stringify(ulTag) + " AND val_binary IN(" +
		kc_join(setUncached, ",", [&](const auto &i) { return lpDatabase->EscapeBinary(i); }) + ")";
        er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if(er != erSuccess)
            return er;

        while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
            if(lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL || lpDBRow[4] == NULL)
                continue;
			if(atoui(lpDBRow[1]) != MAPI_MESSAGE && atoui(lpDBRow[1]) != MAPI_FOLDER)
				continue;
            if(atoui(lpDBRow[1]) == MAPI_MESSAGE)
				setFolders.emplace(atoui(lpDBRow[2]));
            else if(atoui(lpDBRow[1]) == MAPI_FOLDER)
				setFolders.emplace(atoui(lpDBRow[0]));
			g_lpSessionManager->GetCacheManager()->SetObject(atoui(lpDBRow[0]), atoui(lpDBRow[2]), atoui(lpDBRow[3]), atoui(lpDBRow[4]), atoui(lpDBRow[1]));
        }
    }

    // For the items that were cached, but messages, find their parents in the cache first
	for (const auto i : setMessages) {
        unsigned int ulParent = 0;

		if (g_lpSessionManager->GetCacheManager()->QueryParent(i, &ulParent) == erSuccess)
			setFolders.emplace(ulParent);
		else
			setUncachedMessages.emplace(i);
    }

    // Query uncached parents from the database
    if(!setUncachedMessages.empty()) {
		strQuery = "SELECT parent FROM hierarchy WHERE id IN(" +
			kc_join(setUncachedMessages, ",", stringify) + ")";
        er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if(er != erSuccess)
            return er;

        while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
            if(lpDBRow[0] == NULL)
                continue;
			setFolders.emplace(atoui(lpDBRow[0]));
        }
    }

    // Query objectid -> parentid for messages
    if (setFolders.empty())
        // No objects found that we can lock, fail.
		return KCERR_NOT_FOUND;
	dtx = lpDatabase->Begin(dtxerr);
	if (dtxerr != erSuccess)
		return dtxerr;
    return LockFolders(lpDatabase, ulFlags & LOCK_SHARED, setFolders);
}

/**
 * Begin a new transaction and lock folders
 *
 * Sourcekey of folders should be passed in setFolders.
 *
 */
ECRESULT BeginLockFolders(ECDatabase *lpDatabase,
    const std::set<SOURCEKEY> &setFolders, unsigned int ulFlags,
    kd_trans &dtx, ECRESULT &dtxerr)
{
    std::set<std::string> setIds;

	std::transform(setFolders.cbegin(), setFolders.cend(), std::inserter(setIds, setIds.begin()),
		[](const auto &s) { return static_cast<std::string>(s); });
	return BeginLockFolders(lpDatabase, PROP_ID(PR_SOURCE_KEY), setIds,
	       ulFlags, dtx, dtxerr);
}

/**
 * Begin a new transaction and lock folders
 *
 * EntryID of messages and folders to lock can be passed in setEntryIds. In practice, only the folders
 * in which the messages reside are locked.
 */
ECRESULT BeginLockFolders(ECDatabase *lpDatabase,
    const std::set<EntryId> &setEntryIds, unsigned int ulFlags,
    kd_trans &dtx, ECRESULT &dtxerr)
{
    std::set<std::string> setIds;

    std::copy(setEntryIds.begin(), setEntryIds.end(), std::inserter(setIds, setIds.begin()));
	return BeginLockFolders(lpDatabase, PROP_ID(PR_ENTRYID), setIds,
	       ulFlags, dtx, dtxerr);
}

ECRESULT BeginLockFolders(ECDatabase *lpDatabase, const EntryId &entryid,
    unsigned int ulFlags, kd_trans &dtx, ECRESULT &dtxerr)
{
    std::set<EntryId> set;

    // No locking needed for stores
	try {
		if (entryid.type() == MAPI_STORE) {
			dtx = lpDatabase->Begin(dtxerr);
			return dtxerr;
		}
	} catch (const std::runtime_error &e) {
		ec_log_err("entryid.type(): %s", e.what());
		return KCERR_INVALID_PARAMETER;
	}
	set.emplace(entryid);
    return BeginLockFolders(lpDatabase, set, ulFlags, dtx, dtxerr);
}

ECRESULT BeginLockFolders(ECDatabase *lpDatabase, const SOURCEKEY &sourcekey,
    unsigned int ulFlags, kd_trans &dtx, ECRESULT &dtxerr)
{
	return BeginLockFolders(lpDatabase, std::set<SOURCEKEY>({sourcekey}),
	       ulFlags, dtx, dtxerr);
}

// Prepares child property data. This can be passed to ReadProps(). This allows the properties of child objects of object ulObjId to be
// retrieved with far less SQL queries, since this function bulk-receives the data. You may pass EITHER ulObjId OR ulParentId to retrieve an object itself, or
// children of an object.
ECRESULT PrepareReadProps(struct soap *soap, ECDatabase *lpDatabase,
    bool fDoQuery, unsigned int ulObjId, unsigned int ulParentId,
    unsigned int ulMaxSize, ChildPropsMap *lpChildProps,
    NamedPropDefMap *lpNamedPropDefs)
{
	unsigned int ulSize;
	struct propVal sPropVal;
	std::string strQuery;
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;

	if (ulObjId == 0 && ulParentId == 0)
		return KCERR_INVALID_PARAMETER;

    if(fDoQuery) {
		// although we don't always use the names columns, we need to join anyway to check for existing nameids
		// we may never stream propids > 0x8500 without the names data
		if (ulObjId != 0)
			strQuery = "SELECT " PROPCOLORDER ", hierarchyid, names.nameid, names.namestring, names.guid "
				"FROM properties ";
		else
			strQuery = "SELECT " PROPCOLORDER ", hierarchy.id, names.nameid, names.namestring, names.guid "
				"FROM properties JOIN hierarchy "
			        "ON properties.hierarchyid=hierarchy.id ";

		strQuery += "LEFT JOIN names ON properties.tag-34049=names.id ";
		if (ulObjId)
			strQuery += "WHERE hierarchyid=" + stringify(ulObjId);
		else
			strQuery += "WHERE hierarchy.parent=" + stringify(ulParentId);
		strQuery += " AND (tag <= 34048 OR names.id IS NOT NULL)";
		auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if(er != erSuccess)
			return er;
    } else {
		auto er = lpDatabase->GetNextResult(&lpDBResult);
        if(er != erSuccess)
			return er;
    }

	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		auto lpDBLen = lpDBResult.fetch_row_lengths();
        if(lpDBLen == NULL) {
		ec_log_crit("PrepareReadProps(): FetchRowLengths failed");
			return KCERR_DATABASE_ERROR; /* this should never happen */
        }

		auto ulPropTag = PROP_TAG(atoi(lpDBRow[FIELD_NR_TYPE]),atoi(lpDBRow[FIELD_NR_TAG]));
        if (PROP_ID(ulPropTag) > 0x8500 && lpNamedPropDefs) {
			auto resInsert = lpNamedPropDefs->emplace(ulPropTag, NAMEDPROPDEF());
            if (resInsert.second) {
                // New entry
                if (lpDBLen[FIELD_NR_NAMEGUID] != sizeof(resInsert.first->second.guid)) {
			ec_log_err("PrepareReadProps(): record size mismatch");
					return KCERR_DATABASE_ERROR;
                }
                memcpy(&resInsert.first->second.guid, lpDBRow[FIELD_NR_NAMEGUID], sizeof(resInsert.first->second.guid));

                if (lpDBRow[FIELD_NR_NAMEID] != NULL) {
                    resInsert.first->second.ulKind = MNID_ID;
                    resInsert.first->second.ulId = atoui((char*)lpDBRow[FIELD_NR_NAMEID]);
                } else if (lpDBRow[FIELD_NR_NAMESTR] != NULL) {
                    resInsert.first->second.ulKind = MNID_STRING;
                    resInsert.first->second.strName.assign((char*)lpDBRow[FIELD_NR_NAMESTR], lpDBLen[FIELD_NR_NAMESTR]);
                } else {
					return KCERR_INVALID_TYPE;
                }
            }
        }

		// server strings are always unicode, for unicode clients.
		if (PROP_TYPE(ulPropTag) == PT_STRING8)
			ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_UNICODE);
		else if (PROP_TYPE(ulPropTag) == PT_MV_STRING8)
			ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_MV_UNICODE);

		auto ulChildId = atoui(lpDBRow[FIELD_NR_MAX]);
		auto iterChild = lpChildProps->find(ulChildId);
		if (iterChild == lpChildProps->cend())
            // First property for this child
			iterChild = lpChildProps->emplace(ulChildId, CHILDPROPS(soap, 20)).first;
		auto er = iterChild->second.lpPropTags->AddPropTag(ulPropTag);
        if(er != erSuccess)
			return er;

        er = GetPropSize(lpDBRow, lpDBLen, &ulSize);
        if (er == erSuccess && (ulMaxSize == 0 || ulSize < ulMaxSize)) {
            // the size of this property is small enough to send in the initial loading sequence
            er = CopyDatabasePropValToSOAPPropVal(soap, lpDBRow, lpDBLen, &sPropVal);
            if(er != erSuccess)
                continue;
			er = FixPropEncoding(&sPropVal);
			if (er != erSuccess)
				continue;
            iterChild->second.lpPropVals->AddPropVal(sPropVal);
			if (!soap)
				FreePropVal(&sPropVal, false);
        }
    }

    if(fDoQuery) {
		if (ulObjId != 0)
			strQuery = "SELECT " MVPROPCOLORDER ", hierarchyid, names.nameid, names.namestring, names.guid "
				"FROM mvproperties ";
		else
			strQuery = "SELECT " MVPROPCOLORDER ", hierarchy.id, names.nameid, names.namestring, names.guid "
				"FROM mvproperties "
				"JOIN hierarchy "
				    "ON mvproperties.hierarchyid=hierarchy.id ";

		strQuery += "LEFT JOIN names ON mvproperties.tag-34049=names.id ";
        if (ulObjId != 0)
            strQuery +=	"WHERE hierarchyid=" + stringify(ulObjId) +
				" AND (tag <= 34048 OR names.id IS NOT NULL) "
				" GROUP BY hierarchyid, tag";
        else
			strQuery +=	"WHERE hierarchy.parent=" + stringify(ulParentId) +
				" AND (tag <= 34048 OR names.id IS NOT NULL) "
				"GROUP BY tag, mvproperties.type";

		auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if(er != erSuccess)
			return er;
    } else {
		auto er = lpDatabase->GetNextResult(&lpDBResult);
        if(er != erSuccess)
			return er;
    }

    // Do MV props
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		auto lpDBLen = lpDBResult.fetch_row_lengths();
        if(lpDBLen == NULL) {
			ec_log_crit("PrepareReadProps(): FetchRowLengths failed(2)");
			return KCERR_DATABASE_ERROR; /* this should never happen */
        }

        if (lpNamedPropDefs) {
            unsigned int ulPropTag = PROP_TAG(atoi(lpDBRow[FIELD_NR_TYPE]),atoi(lpDBRow[FIELD_NR_TAG]));
            if (PROP_ID(ulPropTag) > 0x8500) {
				auto resInsert = lpNamedPropDefs->emplace(ulPropTag, NAMEDPROPDEF());
                if (resInsert.second) {
                    // New entry
                    if (lpDBLen[FIELD_NR_NAMEGUID] != sizeof(resInsert.first->second.guid)) {
			ec_log_crit("PrepareReadProps(): record size mismatch(2)");
						return KCERR_DATABASE_ERROR;
                    }
                    memcpy(&resInsert.first->second.guid, lpDBRow[FIELD_NR_NAMEGUID], sizeof(resInsert.first->second.guid));

                    if (lpDBRow[FIELD_NR_NAMEID] != NULL) {
                        resInsert.first->second.ulKind = MNID_ID;
                        resInsert.first->second.ulId = atoui((char*)lpDBRow[FIELD_NR_NAMEID]);
                    } else if (lpDBRow[FIELD_NR_NAMESTR] != NULL) {
                        resInsert.first->second.ulKind = MNID_STRING;
                        resInsert.first->second.strName.assign((char*)lpDBRow[FIELD_NR_NAMESTR], lpDBLen[FIELD_NR_NAMESTR]);
                    } else {
						return KCERR_INVALID_TYPE;
                    }
                }
            }
        }

		auto ulChildId = atoui(lpDBRow[FIELD_NR_MAX]);
		auto iterChild = lpChildProps->find(ulChildId);
		if (iterChild == lpChildProps->cend())
            // First property for this child
			iterChild = lpChildProps->emplace(ulChildId, CHILDPROPS(soap, 20)).first;
		auto er = CopyDatabasePropValToSOAPPropVal(soap, lpDBRow, lpDBLen, &sPropVal);
        if(er != erSuccess)
            continue;
		er = FixPropEncoding(&sPropVal);
		if (er != erSuccess)
			continue;
        er = iterChild->second.lpPropTags->AddPropTag(sPropVal.ulPropTag);
        if(er != erSuccess)
            continue;
        iterChild->second.lpPropVals->AddPropVal(sPropVal);
		if (!soap)
			FreePropVal(&sPropVal, false);
    }
	return erSuccess;
}

CHILDPROPS::CHILDPROPS(struct soap *soap, unsigned int hint) :
	lpPropTags(new DynamicPropTagArray(soap)),
	lpPropVals(new DynamicPropValArray(soap, hint))
{}

ECRESULT FixPropEncoding(struct propVal *lpProp)
{
	if (PROP_TYPE(lpProp->ulPropTag) == PT_STRING8 || PROP_TYPE(lpProp->ulPropTag) == PT_UNICODE)
		lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, PT_UNICODE);
	else if (PROP_TYPE(lpProp->ulPropTag) == PT_MV_STRING8 || PROP_TYPE(lpProp->ulPropTag) == PT_MV_UNICODE)
		lpProp->ulPropTag = CHANGE_PROP_TYPE(lpProp->ulPropTag, PT_MV_UNICODE);
	return erSuccess;
}

} /* namespace */
