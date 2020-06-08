/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <string>
#include <vector>
#include <mapispi.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
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
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT PrepareRecips(unsigned int flags, const SPropTagArray *props, ADRLIST *recips) override;

private:
	HRESULT AddFolder(const wchar_t *display_name, ULONG cbStore, LPBYTE lpStore, ULONG cbFolder, LPBYTE lpFolder);
	HRESULT ClearFolderList();

	KC::object_ptr<IMAPISupport> m_lpMAPISup;
	GUID				m_ABPGuid;

	std::vector<zcabFolderEntry> m_lFolders;
	ALLOC_WRAP_FRIEND;
};
