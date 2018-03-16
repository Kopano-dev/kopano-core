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
    ULONG ulObjType, BOOL fModify, ULONG ulAttachNum,
    const ECMAPIProp *lpRoot) :
	ECAttach(lpMsgStore, ulObjType, fModify, ulAttachNum, lpRoot),
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
	return alloc_wrap<ECArchiveAwareAttach>(lpMsgStore, ulObjType, fModify,
	       ulAttachNum, lpRoot).as(IID_ECAttach, lppAttach);
}

HRESULT	ECArchiveAwareAttach::SetPropHandler(ULONG ulPropTag,
    void */*lpProvider*/, const SPropValue *lpsPropValue, void *lpParam)
{
	auto lpAttach = static_cast<ECArchiveAwareAttach *>(lpParam);
	HRESULT hr = hrSuccess;

	switch(ulPropTag) {
	case PR_ATTACH_SIZE:
		if (lpAttach->m_lpRoot && lpAttach->m_lpRoot->IsLoading())
			hr = lpAttach->HrSetRealProp(lpsPropValue);
		else
			hr = MAPI_E_COMPUTED;
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
}
