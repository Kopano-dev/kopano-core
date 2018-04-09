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
 */
#include <new>
#include <kopano/platform.h>
#include <mapiutil.h>
#include "kcore.hpp"
#include <kopano/ECGuid.h>
#include <kopano/memory.hpp>
#include <edkguid.h>
#include "ECABLogon.h"

#include "ECABContainer.h"
#include "ECMailUser.h"
#include <kopano/ECDebug.h>

#include "WSTransport.h"

#include <kopano/Util.h>
#include "Mem.h"
#include <kopano/stringutil.h>
#include "pcutil.hpp"

using namespace KC;

ECABLogon::ECABLogon(LPMAPISUP lpMAPISup, WSTransport *lpTransport,
    ULONG ulProfileFlags, const GUID *lpGUID) :
	ECUnknown("IABLogon"), m_lpMAPISup(lpMAPISup),
	m_lpTransport(lpTransport),
	/* The "legacy" guid used normally (all AB entryIDs have this GUID) */
	m_guid(MUIDECSAB),
	/* The specific GUID for *this* addressbook provider, if available */
	m_ABPGuid((lpGUID != nullptr) ? *lpGUID : GUID_NULL)
{
	if (! (ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS))
		ECNotifyClient::Create(MAPI_ADDRBOOK, this, ulProfileFlags, lpMAPISup, &~m_lpNotifyClient);
}

ECABLogon::~ECABLogon()
{
	if(m_lpTransport)
		m_lpTransport->HrLogOff();

	// Disable all advises
	if(m_lpNotifyClient)
		m_lpNotifyClient->ReleaseAll();
}

HRESULT ECABLogon::Create(IMAPISupport *lpMAPISup, WSTransport *lpTransport,
    ULONG ulProfileFlags, const GUID *lpGuid, ECABLogon **lppECABLogon)
{
	return alloc_wrap<ECABLogon>(lpMAPISup, lpTransport, ulProfileFlags,
	       lpGuid).put(lppECABLogon);
}

HRESULT ECABLogon::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECABLogon, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABLogon, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABLogon::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_CALL_FAILED;
}

HRESULT ECABLogon::Logoff(ULONG ulFlags)
{
	//FIXME: Release all Other open objects ?
	//Releases all open objects, such as any subobjects or the status object. 
	//Releases the provider's support object.
	m_lpMAPISup.reset();
	return hrSuccess;
}

