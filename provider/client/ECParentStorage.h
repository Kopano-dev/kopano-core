/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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

class ECParentStorage final : public KC::ECUnknown, public IECPropStorage {
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
	virtual HRESULT HrLoadProp(ULONG obj_id, ULONG proptag, SPropValue **) override;
	// Save complete object, deletes/adds/modifies/creates
	virtual HRESULT HrSaveObject(ULONG flags, MAPIOBJECT *to) override;
	// Load complete object, deletes/adds/modifies/creates
	virtual HRESULT HrLoadObject(MAPIOBJECT **) override;

	// Returns the correct storage which can connect to the server
	virtual IECPropStorage *GetServerStorage() override { return m_lpServerStorage; }

private:
	KC::object_ptr<ECGenericProp> m_lpParentObject;
	unsigned int m_ulObjId, m_ulUniqueId;
	KC::object_ptr<IECPropStorage> m_lpServerStorage;
	ALLOC_WRAP_FRIEND;
};

#endif
