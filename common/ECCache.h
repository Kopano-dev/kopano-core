/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECCACHE_INCLUDED
#define ECCACHE_INCLUDED

#include <kopano/zcdefs.h>
#include <list>
#include <string>
#include <vector>
#include <utility>
#include <cassert>
#include <cstdint>
#include <kopano/ECLogger.h>
#include <kopano/platform.h>
#include <kopano/kcodes.h> /* ECRESULT */

namespace KC {

template<typename Key> class KeyEntry KC_FINAL {
public:
	Key key;
	time_t ulLastAccess;
};

template<typename Key>
bool KeyEntryOrder(const KeyEntry<Key> &a, const KeyEntry<Key> &b) {
	return a.ulLastAccess < b.ulLastAccess;
}

template<typename Value> inline size_t GetCacheAdditionalSize(const Value &val)
{
	/*
	 * Trivial destructability is an indicator for a "simple" struct.
	 * Conversely, !TD indicates there may be pointers, which a
	 * specialization of GCA should analyze.
	 */
	if (!std::is_trivially_destructible<Value>::value)
		ec_log_notice("K-1035: dev: please specialize for %s", __PRETTY_FUNCTION__);
	return 0;
}

template<> inline size_t GetCacheAdditionalSize(const std::string &s)
{
	return MEMORY_USAGE_STRING(s);
}

class ECsCacheEntry {
public:
	time_t ulLastAccess = 0;
};

struct ECCacheStat {
	std::string name;
	uint64_t items, size, maxsize, req, hit;
};

class KC_EXPORT ECCacheBase {
public:
	typedef unsigned long		count_type;
	typedef size_t size_type;

	KC_HIDDEN virtual ~ECCacheBase() = default;
	KC_HIDDEN virtual count_type ItemCount() const = 0;
	KC_HIDDEN virtual size_type Size() const = 0;
	KC_HIDDEN size_type MaxSize() const { return m_ulMaxSize; }
	KC_HIDDEN long MaxAge() const { return m_lMaxAge; }
	KC_HIDDEN size_type HitCount() const { return m_ulCacheHit; }
	KC_HIDDEN size_type ValidCount() const { return m_ulCacheValid; }

	// Decrement the valid count. Used from ECCacheManger::GetCell.
	KC_HIDDEN void DecrementValidCount()
	{
		assert(m_ulCacheValid >= 1);
		--m_ulCacheValid;
	}

	// Call the provided callback with some statistics.
	ECCacheStat get_stats() const;

