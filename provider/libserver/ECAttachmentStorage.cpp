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

#include <kopano/platform.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <climits>
#include <mapidefs.h>
#include <cerrno>

#include <algorithm>
#include <kopano/lockhelper.hpp>
#include <fcntl.h>

#include <zlib.h>

#include <ECSerializer.h>

#include <sys/types.h>
#include <sys/stat.h>
	#include <unistd.h>

#include "ECAttachmentStorage.h"
#include "SOAPUtils.h"
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <mapitags.h>
#include <kopano/stringutil.h>
#include "StreamUtil.h"
#include "ECS3Attachment.h"

namespace KC {

using std::string;

// chunk size for attachment blobs, must be equal or larger than MAX, MAX may never shrink below 384*1024.
#define CHUNK_SIZE (384 * 1024)

// as adviced by http://www.zlib.net/manual.html we use a 128KB buffer; default is only 8KB
#define ZLIB_BUFFER_SIZE std::max(CHUNK_SIZE, 128 * 1024)

/*
 * Locking requirements of ECAttachmentStorage:
 * In the case of ECAttachmentStorage locking to protect against concurrent access is futile.
 * The theory is as follows:
 * If 2 users have a reference to the same attachment, neither can delete the mail causing
 * the other person to lose the attachment. This means that concurrent copy and delete actions
 * are possible on the same attachment. The only case where this does not hold is the case
 * when a user deletes the mail he is copying at the same time and that this is the last mail
 * which references that specific attachment.
 * The only race condition which might occur is that the dagent delivers a mail with attachment,
 * the server returns the attachment id back to the dagent, but the user for whom the message
 * was intended deletes the mail & attachment. In this case the dagent will no longer send the
 * attachment but only the attachment id. When that happens the server can return an error
 * and simply request the dagent to resend the attachment and to obtain a new attachment id.
 */

// Generic Attachment storage
ECAttachmentStorage::ECAttachmentStorage(ECDatabase *lpDatabase, unsigned int ulCompressionLevel)
	: m_lpDatabase(lpDatabase)
{
	m_ulRef = 0;
	m_bFileCompression = ulCompressionLevel != 0;

	if (ulCompressionLevel > Z_BEST_COMPRESSION)
		ulCompressionLevel = Z_BEST_COMPRESSION;

	m_CompressionLevel = stringify(ulCompressionLevel);
}

ULONG ECAttachmentStorage::AddRef() {
	return ++m_ulRef;
}

ULONG ECAttachmentStorage::Release() {
	ULONG ulRef = --m_ulRef;
	if (ulRef == 0)
		delete this;

	return ulRef;
}

/** 
 * Create an attachment storage object which either uses the
 * ECDatabase or a filesystem as storage.
 * 
 * @param[in] lpDatabase Database class that stays valid during the lifetime of the returned ECAttachmentStorage
 * @param[in] lpConfig The server configuration object
 * @param[out] lppAttachmentStorage The attachment storage object
 * 
 * @return Kopano error code
 * @retval KCERR_DATABASE_ERROR given database pointer wasn't valid
 */
ECRESULT ECAttachmentStorage::CreateAttachmentStorage(ECDatabase *lpDatabase,
    ECConfig *lpConfig, ECAttachmentStorage **lppAttachmentStorage)
{
	ECAttachmentStorage *lpAttachmentStorage = NULL;

	if (lpDatabase == NULL) {
		ec_log_err("ECAttachmentStorage::CreateAttachmentStorage(): DB not available yet");
		return KCERR_DATABASE_ERROR; // somebody called this function too soon.
	}

	const char *ans = lpConfig->GetSetting("attachment_storage");
	const char *dir = lpConfig->GetSetting("attachment_path");
	if (dir == NULL) {
		ec_log_err("No attachment_path set despite attachment_storage=files. Falling back to database attachments.");
		ans = NULL;
	}
	if (ans != NULL && strcmp(ans, "files") == 0) {
		const char *const sync_files_par = lpConfig->GetSetting("attachment_files_fsync");
		bool sync_files = sync_files_par == NULL || strcasecmp(sync_files_par, "yes") == 0;
		const char *comp = lpConfig->GetSetting("attachment_compression");
		unsigned int complvl = (comp == NULL) ? 0 : strtoul(comp, NULL, 0);

		lpAttachmentStorage = new ECFileAttachment(lpDatabase, dir, complvl, sync_files);
#ifdef HAVE_LIBS3_H
	} else if (ans != NULL && strcmp(ans, "s3") == 0) {
		try {
			lpAttachmentStorage = new ECS3Attachment(lpDatabase,
					lpConfig->GetSetting("attachment_s3_protocol"),
					lpConfig->GetSetting("attachment_s3_uristyle"),
					lpConfig->GetSetting("attachment_s3_accesskeyid"),
					lpConfig->GetSetting("attachment_s3_secretaccesskey"),
					lpConfig->GetSetting("attachment_s3_bucketname"),
					lpConfig->GetSetting("attachment_s3_region"),
					lpConfig->GetSetting("attachment_path"),
					strtol(lpConfig->GetSetting("attachment_compression"), NULL, 0));
		} catch (std::runtime_error &e) {
			ec_log_warn("Cannot instantiate ECS3Attachment: %s", e.what());
			return KCERR_DATABASE_ERROR;
		}
#endif
	} else {
		lpAttachmentStorage = new ECDatabaseAttachment(lpDatabase);
	}

	lpAttachmentStorage->AddRef();

	*lppAttachmentStorage = lpAttachmentStorage;

	return erSuccess;
}

/** 
 * Gets the instance id for a given hierarchy id and prop tag.
 * 
 * @param[in] ulObjId Id from the hierarchy table
 * @param[in] ulTag Proptag of the instance data
 * @param[out] lpulInstanceId Id to use as instanceid
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::GetSingleInstanceId(ULONG ulObjId, ULONG ulTag,
    ext_siid *esid)
{	
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	std::string strQuery =
		"SELECT `instanceid` "
		"FROM `singleinstances` "
		"WHERE `hierarchyid` = " + stringify(ulObjId) + " AND `tag` = " + stringify(ulTag) + " LIMIT 1";

	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::GetSingleInstanceId(): DoSelect() failed %x", er);
		return er;
	}

	lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == nullptr || lpDBRow[0] == nullptr)
		// ec_log_err("ECAttachmentStorage::GetSingleInstanceId(): FetchRow() failed %x", er);
		return KCERR_NOT_FOUND;
	if (esid != nullptr)
		esid->siid = atoi(lpDBRow[0]);
	return erSuccess;
}

/** 
 * Get all instance ids from a list of hierarchy ids, independent of
 * the proptag.
 * @todo this should be for a given tag, or we should return the tags too (map<InstanceID, ulPropId>)
 * 
 * @param[in] lstObjIds list of hierarchy ids
 * @param[out] lstAttachIds list of unique corresponding instance ids
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::GetSingleInstanceIds(const std::list<ULONG> &lstObjIds,
    std::list<ext_siid> *lstAttachIds)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	std::list<ext_siid> lstInstanceIds;

	/* No single instances were requested... */
	if (lstObjIds.empty())
		return erSuccess;
	std::string strQuery =
		"SELECT DISTINCT `instanceid` "
		"FROM `singleinstances` "
		"WHERE `hierarchyid` IN (";
	for (auto i = lstObjIds.cbegin(); i != lstObjIds.cend(); ++i) {
		if (i != lstObjIds.cbegin())
			strQuery += ",";
		strQuery += stringify(*i);
	}

	strQuery += ")";
	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;

	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL) {
			ec_log_err("ECAttachmentStorage::GetSingleInstanceIds(): column contains NULL");
			return KCERR_DATABASE_ERROR;
		}
		lstInstanceIds.emplace_back(atoi(lpDBRow[0]));
	}
	*lstAttachIds = std::move(lstInstanceIds);
	return erSuccess;
}

/** 
 * Get all HierarchyIDs for a given InstanceID.
 * 
 * @param[in] ulInstanceId InstanceID to get HierarchyIDs for
 * @param[out] lplstObjIds List of all HierarchyIDs which link to the single instance
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::GetSingleInstanceParents(ULONG ulInstanceId,
    std::list<ext_siid> *lplstObjIds)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	std::list<ext_siid> lstObjIds;
	std::string strQuery =
		"SELECT DISTINCT `hierarchyid` "
		"FROM `singleinstances` "
		"WHERE `instanceid` = " + stringify(ulInstanceId);
	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;

	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL) {
			ec_log_err("ECAttachmentStorage::GetSingleInstanceParents(): column contains NULL");
			return KCERR_DATABASE_ERROR;
		}
		lstObjIds.emplace_back(atoi(lpDBRow[0]));
	}
	*lplstObjIds = std::move(lstObjIds);
	return erSuccess;
}

/** 
 * Checks if there are no references to a given InstanceID anymore.
 * 
 * @param ulInstanceId InstanceID to check
 * @param bOrphan true if instance isn't referenced anymore
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::IsOrphanedSingleInstance(const ext_siid &ulInstanceId, bool *bOrphan)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	std::string strQuery =
		"SELECT `instanceid` "
		"FROM `singleinstances` "
		"WHERE `instanceid` = " + stringify(ulInstanceId.siid) + " "
		"LIMIT 1";
	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	/*
	 * No results: Single Instance ID has been cleared (refcount = 0)
	 */
	*bOrphan = (!lpDBRow || !lpDBRow[0]);
	return erSuccess;
}

