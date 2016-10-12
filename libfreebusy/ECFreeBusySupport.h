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
#include <kopano/Trace.h>
#include <kopano/ECDebug.h>
#include <kopano/ECGuid.h>


#include "ECFBBlockList.h"

/**
 * Implementatie of the IFreeBusySupport interface
 */
class ECFreeBusySupport _kc_final : public ECUnknown {
private:
	ECFreeBusySupport(void);
	~ECFreeBusySupport(void);
public:
	static HRESULT Create(ECFreeBusySupport** lppFreeBusySupport);

	// From IUnknown
		virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);

		// IFreeBusySupport
		virtual HRESULT Open(IMAPISession* lpMAPISession, IMsgStore* lpMsgStore, BOOL bStore);
		virtual HRESULT Close();
		virtual HRESULT LoadFreeBusyData(	ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata,
											HRESULT *phrStatus, ULONG *pcRead);

		virtual HRESULT LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4);
		virtual HRESULT CommitChanges(void) { return S_OK; }
		virtual HRESULT GetDelegateInfo(FBUser, void *) { return E_NOTIMPL; }
		virtual HRESULT SetDelegateInfo(void *) { return E_NOTIMPL; }
		virtual HRESULT AdviseFreeBusy(void *) { return E_NOTIMPL; }
		virtual HRESULT Reload(void *) { return E_NOTIMPL; }
		virtual HRESULT GetFBDetailSupport(void **, BOOL) { return E_NOTIMPL; }
		virtual HRESULT HrHandleServerSched(void *) { return E_NOTIMPL; }
		virtual HRESULT HrHandleServerSchedAccess(void) { return S_OK; }
		virtual BOOL FShowServerSched(BOOL) { return FALSE; }
		virtual HRESULT HrDeleteServerSched(void) { return S_OK; }
		virtual HRESULT GetFReadOnly(void *) { return E_NOTIMPL; }
		virtual HRESULT SetLocalFB(void *) { return E_NOTIMPL; }
		virtual HRESULT PrepareForSync(void) { return E_NOTIMPL; }
		virtual HRESULT GetFBPublishMonthRange(void *) { return E_NOTIMPL; }
		virtual HRESULT PublishRangeChanged(void) { return E_NOTIMPL; }
		virtual HRESULT CleanTombstone(void) { return E_NOTIMPL; }
		virtual HRESULT GetDelegateInfoEx(FBUser sFBUser, unsigned int *lpulStatus, unsigned int *lpulStart, unsigned int *lpulEnd);
		virtual HRESULT PushDelegateInfoToWorkspace(void) { return E_NOTIMPL; }
		virtual HRESULT Placeholder21(void *, HWND, BOOL) { return S_OK; }
		virtual HRESULT Placeholder22(void) { return S_OK; }

