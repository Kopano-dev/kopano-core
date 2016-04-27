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

#ifndef WSABPROPSTORAGE_H
#define WSABPROPSTORAGE_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include "IECPropStorage.h"

#include <kopano/kcodes.h>
#include "WSTransport.h"
#include "soapKCmdProxy.h"

#include <mapi.h>
#include <mapispi.h>
#include <pthread.h>

class WSABPropStorage : public ECUnknown
{

protected:
	WSABPropStorage(ULONG cbEntryId, LPENTRYID lpEntryId, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, WSTransport *lpTransport);
	virtual ~WSABPropStorage();

public:
	static HRESULT Create(ULONG cbEntryId, LPENTRYID lpEntryId, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, WSTransport *lpTransport, WSABPropStorage **lppPropStorage);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);
	
	static HRESULT Reload(void *lpParam, ECSESSIONID sessionId);
	
private:

	// Get a list of the properties
	virtual HRESULT HrReadProps(LPSPropTagArray *lppPropTags,ULONG *cValues, LPSPropValue *ppValues);

	// Get a single (large) property
	virtual HRESULT HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue);

	// Write all properties to disk (overwrites a property if it already exists)
	virtual	HRESULT	HrWriteProps(ULONG cValues, LPSPropValue pValues, ULONG ulFlags = 0);

	// Delete properties from file
	virtual HRESULT HrDeleteProps(LPSPropTagArray lpsPropTagArray);

	// Save complete object to disk
	virtual HRESULT HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);

	// Load complete object from disk
	virtual HRESULT HrLoadObject(MAPIOBJECT **lppsMapiObject);

	virtual IECPropStorage* GetServerStorage();

	virtual HRESULT LockSoap();
	virtual HRESULT UnLockSoap();

public:
	class xECPropStorage _zcp_final : public IECPropStorage {
		public:
			// IECUnknown
			virtual ULONG AddRef(void) _zcp_override;
			virtual ULONG Release(void) _zcp_override;
			virtual HRESULT QueryInterface(REFIID refiid , void **lppInterface) _zcp_override;

			// IECPropStorage
			virtual HRESULT HrReadProps(LPSPropTagArray *lppPropTags,ULONG *cValues, LPSPropValue *lppValues);
			virtual HRESULT HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue);
			virtual	HRESULT	HrWriteProps(ULONG cValues, LPSPropValue lpValues, ULONG ulFlags = 0);
			virtual HRESULT HrDeleteProps(LPSPropTagArray lpsPropTagArray);
			virtual HRESULT HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsSavedObject);
			virtual HRESULT HrLoadObject(MAPIOBJECT **lppsMapiObject);
			virtual IECPropStorage* GetServerStorage();
	}m_xECPropStorage;

private:
	entryId			m_sEntryId;
	KCmd*		lpCmd;
	pthread_mutex_t *lpDataLock;
	ECSESSIONID		ecSessionId;
	WSTransport*	m_lpTransport;
	ULONG			m_ulSessionReloadCallback;
};


#endif
