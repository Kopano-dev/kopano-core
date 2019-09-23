/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapitags.h>
#include <mapiguid.h>
#include <mapiutil.h>
#include "kcore.hpp"
#include <kopano/ECGuid.h>
#include <kopano/ECDefs.h>
#include "ECMsgStore.h"
#include "ECMAPIProp.h"
#include "ECMemStream.h"
#include "Mem.h"
#include <kopano/Util.h>
#include <kopano/mapiext.h>
#include <kopano/CommonUtil.h>
#include <kopano/mapi_ptr.h>
#include "pcutil.hpp"
#include <sstream>

using namespace KC;

struct STREAMDATA {
	ULONG ulPropTag;
	ECMAPIProp *lpProp;
};

typedef memory_ptr<ECPERMISSION> ECPermissionPtr;

static struct rights ECPermToRightsCheap(const ECPERMISSION &p)
{
	struct rights r;
	r.ulType = p.ulType;
	r.ulRights = p.ulRights;
	r.ulState = p.ulState;
	r.sUserId.__size = p.sUserId.cb;
	r.sUserId.__ptr = p.sUserId.lpb;
	return r;
}

static ECPERMISSION RightsToECPermCheap(const struct rights r)
{
	ECPERMISSION p = {r.ulType, r.ulRights, RIGHT_NEW};	// Force to new
	p.sUserId.cb = r.sUserId.__size;
	p.sUserId.lpb = r.sUserId.__ptr;
	return p;
}

ECMAPIProp::ECMAPIProp(ECMsgStore *prov, ULONG objtype, BOOL modify,
    const ECMAPIProp *lpRoot, const char *cls_name) :
	ECGenericProp(prov, objtype, modify, cls_name),
	/*
	 * Track "root object". This is the object that was opened via
	 * OpenEntry or OpenMsgStore, so normally lpRoot == this, but in the
	 * case of attachments and submessages it points to the top-level
	 * message.
	 */
	m_lpRoot(lpRoot != nullptr ? lpRoot : this)
{
	HrAddPropHandlers(PR_STORE_ENTRYID, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_STORE_RECORD_KEY, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_STORE_SUPPORT_MASK, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_STORE_UNICODE_MASK, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_MAPPING_SIGNATURE, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_PARENT_ENTRYID, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_MDB_PROVIDER, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_LAST_MODIFICATION_TIME, DefaultMAPIGetProp, DefaultSetPropSetReal, this);
	HrAddPropHandlers(PR_CREATION_TIME, DefaultMAPIGetProp, DefaultSetPropIgnore, this);
	HrAddPropHandlers(PR_ACCESS_LEVEL, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_PARENT_SOURCE_KEY, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_RECORD_KEY, DefaultGetPropGetReal, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_EC_SERVER_UID, DefaultMAPIGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_EC_HIERARCHYID, DefaultMAPIGetProp, DefaultSetPropComputed, this, false, true);
	// ICS system
	HrAddPropHandlers(PR_SOURCE_KEY, DefaultMAPIGetProp, SetPropHandler, this, false, false);
}

