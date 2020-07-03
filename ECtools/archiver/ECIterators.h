/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>

namespace KC {

class KC_EXPORT ECHierarchyIteratorBase {
public:
	KC_HIDDEN ECHierarchyIteratorBase() :
	    m_ulFlags(0), m_ulDepth(0), m_ulRowIndex(0)
	{
		// creates the "end" iterator
	}
	ECHierarchyIteratorBase(LPMAPICONTAINER lpContainer, ULONG ulFlags = 0, ULONG ulDepth = 0);

	KC_HIDDEN object_ptr<IMAPIContainer> &dereference() const
	{
		assert(m_ptrCurrent != NULL && "attempt to dereference end iterator");
		return const_cast<object_ptr<IMAPIContainer> &>(m_ptrCurrent);
	}

	void increment();

	KC_HIDDEN bool equal(const ECHierarchyIteratorBase &rhs) const
	{
		return m_ptrCurrent == rhs.m_ptrCurrent;
	}

private:
	object_ptr<IMAPIContainer> m_ptrContainer;
	unsigned int m_ulFlags, m_ulDepth, m_ulRowIndex;
	object_ptr<IMAPITable> m_ptrTable;
	rowset_ptr m_ptrRows;
	object_ptr<IMAPIContainer> m_ptrCurrent;
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
		ECHierarchyIteratorBase::dereference()->QueryInterface(iid_of(m_ptr), &~m_ptr);
		return m_ptr;
	}

private:
	mutable ContainerPtrType	m_ptr;
};

typedef ECHierarchyIterator<object_ptr<IMAPIFolder>> ECFolderIterator;
typedef ECHierarchyIterator<object_ptr<IABContainer>> ECABContainerIterator;

} /* namespace */
