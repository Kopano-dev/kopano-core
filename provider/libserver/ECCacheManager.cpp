/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <mapidefs.h>
#include <mapitags.h>
#include <kopano/MAPIErrors.h>
#include <kopano/scope.hpp>
#include "ECDatabase.h"
#include "ECSessionManager.h"
#include "ECDatabaseUtils.h"
#include "ECCacheManager.h"
#include "ECMAPI.h"
#include <kopano/stringutil.h>
#include "ECGenericObjectTable.h"
#include <algorithm>
#ifdef LINUX
#include <sys/sysinfo.h>
#endif

namespace KC {

#define LOG_CACHE_DEBUG(_msg, ...) \
	ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_CACHE, "cache: " _msg, ##__VA_ARGS__)
#define LOG_USERCACHE_DEBUG(_msg, ...) \
	ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_USERCACHE, "usercache: " _msg, ##__VA_ARGS__)
#define LOG_CELLCACHE_DEBUG(_msg, ...) \
	ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_CACHE, "cellcache: " _msg, ##__VA_ARGS__)

// Specialization for ECsACL
template<> size_t GetCacheAdditionalSize(const ECsACLs &val)
{
	return val.ulACLs * sizeof(val.aACL[0]);
}

template<> size_t GetCacheAdditionalSize(const ECsIndexProp &val)
{
	return val.cbData;
}

// Specialization for ECsCell
template<> size_t GetCacheAdditionalSize(const ECsCells &val)
{
	return val.GetSize();
}

template<> size_t GetCacheAdditionalSize(const std::string &val)
{
	return MEMORY_USAGE_STRING(val);
}

template<> size_t GetCacheAdditionalSize(const ECsUEIdKey &val)
{
	return MEMORY_USAGE_STRING(val.strExternId);
}

ECCacheManager::ECCacheManager(std::shared_ptr<ECConfig> lpConfig,
    ECDatabaseFactory *lpDatabaseFactory) :
	m_lpDatabaseFactory(lpDatabaseFactory),
	m_QuotaCache("quota", atoi(lpConfig->GetSetting("cache_quota_size")), atoi(lpConfig->GetSetting("cache_quota_lifetime")) * 60)
, m_QuotaUserDefaultCache("uquota", atoi(lpConfig->GetSetting("cache_quota_size")), atoi(lpConfig->GetSetting("cache_quota_lifetime")) * 60)
, m_ObjectsCache("obj", atoll(lpConfig->GetSetting("cache_object_size")), 0)
, m_StoresCache("store", atoi(lpConfig->GetSetting("cache_store_size")), 0)
, m_UserObjectCache("userid", atoi(lpConfig->GetSetting("cache_user_size")), atoi(lpConfig->GetSetting("cache_userdetails_lifetime")) * 60)
, m_UEIdObjectCache("extern", atoi(lpConfig->GetSetting("cache_user_size")), atoi(lpConfig->GetSetting("cache_userdetails_lifetime")) * 60)
, m_UserObjectDetailsCache("abinfo", atoi(lpConfig->GetSetting("cache_userdetails_size")), atoi(lpConfig->GetSetting("cache_userdetails_lifetime")) * 60)
, m_AclCache("acl", atoi(lpConfig->GetSetting("cache_acl_size")), 0)
, m_CellCache("cell", atoll(lpConfig->GetSetting("cache_cell_size")), 0)
, m_ServerDetailsCache("server", atoi(lpConfig->GetSetting("cache_server_size")), atoi(lpConfig->GetSetting("cache_server_lifetime")) * 60)
, m_PropToObjectCache("index1", atoll(lpConfig->GetSetting("cache_indexedobject_size")), 0)
, m_ObjectToPropCache("index2", atoll(lpConfig->GetSetting("cache_indexedobject_size")), 0)
{
	if (atoll(lpConfig->GetSetting("cache_cell_size")) == 0) {
		#ifdef LINUX
		struct sysinfo s;
		auto res = sysinfo(&s);
		if (res != 0)
			m_CellCache.SetMaxSize(256 << 20);
		else
			m_CellCache.SetMaxSize(std::max(static_cast<uint64_t>(1) << 30, static_cast<uint64_t>(s.totalram) / 4));
		#else
		m_CellCache.SetMaxSize(256 << 20);
		#endif
		ec_log_info("Setting cell cache size: %zu", m_CellCache.MaxSize());
	}

	auto cell_cache_size = m_CellCache.MaxSize();

	if (atoll(lpConfig->GetSetting("cache_object_size")) == 0) {
		m_ObjectsCache.SetMaxSize(std::min(static_cast<size_t>(16 << 20), cell_cache_size / 16));
		ec_log_info("Setting object cache size: %zu", m_ObjectsCache.MaxSize());
	}

	if (atoll(lpConfig->GetSetting("cache_indexedobject_size")) == 0) {
		m_PropToObjectCache.SetMaxSize(std::min(static_cast<size_t>(32 << 20), cell_cache_size / 8));
		m_ObjectToPropCache.SetMaxSize(std::min(static_cast<size_t>(32 << 20), cell_cache_size / 8));
		ec_log_info("Setting indexedobject cache size: %zu", m_PropToObjectCache.MaxSize());
	}

	if (atoll(lpConfig->GetSetting("cache_quota_size")) == 0) {
		m_QuotaCache.SetMaxSize(std::min(static_cast<size_t>(1 << 20), cell_cache_size / 256));
		m_QuotaUserDefaultCache.SetMaxSize(std::min(static_cast<size_t>(1 << 20), cell_cache_size / 256));
		ec_log_info("Setting quota cache size: %zu", m_QuotaCache.MaxSize());
	}

	if (atoll(lpConfig->GetSetting("cache_userdetails_size")) == 0) {
		m_UserObjectCache.SetMaxSize(std::min(static_cast<size_t>(25 << 20), cell_cache_size / 10));
		m_UEIdObjectCache.SetMaxSize(std::min(static_cast<size_t>(25 << 20), cell_cache_size / 10));
		m_UserObjectDetailsCache.SetMaxSize(std::min(static_cast<size_t>(25 << 20), cell_cache_size / 10));
		ec_log_info("Setting userdetails cache size: %zu", m_UserObjectCache.MaxSize());
	}

	/* Initial cleaning/initialization of cache */
	PurgeCache(PURGE_CACHE_ALL);
}

ECCacheManager::~ECCacheManager()
{
	PurgeCache(PURGE_CACHE_ALL);
}

ECRESULT ECCacheManager::PurgeCache(unsigned int ulFlags)
{
	auto start = std::chrono::steady_clock::now();
	LOG_CACHE_DEBUG("Purge cache, flags 0x%08X", ulFlags);

	// cache mutex items
	ulock_rec l_cache(m_hCacheMutex);
	if (ulFlags & PURGE_CACHE_QUOTA)
		m_QuotaCache.ClearCache();
	if (ulFlags & PURGE_CACHE_QUOTADEFAULT)
		m_QuotaUserDefaultCache.ClearCache();
	if (ulFlags & PURGE_CACHE_ACL)
		m_AclCache.ClearCache();
	l_cache.unlock();

	ulock_rec l_object(m_hCacheObjectMutex);
	if (ulFlags & PURGE_CACHE_OBJECTS)
		m_ObjectsCache.ClearCache();
	l_object.unlock();

	ulock_rec l_store(m_hCacheStoreMutex);
	if (ulFlags & PURGE_CACHE_STORES)
		m_StoresCache.ClearCache();
	l_store.unlock();

	// Cell cache mutex
	ulock_rec l_cells(m_hCacheCellsMutex);
	if(ulFlags & PURGE_CACHE_CELL)
		m_CellCache.ClearCache();
	l_cells.unlock();

	// Indexed properties mutex
	ulock_rec l_prop(m_hCacheIndPropMutex);
	if (ulFlags & PURGE_CACHE_INDEX1)
		m_PropToObjectCache.ClearCache();
	if (ulFlags & PURGE_CACHE_INDEX2)
		m_ObjectToPropCache.ClearCache();
	l_prop.unlock();

	ulock_normal l_xp(m_hExcludedIndexPropertiesMutex);
	if (ulFlags & PURGE_CACHE_INDEXEDPROPERTIES)
		m_setExcludedIndexProperties.clear();
	l_xp.unlock();

	l_cache.lock();
	if (ulFlags & PURGE_CACHE_USEROBJECT)
		m_UserObjectCache.ClearCache();
	if (ulFlags & PURGE_CACHE_EXTERNID)
		m_UEIdObjectCache.ClearCache();
	if (ulFlags & PURGE_CACHE_USERDETAILS)
		m_UserObjectDetailsCache.ClearCache();
	if (ulFlags & PURGE_CACHE_SERVER)
		m_ServerDetailsCache.ClearCache();
	l_cache.unlock();

	using namespace std::chrono;
	auto end = duration_cast<milliseconds>(decltype(start)::clock::now() - start);
	ec_log_debug("PurgeCache took %u ms", static_cast<unsigned int>(end.count()));
	return erSuccess;
}

ECRESULT ECCacheManager::Update(unsigned int ulType, unsigned int ulObjId)
{
	switch(ulType)
	{
	case fnevObjectModified:
		LOG_CACHE_DEBUG("Remove cache ACLs, cell, objects for object %d", ulObjId);
		I_DelACLs(ulObjId);
		I_DelCell(ulObjId);
		I_DelObject(ulObjId);
		break;
	case fnevObjectDeleted:
		LOG_CACHE_DEBUG("Remove cache ACLs, cell, objects and store for object %d", ulObjId);
		I_DelObject(ulObjId);
		I_DelStore(ulObjId);
		I_DelACLs(ulObjId);
		I_DelCell(ulObjId);
		break;
	case fnevObjectMoved:
		LOG_CACHE_DEBUG("Remove cache cell, objects and store for object %d", ulObjId);
		I_DelStore(ulObjId);
		I_DelObject(ulObjId);
		I_DelCell(ulObjId);
		break;
	default:
		//Do nothing
		LOG_CACHE_DEBUG("Update cache, action type %d, objectid %d", ulType, ulObjId);
		break;
	}
	return erSuccess;
}

ECRESULT ECCacheManager::UpdateUser(unsigned int ulUserId)
{
	std::string strExternId;
	objectclass_t ulClass;

	LOG_USERCACHE_DEBUG("Remove user id %d from the cache", ulUserId);

	if (I_GetUserObject(ulUserId, &ulClass, NULL, &strExternId, NULL) == erSuccess)
		I_DelUEIdObject(strExternId, ulClass);
	I_DelUserObject(ulUserId);
	I_DelUserObjectDetails(ulUserId);
	I_DelQuota(ulUserId, false);
	I_DelQuota(ulUserId, true);
	return erSuccess;
}

ECRESULT ECCacheManager::I_GetObject(unsigned int ulObjId,
    unsigned int *ulParent, unsigned int *ulOwner, unsigned int *ulFlags,
    unsigned int *ulType)
{
	ECsObjects	*sObject;
	scoped_rlock lock(m_hCacheObjectMutex);

	auto er = m_ObjectsCache.GetCacheItem(ulObjId, &sObject);
	if(er != erSuccess)
		return er;
	assert(sObject->ulType == MAPI_FOLDER || (sObject->ulFlags & ~(MSGFLAG_ASSOCIATED | MSGFLAG_DELETED)) == 0);
	if(ulParent)
		*ulParent = sObject->ulParent;

	if(ulOwner)
		*ulOwner = sObject->ulOwner;

	if(ulFlags)
		*ulFlags = sObject->ulFlags;

	if(ulType)
		*ulType = sObject->ulType;
	return erSuccess;
}

ECRESULT ECCacheManager::SetObject(unsigned int ulObjId, unsigned int ulParent, unsigned int ulOwner, unsigned int ulFlags, unsigned int ulType)
{
	ECsObjects		sObjects;

	if(ulParent == 0 || ulObjId == 0 || ulOwner == 0)
		return 1;
	assert(ulType == MAPI_FOLDER || (ulFlags & ~(MSGFLAG_ASSOCIATED | MSGFLAG_DELETED)) == 0);
	sObjects.ulParent	= ulParent;
	sObjects.ulOwner	= ulOwner;
	sObjects.ulFlags	= ulFlags;
	sObjects.ulType		= ulType;

	scoped_rlock lock(m_hCacheObjectMutex);
	auto er = m_ObjectsCache.AddCacheItem(ulObjId, std::move(sObjects));
	LOG_CACHE_DEBUG("Set cache object id %d, parent %d, owner %d, flags %d, type %d", ulObjId, ulParent, ulOwner, ulFlags, ulType);
	return er;
}

ECRESULT ECCacheManager::I_DelObject(unsigned int ulObjId)
{
	scoped_rlock lock(m_hCacheObjectMutex);
	return m_ObjectsCache.RemoveCacheItem(ulObjId);
}

ECRESULT ECCacheManager::I_GetStore(unsigned int ulObjId, unsigned int *ulStore,
    GUID *lpGuid, unsigned int *lpulType)
{
	ECsStores	*sStores;
	scoped_rlock lock(m_hCacheStoreMutex);

	auto er = m_StoresCache.GetCacheItem(ulObjId, &sStores);
	if(er != erSuccess)
		return er;
	if(ulStore)
		*ulStore = sStores->ulStore;
	if (lpulType != NULL)
		*lpulType = sStores->ulType;
	if(lpGuid)
		memcpy(lpGuid, &sStores->guidStore, sizeof(GUID) );
	return erSuccess;
}

ECRESULT ECCacheManager::SetStore(unsigned int ulObjId, unsigned int ulStore,
    const GUID *lpGuid, unsigned int ulType)
{
	ECsStores		sStores;
	sStores.ulStore = ulStore;
	sStores.guidStore = *lpGuid;
	sStores.ulType = ulType;

	scoped_rlock lock(m_hCacheStoreMutex);
	auto er = m_StoresCache.AddCacheItem(ulObjId, std::move(sStores));
	LOG_CACHE_DEBUG("Set store cache id %d, store %d, type %d, guid %s", ulObjId, ulStore, ulType, (lpGuid != nullptr ? bin2hex(sizeof(GUID), lpGuid).c_str() : "NULL"));
	return er;
}

ECRESULT ECCacheManager::I_DelStore(unsigned int ulObjId)
{
	scoped_rlock lock(m_hCacheStoreMutex);
	return m_StoresCache.RemoveCacheItem(ulObjId);
}

ECRESULT ECCacheManager::GetOwner(unsigned int ulObjId, unsigned int *ulOwner)
{
	ECRESULT	er = erSuccess;
	auto bCacheResult = I_GetObject(ulObjId, nullptr, ulOwner, nullptr, nullptr) == erSuccess;
	if (!bCacheResult)
		er = GetObject(ulObjId, NULL, ulOwner, NULL);
	if (er != erSuccess)
		LOG_CACHE_DEBUG("Get owner for id %d: %s (%x)", ulObjId, GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
	else
		LOG_CACHE_DEBUG("Get Owner for id %d result [%s]: owner %d", ulObjId, ((bCacheResult)?"C":"D"), *ulOwner);
	return er;
}

ECRESULT ECCacheManager::GetParent(unsigned int ulObjId, unsigned int *lpulParent)
{
	unsigned int ulParent = 0;
	auto er = GetObject(ulObjId, &ulParent, nullptr, nullptr);
	if(er != erSuccess)
		return er;
	if (ulParent == CACHE_NO_PARENT)
		return KCERR_NOT_FOUND;
	*lpulParent = ulParent;
	return erSuccess;
}

ECRESULT ECCacheManager::QueryParent(unsigned int ulObjId, unsigned int *lpulParent)
{
	return I_GetObject(ulObjId, lpulParent, nullptr, nullptr, nullptr);
}

// Get the parent of the specified object
ECRESULT ECCacheManager::GetObject(unsigned int ulObjId, unsigned int *lpulParent, unsigned int *lpulOwner, unsigned int *lpulFlags, unsigned int *lpulType)
{
	DB_RESULT lpDBResult;
	DB_ROW		lpDBRow = NULL;
	std::string strQuery;
	ECDatabase	*lpDatabase = NULL;
	unsigned int	ulParent = 0, ulOwner = 0, ulFlags = 0, ulType = 0;
	bool bCacheResult = false;

	auto er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
	if(er != erSuccess)
		goto exit;

	// first check the cache if the item exists
	if (I_GetObject(ulObjId, &ulParent, &ulOwner, &ulFlags, &ulType) == erSuccess) {
		if(lpulParent)
			*lpulParent = ulParent;
		if(lpulOwner)
			*lpulOwner = ulOwner;
		if(lpulFlags)
			*lpulFlags = ulFlags;
		if(lpulType)
			*lpulType = ulType;

		bCacheResult = true;
		goto exit;
	}

	strQuery = "SELECT hierarchy.parent, hierarchy.owner, hierarchy.flags, hierarchy.type FROM hierarchy WHERE hierarchy.id = " + stringify(ulObjId) + " LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;
	lpDBRow = lpDBResult.fetch_row();
	if(lpDBRow == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	if(lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL) {
		// owner or flags should not be NULL
		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECCacheManager::GetObject(): NULL in columns");
		goto exit;
	}

	ulParent = lpDBRow[0] == NULL ? CACHE_NO_PARENT : atoui(lpDBRow[0]);
	ulOwner = atoui(lpDBRow[1]);
	ulFlags = atoui(lpDBRow[2]);
	ulType = atoui(lpDBRow[3]);

	if(lpulParent)
		*lpulParent = ulParent;
	if(lpulOwner)
		*lpulOwner = ulOwner;
	if(lpulFlags)
		*lpulFlags = ulFlags;
	if(lpulType)
		*lpulType = ulType;

	SetObject(ulObjId, ulParent, ulOwner, ulFlags, ulType);

exit:
	if (er != erSuccess)
		LOG_CACHE_DEBUG("Get object id %d error 0x%08x", ulObjId, er);
	else
		LOG_CACHE_DEBUG("Get object id %d result [%s]: parent %d owner %d flags %d type %d", ulObjId, ((bCacheResult)?"C":"D"), ulParent, ulOwner, ulFlags, ulType);
	return er;
}

ECRESULT ECCacheManager::GetObjects(const std::list<sObjectTableKey> &lstObjects,
    std::map<sObjectTableKey, ECsObjects> &mapObjects)
{
	DB_RESULT lpDBResult;
	DB_ROW		lpDBRow = NULL;
	ECDatabase	*lpDatabase = NULL;
	ECsObjects sObject, *lpsObject = nullptr;
	std::set<sObjectTableKey> setUncached;

	auto er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
	if(er != erSuccess)
		goto exit;

    {
        // Get everything from the cache that we can
       scoped_rlock lock(m_hCacheObjectMutex);

	for (const auto &key : lstObjects)
		if (m_ObjectsCache.GetCacheItem(key.ulObjId, &lpsObject) == erSuccess)
			mapObjects[key] = *lpsObject;
		else
			setUncached.emplace(key);
    }
    if(!setUncached.empty()) {
        // Get uncached items from SQL
		auto strQuery = "SELECT id, parent, owner, flags, type FROM hierarchy WHERE id IN(" +
			kc_join(setUncached, ",", [](const auto &key) { return stringify(key.ulObjId); }) + ")";
        er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if (er != erSuccess)
            goto exit;

        while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
            if(!lpDBRow[0] || !lpDBRow[1] || !lpDBRow[2] || !lpDBRow[3])
                continue;
			auto ulObjId = atoui(lpDBRow[0]);
            sObject.ulParent = atoui(lpDBRow[1]);
            sObject.ulOwner = atoui(lpDBRow[2]);
            sObject.ulFlags = atoui(lpDBRow[3]);
            sObject.ulType = atoui(lpDBRow[4]);

			sObjectTableKey key;
            key.ulObjId = ulObjId;
            key.ulOrderId = 0;
            mapObjects[key] = sObject;
        }
    }
	if (mapObjects.size() < lstObjects.size())
		LOG_CACHE_DEBUG("Get objects ids warning %zu objects not found",
			lstObjects.size() - mapObjects.size());
exit:
	if (er != erSuccess)
		LOG_CACHE_DEBUG("Get object ids error 0x%08x", er);
	else
		LOG_CACHE_DEBUG("Get object ids total ids %zu from disk %zu",
			lstObjects.size(), setUncached.size());
	return er;
}

ECRESULT ECCacheManager::GetObjectsFromProp(unsigned int ulTag,
    const std::vector<unsigned int> &cbdata,
    const std::vector<unsigned char *> &lpdata,
    std::map<ECsIndexProp, unsigned int> &mapObjects)
{
	ECRESULT er = erSuccess;
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;
	unsigned int objid;
	std::vector<size_t> uncached;

	for (size_t i = 0; i < lpdata.size(); ++i) {
		if (QueryObjectFromProp(ulTag, cbdata[i], lpdata[i], &objid) == erSuccess) {
			ECsIndexProp p(ulTag, lpdata[i], cbdata[i]);
			mapObjects[std::move(p)] = objid;
		} else {
			uncached.emplace_back(i);
		}
	}

	if (!uncached.empty()) {
		er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
		if (er != erSuccess)
			goto exit;
		auto strQuery = "SELECT hierarchyid, val_binary FROM indexedproperties WHERE tag=" + stringify(ulTag) + " AND val_binary IN(" +
			kc_join(uncached, ",", [&](const auto &j) { return lpDatabase->EscapeBinary(lpdata[j], cbdata[j]); }) + ")";
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess)
			goto exit;

		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			auto lpDBLen = lpDBResult.fetch_row_lengths();
			ECsIndexProp p(ulTag, reinterpret_cast<unsigned char *>(lpDBRow[1]), lpDBLen[1]);
			mapObjects[std::move(p)] = atoui(lpDBRow[0]);
		}
	}
	if (mapObjects.size() < lpdata.size())
		LOG_CACHE_DEBUG("Get objects ids warning %zu objects not found",
			lpdata.size() - mapObjects.size());
 exit:
	if (er != erSuccess)
		LOG_CACHE_DEBUG("Get object ids from props error: 0x%08x", er);
	else
		LOG_CACHE_DEBUG("Get object ids from props total ids %zu from disk %zu", cbdata.size(), uncached.size());
	return er;
}

