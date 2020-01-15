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
#include <kopano/memory.hpp>
#include "ECABContainer.h"
#include "WSTableView.h"
#include "WSTransport.h"
#include <mapi.h>
#include <mapispi.h>

class WSABPropStorage final : public KC::ECUnknown, public IECPropStorage {
protected:
	WSABPropStorage(ULONG eid_size, const ENTRYID *, KC::ECSESSIONID, WSTransport *);
	virtual ~WSABPropStorage();

public:
	static HRESULT Create(ULONG eid_size, const ENTRYID *, KC::ECSESSIONID, WSTransport *, WSABPropStorage **);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	static HRESULT Reload(void *parm, KC::ECSESSIONID);

private:
	// Get a single (large) property
	virtual HRESULT HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue);
	// Save complete object to disk
	virtual HRESULT HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpsMapiObject);
	// Load complete object from disk
	virtual HRESULT HrLoadObject(MAPIOBJECT **lppsMapiObject);
	virtual IECPropStorage *GetServerStorage() override { return this; }

private:
	entryId			m_sEntryId;
	KC::ECSESSIONID ecSessionId;
	KC::object_ptr<WSTransport> m_lpTransport;
	ULONG			m_ulSessionReloadCallback;
	ALLOC_WRAP_FRIEND;
};

class WSABTableView final : public WSTableView {
	public:
	static HRESULT Create(ULONG type, ULONG flags, KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, ECABLogon *, WSTransport *, WSTableView **);
	virtual	HRESULT	QueryInterface(const IID &, void **) override;

	protected:
	WSABTableView(ULONG type, ULONG flags, KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, ECABLogon *, WSTransport *);
	ALLOC_WRAP_FRIEND;
};

#endif
