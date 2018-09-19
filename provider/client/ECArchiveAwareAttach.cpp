/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include "ECArchiveAwareAttach.h"
#include "ECArchiveAwareMessage.h"

HRESULT ECArchiveAwareAttachFactory::Create(ECMsgStore *lpMsgStore,
    ULONG ulObjType, BOOL fModify, ULONG ulAttachNum, const ECMAPIProp *lpRoot,
    ECAttach **lppAttach) const
{
	return ECArchiveAwareAttach::Create(lpMsgStore, ulObjType, fModify, ulAttachNum, lpRoot, lppAttach);
}

ECArchiveAwareAttach::ECArchiveAwareAttach(ECMsgStore *lpMsgStore,
    ULONG objtype, BOOL modify, ULONG atnum, const ECMAPIProp *lpRoot) :
	ECAttach(lpMsgStore, objtype, modify, atnum, lpRoot),
	m_lpRoot(dynamic_cast<const ECArchiveAwareMessage *>(lpRoot))
{
	assert(m_lpRoot != NULL);	// We don't expect an ECArchiveAwareAttach to be ever created by any other object than a ECArchiveAwareMessage.

	// Override the handler defined in ECAttach
	HrAddPropHandlers(PR_ATTACH_SIZE, ECAttach::GetPropHandler, SetPropHandler, this, false, false);
}

HRESULT ECArchiveAwareAttach::Create(ECMsgStore *lpMsgStore, ULONG ulObjType,
    BOOL fModify, ULONG ulAttachNum, const ECMAPIProp *lpRoot,
    ECAttach **lppAttach)
{
	return KC::alloc_wrap<ECArchiveAwareAttach>(lpMsgStore, ulObjType, fModify,
	       ulAttachNum, lpRoot).as(IID_ECAttach, lppAttach);
}

HRESULT	ECArchiveAwareAttach::SetPropHandler(ULONG ulPropTag,
    void */*lpProvider*/, const SPropValue *lpsPropValue, void *lpParam)
{
	auto lpAttach = static_cast<ECArchiveAwareAttach *>(lpParam);
	switch(ulPropTag) {
	case PR_ATTACH_SIZE:
		if (lpAttach->m_lpRoot && lpAttach->m_lpRoot->IsLoading())
			return lpAttach->HrSetRealProp(lpsPropValue);
		return MAPI_E_COMPUTED;
	default:
		return MAPI_E_NOT_FOUND;
	}
}
