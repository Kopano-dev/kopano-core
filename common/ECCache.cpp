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

namespace KC {

ECCacheBase::ECCacheBase(const std::string &strCachename, size_type ulMaxSize, long lMaxAge)
	: m_strCachename(strCachename)
	, m_ulMaxSize(ulMaxSize)
	, m_lMaxAge(lMaxAge)
{ }

ECCacheStat ECCacheBase::get_stats() const
{
	ECCacheStat s;
	s.name = m_strCachename;
	s.items = ItemCount();
	s.size = Size();
	s.maxsize = m_ulMaxSize;
	s.req = HitCount();
	s.hit = ValidCount();
	return s;
}

} /* namespace */