HRESULT ECABLogon::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	HRESULT			hr = hrSuccess;
	object_ptr<ECABContainer> lpABContainer;
	BOOL			fModifyObject = FALSE;
	ABEID			eidRoot =  ABEID(MAPI_ABCONT, MUIDECSAB, 0);
	ABEID lpABeid;
	object_ptr<IECPropStorage> lpPropStorage;
	object_ptr<ECMailUser> lpMailUser;
	object_ptr<ECDistList> 	lpDistList;
	memory_ptr<ENTRYID> lpEntryIDServer;

	// Check input/output variables 
	if (lpulObjType == nullptr || lppUnk == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	/*if(ulFlags & MAPI_MODIFY) {
		if (!fModify)
			return MAPI_E_NO_ACCESS;
		else
			fModifyObject = TRUE;
	}

	if(ulFlags & MAPI_BEST_ACCESS)
		fModifyObject = fModify;
	*/

	if(cbEntryID == 0 && lpEntryID == NULL) {
		memcpy(&lpABeid, &eidRoot, sizeof(lpABeid));
		cbEntryID = CbABEID(&lpABeid);
		lpEntryID = reinterpret_cast<ENTRYID *>(&lpABeid);
	} else {
		if (cbEntryID == 0 || lpEntryID == nullptr || cbEntryID < sizeof(ABEID))
			return MAPI_E_UNKNOWN_ENTRYID;
		hr = KAllocCopy(lpEntryID, cbEntryID, &~lpEntryIDServer);
		if(hr != hrSuccess)
			return hr;
		lpEntryID = lpEntryIDServer;
		memcpy(&lpABeid, lpEntryID, sizeof(ABEID));

		// Check sane entryid
		if (lpABeid.ulType != MAPI_ABCONT &&
		    lpABeid.ulType != MAPI_MAILUSER &&
		    lpABeid.ulType != MAPI_DISTLIST)
			return MAPI_E_UNKNOWN_ENTRYID;

		// Check entryid GUID, must be either MUIDECSAB or m_ABPGuid
		if (memcmp(&lpABeid.guid, &MUIDECSAB, sizeof(MAPIUID)) != 0 &&
		    memcmp(&lpABeid.guid, &m_ABPGuid, sizeof(MAPIUID)) != 0)
			return MAPI_E_UNKNOWN_ENTRYID;
		memcpy(&lpABeid.guid, &MUIDECSAB, sizeof(MAPIUID));
	}

	//TODO: check entryid serverside?
	switch (lpABeid.ulType) {
	case MAPI_ABCONT:
		hr = ECABContainer::Create(this, MAPI_ABCONT, fModifyObject, &~lpABContainer);
		if (hr != hrSuccess)
			return hr;
		hr = lpABContainer->SetEntryId(cbEntryID, lpEntryID);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpABContainer);
		hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
		if (hr != hrSuccess)
			return hr;
		hr = lpABContainer->HrSetPropStorage(lpPropStorage, TRUE);
		if (hr != hrSuccess)
			return hr;
		if (lpInterface)
			hr = lpABContainer->QueryInterface(*lpInterface,(void **)lppUnk);
		else
			hr = lpABContainer->QueryInterface(IID_IABContainer, (void **)lppUnk);
		if (hr != hrSuccess)
			return hr;
		break;
	case MAPI_MAILUSER:
		hr = ECMailUser::Create(this, fModifyObject, &~lpMailUser);
		if (hr != hrSuccess)
			return hr;
		hr = lpMailUser->SetEntryId(cbEntryID, lpEntryID);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpMailUser);
		hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
		if (hr != hrSuccess)
			return hr;
		hr = lpMailUser->HrSetPropStorage(lpPropStorage, TRUE);
		if (hr != hrSuccess)
			return hr;
		if (lpInterface)
			hr = lpMailUser->QueryInterface(*lpInterface,(void **)lppUnk);
		else
			hr = lpMailUser->QueryInterface(IID_IMailUser, (void **)lppUnk);
		if (hr != hrSuccess)
			return hr;
		break;
	case MAPI_DISTLIST:
		hr = ECDistList::Create(this, fModifyObject, &~lpDistList);
		if (hr != hrSuccess)
			return hr;
		hr = lpDistList->SetEntryId(cbEntryID, lpEntryID);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpDistList);
		hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
		if (hr != hrSuccess)
			return hr;
		hr = lpDistList->HrSetPropStorage(lpPropStorage, TRUE);
		if (hr != hrSuccess)
			return hr;
		if (lpInterface)
			hr = lpDistList->QueryInterface(*lpInterface, (void **)lppUnk);
		else
			hr = lpDistList->QueryInterface(IID_IDistList, (void **)lppUnk);
		if (hr != hrSuccess)
			return hr;
		break;
	default:
		return MAPI_E_NOT_FOUND;
	}

	if(lpulObjType)
		*lpulObjType = lpABeid.ulType;
	return hrSuccess;
}

HRESULT ECABLogon::CompareEntryIDs(ULONG cbEntryID1, const ENTRYID *lpEntryID1,
    ULONG cbEntryID2, const ENTRYID *lpEntryID2, ULONG ulFlags,
    ULONG *lpulResult)
{
	if(lpulResult)
		*lpulResult = CompareABEID(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2);
	return hrSuccess;
}

HRESULT ECABLogon::Advise(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink, ULONG *lpulConnection)
{
	if (lpAdviseSink == NULL || lpulConnection == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpEntryID == NULL)
		//NOTE: Normal you must give the entryid of the addressbook toplevel
		return MAPI_E_INVALID_PARAMETER;

	assert(m_lpNotifyClient != NULL && (lpEntryID != NULL || true));
	if(m_lpNotifyClient->Advise(cbEntryID, (LPBYTE)lpEntryID, ulEventMask, lpAdviseSink, lpulConnection) != S_OK)
		return MAPI_E_NO_SUPPORT;
	return hrSuccess;
}

