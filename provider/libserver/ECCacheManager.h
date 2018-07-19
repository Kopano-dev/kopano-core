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

#ifndef ECCACHEMANAGER
#define ECCACHEMANAGER

#include <kopano/zcdefs.h>
#include <map>
#include <memory>
#include <mutex>
#include "ECDatabaseFactory.h"
#include "ECDatabaseUtils.h"
#include "ECGenericObjectTable.h"	// ECListInt
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include "SOAPUtils.h"
#include "cmdutil.hpp"
#include <mapidefs.h>
#include <ECCache.h>
#include <kopano/ECKeyTable.h>

struct soap;

#include <unordered_map>

namespace KC {

class ECSessionManager;

class ECsStores _kc_final : public ECsCacheEntry {
public:
	unsigned int	ulStore, ulType;
	GUID			guidStore;
};

class ECsUserObject _kc_final : public ECsCacheEntry {
public:
	objectclass_t		ulClass;
	std::string strExternId, strSignature;
	unsigned int		ulCompanyId;
};

/* same as objectid_t, join? */
struct ECsUEIdKey {
	objectclass_t		ulClass;
	std::string			strExternId;
};

inline bool operator <(const ECsUEIdKey &a, const ECsUEIdKey &b)
{
	return a.ulClass < b.ulClass ||
	       (a.ulClass == b.ulClass && a.strExternId < b.strExternId);
}

/* Intern Id cache */
class ECsUEIdObject _kc_final : public ECsCacheEntry {
public:
	unsigned int ulCompanyId, ulUserId;
	std::string			strSignature;
};

class ECsUserObjectDetails _kc_final : public ECsCacheEntry {
public:
	objectdetails_t			sDetails;
};

class ECsServerDetails _kc_final : public ECsCacheEntry {
public:
	serverdetails_t			sDetails;
};

class ECsObjects _kc_final : public ECsCacheEntry {
public:
	unsigned int ulParent, ulOwner, ulFlags, ulType;
};

class ECsQuota _kc_final : public ECsCacheEntry {
public:
	quotadetails_t	quota;
};

class ECsIndexObject _kc_final : public ECsCacheEntry {
public:
	inline bool operator==(const ECsIndexObject &other) const noexcept
	{
		return ulObjId == other.ulObjId && ulTag == other.ulTag;
	}

	inline bool operator<(const ECsIndexObject &other) const noexcept
	{
		return ulObjId < other.ulObjId ||
		       (ulObjId == other.ulObjId && ulTag < other.ulTag);
	}

	unsigned int ulObjId, ulTag;
};

class ECsIndexProp _kc_final : public ECsCacheEntry {
public:
	ECsIndexProp(void) = default;
	~ECsIndexProp() {
		delete[] lpData;
	}
    
    ECsIndexProp(const ECsIndexProp &src) {
        if(this == &src)
            return;
        Copy(&src, this);
    }

	ECsIndexProp(ECsIndexProp &&o) :
		ulTag(o.ulTag), cbData(o.cbData), lpData(o.lpData)
	{
		o.lpData = nullptr;
		o.cbData = 0;
	}

	ECsIndexProp(unsigned int tag, const unsigned char *d, unsigned int z)
	{
		SetValue(tag, d, z);
	}
    
    ECsIndexProp& operator=(const ECsIndexProp &src) {
		if (this == &src)
			return *this;
		Free();
		Copy(&src, this);
		return *this;
    }

	// @todo check this function, is this really ok?
	inline bool operator<(const ECsIndexProp &other) const noexcept
	{

		if(cbData < other.cbData)
			return true;
		if (cbData != other.cbData)
			return false;
		if (lpData == NULL && other.lpData)
			return true;
		else if (lpData != NULL && other.lpData == NULL)
			return false;
		else if (lpData == NULL && other.lpData == NULL)
			return false;
		int c = memcmp(lpData, other.lpData, cbData);
		if (c < 0)
			return true;
		else if (c == 0 && ulTag < other.ulTag)
			return true;
		return false;
	}

	inline bool operator==(const ECsIndexProp &other) const noexcept
	{

		if(cbData != other.cbData || ulTag != other.ulTag)
			return false;

		if (lpData == other.lpData)
			return true;

		if (lpData == NULL || other.lpData == NULL)
			return false;

		if(memcmp(lpData, other.lpData, cbData) == 0)
			return true;

		return false;
	}