/** 
 * Make a list of all orphaned instances for a list of given InstanceIDs.
 * 
 * @param[in] lstAttachments List of instance ids to check
 * @param[out] lplstOrphanedAttachments List of orphaned instance ids
 * 
 * @return 
 */
ECRESULT ECAttachmentStorage::GetOrphanedSingleInstances(const std::list<ext_siid> &lstInstanceIds,
    std::list<ext_siid> *lplstOrphanedInstanceIds)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	std::string strQuery =
		"SELECT DISTINCT `instanceid` "
		"FROM `singleinstances` "
		"WHERE `instanceid` IN ( ";
	for (auto i = lstInstanceIds.cbegin(); i != lstInstanceIds.cend(); ++i) {
		if (i != lstInstanceIds.cbegin())
			strQuery += ",";
		strQuery += stringify(i->siid);
	}
	strQuery +=	")";
	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::GetOrphanedSingleInstances(): DoSelect failed %x", er);
		return er;
	}

	/* First make a full copy of the list of Single Instance IDs */
	lplstOrphanedInstanceIds->assign(lstInstanceIds.begin(), lstInstanceIds.end());

	/*
	 * Now filter out any Single Instance IDs which were returned in the query,
	 * any results not returned by the query imply that the refcount is 0
	 */
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL) {
			ec_log_err("ECAttachmentStorage::GetOrphanedSingleInstances(): column contains NULL");
			return KCERR_DATABASE_ERROR;
		}
		lplstOrphanedInstanceIds->remove(ext_siid(atoui(lpDBRow[0])));
	}
	return erSuccess;
}

/** 
 * For a given hierarchy id, check if this has a valid instance id
 * 
 * @param[in] ulObjId hierarchy id to check instance for
 * @param[in] ulPropId property id to check instance for
 * 
 * @return instance present
 */
bool ECAttachmentStorage::ExistAttachment(ULONG ulObjId, ULONG ulPropId)
{
	ext_siid ulInstanceId;
	/*
	 * Convert object id into attachment id
	 */
	auto er = GetSingleInstanceId(ulObjId, ulPropId, &ulInstanceId);
	if (er != erSuccess)
		return false;
	return ExistAttachmentInstance(ulInstanceId.siid);
}

bool ECAttachmentStorage::ExistAttachmentInstance(ULONG ins_id)
{
	return ExistAttachmentInstance(ext_siid(ins_id));
}

/** 
 * Retrieve a large property from the storage, return data as blob.
 * 
 * @param[in] soap Use soap for allocations. Returned data can directly be used to return to the client.
 * @param[in] ulObjId HierarchyID to load property for
 * @param[in] ulPropId property id to load
 * @param[out] lpiSize size of the property
 * @param[out] lppData data of the property
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::LoadAttachment(struct soap *soap, ULONG ulObjId, ULONG ulPropId, size_t *lpiSize, unsigned char **lppData)
{
	ext_siid ulInstanceId;
	/*
	 * Convert object id into attachment id
	 */
	auto er = GetSingleInstanceId(ulObjId, ulPropId, &ulInstanceId);
	if (er != erSuccess)
		return er;
	return LoadAttachmentInstance(soap, ulInstanceId, lpiSize, lppData);
}

/** 
 * Retrieve a large property from the storage, return data in a serializer.
 * 
 * @param[in] ulObjId HierarchyID to load property for
 * @param[in] ulPropId property id to load
 * @param[out] lpiSize size of the property
 * @param[out] lpSink Write in this serializer
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::LoadAttachment(ULONG ulObjId, ULONG ulPropId, size_t *lpiSize, ECSerializer *lpSink)
{
	ext_siid ulInstanceId;
	/*
	 * Convert object id into attachment id
	 */
	auto er = GetSingleInstanceId(ulObjId, ulPropId, &ulInstanceId);
	if (er != erSuccess)
		return er;
	return LoadAttachmentInstance(ulInstanceId, lpiSize, lpSink);
}

/** 
 * Save a property of a specific object from a given blob, optionally remove previous data.
 * 
 * @param[in] ulObjId HierarchyID of object
 * @param[in] ulPropId PropertyID to save
 * @param[in] bDeleteOld Remove old data before saving the new
 * @param[in] iSize size of lpData
 * @param[in] lpData data of the property
 * @param[out] lpulInstanceId InstanceID for the data (optional)
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::SaveAttachment(ULONG ulObjId, ULONG ulPropId, bool bDeleteOld, size_t iSize, unsigned char *lpData, ULONG *lpulInstanceId)
{
	if (lpData == NULL)
		return KCERR_INVALID_PARAMETER;
	if (bDeleteOld) {
		/*
		 * Call DeleteAttachment to decrease the refcount
		 * and optionally delete the original attachment.
		 */
		auto er = DeleteAttachment(ulObjId, ulPropId, true);
		if (er != erSuccess)
			return er;
	}

	/*
	 * Create Attachment reference, detect new attachment id.
	 */
	std::string strQuery =
		"INSERT INTO `singleinstances` (`hierarchyid`, `tag`) VALUES"
		"(" + stringify(ulObjId) + ", " + stringify(ulPropId) + ")";
	ext_siid esid;
	auto er = m_lpDatabase->DoInsert(strQuery, &esid.siid);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::SaveAttachment(): DoInsert failed %x", er);
		return er;
	}
	er = SaveAttachmentInstance(esid, ulPropId, iSize, lpData);
	if (er != erSuccess)
		return er;
	if (lpulInstanceId)
		*lpulInstanceId = esid.siid;
	return erSuccess;
}

/** 
 * Save a property of a specific object from a serializer, optionally remove previous data.
 * 
 * @param[in] ulObjId HierarchyID of object
 * @param[in] ulPropId Property to save
 * @param[in] bDeleteOld Remove old data before saving the new
 * @param[in] iSize size in lpSource
 * @param[in] lpSource serializer to read data from
 * @param[out] lpulInstanceId InstanceID for the data (optional)
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::SaveAttachment(ULONG ulObjId, ULONG ulPropId, bool bDeleteOld, size_t iSize, ECSerializer *lpSource, ULONG *lpulInstanceId)
{
	if (bDeleteOld) {
		/*
		 * Call DeleteAttachment to decrease the refcount
		 * and optionally delete the original attachment.
		 */
		auto er = DeleteAttachment(ulObjId, ulPropId, true);
		if (er != erSuccess)
			return er;
	}

	/*
	 * Create Attachment reference, detect new attachment id.
	 */
	std::string strQuery =
		"INSERT INTO `singleinstances` (`hierarchyid`, `tag`) VALUES"
		"(" + stringify(ulObjId) + ", " + stringify(ulPropId) + ")";
	ext_siid esid;
	auto er = m_lpDatabase->DoInsert(strQuery, &esid.siid);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::SaveAttachment(): DoInsert failed %x", er);
		return er;
	}
	er = SaveAttachmentInstance(esid, ulPropId, iSize, lpSource);
	if (er != erSuccess)
		return er;
	if (lpulInstanceId)
		*lpulInstanceId = esid.siid;
	return erSuccess;
}

/** 
 * Save a property of an object with a given instance id, optionally remove previous data.
 * 
 * @param[in] ulObjId HierarchyID of object
 * @param[in] ulPropId Property of object
 * @param[in] bDeleteOld Remove old data before saving the new
 * @param[in] ulInstanceId Instance id to link
 * @param[out] lpulInstanceId Same number as in ulInstanceId
 * 
 * @return 
 */
ECRESULT ECAttachmentStorage::SaveAttachment(ULONG ulObjId, ULONG ulPropId, bool bDeleteOld, ULONG ulInstanceId, ULONG *lpulInstanceId)
{
	if (bDeleteOld) {
		/*
		 * Call DeleteAttachment to decrease the refcount
		 * and optionally delete the original attachment.
		 */
		ext_siid ulOldAttachId;
		if (GetSingleInstanceId(ulObjId, ulPropId, &ulOldAttachId) == erSuccess &&
		    ulOldAttachId.siid == ulInstanceId)
			// Nothing to do, we already have that instance ID
			return erSuccess;
		auto er = DeleteAttachment(ulObjId, ulPropId, true);
		if (er != erSuccess)
			return er;
	}

	/* Check if attachment reference exists, if not return error */
	if (!ExistAttachmentInstance(ulInstanceId))
		return KCERR_UNABLE_TO_COMPLETE;
	/* Create Attachment reference, use provided attachment id */
	auto strQuery =
		"REPLACE INTO `singleinstances` (`instanceid`, `hierarchyid`, `tag`) VALUES"
		"(" + stringify(ulInstanceId) + ", " + stringify(ulObjId) + ", " +  stringify(ulPropId) + ")";
	unsigned int ignore;
	auto er = m_lpDatabase->DoInsert(strQuery, &ignore);
	if (er != erSuccess)
		return er;
	/* InstanceId is equal to provided AttachId */
	*lpulInstanceId = ulInstanceId;
	return erSuccess;
}

