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

#include <mutex>
#include <mapispi.h>
#include <string>
#include <kopano/ECTags.h>
#include <edkmdb.h>
#include <kopano/zcdefs.h>

class KCmdProxy;
class WSTransport;

struct sGlobalProfileProps {
	std::string strServerPath, strProfileName;
	std::wstring strUserName, strPassword, strImpersonateUser;
	std::string strSSLKeyFile, strSSLKeyPass;
	std::string strProxyHost;
	std::string strProxyUserName, strProxyPassword;
	std::string strClientAppVersion, strClientAppMisc;
	unsigned int ulProfileFlags = 0, ulConnectionTimeOut = 10;
	unsigned int ulProxyFlags = 0, ulProxyPort = 0;
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
};

class WSSoap {
	public:
	KCmdProxy *m_lpCmd = nullptr;
	std::recursive_mutex m_hDataLock;
};

class soap_lock_guard {
	public:
	soap_lock_guard(WSSoap &);
	soap_lock_guard(const soap_lock_guard &) = delete;
	~soap_lock_guard();
	void operator=(const soap_lock_guard &) = delete;
	void unlock();

	private:
	WSSoap &m_parent;
	std::unique_lock<std::recursive_mutex> m_dg;
	bool m_done = false;
};

extern HRESULT HrCreateEntryId(const GUID &store_guid, unsigned int obj_type, ULONG *eid_size, ENTRYID **eid);
extern HRESULT HrGetServerURLFromStoreEntryId(ULONG eid_size, const ENTRYID *eid, std::string &srv_path, bool *pseudo);
HRESULT HrResolvePseudoUrl(WSTransport *lpTransport, const char *lpszUrl, std::string& serverPath, bool *lpbIsPeer);
extern HRESULT HrCompareEntryIdWithStoreGuid(ULONG eid_size, const ENTRYID *eid, const GUID *store_guid);

enum enumPublicEntryID { ePE_None, ePE_IPMSubtree, ePE_Favorites, ePE_PublicFolders, ePE_FavoriteSubFolder };

extern HRESULT GetPublicEntryId(enumPublicEntryID, const GUID &store_guid, void *base, ULONG *eid_size, ENTRYID **eid);
extern BOOL CompareMDBProvider(const BYTE *guid, const GUID *kopano_guid);
extern BOOL CompareMDBProvider(const MAPIUID *guid, const GUID *kopano_guid);

#endif
