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
#include <pthread.h>

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

#include <unordered_map>

template<typename Key, typename T>
struct hash_map {
	typedef std::unordered_map<Key, T, std::hash<Key>, std::equal_to<Key>> Type;
};

class ECSessionManager;

class ECsStores : public ECsCacheEntry {
public:
	unsigned int	ulStore;
	GUID			guidStore;
	unsigned int	ulType;
};

class ECsUserObject : public ECsCacheEntry {
public:
	objectclass_t		ulClass;
	std::string			strExternId;
	unsigned int		ulCompanyId;
	std::string			strSignature;
};

/* same as objectid_t, join? */
typedef struct {
	objectclass_t		ulClass;
	std::string			strExternId;
} ECsUEIdKey;

inline bool operator <(const ECsUEIdKey &a, const ECsUEIdKey &b)
{
	if (a.ulClass < b.ulClass)
		return true;
	if ((a.ulClass == b.ulClass) && a.strExternId < b.strExternId)
		return true;
	return false;
}

/* Intern Id cache */
class ECsUEIdObject _zcp_final : public ECsCacheEntry {
public:
	unsigned int		ulCompanyId;
	unsigned int		ulUserId;
	std::string			strSignature;
};

class ECsUserObjectDetails _zcp_final : public ECsCacheEntry {
public:
	objectdetails_t			sDetails;
};

class ECsServerDetails _zcp_final : public ECsCacheEntry {
public:
	serverdetails_t			sDetails;
};

class ECsObjects _zcp_final : public ECsCacheEntry {
public:
	unsigned int	ulParent;
	unsigned int	ulOwner;
	unsigned int	ulFlags;
	unsigned int	ulType;
};

class ECsQuota _zcp_final : public ECsCacheEntry {
public:
	quotadetails_t	quota;
};

class ECsIndexObject _zcp_final : public ECsCacheEntry {
public:
	inline bool operator==(const ECsIndexObject &other) const
	{
		if (ulObjId == other.ulObjId && ulTag == other.ulTag)
			return true;

		return false;
	}

	inline bool operator<(const ECsIndexObject &other) const
	{
		if(ulObjId < other.ulObjId)
			return true;
		else if(ulObjId == other.ulObjId && ulTag < other.ulTag)
			return true;

		return false;
	}

public:
	unsigned int ulObjId;
	unsigned int ulTag;
};

class ECsIndexProp _zcp_final : public ECsCacheEntry {
public:
    ECsIndexProp() : ECsCacheEntry() { 
		lpData = NULL; 
		ulTag = 0; 
		cbData = 0;
	}

	~ECsIndexProp() {
		delete[] lpData;
	}
    
    ECsIndexProp(const ECsIndexProp &src) {
        if(this == &src)
            return;
        Copy(&src, this);
    }
    
    ECsIndexProp& operator=(const ECsIndexProp &src) {
        if(this != &src) {
			Free();
			Copy(&src, this);
		}
		return *this;
    }

	// @todo check this function, is this really ok?
	inline bool operator<(const ECsIndexProp &other) const
	{

		if(cbData < other.cbData)
			return true;

		if(cbData == other.cbData) {
			if(lpData == NULL && other.lpData)
				return true;
			else if (lpData != NULL && other.lpData == NULL)
				return false;
			else if (lpData == NULL && other.lpData == NULL)
				return false;
			int c = memcmp(lpData, other.lpData, cbData);
			if(c < 0)
				return true;
			else if(c == 0 && ulTag < other.ulTag)
				return true;
		}

		return false;
	}

	inline bool operator==(const ECsIndexProp &other) const
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

