/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <memory>
#include <kopano/platform.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include <mapidefs.h>
#include "WSTransport.h"
#include "ECGenericProp.h"
#include "kcore.hpp"
#include "pcutil.hpp"
#include "Mem.h"
#include <kopano/Util.h>
#include <kopano/ECGuid.h>
#include <kopano/charset/convert.h>
#include "EntryPoint.h"

using namespace KC;

ECGenericProp::ECGenericProp(void *prov, ULONG type, BOOL mod,
    const char *cls_name) :
	ECUnknown(cls_name), ulObjType(type), fModify(mod), lpProvider(prov)
{
	HrAddPropHandlers(PR_EC_OBJECT, DefaultGetProp, DefaultSetPropComputed, this, false, true);
	HrAddPropHandlers(PR_NULL, DefaultGetProp, DefaultSetPropIgnore, this, false, true);
	HrAddPropHandlers(PR_OBJECT_TYPE, DefaultGetProp, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_ENTRYID, DefaultGetProp, DefaultSetPropComputed, this);
}

HRESULT ECGenericProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IUnknown, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECGenericProp::SetProvider(void *prov)
{
	assert(lpProvider == nullptr);
	lpProvider = prov;
	return hrSuccess;
}

HRESULT ECGenericProp::SetEntryId(ULONG cbEntryId, const ENTRYID *lpEntryId)
{
	assert(m_lpEntryId == NULL);
	return Util::HrCopyEntryId(cbEntryId, lpEntryId, &m_cbEntryId, &~m_lpEntryId);
}

// Add a property handler. Usually called by a subclass
HRESULT ECGenericProp::HrAddPropHandlers(unsigned int ulPropTag,
    GetPropCallBack lpfnGetProp, SetPropCallBack lpfnSetProp,
    ECGenericProp *lpParam, BOOL fRemovable, BOOL fHidden)
{
	PROPCALLBACK			sCallBack;

	// Check if the handler defines the right type, If Unicode you should never define a PT_STRING8 as handler!
	assert(PROP_TYPE(ulPropTag) == PT_STRING8 ||
	       PROP_TYPE(ulPropTag) == PT_UNICODE ?
	       PROP_TYPE(ulPropTag) == PT_TSTRING : true);

	// Only Support properties on ID, different types are not supported.
	auto iterCallBack = lstCallBack.find(PROP_ID(ulPropTag));
	if(iterCallBack != lstCallBack.end())
		lstCallBack.erase(iterCallBack);

	sCallBack.lpfnGetProp = lpfnGetProp;
	sCallBack.lpfnSetProp = lpfnSetProp;
	sCallBack.ulPropTag = ulPropTag;
	sCallBack.lpParam = lpParam;
	sCallBack.fRemovable = fRemovable;
	sCallBack.fHidden = fHidden;
	lstCallBack.emplace(PROP_ID(ulPropTag), sCallBack);
	return hrSuccess;
}

// sets an actual value in memory
HRESULT ECGenericProp::HrSetRealProp(const SPropValue *lpsPropValue)
{
	ULONG ulPropId = 0;

	//FIXME: check the property structure -> lpsPropValue

	if (m_bLoading == FALSE && m_sMapiObject) {
		// Only reset instance id when we're being modified, not being reloaded
		HrSIEntryIDToID(m_sMapiObject->cbInstanceID, m_sMapiObject->lpInstanceID, nullptr, nullptr, &ulPropId);
		if (ulPropId == PROP_ID(lpsPropValue->ulPropTag))
			SetSingleInstanceId(0, NULL);
	}
	if (!m_props_loaded) {
		auto hr = HrLoadProps();
		if(hr != hrSuccess)
			return hr;
	}

	auto iterPropsFound = lstProps.end();
	// Loop through all properties, get the first EXACT matching property, but delete ALL
	// other properties with this PROP_ID and the wrong type - this makes sure you can SetProps() with 0x60010003,
	// then with 0x60010102 and then with 0x60010040 for example.
	auto iterProps = lstProps.find(PROP_ID(lpsPropValue->ulPropTag));
	if (iterProps != lstProps.end()) {
		if (iterProps->second.GetPropTag() != lpsPropValue->ulPropTag) {
			// type is different, remove the property and insert a new item
			m_setDeletedProps.emplace(lpsPropValue->ulPropTag);
			lstProps.erase(iterProps);
		} else {
			iterPropsFound = iterProps;
		}
	}

	// Changing an existing property
	if (iterPropsFound != lstProps.end()) {
		iterPropsFound->second.HrSetProp(lpsPropValue);
	} else { // Add new property
		auto lpProperty = std::make_unique<ECProperty>(lpsPropValue);
		if (lpProperty->GetLastError() != 0)
			return lpProperty->GetLastError();

		lstProps.emplace(PROP_ID(lpsPropValue->ulPropTag), ECPropertyEntry(std::move(lpProperty)));
	}

	// Property is now added/modified and marked 'dirty' for saving
	return hrSuccess;
}

