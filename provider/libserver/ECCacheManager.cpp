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
#include <exception>

#include <mapidefs.h>
#include <mapitags.h>

#include "ECDatabaseMySQL.h"
#include "ECSessionManager.h"
#include "ECDatabaseUtils.h"

#include "ECCacheManager.h"

#include "ECMAPI.h"
#include <kopano/stringutil.h>
#include "ECGenericObjectTable.h"
#include <kopano/threadutil.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#include <algorithm>

#define LOG_CACHE_DEBUG(_msg, ...) \
	ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_CACHE, "cache: " _msg, ##__VA_ARGS__)
#define LOG_USERCACHE_DEBUG(_msg, ...) \
	ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_USERCACHE, "usercache: " _msg, ##__VA_ARGS__)
#define LOG_CELLCACHE_DEBUG(_msg, ...) \
	ec_log(EC_LOGLEVEL_DEBUG | EC_LOGLEVEL_CACHE, "cellcache: " _msg, ##__VA_ARGS__)

// Specialization for ECsACL
template<>
unsigned int GetCacheAdditionalSize(const ECsACLs &val) {
	return val.ulACLs * sizeof(val.aACL[0]);
}

template<>
unsigned int GetCacheAdditionalSize(const ECsIndexProp &val) {
	return val.cbData;
}

// Specialization for ECsCell
template<>
unsigned int GetCacheAdditionalSize(const ECsCells &val) {
	return val.GetSize();
}

template<>
unsigned int GetCacheAdditionalSize(const std::string &val) {
	return MEMORY_USAGE_STRING(val);
}

template<>
unsigned int GetCacheAdditionalSize(const ECsUEIdKey &val) {
	return MEMORY_USAGE_STRING(val.strExternId);
}

ECCacheManager::ECCacheManager(ECConfig *lpConfig, ECDatabaseFactory *lpDatabaseFactory)
: m_QuotaCache("quota", atoi(lpConfig->GetSetting("cache_quota_size")), atoi(lpConfig->GetSetting("cache_quota_lifetime")) * 60)
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
	pthread_mutexattr_t mattr;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);

	pthread_mutex_init(&m_hCacheMutex, &mattr);
	pthread_mutex_init(&m_hCacheCellsMutex, &mattr);
	pthread_mutex_init(&m_hCacheIndPropMutex, &mattr);
	
	pthread_mutex_init(&m_hExcludedIndexPropertiesMutex, NULL);

	pthread_mutexattr_destroy(&mattr);

 	/* Initialization of constants */
 	m_lpDatabaseFactory = lpDatabaseFactory;
 	m_bCellCacheDisabled = false;

	/* Initial cleaning/initialization of cache */
	PurgeCache(PURGE_CACHE_ALL);

#ifdef HAVE_SPARSEHASH
	// This is a requirement of sparse_hash_map. Since the value 0 is unused, we can use it as 'deleted' key
	m_mapObjects.set_deleted_key(-1);
	m_mapStores.set_deleted_key(-1);
	m_mapACLs.set_deleted_key(-1);
	m_mapQuota.set_deleted_key(-1);
	m_mapUserObject.set_deleted_key(-1);
	m_mapUserObjectDetails.set_deleted_key(-1);
	m_mapCells.set_deleted_key(-1);
#endif

}

ECCacheManager::~ECCacheManager()
{
	PurgeCache(PURGE_CACHE_ALL);
	pthread_mutex_destroy(&m_hCacheIndPropMutex);
	pthread_mutex_destroy(&m_hCacheMutex);
	pthread_mutex_destroy(&m_hCacheCellsMutex);
	pthread_mutex_destroy(&m_hExcludedIndexPropertiesMutex);
}

ECRESULT ECCacheManager::PurgeCache(unsigned int ulFlags)
{
	ECRESULT er = erSuccess;

	LOG_CACHE_DEBUG("Purge cache, flags 0x%08X", ulFlags);

	// cache mutex items
	pthread_mutex_lock(&m_hCacheMutex);

	if (ulFlags & PURGE_CACHE_QUOTA)
		m_QuotaCache.ClearCache();
	if (ulFlags & PURGE_CACHE_QUOTADEFAULT)
		m_QuotaUserDefaultCache.ClearCache();
	if (ulFlags & PURGE_CACHE_OBJECTS)
		m_ObjectsCache.ClearCache();
	if (ulFlags & PURGE_CACHE_STORES)
		m_StoresCache.ClearCache();
	if (ulFlags & PURGE_CACHE_ACL)
		m_AclCache.ClearCache();

	pthread_mutex_unlock(&m_hCacheMutex);
	
	// Cell cache mutex
	pthread_mutex_lock(&m_hCacheCellsMutex);

	if(ulFlags & PURGE_CACHE_CELL)
    	m_CellCache.ClearCache();

	pthread_mutex_unlock(&m_hCacheCellsMutex);

	// Indexed properties mutex
	pthread_mutex_lock(&m_hCacheIndPropMutex);
	
	if (ulFlags & PURGE_CACHE_INDEX1)
		m_PropToObjectCache.ClearCache();
	if (ulFlags & PURGE_CACHE_INDEX2)
		m_ObjectToPropCache.ClearCache();
	
	pthread_mutex_unlock(&m_hCacheIndPropMutex);

	pthread_mutex_lock(&m_hExcludedIndexPropertiesMutex);
	if (ulFlags & PURGE_CACHE_INDEXEDPROPERTIES)
		m_setExcludedIndexProperties.clear();
	pthread_mutex_unlock(&m_hExcludedIndexPropertiesMutex);

	pthread_mutex_lock(&m_hCacheMutex);

	if (ulFlags & PURGE_CACHE_USEROBJECT)
		m_UserObjectCache.ClearCache();
	if (ulFlags & PURGE_CACHE_EXTERNID)
		m_UEIdObjectCache.ClearCache();
	if (ulFlags & PURGE_CACHE_USERDETAILS)
		m_UserObjectDetailsCache.ClearCache();
	if (ulFlags & PURGE_CACHE_SERVER)
		m_ServerDetailsCache.ClearCache();

	pthread_mutex_unlock(&m_hCacheMutex);

	return er;
}

ECRESULT ECCacheManager::Update(unsigned int ulType, unsigned int ulObjId)
{
	ECRESULT		er = erSuccess;

	switch(ulType)
	{
		case fnevObjectModified:
			LOG_CACHE_DEBUG("Remove cache ACLs, cell, objects for object %d", ulObjId);
			_DelACLs(ulObjId);
			_DelCell(ulObjId);
			_DelObject(ulObjId);
			break;
		case fnevObjectDeleted:
			LOG_CACHE_DEBUG("Remove cache ACLs, cell, objects and store for object %d", ulObjId);
			_DelObject(ulObjId);
			_DelStore(ulObjId);
			_DelACLs(ulObjId);
			_DelCell(ulObjId);
			break;
		case fnevObjectMoved:
			LOG_CACHE_DEBUG("Remove cache cell, objects and store for object %d", ulObjId);
			_DelStore(ulObjId);
			_DelObject(ulObjId);
			_DelCell(ulObjId);
			break;
		default:
			//Do nothing
			LOG_CACHE_DEBUG("Update cache, action type %d, objectid %d", ulType, ulObjId);
			break;
	}

	return er;
}

ECRESULT ECCacheManager::UpdateUser(unsigned int ulUserId)
{
	std::string strExternId;
	objectclass_t ulClass;

	LOG_USERCACHE_DEBUG("Remove user id %d from the cache", ulUserId);

	if (_GetUserObject(ulUserId, &ulClass, NULL, &strExternId, NULL) == erSuccess)
		_DelUEIdObject(strExternId, ulClass);

	_DelUserObject(ulUserId);
	_DelUserObjectDetails(ulUserId);
	_DelQuota(ulUserId, false);
	_DelQuota(ulUserId, true);

	return erSuccess;
}

