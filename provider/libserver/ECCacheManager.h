/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>
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

class Stores final : public CacheEntry {
public:
	unsigned int	ulStore, ulType;
	GUID			guidStore;
};

class UserObject final : public CacheEntry {
public:
	objectclass_t		ulClass;
	std::string strExternId, strSignature;
	unsigned int		ulCompanyId;
};

/* same as objectid_t, join? */
struct UEIdKey {
	objectclass_t		ulClass;
	std::string			strExternId;
};

inline bool operator <(const UEIdKey &a, const UEIdKey &b)
{
	return std::tie(a.ulClass, a.strExternId) <
	       std::tie(b.ulClass, b.strExternId);
}

/* Intern Id cache */
class UEIdObject final : public CacheEntry {
public:
	unsigned int ulCompanyId, ulUserId;
	std::string			strSignature;
};

class UserObjectDetails final : public CacheEntry {
public:
	objectdetails_t			sDetails;
};

class ServerDetails final : public CacheEntry {
public:
	serverdetails_t			sDetails;
};

class Objects final : public CacheEntry {
public:
	unsigned int ulParent, ulOwner, ulFlags, ulType;
};

class Quota final : public CacheEntry {
public:
	quotadetails_t	quota;
};

class IndexObject final : public CacheEntry {
public:
	inline bool operator==(const IndexObject &other) const noexcept
	{
		return ulObjId == other.ulObjId && ulTag == other.ulTag;
	}

	inline bool operator<(const IndexObject &other) const noexcept
	{
		return std::tie(ulObjId, ulTag) < std::tie(other.ulObjId, other.ulTag);
	}

	unsigned int ulObjId, ulTag;
};

class IndexProp final : public CacheEntry {
public:
	IndexProp() = default;
	~IndexProp() {
		delete[] lpData;
	}

	IndexProp(const IndexProp &src) {
		if (this != &src)
			Copy(src, *this);
    }

	IndexProp(IndexProp &&o) :
		ulTag(o.ulTag), cbData(o.cbData), lpData(o.lpData)
	{
		o.lpData = nullptr;
		o.cbData = 0;
	}

	IndexProp(unsigned int tag, const unsigned char *d, unsigned int z)
	{
		SetValue(tag, d, z);
	}

	IndexProp &operator=(const IndexProp &src) {
		if (this == &src)
			return *this;
		Free();
		Copy(src, *this);
		return *this;
    }

	bool operator<(const IndexProp &) const noexcept;
	bool operator==(const IndexProp &) const noexcept;
	void SetValue(unsigned int tag, const unsigned char *data, unsigned int z);

protected:
	void Free() {
		delete[] lpData;
		ulTag = 0;
		cbData = 0;
		lpData = NULL;
	}

	void Copy(const IndexProp &src, IndexProp &dst);

public:
	unsigned int ulTag = 0, cbData = 0;
	unsigned char *lpData = nullptr;
};

class Cells final : public CacheEntry {
public:
	Cells() = default;
	~Cells();
	Cells(const Cells &);
	Cells &operator=(const Cells &);
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

class ACLs final : public CacheEntry {
public:
	ACLs() = default;
	ACLs(const ACLs &src) : ulACLs(src.ulACLs)
	{
		aACL.reset(new ACL[ulACLs]);
		memcpy(aACL.get(), src.aACL.get(), sizeof(ACL) * ulACLs);
	}
	ACLs &operator=(const ACLs &src) {
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

struct SortKeyKey {
	sObjectTableKey	sKey;
	unsigned int	ulPropTag;
};

} /* namespace KC */

namespace std {
	// hash function for type ECsIndexProp
	template<> struct hash<KC::IndexProp> {
		public:
		size_t operator()(const KC::IndexProp &value) const noexcept
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
	template<> struct hash<KC::IndexObject> {
		public:
		size_t operator()(const KC::IndexObject &value) const noexcept
		{
					hash<unsigned int> hasher;
					// @TODO check the hash function!
					return hasher(value.ulObjId * value.ulTag ) ;
			}
	};
}

namespace KC {

#define CACHE_NO_PARENT 0xFFFFFFFF

enum {
	KC_GETCELL_TRUNCATE = 1 << 0,
	KC_GETCELL_NOTRUNC  = 0,
	KC_GETCELL_NEGATIVES = 1 << 1,
};

class ECCacheManager final {
public:
	ECCacheManager(std::shared_ptr<ECConfig>, ECDatabaseFactory *lpDatabase);
	virtual ~ECCacheManager();
	ECRESULT PurgeCache(unsigned int ulFlags);