// Get an actual value from the property array and saves it to the given address. Any extra memory required
// is allocated with MAPIAllocMore with lpBase as the base pointer
//
// Properties are always returned unless:
//
// 1) The underlying system didn't return them in the initial HrReadProps call, and as such are 'too large' and
//    the property has not been force-loaded with OpenProperty
// or
// 2) A MaxSize was specified and the property is larger than that size (normally 8k or so)

HRESULT ECGenericProp::HrGetRealProp(ULONG ulPropTag, ULONG ulFlags, void *lpBase, LPSPropValue lpsPropValue, ULONG ulMaxSize)
{
	if (!m_props_loaded || m_bReload) {
		auto hr = HrLoadProps();
		if(hr != hrSuccess)
			return hr;
		m_bReload = FALSE;
	}

	// Find the property in our list
	auto iterProps = lstProps.find(PROP_ID(ulPropTag));

	// Not found or property is not matching
	if (iterProps == lstProps.end() ||
	    !(PROP_TYPE(ulPropTag) == PT_UNSPECIFIED ||
	    PROP_TYPE(ulPropTag) == PROP_TYPE(iterProps->second.GetPropTag()) ||
	    (((ulPropTag & MV_FLAG) == (iterProps->second.GetPropTag() & MV_FLAG)) &&
	    PROP_TYPE(ulPropTag & ~MV_FLAG) == PT_STRING8 &&
	    PROP_TYPE(iterProps->second.GetPropTag() & ~MV_FLAG) == PT_UNICODE)))
	{
		lpsPropValue->ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_ERROR);
		lpsPropValue->Value.err = MAPI_E_NOT_FOUND;
		return MAPI_W_ERRORS_RETURNED;
	}
	if(!iterProps->second.FIsLoaded()) {
		lpsPropValue->ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_ERROR);
		lpsPropValue->Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
		return MAPI_W_ERRORS_RETURNED;
		// The load should have loaded into the value pointed to by iterProps, so we can use that now
	}

	// Check if a max. size was requested, if so, dont return unless smaller than max. size
	if (ulMaxSize != 0 && iterProps->second.GetProperty()->GetSize() > ulMaxSize) {
		lpsPropValue->ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_ERROR);
		lpsPropValue->Value.err = MAPI_E_NOT_ENOUGH_MEMORY;
		return MAPI_W_ERRORS_RETURNED;
	}
	if (PROP_TYPE(ulPropTag) == PT_UNSPECIFIED) {
		if (PROP_TYPE(iterProps->second.GetPropTag()) == PT_UNICODE)
			ulPropTag = CHANGE_PROP_TYPE(ulPropTag, ((ulFlags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8));
		else if (PROP_TYPE(iterProps->second.GetPropTag()) == PT_MV_UNICODE)
			ulPropTag = CHANGE_PROP_TYPE(ulPropTag, ((ulFlags & MAPI_UNICODE) ? PT_MV_UNICODE : PT_MV_STRING8));
		else
			ulPropTag = iterProps->second.GetPropTag();
	}

	// Copy the property to its final destination, with base pointer for extra allocations, if required.
	iterProps->second.GetProperty()->CopyTo(lpsPropValue, lpBase, ulPropTag);
	return hrSuccess;
}

/**
 * Deletes a property from the internal system
 *
 * @param ulPropTag The requested ulPropTag to remove. Property type is ignored; only the property identifier is used.
 * @param fOverwriteRO Set to TRUE if the object is to be modified eventhough it's read-only. Currently unused! @todo parameter should be removed.
 *
 * @return MAPI error code
 * @retval hrSuccess PropTag is set to be removed during the next SaveChanges call
 * @retval MAPI_E_NOT_FOUND The PropTag is not found in the list (can be type mismatch too)
 */
HRESULT ECGenericProp::HrDeleteRealProp(ULONG ulPropTag, BOOL fOverwriteRO)
{
	if (!m_props_loaded) {
		auto hr = HrLoadProps();
		if(hr != hrSuccess)
			return hr;
	}

	// Now find the real value
	auto iterProps = lstProps.find(PROP_ID(ulPropTag));
	if (iterProps == lstProps.end())
		// Couldn't find it!
		return MAPI_E_NOT_FOUND;
	m_setDeletedProps.emplace(iterProps->second.GetPropTag());
	lstProps.erase(iterProps);
	return hrSuccess;
}