ECRESULT ECCacheManager::_GetObject(unsigned int ulObjId, unsigned int *ulParent, unsigned int *ulOwner, unsigned int *ulFlags, unsigned int *ulType)
{
	ECRESULT er;
	ECsObjects	*sObject;
	scoped_lock lock(m_hCacheMutex);

	er = m_ObjectsCache.GetCacheItem(ulObjId, &sObject);
	if(er != erSuccess)
		return er;

	ASSERT((sObject->ulType != MAPI_FOLDER && (sObject->ulFlags & ~(MAPI_ASSOCIATED | MSGFLAG_DELETED)) == 0) || sObject->ulType == MAPI_FOLDER);

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
	ECRESULT		er = erSuccess;
	ECsObjects		sObjects;

	if(ulParent == 0 || ulObjId == 0 || ulOwner == 0)
		return 1;

	ASSERT((ulType != MAPI_FOLDER && (ulFlags & ~(MAPI_ASSOCIATED | MSGFLAG_DELETED)) == 0) || ulType == MAPI_FOLDER);

	sObjects.ulParent	= ulParent;
	sObjects.ulOwner	= ulOwner;
	sObjects.ulFlags	= ulFlags;
	sObjects.ulType		= ulType;

	scoped_lock lock(m_hCacheMutex);
	er = m_ObjectsCache.AddCacheItem(ulObjId, sObjects);

	LOG_CACHE_DEBUG("Set cache object id %d, parent %d, owner %d, flags %d, type %d", ulObjId, ulParent, ulOwner, ulFlags, ulType);
	return er;
}

ECRESULT ECCacheManager::_DelObject(unsigned int ulObjId)
{
	ECRESULT		er = erSuccess;
	scoped_lock		lock(m_hCacheMutex);

	er = m_ObjectsCache.RemoveCacheItem(ulObjId);

	return er;
}

ECRESULT ECCacheManager::_GetStore(unsigned int ulObjId, unsigned int *ulStore, GUID *lpGuid, unsigned int *lpulType)
{
	ECRESULT er;
	ECsStores	*sStores;

	scoped_lock lock(m_hCacheMutex);

	er = m_StoresCache.GetCacheItem(ulObjId, &sStores);
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
	ECRESULT		er = erSuccess;
	ECsStores		sStores;
	sStores.ulStore = ulStore;
	sStores.guidStore = *lpGuid;
	sStores.ulType = ulType;

	scoped_lock lock(m_hCacheMutex);

	er = m_StoresCache.AddCacheItem(ulObjId, sStores);

	LOG_CACHE_DEBUG("Set store cache id %d, store %d, type %d, guid %s", ulObjId, ulStore, ulType, ((lpGuid)?bin2hex(sizeof(GUID), (const unsigned char*)lpGuid).c_str(): "NULL"));
	return er;
}

ECRESULT ECCacheManager::_DelStore(unsigned int ulObjId)
{
	ECRESULT		er = erSuccess;
	scoped_lock		lock(m_hCacheMutex);

	er = m_StoresCache.RemoveCacheItem(ulObjId);

	return er;
}

ECRESULT ECCacheManager::GetOwner(unsigned int ulObjId, unsigned int *ulOwner)
{
	ECRESULT	er = erSuccess;
	bool bCacheResult = false;

	if(_GetObject(ulObjId, NULL, ulOwner, NULL, NULL) == erSuccess) {
		bCacheResult = true;
		goto exit;
	}

	er = GetObject(ulObjId, NULL, ulOwner, NULL);

exit:
	if (er) {
		LOG_CACHE_DEBUG("Get Owner for id %d error 0x%08X", ulObjId, er);
	} else {
		LOG_CACHE_DEBUG("Get Owner for id %d result [%s]: owner %d", ulObjId, ((bCacheResult)?"C":"D"), *ulOwner);
	}
	return er;
}

ECRESULT ECCacheManager::GetParent(unsigned int ulObjId, unsigned int *lpulParent)
{
	ECRESULT er;
	unsigned int ulParent = 0;

	er = GetObject(ulObjId, &ulParent, NULL, NULL);

	if(er != erSuccess)
		return er;
	if (ulParent == CACHE_NO_PARENT)
		return KCERR_NOT_FOUND;
	*lpulParent = ulParent;
	return erSuccess;
}

ECRESULT ECCacheManager::QueryParent(unsigned int ulObjId, unsigned int *lpulParent)
{
    ECRESULT er = erSuccess;

    er = _GetObject(ulObjId, lpulParent, NULL, NULL, NULL);
    
    return er;
}

// Get the parent of the specified object
ECRESULT ECCacheManager::GetObject(unsigned int ulObjId, unsigned int *lpulParent, unsigned int *lpulOwner, unsigned int *lpulFlags, unsigned int *lpulType)
{
	ECRESULT	er = erSuccess;
	DB_RESULT	lpDBResult = NULL;
	DB_ROW		lpDBRow = NULL;
	std::string strQuery;
	ECDatabase	*lpDatabase = NULL;
	unsigned int	ulParent = 0, ulOwner = 0, ulFlags = 0, ulType = 0;
	bool bCacheResult = false;

	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);

	if(er != erSuccess)
		goto exit;

	// first check the cache if the item exists
	if(_GetObject(ulObjId, &ulParent, &ulOwner, &ulFlags, &ulType) == erSuccess) {
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

	strQuery = "SELECT hierarchy.parent, hierarchy.owner, hierarchy.flags, hierarchy.type FROM hierarchy WHERE hierarchy.id = " + stringify(ulObjId);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	lpDBRow = lpDatabase->FetchRow(lpDBResult);

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
	if (lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	if (er != erSuccess) {
		LOG_CACHE_DEBUG("Get object id %d error 0x%08x", ulObjId, er);
	} else {
		LOG_CACHE_DEBUG("Get object id %d result [%s]: parent %d owner %d flags %d type %d", ulObjId, ((bCacheResult)?"C":"D"), ulParent, ulOwner, ulFlags, ulType);
	}

	return er;

}

ECRESULT ECCacheManager::GetObjects(const std::list<sObjectTableKey> &lstObjects,
    std::map<sObjectTableKey, ECsObjects> &mapObjects)
{
	ECRESULT	er = erSuccess;
	DB_RESULT	lpDBResult = NULL;
	DB_ROW		lpDBRow = NULL;
	std::string strQuery;
	ECDatabase	*lpDatabase = NULL;
	unsigned int	ulObjId = 0;
	sObjectTableKey key;
	ECsObjects  *lpsObject = NULL;
	ECsObjects	sObject;
	std::list<sObjectTableKey>::const_iterator i;
	std::set<sObjectTableKey> setUncached;
	std::set<sObjectTableKey>::const_iterator j;

	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);

	if(er != erSuccess)
		goto exit;
		
    {
        // Get everything from the cache that we can
    	scoped_lock lock(m_hCacheMutex);

        for (i = lstObjects.begin(); i != lstObjects.end(); ++i) {
            if(m_ObjectsCache.GetCacheItem(i->ulObjId, &lpsObject) == erSuccess) {
                mapObjects[*i] = *lpsObject;
            } else {
                setUncached.insert(*i);
            }
        }
    }

    if(!setUncached.empty()) {
        // Get uncached items from SQL
        strQuery = "SELECT id, parent, owner, flags, type FROM hierarchy WHERE id IN(";
        
        for (j = setUncached.begin(); j != setUncached.end(); ++j) {
            strQuery += stringify(j->ulObjId);
            strQuery += ",";
        }
        
        strQuery.resize(strQuery.size()-1);
        strQuery += ")";
        
        er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if (er != erSuccess)
            goto exit;
            
        while((lpDBRow = lpDatabase->FetchRow(lpDBResult))) {
            if(!lpDBRow[0] || !lpDBRow[1] || !lpDBRow[2] || !lpDBRow[3])
                continue;
                
            ulObjId = atoui(lpDBRow[0]);
                
            sObject.ulParent = atoui(lpDBRow[1]);
            sObject.ulOwner = atoui(lpDBRow[2]);
            sObject.ulFlags = atoui(lpDBRow[3]);
            sObject.ulType = atoui(lpDBRow[4]);
            
            key.ulObjId = ulObjId;
            key.ulOrderId = 0;
            
            mapObjects[key] = sObject;
        }
    }
    
exit:
	if (lpDBResult != NULL)
		lpDatabase->FreeResult(lpDBResult);
    
	if (er)	 {
		LOG_CACHE_DEBUG("Get object ids error 0x%08x", er);
	} else {
		LOG_CACHE_DEBUG("Get object ids total ids %lu from disk %lu", lstObjects.size(), setUncached.size());
	}
    return er;
}

