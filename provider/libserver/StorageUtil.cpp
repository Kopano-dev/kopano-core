/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdio>
#include <kopano/platform.h>
#include <kopano/kcodes.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include "StorageUtil.h"
#include "ECDatabase.h"
#include "ECAttachmentStorage.h"
#include "ECSession.h"
#include "ECSessionManager.h"
#include "ECSecurity.h"
#include "cmdutil.hpp"
#include <edkmdb.h>
#include <kopano/MAPIErrors.h>

namespace KC {

static std::unordered_map<unsigned int, FILETIME> ltm_ontime_cache, ltm_offtime_cache;
static std::mutex ltm_ontime_mutex, ltm_offtime_mutex;

ECRESULT CreateObject(ECSession *lpecSession, ECDatabase *lpDatabase, unsigned int ulParentObjId, unsigned int ulParentType, unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulObjId)
{
	unsigned int ulNewObjId = 0, ulAffected = 0;
	assert(ulParentType == MAPI_FOLDER || ulParentType == MAPI_MESSAGE || ulParentType == MAPI_ATTACH);
	//
    // We skip quota checking because we do this during writeProps.

	if(ulParentType == MAPI_FOLDER) {
		// Only check creating items in folders. Creating items in messages and attachments is not security-checked since
		// you should check access rights on the top-level message, not on the underlying objects.
		auto er = lpecSession->GetSecurity()->CheckPermission(ulParentObjId, ecSecurityCreate);
		if(er != erSuccess)
			return er;
	}

	auto ulOwner = lpecSession->GetSecurity()->GetUserId(ulParentObjId); // Owner of object is either the current user or the owner of the folder
	// Create object
	auto strQuery = "INSERT INTO hierarchy (parent, type, flags, owner) values(" + stringify(ulParentObjId) + ", " + stringify(ulObjType) + ", " + stringify(ulFlags) + ", " + stringify(ulOwner) + ")";
	auto er = lpDatabase->DoInsert(strQuery, &ulNewObjId, &ulAffected);
	if(er != erSuccess)
		return er;
	if (ulObjType == MAPI_MESSAGE) {
		strQuery = "INSERT INTO properties (hierarchyid, tag, type, val_ulong) VALUES ("+ stringify(ulNewObjId) + "," + stringify(PROP_ID(PR_MESSAGE_FLAGS)) + "," + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)) + "," + stringify(ulFlags) + ")";
		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			return er;
	}

	// Save this item in the cache, as there is a very high probability that this data will be required very soon (almost 100% sure)
	g_lpSessionManager->GetCacheManager()->SetObject(ulNewObjId, ulParentObjId, ulOwner, ulFlags, ulObjType);
	// return new object id to saveObject
	if (lpulObjId)
		*lpulObjId = ulNewObjId;
	return erSuccess;
}

/* Get the size of an object, PR_MESSAGE_SIZE or PR_ATTACH_SIZE */
ECRESULT GetObjectSize(ECDatabase* lpDatabase, unsigned int ulObjId, unsigned int* lpulSize)
{
	DB_RESULT lpDBResult;
	auto strQuery = "SELECT val_ulong FROM properties WHERE hierarchyid=" + stringify(ulObjId) + " AND ((tag=" + stringify(PROP_ID(PR_MESSAGE_SIZE)) + " AND type=" + stringify(PROP_TYPE(PR_MESSAGE_SIZE)) + ") OR (tag=" + stringify(PROP_ID(PR_ATTACH_SIZE)) + " AND type=" + stringify(PROP_TYPE(PR_ATTACH_SIZE)) + ") )" + " LIMIT 1";
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	if (lpDBResult.get_num_rows() != 1)
		return KCERR_NOT_FOUND;
	auto lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == nullptr || lpDBRow[0] == nullptr)
		return KCERR_NOT_FOUND;
	*lpulSize = atoi(lpDBRow[0]);
	return erSuccess;
}

