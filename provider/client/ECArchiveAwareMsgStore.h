/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECARCHIVEAWAREMSGSTORE_H
#define ECARCHIVEAWAREMSGSTORE_H

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include "ECMsgStore.h"
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <list>
#include <vector>
#include <map>

class ECMessage;

class _kc_export_dycast ECArchiveAwareMsgStore final : public ECMsgStore {
public:
	_kc_hidden ECArchiveAwareMsgStore(const char *profname, IMAPISupport *, WSTransport *, BOOL modify, ULONG profflags, BOOL is_spooler, BOOL is_dfl_store, BOOL offline_store);
	_kc_hidden static HRESULT Create(const char *profname, IMAPISupport *, WSTransport *, BOOL modify, ULONG profflags, BOOL is_spooler, BOOL is_dfl_store, BOOL offline_store, ECMsgStore **ret);
	_kc_hidden virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	_kc_hidden virtual HRESULT OpenItemFromArchive(LPSPropValue propstore_eids, LPSPropValue propitem_eids, ECMessage **ret);

private:
	typedef std::list<SBinary *> BinaryList;
	typedef BinaryList::iterator	BinaryListIterator;
	typedef KC::object_ptr<ECMsgStore> ECMsgStorePtr;
	typedef std::vector<BYTE>		EntryID;
	typedef std::map<EntryID, ECMsgStorePtr>			MsgStoreMap;

	_kc_hidden HRESULT CreateCacheBasedReorderedList(SBinaryArray b_store_eids, SBinaryArray b_item_eids, BinaryList *store_eids, BinaryList *item_eids);
	_kc_hidden HRESULT GetArchiveStore(LPSBinary store_eid, ECMsgStore **ret);

	MsgStoreMap	m_mapStores;
	ALLOC_WRAP_FRIEND;
};

#endif // ndef ECARCHIVEAWAREMSGSTORE_H