// Get the store that the specified object belongs to
ECRESULT ECCacheManager::GetStore(unsigned int ulObjId, unsigned int *lpulStore, GUID *lpGuid, unsigned int maxdepth)
{
	LOG_CACHE_DEBUG("Get store id %d >", ulObjId);
    return GetStoreAndType(ulObjId, lpulStore, lpGuid, NULL, maxdepth);
}

// Get the store that the specified object belongs to
ECRESULT ECCacheManager::GetStoreAndType(unsigned int ulObjId, unsigned int *lpulStore, GUID *lpGuid, unsigned int *lpulType, unsigned int maxdepth)
{
	DB_RESULT lpDBResult;
	ECDatabase	*lpDatabase = NULL;
	unsigned int ulSubObjId = 0, ulStore = 0, ulType = 0;
	GUID guid;
	bool bCacheResult = false;

	if(maxdepth <= 0)
	    return KCERR_NOT_FOUND;
	auto er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
	if(er != erSuccess)
		goto exit;

	// first check the cache if we already know the store for this object
	if (I_GetStore(ulObjId, &ulStore, &guid, &ulType) == erSuccess) {
		bCacheResult = true;
		goto found;
	}

    // Get our parent folder
	if(GetParent(ulObjId, &ulSubObjId) != erSuccess) {
	    // No parent, this must be the top-level item, get the store data from here
		auto strQuery = "SELECT hierarchy_id, guid, type FROM stores WHERE hierarchy_id = " + stringify(ulObjId) + " LIMIT 1";
    	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
    	if(er != erSuccess)
			goto exit;
		if (lpDBResult.get_num_rows() < 1) {
    		er = KCERR_NOT_FOUND;
    		goto exit;
    	}
		auto lpDBRow = lpDBResult.fetch_row();
    	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
    		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECCacheManager::GetStoreAndType(): NULL in columns");
    		goto exit;
    	}
    	ulStore = atoi(lpDBRow[0]);
        memcpy(&guid, lpDBRow[1], sizeof(GUID));
        ulType = atoi(lpDBRow[2]);
	} else {
	    // We have a parent, get the store for our parent by recursively calling ourselves
	    er = GetStoreAndType(ulSubObjId, &ulStore, &guid, &ulType, maxdepth-1);
	    if(er != erSuccess)
	        goto exit;
    }

    // insert the item into the cache
    SetStore(ulObjId, ulStore, &guid, ulType);
