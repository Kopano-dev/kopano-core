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

#ifndef ZCABLOGON_H
#define ZCABLOGON_H

#include <kopano/zcdefs.h>
#include <vector>
#include <mapispi.h>
#include <kopano/ECUnknown.h>

struct zcabFolderEntry {
	ULONG cbStore;
	LPBYTE lpStore;
	ULONG cbFolder;
	LPBYTE lpFolder;
	std::wstring strwDisplayName;
};

class ZCABLogon _kc_final : public ECUnknown {
protected:
	ZCABLogon(LPMAPISUP lpMAPISup, ULONG ulProfileFlags, GUID *lpGUID);
	virtual ~ZCABLogon();

public:
	static  HRESULT Create(LPMAPISUP lpMAPISup, ULONG ulProfileFlags, GUID *lpGUID, ZCABLogon **lppZCABLogon);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Logoff(ULONG ulFlags);
	virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk);
	virtual HRESULT CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult);
	virtual HRESULT Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection);
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPMAPISTATUS * lppMAPIStatus);
	virtual HRESULT OpenTemplateID(ULONG cbTemplateID, LPENTRYID lpTemplateID, ULONG ulTemplateFlags, LPMAPIPROP lpMAPIPropData, LPCIID lpInterface, LPMAPIPROP * lppMAPIPropNew, LPMAPIPROP lpMAPIPropSibling);
	virtual HRESULT GetOneOffTable(ULONG ulFlags, LPMAPITABLE * lppTable);
	virtual HRESULT PrepareRecips(ULONG ulFlags, LPSPropTagArray lpPropTagArray, LPADRLIST lpRecipList);

private:
	class xABLogon _kc_final : public IABLogon {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IABLogon.hpp>
	} m_xABLogon;

private:
	HRESULT AddFolder(const WCHAR* lpwDisplayName, ULONG cbStore, LPBYTE lpStore, ULONG cbFolder, LPBYTE lpFolder);
	HRESULT ClearFolderList();

	LPMAPISUP			m_lpMAPISup;
	GUID				m_ABPGuid;

	std::vector<zcabFolderEntry> m_lFolders;
};

#endif
