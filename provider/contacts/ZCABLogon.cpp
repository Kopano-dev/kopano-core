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
#include <kopano/Trace.h>
#include "ZCABLogon.h"
#include "ZCABContainer.h"
#include <kopano/ECTags.h>
#include <kopano/ECDebug.h>
#include <kopano/ECDebugPrint.h>
#include <kopano/ECGuid.h>
#include <kopano/ECInterfaceDefs.h>
#include "kcore.hpp"
#include <mapix.h>
#include <edkmdb.h>

ZCABLogon::ZCABLogon(LPMAPISUP lpMAPISup, ULONG ulProfileFlags, GUID *lpGUID) : ECUnknown("IABLogon")
{
	// The specific GUID for *this* addressbook provider, if available
	if (lpGUID) {
		m_ABPGuid = *lpGUID;
	} else {
		m_ABPGuid = GUID_NULL;
	}

	m_lpMAPISup = lpMAPISup;
	if(m_lpMAPISup)
		m_lpMAPISup->AddRef();
}

ZCABLogon::~ZCABLogon()
{
	ClearFolderList();
	if(m_lpMAPISup) {
		m_lpMAPISup->Release();
		m_lpMAPISup = NULL;
	}
}

HRESULT ZCABLogon::Create(LPMAPISUP lpMAPISup, ULONG ulProfileFlags, GUID *lpGuid, ZCABLogon **lppZCABLogon)
{
	HRESULT hr = hrSuccess;

	ZCABLogon *lpABLogon = new ZCABLogon(lpMAPISup, ulProfileFlags, lpGuid);

	hr = lpABLogon->QueryInterface(IID_ZCABLogon, (void **)lppZCABLogon);

	if(hr != hrSuccess)
		delete lpABLogon;

	return hr;
}

HRESULT ZCABLogon::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ZCABLogon, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IABLogon, &this->m_xABLogon);
	REGISTER_INTERFACE2(IUnknown, &this->m_xABLogon);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ZCABLogon::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_CALL_FAILED;
}

HRESULT ZCABLogon::Logoff(ULONG ulFlags)
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

/** 
 * Adds a folder to the addressbook provider. Data is read from global profile section.
 * 
 * @param[in] strDisplayName Display string in hierarchy table
 * @param[in] cbStore bytes in lpStore
 * @param[in] lpStore Store entryid lpFolder entryid is in
 * @param[in] cbFolder bytes in lpFolder
 * @param[in] lpFolder Folder entryid pointing to a contacts folder (assumed, should we check?)
 * 
 * @return MAPI Error code
 */
HRESULT ZCABLogon::AddFolder(const WCHAR* lpwDisplayName, ULONG cbStore, LPBYTE lpStore, ULONG cbFolder, LPBYTE lpFolder)
{
	zcabFolderEntry entry;

	if (cbStore == 0 || lpStore == NULL || cbFolder == 0 || lpFolder == NULL)
		return MAPI_E_INVALID_PARAMETER;

	entry.strwDisplayName = lpwDisplayName;

	entry.cbStore = cbStore;
	HRESULT hr = MAPIAllocateBuffer(cbStore,
	             reinterpret_cast<void **>(&entry.lpStore));
	if (hr != hrSuccess)
		return hr;
	memcpy(entry.lpStore, lpStore, cbStore);

	entry.cbFolder = cbFolder;
	hr = MAPIAllocateBuffer(cbFolder, (void**)&entry.lpFolder);
	if (hr != hrSuccess)
		return hr;
	memcpy(entry.lpFolder, lpFolder, cbFolder);

	m_lFolders.push_back(entry);
	return hrSuccess;
}

HRESULT ZCABLogon::ClearFolderList()
{
	for (const auto &i : m_lFolders) {
		MAPIFreeBuffer(i.lpStore);
		MAPIFreeBuffer(i.lpFolder);
	}
	m_lFolders.clear();
	return hrSuccess;
}

/** 
 * EntryID is NULL: Open "root container". Returns an ABContainer
 * which returns the provider root container. The EntryID in that one
 * table entry opens the ABContainer which has all the folders from
 * stores which were added using the AddFolder interface.
 * 
 * Root container EntryID: 00000000727f0430e3924fdab86ae52a7fe46571 (version + guid)
 * Sub container EntryID : 00000000727f0430e3924fdab86ae52a7fe46571 + obj type + 0 + folder contact eid
 * Contact Item EntryID  : 00000000727f0430e3924fdab86ae52a7fe46571 + obj type + email offset + kopano item eid
 * 
 * @param[in] cbEntryID 0 or bytes in lpEntryID
 * @param[in] lpEntryID NULL or a valid entryid
 * @param[in] lpInterface NULL or supported interface
 * @param[in] ulFlags unused
 * @param[out] lpulObjType MAPI_ABCONT, or a type from ZCAB
 * @param[out] lppUnk pointer to ZCABContainer
 * 
 * @return 
 */
