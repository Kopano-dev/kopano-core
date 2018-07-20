/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef WSABPROPSTORAGE_H
#define WSABPROPSTORAGE_H

#include <kopano/zcdefs.h>
#include <mutex>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include "IECPropStorage.h"
#include <kopano/kcodes.h>
#include "ECABLogon.h"
#include "WSTableView.h"
#include "WSTransport.h"
#include <mapi.h>
#include <mapispi.h>

class WSABPropStorage _kc_final : public ECUnknown, public IECPropStorage {
protected:
	WSABPropStorage(ULONG eid_size, const ENTRYID *, ECSESSIONID, WSTransport *);
	virtual ~WSABPropStorage();

public:
	static HRESULT Create(ULONG eid_size, const ENTRYID *, ECSESSIONID, WSTransport *, WSABPropStorage **);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	static HRESULT Reload(void *lpParam, ECSESSIONID sessionId);
	
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
	ECSESSIONID		ecSessionId;
	WSTransport*	m_lpTransport;
	ULONG			m_ulSessionReloadCallback;
	ALLOC_WRAP_FRIEND;
};

class WSABTableView _kc_final : public WSTableView {
	public:
	static HRESULT Create(ULONG type, ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *, ECABLogon *, WSTransport *, WSTableView **);
	virtual	HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

	protected:
	WSABTableView(ULONG type, ULONG flags, ECSESSIONID, ULONG eid_size, const ENTRYID *, ECABLogon *, WSTransport *);
	ALLOC_WRAP_FRIEND;
};

#endif
