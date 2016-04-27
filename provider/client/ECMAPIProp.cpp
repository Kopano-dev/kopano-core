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

#include <kopano/ECDebug.h>
#include <kopano/mapiext.h>

#include <kopano/CommonUtil.h>
#include <kopano/mapi_ptr.h>
#include "pcutil.hpp"

#include <sstream>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static const char THIS_FILE[] = __FILE__;
#endif

static struct rights ECPermToRightsCheap(const ECPERMISSION &p)
{
	struct rights r = {0, p.ulType, p.ulRights, p.ulState};
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

class FindUser {
public:
	FindUser(const ECENTRYID &sEntryID): m_sEntryID(sEntryID) {}
	bool operator()(const ECPERMISSION &sPermission) const {
		return CompareABEID(m_sEntryID.cb, (LPENTRYID)m_sEntryID.lpb, sPermission.sUserId.cb, (LPENTRYID)sPermission.sUserId.lpb);
	}

private:
	const ECENTRYID &m_sEntryID;
};


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

ECMAPIProp::ECMAPIProp(void *lpProvider, ULONG ulObjType, BOOL fModify,
    ECMAPIProp *lpRoot, const char *szClassName) :
	ECGenericProp(lpProvider, ulObjType, fModify, szClassName)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMAPIProp::ECMAPIProp","");
	
	this->HrAddPropHandlers(PR_STORE_ENTRYID,			DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_STORE_RECORD_KEY,		DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_STORE_SUPPORT_MASK,		DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_STORE_UNICODE_MASK,		DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_MAPPING_SIGNATURE,		DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_PARENT_ENTRYID,			DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_MDB_PROVIDER,			DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_LAST_MODIFICATION_TIME,	DefaultMAPIGetProp,		DefaultSetPropSetReal,  (void*) this);
	this->HrAddPropHandlers(PR_CREATION_TIME,			DefaultMAPIGetProp,		DefaultSetPropIgnore,   (void*) this);
	this->HrAddPropHandlers(PR_ACCESS_LEVEL,			DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_PARENT_SOURCE_KEY,		DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_RECORD_KEY,				DefaultGetPropGetReal, 	DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_EC_SERVER_UID,			DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this);
	this->HrAddPropHandlers(PR_EC_HIERARCHYID,			DefaultMAPIGetProp,		DefaultSetPropComputed, (void*) this, FALSE, TRUE);

	// ICS system
	this->HrAddPropHandlers(PR_SOURCE_KEY,		DefaultMAPIGetProp	,SetPropHandler,		(void*) this, FALSE, FALSE);

	// Used for loadsim
	this->HrAddPropHandlers(0x664B0014/*PR_REPLICA_VERSION*/,		DefaultMAPIGetProp	,DefaultSetPropIgnore,		(void*) this, FALSE, FALSE);

	m_bICSObject = FALSE;
	m_ulSyncId = 0;
	m_cbParentID = 0;
	m_lpParentID = NULL;

	// Track 'root object'. This is the object that was opened via OpenEntry or OpenMsgStore, so normally
	// lpRoot == this, but in the case of attachments and submessages it points to the top-level message
	if(lpRoot)
		m_lpRoot = lpRoot;
	else
		m_lpRoot = this;
}

ECMAPIProp::~ECMAPIProp()
{
	TRACE_MAPI(TRACE_ENTRY, "ECMAPIProp::~ECMAPIProp","");
	MAPIFreeBuffer(m_lpParentID);
}

HRESULT ECMAPIProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECMAPIProp, this);
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IMAPIProp, &this->m_xMAPIProp);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xMAPIProp);

	REGISTER_INTERFACE(IID_IECSecurity, &this->m_xECSecurity);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

