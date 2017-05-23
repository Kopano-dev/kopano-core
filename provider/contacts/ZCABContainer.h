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

#ifndef ZCABCONTAINER_H
#define ZCABCONTAINER_H

#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include <mapispi.h>
#include <mapidefs.h>
#include "ZCABLogon.h"
#include "ZCABData.h"
#include "ZCMAPIProp.h"

/* should be derived from IMAPIProp, but since we don't do anything with those functions, let's skip the red tape. */
class ZCABContainer _kc_final : public ECUnknown {
protected:
	ZCABContainer(std::vector<zcabFolderEntry> *lpFolders, IMAPIFolder *lpContacts, LPMAPISUP lpMAPISup, void *lpProvider, const char *szClassName);
	virtual ~ZCABContainer();

private:
	HRESULT MakeWrappedEntryID(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulObjType, ULONG ulOffset, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);

public:
	static HRESULT	Create(std::vector<zcabFolderEntry> *lpFolders, IMAPIFolder *lpContacts, LPMAPISUP lpMAPISup, void* lpProvider, ZCABContainer **lppABContainer);
	static HRESULT	Create(IMessage *lpContact, ULONG cbEntryID, LPENTRYID lpEntryID, LPMAPISUP lpMAPISup, ZCABContainer **lppABContainer);

	HRESULT GetFolderContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	HRESULT GetDistListContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);

	// IUnknown
	virtual HRESULT	QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

	// IABContainer
	virtual HRESULT CreateEntry(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulCreateFlags, LPMAPIPROP* lppMAPIPropEntry);
	virtual HRESULT CopyEntries(LPENTRYLIST lpEntries, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, ULONG ulFlags);
	virtual HRESULT DeleteEntries(LPENTRYLIST lpEntries, ULONG ulFlags);
	virtual HRESULT ResolveNames(const SPropTagArray *lpPropTagArray, ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList);

	// From IMAPIContainer
	virtual HRESULT GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk);
	virtual HRESULT SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags);
	virtual HRESULT GetSearchCriteria(ULONG ulFlags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState);

	// very limited IMAPIProp, passed to ZCMAPIProp for m_lpDistList.
	virtual HRESULT GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray);
	virtual HRESULT GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray);

private:
	class xABContainer _kc_final : public IABContainer {
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IDistList.hpp>
		#include <kopano/xclsfrag/IABContainer.hpp>
		#include <kopano/xclsfrag/IMAPIContainer.hpp>
		#include <kopano/xclsfrag/IMAPIProp.hpp> /* mostly MAPI_E_NO_SUPPORT */
	} m_xABContainer;
	class xDistList _kc_final : public IDistList {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IMAPIContainer.hpp>
		#include <kopano/xclsfrag/IMAPIProp.hpp> /* mostly MAPI_E_NO_SUPPORT */
		#include <kopano/xclsfrag/IABContainer.hpp>
	} m_xDistList;

private:
	/* reference to ZCABLogon .. ZCABLogon needs to live because of this, so AddChild */
	std::vector<zcabFolderEntry> *m_lpFolders;
	IMAPIFolder *m_lpContactFolder;
	LPMAPISUP m_lpMAPISup;
	void *m_lpProvider;

	/* distlist version of this container */
	IMAPIProp *m_lpDistList = nullptr;
	ALLOC_WRAP_FRIEND;
};

#endif
