/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECIterators_INCLUDED
#define ECIterators_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/mapi_ptr.h>

namespace KC {

typedef object_ptr<IMAPIContainer> MAPIContainerPtr;

class KC_EXPORT ECHierarchyIteratorBase {
public:
	KC_HIDDEN ECHierarchyIteratorBase() :
	    m_ulFlags(0), m_ulDepth(0), m_ulRowIndex(0)
	{
		// creates the "end" iterator
	}
	ECHierarchyIteratorBase(LPMAPICONTAINER lpContainer, ULONG ulFlags = 0, ULONG ulDepth = 0);

	KC_HIDDEN MAPIContainerPtr &dereference() const
	{
		assert(m_ptrCurrent != NULL && "attempt to dereference end iterator");
		return const_cast<MAPIContainerPtr&>(m_ptrCurrent);
	}

	void increment();

	KC_HIDDEN bool equal(const ECHierarchyIteratorBase &rhs) const
	{
		return m_ptrCurrent == rhs.m_ptrCurrent;
	}

private:
	MAPIContainerPtr	m_ptrContainer;
	unsigned int m_ulFlags, m_ulDepth, m_ulRowIndex;
	MAPITablePtr		m_ptrTable;
	SRowSetPtr			m_ptrRows;
	MAPIContainerPtr	m_ptrCurrent;
};

template<typename ContainerPtrType> class ECHierarchyIterator final :
    public ECHierarchyIteratorBase
{
public:
	ECHierarchyIterator(void) = default;
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

} /* namespace */

#endif // ndef ECIterators_INCLUDED