found:
    if(lpulStore)
        *lpulStore = ulStore;
    if(lpGuid)
        *lpGuid = guid;
    if(lpulType)
        *lpulType = ulType;
exit:
	if (er != erSuccess)
		LOG_CACHE_DEBUG("Get store and type %d error 0x%08x", ulObjId, er);
	else
		LOG_CACHE_DEBUG("Get store and type %d result [%s]: store %d, type %d, guid %s", ulObjId, (bCacheResult ? "C" : "D"), ulStore, ulType, bin2hex(sizeof(GUID), &guid).c_str());
	return er;
}

ECRESULT ECCacheManager::GetUserObject(unsigned int ulUserId, objectid_t *lpExternId, unsigned int *lpulCompanyId, std::string *lpstrSignature)
{
	ECRESULT	er = erSuccess;
	DB_RESULT lpDBResult;
	DB_ROW		lpDBRow = NULL;
	DB_LENGTHS	lpDBLen = NULL;
	ECDatabase	*lpDatabase = NULL;
	objectclass_t ulClass;
	unsigned int ulCompanyId;
	std::string externid, signature;
	bool bCacheResult = false;

	// first check the cache if we already know the external id for this user
	if (I_GetUserObject(ulUserId, &ulClass, lpulCompanyId, &externid, lpstrSignature) == erSuccess) {
		if (lpExternId) {
			lpExternId->id = externid;
			lpExternId->objclass = ulClass;
		}
		bCacheResult = true;
		goto exit;
	}

	er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->DoSelect("SELECT externid, objectclass, signature, company FROM users "
							  "WHERE id=" + stringify(ulUserId) + " LIMIT 1", &lpDBResult);
	if (er != erSuccess) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECCacheManager::GetUserObject(): NULL in columns");
		goto exit;
	}

	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	ulClass = static_cast<objectclass_t>(atoui(lpDBRow[1]));
	ulCompanyId = atoui(lpDBRow[3]);

	externid.assign(lpDBRow[0], lpDBLen[0]);
	signature.assign(lpDBRow[2], lpDBLen[2]);

	// insert the item into the cache
	I_AddUserObject(ulUserId, ulClass, ulCompanyId, externid, signature);
	if (lpExternId) {
		lpExternId->id = externid;
		lpExternId->objclass = ulClass;
	}

	if(lpulCompanyId)
		*lpulCompanyId = ulCompanyId;

	if(lpstrSignature)
		*lpstrSignature = signature;

exit:
	if (er != erSuccess)
		LOG_USERCACHE_DEBUG("Get user object for user %d error 0x%08x", ulUserId, er);
	else
		LOG_USERCACHE_DEBUG("Get user object for user %d result [%s]: externid \"%s\", class %d, companyid %d, signature \"%s\"",
			ulUserId, (bCacheResult ? "C" : "D"), bin2txt(externid).c_str(), ulClass,
			(lpulCompanyId != nullptr ? *lpulCompanyId : -1),
			(lpstrSignature != nullptr ? bin2txt(*lpstrSignature).c_str() : "-"));
	return er;
}

ECRESULT ECCacheManager::get_all_user_objects(objectclass_t ocls,
    std::map<unsigned int, ECsUserObject> &out)
{
	ECDatabase *db = nullptr;
	DB_RESULT result;
	DB_ROW row;

	auto ret = m_lpDatabaseFactory->get_tls_db(&db);
	if (ret != erSuccess)
		return ret;
	ret = db->DoSelect("SELECT externid, objectclass, signature, company, id "
	      "FROM users WHERE " + OBJECTCLASS_COMPARE_SQL("objectclass", ocls), &result);
	if (ret != erSuccess)
		return KCERR_DATABASE_ERROR;

	out.clear();
	while ((row = result.fetch_row()) != nullptr) {
		ECsUserObject u;
		if (row[0] != nullptr)
			u.strExternId = row[0];
		u.ulClass = static_cast<objectclass_t>(atoui(row[1]));
		u.strSignature = row[2];
		u.ulCompanyId = atoui(row[3]);
		I_AddUserObject(atoui(row[4]), u.ulClass, u.ulCompanyId, u.strExternId, u.strSignature);
		out.emplace(atoui(row[4]), std::move(u));
	}
	return erSuccess;
}

ECRESULT ECCacheManager::GetUserDetails(unsigned int ulUserId, objectdetails_t *details)
{
	auto er = I_GetUserObjectDetails(ulUserId, details);
	// on error, ECUserManagement will update the cache
	if (er != erSuccess)
		LOG_USERCACHE_DEBUG("Get user details for userid %d not found, error 0x%08x", ulUserId, er);
	else
		LOG_USERCACHE_DEBUG("Get user details for userid %d result: %s", ulUserId, details->ToStr().c_str() );
	return er;
}

ECRESULT ECCacheManager::SetUserDetails(unsigned int ulUserId,
    const objectdetails_t &details)
{
	return I_AddUserObjectDetails(ulUserId, details);
}

