/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/**
 * @file
 * Free/busy data for specified users
 *
 * @addtogroup libfreebusy
 * @{
 */
#ifndef ECFREEBUSYSUPPORT_H
#define ECFREEBUSYSUPPORT_H

#include <kopano/zcdefs.h>
#include "freebusy.h"
#include "freebusyguid.h"
#include <mapi.h>
#include <mapidefs.h>
#include <mapix.h>
#include <kopano/ECUnknown.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "ECFBBlockList.h"

namespace KC {

/**
 * Implementatie of the IFreeBusySupport interface
 */
class _kc_export ECFreeBusySupport KC_FINAL_OPG :
    public ECUnknown, public IFreeBusySupport,
    public IFreeBusySupportOutlook2000 {
private:
	_kc_hidden ECFreeBusySupport(void);
public:
	static HRESULT Create(ECFreeBusySupport** lppFreeBusySupport);

	// From IUnknown
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) KC_OVERRIDE;

		// IFreeBusySupport
	virtual HRESULT Open(IMAPISession *, IMsgStore *, BOOL do_store) override;
	virtual HRESULT Close() override;
	_kc_hidden virtual HRESULT LoadFreeBusyData(unsigned int max, FBUser *fbuser, IFreeBusyData **fbdata, HRESULT *status, unsigned int *have_read) override;
	_kc_hidden virtual HRESULT LoadFreeBusyUpdate(unsigned int nusers, FBUser *users, IFreeBusyUpdate **fbup, unsigned int *nfbup, void *data4) override;
	_kc_hidden virtual HRESULT CommitChanges() override { return S_OK; }
	_kc_hidden virtual HRESULT GetDelegateInfo(const FBUser &, void *) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT SetDelegateInfo(void *) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT AdviseFreeBusy(void *) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT Reload(void *) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT GetFBDetailSupport(void **, BOOL) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT HrHandleServerSched(void *) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT HrHandleServerSchedAccess() override { return S_OK; }
	_kc_hidden virtual BOOL FShowServerSched(BOOL) override { return FALSE; }
	_kc_hidden virtual HRESULT HrDeleteServerSched() override { return S_OK; }
	_kc_hidden virtual HRESULT GetFReadOnly(void *) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT SetLocalFB(void *) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT PrepareForSync() override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT GetFBPublishMonthRange(void *) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT PublishRangeChanged() override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT CleanTombstone() override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT GetDelegateInfoEx(const FBUser &, unsigned int *status, unsigned int *start, unsigned int *end) override { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT PushDelegateInfoToWorkspace() override { return E_NOTIMPL; }

private:
	object_ptr<IMAPISession> m_lpSession;
	object_ptr<IMsgStore> m_lpPublicStore, m_lpUserStore;
	object_ptr<IMAPIFolder> m_lpFreeBusyFolder;
	unsigned int	m_ulOutlookVersion;
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif // ECFREEBUSYSUPPORT_H

/** @} */