HRESULT ECABLogon::Unadvise(ULONG ulConnection)
{
	assert(m_lpNotifyClient != NULL);
	m_lpNotifyClient->Unadvise(ulConnection);
	return hrSuccess;
}

HRESULT ECABLogon::OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPMAPISTATUS * lppMAPIStatus)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABLogon::OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid,
    ULONG tpl_flags, IMAPIProp *propdata, const IID *iface, IMAPIProp **propnew,
    IMAPIProp *sibling)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABLogon::GetOneOffTable(ULONG ulFlags, LPMAPITABLE * lppTable)
{
	//return m_lpMAPISup->GetOneOffTable(ulFlags, lppTable);
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECABLogon::PrepareRecips(ULONG ulFlags,
    const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList)
{
	ULONG			cValues;
	ecmem_ptr<SPropValue> lpPropArray, lpNewPropArray;
	ULONG			ulObjType;

	if(lpPropTagArray == NULL || lpPropTagArray->cValues == 0) // There is no work to do.
		return hrSuccess;

	for (unsigned int i = 0; i < lpRecipList->cEntries; ++i) {
		auto rgpropvalsRecip = lpRecipList->aEntries[i].rgPropVals;
		unsigned int cPropsRecip = lpRecipList->aEntries[i].cValues;

		// For each recipient, find its entryid
		auto lpPropVal = PCpropFindProp(rgpropvalsRecip, cPropsRecip, PR_ENTRYID);
		if(!lpPropVal)
			continue; // no
		
		auto lpABeid = reinterpret_cast<ABEID *>(lpPropVal->Value.bin.lpb);
		auto cbABeid = lpPropVal->Value.bin.cb;

		/* Is it one of ours? */
		if ( cbABeid  < CbNewABEID("") || lpABeid == NULL)
			continue;	// no
		if (memcmp(&lpABeid->guid, &m_guid, sizeof(MAPIUID)) != 0)
			continue;	// no

		object_ptr<IMailUser> lpIMailUser;
		auto hr = OpenEntry(cbABeid, reinterpret_cast<ENTRYID *>(lpABeid), nullptr, 0, &ulObjType, &~lpIMailUser);
		if(hr != hrSuccess)
			continue;	// no
		hr = lpIMailUser->GetProps(lpPropTagArray, 0, &cValues, &~lpPropArray);
		if(FAILED(hr) != hrSuccess)
			continue;	// no

		// merge the properties
		hr = ECAllocateBuffer((cValues + cPropsRecip) * sizeof(SPropValue), &~lpNewPropArray);
		if (hr != hrSuccess)
			return hr;

		for (unsigned int j = 0; j < cValues; ++j) {
			lpPropVal = NULL;

			if(PROP_TYPE(lpPropArray[j].ulPropTag) == PT_ERROR)
				lpPropVal = PCpropFindProp(rgpropvalsRecip, cPropsRecip, lpPropTagArray->aulPropTag[j]);

			if(lpPropVal == NULL)
				lpPropVal = &lpPropArray[j];

			hr = Util::HrCopyProperty(lpNewPropArray + j, lpPropVal, lpNewPropArray);
			if(hr != hrSuccess)
				return hr;
		}

		for (unsigned int j = 0; j < cPropsRecip; ++j) {
			if (PCpropFindProp(lpNewPropArray, cValues, rgpropvalsRecip[j].ulPropTag) ||
				PROP_TYPE( rgpropvalsRecip[j].ulPropTag ) == PT_ERROR )
				continue;
			
			hr = Util::HrCopyProperty(lpNewPropArray + cValues, &rgpropvalsRecip[j], lpNewPropArray);
			if(hr != hrSuccess)
				return hr;
			++cValues;
		}

		lpRecipList->aEntries[i].rgPropVals	= lpNewPropArray.release();
		lpRecipList->aEntries[i].cValues	= cValues;

		if(rgpropvalsRecip) {
			ECFreeBuffer(rgpropvalsRecip); 
			rgpropvalsRecip = NULL;
		}
	}

	// Always succeeded on this point
	return hrSuccess;
}
