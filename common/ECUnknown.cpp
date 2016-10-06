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

ECUnknown::ECUnknown(const char *szClassName)
{
	this->m_cRef = 0;
	this->szClassName = szClassName;
	this->lpParent = NULL;
}

ECUnknown::~ECUnknown() {
	if(this->lpParent) {
		assert(false);	// apparently, we're being destructed with delete() while
						// a parent was set up, so we should be deleted via Suicide() !
	}
}

ULONG ECUnknown::AddRef() {
	scoped_lock lock(mutex);
	return ++this->m_cRef;
}

ULONG ECUnknown::Release() {
	bool bLastRef = false;
	
	ulock_normal locker(mutex);
	ULONG nRef = --this->m_cRef;
	if((int)m_cRef == -1)
		assert(false);
		
	bLastRef = this->lstChildren.empty() && this->m_cRef == 0;
	locker.unlock();
	if(bLastRef)
		this->Suicide();

	// The object may be deleted now

	return nRef;
}

HRESULT ECUnknown::QueryInterface(REFIID refiid, void **lppInterface) {
	REGISTER_INTERFACE(IID_ECUnknown, this);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xUnknown);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECUnknown::AddChild(ECUnknown *lpChild) {
	
	scoped_lock locker(mutex);
	if(lpChild) {
		this->lstChildren.push_back(lpChild);
		lpChild->SetParent(this);
	}
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
BOOL ECUnknown::IsParentOf(const ECUnknown *lpObject) {
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
BOOL ECUnknown::IsChildOf(const ECUnknown *lpObject) {
	if (lpObject) {
		for (auto p : lpObject->lstChildren) {
			if (this == p)
				return TRUE;
			if (this->IsChildOf(p))
				return TRUE;
		}
	}
	return FALSE;
}

// We kill the local object if there are no external (AddRef()) and no internal
// (AddChild) objects depending on us. 

HRESULT ECUnknown::Suicide() {
	HRESULT hr = hrSuccess;
	ECUnknown *lpParent = this->lpParent;

	// First, destroy the current object
	this->lpParent = NULL;
	delete this;

	// WARNING: The child list of our parent now contains a pointer to this 
	// DELETED object. We must make sure that nobody ever follows pointer references
	// in this list during this interval. The list is, therefore PRIVATE to this object,
	// and may only be access through functions in ECUnknown.

	// Now, tell our parent to delete this object
	if(lpParent) {
		lpParent->RemoveChild(this);
	}

	return hr;
}

HRESULT __stdcall ECUnknown::xUnknown::QueryInterface(REFIID refiid, void ** lppInterface)
{
	METHOD_PROLOGUE_(ECUnknown , Unknown);
	return pThis->QueryInterface(refiid, lppInterface);
}

ULONG __stdcall ECUnknown::xUnknown::AddRef()
{
	METHOD_PROLOGUE_(ECUnknown , Unknown);
	return pThis->AddRef();
}

ULONG __stdcall ECUnknown::xUnknown::Release()
{
	METHOD_PROLOGUE_(ECUnknown , Unknown);
	return pThis->Release();
}
