/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef WSMAPIPROPSTORAGE_H
#define WSMAPIPROPSTORAGE_H

#include <mutex>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include "IECPropStorage.h"
#include <kopano/kcodes.h>
#include "WSTransport.h"
#include <mapi.h>
#include <mapispi.h>

namespace KC {
class convert_context;
}

class WSMAPIPropStorage final : public KC::ECUnknown, public IECPropStorage {
protected:
	WSMAPIPropStorage(ULONG peid_size, const ENTRYID *parent_eid, ULONG eid_size, const ENTRYID *eid, ULONG flags, KC::ECSESSIONID, unsigned int srv_caps, WSTransport *);
	virtual ~WSMAPIPropStorage();

public:
	static HRESULT Create(ULONG peid_size, const ENTRYID *parent_eid, ULONG eid_size, const ENTRYID *eid, ULONG flags, KC::ECSESSIONID, unsigned int srv_caps, WSTransport *, WSMAPIPropStorage **);
	virtual HRESULT QueryInterface(const IID &, void **) override;

	// For ICS
	virtual HRESULT HrSetSyncId(ULONG ulSyncId);

	// Register advise on load object
	virtual HRESULT RegisterAdvise(ULONG ulEventMask, ULONG ulConnection);

	virtual HRESULT GetEntryIDByRef(ULONG *lpcbEntryID, LPENTRYID *lppEntryID);

private:
	// Get a single (large) property
	virtual HRESULT HrLoadProp(ULONG obj_id, ULONG proptag, SPropValue **) override;

	// Save complete object to server
	virtual HRESULT HrSaveObject(ULONG flags, MAPIOBJECT *to) override;

	// Load complete object from server
	virtual HRESULT HrLoadObject(MAPIOBJECT **) override;
	virtual IECPropStorage *GetServerStorage() override { return this; }

	/* very private */
	virtual KC::ECRESULT EcFillPropTags(const struct saveObject *, MAPIOBJECT *);
	virtual KC::ECRESULT EcFillPropValues(const struct saveObject *, MAPIOBJECT *);
	virtual HRESULT HrMapiObjectToSoapObject(const MAPIOBJECT *, struct saveObject *, KC::convert_context *);
	virtual HRESULT HrUpdateSoapObject(const MAPIOBJECT *, struct saveObject *, KC::convert_context *);
	virtual void    DeleteSoapObject(struct saveObject *lpSaveObj);
	virtual HRESULT HrUpdateMapiObject(MAPIOBJECT *, const struct saveObject *);
	virtual KC::ECRESULT ECSoapObjectToMapiObject(const struct saveObject *, MAPIOBJECT *);
	static HRESULT Reload(void *parm, KC::ECSESSIONID);

	/* ECParentStorage may access my functions (used to read PR_ATTACH_DATA_BIN chunks through HrLoadProp()) */
	friend class ECParentStorage;

private:
	entryId m_sEntryId, m_sParentEntryId;
	KC::ECSESSIONID ecSessionId;
	unsigned int	ulServerCapabilities;
	ULONG m_ulSyncId = 0, m_ulConnection = 0, m_ulEventMask = 0;
	unsigned int m_ulFlags, m_ulSessionReloadCallback;
	KC::object_ptr<WSTransport> m_lpTransport;
	bool m_bSubscribed = false;
	ALLOC_WRAP_FRIEND;
};

#endif