ECRESULT ECCacheManager::GetUserObject(const objectid_t &sExternId, unsigned int *lpulUserId, unsigned int *lpulCompanyId, std::string *lpstrSignature)
{
	ECRESULT	er = erSuccess;
	DB_RESULT lpDBResult;
	DB_ROW		lpDBRow = NULL;
	DB_LENGTHS	lpDBLen = NULL;
	std::string	strQuery;
	ECDatabase	*lpDatabase = NULL;
	unsigned int ulCompanyId, ulUserId;
	std::string signature;
	objectclass_t objclass = sExternId.objclass;
	bool bCacheResult = false;

	if (sExternId.id.empty()) {
		er = KCERR_DATABASE_ERROR;
		//assert(false);
		goto exit;
	}

	// first check the cache if we already know the external id for this user
	if (I_GetUEIdObject(sExternId.id, sExternId.objclass, lpulCompanyId, lpulUserId, lpstrSignature) == erSuccess) {
		bCacheResult = true;
		goto exit;
	}
	er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	strQuery =
		"SELECT id, signature, company, objectclass FROM users "
		"WHERE externid=" + lpDatabase->EscapeBinary(sExternId.id) + " "
			"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", sExternId.objclass) + " LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		er = KCERR_DATABASE_ERROR;
		ec_perror("ECCacheManager::GetUserObject(): query failed", er);
		goto exit;
	}

	// TODO: check, should return 1 answer
	lpDBRow = lpDBResult.fetch_row();
	lpDBLen = lpDBResult.fetch_row_lengths();
	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	ulUserId = atoui(lpDBRow[0]);
	signature.assign(lpDBRow[1], lpDBLen[1]);
	ulCompanyId = atoui(lpDBRow[2]);

	// possibly update objectclass from database, to add the correct info in the cache
	if (OBJECTCLASS_ISTYPE(sExternId.objclass))
		objclass = static_cast<objectclass_t>(atoi(lpDBRow[3]));

	// insert the item into the cache
	I_AddUEIdObject(sExternId.id, objclass, ulCompanyId, ulUserId, signature);
	if(lpulCompanyId)
		*lpulCompanyId = ulCompanyId;

	if(lpulUserId)
		*lpulUserId = ulUserId;

	if(lpstrSignature)
		*lpstrSignature = signature;

exit:
	if (er != erSuccess)
		LOG_USERCACHE_DEBUG("Get user object done: %s (%x)", GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
	else
		LOG_USERCACHE_DEBUG("Get user object from externid \"%s\", class %d result [%s]: company %d, userid %d, signature \"%s\"",
			bin2txt(sExternId.id).c_str(), sExternId.objclass, (bCacheResult ? "C" : "D"),
			(lpulCompanyId != nullptr ? *lpulCompanyId : -1),
			(lpulUserId != nullptr ? *lpulUserId : -1),
			(lpstrSignature != nullptr ? bin2txt(*lpstrSignature).c_str() : "-"));
	return er;
}

ECRESULT ECCacheManager::GetUserObjects(const std::list<objectid_t> &lstExternObjIds,
    std::map<objectid_t, unsigned int> *lpmapLocalObjIds)
{
	ECRESULT er = erSuccess;
	DB_RESULT lpDBResult;
	std::string strQuery;
	ECDatabase *lpDatabase = NULL;
	std::list<objectid_t> lstExternIds;
	objectid_t sExternId;
	std::string strSignature;

	// Collect as many objects from cache as possible,
	// everything we couldn't find must be collected from the database
	LOG_USERCACHE_DEBUG("Get User Objects. requested objects %zu",
		lstExternObjIds.size());

	for (const auto &objid : lstExternObjIds) {
		unsigned int ulLocalId;
		LOG_USERCACHE_DEBUG(" Get user objects from externid \"%s\", class %d",
			bin2txt(objid.id).c_str(), objid.objclass);
		if (I_GetUEIdObject(objid.id, objid.objclass, NULL, &ulLocalId, NULL) == erSuccess)
			/* Object was found in cache. */
			lpmapLocalObjIds->insert({objid, ulLocalId});
		else
			/* Object was not found in cache. */
			lstExternIds.emplace_back(objid);
	}

	// Check if all objects have been collected from the cache
	if (lstExternIds.empty())
		goto exit;
	er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	strQuery = "SELECT id, externid, objectclass, signature, company FROM users WHERE " +
		kc_join(lstExternIds, "OR", [&](const auto &i) { return
			"(" + OBJECTCLASS_COMPARE_SQL("objectclass", i.objclass) +
			" AND externid=" + lpDatabase->EscapeBinary(i.id) + ")"; });
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_perror("ECCacheManager::GetUserObjects() query failed", er);
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}

	while (TRUE) {
		auto lpDBRow = lpDBResult.fetch_row();
		auto lpDBLen = lpDBResult.fetch_row_lengths();
		if (lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL || lpDBRow[4] == NULL)
			break;
		auto ulLocalId = atoi(lpDBRow[0]);
		sExternId.id.assign(lpDBRow[1], lpDBLen[1]);
		sExternId.objclass = static_cast<objectclass_t>(atoi(lpDBRow[2]));
		strSignature.assign(lpDBRow[3], lpDBLen[3]);
		auto ulCompanyId = atoi(lpDBRow[4]);
		lpmapLocalObjIds->insert({sExternId, ulLocalId});
		I_AddUEIdObject(sExternId.id, sExternId.objclass, ulCompanyId, ulLocalId, strSignature);
		LOG_USERCACHE_DEBUG(" Get user objects result company %d, userid %d, signature \"%s\"",
			ulCompanyId, ulLocalId, bin2txt(strSignature).c_str());
	}

	// From this point you can have less items in lpmapLocalObjIds than requested in lstExternObjIds

exit:
	if (er != erSuccess)
		LOG_USERCACHE_DEBUG("Get user objects done: %s (%x)", GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
	else
		LOG_USERCACHE_DEBUG("Get User Objects done. Returned objects %zu",
			lpmapLocalObjIds->size());
	return er;
}

ECRESULT ECCacheManager::I_AddUserObject(unsigned int ulUserId,
    const objectclass_t &ulClass, unsigned int ulCompanyId,
    const std::string &strExternId, const std::string &strSignature)
{
	ECsUserObject sData;

	scoped_rlock lock(m_hCacheMutex);

	if (OBJECTCLASS_ISTYPE(ulClass)) {
		LOG_USERCACHE_DEBUG("_Add user object. userid %d, class %d, companyid %d, externid \"%s\", signature \"%s\". error incomplete object",
			ulUserId, ulClass, ulCompanyId, bin2txt(strExternId).c_str(), bin2txt(strSignature).c_str());
		return erSuccess; // do not add incomplete data into the cache
	}

	LOG_USERCACHE_DEBUG("_Add user object. userid %d, class %d, companyid %d, externid \"%s\", signature \"%s\"",
		ulUserId, ulClass, ulCompanyId, bin2txt(strExternId).c_str(), bin2txt(strSignature).c_str());

	sData.ulClass = ulClass;
	sData.ulCompanyId = ulCompanyId;
	sData.strExternId = strExternId;
	sData.strSignature = strSignature;
	return m_UserObjectCache.AddCacheItem(ulUserId, std::move(sData));
}

ECRESULT ECCacheManager::I_GetUserObject(unsigned int ulUserId,
    objectclass_t *lpulClass, unsigned int *lpulCompanyId,
    std::string *lpstrExternId, std::string *lpstrSignature)
{
	ECsUserObject	*sData;
	scoped_rlock lock(m_hCacheMutex);

	auto er = m_UserObjectCache.GetCacheItem(ulUserId, &sData);
	if(er != erSuccess)
		return er;
	if(lpulClass)
		*lpulClass = sData->ulClass;

	if(lpulCompanyId)
		*lpulCompanyId = sData->ulCompanyId;

	if(lpstrExternId)
		*lpstrExternId = sData->strExternId;

	if(lpstrSignature)
		*lpstrSignature = sData->strSignature;
	return erSuccess;
}

ECRESULT ECCacheManager::I_DelUserObject(unsigned int ulUserId)
{
	scoped_rlock lock(m_hCacheMutex);

	// Remove the user
	return m_UserObjectCache.RemoveCacheItem(ulUserId);
}

ECRESULT ECCacheManager::I_AddUserObjectDetails(unsigned int ulUserId,
    const objectdetails_t &details)
{
	ECsUserObjectDetails sObjectDetails;

	scoped_rlock lock(m_hCacheMutex);
	LOG_USERCACHE_DEBUG("_Add user details. userid %d, %s", ulUserId, details.ToStr().c_str());
	sObjectDetails.sDetails = details;
	return m_UserObjectDetailsCache.AddCacheItem(ulUserId, std::move(sObjectDetails));
}

ECRESULT ECCacheManager::I_GetUserObjectDetails(unsigned int ulUserId, objectdetails_t *details)
{
	ECsUserObjectDetails *sObjectDetails;
	scoped_rlock lock(m_hCacheMutex);

	if (details == NULL)
		return KCERR_INVALID_PARAMETER;
	auto er = m_UserObjectDetailsCache.GetCacheItem(ulUserId, &sObjectDetails);
	if (er != erSuccess)
		return er;

	*details = sObjectDetails->sDetails;
	return erSuccess;
}

ECRESULT ECCacheManager::I_DelUserObjectDetails(unsigned int ulUserId)
{
	scoped_rlock lock(m_hCacheMutex);

	// Remove the user details
	return m_UserObjectDetailsCache.RemoveCacheItem(ulUserId);
}

