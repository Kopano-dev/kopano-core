/*
 * A negligible objectid_t -> DN cache.
 * It does not store any results (attribute values).
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <kopano/ECDefs.h>
#include <kopano/pcuser.hpp>
#include <kopano/timeutil.hpp>
#include "ECCache.h"
#include "plugin.h"

using namespace KC;
class LDAPUserPlugin;
namespace KC { class ECConfig; }

/**
 * @defgroup userplugin_ldap_cache LDAP user plugin cache
 * @ingroup userplugin_ldap
 * @{
 */


/* Cache type, std::string is LDAP DN string */
typedef std::map<objectid_t, std::string> dn_cache_t;
typedef std::list<std::string> dn_list_t;

/**
 * LDAP Cache which collects DNs with the matching
 * objectid and name.
 */
class LDAPCache final {
private:
	/* objectid => DN */
	std::recursive_mutex m_hMutex;
	dn_cache_t m_lpCompanyCache; /* CONTAINER_COMPANY */
	dn_cache_t m_lpGroupCache; /* OBJECTCLASS_DISTLIST */
	dn_cache_t m_lpUserCache; /* OBJECTCLASS_USER */
	dn_cache_t m_lpAddressListCache; /* CONTAINER_ADDRESSLIST */

	public:
	/* objectid => signatures */
	class timed_sglist_t : public signatures_t, public ECsCacheEntry {
		public:
		timed_sglist_t(const signatures_t &a) : signatures_t(a) {}
		timed_sglist_t(signatures_t &&a) : signatures_t(std::move(a)) {}
	};

	private:
	typedef ECCache<std::map<objectid_t, timed_sglist_t>> parent_cache_t;
	std::mutex m_parents_lock;
	std::map<userobject_relation_t, parent_cache_t> m_parent_cache;

public:
	/**
	 * Check if the requested objclass class is cached.
	 *
	 * @param[in]	objclass
	 *					The objectclass which could be cached.
	 * @return TRUE if the object class is cached.
	 */
	bool isObjectTypeCached(objectclass_t objclass);

	/**
	 * Add new entries for the object cache.
	 *
	 * @param[in]	objclass
	 *					The objectclass which should be cached.
	 * @param[in]	lpCache
	 *					The data to add to the cache.
	 */
	void setObjectDNCache(objectclass_t objclass, dn_cache_t &&);

	/**
	 * Obtain the cached data
	 *
	 * @param[in]	lpPlugin
	 *					Pointer to the plugin, if the objectclass was not cached,
	 *					lpPlugin->getAllObjects() will be called to fill the cache.
	 * @param[in]	objclass
	 *						The objectclass for which the cache is requested
	 * @return The cache data
	 */
	dn_cache_t getObjectDNCache(LDAPUserPlugin *, objectclass_t);

	/**
	 * Helper function: Search the cache for the direct parent for a DN.
	 * If the DN has multiple parents only the parent closest to the DN will be returned.
	 *
	 * @param[in]	lpCache
	 *					The cache which should be checked.
	 * @param[in]	dn
	 *					The DN for which the parent should be found.
	 * @return The cache entry of the parent. Contents will be empty if no parent was found.
	 */
	static objectid_t getParentForDN(const dn_cache_t &, const std::string &dn);

	/**
	 * Helper function: List all DNs which are hierarchially below the given DN.
	 *
	 * @param[in]	lpCache
	 *					The cache which should be checked.
	 * @param[in]	dn
	 *					The DN for which the children should be found
	 * @return The list of children for the DN
	 */
	static dn_list_t getChildrenForDN(const dn_cache_t &, const std::string &dn);

	/**
	 * Search the cache to obtain the DN for an object based on the object id.
	 *
	 * @param[in]	lpCache
	 *					The cache which should be checked.
	 * @param[in]	externid
	 *					The objectid which should be found in the cache
	 * @return the DN for the object id
	 */
	static std::string getDNForObject(const dn_cache_t &, const objectid_t &externid);

	/**
	 * Check if the given DN is present in the DN list
	 *
	 * @param[in]	lpList
	 *					The list which should be checked
	 * @param[in]	dn
	 *					The DN which should be found in the list
	 * @return TRUE if the DN is found in the list
	 */
	static bool isDNInList(const dn_list_t &, const std::string &dn);

	std::pair<bool, signatures_t> get_parents(userobject_relation_t, const objectid_t &);
	void set_parents(userobject_relation_t, const objectid_t &, const signatures_t &, ECConfig * = nullptr);
};

/** @} */
