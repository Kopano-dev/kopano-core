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

#ifndef ECMSGSTOREPUBLIC_H
#define ECMSGSTOREPUBLIC_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <mapispi.h>

#include <edkmdb.h>

#include "ECMsgStore.h"
#include "ClientUtil.h"
#include <kopano/ECMemTable.h>

class ECMsgStorePublic _kc_final : public ECMsgStore {
protected:
	ECMsgStorePublic(char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL bOfflineStore);
	~ECMsgStorePublic(void);

public:
	static HRESULT GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);
	static HRESULT	Create(char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL bOfflineStore, ECMsgStore **lppECMsgStore);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

public:
	virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk);
	virtual HRESULT SetEntryId(ULONG cbEntryId, LPENTRYID lpEntryId);

	HRESULT InitEntryIDs();
	HRESULT GetPublicEntryId(enumPublicEntryID ePublicEntryID, void *lpBase, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	HRESULT ComparePublicEntryId(enumPublicEntryID ePublicEntryID, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *lpulResult);

	ECMemTable *GetIPMSubTree();

	// Folder with the favorites links
	HRESULT GetDefaultShortcutFolder(IMAPIFolder** lppFolder);

	virtual HRESULT Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection);

protected:	
	ENTRYID *m_lpIPMSubTreeID = nullptr, *m_lpIPMFavoritesID = nullptr;
	ENTRYID *m_lpIPMPublicFoldersID = nullptr;
	ULONG m_cIPMSubTreeID = 0, m_cIPMFavoritesID = 0;
	ULONG m_cIPMPublicFoldersID = 0;
	ECMemTable *m_lpIPMSubTree = nullptr; // Build-in IPM subtree
	IMsgStore *m_lpDefaultMsgStore = nullptr;

	HRESULT BuildIPMSubTree();
	// entryid : level

};

#endif // #ifndef ECMSGSTOREPUBLIC_H
