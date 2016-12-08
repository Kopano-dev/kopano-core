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

#include <kopano/zcdefs.h>
#include <kopano/platform.h>

#include <string>
#include <memory>
#include <map>
#include <kopano/ustringutil.h>

#include <mapi.h>
#include <mapidefs.h>
#include <mapiutil.h>
#include <mapitags.h>
#include <mapicode.h>
#include <cerrno>
#include <iconv.h>

#include <kopano/ECLogger.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECTags.h>
#include <kopano/ECGuid.h>
#include <kopano/Util.h>
#include <kopano/stringutil.h>
#include <kopano/base64.h>
#include <kopano/mapi_ptr.h>

#include <kopano/charset/convert.h>
#include <kopano/mapiext.h>
#include "freebusytags.h"

#include <edkguid.h>
#include <kopano/mapiguidext.h>
#include <edkmdb.h>
#include <kopano/IECUnknown.h>
#include <kopano/IECServiceAdmin.h>
#include <kopano/EMSAbTag.h>
#include <kopano/ECRestriction.h>
#include <kopano/MAPIErrors.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

using namespace std;

namespace KC {

#define PROFILEPREFIX		"ec-adm-"

/* Indexes of the sPropNewMailColumns property array */
enum {
	NEWMAIL_ENTRYID,		// Array Indexes
	NEWMAIL_PARENT_ENTRYID,
	NEWMAIL_MESSAGE_CLASS,
	NEWMAIL_MESSAGE_FLAGS,
	NUM_NEWMAIL_PROPS,		// Array size
};

/* Newmail Notify columns */
static const SizedSPropTagArray(4, sPropNewMailColumns) = {
	4,
	{
		PR_ENTRYID,
		PR_PARENT_ENTRYID,
		PR_MESSAGE_CLASS_A,
		PR_MESSAGE_FLAGS,
	}
};

} /* namespace */

bool operator ==(const SBinary &left, const SBinary &right)
{
	return left.cb == right.cb && memcmp(left.lpb, right.lpb, left.cb) == 0;
}

bool operator <(const SBinary &left, const SBinary &right)
{
	return left.cb < right.cb || (left.cb == right.cb && memcmp(left.lpb, right.lpb, left.cb) < 0);
}