HRESULT ECMAPIProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	REGISTER_INTERFACE2(IECSecurity, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

// Loads the properties of the saved message for use
HRESULT ECMAPIProp::HrLoadProps()
{
	return dwLastError = ECGenericProp::HrLoadProps();
}

HRESULT ECMAPIProp::SetICSObject(BOOL bICSObject)
{
	m_bICSObject = bICSObject;
	return hrSuccess;
}

BOOL ECMAPIProp::IsICSObject()
{
	return m_bICSObject;
}

// Property handles
HRESULT ECMAPIProp::DefaultMAPIGetProp(unsigned int ulPropTag,
    void *lpProvider, unsigned int ulFlags, SPropValue *lpsPropValue,
    ECGenericProp *lpParam, void *lpBase)
{
	HRESULT		hr = hrSuccess;
	auto lpMsgStore = static_cast<ECMsgStore *>(lpProvider);
	auto lpProp = static_cast<ECMAPIProp *>(lpParam);

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_SOURCE_KEY):
		hr = lpProp->HrGetRealProp(PR_SOURCE_KEY, ulFlags, lpBase, lpsPropValue);
		if(hr != hrSuccess)
			return hr;
		break;

	case PROP_ID(PR_MAPPING_SIGNATURE):
		// get the mapping signature from the store
		if (lpMsgStore == NULL || lpMsgStore->HrGetRealProp(PR_MAPPING_SIGNATURE, ulFlags, lpBase, lpsPropValue) != hrSuccess)
			hr = MAPI_E_NOT_FOUND;
		break;

	case PROP_ID(PR_STORE_RECORD_KEY):
		lpsPropValue->ulPropTag = PR_STORE_RECORD_KEY;
		lpsPropValue->Value.bin.cb = sizeof(MAPIUID);
		hr = ECAllocateMore(sizeof(MAPIUID), lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValue->Value.bin.lpb, &lpProp->GetMsgStore()->GetStoreGuid(), sizeof(MAPIUID));
		break;

	case PROP_ID(PR_STORE_SUPPORT_MASK):
	case PROP_ID(PR_STORE_UNICODE_MASK):
		if (CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_PUBLIC_GUID))
			lpsPropValue->Value.l = EC_SUPPORTMASK_PUBLIC;
		else if (CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID))
			lpsPropValue->Value.l = EC_SUPPORTMASK_DELEGATE;
		else if (CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_ARCHIVE_GUID))
			lpsPropValue->Value.l = EC_SUPPORTMASK_ARCHIVE;
		else
			lpsPropValue->Value.l = EC_SUPPORTMASK_PRIVATE;

		if(lpMsgStore->m_ulClientVersion == CLIENT_VERSION_OLK2000)
			lpsPropValue->Value.l &=~STORE_HTML_OK; // Remove the flag, other way outlook 2000 crashed
		// No real unicode support in outlook 2000 and xp
		if (lpMsgStore->m_ulClientVersion <= CLIENT_VERSION_OLK2002)
			lpsPropValue->Value.l &=~ STORE_UNICODE_OK;
		lpsPropValue->ulPropTag = CHANGE_PROP_TYPE(ulPropTag,PT_LONG);
		break;

	case PROP_ID(PR_STORE_ENTRYID): {
		lpsPropValue->ulPropTag = PR_STORE_ENTRYID;

		ULONG cbWrapped = 0;
		memory_ptr<ENTRYID> lpWrapped;

		hr = lpProp->GetMsgStore()->GetWrappedStoreEntryID(&cbWrapped, &~lpWrapped);
		if(hr == hrSuccess) {
			hr = ECAllocateMore(cbWrapped, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
			if (hr != hrSuccess)
				break;
			memcpy(lpsPropValue->Value.bin.lpb, lpWrapped, cbWrapped);
			lpsPropValue->Value.bin.cb = cbWrapped;
		}
		break;
	}
	case PROP_ID(PR_MDB_PROVIDER):
		hr = ECAllocateMore(sizeof(MAPIUID),lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValue->Value.bin.lpb, &lpMsgStore->m_guidMDB_Provider, sizeof(MAPIUID));
		lpsPropValue->Value.bin.cb = sizeof(MAPIUID);
		lpsPropValue->ulPropTag = PR_MDB_PROVIDER;
		break;

	case PROP_ID(PR_ACCESS_LEVEL):
		if(lpProp->HrGetRealProp(PR_ACCESS_LEVEL, ulFlags, lpBase, lpsPropValue) != erSuccess)
		{
			lpsPropValue->Value.l = lpProp->fModify ? MAPI_MODIFY : 0;
			//lpsPropValue->Value.l = 0;
			lpsPropValue->ulPropTag = PR_ACCESS_LEVEL;
		}
		break;
	case PROP_ID(PR_PARENT_ENTRYID):
		lpsPropValue->ulPropTag = PR_PARENT_ENTRYID;

		if (lpProp->m_lpParentID != NULL) {
			hr = ECAllocateMore(lpProp->m_cbParentID, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
			if (hr != hrSuccess)
				break;
			memcpy(lpsPropValue->Value.bin.lpb, lpProp->m_lpParentID, lpProp->m_cbParentID);
			lpsPropValue->Value.bin.cb = lpProp->m_cbParentID;
		} else {
			hr = lpProp->HrGetRealProp(PR_PARENT_ENTRYID, ulFlags, lpBase, lpsPropValue);
		}
		break;
	case PROP_ID(PR_EC_SERVER_UID): {
		lpsPropValue->ulPropTag = PR_EC_SERVER_UID;

		GUID sServerGuid = {0};
		hr = ((ECMAPIProp*)lpParam)->m_lpRoot->GetMsgStore()->lpTransport->GetServerGUID(&sServerGuid);
		if (hr == hrSuccess)
			hr = ECAllocateMore(sizeof(GUID), lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
		if (hr == hrSuccess) {
			memcpy(lpsPropValue->Value.bin.lpb, &sServerGuid, sizeof(GUID));
			lpsPropValue->Value.bin.cb = sizeof(GUID);
		}
		break;
	}
	case PROP_ID(PR_EC_HIERARCHYID):
		if (lpProp->m_sMapiObject == NULL) {
			hr = lpProp->HrLoadProps();
			if (hr != hrSuccess)
				return hr;
		}
		if (lpProp->m_sMapiObject != nullptr && lpProp->m_sMapiObject->ulObjId > 0) {
			lpsPropValue->ulPropTag = ulPropTag;
			lpsPropValue->Value.ul = lpProp->m_sMapiObject->ulObjId;
		} else {
			hr = MAPI_E_NOT_FOUND;
		}
		break;
	default:
		hr = lpProp->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		break;
	}
	return hr;
}

HRESULT ECMAPIProp::SetPropHandler(unsigned int ulPropTag, void *lpProvider,
    const SPropValue *lpsPropValue, ECGenericProp *lpParam)
{
	auto lpProp = static_cast<ECMAPIProp *>(lpParam);
	switch(ulPropTag) {
	case PR_SOURCE_KEY:
		if (lpProp->IsICSObject())
			return lpProp->HrSetRealProp(lpsPropValue);
		return hrSuccess; /* ignore the property */
	default:
		return MAPI_E_NOT_FOUND;
	}
}

HRESULT ECMAPIProp::TableRowGetProp(void *lpProvider,
    const struct propVal *lpsPropValSrc, SPropValue *lpsPropValDst,
    void **lpBase, ULONG ulType)
{
	HRESULT hr = hrSuccess;
	auto lpMsgStore = static_cast<ECMsgStore *>(lpProvider);

	switch(lpsPropValSrc->ulPropTag) {
	case PR_STORE_ENTRYID:
	{
		ULONG cbWrapped = 0;
		memory_ptr<ENTRYID> lpWrapped;

		// if we know, we are a spooler or a store than we can switch the function for 'speed-up'
		// hr = lpMsgStore->GetWrappedStoreEntryID(&cbWrapped, &lpWrapped);
		hr = lpMsgStore->GetWrappedServerStoreEntryID(lpsPropValSrc->Value.bin->__size, lpsPropValSrc->Value.bin->__ptr, &cbWrapped, &~lpWrapped);
		if (hr != hrSuccess)
			return hr;
		hr = ECAllocateMore(cbWrapped, lpBase, reinterpret_cast<void **>(&lpsPropValDst->Value.bin.lpb));
		if (hr != hrSuccess)
			return hr;
		memcpy(lpsPropValDst->Value.bin.lpb, lpWrapped, cbWrapped);
		lpsPropValDst->Value.bin.cb = cbWrapped;
		lpsPropValDst->ulPropTag = CHANGE_PROP_TYPE(lpsPropValSrc->ulPropTag, PT_BINARY);
		break;
	}
	case CHANGE_PROP_TYPE(PR_DISPLAY_TYPE, PT_ERROR):
		lpsPropValDst->Value.l = DT_FOLDER;				// FIXME, may be a search folder
		lpsPropValDst->ulPropTag = PR_DISPLAY_TYPE;
		break;
	case CHANGE_PROP_TYPE(PR_STORE_SUPPORT_MASK, PT_ERROR):
	case CHANGE_PROP_TYPE(PR_STORE_UNICODE_MASK, PT_ERROR):
		if (CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_PUBLIC_GUID))
			lpsPropValDst->Value.l = EC_SUPPORTMASK_PUBLIC;
		else if (CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID))
			lpsPropValDst->Value.l = EC_SUPPORTMASK_DELEGATE;
		else if (CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_ARCHIVE_GUID))
			lpsPropValDst->Value.l = EC_SUPPORTMASK_ARCHIVE;
		else
			lpsPropValDst->Value.l = EC_SUPPORTMASK_PRIVATE;

		if (lpMsgStore->m_ulClientVersion == CLIENT_VERSION_OLK2000)
			lpsPropValDst->Value.l &=~STORE_HTML_OK; // Remove the flag, other way outlook 2000 crashed
		// No real unicode support in outlook 2000 and xp
		if (lpMsgStore->m_ulClientVersion <= CLIENT_VERSION_OLK2002)
			lpsPropValDst->Value.l &=~ STORE_UNICODE_OK;
		lpsPropValDst->ulPropTag = CHANGE_PROP_TYPE(lpsPropValSrc->ulPropTag, PT_LONG);
		break;

	case CHANGE_PROP_TYPE(PR_STORE_RECORD_KEY, PT_ERROR):
		// Reset type to binary
		lpsPropValDst->ulPropTag = CHANGE_PROP_TYPE(lpsPropValSrc->ulPropTag, PT_BINARY);
		hr = ECAllocateMore(sizeof(MAPIUID), lpBase, reinterpret_cast<void **>(&lpsPropValDst->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValDst->Value.bin.lpb, &lpMsgStore->GetStoreGuid(), sizeof(MAPIUID));
		lpsPropValDst->Value.bin.cb = sizeof(MAPIUID);
		break;

	case CHANGE_PROP_TYPE(PR_MDB_PROVIDER, PT_ERROR):
		lpsPropValDst->ulPropTag = CHANGE_PROP_TYPE(lpsPropValSrc->ulPropTag, PT_BINARY);
		hr = ECAllocateMore(sizeof(MAPIUID), lpBase, reinterpret_cast<void **>(&lpsPropValDst->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValDst->Value.bin.lpb, &lpMsgStore->m_guidMDB_Provider, sizeof(MAPIUID));
		lpsPropValDst->Value.bin.cb = sizeof(MAPIUID);
		break;
	default:
		return MAPI_E_NOT_FOUND;
	}

	return hr;
}

