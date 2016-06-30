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
#include "kcore.hpp"
#include <mapix.h>
#include <edkmdb.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

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
	REGISTER_INTERFACE(IID_ZCABLogon, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IABLogon, &this->m_xABLogon);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xABLogon);

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
	HRESULT hr = hrSuccess;
	zcabFolderEntry entry;

	if (cbStore == 0 || lpStore == NULL || cbFolder == 0 || lpFolder == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	entry.strwDisplayName = lpwDisplayName;

	entry.cbStore = cbStore;
	hr = MAPIAllocateBuffer(cbStore, (void**)&entry.lpStore);
	if (hr != hrSuccess)
		goto exit;
	memcpy(entry.lpStore, lpStore, cbStore);

	entry.cbFolder = cbFolder;
	hr = MAPIAllocateBuffer(cbFolder, (void**)&entry.lpFolder);
	if (hr != hrSuccess)
		goto exit;
	memcpy(entry.lpFolder, lpFolder, cbFolder);

	m_lFolders.push_back(entry);

exit:
	return hr;
}

HRESULT ZCABLogon::ClearFolderList()
{
	for (std::vector<zcabFolderEntry>::const_iterator i = m_lFolders.begin();
	     i != m_lFolders.end(); ++i) {
		MAPIFreeBuffer(i->lpStore);
		MAPIFreeBuffer(i->lpFolder);
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

		hr = lpProfileSection->GetProps((LPSPropTagArray)&sptaFolderProps, 0, &cValues, &lpFolderProps);
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
	HRESULT hr = hrSuccess;

	if(lpAdviseSink == NULL || lpulConnection == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if(lpEntryID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = MAPI_E_NO_SUPPORT;

exit:
	return hr;
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
	HRESULT			hr = hrSuccess;

	if(lpPropTagArray == NULL || lpPropTagArray->cValues == 0) // There is no work to do.
		goto exit;

	hr = MAPI_E_NO_SUPPORT;

exit:
	return hr;
}


HRESULT ZCABLogon::xABLogon::QueryInterface(REFIID refiid, void ** lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ZCABLogon , ABLogon);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG ZCABLogon::xABLogon::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::AddRef", "");
	METHOD_PROLOGUE_(ZCABLogon , ABLogon);
	return pThis->AddRef();
}

ULONG ZCABLogon::xABLogon::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::Release", "");
	METHOD_PROLOGUE_(ZCABLogon , ABLogon);
	ULONG ulRef = pThis->Release();
	TRACE_MAPI(TRACE_RETURN, "IABLogon::Release", "%d", ulRef);
	return ulRef;
}

HRESULT ZCABLogon::xABLogon::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::GetLastError", "");
	METHOD_PROLOGUE_(ZCABLogon , ABLogon);
	HRESULT hr = pThis->GetLastError(hResult, ulFlags, lppMAPIError);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::GetLastError", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ZCABLogon::xABLogon::Logoff(ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::Logoff", "");
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->Logoff(ulFlags);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::Logoff", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ZCABLogon::xABLogon::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) 
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::OpenEntry", "cbEntryID=%d, type=%d, id=%d, interface=%s, flags=0x%08X", cbEntryID, ABEID_TYPE(lpEntryID), ABEID_ID(lpEntryID), (lpInterface)?DBGGUIDToString(*lpInterface).c_str():"", ulFlags);
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::OpenEntry", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ZCABLogon::xABLogon::CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult) 
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::CompareEntryIDs", "flags: %d\nentryid1: %s\nentryid2: %s", ulFlags, bin2hex(cbEntryID1, (BYTE*)lpEntryID1).c_str(), bin2hex(cbEntryID2, (BYTE*)lpEntryID2).c_str());
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->CompareEntryIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::CompareEntryIDs", "%s, result=%s", GetMAPIErrorDescription(hr).c_str(), (lpulResult)?((*lpulResult == TRUE)?"TRUE":"FALSE") : "NULL");
	return hr;
}

HRESULT ZCABLogon::xABLogon::Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection) 
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::Advise", "");
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->Advise(cbEntryID, lpEntryID, ulEventMask, lpAdviseSink, lpulConnection);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::Advise", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ZCABLogon::xABLogon::Unadvise(ULONG ulConnection)
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::Unadvise", "");
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->Unadvise(ulConnection);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::Unadvise", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ZCABLogon::xABLogon::OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPMAPISTATUS * lppMAPIStatus)
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::OpenStatusEntry", "");
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->OpenStatusEntry(lpInterface, ulFlags, lpulObjType, lppMAPIStatus);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::OpenStatusEntry", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ZCABLogon::xABLogon::OpenTemplateID(ULONG cbTemplateID, LPENTRYID lpTemplateID, ULONG ulTemplateFlags, LPMAPIPROP lpMAPIPropData, LPCIID lpInterface, LPMAPIPROP * lppMAPIPropNew, LPMAPIPROP lpMAPIPropSibling)
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::OpenTemplateID", "");
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->OpenTemplateID(cbTemplateID, lpTemplateID, ulTemplateFlags, lpMAPIPropData, lpInterface, lppMAPIPropNew, lpMAPIPropSibling);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::OpenTemplateID", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ZCABLogon::xABLogon::GetOneOffTable(ULONG ulFlags, LPMAPITABLE * lppTable)
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::GetOneOffTable", "flags=0x%08X", ulFlags);
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->GetOneOffTable(ulFlags, lppTable);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::GetOneOffTable", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ZCABLogon::xABLogon::PrepareRecips(ULONG ulFlags, LPSPropTagArray lpPropTagArray, LPADRLIST lpRecipList)
{
	TRACE_MAPI(TRACE_ENTRY, "IABLogon::PrepareRecips", "flags=0x%08X, PropTagArray=%s\nin=%s", ulFlags, PropNameFromPropTagArray(lpPropTagArray).c_str(), RowSetToString((LPSRowSet)lpRecipList).c_str());
	METHOD_PROLOGUE_(ZCABLogon, ABLogon);
	HRESULT hr = pThis->PrepareRecips(ulFlags, lpPropTagArray, lpRecipList);
	TRACE_MAPI(TRACE_RETURN, "IABLogon::PrepareRecips", "%s\nout=%s", GetMAPIErrorDescription(hr).c_str(), RowSetToString((LPSRowSet)lpRecipList).c_str() );
	return hr;
}
