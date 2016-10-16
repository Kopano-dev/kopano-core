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

#include <kopano/platform.h>
#include <kopano/ECInterfaceDefs.h>
#include <mapi.h>
#include <mapiutil.h>
#include <mapispi.h>

#include "ECMSProviderOffline.h"
#include "ECMSProvider.h"

#include <kopano/ECGuid.h>

#include <kopano/Trace.h>
#include <kopano/ECDebug.h>

#include <edkguid.h>
#include "EntryPoint.h"
#include "DLLGlobal.h"
#include <edkmdb.h>

#include "ClientUtil.h"
#include "ECMsgStore.h"
#include <kopano/stringutil.h>

#include "ProviderUtil.h"

ECMSProviderOffline::ECMSProviderOffline(ULONG ulFlags) : 
	ECMSProvider(ulFlags|EC_PROVIDER_OFFLINE, "ECMSProviderOffline")
{

}

HRESULT ECMSProviderOffline::Create(ULONG ulFlags, ECMSProviderOffline **lppMSProvider)
{
	ECMSProviderOffline *lpMSProvider = new ECMSProviderOffline(ulFlags);

	return lpMSProvider->QueryInterface(IID_ECUnknown/*IID_ECMSProviderOffline*/, (void **)lppMSProvider);
}

HRESULT ECMSProviderOffline::QueryInterface(REFIID refiid, void **lppInterface)
{
	/* refiid == IID_ECMSProviderOffline */
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMSProvider, &this->m_xMSProvider);
	REGISTER_INTERFACE2(IUnknown, &this->m_xMSProvider);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

DEF_ULONGMETHOD1(TRACE_MAPI, ECMSProviderOffline, MSProvider, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMSProviderOffline, MSProvider, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMSProviderOffline, MSProvider, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECMSProviderOffline, MSProvider, Shutdown, (ULONG *, lpulFlags))

HRESULT ECMSProviderOffline::xMSProvider::Logon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderOffline::Logon", "flags=%x, cbEntryID=%d", ulFlags, cbEntryID);
	METHOD_PROLOGUE_(ECMSProviderOffline, MSProvider);
	HRESULT hr = pThis->Logon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID,ulFlags, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderOffline::Logon", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMSProviderOffline::xMSProvider::SpoolerLogon(LPMAPISUP lpMAPISup, ULONG ulUIParam, LPTSTR lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG lpcbSpoolSecurity, LPBYTE lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderOffline::SpoolerLogon", "flags=%x", ulFlags);
	METHOD_PROLOGUE_(ECMSProviderOffline, MSProvider);
	HRESULT hr = pThis->SpoolerLogon(lpMAPISup, ulUIParam, lpszProfileName, cbEntryID, lpEntryID,ulFlags, lpInterface, lpcbSpoolSecurity, lppbSpoolSecurity, lppMAPIError, lppMSLogon, lppMDB);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderOffline::SpoolerLogon", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECMSProviderOffline, MSProvider, CompareStoreIDs, (ULONG, cbEntryID1), (LPENTRYID, lpEntryID1), (ULONG, cbEntryID2), (LPENTRYID, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
