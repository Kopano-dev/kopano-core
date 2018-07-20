/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <string>
#include <kopano/platform.h>
#include <kopano/kcodes.h>
#include "ECCache.h"
#include <kopano/ECLogger.h>
#include <kopano/stringutil.h>

using namespace KC::string_literals;

namespace KC {

ECCacheBase::ECCacheBase(const std::string &strCachename, size_type ulMaxSize, long lMaxAge)
	: m_strCachename(strCachename)
	, m_ulMaxSize(ulMaxSize)
	, m_lMaxAge(lMaxAge)
{ }

void ECCacheBase::RequestStats(void(callback)(const std::string &, const std::string &, const std::string &, void*), void *obj)
{
	callback("cache_"s + m_strCachename + "_items", "Cache "s + m_strCachename + " items", stringify_int64(ItemCount()), obj);
	callback("cache_"s + m_strCachename + "_size", "Cache "s + m_strCachename + " size", stringify_int64(Size()), obj);
	callback("cache_"s + m_strCachename + "_maxsz", "Cache "s + m_strCachename + " maximum size", stringify_int64(m_ulMaxSize), obj);
	callback("cache_"s + m_strCachename + "_req", "Cache "s + m_strCachename + " requests", stringify_int64(HitCount()), obj);
	callback("cache_"s + m_strCachename + "_hit", "Cache "s + m_strCachename + " hits", stringify_int64(ValidCount()), obj);
}

void ECCacheBase::DumpStats(void) const
{
	auto strName = m_strCachename + " cache size:";
	unsigned long long z = Size();
	ec_log_info(
		"  %-30s  %8lu (%8llu bytes) (usage %.02f%%)",
		strName.c_str(), ItemCount(), z, z / (double)MaxSize() * 100.0);
	strName = m_strCachename + " cache hits:";
	z = ValidCount();
	ec_log_info("  %-30s  %8llu / %llu (%.02f%%)",
		strName.c_str(), z, static_cast<unsigned long long>(HitCount()),
		z / (double)HitCount() * 100.0);
}

} /* namespace */
