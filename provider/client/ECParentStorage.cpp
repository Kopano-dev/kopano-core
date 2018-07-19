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
#include <new>
#include <kopano/platform.h>
#include "ECParentStorage.h"
#include "Mem.h"
#include <kopano/ECGuid.h>
#include <mapiutil.h>
#include "SOAPUtils.h"
#include "WSUtil.h"
#include <kopano/Util.h>

ECParentStorage::ECParentStorage(ECGenericProp *lpParentObject,
    ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage) :
	m_lpParentObject(lpParentObject), m_ulObjId(ulObjId),
	m_ulUniqueId(ulUniqueId), m_lpServerStorage(lpServerStorage)
{}

HRESULT ECParentStorage::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECParentStorage, this);
	REGISTER_INTERFACE2(IECPropStorage, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECParentStorage::Create(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage, ECParentStorage **lppParentStorage)
{
	return alloc_wrap<ECParentStorage>(lpParentObject, ulUniqueId, ulObjId,
	       lpServerStorage).put(lppParentStorage);
}

HRESULT ECParentStorage::HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue)
{
	if (m_lpServerStorage == NULL)
		return MAPI_E_NOT_FOUND;
	return m_lpServerStorage->HrLoadProp(ulObjId, ulPropTag, lppsPropValue);
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
	if (m_lpParentObject == nullptr)
		return MAPI_E_INVALID_OBJECT;

	ECMapiObjects::const_iterator iterSObj;
	scoped_rlock lock(m_lpParentObject->m_hMutexMAPIObject);
	if (m_lpParentObject->m_sMapiObject == NULL)
		return MAPI_E_INVALID_OBJECT;

	// type is either attachment or message-in-message
	{
		MAPIOBJECT find(MAPI_MESSAGE, m_ulUniqueId);
		MAPIOBJECT findAtt(MAPI_ATTACH, m_ulUniqueId);
		iterSObj = m_lpParentObject->m_sMapiObject->lstChildren.find(&find);
		if (iterSObj == m_lpParentObject->m_sMapiObject->lstChildren.cend())
			iterSObj = m_lpParentObject->m_sMapiObject->lstChildren.find(&findAtt);
	}
	if (iterSObj == m_lpParentObject->m_sMapiObject->lstChildren.cend())
		return MAPI_E_NOT_FOUND;
	// make a complete copy of the object, because of close / re-open
	*lppsMapiObject = new MAPIOBJECT(**iterSObj);
	return hrSuccess;
}