ECRESULT ECCacheManager::GetObjectsFromProp(unsigned int ulTag,
    const std::vector<unsigned int> &cbdata,
    const std::vector<unsigned char *> &lpdata,
    std::map<ECsIndexProp, unsigned int> &mapObjects)
{
	ECRESULT er = erSuccess;
	ECDatabase *lpDatabase = NULL;
	std::string strQuery;
	DB_RESULT lpDBResult = NULL;
	DB_ROW lpDBRow = NULL;
	DB_LENGTHS lpDBLen = NULL;
	ECsIndexProp sObject;
	unsigned int objid;
	std::vector<size_t> uncached;

	for (size_t i = 0; i < lpdata.size(); ++i) {
		if (QueryObjectFromProp(ulTag, cbdata[i], lpdata[i], &objid) == erSuccess) {
			sObject.SetValue(PROP_ID(ulTag), lpdata[i], cbdata[i]);
			mapObjects[sObject] = objid;
		} else {
			uncached.push_back(i);
		}
	}

	if (!uncached.empty()) {
		er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
		if (er != erSuccess)
			goto exit;

		strQuery = "SELECT hierarchyid, val_binary FROM indexedproperties FORCE INDEX(bin) WHERE tag="+stringify(ulTag)+" AND val_binary IN(";
		for (size_t j = 0; j < uncached.size(); ++j) {
			strQuery += lpDatabase->EscapeBinary(lpdata[j], cbdata[j]);
			strQuery += ",";
		}
		strQuery.resize(strQuery.size() - 1);
		strQuery += ")";
		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if (er != erSuccess)
			goto exit;

		while ((lpDBRow = lpDatabase->FetchRow(lpDBResult)) != NULL) {
			lpDBLen = lpDatabase->FetchRowLengths(lpDBResult);
			sObject.SetValue(PROP_ID(ulTag), reinterpret_cast<unsigned char *>(lpDBRow[1]), lpDBLen[1]);
			mapObjects[sObject] = atoui(lpDBRow[0]);
		}
	}
 exit:
	sObject.lpData = NULL;
	if (lpDBResult != NULL)
		lpDatabase->FreeResult(lpDBResult);
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
	ECRESULT	er = erSuccess;
	DB_RESULT	lpDBResult = NULL;
	DB_ROW		lpDBRow = NULL;
	std::string strQuery;
	ECDatabase	*lpDatabase = NULL;
	unsigned int ulSubObjId = 0;
	unsigned int ulStore = 0;
	unsigned int ulType = 0;
	GUID guid;
	bool bCacheResult = false;
	
	if(maxdepth <= 0)
	    return KCERR_NOT_FOUND;

	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);

	if(er != erSuccess)
		goto exit;

	// first check the cache if we already know the store for this object
	if(_GetStore(ulObjId, &ulStore, &guid, &ulType) == erSuccess) {
		bCacheResult = true;
		goto found;
	}

    // Get our parent folder
	if(GetParent(ulObjId, &ulSubObjId) != erSuccess) {
	    // No parent, this must be the top-level item, get the store data from here
    	strQuery = "SELECT hierarchy_id, guid, type FROM stores WHERE hierarchy_id = " + stringify(ulObjId);
    	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
    	if(er != erSuccess)
	    	goto exit;

    	if(lpDatabase->GetNumRows(lpDBResult) < 1) {
    		er = KCERR_NOT_FOUND;
    		goto exit;
    	}

    	lpDBRow = lpDatabase->FetchRow(lpDBResult);

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
	if(lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	if (er) {
		LOG_CACHE_DEBUG("Get store and type %d error 0x%08x", ulObjId, er);
	} else {
		LOG_CACHE_DEBUG("Get store and type %d result [%s]: store %d, type %d, guid %s", ulObjId, ((bCacheResult)?"C":"D"), ulStore, ulType, bin2hex(sizeof(GUID), (const unsigned char*)&guid).c_str());
	}

	return er;
}

ECRESULT ECCacheManager::GetUserObject(unsigned int ulUserId, objectid_t *lpExternId, unsigned int *lpulCompanyId, std::string *lpstrSignature)
{
	ECRESULT	er = erSuccess;
	DB_RESULT	lpDBResult = NULL;
	DB_ROW		lpDBRow = NULL;
	DB_LENGTHS	lpDBLen = NULL;
	std::string	strQuery;
	ECDatabase	*lpDatabase = NULL;
	objectclass_t ulClass;
	unsigned int ulCompanyId;
	std::string externid;
	std::string signature;
	bool bCacheResult = false;

	// first check the cache if we already know the external id for this user
	if (_GetUserObject(ulUserId, &ulClass, lpulCompanyId, &externid, lpstrSignature) == erSuccess) {
		if (lpExternId) {
			lpExternId->id = externid;
			lpExternId->objclass = ulClass;
		}
		bCacheResult = true;
		goto exit;
	}

	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->DoSelect("SELECT externid, objectclass, signature, company FROM users "
							  "WHERE id=" + stringify(ulUserId), &lpDBResult);
	if (er != erSuccess) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECCacheManager::GetUserObject(): NULL in columns");
		goto exit;
	}

	lpDBRow = lpDatabase->FetchRow(lpDBResult);
	lpDBLen = lpDatabase->FetchRowLengths(lpDBResult);

	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	ulClass = (objectclass_t)atoui(lpDBRow[1]);
	ulCompanyId = atoui(lpDBRow[3]);

	externid.assign(lpDBRow[0], lpDBLen[0]);
	signature.assign(lpDBRow[2], lpDBLen[2]);

	// insert the item into the cache
	_AddUserObject(ulUserId, ulClass, ulCompanyId, externid, signature);

	if (lpExternId) {
		lpExternId->id = externid;
		lpExternId->objclass = ulClass;
	}

	if(lpulCompanyId)
		*lpulCompanyId = ulCompanyId;

	if(lpstrSignature)
		*lpstrSignature = signature;

exit:
	if(lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	if (er) {
		LOG_USERCACHE_DEBUG("Get user object for user %d error 0x%08x", ulUserId, er);
	} else {
		LOG_USERCACHE_DEBUG("Get user object for user %d result [%s]: externid '%s', class %d, companyid %d, signature '%s'", ulUserId, ((bCacheResult)?"C":"D"), bin2hex(externid).c_str(), ulClass, ((lpulCompanyId)?*lpulCompanyId:-1), ((lpstrSignature)?bin2hex(*lpstrSignature).c_str():"-") );
	}

	return er;
}

ECRESULT ECCacheManager::GetUserDetails(unsigned int ulUserId, objectdetails_t *details)
{
	ECRESULT er = erSuccess;

	er = _GetUserObjectDetails(ulUserId, details);

	// on error, ECUserManagement will update the cache
	
	if (er != erSuccess) {
		LOG_USERCACHE_DEBUG("Get user details for userid %d not found, error 0x%08x", ulUserId, er);
	} else {
		LOG_USERCACHE_DEBUG("Get user details for userid %d result: %s", ulUserId, details->ToStr().c_str() );
	}

	return er;
}

ECRESULT ECCacheManager::SetUserDetails(unsigned int ulUserId,
    const objectdetails_t *details)
{
	ECRESULT er = erSuccess;

	er = _AddUserObjectDetails(ulUserId, details);

	return er;
}

ECRESULT ECCacheManager::GetUserObject(const objectid_t &sExternId, unsigned int *lpulUserId, unsigned int *lpulCompanyId, std::string *lpstrSignature)
{
	ECRESULT	er = erSuccess;
	DB_RESULT	lpDBResult = NULL;
	DB_ROW		lpDBRow = NULL;
	DB_LENGTHS	lpDBLen = NULL;
	std::string	strQuery;
	ECDatabase	*lpDatabase = NULL;
	unsigned int ulCompanyId;
	unsigned int ulUserId;
	std::string signature;
	objectclass_t objclass = sExternId.objclass;
	bool bCacheResult = false;

	if (sExternId.id.empty()) {
		er = KCERR_DATABASE_ERROR;
		//ASSERT(FALSE);
		goto exit;
	}

	// first check the cache if we already know the external id for this user
	if (_GetUEIdObject(sExternId.id, sExternId.objclass, lpulCompanyId, lpulUserId, lpstrSignature) == erSuccess) {
		bCacheResult = true;
		goto exit;
	}

	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		goto exit;

	strQuery =
		"SELECT id, signature, company, objectclass FROM users "
		"WHERE externid='" + lpDatabase->Escape(sExternId.id) + "' "
			"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", sExternId.objclass);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECCacheManager::GetUserObject(): query failed %x", er);
		goto exit;
	}

	// TODO: check, should return 1 answer

	lpDBRow = lpDatabase->FetchRow(lpDBResult);
	lpDBLen = lpDatabase->FetchRowLengths(lpDBResult);

	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	ulUserId = atoui(lpDBRow[0]);
	signature.assign(lpDBRow[1], lpDBLen[1]);
	ulCompanyId = atoui(lpDBRow[2]);

	// possibly update objectclass from database, to add the correct info in the cache
	if (OBJECTCLASS_ISTYPE(sExternId.objclass))
		objclass = (objectclass_t)atoi(lpDBRow[3]);

	// insert the item into the cache
	_AddUEIdObject(sExternId.id, objclass, ulCompanyId, ulUserId, signature);

	if(lpulCompanyId)
		*lpulCompanyId = ulCompanyId;

	if(lpulUserId)
		*lpulUserId = ulUserId;

	if(lpstrSignature)
		*lpstrSignature = signature;

