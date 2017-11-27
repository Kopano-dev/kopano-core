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
#include <kopano/lockhelper.hpp>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiguid.h>

#include <kopano/ECUnknown.h>
#include <kopano/ECGuid.h>

namespace KC {

ECUnknown::ECUnknown(const char *szClassName)
{
	this->szClassName = szClassName;
}

ECUnknown::~ECUnknown()
{
	if (this->lpParent != nullptr)
		assert(false);	// apparently, we're being destructed with delete() while
						// a parent was set up, so we should be deleted via Suicide() !
}

ULONG ECUnknown::AddRef() {
	return ++this->m_cRef;
}

ULONG ECUnknown::Release() {
	ULONG nRef = --this->m_cRef;
	if (static_cast<int>(nRef) == -1)
		assert(false);
	if (nRef != 0)
		return nRef;
	ulock_normal locker(mutex);
	bool lastref = lstChildren.empty();
	locker.unlock();
	if (lastref)
		this->Suicide();
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
	bool bLastRef;
	ulock_normal locker(mutex);

	if (lpChild != NULL)
		for (iterChild = lstChildren.begin(); iterChild != lstChildren.end(); ++iterChild)
			if(*iterChild == lpChild)
				break;
	if (iterChild == lstChildren.end())
		return MAPI_E_NOT_FOUND;
	lstChildren.erase(iterChild);

	bLastRef = this->lstChildren.empty() && this->m_cRef == 0;
	locker.unlock();
	if(bLastRef)
		this->Suicide();

	// The object may be deleted now
	return hrSuccess;
}

HRESULT ECUnknown::SetParent(ECUnknown *lpParent) {
	// Parent object may only be set once
	assert(this->lpParent == NULL);
	this->lpParent = lpParent;

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
	while (lpObject && lpObject->lpParent) {
		if (lpObject->lpParent == this)
			return TRUE;
		lpObject = lpObject->lpParent;
	}
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
	for (auto p : lpObject->lstChildren) {
		if (this == p)
			return TRUE;
		if (this->IsChildOf(p))
			return TRUE;
	}
	return FALSE;
}

// We kill the local object if there are no external (AddRef()) and no internal
// (AddChild) objects depending on us. 

HRESULT ECUnknown::Suicide() {
	ECUnknown *lpParent = this->lpParent;
	auto self = this;

	// First, destroy the current object
	this->lpParent = NULL;
	delete this;

	// WARNING: The child list of our parent now contains a pointer to this 
	// DELETED object. We must make sure that nobody ever follows pointer references
	// in this list during this interval. The list is, therefore PRIVATE to this object,
	// and may only be access through functions in ECUnknown.

	// Now, tell our parent to delete this object
	if (lpParent != nullptr)
		lpParent->RemoveChild(self);
	return hrSuccess;
}

} /* namespace */
