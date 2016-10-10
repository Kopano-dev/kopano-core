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
#include "ECMsgStore.h"
#include <kopano/mapi_ptr/mapi_object_ptr.h>
#include <kopano/ECGuid.h>

#include <list>
#include <vector>
#include <map>

class ECMessage;

class ECArchiveAwareMsgStore _kc_final : public ECMsgStore {
public:
	ECArchiveAwareMsgStore(char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore);

	static HRESULT Create(char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL bIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore, ECMsgStore **lppECMsgStore);

	virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk);

	virtual HRESULT OpenItemFromArchive(LPSPropValue lpPropStoreEIDs, LPSPropValue lpPropItemEIDs, ECMessage **lppMessage);

private:
	typedef std::list<SBinary *> BinaryList;
	typedef BinaryList::iterator	BinaryListIterator;
	typedef mapi_object_ptr<ECMsgStore, IID_ECMsgStore>	ECMsgStorePtr;
	typedef std::vector<BYTE>		EntryID;
	typedef std::map<EntryID, ECMsgStorePtr>			MsgStoreMap;

	HRESULT CreateCacheBasedReorderedList(SBinaryArray sbaStoreEIDs, SBinaryArray sbaItemEIDs, BinaryList *lplstStoreEIDs, BinaryList *lplstItemEIDs);
	HRESULT GetArchiveStore(LPSBinary lpStoreEID, ECMsgStore **lppArchiveStore);

private:
	MsgStoreMap	m_mapStores;
};

#endif // ndef ECARCHIVEAWAREMSGSTORE_H
