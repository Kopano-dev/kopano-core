/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiguid.h>
#include <kopano/ECUnknown.h>
#include <kopano/ECGuid.h>

namespace KC {

ECUnknown::ECUnknown(const char *name) :
	szClassName(name)
{}

ECUnknown::~ECUnknown()
{
	if (lpParent != nullptr)
		assert(false);	// apparently, we're being destructed with delete() while
						// a parent was set up, so we should be deleted via Suicide() !
}

ULONG ECUnknown::AddRef() {
	return ++m_cRef;
}

ULONG ECUnknown::Release() {
	ULONG nRef = --m_cRef;
	if (static_cast<int>(nRef) == -1)
		assert(false);
	if (nRef != 0)
		return nRef;
	ulock_normal locker(mutex);
	bool lastref = lstChildren.empty();
	locker.unlock();
	if (lastref)
		Suicide();
	// The object may be deleted now
	return nRef;
}

HRESULT ECUnknown::QueryInterface(REFIID refiid, void **lppInterface) {
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECUnknown::AddChild(ECUnknown *lpChild) {
	if (lpChild == nullptr)
		return hrSuccess;
	scoped_lock locker(mutex);
	lstChildren.emplace_back(lpChild);
	lpChild->SetParent(this);
	return hrSuccess;
}

HRESULT ECUnknown::RemoveChild(ECUnknown *lpChild) {
	std::list<ECUnknown *>::iterator iterChild;
	ulock_normal locker(mutex);

	if (lpChild != NULL)
		for (iterChild = lstChildren.begin(); iterChild != lstChildren.end(); ++iterChild)
			if(*iterChild == lpChild)
				break;
	if (iterChild == lstChildren.end())
		return MAPI_E_NOT_FOUND;
	lstChildren.erase(iterChild);
	bool bLastRef = lstChildren.empty() && m_cRef == 0;
	locker.unlock();
	if(bLastRef)
		Suicide();
	// The object may be deleted now
	return hrSuccess;
}

HRESULT ECUnknown::SetParent(ECUnknown *parent)
{
	// Parent object may only be set once
	assert(lpParent == nullptr);
	lpParent = parent;
	return hrSuccess;
}

/** 
 * Returns whether this object is the parent of passed object
 * 
 * @param lpObject Possible child object
 * 
 * @return this is a parent of lpObject, or not
 */
BOOL ECUnknown::IsParentOf(const ECUnknown *lpObject) const
{
	for (; lpObject != nullptr && lpObject->lpParent != nullptr;
	     lpObject = lpObject->lpParent)
		if (lpObject->lpParent == this)
			return TRUE;
	return FALSE;
}

/** 
 * Returns whether this object is a child of passed object
 * 
 * @param lpObject IUnknown object which may be a child of this
 * 
 * @return lpObject is a parent of this, or not
 */
BOOL ECUnknown::IsChildOf(const ECUnknown *lpObject) const
{
	if (lpObject == nullptr)
		return false;
	for (auto p : lpObject->lstChildren)
		if (this == p || IsChildOf(p))
			return TRUE;
	return FALSE;
}

// We kill the local object if there are no external (AddRef()) and no internal
// (AddChild) objects depending on us. 

HRESULT ECUnknown::Suicide() {
	auto parent = lpParent;
	auto self = this;

	// First, destroy the current object
	lpParent = nullptr;
	delete this;

	// WARNING: The child list of our parent now contains a pointer to this 
	// DELETED object. We must make sure that nobody ever follows pointer references
	// in this list during this interval. The list is, therefore PRIVATE to this object,
	// and may only be access through functions in ECUnknown.

	// Now, tell our parent to delete this object
	if (parent != nullptr)
		parent->RemoveChild(self);
	return hrSuccess;
}

} /* namespace */