ECMsgStore* ECMAPIProp::GetMsgStore()
{
	return (ECMsgStore*)lpProvider;
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

/////////////////////////////////////////////////
// Property handles
//

HRESULT	ECMAPIProp::DefaultMAPIGetProp(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT		hr = hrSuccess;
	ECMsgStore* lpMsgStore = (ECMsgStore*) lpProvider;
	ECMAPIProp*	lpProp = (ECMAPIProp *)lpParam;

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_SOURCE_KEY):
		hr = lpProp->HrGetRealProp(PR_SOURCE_KEY, ulFlags, lpBase, lpsPropValue);
		if(hr != hrSuccess)
			return hr;
#ifdef WIN32
		if(lpMsgStore->m_ulProfileFlags & EC_PROFILE_FLAGS_TRUNCATE_SOURCEKEY && lpsPropValue->Value.bin.cb > 22) {
			/*
			 * Make our source key a little shorter; MSEMS has 22-byte sourcekeys, while we can have 24-byte
			 * source keys. This can cause buffer overflows in applications that (wrongly) assume fixed 22-byte
			 * source keys. We're not losing any data here actually, since the last two bytes are always 0000 since
			 * the source key ends in a 128-bit little-endian counter which will definitely never increment the last two
			 * bytes in the coming 1000000 years ;)
			 *
			 * Then, mark the sourcekey as being truncated by setting the highest bit in the ID part to 1, so we can 
			 * untruncate it when needed.
			 */
			lpsPropValue->Value.bin.cb = 22;
			lpsPropValue->Value.bin.lpb[lpsPropValue->Value.bin.cb-1] |= 0x80; // Set top bit
		}
#endif
		break;
		
	case PROP_ID(0x664B0014):
		// Used for loadsim
		lpsPropValue->ulPropTag = 0x664B0014;//PR_REPLICA_VERSION;
		lpsPropValue->Value.li.QuadPart = 1688871835664386LL;
		break;
		
	case PROP_ID(PR_MAPPING_SIGNATURE):
		// get the mapping signature from the store
		if(lpMsgStore == NULL || lpMsgStore->HrGetRealProp(PR_MAPPING_SIGNATURE, ulFlags, lpBase, lpsPropValue) != hrSuccess) {
			hr = MAPI_E_NOT_FOUND;
		}
		break;

	case PROP_ID(PR_STORE_RECORD_KEY):
		lpsPropValue->ulPropTag = PR_STORE_RECORD_KEY;
		lpsPropValue->Value.bin.cb = sizeof(MAPIUID);
		ECAllocateMore(sizeof(MAPIUID), lpBase, (void **)&lpsPropValue->Value.bin.lpb);
		memcpy(lpsPropValue->Value.bin.lpb, &lpProp->GetMsgStore()->GetStoreGuid(), sizeof(MAPIUID));
		break;

	case PROP_ID(PR_STORE_SUPPORT_MASK):
	case PROP_ID(PR_STORE_UNICODE_MASK):
		if(CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_PUBLIC_GUID))
			lpsPropValue->Value.l = EC_SUPPORTMASK_PUBLIC;
		else if(CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID)){
			lpsPropValue->Value.l = EC_SUPPORTMASK_DELEGATE;
		}else if(CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_ARCHIVE_GUID)) {
			lpsPropValue->Value.l = EC_SUPPORTMASK_ARCHIVE;
		}else {
			lpsPropValue->Value.l = EC_SUPPORTMASK_PRIVATE;
		}

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
		LPENTRYID lpWrapped = NULL;

		hr = lpProp->GetMsgStore()->GetWrappedStoreEntryID(&cbWrapped, &lpWrapped);

		if(hr == hrSuccess) {
			ECAllocateMore(cbWrapped, lpBase, (LPVOID *)&lpsPropValue->Value.bin.lpb);
			memcpy(lpsPropValue->Value.bin.lpb, lpWrapped, cbWrapped);
			lpsPropValue->Value.bin.cb = cbWrapped;
			MAPIFreeBuffer(lpWrapped);
		}
		break;
	}
	case PROP_ID(PR_MDB_PROVIDER):
		ECAllocateMore(sizeof(MAPIUID),lpBase, (LPVOID *)&lpsPropValue->Value.bin.lpb);
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
			ECAllocateMore(lpProp->m_cbParentID, lpBase, (LPVOID *)&lpsPropValue->Value.bin.lpb);
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
			hr = ECAllocateMore(sizeof(GUID), lpBase, (LPVOID*)&lpsPropValue->Value.bin.lpb);
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
		if (lpProp->m_sMapiObject->ulObjId > 0) {
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

HRESULT ECMAPIProp::SetPropHandler(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam)
{
	HRESULT hr = hrSuccess;
	ECMAPIProp*	lpProp = (ECMAPIProp *)lpParam;

	switch(ulPropTag) {
	case PR_SOURCE_KEY:
		if (lpProp->IsICSObject())
			hr = lpProp->HrSetRealProp(lpsPropValue);
		else
			hr = hrSuccess; // ignore the property
		break;

	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
}

HRESULT ECMAPIProp::TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType)
{
	HRESULT hr = hrSuccess;

	ECMsgStore* lpMsgStore = (ECMsgStore*)lpProvider;

	switch(lpsPropValSrc->ulPropTag) {

		case PR_STORE_ENTRYID:
		{				
			ULONG cbWrapped = 0;
			LPENTRYID lpWrapped = NULL;

			// if we know, we are a spooler or a store than we can switch the function for 'speed-up'
			// hr = lpMsgStore->GetWrappedStoreEntryID(&cbWrapped, &lpWrapped);
			hr = lpMsgStore->GetWrappedServerStoreEntryID(lpsPropValSrc->Value.bin->__size, lpsPropValSrc->Value.bin->__ptr, &cbWrapped, &lpWrapped);

			if(hr == hrSuccess) {
				ECAllocateMore(cbWrapped, lpBase, (void **)&lpsPropValDst->Value.bin.lpb);
				memcpy(lpsPropValDst->Value.bin.lpb, lpWrapped, cbWrapped);
				lpsPropValDst->Value.bin.cb = cbWrapped;
				lpsPropValDst->ulPropTag = PROP_TAG(PT_BINARY,PROP_ID(lpsPropValSrc->ulPropTag));
				MAPIFreeBuffer(lpWrapped);
			}

			break;
		}	

		case PROP_TAG(PT_ERROR,PROP_ID(PR_DISPLAY_TYPE)): 
			lpsPropValDst->Value.l = DT_FOLDER;				// FIXME, may be a search folder
			lpsPropValDst->ulPropTag = PR_DISPLAY_TYPE;
			break;
		
		case PROP_TAG(PT_ERROR,PROP_ID(PR_STORE_SUPPORT_MASK)):
		case PROP_TAG(PT_ERROR,PROP_ID(PR_STORE_UNICODE_MASK)):
			if(CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_PUBLIC_GUID))
				lpsPropValDst->Value.l = EC_SUPPORTMASK_PUBLIC;
			else if(CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID))
				lpsPropValDst->Value.l = EC_SUPPORTMASK_DELEGATE;
			else if(CompareMDBProvider(&lpMsgStore->m_guidMDB_Provider, &KOPANO_STORE_ARCHIVE_GUID))
				lpsPropValDst->Value.l = EC_SUPPORTMASK_ARCHIVE;
			else 
				lpsPropValDst->Value.l = EC_SUPPORTMASK_PRIVATE;

			if(lpMsgStore->m_ulClientVersion == CLIENT_VERSION_OLK2000)
				lpsPropValDst->Value.l &=~STORE_HTML_OK; // Remove the flag, other way outlook 2000 crashed
		
			// No real unicode support in outlook 2000 and xp
			if (lpMsgStore->m_ulClientVersion <= CLIENT_VERSION_OLK2002)
				lpsPropValDst->Value.l &=~ STORE_UNICODE_OK;

			lpsPropValDst->ulPropTag = CHANGE_PROP_TYPE(lpsPropValSrc->ulPropTag, PT_LONG);
			break;
		
		case PROP_TAG(PT_ERROR,PROP_ID(PR_STORE_RECORD_KEY)): 
			// Reset type to binary
			lpsPropValDst->ulPropTag = PROP_TAG(PT_BINARY,PROP_ID(lpsPropValSrc->ulPropTag));

			ECAllocateMore(sizeof(MAPIUID), lpBase, (void **)&lpsPropValDst->Value.bin.lpb);
			memcpy(lpsPropValDst->Value.bin.lpb, &lpMsgStore->GetStoreGuid(), sizeof(MAPIUID));
			lpsPropValDst->Value.bin.cb = sizeof(MAPIUID);
			break;

		case PROP_TAG(PT_ERROR,PROP_ID(PR_MDB_PROVIDER)):
			lpsPropValDst->ulPropTag = PROP_TAG(PT_BINARY,PROP_ID(lpsPropValSrc->ulPropTag));

			ECAllocateMore(sizeof(MAPIUID), lpBase, (void **)&lpsPropValDst->Value.bin.lpb);
			memcpy(lpsPropValDst->Value.bin.lpb, &lpMsgStore->m_guidMDB_Provider, sizeof(MAPIUID));
			lpsPropValDst->Value.bin.cb = sizeof(MAPIUID);
			break;
		default:
			hr = MAPI_E_NOT_FOUND;
			break;
	}

	return hr;
}