	void SetValue(unsigned int tag, const unsigned char *data, unsigned int z)
	{
		if (data == nullptr || z == 0)
			return;

		Free();
		lpData = new unsigned char[z];
		cbData = z;
		ulTag = tag;
		memcpy(lpData, data, z);
	}
protected:
	void Free() {
		delete[] lpData;
		ulTag = 0; 
		cbData = 0;
		lpData = NULL;
	}

	void Copy(const ECsIndexProp* src, ECsIndexProp* dst) {
		if(src->lpData != NULL && src->cbData>0) {
			dst->lpData = new unsigned char[src->cbData];
			memcpy(dst->lpData, src->lpData, (size_t)src->cbData);
			dst->cbData = src->cbData;
		} else {
			dst->lpData = NULL;
			dst->cbData = 0;
		}

		dst->ulTag = src->ulTag;
	}
public:
	unsigned int ulTag = 0, cbData = 0;
	unsigned char *lpData = nullptr;
};

class ECsCells _kc_final : public ECsCacheEntry {
public:
	ECsCells(void) = default;
    ~ECsCells() {
		for (auto &p : mapPropVals)
			FreePropVal(&p.second, false);
    };
    
    ECsCells(const ECsCells &src) {
        struct propVal val;
		for (const auto &p : src.mapPropVals) {
			CopyPropVal(const_cast<struct propVal *>(&p.second), &val);
			mapPropVals[p.first] = val;
        }
        m_bComplete = src.m_bComplete;
    }
    
