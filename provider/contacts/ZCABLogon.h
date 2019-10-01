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
#include <kopano/zcdefs.h>

struct zcabFolderEntry {
	ULONG cbStore;
	LPBYTE lpStore;
	ULONG cbFolder;
	LPBYTE lpFolder;
	std::wstring strwDisplayName;
};

class ZCABLogon KC_FINAL_OPG : public KC::ECUnknown, public IABLogon {
protected:
	ZCABLogon(IMAPISupport *, ULONG profile_flags, const GUID *);
	virtual ~ZCABLogon();

public:
	static HRESULT Create(IMAPISupport *, ULONG profile_flags, const GUID *, ZCABLogon **);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT GetLastError(HRESULT, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT Logoff(unsigned int flags) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT CompareEntryIDs(unsigned int asize, const ENTRYID *a, unsigned int bsize, const ENTRYID *b, unsigned int cmp_flags, unsigned int *result) override;
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(unsigned int conn) override;
	virtual HRESULT OpenStatusEntry(const IID *intf, unsigned int flags, unsigned int *obj_type, IMAPIStatus **) override;
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) override;
	virtual HRESULT GetOneOffTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT PrepareRecips(unsigned int flags, const SPropTagArray *props, ADRLIST *recips) override;

private:
	HRESULT AddFolder(const wchar_t *display_name, ULONG cbStore, LPBYTE lpStore, ULONG cbFolder, LPBYTE lpFolder);
	HRESULT ClearFolderList();

	LPMAPISUP			m_lpMAPISup;
	GUID				m_ABPGuid;

	std::vector<zcabFolderEntry> m_lFolders;
	ALLOC_WRAP_FRIEND;
};

#endif
