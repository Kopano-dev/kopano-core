/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ZCABLOGON_H
#define ZCABLOGON_H

#include <vector>
#include <mapispi.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>

struct zcabFolderEntry {
	ULONG cbStore;
	LPBYTE lpStore;
	ULONG cbFolder;
	LPBYTE lpFolder;
	std::wstring strwDisplayName;
};

class ZCABLogon final : public KC::ECUnknown, public IABLogon {
protected:
	ZCABLogon(IMAPISupport *, ULONG profile_flags, const GUID *);
	virtual ~ZCABLogon();

public:
	static HRESULT Create(IMAPISupport *, ULONG profile_flags, const GUID *, ZCABLogon **);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT Logoff(ULONG ulFlags);
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result);
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPMAPISTATUS * lppMAPIStatus);
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) override;
	virtual HRESULT GetOneOffTable(ULONG ulFlags, LPMAPITABLE * lppTable);
	virtual HRESULT PrepareRecips(ULONG ulFlags, const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList);

private:
	HRESULT AddFolder(const WCHAR* lpwDisplayName, ULONG cbStore, LPBYTE lpStore, ULONG cbFolder, LPBYTE lpFolder);
	HRESULT ClearFolderList();

	LPMAPISUP			m_lpMAPISup;
	GUID				m_ABPGuid;

	std::vector<zcabFolderEntry> m_lFolders;
	ALLOC_WRAP_FRIEND;
};

#endif