/** 
 * Make a copy of attachment data for a given object.
 *
 * In reality, the data is not copied, but an extra singleinstance
 * entry is added for the new hierarchyid.
 * 
 * @param[in] ulObjId Source hierarchy id to instance data from
 * @param[in] ulNewObjId Additional hierarchy id which has the same data
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::CopyAttachment(ULONG ulObjId, ULONG ulNewObjId)
{
	/*
	 * Only update the reference count in the `singleinstances` table,
	 * no need to really physically store the attachment twice.
	 */
	std::string strQuery =
		"INSERT INTO `singleinstances` (`hierarchyid`, `instanceid`, `tag`) "
			"SELECT " + stringify(ulNewObjId) + ", `instanceid`, `tag` "
			"FROM `singleinstances` "
			"WHERE `hierarchyid` = " + stringify(ulObjId);
	auto er = m_lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		ec_log_err("ECAttachmentStorage::CopyAttachment(): DoInsert failed %x", er);
	return er;
}

/** 
 * Delete all properties of given list of hierarchy ids.
 * 
 * @param[in] lstDeleteObjects list of hierarchy ids to delete singleinstance data for
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::DeleteAttachments(const std::list<ULONG> &lstDeleteObjects)
{
	std::list<ext_siid> lstAttachments, lstDeleteAttach;

	/* Convert object ids into attachment ids */
	auto er = GetSingleInstanceIds(lstDeleteObjects, &lstAttachments);
	if (er != erSuccess)
		return er;
	/* No attachments present, we're done */
	if (lstAttachments.empty())
		return er;
	/*
	 * Remove all objects from `singleinstances` table this will decrease the
	 * reference count for each attachment.
	 */
	std::string strQuery =
		"DELETE FROM `singleinstances` "
		"WHERE `hierarchyid` IN (";
	for (auto i = lstDeleteObjects.cbegin(); i != lstDeleteObjects.cend(); ++i) {
		if (i != lstDeleteObjects.cbegin())
			strQuery += ",";
		strQuery += stringify(*i);
	}
	strQuery += ")";

	er = m_lpDatabase->DoDelete(strQuery);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::DeleteAttachments(): DoDelete failed %x", er);
		return er;
	}

	/*
	 * Get the list of orphaned Single Instance IDs which we can delete.
	 */
	er = GetOrphanedSingleInstances(lstAttachments, &lstDeleteAttach);
	if (er != erSuccess)
		return er;
	if (!lstDeleteAttach.empty()) {
		er = DeleteAttachmentInstances(lstDeleteAttach, false);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

/** 
 * Delete one single instance property of an object.
 * public interface version
 * 
 * @param[in] ulObjId HierarchyID of object to delete single instance property from
 * @param[in] ulPropId Property of object to remove
 * 
 * @return 
 */
ECRESULT ECAttachmentStorage::DeleteAttachment(ULONG ulObjId, ULONG ulPropId) {
	return DeleteAttachment(ulObjId, ulPropId, false);
}

/** 
 * Delete one single instance property of an object.
 * 
 * @param[in] ulObjId HierarchyID of object to delete single instance property from
 * @param[in] ulPropId Property of object to remove
 * @param[in] bReplace Flag used for transations in ECFileStorage
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::DeleteAttachment(ULONG ulObjId, ULONG ulPropId, bool bReplace)
{
	ext_siid ulInstanceId;
	bool bOrphan = false;

	/*
	 * Convert object id into attachment id
	 */
	auto er = GetSingleInstanceId(ulObjId, ulPropId, &ulInstanceId);
	if (er != erSuccess) {
		if (er == KCERR_NOT_FOUND)
			er = erSuccess;	// Nothing to delete
		return er;
	}

	/*
	 * Remove object from `singleinstances` table, this will decrease the
	 * reference count for the attachment.
	 */
	std::string strQuery =
		"DELETE FROM `singleinstances` "
		"WHERE `hierarchyid` = " + stringify(ulObjId) + " "
		"AND `tag` = " + stringify(ulPropId);

	er = m_lpDatabase->DoDelete(strQuery);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::DeleteAttachment(): DoDelete failed %x", er);
		return er;
	}

	/*
	 * Check if the attachment can be permanently deleted.
	 */
	if (IsOrphanedSingleInstance(ulInstanceId, &bOrphan) == erSuccess && bOrphan) {
		er = DeleteAttachmentInstance(ulInstanceId, bReplace);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

/** 
 * Get the size of a large property of a specific object
 * 
 * @param[in] ulObjId HierarchyID of object
 * @param[in] ulPropId PropertyID of object
 * @param[out] lpulSize size of property
 * 
 * @return Kopano error code
 */
ECRESULT ECAttachmentStorage::GetSize(ULONG ulObjId, ULONG ulPropId, size_t *lpulSize)
{
	ext_siid ulInstanceId;
	/*
	 * Convert object id into attachment id
	 */
	auto er = GetSingleInstanceId(ulObjId, ulPropId, &ulInstanceId);
	if (er == KCERR_NOT_FOUND) {
		*lpulSize = 0;
		return erSuccess;
	} else if (er != erSuccess) {
		return er;
	}
	return GetSizeInstance(ulInstanceId, lpulSize);
}

// Attachment storage is in database
ECDatabaseAttachment::ECDatabaseAttachment(ECDatabase *lpDatabase) :
	ECAttachmentStorage(lpDatabase, 0)
{
}

/** 
 * For a given instance id, check if this has a valid attachment data present
 * 
 * @param[in] ulInstanceId instance id to check validity
 * 
 * @return instance present
 */
bool ECDatabaseAttachment::ExistAttachmentInstance(const ext_siid &ulInstanceId)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	auto strQuery = "SELECT instanceid FROM lob WHERE instanceid = " + stringify(ulInstanceId.siid) + " LIMIT 1";
	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::ExistAttachmentInstance(): DoSelect failed %x", er);
		return false;
	}

	lpDBRow = lpDBResult.fetch_row();
	if (!lpDBRow || !lpDBRow[0])
		return false; /* KCERR_NOT_FOUND */
	return true;
}

/** 
 * Load instance data using soap and return as blob.
 * 
 * @param[in] soap soap to use memory allocations for
 * @param[in] ulInstanceId InstanceID to load
 * @param[out] lpiSize size in lppData
 * @param[out] lppData data of instance
 * 
 * @return Kopano error code
 */
ECRESULT ECDatabaseAttachment::LoadAttachmentInstance(struct soap *soap,
    const ext_siid &ulInstanceId, size_t *lpiSize, unsigned char **lppData)
{
	size_t iSize = 0;
	size_t iReadSize = 0;
	unsigned char *lpData = NULL;
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	DB_LENGTHS lpDBLen = NULL;

	// we first need to know the complete size of the attachment (some old databases don't have the correct chunk size)
	auto strQuery = "SELECT SUM(LENGTH(val_binary)) FROM lob WHERE instanceid = " + stringify(ulInstanceId.siid);
	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::LoadAttachmentInstance(): DoSelect failed %x", er);
		return er;
	}

	lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL || lpDBRow[0] == NULL) {
		ec_log_err("ECDatabaseAttachment::LoadAttachmentInstance(): no row returned");
		return KCERR_DATABASE_ERROR;
	}

	iSize = strtoul(lpDBRow[0], NULL, 0);

	// get all chunks
	strQuery = "SELECT val_binary FROM lob WHERE instanceid = " + stringify(ulInstanceId.siid) + " ORDER BY chunkid";
	er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::LoadAttachmentInstance(): DoSelect(2) failed %x", er);
		return er;
	}

	lpData = s_alloc<unsigned char>(soap, iSize);
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL) {
			// broken attachment!
			er = KCERR_DATABASE_ERROR;
			ec_log_err("ECDatabaseAttachment::LoadAttachmentInstance(): column contained NULL");
			goto exit;
		}

		lpDBLen = lpDBResult.fetch_row_lengths();
		memcpy(lpData + iReadSize, lpDBRow[0], lpDBLen[0]);
		iReadSize += lpDBLen[0];
	}

	*lpiSize = iReadSize;
	*lppData = lpData;

exit:
	if (er != erSuccess && !soap)
		delete [] lpData;
	return er;
}

