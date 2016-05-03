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

#include "WSMAPIPropStorage.h"

#include "Mem.h"
#include <kopano/ECGuid.h>

// Utils
#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/Util.h>
#include "pcutil.hpp"

#include <kopano/charset/convert.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

/*
 *
 * This is a PropStorage object for use with the WebServices storage platform
 *
 */

#define START_SOAP_CALL retry:

#define END_SOAP_CALL 	\
	if(er == KCERR_END_OF_SESSION) { if(this->m_lpTransport->HrReLogon() == hrSuccess) goto retry; } \
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND); \
	if(hr != hrSuccess) \
		goto exit;

WSMAPIPropStorage::WSMAPIPropStorage(ULONG cbParentEntryId, LPENTRYID lpParentEntryId, ULONG cbEntryId, LPENTRYID lpEntryId, ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, unsigned int ulServerCapabilities, WSTransport *lpTransport) : ECUnknown("WSMAPIPropStorage")
{
	CopyMAPIEntryIdToSOAPEntryId(cbEntryId, lpEntryId, &m_sEntryId);
	CopyMAPIEntryIdToSOAPEntryId(cbParentEntryId, lpParentEntryId, &m_sParentEntryId);

	this->lpCmd = lpCmd;
	this->lpDataLock = lpDataLock;
	this->ecSessionId = ecSessionId;
	this->ulServerCapabilities = ulServerCapabilities;
	this->m_ulSyncId = 0;
	this->m_ulFlags = ulFlags;
	m_ulConnection = 0;
	m_ulEventMask = 0;
	m_lpTransport = lpTransport;
	m_bSubscribed = 0;

	lpTransport->AddSessionReloadCallback(this, Reload, &m_ulSessionReloadCallback);
}

WSMAPIPropStorage::~WSMAPIPropStorage()
{
	// Unregister the notification request we sent
	if(m_bSubscribed) {
		ECRESULT er = erSuccess;

		LockSoap();		
		lpCmd->ns__notifyUnSubscribe(ecSessionId, m_ulConnection, &er);
		UnLockSoap();
	}
	
	FreeEntryId(&m_sEntryId, false);
	FreeEntryId(&m_sParentEntryId, false);

	m_lpTransport->RemoveSessionReloadCallback(m_ulSessionReloadCallback);
}

HRESULT WSMAPIPropStorage::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_WSMAPIPropStorage, this);

	REGISTER_INTERFACE(IID_IECPropStorage, &this->m_xECPropStorage);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT WSMAPIPropStorage::Create(ULONG cbParentEntryId, LPENTRYID lpParentEntryId, ULONG cbEntryId, LPENTRYID lpEntryId, ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, unsigned int ulServerCapabilities, WSTransport *lpTransport, WSMAPIPropStorage **lppPropStorage)
{
	HRESULT hr = hrSuccess;
	WSMAPIPropStorage *lpStorage = NULL;
	
	lpStorage = new WSMAPIPropStorage(cbParentEntryId, lpParentEntryId, cbEntryId, lpEntryId, ulFlags, lpCmd, lpDataLock, ecSessionId, ulServerCapabilities, lpTransport);

	hr = lpStorage->QueryInterface(IID_WSMAPIPropStorage, (void **)lppPropStorage);

	if(hr != hrSuccess)
		delete lpStorage;

	return hr;
}

HRESULT WSMAPIPropStorage::HrReadProps(LPSPropTagArray *lppPropTags,ULONG *cValues, LPSPropValue *ppValues)
{
	// this call should disappear
	return MAPI_E_NO_SUPPORT;
}