// FIXME openproperty on computed value is illegal
HRESULT ECMAPIProp::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	if ((ulFlags & MAPI_CREATE && !(ulFlags & MAPI_MODIFY)) || lpiid == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	/* Only support certain property types */
	if (PROP_TYPE(ulPropTag) != PT_BINARY &&
	    PROP_TYPE(ulPropTag) != PT_UNICODE &&
	    PROP_TYPE(ulPropTag) != PT_STRING8)
		return MAPI_E_INVALID_PARAMETER;
	if (*lpiid != IID_IStream && *lpiid != IID_IStorage)
		return MAPI_E_INTERFACE_NOT_SUPPORTED;
	if (PROP_TYPE(ulPropTag) != PT_STRING8 &&
	    PROP_TYPE(ulPropTag) != PT_BINARY &&
	    PROP_TYPE(ulPropTag) != PT_UNICODE)
		return MAPI_E_NOT_FOUND;

	object_ptr<ECMemStream> lpStream;
	ecmem_ptr<SPropValue> lpsPropValue;

	if (*lpiid == IID_IStream && !m_props_loaded &&
	    PROP_TYPE(ulPropTag) == PT_BINARY && !(ulFlags & MAPI_MODIFY) &&
	    // Shortcut: don't load entire object if only one property is being requested for read-only. HrLoadProp() will return
		// without querying the server if the server does not support this capability (introduced in 6.20.8). Main reason is
		// calendar loading time with large recursive entries in outlook XP.
	    // If HrLoadProp failed, just fallback to the 'normal' way of loading properties.
	    lpStorage->HrLoadProp(0, ulPropTag, &~lpsPropValue) == erSuccess) {
		/* is freed by HrStreamCleanup, called by ECMemStream on refcount == 0 */
		auto lpStreamData = make_unique_nt<STREAMDATA>();
		if (lpStreamData == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		lpStreamData->ulPropTag = ulPropTag;
		lpStreamData->lpProp = this;
		auto hr = ECMemStream::Create(reinterpret_cast<char *>(lpsPropValue->Value.bin.lpb),
		          lpsPropValue->Value.bin.cb, ulInterfaceOptions,
		          nullptr, ECMAPIProp::HrStreamCleanup, lpStreamData.get(), &~lpStream);
		if (hr != hrSuccess)
			return hr;
		lpStreamData.release();
		lpStream->QueryInterface(IID_IStream, reinterpret_cast<void **>(lppUnk));
		AddChild(lpStream);
		return hr;
	}

	if (ulFlags & MAPI_MODIFY)
		ulInterfaceOptions |= STGM_WRITE;
	// IStream requested for a property
	auto hr = ECAllocateBuffer(sizeof(SPropValue), &~lpsPropValue);
	if (hr != hrSuccess)
		return hr;
	// Yank the property in from disk if it wasn't loaded yet
	HrLoadProp(ulPropTag);

	// For MAPI_CREATE, reset (or create) the property now
	if (ulFlags & MAPI_CREATE) {
		if (!fModify)
			return MAPI_E_NO_ACCESS;
		SPropValue sProp;
		sProp.ulPropTag = ulPropTag;
		if (PROP_TYPE(ulPropTag) == PT_BINARY) {
			sProp.Value.bin.cb = 0;
			sProp.Value.bin.lpb = NULL;
		} else {
			// Handles lpszA and lpszW since they are the same field in the union
			sProp.Value.lpszW = const_cast<wchar_t *>(L"");
		}
		hr = HrSetRealProp(&sProp);
		if (hr != hrSuccess)
			return hr;
	}

	hr = HrGetRealProp(ulPropTag, ulFlags, lpsPropValue, lpsPropValue);
	if (hr != hrSuccess)
		// Promote warnings from GetProps to error
		return MAPI_E_NOT_FOUND;

	/* is freed by HrStreamCleanup, called by ECMemStream on refcount == 0 */
	auto lpStreamData = make_unique_nt<STREAMDATA>();
	if (lpStreamData == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	lpStreamData->ulPropTag = ulPropTag;
	lpStreamData->lpProp = this;

	if (ulFlags & MAPI_CREATE) {
		hr = ECMemStream::Create(NULL, 0, ulInterfaceOptions,
		     ECMAPIProp::HrStreamCommit, ECMAPIProp::HrStreamCleanup, lpStreamData.get(), &~lpStream);
	} else {
		switch (PROP_TYPE(lpsPropValue->ulPropTag)) {
		case PT_STRING8:
			hr = ECMemStream::Create(lpsPropValue->Value.lpszA, strlen(lpsPropValue->Value.lpszA), ulInterfaceOptions,
			     ECMAPIProp::HrStreamCommit, ECMAPIProp::HrStreamCleanup,
			     lpStreamData.get(), &~lpStream);
			break;
		case PT_UNICODE:
			hr = ECMemStream::Create((char *)lpsPropValue->Value.lpszW,
			     wcslen(lpsPropValue->Value.lpszW) * sizeof(wchar_t), ulInterfaceOptions,
			     ECMAPIProp::HrStreamCommit, ECMAPIProp::HrStreamCleanup,
			     lpStreamData.get(), &~lpStream);
			break;
		case PT_BINARY:
			hr = ECMemStream::Create((char *)lpsPropValue->Value.bin.lpb, lpsPropValue->Value.bin.cb, ulInterfaceOptions,
			     ECMAPIProp::HrStreamCommit, ECMAPIProp::HrStreamCleanup,
			     lpStreamData.get(), &~lpStream);
			break;
		default:
			assert(false);
			hr = MAPI_E_NOT_FOUND;
			break;
		}
	}
	if (hr != hrSuccess)
		return hr;
	lpStreamData.release();
	if (*lpiid == IID_IStorage) //*lpiid == IID_IStreamDocfile ||
		//FIXME: Unknown what to do with flag STGSTRM_CURRENT
		hr = GetMsgStore()->lpSupport->IStorageFromStream(lpStream, nullptr,
		     ((ulFlags & MAPI_MODIFY) ? STGSTRM_MODIFY : 0) |
		     ((ulFlags & MAPI_CREATE) ? STGSTRM_CREATE : 0),
		     reinterpret_cast<IStorage **>(lppUnk));
	else
		hr = lpStream->QueryInterface(*lpiid, reinterpret_cast<void **>(lppUnk));
	if(hr != hrSuccess)
		return hr;
	AddChild(lpStream);
	return hrSuccess;
}

HRESULT ECMAPIProp::SaveChanges(ULONG ulFlags)
{
	if (lpStorage == nullptr)
		return MAPI_E_NOT_FOUND;
	if (!fModify)
		return MAPI_E_NO_ACCESS;

	object_ptr<WSMAPIPropStorage> lpMAPIPropStorage;

	// only folders and main messages have a syncid, attachments and msg-in-msg don't
	if (lpStorage->QueryInterface(IID_WSMAPIPropStorage, &~lpMAPIPropStorage) == hrSuccess) {
		auto hr = lpMAPIPropStorage->HrSetSyncId(m_ulSyncId);
		if(hr != hrSuccess)
			return hr;
	}
	return ECGenericProp::SaveChanges(ulFlags);
}

HRESULT ECMAPIProp::HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject) {
	// ECMessage implements saving an attachment
	// ECAttach implements saving a sub-message
	return MAPI_E_INVALID_OBJECT;
}

HRESULT ECMAPIProp::GetSerializedACLData(LPVOID lpBase, LPSPropValue lpsPropValue)
{
	object_ptr<IECSecurity> ptrSecurity;
	ULONG				cPerms = 0;
	ECPermissionPtr		ptrPerms;
	struct soap			soap;
	std::ostringstream	os;
	struct rightsArray	rights;
	std::string			strAclData;

	auto laters = make_scope_success([&]() {
		soap_destroy(&soap);
		soap_end(&soap); // clean up allocated temporaries
	});
	auto hr = QueryInterface(IID_IECSecurity, &~ptrSecurity);
	if (hr != hrSuccess)
		return hr;
	hr = ptrSecurity->GetPermissionRules(ACCESS_TYPE_GRANT, &cPerms, &~ptrPerms);
	if (hr != hrSuccess)
		return hr;

	rights.__size = cPerms;
	rights.__ptr  = soap_new_rights(&soap, cPerms);
	std::transform(ptrPerms.get(), ptrPerms + cPerms, rights.__ptr, &ECPermToRightsCheap);

	soap_set_omode(&soap, SOAP_C_UTFSTRING);
	soap_begin(&soap);
	soap.os = &os;
	soap_serialize_rightsArray(&soap, &rights);
	if (soap_begin_send(&soap) != 0 ||
	    soap_put_rightsArray(&soap, &rights, "rights", "rightsArray") != 0 ||
	    soap_end_send(&soap) != 0)
		return MAPI_E_NETWORK_ERROR;

	strAclData = os.str();
	lpsPropValue->Value.bin.cb = strAclData.size();
	return KAllocCopy(strAclData.data(), lpsPropValue->Value.bin.cb, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb), lpBase);
}