exit:
	if(lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	if (er) {
		LOG_USERCACHE_DEBUG("Get user object done. error 0x%08X", er);
	} else {
		LOG_USERCACHE_DEBUG("Get user object from externid '%s', class %d result [%s]: company %d, userid %d, signature '%s'" , bin2hex(sExternId.id).c_str(), sExternId.objclass, ((bCacheResult)?"C":"D"), ((lpulCompanyId)?*lpulCompanyId:-1), ((lpulUserId)?*lpulUserId:-1), ((lpstrSignature)?bin2hex(*lpstrSignature).c_str():"-") );
	}

	return er;
}

ECRESULT ECCacheManager::GetUserObjects(const list<objectid_t> &lstExternObjIds, map<objectid_t, unsigned int> *lpmapLocalObjIds)
{
	ECRESULT er = erSuccess;
	DB_RESULT lpDBResult = NULL;
	DB_ROW lpDBRow = NULL;
	DB_LENGTHS lpDBLen = NULL;
	std::string strQuery;
	ECDatabase *lpDatabase = NULL;
	list<objectid_t> lstExternIds;
	list<objectid_t>::const_iterator iter;
	objectid_t sExternId;
	string strSignature;
	unsigned int ulLocalId = 0;
	unsigned int ulCompanyId;

	// Collect as many objects from cache as possible,
	// everything we couldn't find must be collected from the database
	LOG_USERCACHE_DEBUG("Get User Objects. requested objects %lu", lstExternObjIds.size() );

	for (iter = lstExternObjIds.begin(); iter != lstExternObjIds.end(); ++iter) {
		LOG_USERCACHE_DEBUG(" Get user objects from externid '%s', class %d" , bin2hex(iter->id).c_str(), iter->objclass);
		if (_GetUEIdObject(iter->id, iter->objclass, NULL, &ulLocalId, NULL) == erSuccess)
			lpmapLocalObjIds->insert(make_pair(*iter, ulLocalId)); // object was found in cache
		else
			lstExternIds.push_back(*iter); // object was not found in cache
	}

	// Check if all objects have been collected from the cache
	if (lstExternIds.empty())
		goto exit;

	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
	if (er != erSuccess)
		goto exit;

	strQuery = "SELECT id, externid, objectclass, signature, company FROM users WHERE ";
	for (iter = lstExternIds.begin(); iter != lstExternIds.end(); ++iter) {
		if (iter != lstExternIds.begin())
			strQuery += " OR ";
		strQuery +=
			"(" + OBJECTCLASS_COMPARE_SQL("objectclass", iter->objclass) +
			" AND externid = '" + lpDatabase->Escape(iter->id) + "')";
	}

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess) {
		ec_log_err("ECCacheManager::GetUserObjects() query failed %x", er);
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}

	while (TRUE) {
		lpDBRow = lpDatabase->FetchRow(lpDBResult);
		lpDBLen = lpDatabase->FetchRowLengths(lpDBResult);

		if (lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[3] == NULL || lpDBRow[4] == NULL)
			break;

		ulLocalId = atoi(lpDBRow[0]);
		sExternId.id.assign(lpDBRow[1], lpDBLen[1]);
		sExternId.objclass = (objectclass_t)atoi(lpDBRow[2]);
		strSignature.assign(lpDBRow[3], lpDBLen[3]);
		ulCompanyId = atoi(lpDBRow[4]);

		lpmapLocalObjIds->insert(make_pair(sExternId, ulLocalId));

		_AddUEIdObject(sExternId.id, sExternId.objclass, ulCompanyId, ulLocalId, strSignature);

		LOG_USERCACHE_DEBUG(" Get user objects result company %d, userid %d, signature '%s'", ulCompanyId, ulLocalId, bin2hex(strSignature).c_str());
	}

	// From this point you can have less items in lpmapLocalObjIds than requested in lstExternObjIds

exit:
	if(lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	if (er) {
		LOG_USERCACHE_DEBUG("Get User Objects done. Error 0x%08X", er);
	} else {
		LOG_USERCACHE_DEBUG("Get User Objects done. Returned objects %lu", lpmapLocalObjIds->size());
	}

	return er;
}

ECRESULT ECCacheManager::_AddUserObject(unsigned int ulUserId,
    const objectclass_t &ulClass, unsigned int ulCompanyId,
    const std::string &strExternId, const std::string &strSignature)
{
	ECsUserObject sData;

	scoped_lock lock(m_hCacheMutex);

	if (OBJECTCLASS_ISTYPE(ulClass)) {
		LOG_USERCACHE_DEBUG("_Add user object. userid %d, class %d, companyid %d, externid '%s', signature '%s'. error incomplete object", ulUserId, ulClass, ulCompanyId, bin2hex(strExternId).c_str(), bin2hex(strSignature).c_str());
		return erSuccess; // do not add incomplete data into the cache
	}

	LOG_USERCACHE_DEBUG("_Add user object. userid %d, class %d, companyid %d, externid '%s', signature '%s'", ulUserId, ulClass, ulCompanyId, bin2hex(strExternId).c_str(), bin2hex(strSignature).c_str());

	sData.ulClass = ulClass;
	sData.ulCompanyId = ulCompanyId;
	sData.strExternId = strExternId;
	sData.strSignature = strSignature;
	return m_UserObjectCache.AddCacheItem(ulUserId, sData);
}