ECRESULT ECCacheManager::I_AddUEIdObject(const std::string &strExternId,
    const objectclass_t &ulClass, unsigned int ulCompanyId,
    unsigned int ulUserId, const std::string &strSignature)
{
	ECsUEIdKey sKey;
	ECsUEIdObject sData;

	scoped_rlock lock(m_hCacheMutex);

	if (OBJECTCLASS_ISTYPE(ulClass))
		return erSuccess; // do not add incomplete data into the cache

	sData.ulCompanyId = ulCompanyId;
	sData.ulUserId = ulUserId;
	sData.strSignature = strSignature;

	sKey.ulClass = ulClass;
	sKey.strExternId = strExternId;
	return m_UEIdObjectCache.AddCacheItem(sKey, std::move(sData));
}

ECRESULT ECCacheManager::I_GetUEIdObject(const std::string &strExternId,
    objectclass_t ulClass, unsigned int *lpulCompanyId,
    unsigned int *lpulUserId, std::string *lpstrSignature)
{
	ECsUEIdKey		sKey;
	ECsUEIdObject	*sData;

	sKey.ulClass = ulClass;
	sKey.strExternId = strExternId;

	scoped_rlock lock(m_hCacheMutex);

	auto er = m_UEIdObjectCache.GetCacheItem(sKey, &sData);
	if(er != erSuccess)
		return er;
	if(lpulCompanyId)
		*lpulCompanyId = sData->ulCompanyId;

	if(lpulUserId)
		*lpulUserId = sData->ulUserId;

	if(lpstrSignature)
		*lpstrSignature = sData->strSignature;
	return erSuccess;
}

ECRESULT ECCacheManager::I_DelUEIdObject(const std::string &strExternId,
    objectclass_t ulClass)
{
	ECsUEIdKey	sKey;

	LOG_USERCACHE_DEBUG("Remove user externid \"%s\" class %d", bin2txt(strExternId).c_str(), ulClass);
	sKey.strExternId = strExternId;
	sKey.ulClass = ulClass;

	scoped_rlock lock(m_hCacheMutex);
	m_UEIdObjectCache.RemoveCacheItem(sKey);
	return erSuccess;
}

ECRESULT ECCacheManager::GetACLs(unsigned int ulObjId, struct rightsArray **lppRights)
{
	DB_RESULT lpResult;
    ECDatabase *lpDatabase = NULL;
	LOG_USERCACHE_DEBUG("Get ACLs for objectid %d", ulObjId);

	/* Try cache first */
	if (I_GetACLs(ulObjId, lppRights) == erSuccess)
		return erSuccess;

	/* Failed, get it from the cache */
	auto er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
	if(er != erSuccess)
		return er;
	auto strQuery = "SELECT id, type, rights FROM acl WHERE hierarchy_id=" + stringify(ulObjId);
    er = lpDatabase->DoSelect(strQuery, &lpResult);
    if(er != erSuccess)
		return er;

	auto ulRows = lpResult.get_num_rows();
	auto lpRights = s_alloc<rightsArray>(nullptr);
    if (ulRows > 0)
    {
	    lpRights->__size = ulRows;
		lpRights->__ptr = s_alloc<rights>(nullptr, ulRows);
		memset(lpRights->__ptr, 0, sizeof(struct rights) * ulRows);

		for (unsigned int i = 0; i < ulRows; ++i) {
			auto lpRow = lpResult.fetch_row();
			if(lpRow == NULL || lpRow[0] == NULL || lpRow[1] == NULL || lpRow[2] == NULL) {
				s_free(nullptr, lpRights->__ptr);
				s_free(nullptr, lpRights);
				ec_perror("ECCacheManager::GetACLs(): ROW or COLUMNS null", er);
				return KCERR_DATABASE_ERROR;
			}

			lpRights->__ptr[i].ulUserid = atoi(lpRow[0]);
			lpRights->__ptr[i].ulType = atoi(lpRow[1]);
			lpRights->__ptr[i].ulRights = atoi(lpRow[2]);

			LOG_USERCACHE_DEBUG(" Get ACLs result for objectid %d: userid %d, type %d, permissions %d", ulObjId, lpRights->__ptr[i].ulUserid, lpRights->__ptr[i].ulType, lpRights->__ptr[i].ulRights);
		}
	}
	else
	    memset(lpRights, 0, sizeof *lpRights);

	SetACLs(ulObjId, *lpRights);
    *lppRights = lpRights;
	return erSuccess;
}

ECRESULT ECCacheManager::I_GetACLs(unsigned int ulObjId, struct rightsArray **lppRights)
{
    ECsACLs *sACL;
	scoped_rlock lock(m_hCacheMutex);

	auto er = m_AclCache.GetCacheItem(ulObjId, &sACL);
	if(er != erSuccess)
		return er;

	auto lpRights = s_alloc<rightsArray>(nullptr);
    if (sACL->ulACLs > 0)
    {
        lpRights->__size = sACL->ulACLs;
		lpRights->__ptr = s_alloc<rights>(nullptr, sACL->ulACLs);
        memset(lpRights->__ptr, 0, sizeof(struct rights) * sACL->ulACLs);

        for (unsigned int i = 0; i < sACL->ulACLs; ++i) {
            lpRights->__ptr[i].ulType = sACL->aACL[i].ulType;
            lpRights->__ptr[i].ulRights = sACL->aACL[i].ulMask;
            lpRights->__ptr[i].ulUserid = sACL->aACL[i].ulUserId;

			LOG_USERCACHE_DEBUG("_Get ACLs result for objectid %d: userid %d, type %d, permissions %d", ulObjId, lpRights->__ptr[i].ulUserid, lpRights->__ptr[i].ulType, lpRights->__ptr[i].ulRights);
        }
    }
	else
		memset(lpRights, 0, sizeof *lpRights);

	*lppRights = lpRights;
	return erSuccess;
}

ECRESULT ECCacheManager::SetACLs(unsigned int ulObjId,
    const struct rightsArray &lpRights)
{
    ECsACLs sACLs;

	LOG_USERCACHE_DEBUG("Set ACLs for objectid %d", ulObjId);
	sACLs.ulACLs = lpRights.__size;
	sACLs.aACL.reset(new ECsACLs::ACL[lpRights.__size]);
	for (gsoap_size_t i = 0; i < lpRights.__size; ++i) {
		sACLs.aACL[i].ulType   = lpRights.__ptr[i].ulType;
		sACLs.aACL[i].ulMask   = lpRights.__ptr[i].ulRights;
		sACLs.aACL[i].ulUserId = lpRights.__ptr[i].ulUserid;
		LOG_USERCACHE_DEBUG("Set ACLs for objectid %d: userid %d, type %d, permissions %d",
			ulObjId, lpRights.__ptr[i].ulUserid,
			lpRights.__ptr[i].ulType, lpRights.__ptr[i].ulRights);
    }

	scoped_rlock lock(m_hCacheMutex);
	return m_AclCache.AddCacheItem(ulObjId, std::move(sACLs));
}

ECRESULT ECCacheManager::I_DelACLs(unsigned int ulObjId)
{
	scoped_rlock lock(m_hCacheMutex);
	LOG_USERCACHE_DEBUG("Remove ACLs for objectid %d", ulObjId);
	return m_AclCache.RemoveCacheItem(ulObjId);
}

ECRESULT ECCacheManager::GetQuota(unsigned int ulUserId, bool bIsDefaultQuota, quotadetails_t *quota)
{
	// Try cache first
	return I_GetQuota(ulUserId, bIsDefaultQuota, quota);
	// on error, ECSecurity will update the cache
}

ECRESULT ECCacheManager::SetQuota(unsigned int ulUserId, bool bIsDefaultQuota,
    const quotadetails_t &quota)
{
	ECsQuota	sQuota;
	sQuota.quota = quota;
	scoped_rlock lock(m_hCacheMutex);
	if (bIsDefaultQuota)
		return m_QuotaUserDefaultCache.AddCacheItem(ulUserId, std::move(sQuota));
	return m_QuotaCache.AddCacheItem(ulUserId, std::move(sQuota));
}

ECRESULT ECCacheManager::I_GetQuota(unsigned int ulUserId, bool bIsDefaultQuota, quotadetails_t *quota)
{
	ECRESULT er;
	ECsQuota	*sQuota;
	scoped_rlock lock(m_hCacheMutex);

	if (quota == NULL)
		return KCERR_INVALID_PARAMETER;
	if (bIsDefaultQuota)
		er = m_QuotaUserDefaultCache.GetCacheItem(ulUserId, &sQuota);
	else
		er = m_QuotaCache.GetCacheItem(ulUserId, &sQuota);
	if(er != erSuccess)
		return er;
	*quota = sQuota->quota;
	return erSuccess;
}

ECRESULT ECCacheManager::I_DelQuota(unsigned int ulUserId, bool bIsDefaultQuota)
{
	scoped_rlock lock(m_hCacheMutex);
	if (bIsDefaultQuota)
		return m_QuotaUserDefaultCache.RemoveCacheItem(ulUserId);
	return m_QuotaCache.RemoveCacheItem(ulUserId);
}

void ECCacheManager::update_extra_stats(ECStatsCollector &sc)
{
	auto f = [&](ECCacheStat &&s) {
		sc.setg("cache_" + s.name + "_items", "Cache " + s.name + " items", s.items);
		sc.setg("cache_" + s.name + "_size", "Cache " + s.name + " size", s.size);
		sc.setg("cache_" + s.name + "_maxsz", "Cache " + s.name + " maximum size", s.maxsize);
		sc.set("cache_" + s.name + "_req", "Cache " + s.name + " requests", s.req);
		/* Not quite clear about hit; looks like a counter, but is decremented sometimes */
		sc.setg("cache_" + s.name + "_hit", "Cache " + s.name + " hits", s.hit);
	};
	ulock_rec l_cache(m_hCacheMutex);
	f(m_AclCache.get_stats());
	f(m_QuotaCache.get_stats());
	f(m_QuotaUserDefaultCache.get_stats());
	f(m_UEIdObjectCache.get_stats());
	f(m_UserObjectCache.get_stats());
	f(m_UserObjectDetailsCache.get_stats());
	f(m_ServerDetailsCache.get_stats());
	l_cache.unlock();

	ulock_rec l_store(m_hCacheStoreMutex);
	f(m_StoresCache.get_stats());
	l_store.unlock();

	ulock_rec l_object(m_hCacheObjectMutex);
	f(m_ObjectsCache.get_stats());
	l_object.unlock();

	ulock_rec l_cell(m_hCacheCellsMutex);
	f(m_CellCache.get_stats());
	l_cell.unlock();

	ulock_rec l_prop(m_hCacheIndPropMutex);
	f(m_PropToObjectCache.get_stats());
	f(m_ObjectToPropCache.get_stats());
	l_prop.unlock();
}

