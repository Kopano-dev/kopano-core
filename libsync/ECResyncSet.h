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

#ifndef ECResyncSet_INCLUDED
#define ECResyncSet_INCLUDED

#include <map>
#include <vector>

#include <mapidefs.h>
#include <edkmdb.h>

class ECResyncSetIterator;
class ECResyncSet
{
public:
	typedef std::vector<BYTE>						array_type;
	struct storage_type {
		storage_type(const array_type& _entryId, const FILETIME& _lastModTime)
			: entryId(_entryId), lastModTime(_lastModTime), flags(SYNC_NEW_MESSAGE)
		{ }

		array_type	entryId;
		FILETIME	lastModTime;
		ULONG		flags;
	};
	typedef std::map<array_type, storage_type>		map_type;
	typedef map_type::size_type						size_type;

	void Append(const SBinary &sbinSourceKey, const SBinary &sbinEntryID, const FILETIME &lastModTime);
	bool Remove(const SBinary &sbinSourceKey);
	ECResyncSetIterator Find(const SBinary &sBinSourceKey);

	bool IsEmpty() const { return m_map.empty(); }
	size_type Size() const { return m_map.size(); }

private:
	map_type	m_map;

	friend class ECResyncSetIterator;
};

class ECResyncSetIterator
{
public:
	ECResyncSetIterator(ECResyncSet &resyncSet);
	ECResyncSetIterator(ECResyncSet &resyncSet, const SBinary &sBinSourceKey);

	bool IsValid() const;
	LPENTRYID GetEntryID() const;
	ULONG GetEntryIDSize() const;
	const FILETIME& GetLastModTime() const;
	ULONG GetFlags() const;
	void SetFlags(ULONG flags);
	void Next();

private:
	typedef ECResyncSet::map_type::iterator	iterator_type;

	ECResyncSet		*m_lpResyncSet;
	iterator_type	m_iterator;

	const static FILETIME	s_nullTime;
};


#endif // ndef ECResyncSet_INCLUDED
