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
#include <kopano/platform.h>
#include <kopano/ECInterfaceDefs.h>
#include <mapiutil.h>
#include "kcore.hpp"
#include <kopano/ECGuid.h>
#include <kopano/memory.hpp>
#include <edkguid.h>
#include "ECABLogon.h"

#include "ECABContainer.h"
#include "ECMailUser.h"
#include "ECDistList.h"

#include <kopano/ECDebug.h>

#include "WSTransport.h"

#include <kopano/Util.h>
#include "Mem.h"
#include <kopano/stringutil.h>
#include "pcutil.hpp"

using namespace KCHL;

ECABLogon::ECABLogon(LPMAPISUP lpMAPISup, WSTransport* lpTransport, ULONG ulProfileFlags, GUID *lpGUID) : ECUnknown("IABLogon")
{
	// The 'legacy' guid used normally (all AB entryIDs have this GUID)
	m_guid = MUIDECSAB;

	// The specific GUID for *this* addressbook provider, if available
	m_ABPGuid = (lpGUID != nullptr) ? *lpGUID : GUID_NULL;

	m_lpNotifyClient = NULL;

	m_lpTransport = lpTransport;
	if(m_lpTransport)
		m_lpTransport->AddRef();

	m_lpMAPISup = lpMAPISup;
	if(m_lpMAPISup)
		m_lpMAPISup->AddRef();

	if (! (ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS))
		ECNotifyClient::Create(MAPI_ADDRBOOK, this, ulProfileFlags, lpMAPISup, &m_lpNotifyClient);
}

ECABLogon::~ECABLogon()
{
	if(m_lpTransport)
		m_lpTransport->HrLogOff();

	// Disable all advises
	if(m_lpNotifyClient)
		m_lpNotifyClient->ReleaseAll();

	if(m_lpNotifyClient)
		m_lpNotifyClient->Release();

	if(m_lpMAPISup) {
		m_lpMAPISup->Release();
		m_lpMAPISup = NULL;
	}

	if(m_lpTransport)
		m_lpTransport->Release();
}

HRESULT ECABLogon::Create(LPMAPISUP lpMAPISup, WSTransport* lpTransport, ULONG ulProfileFlags, GUID *lpGuid, ECABLogon **lppECABLogon)
{
	HRESULT hr = hrSuccess;

	ECABLogon *lpABLogon = new ECABLogon(lpMAPISup, lpTransport, ulProfileFlags, lpGuid);

	hr = lpABLogon->QueryInterface(IID_ECABLogon, (void **)lppECABLogon);

	if(hr != hrSuccess)
		delete lpABLogon;

	return hr;
}

HRESULT ECABLogon::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECABLogon, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABLogon, &this->m_xABLogon);
	REGISTER_INTERFACE2(IUnknown, &this->m_xABLogon);
	REGISTER_INTERFACE3(ISelectUnicode, IUnknown, &this->m_xUnknown);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECABLogon::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_CALL_FAILED;
}

HRESULT ECABLogon::Logoff(ULONG ulFlags)
{
	HRESULT hr = hrSuccess;

	//FIXME: Release all Other open objects ?
	//Releases all open objects, such as any subobjects or the status object. 
	//Releases the provider's support object.

	if(m_lpMAPISup)
	{
		m_lpMAPISup->Release();
		m_lpMAPISup = NULL;
	}

	return hr;
}