// FIXME openproperty on computed value is illegal
HRESULT ECMAPIProp::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk)
{
    HRESULT hr = hrSuccess;
	ECMemStream *lpStream = NULL;
	LPSPropValue lpsPropValue = NULL;
	STREAMDATA *lpStreamData = NULL;

	if((ulFlags&MAPI_CREATE && !(ulFlags&MAPI_MODIFY)) || lpiid == NULL)
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	
	// Only support certain property types
	if(PROP_TYPE(ulPropTag) != PT_BINARY && PROP_TYPE(ulPropTag) != PT_UNICODE && PROP_TYPE(ulPropTag) != PT_STRING8) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if(*lpiid == IID_IStream || *lpiid == IID_IStorage) {
		if(PROP_TYPE(ulPropTag) != PT_STRING8 && PROP_TYPE(ulPropTag) != PT_BINARY && PROP_TYPE(ulPropTag) != PT_UNICODE) {
			hr = MAPI_E_NOT_FOUND;
			goto exit;
		}

		if(*lpiid == IID_IStream && this->lstProps == NULL && PROP_TYPE(ulPropTag) == PT_BINARY && !(ulFlags & MAPI_MODIFY)) {
		    // Shortcut: don't load entire object if only one property is being requested for read-only. HrLoadProp() will return
			// without querying the server if the server does not support this capability (introduced in 6.20.8). Main reason is
			// calendar loading time with large recursive entries in outlook XP.
			if(this->lpStorage->HrLoadProp(0, ulPropTag, &lpsPropValue) == erSuccess) {
				lpStreamData = new STREAMDATA; // is freed by HrStreamCleanup, called by ECMemStream on refcount == 0
				lpStreamData->ulPropTag = ulPropTag;
				lpStreamData->lpProp = this;

				hr = ECMemStream::Create((char*)lpsPropValue->Value.bin.lpb, lpsPropValue->Value.bin.cb, ulInterfaceOptions,
												NULL, ECMAPIProp::HrStreamCleanup, (void *)lpStreamData, &lpStream);
				if (hr != hrSuccess)
					goto exit;

				lpStream->QueryInterface(IID_IStream, (void **)lppUnk);
				AddChild(lpStream);
				
				lpStream->Release();
				
				goto exit;
			}
			// If this fails, just fallback to the 'normal' way of loading properties.
		}
		
		if (ulFlags & MAPI_MODIFY)
			ulInterfaceOptions |= STGM_WRITE;

		// IStream requested for a property

		ECAllocateBuffer(sizeof(SPropValue), (void **) &lpsPropValue);

		// Yank the property in from disk if it wasn't loaded yet
		HrLoadProp(ulPropTag);

		// For MAPI_CREATE, reset (or create) the property now
		if(ulFlags & MAPI_CREATE) {
			if(!this->fModify) {
				hr = MAPI_E_NO_ACCESS;
				goto exit;
			}
			
			SPropValue sProp;
			sProp.ulPropTag = ulPropTag;
			
			if(PROP_TYPE(ulPropTag) == PT_BINARY) {
				sProp.Value.bin.cb = 0;
				sProp.Value.bin.lpb = NULL;
			} else {
				// Handles lpszA and lpszW since they are the same field in the union
				sProp.Value.lpszW = const_cast<wchar_t *>(L"");
			}
				
			hr = HrSetRealProp(&sProp);
			if(hr != hrSuccess)
				goto exit;
		}
			
		hr = HrGetRealProp(ulPropTag, ulFlags, lpsPropValue, lpsPropValue);

		if(hr != hrSuccess) {
			// Promote warnings from GetProps to error
			hr = MAPI_E_NOT_FOUND;
			goto exit;
		}
		
		lpStreamData = new STREAMDATA; // is freed by HrStreamCleanup, called by ECMemStream on refcount == 0
		lpStreamData->ulPropTag = ulPropTag;
		lpStreamData->lpProp = this;

		if((ulFlags & MAPI_CREATE) == MAPI_CREATE)
		{
			hr = ECMemStream::Create(NULL, 0, ulInterfaceOptions,
									 ECMAPIProp::HrStreamCommit, ECMAPIProp::HrStreamCleanup, (void *)lpStreamData, &lpStream);
		}else {

			switch(PROP_TYPE(lpsPropValue->ulPropTag)) {
			case PT_STRING8:
				hr = ECMemStream::Create(lpsPropValue->Value.lpszA, strlen(lpsPropValue->Value.lpszA), ulInterfaceOptions,
										ECMAPIProp::HrStreamCommit, ECMAPIProp::HrStreamCleanup, (void *)lpStreamData, &lpStream);
				break;
			case PT_UNICODE:
				hr = ECMemStream::Create((char*)lpsPropValue->Value.lpszW, wcslen(lpsPropValue->Value.lpszW)*sizeof(WCHAR), ulInterfaceOptions,
										ECMAPIProp::HrStreamCommit, ECMAPIProp::HrStreamCleanup, (void *)lpStreamData, &lpStream);
				break;
			case PT_BINARY:
				hr = ECMemStream::Create((char *)lpsPropValue->Value.bin.lpb, lpsPropValue->Value.bin.cb, ulInterfaceOptions,
										ECMAPIProp::HrStreamCommit, ECMAPIProp::HrStreamCleanup, (void *)lpStreamData, &lpStream);
				break;
			default:
				ASSERT(false);
				hr = MAPI_E_NOT_FOUND;
				delete lpStreamData;
				break;
			}
		}

		if(hr != hrSuccess)
			goto exit;

		if(*lpiid == IID_IStorage ) {//*lpiid == IID_IStreamDocfile  || 
			//FIXME: Unknown what to do with flag STGSTRM_CURRENT
		
			hr = GetMsgStore()->lpSupport->IStorageFromStream((LPUNKNOWN)&lpStream->m_xStream, NULL, ((ulFlags &MAPI_MODIFY)?STGSTRM_MODIFY : 0) | ((ulFlags & MAPI_CREATE)?STGSTRM_CREATE:0), (LPSTORAGE*)lppUnk);
			if(hr != hrSuccess)
				goto exit;
		}else
			hr = lpStream->QueryInterface(*lpiid, (void **)lppUnk);

		// Release our copy
		lpStream->Release();

		if(hr != hrSuccess)
			goto exit;

		AddChild(lpStream);

	} else {
		hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
	}

exit:	
	if(lpsPropValue)
		ECFreeBuffer(lpsPropValue);


	return hr;
}

