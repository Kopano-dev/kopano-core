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
class KC_EXPORT ECFreeBusySupport KC_FINAL_OPG :
    public ECUnknown, public IFreeBusySupport,
    public IFreeBusySupportOutlook2000 {
private:
	KC_HIDDEN ECFreeBusySupport();
public:
	static HRESULT Create(ECFreeBusySupport** lppFreeBusySupport);

	// From IUnknown
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) KC_OVERRIDE;

		// IFreeBusySupport
	virtual HRESULT Open(IMAPISession *, IMsgStore *, BOOL do_store) override;
	virtual HRESULT Close() override;
	KC_HIDDEN virtual HRESULT LoadFreeBusyData(unsigned int max, FBUser *fbuser, IFreeBusyData **fbdata, HRESULT *status, unsigned int *have_read) override;
	KC_HIDDEN virtual HRESULT LoadFreeBusyUpdate(unsigned int nusers, FBUser *users, IFreeBusyUpdate **fbup, unsigned int *nfbup, void *data4) override;
	KC_HIDDEN virtual HRESULT CommitChanges() override { return S_OK; }
	KC_HIDDEN virtual HRESULT GetDelegateInfo(const FBUser &, void *) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT SetDelegateInfo(void *) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT AdviseFreeBusy(void *) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT Reload(void *) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT GetFBDetailSupport(void **, BOOL) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT HrHandleServerSched(void *) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT HrHandleServerSchedAccess() override { return S_OK; }
	KC_HIDDEN virtual BOOL FShowServerSched(BOOL) override { return FALSE; }
	KC_HIDDEN virtual HRESULT HrDeleteServerSched() override { return S_OK; }
	KC_HIDDEN virtual HRESULT GetFReadOnly(void *) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT SetLocalFB(void *) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT PrepareForSync() override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT GetFBPublishMonthRange(void *) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT PublishRangeChanged() override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT CleanTombstone() override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT GetDelegateInfoEx(const FBUser &, unsigned int *status, unsigned int *start, unsigned int *end) override { return E_NOTIMPL; }
	KC_HIDDEN virtual HRESULT PushDelegateInfoToWorkspace() override { return E_NOTIMPL; }

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
