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

#ifndef ECCACHE_INCLUDED
#define ECCACHE_INCLUDED

#include <kopano/zcdefs.h>
#include <list>
#include <string>
#include <vector>
#include <cassert>

#include <kopano/platform.h>

class ECLogger;

template<typename Key>
class KeyEntry _zcp_final {
public:
	Key key;
	time_t ulLastAccess;
};

template<typename Key>
bool KeyEntryOrder(const KeyEntry<Key> &a, const KeyEntry<Key> &b) {
	return a.ulLastAccess < b.ulLastAccess;
}

template<typename Value>
unsigned int GetCacheAdditionalSize(const Value &val) {
	return 0;
}

class ECsCacheEntry {
public:
	ECsCacheEntry() { ulLastAccess = 0; }

	time_t 	ulLastAccess;
};

class ECCacheBase
{
public:
	typedef unsigned long		count_type;
		typedef uint64_t	size_type;

	virtual ~ECCacheBase(void) {}

	virtual count_type ItemCount() const = 0;
	virtual size_type Size() const = 0;
	
	size_type MaxSize() const { return m_ulMaxSize; }
	long MaxAge() const { return m_lMaxAge; }
	size_type HitCount() const { return m_ulCacheHit; }
	size_type ValidCount() const { return m_ulCacheValid; }

	// Decrement the valid count. Used from ECCacheManger::GetCell.
	void DecrementValidCount() { 
		assert(m_ulCacheValid >= 1);
		--m_ulCacheValid;
	}

	// Call the provided callback with some statistics.
	void RequestStats(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj);

	// Dump statistics
	void DumpStats(void) const;

protected:
	ECCacheBase(const std::string &strCachename, size_type ulMaxSize, long lMaxAge);
	void IncrementHitCount(void) { ++m_ulCacheHit; }
	void IncrementValidCount(void) { ++m_ulCacheValid; }
	void ClearCounters() { m_ulCacheHit = m_ulCacheValid = 0; }

private:
	const std::string	m_strCachename;
	const size_type		m_ulMaxSize;
	const long			m_lMaxAge;
	size_type			m_ulCacheHit;
	size_type			m_ulCacheValid;
};


template<typename _MapType>
class ECCache _zcp_final : public ECCacheBase
{
public:
	typedef typename _MapType::key_type		key_type;
	typedef typename _MapType::mapped_type	mapped_type;
	
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
	
	count_type ItemCount() const _zcp_override
	{
		return m_map.size();
	}
	
	size_type Size() const _zcp_override
	{
		// it works with map and hash_map
		return (m_map.size() * (sizeof(typename _MapType::value_type) + sizeof(_MapType) )) + m_ulSize;
	}

	ECRESULT RemoveCacheItem(const key_type &key) 
	{
		typename _MapType::iterator iter;

		iter = m_map.find(key);
		if (iter == m_map.end())
			return KCERR_NOT_FOUND;

		m_ulSize -= GetCacheAdditionalSize(iter->second);
		m_ulSize -= GetCacheAdditionalSize(key);
		m_map.erase(iter);
		return erSuccess;
	}
	
	ECRESULT GetCacheItem(const key_type &key, mapped_type **lppValue)
	{
		ECRESULT er = erSuccess;
		time_t	tNow  = GetProcessTime();
		typename _MapType::iterator iter;

		iter = m_map.find(key);
		
		if (iter != m_map.end()) {
			// Cache age of the cached item, if expired remove the item from the cache
			if (MaxAge() != 0 && (long)(tNow - iter->second.ulLastAccess) >= MaxAge()) {
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
						dl.push_back(iter->first);
				for (const auto &i : dl)
					m_map.erase(i);
				er = KCERR_NOT_FOUND;
			} else {
				*lppValue = &iter->second;
				// If we have an aging cache, we don't update the timestamp,
				// so we can't keep a value longer in the cache than the max age.
				// If we have a non-aging cache, we need to update it,
				// to see the oldest 5% to purge from the cache.
				if (MaxAge() == 0)
					iter->second.ulLastAccess = tNow;
				er = erSuccess;
			}
		} else {
			er = KCERR_NOT_FOUND;
		}

		IncrementHitCount();
		if (er == erSuccess)
			IncrementValidCount();

		return er;
	}

	ECRESULT GetCacheRange(const key_type &lower, const key_type &upper, std::list<typename _MapType::value_type> *values)
	{
		typedef typename _MapType::iterator iterator;

		iterator iLower = m_map.lower_bound(lower);
		iterator iUpper = m_map.upper_bound(upper);
		for (iterator i = iLower; i != iUpper; ++i)
			values->push_back(*i);

		return erSuccess;
	}
	
	ECRESULT AddCacheItem(const key_type &key, const mapped_type &value)
	{
		typedef typename _MapType::value_type value_type;
		typedef typename _MapType::iterator iterator;
		std::pair<iterator,bool> result;

		if (MaxSize() == 0)
			return erSuccess;

		result = m_map.insert(value_type(key, value));

		if (result.second == false) {
			// The key already exists but its value is unmodified. So update it now
			m_ulSize += GetCacheAdditionalSize(value);
			m_ulSize -= GetCacheAdditionalSize(result.first->second);
			result.first->second = value;
			result.first->second.ulLastAccess = GetProcessTime();
			// Since there is a very small chance that we need to purge the cache, we're skipping that here.
		} else {
			// We just inserted a new entry.
			m_ulSize += GetCacheAdditionalSize(value);
			m_ulSize += GetCacheAdditionalSize(key);
			
			result.first->second.ulLastAccess = GetProcessTime();
			
			UpdateCache(0.05F);
		}

		return erSuccess;
	}
	
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
		typename std::list<KeyEntry<key_type> >::iterator iterEntry;

		for (const auto &im : m_map) {
			KeyEntry<key_type> k;
			k.key = im.first;
			k.ulLastAccess = im.second.ulLastAccess;
			lstEntries.push_back(k);
		}

		lstEntries.sort(KeyEntryOrder<key_type>);

		// We now have a list of all cache items, sorted by access time, (oldest first)
		unsigned int ulDelete = (unsigned int)(m_map.size() * ratio);

		// Remove the oldest ulDelete entries from the cache, removing [ratio] % of all
		// cache entries.
		for (iterEntry = lstEntries.begin(); iterEntry != lstEntries.end() && ulDelete > 0; ++iterEntry, --ulDelete) {
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
		if( Size() > MaxSize()) {
			PurgeCache(ratio);
		}

		return erSuccess;
	}

private:
	_MapType			m_map;	
	size_type			m_ulSize;
};

#endif // ndef ECCACHE_INCLUDED
