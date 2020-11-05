/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <mutex>
#include <utility>
#include <string>
#include <kopano/platform.h>
#include <kopano/ECConfig.h>
#include "LDAPCache.h"
#include "LDAPUserPlugin.h"
#include <kopano/stringutil.h>

using namespace KC;

dn_cache_t held_dn_cache_t::dummy_cache;

namespace KC {

template<> inline size_t GetCacheAdditionalSize(const objectsignature_t &v)
{
	return v.get_object_size() - sizeof(v);
}

template<> inline size_t GetCacheAdditionalSize(const objectid_t &v)
{
	return v.get_object_size() - sizeof(v);
}

template<> size_t GetCacheAdditionalSize(const LDAPCache::timed_sglist_t &t)
{
	size_t z = 0;
	for (const auto &e : t)
		z += GetCacheAdditionalSize(e);
	return z;
}

}

bool LDAPCache::isObjectTypeCached(objectclass_t objclass)
{
	scoped_rlock biglock(m_hMutex);

	switch (objclass) {
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		return !m_lpUserCache.empty();
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		return !m_lpGroupCache.empty();
	case CONTAINER_COMPANY:
		return !m_lpCompanyCache.empty();
	case CONTAINER_ADDRESSLIST:
		return !m_lpAddressListCache.empty();
	default:
		return false;
	}
}

void LDAPCache::setObjectDNCache(objectclass_t objclass, dn_cache_t &&lpCache)
{
	/* Always merge caches rather then overwriting them. */
	auto lpTmp = getObjectDNCache(nullptr, objclass);
	// cannot use insert() because it does not override existing entries
	for (const auto &i : lpCache)
		lpTmp.get()[i.first] = std::move(i.second);
}

held_dn_cache_t
LDAPCache::getObjectDNCache(LDAPUserPlugin *lpPlugin, objectclass_t objclass)
{
	std::unique_lock<std::recursive_mutex> biglock(m_hMutex);

	/* If item was not yet cached, make sure it is done now. */
	if (!isObjectTypeCached(objclass) && lpPlugin)
		lpPlugin->getAllObjects(objectid_t(), objclass); // empty company, so request all objects of type

	switch (objclass) {
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		return {m_lpUserCache, std::move(biglock)};
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		return {m_lpGroupCache, std::move(biglock)};
	case CONTAINER_COMPANY:
		return {m_lpCompanyCache, std::move(biglock)};
	case CONTAINER_ADDRESSLIST:
		return {m_lpAddressListCache, std::move(biglock)};
	default:
		return {};
	}
}

objectid_t LDAPCache::getParentForDN(const dn_cache_t &lpCache,
    const std::string &dn)
{
	objectid_t entry;
	std::string parent_dn;

	if (lpCache.empty())
		return entry; /* empty */

	// @todo make sure we find the largest DN match
	for (const auto &i : lpCache)
		/* Key should be larger than the current guess, but has to be smaller than the userobject dn. */
		/* If key matches the end of the userobject dn, we have a positive match */
		if (i.second.size() > parent_dn.size() && i.second.size() < dn.size() &&
		    strcasecmp(dn.c_str() + (dn.size() - i.second.size()), i.second.c_str()) == 0) {
			parent_dn = i.second;
			entry = i.first;
		}

	/* Either empty, or the correct result */
	return entry;
}

dn_list_t
LDAPCache::getChildrenForDN(const dn_cache_t &lpCache, const std::string &dn)
{
	dn_list_t list;

	/* Find al DNs which are hierarchically below the given dn */
	for (const auto &i : lpCache)
		/* Key should be larger than the root DN. */
		/* If key matches the end of the root dn, we have a positive match */
		if (i.second.size() > dn.size() &&
		    strcasecmp(i.second.c_str() + (i.second.size() - dn.size()), dn.c_str()) == 0)
			list.emplace_back(i.second);
	return list;
}

std::string
LDAPCache::getDNForObject(const dn_cache_t &lpCache, const objectid_t &externid)
{
	dn_cache_t::const_iterator it = lpCache.find(externid);
	return it == lpCache.cend() ? std::string() : it->second;
}

bool LDAPCache::isDNInList(const dn_list_t &lpList, const std::string &dn)
{
	/* We were given a DN, check if a parent of that dn is listed as filtered */
	for (const auto &i : lpList)
		/* Key should be larger or equal then user DN */
		/* If key matches the end of the user dn, we have a positive match */
		if (i.size() <= dn.size() &&
		    strcasecmp(dn.c_str() + (dn.size() - i.size()), i.c_str()) == 0)
			return true;

	return false;
}

std::pair<bool, signatures_t> LDAPCache::get_parents(userobject_relation_t rel,
    const objectid_t &child)
{
	ulock_normal lock(m_parents_lock);
	signatures_t sigstor;
	timed_sglist_t *sigp = nullptr;
	auto ci = m_parent_cache.find(rel);
	if (ci == m_parent_cache.cend())
		return {false, sigstor};
	auto &cac = ci->second;
	auto ret = cac.GetCacheItem(child, &sigp);
	if (ret != erSuccess)
		return {false, sigstor};
	sigstor = *sigp;
	lock.unlock();
	return {true, sigstor};
}

void LDAPCache::set_parents(userobject_relation_t rel, const objectid_t &child,
    const signatures_t &sig, ECConfig *cfg)
{
	std::lock_guard<std::mutex> lock(m_parents_lock);
	auto ci = m_parent_cache.find(rel);
	if (ci == m_parent_cache.cend()) {
		size_t size = 256 << 10;
		unsigned int lft = 300;
		if (cfg != nullptr) {
			auto v = cfg->GetSetting("ldap_membership_cache_size");
			if (v != nullptr)
				size = strtoull(v, nullptr, 0);
			v = cfg->GetSetting("ldap_membership_cache_lifetime");
			if (v != nullptr)
				lft = strtoul(v, nullptr, 0) * 60;
		}
		ci = m_parent_cache.emplace(rel, parent_cache_t("ldapcache-parent", size, lft)).first;
	}
	auto &cac = ci->second;
	cac.AddCacheItem(child, sig);
}
