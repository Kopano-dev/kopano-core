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

#ifndef ECPARENTSTORAGE_H
#define ECPARENTSTORAGE_H

/* This PropStorate class writes the data to the parent object, so this is only used in attachments and msg-in-msg objects
   It reads from the saved data in the parent
*/

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include "IECPropStorage.h"

#include "ECGenericProp.h"
#include "WSMAPIPropStorage.h"

#include <kopano/kcodes.h>
#include "soapKCmdProxy.h"

#include <mapi.h>
#include <mapispi.h>

class ECParentStorage _kc_final : public ECUnknown {
	/*
	  lpParentObject:	The property object of the parent (eg. ECMessage for ECAttach)
	  ulUniqueId:		A unique client-side to find the object in the children list on the parent (PR_ATTACH_NUM (attachments) or PR_ROWID (recipients))
	  ulObjId:			The hierarchy id on the server (0 for a new item)
	  lpServerStorage:	A WSMAPIPropStorage interface which has the communication line to the server
	 */
protected:
	ECParentStorage(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage);
	virtual ~ECParentStorage();

public:
	static HRESULT Create(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage, ECParentStorage **lppParentStorage);

	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

private:

	// Get a list of the properties
	virtual HRESULT HrReadProps(LPSPropTagArray *lppPropTags, ULONG *cValues, LPSPropValue *ppValues);

	// Get a single (large) property
	virtual HRESULT HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue);

	// Not implemented
	virtual	HRESULT	HrWriteProps(ULONG cValues, LPSPropValue pValues, ULONG ulFlags = 0);

	// Not implemented
	virtual HRESULT HrDeleteProps(LPSPropTagArray lpsPropTagArray);

	// Save complete object, deletes/adds/modifies/creates
	virtual HRESULT HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);

	// Load complete object, deletes/adds/modifies/creates
	virtual HRESULT HrLoadObject(MAPIOBJECT **lppsMapiObject);
	
	// Returns the correct storage which can connect to the server
	virtual IECPropStorage* GetServerStorage();

public:
	class xECPropStorage _kc_final : public IECPropStorage {
		#include <kopano/xclsfrag/IECUnknown.hpp>
		#include <kopano/xclsfrag/IECPropStorage.hpp>
	} m_xECPropStorage;

private:
	ECGenericProp *m_lpParentObject;
	ULONG m_ulObjId;
	ULONG m_ulUniqueId;
	IECPropStorage *m_lpServerStorage;
};


#endif