ECRESULT ECCacheManager::_GetUserObject(unsigned int ulUserId, objectclass_t* lpulClass, unsigned int *lpulCompanyId,
										std::string* lpstrExternId, std::string* lpstrSignature)
{
	ECRESULT er;
	ECsUserObject	*sData;

	scoped_lock lock(m_hCacheMutex);

	er = m_UserObjectCache.GetCacheItem(ulUserId, &sData);
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

ECRESULT ECCacheManager::_DelUserObject(unsigned int ulUserId)
{
	ECRESULT			er = erSuccess;
	scoped_lock			lock(m_hCacheMutex);

	// Remove the user
	er = m_UserObjectCache.RemoveCacheItem(ulUserId);

	return er;
}

ECRESULT ECCacheManager::_AddUserObjectDetails(unsigned int ulUserId,
    const objectdetails_t *details)
{
	ECsUserObjectDetails sObjectDetails;

	scoped_lock lock(m_hCacheMutex);

	if (details == NULL)
		return KCERR_INVALID_PARAMETER;

	LOG_USERCACHE_DEBUG("_Add user details. userid %d, %s", ulUserId, details->ToStr().c_str() );

	sObjectDetails.sDetails = *details;
	return m_UserObjectDetailsCache.AddCacheItem(ulUserId, sObjectDetails);
}

ECRESULT ECCacheManager::_GetUserObjectDetails(unsigned int ulUserId, objectdetails_t *details)
{
	ECRESULT er;
	ECsUserObjectDetails *sObjectDetails;

	scoped_lock lock(m_hCacheMutex);

	if (details == NULL)
		return KCERR_INVALID_PARAMETER;
	er = m_UserObjectDetailsCache.GetCacheItem(ulUserId, &sObjectDetails);
	if (er != erSuccess)
		return er;

	*details = sObjectDetails->sDetails; 
	return erSuccess;
}

ECRESULT ECCacheManager::_DelUserObjectDetails(unsigned int ulUserId)
{
	ECRESULT			er = erSuccess;
	scoped_lock			lock(m_hCacheMutex);

	// Remove the user details
	er = m_UserObjectDetailsCache.RemoveCacheItem(ulUserId);

	return er;
}

ECRESULT ECCacheManager::_AddUEIdObject(const std::string &strExternId,
    const objectclass_t &ulClass, unsigned int ulCompanyId,
    unsigned int ulUserId, const std::string &strSignature)
{
	ECsUEIdKey sKey;
	ECsUEIdObject sData;

	scoped_lock lock(m_hCacheMutex);

	if (OBJECTCLASS_ISTYPE(ulClass))
		return erSuccess; // do not add incomplete data into the cache

	sData.ulCompanyId = ulCompanyId;
	sData.ulUserId = ulUserId;
	sData.strSignature = strSignature;

	sKey.ulClass = ulClass;
	sKey.strExternId = strExternId;
	return m_UEIdObjectCache.AddCacheItem(sKey, sData);
}

ECRESULT ECCacheManager::_GetUEIdObject(const std::string &strExternId,
    objectclass_t ulClass, unsigned int *lpulCompanyId,
    unsigned int *lpulUserId, std::string *lpstrSignature)
{
	ECRESULT er;
	ECsUEIdKey		sKey;
	ECsUEIdObject	*sData;

	sKey.ulClass = ulClass;
	sKey.strExternId = strExternId;

	scoped_lock lock(m_hCacheMutex);

	er = m_UEIdObjectCache.GetCacheItem(sKey, &sData);
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

ECRESULT ECCacheManager::_DelUEIdObject(const std::string &strExternId,
    objectclass_t ulClass)
{
	ECRESULT	er = erSuccess;
	ECsUEIdKey	sKey;

	LOG_USERCACHE_DEBUG("Remove user externid '%s' class %d", bin2hex(strExternId).c_str(), ulClass);

	// Remove the user
	sKey.strExternId = strExternId;
	sKey.ulClass = ulClass;

	scoped_lock lock(m_hCacheMutex);
	m_UEIdObjectCache.RemoveCacheItem(sKey);

	return er;
}

ECRESULT ECCacheManager::GetACLs(unsigned int ulObjId, struct rightsArray **lppRights)
{
    ECRESULT er = erSuccess;
    DB_RESULT	lpResult = NULL;
    DB_ROW		lpRow = NULL;
    ECDatabase *lpDatabase = NULL;
    std::string strQuery;
    struct rightsArray *lpRights = NULL;
    unsigned int ulRows = 0;

	LOG_USERCACHE_DEBUG("Get ACLs for objectid %d", ulObjId);

	/* Try cache first */
	if (_GetACLs(ulObjId, lppRights) == erSuccess)
		goto exit;

	/* Failed, get it from the cache */
	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);

	if(er != erSuccess)
		goto exit;

    strQuery = "SELECT id, type, rights FROM acl WHERE hierarchy_id=" + stringify(ulObjId);

    er = lpDatabase->DoSelect(strQuery, &lpResult);
    if(er != erSuccess)
        goto exit;

    ulRows = lpDatabase->GetNumRows(lpResult);

    lpRights = new struct rightsArray;
    if (ulRows > 0)
    {
	    lpRights->__size = ulRows;
		lpRights->__ptr = new struct rights [ulRows];
		memset(lpRights->__ptr, 0, sizeof(struct rights) * ulRows);

		for (unsigned int i = 0; i < ulRows; ++i) {
			lpRow = lpDatabase->FetchRow(lpResult);

			if(lpRow == NULL || lpRow[0] == NULL || lpRow[1] == NULL || lpRow[2] == NULL) {
				delete[] lpRights->__ptr;
				delete lpRights;
				er = KCERR_DATABASE_ERROR;
				ec_log_err("ECCacheManager::GetACLs(): ROW or COLUMNS null %x", er);
				goto exit;
			}

			lpRights->__ptr[i].ulUserid = atoi(lpRow[0]);
			lpRights->__ptr[i].ulType = atoi(lpRow[1]);
			lpRights->__ptr[i].ulRights = atoi(lpRow[2]);

			LOG_USERCACHE_DEBUG(" Get ACLs result for objectid %d: userid %d, type %d, permissions %d", ulObjId, lpRights->__ptr[i].ulUserid, lpRights->__ptr[i].ulType, lpRights->__ptr[i].ulRights);
		}
	}
	else
	    memset(lpRights, 0, sizeof *lpRights);

    SetACLs(ulObjId, lpRights);

    *lppRights = lpRights;

exit:
    if(lpResult)
        lpDatabase->FreeResult(lpResult);

    return er;
}

ECRESULT ECCacheManager::_GetACLs(unsigned int ulObjId, struct rightsArray **lppRights)
{
	ECRESULT er;
    ECsACLs *sACL;
    struct rightsArray *lpRights = NULL;

	scoped_lock lock(m_hCacheMutex);

	er = m_AclCache.GetCacheItem(ulObjId, &sACL);
	if(er != erSuccess)
		return er;

    lpRights = new struct rightsArray;
    if (sACL->ulACLs > 0)
    {
        lpRights->__size = sACL->ulACLs;
        lpRights->__ptr = new struct rights [sACL->ulACLs];
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
    const struct rightsArray *lpRights)
{
    ECsACLs sACLs;
    ECRESULT er = erSuccess;

	LOG_USERCACHE_DEBUG("Set ACLs for objectid %d", ulObjId);

    sACLs.ulACLs = lpRights->__size;
    sACLs.aACL = new ECsACLs::ACL [lpRights->__size];

    for (gsoap_size_t i = 0; i < lpRights->__size; ++i) {
        sACLs.aACL[i].ulType = lpRights->__ptr[i].ulType;
        sACLs.aACL[i].ulMask = lpRights->__ptr[i].ulRights;
        sACLs.aACL[i].ulUserId = lpRights->__ptr[i].ulUserid;

		LOG_USERCACHE_DEBUG("Set ACLs for objectid %d: userid %d, type %d, permissions %d", ulObjId, lpRights->__ptr[i].ulUserid, lpRights->__ptr[i].ulType, lpRights->__ptr[i].ulRights);
    }

	scoped_lock lock(m_hCacheMutex);
	er = m_AclCache.AddCacheItem(ulObjId, sACLs);

	return er;
}

ECRESULT ECCacheManager::_DelACLs(unsigned int ulObjId)
{
    ECRESULT er = erSuccess;
	scoped_lock lock(m_hCacheMutex);
	
	LOG_USERCACHE_DEBUG("Remove ACLs for objectid %d", ulObjId);

	er = m_AclCache.RemoveCacheItem(ulObjId);

    return er;
}

ECRESULT ECCacheManager::GetQuota(unsigned int ulUserId, bool bIsDefaultQuota, quotadetails_t *quota)
{
	ECRESULT er = erSuccess;

	// Try cache first
	er = _GetQuota(ulUserId, bIsDefaultQuota, quota);

	// on error, ECSecurity will update the cache

	return er;
}

ECRESULT ECCacheManager::SetQuota(unsigned int ulUserId, bool bIsDefaultQuota,
    const quotadetails_t &quota)
{
	ECRESULT er = erSuccess;
	ECsQuota	sQuota;

	sQuota.quota = quota;

	scoped_lock lock(m_hCacheMutex);

	if (bIsDefaultQuota)
		er = m_QuotaUserDefaultCache.AddCacheItem(ulUserId, sQuota);
	else
		er = m_QuotaCache.AddCacheItem(ulUserId, sQuota);

	return er;
}

ECRESULT ECCacheManager::_GetQuota(unsigned int ulUserId, bool bIsDefaultQuota, quotadetails_t *quota)
{
	ECRESULT er;
	ECsQuota	*sQuota;

	scoped_lock lock(m_hCacheMutex);

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

ECRESULT ECCacheManager::_DelQuota(unsigned int ulUserId, bool bIsDefaultQuota)
{
	ECRESULT er = erSuccess;
	scoped_lock lock(m_hCacheMutex);

	if (bIsDefaultQuota)
		er = m_QuotaUserDefaultCache.RemoveCacheItem(ulUserId);
	else
		er = m_QuotaCache.RemoveCacheItem(ulUserId);

	return er;
}

void ECCacheManager::ForEachCacheItem(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj)
{
	pthread_mutex_lock(&m_hCacheMutex);

	m_ObjectsCache.RequestStats(callback, obj);
	m_StoresCache.RequestStats(callback, obj);
	m_AclCache.RequestStats(callback, obj);
	m_QuotaCache.RequestStats(callback, obj);
	m_QuotaUserDefaultCache.RequestStats( callback, obj);
	m_UEIdObjectCache.RequestStats(callback, obj);
	m_UserObjectCache.RequestStats(callback, obj);
	m_UserObjectDetailsCache.RequestStats(callback, obj);
	m_ServerDetailsCache.RequestStats(callback, obj);
	
	pthread_mutex_unlock(&m_hCacheMutex);


	pthread_mutex_lock(&m_hCacheCellsMutex);

	m_CellCache.RequestStats(callback, obj);

	pthread_mutex_unlock(&m_hCacheCellsMutex);


	pthread_mutex_lock(&m_hCacheIndPropMutex);

	m_PropToObjectCache.RequestStats(callback, obj);
	m_ObjectToPropCache.RequestStats(callback, obj);

	pthread_mutex_unlock(&m_hCacheIndPropMutex);
}

ECRESULT ECCacheManager::DumpStats()
{
	ec_log_info("Dumping cache stats:");

	pthread_mutex_lock(&m_hCacheMutex);
	m_ObjectsCache.DumpStats();
	m_StoresCache.DumpStats();
	m_AclCache.DumpStats();
	m_QuotaCache.DumpStats();
	m_QuotaUserDefaultCache.DumpStats();
	m_UEIdObjectCache.DumpStats();
	m_UserObjectCache.DumpStats();
	m_UserObjectDetailsCache.DumpStats();
	m_ServerDetailsCache.DumpStats();
	pthread_mutex_unlock(&m_hCacheMutex);


	pthread_mutex_lock(&m_hCacheCellsMutex);
	m_CellCache.DumpStats();
	pthread_mutex_unlock(&m_hCacheCellsMutex);


	pthread_mutex_lock(&m_hCacheIndPropMutex);
	m_PropToObjectCache.DumpStats();
	m_ObjectToPropCache.DumpStats();
	pthread_mutex_unlock(&m_hCacheIndPropMutex);

	return erSuccess;
}

ECRESULT ECCacheManager::GetObjectFlags(unsigned int ulObjId, unsigned int *ulFlags)
{
	return GetObject(ulObjId, NULL, NULL, ulFlags);
}

ECRESULT ECCacheManager::GetCell(const sObjectTableKey *lpsRowItem,
    unsigned int ulPropTag, struct propVal *lpDest, struct soap *soap,
    bool bComputed)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;

	scoped_lock lock(m_hCacheCellsMutex);

    if (m_bCellCacheDisabled) {
        er = KCERR_NOT_FOUND;
        goto exit;
    }

    // only support caching order id 0 (non-multi-valued)
	if(lpsRowItem->ulOrderId != 0) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	er = m_CellCache.GetCacheItem(lpsRowItem->ulObjId, &sCell);
	if(er != erSuccess)
	    goto exit;

    if(!sCell->GetPropVal(ulPropTag, lpDest, soap)) {
        if(!sCell->GetComplete() || bComputed) {
            // Object is not complete, and item is not in cache. We simply don't know anything about
            // the item, so return NOT_FOUND. Or, the item is complete but the requested property is computed, and therefore
            // not in the cache.
			m_CellCache.DecrementValidCount();
            er = KCERR_NOT_FOUND;
            goto exit;
        } else {
            // Object is complete and property is not found; we know that the property does not exist
            // so return OK with a NOT_FOUND propvalue
            lpDest->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(ulPropTag));
            lpDest->Value.ul = KCERR_NOT_FOUND;
            lpDest->__union = SOAP_UNION_propValData_ul;
        }
    }

exit:
	if (er) {
		LOG_CELLCACHE_DEBUG("Get cell object %d tag 0x%08X item not found", lpsRowItem->ulObjId, ulPropTag);
	} else {
		LOG_CELLCACHE_DEBUG("Get cell object %d tag 0x%08X result found", lpsRowItem->ulObjId, ulPropTag);
	}
    return er;
}

ECRESULT ECCacheManager::SetCell(const sObjectTableKey *lpsRowItem,
    unsigned int ulPropTag, const struct propVal *lpSrc)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;

	if (lpsRowItem->ulOrderId != 0)
		return KCERR_NOT_FOUND;

	scoped_lock lock(m_hCacheCellsMutex);

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
        
		er = m_CellCache.AddCacheItem(lpsRowItem->ulObjId, sNewCell);
    	if(er != erSuccess)
    	    goto exit;
    }