// Default property handles
HRESULT ECGenericProp::DefaultGetProp(unsigned int ulPropTag, void *lpProvider,
    unsigned int ulFlags, SPropValue *lpsPropValue, ECGenericProp *lpProp,
    void *lpBase)
{
	switch(PROP_ID(ulPropTag))
	{
	case PROP_ID(PR_ENTRYID): {
		if (lpProp->m_cbEntryId == 0)
			return MAPI_E_NOT_FOUND;
		lpsPropValue->ulPropTag = PR_ENTRYID;
		lpsPropValue->Value.bin.cb = lpProp->m_cbEntryId;
		if (lpBase == NULL)
			assert(false);
		auto hr = ECAllocateMore(lpProp->m_cbEntryId, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
		if (hr != hrSuccess)
			return hr;
		memcpy(lpsPropValue->Value.bin.lpb, lpProp->m_lpEntryId, lpProp->m_cbEntryId);
		break;
	}
	// Gives access to the actual ECUnknown underlying object
	case PROP_ID(PR_EC_OBJECT):
		/*
		 * NOTE: we place the object pointer in lpszA to make sure it
		 * is on the same offset as Value.x on 32-bit as 64-bit
		 * machines.
		 */
		lpsPropValue->ulPropTag = PR_EC_OBJECT;
		lpsPropValue->Value.lpszA = reinterpret_cast<char *>(static_cast<IUnknown *>(lpProp));
		break;
	case PROP_ID(PR_NULL):
		// outlook with export contacts to csv (IMessage)(0x00000000) <- skip this one
		// Palm used PR_NULL (IMAPIFolder)(0x00000001)
		if (ulPropTag != PR_NULL)
			return MAPI_E_NOT_FOUND;
		lpsPropValue->ulPropTag = PR_NULL;
		memset(&lpsPropValue->Value, 0, sizeof(lpsPropValue->Value)); // make sure all bits, 32 or 64, are 0
		break;
	case PROP_ID(PR_OBJECT_TYPE):
		lpsPropValue->Value.l = lpProp->ulObjType;
		lpsPropValue->ulPropTag = PR_OBJECT_TYPE;
		break;
	default:
		return lpProp->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
	}

	return hrSuccess;
}

HRESULT ECGenericProp::DefaultGetPropGetReal(unsigned int ulPropTag,
    void *lpProvider, unsigned int ulFlags, SPropValue *lpsPropValue,
    ECGenericProp *lpProp, void *lpBase)
{
	return lpProp->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue, lpProp->m_ulMaxPropSize);
}

HRESULT ECGenericProp::DefaultSetPropSetReal(unsigned int ulPropTag,
    void *lpProvider, const SPropValue *lpsPropValue, ECGenericProp *lpParam)
{
	return lpParam->HrSetRealProp(lpsPropValue);
}

HRESULT ECGenericProp::DefaultSetPropComputed(unsigned int tag, void *provider,
    const SPropValue *, ECGenericProp *)
{
	return MAPI_E_COMPUTED;
}

HRESULT ECGenericProp::DefaultSetPropIgnore(unsigned int tag, void *provider,
    const SPropValue *, ECGenericProp *)
{
	return hrSuccess;
}

HRESULT ECGenericProp::TableRowGetProp(void *lpProvider,
    const struct propVal *lpsPropValSrc, SPropValue *lpsPropValDst,
    void **lpBase, ULONG ulType)
{
	switch(lpsPropValSrc->ulPropTag) {
	case CHANGE_PROP_TYPE(PR_NULL, PT_ERROR):
		lpsPropValDst->Value.l = 0;
		lpsPropValDst->ulPropTag = PR_NULL;
		break;
	default:
		return MAPI_E_NOT_FOUND;
	}
	return hrSuccess;
}

// Sets all the properties 'clean', i.e. un-dirty
HRESULT ECGenericProp::HrSetClean()
{
	// also remove deleted marked properties, since the object isn't reloaded from the server anymore
	for (auto iterProps = lstProps.begin(); iterProps != lstProps.end(); ++iterProps)
		iterProps->second.HrSetClean();
	m_setDeletedProps.clear();
	return hrSuccess;
}

HRESULT ECGenericProp::HrSaveChild(ULONG ulFlags, MAPIOBJECT *lpsMapiObject)
{
	// ECMessage implements saving an attachment
	// ECAttach implements saving a sub-message
	return MAPI_E_INVALID_OBJECT;
}

HRESULT ECGenericProp::HrRemoveModifications(MAPIOBJECT *lpsMapiObject, ULONG ulPropTag)
{
	lpsMapiObject->lstDeleted.remove(ulPropTag);
	for (auto iterProps = lpsMapiObject->lstModified.begin();
	     iterProps != lpsMapiObject->lstModified.end(); ++iterProps)
		if(iterProps->GetPropTag() == ulPropTag) {
			lpsMapiObject->lstModified.erase(iterProps);
			break;
		}
	return hrSuccess;
}

