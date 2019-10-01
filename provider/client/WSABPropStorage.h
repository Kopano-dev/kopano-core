/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef WSABPROPSTORAGE_H
#define WSABPROPSTORAGE_H

#include <mutex>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include "IECPropStorage.h"
#include <kopano/kcodes.h>
#include <kopano/zcdefs.h>
#include "ECABContainer.h"
#include "WSTableView.h"
#include "WSTransport.h"
#include <mapi.h>
#include <mapispi.h>

class WSABPropStorage KC_FINAL_OPG :
    public KC::ECUnknown, public IECPropStorage {
protected:
	WSABPropStorage(ULONG eid_size, const ENTRYID *, KC::ECSESSIONID, WSTransport *);
	virtual ~WSABPropStorage();

public:
	static HRESULT Create(ULONG eid_size, const ENTRYID *, KC::ECSESSIONID, WSTransport *, WSABPropStorage **);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	static HRESULT Reload(void *parm, KC::ECSESSIONID);

private:
	// Get a single (large) property
	virtual HRESULT HrLoadProp(unsigned int obj_id, unsigned int proptag, SPropValue **) override;
	// Save complete object to disk
	virtual HRESULT HrSaveObject(unsigned int flags, MAPIOBJECT *) override;
	// Load complete object from disk
	virtual HRESULT HrLoadObject(MAPIOBJECT **) override;
	virtual IECPropStorage *GetServerStorage() override { return this; }

private:
	entryId			m_sEntryId;
	KC::ECSESSIONID ecSessionId;
	WSTransport*	m_lpTransport;
	ULONG			m_ulSessionReloadCallback;
	ALLOC_WRAP_FRIEND;
};

class WSABTableView KC_FINAL_OPG : public WSTableView {
	public:
	static HRESULT Create(ULONG type, ULONG flags, KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, ECABLogon *, WSTransport *, WSTableView **);
	virtual	HRESULT	QueryInterface(const IID &, void **) override;

	protected:
	WSABTableView(ULONG type, ULONG flags, KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, ECABLogon *, WSTransport *);
	ALLOC_WRAP_FRIEND;
};

#endif
