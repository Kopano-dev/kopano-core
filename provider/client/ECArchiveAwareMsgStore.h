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

#ifndef ECARCHIVEAWAREMSGSTORE_H
#define ECARCHIVEAWAREMSGSTORE_H

#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include "ECMsgStore.h"
#include <kopano/ECGuid.h>

#include <list>
#include <vector>
#include <map>

class ECMessage;

class _kc_export_dycast ECArchiveAwareMsgStore _kc_final : public ECMsgStore {
public:
	_kc_hidden ECArchiveAwareMsgStore(char *profname, LPMAPISUP, WSTransport *, BOOL modify, ULONG profflags, BOOL is_spooler, BOOL is_dfl_store, BOOL offline_store);
	_kc_hidden static HRESULT Create(char *profname, LPMAPISUP, WSTransport *, BOOL modify, ULONG profflags, BOOL is_spooler, BOOL is_dfl_store, BOOL offline_store, ECMsgStore **ret);
	_kc_hidden virtual HRESULT OpenEntry(ULONG eid_size, LPENTRYID eid, LPCIID, ULONG flags, ULONG *obj_type, LPUNKNOWN *ret);
	_kc_hidden virtual HRESULT OpenItemFromArchive(LPSPropValue propstore_eids, LPSPropValue propitem_eids, ECMessage **ret);

private:
	typedef std::list<SBinary *> BinaryList;
	typedef BinaryList::iterator	BinaryListIterator;
	typedef KCHL::object_ptr<ECMsgStore, IID_ECMsgStore> ECMsgStorePtr;
	typedef std::vector<BYTE>		EntryID;
	typedef std::map<EntryID, ECMsgStorePtr>			MsgStoreMap;

	_kc_hidden HRESULT CreateCacheBasedReorderedList(SBinaryArray b_store_eids, SBinaryArray b_item_eids, BinaryList *store_eids, BinaryList *item_eids);
	_kc_hidden HRESULT GetArchiveStore(LPSBinary store_eid, ECMsgStore **ret);

	MsgStoreMap	m_mapStores;
};

#endif // ndef ECARCHIVEAWAREMSGSTORE_H