ECRESULT CalculateObjectSize(ECDatabase* lpDatabase, unsigned int objid, unsigned int ulObjType, unsigned int* lpulSize)
{
	DB_RESULT lpDBResult;
	ECDatabaseAttachment *lpDatabaseStorage = NULL;

	*lpulSize = 0;
	// SQLite doesn't support IF-type statements, so we're now using a slightly simpler construct ..
	auto strQuery = "SELECT (SELECT SUM(20 + LENGTH(IFNULL(val_string, ''))+length(IFNULL(val_binary, ''))) FROM properties WHERE hierarchyid=" + stringify(objid) + ") + IFNULL( (SELECT SUM(LENGTH(lob.val_binary)) FROM `lob` JOIN `singleinstances` ON singleinstances.instanceid = lob.instanceid WHERE singleinstances.hierarchyid=" + stringify(objid) + "), '')";
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	auto lpDBRow = lpDBResult.fetch_row();
	unsigned int ulSize = (lpDBRow == nullptr || lpDBRow[0] == nullptr) ? 0 :
	                      atoui(lpDBRow[0]) + 28; /* + hierarchy size */
	std::unique_ptr<ECAttachmentStorage> lpAttachmentStorage(g_lpSessionManager->get_atxconfig()->new_handle(lpDatabase));
	if (lpAttachmentStorage == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;

	// since we already did the length magic in the previous query, we only need the
	// extra size for filestorage and S3 storage, i.e. not database storage
	lpDatabaseStorage = dynamic_cast<ECDatabaseAttachment *>(lpAttachmentStorage.get());
	if (!lpDatabaseStorage) {
		size_t ulAttachSize = 0;
		er = lpAttachmentStorage->GetSize(objid, PROP_ID(PR_ATTACH_DATA_BIN), &ulAttachSize);
		if (er != erSuccess)
			return er;
		ulSize += ulAttachSize;
	}

	// Calculate also mv-props
	strQuery = "SELECT SUM(20 + LENGTH(IFNULL(val_string, ''))+length(IFNULL(val_binary, ''))) FROM mvproperties WHERE hierarchyid=" + stringify(objid);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	if(lpDBRow != NULL && lpDBRow[0] != NULL)
		ulSize += atoui(lpDBRow[0]); // Add the size

	// Get parent sizes
	strQuery = "SELECT SUM(IFNULL(p.val_ulong, 0)) FROM hierarchy as h JOIN properties AS p ON hierarchyid=h.id WHERE h.parent=" + stringify(objid)+ " AND ((p.tag="+stringify(PROP_ID(PR_MESSAGE_SIZE))+" AND p.type="+stringify(PROP_TYPE(PR_MESSAGE_SIZE)) + ") || (p.tag="+stringify(PROP_ID(PR_ATTACH_SIZE))+" AND p.type="+stringify(PROP_TYPE(PR_ATTACH_SIZE))+ "))";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	lpDBRow = lpDBResult.fetch_row();
	if(lpDBRow != NULL && lpDBRow[0] != NULL)
		ulSize += atoui(lpDBRow[0]); // Add the size
	*lpulSize = ulSize;
	return erSuccess;
}

ECRESULT UpdateObjectSize(ECDatabase* lpDatabase, unsigned int ulObjId, unsigned int ulObjType, eSizeUpdateAction updateAction, long long llSize)
{
	unsigned int ulPropTag = 0, ulAffRows = 0;
	std::string strField;

	if(ulObjType == MAPI_ATTACH) {
		ulPropTag = PR_ATTACH_SIZE;
		strField = "val_ulong";
	}else if(ulObjType == MAPI_STORE) {
		ulPropTag = PR_MESSAGE_SIZE_EXTENDED;
		strField = "val_longint";
	}else {
		ulPropTag = PR_MESSAGE_SIZE;
		strField = "val_ulong";
	}

	auto gcache = g_lpSessionManager->GetCacheManager();
	if (updateAction == UPDATE_SET) {
		auto strQuery = "REPLACE INTO properties(hierarchyid, tag, type, " + strField + ") VALUES(" + stringify(ulObjId) + "," + stringify(PROP_ID(ulPropTag)) + "," + stringify(PROP_TYPE(ulPropTag)) + "," + stringify_int64(llSize) + ")";
		auto er = lpDatabase->DoInsert(strQuery);
		if(er != erSuccess)
			return er;
		// Update cell cache for new size
		sObjectTableKey key;
		struct propVal sPropVal;
		key.ulObjId = ulObjId;
		key.ulOrderId = 0;
		sPropVal.ulPropTag = ulPropTag;
		sPropVal.Value.ul = llSize;
		sPropVal.__union = SOAP_UNION_propValData_ul;
		return gcache->SetCell(&key, ulPropTag, &sPropVal);
	}
	auto strQuery = "UPDATE properties SET " + strField + "=";
	if (updateAction == UPDATE_ADD)
		strQuery += strField+"+";
	else if (updateAction == UPDATE_SUB)
		strQuery += strField+"-";
	strQuery += stringify_int64(llSize) +" WHERE tag="+stringify(PROP_ID(ulPropTag))+" AND type="+stringify(PROP_TYPE(ulPropTag)) + " AND hierarchyid="+stringify(ulObjId);
	if (updateAction == UPDATE_SUB)
		strQuery += " AND "+strField+" >="+stringify_int64(llSize);
	auto er = lpDatabase->DoUpdate(strQuery, &ulAffRows);
	if (er != erSuccess)
		return er;
	er = gcache->UpdateCell(ulObjId, ulPropTag, (updateAction == UPDATE_ADD ? llSize : -llSize));
	if (er != erSuccess)
		ec_log_debug("Unable to update %d: %s (%x)", ulObjId, GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
	return erSuccess;
}

static ECRESULT ltm_sync_time(ECCacheManager *cache, ECDatabase *db,
    const std::pair<unsigned int, FILETIME> &e, bool dir)
{
	auto query = "SELECT hierarchy_id FROM stores WHERE stores.user_id=" + stringify(e.first) + " LIMIT 1";
	DB_RESULT result;
	auto ret = db->DoSelect(query, &result);
	if (ret != erSuccess)
		return ret;
	if (result.get_num_rows() == 0)
		return erSuccess;
	auto row = result.fetch_row();
	if (row == nullptr)
		return erSuccess;
	unsigned int store_id = strtoul(row[0], nullptr, 0);
	struct hiloLong hl;
	hl.hi = e.second.dwHighDateTime;
	hl.lo = e.second.dwLowDateTime;
	struct propVal pv;
	pv.__union = SOAP_UNION_propValData_hilo;
	pv.ulPropTag = dir ? PR_LAST_LOGON_TIME : PR_LAST_LOGOFF_TIME;
	pv.Value.hilo = &hl;
	ret = WriteProp(db, store_id, 0, &pv);
	if (cache == nullptr)
		return erSuccess;
	sObjectTableKey key(store_id, 0);
	return cache->SetCell(&key, pv.ulPropTag, &pv);
}

void sync_logon_times(ECCacheManager *cache, ECDatabase *db)
{
	/*
	 * Switchgrab the global map, so that we can run it to the database
	 * without holdings locks.
	 */
	bool failed = false;
	ltm_ontime_mutex.lock();
	decltype(ltm_ontime_cache) logon_time;
	std::swap(ltm_ontime_cache, logon_time);
	ltm_ontime_mutex.unlock();
	ltm_offtime_mutex.lock();
	decltype(ltm_offtime_cache) logoff_time;
	std::swap(ltm_offtime_cache, logoff_time);
	ltm_offtime_mutex.unlock();
	for (const auto &i : logon_time)
		failed |= ltm_sync_time(cache, db, i, 0) != erSuccess;
	for (const auto &i : logoff_time)
		failed |= ltm_sync_time(cache, db, i, 1) != erSuccess;
	if (failed)
		ec_log_warn("Writeout of logon/off time cache unsuccessful");
}

/*
 * Save the current time as the last logon time for the logged-on user of
 * @ses.
 */
void record_logon_time(ECSession *ses, bool logon)
{
	unsigned int uid = ses->GetSecurity()->GetUserId();
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	if (logon) {
		ltm_ontime_mutex.lock();
		ltm_ontime_cache[uid] = std::move(ft);
		ltm_ontime_mutex.unlock();
	} else {
		ltm_offtime_mutex.lock();
		ltm_offtime_cache[uid] = std::move(ft);
		ltm_offtime_mutex.unlock();
	}
}

} /* namespace */