	// Dump statistics
	void SetMaxSize(size_type ulMaxSize)
	{
		m_ulMaxSize = ulMaxSize;
	}

protected:
	ECCacheBase(const std::string &strCachename, size_type ulMaxSize, long lMaxAge);
	KC_HIDDEN void IncrementHitCount() { ++m_ulCacheHit; }
	KC_HIDDEN void IncrementValidCount() { ++m_ulCacheValid; }
	KC_HIDDEN void ClearCounters() { m_ulCacheHit = m_ulCacheValid = 0; }

private:
	const std::string	m_strCachename;
	size_type		m_ulMaxSize;
	const long			m_lMaxAge;
	size_type m_ulCacheHit = 0, m_ulCacheValid = 0;
};

template<typename MapType> class ECCache KC_FINAL : public ECCacheBase {
public:
	typedef typename MapType::key_type key_type;
	typedef typename MapType::mapped_type mapped_type;

	ECCache(const std::string &strCachename, size_type ulMaxSize, long lMaxAge)
		: ECCacheBase(strCachename, ulMaxSize, lMaxAge)
		, m_ulSize(0)
	{ }

	ECRESULT ClearCache()
	{
		m_map.clear();
		m_ulSize = 0;
		ClearCounters();
		return erSuccess;
	}

	count_type ItemCount() const override
	{
		return m_map.size();
	}

	size_type Size() const override
	{
		/* It works with map and unordered_map. */
		return m_map.size() * sizeof(typename MapType::value_type) + sizeof(MapType) + m_ulSize;
	}

	ECRESULT RemoveCacheItem(const key_type &key)
	{
		auto iter = m_map.find(key);
		if (iter == m_map.end())
			return KCERR_NOT_FOUND;

		m_ulSize -= GetCacheAdditionalSize(iter->second);
		m_ulSize -= GetCacheAdditionalSize(key);
		m_map.erase(iter);
		return erSuccess;
	}

	ECRESULT GetCacheItem(const key_type &key, mapped_type **lppValue)
	{
		time_t	tNow  = GetProcessTime();
		auto iter = m_map.find(key);

		if (iter == m_map.end()) {
			IncrementHitCount();
			return KCERR_NOT_FOUND;
		}
		if (MaxAge() == 0 || static_cast<long>(tNow - iter->second.ulLastAccess) < MaxAge()) {
			*lppValue = &iter->second;
			// If we have an aging cache, we don't update the timestamp,
			// so we can't keep a value longer in the cache than the max age.
			// If we have a non-aging cache, we need to update it,
			// to see the oldest 5% to purge from the cache.
			if (MaxAge() == 0)
				iter->second.ulLastAccess = tNow;
			IncrementHitCount();
			IncrementValidCount();
			return erSuccess;
		}
		// Cache age of the cached item, if expired remove the item from the cache
		/*
		 * Because of templates, there is no guarantee
		 * that m_map keeps iterators valid while
		 * elements are deleted from it. Track them in
		 * a separate delete list.
		 */
		std::vector<key_type> dl;

		// Loop through all items and check
		for (iter = m_map.begin(); iter != m_map.end(); ++iter)
			if ((long)(tNow - iter->second.ulLastAccess) >= MaxAge())
				dl.emplace_back(iter->first);
		for (const auto &i : dl)
			m_map.erase(i);
		IncrementHitCount();
		return KCERR_NOT_FOUND;
	}

	ECRESULT GetCacheRange(const key_type &lower, const key_type &upper, std::list<typename MapType::value_type> *values)
	{
		auto iLower = m_map.lower_bound(lower);
		auto iUpper = m_map.upper_bound(upper);
		for (auto i = iLower; i != iUpper; ++i)
			values->emplace_back(*i);
		return erSuccess;
	}

	ECRESULT AddCacheItem(const key_type &key, const mapped_type &value)
	{
		if (MaxSize() == 0)
			return erSuccess;
		auto result = m_map.emplace(key, value);
		if (!result.second) {
			// The key already exists but its value is unmodified. So update it now
			m_ulSize += GetCacheAdditionalSize(value);
			m_ulSize -= GetCacheAdditionalSize(result.first->second);
			result.first->second = value;
			result.first->second.ulLastAccess = GetProcessTime();
			// Since there is a very small chance that we need to purge the cache, we're skipping that here.
			return erSuccess;
		}
		// We just inserted a new entry.
		m_ulSize += GetCacheAdditionalSize(value);
		m_ulSize += GetCacheAdditionalSize(key);
		result.first->second.ulLastAccess = GetProcessTime();
		UpdateCache(0.05F);
		return erSuccess;
	}

#ifdef KC_USES_CXX17
	ECRESULT AddCacheItem(const key_type &key, mapped_type &&value)
	{
		if (MaxSize() == 0)
			return erSuccess;
		auto result = m_map.try_emplace(key, std::move(value));
		if (!result.second) {
			/*
			 * The key already exists but its value is unmodified,
			 * so update it now. insert_or_assign cannot be used,
			 * because then the old value is gone and its size
			 * could not be subtracted.
			 */
			m_ulSize += GetCacheAdditionalSize(value);
			m_ulSize -= GetCacheAdditionalSize(result.first->second);
			result.first->second = std::move(value);
			result.first->second.ulLastAccess = GetProcessTime();
			return erSuccess;
		}
		/* New entry */
		m_ulSize += GetCacheAdditionalSize(result.first->second);
		m_ulSize += GetCacheAdditionalSize(key);
		result.first->second.ulLastAccess = GetProcessTime();
		UpdateCache(0.05);
		return erSuccess;
	}
#endif

	// Used in ECCacheManager::SetCell, where the content of a cache item is modified.
	ECRESULT AddToSize(int64_t ulSize)
	{
		m_ulSize += ulSize;
		return erSuccess;
	}

private:
	ECRESULT PurgeCache(float ratio)
	{
		std::list<KeyEntry<key_type> > lstEntries;

		for (const auto &im : m_map) {
			KeyEntry<key_type> k;
			k.key = im.first;
			k.ulLastAccess = im.second.ulLastAccess;
			lstEntries.emplace_back(std::move(k));
		}

		lstEntries.sort(KeyEntryOrder<key_type>);
		// We now have a list of all cache items, sorted by access time, (oldest first)
		size_t ulDelete = m_map.size() * ratio;

		// Remove the oldest ulDelete entries from the cache, removing [ratio] % of all
		// cache entries.
		for (auto iterEntry = lstEntries.cbegin();
		     iterEntry != lstEntries.cend() && ulDelete > 0;
		     ++iterEntry, --ulDelete) {
			auto iterMap = m_map.find(iterEntry->key);
			assert(iterMap != m_map.end());
			m_ulSize -= GetCacheAdditionalSize(iterMap->second);
			m_ulSize -= GetCacheAdditionalSize(iterMap->first);
			m_map.erase(iterMap);
		}

		return erSuccess;
	}

	ECRESULT UpdateCache(float ratio)
	{
		if (Size() > MaxSize())
			PurgeCache(ratio);
		return erSuccess;
	}

	MapType m_map;
	size_type			m_ulSize;
};

} /* namespace */

#endif // ndef ECCACHE_INCLUDED