exit:
	if (er) {
		LOG_CELLCACHE_DEBUG("Set cell object %d tag 0x%08X error 0x%08X", lpsRowItem->ulObjId, ulPropTag, er);
	} else {
		LOG_CELLCACHE_DEBUG("Set cell object %d tag 0x%08X", lpsRowItem->ulObjId, ulPropTag);
	}
    return er;
}

ECRESULT ECCacheManager::SetComplete(unsigned int ulObjId)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;
    
	scoped_lock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(ulObjId, &sCell) == erSuccess) {
        sCell->SetComplete(true);
    } else {
        er = KCERR_NOT_FOUND;
        goto exit;
    }

exit:
	if (er) {
		LOG_CELLCACHE_DEBUG("Set cell complete for object %d failed cell not found", ulObjId);
	} else {
		LOG_CELLCACHE_DEBUG("Set cell complete for object %d", ulObjId);
	}

    return er;
}

ECRESULT ECCacheManager::UpdateCell(unsigned int ulObjId, unsigned int ulPropTag, int lDelta)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;
    
	scoped_lock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(ulObjId, &sCell) == erSuccess) {
        sCell->UpdatePropVal(ulPropTag, lDelta);
    } else {
        er = KCERR_NOT_FOUND;
        goto exit;
    }

exit:
	if (er) {
		LOG_CELLCACHE_DEBUG("Update cell object %d tag 0x%08X, delta %d failed cell not found", ulObjId, ulPropTag, lDelta);
	} else {
		LOG_CELLCACHE_DEBUG("Update cell object %d tag 0x%08X, delta %d", ulObjId, ulPropTag, lDelta);
	}
    return er;
}

ECRESULT ECCacheManager::UpdateCell(unsigned int ulObjId, unsigned int ulPropTag, unsigned int ulMask, unsigned int ulValue)
{
    ECRESULT er = erSuccess;
    ECsCells *sCell;
    
	scoped_lock lock(m_hCacheCellsMutex);

	if (m_CellCache.GetCacheItem(ulObjId, &sCell) == erSuccess) {
        sCell->UpdatePropVal(ulPropTag, ulMask, ulValue);
    } else {
        er = KCERR_NOT_FOUND;
        goto exit;
    }

exit:
	if (er) {
		LOG_CELLCACHE_DEBUG("Update cell object %d tag 0x%08X, mask 0x%08X, value %d failed cell not found", ulObjId, ulPropTag, ulMask, ulValue);
	} else {
		LOG_CELLCACHE_DEBUG("Update cell object %d tag 0x%08X, mask 0x%08X, value %d", ulObjId, ulPropTag, ulMask, ulValue);
	}
    return er;
}