HRESULT ECGenericProp::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	ecmem_ptr<MAPIERROR> lpMapiError;
	memory_ptr<TCHAR> lpszErrorMsg;

	auto hr = Util::HrMAPIErrorToText((hResult == hrSuccess)?MAPI_E_NO_ACCESS : hResult, &~lpszErrorMsg);
	if (hr != hrSuccess)
		return hr;
	hr = ECAllocateBuffer(sizeof(MAPIERROR), &~lpMapiError);
	if(hr != hrSuccess)
		return hr;

	if (ulFlags & MAPI_UNICODE) {
		std::wstring wstrErrorMsg = convert_to<std::wstring>(lpszErrorMsg.get());
		std::wstring wstrCompName = convert_to<std::wstring>(g_strProductName.c_str());

		hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (wstrErrorMsg.size() + 1),
		     lpMapiError, reinterpret_cast<void **>(&lpMapiError->lpszError));
		if (hr != hrSuccess)
			return hr;
		wcscpy((wchar_t*)lpMapiError->lpszError, wstrErrorMsg.c_str());
		hr = MAPIAllocateMore(sizeof(std::wstring::value_type) * (wstrCompName.size() + 1),
		     lpMapiError, reinterpret_cast<void **>(&lpMapiError->lpszComponent));
		if (hr != hrSuccess)
			return hr;
		wcscpy((wchar_t *)lpMapiError->lpszComponent, wstrCompName.c_str());
	} else {
		std::string strErrorMsg = convert_to<std::string>(lpszErrorMsg.get());
		std::string strCompName = convert_to<std::string>(g_strProductName.c_str());

		hr = MAPIAllocateMore(strErrorMsg.size() + 1, lpMapiError,
		     reinterpret_cast<void **>(&lpMapiError->lpszError));
		if (hr != hrSuccess)
			return hr;
		strcpy((char*)lpMapiError->lpszError, strErrorMsg.c_str());

		hr = MAPIAllocateMore(strCompName.size() + 1, lpMapiError,
		     reinterpret_cast<void **>(&lpMapiError->lpszComponent));
		if (hr != hrSuccess)
			return hr;
		strcpy((char*)lpMapiError->lpszComponent, strCompName.c_str());
	}

	lpMapiError->ulContext		= 0;
	lpMapiError->ulLowLevelError= 0;
	lpMapiError->ulVersion		= 0;
	*lppMAPIError = lpMapiError.release();
	return hrSuccess;
}

// Differential save of changed properties
HRESULT ECGenericProp::SaveChanges(ULONG ulFlags)
{
	HRESULT			hr = hrSuccess;
	scoped_rlock l_obj(m_hMutexMAPIObject);

	if (!fModify)
		return MAPI_E_NO_ACCESS;
	if (m_sMapiObject == nullptr || !m_props_loaded)
		return MAPI_E_CALL_FAILED;
	// no props -> succeed (no changes made)
	if (lstProps.empty())
		goto exit;
	if (lpStorage == nullptr)
		// no way to save our properties !
		return MAPI_E_NO_ACCESS;

	// Note: m_sMapiObject->lstProperties and m_sMapiObject->lstAvailable are empty
	// here, because they are cleared after HrLoadProps and SaveChanges
	// save into m_sMapiObject

	for (auto l : m_setDeletedProps) {
		// Make sure the property is not present in deleted/modified list
		HrRemoveModifications(m_sMapiObject.get(), l);
		m_sMapiObject->lstDeleted.emplace_back(l);
	}

	for (auto &p : lstProps) {
		// Property is dirty, so we have to save it
		if (p.second.FIsDirty()) {
			// Save in the 'modified' list
			// Make sure the property is not present in deleted/modified list
			HrRemoveModifications(m_sMapiObject.get(), p.second.GetPropTag());
			// Save modified property
			m_sMapiObject->lstModified.emplace_back(*p.second.GetProperty());
			// Save in the normal properties list
			m_sMapiObject->lstProperties.emplace_back(*p.second.GetProperty());
			continue;
		}

		// Normal property: either non-loaded or loaded
		if (!p.second.FIsLoaded())	// skip pt_error anyway
			m_sMapiObject->lstAvailable.emplace_back(p.second.GetPropTag());
		else
			m_sMapiObject->lstProperties.emplace_back(*p.second.GetProperty());
	}

	m_sMapiObject->bChanged = true;

	// Our s_MapiObject now contains its full property list in lstProperties and lstAvailable,
	// and its modifications in lstModified and lstDeleted.
	// save to parent or server
	hr = lpStorage->HrSaveObject(ulObjFlags, m_sMapiObject.get());
	if (hr != hrSuccess)
		return hr;

	// HrSaveObject() has appended any new properties in lstAvailable and lstProperties. We need to load the
	// new properties. The easiest way to do this is to simply load all properties. Note that in embedded objects
	// that save to ECParentStorage, the object will be untouched. The code below will do nothing.
	// Large properties received
	for (auto tag : m_sMapiObject->lstAvailable) {
		// ONLY if not present
		auto ip = lstProps.find(PROP_ID(tag));
		if (ip == lstProps.cend() || ip->second.GetPropTag() != tag)
			lstProps.emplace(PROP_ID(tag), ECPropertyEntry(tag));
	}
	m_sMapiObject->lstAvailable.clear();

	// Normal properties with value
	for (const auto &pv : m_sMapiObject->lstProperties)
		// don't add any 'error' types ... (the storage object shouldn't really give us these anyway ..)
		if (PROP_TYPE(pv.GetPropTag()) != PT_ERROR) {
			SPropValue tmp = pv.GetMAPIPropValRef();
			HrSetRealProp(&tmp);
		}

	// Note that we currently don't support the server removing properties after the SaveObject call
	// We have loaded all properties, so clear the properties in the m_sMapiObject
	m_sMapiObject->lstProperties.clear();
	m_sMapiObject->lstAvailable.clear();

	// We are now in sync with the server again, so set everything as clean
	HrSetClean();
	fSaved = true;
exit:
	if (hr == hrSuccess)
		// Unless the user requests to continue with modify access, switch
		// down to read-only access. This means that specifying neither of
		// the KEEP_OPEN flags means the same thing as KEEP_OPEN_READONLY.
		if (!(ulFlags & (KEEP_OPEN_READWRITE|FORCE_SAVE)))
			fModify = FALSE;
	return hr;
}

