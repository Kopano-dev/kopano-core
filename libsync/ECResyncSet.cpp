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
#include "ECResyncSet.h"

void ECResyncSet::Append(const SBinary &sbinSourceKey, const SBinary &sbinEntryID, const FILETIME &lastModTime)
{
	m_map.insert(map_type::value_type(
		array_type(sbinSourceKey.lpb, sbinSourceKey.lpb + sbinSourceKey.cb),
		storage_type(array_type(sbinEntryID.lpb, sbinEntryID.lpb + sbinEntryID.cb), lastModTime)));
}

bool ECResyncSet::Remove(const SBinary &sbinSourceKey)
{
	return m_map.erase(array_type(sbinSourceKey.lpb, sbinSourceKey.lpb + sbinSourceKey.cb)) == 1;
}


ECResyncSetIterator::ECResyncSetIterator(ECResyncSet &resyncSet)
: m_lpResyncSet(&resyncSet)
, m_iterator(m_lpResyncSet->m_map.begin())
{ }

ECResyncSetIterator::ECResyncSetIterator(ECResyncSet &resyncSet, const SBinary &sbinSourceKey)
: m_lpResyncSet(&resyncSet)
, m_iterator(m_lpResyncSet->m_map.find(ECResyncSet::array_type(sbinSourceKey.lpb, sbinSourceKey.lpb + sbinSourceKey.cb)))
{ }

bool ECResyncSetIterator::IsValid() const
{
	return m_lpResyncSet && m_iterator != m_lpResyncSet->m_map.end();
}

LPENTRYID ECResyncSetIterator::GetEntryID() const 
{
	return IsValid() ? (LPENTRYID)&m_iterator->second.entryId.front() : NULL;
}

ULONG ECResyncSetIterator::GetEntryIDSize() const
{
	return IsValid() ? m_iterator->second.entryId.size() : 0;
}

const FILETIME& ECResyncSetIterator::GetLastModTime() const
{
	return IsValid() ? m_iterator->second.lastModTime : s_nullTime;
}

ULONG ECResyncSetIterator::GetFlags() const
{
	return IsValid() ? m_iterator->second.flags : 0;
}

void ECResyncSetIterator::SetFlags(ULONG flags)
{
	if (IsValid())
		m_iterator->second.flags = flags;
}

void ECResyncSetIterator::Next()
{
	if (IsValid())
		++m_iterator;
}

const FILETIME ECResyncSetIterator::s_nullTime = {0, 0};