HRESULT ECMAPIProp::SetSerializedACLData(const SPropValue *lpsPropValue)
{
	if (lpsPropValue == nullptr ||
	    PROP_TYPE(lpsPropValue->ulPropTag) != PT_BINARY)
		return MAPI_E_INVALID_PARAMETER;

	ECPermissionPtr		ptrPerms;
	struct soap			soap;
	struct rightsArray	rights;
	std::string			strAclData;

	auto laters = make_scope_success([&]() {
		soap_destroy(&soap);
		soap_end(&soap); // clean up allocated temporaries
	});

	{
		std::istringstream is(std::string((char*)lpsPropValue->Value.bin.lpb, lpsPropValue->Value.bin.cb));

		soap.is = &is;
		soap_set_imode(&soap, SOAP_C_UTFSTRING);
		soap_begin(&soap);
		if (soap_begin_recv(&soap) != 0)
			return MAPI_E_NETWORK_FAILURE;

		if (!soap_get_rightsArray(&soap, &rights, "rights", "rightsArray"))
			return MAPI_E_CORRUPT_DATA;

		if (soap_end_recv(&soap) != 0)
			return MAPI_E_NETWORK_ERROR;
	}
	auto hr = MAPIAllocateBuffer(rights.__size * sizeof(ECPERMISSION), &~ptrPerms);
	if (hr != hrSuccess)
		return hr;

	std::transform(rights.__ptr, rights.__ptr + rights.__size, ptrPerms.get(), &RightsToECPermCheap);
	return UpdateACLs(rights.__size, ptrPerms);
}