// Check if property is dirty (delete properties gives MAPI_E_NOT_FOUND)
HRESULT ECGenericProp::IsPropDirty(ULONG ulPropTag, BOOL *lpbDirty)
{
	auto iterProps = lstProps.find(PROP_ID(ulPropTag));
	if (iterProps == lstProps.end() ||
	    (PROP_TYPE(ulPropTag) != PT_UNSPECIFIED &&
	    ulPropTag != iterProps->second.GetPropTag()))
		return MAPI_E_NOT_FOUND;

	*lpbDirty = iterProps->second.FIsDirty();
	return hrSuccess;
}

/**
 * Clears the 'dirty' flag for a property
 *
 * This means that during a save the property will not be sent to the server. The dirty flag will be set
 * again after a HrSetRealProp() again.
 *
 * @param[in] ulPropTag Property to mark un-dirty
 * @result HRESULT
 */
HRESULT ECGenericProp::HrSetCleanProperty(ULONG ulPropTag)
{
	auto iterProps = lstProps.find(PROP_ID(ulPropTag));
	if (iterProps == lstProps.end() ||
	    (PROP_TYPE(ulPropTag) != PT_UNSPECIFIED &&
	    ulPropTag != iterProps->second.GetPropTag()))
		return MAPI_E_NOT_FOUND;

	iterProps->second.HrSetClean();
	return hrSuccess;
}

// Get the handler(s) for a given property tag
HRESULT ECGenericProp::HrGetHandler(unsigned int ulPropTag,
    SetPropCallBack *lpfnSetProp, GetPropCallBack *lpfnGetProp,
    ECGenericProp **lpParam)
{
	ECPropCallBackIterator iterCallBack;

	iterCallBack = lstCallBack.find(PROP_ID(ulPropTag));
	if (iterCallBack == lstCallBack.end() ||
		(ulPropTag != iterCallBack->second.ulPropTag && PROP_TYPE(ulPropTag) != PT_UNSPECIFIED &&
		!(PROP_TYPE(iterCallBack->second.ulPropTag) == PT_TSTRING && (PROP_TYPE(ulPropTag) == PT_STRING8 || PROP_TYPE(ulPropTag) == PT_UNICODE) )
		) )
		return MAPI_E_NOT_FOUND;

	if(lpfnSetProp)
		*lpfnSetProp = iterCallBack->second.lpfnSetProp;
	if(lpfnGetProp)
		*lpfnGetProp = iterCallBack->second.lpfnGetProp;
	if(lpParam)
		*lpParam = iterCallBack->second.lpParam;
	return hrSuccess;
}