ECRESULT ECCacheManager::GetObjectFlags(unsigned int ulObjId, unsigned int *ulFlags)
{
	return GetObject(ulObjId, NULL, NULL, ulFlags);
}

ECRESULT ECCacheManager::GetCell(const sObjectTableKey *lpsRowItem,
    unsigned int ulPropTag, struct propVal *lpDest, struct soap *soap,
    bool bComputed, bool truncate)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;
	scoped_rlock lock(m_hCacheCellsMutex);

    if (m_bCellCacheDisabled) {
        er = KCERR_NOT_FOUND;
        goto exit;
    }
	/* ignoring orderId for now */
	er = m_CellCache.GetCacheItem(lpsRowItem->ulObjId, &sCell);
	if(er != erSuccess)
	    goto exit;

    if (!sCell->GetPropVal(ulPropTag, lpDest, soap, truncate)) {
        if(!sCell->GetComplete() || bComputed) {
            // Object is not complete, and item is not in cache. We simply don't know anything about
            // the item, so return NOT_FOUND. Or, the item is complete but the requested property is computed, and therefore
            // not in the cache.
			m_CellCache.DecrementValidCount();
            er = KCERR_NOT_FOUND;
        } else {
            // Object is complete and property is not found; we know that the property does not exist
            // so return OK with a NOT_FOUND propvalue
			lpDest->ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_ERROR);
            lpDest->Value.ul = KCERR_NOT_FOUND;
            lpDest->__union = SOAP_UNION_propValData_ul;
        }
    }
exit:
	if (er != erSuccess)
		LOG_CELLCACHE_DEBUG("Get cell object %d tag 0x%08X item not found", lpsRowItem->ulObjId, ulPropTag);
	else
		LOG_CELLCACHE_DEBUG("Get cell object %d tag 0x%08X result found", lpsRowItem->ulObjId, ulPropTag);
	return er;
}

ECRESULT ECCacheManager::SetCell(const sObjectTableKey *lpsRowItem,
    unsigned int ulPropTag, const struct propVal *lpSrc)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;
	/* ignoring orderId for now */
	scoped_rlock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(lpsRowItem->ulObjId, &sCell) == erSuccess) {
        long long ulSize = sCell->GetSize();
        sCell->AddPropVal(ulPropTag, lpSrc);
        ulSize -= sCell->GetSize();
        // ulSize is positive if the cache shrank
        //m_ulCellSize -= ulSize;
		m_CellCache.AddToSize(-ulSize);
    } else {
        ECsCells sNewCell;
        sNewCell.AddPropVal(ulPropTag, lpSrc);
		er = m_CellCache.AddCacheItem(lpsRowItem->ulObjId, std::move(sNewCell));
    }
	if (er != erSuccess)
		LOG_CELLCACHE_DEBUG("Set cell object %d tag 0x%08X error 0x%08X", lpsRowItem->ulObjId, ulPropTag, er);
	else
		LOG_CELLCACHE_DEBUG("Set cell object %d tag 0x%08X", lpsRowItem->ulObjId, ulPropTag);
	return er;
}

ECRESULT ECCacheManager::SetComplete(unsigned int ulObjId)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;
	scoped_rlock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(ulObjId, &sCell) == erSuccess)
		sCell->SetComplete(true);
	else
		er = KCERR_NOT_FOUND;
	if (er != erSuccess)
		LOG_CELLCACHE_DEBUG("Set cell complete for object %d failed cell not found", ulObjId);
	else
		LOG_CELLCACHE_DEBUG("Set cell complete for object %d", ulObjId);
	return er;
}

ECRESULT ECCacheManager::GetComplete(unsigned int ulObjId, bool &complete)
{
	ECRESULT er = erSuccess;
	ECsCells *sCell;
	scoped_rlock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(ulObjId, &sCell) == erSuccess)
		complete = sCell->GetComplete();
	else
		er = KCERR_NOT_FOUND;
	if (er != erSuccess)
		LOG_CELLCACHE_DEBUG("Get complete for object %d failed cell not found", ulObjId);
	else
		LOG_CELLCACHE_DEBUG("Get complete for object %d", ulObjId);
	return er;
}

ECRESULT ECCacheManager::GetPropTags(unsigned int ulObjId, std::vector<unsigned int> &proptags)
{
	ECRESULT er = erSuccess;
	ECsCells *sCell;
	scoped_rlock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(ulObjId, &sCell) == erSuccess)
		proptags = sCell->GetPropTags();
	else
		er = KCERR_NOT_FOUND;
	if (er != erSuccess)
		LOG_CELLCACHE_DEBUG("get proptags for object %d failed cell not found", ulObjId);
	else
		LOG_CELLCACHE_DEBUG("get proptags complete for object %d", ulObjId);
	return er;
}

ECRESULT ECCacheManager::UpdateCell(unsigned int ulObjId, unsigned int ulPropTag, int lDelta)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;
	scoped_rlock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(ulObjId, &sCell) == erSuccess)
		sCell->UpdatePropVal(ulPropTag, lDelta);
	else
		er = KCERR_NOT_FOUND;
	if (er != erSuccess)
		LOG_CELLCACHE_DEBUG("Update cell object %d tag 0x%08X, delta %d failed cell not found", ulObjId, ulPropTag, lDelta);
	else
		LOG_CELLCACHE_DEBUG("Update cell object %d tag 0x%08X, delta %d", ulObjId, ulPropTag, lDelta);
	return er;
}

ECRESULT ECCacheManager::UpdateCell(unsigned int ulObjId, unsigned int ulPropTag, unsigned int ulMask, unsigned int ulValue)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;
	scoped_rlock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(ulObjId, &sCell) == erSuccess)
		sCell->UpdatePropVal(ulPropTag, ulMask, ulValue);
	else
		er = KCERR_NOT_FOUND;
	if (er != erSuccess)
		LOG_CELLCACHE_DEBUG("Update cell object %d tag 0x%08X, mask 0x%08X, value %d failed cell not found", ulObjId, ulPropTag, ulMask, ulValue);
	else
		LOG_CELLCACHE_DEBUG("Update cell object %d tag 0x%08X, mask 0x%08X, value %d", ulObjId, ulPropTag, ulMask, ulValue);
	return er;
}

ECRESULT ECCacheManager::I_DelCell(unsigned int ulObjId)
{
	scoped_rlock lock(m_hCacheCellsMutex);
	return m_CellCache.RemoveCacheItem(ulObjId);
}

ECRESULT ECCacheManager::GetServerDetails(const std::string &strServerId, serverdetails_t *lpsDetails)
{
	ECsServerDetails	*sEntry;
	scoped_rlock lock(m_hCacheMutex);
	ECRESULT er = m_ServerDetailsCache.GetCacheItem(strToLower(strServerId), &sEntry);
	if (er != erSuccess)
		return er;
	if (lpsDetails)
		*lpsDetails = sEntry->sDetails;
	return er;
}

ECRESULT ECCacheManager::SetServerDetails(const std::string &strServerId, const serverdetails_t &sDetails)
{
	ECsServerDetails	sEntry;
	sEntry.sDetails = sDetails;

	scoped_rlock lock(m_hCacheMutex);
	return m_ServerDetailsCache.AddCacheItem(strToLower(strServerId), std::move(sEntry));
}

ECRESULT ECCacheManager::RemoveIndexData(unsigned int ulObjId)
{
	ECsIndexObject	sObjectKeyLower, sObjectKeyUpper;
	std::list<ECMapObjectToProp::value_type> lstItems;

	// Get all records with specified hierarchyid and all tags (0 -> 0xffffffff)
	sObjectKeyLower.ulObjId = ulObjId;
	sObjectKeyLower.ulTag = 0;
	sObjectKeyUpper.ulObjId = ulObjId;
	sObjectKeyUpper.ulTag = 0xffffffff;

	scoped_rlock lock(m_hCacheIndPropMutex);
	auto er = m_ObjectToPropCache.GetCacheRange(sObjectKeyLower, sObjectKeyUpper, &lstItems);
	for (const auto &p : lstItems) {
		m_ObjectToPropCache.RemoveCacheItem(p.first);
		m_PropToObjectCache.RemoveCacheItem(p.second);
	}
	return er;
}

ECRESULT ECCacheManager::RemoveIndexData(unsigned int ulPropTag,
    unsigned int cbData, const unsigned char *lpData)
{
	ECsIndexProp	sObject;
	ECsIndexObject	*sObjectId;

	if (lpData == NULL || cbData == 0)
		return KCERR_INVALID_PARAMETER;

	LOG_CACHE_DEBUG("Remove indexdata proptag 0x%08X, data %s", ulPropTag, bin2hex(cbData, lpData).c_str());
	sObject.ulTag = PROP_ID(ulPropTag);
	sObject.cbData = cbData;
	sObject.lpData = const_cast<unsigned char *>(lpData); /* Cheap copy, set this item to nullptr before exiting */

	scoped_rlock lock(m_hCacheIndPropMutex);
        if(m_PropToObjectCache.GetCacheItem(sObject, &sObjectId) == erSuccess) {
            m_ObjectToPropCache.RemoveCacheItem(*sObjectId);
            m_PropToObjectCache.RemoveCacheItem(sObject);
        }
	// Make sure there's no delete when it goes out of scope
	sObject.lpData = NULL;
	return erSuccess;
}

