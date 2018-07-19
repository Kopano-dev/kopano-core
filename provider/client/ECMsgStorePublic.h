/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECMSGSTOREPUBLIC_H
#define ECMSGSTOREPUBLIC_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <mapispi.h>

#include <edkmdb.h>

#include "ECMsgStore.h"
#include "ClientUtil.h"
#include <kopano/ECMemTable.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>

class ECMsgStorePublic _kc_final : public ECMsgStore {
protected:
	ECMsgStorePublic(const char *profile, IMAPISupport *, WSTransport *, BOOL modify, ULONG profile_flags, BOOL is_spooler, BOOL offline_store);
public:
	static HRESULT GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);
	static HRESULT Create(const char *profile, IMAPISupport *, WSTransport *, BOOL modify, ULONG profile_flags, BOOL is_spooler, BOOL offline_store, ECMsgStore **);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT SetEntryId(ULONG eid_size, const ENTRYID *eid);
	HRESULT InitEntryIDs();
	HRESULT GetPublicEntryId(enumPublicEntryID ePublicEntryID, void *lpBase, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	HRESULT ComparePublicEntryId(enumPublicEntryID, ULONG eid_size, const ENTRYID *eid, ULONG *result);
	ECMemTable *GetIPMSubTree();

	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;

protected:	
	KC::memory_ptr<ENTRYID> m_lpIPMSubTreeID, m_lpIPMFavoritesID;
	KC::memory_ptr<ENTRYID> m_lpIPMPublicFoldersID;
	ULONG m_cIPMSubTreeID = 0, m_cIPMFavoritesID = 0;
	ULONG m_cIPMPublicFoldersID = 0;
	KC::object_ptr<ECMemTable> m_lpIPMSubTree; /* Built-in IPM subtree */
	KC::object_ptr<IMsgStore> m_lpDefaultMsgStore;

	HRESULT BuildIPMSubTree();
	// entryid : level
	ALLOC_WRAP_FRIEND;
};

#endif // #ifndef ECMSGSTOREPUBLIC_H