HRESULT ECMAPIProp::SaveChanges(ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	WSMAPIPropStorage *lpMAPIPropStorage = NULL;
	
	if (lpStorage == NULL) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	if (!fModify) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}

	// only folders and main messages have a syncid, attachments and msg-in-msg don't
	if (lpStorage->QueryInterface(IID_WSMAPIPropStorage, (void **)&lpMAPIPropStorage) == hrSuccess) {
		hr = lpMAPIPropStorage->HrSetSyncId(m_ulSyncId);
		if(hr != hrSuccess)
			goto exit;
	}

	hr = ECGenericProp::SaveChanges(ulFlags);

exit:
	if(lpMAPIPropStorage)
		lpMAPIPropStorage->Release();

	return hr;
}

HRESULT ECMAPIProp::HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject) {
	// ECMessage implements saving an attachment
	// ECAttach implements saving a sub-message
	return MAPI_E_INVALID_OBJECT;
}

HRESULT ECMAPIProp::GetSerializedACLData(LPVOID lpBase, LPSPropValue lpsPropValue)
{
	HRESULT				hr = hrSuccess;
	ECSecurityPtr		ptrSecurity;
	ULONG				cPerms = 0;
	ECPermissionPtr		ptrPerms;
	struct soap			soap;
	std::ostringstream	os;
	struct rightsArray	rights;
	std::string			strAclData;

	hr = QueryInterface(IID_IECSecurity, &ptrSecurity);
	if (hr != hrSuccess)
		goto exit;

	hr = ptrSecurity->GetPermissionRules(ACCESS_TYPE_GRANT, &cPerms, &ptrPerms);
	if (hr != hrSuccess)
		goto exit;

	rights.__size = cPerms;
	rights.__ptr = s_alloc<struct rights>(&soap, cPerms);
	std::transform(ptrPerms.get(), ptrPerms + cPerms, rights.__ptr, &ECPermToRightsCheap);

	soap_set_omode(&soap, SOAP_C_UTFSTRING);
	soap_begin(&soap);
	soap.os = &os;
	soap_serialize_rightsArray(&soap, &rights);
	soap_begin_send(&soap);
	soap_put_rightsArray(&soap, &rights, "rights", "rightsArray");
	soap_end_send(&soap);

	strAclData = os.str();
	lpsPropValue->Value.bin.cb = strAclData.size();
	hr = MAPIAllocateMore(lpsPropValue->Value.bin.cb, lpBase, (LPVOID*)&lpsPropValue->Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;
	memcpy(lpsPropValue->Value.bin.lpb, strAclData.data(), lpsPropValue->Value.bin.cb);

exit:
	soap_destroy(&soap);
	soap_end(&soap); // clean up allocated temporaries 
	
	return hr;
}

HRESULT ECMAPIProp::SetSerializedACLData(LPSPropValue lpsPropValue)
{
	HRESULT				hr = hrSuccess;
	ECPermissionPtr		ptrPerms;
	struct soap			soap;
	struct rightsArray	rights;
	std::string			strAclData;

	if (lpsPropValue == NULL || PROP_TYPE(lpsPropValue->ulPropTag) != PT_BINARY) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	{
		std::istringstream is(std::string((char*)lpsPropValue->Value.bin.lpb, lpsPropValue->Value.bin.cb));

		soap.is = &is;
		soap_set_imode(&soap, SOAP_C_UTFSTRING);
		soap_begin(&soap);
		if (soap_begin_recv(&soap) != 0) {
			hr = MAPI_E_NETWORK_FAILURE;
			goto exit;
		}
		if (!soap_get_rightsArray(&soap, &rights, "rights", "rightsArray")) {
			hr = MAPI_E_CORRUPT_DATA;
			goto exit;
		}
		if (soap_end_recv(&soap) != 0) {
			hr = MAPI_E_NETWORK_ERROR;
			goto exit;
		}
	}

	hr = MAPIAllocateBuffer(rights.__size * sizeof(ECPERMISSION), &ptrPerms);
	if (hr != hrSuccess)
		goto exit;

	std::transform(rights.__ptr, rights.__ptr + rights.__size, ptrPerms.get(), &RightsToECPermCheap);
	hr = UpdateACLs(rights.__size, ptrPerms);

exit:
	soap_destroy(&soap);
	soap_end(&soap); // clean up allocated temporaries 

	return hr;
}

HRESULT	ECMAPIProp::UpdateACLs(ULONG cNewPerms, ECPERMISSION *lpNewPerms)
{
	HRESULT hr;
	ECSecurityPtr			ptrSecurity;
	ULONG					cPerms = 0;
	ECPermissionArrayPtr	ptrPerms;
	ULONG					cSparePerms = 0;
	ECPermissionPtr			ptrTmpPerms;
	ECPERMISSION *lpPermissions = NULL;

	hr = QueryInterface(IID_IECSecurity, &ptrSecurity);
	if (hr != hrSuccess)
		return hr;

	hr = ptrSecurity->GetPermissionRules(ACCESS_TYPE_GRANT, &cPerms, &ptrPerms);
	if (hr != hrSuccess)
		return hr;

	// Since we want to replace the current ACL with a new one, we need to mark
	// each existing item as deleted, and add all new ones as new.
	// But there can also be overlap, where some items are left unchanged, and
	// other modified.
	for (ULONG i = 0; i < cPerms; ++i) {
		ECPERMISSION *lpMatch = std::find_if(lpNewPerms, lpNewPerms + cNewPerms, FindUser(ptrPerms[i].sUserId));
		if (lpMatch == lpNewPerms + cNewPerms) {
			// Not in new set, so delete
			ptrPerms[i].ulState = RIGHT_DELETED;
		} else {
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
	}

	// Now see if there are still some new ACL's left. If enough spare space is available
	// we'll reuse the ptrPerms storage. If not we'll reallocate the whole array.
	lpPermissions = ptrPerms.get();
	if (cNewPerms > 0) {
		if (cNewPerms <= cSparePerms) {
			memcpy(&ptrPerms[cPerms], lpNewPerms, cNewPerms * sizeof(ECPERMISSION));
		} else if (cPerms == 0) {
			lpPermissions = lpNewPerms;
		} else {
			hr = MAPIAllocateBuffer((cPerms + cNewPerms) * sizeof(ECPERMISSION), &ptrTmpPerms);
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


HRESULT ECMAPIProp::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	return Util::DoCopyTo(&IID_IMAPIProp, &this->m_xMAPIProp, ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

HRESULT ECMAPIProp::CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	return Util::DoCopyProps(&IID_IMAPIProp, &this->m_xMAPIProp, lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
}

// Pass call off to lpMsgStore
HRESULT ECMAPIProp::GetNamesFromIDs(LPSPropTagArray FAR * lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG FAR * lpcPropNames, LPMAPINAMEID FAR * FAR * lpppPropNames)
{
	return this->GetMsgStore()->lpNamedProp->GetNamesFromIDs(lppPropTags, lpPropSetGuid, ulFlags, lpcPropNames, lpppPropNames);
}

HRESULT ECMAPIProp::GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID FAR * lppPropNames, ULONG ulFlags, LPSPropTagArray FAR * lppPropTags)
{
	return this->GetMsgStore()->lpNamedProp->GetIDsFromNames(cPropNames, lppPropNames, ulFlags, lppPropTags);
}

/////////////////////////////////////////////
// Stream functions
//

HRESULT ECMAPIProp::HrStreamCommit(IStream *lpStream, void *lpData)
{
	HRESULT hr = hrSuccess;
	STREAMDATA *lpStreamData = (STREAMDATA *)lpData;
	char *buffer = NULL;
	LPSPropValue lpPropValue = NULL;
	STATSTG sStat;
	ULONG ulSize = 0;
	ECMemStream* lpECStream = NULL;

	hr = ECAllocateBuffer(sizeof(SPropValue), (void **)&lpPropValue);

	if(hr != hrSuccess)
		goto exit;

	hr = lpStream->Stat(&sStat, 0);

	if(hr != hrSuccess)
		goto exit;

	if(PROP_TYPE(lpStreamData->ulPropTag) == PT_STRING8) {
		hr = ECAllocateMore((ULONG)sStat.cbSize.QuadPart+1, lpPropValue, (void **)&buffer);
	
		if(hr != hrSuccess)
			goto exit;

		// read the data into the buffer
		hr = lpStream->Read(buffer, (ULONG)sStat.cbSize.QuadPart, &ulSize);
	} else if(PROP_TYPE(lpStreamData->ulPropTag) == PT_UNICODE) {
		hr = ECAllocateMore((ULONG)sStat.cbSize.QuadPart+sizeof(WCHAR), lpPropValue, (void **)&buffer);
	
		if(hr != hrSuccess)
			goto exit;

		// read the data into the buffer
		hr = lpStream->Read(buffer, (ULONG)sStat.cbSize.QuadPart, &ulSize);
	} else{
		hr = lpStream->QueryInterface(IID_ECMemStream, (void**)&lpECStream);
		if(hr != hrSuccess)
			goto exit;

		ulSize = (ULONG)sStat.cbSize.QuadPart;
		buffer = lpECStream->GetBuffer();
	}

	lpPropValue->ulPropTag = lpStreamData->ulPropTag;

	switch(PROP_TYPE(lpStreamData->ulPropTag)) {
	case PT_STRING8:
		buffer[ulSize] = 0;
		lpPropValue->Value.lpszA = buffer;
		break;
	case PT_UNICODE:
		memset(&buffer[ulSize], 0, sizeof(wchar_t));
		lpPropValue->Value.lpszW = (WCHAR *)buffer;
		break;
	case PT_BINARY:
		lpPropValue->Value.bin.cb = ulSize;
		lpPropValue->Value.bin.lpb = (unsigned char *)buffer;
		break;
	}

	hr = lpStreamData->lpProp->HrSetRealProp(lpPropValue);
	if (hr != hrSuccess)
		goto exit;
	// on a non transacted object SaveChanges is required
	if (!lpStreamData->lpProp->isTransactedObject)
		hr = lpStreamData->lpProp->ECGenericProp::SaveChanges(KEEP_OPEN_READWRITE);

exit:
	if(lpPropValue)
		ECFreeBuffer(lpPropValue);

	if(lpECStream)
		lpECStream->Release();

	return hr;
}

HRESULT ECMAPIProp::HrStreamCleanup(void *lpData)
{
	STREAMDATA *lpStreamData = (STREAMDATA *)lpData;
	delete lpStreamData;
	return hrSuccess;
}

HRESULT ECMAPIProp::HrSetSyncId(ULONG ulSyncId)
{
	HRESULT hr = hrSuccess;

	WSMAPIPropStorage *lpMAPIPropStorage = NULL;
	
	if(lpStorage && lpStorage->QueryInterface(IID_WSMAPIPropStorage, (void **)&lpMAPIPropStorage) ==  hrSuccess){
		hr = lpMAPIPropStorage->HrSetSyncId(ulSyncId);
		if(hr != hrSuccess)
			goto exit;
	}
	m_ulSyncId = ulSyncId;
exit:
	if(lpMAPIPropStorage)
		lpMAPIPropStorage->Release();
	return hr;
}

////////////////////////////////////////////////////////////////
// Security fucntions
//
HRESULT ECMAPIProp::GetPermissionRules(int ulType, ULONG *lpcPermissions,
    ECPERMISSION **lppECPermissions)
{
	if (m_lpEntryId == NULL)
		return MAPI_E_NO_ACCESS;

	return this->GetMsgStore()->lpTransport->HrGetPermissionRules(ulType, m_cbEntryId, m_lpEntryId, lpcPermissions, lppECPermissions);
}

HRESULT ECMAPIProp::SetPermissionRules(ULONG cPermissions,
    ECPERMISSION *lpECPermissions)
{
	if (m_lpEntryId == NULL)
		return MAPI_E_NO_ACCESS;

	return this->GetMsgStore()->lpTransport->HrSetPermissionRules(m_cbEntryId, m_lpEntryId, cPermissions, lpECPermissions);
}

HRESULT ECMAPIProp::GetOwner(ULONG *lpcbOwner, LPENTRYID *lppOwner)
{
	if (lpcbOwner == NULL || lppOwner == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if (m_lpEntryId == NULL)
		return MAPI_E_NO_ACCESS;
	return GetMsgStore()->lpTransport->HrGetOwner(m_cbEntryId, m_lpEntryId, lpcbOwner, lppOwner);
}

HRESULT ECMAPIProp::GetUserList(ULONG cbCompanyId, LPENTRYID lpCompanyId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	return GetMsgStore()->lpTransport->HrGetUserList(cbCompanyId, lpCompanyId, ulFlags, lpcUsers, lppsUsers);
}

HRESULT ECMAPIProp::GetGroupList(ULONG cbCompanyId, LPENTRYID lpCompanyId,
    ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups)
{
	return GetMsgStore()->lpTransport->HrGetGroupList(cbCompanyId, lpCompanyId, ulFlags, lpcGroups, lppsGroups);
}

HRESULT ECMAPIProp::GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies,
    ECCOMPANY **lppsCompanies)
{
	return GetMsgStore()->lpTransport->HrGetCompanyList(ulFlags, lpcCompanies, lppsCompanies);
}

HRESULT ECMAPIProp::SetParentID(ULONG cbParentID, LPENTRYID lpParentID)
{
	HRESULT hr;

	ASSERT(m_lpParentID == NULL);
	if (lpParentID == NULL || cbParentID == 0)
		return MAPI_E_INVALID_PARAMETER;

	hr = MAPIAllocateBuffer(cbParentID, (void**)&m_lpParentID);
	if (hr != hrSuccess)
		return hr;

	m_cbParentID = cbParentID;
	memcpy(m_lpParentID, lpParentID, cbParentID);
	return hrSuccess;
}

////////////////////////////////////////////
// Interface IMAPIProp

HRESULT __stdcall ECMAPIProp::xMAPIProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

ULONG __stdcall ECMAPIProp::xMAPIProp::AddRef()
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::AddRef", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	return pThis->AddRef();
}

ULONG __stdcall ECMAPIProp::xMAPIProp::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::Release", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	ULONG ulRef = pThis->Release();
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::Release", "%d", ulRef);
	return ulRef;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::GetLastError(HRESULT hError, ULONG ulFlags,
    LPMAPIERROR * lppMapiError)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::GetLastError", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->GetLastError(hError, ulFlags, lppMapiError);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::GetLastError", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::SaveChanges(ULONG ulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::SaveChanges", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->SaveChanges(ulFlags);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::SaveChanges", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::GetProps(LPSPropTagArray lpPropTagArray, ULONG ulFlags, ULONG FAR * lpcValues, LPSPropValue FAR * lppPropArray)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::GetProps", "%s, flags=%08X", PropNameFromPropTagArray(lpPropTagArray).c_str(), ulFlags);
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->GetProps(lpPropTagArray, ulFlags, lpcValues, lppPropArray);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::GetProps", "%s\n %s", GetMAPIErrorDescription(hr).c_str(), PropNameFromPropArray(*lpcValues, *lppPropArray).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::GetPropList(ULONG ulFlags, LPSPropTagArray FAR * lppPropTagArray)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::GetPropList", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->GetPropList(ulFlags, lppPropTagArray);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::GetPropList", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN FAR * lppUnk)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::OpenProperty", "proptag=%s, flags=%d, lpiid=%s, InterfaceOptions=%d", PropNameFromPropTag(ulPropTag).c_str(), ulFlags, (lpiid)?DBGGUIDToString(*lpiid).c_str():"NULL", ulInterfaceOptions);
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::OpenProperty", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::SetProps", "%s", PropNameFromPropArray(cValues, lpPropArray).c_str());
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->SetProps(cValues, lpPropArray, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::SetProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::DeleteProps", "%s", PropNameFromPropTagArray(lpPropTagArray).c_str());
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->DeleteProps(lpPropTagArray, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::DeleteProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::CopyTo", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->CopyTo(ciidExclude, rgiidExclude, lpExcludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::CopyTo", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray FAR * lppProblems)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::CopyProps", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->CopyProps(lpIncludeProps, ulUIParam, lpProgress, lpInterface, lpDestObj, ulFlags, lppProblems);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::CopyProps", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::GetNamesFromIDs(LPSPropTagArray * pptaga, LPGUID lpguid, ULONG ulFlags, ULONG * pcNames, LPMAPINAMEID ** pppNames)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::GetNamesFromIDs", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->GetNamesFromIDs(pptaga, lpguid, ulFlags, pcNames, pppNames);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::GetNamesFromIDs", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT __stdcall ECMAPIProp::xMAPIProp::GetIDsFromNames(ULONG cNames, LPMAPINAMEID * ppNames, ULONG ulFlags, LPSPropTagArray * pptaga)
{
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::GetIDsFromNames", "");
	METHOD_PROLOGUE_(ECMAPIProp , MAPIProp);
	HRESULT hr = pThis->GetIDsFromNames(cNames, ppNames, ulFlags, pptaga);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::GetIDsFromNames", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// Interface ECSecurity

HRESULT ECMAPIProp::xECSecurity::QueryInterface(REFIID refiid, void ** lppInterface)
{
	METHOD_PROLOGUE_(ECMAPIProp , ECSecurity);
	return pThis->QueryInterface(refiid, lppInterface);
}

ULONG ECMAPIProp::xECSecurity::AddRef()
{
	METHOD_PROLOGUE_(ECMAPIProp , ECSecurity);
	return pThis->AddRef();
}

ULONG ECMAPIProp::xECSecurity::Release()
{
	METHOD_PROLOGUE_(ECMAPIProp , ECSecurity);
	return pThis->Release();
}

HRESULT ECMAPIProp::xECSecurity::GetOwner(ULONG *lpcbOwner, LPENTRYID *lppOwner)
{
	METHOD_PROLOGUE_(ECMAPIProp , ECSecurity);
	return pThis->GetOwner(lpcbOwner, lppOwner);
}

HRESULT ECMAPIProp::xECSecurity::GetUserList(ULONG cbCompanyId,
    LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lpsUsers)
{
	METHOD_PROLOGUE_(ECMAPIProp, ECSecurity);
	return pThis->GetUserList(cbCompanyId, lpCompanyId, ulFlags, lpcUsers, lpsUsers);
}

HRESULT ECMAPIProp::xECSecurity::GetGroupList(ULONG cbCompanyId,
    LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcGroups,
    ECGROUP **lppsGroups)
{
	METHOD_PROLOGUE_(ECMAPIProp, ECSecurity);
	return pThis->GetGroupList(cbCompanyId, lpCompanyId, ulFlags, lpcGroups, lppsGroups);
}

HRESULT ECMAPIProp::xECSecurity::GetCompanyList(ULONG ulFlags,
    ULONG *lpcCompanies, ECCOMPANY **lppsCompanies)
{
	METHOD_PROLOGUE_(ECMAPIProp, ECSecurity);
	return pThis->GetCompanyList(ulFlags, lpcCompanies, lppsCompanies);
}

HRESULT ECMAPIProp::xECSecurity::GetPermissionRules(int ulType,
    ULONG *lpcPermissions, ECPERMISSION **lppECPermissions)
{
	METHOD_PROLOGUE_(ECMAPIProp, ECSecurity);
	return pThis->GetPermissionRules(ulType, lpcPermissions, lppECPermissions);
}

HRESULT ECMAPIProp::xECSecurity::SetPermissionRules(ULONG cPermissions,
    ECPERMISSION *lpECPermissions)
{
	METHOD_PROLOGUE_(ECMAPIProp, ECSecurity);
	TRACE_MAPI(TRACE_ENTRY, "IMAPIProp::IECSecurity::SetPermissionRules", "%s", PermissionRulesToString(cPermissions, lpECPermissions).c_str());
	HRESULT hr = pThis->SetPermissionRules(cPermissions, lpECPermissions);
	TRACE_MAPI(TRACE_RETURN, "IMAPIProp::IECSecurity::SetPermissionRules", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}