HRESULT	ECMAPIProp::UpdateACLs(ULONG cNewPerms, ECPERMISSION *lpNewPerms)
{
	object_ptr<IECSecurity> ptrSecurity;
	ULONG cPerms = 0, cSparePerms = 0;
	memory_ptr<ECPERMISSION> ptrPerms;
	ECPermissionPtr			ptrTmpPerms;
	auto hr = QueryInterface(IID_IECSecurity, &~ptrSecurity);
	if (hr != hrSuccess)
		return hr;
	hr = ptrSecurity->GetPermissionRules(ACCESS_TYPE_GRANT, &cPerms, &~ptrPerms);
	if (hr != hrSuccess)
		return hr;

	// Since we want to replace the current ACL with a new one, we need to mark
	// each existing item as deleted, and add all new ones as new.
	// But there can also be overlap, where some items are left unchanged, and
	// other modified.
	for (ULONG i = 0; i < cPerms; ++i) {
		auto lpMatch = std::find_if(lpNewPerms, lpNewPerms + cNewPerms,
			[&](const ECPERMISSION &r) {
				auto &l = ptrPerms[i].sUserId;
				return CompareABEID(l.cb, reinterpret_cast<ENTRYID *>(l.lpb),
				       r.sUserId.cb, reinterpret_cast<ENTRYID *>(r.sUserId.lpb));
			});
		if (lpMatch == lpNewPerms + cNewPerms) {
			// Not in new set, so delete
			ptrPerms[i].ulState = RIGHT_DELETED;
			continue;
		}
		// Found an entry in the new set, check if it's different
		if (ptrPerms[i].ulRights == lpMatch->ulRights &&
			ptrPerms[i].ulType == lpMatch->ulType)
		{
			// Nothing changes, remove from set.
			if (i < (cPerms - 1))
				std::swap(ptrPerms[i], ptrPerms[cPerms - 1]);
			--cPerms;
			--i;
			++cSparePerms;
		} else {
			ptrPerms[i].ulRights = lpMatch->ulRights;
			ptrPerms[i].ulType = lpMatch->ulType;
			ptrPerms[i].ulState = RIGHT_MODIFY;
		}

		// Remove from list of new permissions
		if (lpMatch != &lpNewPerms[cNewPerms - 1])
			std::swap(*lpMatch, lpNewPerms[cNewPerms - 1]);
		--cNewPerms;
	}

	// Now see if there are still some new ACLs left. If enough spare space is available,
	// we will reuse the ptrPerms storage. If not, we will reallocate the whole array.
	auto lpPermissions = ptrPerms.get();
	if (cNewPerms > 0) {
		if (cNewPerms <= cSparePerms) {
			memcpy(&ptrPerms[cPerms], lpNewPerms, cNewPerms * sizeof(ECPERMISSION));
		} else if (cPerms == 0) {
			lpPermissions = lpNewPerms;
		} else {
			hr = MAPIAllocateBuffer((cPerms + cNewPerms) * sizeof(ECPERMISSION), &~ptrTmpPerms);
			if (hr != hrSuccess)
				return hr;
			memcpy(ptrTmpPerms, ptrPerms, cPerms * sizeof(ECPERMISSION));
			memcpy(ptrTmpPerms + cPerms, lpNewPerms, cNewPerms * sizeof(ECPERMISSION));
			lpPermissions = ptrTmpPerms;
		}
	}

	if (cPerms + cNewPerms > 0)
		hr = ptrSecurity->SetPermissionRules(cPerms + cNewPerms, lpPermissions);
	return hrSuccess;
}