HRESULT ECGenericProp::HrSetPropStorage(IECPropStorage *storage, BOOL fLoadProps)
{
	SPropValue sPropValue;

	lpStorage.reset(storage);
	if (!fLoadProps)
		return hrSuccess;
	auto hr = HrLoadProps();
	if(hr != hrSuccess)
		return hr;
	if (HrGetRealProp(PR_OBJECT_TYPE, 0, NULL, &sPropValue, m_ulMaxPropSize) == hrSuccess &&
	    // The server sent a PR_OBJECT_TYPE, check if it is correct
	    ulObjType != sPropValue.Value.ul)
		// Return NOT FOUND because the entryid given was the incorrect type. This means
		// that the object was basically not found.
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

HRESULT ECGenericProp::HrLoadEmptyProps()
{
	scoped_rlock lock(m_hMutexMAPIObject);
	assert(!m_props_loaded);
	assert(m_sMapiObject == NULL);
	lstProps.clear(); /* release build has no asserts */
	m_props_loaded = true;
	m_sMapiObject.reset(new MAPIOBJECT(0, 0, ulObjType));
	return hrSuccess;
}

// Loads the properties of the saved message for use
HRESULT ECGenericProp::HrLoadProps()
{
	HRESULT			hr = hrSuccess;

	if(lpStorage == NULL)
		return MAPI_E_CALL_FAILED;

	scoped_rlock lock(m_hMutexMAPIObject);
	if (m_props_loaded && !m_bReload)
		goto exit; // already loaded

	m_bLoading = TRUE;

	if (m_sMapiObject != NULL) {
		// remove what we know, (scenario: keep open r/w, drop props, get all again causes to know the server changes, incl. the hierarchy id)
		m_sMapiObject.reset();
		// only remove my own properties: keep recipients and attachment tables
		lstProps.clear();
		m_setDeletedProps.clear();
	}
	hr = lpStorage->HrLoadObject(&unique_tie(m_sMapiObject));
	if (hr != hrSuccess)
		goto exit;
	m_props_loaded = true;

	// Add *all* the entries as with empty values; values for these properties will be
	// retrieved on-demand
	for (auto tag : m_sMapiObject->lstAvailable)
		lstProps.emplace(PROP_ID(tag), ECPropertyEntry(tag));

	// Load properties
	for (const auto &pv : m_sMapiObject->lstProperties)
		// don't add any 'error' types ... (the storage object shouldn't really give us these anyway ..)
		if (PROP_TYPE(pv.GetPropTag()) != PT_ERROR) {
			SPropValue tmp = pv.GetMAPIPropValRef();
			HrSetRealProp(&tmp);
		}

	// remove copied proptags, subobjects are still present
	m_sMapiObject->lstAvailable.clear();
	m_sMapiObject->lstProperties.clear(); // pointers are now only present in lstProps (this removes memory usage!)

	// at this point: children still known, ulObjId and ulObjType too
	// Mark all properties now in memory as 'clean' (need not be saved)
	hr = HrSetClean();
	if(hr != hrSuccess)
		goto exit;
	// We just read the properties from the disk, so it is a 'saved' (ie on-disk) message
	fSaved = true;
exit:
	m_bReload = FALSE;
	m_bLoading = FALSE;
	return hr;
}

// Load a single (large) property from the storage
HRESULT ECGenericProp::HrLoadProp(ULONG ulPropTag)
{
	ecmem_ptr<SPropValue> lpsPropVal;

	if(lpStorage == NULL)
		return MAPI_E_CALL_FAILED;

	ulPropTag = NormalizePropTag(ulPropTag);

	scoped_rlock lock(m_hMutexMAPIObject);
	if (!m_props_loaded || m_bReload) {
		auto hr = HrLoadProps();
		if(hr != hrSuccess)
			return hr;
	}
	auto iterProps = lstProps.find(PROP_ID(ulPropTag));
	if (iterProps == lstProps.end() ||
	    (PROP_TYPE(ulPropTag) != PT_UNSPECIFIED &&
	    PROP_TYPE(ulPropTag) != PROP_TYPE(iterProps->second.GetPropTag())))
		return MAPI_E_NOT_FOUND;
	// Don't load the data if it was already loaded
	if (iterProps->second.FIsLoaded())
		return MAPI_E_NOT_FOUND;

  	// The property was not loaded yet, demand-load it now
	auto hr = lpStorage->HrLoadProp(m_sMapiObject->ulObjId, iterProps->second.GetPropTag(), &~lpsPropVal);
	if(hr != hrSuccess)
		return hr;
	hr = iterProps->second.HrSetProp(new ECProperty(lpsPropVal));
	if(hr != hrSuccess)
		return hr;
	// It's clean 'cause we just loaded it
	iterProps->second.HrSetClean();
	return hrSuccess;
}

HRESULT ECGenericProp::GetProps(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags, ULONG *lpcValues, SPropValue **lppPropArray)
{
	/* FIXME: check lpPropTagArray on PROP_TYPE() */
	if ((lpPropTagArray != nullptr && lpPropTagArray->cValues == 0) ||
	    !Util::ValidatePropTagArray(lpPropTagArray))
		return MAPI_E_INVALID_PARAMETER;

	HRESULT			hrT = hrSuccess;
	ecmem_ptr<SPropTagArray> lpGetPropTagArray;
	GetPropCallBack	lpfnGetProp = NULL;
	ecmem_ptr<SPropValue> lpsPropValue;

	if (lpPropTagArray == NULL) {
		auto hr = GetPropList(ulFlags, &~lpGetPropTagArray);
		if(hr != hrSuccess)
			return hr;
		lpPropTagArray = lpGetPropTagArray.get();
	}
	auto hr = ECAllocateBuffer(sizeof(SPropValue) * lpPropTagArray->cValues, &~lpsPropValue);
	if (hr != hrSuccess)
		return hr;

	for (unsigned int i = 0; i < lpPropTagArray->cValues; ++i) {
		ECGenericProp *lpParam = nullptr;
		if (HrGetHandler(lpPropTagArray->aulPropTag[i], NULL, &lpfnGetProp, &lpParam) == hrSuccess) {
			lpsPropValue[i].ulPropTag = lpPropTagArray->aulPropTag[i];
			hrT = lpfnGetProp(lpPropTagArray->aulPropTag[i], lpProvider, ulFlags, &lpsPropValue[i], lpParam, lpsPropValue);
		} else {
			hrT = HrGetRealProp(lpPropTagArray->aulPropTag[i], ulFlags, lpsPropValue, &lpsPropValue[i], m_ulMaxPropSize);
			if (hrT != hrSuccess && hrT != MAPI_E_NOT_FOUND &&
			    hrT != MAPI_E_NOT_ENOUGH_MEMORY &&
			    hrT != MAPI_W_ERRORS_RETURNED)
				return hrT;
		}

		if(HR_FAILED(hrT)) {
			lpsPropValue[i].ulPropTag = CHANGE_PROP_TYPE(lpPropTagArray->aulPropTag[i], PT_ERROR);
			lpsPropValue[i].Value.err = hrT;
			hr = MAPI_W_ERRORS_RETURNED;
		} else if(hrT != hrSuccess) {
			hr = MAPI_W_ERRORS_RETURNED;
		}
	}

	*lppPropArray = lpsPropValue.release();
	*lpcValues = lpPropTagArray->cValues;
	return hr;
}

HRESULT ECGenericProp::GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray)
{
	ecmem_ptr<SPropTagArray> lpPropTagArray;
	int					n = 0;

	if (!m_props_loaded) {
		auto hr = HrLoadProps();
		if(hr != hrSuccess)
			return hr;
	}

	// The size of the property tag array is never larger than (static properties + generated properties)
	auto hr = ECAllocateBuffer(CbNewSPropTagArray(lstProps.size() + lstCallBack.size()),
	          &~lpPropTagArray);
	if (hr != hrSuccess)
		return hr;

	// Some will overlap so we've actually allocated slightly too much memory
	// Add the callback types first
	for (auto iterCallBack = lstCallBack.begin();
	     iterCallBack != lstCallBack.end(); ++iterCallBack) {
		// Don't add 'hidden' properties
		if(iterCallBack->second.fHidden)
			continue;

		// Check if the callback actually returns OK
		// a bit wasteful but fine for now.
		ecmem_ptr<SPropValue> lpsPropValue;
		HRESULT hrT = hrSuccess;

		hr = ECAllocateBuffer(sizeof(SPropValue), &~lpsPropValue);
		if (hr != hrSuccess)
			return hr;
		hrT = iterCallBack->second.lpfnGetProp(iterCallBack->second.ulPropTag,
		      lpProvider, ulFlags, lpsPropValue, this, lpsPropValue);
		if (HR_FAILED(hrT) && hrT != MAPI_E_NOT_ENOUGH_MEMORY)
			continue;
		if (PROP_TYPE(lpsPropValue->ulPropTag) == PT_ERROR &&
		    lpsPropValue->Value.err != MAPI_E_NOT_ENOUGH_MEMORY)
			continue;

		ULONG ulPropTag = iterCallBack->second.ulPropTag;
		if (PROP_TYPE(ulPropTag) == PT_UNICODE || PROP_TYPE(ulPropTag) == PT_STRING8)
			ulPropTag = CHANGE_PROP_TYPE(ulPropTag, ((ulFlags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8));
		lpPropTagArray->aulPropTag[n++] = ulPropTag;
	}

	// Then add the others, if not added yet
	for (auto iterProps = lstProps.begin(); iterProps != lstProps.end(); ++iterProps) {
		if (HrGetHandler(iterProps->second.GetPropTag(), nullptr, nullptr, nullptr) == 0)
			continue;
		ULONG ulPropTag = iterProps->second.GetPropTag();
		if (!(ulFlags & MAPI_UNICODE)) {
			// Downgrade to ansi
			if(PROP_TYPE(ulPropTag) == PT_UNICODE)
				ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_STRING8);
			else if(PROP_TYPE(ulPropTag) == PT_MV_UNICODE)
				ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_MV_STRING8);
		}
		lpPropTagArray->aulPropTag[n++] = ulPropTag;
	}

	lpPropTagArray->cValues = n;
	*lppPropTagArray = lpPropTagArray.release();
	return hrSuccess;
}