HRESULT ECABLogon::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	HRESULT			hr = hrSuccess;
	object_ptr<ECABContainer> lpABContainer;
	BOOL			fModifyObject = FALSE;
	ABEID			eidRoot =  ABEID(MAPI_ABCONT, MUIDECSAB, 0);
	ABEID *lpABeid = NULL;
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
		lpABeid = &eidRoot;

		cbEntryID = CbABEID(lpABeid);
		lpEntryID = (LPENTRYID)lpABeid;
	} else {
		if (cbEntryID == 0 || lpEntryID == nullptr)
			return MAPI_E_UNKNOWN_ENTRYID;
		hr = MAPIAllocateBuffer(cbEntryID, &~lpEntryIDServer);
		if(hr != hrSuccess)
			return hr;

		memcpy(lpEntryIDServer, lpEntryID, cbEntryID);
		lpEntryID = lpEntryIDServer;

		lpABeid = reinterpret_cast<ABEID *>(lpEntryID);

		// Check sane entryid
		if (lpABeid->ulType != MAPI_ABCONT && lpABeid->ulType != MAPI_MAILUSER && lpABeid->ulType != MAPI_DISTLIST) 
			return MAPI_E_UNKNOWN_ENTRYID;

		// Check entryid GUID, must be either MUIDECSAB or m_ABPGuid
		if (memcmp(&lpABeid->guid, &MUIDECSAB, sizeof(MAPIUID)) != 0 &&
		    memcmp(&lpABeid->guid, &m_ABPGuid, sizeof(MAPIUID)) != 0)
			return MAPI_E_UNKNOWN_ENTRYID;

		memcpy(&lpABeid->guid, &MUIDECSAB, sizeof(MAPIUID));
	}

	//TODO: check entryid serverside?

	switch(lpABeid->ulType) {
		case MAPI_ABCONT:
			hr = ECABContainer::Create(this, MAPI_ABCONT, fModifyObject, &~lpABContainer);
			if(hr != hrSuccess)
				return hr;
			hr = lpABContainer->SetEntryId(cbEntryID, lpEntryID);
			if(hr != hrSuccess)
				return hr;
			AddChild(lpABContainer);
			hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
			if(hr != hrSuccess)
				return hr;
			hr = lpABContainer->HrSetPropStorage(lpPropStorage, TRUE);
			if(hr != hrSuccess)
				return hr;
			if(lpInterface)
				hr = lpABContainer->QueryInterface(*lpInterface,(void **)lppUnk);
			else
				hr = lpABContainer->QueryInterface(IID_IABContainer, (void **)lppUnk);
			if(hr != hrSuccess)
				return hr;
			break;
		case MAPI_MAILUSER:
			hr = ECMailUser::Create(this, fModifyObject, &~lpMailUser);
			if(hr != hrSuccess)
				return hr;
			hr = lpMailUser->SetEntryId(cbEntryID, lpEntryID);
			if(hr != hrSuccess)
				return hr;
			AddChild(lpMailUser);
			hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
			if(hr != hrSuccess)
				return hr;
			hr = lpMailUser->HrSetPropStorage(lpPropStorage, TRUE);
			if(hr != hrSuccess)
				return hr;
			if(lpInterface)
				hr = lpMailUser->QueryInterface(*lpInterface,(void **)lppUnk);
			else
				hr = lpMailUser->QueryInterface(IID_IMailUser, (void **)lppUnk);

			if(hr != hrSuccess)
				return hr;
			break;
		case MAPI_DISTLIST:
			hr = ECDistList::Create(this, fModifyObject, &~lpDistList);
			if(hr != hrSuccess)
				return hr;
			hr = lpDistList->SetEntryId(cbEntryID, lpEntryID);
			if(hr != hrSuccess)
				return hr;
			AddChild(lpDistList);
			hr = m_lpTransport->HrOpenABPropStorage(cbEntryID, lpEntryID, &~lpPropStorage);
			if(hr != hrSuccess)
				return hr;
			hr = lpDistList->HrSetPropStorage(lpPropStorage, TRUE);
			if(hr != hrSuccess)
				return hr;
			if(lpInterface)
				hr = lpDistList->QueryInterface(*lpInterface,(void **)lppUnk);
			else
				hr = lpDistList->QueryInterface(IID_IDistList, (void **)lppUnk);

			if(hr != hrSuccess)
				return hr;
			break;
		default:
			return MAPI_E_NOT_FOUND;
	}

	if(lpulObjType)
		*lpulObjType = lpABeid->ulType;
	return hrSuccess;
}

HRESULT ECABLogon::CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	if(lpulResult)
		*lpulResult = (CompareABEID(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2) ? TRUE : FALSE);

	return hrSuccess;
}

