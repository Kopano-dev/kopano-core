/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <new>
#include <kopano/platform.h>
#include "WSMAPIPropStorage.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/Util.h>
#include "pcutil.hpp"
#include <kopano/charset/convert.h>
#include "soapKCmdProxy.h"

/*
 * This is a PropStorage object for use with the WebServices storage platform
 */
#define START_SOAP_CALL retry:
#define END_SOAP_CALL 	\
	if (er == KCERR_END_OF_SESSION && m_lpTransport->HrReLogon() == hrSuccess) \
		goto retry; \
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND); \
	if(hr != hrSuccess) \
		goto exit;

using namespace KC;

WSMAPIPropStorage::WSMAPIPropStorage(ULONG cbParentEntryId,
    const ENTRYID *lpParentEntryId, ULONG cbEntryId, const ENTRYID *lpEntryId,
    ULONG ulFlags, ECSESSIONID sid, unsigned int sc, WSTransport *tp) :
	ECUnknown("WSMAPIPropStorage"), ecSessionId(sid),
	ulServerCapabilities(sc), m_ulFlags(ulFlags), m_lpTransport(tp)
{
	CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &m_sEntryId);
	CopyMAPIEntryIdToSOAPEntryId(cbParentEntryId, lpParentEntryId, &m_sParentEntryId);
	tp->AddSessionReloadCallback(this, Reload, &m_ulSessionReloadCallback);
}

WSMAPIPropStorage::~WSMAPIPropStorage()
{
	// Unregister the notification request we sent
	if(m_bSubscribed) {
		ECRESULT er = erSuccess;
		soap_lock_guard spg(*m_lpTransport);
		m_lpTransport->m_lpCmd->notifyUnSubscribe(ecSessionId, m_ulConnection, &er);
	}

	FreeEntryId(&m_sEntryId, false);
	FreeEntryId(&m_sParentEntryId, false);
	m_lpTransport->RemoveSessionReloadCallback(m_ulSessionReloadCallback);
}

