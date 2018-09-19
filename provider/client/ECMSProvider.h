/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef MSPROVIDER_H
#define MSPROVIDER_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "WSTransport.h"
#include <string>

class ECMSProvider _kc_final : public KC::ECUnknown, public IMSProvider {
protected:
	ECMSProvider(ULONG ulFlags, const char *szClassName);
public:
	static  HRESULT Create(ULONG ulFlags, ECMSProvider **lppECMSProvider);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Shutdown(ULONG * lpulFlags);
	virtual HRESULT Logon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG eid_size, const ENTRYID *eid, ULONG flags, const IID *intf, ULONG *ssec_size, BYTE **spool_sec, MAPIERROR **, IMSLogon **, IMsgStore **) override;
	virtual HRESULT SpoolerLogon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG eid_size, const ENTRYID *eid, ULONG flags, const IID *intf, ULONG ssec_size, const BYTE *spool_sec, MAPIERROR **, IMSLogon **, IMsgStore **) override;
	virtual HRESULT CompareStoreIDs(ULONG eid1_size, const ENTRYID *eid1, ULONG eid2_size, const ENTRYID *eid2, ULONG flags, ULONG *result) override;

private:
	static HRESULT LogonByEntryID(KC::object_ptr<WSTransport> &, sGlobalProfileProps *, ULONG eid_size, const ENTRYID *eid);

	ULONG			m_ulFlags;
	std::string m_strLastUser, m_strLastPassword;
	ALLOC_WRAP_FRIEND;
};

class ECMSProviderSwitch _kc_final : public KC::ECUnknown, public IMSProvider {
protected:
	ECMSProviderSwitch(ULONG ulFlags);

public:
	static  HRESULT Create(ULONG ulFlags, ECMSProviderSwitch **lppMSProvider);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Shutdown(ULONG * lpulFlags);
	virtual HRESULT Logon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG eid_size, const ENTRYID *eid, ULONG flags, const IID *intf, ULONG *ssec_size, BYTE **spool_sec, MAPIERROR **, IMSLogon **, IMsgStore **) override;
	virtual HRESULT SpoolerLogon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG eid_size, const ENTRYID *eid, ULONG flags, const IID *intf, ULONG ssec_size, const BYTE *spool_sec, MAPIERROR **, IMSLogon **, IMsgStore **) override;
	virtual HRESULT CompareStoreIDs(ULONG eid1_size, const ENTRYID *eid1, ULONG eid2_size, const ENTRYID *eid2, ULONG flags, ULONG *result) override;

protected:
	ULONG			m_ulFlags;
	ALLOC_WRAP_FRIEND;
};

#endif // MSPROVIDER_H