/** 
 * Load instance data using a serializer.
 * 
 * @param[in] ulInstanceId InstanceID to load
 * @param[out] lpiSize size written in in lpSink
 * @param[in] lpSink serializer to write in
 * 
 * @return 
 */
ECRESULT ECDatabaseAttachment::LoadAttachmentInstance(const ext_siid &ulInstanceId,
    size_t *lpiSize, ECSerializer *lpSink)
{
	size_t iReadSize = 0;
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	DB_LENGTHS lpDBLen = NULL;

	// get all chunks
	auto strQuery = "SELECT val_binary FROM lob WHERE instanceid = " + stringify(ulInstanceId.siid) + " ORDER BY chunkid";
	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("ECAttachmentStorage::LoadAttachmentInstance(): DoSelect failed %x", er);
		return er;
	}

	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL) {
			// broken attachment !
			ec_log_err("ECDatabaseAttachment::LoadAttachmentInstance(): column contained NULL");
			return KCERR_DATABASE_ERROR;
		}
		lpDBLen = lpDBResult.fetch_row_lengths();
		er = lpSink->Write(lpDBRow[0], 1, lpDBLen[0]);
		if (er != erSuccess) {
			ec_log_err("ECAttachmentStorage::LoadAttachmentInstance(): Write failed %x", er);
			return er;
		}

		iReadSize += lpDBLen[0];
	}

	*lpiSize = iReadSize;
	return erSuccess;
}

/** 
 * Save a property in a new instance from a blob
 *
 * @note Property id here is actually useless, but legacy requires
 * this. Removing the `tag` column from the database would require a
 * database update on the lob table, which would make database
 * attachment users very unhappy.
 * 
 * @param[in] ulInstanceId InstanceID to save data under
 * @param[in] ulPropId PropertyID to save
 * @param[in] iSize size of lpData
 * @param[in] lpData Data of property
 * 
 * @return Kopano error code
 */
ECRESULT ECDatabaseAttachment::SaveAttachmentInstance(const ext_siid &ulInstanceId,
    ULONG ulPropId, size_t iSize, unsigned char *lpData)
{
	std::string strQuery;

	// make chunks of 393216 bytes (384*1024)
	size_t iSizeLeft = iSize;
	size_t iPtr = 0;
	size_t ulChunk = 0;

	do {
		size_t iChunkSize = iSizeLeft < CHUNK_SIZE ? iSizeLeft : CHUNK_SIZE;
		std::string strQuery = "INSERT INTO lob (instanceid, chunkid, tag, val_binary) VALUES (" +
			stringify(ulInstanceId.siid) + ", " + stringify(ulChunk) + ", " + stringify(ulPropId) +
			", " + m_lpDatabase->EscapeBinary(lpData + iPtr, iChunkSize) + ")";

		auto er = m_lpDatabase->DoInsert(strQuery);
		if (er != erSuccess) {
			ec_log_err("ECAttachmentStorage::SaveAttachmentInstance(): DoInsert failed %x", er);
			return er;
		}
		++ulChunk;
		iSizeLeft -= iChunkSize;
		iPtr += iChunkSize;
	} while (iSizeLeft > 0);

	return erSuccess;
}

/** 
 * Save a property in a new instance from a serializer
 *
 * @note Property id here is actually useless, but legacy requires
 * this. Removing the `tag` column from the database would require a
 * database update on the lob table, which would make database
 * attachment users very unhappy.
 * 
 * @param[in] ulInstanceId InstanceID to save data under
 * @param[in] ulPropId PropertyID to save
 * @param[in] iSize size in lpSource
 * @param[in] lpSource serializer to read data from
 * 
 * @return Kopano error code
 */
ECRESULT ECDatabaseAttachment::SaveAttachmentInstance(const ext_siid &ulInstanceId,
    ULONG ulPropId, size_t iSize, ECSerializer *lpSource)
{
	unsigned char szBuffer[CHUNK_SIZE] = {0};

	// make chunks of 393216 bytes (384*1024)
	size_t iSizeLeft = iSize;
	size_t ulChunk = 0;

	while (iSizeLeft > 0) {
		size_t iChunkSize = iSizeLeft < CHUNK_SIZE ? iSizeLeft : CHUNK_SIZE;
		auto er = lpSource->Read(szBuffer, 1, iChunkSize);
		if (er != erSuccess)
			return er;

		std::string strQuery = "INSERT INTO lob (instanceid, chunkid, tag, val_binary) VALUES (" +
			stringify(ulInstanceId.siid) + ", " + stringify(ulChunk) + ", " + stringify(ulPropId) +
			", " + m_lpDatabase->EscapeBinary(szBuffer, iChunkSize) + ")";

		er = m_lpDatabase->DoInsert(strQuery);
		if (er != erSuccess) {
			ec_log_err("ECAttachmentStorage::SaveAttachmentInstance(): DoInsert failed %x", er);
			return er;
		}
		++ulChunk;
		iSizeLeft -= iChunkSize;
	}
	return erSuccess;
}

/** 
 * Delete given instances from the database
 * 
 * @param[in] lstDeleteInstances List of instance ids to remove from the database
 * @param[in] bReplace unused, see ECFileAttachment
 * 
 * @return Kopano error code
 */
ECRESULT ECDatabaseAttachment::DeleteAttachmentInstances(const std::list<ext_siid> &lstDeleteInstances, bool bReplace)
{
	std::string strQuery = (std::string)"DELETE FROM lob WHERE instanceid IN (";
	for (auto iterDel = lstDeleteInstances.cbegin();
	     iterDel != lstDeleteInstances.cend(); ++iterDel) {
		if (iterDel != lstDeleteInstances.cbegin())
			strQuery += ",";
		strQuery += stringify(iterDel->siid);
	}

	strQuery += ")";
	return m_lpDatabase->DoDelete(strQuery);
}

/** 
 * Delete a single instanceid from the database
 * 
 * @param[in] ulInstanceId instance id to remove
 * @param[in] bReplace unused, see ECFileAttachment
 * 
 * @return 
 */
ECRESULT ECDatabaseAttachment::DeleteAttachmentInstance(const ext_siid &ulInstanceId, bool bReplace)
{
	std::string strQuery = "DELETE FROM lob WHERE instanceid=" + stringify(ulInstanceId.siid);
	return m_lpDatabase->DoDelete(strQuery);
}

/** 
 * Return the size of an instance
 * 
 * @param[in] ulInstanceId InstanceID to check the size for
 * @param[out] lpulSize Size of the instance
 * @param[out] lpbCompressed unused, see ECFileAttachment
 * 
 * @return Kopano error code
 */
ECRESULT ECDatabaseAttachment::GetSizeInstance(const ext_siid &ulInstanceId,
    size_t *lpulSize, bool *lpbCompressed)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL; 
	auto strQuery = "SELECT SUM(LENGTH(val_binary)) FROM lob WHERE instanceid = " + stringify(ulInstanceId.siid);
	auto er = m_lpDatabase->DoSelect(strQuery, &lpDBResult); 
	if (er != erSuccess)  {
		ec_log_err("ECAttachmentStorage::GetSizeInstance(): DoSelect failed %x", er);
		return er;
	}

	lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL || lpDBRow[0] == NULL) { 
		ec_log_err("ECDatabaseAttachment::GetSizeInstance(): now row or column contained NULL");
		return KCERR_DATABASE_ERROR;
	} 
 	 
	*lpulSize = strtoul(lpDBRow[0], NULL, 0);
	if (lpbCompressed)
		*lpbCompressed = false;
	return erSuccess;
}

kd_trans ECDatabaseAttachment::Begin(ECRESULT &res)
{
	return kd_trans(*this, res);
}

ECRESULT ECDatabaseAttachment::Commit()
{
	return erSuccess;
}

ECRESULT ECDatabaseAttachment::Rollback()
{
	return erSuccess;
}

// Attachment storage is in separate files
ECFileAttachment::ECFileAttachment(ECDatabase *lpDatabase,
    const std::string &basepath, unsigned int ulCompressionLevel,
    const bool force_changes_to_disk) :
	ECAttachmentStorage(lpDatabase, ulCompressionLevel),
	m_basepath(basepath)
{
	if (m_basepath.empty())
		m_basepath = "/var/lib/kopano";

	this -> force_changes_to_disk = force_changes_to_disk;
	if (force_changes_to_disk) {
		m_dirp = opendir(m_basepath.c_str());

		if (m_dirp)
			m_dirFd = dirfd(m_dirp);

		if (m_dirFd == -1)
			ec_log_warn("Problem opening directory file \"%s\": %s - attachment storage atomicity not guaranteed", m_basepath.c_str(), strerror(errno));
	}
	attachment_size_safety_limit = 512 * 1024 * 1024; // FIXME make configurable
}

ECFileAttachment::~ECFileAttachment()
{
	if (m_dirp != NULL)
		closedir(m_dirp);
	if (m_bTransaction)
		assert(false);
}