HRESULT WSMAPIPropStorage::HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue)
{
	ECRESULT		er = erSuccess;
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpsPropValDst = NULL;

	struct loadPropResponse	sResponse;
	
	LockSoap();
	
	if (ulObjId == 0 && ((this->ulServerCapabilities & KOPANO_CAP_LOADPROP_ENTRYID) == 0)) {
		hr = MAPI_E_NO_SUPPORT; 
		goto exit; 
	} 

	START_SOAP_CALL
	{
		if(SOAP_OK != lpCmd->ns__loadProp(ecSessionId, m_sEntryId, ulObjId, ulPropTag, &sResponse))
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
	UnLockSoap();

	return hr;
}

HRESULT WSMAPIPropStorage::HrWriteProps(ULONG cValues, LPSPropValue pValues, ULONG ulFlags)
{
	// this call should disappear
	return MAPI_E_NO_SUPPORT;
}

HRESULT WSMAPIPropStorage::HrDeleteProps(LPSPropTagArray lpsPropTagArray)
{
	// this call should disappear
	return MAPI_E_NO_SUPPORT;
}

HRESULT WSMAPIPropStorage::HrMapiObjectToSoapObject(MAPIOBJECT *lpsMapiObject, struct saveObject *lpSaveObj, convert_context *lpConverter)
{
	HRESULT hr = hrSuccess;
	unsigned int size;
	unsigned int i;
	std::list<ULONG>::const_iterator iterDelProps;
	std::list<ECProperty>::const_iterator iterModProps;
	ECMapiObjects::const_iterator iterChildren;
	ULONG ulPropId = 0;
	GUID sServerGUID = {0};
	GUID sSIGUID = {0};
	
	if (lpConverter == NULL) {
		convert_context converter;
		hr = HrMapiObjectToSoapObject(lpsMapiObject, lpSaveObj, &converter);
		goto exit;
	}

	if (lpsMapiObject->lpInstanceID) {
		lpSaveObj->lpInstanceIds = new struct entryList;
		lpSaveObj->lpInstanceIds->__size = 1;
		lpSaveObj->lpInstanceIds->__ptr = new entryId[lpSaveObj->lpInstanceIds->__size];
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
	size = lpsMapiObject->lstDeleted->size();
	if (size != 0) {
		lpSaveObj->delProps.__ptr = new unsigned int[size];
		lpSaveObj->delProps.__size = size;
		i = 0;
		for (iterDelProps = lpsMapiObject->lstDeleted->begin();
		     iterDelProps != lpsMapiObject->lstDeleted->end();
		     ++iterDelProps, ++i)
			lpSaveObj->delProps.__ptr[i] = *iterDelProps;
	} else {
		lpSaveObj->delProps.__ptr = NULL;
		lpSaveObj->delProps.__size = 0;
	}

	// modified props
	size = lpsMapiObject->lstModified->size();
	if (size != 0) {
		lpSaveObj->modProps.__ptr = new struct propVal[size];
		i = 0;
		for (iterModProps = lpsMapiObject->lstModified->begin();
		     iterModProps != lpsMapiObject->lstModified->end();
		     ++iterModProps)
		{
			SPropValue tmp = iterModProps->GetMAPIPropValRef();

			if( PROP_ID(tmp.ulPropTag) == ulPropId) {

				/* Skip the data if it is a Instance ID, If the instance id is invalid 
				 * the data will be replaced in HrUpdateSoapObject function. The memory is already 
				 * allocated, but not used. */
				if (/*lpsMapiObject->bChangedInstance &&*/ lpsMapiObject->lpInstanceID)
					continue;
			}

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
	if (lpsMapiObject->bDelete == false) {
		size = lpsMapiObject->lstChildren->size();
		if (size != 0) {
			lpSaveObj->__ptr = new struct saveObject[size];
			i = 0;
			size = 0;
			for (iterChildren = lpsMapiObject->lstChildren->begin();
			     iterChildren != lpsMapiObject->lstChildren->end();
			     ++iterChildren, ++i) {
				// Only send children if:
				// - Modified AND NOT deleted
				// - Deleted AND loaded from server (locally created/deleted items with no server ID needn't be sent)
				if (((*iterChildren)->bChanged && !(*iterChildren)->bDelete) || ((*iterChildren)->ulObjId && (*iterChildren)->bDelete))
					hr = HrMapiObjectToSoapObject(*iterChildren, &lpSaveObj->__ptr[size++], lpConverter);
			}
			lpSaveObj->__size = size;
		}
	}

	// object info
	lpSaveObj->bDelete = lpsMapiObject->bDelete;
	lpSaveObj->ulClientId = lpsMapiObject->ulUniqueId;
	lpSaveObj->ulServerId = lpsMapiObject->ulObjId;
	lpSaveObj->ulObjType = lpsMapiObject->ulObjType;

exit:
	return hr;
} 

HRESULT WSMAPIPropStorage::HrUpdateSoapObject(MAPIOBJECT *lpsMapiObject, struct saveObject *lpsSaveObj, convert_context *lpConverter)
{
	HRESULT hr;
	ECMapiObjects::const_iterator iter;
	std::list<ECProperty>::const_iterator iterProps;
	SPropValue sData;
	ULONG ulPropId = 0;
	int i;

	if (lpConverter == NULL) {
		convert_context converter;
		return HrUpdateSoapObject(lpsMapiObject, lpsSaveObj, &converter);
	}

	/* FIXME: Support Multiple Single Instances */
	if (lpsSaveObj->lpInstanceIds && lpsSaveObj->lpInstanceIds->__size) {
		/* Check which property this Instance ID is for */
		hr = HrSIEntryIDToID(lpsSaveObj->lpInstanceIds->__ptr[0].__size, lpsSaveObj->lpInstanceIds->__ptr[0].__ptr, NULL, NULL, (unsigned int*)&ulPropId);
		if (hr != hrSuccess)
			return hr;

		/* Instance ID was incorrect, remove it */
		FreeEntryList(lpsSaveObj->lpInstanceIds, true);
		lpsSaveObj->lpInstanceIds = NULL;

		/* Search for the correct property and copy it into the soap object, note that we already allocated the required memory... */
		for (iterProps = lpsMapiObject->lstModified->begin();
		     iterProps != lpsMapiObject->lstModified->end();
		     ++iterProps) {
			sData = iterProps->GetMAPIPropValRef();

			if (PROP_ID(sData.ulPropTag) != ulPropId)
				continue;

			// Extra check for protect the modProps array
			if(lpsSaveObj->modProps.__size+1 > lpsMapiObject->lstModified->size() ) {
				ASSERT(FALSE);
				return MAPI_E_NOT_ENOUGH_MEMORY;
			}

			hr = CopyMAPIPropValToSOAPPropVal(&lpsSaveObj->modProps.__ptr[lpsSaveObj->modProps.__size], &sData, lpConverter);
			if(hr != hrSuccess)
				return hr;
			++lpsSaveObj->modProps.__size;
			break;
		}

		// Broken single instance ID without data.
		ASSERT(!(iterProps == lpsMapiObject->lstModified->end()) );
	}

	for (i = 0; i < lpsSaveObj->__size; ++i) {
		MAPIOBJECT find(lpsSaveObj->__ptr[i].ulObjType, lpsSaveObj->__ptr[i].ulClientId);
		iter = lpsMapiObject->lstChildren->find(&find);
		
		if(iter != lpsMapiObject->lstChildren->end()) {
			hr = HrUpdateSoapObject(*iter, &lpsSaveObj->__ptr[i], lpConverter);
			if (hr != hrSuccess)
				return hr;
		}
	}
	return hrSuccess;
}

void WSMAPIPropStorage::DeleteSoapObject(struct saveObject *lpSaveObj)
{
	int i;

	if (lpSaveObj->__ptr) {
		for (i = 0; i < lpSaveObj->__size; ++i)
			DeleteSoapObject(&lpSaveObj->__ptr[i]);
		delete [] lpSaveObj->__ptr;
	}

	if (lpSaveObj->modProps.__ptr) {
		for (i = 0; i < lpSaveObj->modProps.__size; ++i)
			FreePropVal(&lpSaveObj->modProps.__ptr[i], false);
		delete [] lpSaveObj->modProps.__ptr;
	}

	delete[] lpSaveObj->delProps.__ptr;
	if (lpSaveObj->lpInstanceIds)
		FreeEntryList(lpSaveObj->lpInstanceIds, true);
}

ECRESULT WSMAPIPropStorage::EcFillPropTags(struct saveObject *lpsSaveObj, MAPIOBJECT *lpsMapiObj)
{
	for (int i = 0; i < lpsSaveObj->delProps.__size; ++i)
		lpsMapiObj->lstAvailable->push_back(lpsSaveObj->delProps.__ptr[i]);
	return erSuccess;
}

ECRESULT WSMAPIPropStorage::EcFillPropValues(struct saveObject *lpsSaveObj, MAPIOBJECT *lpsMapiObj)
{
	ECRESULT ec = erSuccess;
	LPSPropValue lpsProp = NULL;
	convert_context	context;

	for (int i = 0; i < lpsSaveObj->modProps.__size; ++i) {
		ECAllocateBuffer(sizeof(SPropValue), (void **)&lpsProp);

		ec = CopySOAPPropValToMAPIPropVal(lpsProp, &lpsSaveObj->modProps.__ptr[i], lpsProp, &context);
		if (ec != erSuccess)
			break;

		lpsMapiObj->lstProperties->push_back(ECProperty(lpsProp));

		ECFreeBuffer(lpsProp);
	}

	return ec;
}

// sets the ulObjId from the server in the object (hierarchyid)
// removes deleted sub-objects from memory
// removes current list of del/mod props, and sets server changes in the lists
HRESULT WSMAPIPropStorage::HrUpdateMapiObject(MAPIOBJECT *lpClientObj, struct saveObject *lpsServerObj)
{
	ECMapiObjects::const_iterator iterObj, iterDel;
	int i;

	lpClientObj->ulObjId = lpsServerObj->ulServerId;

	// The deleted properties have been deleted, so forget about them
	lpClientObj->lstDeleted->clear();

	// The modified properties have been sent. Delete them.
	lpClientObj->lstModified->clear(); 

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

	for (iterObj = lpClientObj->lstChildren->begin(); iterObj != lpClientObj->lstChildren->end(); ) {
		if ((*iterObj)->bDelete) {
			// this child was removed, so we don't need it anymore
			iterDel = iterObj;
			++iterObj;
			FreeMapiObject(*iterDel);
			lpClientObj->lstChildren->erase(iterDel);
		} else if ((*iterObj)->bChanged) {
			// find by client id, and set server id
			i = 0;
			while (i < lpsServerObj->__size) {
				if ((*iterObj)->ulUniqueId == lpsServerObj->__ptr[i].ulClientId && (*iterObj)->ulObjType == lpsServerObj->__ptr[i].ulObjType)
					break;
				++i;
			}
			if (i == lpsServerObj->__size) {
				// huh?
				ASSERT(false);
				return MAPI_E_NOT_FOUND;
			}

			HrUpdateMapiObject(*iterObj, &lpsServerObj->__ptr[i]);

			++iterObj;
		} else {
			++iterObj;
			// this was never sent to the server, so it is not going to be in the server object
		}
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

	LockSoap();

	// ulFlags == object flags, eg. MAPI_ASSOCIATE for messages, FOLDER_SEARCH on folders...
	START_SOAP_CALL
	{
		if (SOAP_OK != lpCmd->ns__saveObject(ecSessionId, m_sParentEntryId, m_sEntryId, &sSaveObj, ulFlags, m_ulSyncId, &sResponse))
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

	// Update our copy of the object with the id's and mod props from the server
	hr = HrUpdateMapiObject(lpsMapiObject, &sResponse.sSaveObject);
	if (hr != hrSuccess)
		goto exit;

	// HrUpdateMapiObject() has now moved the modified properties of all objects recursively
	// into their 'normal' properties list (lstProperties). This is correct because when we now
	// open a subobject, it needs all the properties.

	// However, because we already have all properties of the top-level message in-memory via
	// ECGenericProps, the properties in 

exit:
	UnLockSoap();

	DeleteSoapObject(&sSaveObj);

	return hr;
}

ECRESULT WSMAPIPropStorage::ECSoapObjectToMapiObject(struct saveObject *lpsSaveObj, MAPIOBJECT *lpsMapiObject) {
	ECRESULT ec;
	int i = 0;
	MAPIOBJECT *mo = NULL;
	ULONG ulAttachUniqueId = 0;
	ULONG ulRecipUniqueId = 0;

	// delProps contains all the available property tag
	ec = EcFillPropTags(lpsSaveObj, lpsMapiObject);

	// modProps contains all the props < 8K
	ec = EcFillPropValues(lpsSaveObj, lpsMapiObject);

	// delete stays false, unique id is set upon allocation
	lpsMapiObject->ulObjId = lpsSaveObj->ulServerId;
	lpsMapiObject->ulObjType = lpsSaveObj->ulObjType;

	// children
	for (i = 0; i < lpsSaveObj->__size; ++i) {
		switch (lpsSaveObj->__ptr[i].ulObjType) {
		case MAPI_ATTACH:
			AllocNewMapiObject(ulAttachUniqueId++, lpsSaveObj->__ptr[i].ulServerId, lpsSaveObj->__ptr[i].ulObjType, &mo);
			break;
		case MAPI_MAILUSER:
		case MAPI_DISTLIST:
			AllocNewMapiObject(ulRecipUniqueId++, lpsSaveObj->__ptr[i].ulServerId, lpsSaveObj->__ptr[i].ulObjType, &mo);
			break;
		default:
			AllocNewMapiObject(0, lpsSaveObj->__ptr[i].ulServerId, lpsSaveObj->__ptr[i].ulObjType, &mo);
			break;
		}

		ECSoapObjectToMapiObject(&lpsSaveObj->__ptr[i], mo);
		lpsMapiObject->lstChildren->insert(mo);
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
	struct notifySubscribe sNotSubscribe = {0};

	if (m_ulConnection) {
		// Register notification
		sNotSubscribe.ulConnection = m_ulConnection;
		sNotSubscribe.ulEventMask = m_ulEventMask;

		sNotSubscribe.sKey.__size = m_sEntryId.__size;
		sNotSubscribe.sKey.__ptr = m_sEntryId.__ptr;
	}

	LockSoap();

	if (!lppsMapiObject) {
		ASSERT(FALSE);
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	if (*lppsMapiObject) {
		// memleak detected
		ASSERT(FALSE);
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	START_SOAP_CALL
	{
		if (SOAP_OK != lpCmd->ns__loadObject(ecSessionId, m_sEntryId, ((m_ulConnection == 0) || m_bSubscribed)?NULL:&sNotSubscribe, m_ulFlags | 0x80000000, &sResponse))
			er = KCERR_NETWORK_ERROR;
		else
			er = sResponse.er;
	}
	//END_SOAP_CALL
	if(er == KCERR_END_OF_SESSION) { if(m_lpTransport->HrReLogon() == hrSuccess) goto retry; }
	hr = kcerr_to_mapierr(er, MAPI_E_NOT_FOUND);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE)	// Store does not exist on this server
		hr = MAPI_E_UNCONFIGURED;			// Force a reconfigure
	if(hr != hrSuccess)
		goto exit;

	AllocNewMapiObject(0, 0, 0, &lpsMapiObject); // ulObjType, ulObjId and ulUniqueId are unknown here

	ECSoapObjectToMapiObject(&sResponse.sSaveObject, lpsMapiObject);

	*lppsMapiObject = lpsMapiObject;
	
	m_bSubscribed = m_ulConnection != 0;

exit:
	UnLockSoap();

	return hr;
}

IECPropStorage* WSMAPIPropStorage::GetServerStorage() {
	return &this->m_xECPropStorage;				// I am the server storage
}


//FIXME: one lock/unlock function
HRESULT WSMAPIPropStorage::LockSoap()
{
	pthread_mutex_lock(lpDataLock);
	return erSuccess;
}

HRESULT WSMAPIPropStorage::UnLockSoap()
{
	//Clean up data create with soap_malloc
	if(lpCmd->soap) {
		soap_destroy(lpCmd->soap);
		soap_end(lpCmd->soap);
	}

	pthread_mutex_unlock(lpDataLock);
	return erSuccess;
}

HRESULT WSMAPIPropStorage::HrSetSyncId(ULONG ulSyncId) {
	m_ulSyncId = ulSyncId;
	return hrSuccess;
}

// Called when the session ID has changed
HRESULT WSMAPIPropStorage::Reload(void *lpParam, ECSESSIONID sessionId) {
	WSMAPIPropStorage *lpThis = (WSMAPIPropStorage *)lpParam;
	lpThis->ecSessionId = sessionId;

	return hrSuccess;
}


////////////////////////////////////////////////
// Interface IECPropStorage
//

ULONG WSMAPIPropStorage::xECPropStorage::AddRef()
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->AddRef();
}

ULONG WSMAPIPropStorage::xECPropStorage::Release()
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->Release();
}

HRESULT WSMAPIPropStorage::xECPropStorage::QueryInterface(REFIID refiid , void** lppInterface)
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->QueryInterface(refiid, lppInterface);
}

HRESULT WSMAPIPropStorage::xECPropStorage::HrReadProps(LPSPropTagArray *lppPropTags,ULONG *cValues, LPSPropValue *lppValues)
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->HrReadProps(lppPropTags,cValues, lppValues);
}
			
HRESULT WSMAPIPropStorage::xECPropStorage::HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue)
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->HrLoadProp(ulObjId, ulPropTag, lppsPropValue);
}

HRESULT WSMAPIPropStorage::xECPropStorage::HrWriteProps(ULONG cValues, LPSPropValue lpValues, ULONG ulFlags)
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->HrWriteProps(cValues, lpValues, ulFlags);
}	

HRESULT WSMAPIPropStorage::xECPropStorage::HrDeleteProps(LPSPropTagArray lpsPropTagArray)
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->HrDeleteProps(lpsPropTagArray);
}

HRESULT WSMAPIPropStorage::xECPropStorage::HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject)
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->HrSaveObject(ulFlags, lpsMapiObject);
}

HRESULT WSMAPIPropStorage::xECPropStorage::HrLoadObject(MAPIOBJECT **lppsMapiObject)
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->HrLoadObject(lppsMapiObject);
}

IECPropStorage* WSMAPIPropStorage::xECPropStorage::GetServerStorage()
{
	METHOD_PROLOGUE_(WSMAPIPropStorage, ECPropStorage);
	return pThis->GetServerStorage();
}
