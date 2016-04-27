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

#ifndef PROVIDERUTIL_H
#define PROVIDERUTIL_H

#include "WSTransport.h"

#define CT_UNSPECIFIED		0x00
#define CT_ONLINE			0x01
#define CT_OFFLINE			0x02

typedef struct {
	IMSProvider *lpMSProviderOnline;
	IMSProvider *lpMSProviderOffline;
	IABProvider *lpABProviderOnline;
	IABProvider *lpABProviderOffline;
	ULONG		ulProfileFlags;	//  Profile flags when you start the first time
	ULONG		ulConnectType; // CT_* values, The type of connection when you start the first time
}PROVIDER_INFO;

typedef std::map<std::string, PROVIDER_INFO> ECMapProvider;

HRESULT CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult);

HRESULT CreateMsgStoreObject(char *lpszProfname, LPMAPISUP lpMAPISup, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulMsgFlags, ULONG ulProfileFlags, WSTransport* lpTransport,
							MAPIUID* lpguidMDBProvider, BOOL bSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore,
							ECMsgStore** lppMsgStore);

#ifdef WIN32
HRESULT FirstFolderSync(HWND hWnd, IMSProvider *lpOffline, IMSProvider *lpOnline, ULONG cbEntryID, LPENTRYID lpEntryID, LPMAPISUP lpMAPISupport);
HRESULT FirstAddressBookSync(HWND hWnd, IABProvider *lpOffline, IABProvider *lpOnline, LPMAPISUP lpMAPISupport);
#endif

#ifdef HAVE_OFFLINE_SUPPORT
/*
	Start a storage server with a unique pipe and database

	lpWorkDir
		The directory of the storage server
	lpUniqueId
		used for database name and pipe name
	lpDatabasePath
		Database path
	lpDbConfigName
		Database configuration file path
	dwSecondsToWait
		The seconds to wait to run a storage server
*/
BOOL StartServer(LPCTSTR lpWorkDir, LPCTSTR lpDbConfigName, LPCTSTR lpDatabasePath, LPCTSTR lpUniqueId, LPPROCESS_INFORMATION lpProcInfo);
#endif

HRESULT RemoveAllProviders(ECMapProvider *lpmapProvider);
HRESULT SetProviderMode(IMAPISupport *lpMAPISup, ECMapProvider *lpmapProvider, LPCSTR lpszProfileName, ULONG ulConnectType);
HRESULT GetProviders(ECMapProvider *lpmapProvider, IMAPISupport *lpMAPISup, LPCSTR lpszProfileName, ULONG ulFlags, PROVIDER_INFO *lpsProviderInfo);
HRESULT GetLastConnectionType(IMAPISupport *lpMAPISup, ULONG *lpulType);

HRESULT GetMAPIUniqueProfileId(LPMAPISUP lpMAPISup, tstring *lpstrUniqueId);
#ifdef HAVE_OFFLINE_SUPPORT
HRESULT CheckStartServerAndGetServerURL(IMAPISupport *lpMAPISup, LPCTSTR lpszUserLocalAppDataKopano, LPCTSTR lpszKopanoDirectory, std::string *lpstrServerURL);
HRESULT GetOfflineServerURL(IMAPISupport *lpMAPISup, std::string *lpstrServerURL, tstring *lpstrUniqueId = NULL);
#endif

HRESULT GetTransportToNamedServer(WSTransport *lpTransport, LPCTSTR lpszServerName, ULONG ulFlags, WSTransport **lppTransport);

#endif // #ifndef PROVIDERUTIL_H