/** 
 * For a given instance id, check if this has a valid attachment data present
 * 
 * @param[in] ulInstanceId instance id to check validity
 * 
 * @return instance present
 */
bool ECFileAttachment::ExistAttachmentInstance(const ext_siid &ulInstanceId)
{
	auto filename = CreateAttachmentFilename(ulInstanceId, m_bFileCompression);
	struct stat st;
	if (stat(filename.c_str(), &st) == -1) {
		filename = CreateAttachmentFilename(ulInstanceId, !m_bFileCompression);
		if (stat(filename.c_str(), &st) == -1)
			return false;
	}
	return true;
}

/**
 * @uclen:	number of uncompressed bytes that is requested
 */
static ssize_t gzread_retry(gzFile fp, void *data, size_t uclen)
{
	ssize_t read_total = 0;
	auto buf = static_cast<char *>(data);

	if (uclen == 0)
		/* Avoid useless churn. */
		return 0;

	while (uclen > 0) {
		int chunk_size = (uclen > INT_MAX) ? INT_MAX : uclen;
		int ret = gzread(fp, buf, chunk_size);

		/*
		 * Save @errno now, since gzerror() code looks prone to
		 * reset it.
		 */
		int saved_errno = errno;
		int zerror;
		const char *zerrstr = gzerror(fp, &zerror);

		if (ret < 0 && zerror == Z_ERRNO &&
		    (saved_errno == EINTR || saved_errno == EAGAIN))
			/* Server delay (cf. gzread_write) */
			continue;
		if (ret < 0 && zerror == Z_ERRNO) {
			ec_log_err("gzread: %s (%d): %s",
			         zerrstr, zerror, strerror(saved_errno));
			return ret;
		}
		if (ret < 0) {
			ec_log_err("gzread: %s (%d)", zerrstr, zerror);
			return ret;
		}
		if (ret == 0)
			/*
			 * EOF (since we already excluded chunk_size==0
			 * requests).
			 */
			break;
		buf += ret;
		read_total += ret;
		uclen -= ret;
	}
	return read_total;
}

static ssize_t gzwrite_retry(gzFile fp, const void *data, size_t uclen)
{
	size_t wrote_total = 0;
	auto buf = static_cast<const char *>(data);

	if (uclen == 0)
		/* Avoid useless churn. */
		return 0;

	while (uclen > 0) {
		int chunk_size = (uclen > INT_MAX) ? INT_MAX : uclen;
		int ret = gzwrite(fp, buf, chunk_size);
		int saved_errno = errno;
		int zerror;
		const char *zerrstr = gzerror(fp, &zerror);

		if (ret < 0 && zerror == Z_ERRNO &&
		    (saved_errno == EINTR || saved_errno == EAGAIN))
			/*
			 * Choosing to continue reading can delay server
			 * shutdown... but we are in a soap handler, so
			 * whatelse could we do.
			 */
			continue;
		if (ret < 0 && zerror == Z_ERRNO) {
			ec_log_err("gzwrite: %s (%d): %s",
			         zerrstr, zerror, strerror(saved_errno));
			return ret;
		}
		if (ret < 0) {
			ec_log_err("gzwrite: %s (%d)", zerrstr, zerror);
			return ret;
		}
		if (ret == 0)
			/* ??? - could happen if the file is not open for write */
			break;
		buf += ret;
		wrote_total += ret;
		uclen -= ret;
	}
	return wrote_total;
}

bool ECFileAttachment::VerifyInstanceSize(const ext_siid &instanceId,
    const size_t expectedSize, const std::string &filename)
{
	bool bCompressed = false;
	size_t ulSize = 0;
	if (GetSizeInstance(instanceId, &ulSize, &bCompressed) != erSuccess) {
		ec_log_debug("ECFileAttachment::VerifyInstanceSize(): Failed verifying size of \"%s\"", filename.c_str());
		return false;
	}

	if (ulSize != expectedSize) {
		ec_log_debug("ECFileAttachment::VerifyInstanceSize(): Uncompressed size unexpected for \"%s\": expected %zu, got %zu.", filename.c_str(), expectedSize, ulSize);
		return false;
	}

	if (ulSize > attachment_size_safety_limit)
		ec_log_debug("ECFileAttachment::VerifyInstanceSize(): Overly large file (%zu/%zu): \"%s\"", expectedSize, ulSize, filename.c_str());
	return true;
}

/** 
 * Load instance data using soap and return as blob.
 * 
 * @param[in] soap soap to use memory allocations for
 * @param[in] ulInstanceId InstanceID to load
 * @param[out] lpiSize size in lppData
 * @param[out] lppData data of instance
 * 
 * @return Kopano error code
 */
ECRESULT ECFileAttachment::LoadAttachmentInstance(struct soap *soap,
    const ext_siid &ulInstanceId, size_t *lpiSize, unsigned char **lppData)
{
	ECRESULT er = erSuccess;
	string filename;
	unsigned char *lpData = NULL;
	bool bCompressed = false;
	gzFile gzfp = NULL;

	*lpiSize = 0;
	filename = CreateAttachmentFilename(ulInstanceId, bCompressed);
	int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0 && errno != ENOENT) {
		/* Access problems */
		ec_log_err("K-1561: cannot open attachment \"%s\": %s", filename.c_str(), strerror(errno));
		return KCERR_NO_ACCESS;
	} else if (fd < 0) {
		/* Not found, try gzip */
		bCompressed = true;
		filename = CreateAttachmentFilename(ulInstanceId, bCompressed);
		fd = open(filename.c_str(), O_RDONLY);
		if (fd < 0) {
			ec_log_err("K-1562: cannot open attachment \"%s\": %s", filename.c_str(), strerror(errno));
			return KCERR_NOT_FOUND;
		}
	}
	my_readahead(fd);

	/*
	 * CreateAttachmentFilename Already checked if we are working with a compressed or uncompressed file,
	 * no need to perform retries when our first guess (which is based on CreateAttachmentFilename) fails.
	 */
	if (bCompressed) {
		unsigned char *temp = NULL;

		/* Compressed attachment */
		gzfp = gzdopen(fd, "rb");
		if (!gzfp) {
			// do not use KCERR_NOT_FOUND: the file is already open so it exists
			// so something else is going wrong here
			ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): cannot gzopen attachment \"%s\": %s", filename.c_str(), strerror(errno));
			er = KCERR_UNKNOWN;
			goto exit;
		}

#if ZLIB_VERNUM >= 0x1240
		if (gzbuffer(gzfp, ZLIB_BUFFER_SIZE) == -1)
			ec_log_warn("gzbuffer failed");
#endif

		size_t memory_block_size = 0;

		for(;;)
		{
			int ret = -1;

			if (memory_block_size == *lpiSize) {
				if (memory_block_size)
					memory_block_size *= 2;
				else
					memory_block_size = CHUNK_SIZE;

				auto new_temp = static_cast<unsigned char *>(realloc(temp, memory_block_size));
				if (!new_temp) {
					// first free memory or the logging may fail too
					free(temp);
					temp = NULL;
					*lpiSize = 0;

					ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): Out of memory while reading \"%s\"", filename.c_str());
					er = KCERR_UNABLE_TO_COMPLETE;
					goto exit;
				}

				temp = new_temp;
			}

			ret = gzread_retry(gzfp, &temp[*lpiSize], memory_block_size - *lpiSize);

			if (ret < 0) {
				ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): Error while gzreading attachment data from \"%s\"", filename.c_str());
				// er = KCERR_DATABASE_ERROR;
				//break;
				*lpiSize = 0;
				break;
			}

			if (ret == 0)
				break;

			*lpiSize += ret;

			if (*lpiSize >= attachment_size_safety_limit) {
				ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): Size safety limit (%zu) reached for \"%s\" (compressed)",
					attachment_size_safety_limit, filename.c_str());
				// er = KCERR_DATABASE_ERROR;
				//break;
				*lpiSize = 0;
				break;
			}
		}

		if (er == erSuccess)
			VerifyInstanceSize(ulInstanceId, *lpiSize, filename);
		if (er == erSuccess) {
			lpData = s_alloc<unsigned char>(soap, *lpiSize);

			memcpy(lpData, temp, *lpiSize);
		}

		free(temp);
	}
	else {
		ssize_t lReadSize = 0;

		struct stat st;
		if (fstat(fd, &st) == -1)
		{
			ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): Error while doing fstat on \"%s\": %s", filename.c_str(), strerror(errno));
			// FIXME er = KCERR_DATABASE_ERROR;
			*lpiSize = 0;
			lpData = s_alloc<unsigned char>(soap, *lpiSize);
			*lppData = lpData;
			goto exit;
		}

		*lpiSize = st.st_size;

		if (*lpiSize >= attachment_size_safety_limit) {
			ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): Size safety limit (%zu) reached for \"%s\" (uncompressed)",
				attachment_size_safety_limit, filename.c_str());
			// FIXME er = KCERR_DATABASE_ERROR;
			*lpiSize = 0;
			lpData = s_alloc<unsigned char>(soap, *lpiSize);
			*lppData = lpData;
			goto exit;
		}

		lpData = s_alloc<unsigned char>(soap, *lpiSize);

		/* Uncompressed attachment */
		lReadSize = read_retry(fd, lpData, *lpiSize);
		if (lReadSize < 0) {
			ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): Error while reading attachment data from \"%s\": %s", filename.c_str(), strerror(errno));
			// FIXME er = KCERR_DATABASE_ERROR;
			*lpiSize = 0;
			*lppData = lpData;
			goto exit;
		}

		if (lReadSize != static_cast<ssize_t>(*lpiSize)) {
			ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): Short read while reading attachment data from \"%s\": expected %zu, got %zd.",
				filename.c_str(), *lpiSize, lReadSize);
			// FIXME er = KCERR_DATABASE_ERROR;
			*lpiSize = 0;
			*lppData = lpData;
			goto exit;
		}
	}

	*lppData = lpData;

