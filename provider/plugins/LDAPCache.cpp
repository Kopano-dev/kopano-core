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
#include <kopano/lockhelper.hpp>
#include "LDAPCache.h"
#include "LDAPUserPlugin.h"
#include <kopano/stringutil.h>

LDAPCache::LDAPCache()
{
	m_lpCompanyCache.reset(new dn_cache_t());
	m_lpGroupCache.reset(new dn_cache_t());
	m_lpUserCache.reset(new dn_cache_t());
	m_lpAddressListCache.reset(new dn_cache_t());
}

bool LDAPCache::isObjectTypeCached(objectclass_t objclass)
{
	bool bCached = false;
	scoped_rlock biglock(m_hMutex);

	switch (objclass) {
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		bCached = !m_lpUserCache->empty();
		break;
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		bCached = !m_lpGroupCache->empty();
		break;
	case CONTAINER_COMPANY:
		bCached = !m_lpCompanyCache->empty();
		break;
	case CONTAINER_ADDRESSLIST:
		bCached = !m_lpAddressListCache->empty();
		break;
	default:
		break;
	}
	return bCached;
}

void LDAPCache::setObjectDNCache(objectclass_t objclass,
    std::unique_ptr<dn_cache_t> lpCache)
{
	/*
	 * Always merge caches rather then overwritting them.
	 */
	std::unique_ptr<dn_cache_t> lpTmp = getObjectDNCache(NULL, objclass);
	// cannot use insert() because it does not override existing entries
	for (const auto &i : *lpCache)
		(*lpTmp)[i.first] = i.second;
	lpCache = std::move(lpTmp);

	scoped_rlock biglock(m_hMutex);
	switch (objclass) {
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		m_lpUserCache = std::move(lpCache);
		break;
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		m_lpGroupCache = std::move(lpCache);
		break;
	case CONTAINER_COMPANY:
		m_lpCompanyCache = std::move(lpCache);
		break;
	case CONTAINER_ADDRESSLIST:
		m_lpAddressListCache = std::move(lpCache);
		break;
	default:
		break;
	}
}

std::unique_ptr<dn_cache_t>
LDAPCache::getObjectDNCache(LDAPUserPlugin *lpPlugin, objectclass_t objclass)
{
	std::unique_ptr<dn_cache_t> cache;
	scoped_rlock biglock(m_hMutex);

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
		cache.reset(new dn_cache_t(*m_lpUserCache.get()));
		break;
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		cache.reset(new dn_cache_t(*m_lpGroupCache.get()));
		break;
	case CONTAINER_COMPANY:
		cache.reset(new dn_cache_t(*m_lpCompanyCache.get()));
		break;
	case CONTAINER_ADDRESSLIST:
		cache.reset(new dn_cache_t(*m_lpAddressListCache.get()));
		break;
	default:
		break;
	}
	return cache;
}

objectid_t LDAPCache::getParentForDN(const std::unique_ptr<dn_cache_t> &lpCache,
    const std::string &dn)
{
	objectid_t entry;
	std::string parent_dn;

	if (lpCache->empty())
		return entry; /* empty */

	// @todo make sure we find the largest DN match
	for (const auto &i : *lpCache)
		/* Key should be larger then current guess, but has to be smaller then the userobject dn */
		/* If key matches the end of the userobject dn, we have a positive match */
		if (i.second.size() > parent_dn.size() && i.second.size() < dn.size() &&
		    strcasecmp(dn.c_str() + (dn.size() - i.second.size()), i.second.c_str()) == 0) {
			parent_dn = i.second;
			entry = i.first;
		}

	/* Either empty, or the correct result */
	return entry;
}

std::unique_ptr<dn_list_t>
LDAPCache::getChildrenForDN(const std::unique_ptr<dn_cache_t> &lpCache,
    const std::string &dn)
{
	std::unique_ptr<dn_list_t> list(new dn_list_t());

	/* Find al DNs which are hierarchically below the given dn */
	for (const auto &i : *lpCache)
		/* Key should be larger then root DN */
		/* If key matches the end of the root dn, we have a positive match */
		if (i.second.size() > dn.size() &&
		    strcasecmp(i.second.c_str() + (i.second.size() - dn.size()), dn.c_str()) == 0)
			list->push_back(i.second);
	return list;
}

std::string
LDAPCache::getDNForObject(const std::unique_ptr<dn_cache_t> &lpCache,
    const objectid_t &externid)
{
	dn_cache_t::const_iterator it = lpCache->find(externid);
	return it == lpCache->cend() ? std::string() : it->second;
}

bool LDAPCache::isDNInList(const std::unique_ptr<dn_list_t> &lpList,
    const std::string &dn)
{
	/* We were given a DN, check if a parent of that dn is listed as filterd */
	for (const auto &i : *lpList)
		/* Key should be larger or equal then user DN */
		/* If key matches the end of the user dn, we have a positive match */
		if (i.size() <= dn.size() &&
		    strcasecmp(dn.c_str() + (dn.size() - i.size()), i.c_str()) == 0)
			return true;

	return false;
}
