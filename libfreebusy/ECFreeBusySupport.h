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
#include <kopano/ECDebug.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "ECFBBlockList.h"

namespace KC {

/**
 * Implementatie of the IFreeBusySupport interface
 */
class _kc_export ECFreeBusySupport _kc_final :
    public ECUnknown, public IFreeBusySupport,
    public IFreeBusySupportOutlook2000 {
private:
	_kc_hidden ECFreeBusySupport(void);
public:
	static HRESULT Create(ECFreeBusySupport** lppFreeBusySupport);

	// From IUnknown
		virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;

		// IFreeBusySupport
		virtual HRESULT Open(IMAPISession* lpMAPISession, IMsgStore* lpMsgStore, BOOL bStore);
		virtual HRESULT Close();
		_kc_hidden virtual HRESULT LoadFreeBusyData(ULONG max, FBUser *fbuser, IFreeBusyData **fbdata, HRESULT *status, ULONG *have_read);
		_kc_hidden virtual HRESULT LoadFreeBusyUpdate(ULONG nusers, FBUser *users, IFreeBusyUpdate **fbup, ULONG *nfbup, void *data4);
		_kc_hidden virtual HRESULT CommitChanges(void) { return S_OK; }
	_kc_hidden virtual HRESULT GetDelegateInfo(const FBUser &, void *) override { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT SetDelegateInfo(void *) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT AdviseFreeBusy(void *) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT Reload(void *) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT GetFBDetailSupport(void **, BOOL) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT HrHandleServerSched(void *) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT HrHandleServerSchedAccess(void) { return S_OK; }
		_kc_hidden virtual BOOL FShowServerSched(BOOL) { return FALSE; }
		_kc_hidden virtual HRESULT HrDeleteServerSched(void) { return S_OK; }
		_kc_hidden virtual HRESULT GetFReadOnly(void *) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT SetLocalFB(void *) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT PrepareForSync(void) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT GetFBPublishMonthRange(void *) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT PublishRangeChanged(void) { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT CleanTombstone(void) { return E_NOTIMPL; }
	_kc_hidden virtual HRESULT GetDelegateInfoEx(const FBUser &, unsigned int *status, unsigned int *start, unsigned int *end) override { return E_NOTIMPL; }
		_kc_hidden virtual HRESULT PushDelegateInfoToWorkspace(void) { return E_NOTIMPL; }

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
