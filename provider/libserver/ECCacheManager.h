/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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

class ECsStores final : public ECsCacheEntry {
public:
	unsigned int	ulStore, ulType;
	GUID			guidStore;
};

class ECsUserObject final : public ECsCacheEntry {
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
class ECsUEIdObject final : public ECsCacheEntry {
public:
	unsigned int ulCompanyId, ulUserId;
	std::string			strSignature;
};

class ECsUserObjectDetails final : public ECsCacheEntry {
public:
	objectdetails_t			sDetails;
};

class ECsServerDetails final : public ECsCacheEntry {
public:
	serverdetails_t			sDetails;
};

class ECsObjects final : public ECsCacheEntry {
public:
	unsigned int ulParent, ulOwner, ulFlags, ulType;
};

class ECsQuota final : public ECsCacheEntry {
public:
	quotadetails_t	quota;
};

class ECsIndexObject final : public ECsCacheEntry {
public:
	inline bool operator==(const ECsIndexObject &other) const noexcept
	{
		return ulObjId == other.ulObjId && ulTag == other.ulTag;
	}

	inline bool operator<(const ECsIndexObject &other) const noexcept
	{
		return std::tie(ulObjId, ulTag) < std::tie(other.ulObjId, other.ulTag);
	}

	unsigned int ulObjId, ulTag;
};

class ECsIndexProp final : public ECsCacheEntry {
public:
	ECsIndexProp(void) = default;
	~ECsIndexProp() {
		delete[] lpData;
	}

    ECsIndexProp(const ECsIndexProp &src) {
		if (this != &src)
			Copy(src, *this);
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
		Copy(src, *this);
		return *this;
    }

	bool operator<(const ECsIndexProp &) const noexcept;
	bool operator==(const ECsIndexProp &) const noexcept;
	void SetValue(unsigned int tag, const unsigned char *data, unsigned int z);

protected:
	void Free() {
		delete[] lpData;
		ulTag = 0;
		cbData = 0;
		lpData = NULL;
	}

	void Copy(const ECsIndexProp &src, ECsIndexProp &dst);

public:
	unsigned int ulTag = 0, cbData = 0;
	unsigned char *lpData = nullptr;
};

class ECsCells final : public ECsCacheEntry {
public:
	ECsCells(void) = default;
	~ECsCells();
	ECsCells(const ECsCells &);
	ECsCells &operator=(const ECsCells &);
	void AddPropVal(unsigned int tag, const struct propVal *);
	bool GetPropVal(unsigned int tag, struct propVal *, struct soap *, bool trunc) const;
	std::vector<unsigned int> GetPropTags() const;
	void UpdatePropVal(unsigned int tag, int delta);
	void UpdatePropVal(unsigned int tag, unsigned int mask, unsigned int value);

	void SetComplete(bool bComplete) { m_bComplete = bComplete; }
	bool GetComplete() const { return m_bComplete; }
	size_t GetSize() const;

    // All properties for this object; propTag => propVal
    std::map<unsigned int, struct propVal> mapPropVals;
	bool m_bComplete = false;
};

class ECsACLs final : public ECsCacheEntry {
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

} /* namespace KC */

namespace std {
	// hash function for type ECsIndexProp
	template<> struct hash<KC::ECsIndexProp> {
		public:
		size_t operator()(const KC::ECsIndexProp &value) const noexcept
		{
			/* Robert Sedgewick string hash function */
			unsigned int a = 63689, b = 378551, hash = 0;
			for (size_t i = 0; i < value.cbData; ++i) {
				hash = hash * a + value.lpData[i];
				a *= b;
			}
			return hash;
		}
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

#define CACHE_NO_PARENT 0xFFFFFFFF

class ECCacheManager final {
public:
	ECCacheManager(std::shared_ptr<ECConfig>, ECDatabaseFactory *lpDatabase);
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
	ECRESULT get_all_user_objects(objectclass_t, std::map<unsigned int, ECsUserObject> &out);

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
	ECRESULT GetObjectFromProp(unsigned int tag, unsigned int dsize, const unsigned char *data, unsigned int *objid);

	ECRESULT RemoveIndexData(unsigned int ulObjId);
	ECRESULT RemoveIndexData(unsigned int ulPropTag, unsigned int cbData, const unsigned char *lpData);
	ECRESULT RemoveIndexData(unsigned int ulPropTag, unsigned int ulObjId);

	// Read cache only
	ECRESULT QueryObjectFromProp(unsigned int tag, unsigned int dsize, const unsigned char *data, unsigned int *objid);

	ECRESULT SetObjectProp(unsigned int tag, unsigned int dsize, const unsigned char *data, unsigned int obj_id);
	void update_extra_stats(ECStatsCollector &);

	// Cache list of properties indexed by kopano-search
	ECRESULT GetExcludedIndexProperties(std::set<unsigned int>& set);
	ECRESULT SetExcludedIndexProperties(const std::set<unsigned int> &);

	// Test
	void DisableCellCache();
	void EnableCellCache();

private:
	typedef std::unordered_map<unsigned int, ECsQuota> ECMapQuota;
	typedef std::map<ECsIndexObject, ECsIndexProp> ECMapObjectToProp;

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
	ECCache<std::unordered_map<unsigned int, ECsObjects>> m_ObjectsCache;
	// Store cache
	ECCache<std::unordered_map<unsigned int, ECsStores>> m_StoresCache;
	// User cache
	ECCache<std::unordered_map<unsigned int, ECsUserObject>> m_UserObjectCache; /* userid to user object */
	ECCache<std::map<ECsUEIdKey, ECsUEIdObject>> m_UEIdObjectCache; /* user type + externid to user object */
	ECCache<std::unordered_map<unsigned int, ECsUserObjectDetails>>	m_UserObjectDetailsCache; /* userid to user obejct data */
	// ACL cache
	ECCache<std::unordered_map<unsigned int, ECsACLs>> m_AclCache;
	// Cell cache, include the column data of a loaded table
	ECCache<std::unordered_map<unsigned int, ECsCells>> m_CellCache;
	// Server cache
	ECCache<std::map<std::string, ECsServerDetails>> m_ServerDetailsCache;
	//Index properties
	ECCache<std::unordered_map<ECsIndexProp, ECsIndexObject>> m_PropToObjectCache;
	ECCache<ECMapObjectToProp> m_ObjectToPropCache;
	// Properties from kopano-search
	std::set<unsigned int> 		m_setExcludedIndexProperties;
	std::mutex m_hExcludedIndexPropertiesMutex;
	// Testing
	bool m_bCellCacheDisabled = false;
};

} /* namespace */

#endif