exit:
	if (er != erSuccess)
		delete [] lpData;

	if (gzfp)
	{
		gzclose(gzfp);
		fd = -1;
	}

	if (fd != -1)
		close(fd);

	return er;
}

/** 
 * Load instance data using a serializer.
 * 
 * @param[in] ulInstanceId InstanceID to load
 * @param[out] lpiSize size written in in lpSink
 * @param[in] lpSink serializer to write in
 * 
 * @return 
 */
ECRESULT ECFileAttachment::LoadAttachmentInstance(const ext_siid &ulInstanceId,
    size_t *lpiSize, ECSerializer *lpSink)
{
	ECRESULT er = erSuccess;
	bool bCompressed = false;
	char buffer[CHUNK_SIZE];
	int fd = -1;
	gzFile gzfp = NULL;

	*lpiSize = 0;
	auto filename = CreateAttachmentFilename(ulInstanceId, bCompressed);
	fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0 && errno != ENOENT) {
		/* Access problems */
		ec_log_err("K-1563: cannot open attachment \"%s\": %s", filename.c_str(), strerror(errno));
		return KCERR_NO_ACCESS;
	} else if (fd < 0) {
		/* Not found, try gzip */
		bCompressed = true;
		filename = CreateAttachmentFilename(ulInstanceId, bCompressed);
		fd = open(filename.c_str(), O_RDONLY);
		if (fd < 0) {
			ec_log_err("K-1564: cannot open attachment \"%s\": %s", filename.c_str(), strerror(errno));
			return KCERR_NOT_FOUND;
		}
	}
	my_readahead(fd);

	if (bCompressed) {
		/* Compressed attachment */
		gzfp = gzdopen(fd, "rb");
		if (!gzfp) {
			er = KCERR_UNKNOWN;
			goto exit;
		}

#if ZLIB_VERNUM >= 0x1240
		if (gzbuffer(gzfp, ZLIB_BUFFER_SIZE) == -1)
			ec_log_warn("gzbuffer failed");
#endif

		for(;;) {
			ssize_t lReadNow = gzread_retry(gzfp, buffer, sizeof(buffer));
			if (lReadNow < 0) {
				ec_log_err("ECFileAttachment::LoadAttachmentInstance(): Error while gzreading attachment data from \"%s\".", filename.c_str());
				er = KCERR_DATABASE_ERROR;
				goto exit;
			}

			if (lReadNow == 0)
				break;

			lpSink->Write(buffer, 1, lReadNow);

			*lpiSize += lReadNow;
		}

		if (er == erSuccess)
			VerifyInstanceSize(ulInstanceId, *lpiSize, filename);
	}
	else {
		for(;;) {
			ssize_t lReadNow = read_retry(fd, buffer, sizeof(buffer));
			if (lReadNow < 0) {
				ec_log_err("ECFileAttachment::LoadAttachmentInstance(SOAP): Error while reading attachment data from \"%s\": %s", filename.c_str(), strerror(errno));
				er = KCERR_DATABASE_ERROR;
				goto exit;
			}

			if (lReadNow == 0)
				break;

			lpSink->Write(buffer, 1, lReadNow);

			*lpiSize += lReadNow;
		}
	}

exit:
	if (gzfp) {
		gzclose(gzfp);
		fd = -1;
	}

	if (fd != -1)
		close(fd);

	return er;
}

static bool EvaluateCompressibleness(const uint8_t *const lpData, const size_t iSize) {
	// If a file is smallar than the (usual) blocksize of the filesystem
	// then don't bother compressing it; it will give no gain as the
	// whole block will be read anyway when it is retrieved from disk.
	// In theory we could still try to compress it to see if the result
	// is smaller than 60: if a file is 60 bytes or less in size, then
	// it can be stored into the inode itself (at least for the ext4
	// filesystem). But note: gzip has a metadata overhead of 18 bytes
	// (see http://en.wikipedia.org/wiki/Gzip#File_format ) so a file
	// should be compressible to at most 42 bytes.
	if (iSize <= 4096)
		return false;

	// ZIP (also most OpenOffice documents; those are multiple files in a ZIP-file)
	if (lpData[0] == 'P' && lpData[1] == 'K' && lpData[2] == 0x03 && lpData[3] == 0x04)
		return false;

	// JPEG
	if (lpData[0] == 0xff && lpData[1] == 0xd8)
		return false;

	// GIF
	if (lpData[0] == 'G' && lpData[1] == 'I' && lpData[2] == 'F' && lpData[3] == '8')
		return false;

	// PNG
	if (lpData[0] == 0x89 && lpData[1] == 0x50 && lpData[2] == 0x4e && lpData[3] == 0x47 && lpData[4] == 0x0d && lpData[5] == 0x0a && lpData[6] == 0x1a && lpData[7] == 0x0a)
		return false;

	// RAR
	if (lpData[0] == 0x52 && lpData[1] == 0x61 && lpData[2] == 0x72 && lpData[3] == 0x21 && lpData[4] == 0x1A && lpData[5] == 0x07)
		return false;

	// GZIP
	if (lpData[0] == 0x1f && lpData[1] == 0x8b)
		return false;

	// XZ
	if (lpData[0] == 0xfd && lpData[1] == '7' && lpData[2] == 'z' && lpData[3] == 'X' && lpData[4] == 'Z' && lpData[5] == 0x00)
		return false;

	// BZ (.bz2 et al)
	if (lpData[0] == 0x42 && lpData[1] == 0x5A && lpData[2] == 0x68)
		return false;

	// MP3
	if ((lpData[0] == 0x49 && lpData[1] == 0x44 && lpData[2] == 0x33) || (lpData[0] == 0xff && lpData[1] == 0xfb))
		return false;

	// FIXME what to do with PDF? ('%PDF')

	return true;
}

/** 
 * Save a property in a new instance from a blob
 *
 * @param[in] ulInstanceId InstanceID to save data under
 * @param[in] ulPropId unused, required by interface, see ECDatabaseAttachment
 * @param[in] iSize size of lpData
 * @param[in] lpData Data of property
 * 
 * @return Kopano error code
 */
ECRESULT ECFileAttachment::SaveAttachmentInstance(const ext_siid &ulInstanceId,
    ULONG ulPropId, size_t iSize, unsigned char *lpData)
{
	bool compressible = EvaluateCompressibleness(lpData, iSize);

	bool compressAttachment = compressible ? m_bFileCompression && iSize : false;

	ECRESULT er = erSuccess;
	auto filename = CreateAttachmentFilename(ulInstanceId, compressAttachment);
	gzFile gzfp = NULL;
	int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR | S_IRGRP);
	if (fd < 0) {
		ec_log_err("Unable to open attachment \"%s\" for writing: %s", filename.c_str(), strerror(errno));
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}

	// no need to remove the file, just overwrite it
	if (compressAttachment) {
		gzfp = gzdopen(fd, std::string("wb" + m_CompressionLevel).c_str());
		if (!gzfp) {
			ec_log_err("Unable to gzopen attachment \"%s\" for writing: %s", filename.c_str(), strerror(errno));
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}

		ssize_t iWritten = gzwrite_retry(gzfp, lpData, iSize);
		if (iWritten != static_cast<ssize_t>(iSize)) {
			ec_log_err("Unable to gzwrite %zu bytes to attachment \"%s\", returned %zd.",
				iSize, filename.c_str(), iWritten);
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}
	}
	else {
		give_filesize_hint(fd, iSize);

		ssize_t iWritten = write_retry(fd, lpData, iSize);
		if (iWritten != static_cast<ssize_t>(iSize)) {
			ec_log_err("Unable to write %zu bytes to attachment \"%s\": %s. Returned %zu.",
				iSize, filename.c_str(), strerror(errno), iWritten);
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}
	}

	// set in transaction before disk full check to remove empty file
	if(m_bTransaction)
		m_setNewAttachment.emplace(ulInstanceId);
