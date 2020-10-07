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
	ECHierarchyIteratorBase(IMAPIContainer *, unsigned int flags = 0, unsigned int depth = 0);

	KC_HIDDEN object_ptr<IMAPIContainer> dereference() const
	{
		assert(m_ptrCurrent != NULL && "attempt to dereference end iterator");
		return m_ptrCurrent;
	}

	void increment();

	bool operator==(const ECHierarchyIteratorBase &rhs) const
	{
		return m_ptrCurrent == rhs.m_ptrCurrent;
	}
	bool operator!=(const ECHierarchyIteratorBase &rhs) const
	{
		return !operator==(rhs);
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
	ECHierarchyIterator() = default;
	ECHierarchyIterator(IMAPIContainer *c, unsigned int flags = 0, unsigned int depth = 0) :
		ECHierarchyIteratorBase(c, flags, depth)
	{}

	ECHierarchyIterator<ContainerPtrType> &operator++()
	{
		ECHierarchyIteratorBase::increment();
		return *this;
	}
	ContainerPtrType &operator*() const
	{
		ECHierarchyIteratorBase::dereference()->QueryInterface(iid_of(m_ptr), &~m_ptr);
		return m_ptr;
	}

private:
	mutable ContainerPtrType	m_ptr;
};

} /* namespace */
