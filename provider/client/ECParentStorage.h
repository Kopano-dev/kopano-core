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
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "IECPropStorage.h"

#include "ECGenericProp.h"
#include "WSMAPIPropStorage.h"

#include <kopano/kcodes.h>
#include <mapi.h>
#include <mapispi.h>

using namespace KC;

class ECParentStorage _kc_final : public ECUnknown, public IECPropStorage {
	/*
	  lpParentObject:	The property object of the parent (e.g. ECMessage for ECAttach)
	  ulUniqueId:		A unique client-side to find the object in the children list on the parent (PR_ATTACH_NUM (attachments) or PR_ROWID (recipients))
	  ulObjId:			The hierarchy id on the server (0 for a new item)
	  lpServerStorage:	A WSMAPIPropStorage interface which has the communication line to the server
	 */
protected:
	ECParentStorage(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage);

public:
	static HRESULT Create(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage, ECParentStorage **lppParentStorage);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

private:

	// Get a single (large) property
	virtual HRESULT HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue);

	// Save complete object, deletes/adds/modifies/creates
	virtual HRESULT HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);

	// Load complete object, deletes/adds/modifies/creates
	virtual HRESULT HrLoadObject(MAPIOBJECT **lppsMapiObject);
	
	// Returns the correct storage which can connect to the server
	virtual IECPropStorage* GetServerStorage();

private:
	KC::object_ptr<ECGenericProp> m_lpParentObject;
	ULONG m_ulObjId;
	ULONG m_ulUniqueId;
	KC::object_ptr<IECPropStorage> m_lpServerStorage;
	ALLOC_WRAP_FRIEND;
};


#endif