HRESULT ECABLogon::Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection)
{
	HRESULT hr = hrSuccess;

	if (lpAdviseSink == NULL || lpulConnection == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpEntryID == NULL)
		//NOTE: Normal you must give the entryid of the addressbook toplevel
		return MAPI_E_INVALID_PARAMETER;

	assert(m_lpNotifyClient != NULL && (lpEntryID != NULL || true));
	if(m_lpNotifyClient->Advise(cbEntryID, (LPBYTE)lpEntryID, ulEventMask, lpAdviseSink, lpulConnection) != S_OK)
		hr = MAPI_E_NO_SUPPORT;
	return hr;
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

HRESULT ECABLogon::OpenTemplateID(ULONG cbTemplateID, LPENTRYID lpTemplateID, ULONG ulTemplateFlags, LPMAPIPROP lpMAPIPropData, LPCIID lpInterface, LPMAPIPROP * lppMAPIPropNew, LPMAPIPROP lpMAPIPropSibling)
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
	HRESULT			hr = hrSuccess;
	ULONG			cPropsRecip;
	LPSPropValue	rgpropvalsRecip;
	LPSPropValue	lpPropVal = NULL;
	ABEID *lpABeid = NULL;
	ULONG			cbABeid;
	ULONG			cValues;
	LPSPropValue	lpPropArray = NULL;
	LPSPropValue	lpNewPropArray = NULL;
	unsigned int	j;
	ULONG			ulObjType;

	if(lpPropTagArray == NULL || lpPropTagArray->cValues == 0) // There is no work to do.
		goto exit;

	for (unsigned int i = 0; i < lpRecipList->cEntries; ++i) {
		rgpropvalsRecip	= lpRecipList->aEntries[i].rgPropVals;
		cPropsRecip		= lpRecipList->aEntries[i].cValues;

		// For each recipient, find its entryid
		lpPropVal = PpropFindProp( rgpropvalsRecip, cPropsRecip, PR_ENTRYID );
		if(!lpPropVal)
			continue; // no
		
		lpABeid = reinterpret_cast<ABEID *>(lpPropVal->Value.bin.lpb);
		cbABeid = lpPropVal->Value.bin.cb;

		/* Is it one of ours? */
		if ( cbABeid  < CbNewABEID("") || lpABeid == NULL)
			continue;	// no

		if ( memcmp( &(lpABeid->guid), &this->m_guid, sizeof(MAPIUID) ) != 0)
			continue;	// no

		object_ptr<IMailUser> lpIMailUser;
		hr = OpenEntry(cbABeid, reinterpret_cast<ENTRYID *>(lpABeid), nullptr, 0, &ulObjType, &~lpIMailUser);
		if(hr != hrSuccess)
			continue;	// no
		
		hr = lpIMailUser->GetProps(lpPropTagArray, 0, &cValues, &lpPropArray);
		if(FAILED(hr) != hrSuccess)
			goto skip;	// no

		// merge the properties
		ECAllocateBuffer((cValues+cPropsRecip)*sizeof(SPropValue), (void**)&lpNewPropArray);

		for (j = 0; j < cValues; ++j) {
			lpPropVal = NULL;

			if(PROP_TYPE(lpPropArray[j].ulPropTag) == PT_ERROR)
				lpPropVal = PpropFindProp( rgpropvalsRecip, cPropsRecip, lpPropTagArray->aulPropTag[j]);

			if(lpPropVal == NULL)
				lpPropVal = &lpPropArray[j];

			hr = Util::HrCopyProperty(lpNewPropArray + j, lpPropVal, lpNewPropArray);
			if(hr != hrSuccess)
				goto exit;
		}

		for (j = 0; j < cPropsRecip; ++j) {
			if ( PpropFindProp(lpNewPropArray, cValues, rgpropvalsRecip[j].ulPropTag ) ||
				PROP_TYPE( rgpropvalsRecip[j].ulPropTag ) == PT_ERROR )
				continue;
			
			hr = Util::HrCopyProperty(lpNewPropArray + cValues, &rgpropvalsRecip[j], lpNewPropArray);
			if(hr != hrSuccess)
				goto exit;			
			++cValues;
		}

		lpRecipList->aEntries[i].rgPropVals	= lpNewPropArray;
		lpRecipList->aEntries[i].cValues	= cValues;

		if(rgpropvalsRecip) {
			ECFreeBuffer(rgpropvalsRecip); 
			rgpropvalsRecip = NULL;
		}
		
		lpNewPropArray = NULL; // Everthing oke, should not be freed..

	skip:
		if(lpPropArray){ ECFreeBuffer(lpPropArray); lpPropArray = NULL; }
	}

	// Always succeeded on this point
	hr = hrSuccess;

exit:
	if(lpPropArray)
		ECFreeBuffer(lpPropArray);

	if(lpNewPropArray)
		ECFreeBuffer(lpNewPropArray);
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, GetLastError, (HRESULT, hResult), (ULONG, ulFlags), (LPMAPIERROR *, lppMAPIError))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, Logoff, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, OpenEntry, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, CompareEntryIDs, (ULONG, cbEntryID1), (LPENTRYID, lpEntryID1), (ULONG, cbEntryID2), (LPENTRYID, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, Advise, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG, ulEventMask), (LPMAPIADVISESINK, lpAdviseSink), (ULONG *, lpulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, Unadvise, (ULONG, ulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, OpenStatusEntry, (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPMAPISTATUS *, lppMAPIStatus))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, OpenTemplateID, (ULONG, cbTemplateID), (LPENTRYID, lpTemplateID), (ULONG, ulTemplateFlags), (LPMAPIPROP, lpMAPIPropData), (LPCIID, lpInterface), (LPMAPIPROP *, lppMAPIPropNew), (LPMAPIPROP, lpMAPIPropSibling))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, GetOneOffTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECABLogon, ABLogon, PrepareRecips, (ULONG, ulFlags), (const SPropTagArray *, lpPropTagArray), (LPADRLIST, lpRecipList))