HRESULT WSMAPIPropStorage::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(WSMAPIPropStorage, this);
	REGISTER_INTERFACE2(IECPropStorage, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSMAPIPropStorage::Create(ULONG cbParentEntryId,
    const ENTRYID *lpParentEntryId, ULONG cbEntryId, const ENTRYID *lpEntryId,
    ULONG ulFlags, ECSESSIONID ecSessionId, unsigned int ulServerCapabilities,
    WSTransport *lpTransport, WSMAPIPropStorage **lppPropStorage)
{
	return alloc_wrap<WSMAPIPropStorage>(cbParentEntryId, lpParentEntryId,
	       cbEntryId, lpEntryId, ulFlags, ecSessionId,
	       ulServerCapabilities, lpTransport).put(lppPropStorage);
}

HRESULT WSMAPIPropStorage::HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue)
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpsPropValDst = NULL;

	struct loadPropResponse	sResponse;
	soap_lock_guard spg(*m_lpTransport);

	if (ulObjId == 0 && (ulServerCapabilities & KOPANO_CAP_LOADPROP_ENTRYID) == 0) {
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->loadProp(ecSessionId, m_sEntryId,
		    ulObjId, ulPropTag, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	END_SOAP_CALL

	hr = ECAllocateBuffer(sizeof(SPropValue), (void **)&lpsPropValDst);
	if(hr != hrSuccess)
		goto exit;

	if(sResponse.lpPropVal == NULL) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	hr = CopySOAPPropValToMAPIPropVal(lpsPropValDst, sResponse.lpPropVal, lpsPropValDst);
	*lppsPropValue = lpsPropValDst;
exit:
	return hr;
}

HRESULT WSMAPIPropStorage::HrMapiObjectToSoapObject(const MAPIOBJECT *lpsMapiObject,
    struct saveObject *lpSaveObj, convert_context *lpConverter)
{
	if (lpConverter == NULL) {
		convert_context converter;
		return HrMapiObjectToSoapObject(lpsMapiObject, lpSaveObj, &converter);
	}

	HRESULT hr = hrSuccess;
	ULONG ulPropId = 0;
	GUID sServerGUID = {0}, sSIGUID = {0};

	if (lpsMapiObject->lpInstanceID) {
		lpSaveObj->lpInstanceIds = s_alloc<entryList>(nullptr);
		lpSaveObj->lpInstanceIds->__size = 1;
		lpSaveObj->lpInstanceIds->__ptr = s_alloc<entryId>(nullptr, lpSaveObj->lpInstanceIds->__size);
		memset(lpSaveObj->lpInstanceIds->__ptr, 0, lpSaveObj->lpInstanceIds->__size * sizeof(entryId));

		if ((m_lpTransport->GetServerGUID(&sServerGUID) != hrSuccess) ||
			(HrSIEntryIDToID(lpsMapiObject->cbInstanceID, (LPBYTE)lpsMapiObject->lpInstanceID, &sSIGUID, NULL, (unsigned int*)&ulPropId) != hrSuccess) ||
			(sSIGUID != sServerGUID) ||
			(CopyMAPIEntryIdToSOAPEntryId(lpsMapiObject->cbInstanceID, (LPENTRYID)lpsMapiObject->lpInstanceID, &lpSaveObj->lpInstanceIds->__ptr[0]) != hrSuccess)) {
				ulPropId = 0;
				FreeEntryList(lpSaveObj->lpInstanceIds, true);
				lpSaveObj->lpInstanceIds = NULL;
		}
	} else {
		lpSaveObj->lpInstanceIds = NULL;
	}

	// deleted props
	unsigned int size = lpsMapiObject->lstDeleted.size();
	if (size != 0) {
		lpSaveObj->delProps.__ptr = s_alloc<unsigned int>(nullptr, size);
		lpSaveObj->delProps.__size = size;
		unsigned int i = 0;
		for (auto id : lpsMapiObject->lstDeleted)
			lpSaveObj->delProps.__ptr[i++] = id;
	} else {
		lpSaveObj->delProps.__ptr = NULL;
		lpSaveObj->delProps.__size = 0;
	}

	// modified props
	size = lpsMapiObject->lstModified.size();
	if (size != 0) {
		lpSaveObj->modProps.__ptr = s_alloc<propVal>(nullptr, size);
		unsigned int i = 0;
		for (const auto &prop : lpsMapiObject->lstModified) {
			SPropValue tmp = prop.GetMAPIPropValRef();
			if (PROP_ID(tmp.ulPropTag) == ulPropId)
				/* Skip the data if it is a Instance ID, If the instance id is invalid
				 * the data will be replaced in HrUpdateSoapObject function. The memory is already
				 * allocated, but not used. */
				if (/*lpsMapiObject->bChangedInstance &&*/ lpsMapiObject->lpInstanceID)
					continue;

			hr = CopyMAPIPropValToSOAPPropVal(&lpSaveObj->modProps.__ptr[i], &tmp, lpConverter);
			if(hr == hrSuccess)
				++i;
		}
		lpSaveObj->modProps.__size = i;
	} else {
		lpSaveObj->modProps.__ptr = NULL;
		lpSaveObj->modProps.__size = 0;
	}

	// recursive process all children when parent is still present
	lpSaveObj->__size = 0;
	lpSaveObj->__ptr = NULL;
	if (!lpsMapiObject->bDelete) {
		size = lpsMapiObject->lstChildren.size();
		if (size != 0) {
			lpSaveObj->__ptr = s_alloc<saveObject>(nullptr, size);
			size = 0;
			for (const auto &cld : lpsMapiObject->lstChildren)
				// Only send children if:
				// - Modified AND NOT deleted
				// - Deleted AND loaded from server (locally created/deleted items with no server ID needn't be sent)
				if ((cld->bChanged && !cld->bDelete) || (cld->ulObjId && cld->bDelete))
					hr = HrMapiObjectToSoapObject(cld, &lpSaveObj->__ptr[size++], lpConverter);
			lpSaveObj->__size = size;
		}
	}

	// object info
	lpSaveObj->bDelete = lpsMapiObject->bDelete;
	lpSaveObj->ulClientId = lpsMapiObject->ulUniqueId;
	lpSaveObj->ulServerId = lpsMapiObject->ulObjId;
	lpSaveObj->ulObjType = lpsMapiObject->ulObjType;
	return hr;
}

HRESULT WSMAPIPropStorage::HrUpdateSoapObject(const MAPIOBJECT *lpsMapiObject,
    struct saveObject *lpsSaveObj, convert_context *lpConverter)
{
	std::list<ECProperty>::const_iterator iterProps;
	ULONG ulPropId = 0;

	if (lpConverter == NULL) {
		convert_context converter;
		return HrUpdateSoapObject(lpsMapiObject, lpsSaveObj, &converter);
	}

	/* FIXME: Support Multiple Single Instances */
	if (lpsSaveObj->lpInstanceIds && lpsSaveObj->lpInstanceIds->__size) {
		/* Check which property this Instance ID is for */
		auto hr = HrSIEntryIDToID(lpsSaveObj->lpInstanceIds->__ptr[0].__size, lpsSaveObj->lpInstanceIds->__ptr[0].__ptr, NULL, NULL, (unsigned int*)&ulPropId);
		if (hr != hrSuccess)
			return hr;
		/* Instance ID was incorrect, remove it */
		FreeEntryList(lpsSaveObj->lpInstanceIds, true);
		lpsSaveObj->lpInstanceIds = NULL;

		/* Search for the correct property and copy it into the soap object, note that we already allocated the required memory... */
		for (iterProps = lpsMapiObject->lstModified.cbegin();
		     iterProps != lpsMapiObject->lstModified.cend();
		     ++iterProps) {
			const auto &sData = iterProps->GetMAPIPropValRef();
			if (PROP_ID(sData.ulPropTag) != ulPropId)
				continue;

			// Extra check for protect the modProps array
			if (lpsSaveObj->modProps.__size >= 0 &&
			    static_cast<size_t>(lpsSaveObj->modProps.__size) >= lpsMapiObject->lstModified.size()) {
				/*
				 * modProps.size+1 > lpsMapiObject->lstModified->size()
				 * (a+1>b) transformed to (a>=b)
				 */
				assert(false);
				return MAPI_E_NOT_ENOUGH_MEMORY;
			}

			hr = CopyMAPIPropValToSOAPPropVal(&lpsSaveObj->modProps.__ptr[lpsSaveObj->modProps.__size], &sData, lpConverter);
			if(hr != hrSuccess)
				return hr;
			++lpsSaveObj->modProps.__size;
			break;
		}

		// Broken single instance ID without data.
		assert(iterProps != lpsMapiObject->lstModified.cend());
	}

	for (gsoap_size_t i = 0; i < lpsSaveObj->__size; ++i) {
		MAPIOBJECT find(lpsSaveObj->__ptr[i].ulObjType, lpsSaveObj->__ptr[i].ulClientId);
		auto iter = lpsMapiObject->lstChildren.find(&find);
		if (iter != lpsMapiObject->lstChildren.cend()) {
			auto hr = HrUpdateSoapObject(*iter, &lpsSaveObj->__ptr[i], lpConverter);
			if (hr != hrSuccess)
				return hr;
		}
	}
	return hrSuccess;
}

void WSMAPIPropStorage::DeleteSoapObject(struct saveObject *lpSaveObj)
{
	if (lpSaveObj->__ptr) {
		for (gsoap_size_t i = 0; i < lpSaveObj->__size; ++i)
			DeleteSoapObject(&lpSaveObj->__ptr[i]);
		s_free(nullptr, lpSaveObj->__ptr);
	}
	if (lpSaveObj->modProps.__ptr) {
		for (gsoap_size_t i = 0; i < lpSaveObj->modProps.__size; ++i)
			FreePropVal(&lpSaveObj->modProps.__ptr[i], false);
		s_free(nullptr, lpSaveObj->modProps.__ptr);
	}
	s_free(nullptr, lpSaveObj->delProps.__ptr);
	if (lpSaveObj->lpInstanceIds)
		FreeEntryList(lpSaveObj->lpInstanceIds, true);
}

ECRESULT WSMAPIPropStorage::EcFillPropTags(const struct saveObject *lpsSaveObj,
    MAPIOBJECT *lpsMapiObj)
{
	for (gsoap_size_t i = 0; i < lpsSaveObj->delProps.__size; ++i)
		lpsMapiObj->lstAvailable.emplace_back(lpsSaveObj->delProps.__ptr[i]);
	return erSuccess;
}

ECRESULT WSMAPIPropStorage::EcFillPropValues(const struct saveObject *lpsSaveObj,
     MAPIOBJECT *lpsMapiObj)
{
	convert_context	context;

	for (gsoap_size_t i = 0; i < lpsSaveObj->modProps.__size; ++i) {
		ecmem_ptr<SPropValue> lpsProp;
		auto ec = ECAllocateBuffer(sizeof(SPropValue), &~lpsProp);
		if (ec != erSuccess)
			return ec;
		ec = CopySOAPPropValToMAPIPropVal(lpsProp, &lpsSaveObj->modProps.__ptr[i], lpsProp, &context);
		if (ec != erSuccess)
			return ec;
		lpsMapiObj->lstProperties.emplace_back(lpsProp);
	}
	return erSuccess;
}

// sets the ulObjId from the server in the object (hierarchyid)
// removes deleted sub-objects from memory
// removes current list of del/mod props, and sets server changes in the lists
HRESULT WSMAPIPropStorage::HrUpdateMapiObject(MAPIOBJECT *lpClientObj,
    const struct saveObject *lpsServerObj)
{
	lpClientObj->ulObjId = lpsServerObj->ulServerId;
	// The deleted properties have been deleted, so forget about them
	lpClientObj->lstDeleted.clear();
	// The modified properties have been sent. Delete them.
	lpClientObj->lstModified.clear();
	// The object is no longer 'changed'
	lpClientObj->bChangedInstance = false;
	lpClientObj->bChanged = false;

	// copy new server props to object (these are the properties we have received after saving the
	// object. For example, a new PR_LAST_MODIFICATION_TIME). These append lstProperties and lstAvailable
	// with the new properties.
	EcFillPropTags(lpsServerObj, lpClientObj);
	EcFillPropValues(lpsServerObj, lpClientObj);

	// We might have received a new instance ID from the server
	if (lpClientObj->lpInstanceID) {
		ECFreeBuffer(lpClientObj->lpInstanceID);
		lpClientObj->lpInstanceID = NULL;
		lpClientObj->cbInstanceID = 0;
	}

	/* FIXME: Support Multiple Single Instances */
	if (lpsServerObj->lpInstanceIds && lpsServerObj->lpInstanceIds->__size &&
	    CopySOAPEntryIdToMAPIEntryId(&lpsServerObj->lpInstanceIds->__ptr[0], &lpClientObj->cbInstanceID, (LPENTRYID *)&lpClientObj->lpInstanceID) != hrSuccess)
		return MAPI_E_INVALID_PARAMETER;

	for (auto iterObj = lpClientObj->lstChildren.cbegin();
	     iterObj != lpClientObj->lstChildren.cend(); ) {
		if ((*iterObj)->bDelete) {
			// this child was removed, so we don't need it anymore
			auto iterDel = iterObj;
			++iterObj;
			delete *iterDel;
			lpClientObj->lstChildren.erase(iterDel);
			continue;
		} else if (!(*iterObj)->bChanged) {
			// this was never sent to the server, so it is not going to be in the server object
			++iterObj;
			continue;
		}
		// find by client id, and set server id
		gsoap_size_t i = 0;
		while (i < lpsServerObj->__size) {
			if ((*iterObj)->ulUniqueId == lpsServerObj->__ptr[i].ulClientId && (*iterObj)->ulObjType == lpsServerObj->__ptr[i].ulObjType)
				break;
			++i;
		}
		if (i == lpsServerObj->__size) {
			// huh?
			assert(false);
			return MAPI_E_NOT_FOUND;
		}
		HrUpdateMapiObject(*iterObj, &lpsServerObj->__ptr[i]);
		++iterObj;
	}
	return hrSuccess;
}

HRESULT WSMAPIPropStorage::HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject)
{
	ECRESULT	er = erSuccess;
	HRESULT		hr = hrSuccess;
	struct saveObject sSaveObj;
	struct loadObjectResponse sResponse;
	convert_context converter;

	HrMapiObjectToSoapObject(lpsMapiObject, &sSaveObj, &converter);
	soap_lock_guard spg(*m_lpTransport);
	// ulFlags == object flags, e.g. MAPI_ASSOCIATE for messages, FOLDER_SEARCH on folders...
	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->saveObject(ecSessionId,
		    m_sParentEntryId, m_sEntryId, &sSaveObj, ulFlags,
		    m_ulSyncId, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}

	if (er == KCERR_UNKNOWN_INSTANCE_ID) {
		/* Instance ID was unknown, we should resend entire message again, but this time include the instance body */
		hr = HrUpdateSoapObject(lpsMapiObject, &sSaveObj, &converter);
		if (hr != hrSuccess)
			goto exit;
		goto retry;
	}
	END_SOAP_CALL

	// Update our copy of the object with the IDs and mod props from the server
	hr = HrUpdateMapiObject(lpsMapiObject, &sResponse.sSaveObject);
	if (hr != hrSuccess)
		goto exit;

	// HrUpdateMapiObject() has now moved the modified properties of all objects recursively
	// into their 'normal' properties list (lstProperties). This is correct because when we now
	// open a subobject, it needs all the properties.
	// However, because we already have all properties of the top-level message in-memory via
	// ECGenericProps, the properties in
exit:
	spg.unlock();
	DeleteSoapObject(&sSaveObj);
	return hr;
}