ECRESULT ECCacheManager::_DelCell(unsigned int ulObjId)
{
    ECRESULT er = erSuccess;
	scoped_lock lock(m_hCacheCellsMutex);
    
	er = m_CellCache.RemoveCacheItem(ulObjId);

	return er;
}

ECRESULT ECCacheManager::GetServerDetails(const std::string &strServerId, serverdetails_t *lpsDetails)
{
	ECRESULT er;
	ECsServerDetails	*sEntry;
	std::string			strServerIdLc;

	// make the key lowercase
	strServerIdLc.reserve(strServerId.size());
	std::transform(strServerId.begin(), strServerId.end(), std::back_inserter(strServerIdLc), ::tolower);

	scoped_lock lock(m_hCacheMutex);

	er = m_ServerDetailsCache.GetCacheItem(strServerIdLc, &sEntry);
	if (er != erSuccess)
		return er;
	if (lpsDetails)
		*lpsDetails = sEntry->sDetails;
	return er;
}

ECRESULT ECCacheManager::SetServerDetails(const std::string &strServerId, const serverdetails_t &sDetails)
{
	ECsServerDetails	sEntry;
	std::string			strServerIdLc;

	// make the key lowercase
	strServerIdLc.reserve(strServerId.size());
	std::transform(strServerId.begin(), strServerId.end(), std::back_inserter(strServerIdLc), ::tolower);

	sEntry.sDetails = sDetails;

	scoped_lock lock(m_hCacheMutex);
	return m_ServerDetailsCache.AddCacheItem(strServerIdLc, sEntry);
}

ECRESULT ECCacheManager::RemoveIndexData(unsigned int ulObjId)
{
	ECRESULT				er = erSuccess;
	ECsIndexObject	sObjectKeyLower, sObjectKeyUpper;
	ECMapPropToObject::const_iterator iterPropToObj;

	std::list<ECMapObjectToProp::value_type> lstItems;
	std::list<ECMapObjectToProp::value_type>::const_iterator iItem;

	// Get all records with specified hierarchyid and all tags (0 -> 0xffffffff)
	sObjectKeyLower.ulObjId = ulObjId;
	sObjectKeyLower.ulTag = 0;
	sObjectKeyUpper.ulObjId = ulObjId;
	sObjectKeyUpper.ulTag = 0xffffffff;

	scoped_lock lock(m_hCacheIndPropMutex);

	er = m_ObjectToPropCache.GetCacheRange(sObjectKeyLower, sObjectKeyUpper, &lstItems);
	for (iItem = lstItems.begin(); iItem != lstItems.end(); ++iItem) {
		m_ObjectToPropCache.RemoveCacheItem(iItem->first);
		m_PropToObjectCache.RemoveCacheItem(iItem->second);
	}

	return er;
}


ECRESULT ECCacheManager::RemoveIndexData(unsigned int ulPropTag, unsigned int cbData, unsigned char *lpData)
{
	ECsIndexProp	sObject;
	ECsIndexObject	*sObjectId;

	if (lpData == NULL || cbData == 0)
		return KCERR_INVALID_PARAMETER;

	LOG_CACHE_DEBUG("Remove indexdata proptag 0x%08X, data %s", ulPropTag, bin2hex(cbData, lpData).c_str());

	sObject.ulTag = PROP_ID(ulPropTag);
	sObject.cbData = cbData;
	sObject.lpData = lpData; // Cheap copy, Set this item on NULL before you exit

	{
        scoped_lock lock(m_hCacheIndPropMutex);

        if(m_PropToObjectCache.GetCacheItem(sObject, &sObjectId) == erSuccess) {
            m_ObjectToPropCache.RemoveCacheItem(*sObjectId);
            m_PropToObjectCache.RemoveCacheItem(sObject);
        }
	}

    // Make sure there's no delete when it goes out of scope	
	sObject.lpData = NULL;
	return erSuccess;
}

ECRESULT ECCacheManager::RemoveIndexData(unsigned int ulPropTag, unsigned int ulObjId)
{
	ECRESULT				er = erSuccess;
	ECsIndexObject	sObject;
	ECsIndexProp	*sObjectId;

	sObject.ulTag = PROP_ID(ulPropTag);
	sObject.ulObjId = ulObjId;

	LOG_CACHE_DEBUG("Remove index data proptag 0x%08X, objectid %d", ulPropTag, ulObjId);

	{
        scoped_lock lock(m_hCacheIndPropMutex);

        if(m_ObjectToPropCache.GetCacheItem(sObject, &sObjectId) == erSuccess) {
            m_PropToObjectCache.RemoveCacheItem(*sObjectId);
            m_ObjectToPropCache.RemoveCacheItem(sObject);
        }
	}

	return er;
}

ECRESULT ECCacheManager::_AddIndexData(const ECsIndexObject *lpObject,
    const ECsIndexProp *lpProp)
{
	ECRESULT er;
	scoped_lock lock(m_hCacheIndPropMutex);

    // Remove any pre-existing references to this data
    RemoveIndexData(PROP_TAG(PT_UNSPECIFIED, lpObject->ulTag), lpObject->ulObjId);
    RemoveIndexData(PROP_TAG(PT_UNSPECIFIED, lpProp->ulTag), lpProp->cbData, lpProp->lpData);
    
	er = m_PropToObjectCache.AddCacheItem(*lpProp, *lpObject);
	if(er != erSuccess)
		return er;
	er = m_ObjectToPropCache.AddCacheItem(*lpObject, *lpProp);
	if(er != erSuccess)
		return er;
	return erSuccess;
}

