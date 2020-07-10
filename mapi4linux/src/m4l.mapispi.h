/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/memory.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <kopano/ECUnknown.h>
#include "m4l.mapisvc.h"
#include <mapispi.h>
#include <mapix.h>
#include <edkmdb.h>

struct M4LSUPPORTADVISE {
	M4LSUPPORTADVISE(KC::memory_ptr<NOTIFKEY> &&k, unsigned int m,
	    unsigned int f, IMAPIAdviseSink *s) :
		lpKey(std::move(k)), ulEventMask(m), ulFlags(f), lpAdviseSink(s)
	{}

	KC::memory_ptr<NOTIFKEY> lpKey;
	unsigned int ulEventMask, ulFlags;
	LPMAPIADVISESINK lpAdviseSink;
};

class M4LMAPIGetSession : public KC::ECUnknown, public IMAPIGetSession {
private:
	KC::object_ptr<IMAPISession> session;

public:
	M4LMAPIGetSession(LPMAPISESSION new_session);

	// IMAPIGetSession
	virtual HRESULT GetMAPISession(IUnknown **ses) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};

class M4LMAPISupport : public KC::ECUnknown, public IMAPISupport {
private:
	LPMAPISESSION		session;
	std::unique_ptr<MAPIUID> lpsProviderUID;
	std::shared_ptr<SVCService> service;
	std::mutex m_advises_mutex;
	std::map<unsigned int, M4LSUPPORTADVISE> m_advises;
	ULONG m_connections = 0;

public:
	M4LMAPISupport(IMAPISession *, MAPIUID *prov_uid, std::shared_ptr<SVCService> &&);
	virtual ~M4LMAPISupport();
	virtual HRESULT GetMemAllocRoutines(ALLOCATEBUFFER **, ALLOCATEMORE **, FREEBUFFER **) override;
	virtual HRESULT Subscribe(const NOTIFKEY *key, ULONG evt_mask, ULONG flags, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unsubscribe(unsigned int conn) override;
	virtual HRESULT Notify(const NOTIFKEY *key, ULONG nnotifs, NOTIFICATION *, ULONG *flags) override;
	virtual HRESULT ModifyStatusRow(ULONG nvals, const SPropValue *, ULONG flags) override;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, unsigned int flags, IProfSect **) override;
	virtual HRESULT NewUID(MAPIUID *) override;
	virtual HRESULT CreateOneOff(const TCHAR *name, const TCHAR *addrtype, const TCHAR *addr, ULONG flags, ULONG *eid_size, ENTRYID **) override;
	virtual HRESULT SetProviderUID(const MAPIUID *, ULONG flags) override;
	virtual HRESULT CompareEntryIDs(unsigned int asize, const ENTRYID *a, unsigned int bsize, const ENTRYID *b, unsigned int cmp_flags, unsigned int *result) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT CopyMessages(const IID *src_intf, void *src_fld, const ENTRYLIST *msglist, const IID *dst_intf, void *dst_fld, ULONG_PTR ui_param, IMAPIProgress *, ULONG flags) override;
	virtual HRESULT CopyFolder(const IID *src_intf, void *src_fld, unsigned int eid_size, const ENTRYID *eid, const IID *dst_intf, void *dst_fld, const TCHAR *newname, ULONG_PTR ui_param, IMAPIProgress *, unsigned int flags) override;
	virtual HRESULT DoCopyTo(const IID *src_intf, void *src_obj, unsigned int nexcl, const IID *excl, const SPropTagArray *exclprop, unsigned int ui_param, IMAPIProgress *, const IID *dst_intf, void *dst_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT DoCopyProps(const IID *src_intf, void *src_obj, const SPropTagArray *inclprop, unsigned int ui_param, IMAPIProgress *, const IID *dst_intf, void *dst_obj, unsigned int flags, SPropProblemArray **) override;
	virtual HRESULT ExpandRecips(IMessage *, unsigned int *flags) override;
	virtual HRESULT DoSentMail(unsigned int flags, IMessage *) override;
	virtual HRESULT WrapStoreEntryID(unsigned int orig_size, const ENTRYID *orig_eid, unsigned int *wrap_size, ENTRYID **wrap_eid) override;
	virtual HRESULT ModifyProfile(unsigned int flags) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
};
