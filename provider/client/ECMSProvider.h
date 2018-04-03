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

#ifndef MSPROVIDER_H
#define MSPROVIDER_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include <kopano/memory.hpp>
#include "WSTransport.h"

#include <string>

using namespace KC;

class ECMSProvider _kc_final : public ECUnknown, public IMSProvider {
protected:
	ECMSProvider(ULONG ulFlags, const char *szClassName);
public:
	static  HRESULT Create(ULONG ulFlags, ECMSProvider **lppECMSProvider);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Shutdown(ULONG * lpulFlags);
	virtual HRESULT Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam, const TCHAR *lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB);
	virtual HRESULT SpoolerLogon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam, const TCHAR *lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG lpcbSpoolSecurity, LPBYTE lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB);
	virtual HRESULT CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult);

private:
	static HRESULT LogonByEntryID(KC::object_ptr<WSTransport> &, sGlobalProfileProps *, ULONG eid_size, ENTRYID *eid);

	ULONG			m_ulFlags;
	std::string m_strLastUser, m_strLastPassword;
	ALLOC_WRAP_FRIEND;
};

class ECMSProviderSwitch _kc_final : public ECUnknown, public IMSProvider {
protected:
	ECMSProviderSwitch(ULONG ulFlags);
public:
	static  HRESULT Create(ULONG ulFlags, ECMSProviderSwitch **lppMSProvider);
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT Shutdown(ULONG * lpulFlags);
	virtual HRESULT Logon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam, const TCHAR *lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG *lpcbSpoolSecurity, LPBYTE *lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB);
	virtual HRESULT SpoolerLogon(LPMAPISUP lpMAPISup, ULONG_PTR ulUIParam, const TCHAR *lpszProfileName, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, LPCIID lpInterface, ULONG lpcbSpoolSecurity, LPBYTE lppbSpoolSecurity, LPMAPIERROR *lppMAPIError, LPMSLOGON *lppMSLogon, LPMDB *lppMDB);
	virtual HRESULT CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult);

protected:
	
	ULONG			m_ulFlags;
	ALLOC_WRAP_FRIEND;
};

#endif // MSPROVIDER_H
