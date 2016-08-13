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
#include "ECParentStorage.h"

#include "Mem.h"
#include <kopano/ECGuid.h>

#include <mapiutil.h>
#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/Util.h>

/*
 * ECParentStorage implementation
 */

ECParentStorage::ECParentStorage(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage)
{
	m_lpParentObject = lpParentObject;
	if (m_lpParentObject)
		m_lpParentObject->AddRef();

	m_ulObjId = ulObjId;
	m_ulUniqueId = ulUniqueId;

	m_lpServerStorage = lpServerStorage;
	if (m_lpServerStorage)
		m_lpServerStorage->AddRef();
}

ECParentStorage::~ECParentStorage()
{
	if (m_lpParentObject)
		m_lpParentObject->Release();

	if (m_lpServerStorage)
		m_lpServerStorage->Release();
}

HRESULT ECParentStorage::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE(IID_ECParentStorage, this);

	REGISTER_INTERFACE(IID_IECPropStorage, &this->m_xECPropStorage);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECParentStorage::Create(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage, ECParentStorage **lppParentStorage)
{
	ECParentStorage *lpParentStorage = NULL;

	lpParentStorage = new ECParentStorage(lpParentObject, ulUniqueId, ulObjId, lpServerStorage);
	return lpParentStorage->QueryInterface(IID_ECParentStorage, reinterpret_cast<void **>(lppParentStorage)); //FIXME: Use other interface
}

HRESULT ECParentStorage::HrReadProps(LPSPropTagArray *lppPropTags, ULONG *lpcValues, LPSPropValue *lppValues)
{
	// this call should disappear
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECParentStorage::HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue)
{
	if (m_lpServerStorage == NULL)
		return MAPI_E_NOT_FOUND;
	return m_lpServerStorage->HrLoadProp(ulObjId, ulPropTag, lppsPropValue);
}

HRESULT	ECParentStorage::HrWriteProps(ULONG cValues, LPSPropValue pValues, ULONG ulFlags)
{
	// this call should disappear
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECParentStorage::HrDeleteProps(LPSPropTagArray lpsPropTagArray)
{
	// this call should disappear
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECParentStorage::HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject)
{
	if (m_lpParentObject == NULL)
		return MAPI_E_INVALID_OBJECT;

	lpsMapiObject->ulUniqueId = m_ulUniqueId;
	lpsMapiObject->ulObjId = m_ulObjId;
	return m_lpParentObject->HrSaveChild(ulFlags, lpsMapiObject);
}

HRESULT ECParentStorage::HrLoadObject(MAPIOBJECT **lppsMapiObject)
{
	HRESULT hr = hrSuccess;
	ECMapiObjects::const_iterator iterSObj;

	if (!m_lpParentObject)
		return MAPI_E_INVALID_OBJECT;
		
	pthread_mutex_lock(&m_lpParentObject->m_hMutexMAPIObject);
		
	if (!m_lpParentObject->m_sMapiObject) {
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}

	// type is either attachment or message-in-message
	{
		MAPIOBJECT find(MAPI_MESSAGE, m_ulUniqueId);
		MAPIOBJECT findAtt(MAPI_ATTACH, m_ulUniqueId);
	    iterSObj = m_lpParentObject->m_sMapiObject->lstChildren->find(&find);
	    if(iterSObj == m_lpParentObject->m_sMapiObject->lstChildren->end())
    		iterSObj = m_lpParentObject->m_sMapiObject->lstChildren->find(&findAtt);
	}
    	
	if (iterSObj == m_lpParentObject->m_sMapiObject->lstChildren->end()) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// make a complete copy of the object, because of close / re-open
	*lppsMapiObject = new MAPIOBJECT(*iterSObj);

exit:
	pthread_mutex_unlock(&m_lpParentObject->m_hMutexMAPIObject);

	return hr;
}

IECPropStorage* ECParentStorage::GetServerStorage() {
	return m_lpServerStorage;
}

// Interface IECPropStorage
ULONG ECParentStorage::xECPropStorage::AddRef()
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->AddRef();
}

ULONG ECParentStorage::xECPropStorage::Release()
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->Release();
}

HRESULT ECParentStorage::xECPropStorage::QueryInterface(REFIID refiid , void** lppInterface)
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->QueryInterface(refiid, lppInterface);
}

HRESULT ECParentStorage::xECPropStorage::HrReadProps(LPSPropTagArray *lppPropTags,ULONG *cValues, LPSPropValue *lppValues)
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->HrReadProps(lppPropTags,cValues, lppValues);
}
			
HRESULT ECParentStorage::xECPropStorage::HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue)
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->HrLoadProp(ulObjId, ulPropTag, lppsPropValue);
}

HRESULT ECParentStorage::xECPropStorage::HrWriteProps(ULONG cValues, LPSPropValue lpValues, ULONG ulFlags)
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->HrWriteProps(cValues, lpValues, ulFlags);
}	

HRESULT ECParentStorage::xECPropStorage::HrDeleteProps(LPSPropTagArray lpsPropTagArray)
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->HrDeleteProps(lpsPropTagArray);
}

HRESULT ECParentStorage::xECPropStorage::HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject)
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->HrSaveObject(ulFlags, lpsMapiObject);
}

HRESULT ECParentStorage::xECPropStorage::HrLoadObject(MAPIOBJECT **lppsMapiObject)
{
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->HrLoadObject(lppsMapiObject);
}

IECPropStorage* ECParentStorage::xECPropStorage::GetServerStorage() {
	METHOD_PROLOGUE_(ECParentStorage, ECPropStorage);
	return pThis->GetServerStorage();
}