ECRESULT ECCacheManager::RemoveIndexData(unsigned int ulPropTag, unsigned int ulObjId)
{
	ECsIndexObject	sObject;
	ECsIndexProp	*sObjectId;

	sObject.ulTag = PROP_ID(ulPropTag);
	sObject.ulObjId = ulObjId;

	LOG_CACHE_DEBUG("Remove index data proptag 0x%08X, objectid %d", ulPropTag, ulObjId);
	scoped_rlock lock(m_hCacheIndPropMutex);
       if(m_ObjectToPropCache.GetCacheItem(sObject, &sObjectId) == erSuccess) {
            m_PropToObjectCache.RemoveCacheItem(*sObjectId);
            m_ObjectToPropCache.RemoveCacheItem(sObject);
        }
	return erSuccess;
}

ECRESULT ECCacheManager::I_AddIndexData(const ECsIndexObject &lpObject,
    const ECsIndexProp &lpProp)
{
	scoped_rlock lock(m_hCacheIndPropMutex);

    // Remove any pre-existing references to this data
	RemoveIndexData(PROP_TAG(PT_UNSPECIFIED, lpObject.ulTag), lpObject.ulObjId);
	RemoveIndexData(PROP_TAG(PT_UNSPECIFIED, lpProp.ulTag), lpProp.cbData, lpProp.lpData);
	auto er = m_PropToObjectCache.AddCacheItem(lpProp, lpObject);
	if(er != erSuccess)
		return er;
	return m_ObjectToPropCache.AddCacheItem(lpObject, lpProp);
}

ECRESULT ECCacheManager::GetPropFromObject(unsigned int ulTag, unsigned int ulObjId, struct soap *soap, unsigned int* lpcbData, unsigned char** lppData)
{
	ECRESULT		er = erSuccess;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow = NULL;
	DB_LENGTHS		lpDBLenths = NULL;
	std::string		strQuery;
	ECDatabase*		lpDatabase = NULL;
	ECsIndexProp *sObject = NULL;
	ECsIndexObject	sObjectKey;
    ECsIndexProp sNewObject;

	sObjectKey.ulObjId = ulObjId;
	sObjectKey.ulTag = ulTag;

	LOG_CACHE_DEBUG("Get Prop From Object tag=0x%04X, objectid %d", ulTag, ulObjId);

	{
		scoped_rlock lock(m_hCacheIndPropMutex);
		er = m_ObjectToPropCache.GetCacheItem(sObjectKey, &sObject);

		if(er == erSuccess) {
			*lppData = s_alloc<unsigned char>(soap, sObject->cbData);
			*lpcbData = sObject->cbData;

			memcpy(*lppData, sObject->lpData, sObject->cbData);

			// All done
			LOG_CACHE_DEBUG("Get Prop From Object tag=0x%04X, objectid %d, data %s", ulTag, ulObjId, bin2hex(sObject->cbData, sObject->lpData).c_str());
			return erSuccess;
		}
	}

	// item not found, search in the database
	er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
	if(er != erSuccess)
		goto exit;
	// Get them from the database
	strQuery = "SELECT val_binary FROM indexedproperties WHERE tag="+stringify(ulTag)+" AND hierarchyid="+stringify(ulObjId) + " LIMIT 1";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;
	lpDBRow = lpDBResult.fetch_row();
	lpDBLenths = lpDBResult.fetch_row_lengths();
	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBLenths == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	sNewObject.SetValue(ulTag, (unsigned char*) lpDBRow[0], (unsigned int) lpDBLenths[0]);
	er = I_AddIndexData(sObjectKey, sNewObject);
	if(er != erSuccess)
		goto exit;
	sObject = &sNewObject;
	*lppData = s_alloc<unsigned char>(soap, sObject->cbData);
	*lpcbData = sObject->cbData;
	memcpy(*lppData, sObject->lpData, sObject->cbData);
exit:
	if (er != erSuccess || sObject == NULL)
		LOG_CACHE_DEBUG("Get Prop From Object tag=0x%04X, objectid %d, error 0x%08x", ulTag, ulObjId, er);
	else
	    LOG_CACHE_DEBUG("Get Prop From Object tag=0x%04X, objectid %d, data %s", ulTag, ulObjId, bin2hex(sObject->cbData, sObject->lpData).c_str());
	return er;
}

ECRESULT ECCacheManager::GetObjectFromProp(unsigned int ulTag, unsigned int cbData,
    const unsigned char *lpData, unsigned int *lpulObjId)
{
	ECRESULT		er = erSuccess;
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow = NULL;
	std::string		strQuery;
	ECDatabase*		lpDatabase = NULL;
    ECsIndexObject sNewIndexObject;
	ECsIndexProp	sObject;
	bool bCacheResult = false;

	if(lpData == NULL || lpulObjId == NULL || cbData == 0) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}
	if(QueryObjectFromProp(ulTag, cbData, lpData, lpulObjId) == erSuccess) {
		bCacheResult = true;
	    goto exit;
	}

	// Item not found, search in database
	er = m_lpDatabaseFactory->get_tls_db(&lpDatabase);
    if(er != erSuccess)
        goto exit;
    // Get them from the database
    strQuery = "SELECT hierarchyid FROM indexedproperties WHERE tag="+stringify(ulTag)+" AND val_binary="+ lpDatabase->EscapeBinary(lpData, cbData) + " LIMIT 1";
    er = lpDatabase->DoSelect(strQuery, &lpDBResult);
    if(er != erSuccess)
		goto exit;
	lpDBRow = lpDBResult.fetch_row();
    if(lpDBRow == NULL || lpDBRow[0] == NULL) {
        er = KCERR_NOT_FOUND;
        goto exit;
    }

    sNewIndexObject.ulTag = ulTag;
    sNewIndexObject.ulObjId = atoui(lpDBRow[0]);
	sObject.ulTag = ulTag;
	sObject.cbData = cbData;
	sObject.lpData = const_cast<unsigned char *>(lpData); /* Cheap copy, set this item to nullptr before exiting */
	er = I_AddIndexData(sNewIndexObject, sObject);
	if (er != erSuccess)
		goto exit;
	*lpulObjId = sNewIndexObject.ulObjId;
exit:
	sObject.lpData = NULL; // Remove reference
	if (er != erSuccess)
		LOG_CACHE_DEBUG("Get object from prop tag 0x%04X, data %s error 0x%08x", ulTag, bin2hex(cbData, lpData).c_str(), er);
	else
		LOG_CACHE_DEBUG("Get object from prop tag 0x%04X, data %s result [%s]: objectid %d", ulTag, bin2hex(cbData, lpData).c_str(), ((bCacheResult)?"C":"D"), *lpulObjId);
	return er;
}

ECRESULT ECCacheManager::QueryObjectFromProp(unsigned int ulTag, unsigned int cbData,
    const unsigned char *lpData, unsigned int *lpulObjId)
{
	ECsIndexProp	sObject;
	ECsIndexObject	*sIndexObject;

	if (lpData == nullptr || lpulObjId == nullptr || cbData == 0)
		return KCERR_INVALID_PARAMETER;

	sObject.ulTag = ulTag;
	sObject.cbData = cbData;
	sObject.lpData = const_cast<unsigned char *>(lpData); /* Cheap copy, set this item to nullptr before exiting */

	scoped_rlock lock(m_hCacheIndPropMutex);
	auto er = m_PropToObjectCache.GetCacheItem(sObject, &sIndexObject);
	if (er == erSuccess)
		*lpulObjId = sIndexObject->ulObjId;
	sObject.lpData = NULL;
    return er;
}

ECRESULT ECCacheManager::SetObjectProp(unsigned int ulTag, unsigned int cbData,
    const unsigned char *lpData, unsigned int ulObjId)
{
    ECsIndexObject sObject;
    ECsIndexProp sProp;

    sObject.ulTag = ulTag;
    sObject.ulObjId = ulObjId;

    sProp.SetValue(ulTag, lpData, cbData);
	auto er = I_AddIndexData(sObject, sProp);
	LOG_CACHE_DEBUG("Set object prop tag 0x%04X, data %s, objectid %d", ulTag, bin2hex(cbData, lpData).c_str(), ulObjId);
    return er;
}

ECRESULT ECCacheManager::GetEntryIdFromObject(unsigned int ulObjId, struct soap *soap, unsigned int ulFlags, entryId** lppEntryId)
{
	entryId*	lpEntryId = s_alloc<entryId>(soap);
	auto er = GetEntryIdFromObject(ulObjId, soap, ulFlags, lpEntryId);
	if (er != erSuccess) {
		s_free(nullptr, lpEntryId);
		return er;
	}
	// Flags already set by GetEntryIdFromObject(4args)
	*lppEntryId = lpEntryId;
	return erSuccess;
}

ECRESULT ECCacheManager::GetEntryIdFromObject(unsigned int ulObjId, struct soap *soap, unsigned int ulFlags, entryId* lpEntryId)
{
	auto er = GetPropFromObject(PROP_ID(PR_ENTRYID), ulObjId, soap, reinterpret_cast<unsigned int *>(&lpEntryId->__size), &lpEntryId->__ptr);
	if (er != erSuccess)
		return er;
	// Set flags in entryid
	static_assert(offsetof(EID, usFlags) == offsetof(EID_V0, usFlags),
		"usFlags member not at same position");
	auto d = reinterpret_cast<EID *>(lpEntryId->__ptr);
	if (lpEntryId->__size < 0 ||
	    static_cast<size_t>(lpEntryId->__size) <
	    offsetof(EID, usFlags) + sizeof(d->usFlags)) {
		ec_log_err("K-1572: %s: entryid has size %d; not enough for EID_V1.usFlags (%zu)",
			__func__, lpEntryId->__size, offsetof(EID, usFlags) + sizeof(d->usFlags));
		return MAPI_E_CORRUPT_DATA;
	}
	d->usFlags = ulFlags;
	return erSuccess;
}