HRESULT ECGenericProp::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	return  MAPI_E_NO_SUPPORT;
}

HRESULT ECGenericProp::SetProps(ULONG cValues, const SPropValue *lpPropArray,
    SPropProblemArray **lppProblems)
{
	if (lpPropArray == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT				hrT = hrSuccess;
	ecmem_ptr<SPropProblemArray> lpProblems;
	int					nProblem = 0;
	SetPropCallBack		lpfnSetProp = NULL;
	auto hr = ECAllocateBuffer(CbNewSPropProblemArray(cValues), &~lpProblems);
	if(hr != hrSuccess)
		return hr;

	for (unsigned int i = 0; i < cValues; ++i) {
		// Ignore the PR_NULL property tag and all properties with a type of PT_ERROR;
		// no changes and report no problems in the SPropProblemArray structure.
		if(PROP_TYPE(lpPropArray[i].ulPropTag) == PR_NULL ||
			PROP_TYPE(lpPropArray[i].ulPropTag) == PT_ERROR)
			continue;

		ECGenericProp *lpParam = nullptr;
		if (HrGetHandler(lpPropArray[i].ulPropTag, &lpfnSetProp, NULL, &lpParam) == hrSuccess)
			hrT = lpfnSetProp(lpPropArray[i].ulPropTag, lpProvider, &lpPropArray[i], lpParam);
		else
			hrT = HrSetRealProp(&lpPropArray[i]); // SC: TODO: this does a ref copy ?!

		if(hrT != hrSuccess) {
			lpProblems->aProblem[nProblem].scode = hrT;
			lpProblems->aProblem[nProblem].ulIndex = i;
			lpProblems->aProblem[nProblem].ulPropTag = lpPropArray[i].ulPropTag; // Hold here the real property
			++nProblem;
		}
	}

	lpProblems->cProblem = nProblem;
	if (lppProblems != nullptr && nProblem != 0)
		*lppProblems = lpProblems.release();
	else if (lppProblems != nullptr)
		*lppProblems = NULL;
	return hrSuccess;
}

/**
 * Delete properties from the current object
 *
 * @param lpPropTagArray PropTagArray with properties to remove from the object. If a property from this list is not found, it will be added to the lppProblems array.
 * @param lppProblems An array of proptags that could not be removed from the object. Returns NULL if everything requested was removed. Can be NULL not to want the problem array.
 *
 * @remark Property types are ignored; only the property identifiers are used.
 *
 * @return MAPI error code
 * @retval hrSuccess 0 or more properties are set to be removed on SaveChanges
 * @retval MAPI_E_NO_ACCESS the object is read-only
 * @retval MAPI_E_NOT_ENOUGH_MEMORY unable to allocate memory to remove the problem array
 */
HRESULT ECGenericProp::DeleteProps(const SPropTagArray *lpPropTagArray,
    SPropProblemArray **lppProblems)
{
	if (lpPropTagArray == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ecmem_ptr<SPropProblemArray> lpProblems;
	int						nProblem = 0;
	// over-allocate the problem array
	auto er = ECAllocateBuffer(CbNewSPropProblemArray(lpPropTagArray->cValues), &~lpProblems);
	if (er != erSuccess)
		return MAPI_E_NOT_ENOUGH_MEMORY;

	for (unsigned int i = 0; i < lpPropTagArray->cValues; ++i) {
		// See if it's computed
		auto iterCallBack = lstCallBack.find(PROP_ID(lpPropTagArray->aulPropTag[i]));

		// Ignore removable callbacks
		if(iterCallBack != lstCallBack.end() && !iterCallBack->second.fRemovable) {
			// This is a computed value
			lpProblems->aProblem[nProblem].scode = MAPI_E_COMPUTED;
			lpProblems->aProblem[nProblem].ulIndex = i;
			lpProblems->aProblem[nProblem].ulPropTag = lpPropTagArray->aulPropTag[i];
			++nProblem;
			continue;
		}
		auto hrT = HrDeleteRealProp(lpPropTagArray->aulPropTag[i],FALSE);
		if (hrT == hrSuccess)
			continue;
		// Add the error
		lpProblems->aProblem[nProblem].scode = hrT;
		lpProblems->aProblem[nProblem].ulIndex = i;
		lpProblems->aProblem[nProblem].ulPropTag = lpPropTagArray->aulPropTag[i];
		++nProblem;
	}

	lpProblems->cProblem = nProblem;
	if(lppProblems && nProblem)
		*lppProblems = lpProblems.release();
	else if (lppProblems != nullptr)
		*lppProblems = NULL;
	return hrSuccess;
}

HRESULT ECGenericProp::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *lpExcludeProps, ULONG ulUIParam,
    LPMAPIPROGRESS lpProgress, LPCIID lpInterface, void *lpDestObj,
    ULONG ulFlags, SPropProblemArray **lppProblems)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECGenericProp::CopyProps(const SPropTagArray *, ULONG ui_param,
    IMAPIProgress *, const IID *iface, void *dest_obj, ULONG flags,
    SPropProblemArray **)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECGenericProp::GetNamesFromIDs(SPropTagArray **tags, const GUID *propset,
    ULONG flags, ULONG *nvals, MAPINAMEID ***names)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECGenericProp::GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags)
{
	return MAPI_E_NO_SUPPORT;
}