    ECsCells& operator=(const ECsCells &src) {
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
    
    // Add a property value for this object
    void AddPropVal(unsigned int ulPropTag, const struct propVal *lpPropVal) {
        struct propVal val;
        ulPropTag = NormalizeDBPropTag(ulPropTag); // Only cache PT_STRING8
        CopyPropVal(lpPropVal, &val, NULL, false, true);
        val.ulPropTag = NormalizeDBPropTag(val.ulPropTag);
		auto res = mapPropVals.emplace(ulPropTag, val);
		if (!res.second) {
            FreePropVal(&res.first->second, false); 
            res.first->second = val;	// reassign
        }
    }
    
    // get a property value for this object
    bool GetPropVal(unsigned int ulPropTag, struct propVal *lpPropVal, struct soap *soap, bool truncate) {
		auto i = mapPropVals.find(NormalizeDBPropTag(ulPropTag));
		if (i == mapPropVals.cend())
			return false;
        CopyPropVal(&i->second, lpPropVal, soap, truncate);
        if(NormalizeDBPropTag(ulPropTag) == lpPropVal->ulPropTag)
	        lpPropVal->ulPropTag = ulPropTag; // Switch back to requested type (not on PT_ERROR of course)
        return true;
    }
    
	std::vector<unsigned int> GetPropTags() {
		std::vector<unsigned int> result;
		for (auto iter = mapPropVals.cbegin(); iter != mapPropVals.cend(); iter++) {
			result.push_back(iter->first);
		}
		return result;
	}

    // Updates a LONG type property
    void UpdatePropVal(unsigned int ulPropTag, int lDelta) {
        if(PROP_TYPE(ulPropTag) != PT_LONG && PROP_TYPE(ulPropTag) != PT_LONGLONG)
            return;
		auto i = mapPropVals.find(ulPropTag);
		if (i == mapPropVals.cend())
			return;
		if (PROP_TYPE(i->second.ulPropTag) == PT_LONG)
			i->second.Value.ul += lDelta;
		if (PROP_TYPE(i->second.ulPropTag) == PT_LONGLONG)
			i->second.Value.li += lDelta;
    }
    
    // Updates a LONG type property
    void UpdatePropVal(unsigned int ulPropTag, unsigned int ulMask, unsigned int ulValue) {
        if(PROP_TYPE(ulPropTag) != PT_LONG && PROP_TYPE(ulPropTag) != PT_LONGLONG)
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
    
	void SetComplete(bool bComplete) { m_bComplete = bComplete; }
	bool GetComplete() const { return m_bComplete; }

    // Gets the amount of memory used by this object    
    size_t GetSize() const {
        size_t ulSize = 0;
        
        for (const auto &p : mapPropVals) {
            switch (p.second.__union) {
                case SOAP_UNION_propValData_lpszA:
                    ulSize += p.second.Value.lpszA != NULL ?
                              strlen(p.second.Value.lpszA) : 0;
					break;
                case SOAP_UNION_propValData_bin:
                    ulSize += p.second.Value.bin != NULL ?
                              p.second.Value.bin->__size +
                              sizeof(p.second.Value.bin[0]) : 0;
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
    
    // All properties for this object; propTag => propVal
    std::map<unsigned int, struct propVal> mapPropVals;
	bool m_bComplete = false;
};

class ECsACLs _kc_final : public ECsCacheEntry {
public:
	ECsACLs(void) = default;
	ECsACLs(const ECsACLs &src) : ulACLs(src.ulACLs)
	{
		aACL.reset(new ACL[ulACLs]);
		memcpy(aACL.get(), src.aACL.get(), sizeof(ACL) * ulACLs);
	}
    ECsACLs& operator=(const ECsACLs &src) {
		if (this != &src) {
			ulACLs = src.ulACLs;
			aACL.reset(new ACL[ulACLs]);
			memcpy(aACL.get(), src.aACL.get(), sizeof(ACL) * ulACLs);
		}
		return *this;
    };
	unsigned int ulACLs = 0;
    struct ACL {
		unsigned int ulType, ulMask, ulUserId;
    };
	std::unique_ptr<ACL[]> aACL;
};

struct ECsSortKeyKey {
	sObjectTableKey	sKey;
	unsigned int	ulPropTag;
};

inline unsigned int IPRSHash(const ECsIndexProp &_Keyval1) noexcept
{
	unsigned int b = 378551, a = 63689, hash = 0;
	for (std::size_t i = 0; i < _Keyval1.cbData; ++i) {
		hash = hash * a + _Keyval1.lpData[i];
		a *= b;
	}

	return hash;
}

} /* namespace KC */

namespace std {
	// hash function for type ECsIndexProp
	template<> struct hash<KC::ECsIndexProp> {
		public:
		size_t operator()(const KC::ECsIndexProp &value) const noexcept { return KC::IPRSHash(value); }
	};

	// hash function for type ECsIndexObject
	template<> struct hash<KC::ECsIndexObject> {
		public:
		size_t operator()(const KC::ECsIndexObject &value) const noexcept
		{
					hash<unsigned int> hasher;
					// @TODO check the hash function!
					return hasher(value.ulObjId * value.ulTag ) ;
			}
	};
}

namespace KC {

typedef std::unordered_map<unsigned int, ECsObjects> ECMapObjects;
typedef std::unordered_map<unsigned int, ECsStores> ECMapStores;
typedef std::unordered_map<unsigned int, ECsACLs> ECMapACLs;
typedef std::unordered_map<unsigned int, ECsQuota> ECMapQuota;
typedef std::unordered_map<unsigned int, ECsUserObject> ECMapUserObject; // userid to user object
typedef std::map<ECsUEIdKey, ECsUEIdObject> ECMapUEIdObject; // user type + externid to user object
typedef std::unordered_map<unsigned int, ECsUserObjectDetails> ECMapUserObjectDetails; // userid to user object data
typedef std::map<std::string, ECsServerDetails> ECMapServerDetails;
typedef std::unordered_map<unsigned int, ECsCells> ECMapCells;

// Index properties
typedef std::map<ECsIndexObject, ECsIndexProp> ECMapObjectToProp;
typedef std::unordered_map<ECsIndexProp, ECsIndexObject> ECMapPropToObject;

#define CACHE_NO_PARENT 0xFFFFFFFF

class ECCacheManager _kc_final {
public:
	ECCacheManager(ECConfig *lpConfig, ECDatabaseFactory *lpDatabase);
	virtual ~ECCacheManager();

	ECRESULT PurgeCache(unsigned int ulFlags);

	// These are read-through (ie they access the DB if they can't find the data)
	ECRESULT GetParent(unsigned int ulObjId, unsigned int *ulParent);
	ECRESULT GetOwner(unsigned int ulObjId, unsigned int *ulOwner);
	ECRESULT GetObject(unsigned int ulObjId, unsigned int *ulParent, unsigned int *ulOwner, unsigned int *ulFlags, unsigned int *ulType = NULL);
	ECRESULT SetObject(unsigned int ulObjId, unsigned int ulParent, unsigned int ulOwner, unsigned int ulFlags, unsigned int ulType);
	// Query cache only
	ECRESULT QueryParent(unsigned int ulObjId, unsigned int *ulParent);
	
	ECRESULT GetObjects(const std::list<sObjectTableKey> &lstObjects, std::map<sObjectTableKey, ECsObjects> &mapObjects);
	ECRESULT GetObjectsFromProp(unsigned int ulTag, const std::vector<unsigned int> &cbdata, const std::vector<unsigned char *> &lpdata, std::map<ECsIndexProp, unsigned int> &mapObjects);

	ECRESULT GetStore(unsigned int ulObjId, unsigned int *ulStore, GUID *lpGuid, unsigned int maxdepth = 100);
	ECRESULT GetStoreAndType(unsigned int ulObjId, unsigned int *ulStore, GUID *lpGuid, unsigned int *ulType, unsigned int maxdepth = 100);
	ECRESULT GetObjectFlags(unsigned int ulObjId, unsigned int *ulFlags);
	ECRESULT SetStore(unsigned int ulObjId, unsigned int ulStore, const GUID *, unsigned int ulType);

	ECRESULT GetServerDetails(const std::string &strServerId, serverdetails_t *lpsDetails);
	ECRESULT SetServerDetails(const std::string &strServerId, const serverdetails_t &sDetails);

	// Cache user table
	/* hmm, externid is in the signature as well, optimize? */
	ECRESULT GetUserObject(unsigned int ulUserId, objectid_t *lpExternId, unsigned int *lpulCompanyId, std::string *lpstrSignature);
	ECRESULT GetUserObject(const objectid_t &sExternId, unsigned int *lpulUserId, unsigned int *lpulCompanyId, std::string *lpstrSignature);
	ECRESULT GetUserObjects(const std::list<objectid_t> &lstExternObjIds, std::map<objectid_t, unsigned int> *lpmapLocalObjIds);

	// Cache user information
	ECRESULT GetUserDetails(unsigned int ulUserId, objectdetails_t *details);
	ECRESULT SetUserDetails(unsigned int, const objectdetails_t &);
	ECRESULT GetACLs(unsigned int ulObjId, struct rightsArray **lppRights);
	ECRESULT SetACLs(unsigned int ulObjId, const struct rightsArray &);
	ECRESULT GetQuota(unsigned int ulUserId, bool bIsDefaultQuota, quotadetails_t *quota);
	ECRESULT SetQuota(unsigned int ulUserId, bool bIsDefaultQuota, const quotadetails_t &);

	ECRESULT Update(unsigned int ulType, unsigned int ulObjId);
	ECRESULT UpdateUser(unsigned int ulUserId);

	ECRESULT GetEntryIdFromObject(unsigned int ulObjId, struct soap *soap, unsigned int ulFlags, entryId* lpEntrId);
	ECRESULT GetEntryIdFromObject(unsigned int ulObjId, struct soap *soap, unsigned int ulFlags, entryId** lppEntryId);
	ECRESULT GetObjectFromEntryId(const entryId *id, unsigned int *obj);
	ECRESULT SetObjectEntryId(const entryId *, unsigned int ulObjId);
	ECRESULT GetEntryListToObjectList(struct entryList *lpEntryList, ECListInt* lplObjectList);
	ECRESULT GetEntryListFromObjectList(ECListInt* lplObjectList, struct soap *soap, struct entryList **lppEntryList);

	// Table data functions (pure cache functions, they will never access the DB themselves. Data must be provided through Set functions)
	ECRESULT GetCell(const sObjectTableKey *, unsigned int tag, struct propVal *, struct soap *, bool computed, bool truncated = true);
	ECRESULT SetCell(const sObjectTableKey *, unsigned int tag, const struct propVal *);
	ECRESULT UpdateCell(unsigned int ulObjId, unsigned int ulPropTag, int lDelta);
	ECRESULT UpdateCell(unsigned int ulObjId, unsigned int ulPropTag, unsigned int ulMask, unsigned int ulValue);
	ECRESULT SetComplete(unsigned int ulObjId);
	ECRESULT GetComplete(unsigned int ulObjId, bool &complete);
	ECRESULT GetPropTags(unsigned int ulObjId, std::vector<unsigned int> &proptags);
	// Cache Index properties
	
	// Read-through
	ECRESULT GetPropFromObject(unsigned int ulTag, unsigned int ulObjId, struct soap *soap, unsigned int* lpcbData, unsigned char** lppData);
	ECRESULT GetObjectFromProp(unsigned int ulTag, unsigned int cbData, unsigned char* lpData, unsigned int* lpulObjId);
	
	ECRESULT RemoveIndexData(unsigned int ulObjId);
	ECRESULT RemoveIndexData(unsigned int ulPropTag, unsigned int cbData, unsigned char *lpData);
	ECRESULT RemoveIndexData(unsigned int ulPropTag, unsigned int ulObjId);
	
	// Read cache only
	ECRESULT QueryObjectFromProp(unsigned int ulTag, unsigned int cbData, unsigned char* lpData, unsigned int* lpulObjId);

	ECRESULT SetObjectProp(unsigned int ulTag, unsigned int cbData, unsigned char* lpData, unsigned int ulObjId);

	void ForEachCacheItem(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj);
	ECRESULT DumpStats();
	
	// Cache list of properties indexed by kopano-search
	ECRESULT GetExcludedIndexProperties(std::set<unsigned int>& set);
	ECRESULT SetExcludedIndexProperties(const std::set<unsigned int> &);

	// Test
	void DisableCellCache();
	void EnableCellCache();
	
private:
	// cache functions
	ECRESULT I_GetACLs(unsigned int obj_id, struct rightsArray **);
	ECRESULT I_DelACLs(unsigned int obj_id);
	ECRESULT I_GetObject(unsigned int obj_id, unsigned int *parent, unsigned int *owner, unsigned int *flags, unsigned int *type);
	ECRESULT I_DelObject(unsigned int obj_id);
	ECRESULT I_GetStore(unsigned int obj_id, unsigned int *store, GUID *, unsigned int *type);
	ECRESULT I_DelStore(unsigned int obj_id);
	ECRESULT I_AddUserObject(unsigned int user_id, const objectclass_t &, unsigned int company_id, const std::string &ext_id, const std::string &signautre);
	ECRESULT I_GetUserObject(unsigned int user_id, objectclass_t *, unsigned int *company_id, std::string *extern_id, std::string *signature);
	ECRESULT I_DelUserObject(unsigned int user_id);
	ECRESULT I_AddUEIdObject(const std::string &ext_id, const objectclass_t &, unsigned int company_id, unsigned int user_id, const std::string &signature);
	ECRESULT I_GetUEIdObject(const std::string &ext_id, objectclass_t, unsigned int *company_id, unsigned int *user_id, std::string *signature);
	ECRESULT I_DelUEIdObject(const std::string &ext_id, objectclass_t);
	ECRESULT I_AddUserObjectDetails(unsigned int, const objectdetails_t &);
	ECRESULT I_GetUserObjectDetails(unsigned int user_id, objectdetails_t *);
	ECRESULT I_DelUserObjectDetails(unsigned int user_id);
	ECRESULT I_DelCell(unsigned int obj_id);
	ECRESULT I_GetQuota(unsigned int user_id, bool bIsDefaultQuota, quotadetails_t *quota);
	ECRESULT I_DelQuota(unsigned int user_id, bool bIsDefaultQuota);

	// Cache Index properties
	ECRESULT I_AddIndexData(const ECsIndexObject &, const ECsIndexProp &);

	ECDatabaseFactory*	m_lpDatabaseFactory;
	std::recursive_mutex m_hCacheMutex; /* User, ACL, server cache */
	std::recursive_mutex m_hCacheStoreMutex;
	std::recursive_mutex m_hCacheObjectMutex;
	std::recursive_mutex m_hCacheCellsMutex; /* Cell cache */
	std::recursive_mutex m_hCacheIndPropMutex; /* Indexed properties cache */
	
	// Quota cache, to reduce the impact of the user plugin
	// m_mapQuota contains user and company cache, except when it's the company user default quota
	// m_mapQuotaUserDefault contains company user default quota
	// this can't be in the same map, since the id is the same for "company" and "company user default"
	ECCache<ECMapQuota>			m_QuotaCache;
	ECCache<ECMapQuota>			m_QuotaUserDefaultCache;

	// Object cache, (hierarchy table)
	ECCache<ECMapObjects>		m_ObjectsCache;

	// Store cache
	ECCache<ECMapStores>		m_StoresCache;

	// User cache
	ECCache<ECMapUserObject>	m_UserObjectCache;
	ECCache<ECMapUEIdObject>	m_UEIdObjectCache;
	ECCache<ECMapUserObjectDetails>	m_UserObjectDetailsCache;

	// ACL cache
	ECCache<ECMapACLs>			m_AclCache;

	// Cell cache, include the column data of a loaded table
	ECCache<ECMapCells>			m_CellCache;
	
	// Server cache
	ECCache<ECMapServerDetails>	m_ServerDetailsCache;
	
	//Index properties
	ECCache<ECMapPropToObject>	m_PropToObjectCache;
	ECCache<ECMapObjectToProp>	m_ObjectToPropCache;
	
	// Properties from kopano-search
	std::set<unsigned int> 		m_setExcludedIndexProperties;
	std::mutex m_hExcludedIndexPropertiesMutex;
	
	// Testing
	bool m_bCellCacheDisabled = false;
};

} /* namespace */

#endif
