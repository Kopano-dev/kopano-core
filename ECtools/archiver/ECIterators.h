/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>

namespace KC {

class KC_EXPORT HierarchyIteratorBase {
public:
	KC_HIDDEN HierarchyIteratorBase() :
	    m_ulFlags(0), m_ulDepth(0), m_ulRowIndex(0)
	{
		// creates the "end" iterator
	}
	HierarchyIteratorBase(IMAPIContainer *, unsigned int flags = 0, unsigned int depth = 0);

	KC_HIDDEN object_ptr<IMAPIContainer> dereference() const
	{
		assert(m_ptrCurrent != NULL && "attempt to dereference end iterator");
		return m_ptrCurrent;
	}

	void increment();

	bool operator==(const HierarchyIteratorBase &rhs) const
	{
		return m_ptrCurrent == rhs.m_ptrCurrent;
	}
	bool operator!=(const HierarchyIteratorBase &rhs) const
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

using ECHierarchyIteratorBase = HierarchyIteratorBase;

template<typename ContainerPtrType> class HierarchyIterator final :
    public ECHierarchyIteratorBase
{
public:
	HierarchyIterator() = default;
	HierarchyIterator(IMAPIContainer *c, unsigned int flags = 0, unsigned int depth = 0) :
		ECHierarchyIteratorBase(c, flags, depth)
	{}

	HierarchyIterator<ContainerPtrType> &operator++()
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

template<typename T> using ECHierarchyIterator = HierarchyIterator<T>;

} /* namespace */