ECRESULT ECCacheManager::GetObjectFromEntryId(const entryId *lpEntryId,
    unsigned int *lpulObjId)
{
	// Make sure flags is 0 when getting from db/cache
	if (lpEntryId == nullptr)
		ec_log_err("K-1575: null entryid passed to %s", __func__);
	EntryId eid(lpEntryId);
	try {
		eid.setFlags(0);
	} catch (const std::runtime_error &e) {
		ec_log_err("K-1573: eid.setFlags: %s", e.what());
		/*
		 * The subsequent functions will catch the too-small eid.size
		 * and return INVALID_PARAM appropriately.
		 */
	}
	return GetObjectFromProp(PROP_ID(PR_ENTRYID), eid.size(), eid, lpulObjId);
}

ECRESULT ECCacheManager::SetObjectEntryId(const entryId *lpEntryId,
    unsigned int ulObjId)
{
    // MAke sure flags is 0 when saving in DB
	if (lpEntryId == nullptr)
		ec_log_err("K-1576: null entryid passed to %s", __func__);
    EntryId eid(lpEntryId);
	try {
		eid.setFlags(0);
	} catch (const std::runtime_error &e) {
		ec_log_err("K-1574: eid.setFlags: %s", e.what());
		/* ignore exception - the following functions will catch the too-small eid.size */
	}
	return SetObjectProp(PROP_ID(PR_ENTRYID), eid.size(), eid, ulObjId);
}

/**
 * Convert entryid to database object id
 */
ECRESULT ECCacheManager::GetEntryListToObjectList(struct entryList *lpEntryList, ECListInt* lplObjectList)
{
	unsigned int	ulId = 0;
	bool			bPartialCompletion = false;

	if (lpEntryList == nullptr)
		return KCERR_INVALID_PARAMETER;

	for (unsigned int i = 0; i < lpEntryList->__size; ++i) {
		if(GetObjectFromEntryId(&lpEntryList->__ptr[i], &ulId) != erSuccess) {
			bPartialCompletion = true;
			continue; // Unknown entryid, next item
		}
		lplObjectList->emplace_back(ulId);
	}

	if(bPartialCompletion)
		return KCWARN_PARTIAL_COMPLETION;
	return erSuccess;
}

/**
 * Get list of indexed properties for the indexer
 *
 * This is not a read-through, the data must be set via SetIndexedProperties
 *
 * @param[out] map Map of property ID -> text name
 * @return Result
 */
ECRESULT ECCacheManager::GetExcludedIndexProperties(std::set<unsigned int>& set)
{
	scoped_lock lock(m_hExcludedIndexPropertiesMutex);
	if (m_setExcludedIndexProperties.empty())
		return KCERR_NOT_FOUND;
	set = m_setExcludedIndexProperties;
	return erSuccess;
}

/**
 * Set list of indexed properties for the indexer
 *
 * @param[in] map Map of property ID -> text name
 * @return result
 */
ECRESULT ECCacheManager::SetExcludedIndexProperties(const std::set<unsigned int> &set)
{
	scoped_lock lock(m_hExcludedIndexPropertiesMutex);
	m_setExcludedIndexProperties = set;
	return erSuccess;
}

void ECCacheManager::DisableCellCache()
{
	LOG_CELLCACHE_DEBUG("Disable cell cache");
	m_bCellCacheDisabled = true;
}

void ECCacheManager::EnableCellCache()
{
	LOG_CELLCACHE_DEBUG("Enable cell cache");
	m_bCellCacheDisabled = false;
}

ECsCells::ECsCells(const ECsCells &src)
{
	struct propVal val;
	for (const auto &p : src.mapPropVals) {
		CopyPropVal(const_cast<struct propVal *>(&p.second), &val);
		mapPropVals[p.first] = val;
	}
	m_bComplete = src.m_bComplete;
}

ECsCells::~ECsCells() {
	for (auto &p : mapPropVals)
		FreePropVal(&p.second, false);
}

ECsCells &ECsCells::operator=(const ECsCells &src)
{
	struct propVal val;
	for (auto &p : mapPropVals)
		FreePropVal(&p.second, false);
	mapPropVals.clear();
	for (const auto &p : src.mapPropVals) {
		CopyPropVal(const_cast<struct propVal *>(&p.second), &val);
		mapPropVals[p.first] = val;
	}
	m_bComplete = src.m_bComplete;
	return *this;
}

/* Add a property value for this object */
void ECsCells::AddPropVal(unsigned int ulPropTag, const struct propVal *lpPropVal)
{
	struct propVal val;
	ulPropTag = NormalizeDBPropTag(ulPropTag); /* Only cache PT_STRING8 */
	CopyPropVal(lpPropVal, &val, nullptr, false);
	val.ulPropTag = NormalizeDBPropTag(val.ulPropTag);
	auto res = mapPropVals.emplace(ulPropTag, val);
	if (!res.second) {
		FreePropVal(&res.first->second, false);
		res.first->second = val; /* reassign */
	}
}

/* get a property value for this object */
bool ECsCells::GetPropVal(unsigned int ulPropTag, struct propVal *lpPropVal,
    struct soap *soap, bool truncate) const
{
	auto i = mapPropVals.find(NormalizeDBPropTag(ulPropTag));
	if (i == mapPropVals.cend())
		return false;
	CopyPropVal(&i->second, lpPropVal, soap, truncate);
	if (NormalizeDBPropTag(ulPropTag) == lpPropVal->ulPropTag)
		/* Switch back to requested type (not on PT_ERROR of course) */
		lpPropVal->ulPropTag = ulPropTag;
	return true;
}

std::vector<unsigned int> ECsCells::GetPropTags() const
{
	std::vector<unsigned int> result;
	result.reserve(mapPropVals.size());
	for (const auto &p : mapPropVals)
		result.push_back(p.first);
	return result;
}

/* Updates a LONG type property */
void ECsCells::UpdatePropVal(unsigned int ulPropTag, int lDelta)
{
	if (PROP_TYPE(ulPropTag) != PT_LONG && PROP_TYPE(ulPropTag) != PT_LONGLONG)
		return;
	auto i = mapPropVals.find(ulPropTag);
	if (i == mapPropVals.cend())
		return;
	if (PROP_TYPE(i->second.ulPropTag) == PT_LONG)
		i->second.Value.ul += lDelta;
	if (PROP_TYPE(i->second.ulPropTag) == PT_LONGLONG)
		i->second.Value.li += lDelta;
}

/* Updates a LONG type property */
void ECsCells::UpdatePropVal(unsigned int ulPropTag, unsigned int ulMask,
    unsigned int ulValue)
{
	if (PROP_TYPE(ulPropTag) != PT_LONG && PROP_TYPE(ulPropTag) != PT_LONGLONG)
		return;
	auto i = mapPropVals.find(ulPropTag);
	if (i == mapPropVals.cend())
		return;
	if (PROP_TYPE(i->second.ulPropTag) == PT_LONG) {
		i->second.Value.ul &= ~ulMask;
		i->second.Value.ul |= ulValue & ulMask;
	}
	if (PROP_TYPE(i->second.ulPropTag) == PT_LONGLONG) {
		i->second.Value.li &= ~ulMask;
		i->second.Value.li |= ulValue & ulMask;
	}
}

size_t ECsCells::GetSize() const
{
	size_t ulSize = 0;

	for (const auto &p : mapPropVals) {
		switch (p.second.__union) {
		case SOAP_UNION_propValData_lpszA:
			ulSize += p.second.Value.lpszA != nullptr ? strlen(p.second.Value.lpszA) : 0;
			break;
		case SOAP_UNION_propValData_bin:
			ulSize += p.second.Value.bin != nullptr ? p.second.Value.bin->__size + sizeof(p.second.Value.bin[0]) : 0;
			break;
		case SOAP_UNION_propValData_hilo:
			ulSize += sizeof(p.second.Value.hilo[0]);
			break;
		default:
			break;
		}
		ulSize += sizeof(std::map<unsigned int, struct propVal>::value_type);
	}
	ulSize += sizeof(*this);
	return ulSize;
}

/* @todo check this function, is this really ok? */
bool ECsIndexProp::operator<(const ECsIndexProp &other) const noexcept
{
	if (cbData < other.cbData)
		return true;
	if (cbData != other.cbData)
		return false;
	if (lpData == nullptr && other.lpData)
		return true;
	else if (lpData != nullptr && other.lpData == nullptr)
		return false;
	else if (lpData == nullptr && other.lpData == nullptr)
		return false;
	int c = memcmp(lpData, other.lpData, cbData);
	if (c < 0)
		return true;
	else if (c == 0 && ulTag < other.ulTag)
		return true;
	return false;
}

bool ECsIndexProp::operator==(const ECsIndexProp &other) const noexcept
{
	if (cbData != other.cbData || ulTag != other.ulTag)
		return false;
	if (lpData == other.lpData)
		return true;
	if (lpData == nullptr || other.lpData == nullptr)
		return false;
	if (memcmp(lpData, other.lpData, cbData) == 0)
		return true;
	return false;
}

void ECsIndexProp::SetValue(unsigned int tag, const unsigned char *data, unsigned int z)
{
	if (data == nullptr || z == 0)
		return;
	Free();
	lpData = new unsigned char[z];
	cbData = z;
	ulTag = tag;
	memcpy(lpData, data, z);
}

void ECsIndexProp::Copy(const ECsIndexProp &src, ECsIndexProp &dst)
{
	if (src.lpData != nullptr && src.cbData > 0) {
		dst.lpData = new unsigned char[src.cbData];
		memcpy(dst.lpData, src.lpData, src.cbData);
		dst.cbData = src.cbData;
	} else {
		dst.lpData = nullptr;
		dst.cbData = 0;
	}
	dst.ulTag = src.ulTag;
}

} /* namespace */