// Interface IECSingleInstance
HRESULT ECGenericProp::GetSingleInstanceId(ULONG *lpcbInstanceID,
    ENTRYID **lppInstanceID)
{
	if (lpcbInstanceID == nullptr || lppInstanceID == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	scoped_rlock lock(m_hMutexMAPIObject);
	if (m_sMapiObject == NULL)
		return MAPI_E_NOT_FOUND;
	return Util::HrCopyEntryId(m_sMapiObject->cbInstanceID,
	       reinterpret_cast<ENTRYID *>(m_sMapiObject->lpInstanceID),
	       lpcbInstanceID, lppInstanceID);
}

HRESULT ECGenericProp::SetSingleInstanceId(ULONG cbInstanceID,
    const ENTRYID *lpInstanceID)
{
	scoped_rlock lock(m_hMutexMAPIObject);

	if (m_sMapiObject == NULL)
		return MAPI_E_NOT_FOUND;
	if (m_sMapiObject->lpInstanceID)
		ECFreeBuffer(m_sMapiObject->lpInstanceID);

	m_sMapiObject->lpInstanceID = NULL;
	m_sMapiObject->cbInstanceID = 0;
	m_sMapiObject->bChangedInstance = false;

	HRESULT hr = Util::HrCopyEntryId(cbInstanceID,
		lpInstanceID,
		&m_sMapiObject->cbInstanceID,
		reinterpret_cast<ENTRYID **>(&m_sMapiObject->lpInstanceID));
	if (hr != hrSuccess)
		return hr;
	m_sMapiObject->bChangedInstance = true;
	return hr;
}