ECRESULT WSMAPIPropStorage::ECSoapObjectToMapiObject(const struct saveObject *lpsSaveObj,
    MAPIOBJECT *lpsMapiObject)
{
	MAPIOBJECT *mo = NULL;
	unsigned int ulAttachUniqueId = 0, ulRecipUniqueId = 0;

	// delProps contains all the available property tag
	EcFillPropTags(lpsSaveObj, lpsMapiObject);
	/* modProps contains all the props < MAX_PROP_SIZE */
	EcFillPropValues(lpsSaveObj, lpsMapiObject);
	// delete stays false, unique id is set upon allocation
	lpsMapiObject->ulObjId = lpsSaveObj->ulServerId;
	lpsMapiObject->ulObjType = lpsSaveObj->ulObjType;

	// children
	for (gsoap_size_t i = 0; i < lpsSaveObj->__size; ++i) {
		switch (lpsSaveObj->__ptr[i].ulObjType) {
		case MAPI_ATTACH:
			mo = new MAPIOBJECT(ulAttachUniqueId++, lpsSaveObj->__ptr[i].ulServerId, lpsSaveObj->__ptr[i].ulObjType);
			break;
		case MAPI_MAILUSER:
		case MAPI_DISTLIST:
			mo = new MAPIOBJECT(ulRecipUniqueId++, lpsSaveObj->__ptr[i].ulServerId, lpsSaveObj->__ptr[i].ulObjType);
			break;
		default:
			mo = new MAPIOBJECT(0, lpsSaveObj->__ptr[i].ulServerId, lpsSaveObj->__ptr[i].ulObjType);
			break;
		}

		ECSoapObjectToMapiObject(&lpsSaveObj->__ptr[i], mo);
		lpsMapiObject->lstChildren.emplace(mo);
	}

	if (lpsMapiObject->lpInstanceID) {
		ECFreeBuffer(lpsMapiObject->lpInstanceID);
		lpsMapiObject->lpInstanceID = NULL;
		lpsMapiObject->cbInstanceID = 0;
	}

	/* FIXME: Support Multiple Single Instances */
	if (lpsSaveObj->lpInstanceIds && lpsSaveObj->lpInstanceIds->__size &&
	    CopySOAPEntryIdToMAPIEntryId(&lpsSaveObj->lpInstanceIds->__ptr[0], &lpsMapiObject->cbInstanceID, (LPENTRYID *)&lpsMapiObject->lpInstanceID) != hrSuccess)
		return KCERR_INVALID_PARAMETER;

	return erSuccess;
}