namespace KC {

const char *GetServerUnixSocket(const char *szPreferred)
{
	const char *env = getenv("KOPANO_SOCKET");
	if (env && env[0] != '\0')
		return env;
	else if (szPreferred && szPreferred[0] != '\0')
		return szPreferred;
	else
		return "";
}

/** 
 * Return the FQDN of this server (guessed).
 * 
 * @return hostname string
 */
std::string GetServerFQDN()
{
	string retval = "localhost";
	int rc;
	char hostname[256] = {0};
	struct addrinfo hints = {0};
	struct addrinfo *aiResult = NULL;
	struct sockaddr_in saddr = {0};

	rc = gethostname(hostname, sizeof(hostname));
	if (rc != 0)
		goto exit;

	retval = hostname;

	rc = getaddrinfo(hostname, NULL, &hints, &aiResult);
	if (rc != 0)
		goto exit;

	// no need to set other contents of saddr struct, we're just intrested in the DNS lookup.
	saddr = *((sockaddr_in*)aiResult->ai_addr);

	// Name lookup is required, so set that flag
	rc = getnameinfo((const sockaddr*)&saddr, sizeof(saddr), hostname, sizeof(hostname), NULL, 0, NI_NAMEREQD);
	if (rc != 0)
		goto exit;

	if (hostname[0] != '\0')
		retval = hostname;

exit:
	if (aiResult)
		freeaddrinfo(aiResult);

	return retval;
}

// FIXME: replace it to the right place
#define	pbGlobalProfileSectionGuid	"\x13\xDB\xB0\xC8\xAA\x05\x10\x1A\x9B\xB0\x00\xAA\x00\x2F\xC4\x5A"

/**
 * Creates a new profile with given information.
 *
 * A new Kopano profile will be created with the information given in
 * the paramters. See common/ECTags.h for possible profileflags. These
 * will be placed in PR_EC_FLAGS.
 * Any existing profile with the name in szProfName will first be removed.
 *
 * @param[in]	username	Username to logon with
 * @param[in]	password	Password of the username
 * @param[in]	path		In URI form. Eg. file:///var/run/kopano/server.sock
 * @param[in]	szProfName	Name of the profile to create
 * @param[in]	ulProfileFlags See EC_PROFILE_FLAGS_* in common/ECTags.h
 * @param[in]	sslkey_file	May be NULL. Logon with this sslkey instead of password.
 * @param[in]	sslkey_password	May be NULL. Password of the sslkey_file.
 *
 * @return		HRESULT		Mapi error code.
 */
HRESULT CreateProfileTemp(const wchar_t *username, const wchar_t *password,
    const char *path, const char* szProfName, ULONG ulProfileFlags,
    const char *sslkey_file, const char *sslkey_password,
    const char *app_version, const char *app_misc)
{
	HRESULT hr = hrSuccess;
	IProfAdmin *lpProfAdmin = NULL;
	LPSERVICEADMIN lpServiceAdmin = NULL;
	LPSPropValue lpServiceUID = NULL;
	SPropValue sProps[9];	// server, username, password and profile -name and -flags, optional sslkey file with sslkey password
	LPSPropValue lpServiceName = NULL;

	LPMAPITABLE	lpTable = NULL;
	LPSRowSet	lpRows = NULL;
	int i;

//-- create profile
	hr = MAPIAdminProfiles(0, &lpProfAdmin);
	if (hr != hrSuccess) {
		ec_log_crit("CreateProfileTemp(): MAPIAdminProfiles failed %x: %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	lpProfAdmin->DeleteProfile((LPTSTR)szProfName, 0);
	hr = lpProfAdmin->CreateProfile((LPTSTR)szProfName, (LPTSTR)"", 0, 0);
	if (hr != hrSuccess) {
		ec_log_crit("CreateProfileTemp(): CreateProfile failed %x: %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	hr = lpProfAdmin->AdminServices((LPTSTR)szProfName, (LPTSTR)"", 0, 0, &lpServiceAdmin);
	if (hr != hrSuccess) {
		ec_log_crit("CreateProfileTemp(): AdminServices failed %x: %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}
	
	hr = lpServiceAdmin->CreateMsgService((LPTSTR)"ZARAFA6", (LPTSTR)"", 0, 0);
	if (hr != hrSuccess) {
		ec_log_crit("CreateProfileTemp(): CreateMsgService ZARAFA6 failed: %s (%x)", GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	// Strangely we now have to get the SERVICE_UID for the service we just added from
	// the table. (see MSDN help page of CreateMsgService at the bottom of the page)
	hr = lpServiceAdmin->GetMsgServiceTable(0, &lpTable);
	if(hr != hrSuccess) {
		ec_log_crit("CreateProfileTemp(): GetMsgServiceTable failed %x: %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// Find the correct row
	while(TRUE) {
		hr = lpTable->QueryRows(1, 0, &lpRows);
		
		if(hr != hrSuccess) {
			ec_log_crit("CreateProfileTemp(): QueryRows failed %x: %s", hr, GetMAPIErrorMessage(hr));
			goto exit;
		}
			
		if(lpRows->cRows != 1)
			break;
	
		lpServiceName = PpropFindProp(lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues, PR_SERVICE_NAME_A);
		
		if(lpServiceName && strcmp(lpServiceName->Value.lpszA, "ZARAFA6") == 0)
			break;
			
		FreeProws(lpRows);
		lpRows = NULL;
			
	}
	
	if(lpRows->cRows != 1) {
		ec_log_warn("CreateProfileTemp(): no rows found");
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// Get the PR_SERVICE_UID from the row
	lpServiceUID = PpropFindProp(lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues, PR_SERVICE_UID);
	if(!lpServiceUID) {
		ec_log_crit("CreateProfileTemp(): PpropFindProp failed %x: %s", hr, GetMAPIErrorMessage(hr));
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	i = 0;
	sProps[i].ulPropTag = PR_EC_PATH;
	sProps[i].Value.lpszA = const_cast<char *>(path != NULL && *path != '\0' ? path : "default:");
	++i;

	sProps[i].ulPropTag = PR_EC_USERNAME_W;
	sProps[i].Value.lpszW = (WCHAR*)username;
	++i;

	sProps[i].ulPropTag = PR_EC_USERPASSWORD_W;
	sProps[i].Value.lpszW = (WCHAR*)password;
	++i;

	sProps[i].ulPropTag = PR_EC_FLAGS;
	sProps[i].Value.ul = ulProfileFlags;
	++i;

	sProps[i].ulPropTag = PR_PROFILE_NAME_A;
	sProps[i].Value.lpszA = (char*)szProfName;
	++i;

	if (sslkey_file) {
		// always add SSL keys info as we might be redirected to an SSL connection
		sProps[i].ulPropTag = PR_EC_SSLKEY_FILE;
		sProps[i].Value.lpszA = (char*)sslkey_file;
		++i;

		if (sslkey_password) {
			sProps[i].ulPropTag = PR_EC_SSLKEY_PASS;
			sProps[i].Value.lpszA = (char*)sslkey_password;
			++i;
		}
	}

	if (app_version) {
		sProps[i].ulPropTag = PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION;
		sProps[i].Value.lpszA = (char*)app_version;
		++i;
	}

	if (app_misc) {
		sProps[i].ulPropTag = PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC;
		sProps[i].Value.lpszA = (char*)app_misc;
		++i;
	}

	hr = lpServiceAdmin->ConfigureMsgService((MAPIUID *)lpServiceUID->Value.bin.lpb, 0, 0, i, sProps);
	if (hr != hrSuccess) {
		ec_log_crit("CreateProfileTemp(): ConfigureMsgService failed %x: %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

exit:
	if (lpRows)
		FreeProws(lpRows);
	
	if (lpTable)
		lpTable->Release();
		
	if (lpProfAdmin)
		lpProfAdmin->Release();

	if (lpServiceAdmin)
		lpServiceAdmin->Release();

	return hr;
}

/**
 * Deletes a profile with specified name.
 *
 * @param[in]	szProfName	Name of the profile to delete
 *
 * @return		HRESULT		Mapi error code.
 */
HRESULT DeleteProfileTemp(char *szProfName)
{
	IProfAdmin *lpProfAdmin = NULL;
	HRESULT hr = hrSuccess;

	// Get the MAPI Profile administration object
	hr = MAPIAdminProfiles(0, &lpProfAdmin);
	if (hr != hrSuccess)
		goto exit;

	hr = lpProfAdmin->DeleteProfile((LPTSTR)szProfName, 0);

exit:
	if(lpProfAdmin)
		lpProfAdmin->Release();

	return hr;
}

HRESULT HrOpenECAdminSession(IMAPISession **lppSession,
    const char *const app_version, const char *const app_misc,
    const char *szPath, ULONG ulProfileFlags, const char *sslkey_file,
    const char *sslkey_password)
{
	return HrOpenECSession(lppSession, app_version, app_misc, KOPANO_SYSTEM_USER_W, KOPANO_SYSTEM_USER_W, szPath, ulProfileFlags, sslkey_file, sslkey_password);
}

HRESULT HrOpenECSession(IMAPISession **lppSession,
    const char *const app_version, const char *const app_misc,
    const wchar_t *szUsername, const wchar_t *szPassword, const char *szPath,
    ULONG ulProfileFlags, const char *sslkey_file, const char *sslkey_password,
    const char *profname)
{
	HRESULT		hr = hrSuccess;
	ULONG		ulProfNum = 0;
	char		*szProfName = new char[strlen(PROFILEPREFIX)+10+1];
	IMAPISession *lpMAPISession = NULL;

	if (profname == NULL) {
		ulProfNum = rand_mt();
		snprintf(szProfName, strlen(PROFILEPREFIX)+10+1, "%s%010u", PROFILEPREFIX, ulProfNum);
	}
	else {
		strcpy(szProfName, profname);
	}

	if (sslkey_file != NULL) {
		FILE *ssltest = fopen(sslkey_file, "r");
		if (!ssltest) {
			ec_log_crit("Cannot access %s: %s", sslkey_file, strerror(errno));

			// do not pass sslkey if the file does not exist
			// otherwise normal connections do not work either
			sslkey_file = NULL;
			sslkey_password = NULL;
		}
		else {
			// TODO: test password of certificate
			fclose(ssltest);
		}
	}

	hr = CreateProfileTemp(szUsername, szPassword, szPath, (const char*)szProfName, ulProfileFlags, sslkey_file, sslkey_password, app_version, app_misc);
	if (hr != hrSuccess) {
		ec_log_warn("CreateProfileTemp failed: %x: %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	// Log on the the profile
	hr = MAPILogonEx(0, (LPTSTR)szProfName, (LPTSTR)"", MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL, &lpMAPISession);
	if (hr != hrSuccess) {
		ec_log_warn("MAPILogonEx failed: %x: %s", hr, GetMAPIErrorMessage(hr));
		goto exit;
	}

	*lppSession = lpMAPISession;

exit:
	// always try to delete the temporary profile
	DeleteProfileTemp(szProfName);
	delete[] szProfName;
	return hr;
}

HRESULT HrSearchECStoreEntryId(IMAPISession *lpMAPISession, BOOL bPublic, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	HRESULT			hr = hrSuccess;
	LPSRowSet		lpRows = NULL;
	IMAPITable		*lpStoreTable = NULL;
	LPSPropValue	lpStoreProp = NULL;
	LPSPropValue	lpEntryIDProp = NULL;

	// Get the default store by searching through the message store table and finding the
	// store with PR_MDB_PROVIDER set to the kopano public store GUID

	hr = lpMAPISession->GetMsgStoresTable(0, &lpStoreTable);
	if(hr != hrSuccess)
		goto exit;

	while(TRUE) {
			hr = lpStoreTable->QueryRows(1, 0, &lpRows);

			if (hr != hrSuccess || lpRows->cRows != 1) {
				hr = MAPI_E_NOT_FOUND;
				break;
			}

			if (bPublic) {
				lpStoreProp = PpropFindProp(lpRows->aRow[0].lpProps,lpRows->aRow[0].cValues, PR_MDB_PROVIDER);
				if (lpStoreProp != NULL && memcmp(lpStoreProp->Value.bin.lpb, &KOPANO_STORE_PUBLIC_GUID, sizeof(MAPIUID)) == 0 )
					break;
			} else {
				lpStoreProp = PpropFindProp(lpRows->aRow[0].lpProps,lpRows->aRow[0].cValues, PR_RESOURCE_FLAGS);
				if (lpStoreProp != NULL && lpStoreProp->Value.ul & STATUS_DEFAULT_STORE)
					break;
			}

			FreeProws(lpRows);
			lpRows = NULL;
	}

	if (hr != hrSuccess)
		goto exit;

	lpEntryIDProp = PpropFindProp(lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues, PR_ENTRYID);
	if (lpEntryIDProp == NULL) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// copy entryid so we continue in the same code piece in windows/linux
	hr = Util::HrCopyEntryId(lpEntryIDProp->Value.bin.cb, (LPENTRYID)lpEntryIDProp->Value.bin.lpb, lpcbEntryID, lppEntryID);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (lpRows)
		FreeProws(lpRows);

	if (lpStoreTable)
		lpStoreTable->Release();

	return hr;
}

HRESULT HrOpenDefaultStore(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore) {
	return HrOpenDefaultStore(lpMAPISession, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, lppMsgStore);
}

HRESULT HrOpenDefaultStoreOffline(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore)
{
	HRESULT	hr = hrSuccess;
	IMsgStore *lpMsgStore = NULL;
	IMsgStore *lpProxedMsgStore = NULL;

	hr = HrOpenDefaultStore(lpMAPISession, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, &lpMsgStore);
	if(hr != hrSuccess)
		goto exit;

	hr = GetProxyStoreObject(lpMsgStore, &lpProxedMsgStore);
	if (hr != hrSuccess)
		goto exit;
	
	hr = lpProxedMsgStore->QueryInterface(IID_ECMsgStoreOffline, (void**)lppMsgStore);
	if(hr != hrSuccess)
		goto exit;

exit:
	if (lpProxedMsgStore)
		lpProxedMsgStore->Release();

	if (lpMsgStore)
		lpMsgStore->Release();

	return hr;
}

HRESULT HrOpenDefaultStoreOnline(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore)
{
	HRESULT	hr = hrSuccess;
	IMsgStore *lpMsgStore = NULL;
	IMsgStore *lpProxedMsgStore = NULL;

	hr = HrOpenDefaultStore(lpMAPISession, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, &lpMsgStore);
	if(hr != hrSuccess)
		goto exit;

	hr = GetProxyStoreObject(lpMsgStore, &lpProxedMsgStore);
	if (hr != hrSuccess)
		goto exit;
	
	hr = lpProxedMsgStore->QueryInterface(IID_ECMsgStoreOnline, (void**)lppMsgStore);
	if(hr != hrSuccess)
		goto exit;

exit:
	if (lpProxedMsgStore)
		lpProxedMsgStore->Release();

	if (lpMsgStore)
		lpMsgStore->Release();

	return hr;
}

HRESULT HrOpenStoreOnline(IMAPISession *lpMAPISession, ULONG cbEntryID, LPENTRYID lpEntryID, IMsgStore **lppMsgStore)
{
	HRESULT	hr = hrSuccess;
	IMsgStore *lpMsgStore = NULL;
	IMsgStore *lpProxedMsgStore = NULL;

	if (lpMAPISession == NULL || lppMsgStore == NULL || lpEntryID == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpMAPISession->OpenMsgStore(0, cbEntryID, lpEntryID, &IID_IMsgStore, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, &lpMsgStore);
	if (hr != hrSuccess)
		goto exit;

	hr = GetProxyStoreObject(lpMsgStore, &lpProxedMsgStore);
	if (hr != hrSuccess)
		goto exit;
	
	hr = lpProxedMsgStore->QueryInterface(IID_ECMsgStoreOnline, (void**)lppMsgStore);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (lpProxedMsgStore)
		lpProxedMsgStore->Release();

	if (lpMsgStore)
		lpMsgStore->Release();

	return hr;
}

HRESULT GetProxyStoreObject(IMsgStore *lpMsgStore, IMsgStore **lppMsgStore)
{
	HRESULT	hr = hrSuccess;
	IProxyStoreObject *lpProxyStoreObject = NULL;
	IECUnknown* lpECMsgStore = NULL;
	LPSPropValue lpPropValue = NULL;

	if (lpMsgStore == NULL || lppMsgStore == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	if ( lpMsgStore->QueryInterface(IID_IProxyStoreObject, (void**)&lpProxyStoreObject) == hrSuccess) {

		hr = lpProxyStoreObject->UnwrapNoRef((LPVOID*)lppMsgStore);
		if (hr != hrSuccess)
			goto exit;

		(*lppMsgStore)->AddRef();

	} else if(HrGetOneProp(lpMsgStore, PR_EC_OBJECT, &lpPropValue) == hrSuccess) {

		lpECMsgStore = (IECUnknown *)lpPropValue->Value.lpszA;
		if (lpECMsgStore == NULL) {
			hr = MAPI_E_INVALID_PARAMETER;
			goto exit;
		}

		hr = lpECMsgStore->QueryInterface(IID_IMsgStore, (void**)lppMsgStore);
	} else {
		// Possible object already wrapped, gives the original object back
		(*lppMsgStore) = lpMsgStore;
		(*lppMsgStore)->AddRef();
	}

exit:
	MAPIFreeBuffer(lpPropValue);
	if (lpProxyStoreObject)
		lpProxyStoreObject->Release();

	return hr;
}

HRESULT HrOpenDefaultStore(IMAPISession *lpMAPISession, ULONG ulFlags, IMsgStore **lppMsgStore) {
	HRESULT			hr = hrSuccess;
	IMsgStore		*lpMsgStore = NULL;
	ULONG			cbEntryID = 0;
	LPENTRYID		lpEntryID = NULL;

	hr = HrSearchECStoreEntryId(lpMAPISession, FALSE, &cbEntryID, &lpEntryID);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMAPISession->OpenMsgStore(0, cbEntryID, lpEntryID, &IID_IMsgStore, ulFlags, &lpMsgStore);
	if (hr != hrSuccess)
		goto exit;

	*lppMsgStore = lpMsgStore;

exit:
	MAPIFreeBuffer(lpEntryID);
	return hr;
}

HRESULT HrOpenECPublicStore(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore){
	return HrOpenECPublicStore(lpMAPISession, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, lppMsgStore);
}

HRESULT HrOpenECPublicStoreOnline(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore)
{
	HRESULT	hr = hrSuccess;
	IMsgStore *lpMsgStore = NULL;
	IMsgStore *lpProxedMsgStore = NULL;

	hr = HrOpenECPublicStore(lpMAPISession, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, &lpMsgStore);
	if(hr != hrSuccess)
		goto exit;

	hr = GetProxyStoreObject(lpMsgStore, &lpProxedMsgStore);
	if (hr != hrSuccess)
		goto exit;
	
	hr = lpProxedMsgStore->QueryInterface(IID_ECMsgStoreOnline, (void**)lppMsgStore);
	if(hr != hrSuccess)
		goto exit;

exit:
	if (lpProxedMsgStore)
		lpProxedMsgStore->Release();

	if (lpMsgStore)
		lpMsgStore->Release();

	return hr;
}

HRESULT HrOpenECPublicStore(IMAPISession *lpMAPISession, ULONG ulFlags, IMsgStore **lppMsgStore)
{
	HRESULT			hr = hrSuccess;
	IMsgStore		*lpMsgStore = NULL;
	ULONG			cbEntryID = 0;
	LPENTRYID		lpEntryID = NULL;

	hr = HrSearchECStoreEntryId(lpMAPISession, TRUE, &cbEntryID, &lpEntryID);
	if(hr != hrSuccess)
		goto exit;

	hr = lpMAPISession->OpenMsgStore(0, cbEntryID, lpEntryID, &IID_IMsgStore, ulFlags, &lpMsgStore);
	if(hr != hrSuccess)
		goto exit;

	*lppMsgStore = lpMsgStore;

exit:
	MAPIFreeBuffer(lpEntryID);
	return hr;
}

HRESULT HrGetECProviderAdmin(LPMAPISESSION lpSession, LPPROVIDERADMIN *lppProviderAdmin)
{
	HRESULT			hr = hrSuccess;
	LPSERVICEADMIN	lpMsgServiceAdmin = NULL;
	LPMAPITABLE		lpServiceTable = NULL;
	SRestriction	sRestrict;
	SPropValue		sPropRestrict;
	LPSRowSet		lpsRowSet = NULL;
	LPSPropValue	lpProviderUID = NULL;

	// Get the service admin
	hr = lpSession->AdminServices(0, &lpMsgServiceAdmin);
	if(hr != hrSuccess)
		goto exit;

	//Getdefault profile
	hr = lpMsgServiceAdmin->GetMsgServiceTable(0, &lpServiceTable);
	if(hr != hrSuccess)
		goto exit;
	
	// restrict the table
	sPropRestrict.ulPropTag = PR_SERVICE_NAME_A;
	sPropRestrict.Value.lpszA = const_cast<char *>("ZARAFA6");
	sRestrict.res.resContent.ulFuzzyLevel = FL_FULLSTRING;
	sRestrict.res.resContent.ulPropTag = PR_SERVICE_NAME_A;
	sRestrict.res.resContent.lpProp = &sPropRestrict;
	sRestrict.rt = RES_CONTENT;

	hr = lpServiceTable->Restrict(&sRestrict,0);
	if(hr != hrSuccess)
		goto exit;
	
	//Seek to the end
	hr = lpServiceTable->SeekRow(BOOKMARK_END, -1, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpServiceTable->QueryRows(1, 0, &lpsRowSet);
	if(hr != hrSuccess || lpsRowSet == NULL || lpsRowSet->cRows != 1)
	{
		if(hr == hrSuccess) hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// Get the Service UID
	lpProviderUID = PpropFindProp(lpsRowSet->aRow[0].lpProps, lpsRowSet->aRow[0].cValues, PR_SERVICE_UID);
	if(lpProviderUID == NULL){
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// Get a admin provider pointer
	hr = lpMsgServiceAdmin->AdminProviders((MAPIUID *)lpProviderUID->Value.bin.lpb, 0, lppProviderAdmin);
	if(hr != hrSuccess)
		goto exit;

exit:
	if(lpServiceTable)
		lpServiceTable->Release();

	if(lpMsgServiceAdmin)
		lpMsgServiceAdmin->Release();

	if(lpsRowSet)
		FreeProws(lpsRowSet);

	return hr;
}

HRESULT HrRemoveECMailBox(LPMAPISESSION lpSession, LPMAPIUID lpsProviderUID)
{
	HRESULT			hr = hrSuccess;
	LPPROVIDERADMIN lpProviderAdmin = NULL;

	hr = HrGetECProviderAdmin(lpSession, &lpProviderAdmin);
	if(hr != hrSuccess)
		goto exit;

	hr = HrRemoveECMailBox(lpProviderAdmin, lpsProviderUID);
	if(hr != hrSuccess)
		goto exit;

exit:
	if(lpProviderAdmin)
		lpProviderAdmin->Release();

	return hr;
}

HRESULT HrRemoveECMailBox(LPPROVIDERADMIN lpProviderAdmin, LPMAPIUID lpsProviderUID)
{
	HRESULT			hr = hrSuccess;
	
	LPPROFSECT		lpGlobalProfSect = NULL;
	LPSPropValue	lpGlobalProps = NULL;	
	LPSPropValue	lpNewProp = NULL;
	ULONG			cValues = 0;
	ULONG			cSize = 0;
	unsigned int	i = 0;

	//Open global profile, add the store.(for show list, delete etc)
	hr = lpProviderAdmin->OpenProfileSection((LPMAPIUID)pbGlobalProfileSectionGuid, NULL, MAPI_MODIFY , &lpGlobalProfSect);
	if(hr != hrSuccess)
		goto exit;

	// The the prop value PR_STORE_PROVIDERS
	static constexpr SizedSPropTagArray(1, proptag) = {1, {PR_STORE_PROVIDERS}};
	hr = lpGlobalProfSect->GetProps(proptag, 0, &cValues, &lpGlobalProps);
	if(hr == hrSuccess && lpGlobalProps->Value.bin.cb >= sizeof(MAPIUID)) 
	{
		hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&lpNewProp);
		if(hr != hrSuccess)
			goto exit;

		cSize = lpGlobalProps->Value.bin.cb - sizeof(MAPIUID);

		hr = MAPIAllocateMore(cSize, lpNewProp, (void**)&lpNewProp->Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;

		lpNewProp->Value.bin.cb	= 0;
		lpNewProp->ulPropTag	= PR_STORE_PROVIDERS;

		for (i = 0; i < lpGlobalProps->Value.bin.cb / sizeof(MAPIUID); ++i) {
			if(memcmp(lpGlobalProps->Value.bin.lpb+(sizeof(MAPIUID) * i), lpsProviderUID, sizeof(MAPIUID)) != 0)
			{
				memcpy(lpNewProp->Value.bin.lpb+lpNewProp->Value.bin.cb, lpGlobalProps->Value.bin.lpb+(sizeof(MAPIUID) * i), sizeof(MAPIUID));
				lpNewProp->Value.bin.cb += sizeof(MAPIUID);
			}
		}

		hr = lpGlobalProfSect->SetProps(1, lpNewProp, NULL);
		if(hr != hrSuccess)
			goto exit;

		hr = lpGlobalProfSect->SaveChanges(0);
		if(hr != hrSuccess)
			goto exit;	
	}
	lpGlobalProfSect->Release();
	lpGlobalProfSect = NULL;
	//Remove Store
	hr = lpProviderAdmin->DeleteProvider(lpsProviderUID);
	//FIXME: unknown error 0x80070005 by delete (HACK)
	hr = hrSuccess;
exit:
	if(lpGlobalProfSect)
		lpGlobalProfSect->Release();
	MAPIFreeBuffer(lpGlobalProps);
	MAPIFreeBuffer(lpNewProp);
	return hr;
}

static HRESULT HrAddProfileUID(LPPROVIDERADMIN lpProviderAdmin, LPMAPIUID lpNewProfileUID)
{
	ProfSectPtr			ptrGlobalProfSect;
	ULONG				cValues;
	SPropValuePtr		ptrGlobalProps;
	ULONG				csNewMapiUID;
	SPropValuePtr		ptrNewProp;

	SizedSPropTagArray(1, sptaGlobalProps) = {1, {PR_STORE_PROVIDERS}};

	//Open global profile, add the store.(for show list, delete etc)
	HRESULT hr = lpProviderAdmin->OpenProfileSection(reinterpret_cast<MAPIUID *>(const_cast<char *>(pbGlobalProfileSectionGuid)),
	             NULL, MAPI_MODIFY, &ptrGlobalProfSect);
	if (hr != hrSuccess)
		return hr;

	// The prop value PR_STORE_PROVIDERS
	hr = ptrGlobalProfSect->GetProps(sptaGlobalProps, 0, &cValues, &~ptrGlobalProps);
	if (HR_FAILED(hr))
		return hr;
	if (ptrGlobalProps->ulPropTag != PR_STORE_PROVIDERS)
		ptrGlobalProps->Value.bin.cb = 0;

	// The new size of stores provider uid
	csNewMapiUID = ptrGlobalProps->Value.bin.cb + sizeof(MAPIUID); //lpNewProfileUID
	hr = MAPIAllocateBuffer(sizeof(SPropValue), &~ptrNewProp);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(csNewMapiUID, ptrNewProp, (LPVOID*)&ptrNewProp->Value.bin.lpb);
	if (hr != hrSuccess)
		return hr;

	ptrNewProp->Value.bin.cb = csNewMapiUID;
	ptrNewProp->ulPropTag = PR_STORE_PROVIDERS;

	if (ptrGlobalProps->Value.bin.cb > 0)
		memcpy(ptrNewProp->Value.bin.lpb, ptrGlobalProps->Value.bin.lpb, ptrGlobalProps->Value.bin.cb);

	memcpy(ptrNewProp->Value.bin.lpb + ptrGlobalProps->Value.bin.cb, lpNewProfileUID, sizeof(MAPIUID));

	hr = ptrGlobalProfSect->SetProps(1, ptrNewProp, NULL);
	if (hr != hrSuccess)
		return hr;
	return ptrGlobalProfSect->SaveChanges(0);
}

HRESULT HrAddECMailBox(LPMAPISESSION lpSession, LPCWSTR lpszUserName)
{
	HRESULT			hr = hrSuccess;
	LPPROVIDERADMIN lpProviderAdmin = NULL;

	hr = HrGetECProviderAdmin(lpSession, &lpProviderAdmin);
	if(hr != hrSuccess)
		goto exit;

	hr = HrAddECMailBox(lpProviderAdmin, lpszUserName);
	if(hr != hrSuccess)
		goto exit;

exit:
	if(lpProviderAdmin)
		lpProviderAdmin->Release();
	
	return hr;
}

HRESULT HrAddECMailBox(LPPROVIDERADMIN lpProviderAdmin, LPCWSTR lpszUserName)
{
	MAPIUID		sNewProfileUID;
	SPropValue	asProfileProps[1];

	if (lpProviderAdmin == NULL || lpszUserName == NULL)
		return MAPI_E_INVALID_PARAMETER;

	asProfileProps[0].ulPropTag = PR_EC_USERNAME_W;
	asProfileProps[0].Value.lpszW = (WCHAR*)lpszUserName;

	// Create the profile, now the profile is shown in outlook
	HRESULT hr = lpProviderAdmin->CreateProvider((TCHAR *)"ZARAFA6_MSMDB_Delegate",
	             ARRAY_SIZE(asProfileProps), asProfileProps, 0, 0,
	             &sNewProfileUID);
	if (hr != hrSuccess)
		return hr;
	return HrAddProfileUID(lpProviderAdmin, &sNewProfileUID);
}

HRESULT HrAddArchiveMailBox(LPPROVIDERADMIN lpProviderAdmin, LPCWSTR lpszUserName, LPCWSTR lpszServerName, LPMAPIUID lpProviderUID)
{
	MAPIUID		sNewProfileUID;
	SPropValue	asProfileProps[2];

	if (lpProviderAdmin == NULL || lpszUserName == NULL ||
	    lpszServerName == NULL)
		return MAPI_E_INVALID_PARAMETER;

	asProfileProps[0].ulPropTag = PR_EC_USERNAME_W;
	asProfileProps[0].Value.lpszW = (LPWSTR)lpszUserName;

	asProfileProps[1].ulPropTag = PR_EC_SERVERNAME_W;
	asProfileProps[1].Value.lpszW = (LPWSTR)lpszServerName;

	// Create the profile, now the profile is shown in outlook
	HRESULT hr = lpProviderAdmin->CreateProvider((TCHAR *)("ZARAFA6_MSMDB_archive"),
	             ARRAY_SIZE(asProfileProps), asProfileProps, 0, 0,
	             &sNewProfileUID);
	if (hr != hrSuccess)
		return hr;
	hr = HrAddProfileUID(lpProviderAdmin, &sNewProfileUID);
	if (hr != hrSuccess)
		return hr;
	if (lpProviderUID)
		*lpProviderUID = sNewProfileUID;
	return hrSuccess;
}

/**
 * Create a OneOff EntryID.
 *
 * @param[in]	lpszName		Displayname of object
 * @param[in]	lpszAdrType		Addresstype of EntryID. Mostly SMTP or ZARAFA.
 * @param[in]	lpszAddress		Address of EntryID, according to type.
 * @param[in]	ulFlags			Enable MAPI_UNICODE flag if input strings are WCHAR strings. Output will be unicode too.
 * @param[out]	lpcbEntryID		Length of lppEntryID
 * @param[out]	lpplpEntryID	OneOff EntryID for object.
 *
 * @return	HRESULT
 *
 * @note If UNICODE strings are used, we must use windows UCS-2 format.
 */
HRESULT ECCreateOneOff(LPTSTR lpszName, LPTSTR lpszAdrType, LPTSTR lpszAddress, ULONG ulFlags, ULONG* lpcbEntryID, LPENTRYID* lppEntryID)
{
	std::string strOneOff;
	MAPIUID uid = {MAPI_ONE_OFF_UID};
	unsigned short usFlags = (((ulFlags & MAPI_UNICODE)?MAPI_ONE_OFF_UNICODE:0) | ((ulFlags & MAPI_SEND_NO_RICH_INFO)?MAPI_ONE_OFF_NO_RICH_INFO:0));

	if (lpszAdrType == NULL || lpszAddress == NULL)
		return MAPI_E_INVALID_PARAMETER;

	strOneOff.append(4, '\0'); // abFlags
	strOneOff.append((char *)&uid, sizeof(MAPIUID));
	strOneOff.append(2, '\0'); // version (0)
	strOneOff.append((char *)&usFlags, sizeof(usFlags));

	if(ulFlags & MAPI_UNICODE)
	{
		std::wstring wstrName;
		std::u16string strUnicode;

		if (lpszName)
			wstrName = (WCHAR*)lpszName;
		else
			wstrName = (WCHAR*)lpszAddress;
		strUnicode = convert_to<std::u16string>(wstrName);
		strOneOff.append((char*)strUnicode.c_str(), (strUnicode.length()+1)*sizeof(unsigned short));
		strUnicode = convert_to<std::u16string>(reinterpret_cast<wchar_t *>(lpszAdrType));
		strOneOff.append((char*)strUnicode.c_str(), (strUnicode.length()+1)*sizeof(unsigned short));
		strUnicode = convert_to<std::u16string>(reinterpret_cast<wchar_t *>(lpszAddress));
		strOneOff.append((char*)strUnicode.c_str(), (strUnicode.length()+1)*sizeof(unsigned short));
	} else {
		if (lpszName)
			strOneOff.append((char *)lpszName, strlen((char *)lpszName) + 1);
		else
			strOneOff.append(1, '\0');
		strOneOff.append((char *)lpszAdrType, strlen((char *)lpszAdrType) + 1);
		strOneOff.append((char *)lpszAddress, strlen((char *)lpszAddress) + 1);
	}

	HRESULT hr = MAPIAllocateBuffer(strOneOff.size(),
	             reinterpret_cast<void **>(lppEntryID));
	if(hr != hrSuccess)
		return hr;

	memcpy(*lppEntryID, strOneOff.c_str(), strOneOff.size());
	*lpcbEntryID = strOneOff.size();
	return hrSuccess;
}

/**
 * Parse a OneOff EntryID. Fails if the input is not a correct OneOff EntryID. Returns strings always in unicode.
 *
 * @param[in]	cbEntryID		Length of lppEntryID
 * @param[in]	lplpEntryID	OneOff EntryID for object.
 * @param[out]	strWName		Displayname of object
 * @param[out]	strWType		Addresstype of EntryID. Mostly SMTP or ZARAFA.
 * @param[out]	strWAddress		Address of EntryID, according to type.
 *
 * @return	HRESULT
 * @retval	MAPI_E_INVALID_PARAMETER	EntryID is not a OneOff EntryID.
 */
HRESULT ECParseOneOff(const ENTRYID *lpEntryID, ULONG cbEntryID,
    std::wstring &strWName, std::wstring &strWType, std::wstring &strWAddress)
{
	HRESULT hr = hrSuccess;
	MAPIUID		muidOneOff = {MAPI_ONE_OFF_UID};
	const char *lpBuffer = reinterpret_cast<const char *>(lpEntryID);
	unsigned short usFlags;
	std::wstring name;
	std::wstring type;
	std::wstring addr;

	if (cbEntryID < (8 + sizeof(MAPIUID)) || lpEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;
	if(*reinterpret_cast<const unsigned int *>(lpBuffer) != 0)
		return MAPI_E_INVALID_PARAMETER;
	lpBuffer += 4;

	if (memcmp(&muidOneOff, lpBuffer, sizeof(MAPIUID)) != 0)
		return MAPI_E_INVALID_PARAMETER;
	lpBuffer += sizeof(MAPIUID);

	if (*reinterpret_cast<const unsigned short *>(lpBuffer) != 0)
		return MAPI_E_INVALID_PARAMETER;
	lpBuffer += 2;

	memcpy(&usFlags, lpBuffer, sizeof(usFlags));

	lpBuffer += 2;

	if(usFlags & MAPI_ONE_OFF_UNICODE) {
		std::u16string str;

		str.assign(reinterpret_cast<std::u16string::const_pointer>(lpBuffer));
		// can be 0 length
		if ((hr = TryConvert(str, name)) != hrSuccess)
			return hr;
		lpBuffer += (str.length() + 1) * sizeof(unsigned short);

		str.assign(reinterpret_cast<std::u16string::const_pointer>(lpBuffer));
		if (str.length() == 0)
			return MAPI_E_INVALID_PARAMETER;
		if ((hr = TryConvert(str, type)) != hrSuccess)
			return hr;
		lpBuffer += (str.length() + 1) * sizeof(unsigned short);

		str.assign(reinterpret_cast<std::u16string::const_pointer>(lpBuffer));
		if (str.length() == 0)
			return MAPI_E_INVALID_PARAMETER;
		if ((hr = TryConvert(str, addr)) != hrSuccess)
			return hr;
		lpBuffer += (str.length() + 1) * sizeof(unsigned short);
		
	} else {
		/*
		 * Assumption: This should be an old OneOffEntryID in the
		 * windows-1252 charset.
		 */
		string str;

		str = (char*)lpBuffer;
		// can be 0 length
		hr = TryConvert(lpBuffer, rawsize(lpBuffer), "windows-1252", name);
		if (hr != hrSuccess)
			return hr;
		lpBuffer += str.length() + 1;

		str = (char*)lpBuffer;
		if (str.length() == 0)
			return MAPI_E_INVALID_PARAMETER;
		if ((hr = TryConvert(str, type)) != hrSuccess)
			return hr;
		lpBuffer += str.length() + 1;

		str = (char*)lpBuffer;
		if (str.length() == 0)
			return MAPI_E_INVALID_PARAMETER;
		if ((hr = TryConvert(str, addr)) != hrSuccess)
			return hr;
		lpBuffer += str.length() + 1;
	}

	strWName = name;
	strWType = type;
	strWAddress = addr;
	return hrSuccess;
}

/**
 * Convert string to e-mail header format, base64 encoded with
 * specified charset.
 *
 * @param[in]	input	Input string
 * @param[in]	charset	Charset of input string
 * @return				Output string in e-mail header format
 */
std::string ToQuotedBase64Header(const std::string &input,
    const std::string &charset)
{
	std::string output;

	output = "=?"+charset+"?B?";
	output += base64_encode((const unsigned char*)input.c_str(), input.length());
	output += "?=";

	return output;
}

/**
 * Convert string to e-mail header format, base64 encoded with
 * UTF-8 charset.
 *
 * @param[in]	input	Input wide string
 * @return				Output string in e-mail header format
 */
std::string ToQuotedBase64Header(const std::wstring &input)
{
	return ToQuotedBase64Header(convert_to<std::string>("UTF-8", input, rawsize(input), CHARSET_WCHAR), "UTF-8");
}

/**
 * Convert string to e-mail format, quoted-printable encoded with
 * specified charset. Can optionally add markings used in e-mail
 * headers. If no special quoted printable entities were required, it
 * will return the input string.
 *
 * @param[in]	input	Input string
 * @param[in]	charset	Charset of input string
 * @param[in]	header	Use extra markings to make the string valid for e-mail headers
 * @param[in]	imap	Also encode \ and " for IMAP
 * @return quoted printable valid string
 */
std::string ToQuotedPrintable(const std::string &input,
    const std::string &charset, bool header, bool imap)
{
	ULONG i;
	string tmp;
	bool qp = false;
	const char digits[] = "0123456789ABCDEF";

	if (charset.empty())
		return input;

	// only email headers have this prefix
	if (header)
		tmp = "=?"+charset+"?Q?";

	for (i = 0; i < input.size(); ++i) {
		if ((unsigned char)input[i] > 127) {
			tmp.push_back('=');
			tmp.push_back(digits[((unsigned char)input[i]>>4)]);
			tmp.push_back(digits[((unsigned char)input[i]&0x0F)]);
			qp = true;
		} else {
			switch ((unsigned char)input[i]) {
			case ' ':
				if (header)
					tmp.push_back('_'); // only email headers need this, don't set qp marker if only spaces are found
				else
					tmp.push_back(input[i]); // leave email body unaffected
				break;
			case '\r':
			case '\n':
				// no idea how a user would enter a \r\n in the subject, but it s doable of course ;)
				if (header) {
					tmp.push_back('=');
					tmp.push_back(digits[((unsigned char)input[i]>>4)]);
					tmp.push_back(digits[((unsigned char)input[i]&0x0F)]);
					qp = true;
				} else
					tmp.push_back(input[i]); // leave email body unaffected
				break;
			case '\t':
			case ',':
			case ';':
			case ':':
			case '_':
			case '@':
			case '(':
			case ')':
			case '<':
			case '>':
			case '[':
			case ']':
			case '?':
			case '=':
				tmp.push_back('=');
				tmp.push_back(digits[((unsigned char)input[i]>>4)]);
				tmp.push_back(digits[((unsigned char)input[i]&0x0F)]);
				qp = true;
				break;
			case '\\':
			case '"':
				// IMAP requires encoding of these 2 characters
				if (imap) {
					tmp.push_back('=');
					tmp.push_back(digits[((unsigned char)input[i]>>4)]);
					tmp.push_back(digits[((unsigned char)input[i]&0x0F)]);
					qp = true;
				} else
					tmp.push_back(input[i]);
				break;
			default:
				tmp.push_back(input[i]);
			};
		}
	}

	if (header)
		tmp += "?=";

	if (qp)
		return tmp;
	else
		return input;           // simple string was good enough
}

/**
 * Send a new mail notification to the store.
 *
 * Sends a notification to the given store with information of the
 * given lpMessage. This is to get the new mail popup in Outlook. It
 * is different from the create notification.
 *
 * @param[in]	lpMDB		The store where lpMessage was just created.
 * @param[in]	lpMessage	The message that was just created and saved.
 *
 * @return		Mapi error code.
 */
HRESULT HrNewMailNotification(IMsgStore* lpMDB, IMessage* lpMessage)
{
	HRESULT hr = hrSuccess;

	// Newmail notify
	ULONG			cNewMailValues = 0;
	LPSPropValue	lpNewMailPropArray = NULL;
	NOTIFICATION	sNotification;

	// Get notify properties
	hr = lpMessage->GetProps(sPropNewMailColumns, 0, &cNewMailValues, &lpNewMailPropArray);
	if (hr != hrSuccess)
		goto exit;

	// Notification type
	sNotification.ulEventType = fnevNewMail;
	
	// PR_ENTRYID
	sNotification.info.newmail.cbEntryID = lpNewMailPropArray[NEWMAIL_ENTRYID].Value.bin.cb;
	sNotification.info.newmail.lpEntryID = (LPENTRYID)lpNewMailPropArray[NEWMAIL_ENTRYID].Value.bin.lpb;
	
	// PR_PARENT_ENTRYID
	sNotification.info.newmail.cbParentID = lpNewMailPropArray[NEWMAIL_PARENT_ENTRYID].Value.bin.cb;
	sNotification.info.newmail.lpParentID = (LPENTRYID)lpNewMailPropArray[NEWMAIL_PARENT_ENTRYID].Value.bin.lpb;

	// flags if unicode
	sNotification.info.newmail.ulFlags = 0;

	// PR_MESSAGE_CLASS
	sNotification.info.newmail.lpszMessageClass = (LPTSTR)lpNewMailPropArray[NEWMAIL_MESSAGE_CLASS].Value.lpszA;

	// PR_MESSAGE_FLAGS
	sNotification.info.newmail.ulMessageFlags = lpNewMailPropArray[NEWMAIL_MESSAGE_FLAGS].Value.ul;

	hr = lpMDB->NotifyNewMail(&sNotification);
	// TODO: this error should be a warning?

exit:
	// Newmail notify
	MAPIFreeBuffer(lpNewMailPropArray);
	return hr;
}

static void strupr(char *a)
{
	while (*a != '\0') {
		*a = toupper(*a);
		++a;
	}
}

// Create Search key for recipients
HRESULT HrCreateEmailSearchKey(const char *lpszEmailType,
    const char *lpszEmail, ULONG *cb, LPBYTE *lppByte)
{
	HRESULT	hr = hrSuccess;
	LPBYTE	lpByte = NULL;
	ULONG	size;
	ULONG	sizeEmailType;
	ULONG	sizeEmail;

	size = 2; // : and \0
	sizeEmailType = (lpszEmailType)?strlen(lpszEmailType) : 0;
	sizeEmail = (lpszEmail)?strlen(lpszEmail) : 0;

	size = sizeEmailType + sizeEmail + 2; // : and \0
	
	hr = MAPIAllocateBuffer(size, (void**)&lpByte);
	if(hr != hrSuccess)
		goto exit;

	memcpy(lpByte, lpszEmailType, sizeEmailType);
	*(lpByte + sizeEmailType) = ':';
	memcpy(lpByte + sizeEmailType + 1, lpszEmail, sizeEmail);
	*(lpByte + size - 1) = 0;

	strupr((char*)lpByte);

	*lppByte = lpByte;
	*cb = size;
exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(lpByte);

	return hr;
}

/**
 * Get SMTP emailaddress strings in a set of properties
 *
 * @param[in] lpSession MAPI Session to use for the lookup (note: uses adressbook from this session)
 * @param[in] lpProps Properties to use to lookup email address strings
 * @param[in] cValues Number of properties pointed to by lpProps
 * @param[in] ulPropTagEntryID Property tag fo the entryid part of the recipient (eg PR_ENTRYID)
 * @param[in] ulPropTagName Property tag of the display name part of the recipeint (eg PR_DISPLAY_NAME)
 * @param[in] ulPropTagType Property tag of the address type of the recipient (eg PR_ADDRTYPE)
 * @param[in] ulPropTagEmailAddress Property tag of the email address part of the recipient (eg PR_EMAIL_ADDRESS)
 * @param[out] strName Return string for display name
 * @param[out] strType Return string for address type
 * @param[out] strEmailAddress Return string for email address
 *
 * This function is a utility function to retrieve the name/type/address information for a recipient. The recipient
 * may be a direct entry in a recipient table or point to an addressbook item. 
 *
 * Data is retrieved from the following places (in order)
 * 1. Addressbook (if ulPropTagEntryID is available)
 * 2. Passed properties
 *
 * Also, the address will be resolved to SMTP if steps 1 and 2 did not provide one.
 */
HRESULT HrGetAddress(IMAPISession *lpSession, LPSPropValue lpProps, ULONG cValues, ULONG ulPropTagEntryID, ULONG ulPropTagName, ULONG ulPropTagType, ULONG ulPropTagEmailAddress,
					 std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress)
{
	HRESULT hr = hrSuccess;
	LPADRBOOK lpAdrBook = NULL;

	if (!lpSession || !lpProps) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	
	// If entryid is invalid, there is no need in opening the addressbook,
	// though we still call HrGetAddress with lpAdrBook
	if (PpropFindProp(lpProps, cValues, ulPropTagEntryID))
		// First, try through the entryid
		lpSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, &lpAdrBook);
		// fallthrough .. don't mind if no Addressbook could not be created (probably never happens)
	 
	hr = HrGetAddress(lpAdrBook, lpProps, cValues, ulPropTagEntryID, ulPropTagName, ulPropTagType, ulPropTagEmailAddress, strName, strType, strEmailAddress);

exit:
	if(lpAdrBook)
		lpAdrBook->Release();

	return hr;
}

/**
 * Get SMTP emailaddress strings in a IMessage object
 *
 * @param[in] lpSession MAPI Session to use for the lookup (note: uses adressbook from this session)
 * @param[in] lpMessage IMessage object to get address from
 * @param[in] ulPropTagEntryID Property tag fo the entryid part of the recipient (eg PR_ENTRYID)
 * @param[in] ulPropTagName Property tag of the display name part of the recipeint (eg PR_DISPLAY_NAME)
 * @param[in] ulPropTagType Property tag of the address type of the recipient (eg PR_ADDRTYPE)
 * @param[in] ulPropTagEmailAddress Property tag of the email address part of the recipient (eg PR_EMAIL_ADDRESS)
 * @param[out] strName Return string for display name
 * @param[out] strType Return string for address type
 * @param[out] strEmailAddress Return string for email address
 *
 * This function is a utility function to retrieve the name/type/address information for a recipient. The recipient
 * may be a direct entry in a recipient table or point to an addressbook item. 
 *
 * Data is retrieved from the following places (in order)
 * 1. Addressbook (if ulPropTagEntryID is available)
 * 2. Passed properties
 *
 * Also, the address will be resolved to SMTP if steps 1 and 2 did not provide one.
 */
HRESULT HrGetAddress(IMAPISession *lpSession, IMessage *lpMessage, ULONG ulPropTagEntryID, ULONG ulPropTagName, ULONG ulPropTagType, ULONG ulPropTagEmailAddress,
					 std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress)
{
	HRESULT hr = hrSuccess;
	SizedSPropTagArray(4, sptaProps) = { 4, { ulPropTagEntryID, ulPropTagName, ulPropTagType, ulPropTagEmailAddress } };
	ULONG cValues = 0;
	LPSPropValue lpProps = NULL;

	if (!lpSession || !lpMessage) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpMessage->GetProps(sptaProps, 0, &cValues, &lpProps);
	if(FAILED(hr))
		goto exit;

	// @todo: inline here
	hr = HrGetAddress(lpSession, lpProps, cValues, ulPropTagEntryID, ulPropTagName, ulPropTagType, ulPropTagEmailAddress, strName, strType, strEmailAddress);

exit:
	MAPIFreeBuffer(lpProps);
	return hr;
}

/**
 * Get SMTP emailaddress strings in a IMessage object
 *
 * @param[in] lpAdrBook Addressbook object to use for lookup
 * @param[in] lpMessage IMessage object to get address from
 * @param[in] ulPropTagEntryID Property tag fo the entryid part of the recipient (eg PR_ENTRYID)
 * @param[in] ulPropTagName Property tag of the display name part of the recipeint (eg PR_DISPLAY_NAME)
 * @param[in] ulPropTagType Property tag of the address type of the recipient (eg PR_ADDRTYPE)
 * @param[in] ulPropTagEmailAddress Property tag of the email address part of the recipient (eg PR_EMAIL_ADDRESS)
 * @param[out] strName Return string for display name
 * @param[out] strType Return string for address type
 * @param[out] strEmailAddress Return string for email address
 *
 * This function is a utility function to retrieve the name/type/address information for a recipient. The recipient
 * may be a direct entry in a recipient table or point to an addressbook item. 
 *
 * Data is retrieved from the following places (in order)
 * 1. Addressbook (if ulPropTagEntryID is available)
 * 2. Passed properties
 *
 * Also, the address will be resolved to SMTP if steps 1 and 2 did not provide one.
 */
HRESULT HrGetAddress(LPADRBOOK lpAdrBook, IMessage *lpMessage, ULONG ulPropTagEntryID, ULONG ulPropTagName, ULONG ulPropTagType, ULONG ulPropTagEmailAddress,
					 std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress)
{
	HRESULT hr = hrSuccess;
	SizedSPropTagArray(4, sptaProps) = { 4, { ulPropTagEntryID, ulPropTagName, ulPropTagType, ulPropTagEmailAddress } };
	ULONG cValues = 0;
	LPSPropValue lpProps = NULL;

	if (!lpAdrBook || !lpMessage) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpMessage->GetProps(sptaProps, 0, &cValues, &lpProps);
	if (FAILED(hr))
		goto exit;

	hr = HrGetAddress(lpAdrBook, lpProps, cValues, ulPropTagEntryID, ulPropTagName, ulPropTagType, ulPropTagEmailAddress, strName, strType, strEmailAddress);

exit:
	MAPIFreeBuffer(lpProps);
	return hr;
}

/*
 * Attempts to get the SMTP email address for an addressbook entity. 
 *
 * @param[in] lpAdrBook Addressbook object to use to lookup the address
 * @param[in] strResolve String to resolve
 * @param[in] ulFlags 0 or EMS_AB_ADDRESS_LOOKUP for exact-match only
 * @param[out] strSMTPAddress Resolved SMTP address
 *
 * This function will attempt to resolve the string strReolve to an SMTP address. This can be either a group or user
 * SMTP address, and will only be returned if the match is unambiguous. You may also pass the flags EMS_AB_ADDRESS_LOOKUP
 * to ensure only exact (full-string) matches will be returned.
 *
 * The match is done against various strings including display name and email address.
 */
static HRESULT HrResolveToSMTP(LPADRBOOK lpAdrBook,
    const std::wstring &strResolve, unsigned int ulFlags,
    std::wstring &strSMTPAddress)
{
    HRESULT hr = hrSuccess;
    LPADRLIST lpAdrList = NULL;
    LPSPropValue lpEntryID = NULL;
    ULONG ulType = 0;
    IMAPIProp *lpMailUser = NULL;
    LPSPropValue lpSMTPAddress = NULL;
    LPSPropValue lpEmailAddress = NULL;
     
    hr = MAPIAllocateBuffer(sizeof(ADRLIST), (void **)&lpAdrList);
    if(hr != hrSuccess)
        goto exit;
    
    lpAdrList->cEntries = 1;
    lpAdrList->aEntries[0].cValues = 1;

    hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpAdrList->aEntries[0].rgPropVals);
    if(hr != hrSuccess)
        goto exit;
        
    lpAdrList->aEntries[0].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_W;
    lpAdrList->aEntries[0].rgPropVals[0].Value.lpszW = (WCHAR *)strResolve.c_str();
    
    hr = lpAdrBook->ResolveName(0, ulFlags | MAPI_UNICODE, NULL, lpAdrList);
    if(hr != hrSuccess)
        goto exit;
        
    if(lpAdrList->cEntries != 1) {
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    lpEntryID = PpropFindProp(lpAdrList->aEntries[0].rgPropVals, lpAdrList->aEntries[0].cValues, PR_ENTRYID);
    if(!lpEntryID) {
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    hr = lpAdrBook->OpenEntry(lpEntryID->Value.bin.cb, (LPENTRYID)lpEntryID->Value.bin.lpb, &IID_IMAPIProp, 0, &ulType, (LPUNKNOWN *)&lpMailUser);
    if (hr != hrSuccess)
        goto exit;
    hr = HrGetOneProp(lpMailUser, PR_SMTP_ADDRESS_W, &lpSMTPAddress);
    if(hr != hrSuccess) {
        // Not always an error
        lpSMTPAddress = NULL;
        hr = hrSuccess;
    }

    if (ulType == MAPI_DISTLIST && (lpSMTPAddress == NULL || wcslen(lpSMTPAddress->Value.lpszW) == 0)) {
        // For a group, we define the SMTP Address to be the same as the name of the group, unless
        // an explicit email address has been set for the group. This sounds unlogical, but it isn't 
        // really that strings since whenever we convert to SMTP for a group, we just put the group 
        // name as if it were an SMTP address. 
        // (Eg. 'To: Everyone; user@domain.com')
        hr = HrGetOneProp(lpMailUser, PR_EMAIL_ADDRESS_W, &lpEmailAddress);
        if(hr != hrSuccess)
            goto exit;
            
        strSMTPAddress = lpEmailAddress->Value.lpszW;
    } else {
        if(lpSMTPAddress == NULL) {
            hr = MAPI_E_NOT_FOUND;
            goto exit;
        }
        strSMTPAddress = lpSMTPAddress->Value.lpszW;
    }
    
exit:
	if (lpAdrList)
		FreePadrlist(lpAdrList);
    
	MAPIFreeBuffer(lpEmailAddress);
	MAPIFreeBuffer(lpSMTPAddress);
	if (lpMailUser)
		lpMailUser->Release();
        
	return hr;
}

/**
 * Get SMTP emailaddress strings in a set of properties
 *
 * @param[in] lpAdrBook Addressbook object to use to lookup the address
 * @param[in] lpProps Properties to use to lookup email address strings
 * @param[in] cValues Number of properties pointed to by lpProps
 * @param[in] ulPropTagEntryID Property tag fo the entryid part of the recipient (eg PR_ENTRYID)
 * @param[in] ulPropTagName Property tag of the display name part of the recipeint (eg PR_DISPLAY_NAME)
 * @param[in] ulPropTagType Property tag of the address type of the recipient (eg PR_ADDRTYPE)
 * @param[in] ulPropTagEmailAddress Property tag of the email address part of the recipient (eg PR_EMAIL_ADDRESS)
 * @param[out] strName Return string for display name
 * @param[out] strType Return string for address type
 * @param[out] strEmailAddress Return string for email address
 *
 * This function is a utility function to retrieve the name/type/address information for a recipient. The recipient
 * may be a direct entry in a recipient table or point to an addressbook item. 
 *
 * Data is retrieved from the following places (in order)
 * 1. Addressbook (if ulPropTagEntryID is available)
 * 2. Passed properties
 *
 * Also, the address will be resolved to SMTP if steps 1 and 2 did not provide one.
 */
HRESULT HrGetAddress(LPADRBOOK lpAdrBook, LPSPropValue lpProps, ULONG cValues, ULONG ulPropTagEntryID, ULONG ulPropTagName, ULONG ulPropTagType, ULONG ulPropTagEmailAddress,
					 std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress)
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpEntryID = NULL;
	LPSPropValue lpName = NULL;
	LPSPropValue lpType = NULL;
	LPSPropValue lpAddress = NULL;
	std::wstring strSMTPAddress;
	convert_context converter;

	strName.clear();
	strType.clear();
	strEmailAddress.clear();

	if (lpProps && cValues) {
		lpEntryID	= PpropFindProp(lpProps, cValues, ulPropTagEntryID);
		lpName		= PpropFindProp(lpProps, cValues, ulPropTagName);
		lpType		= PpropFindProp(lpProps, cValues, ulPropTagType);
		lpAddress	= PpropFindProp(lpProps, cValues, ulPropTagEmailAddress);
		if (lpEntryID && PROP_TYPE(lpEntryID->ulPropTag) != PT_BINARY)
			lpEntryID = NULL;
		if (lpName && PROP_TYPE(lpName->ulPropTag) != PT_STRING8 && PROP_TYPE(lpName->ulPropTag) != PT_UNICODE)
			lpName = NULL;
		if (lpType && PROP_TYPE(lpType->ulPropTag) != PT_STRING8 && PROP_TYPE(lpType->ulPropTag) != PT_UNICODE)
			lpType = NULL;
		if (lpAddress && PROP_TYPE(lpAddress->ulPropTag) != PT_STRING8 && PROP_TYPE(lpAddress->ulPropTag) != PT_UNICODE)
			lpAddress = NULL;
	}

	if (lpEntryID == NULL || lpAdrBook == NULL ||
		HrGetAddress(lpAdrBook, (LPENTRYID)lpEntryID->Value.bin.lpb, lpEntryID->Value.bin.cb, strName, strType, strEmailAddress) != hrSuccess)
	{
        // EntryID failed, try fallback
        if (lpName) {
			if (PROP_TYPE(lpName->ulPropTag) == PT_UNICODE)
				strName = lpName->Value.lpszW;
			else
				strName = converter.convert_to<wstring>(lpName->Value.lpszA);
		}
        if (lpType) {
			if (PROP_TYPE(lpType->ulPropTag) == PT_UNICODE)
				strType = lpType->Value.lpszW;
			else
				strType = converter.convert_to<wstring>(lpType->Value.lpszA);
		}
        if (lpAddress) {
			if (PROP_TYPE(lpAddress->ulPropTag) == PT_UNICODE)
				strEmailAddress = lpAddress->Value.lpszW;
			else
				strEmailAddress = converter.convert_to<wstring>(lpAddress->Value.lpszA);
		}
    }
    		
    // If we don't have an SMTP address yet, try to resolve the item to get the SMTP address
	if (lpAdrBook != nullptr && lpType != nullptr &&
	    lpAddress != nullptr && wcscasecmp(strType.c_str(), L"SMTP") != 0 &&
	    HrResolveToSMTP(lpAdrBook, strEmailAddress, EMS_AB_ADDRESS_LOOKUP, strSMTPAddress) == hrSuccess)
		strEmailAddress = strSMTPAddress;
	return hr;
}

/**
 *
 * Gets address from addressbook for specified GAB entryid
 *
 * @param[in] lpAdrBook Addressbook object to use for the lookup
 * @param[in] lpEntryID EntryID of the object to lookup in the addressbook
 * @param[in] cbEntryID Number of bytes pointed to by lpEntryID
 * @param[out] strName Return for display name
 * @param[out] strType Return for address type (ZARAFA)
 * @param[out] strEmailAddress Return for email address
 *
 * This function opens the passed entryid on the passed addressbook and retrieves the recipient
 * address parts to be returned to the caller. If an SMTP address is available, returns the SMTP
 * address for the user, otherwise the ZARAFA addresstype and address is returned.
 */
HRESULT HrGetAddress(LPADRBOOK lpAdrBook, LPENTRYID lpEntryID, ULONG cbEntryID, std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress)
{
	HRESULT hr = hrSuccess;
	IMailUser	*lpMailUser = NULL;
	ULONG		ulType = 0;
	ULONG		cMailUserValues = 0;
	LPSPropValue lpMailUserProps = NULL;
	SizedSPropTagArray(4, sptaAddressProps) = { 4, { PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W, PR_SMTP_ADDRESS_W } };

	if (!lpAdrBook || !lpEntryID) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpAdrBook->OpenEntry(cbEntryID, lpEntryID, &IID_IMailUser, 0, &ulType, (IUnknown **)&lpMailUser);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMailUser->GetProps(sptaAddressProps, 0, &cMailUserValues, &lpMailUserProps);

	if (FAILED(hr))
		goto exit;

	if(lpMailUserProps[0].ulPropTag == PR_DISPLAY_NAME_W)
		strName = lpMailUserProps[0].Value.lpszW;
	if(lpMailUserProps[1].ulPropTag == PR_ADDRTYPE_W)
		strType = lpMailUserProps[1].Value.lpszW;

	if(lpMailUserProps[3].ulPropTag == PR_SMTP_ADDRESS_W) {
		strEmailAddress = lpMailUserProps[3].Value.lpszW;
		strType = L"SMTP";
    }
	else if(lpMailUserProps[2].ulPropTag == PR_EMAIL_ADDRESS_W)
		strEmailAddress = lpMailUserProps[2].Value.lpszW;

	hr = hrSuccess;

exit:
	if(lpMailUser)
		lpMailUser->Release();
	MAPIFreeBuffer(lpMailUserProps);
	return hr;
}

HRESULT DoSentMail(IMAPISession *lpSession, IMsgStore *lpMDBParam, ULONG ulFlags, LPMESSAGE lpMessage) {
	HRESULT			hr = hrSuccess;
	LPMDB			lpMDB = NULL;
	LPMAPIFOLDER 	lpFolder = NULL;
	ENTRYLIST		sMsgList;
	SBinary			sEntryID;
	LPSPropValue	lpPropValue = NULL;
	ULONG			cValues = 0;
	ULONG			ulType = 0;
	
	enum esPropDoSentMail{ DSM_ENTRYID, DSM_PARENT_ENTRYID, DSM_SENTMAIL_ENTRYID, DSM_DELETE_AFTER_SUBMIT, DSM_STORE_ENTRYID};
	SizedSPropTagArray(5, sPropDoSentMail) = {5, {PR_ENTRYID, PR_PARENT_ENTRYID, PR_SENTMAIL_ENTRYID, PR_DELETE_AFTER_SUBMIT, PR_STORE_ENTRYID} };

	assert(lpSession != NULL || lpMDBParam != NULL);
    
	// Check incomming parameter
	if(lpMessage == NULL) {
		hr = MAPI_E_INVALID_OBJECT;
		goto exit;
	}

	// Get Sentmail properties
	hr = lpMessage->GetProps(sPropDoSentMail, 0, &cValues, &lpPropValue);
	if(FAILED(hr) || 
		(lpPropValue[DSM_SENTMAIL_ENTRYID].ulPropTag != PR_SENTMAIL_ENTRYID && 
		lpPropValue[DSM_DELETE_AFTER_SUBMIT].ulPropTag != PR_DELETE_AFTER_SUBMIT)
	  )
	{
		// Ignore error, leave the mail where it is
		hr = hrSuccess;
		lpMessage->Release();
		goto exit;
	}else if(lpPropValue[DSM_ENTRYID].ulPropTag != PR_ENTRYID || 
			 lpPropValue[DSM_PARENT_ENTRYID].ulPropTag != PR_PARENT_ENTRYID ||
			 lpPropValue[DSM_STORE_ENTRYID].ulPropTag != PR_STORE_ENTRYID)
	{
		// Those properties always needed
		hr = MAPI_E_NOT_FOUND;
		lpMessage->Release();
		goto exit;
	}

	lpMessage->Release(); // Yes, we release the message for the caller

	if (lpMDBParam == NULL)
		hr = lpSession->OpenMsgStore(0, lpPropValue[DSM_STORE_ENTRYID].Value.bin.cb, (LPENTRYID)lpPropValue[DSM_STORE_ENTRYID].Value.bin.lpb, NULL, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL |MDB_TEMPORARY, &lpMDB);
	else
		hr = lpMDBParam->QueryInterface(IID_IMsgStore, (void**)&lpMDB);
	if(hr != hrSuccess)
		goto exit;

	sEntryID.cb = lpPropValue[DSM_ENTRYID].Value.bin.cb;
	sEntryID.lpb = lpPropValue[DSM_ENTRYID].Value.bin.lpb;
	sMsgList.cValues = 1;
	sMsgList.lpbin = &sEntryID;

	// Handle PR_SENTMAIL_ENTRYID
	if(lpPropValue[DSM_SENTMAIL_ENTRYID].ulPropTag == PR_SENTMAIL_ENTRYID)
	{
		//Open Sentmail Folder
		hr = lpMDB->OpenEntry(lpPropValue[DSM_SENTMAIL_ENTRYID].Value.bin.cb, (LPENTRYID)lpPropValue[DSM_SENTMAIL_ENTRYID].Value.bin.lpb, NULL, MAPI_MODIFY, &ulType, (IUnknown **)&lpFolder);
		if(hr != hrSuccess)
			goto exit;

		// Move Message
		hr = lpFolder->CopyMessages(&sMsgList, &IID_IMAPIFolder, lpFolder, 0, NULL, MESSAGE_MOVE);
	}

	// Handle PR_DELETE_AFTER_SUBMIT
	if(lpPropValue[DSM_DELETE_AFTER_SUBMIT].ulPropTag == PR_DELETE_AFTER_SUBMIT && lpPropValue[DSM_DELETE_AFTER_SUBMIT].Value.b == TRUE)
	{
		if(lpFolder == NULL)
		{
			// Open parent folder of the sent message
			hr = lpMDB->OpenEntry(lpPropValue[DSM_PARENT_ENTRYID].Value.bin.cb, (LPENTRYID)lpPropValue[DSM_PARENT_ENTRYID].Value.bin.lpb, NULL, MAPI_MODIFY, &ulType, (IUnknown **)&lpFolder);
			if(hr != hrSuccess)
				goto exit;
		}

		// Delete Message
		hr = lpFolder->DeleteMessages(&sMsgList, 0, NULL, 0); 
	}

exit:
	if(lpFolder)
		lpFolder->Release();

	if(lpMDB)
		lpMDB->Release();
	MAPIFreeBuffer(lpPropValue);
	return hr;
}

// This is a class that implements IMAPIProp's GetProps(), and nothing else. Its data
// is retrieved from the passed lpProps/cValues property array
class ECRowWrapper _kc_final : public IMAPIProp {
public:
	ECRowWrapper(LPSPropValue lpProps, ULONG cValues) : m_cValues(cValues), m_lpProps(lpProps) {};
	~ECRowWrapper() {};

	ULONG __stdcall AddRef() { return 1; }; // no ref. counting
	ULONG __stdcall Release() { return 1; };
	HRESULT __stdcall QueryInterface(const IID &iid, LPVOID *lpvoid) { return MAPI_E_INTERFACE_NOT_SUPPORTED; };

	HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) { return MAPI_E_NOT_FOUND; }
	HRESULT __stdcall SaveChanges(ULONG ulFlags) { return MAPI_E_NO_SUPPORT; }
	HRESULT __stdcall GetProps(LPSPropTagArray lpTags, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppProps) { 
		HRESULT hr = hrSuccess;
		BOOL bError;
		ULONG i = 0;
		LPSPropValue lpProps = NULL;
		LPSPropValue lpFind = NULL;
		convert_context converter;

		if ((hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpTags->cValues, (void **) &lpProps)) != hrSuccess)
			return hr;

		for (i = 0; i<lpTags->cValues; ++i) {
			bError = FALSE;
			lpFind = PpropFindProp(m_lpProps, m_cValues, CHANGE_PROP_TYPE(lpTags->aulPropTag[i], PT_UNSPECIFIED));
			if(lpFind && PROP_TYPE(lpFind->ulPropTag) != PT_ERROR) {
				
				if (PROP_TYPE(lpFind->ulPropTag) == PT_STRING8 && PROP_TYPE(lpTags->aulPropTag[i]) == PT_UNICODE) {
					lpProps[i].ulPropTag = lpTags->aulPropTag[i];
					std::wstring wstrTmp = converter.convert_to<std::wstring>(lpFind->Value.lpszA);
					MAPIAllocateMore((wstrTmp.length() + 1) * sizeof *lpProps[i].Value.lpszW, lpProps, (LPVOID*)&lpProps[i].Value.lpszW);
					wcscpy(lpProps[i].Value.lpszW, wstrTmp.c_str());
				} else if (PROP_TYPE(lpFind->ulPropTag) ==  PT_UNICODE && PROP_TYPE(lpTags->aulPropTag[i]) == PT_STRING8) {
					lpProps[i].ulPropTag = lpTags->aulPropTag[i];
					std::string strTmp = converter.convert_to<std::string>(lpFind->Value.lpszW);
					MAPIAllocateMore(strTmp.length() + 1, lpProps, (LPVOID*)&lpProps[i].Value.lpszA);
					strcpy(lpProps[i].Value.lpszA, strTmp.c_str());
				} else if (PROP_TYPE(lpFind->ulPropTag) != PROP_TYPE(lpTags->aulPropTag[i])) {
					bError = TRUE;
				} else if(Util::HrCopyProperty(&lpProps[i], lpFind, lpProps) != hrSuccess) {
					bError = TRUE;
				}

			} else {
				bError = TRUE;
			}
			
			if(bError) {
				lpProps[i].ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(lpTags->aulPropTag[i]));
				lpProps[i].Value.err = MAPI_E_NOT_FOUND;

				hr = MAPI_W_ERRORS_RETURNED;
			}
		}

		*lppProps = lpProps;
		*lpcValues = lpTags->cValues;

		return hr;
	};
	HRESULT __stdcall GetPropList(ULONG ulFlags, LPSPropTagArray *lppTags) { return MAPI_E_NO_SUPPORT; }
	HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk){ return MAPI_E_NO_SUPPORT; }
	HRESULT __stdcall SetProps(ULONG cValues, LPSPropValue lpProps, LPSPropProblemArray *lppProblems) { return MAPI_E_NO_SUPPORT; }
	HRESULT __stdcall DeleteProps(LPSPropTagArray lpToDelete, LPSPropProblemArray *lppProblems) { return MAPI_E_NO_SUPPORT; }
	HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, LPSPropTagArray lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) { return MAPI_E_NO_SUPPORT; }
	HRESULT __stdcall CopyProps(LPSPropTagArray lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) { return MAPI_E_NO_SUPPORT; }
	HRESULT __stdcall GetNamesFromIDs( LPSPropTagArray *lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) { return MAPI_E_NO_SUPPORT; }
	HRESULT __stdcall GetIDsFromNames( ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags) { return MAPI_E_NO_SUPPORT; }
private:
	ULONG			m_cValues;
	LPSPropValue	m_lpProps;
};

static HRESULT TestRelop(ULONG relop, int result, bool* fMatch)
{
	HRESULT hr = hrSuccess;
	switch (relop) {
	case RELOP_LT:
		*fMatch = result < 0;
		break;
	case RELOP_LE:
		*fMatch = result <= 0;
		break;
	case RELOP_GT:
		*fMatch = result > 0;
		break;
	case RELOP_GE:
		*fMatch = result >= 0;
		break;
	case RELOP_EQ:
		*fMatch = result == 0;
		break;
	case RELOP_NE:
		*fMatch = result != 0;
		break;
	case RELOP_RE:
	default:
		*fMatch = false;
		hr = MAPI_E_TOO_COMPLEX;
		break;
	};
	return hr;
}

#define RESTRICT_MAX_RECURSE_LEVEL 16
static HRESULT GetRestrictTagsRecursive(const SRestriction *lpRestriction,
    std::list<unsigned int> *lpList, ULONG ulLevel)
{
	HRESULT hr = hrSuccess;
	ULONG i;

	if(ulLevel > RESTRICT_MAX_RECURSE_LEVEL)
		return MAPI_E_TOO_COMPLEX;

	switch(lpRestriction->rt) {
		case RES_AND:
			for (i = 0; i < lpRestriction->res.resAnd.cRes; ++i) {
				hr = GetRestrictTagsRecursive(&lpRestriction->res.resAnd.lpRes[i], lpList, ulLevel+1);
				if(hr != hrSuccess)
					return hr;
			}
			break;
		case RES_OR:
			for (i = 0; i < lpRestriction->res.resOr.cRes; ++i) {
				hr = GetRestrictTagsRecursive(&lpRestriction->res.resOr.lpRes[i], lpList, ulLevel+1);
				if(hr != hrSuccess)
					return hr;
			}
			break;
		case RES_NOT:
			hr = GetRestrictTagsRecursive(lpRestriction->res.resNot.lpRes, lpList, ulLevel+1);
			break;
		case RES_CONTENT:
			lpList->push_back(lpRestriction->res.resContent.ulPropTag);
			lpList->push_back(lpRestriction->res.resContent.lpProp->ulPropTag);
			break;
		case RES_PROPERTY:
			lpList->push_back(lpRestriction->res.resProperty.ulPropTag);
			lpList->push_back(lpRestriction->res.resProperty.lpProp->ulPropTag);
			break;
		case RES_COMPAREPROPS:
			lpList->push_back(lpRestriction->res.resCompareProps.ulPropTag1);
			lpList->push_back(lpRestriction->res.resCompareProps.ulPropTag2);
			break;
		case RES_BITMASK:
			lpList->push_back(lpRestriction->res.resBitMask.ulPropTag);
			break;
		case RES_SIZE:
			lpList->push_back(lpRestriction->res.resSize.ulPropTag);
			break;
		case RES_EXIST:
			lpList->push_back(lpRestriction->res.resExist.ulPropTag);
			break;
		case RES_SUBRESTRICTION:
			lpList->push_back(lpRestriction->res.resSub.ulSubObject);
			break;
		case RES_COMMENT:
			hr = GetRestrictTagsRecursive(lpRestriction->res.resComment.lpRes, lpList, ulLevel+1);
			break;
	}
	return hr;
}

static HRESULT GetRestrictTags(const SRestriction *lpRestriction,
    LPSPropTagArray *lppTags)
{
	std::list<unsigned int> lstTags;
	ULONG n = 0;

	LPSPropTagArray lpTags = NULL;

	HRESULT hr = GetRestrictTagsRecursive(lpRestriction, &lstTags, 0);
	if(hr != hrSuccess)
		return hr;
	if ((hr = MAPIAllocateBuffer(CbNewSPropTagArray(lstTags.size()), (void **) &lpTags)) != hrSuccess)
		return hr;
	lpTags->cValues = lstTags.size();

	lstTags.sort();
	lstTags.unique();

	for (auto tag : lstTags)
		lpTags->aulPropTag[n++] = tag;
	
	lpTags->cValues = n;

	*lppTags = lpTags;
	return hrSuccess;
}

HRESULT TestRestriction(LPSRestriction lpCondition, ULONG cValues, LPSPropValue lpPropVals, const ECLocale &locale, ULONG ulLevel) {
	HRESULT hr = hrSuccess;
	ECRowWrapper *lpRowWrapper = new ECRowWrapper(lpPropVals, cValues);

	hr = TestRestriction(lpCondition, (IMAPIProp *)lpRowWrapper, locale, ulLevel);
	delete lpRowWrapper;
	return hr;
}

HRESULT TestRestriction(LPSRestriction lpCondition, IMAPIProp *lpMessage, const ECLocale &locale, ULONG ulLevel) {
	HRESULT hr = hrSuccess;
	ULONG c;
	bool fMatch = false;
	LPSPropValue lpProp = NULL;
	LPSPropValue lpProp2 = NULL;
	ULONG ulPropType;
	int result;
	unsigned int ulSize;
	IMAPITable *lpTable = NULL;
	LPSPropTagArray lpTags = NULL;
	LPSRowSet lpRowSet = NULL;
	ECRowWrapper *lpRowWrapper = NULL;

	if (ulLevel > RESTRICT_MAX_RECURSE_LEVEL)
		return MAPI_E_TOO_COMPLEX;

	if (!lpCondition)
		return MAPI_E_INVALID_PARAMETER;

	switch (lpCondition->rt) {
	// loops
	case RES_AND:
		for (c = 0; c < lpCondition->res.resAnd.cRes; ++c) {
			hr = TestRestriction(&lpCondition->res.resAnd.lpRes[c], lpMessage, locale, ulLevel+1);
			if (hr != hrSuccess) {
				fMatch = false;
				break;
			}
			fMatch = true;
		}
		break;
	case RES_OR:
		for (c = 0; c < lpCondition->res.resAnd.cRes; ++c) {
			hr = TestRestriction(&lpCondition->res.resOr.lpRes[c], lpMessage, locale, ulLevel+1);
			if (hr == hrSuccess) {
				fMatch = true;
				break;
			} else if (hr == MAPI_E_TOO_COMPLEX)
				break;
		}
		break;
	case RES_NOT:
		hr = TestRestriction(lpCondition->res.resNot.lpRes, lpMessage, locale, ulLevel+1);
		if (hr != MAPI_E_TOO_COMPLEX) {
			if (hr == hrSuccess) {
				hr = MAPI_E_NOT_FOUND;
				fMatch = false;
			} else {
				hr = hrSuccess;
				fMatch = true;
			}
		}
		break;

	// Prop compares
	case RES_CONTENT:

		// @todo: support PT_MV_STRING8, PT_MV_UNICODE and PT_MV_BINARY
		// fuzzy string compare
		if (PROP_TYPE(lpCondition->res.resContent.ulPropTag) != PT_STRING8 &&
			PROP_TYPE(lpCondition->res.resContent.ulPropTag) != PT_UNICODE &&
			PROP_TYPE(lpCondition->res.resContent.ulPropTag) != PT_BINARY) {
			hr = MAPI_E_TOO_COMPLEX;
			break;
		}
		ulPropType = PROP_TYPE(lpCondition->res.resContent.ulPropTag);
		hr = HrGetOneProp(lpMessage, lpCondition->res.resContent.ulPropTag, &lpProp);
		if (hr == hrSuccess) {
			char *lpSearchString = NULL, *lpSearchData = NULL;
			wchar_t *lpwSearchString = NULL, *lpwSearchData = NULL;
			unsigned int ulSearchStringSize = 0, ulSearchDataSize = 0;
			ULONG ulFuzzyLevel;

			if (ulPropType == PT_STRING8) {
				lpSearchString = lpCondition->res.resContent.lpProp->Value.lpszA;
				lpSearchData = lpProp->Value.lpszA;
				ulSearchStringSize = lpSearchString?strlen(lpSearchString):0;
				ulSearchDataSize = lpSearchData?strlen(lpSearchData):0;
			} else if (ulPropType == PT_UNICODE) {
				lpwSearchString = lpCondition->res.resContent.lpProp->Value.lpszW;
				lpwSearchData = lpProp->Value.lpszW;
				ulSearchStringSize = lpwSearchString?wcslen(lpwSearchString):0;
				ulSearchDataSize = lpwSearchData?wcslen(lpwSearchData):0;
			} else {
				// PT_BINARY
				lpSearchString = (char*)lpCondition->res.resContent.lpProp->Value.bin.lpb;
				lpSearchData = (char*)lpProp->Value.bin.lpb;
				ulSearchStringSize = lpCondition->res.resContent.lpProp->Value.bin.cb;
				ulSearchDataSize = lpProp->Value.bin.cb;
			}

			ulFuzzyLevel = lpCondition->res.resContent.ulFuzzyLevel;
			switch (ulFuzzyLevel & 0xFFFF) {
			case FL_FULLSTRING:
				if(ulSearchDataSize == ulSearchStringSize) {
					if ((ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) && lpSearchData != NULL && lpSearchString != NULL && str_iequals(lpSearchData, lpSearchString, locale)) ||
						(ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpSearchData != NULL && lpSearchString != NULL && str_equals(lpSearchData, lpSearchString, locale)) ||
						(ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) && lpwSearchData != NULL && lpwSearchString != NULL && wcs_iequals(lpwSearchData, lpwSearchString, locale)) ||
						(ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpwSearchData != NULL && lpwSearchString != NULL && wcs_equals(lpwSearchData, lpwSearchString, locale)) ||
						(ulPropType == PT_BINARY && lpSearchData != NULL && lpwSearchString != NULL && memcmp(lpSearchData, lpSearchString, ulSearchDataSize) == 0))
					{
						fMatch = true;
						break;
					}
				}
				break;
			case FL_PREFIX:
				if(ulSearchDataSize >= ulSearchStringSize) {
					if ((ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) && lpSearchData != NULL && lpSearchString != NULL && str_istartswith(lpSearchData, lpSearchString, locale)) ||
						(ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpSearchData != NULL && lpSearchString != NULL && str_startswith(lpSearchData, lpSearchString, locale)) ||
						(ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) && lpwSearchData != NULL && lpwSearchString != NULL && wcs_istartswith(lpwSearchData, lpwSearchString, locale)) ||
						(ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpwSearchData != NULL && lpwSearchString != NULL && wcs_startswith(lpwSearchData, lpwSearchString, locale)) ||
						(ulPropType == PT_BINARY && lpSearchData != NULL && lpSearchString != NULL && memcmp(lpSearchData, lpSearchString, ulSearchDataSize) == 0))
					{
						fMatch = true;
						break;
					}
				}
				break;
			case FL_SUBSTRING:
				if ((ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) && lpSearchData != NULL && lpSearchString != NULL && str_icontains(lpSearchData, lpSearchString, locale)) ||
					(ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpSearchData != NULL && lpSearchString != NULL && str_contains(lpSearchData, lpSearchString, locale)) ||
					(ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) && lpwSearchData != NULL && lpwSearchString != NULL && wcs_icontains(lpwSearchData, lpwSearchString, locale)) ||
					(ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpwSearchData != NULL && lpwSearchString != NULL && wcs_contains(lpwSearchData, lpwSearchString, locale)) ||
					(ulPropType == PT_BINARY && lpSearchData != NULL && lpSearchString != NULL && memsubstr(lpSearchData, ulSearchDataSize, lpSearchString, ulSearchStringSize) == 0))
				{
					fMatch = true;
					break;
				}
				break;
			};
		}
		break;
	case RES_PROPERTY:
		if(PROP_TYPE(lpCondition->res.resProperty.ulPropTag) != PROP_TYPE(lpCondition->res.resProperty.lpProp->ulPropTag)) {
			// cannot compare two different types
			hr = MAPI_E_TOO_COMPLEX;
			break;
		}
		hr = HrGetOneProp(lpMessage, lpCondition->res.resProperty.ulPropTag, &lpProp);
		if (hr != hrSuccess)
			break;

		Util::CompareProp(lpProp, lpCondition->res.resProperty.lpProp, locale, &result);
		hr = TestRelop(lpCondition->res.resProperty.relop, result, &fMatch);
		break;
	case RES_COMPAREPROPS:
		if(PROP_TYPE(lpCondition->res.resCompareProps.ulPropTag1) != PROP_TYPE(lpCondition->res.resCompareProps.ulPropTag2)) {
			// cannot compare two different types
			hr = MAPI_E_TOO_COMPLEX;
			break;
		}
		hr = HrGetOneProp(lpMessage, lpCondition->res.resCompareProps.ulPropTag1, &lpProp);
		if (hr != hrSuccess)
			break;
		hr = HrGetOneProp(lpMessage, lpCondition->res.resCompareProps.ulPropTag2, &lpProp2);
		if (hr != hrSuccess)
			break;
		Util::CompareProp(lpProp, lpProp2, locale, &result);
		hr = TestRelop(lpCondition->res.resProperty.relop, result, &fMatch);
		break;
	case RES_BITMASK:
		if (PROP_TYPE(lpCondition->res.resBitMask.ulPropTag) != PT_LONG) {
			hr = MAPI_E_TOO_COMPLEX;
			break;
		}
		hr = HrGetOneProp(lpMessage, lpCondition->res.resBitMask.ulPropTag, &lpProp);
		if (hr != hrSuccess)
			break;
		fMatch = (lpProp->Value.ul & lpCondition->res.resBitMask.ulMask) == 0;
		if (lpCondition->res.resBitMask.relBMR == BMR_NEZ)
			fMatch = !fMatch;
		break;
	case RES_SIZE:
		hr = HrGetOneProp(lpMessage, lpCondition->res.resSize.ulPropTag, &lpProp);
		if (hr != hrSuccess)
			break;
		ulSize = Util::PropSize(lpProp);
		result = ulSize - lpCondition->res.resSize.cb;
		hr = TestRelop(lpCondition->res.resSize.relop, result, &fMatch);
		break;
	case RES_EXIST:
		hr = HrGetOneProp(lpMessage, lpCondition->res.resExist.ulPropTag, &lpProp);
		if (hr != hrSuccess)
			break;
		fMatch = true;
		break;
	case RES_SUBRESTRICTION:
		// A subrestriction is basically an OR restriction over all the rows in a specific
		// table. We currently support the attachment table (PR_MESSAGE_ATTACHMENTS) and the 
		// recipient table (PR_MESSAGE_RECIPIENTS) here.
		hr = lpMessage->OpenProperty(lpCondition->res.resSub.ulSubObject, &IID_IMAPITable, 0, 0, (LPUNKNOWN *)&lpTable);
		if(hr != hrSuccess) {
			hr = MAPI_E_TOO_COMPLEX;
			goto exit;
		}
		// Get a list of properties we may be needing
		hr = GetRestrictTags(lpCondition->res.resSub.lpRes, &lpTags);
		if(hr != hrSuccess)
			goto exit;

		hr = lpTable->SetColumns(lpTags, 0);
		if(hr != hrSuccess)
			goto exit;

		while(1) {
			hr = lpTable->QueryRows(1, 0, &lpRowSet);
			if(hr != hrSuccess)
				goto exit;

			if(lpRowSet->cRows != 1)
				break;

			// Wrap the row into an IMAPIProp compatible object so we can recursively call
			// this function (which obviously itself doesn't support RES_SUBRESTRICTION as 
			// there aren't any subobjects under the subobjects .. unless we count
			// messages in PR_ATTACH_DATA_OBJ under attachments... Well we don't support
			// that in any case ...)

			hr = TestRestriction(lpCondition->res.resSub.lpRes, lpRowSet->aRow[0].cValues, lpRowSet->aRow[0].lpProps, locale, ulLevel+1);
			if(hr == hrSuccess) {
				fMatch = true;
				break;
			}

			FreeProws(lpRowSet);
			lpRowSet = NULL;

			delete lpRowWrapper;
			lpRowWrapper = NULL;
		}
		break;

	case RES_COMMENT:
		hr = TestRestriction(lpCondition->res.resComment.lpRes, lpMessage, locale, ulLevel+1);
		if(hr == hrSuccess)
			fMatch = true;
		else
			fMatch = false;
		break;

	default:
		break;
	};

exit:
	delete lpRowWrapper;
	if (lpRowSet)
		FreeProws(lpRowSet);
	MAPIFreeBuffer(lpTags);
	if (lpTable)
		lpTable->Release();
	MAPIFreeBuffer(lpProp);
	MAPIFreeBuffer(lpProp2);
	if (fMatch)
		return hrSuccess;
	else if (hr == hrSuccess)
		return MAPI_E_NOT_FOUND;
	return hr;
}

HRESULT GetClientVersion(unsigned int* ulVersion)
{
	HRESULT hr = hrSuccess;
	*ulVersion = CLIENT_VERSION_LATEST;
	return hr;
}

/**
 * Find a folder name in a table (hierarchy)
 *
 * Given a hierarchy table, the function searches for a foldername in
 * the PR_DISPLAY_NAME_A property. The EntryID will be returned in the
 * out parameter. The table pointer will be left where the entry was
 * found.
 *
 * @todo make unicode compatible
 *
 * @param[in]	lpTable			IMAPITable interface, pointing to a hierarchy table
 * @param[in]	folder			foldername to find in the list
 * @param[out]	lppFolderProp	Property containing the EntryID of the found folder
 * @return		HRESULT			Mapi error code
 * @retval		MAPI_E_NOT_FOUND if folder not found.
 */
HRESULT FindFolder(LPMAPITABLE lpTable, const WCHAR *folder, LPSPropValue *lppFolderProp) {
	HRESULT hr;
	LPSRowSet		lpRowSet = NULL;
	ULONG nValues;
	SizedSPropTagArray(2, sptaName) = { 2, { PR_DISPLAY_NAME_W, PR_ENTRYID } };

	hr = lpTable->SetColumns(sptaName, 0);
	if (hr != hrSuccess)
		goto exit;

	while (TRUE) {
		hr = lpTable->QueryRows(1, 0, &lpRowSet);
		if (hr != hrSuccess)
			break;

		if(lpRowSet->cRows == 0) {
			hr = MAPI_E_NOT_FOUND;
			break;
		}

		if (wcscasecmp(lpRowSet->aRow[0].lpProps[0].Value.lpszW, folder) == 0) {
			// found the folder
			hr = Util::HrCopyPropertyArray(&lpRowSet->aRow[0].lpProps[1], 1, lppFolderProp, &nValues);
			break;
		}

		FreeProws(lpRowSet);
		lpRowSet = NULL;
	}

exit:
	if (lpRowSet)
		FreeProws(lpRowSet);

	return hr;
}

/**
 * Opens any subfolder from the name at any depth. The folder
 * separator character is also passed. You can also open the IPM
 * subtree not by passing the foldername.
 *
 * @param[in]	lpMDB	A store to open the folder in. If you pass the public store, set the matching bool to true.
 * @param[in]	folder	The name of the folder you want to open. Can be at any depth, eg. INBOX/folder name1/folder name2. Pass / as separator.
 *						Pass NULL to open the IPM subtree of the passed store.
 * @param[in]	psep	The foldername separator in the folder parameter.
 * @param[in]	bIsPublic	The lpMDB parameter is the public store if true, otherwise false.
 * @param[in]	bCreateFolder	Create the subfolders if they are not found, otherwise returns MAPI_E_NOT_FOUND if a folder is not present.
 * @param[out]	lppSubFolder	The final opened subfolder.
 * @return	MAPI error code
 * @retval	MAPI_E_NOT_FOUND, MAPI_E_NO_ACCESS, other.
 */
HRESULT OpenSubFolder(LPMDB lpMDB, const wchar_t *folder, wchar_t psep,
    bool bIsPublic, bool bCreateFolder, LPMAPIFOLDER *lppSubFolder)
{
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpPropIPMSubtree = NULL;
	LPMAPITABLE		lpTable = NULL;
	ULONG			ulObjType;
	LPSPropValue	lpPropFolder = NULL;
	LPMAPIFOLDER	lpFoundFolder = NULL;
	LPMAPIFOLDER	lpNewFolder = NULL;
	const WCHAR*	ptr = NULL;

	if(bIsPublic)
	{
		hr = HrGetOneProp(lpMDB, PR_IPM_PUBLIC_FOLDERS_ENTRYID, &lpPropIPMSubtree);
		if (hr != hrSuccess) {
			ec_log_crit("Unable to find PR_IPM_PUBLIC_FOLDERS_ENTRYID object, error code: 0x%08X", hr);
			goto exit;
		}
	}
	else
	{
		hr = HrGetOneProp(lpMDB, PR_IPM_SUBTREE_ENTRYID, &lpPropIPMSubtree);
		if (hr != hrSuccess) {
			ec_log_crit("Unable to find IPM_SUBTREE object, error code: 0x%08X", hr);
			goto exit;
		}
	}

	hr = lpMDB->OpenEntry(lpPropIPMSubtree->Value.bin.cb, (LPENTRYID)lpPropIPMSubtree->Value.bin.lpb,
						  &IID_IMAPIFolder, 0, &ulObjType, (LPUNKNOWN*)&lpFoundFolder);
	if (hr != hrSuccess || ulObjType != MAPI_FOLDER) {
		ec_log_crit("Unable to open IPM_SUBTREE object, error code: 0x%08X", hr);
		goto exit;
	}

	// correctly return IPM subtree as found folder
	if (!folder)
		goto found;

	// Loop through the folder string to find the wanted folder in the store
	do {
		wstring subfld;

		ptr = wcschr(folder, psep);
		if (ptr)
			subfld = wstring(folder, ptr-folder);
		else
			subfld = wstring(folder);
		folder = ptr ? ptr+1 : NULL;

		hr = lpFoundFolder->GetHierarchyTable(0, &lpTable);
		if (hr != hrSuccess) {
			ec_log_crit("Unable to view folder, error code: 0x%08X", hr);
			goto exit;
		}

		hr = FindFolder(lpTable, subfld.c_str(), &lpPropFolder);
		if (hr == MAPI_E_NOT_FOUND && bCreateFolder) {
			hr = lpFoundFolder->CreateFolder(FOLDER_GENERIC, (LPTSTR)subfld.c_str(), (LPTSTR)L"Auto-created by Kopano", &IID_IMAPIFolder, MAPI_UNICODE | OPEN_IF_EXISTS, &lpNewFolder);
			if (hr != hrSuccess) {
				ec_log_crit("Unable to create folder \"%ls\", error code: 0x%08X", subfld.c_str(), hr);
				goto exit;
			}
		} else if (hr != hrSuccess)
			goto exit;

		// not needed anymore
		lpFoundFolder->Release();
		lpFoundFolder = NULL;

		lpTable->Release();
		lpTable = NULL;

		if (lpNewFolder) {
			lpFoundFolder = lpNewFolder;
			lpNewFolder = NULL;
		} else {
			hr = lpMDB->OpenEntry(lpPropFolder->Value.bin.cb, (LPENTRYID)lpPropFolder->Value.bin.lpb,
								  &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, (LPUNKNOWN*)&lpFoundFolder);
			if (hr != hrSuccess) {
				ec_log_crit("Unable to open folder \"%ls\", error code: 0x%08X", subfld.c_str(), hr);
				goto exit;
			}
		}
	} while (ptr);

found:
	if (lpFoundFolder) {
		lpFoundFolder->AddRef();
		*lppSubFolder = lpFoundFolder;
	}

exit:
	MAPIFreeBuffer(lpPropFolder);
	MAPIFreeBuffer(lpPropIPMSubtree);
	if (lpFoundFolder)
		lpFoundFolder->Release();

	if (lpTable)
		lpTable->Release();

	return hr;
}

HRESULT HrOpenUserMsgStore(LPMAPISESSION lpSession, WCHAR *lpszUser, LPMDB *lppStore)
{
	return HrOpenUserMsgStore(lpSession, NULL, lpszUser, lppStore);
}

/**
 * Opens the default store of a given user using a MAPISession.
 *
 * Use this to open any user store a user is allowed to open.
 *
 * @param[in]	lpSession	The IMAPISession object you received from the logon procedure.
 * @param[in]	lpStore		Optional store 
 * @param[in]	lpszUser	Login name of the user's store you want to open.
 * @param[out]	lppStore	Pointer to the store of the given user.
 *
 * @return		HRESULT		Mapi error code.
 */
HRESULT HrOpenUserMsgStore(LPMAPISESSION lpSession, LPMDB lpStore, WCHAR *lpszUser, LPMDB *lppStore)
{
	HRESULT					hr = hrSuccess;
	LPMDB					lpDefaultStore = NULL;
	LPMDB					lpMsgStore = NULL;
	IExchangeManageStore	*lpExchManageStore = NULL;
	ULONG					cbStoreEntryID = 0;
	LPENTRYID				lpStoreEntryID = NULL;

	if (lpStore == NULL) {
		hr = HrOpenDefaultStore(lpSession, &lpDefaultStore);
		if (hr != hrSuccess)
			goto exit;

		lpStore = lpDefaultStore;
	}

	// Find and open the store for lpszUser.
	hr = lpStore->QueryInterface(IID_IExchangeManageStore, (LPVOID*)&lpExchManageStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpExchManageStore->CreateStoreEntryID(NULL, (LPTSTR)lpszUser, MAPI_UNICODE, &cbStoreEntryID, &lpStoreEntryID);
	if (hr != hrSuccess)
		goto exit;

	hr = lpSession->OpenMsgStore(0, cbStoreEntryID, lpStoreEntryID, &IID_IMsgStore, MDB_WRITE, &lpMsgStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMsgStore->QueryInterface(IID_IMsgStore, (void**)lppStore);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (lpMsgStore)
		lpMsgStore->Release();
	MAPIFreeBuffer(lpStoreEntryID);
	if (lpExchManageStore)
		lpExchManageStore->Release();

	if (lpDefaultStore)
		lpDefaultStore->Release();

	return hr;
}

/*
 * NAMED PROPERTY util functions (used with PROPMAP_* macro's)
 */

ECPropMapEntry::ECPropMapEntry(GUID guid, ULONG ulId) { 
    m_sMAPINameId.ulKind = MNID_ID; 
    m_sGuid = guid;  
    m_sMAPINameId.lpguid = &m_sGuid; 
    m_sMAPINameId.Kind.lID = ulId; 
}
    
ECPropMapEntry::ECPropMapEntry(GUID guid, const char *strId)
{
    m_sMAPINameId.ulKind = MNID_STRING; 
    m_sGuid = guid;  
    m_sMAPINameId.lpguid = &m_sGuid; 
    m_sMAPINameId.Kind.lpwstrName = new WCHAR[strlen(strId)+1];
    mbstowcs(m_sMAPINameId.Kind.lpwstrName, strId, strlen(strId)+1);
}
    
ECPropMapEntry::ECPropMapEntry(const ECPropMapEntry &other) { 
    m_sMAPINameId.ulKind = other.m_sMAPINameId.ulKind;
    m_sGuid = other.m_sGuid;
    m_sMAPINameId.lpguid = &m_sGuid;
    if(other.m_sMAPINameId.ulKind == MNID_ID) {
        m_sMAPINameId.Kind.lID = other.m_sMAPINameId.Kind.lID;
    } else {
        m_sMAPINameId.Kind.lpwstrName = new WCHAR[wcslen( other.m_sMAPINameId.Kind.lpwstrName )+1];
        wcscpy(m_sMAPINameId.Kind.lpwstrName, other.m_sMAPINameId.Kind.lpwstrName);
    }
}

ECPropMapEntry::ECPropMapEntry(ECPropMapEntry &&other)
{
	m_sMAPINameId.ulKind = other.m_sMAPINameId.ulKind;
	m_sGuid = other.m_sGuid;
	m_sMAPINameId.lpguid = &m_sGuid;
	if (other.m_sMAPINameId.ulKind == MNID_ID) {
		m_sMAPINameId.Kind.lID = other.m_sMAPINameId.Kind.lID;
	} else {
		m_sMAPINameId.Kind.lpwstrName = other.m_sMAPINameId.Kind.lpwstrName;
		other.m_sMAPINameId.Kind.lpwstrName = nullptr;
	}
}

ECPropMapEntry::~ECPropMapEntry()
{
	if (m_sMAPINameId.ulKind == MNID_STRING)
		delete[] m_sMAPINameId.Kind.lpwstrName;
}
    
MAPINAMEID* ECPropMapEntry::GetMAPINameId() { 
    return &m_sMAPINameId; 
}

ECPropMap::ECPropMap(size_t hint)
{
	lstNames.reserve(hint);
	lstVars.reserve(hint);
	lstTypes.reserve(hint);
}
    
void ECPropMap::AddProp(ULONG *lpId, ULONG ulType, const ECPropMapEntry &entry)
{
    // Add reference to proptag for later Resolve();
    lstNames.push_back(entry);
    lstVars.push_back(lpId);
    lstTypes.push_back(ulType);
}
    
HRESULT ECPropMap::Resolve(IMAPIProp *lpMAPIProp) {
    HRESULT hr = hrSuccess;
    MAPINAMEID **lppNames = NULL;
    std::vector<ULONG *>::const_iterator j;
    std::vector<ULONG>::const_iterator k;
    int n = 0;
    LPSPropTagArray lpPropTags = NULL;

	if (lpMAPIProp == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
    
    // Do GetIDsFromNames() and store result in correct places
    lppNames = new MAPINAMEID *[lstNames.size()];
	for (auto &mapent : lstNames)
		lppNames[n++] = mapent.GetMAPINameId();
    
    hr = lpMAPIProp->GetIDsFromNames(n, lppNames, MAPI_CREATE, &lpPropTags);
    if(hr != hrSuccess)
        goto exit;
    
    n = 0;
    k = lstTypes.begin();
    for (j = lstVars.begin(); j != lstVars.end(); ++j, ++k)
        *(*j) = CHANGE_PROP_TYPE(lpPropTags->aulPropTag[n++], *k);
    
exit:
	MAPIFreeBuffer(lpPropTags);
	delete[] lppNames;
	return hr;
}

/**
 * Opens the Default Calendar folder of the store.
 *
 * @param[in]	lpMsgStore			Users Store. 
 * @param[out]	lppFolder			Default Calendar Folder of the store. 
 * @return		HRESULT 
 * @retval		MAPI_E_NOT_FOUND	Default Folder not found. 
 * @retval		MAPI_E_NO_ACCESS	Insufficient permissions to open the folder.  
 */
HRESULT HrOpenDefaultCalendar(LPMDB lpMsgStore, LPMAPIFOLDER *lppFolder)
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpPropDefFld = NULL;
	LPMAPIFOLDER lpRootFld = NULL;
	LPMAPIFOLDER lpDefaultFolder = NULL;
	ULONG ulType = 0;
	
	//open Root Container.
	hr = lpMsgStore->OpenEntry(0, NULL, NULL, 0, &ulType, (LPUNKNOWN*)&lpRootFld);
	if (hr != hrSuccess || ulType != MAPI_FOLDER) 
	{
		ec_log_crit("Unable to open Root Container, error code: 0x%08X", hr);
		goto exit;
	}

	//retrive Entryid of Default Calendar Folder.
	hr = HrGetOneProp(lpRootFld, PR_IPM_APPOINTMENT_ENTRYID, &lpPropDefFld);
	if (hr != hrSuccess) 
	{
		ec_log_crit("Unable to find PR_IPM_APPOINTMENT_ENTRYID, error code: 0x%08X", hr);
		goto exit;
	}
	
	hr = lpMsgStore->OpenEntry(lpPropDefFld->Value.bin.cb, (LPENTRYID)lpPropDefFld->Value.bin.lpb, NULL, MAPI_MODIFY, &ulType, (LPUNKNOWN*)&lpDefaultFolder);
	if (hr != hrSuccess || ulType != MAPI_FOLDER) 
	{
		ec_log_crit("Unable to open IPM_SUBTREE object, error code: 0x%08X", hr);
		goto exit;
	}

	*lppFolder = lpDefaultFolder;
	lpDefaultFolder = NULL;

exit:
	if (lpDefaultFolder)
		lpDefaultFolder->Release();

	if (lpRootFld)
		lpRootFld->Release();
	MAPIFreeBuffer(lpPropDefFld);
	return hr;
}

/**
 * Gets all properties for passed object
 *
 * This includes properties that are normally returned from GetProps() as MAPI_E_NOT_ENOUGH_MEMORY. The
 * rest of the semantics of this call are equal to those of calling IMAPIProp::GetProps() with NULL as the
 * property tag array.
 *
 * @param[in] lpProp IMAPIProp object to get properties from
 * @param[in] ulFlags MAPI_UNICODE or 0
 * @param[out] lpcValues Number of properties saved in lppProps
 * @param[out] lppProps Output properties
 */
HRESULT HrGetAllProps(IMAPIProp *lpProp, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppProps)
{
	SPropTagArrayPtr lpTags;
	SPropArrayPtr lpProps;
	ULONG cValues = 0;
	StreamPtr lpStream;	
	std::string strData;
	void *lpData = NULL;
	
	HRESULT hr = lpProp->GetPropList(ulFlags, &~lpTags);
	if(hr != hrSuccess)
		return hr;
	hr = lpProp->GetProps(lpTags, ulFlags, &cValues, &~lpProps);
	if(FAILED(hr))
		return hr;
		
	for (unsigned int i = 0; i < cValues; ++i) {
		if(PROP_TYPE(lpProps[i].ulPropTag) == PT_ERROR && lpProps[i].Value.err == MAPI_E_NOT_ENOUGH_MEMORY) {
			if(PROP_TYPE(lpTags->aulPropTag[i]) != PT_STRING8 && PROP_TYPE(lpTags->aulPropTag[i]) != PT_UNICODE && PROP_TYPE(lpTags->aulPropTag[i]) != PT_BINARY)
				continue;
				
			if(lpProp->OpenProperty(lpTags->aulPropTag[i], &IID_IStream, 0, 0, (IUnknown **)&lpStream) != hrSuccess)
				continue;
				
			strData.clear();
			if(Util::HrStreamToString(lpStream.get(), strData) != hrSuccess)
				continue;
				
			if ((hr = MAPIAllocateMore(strData.size() + sizeof(WCHAR), lpProps, (void **)&lpData)) != hrSuccess)
				return hr;

			memcpy(lpData, strData.data(), strData.size());
			
			lpProps[i].ulPropTag = lpTags->aulPropTag[i];
				
			switch PROP_TYPE(lpTags->aulPropTag[i]) {
				case PT_STRING8:
					lpProps[i].Value.lpszA = (char *)lpData;
					lpProps[i].Value.lpszA[strData.size()] = 0;
					break;
				case PT_UNICODE:
					lpProps[i].Value.lpszW = (wchar_t *)lpData;
					lpProps[i].Value.lpszW[strData.size() / sizeof(WCHAR)] = 0;
					break;
				case PT_BINARY:
					lpProps[i].Value.bin.lpb = (LPBYTE)lpData;
					lpProps[i].Value.bin.cb = strData.size();
					break;
				default:
					assert(false);
			}
		}
	}
	
	*lppProps = lpProps.release();
	*lpcValues = cValues;
	return hrSuccess;
}

/**
 * Converts a wrapped message store's entry identifier to a message store entry identifier.
 *
 * MAPI supplies a wrapped version of a store entryid which indentified a specific service provider. 
 * A MAPI client can use IMAPISupport::WrapStoreEntryID to generate a wrapped entryid. The PR_ENTRYID and 
 * PR_STORE_ENTRYID are wrapped entries which can be unwrapped by using this function. 
 * 
 * @param[in] cbOrigEntry
 *				Size, in bytes, of the original entry identifier for the wrapped message store.
 * @param[in] lpOrigEntry
 *				Pointer to an ENTRYID structure that contains the original wrapped entry identifier.
 * @param[out] lpcbUnWrappedEntry
 *				Pointer to the size, in bytes, of the new unwrapped entry identifier.
 * @param[out] lppUnWrappedEntry
 *				Pointer to a pointer to an ENTRYID structure that contains the new unwrapped entry identifier
 *
 * @retval MAPI_E_INVALID_PARAMETER
 *				One or more values are NULL.
 * @retval MAPI_E_INVALID_ENTRYID
 *				The entry ID is not valid. It shouyld be a wrapped entry identifier
 */
HRESULT __stdcall UnWrapStoreEntryID(ULONG cbOrigEntry, LPENTRYID lpOrigEntry, ULONG *lpcbUnWrappedEntry, LPENTRYID *lppUnWrappedEntry)
{
	HRESULT hr = hrSuccess;
	ULONG cbRemove = 0;
	ULONG cbDLLName = 0;
	LPENTRYID lpEntryID = NULL;

	if (lpOrigEntry == NULL || lpcbUnWrappedEntry == NULL || lppUnWrappedEntry == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	
	// Check if this a wrapped store entryid
	if (cbOrigEntry < (4 + sizeof(GUID) + 3) || memcmp(lpOrigEntry->ab, &muidStoreWrap, sizeof(GUID)) != 0) {
		hr = MAPI_E_INVALID_ENTRYID;
		goto exit;
	}

	cbRemove = 4; // Flags
	cbRemove+= sizeof(GUID); //Wrapped identifier
	cbRemove+= 2; // Unknown, Unicode flag?

	// Dllname size
	cbDLLName = (ULONG)strlen((LPCSTR)lpOrigEntry+cbRemove) + 1;
	cbRemove+= cbDLLName;

	cbRemove += (4 - (cbRemove & 0x03)) & 0x03;; // padding to 4byte block

	if (cbOrigEntry <= cbRemove) {
		hr = MAPI_E_INVALID_ENTRYID;
		goto exit;
	}

	// Create Unwrap entryid
	hr = MAPIAllocateBuffer(cbOrigEntry - cbRemove, (void**)&lpEntryID);
	if (hr != hrSuccess)
		goto exit;

	memcpy(lpEntryID, ((LPBYTE)lpOrigEntry)+cbRemove, cbOrigEntry - cbRemove);

	*lpcbUnWrappedEntry = cbOrigEntry - cbRemove;
	*lppUnWrappedEntry = lpEntryID;

exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(lpEntryID);

	return hr;
}

/**
 * Call IAddrBook::Address with correct flags
 *
 * Various versions of outlook support different flags in IAddrBook::Address. This function
 * removes the unsupported flags and converts strings to non-unicode strings if needed. 
 *
 * NOTE: since you cannot rely on the MAPI_UNICODE flag being passed, the data in lpResult may
 * contain either PT_STRING8 or PT_UNICODE. The calling code should handle both types.
 */
HRESULT DoAddress(IAddrBook *lpAdrBook, ULONG* hWnd, LPADRPARM lpAdrParam, LPADRLIST *lpAdrList)
{
	ULONG ulUnCapabilities = 0; // All the UNSUPPORTED features for this outlook version
	std::string strCaption;
	std::string strNewEntryTitle;
	std::string strDestWellsTitle;
	std::string strHelpFileName;
	unsigned int ulVersion = 0;
	LPADRLIST lpResult = *lpAdrList;
	ADRPARM sAdrParam = *lpAdrParam;
	std::vector<std::string> vDestFields;

	HRESULT hr = GetClientVersion(&ulVersion);
	if(hr != hrSuccess)
		return hr;

	// Various versions of outlook support some flags and dont support others
	if (ulVersion <= CLIENT_VERSION_OLK2000)
		ulUnCapabilities |= MAPI_UNICODE;

	if (ulVersion <= CLIENT_VERSION_OLK2002)
		ulUnCapabilities |= AB_UNICODEUI;

	if (ulVersion <= CLIENT_VERSION_OLK2003)
		ulUnCapabilities |= AB_LOCK_NON_ACL;

	if((sAdrParam.ulFlags & AB_UNICODEUI) && (ulUnCapabilities & AB_UNICODEUI)) {
		// Addressbook doesn't support AB_UNICODEUI, convert data
		if(sAdrParam.lpszCaption) {
			strCaption = convert_to<string>((LPWSTR)sAdrParam.lpszCaption);
			sAdrParam.lpszCaption = (LPTSTR)strCaption.c_str();
		}

		if(sAdrParam.lpszNewEntryTitle) {
			strNewEntryTitle = convert_to<string>((LPWSTR)sAdrParam.lpszNewEntryTitle);
			sAdrParam.lpszCaption = (LPTSTR)strNewEntryTitle.c_str();
		}

		if(sAdrParam.lpszDestWellsTitle) {
			strDestWellsTitle = convert_to<string>((LPWSTR)sAdrParam.lpszDestWellsTitle);
			sAdrParam.lpszDestWellsTitle = (LPTSTR)strDestWellsTitle.c_str();
		}
		
		if(sAdrParam.lpszHelpFileName) {
			strHelpFileName = convert_to<string>((LPWSTR)sAdrParam.lpszHelpFileName);
			sAdrParam.lpszHelpFileName = (LPTSTR)strHelpFileName.c_str();
		}

		// Same for lpAdrParam
		for (unsigned int i = 0; i < sAdrParam.cDestFields; ++i) {
			std::string strField = convert_to<string>((LPWSTR)sAdrParam.lppszDestTitles[i]);

			vDestFields.push_back(strField);

			sAdrParam.lppszDestTitles[i] = (LPTSTR)vDestFields.back().c_str();

		}
	}

	// Remove unsupported flags
	sAdrParam.ulFlags &= ~ulUnCapabilities;
	
	hr = lpAdrBook->Address(hWnd, &sAdrParam, &lpResult);
	if(hr != hrSuccess) 
		return hr;

	if ((ulUnCapabilities & MAPI_UNICODE) && (lpAdrParam->ulFlags & MAPI_UNICODE)) {
		// MAPI_UNICODE was requested, but the addressbook did not support it. This means we have to convert all the PT_STRING8 data
		// back to PT_UNICODE.

		for (unsigned int i = 0; i < lpResult->cEntries; ++i) {
			for (unsigned int j = 0; j < lpResult->aEntries[i].cValues; ++j) {
				if(PROP_TYPE(lpResult->aEntries[i].rgPropVals[j].ulPropTag) == PT_STRING8) {
					std::wstring wstrData = convert_to<wstring>(lpResult->aEntries[i].rgPropVals[j].Value.lpszA);

					hr = MAPIAllocateMore((wstrData.size() + 1) * sizeof(WCHAR), lpResult->aEntries[i].rgPropVals, (void **)&lpResult->aEntries[i].rgPropVals[j].Value.lpszW);
					if(hr != hrSuccess)
						return hr;

					memcpy(lpResult->aEntries[i].rgPropVals[j].Value.lpszW, wstrData.c_str(), (wstrData.size() + 1) * sizeof(WCHAR));

					lpResult->aEntries[i].rgPropVals[j].ulPropTag = CHANGE_PROP_TYPE(lpResult->aEntries[i].rgPropVals[j].ulPropTag, PT_UNICODE);
				}
			}
		}
	}

	*lpAdrList = lpResult;
	return hrSuccess;
}

/**
 * An enumeration for getting the localfreebusy from the calendar or from the free/busy data folder.
 *
 * @note it's also the array position of property PR_FREEBUSY_ENTRYIDS
 */
enum DGMessageType { 
	dgAssociated = 0,	/**< Localfreebusy message in default associated calendar folder */
	dgFreebusydata = 1	/**< Localfreebusy message in Free/busy data folder */
};

// Default freebusy publish months
#define ECFREEBUSY_DEFAULT_PUBLISH_MONTHS		6

/**
 * Create a local free/busy message
 *
 * @param[in] lpFolder
 *				Destenation folder for creating the local free/busy message
 * @param[in] ulFlags
 *				MAPI_ASSOCIATED	for Localfreebusy message in default associated calendar folder
 * @param[out] lppMessage
 *				The localfreebusy message with the free/busy settings
 *
 * @todo move the code to a common file
 *
 */
static HRESULT CreateLocalFreeBusyMessage(LPMAPIFOLDER lpFolder, ULONG ulFlags,
    LPMESSAGE *lppMessage)
{
	HRESULT hr = hrSuccess;
	LPMESSAGE lpMessage = NULL;
	SPropValue sPropValMessage[6];
	
	memset(sPropValMessage, 0, sizeof(SPropValue) * 6);

	if (lpFolder == NULL || lppMessage == NULL || (ulFlags&~MAPI_ASSOCIATED) != 0) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpFolder->CreateMessage(&IID_IMessage, (ulFlags&MAPI_ASSOCIATED), &lpMessage);
	if(hr != hrSuccess)
		goto exit;

	sPropValMessage[0].ulPropTag = PR_MESSAGE_CLASS_W;
	sPropValMessage[0].Value.lpszW = const_cast<wchar_t *>(L"IPM.Microsoft.ScheduleData.FreeBusy");

	sPropValMessage[1].ulPropTag = PR_SUBJECT_W;
	sPropValMessage[1].Value.lpszW = const_cast<wchar_t *>(L"LocalFreebusy");

	sPropValMessage[2].ulPropTag = PR_FREEBUSY_NUM_MONTHS;
	sPropValMessage[2].Value.ul = ECFREEBUSY_DEFAULT_PUBLISH_MONTHS;

	sPropValMessage[3].ulPropTag = PR_DECLINE_RECURRING_MEETING_REQUESTS;
	sPropValMessage[3].Value.b = false;

	sPropValMessage[4].ulPropTag = PR_DECLINE_CONFLICTING_MEETING_REQUESTS;
	sPropValMessage[4].Value.b = false;

	sPropValMessage[5].ulPropTag = PR_PROCESS_MEETING_REQUESTS;
	sPropValMessage[5].Value.b = false;

	hr = lpMessage->SetProps(6, sPropValMessage, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		goto exit;

	hr = lpMessage->QueryInterface(IID_IMessage, (void**)lppMessage);

exit:
	if (lpMessage)
		lpMessage->Release();

	return hr;
}

/*
 * Get local free/busy message to get delegate information
 * 
 * @note There is a differents between Outlook before 2003 and from 2003. 
 *       The free/busy settings are written on another place.
 *       Outlook 2000 and 2002, a message in the calendar associated folder
 *       Outlook 2003 and higher, a message in the freebusydata folder
 *		 exchange uses always the same?
 *
 * @param[in]	ulSettingsLocation
 *					Type message to get information
 * @param[in]	lpMsgStore
 *					the delegate message store for getting delegate information.
 * @param[in]	bCreateIfMissing
 *					If not exist create the localfreebusy message
 * @param[out]	lppFBMessage
 *					The localfreebusy message with the free/busy settings
 *
 * @return MAPI_E_NOT_FOUND 
 *			Local free/busy message is not exist
 *
 */
static HRESULT OpenLocalFBMessage(DGMessageType eDGMsgType,
    IMsgStore *lpMsgStore, bool bCreateIfMissing, IMessage **lppFBMessage)
{
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpRoot = NULL;
	IMAPIFolder *lpFolder = NULL;
	IMessage *lpMessage = NULL;
	ULONG ulType = 0;
	LPSPropValue lpPropFB = NULL;
	LPSPropValue lpPropFBNew = NULL;
	LPSPropValue lpPVFBFolder = NULL;
	LPSPropValue lpEntryID = NULL;
	LPSPropValue lpPropFBRef = NULL; // Non-free
	LPSPropValue lpAppEntryID = NULL;
	ULONG cbEntryIDInbox = 0;
	LPENTRYID lpEntryIDInbox = NULL;
	IMAPIFolder *lpInbox = NULL;
	LPTSTR lpszExplicitClass = NULL;

	hr = lpMsgStore->OpenEntry(0, NULL, &IID_IMAPIFolder, MAPI_MODIFY, &ulType, (IUnknown **) &lpRoot);
	if(hr != hrSuccess)
		goto exit;

	// Check if the freebusydata folder and LocalFreeBusy is exist. Create the folder and message if it is request.
	if((HrGetOneProp(lpRoot, PR_FREEBUSY_ENTRYIDS, &lpPropFB) != hrSuccess ||
		lpPropFB->Value.MVbin.cValues < 2 ||
		lpPropFB->Value.MVbin.lpbin[eDGMsgType].lpb == NULL ||
		lpMsgStore->OpenEntry(lpPropFB->Value.MVbin.lpbin[eDGMsgType].cb, (LPENTRYID)lpPropFB->Value.MVbin.lpbin[eDGMsgType].lpb, &IID_IMessage, MAPI_MODIFY, &ulType, (IUnknown **) &lpMessage) != hrSuccess)
	   && bCreateIfMissing) {
		
		// Open the inbox
		hr = lpMsgStore->GetReceiveFolder((LPTSTR)"", 0, &cbEntryIDInbox, &lpEntryIDInbox, &lpszExplicitClass);
		if(hr != hrSuccess)
			goto exit;

		hr = lpMsgStore->OpenEntry(cbEntryIDInbox, lpEntryIDInbox, &IID_IMAPIFolder, MAPI_MODIFY, &ulType, (IUnknown **) &lpInbox);
		if(hr != hrSuccess)
			goto exit;

		if (eDGMsgType == dgFreebusydata) {
			// Create freebusydata Folder
			hr = lpRoot->CreateFolder(FOLDER_GENERIC, (LPTSTR)"Freebusy Data", (LPTSTR)"", &IID_IMAPIFolder, OPEN_IF_EXISTS, &lpFolder);
			if(hr != hrSuccess)
				goto exit;

			// Get entryid of freebusydata
			hr = HrGetOneProp(lpFolder, PR_ENTRYID, &lpPVFBFolder);
			if(hr != hrSuccess)
				goto exit;

		} else if (eDGMsgType == dgAssociated) {
			//Open default calendar
			hr = HrGetOneProp(lpInbox, PR_IPM_APPOINTMENT_ENTRYID, &lpAppEntryID);
			if(hr != hrSuccess)
				goto exit;

			hr = lpMsgStore->OpenEntry(lpAppEntryID->Value.bin.cb, (LPENTRYID)lpAppEntryID->Value.bin.lpb, &IID_IMAPIFolder, MAPI_MODIFY, &ulType, (IUnknown **) &lpFolder);
			if(hr != hrSuccess)
				goto exit;
		}

		hr = CreateLocalFreeBusyMessage(lpFolder, (eDGMsgType == dgAssociated)?MAPI_ASSOCIATED : 0, &lpMessage);
		if(hr != hrSuccess)
			goto exit;

		hr = HrGetOneProp(lpMessage, PR_ENTRYID, &lpEntryID);
		if(hr != hrSuccess)
			goto exit;

		// Update Free/Busy entryid
		if(lpPropFB == NULL || lpPropFB->Value.MVbin.cValues < 2) {
			hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpPropFBNew);
			if(hr != hrSuccess)
				goto exit;

			lpPropFBNew->ulPropTag = PR_FREEBUSY_ENTRYIDS;
			hr = MAPIAllocateMore(sizeof(SBinary) * 4, lpPropFB, (void **)&lpPropFBNew->Value.MVbin.lpbin);
			if(hr != hrSuccess)
				goto exit;

			memset(lpPropFBNew->Value.MVbin.lpbin, 0, sizeof(SBinary) * 4);

			if (eDGMsgType == dgFreebusydata) {

				if(lpPropFB && lpPropFB->Value.MVbin.cValues > 0) {
					lpPropFBNew->Value.MVbin.lpbin[0] = lpPropFB->Value.MVbin.lpbin[0];

					if (lpPropFB->Value.MVbin.cValues > 2)
						lpPropFBNew->Value.MVbin.lpbin[2] = lpPropFB->Value.MVbin.lpbin[2];
				}

				lpPropFBNew->Value.MVbin.lpbin[1].cb = lpEntryID->Value.bin.cb;
				lpPropFBNew->Value.MVbin.lpbin[1].lpb = lpEntryID->Value.bin.lpb;

				lpPropFBNew->Value.MVbin.lpbin[3].cb = lpPVFBFolder->Value.bin.cb;
				lpPropFBNew->Value.MVbin.lpbin[3].lpb = lpPVFBFolder->Value.bin.lpb;

			} else if(eDGMsgType == dgAssociated) {
				lpPropFBNew->Value.MVbin.lpbin[0].cb = lpEntryID->Value.bin.cb;
				lpPropFBNew->Value.MVbin.lpbin[0].lpb = lpEntryID->Value.bin.lpb;
			}

			lpPropFBNew->Value.MVbin.cValues = 4; // no problem if the data is NULL

			lpPropFBRef = lpPropFBNew; // use this one later on

		} else {

			lpPropFB->Value.MVbin.lpbin[eDGMsgType].cb = lpEntryID->Value.bin.cb;
			lpPropFB->Value.MVbin.lpbin[eDGMsgType].lpb = lpEntryID->Value.bin.lpb;

			lpPropFBRef = lpPropFB; // use this one later on
		}

		// Put the MV property in the root folder
		hr = lpRoot->SetProps(1, lpPropFBRef, NULL);
		if(hr != hrSuccess)
			goto exit;

		// Put the MV property in the inbox folder
		hr = lpInbox->SetProps(1, lpPropFBRef, NULL);
		if(hr != hrSuccess)
			goto exit;
	}

	if(lpMessage == NULL) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// We now have a message lpMessage which is the LocalFreeBusy message.
	*lppFBMessage = lpMessage;

exit:
	MAPIFreeBuffer(lpszExplicitClass);
	MAPIFreeBuffer(lpAppEntryID);
	if(lpRoot)
		lpRoot->Release();
	MAPIFreeBuffer(lpPropFB);
	MAPIFreeBuffer(lpPropFBNew);
	MAPIFreeBuffer(lpEntryID);
	MAPIFreeBuffer(lpEntryIDInbox);
	if(lpInbox)
		lpInbox->Release();

	return hr;
}

/**
 * Set proccessing meeting request options of a user
 *
 * Use these options if you are responsible for coordinating resources, such as conference rooms.
 *
 * @param[in] lpMsgStore user store to get the options
 * @param[out] bAutoAccept Automatically accept meeting requests and proccess cancellations
 * @param[out] bDeclineConflict Automatically decline conflicting meeting requests
 * @param[out] bDeclineRecurring Automatically decline recurring meeting requests
 *
 * @note because a unknown issue it will update two different free/busy messages, one for 
 *		outlook 2000/xp and one for outlook 2003/2007.
 *
 * @todo find out why outlook 2000/xp opened the wrong local free/busy message
 * @todo check, should the properties PR_SCHDINFO_BOSS_WANTS_COPY, PR_SCHDINFO_DONT_MAIL_DELEGATES, 
 *		PR_SCHDINFO_BOSS_WANTS_INFO on TRUE?
 */
HRESULT SetAutoAcceptSettings(IMsgStore *lpMsgStore, bool bAutoAccept, bool bDeclineConflict, bool bDeclineRecurring)
{
	HRESULT hr = hrSuccess;
	IMessage *lpLocalFBMessage = NULL;
	SPropValue FBProps[6];

	// Meaning of these values are unknown, but are always TRUE in cases seen until now
	FBProps[0].ulPropTag = PROP_TAG(PT_BOOLEAN, 0x6842); // PR_SCHDINFO_BOSS_WANTS_COPY
	FBProps[0].Value.b = TRUE;
	FBProps[1].ulPropTag = PROP_TAG(PT_BOOLEAN, 0x6843); //PR_SCHDINFO_DONT_MAIL_DELEGATES
	FBProps[1].Value.b = TRUE;
	FBProps[2].ulPropTag = PROP_TAG(PT_BOOLEAN, 0x684B); // PR_SCHDINFO_BOSS_WANTS_INFO
	FBProps[2].Value.b = TRUE;

	FBProps[3].ulPropTag = PR_PROCESS_MEETING_REQUESTS;
	FBProps[3].Value.b = bAutoAccept ? TRUE : FALSE;
	FBProps[4].ulPropTag = PR_DECLINE_CONFLICTING_MEETING_REQUESTS;
	FBProps[4].Value.b = bDeclineConflict ? TRUE : FALSE;
	FBProps[5].ulPropTag = PR_DECLINE_RECURRING_MEETING_REQUESTS;
	FBProps[5].Value.b = bDeclineRecurring ? TRUE : FALSE;

	// Save localfreebusy settings
	hr = OpenLocalFBMessage(dgFreebusydata, lpMsgStore, true, &lpLocalFBMessage);
	if(hr != hrSuccess)
		goto exit;

	hr = lpLocalFBMessage->SetProps(6, FBProps, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpLocalFBMessage->SaveChanges(0);
	if(hr != hrSuccess)
		goto exit;

	lpLocalFBMessage->Release();
	lpLocalFBMessage = NULL;

	// Hack to support outlook 2000/2002 with resources
	hr = OpenLocalFBMessage(dgAssociated, lpMsgStore, true, &lpLocalFBMessage);
	if(hr != hrSuccess)
		goto exit;

	hr = lpLocalFBMessage->SetProps(6, FBProps, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpLocalFBMessage->SaveChanges(0);
	if(hr != hrSuccess)
		goto exit;

exit:
	if(lpLocalFBMessage)
		lpLocalFBMessage->Release();

	return hr;
}

/**
 * Get the proccessing meeting request options of a user
 *
 * Use these options if you are responsible for coordinating resources, such as conference rooms.
 *
 * @param[in] lpMsgStore user store to get the options
 * @param[out] lpbAutoAccept Automatically accept meeting requests and proccess cancellations
 * @param[out] lpbDeclineConflict Automatically decline conflicting meeting requests
 * @param[out] lpbDeclineRecurring Automatically decline recurring meeting requests
 *
 * @note you get the outlook 2003/2007 settings
 */
HRESULT GetAutoAcceptSettings(IMsgStore *lpMsgStore, bool *lpbAutoAccept, bool *lpbDeclineConflict, bool *lpbDeclineRecurring)
{
	HRESULT hr = hrSuccess;
	IMessage *lpLocalFBMessage = NULL;
	LPSPropValue lpProps = NULL;
	SizedSPropTagArray(3, sptaFBProps) = {3, {PR_PROCESS_MEETING_REQUESTS, PR_DECLINE_CONFLICTING_MEETING_REQUESTS, PR_DECLINE_RECURRING_MEETING_REQUESTS}};
	ULONG cValues = 0;

	bool bAutoAccept = false;
	bool bDeclineConflict = false;
	bool bDeclineRecurring = false;

	hr = OpenLocalFBMessage(dgFreebusydata, lpMsgStore, false, &lpLocalFBMessage);
	if(hr == hrSuccess) {
		hr = lpLocalFBMessage->GetProps(sptaFBProps, 0, &cValues, &lpProps);
		if(FAILED(hr))
			goto exit;

		if(lpProps[0].ulPropTag == PR_PROCESS_MEETING_REQUESTS)
			bAutoAccept = lpProps[0].Value.b;
		if(lpProps[1].ulPropTag == PR_DECLINE_CONFLICTING_MEETING_REQUESTS)
			bDeclineConflict = lpProps[1].Value.b;
		if(lpProps[2].ulPropTag == PR_DECLINE_RECURRING_MEETING_REQUESTS)
			bDeclineRecurring = lpProps[2].Value.b;
	}
	// else, hr != hrSuccess: no FB -> all settings are FALSE
	hr = hrSuccess;

	*lpbAutoAccept = bAutoAccept;
	*lpbDeclineConflict = bDeclineConflict;
	*lpbDeclineRecurring = bDeclineRecurring;

exit:
	MAPIFreeBuffer(lpProps);
	if(lpLocalFBMessage)
		lpLocalFBMessage->Release();

	return hr;
}

HRESULT HrGetRemoteAdminStore(IMAPISession *lpMAPISession, IMsgStore *lpMsgStore, LPCTSTR lpszServerName, ULONG ulFlags, IMsgStore **lppMsgStore)
{
	ExchangeManageStorePtr ptrEMS;
	ULONG cbStoreId;
	EntryIdPtr ptrStoreId;
	MsgStorePtr ptrMsgStore;

	if (lpMAPISession == NULL || lpMsgStore == NULL ||
	    lpszServerName == NULL || (ulFlags & ~(MAPI_UNICODE | MDB_WRITE)) ||
	    lppMsgStore == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = lpMsgStore->QueryInterface(ptrEMS.iid, &ptrEMS);
	if (hr != hrSuccess)
		return hr;
	if (ulFlags & MAPI_UNICODE) {
		std::wstring strMsgStoreDN = std::wstring(L"cn=") + (LPCWSTR)lpszServerName + L"/cn=Microsoft Private MDB";
		hr = ptrEMS->CreateStoreEntryID((LPTSTR)strMsgStoreDN.c_str(), (LPTSTR)L"SYSTEM", MAPI_UNICODE|OPENSTORE_OVERRIDE_HOME_MDB, &cbStoreId, &~ptrStoreId);
	} else {
		std::string strMsgStoreDN = std::string("cn=") + (LPCSTR)lpszServerName + "/cn=Microsoft Private MDB";
		hr = ptrEMS->CreateStoreEntryID((LPTSTR)strMsgStoreDN.c_str(), (LPTSTR)"SYSTEM", OPENSTORE_OVERRIDE_HOME_MDB, &cbStoreId, &~ptrStoreId);
	}
	if (hr != hrSuccess)
		return hr;
	hr = lpMAPISession->OpenMsgStore(0, cbStoreId, ptrStoreId, &ptrMsgStore.iid, ulFlags & MDB_WRITE, &ptrMsgStore);
	if (hr != hrSuccess)
		return hr;
	return ptrMsgStore->QueryInterface(IID_IMsgStore,
	       reinterpret_cast<LPVOID *>(lppMsgStore));
}

HRESULT HrGetGAB(LPMAPISESSION lpSession, LPABCONT *lppGAB)
{
	AddrBookPtr ptrAddrBook;

	if (lpSession == NULL || lppGAB == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = lpSession->OpenAddressBook(0, 0, 0, &ptrAddrBook);
	if (hr != hrSuccess)
		return hr;
	return HrGetGAB(ptrAddrBook, lppGAB);
}

HRESULT HrGetGAB(LPADRBOOK lpAddrBook, LPABCONT *lppGAB)
{
	ULONG ulType = 0;
	ABContainerPtr ptrRoot;
	MAPITablePtr ptrTable;
	SRowSetPtr ptrRows;
	ABContainerPtr ptrGAB;

	SPropValue propDisplayType;
	SPropValue propEmsAbContainerid;

	SizedSPropTagArray(1, sptaTableProps) = {1, {PR_ENTRYID}};

	if (lpAddrBook == NULL || lppGAB == NULL)
		return MAPI_E_INVALID_PARAMETER;
	HRESULT hr = lpAddrBook->OpenEntry(0, NULL, &ptrRoot.iid, MAPI_DEFERRED_ERRORS, &ulType, &ptrRoot);
	if (hr != hrSuccess)
		return hr;
	hr = ptrRoot->GetHierarchyTable(MAPI_DEFERRED_ERRORS, &ptrTable);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->SetColumns(sptaTableProps, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;

	propDisplayType.ulPropTag = PR_DISPLAY_TYPE;
	propDisplayType.Value.l = DT_GLOBAL;

	propEmsAbContainerid.ulPropTag = PR_EMS_AB_CONTAINERID;
	propEmsAbContainerid.Value.l = 0;

	hr = ECOrRestriction(
			ECPropertyRestriction(RELOP_EQ, PR_DISPLAY_TYPE, &propDisplayType, ECRestriction::Shallow) +
			ECAndRestriction(
				ECExistRestriction(PR_EMS_AB_CONTAINERID) +
				ECPropertyRestriction(RELOP_EQ, PR_EMS_AB_CONTAINERID, &propEmsAbContainerid, ECRestriction::Shallow)
			)
		).FindRowIn(ptrTable, BOOKMARK_BEGINNING, 0);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTable->QueryRows(1, 0, &ptrRows);
	if (hr != hrSuccess)
		return hr;
	hr = lpAddrBook->OpenEntry(ptrRows[0].lpProps[0].Value.bin.cb, (LPENTRYID)ptrRows[0].lpProps[0].Value.bin.lpb, &ptrGAB.iid, 0, &ulType, &ptrGAB);
	if (hr != hrSuccess)
		return hr;
	return ptrGAB->QueryInterface(IID_IABContainer, reinterpret_cast<LPVOID *>(lppGAB));
}

/** 
 * Opens or creates an associated message in the non-ipm subtree of
 * the given store.
 * 
 * @param[in] lpStore User or public store to find message in
 * @param[in] szMessageName Name of the configuration message
 * @param[out] lppMessage Message to load/save your custom data from/to
 * 
 * @return MAPI Error code
 */
HRESULT GetConfigMessage(LPMDB lpStore, const char* szMessageName, IMessage **lppMessage)
{
	ULONG cValues;
	SPropArrayPtr ptrEntryIDs;
	MAPIFolderPtr ptrFolder;
	ULONG ulType;
	MAPITablePtr ptrTable;
	SPropValue propSubject;
	SRowSetPtr ptrRows;
	LPSPropValue lpEntryID = NULL;
	MessagePtr ptrMessage;
	SizedSPropTagArray(2, sptaTreeProps) = {2, {PR_NON_IPM_SUBTREE_ENTRYID, PR_IPM_SUBTREE_ENTRYID}};

	HRESULT hr = lpStore->GetProps(sptaTreeProps, 0, &cValues, &~ptrEntryIDs);
	if (FAILED(hr))
		return hr;

	// NON_IPM on a public store, IPM on a normal store
	if (ptrEntryIDs[0].ulPropTag == sptaTreeProps.aulPropTag[0])
		hr = lpStore->OpenEntry(ptrEntryIDs[0].Value.bin.cb, (LPENTRYID)ptrEntryIDs[0].Value.bin.lpb, NULL, MAPI_MODIFY, &ulType, &ptrFolder);
	else if (ptrEntryIDs[1].ulPropTag == sptaTreeProps.aulPropTag[1])
		hr = lpStore->OpenEntry(ptrEntryIDs[1].Value.bin.cb, (LPENTRYID)ptrEntryIDs[1].Value.bin.lpb, NULL, MAPI_MODIFY, &ulType, &ptrFolder);
	else
		hr = MAPI_E_INVALID_PARAMETER;
	if (hr != hrSuccess)
		return hr;
	hr = ptrFolder->GetContentsTable(MAPI_DEFERRED_ERRORS | MAPI_ASSOCIATED, &ptrTable);
	if (hr != hrSuccess)
		return hr;

	propSubject.ulPropTag = PR_SUBJECT_A;
	propSubject.Value.lpszA = (char*)szMessageName;

	hr = ECPropertyRestriction(RELOP_EQ, PR_SUBJECT_A, &propSubject, ECRestriction::Shallow).FindRowIn(ptrTable, BOOKMARK_BEGINNING, 0);
	if (hr == hrSuccess) {
		hr = ptrTable->QueryRows(1, 0, &ptrRows);
		if (hr != hrSuccess)
			return hr;
	}

	if (!ptrRows.empty()) {
		// message found, open it
		lpEntryID = PpropFindProp(ptrRows[0].lpProps, ptrRows[0].cValues, PR_ENTRYID);
		if (lpEntryID == NULL)
			return MAPI_E_INVALID_ENTRYID;
		hr = ptrFolder->OpenEntry(lpEntryID->Value.bin.cb, (LPENTRYID)lpEntryID->Value.bin.lpb, NULL, MAPI_MODIFY, &ulType, &ptrMessage);
		if (hr != hrSuccess)
			return hr;
	} else {
		// not found in folder, create new message
		hr = ptrFolder->CreateMessage(&IID_IMessage, MAPI_ASSOCIATED, &ptrMessage);
		if (hr != hrSuccess)
			return hr;
		hr = ptrMessage->SetProps(1, &propSubject, NULL);
		if (hr != hrSuccess)
			return hr;

		// set mandatory message property
		propSubject.ulPropTag = PR_MESSAGE_CLASS_A;
		propSubject.Value.lpszA = const_cast<char *>("IPM.Zarafa.Configuration");

		hr = ptrMessage->SetProps(1, &propSubject, NULL);
		if (hr != hrSuccess)
			return hr;
	}

	*lppMessage = ptrMessage.release();
	return hrSuccess;
}

} /* namespace */
