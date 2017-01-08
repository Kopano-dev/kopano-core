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

#ifndef ECIterators_INCLUDED
#define ECIterators_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/mapi_ptr.h>

class ECHierarchyIteratorBase
{
public:
	ECHierarchyIteratorBase(): m_ulFlags(0), m_ulDepth(0), m_ulRowIndex(0) {}  // creates the "end" iterator
	ECHierarchyIteratorBase(LPMAPICONTAINER lpContainer, ULONG ulFlags = 0, ULONG ulDepth = 0);

	MAPIContainerPtr& dereference() const
	{
		ASSERT(m_ptrCurrent && "attempt to dereference end iterator");
		return const_cast<MAPIContainerPtr&>(m_ptrCurrent);
	}

	void increment();

	bool equal(const ECHierarchyIteratorBase &rhs) const
	{
		return m_ptrCurrent == rhs.m_ptrCurrent; 
	}

private:
	MAPIContainerPtr	m_ptrContainer;
	ULONG				m_ulFlags;
	ULONG				m_ulDepth;
	MAPITablePtr		m_ptrTable;
	SRowSetPtr			m_ptrRows;
	ULONG				m_ulRowIndex;
	MAPIContainerPtr	m_ptrCurrent;
};

template <typename ContainerPtrType>
class ECHierarchyIterator _zcp_final :
    public ECHierarchyIteratorBase
{
public:
	ECHierarchyIterator() {}
	ECHierarchyIterator(LPMAPICONTAINER lpContainer, ULONG ulFlags = 0, ULONG ulDepth = 0)
		: ECHierarchyIteratorBase(lpContainer, ulFlags, ulDepth) {}

	bool operator==(const ECHierarchyIterator<ContainerPtrType> &r) const
	{
		return ECHierarchyIteratorBase::equal(r);
	}
	bool operator!=(const ECHierarchyIterator<ContainerPtrType> &r) const
	{
		return !operator==(r);
	}
	ECHierarchyIterator<ContainerPtrType> &operator++(void)
	{
		ECHierarchyIteratorBase::increment();
		return *this;
	}
	ContainerPtrType &operator*(void) const
	{
		ECHierarchyIteratorBase::dereference().QueryInterface(m_ptr);
		return m_ptr;
	}

private:
	mutable ContainerPtrType	m_ptr;
};

typedef ECHierarchyIterator<MAPIFolderPtr> ECFolderIterator;
typedef ECHierarchyIterator<ABContainerPtr> ECABContainerIterator;

#endif // ndef ECIterators_INCLUDED