exit:
	if (gzfp != NULL) {
		int ret = gzclose(gzfp);
		if (ret != Z_OK) {
			ec_log_err("gzclose on attachment \"%s\" failed: %s",
				filename.c_str(), (ret == Z_ERRNO) ? strerror(errno) : gzerror(gzfp, NULL));
			er = KCERR_DATABASE_ERROR;
		}
		fd = -1;
	}
	if (fd != -1)
		close(fd);
	return er;
}

/** 
 * Save a property in a new instance from a serializer
 * 
 * @param[in] ulInstanceId InstanceID to save data under
 * @param[in] ulPropId unused, required by interface, see ECDatabaseAttachment
 * @param[in] iSize size in lpSource
 * @param[in] lpSource serializer to read data from
 * 
 * @return Kopano error code
 */
ECRESULT ECFileAttachment::SaveAttachmentInstance(const ext_siid &ulInstanceId,
    ULONG ulPropId, size_t iSize, ECSerializer *lpSource)
{
	ECRESULT er = erSuccess;
	auto filename = CreateAttachmentFilename(ulInstanceId, m_bFileCompression);
	unsigned char szBuffer[CHUNK_SIZE];
	size_t iSizeLeft = iSize;

	int fd = open(filename.c_str(), O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
	if (fd == -1) {
		ec_log_err("Unable to open attachment \"%s\" for writing: %s.",
			filename.c_str(), strerror(errno));
		return KCERR_DATABASE_ERROR;
	}

	//no need to remove the file, just overwrite it
	if (m_bFileCompression) {
		gzFile gzfp = gzdopen(fd, std::string("wb" + m_CompressionLevel).c_str());
		if (!gzfp) {
			ec_log_err("Unable to gzdopen attachment \"%s\" for writing: %s",
				filename.c_str(), strerror(errno));
			er = KCERR_DATABASE_ERROR;
			goto exit;
		}

		// file created on disk, now in transaction
		if (m_bTransaction)
			m_setNewAttachment.emplace(ulInstanceId);

		while (iSizeLeft > 0) {
			size_t iChunkSize = iSizeLeft < CHUNK_SIZE ? iSizeLeft : CHUNK_SIZE;

			er = lpSource->Read(szBuffer, 1, iChunkSize);
			if (er != erSuccess) {
				ec_log_err("Problem retrieving attachment from ECSource: %s (0x%x)", GetMAPIErrorMessage(kcerr_to_mapierr(er, ~0U /* anything yielding UNKNOWN */)), er);
				er = KCERR_DATABASE_ERROR;
				break;
			}

			ssize_t iWritten = gzwrite_retry(gzfp, szBuffer, iChunkSize);
			if (iWritten != static_cast<ssize_t>(iChunkSize)) {
				ec_log_err("Unable to gzwrite %zu bytes to attachment \"%s\", returned %zd",
					iChunkSize, filename.c_str(), iWritten);
				er = KCERR_DATABASE_ERROR;
				break;
			}

			iSizeLeft -= iChunkSize;
		}

		if (er == erSuccess) {
			if (gzflush(gzfp, 0)) {
				int saved_errno = errno;
				int zerror;
				const char *zstrerr = gzerror(gzfp, &zerror);
				ec_log_err("gzflush failed: %s (%d)", zstrerr, zerror);
				if (zerror == Z_ERRNO)
					ec_log_err("gzflush failed: stdio says: %s", strerror(saved_errno));
				er = KCERR_DATABASE_ERROR;
			}

			if (force_changes_to_disk && !force_buffers_to_disk(fd)) {
				ec_log_warn("Problem syncing file \"%s\": %s", filename.c_str(), strerror(errno));
				er = KCERR_DATABASE_ERROR;
			}
		}

		int ret = gzclose(gzfp);
		if (ret != Z_OK) {
			ec_log_err("Problem closing file \"%s\": %s",
				filename.c_str(), (ret == Z_ERRNO) ? strerror(errno) : gzerror(gzfp, NULL));
			er = KCERR_DATABASE_ERROR;
		}

		// if gzclose fails, we can't know if the fd is still valid or not
		fd = -1;
	}
	else {
		give_filesize_hint(fd, iSize);

		// file created on disk, now in transaction
		if (m_bTransaction)
			m_setNewAttachment.emplace(ulInstanceId);

		while (iSizeLeft > 0) {
			size_t iChunkSize = iSizeLeft < CHUNK_SIZE ? iSizeLeft : CHUNK_SIZE;

			er = lpSource->Read(szBuffer, 1, iChunkSize);
			if (er != erSuccess) {
				ec_log_err("Problem retrieving attachment from ECSource: %s (0x%x)", GetMAPIErrorMessage(kcerr_to_mapierr(er, ~0U)), er);
				er = KCERR_DATABASE_ERROR;
				break;
			}

			ssize_t iWritten = write_retry(fd, szBuffer, iChunkSize);
			if (iWritten != static_cast<ssize_t>(iChunkSize)) {
				ec_log_err("Unable to write %zu bytes to streaming attachment: %s", iChunkSize, strerror(errno));
				er = KCERR_DATABASE_ERROR;
				break;
			}

			iSizeLeft -= iChunkSize;
		}

		if (er == erSuccess) {
			if (force_changes_to_disk && !force_buffers_to_disk(fd)) {
				ec_log_warn("Problem syncing file \"%s\": %s", filename.c_str(), strerror(errno));
				er = KCERR_DATABASE_ERROR;
			}
		}

		close(fd);
		fd = -1;
	}

exit:
	if (er == erSuccess && m_dirFd != -1 && fsync(m_dirFd) == -1)
		ec_log_warn("Problem syncing parent directory of \"%s\": %s", filename.c_str(), strerror(errno));
	if (fd != -1)
		close(fd);

	return er;
}

/** 
 * Delete given instances from the filesystem
 * 
 * @param[in] lstDeleteInstances List of instance ids to remove from the filesystem
 * @param[in] bReplace Transaction marker
 * 
 * @return Kopano error code
 */
ECRESULT ECFileAttachment::DeleteAttachmentInstances(const std::list<ext_siid> &lstDeleteInstances, bool bReplace)
{
	int errors = 0;

	for (const auto &del_id : lstDeleteInstances) {
		auto er = this->DeleteAttachmentInstance(del_id, bReplace);
		if (er != erSuccess)
			++errors;
	}

	if (errors)
		ec_log_err("ECFileAttachment::DeleteAttachmentInstances(): %x delete fails", errors);

	return errors == 0 ? erSuccess : KCERR_DATABASE_ERROR;
}

/** 
 * Mark a file deleted by renaming it
 * 
 * @param[in] ulInstanceId instance id to mark
 * 
 * @return Kopano error code
 */
ECRESULT ECFileAttachment::MarkAttachmentForDeletion(const ext_siid &ulInstanceId)
{
	auto filename = CreateAttachmentFilename(ulInstanceId, m_bFileCompression);

	if(rename(filename.c_str(), string(filename+".deleted").c_str()) == 0)
		return erSuccess;

	if (errno == ENOENT) {
		// retry with another filename
		filename = CreateAttachmentFilename(ulInstanceId, !m_bFileCompression);
		if(rename(filename.c_str(), string(filename+".deleted").c_str()) == 0)
			return erSuccess;
	}

	// FIXME log in all errno cases
	if (errno == EACCES || errno == EPERM)
		return KCERR_NO_ACCESS;
	else if (errno == ENOENT)
		return KCERR_NOT_FOUND;
	ec_log_err("ECFileAttachment::MarkAttachmentForDeletion(): cannot mark %u", ulInstanceId.siid);
	return KCERR_DATABASE_ERROR;
}

/** 
 * Revert a delete marked instance
 * 
 * @param[in] ulInstanceId instance id to restore
 * 
 * @return Kopano error code
 */
ECRESULT ECFileAttachment::RestoreMarkedAttachment(const ext_siid &ulInstanceId)
{
	auto filename = CreateAttachmentFilename(ulInstanceId, m_bFileCompression);
	if(rename(string(filename+".deleted").c_str(), filename.c_str()) == 0)
		return erSuccess;

	if (errno == ENOENT) {
		// retry with another filename
		filename = CreateAttachmentFilename(ulInstanceId, !m_bFileCompression);
		if(rename(string(filename+".deleted").c_str(), filename.c_str()) == 0)
			return erSuccess;
	}
	
    if (errno == EACCES || errno == EPERM)
		return KCERR_NO_ACCESS;
    else if (errno == ENOENT)
		return KCERR_NOT_FOUND;
	ec_log_err("ECFileAttachment::RestoreMarkedAttachment(): cannot mark %u", ulInstanceId.siid);
	return KCERR_DATABASE_ERROR;
}

/** 
 * Delete a marked instance from the filesystem
 * 
 * @param[in] ulInstanceId instance id to remove
 * 
 * @return Kopano error code
 */
ECRESULT ECFileAttachment::DeleteMarkedAttachment(const ext_siid &ulInstanceId)
{
	auto filename = CreateAttachmentFilename(ulInstanceId, m_bFileCompression) + ".deleted";

	if (unlink(filename.c_str()) == 0)
		return erSuccess;

	if (errno == ENOENT) {
		filename = CreateAttachmentFilename(ulInstanceId, !m_bFileCompression) + ".deleted";
		if (unlink(filename.c_str()) == 0)
			return erSuccess;
	}
	ec_log_err("%s unlink %s failed: %s\n", __PRETTY_FUNCTION__, filename.c_str(), strerror(errno));
	if (errno == EACCES || errno == EPERM)
		return KCERR_NO_ACCESS;
	if (errno != ENOENT) { // ignore "file not found" error
		ec_log_err("ECFileAttachment::DeleteMarkedAttachment() cannot delete instance %u", ulInstanceId.siid);
		return KCERR_DATABASE_ERROR;
	}
	return erSuccess;
}

/** 
 * Delete a single instanceid from the filesystem
 * 
 * @param[in] ulInstanceId instance id to remove
 * @param[in] bReplace Transaction marker
 * 
 * @return 
 */
ECRESULT ECFileAttachment::DeleteAttachmentInstance(const ext_siid &ulInstanceId, bool bReplace)
{
	ECRESULT er = erSuccess;
	auto filename = CreateAttachmentFilename(ulInstanceId, m_bFileCompression);
   
	if(m_bTransaction) {
		if (bReplace) {
			er = MarkAttachmentForDeletion(ulInstanceId);
			if (er != erSuccess && er != KCERR_NOT_FOUND) {
				assert(false);
				return er;
			} else if(er != KCERR_NOT_FOUND) {
				 m_setMarkedAttachment.emplace(ulInstanceId);
			}

			er = erSuccess;
		} else {
			m_setDeletedAttachment.emplace(ulInstanceId);
		}
		return er;
	}

	if (unlink(filename.c_str()) != 0) {
		if (errno == ENOENT){
			filename = CreateAttachmentFilename(ulInstanceId, !m_bFileCompression);
			if (unlink(filename.c_str()) == 0)
				return erSuccess;
		}

		if (errno == EACCES || errno == EPERM)
			er = KCERR_NO_ACCESS;
		else if (errno != ENOENT) { // ignore "file not found" error
			er = KCERR_DATABASE_ERROR;
			ec_log_err("ECFileAttachment::DeleteAttachmentInstance() id %u fail", ulInstanceId.siid);
		}
	}
	return er;
}

/** 
 * Return a filename for an instance id
 * 
 * @param[in] ulInstanceId instance id to convert to a filename
 * @param[in] bCompressed add compression marker to filename
 * 
 * @return Kopano error code
 */
std::string ECFileAttachment::CreateAttachmentFilename(const ext_siid &esid, bool bCompressed)
{
	unsigned int l1 = esid.siid % ATTACH_PATHDEPTH_LEVEL1;
	unsigned int l2 = (esid.siid / ATTACH_PATHDEPTH_LEVEL1) % ATTACH_PATHDEPTH_LEVEL2;
	auto filename = m_basepath + PATH_SEPARATOR + stringify(l1) + PATH_SEPARATOR + stringify(l2) + PATH_SEPARATOR + stringify(esid.siid);
	if (bCompressed)
		filename += ".gz";

	return filename;
}

/** 
 * Return the size of an instance
 * 
 * @param[in] ulInstanceId InstanceID to check the size for
 * @param[out] lpulSize Size of the instance
 * @param[out] lpbCompressed the instance was compressed
 * 
 * @return Kopano error code
 */
ECRESULT ECFileAttachment::GetSizeInstance(const ext_siid &ulInstanceId,
    size_t *lpulSize, bool *lpbCompressed)
{
	ECRESULT er = erSuccess;
	auto filename = CreateAttachmentFilename(ulInstanceId, m_bFileCompression);
	bool bCompressed = m_bFileCompression;
	struct stat st;

	/*
	 * We are always going to use the normal FILE handler for determining the file size,
	 * the gzFile handler is broken since it doesn't support SEEK_END and gzseek itself
	 * is very slow. When the attachment has been zipped, we are going to read the
	 * last 4 bytes of the file, which contain the uncompressed filesize
	 * (provided that there is a single gzip stream).
	 *
	 * For uncompressed files we use fstat() which is the fastest as the inode is already
	 * in memory due to the earlier open().
	 */
	int fd = open(filename.c_str(), O_RDONLY);
	if (fd == -1) {
		filename = CreateAttachmentFilename(ulInstanceId, !m_bFileCompression);
		bCompressed = !m_bFileCompression;

		fd = open(filename.c_str(), O_RDONLY);
	}

	if (fd == -1) {
		ec_log_err("ECFileAttachment::GetSizeInstance(): file \"%s\" cannot be accessed: %s", filename.c_str(), strerror(errno));
		return KCERR_NOT_FOUND;
	}

	/* Uncompressed attachment */
	if (fstat(fd, &st) == -1) {
		ec_log_err("ECFileAttachment::GetSizeInstance(): file \"%s\" fstat failed: %s", filename.c_str(), strerror(errno));
		// FIXME er = KCERR_DATABASE_ERROR;
		goto exit;
	}

	if (!bCompressed) {
		*lpulSize = st.st_size;
	} else if (st.st_size >= 4) {
		/* Compressed attachment */
		// a compressed file of only 4 bytes does not exist so we could
		// make this minimum size bigger
		if (lseek(fd, -4, SEEK_END) == -1) {
			ec_log_err("ECFileAttachment::GetSizeInstance(): file \"%s\" fseek (compressed file) failed: %s", filename.c_str(), strerror(errno));
			// FIXME er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		// FIXME endianness
		uint32_t atsize;
		if (read_retry(fd, &atsize, 4) != 4) {
			ec_log_err("ECFileAttachment::GetSizeInstance(): file \"%s\" fread failed: %s", filename.c_str(), strerror(errno));
			// FIXME er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		if (st.st_size >= 40 && atsize == 0) {
			ec_log_warn("ECFileAttachment: %s seems to be an unsupported multi-stream gzip file (KC-104).", filename.c_str());
			//er = KCERR_DATABASE_ERROR;
			goto exit;
		}
		*lpulSize = atsize;
	} else {
		*lpulSize = 0;
		ec_log_debug("ECFileAttachment::GetSizeInstance(): file \"%s\" is truncated!", filename.c_str());
		// FIXME return some error
	}

	if (lpbCompressed)
		*lpbCompressed = bCompressed;

exit:
	if (fd != -1)
		close(fd);

	return er;
}

kd_trans ECFileAttachment::Begin(ECRESULT &trigger)
{
	if(m_bTransaction) {
		// Possible a duplicate begin call, don't destroy the data in production
		assert(false);
		return kd_trans();
	}

	// Set begin values
	m_setNewAttachment.clear();
	m_setDeletedAttachment.clear();
	m_setMarkedAttachment.clear();
	m_bTransaction = true;
	return kd_trans(*this, trigger);
}

ECRESULT ECFileAttachment::Commit()
{
	ECRESULT er = erSuccess;
	bool bError = false;
	
	if(!m_bTransaction) {
		assert(false);
		return erSuccess;
	}

	// Disable the transaction
	m_bTransaction = false;

	// Delete the attachments
	for (const auto &att_id : m_setDeletedAttachment)
		if (DeleteAttachmentInstance(att_id, false) != erSuccess)
			bError = true;
	// Delete marked attachments
	for (const auto &att_id : m_setMarkedAttachment)
		if (DeleteMarkedAttachment(att_id) != erSuccess)
			bError = true;

	if (bError) {
		assert(false);
		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECFileAttachment::Commit() error during commit");
	}

	m_setNewAttachment.clear();
	m_setDeletedAttachment.clear();
	m_setMarkedAttachment.clear();
	return er;
}

ECRESULT ECFileAttachment::Rollback()
{
	bool bError = false;

	if(!m_bTransaction) {
		assert(false);
		return erSuccess;
	}

	// Disable the transaction
	m_bTransaction = false;

	// Don't delete the attachments
	m_setDeletedAttachment.clear();
	
	// Remove the created attachments
	for (const auto &att_id : m_setNewAttachment)
		if (DeleteAttachmentInstance(att_id, false) != erSuccess)
			bError = true;
	// Restore marked attachment
	for (const auto &att_id : m_setMarkedAttachment)
		if (RestoreMarkedAttachment(att_id) != erSuccess)
			bError = true;

	m_setNewAttachment.clear();
	m_setMarkedAttachment.clear();
	
	if (bError) {
		assert(false);
		ec_log_err("ECFileAttachment::Rollback() error");
		return KCERR_DATABASE_ERROR;
	}
	return erSuccess;
}

} /* namespace */