HRESULT ZCABLogon::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	HRESULT			hr = hrSuccess;
	ZCABContainer *lpRootContainer = NULL;
	ZCMAPIProp *lpContact = NULL;
	LPPROFSECT lpProfileSection = NULL;
	LPSPropValue lpFolderProps = NULL;
	ULONG cValues = 0;
	SizedSPropTagArray(3, sptaFolderProps) = {3, {PR_ZC_CONTACT_STORE_ENTRYIDS, PR_ZC_CONTACT_FOLDER_ENTRYIDS, PR_ZC_CONTACT_FOLDER_NAMES_W}};
	
	// Check input/output variables 
	if(lpulObjType == NULL || lppUnk == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if(cbEntryID == 0 && lpEntryID == NULL) {
		// this is the "Kopano Contacts Folders" container. Get the hierarchy of this folder. SetEntryID(0000 + guid + MAPI_ABCONT + ?) ofzo?
		hr = ZCABContainer::Create(NULL, NULL, m_lpMAPISup, this, &lpRootContainer);
		if (hr != hrSuccess)
			goto exit;

	} else {
		if (cbEntryID == 0 || lpEntryID == NULL) {
			hr = MAPI_E_UNKNOWN_ENTRYID;
			goto exit;
		}

		// you can only open the top level container
		if (memcmp((LPBYTE)lpEntryID +4, &MUIDZCSAB, sizeof(GUID)) != 0) {
			hr = MAPI_E_UNKNOWN_ENTRYID;
			goto exit;
		}

		hr = m_lpMAPISup->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, 0, &lpProfileSection);
		if (hr != hrSuccess)
			goto exit;

		hr = lpProfileSection->GetProps(sptaFolderProps, 0, &cValues, &lpFolderProps);
		if (FAILED(hr))
			goto exit;
		hr = hrSuccess;

		// remove old list, if present
		ClearFolderList();

		// make the list
		if (lpFolderProps[0].ulPropTag == PR_ZC_CONTACT_STORE_ENTRYIDS &&
			lpFolderProps[1].ulPropTag == PR_ZC_CONTACT_FOLDER_ENTRYIDS &&
			lpFolderProps[2].ulPropTag == PR_ZC_CONTACT_FOLDER_NAMES_W &&
			lpFolderProps[0].Value.MVbin.cValues == lpFolderProps[1].Value.MVbin.cValues &&
			lpFolderProps[1].Value.MVbin.cValues == lpFolderProps[2].Value.MVszW.cValues)
			for (ULONG c = 0; c < lpFolderProps[1].Value.MVbin.cValues; ++c)
				AddFolder(lpFolderProps[2].Value.MVszW.lppszW[c],
						  lpFolderProps[0].Value.MVbin.lpbin[c].cb, lpFolderProps[0].Value.MVbin.lpbin[c].lpb,
						  lpFolderProps[1].Value.MVbin.lpbin[c].cb, lpFolderProps[1].Value.MVbin.lpbin[c].lpb);

		hr = ZCABContainer::Create(&m_lFolders, NULL, m_lpMAPISup, this, &lpRootContainer);
		if (hr != hrSuccess)
			goto exit;

		if (cbEntryID > 4+sizeof(GUID)) {
			// we're actually opening a contact .. so pass-through to the just opened rootcontainer
			hr = lpRootContainer->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, (LPUNKNOWN*)&lpContact);
			if (hr != hrSuccess)
				goto exit;
		}
	}

	if (lpContact) {
		if(lpInterface)
			hr = lpContact->QueryInterface(*lpInterface, (void **)lppUnk);
		else
			hr = lpContact->QueryInterface(IID_IMAPIProp, (void **)lppUnk);
	} else {
		*lpulObjType = MAPI_ABCONT;

		if(lpInterface)
			hr = lpRootContainer->QueryInterface(*lpInterface, (void **)lppUnk);
		else
			hr = lpRootContainer->QueryInterface(IID_IABContainer, (void **)lppUnk);
	}
	if(hr != hrSuccess)
		goto exit;

	if (!lpContact) {
		// root container has pointer to my m_lFolders
		AddChild(lpRootContainer);
	}

exit:
	if (lpProfileSection)
		lpProfileSection->Release();
	MAPIFreeBuffer(lpFolderProps);
	if (lpRootContainer)
		lpRootContainer->Release();

	if (lpContact)
		lpContact->Release();

	return hr;
}

HRESULT ZCABLogon::CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	// we don't implement this .. a real ab provider should do this action.
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCABLogon::Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection)
{
	if (lpAdviseSink == NULL || lpulConnection == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (lpEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCABLogon::Unadvise(ULONG ulConnection)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCABLogon::OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPMAPISTATUS * lppMAPIStatus)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCABLogon::OpenTemplateID(ULONG cbTemplateID, LPENTRYID lpTemplateID, ULONG ulTemplateFlags, LPMAPIPROP lpMAPIPropData, LPCIID lpInterface, LPMAPIPROP * lppMAPIPropNew, LPMAPIPROP lpMAPIPropSibling)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCABLogon::GetOneOffTable(ULONG ulFlags, LPMAPITABLE * lppTable)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCABLogon::PrepareRecips(ULONG ulFlags, LPSPropTagArray lpPropTagArray, LPADRLIST lpRecipList)
{
	if(lpPropTagArray == NULL || lpPropTagArray->cValues == 0)
		/* There is no work to do. */
		return hrSuccess;
	return MAPI_E_NO_SUPPORT;
}

DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, GetLastError, (HRESULT, hResult), (ULONG, ulFlags), (LPMAPIERROR *, lppMAPIError))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, Logoff, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, OpenEntry, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, CompareEntryIDs, (ULONG, cbEntryID1), (LPENTRYID, lpEntryID1), (ULONG, cbEntryID2), (LPENTRYID, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, Advise, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG, ulEventMask), (LPMAPIADVISESINK, lpAdviseSink), (ULONG *, lpulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, Unadvise, (ULONG, ulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, OpenStatusEntry, (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPMAPISTATUS *, lppMAPIStatus))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, OpenTemplateID, (ULONG, cbTemplateID), (LPENTRYID, lpTemplateID), (ULONG, ulTemplateFlags), (LPMAPIPROP, lpMAPIPropData), (LPCIID, lpInterface), (LPMAPIPROP *, lppMAPIPropNew), (LPMAPIPROP, lpMAPIPropSibling))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, GetOneOffTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ZCABLogon, ABLogon, PrepareRecips, (ULONG, ulFlags), (LPSPropTagArray, lpPropTagArray), (LPADRLIST, lpRecipList))
