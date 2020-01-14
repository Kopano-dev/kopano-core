/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <algorithm>
#include <kopano/platform.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiguid.h>
#include <kopano/ECABEntryID.h>
#include <kopano/ECGuid.h>
#include <kopano/ECUnknown.h>
#include "ECCache.h"
#include "../provider/include/kcore.hpp"

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
	ulock_normal locker(mutex);
	auto iterChild = lstChildren.end();

	if (lpChild != NULL)
		iterChild = std::find(lstChildren.begin(), lstChildren.end(), lpChild);
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
	lpParent = nullptr;
	/*
	 * Haphazard lifetime design of EC objects.
	 * The child's dtor should run before the parent's dtor,
	 * because ~WSMAPIPropStorage wants to unadvise before
	 * ~ECMsgStore sends a blunt logoff.
	 */
	auto base = dynamic_cast<void *>(this);
	this->~ECUnknown();
	if (parent != nullptr)
		parent->RemoveChild(this);
	operator delete(base);
	return hrSuccess;
}

static const ABEID_FIXED g_sEveryOneEid(MAPI_DISTLIST, MUIDECSAB, 1);
unsigned char *g_lpEveryoneEid = (unsigned char*)&g_sEveryOneEid;
const unsigned int g_cbEveryoneEid = sizeof(g_sEveryOneEid);

static const ABEID_FIXED g_sSystemEid(MAPI_MAILUSER, MUIDECSAB, 2);
unsigned char *g_lpSystemEid = (unsigned char*)&g_sSystemEid;
const unsigned int g_cbSystemEid = sizeof(g_sSystemEid);

static HRESULT CheckEntryId(unsigned int eid_size, const ENTRYID *eid,
    unsigned int id, unsigned int type, bool *res)
{
	if (eid_size < sizeof(ABEID) || eid == nullptr || res == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	*res = true;
	auto ab = reinterpret_cast<const ABEID *>(eid);
	if (ab->ulId != id)
		*res = false;
	else if (ab->ulType != type)
		*res = false;
	else if (ab->ulVersion == 1 && eid_size > sizeof(ABEID) && ab->szExId[0] != '\0')
		*res = false;
	return hrSuccess;
}

HRESULT EntryIdIsEveryone(unsigned int eid_size, const ENTRYID *eid, bool *res)
{
	return CheckEntryId(eid_size, eid, 1, MAPI_DISTLIST, res);
}

HRESULT GetNonPortableObjectType(unsigned int eid_size,
    const ENTRYID *eid, unsigned int *obj_type)
{
	if (eid_size < sizeof(ABEID) || eid == nullptr || obj_type == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	*obj_type = reinterpret_cast<const ABEID *>(eid)->ulType;
	return hrSuccess;
}

ECCacheBase::ECCacheBase(const std::string &name, size_type size, long age) :
	m_strCachename(name), m_ulMaxSize(size), m_lMaxAge(age)
{}

ECCacheStat ECCacheBase::get_stats() const
{
	ECCacheStat s;
	s.name = m_strCachename;
	s.items = ItemCount();
	s.size = Size();
	s.maxsize = m_ulMaxSize;
	s.req = HitCount();
	s.hit = ValidCount();
	return s;
}

} /* namespace */