public:
	// Interface voor Outlook 2002 and up
	class xFreeBusySupport _kc_final : public IFreeBusySupport {
		public:
			// From IUnknown
			virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
			virtual ULONG __stdcall AddRef(void) _zcp_override;
			virtual ULONG __stdcall Release(void) _zcp_override;

			// From IFreeBusySupport
			virtual HRESULT __stdcall Open(IMAPISession *lpMAPISession, IMsgStore *lpMsgStore, BOOL bStore) _zcp_override;
			virtual HRESULT __stdcall Close(void) _zcp_override;
			virtual HRESULT __stdcall LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead) _zcp_override;
			virtual HRESULT __stdcall LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4) _zcp_override;
			virtual HRESULT __stdcall CommitChanges(void) _zcp_override;
			virtual HRESULT __stdcall GetDelegateInfo(FBUser fbUser, void *lpData) _zcp_override;
			virtual HRESULT __stdcall SetDelegateInfo(void *lpData) _zcp_override;
			virtual HRESULT __stdcall AdviseFreeBusy(void *lpData) _zcp_override;
			virtual HRESULT __stdcall Reload(void *lpData) _zcp_override;
			virtual HRESULT __stdcall GetFBDetailSupport(void **lppData, BOOL bData) _zcp_override;
			virtual HRESULT __stdcall HrHandleServerSched(void *lpData) _zcp_override;
			virtual HRESULT __stdcall HrHandleServerSchedAccess(void) _zcp_override;
			virtual BOOL __stdcall FShowServerSched(BOOL bData) _zcp_override;
			virtual HRESULT __stdcall HrDeleteServerSched(void) _zcp_override;
			virtual HRESULT __stdcall GetFReadOnly(void *lpData) _zcp_override;
			virtual HRESULT __stdcall SetLocalFB(void *lpData) _zcp_override;
			virtual HRESULT __stdcall PrepareForSync(void) _zcp_override;
			virtual HRESULT __stdcall GetFBPublishMonthRange(void *lpData) _zcp_override;
			virtual HRESULT __stdcall PublishRangeChanged(void) _zcp_override;
			virtual HRESULT __stdcall CleanTombstone(void) _zcp_override;
			virtual HRESULT __stdcall GetDelegateInfoEx(FBUser fbUser, unsigned int *lpData1, unsigned int *lpData2, unsigned int *lpData3) _zcp_override;
			virtual HRESULT __stdcall PushDelegateInfoToWorkspace(void) _zcp_override;
			virtual HRESULT __stdcall Placeholder21(void *lpData, HWND hwnd, BOOL bData) _zcp_override;
			virtual HRESULT __stdcall Placeholder22(void) _zcp_override;
	} m_xFreeBusySupport;

	// Interface for Outlook 2000
	class xFreeBusySupportOutlook2000 _zcp_final : public IFreeBusySupportOutlook2000 {
		public:
			// From IUnknown
			virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lppInterface) _zcp_override;
			virtual ULONG __stdcall AddRef(void) _zcp_override;
			virtual ULONG __stdcall Release(void) _zcp_override;

			// From IFreeBusySupport
			virtual HRESULT __stdcall Open(IMAPISession *lpMAPISession, IMsgStore *lpMsgStore, BOOL bStore) _zcp_override;
			virtual HRESULT __stdcall Close(void) _zcp_override;
			virtual HRESULT __stdcall LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead) _zcp_override;
			virtual HRESULT __stdcall LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4) _zcp_override;
			virtual HRESULT __stdcall CommitChanges(void) _zcp_override;
			virtual HRESULT __stdcall GetDelegateInfo(FBUser fbUser, void *lpData) _zcp_override;
			virtual HRESULT __stdcall SetDelegateInfo(void *lpData) _zcp_override;
			virtual HRESULT __stdcall AdviseFreeBusy(void *lpData) _zcp_override;
			virtual HRESULT __stdcall Reload(void *lpData) _zcp_override;
			virtual HRESULT __stdcall GetFBDetailSupport(void **lppData, BOOL bData) _zcp_override;
			virtual HRESULT __stdcall HrHandleServerSched(void *lpData) _zcp_override;
			virtual HRESULT __stdcall HrHandleServerSchedAccess(void) _zcp_override;
			virtual BOOL __stdcall FShowServerSched(BOOL bData) _zcp_override;
			virtual HRESULT __stdcall HrDeleteServerSched(void) _zcp_override;
			virtual HRESULT __stdcall GetFReadOnly(void *lpData) _zcp_override;
			virtual HRESULT __stdcall SetLocalFB(void *lpData) _zcp_override;
			virtual HRESULT __stdcall PrepareForSync(void) _zcp_override;
			virtual HRESULT __stdcall GetFBPublishMonthRange(void *lpData) _zcp_override;
			virtual HRESULT __stdcall PublishRangeChanged(void) _zcp_override;
			//virtual HRESULT __stdcall CleanTombstone(void) _zcp_override;
			virtual HRESULT __stdcall GetDelegateInfoEx(FBUser fbUser, unsigned int *lpData1, unsigned int *lpData2, unsigned int *lpData3) _zcp_override;
			virtual HRESULT __stdcall PushDelegateInfoToWorkspace(void) _zcp_override;
			virtual HRESULT __stdcall Placeholder21(void *lpData, HWND hwnd, BOOL bData) _zcp_override;
			virtual HRESULT __stdcall Placeholder22(void) _zcp_override;
	} m_xFreeBusySupportOutlook2000;

private:
	IMAPISession*	m_lpSession;
	IMsgStore*		m_lpPublicStore;
	IMsgStore*		m_lpUserStore;
	IMAPIFolder*	m_lpFreeBusyFolder;
	unsigned int	m_ulOutlookVersion;
};

#endif // ECFREEBUSYSUPPORT_H

/** @} */