ECRESULT ECCacheManager::GetPropFromObject(unsigned int ulTag, unsigned int ulObjId, struct soap *soap, unsigned int* lpcbData, unsigned char** lppData)
{
	ECRESULT		er = erSuccess;
	DB_RESULT		lpDBResult = NULL;
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
		scoped_lock lock(m_hCacheIndPropMutex);
		er = m_ObjectToPropCache.GetCacheItem(sObjectKey, &sObject);
		
		if(er == erSuccess) {
			*lppData = s_alloc<unsigned char>(soap, sObject->cbData);
			*lpcbData = sObject->cbData;

			memcpy(*lppData, sObject->lpData, sObject->cbData);
			
			// All done
			goto exit;
		}
	}

	// item not found, search in the database
	er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
	if(er != erSuccess)
		goto exit;

	// Get them from the database
	strQuery = "SELECT val_binary FROM indexedproperties FORCE INDEX(PRIMARY) WHERE tag="+stringify(ulTag)+" AND hierarchyid="+stringify(ulObjId);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	lpDBRow = lpDatabase->FetchRow(lpDBResult);
	lpDBLenths = lpDatabase->FetchRowLengths(lpDBResult);
	if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBLenths == NULL) {
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	sNewObject.SetValue(ulTag, (unsigned char*) lpDBRow[0], (unsigned int) lpDBLenths[0]);

	er = _AddIndexData(&sObjectKey, &sNewObject);
	if(er != erSuccess)
		goto exit;

	sObject = &sNewObject;

	*lppData = s_alloc<unsigned char>(soap, sObject->cbData);
	*lpcbData = sObject->cbData;

	memcpy(*lppData, sObject->lpData, sObject->cbData);

exit:
	if (lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	if (er != erSuccess || sObject == NULL) {
		LOG_CACHE_DEBUG("Get Prop From Object tag=0x%04X, objectid %d, error 0x%08x", ulTag, ulObjId, er);
	} else {
	    LOG_CACHE_DEBUG("Get Prop From Object tag=0x%04X, objectid %d, data %s", ulTag, ulObjId, bin2hex(sObject->cbData, sObject->lpData).c_str());
	}

	return er;
}

ECRESULT ECCacheManager::GetObjectFromProp(unsigned int ulTag, unsigned int cbData, unsigned char* lpData, unsigned int* lpulObjId)
{
	ECRESULT		er = erSuccess;
	DB_RESULT		lpDBResult = NULL;
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
    er = GetThreadLocalDatabase(this->m_lpDatabaseFactory, &lpDatabase);
    if(er != erSuccess)
        goto exit;

    // Get them from the database
    strQuery = "SELECT hierarchyid FROM indexedproperties FORCE INDEX(bin) WHERE tag="+stringify(ulTag)+" AND val_binary="+ lpDatabase->EscapeBinary(lpData, cbData);
    er = lpDatabase->DoSelect(strQuery, &lpDBResult);
    if(er != erSuccess)
        goto exit;

    lpDBRow = lpDatabase->FetchRow(lpDBResult);
    if(lpDBRow == NULL || lpDBRow[0] == NULL) {
        er = KCERR_NOT_FOUND;
        goto exit;
    }

    sNewIndexObject.ulTag = ulTag;
    sNewIndexObject.ulObjId = atoui(lpDBRow[0]);

	sObject.ulTag = ulTag;
	sObject.cbData = cbData;
	sObject.lpData = lpData; // Cheap copy, Set this item on NULL before you exit

	er = _AddIndexData(&sNewIndexObject, &sObject);
	if (er != erSuccess)
		goto exit;
	*lpulObjId = sNewIndexObject.ulObjId;

exit:

	if (lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	sObject.lpData = NULL; // Remove reference

	if (er) {
		LOG_CACHE_DEBUG("Get object from prop tag 0x%04X, data %s error 0x%08x", ulTag, bin2hex(cbData, lpData).c_str(), er);
	} else {
		LOG_CACHE_DEBUG("Get object from prop tag 0x%04X, data %s result [%s]: objectid %d", ulTag, bin2hex(cbData, lpData).c_str(), ((bCacheResult)?"C":"D"), *lpulObjId);
	}

	return er;
}

ECRESULT ECCacheManager::QueryObjectFromProp(unsigned int ulTag, unsigned int cbData, unsigned char* lpData, unsigned int* lpulObjId)
{
	ECRESULT		er = erSuccess;
	ECsIndexProp	sObject;
	ECsIndexObject	*sIndexObject;

	if(lpData == NULL || lpulObjId == NULL || cbData == 0) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	sObject.ulTag = ulTag;
	sObject.cbData = cbData;
	sObject.lpData = lpData; // Cheap copy, Set this item on NULL before you exit

	{
		scoped_lock lock(m_hCacheIndPropMutex);
		er = m_PropToObjectCache.GetCacheItem(sObject, &sIndexObject);
		if(er != erSuccess)
		    goto exit;
		    
		*lpulObjId = sIndexObject->ulObjId;
	}
	
exit:
	sObject.lpData = NULL;
    return er;
}

ECRESULT ECCacheManager::SetObjectProp(unsigned int ulTag, unsigned int cbData, unsigned char *lpData, unsigned int ulObjId)
{
    ECRESULT er = erSuccess;

    ECsIndexObject sObject;
    ECsIndexProp sProp;

    sObject.ulTag = ulTag;
    sObject.ulObjId = ulObjId;
    
    sProp.SetValue(ulTag, lpData, cbData);

    er = _AddIndexData(&sObject, &sProp);

	LOG_CACHE_DEBUG("Set object prop tag 0x%04X, data %s, objectid %d", ulTag, bin2hex(cbData, lpData).c_str(), ulObjId);
    return er;
}

ECRESULT ECCacheManager::GetEntryIdFromObject(unsigned int ulObjId, struct soap *soap, unsigned int ulFlags, entryId** lppEntryId)
{
	ECRESULT	er = erSuccess;
	entryId*	lpEntryId = s_alloc<entryId>(soap);

	er = GetEntryIdFromObject(ulObjId, soap, ulFlags, lpEntryId);
	if (er != erSuccess)
		goto exit;

	// Flags already set by GetEntryIdFromObject(4args)
	*lppEntryId = lpEntryId;
exit:
	if (er != erSuccess)
		delete lpEntryId;

	return er;
}

ECRESULT ECCacheManager::GetEntryIdFromObject(unsigned int ulObjId, struct soap *soap, unsigned int ulFlags, entryId* lpEntryId)
{
	ECRESULT er;

	er = GetPropFromObject( PROP_ID(PR_ENTRYID), ulObjId, soap, (unsigned int*)&lpEntryId->__size, &lpEntryId->__ptr);
	if (er != erSuccess)
		return er;
	// Set flags in entryid
	static_assert(offsetof(EID, usFlags) == offsetof(EID_V0, usFlags),
		"usFlags member not at same position");
	auto d = reinterpret_cast<EID *>(lpEntryId->__ptr);
	if (lpEntryId->__size < 0 ||
	    static_cast<size_t>(lpEntryId->__size) <
	    offsetof(EID, usFlags) + sizeof(d->usFlags)) {
		ec_log_err("%s: entryid has size %d; not enough for EID_V1.usFlags",
			__func__, lpEntryId->__size);
		return MAPI_E_CORRUPT_DATA;
	}
	d->usFlags = ulFlags;
	return erSuccess;
}

ECRESULT ECCacheManager::GetObjectFromEntryId(const entryId *lpEntryId,
    unsigned int *lpulObjId)
{
	// Make sure flags is 0 when getting from db/cache
	EntryId eid(lpEntryId);
	try {
		eid.setFlags(0);
	} catch (runtime_error &e) {
		ec_log_err("eid.setFlags: %s\n", e.what());
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
    ECRESULT 	er = erSuccess;
    
    // MAke sure flags is 0 when saving in DB
    EntryId eid(lpEntryId);
	try {
		eid.setFlags(0);
	} catch (runtime_error &e) {
		ec_log_err("eid.setFlags: %s\n", e.what());
		/* ignore exception - the following functions will catch the too-small eid.size */
	}
    er = SetObjectProp( PROP_ID(PR_ENTRYID), eid.size(), eid, ulObjId);

    return er;
}

/**
 * Convert entryid to database object id
 */
ECRESULT ECCacheManager::GetEntryListToObjectList(struct entryList *lpEntryList, ECListInt* lplObjectList)
{
	ECRESULT		er = erSuccess;
	unsigned int	ulId = 0;
	bool			bPartialCompletion = false;

	if(lpEntryList == NULL) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	for (unsigned int i = 0; i < lpEntryList->__size; ++i) {
		if(GetObjectFromEntryId(&lpEntryList->__ptr[i], &ulId) != erSuccess) {
			bPartialCompletion = true;
			continue; // Unknown entryid, next item
		}

		lplObjectList->push_back(ulId);
	}

exit:
	if(bPartialCompletion)
		er = KCWARN_PARTIAL_COMPLETION;

	return er;
}

/**
 * Convert database object id to entryid
 */
ECRESULT ECCacheManager::GetEntryListFromObjectList(ECListInt* lplObjectList, struct soap *soap, struct entryList **lppEntryList)
{
	ECRESULT		er = erSuccess;
	bool			bPartialCompletion = false;
	ECListInt::const_iterator iterList;
	entryList*		lpEntryList = s_alloc<entryList>(soap);

	if(lplObjectList == NULL || lppEntryList == NULL) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	lpEntryList->__ptr = s_alloc<entryId>(soap, lplObjectList->size());
	lpEntryList->__size = 0;

	for (iterList = lplObjectList->begin(); iterList != lplObjectList->end(); ++iterList) {
		if(GetEntryIdFromObject(*iterList, soap, 0, &lpEntryList->__ptr[lpEntryList->__size]) != erSuccess) {
			bPartialCompletion = true;
			continue; // Unknown entryid, next item
		}
		++lpEntryList->__size;
	}

	*lppEntryList = lpEntryList;
exit:
	if (er != erSuccess && lpEntryList)
		FreeEntryList(lpEntryList, true);

	if(bPartialCompletion)
		er = KCWARN_PARTIAL_COMPLETION;


	return er;
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
	
	if(m_setExcludedIndexProperties.empty()) {
		return KCERR_NOT_FOUND;
	}
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
	LOG_CELLCACHE_DEBUG("%s", "Disable cell cache");

    m_bCellCacheDisabled = true;
}

void ECCacheManager::EnableCellCache()
{
	LOG_CELLCACHE_DEBUG("%s", "Enable cell cache");
    m_bCellCacheDisabled = false;
}
