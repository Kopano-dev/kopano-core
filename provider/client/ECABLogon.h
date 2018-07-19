/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECABLOGON_H
#define ECABLOGON_H

#include <kopano/zcdefs.h>
#include <mapispi.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "ECNotifyClient.h"
#include "WSTransport.h"

using namespace KC;
class WSTransport;

class ECABLogon _kc_final : public ECUnknown, public IABLogon {
protected:
	ECABLogon(IMAPISupport *, WSTransport *, ULONG profile_flags, const GUID *);
	virtual ~ECABLogon();

public:
	static  HRESULT Create(IMAPISupport *, WSTransport *, ULONG profile_flags, const GUID *, ECABLogon **);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT GetLastError(HRESULT, ULONG flags, MAPIERROR **) override;
	virtual HRESULT Logoff(ULONG flags) override;
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) override;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) override;
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(ULONG conn) override;
	virtual HRESULT OpenStatusEntry(const IID *intf, ULONG flags, ULONG *objtype, IMAPIStatus **) override;
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) override;
	virtual HRESULT GetOneOffTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT PrepareRecips(ULONG flags, const SPropTagArray *, ADRLIST *recips) override;

	KC::object_ptr<IMAPISupport> m_lpMAPISup;
	KC::object_ptr<WSTransport> m_lpTransport;
	KC::object_ptr<ECNotifyClient> m_lpNotifyClient;
	GUID m_guid, m_ABPGuid;
	ALLOC_WRAP_FRIEND;
};

#endif // #ifndef ECABLOGON
