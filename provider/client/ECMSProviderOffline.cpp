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

#include <memory.h>
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

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


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
	REGISTER_INTERFACE(IID_ECUnknown, this);

	REGISTER_INTERFACE(IID_IMSProvider, &this->m_xMSProvider);
	REGISTER_INTERFACE(IID_IUnknown, &this->m_xMSProvider);

	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

ULONG ECMSProviderOffline::xMSProvider::AddRef() 
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderOffline::AddRef", "");
	METHOD_PROLOGUE_(ECMSProviderOffline, MSProvider);
	return pThis->AddRef();
}

ULONG ECMSProviderOffline::xMSProvider::Release()
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderOffline::Release", "");
	METHOD_PROLOGUE_(ECMSProviderOffline, MSProvider);
	return pThis->Release();
}

HRESULT ECMSProviderOffline::xMSProvider::QueryInterface(REFIID refiid, void **lppInterface)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderOffline::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECMSProviderOffline, MSProvider);
	HRESULT hr = pThis->QueryInterface(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderOffline::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

HRESULT ECMSProviderOffline::xMSProvider::Shutdown(ULONG *lpulFlags)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderOffline::Shutdown", "");
	METHOD_PROLOGUE_(ECMSProviderOffline, MSProvider);
	return pThis->Shutdown(lpulFlags);
}

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

HRESULT ECMSProviderOffline::xMSProvider::CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMSProviderOffline::CompareStoreIDs", "flags: %d\ncb=%d  entryid1: %s\n cb=%d entryid2: %s", ulFlags, cbEntryID1, bin2hex(cbEntryID1, (BYTE*)lpEntryID1).c_str(), cbEntryID2, bin2hex(cbEntryID2, (BYTE*)lpEntryID2).c_str());
	METHOD_PROLOGUE_(ECMSProviderOffline, MSProvider);
	HRESULT hr = pThis->CompareStoreIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
	TRACE_MAPI(TRACE_RETURN, "ECMSProviderOffline::CompareStoreIDs", "%s %s",GetMAPIErrorDescription(hr).c_str(), (*lpulResult == TRUE)?"TRUE": "FALSE");
	return hr;
}