	// These are read-through (i.e. they access the DB if they can't find the data)
	ECRESULT GetParent(unsigned int ulObjId, unsigned int *ulParent);
	ECRESULT GetOwner(unsigned int ulObjId, unsigned int *ulOwner);
	ECRESULT GetObject(unsigned int ulObjId, unsigned int *ulParent, unsigned int *ulOwner, unsigned int *ulFlags, unsigned int *ulType = NULL);
	ECRESULT SetObject(unsigned int ulObjId, unsigned int ulParent, unsigned int ulOwner, unsigned int ulFlags, unsigned int ulType);
	// Query cache only
	ECRESULT QueryParent(unsigned int ulObjId, unsigned int *ulParent);

	ECRESULT GetObjects(const std::list<sObjectTableKey> &lstObjects, std::map<sObjectTableKey, Objects> &mapObjects);
	ECRESULT GetObjectsFromProp(unsigned int ulTag, const std::vector<unsigned int> &cbdata, const std::vector<unsigned char *> &lpdata, std::map<IndexProp, unsigned int> &mapObjects);
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
	ECRESULT get_all_user_objects(objectclass_t, bool hosted, unsigned int company, std::map<unsigned int, UserObject> &out);

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
	ECRESULT GetCell(const sObjectTableKey *, unsigned int tag, struct propVal *, struct soap *, unsigned int flags = KC_GETCELL_TRUNCATE);
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

	bool m_bCellCacheDisabled = false;

private:
	typedef std::unordered_map<unsigned int, Quota> ECMapQuota;
	typedef std::map<IndexObject, IndexProp> ECMapObjectToProp;

	// cache functions
	ECRESULT I_GetACLs(unsigned int obj_id, struct rightsArray **);
	void I_DelACLs(unsigned int obj_id);
	ECRESULT I_GetObject(unsigned int obj_id, unsigned int *parent, unsigned int *owner, unsigned int *flags, unsigned int *type);
	void I_DelObject(unsigned int obj_id);
	ECRESULT I_GetStore(unsigned int obj_id, unsigned int *store, GUID *, unsigned int *type);
	void I_DelStore(unsigned int obj_id);
	ECRESULT I_AddUserObject(unsigned int user_id, const objectclass_t &, unsigned int company_id, const std::string &ext_id, const std::string &signautre);
	ECRESULT I_GetUserObject(unsigned int user_id, objectclass_t *, unsigned int *company_id, std::string *extern_id, std::string *signature);
	void I_DelUserObject(unsigned int user_id);
	ECRESULT I_AddUEIdObject(const std::string &ext_id, const objectclass_t &, unsigned int company_id, unsigned int user_id, const std::string &signature);
	ECRESULT I_GetUEIdObject(const std::string &ext_id, objectclass_t, unsigned int *company_id, unsigned int *user_id, std::string *signature);
	void I_DelUEIdObject(const std::string &ext_id, objectclass_t);
	ECRESULT I_AddUserObjectDetails(unsigned int, const objectdetails_t &);
	ECRESULT I_GetUserObjectDetails(unsigned int user_id, objectdetails_t *);
	void I_DelUserObjectDetails(unsigned int user_id);
	void I_DelCell(unsigned int obj_id);
	ECRESULT I_GetQuota(unsigned int user_id, bool bIsDefaultQuota, quotadetails_t *quota);
	void I_DelQuota(unsigned int user_id, bool is_dfl_quota);

	// Cache Index properties
	ECRESULT I_AddIndexData(const IndexObject &, const IndexProp &);

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
	// "hierarchy" table
	ECCache<std::unordered_map<unsigned int, Objects>> m_ObjectsCache;
	// Store cache (objid -> storeid/guid)
	ECCache<std::unordered_map<unsigned int, Stores>> m_StoresCache;
	// User cache
	ECCache<std::unordered_map<unsigned int, UserObject>> m_UserObjectCache; /* userid to user object */
	ECCache<std::map<UEIdKey, UEIdObject>> m_UEIdObjectCache; /* user type + externid to user object */
	ECCache<std::unordered_map<unsigned int, UserObjectDetails>>	m_UserObjectDetailsCache; /* userid to user object data */
	// ACL cache
	ECCache<std::unordered_map<unsigned int, ACLs>> m_AclCache;
	// properties and tproperties
	ECCache<std::unordered_map<unsigned int, Cells>> m_CellCache;
	// Server cache
	ECCache<std::map<std::string, ServerDetails>> m_ServerDetailsCache;
	// "indexedproperties" index2: {tag, data(entryid or sourcekey)} -> {objid,tag}
	ECCache<std::unordered_map<IndexProp, IndexObject>> m_PropToObjectCache;
	// "indexedproperties" index1: {objid,tag} -> {tag, data}
	ECCache<ECMapObjectToProp> m_ObjectToPropCache;
	// Properties from kopano-search
	std::set<unsigned int> 		m_setExcludedIndexProperties;
	std::mutex m_hExcludedIndexPropertiesMutex;
};

} /* namespace */