HRESULT ECMAPIProp::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyTo(&IID_IMAPIProp, static_cast<IMAPIProp *>(this), ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMAPIProp::CopyProps(const SPropTagArray *lpIncludeProps,
    ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface,
    void *lpDestObj, ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return Util::DoCopyProps(&IID_IMAPIProp, static_cast<IMAPIProp *>(this), lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

// Pass call off to lpMsgStore
HRESULT ECMAPIProp::GetNamesFromIDs(SPropTagArray **lppPropTags,
    const GUID *lpPropSetGuid, ULONG ulFlags, ULONG *lpcPropNames,
    MAPINAMEID ***lpppPropNames)
{
	return GetMsgStore()->lpNamedProp.GetNamesFromIDs(lppPropTags, lpPropSetGuid, ulFlags, lpcPropNames, lpppPropNames);
}

HRESULT ECMAPIProp::GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags)
{
	return GetMsgStore()->lpNamedProp.GetIDsFromNames(cPropNames, lppPropNames, ulFlags, lppPropTags);
}

// Stream functions
HRESULT ECMAPIProp::HrStreamCommit(IStream *lpStream, void *lpData)
{
	auto lpStreamData = static_cast<STREAMDATA *>(lpData);
	ecmem_ptr<SPropValue> lpPropValue;
	STATSTG sStat;
	ULONG ulSize = 0;
	object_ptr<ECMemStream> lpECStream;
	auto hr = ECAllocateBuffer(sizeof(SPropValue), &~lpPropValue);
	if(hr != hrSuccess)
		return hr;
	hr = lpStream->Stat(&sStat, 0);
	if(hr != hrSuccess)
		return hr;

	if(PROP_TYPE(lpStreamData->ulPropTag) == PT_STRING8) {
		char *buffer = nullptr;
		hr = ECAllocateMore((ULONG)sStat.cbSize.QuadPart + 1, lpPropValue, reinterpret_cast<void **>(&buffer));
		if(hr != hrSuccess)
			return hr;
		// read the data into the buffer
		hr = lpStream->Read(buffer, (ULONG)sStat.cbSize.QuadPart, &ulSize);
		buffer[ulSize] = '\0';
		lpPropValue->Value.lpszA = buffer;
	} else if(PROP_TYPE(lpStreamData->ulPropTag) == PT_UNICODE) {
		wchar_t *buffer = nullptr;
		hr = ECAllocateMore((ULONG)sStat.cbSize.QuadPart + sizeof(wchar_t),
		     lpPropValue, reinterpret_cast<void **>(&buffer));
		if(hr != hrSuccess)
			return hr;
		// read the data into the buffer
		hr = lpStream->Read(buffer, (ULONG)sStat.cbSize.QuadPart, &ulSize);
		/* no point in dealing with an incomplete multibyte sequence */
		ulSize = ulSize / sizeof(wchar_t);
		buffer[ulSize] = '\0';
		lpPropValue->Value.lpszW = buffer;
	} else{
		hr = lpStream->QueryInterface(IID_ECMemStream, &~lpECStream);
		if(hr != hrSuccess)
			return hr;
		lpPropValue->Value.bin.cb = static_cast<unsigned int>(sStat.cbSize.QuadPart);
		lpPropValue->Value.bin.lpb = reinterpret_cast<unsigned char *>(lpECStream->GetBuffer());
	}

	lpPropValue->ulPropTag = lpStreamData->ulPropTag;
	hr = lpStreamData->lpProp->HrSetRealProp(lpPropValue);
	if (hr != hrSuccess)
		return hr;
	// on a non transacted object SaveChanges is required
	if (!lpStreamData->lpProp->isTransactedObject)
		hr = lpStreamData->lpProp->ECGenericProp::SaveChanges(KEEP_OPEN_READWRITE);
	return hr;
}

HRESULT ECMAPIProp::HrStreamCleanup(void *lpData)
{
	delete static_cast<STREAMDATA *>(lpData);
	return hrSuccess;
}

HRESULT ECMAPIProp::HrSetSyncId(ULONG ulSyncId)
{
	object_ptr<WSMAPIPropStorage> lpMAPIPropStorage;

	if (lpStorage != nullptr && lpStorage->QueryInterface(IID_WSMAPIPropStorage, &~lpMAPIPropStorage) == hrSuccess) {
		auto hr = lpMAPIPropStorage->HrSetSyncId(ulSyncId);
		if(hr != hrSuccess)
			return hr;
	}
	m_ulSyncId = ulSyncId;
	return hrSuccess;
}

// Security functions
HRESULT ECMAPIProp::GetPermissionRules(int ulType, ULONG *lpcPermissions,
    ECPERMISSION **lppECPermissions)
{
	if (m_lpEntryId == NULL)
		return MAPI_E_NO_ACCESS;
	return GetMsgStore()->lpTransport->HrGetPermissionRules(ulType,
	       m_cbEntryId, m_lpEntryId, lpcPermissions, lppECPermissions);
}

HRESULT ECMAPIProp::SetPermissionRules(ULONG cPermissions,
    const ECPERMISSION *lpECPermissions)
{
	if (m_lpEntryId == NULL)
		return MAPI_E_NO_ACCESS;
	return GetMsgStore()->lpTransport->HrSetPermissionRules(m_cbEntryId,
	       m_lpEntryId, cPermissions, lpECPermissions);
}

HRESULT ECMAPIProp::GetOwner(ULONG *lpcbOwner, LPENTRYID *lppOwner)
{
	if (lpcbOwner == NULL || lppOwner == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (m_lpEntryId == NULL)
		return MAPI_E_NO_ACCESS;
	return GetMsgStore()->lpTransport->HrGetOwner(m_cbEntryId, m_lpEntryId, lpcbOwner, lppOwner);
}

HRESULT ECMAPIProp::GetUserList(ULONG cbCompanyId, const ENTRYID *lpCompanyId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	return GetMsgStore()->lpTransport->HrGetUserList(cbCompanyId, lpCompanyId, ulFlags, lpcUsers, lppsUsers);
}

HRESULT ECMAPIProp::GetGroupList(ULONG cbCompanyId, const ENTRYID *lpCompanyId,
    ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups)
{
	return GetMsgStore()->lpTransport->HrGetGroupList(cbCompanyId, lpCompanyId, ulFlags, lpcGroups, lppsGroups);
}

HRESULT ECMAPIProp::GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies,
    ECCOMPANY **lppsCompanies)
{
	return GetMsgStore()->lpTransport->HrGetCompanyList(ulFlags, lpcCompanies, lppsCompanies);
}

HRESULT ECMAPIProp::SetParentID(ULONG cbParentID, const ENTRYID *lpParentID)
{
	assert(m_lpParentID == NULL);
	if (lpParentID == NULL || cbParentID == 0)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = KAllocCopy(lpParentID, cbParentID, &~m_lpParentID);
	if (hr != hrSuccess)
		return hr;
	m_cbParentID = cbParentID;
	return hrSuccess;
}
