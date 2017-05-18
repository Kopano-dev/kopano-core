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

#ifndef CLIENTUTIL_H
#define CLIENTUTIL_H

#include <mapispi.h>
#include <string>
#include <kopano/ECTags.h>
#include <edkmdb.h>
#include <kopano/zcdefs.h>

class WSTransport;

struct sGlobalProfileProps {
	std::string		strServerPath;
	std::string		strProfileName;
	std::wstring		strUserName;
	std::wstring		strPassword;
    	std::wstring    	strImpersonateUser;
	ULONG			ulProfileFlags;
	std::string		strSSLKeyFile;
	std::string		strSSLKeyPass;
	ULONG			ulConnectionTimeOut;
	ULONG			ulProxyFlags;
	std::string		strProxyHost;
	ULONG			ulProxyPort;
	std::string		strProxyUserName;
	std::string		strProxyPassword;
	tstring			strOfflinePath;
	bool			bIsEMS;
	std::string		strClientAppVersion;
	std::string		strClientAppMisc;
};

class ClientUtil _kc_final {
public:
	static HRESULT	HrInitializeStatusRow (const char * lpszProviderDisplay, ULONG ulResourceType, LPMAPISUP lpMAPISup, LPSPropValue lpspvIdentity, ULONG ulFlags);
	static HRESULT	HrSetIdentity(WSTransport *lpTransport, LPMAPISUP lpMAPISup, LPSPropValue* lppIdentityProps);

	static HRESULT ReadReceipt(ULONG ulFlags, LPMESSAGE lpReadMessage, LPMESSAGE* lppEmptyMessage);

	// Get the global properties
	static HRESULT GetGlobalProfileProperties(LPPROFSECT lpGlobalProfSect, struct sGlobalProfileProps* lpsProfileProps);
	static HRESULT GetGlobalProfileProperties(LPMAPISUP lpMAPISup, struct sGlobalProfileProps* lpsProfileProps);

	/* Get the delegate stores from the global profile. */
	static HRESULT GetGlobalProfileDelegateStoresProp(LPPROFSECT lpGlobalProfSect, ULONG *lpcDelegates, LPBYTE *lppDelegateStores);

	// Get MSEMS emulator config
	static HRESULT GetConfigPath(std::string *lpConfigPath);
	// Convert MSEMS profile properties into ZARAFA profile properties
	static HRESULT ConvertMSEMSProps(ULONG cValues, LPSPropValue pValues, ULONG *lpcValues, LPSPropValue *lppProps);

};

HRESULT HrCreateEntryId(GUID guidStore, unsigned int ulObjType, ULONG* lpcbEntryId, LPENTRYID* lppEntryId);
HRESULT HrGetServerURLFromStoreEntryId(ULONG cbEntryId, LPENTRYID lpEntryId, std::string& rServerPath, bool *lpbIsPseudoUrl);
HRESULT HrResolvePseudoUrl(WSTransport *lpTransport, const char *lpszUrl, std::string& serverPath, bool *lpbIsPeer);
HRESULT HrCompareEntryIdWithStoreGuid(ULONG cbEntryID, LPENTRYID lpEntryID, LPCGUID guidStore);

enum enumPublicEntryID { ePE_None, ePE_IPMSubtree, ePE_Favorites, ePE_PublicFolders, ePE_FavoriteSubFolder };

HRESULT GetPublicEntryId(enumPublicEntryID ePublicEntryID, GUID guidStore, void *lpBase, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);

BOOL CompareMDBProvider(LPBYTE lpguid, const GUID *lpguidKopano);
BOOL CompareMDBProvider(MAPIUID* lpguid, const GUID *lpguidKopano);

#endif