	void SetValue(unsigned int ulTag, unsigned char* lpData, unsigned int cbData) {

		if(lpData == NULL|| cbData == 0)
			return;

		Free();

		this->lpData = new unsigned char[cbData];
		this->cbData = cbData;
		this->ulTag = ulTag;

		memcpy(this->lpData, lpData, (size_t)cbData);

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
	unsigned int	ulTag;
	unsigned char*	lpData;
	unsigned int	cbData;
};

class ECsCells _zcp_final : public ECsCacheEntry {
public:
    ECsCells() : ECsCacheEntry() { 
    	m_bComplete = false; 
	};
    ~ECsCells() {
		std::map<unsigned int, struct propVal>::iterator i;
		for (i = mapPropVals.begin(); i != mapPropVals.end(); ++i)
			FreePropVal(&i->second, false);
    };
    
    ECsCells(const ECsCells &src) {
        struct propVal val;
        std::map<unsigned int, struct propVal>::const_iterator i;
        for (i = src.mapPropVals.begin(); i != src.mapPropVals.end(); ++i) {
            CopyPropVal((struct propVal *)&i->second, &val);
            mapPropVals[i->first] = val;
        }
        m_bComplete = src.m_bComplete;
    }
    
    ECsCells& operator=(const ECsCells &src) {
        struct propVal val;
        std::map<unsigned int, struct propVal>::iterator i;
		for (i = mapPropVals.begin(); i != mapPropVals.end(); ++i)
			FreePropVal(&i->second, false);
        mapPropVals.clear();
        
        for (i = ((ECsCells &)src).mapPropVals.begin();
             i != ((ECsCells &)src).mapPropVals.end(); ++i) {
            CopyPropVal((struct propVal *)&i->second, &val);
            mapPropVals[i->first] = val;
        }
        m_bComplete = src.m_bComplete;
		return *this;
    }
    
    // Add a property value for this object
    void AddPropVal(unsigned int ulPropTag, const struct propVal *lpPropVal) {
        struct propVal val;
        ulPropTag = NormalizeDBPropTag(ulPropTag); // Only cache PT_STRING8
		std::pair<std::map<unsigned int, struct propVal>::iterator,bool> res;
        CopyPropVal(lpPropVal, &val, NULL, true);
        val.ulPropTag = NormalizeDBPropTag(val.ulPropTag);
		res = mapPropVals.insert(std::make_pair(ulPropTag, val));
		if (res.second == false) {
            FreePropVal(&res.first->second, false); 
            res.first->second = val;	// reassign
        }
    }
    
    // get a property value for this object
    bool GetPropVal(unsigned int ulPropTag, struct propVal *lpPropVal, struct soap *soap) {
        std::map<unsigned int, struct propVal>::const_iterator i;
        i = mapPropVals.find(NormalizeDBPropTag(ulPropTag));
        if(i == mapPropVals.end())
            return false;
        CopyPropVal(&i->second, lpPropVal, soap);
        if(NormalizeDBPropTag(ulPropTag) == lpPropVal->ulPropTag)
	        lpPropVal->ulPropTag = ulPropTag; // Switch back to requested type (not on PT_ERROR of course)
        return true;
    }
    
    // Updates a LONG type property
    void UpdatePropVal(unsigned int ulPropTag, int lDelta) {
        std::map<unsigned int, struct propVal>::iterator i;
        if(PROP_TYPE(ulPropTag) != PT_LONG)
            return;
        i = mapPropVals.find(ulPropTag);
        if(i == mapPropVals.end() || PROP_TYPE(i->second.ulPropTag) != PT_LONG)
            return;
        i->second.Value.ul += lDelta;
    }
    
    // Updates a LONG type property
    void UpdatePropVal(unsigned int ulPropTag, unsigned int ulMask, unsigned int ulValue) {
        std::map<unsigned int, struct propVal>::iterator i;
        if(PROP_TYPE(ulPropTag) != PT_LONG)
            return;
        i = mapPropVals.find(ulPropTag);
        if(i == mapPropVals.end() || PROP_TYPE(i->second.ulPropTag) != PT_LONG)
            return;
        i->second.Value.ul &= ~ulMask;
        i->second.Value.ul |= ulValue & ulMask;
    }
    
    void SetComplete(bool bComplete) {
        this->m_bComplete = bComplete;
    }
    
    bool GetComplete() {
        return this->m_bComplete;
    }