// Register an advise without an extra server call
HRESULT WSMAPIPropStorage::RegisterAdvise(ULONG ulEventMask, ULONG ulConnection)
{
	m_ulConnection = ulConnection;
	m_ulEventMask = ulEventMask;
	return hrSuccess;
}

HRESULT WSMAPIPropStorage::GetEntryIDByRef(ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	if (lpcbEntryID == NULL || lppEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;
	*lpcbEntryID = m_sEntryId.__size;
	*lppEntryID = (LPENTRYID)m_sEntryId.__ptr;
	return hrSuccess;
}

HRESULT WSMAPIPropStorage::HrLoadObject(MAPIOBJECT **lppsMapiObject)
{
	ECRESULT er = erSuccess;
	HRESULT hr = hrSuccess;
	struct loadObjectResponse sResponse;
	MAPIOBJECT *lpsMapiObject = NULL;
	struct notifySubscribe sNotSubscribe;

	if (m_ulConnection) {
		// Register notification
		sNotSubscribe.ulConnection = m_ulConnection;
		sNotSubscribe.ulEventMask = m_ulEventMask;
		sNotSubscribe.sKey.__size = m_sEntryId.__size;
		sNotSubscribe.sKey.__ptr = m_sEntryId.__ptr;
	}

	soap_lock_guard spg(*m_lpTransport);
	if (!lppsMapiObject) {
		assert(false);
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}
	if (*lppsMapiObject) {
		// memleak detected
		assert(false);
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	START_SOAP_CALL
	{
		if (m_lpTransport->m_lpCmd->loadObject(ecSessionId, m_sEntryId,
		    (m_ulConnection == 0 || m_bSubscribed) ? nullptr : &sNotSubscribe,
		    m_ulFlags | 0x80000000, &sResponse) != SOAP_OK)
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	//END_SOAP_CALL

	if (er == KCERR_END_OF_SESSION && m_lpTransport->HrReLogon() == hrSuccess)
		goto retry;
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)	// Store does not exist on this server
		hr = MAPI_E_UNCONFIGURED;			// Force a reconfigure
	if(hr != hrSuccess)
		goto exit;
	lpsMapiObject = new MAPIOBJECT; /* ulObjType, ulObjId and ulUniqueId are unknown here */
	ECSoapObjectToMapiObject(&sResponse.sSaveObject, lpsMapiObject);
	*lppsMapiObject = lpsMapiObject;
	m_bSubscribed = m_ulConnection != 0;
exit:
	return hr;
}

HRESULT WSMAPIPropStorage::HrSetSyncId(ULONG ulSyncId) {
	m_ulSyncId = ulSyncId;
	return hrSuccess;
}

// Called when the session ID has changed
HRESULT WSMAPIPropStorage::Reload(void *lpParam, ECSESSIONID sessionId) {
	static_cast<WSMAPIPropStorage *>(lpParam)->ecSessionId = sessionId;
	return hrSuccess;
}