    // Gets the amount of memory used by this object    
    size_t GetSize() const {
        size_t ulSize = 0;
        
        std::map<unsigned int, struct propVal>::const_iterator i;
        for (i = mapPropVals.begin(); i != mapPropVals.end(); ++i) {
            switch(i->second.__union) {
                case SOAP_UNION_propValData_lpszA:
                    ulSize += i->second.Value.lpszA ? (unsigned int)strlen(i->second.Value.lpszA) : 0;
					break;
                case SOAP_UNION_propValData_bin:
                    ulSize += i->second.Value.bin ? i->second.Value.bin->__size + sizeof(i->second.Value.bin[0]) : 0;
					break;
                case SOAP_UNION_propValData_hilo:
                    ulSize += sizeof(i->second.Value.hilo[0]);
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
    
    bool m_bComplete;
};

class ECsACLs _zcp_final : public ECsCacheEntry {
public:
	ECsACLs() : ECsCacheEntry() { ulACLs = 0; aACL = NULL; }
    ECsACLs(const ECsACLs &src) {
        ulACLs = src.ulACLs;
        aACL = new ACL[src.ulACLs];
        memcpy(aACL, src.aACL, sizeof(ACL) * src.ulACLs);
    };
    ECsACLs& operator=(const ECsACLs &src) {
		if (this != &src) {
			delete[] aACL;
			ulACLs = src.ulACLs;
			aACL = new ACL[src.ulACLs];
			memcpy(aACL, src.aACL, sizeof(ACL) * src.ulACLs);
		}
		return *this;
    };
    ~ECsACLs() {
		delete[] aACL;
    }
    unsigned int 	ulACLs;
    struct ACL {
        unsigned int ulType;
        unsigned int ulMask;
        unsigned int ulUserId;
    } *aACL;
};

typedef struct {
	sObjectTableKey	sKey;
	unsigned int	ulPropTag;
}ECsSortKeyKey;


struct lessindexobjectkey {
	bool operator()(const ECsIndexObject& a, const ECsIndexObject& b) const
	{
		if(a.ulObjId < b.ulObjId)
			return true;
		else if(a.ulObjId == b.ulObjId && a.ulTag < b.ulTag)
			return true;

		return false;
	}

};

inline unsigned int IPRSHash(const ECsIndexProp& _Keyval1)
{
	unsigned int b    = 378551;
	unsigned int a    = 63689;
	unsigned int hash = 0;

	for (std::size_t i = 0; i < _Keyval1.cbData; ++i) {
		hash = hash * a + _Keyval1.lpData[i];
		a *= b;
	}

	return hash;
}

namespace std {
	// hash function for type ECsIndexProp
	template<>
	struct hash<ECsIndexProp> {
		public:
			hash() {};
			~hash() {};

			size_t operator() (const ECsIndexProp &value) const { return IPRSHash(value); }
	};

	// hash function for type ECsIndexObject
	template<>
	struct hash<ECsIndexObject> {
		public:
			hash() {};
			~hash() {};
			size_t operator() (const ECsIndexObject &value) const {
					hash<unsigned int> hasher;
					// @TODO check the hash function!
					return hasher(value.ulObjId * value.ulTag ) ;
			}
	};
}

typedef hash_map<unsigned int, ECsObjects>::Type ECMapObjects;
typedef hash_map<unsigned int, ECsStores>::Type ECMapStores;
typedef hash_map<unsigned int, ECsACLs>::Type ECMapACLs;
typedef hash_map<unsigned int, ECsQuota>::Type ECMapQuota;
typedef hash_map<unsigned int, ECsUserObject>::Type ECMapUserObject; // userid to user object
typedef std::map<ECsUEIdKey, ECsUEIdObject> ECMapUEIdObject; // user type + externid to user object
typedef hash_map<unsigned int, ECsUserObjectDetails>::Type ECMapUserObjectDetails; // userid to user object data
typedef std::map<std::string, ECsServerDetails> ECMapServerDetails;
typedef hash_map<unsigned int, ECsCells>::Type ECMapCells;

// Index properties
typedef std::map<ECsIndexObject, ECsIndexProp, lessindexobjectkey > ECMapObjectToProp;
typedef hash_map<ECsIndexProp, ECsIndexObject>::Type ECMapPropToObject;

#define CACHE_NO_PARENT 0xFFFFFFFF

class ECCacheManager _zcp_final {
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
	ECRESULT SetUserDetails(unsigned int, const objectdetails_t *);

	ECRESULT GetACLs(unsigned int ulObjId, struct rightsArray **lppRights);
	ECRESULT SetACLs(unsigned int ulObjId, const struct rightsArray *);

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
	ECRESULT GetCell(const sObjectTableKey *, unsigned int tag, struct propVal *, struct soap *, bool computed);
	ECRESULT SetCell(const sObjectTableKey *, unsigned int tag, const struct propVal *);
	ECRESULT UpdateCell(unsigned int ulObjId, unsigned int ulPropTag, int lDelta);
	ECRESULT UpdateCell(unsigned int ulObjId, unsigned int ulPropTag, unsigned int ulMask, unsigned int ulValue);
	ECRESULT SetComplete(unsigned int ulObjId);
	
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
	ECRESULT _GetACLs(unsigned int ulObjId, struct rightsArray **lppRights);
	ECRESULT _DelACLs(unsigned int ulObjId);
	
	ECRESULT _GetObject(unsigned int ulObjId, unsigned int *ulParent, unsigned int *ulOwner, unsigned int *ulFlags, unsigned int *ulType);
	ECRESULT _DelObject(unsigned int ulObjId);

	ECRESULT _GetStore(unsigned int ulObjId, unsigned int *ulStore, GUID *lpGuid, unsigned int *ulType);
	ECRESULT _DelStore(unsigned int ulObjId);

	ECRESULT _AddUserObject(unsigned int ulUserId, const objectclass_t &ulClass, unsigned int ulCompanyId, const std::string &strExternId, const std::string &strSignature);
	ECRESULT _GetUserObject(unsigned int ulUserId, objectclass_t* lpulClass, unsigned int *lpulCompanyId,
							std::string* lpstrExternId, std::string* lpstrSignature);
	ECRESULT _DelUserObject(unsigned int ulUserId);

	ECRESULT _AddUEIdObject(const std::string &strExternId, const objectclass_t &ulClass, unsigned int ulCompanyId, unsigned int ulUserId, const std::string &strSignature);
	ECRESULT _GetUEIdObject(const std::string &strExternId, objectclass_t ulClass, unsigned int *lpulCompanyId, unsigned int* lpulUserId, std::string* lpstrSignature);
	ECRESULT _DelUEIdObject(const std::string &strExternId, objectclass_t ulClass);

	ECRESULT _AddUserObjectDetails(unsigned int, const objectdetails_t *);
	ECRESULT _GetUserObjectDetails(unsigned int ulUserId, objectdetails_t *details);
	ECRESULT _DelUserObjectDetails(unsigned int ulUserId);

	ECRESULT _DelCell(unsigned int ulObjId);

	ECRESULT _GetQuota(unsigned int ulUserId, bool bIsDefaultQuota, quotadetails_t *quota);
	ECRESULT _DelQuota(unsigned int ulUserId, bool bIsDefaultQuota);

	// Cache Index properties
	ECRESULT _AddIndexData(const ECsIndexObject *lpObject, const ECsIndexProp *lpProp);

private:
	ECDatabaseFactory*	m_lpDatabaseFactory;
	pthread_mutex_t		m_hCacheMutex;			// Store, Object, User, ACL, server cache
	pthread_mutex_t		m_hCacheCellsMutex;		// Cell cache
	pthread_mutex_t		m_hCacheIndPropMutex;	// Indexed properties cache
	
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
	pthread_mutex_t				m_hExcludedIndexPropertiesMutex;
	
	// Testing
	bool						m_bCellCacheDisabled;
};

#endif
