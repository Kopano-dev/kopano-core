/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <string>
#include <memory>
#include <map>
#include <utility>
#include <kopano/memory.hpp>
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
#include <kopano/mapi_ptr.h>
#include <kopano/namedprops.h>
#include <kopano/charset/convert.h>
#include <kopano/mapiext.h>
#include "freebusytags.h"
#include <edkguid.h>
#include <kopano/mapiguidext.h>
#include <edkmdb.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/EMSAbTag.h>
#include <kopano/ECRestriction.h>
#include <kopano/MAPIErrors.h>
#include <sys/types.h>
#include <sys/socket.h>

using namespace std::string_literals;

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

static HRESULT HrOpenECPublicStore(IMAPISession *, ULONG flags, IMsgStore **out);
static HRESULT HrOpenUserMsgStore(IMAPISession *, IMsgStore *, const wchar_t *user, IMsgStore **out);

/* Newmail Notify columns */
static constexpr const SizedSPropTagArray(4, sPropNewMailColumns) = {
	4,
	{
		PR_ENTRYID,
		PR_PARENT_ENTRYID,
		PR_MESSAGE_CLASS_A,
		PR_MESSAGE_FLAGS,
	}
};

} /* namespace */

bool operator==(const SBinary &left, const SBinary &right) noexcept
{
	return left.cb == right.cb && memcmp(left.lpb, right.lpb, left.cb) == 0;
}

bool operator<(const SBinary &left, const SBinary &right) noexcept
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
	return "";
}

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
static HRESULT CreateProfileTemp(const wchar_t *username,
    const wchar_t *password, const char *path, const char *szProfName,
    ULONG ulProfileFlags, const char *sslkey_file, const char *sslkey_password,
    const char *app_version, const char *app_misc)
{
	object_ptr<IProfAdmin> lpProfAdmin;
	object_ptr<IMsgServiceAdmin> lpServiceAdmin1;
	object_ptr<IMsgServiceAdmin2> lpServiceAdmin;
	MAPIUID service_uid;
	SPropValue sProps[9];	// server, username, password and profile -name and -flags, optional sslkey file with sslkey password
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRows;

//-- create profile
	auto hr = MAPIAdminProfiles(0, &~lpProfAdmin);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAdminProfiles failed", hr);
	lpProfAdmin->DeleteProfile(reinterpret_cast<const TCHAR *>(szProfName), 0);
	hr = lpProfAdmin->CreateProfile(reinterpret_cast<const TCHAR *>(szProfName), reinterpret_cast<const TCHAR *>(""), 0, 0);
	if (hr != hrSuccess)
		return kc_perrorf("CreateProfile failed", hr);
	hr = lpProfAdmin->AdminServices(reinterpret_cast<const TCHAR *>(szProfName), reinterpret_cast<const TCHAR *>(""), 0, 0, &~lpServiceAdmin1);
	if (hr != hrSuccess)
		return kc_perrorf("AdminServices failed", hr);
	hr = lpServiceAdmin1->QueryInterface(IID_IMsgServiceAdmin2, &~lpServiceAdmin);
	if (hr != hrSuccess)
		return kc_perrorf("QueryInterface failed", hr);
	hr = lpServiceAdmin->CreateMsgServiceEx("ZARAFA6", "", 0, 0, &service_uid);
	if (hr != hrSuccess)
		return kc_perrorf("CreateMsgService ZARAFA6 failed", hr);
	// Get the PR_SERVICE_UID from the row
	unsigned int i = 0;
	sProps[i].ulPropTag = PR_EC_PATH;
	sProps[i].Value.lpszA = const_cast<char *>(path != NULL && *path != '\0' ? path : "default:");
	++i;
	if (username != nullptr) {
		sProps[i].ulPropTag = PR_EC_USERNAME_W;
		sProps[i++].Value.lpszW = const_cast<wchar_t *>(username);
	}
	if (password != nullptr) {
		sProps[i].ulPropTag = PR_EC_USERPASSWORD_W;
		sProps[i++].Value.lpszW = const_cast<wchar_t *>(password);
	}
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
	return lpServiceAdmin->ConfigureMsgService(&service_uid, 0, 0, i, sProps);
}

/**
 * Deletes a profile with specified name.
 *
 * @param[in]	szProfName	Name of the profile to delete
 *
 * @return		HRESULT		Mapi error code.
 */
static HRESULT DeleteProfileTemp(const char *szProfName)
{
	object_ptr<IProfAdmin> lpProfAdmin;
	// Get the MAPI Profile administration object
	auto hr = MAPIAdminProfiles(0, &~lpProfAdmin);
	if (hr != hrSuccess)
		return hr;
	return lpProfAdmin->DeleteProfile(reinterpret_cast<const TCHAR *>(szProfName), 0);
}

HRESULT HrOpenECAdminSession(IMAPISession **lppSession,
    const char *const app_version, const char *const app_misc,
    const char *szPath, ULONG ulProfileFlags, const char *sslkey_file,
    const char *sslkey_password)
{
	return HrOpenECSession(lppSession, app_version, app_misc, KOPANO_SYSTEM_USER_W, KOPANO_SYSTEM_USER_W, szPath, ulProfileFlags, sslkey_file, sslkey_password);
}

HRESULT HrOpenECSession(IMAPISession **ses, const char *appver,
    const char *appmisc, const char *user, const char *pass, const char *path,
    ULONG flags, const char *sslkey, const char *sslpass, const char *profname)
{
	const wchar_t *u = nullptr, *p = nullptr;
	std::wstring wu, wp;
	try {
		if (user != nullptr) {
			wu = convert_to<std::wstring>(user);
			u = wu.c_str();
		}
		if (pass != nullptr) {
			wp = convert_to<std::wstring>(pass);
			p = wp.c_str();
		}
	} catch (const convert_exception &) {
		return MAPI_E_BAD_CHARWIDTH;
	}
	return HrOpenECSession(ses, appver, appmisc, u, p, path, flags,
	       sslkey, sslpass, profname);
}

HRESULT HrOpenECSession(IMAPISession **lppSession,
    const char *const app_version, const char *const app_misc,
    const wchar_t *szUsername, const wchar_t *szPassword, const char *szPath,
    ULONG ulProfileFlags, const char *sslkey_file, const char *sslkey_password,
    const char *profname)
{
	auto szProfName = make_unique_nt<char[]>(strlen(PROFILEPREFIX) + 10 + 1);
	if (szProfName == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	IMAPISession *lpMAPISession = NULL;

	if (profname == nullptr)
		snprintf(szProfName.get(), strlen(PROFILEPREFIX)+10+1, "%s%010u", PROFILEPREFIX, rand_mt());
	else
		strcpy(szProfName.get(), profname);

	if (sslkey_file != NULL) {
		FILE *ssltest = fopen(sslkey_file, "r");
		if (!ssltest) {
			ec_log_crit("Cannot access SSL key file \"%s\": %s", sslkey_file, strerror(errno));

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

	auto hr = CreateProfileTemp(szUsername, szPassword, szPath, szProfName.get(), ulProfileFlags, sslkey_file, sslkey_password, app_version, app_misc);
	if (hr != hrSuccess)
		goto exit;

	// Log on the the profile
	hr = MAPILogonEx(0, (LPTSTR)szProfName.get(), (LPTSTR)"", MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_NO_MAIL, &lpMAPISession);
	if (hr != hrSuccess) {
		kc_perror("MAPILogonEx failed", hr);
		goto exit;
	}

	*lppSession = lpMAPISession;

exit:
	/*
	 * Always try to delete the temporary profile. On M4L, the session will
	 * now reference an anonymous profile. (The profile still has a name to
	 * itself, but is not reachable from the Profile List.)
	 */
	DeleteProfileTemp(szProfName.get());
	return hr;
}

static HRESULT HrSearchECStoreEntryId(IMAPISession *lpMAPISession,
    BOOL bPublic, ULONG *lpcbEntryID, ENTRYID **lppEntryID)
{
	rowset_ptr lpRows;
	object_ptr<IMAPITable> lpStoreTable;
	const SPropValue *lpEntryIDProp = nullptr;

	// Get the default store by searching through the message store table and finding the
	// store with PR_MDB_PROVIDER set to the kopano public store GUID
	auto hr = lpMAPISession->GetMsgStoresTable(0, &~lpStoreTable);
	if(hr != hrSuccess)
		return hr;

	while(TRUE) {
		hr = lpStoreTable->QueryRows(1, 0, &~lpRows);
		if (hr != hrSuccess || lpRows->cRows != 1)
			return MAPI_E_NOT_FOUND;
		if (bPublic) {
			auto lpStoreProp = lpRows[0].cfind(PR_MDB_PROVIDER);
			if (lpStoreProp != NULL && memcmp(lpStoreProp->Value.bin.lpb, &KOPANO_STORE_PUBLIC_GUID, sizeof(MAPIUID)) == 0 )
				break;
		} else {
			auto lpStoreProp = lpRows[0].cfind(PR_RESOURCE_FLAGS);
			if (lpStoreProp != NULL && lpStoreProp->Value.ul & STATUS_DEFAULT_STORE)
				break;
		}
	}

	lpEntryIDProp = lpRows[0].cfind(PR_ENTRYID);
	if (lpEntryIDProp == nullptr)
		return MAPI_E_NOT_FOUND;

	// copy entryid so we continue in the same code piece in windows/linux
	return Util::HrCopyEntryId(lpEntryIDProp->Value.bin.cb,
	       reinterpret_cast<ENTRYID *>(lpEntryIDProp->Value.bin.lpb),
	       lpcbEntryID, lppEntryID);
}

HRESULT HrOpenDefaultStore(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore) {
	return HrOpenDefaultStore(lpMAPISession, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, lppMsgStore);
}

static HRESULT GetProxyStoreObject(IMsgStore *lpMsgStore, IMsgStore **lppMsgStore)
{
	object_ptr<IProxyStoreObject> lpProxyStoreObject;
	IUnknown *lpECMsgStore = nullptr;
	memory_ptr<SPropValue> lpPropValue;

	if (lpMsgStore == nullptr || lppMsgStore == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (lpMsgStore->QueryInterface(IID_IProxyStoreObject, &~lpProxyStoreObject) == hrSuccess) {
		auto hr = lpProxyStoreObject->UnwrapNoRef((LPVOID*)lppMsgStore);
		if (hr != hrSuccess)
			return hr;
		(*lppMsgStore)->AddRef();
		return hrSuccess;
	} else if (HrGetOneProp(lpMsgStore, PR_EC_OBJECT, &~lpPropValue) == hrSuccess) {
		lpECMsgStore = reinterpret_cast<IUnknown *>(lpPropValue->Value.lpszA);
		if (lpECMsgStore == nullptr)
			return MAPI_E_INVALID_PARAMETER;
		return lpECMsgStore->QueryInterface(IID_IMsgStore, (void**)lppMsgStore);
	}
	// Possible object already wrapped, gives the original object back
	(*lppMsgStore) = lpMsgStore;
	(*lppMsgStore)->AddRef();
	return hrSuccess;
}

HRESULT HrOpenDefaultStore(IMAPISession *lpMAPISession, ULONG ulFlags, IMsgStore **lppMsgStore) {
	ULONG			cbEntryID = 0;
	memory_ptr<ENTRYID> lpEntryID;

	HRESULT hr = HrSearchECStoreEntryId(lpMAPISession, FALSE, &cbEntryID, &~lpEntryID);
	if (hr != hrSuccess)
		return hr;
	return lpMAPISession->OpenMsgStore(0, cbEntryID, lpEntryID,
	       &IID_IMsgStore, ulFlags, lppMsgStore);
}

HRESULT HrOpenECPublicStore(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore){
	return HrOpenECPublicStore(lpMAPISession, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, lppMsgStore);
}

HRESULT HrOpenECPublicStoreOnline(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore)
{
	object_ptr<IMsgStore> lpMsgStore, lpProxedMsgStore;
	auto hr = HrOpenECPublicStore(lpMAPISession, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, &~lpMsgStore);
	if(hr != hrSuccess)
		return hr;
	hr = GetProxyStoreObject(lpMsgStore, &~lpProxedMsgStore);
	if (hr != hrSuccess)
		return hr;
	return lpProxedMsgStore->QueryInterface(IID_ECMsgStoreOnline, reinterpret_cast<void **>(lppMsgStore));
}

static HRESULT HrOpenECPublicStore(IMAPISession *lpMAPISession, ULONG ulFlags,
    IMsgStore **lppMsgStore)
{
	ULONG			cbEntryID = 0;
	memory_ptr<ENTRYID> lpEntryID;
	HRESULT hr = HrSearchECStoreEntryId(lpMAPISession, TRUE, &cbEntryID, &~lpEntryID);
	if(hr != hrSuccess)
		return hr;
	return lpMAPISession->OpenMsgStore(0, cbEntryID, lpEntryID,
	       &IID_IMsgStore, ulFlags, lppMsgStore);
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
HRESULT ECCreateOneOff(const TCHAR *lpszName, const TCHAR *lpszAdrType,
    const TCHAR *lpszAddress, ULONG ulFlags, ULONG *lpcbEntryID,
    ENTRYID **lppEntryID)
{
	std::string strOneOff;
	MAPIUID uid = {MAPI_ONE_OFF_UID};
	unsigned short usFlags = (((ulFlags & MAPI_UNICODE)?MAPI_ONE_OFF_UNICODE:0) | ((ulFlags & MAPI_SEND_NO_RICH_INFO)?MAPI_ONE_OFF_NO_RICH_INFO:0));

	if (lpszAdrType == NULL || lpszAddress == NULL)
		return MAPI_E_INVALID_PARAMETER;

	strOneOff.append(4, '\0'); // abFlags
	strOneOff.append(reinterpret_cast<const char *>(&uid), sizeof(MAPIUID));
	strOneOff.append(2, '\0'); // version (0)
	usFlags = cpu_to_le16(usFlags);
	strOneOff.append(reinterpret_cast<const char *>(&usFlags), sizeof(usFlags));

	if(ulFlags & MAPI_UNICODE)
	{
		std::wstring wstrName(const_cast<wchar_t *>(lpszName != nullptr ? lpszName : lpszAddress));
		auto strUnicode = convert_to<std::u16string>(wstrName);
		strOneOff.append(reinterpret_cast<const char *>(strUnicode.c_str()), (strUnicode.length() + 1) * sizeof(char16_t));
		strUnicode = convert_to<std::u16string>(reinterpret_cast<const wchar_t *>(lpszAdrType));
		strOneOff.append(reinterpret_cast<const char *>(strUnicode.c_str()), (strUnicode.length() + 1) * sizeof(char16_t));
		strUnicode = convert_to<std::u16string>(reinterpret_cast<const wchar_t *>(lpszAddress));
		strOneOff.append(reinterpret_cast<const char *>(strUnicode.c_str()), (strUnicode.length() + 1) * sizeof(char16_t));
	} else {
		auto name = reinterpret_cast<const char *>(lpszName);
		auto atyp = reinterpret_cast<const char *>(lpszAdrType);
		auto addr = reinterpret_cast<const char *>(lpszAddress);
		if (lpszName)
			strOneOff.append(name, strlen(name) + 1);
		else
			strOneOff.append(1, '\0');
		strOneOff.append(atyp, strlen(atyp) + 1);
		strOneOff.append(addr, strlen(addr) + 1);
	}

	auto hr = KAllocCopy(strOneOff.c_str(), strOneOff.size(), reinterpret_cast<void **>(lppEntryID));
	if(hr != hrSuccess)
		return hr;
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
	MAPIUID		muidOneOff = {MAPI_ONE_OFF_UID};
	auto lpBuffer = reinterpret_cast<const char *>(lpEntryID);
	unsigned short usFlags;
	std::wstring name, type, addr;
	uint16_t tmp2;
	uint32_t tmp4;

	if (cbEntryID < (8 + sizeof(MAPIUID)) || lpEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;
	memcpy(&tmp4, lpBuffer, sizeof(tmp4));
	tmp4 = le32_to_cpu(tmp4);
	if (tmp4 != 0)
		return MAPI_E_INVALID_PARAMETER;
	lpBuffer += 4;

	if (memcmp(&muidOneOff, lpBuffer, sizeof(MAPIUID)) != 0)
		return MAPI_E_INVALID_PARAMETER;
	lpBuffer += sizeof(MAPIUID);
	memcpy(&tmp2, lpBuffer, sizeof(tmp2));
	tmp2 = le16_to_cpu(tmp2);
	if (tmp2 != 0)
		return MAPI_E_INVALID_PARAMETER;
	lpBuffer += 2;

	memcpy(&usFlags, lpBuffer, sizeof(usFlags));
	usFlags = le16_to_cpu(usFlags);
	lpBuffer += 2;

	if(usFlags & MAPI_ONE_OFF_UNICODE) {
		std::u16string str;

		str.assign(reinterpret_cast<std::u16string::const_pointer>(lpBuffer));
		// can be 0 length
		auto hr = TryConvert(str, name);
		if (hr != hrSuccess)
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
		std::string str = lpBuffer;
		// can be 0 length
		auto hr = TryConvert(lpBuffer, rawsize(lpBuffer), "windows-1252", name);
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
 * UTF-8 charset.
 *
 * @param[in]	input	Input wide string
 * @return				Output string in e-mail header format
 */
std::string ToQuotedBase64Header(const std::wstring &input)
{
	auto str = convert_to<std::string>("UTF-8", input, rawsize(input), CHARSET_WCHAR);
	return "=?UTF-8?B?" + base64_encode(str.c_str(), str.length()) += "?=";
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
	// Newmail notify
	ULONG			cNewMailValues = 0;
	memory_ptr<SPropValue> lpNewMailPropArray;
	NOTIFICATION	sNotification;

	// Get notify properties
	auto hr = lpMessage->GetProps(sPropNewMailColumns, 0, &cNewMailValues, &~lpNewMailPropArray);
	if (hr != hrSuccess)
		return hr;

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

	// TODO: errors of NotifyNewMail should be demoted to a warning?
	return lpMDB->NotifyNewMail(&sNotification);
}

// Create Search key for recipients
HRESULT HrCreateEmailSearchKey(const char *lpszEmailType,
    const char *lpszEmail, ULONG *cb, LPBYTE *lppByte)
{
	memory_ptr<BYTE> lpByte;
	unsigned int size = 2; // : and \0
	unsigned int sizeEmailType = lpszEmailType != nullptr ? strlen(lpszEmailType) : 0;
	unsigned int sizeEmail = lpszEmail != nullptr ? strlen(lpszEmail) : 0;
	size = sizeEmailType + sizeEmail + 2; // : and \0
	auto hr = MAPIAllocateBuffer(size, &~lpByte);
	if(hr != hrSuccess)
		return hr;
	memcpy(lpByte, lpszEmailType, sizeEmailType);
	*(lpByte + sizeEmailType) = ':';
	memcpy(lpByte + sizeEmailType + 1, lpszEmail, sizeEmail);
	*(lpByte + size - 1) = 0;
	auto a = lpByte.get();
	while (*a != '\0') {
		*a = toupper(*a);
		++a;
	}
	*lppByte = lpByte.release();
	*cb = size;
	return hrSuccess;
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
	SizedSPropTagArray(4, sptaProps) = { 4, { ulPropTagEntryID, ulPropTagName, ulPropTagType, ulPropTagEmailAddress } };
	ULONG cValues = 0;
	memory_ptr<SPropValue> lpProps;

	if (lpAdrBook == nullptr || lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpMessage->GetProps(sptaProps, 0, &cValues, &~lpProps);
	if (FAILED(hr))
		return hr;
	return HrGetAddress(lpAdrBook, lpProps, cValues, ulPropTagEntryID,
	       ulPropTagName, ulPropTagType, ulPropTagEmailAddress, strName,
	       strType, strEmailAddress);
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
	adrlist_ptr lpAdrList;
	const SPropValue *lpEntryID = NULL;
    ULONG ulType = 0;
	object_ptr<IMAPIProp> lpMailUser;
	memory_ptr<SPropValue> lpSMTPAddress, lpEmailAddress;
     
	auto hr = MAPIAllocateBuffer(CbNewADRLIST(1), &~lpAdrList);
	if (hr != hrSuccess)
		return hr;
	lpAdrList->cEntries = 0;
    lpAdrList->aEntries[0].cValues = 1;

    hr = MAPIAllocateBuffer(sizeof(SPropValue), (void **)&lpAdrList->aEntries[0].rgPropVals);
    if(hr != hrSuccess)
		return hr;
	++lpAdrList->cEntries;
    lpAdrList->aEntries[0].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_W;
    lpAdrList->aEntries[0].rgPropVals[0].Value.lpszW = (WCHAR *)strResolve.c_str();
    
    hr = lpAdrBook->ResolveName(0, ulFlags | MAPI_UNICODE, NULL, lpAdrList);
    if(hr != hrSuccess)
		return hr;
	if (lpAdrList->cEntries != 1)
		return MAPI_E_NOT_FOUND;
	lpEntryID = lpAdrList->aEntries[0].cfind(PR_ENTRYID);
	if (lpEntryID == nullptr)
		return MAPI_E_NOT_FOUND;
    hr = lpAdrBook->OpenEntry(lpEntryID->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpEntryID->Value.bin.lpb), &IID_IMAPIProp, 0, &ulType, &~lpMailUser);
    if (hr != hrSuccess)
		return hr;
    hr = HrGetOneProp(lpMailUser, PR_SMTP_ADDRESS_W, &~lpSMTPAddress);
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
        hr = HrGetOneProp(lpMailUser, PR_EMAIL_ADDRESS_W, &~lpEmailAddress);
        if(hr != hrSuccess)
			return hr;
        strSMTPAddress = lpEmailAddress->Value.lpszW;
    } else {
		if (lpSMTPAddress == nullptr)
			return MAPI_E_NOT_FOUND;
        strSMTPAddress = lpSMTPAddress->Value.lpszW;
    }
	return hrSuccess;
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
HRESULT HrGetAddress(IAddrBook *lpAdrBook, const SPropValue *lpProps,
    ULONG cValues, ULONG ulPropTagEntryID, ULONG ulPropTagName,
    ULONG ulPropTagType, ULONG ulPropTagEmailAddress, std::wstring &strName,
    std::wstring &strType, std::wstring &strEmailAddress)
{
	HRESULT hr = hrSuccess;
	const SPropValue *lpEntryID = nullptr, *lpName = nullptr;
	const SPropValue *lpType = nullptr, *lpAddress = nullptr;
	std::wstring strSMTPAddress;
	convert_context converter;

	strName.clear();
	strType.clear();
	strEmailAddress.clear();

	if (lpProps && cValues) {
		lpEntryID	= PCpropFindProp(lpProps, cValues, ulPropTagEntryID);
		lpName		= PCpropFindProp(lpProps, cValues, ulPropTagName);
		lpType		= PCpropFindProp(lpProps, cValues, ulPropTagType);
		lpAddress	= PCpropFindProp(lpProps, cValues, ulPropTagEmailAddress);
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
	    HrGetAddress(lpAdrBook, reinterpret_cast<const ENTRYID *>(lpEntryID->Value.bin.lpb), lpEntryID->Value.bin.cb, strName, strType, strEmailAddress) != hrSuccess) {
        // EntryID failed, try fallback
        if (lpName) {
			if (PROP_TYPE(lpName->ulPropTag) == PT_UNICODE)
				strName = lpName->Value.lpszW;
			else
				strName = converter.convert_to<std::wstring>(lpName->Value.lpszA);
		}
        if (lpType) {
			if (PROP_TYPE(lpType->ulPropTag) == PT_UNICODE)
				strType = lpType->Value.lpszW;
			else
				strType = converter.convert_to<std::wstring>(lpType->Value.lpszA);
		}
        if (lpAddress) {
			if (PROP_TYPE(lpAddress->ulPropTag) == PT_UNICODE)
				strEmailAddress = lpAddress->Value.lpszW;
			else
				strEmailAddress = converter.convert_to<std::wstring>(lpAddress->Value.lpszA);
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
HRESULT HrGetAddress(IAddrBook *lpAdrBook, const ENTRYID *lpEntryID,
    ULONG cbEntryID, std::wstring &strName, std::wstring &strType,
    std::wstring &strEmailAddress)
{
	object_ptr<IMailUser> lpMailUser;
	unsigned int ulType = 0, cMailUserValues = 0;
	memory_ptr<SPropValue> lpMailUserProps;
	static constexpr const SizedSPropTagArray(4, sptaAddressProps) =
		{4, {PR_DISPLAY_NAME_W, PR_ADDRTYPE_W, PR_EMAIL_ADDRESS_W,
		PR_SMTP_ADDRESS_W}};

	if (lpAdrBook == nullptr || lpEntryID == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpAdrBook->OpenEntry(cbEntryID, lpEntryID, &IID_IMailUser, 0, &ulType, &~lpMailUser);
	if (hr != hrSuccess)
		return hr;
	hr = lpMailUser->GetProps(sptaAddressProps, MAPI_UNICODE, &cMailUserValues, &~lpMailUserProps);
	if (FAILED(hr))
		return hr;
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
	return hrSuccess;
}

HRESULT DoSentMail(IMAPISession *lpSession, IMsgStore *lpMDBParam,
    ULONG ulFlags, object_ptr<IMessage> lpMessage)
{
	object_ptr<IMsgStore> lpMDB;
	object_ptr<IMAPIFolder> lpFolder;
	ENTRYLIST		sMsgList;
	SBinary			sEntryID;
	memory_ptr<SPropValue> lpPropValue;
	unsigned int cValues = 0, ulType = 0;
	enum esPropDoSentMail{ DSM_ENTRYID, DSM_PARENT_ENTRYID, DSM_SENTMAIL_ENTRYID, DSM_DELETE_AFTER_SUBMIT, DSM_STORE_ENTRYID};
	static constexpr const SizedSPropTagArray(5, sPropDoSentMail) =
		{5, {PR_ENTRYID, PR_PARENT_ENTRYID, PR_SENTMAIL_ENTRYID,
		PR_DELETE_AFTER_SUBMIT, PR_STORE_ENTRYID}};

	assert(lpSession != NULL || lpMDBParam != NULL);
    
	// Check incomming parameter
	if (lpMessage == nullptr)
		return MAPI_E_INVALID_OBJECT;

	// Get Sentmail properties
	auto hr = lpMessage->GetProps(sPropDoSentMail, 0, &cValues, &~lpPropValue);
	if(FAILED(hr) || 
		(lpPropValue[DSM_SENTMAIL_ENTRYID].ulPropTag != PR_SENTMAIL_ENTRYID && 
		lpPropValue[DSM_DELETE_AFTER_SUBMIT].ulPropTag != PR_DELETE_AFTER_SUBMIT)
	  )
		// Ignore error, leave the mail where it is
		return hrSuccess;
	else if (lpPropValue[DSM_ENTRYID].ulPropTag != PR_ENTRYID ||
			 lpPropValue[DSM_PARENT_ENTRYID].ulPropTag != PR_PARENT_ENTRYID ||
			 lpPropValue[DSM_STORE_ENTRYID].ulPropTag != PR_STORE_ENTRYID)
		// Those properties are always needed
		return MAPI_E_NOT_FOUND;

	lpMessage.reset();

	if (lpMDBParam == NULL)
		hr = lpSession->OpenMsgStore(0, lpPropValue[DSM_STORE_ENTRYID].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpPropValue[DSM_STORE_ENTRYID].Value.bin.lpb), nullptr, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL |MDB_TEMPORARY, &~lpMDB);
	else
		hr = lpMDBParam->QueryInterface(IID_IMsgStore, &~lpMDB);
	if(hr != hrSuccess)
		return hr;

	sEntryID = lpPropValue[DSM_ENTRYID].Value.bin;
	sMsgList.cValues = 1;
	sMsgList.lpbin = &sEntryID;

	// Handle PR_SENTMAIL_ENTRYID
	if(lpPropValue[DSM_SENTMAIL_ENTRYID].ulPropTag == PR_SENTMAIL_ENTRYID)
	{
		//Open Sentmail Folder
		hr = lpMDB->OpenEntry(lpPropValue[DSM_SENTMAIL_ENTRYID].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpPropValue[DSM_SENTMAIL_ENTRYID].Value.bin.lpb),
		     &iid_of(lpFolder), MAPI_MODIFY, &ulType, &~lpFolder);
		if(hr != hrSuccess)
			return hr;

		// Move Message
		hr = lpFolder->CopyMessages(&sMsgList, &IID_IMAPIFolder, lpFolder, 0, NULL, MESSAGE_MOVE);
	}

	// Handle PR_DELETE_AFTER_SUBMIT
	if (lpPropValue[DSM_DELETE_AFTER_SUBMIT].ulPropTag != PR_DELETE_AFTER_SUBMIT ||
	    lpPropValue[DSM_DELETE_AFTER_SUBMIT].Value.b != TRUE)
		return hr;

	if(lpFolder == NULL)
	{
		// Open parent folder of the sent message
		hr = lpMDB->OpenEntry(lpPropValue[DSM_PARENT_ENTRYID].Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(lpPropValue[DSM_PARENT_ENTRYID].Value.bin.lpb),
		     &iid_of(lpFolder), MAPI_MODIFY, &ulType, &~lpFolder);
		if(hr != hrSuccess)
			return hr;
	}

	// Delete Message
	return lpFolder->DeleteMessages(&sMsgList, 0, nullptr, 0);
}

// This is a class that implements IMAPIProp's GetProps(), and nothing else. Its data
// is retrieved from the passed lpProps/cValues property array
class ECRowWrapper final : public IMAPIProp {
public:
	ECRowWrapper(const SPropValue *lpProps, ULONG cValues) : m_cValues(cValues), m_lpProps(lpProps) {}
	ULONG AddRef() override { return 1; } /* no ref counting */
	ULONG Release() override { return 1; }
	HRESULT QueryInterface(const IID &, void **) override { return MAPI_E_INTERFACE_NOT_SUPPORTED; }

	HRESULT GetLastError(HRESULT result, unsigned int flags, MAPIERROR **) override { return MAPI_E_NOT_FOUND; }
	HRESULT SaveChanges(unsigned int flags) override { return MAPI_E_NO_SUPPORT; }
	HRESULT GetProps(const SPropTagArray *lpTags, ULONG ulFlags, ULONG *lpcValues, SPropValue **lppProps) override
	{
		memory_ptr<SPropValue> lpProps;
		convert_context converter;

		auto hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpTags->cValues, &~lpProps);
		if (hr != hrSuccess)
			return hr;

		for (unsigned int i = 0; i < lpTags->cValues; ++i) {
			bool bError = false;
			auto lpFind = PCpropFindProp(m_lpProps, m_cValues, CHANGE_PROP_TYPE(lpTags->aulPropTag[i], PT_UNSPECIFIED));
			if (lpFind == nullptr || PROP_TYPE(lpFind->ulPropTag) == PT_ERROR) {
				bError = true;
			} else if (PROP_TYPE(lpFind->ulPropTag) == PT_STRING8 && PROP_TYPE(lpTags->aulPropTag[i]) == PT_UNICODE) {
				lpProps[i].ulPropTag = lpTags->aulPropTag[i];
				std::wstring wstrTmp = converter.convert_to<std::wstring>(lpFind->Value.lpszA);
				hr = MAPIAllocateMore((wstrTmp.length() + 1) * sizeof *lpProps[i].Value.lpszW, lpProps, reinterpret_cast<void **>(&lpProps[i].Value.lpszW));
				if (hr != hrSuccess)
					return hr;
				wcscpy(lpProps[i].Value.lpszW, wstrTmp.c_str());
			} else if (PROP_TYPE(lpFind->ulPropTag) ==  PT_UNICODE && PROP_TYPE(lpTags->aulPropTag[i]) == PT_STRING8) {
				lpProps[i].ulPropTag = lpTags->aulPropTag[i];
				std::string strTmp = converter.convert_to<std::string>(lpFind->Value.lpszW);
				hr = MAPIAllocateMore(strTmp.length() + 1, lpProps, reinterpret_cast<void **>(&lpProps[i].Value.lpszA));
				if (hr != hrSuccess)
					return hr;
				strcpy(lpProps[i].Value.lpszA, strTmp.c_str());
			} else if (PROP_TYPE(lpFind->ulPropTag) != PROP_TYPE(lpTags->aulPropTag[i])) {
				bError = TRUE;
			} else if (Util::HrCopyProperty(&lpProps[i], lpFind, lpProps) != hrSuccess) {
				bError = TRUE;
			}
			
			if(bError) {
				lpProps[i].ulPropTag = CHANGE_PROP_TYPE(lpTags->aulPropTag[i], PT_ERROR);
				lpProps[i].Value.err = MAPI_E_NOT_FOUND;

				hr = MAPI_W_ERRORS_RETURNED;
			}
		}

		*lppProps = lpProps.release();
		*lpcValues = lpTags->cValues;

		return hr;
	};
	HRESULT GetPropList(unsigned int flags, SPropTagArray **tags) override { return MAPI_E_NO_SUPPORT; }
	HRESULT OpenProperty(unsigned int tag, const IID *, unsigned int intf_opts, unsigned int flags, IUnknown **) override { return MAPI_E_NO_SUPPORT; }
	HRESULT SetProps(unsigned int nval, const SPropValue *props, SPropProblemArray **) override { return MAPI_E_NO_SUPPORT; }
	HRESULT DeleteProps(const SPropTagArray *, SPropProblemArray **) override { return MAPI_E_NO_SUPPORT; }
	HRESULT CopyTo(unsigned int nexcl, const IID *excliid, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, unsigned int flags, SPropProblemArray **) override { return MAPI_E_NO_SUPPORT; }
	HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest, unsigned int flags, SPropProblemArray **) override { return MAPI_E_NO_SUPPORT; }
	HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, unsigned int flags, unsigned int *nvals, MAPINAMEID ***names) override { return MAPI_E_NO_SUPPORT; }
	HRESULT GetIDsFromNames(unsigned int n, MAPINAMEID **propnames, unsigned int flags, SPropTagArray **proptags) override { return MAPI_E_NO_SUPPORT; }
private:
	ULONG			m_cValues;
	const SPropValue *m_lpProps;
};

static HRESULT TestRelop(ULONG relop, int result, bool* fMatch)
{
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
		return MAPI_E_TOO_COMPLEX;
	};
	return hrSuccess;
}

#define RESTRICT_MAX_RECURSE_LEVEL 16
static HRESULT GetRestrictTagsRecursive(const SRestriction *lpRestriction,
    std::list<unsigned int> *lpList, ULONG ulLevel)
{
	if(ulLevel > RESTRICT_MAX_RECURSE_LEVEL)
		return MAPI_E_TOO_COMPLEX;

	switch(lpRestriction->rt) {
	case RES_AND:
		for (unsigned int i = 0; i < lpRestriction->res.resAnd.cRes; ++i) {
			auto hr = GetRestrictTagsRecursive(&lpRestriction->res.resAnd.lpRes[i], lpList, ulLevel + 1);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	case RES_OR:
		for (unsigned int i = 0; i < lpRestriction->res.resOr.cRes; ++i) {
			auto hr = GetRestrictTagsRecursive(&lpRestriction->res.resOr.lpRes[i], lpList, ulLevel + 1);
			if (hr != hrSuccess)
				return hr;
		}
		break;
	case RES_NOT:
		return GetRestrictTagsRecursive(lpRestriction->res.resNot.lpRes, lpList, ulLevel+1);
	case RES_CONTENT:
		lpList->emplace_back(lpRestriction->res.resContent.ulPropTag);
		lpList->emplace_back(lpRestriction->res.resContent.lpProp->ulPropTag);
		break;
	case RES_PROPERTY:
		lpList->emplace_back(lpRestriction->res.resProperty.ulPropTag);
		lpList->emplace_back(lpRestriction->res.resProperty.lpProp->ulPropTag);
		break;
	case RES_COMPAREPROPS:
		lpList->emplace_back(lpRestriction->res.resCompareProps.ulPropTag1);
		lpList->emplace_back(lpRestriction->res.resCompareProps.ulPropTag2);
		break;
	case RES_BITMASK:
		lpList->emplace_back(lpRestriction->res.resBitMask.ulPropTag);
		break;
	case RES_SIZE:
		lpList->emplace_back(lpRestriction->res.resSize.ulPropTag);
		break;
	case RES_EXIST:
		lpList->emplace_back(lpRestriction->res.resExist.ulPropTag);
		break;
	case RES_SUBRESTRICTION:
		lpList->emplace_back(lpRestriction->res.resSub.ulSubObject);
	case RES_COMMENT:
		return GetRestrictTagsRecursive(lpRestriction->res.resComment.lpRes, lpList, ulLevel+1);
	}
	return hrSuccess;
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

HRESULT TestRestriction(const SRestriction *lpCondition, ULONG cValues,
    const SPropValue *lpPropVals, const ECLocale &locale, ULONG ulLevel)
{
	ECRowWrapper lpRowWrapper(lpPropVals, cValues);
	return TestRestriction(lpCondition, static_cast<IMAPIProp *>(&lpRowWrapper), locale, ulLevel);
}

HRESULT TestRestriction(const SRestriction *lpCondition, IMAPIProp *lpMessage,
    const ECLocale &locale, ULONG ulLevel)
{
	HRESULT hr = hrSuccess;
	bool fMatch = false;
	memory_ptr<SPropValue> lpProp, lpProp2;
	int result;
	unsigned int ulSize;
	object_ptr<IMAPITable> lpTable;
	memory_ptr<SPropTagArray> lpTags;

	if (ulLevel > RESTRICT_MAX_RECURSE_LEVEL)
		return MAPI_E_TOO_COMPLEX;

	if (!lpCondition)
		return MAPI_E_INVALID_PARAMETER;

	switch (lpCondition->rt) {
	// loops
	case RES_AND:
		for (unsigned int c = 0; c < lpCondition->res.resAnd.cRes; ++c) {
			hr = TestRestriction(&lpCondition->res.resAnd.lpRes[c], lpMessage, locale, ulLevel+1);
			if (hr != hrSuccess) {
				fMatch = false;
				break;
			}
			fMatch = true;
		}
		break;
	case RES_OR:
		for (unsigned int c = 0; c < lpCondition->res.resAnd.cRes; ++c) {
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
			fMatch = hr != hrSuccess;
			hr = fMatch ? hrSuccess : MAPI_E_NOT_FOUND;
		}
		break;

	// Prop compares
	case RES_CONTENT: {
		// @todo: support PT_MV_STRING8, PT_MV_UNICODE and PT_MV_BINARY
		// fuzzy string compare
		if (PROP_TYPE(lpCondition->res.resContent.ulPropTag) != PT_STRING8 &&
			PROP_TYPE(lpCondition->res.resContent.ulPropTag) != PT_UNICODE &&
			PROP_TYPE(lpCondition->res.resContent.ulPropTag) != PT_BINARY) {
			hr = MAPI_E_TOO_COMPLEX;
			break;
		}
		unsigned int ulPropType = PROP_TYPE(lpCondition->res.resContent.ulPropTag);
		hr = HrGetOneProp(lpMessage, lpCondition->res.resContent.ulPropTag, &~lpProp);
		if (hr != hrSuccess)
			break;

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
			if (ulSearchDataSize != ulSearchStringSize)
				break;
			if ((ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) && lpSearchData != NULL && lpSearchString != NULL && str_iequals(lpSearchData, lpSearchString, locale)) ||
			    (ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpSearchData != NULL && lpSearchString != NULL && str_equals(lpSearchData, lpSearchString, locale)) ||
			    (ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) && lpwSearchData != NULL && lpwSearchString != NULL && wcs_iequals(lpwSearchData, lpwSearchString, locale)) ||
			    (ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpwSearchData != NULL && lpwSearchString != NULL && wcs_equals(lpwSearchData, lpwSearchString, locale)) ||
			    (ulPropType == PT_BINARY && lpSearchData != NULL && lpSearchString != NULL && memcmp(lpSearchData, lpSearchString, ulSearchDataSize) == 0))
				fMatch = true;
			break;
		case FL_PREFIX:
			if (ulSearchDataSize < ulSearchStringSize)
				break;
			if ((ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) && lpSearchData != NULL && lpSearchString != NULL && str_istartswith(lpSearchData, lpSearchString, locale)) ||
			    (ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpSearchData != NULL && lpSearchString != NULL && str_startswith(lpSearchData, lpSearchString, locale)) ||
			    (ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) && lpwSearchData != NULL && lpwSearchString != NULL && wcs_istartswith(lpwSearchData, lpwSearchString, locale)) ||
			    (ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpwSearchData != NULL && lpwSearchString != NULL && wcs_startswith(lpwSearchData, lpwSearchString, locale)) ||
			    (ulPropType == PT_BINARY && lpSearchData != NULL && lpSearchString != NULL && memcmp(lpSearchData, lpSearchString, ulSearchDataSize) == 0))
				fMatch = true;
			break;
		case FL_SUBSTRING:
			if ((ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) && lpSearchData != NULL && lpSearchString != NULL && str_icontains(lpSearchData, lpSearchString, locale)) ||
			    (ulPropType == PT_STRING8 && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpSearchData != NULL && lpSearchString != NULL && str_contains(lpSearchData, lpSearchString, locale)) ||
			    (ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) && lpwSearchData != NULL && lpwSearchString != NULL && wcs_icontains(lpwSearchData, lpwSearchString, locale)) ||
			    (ulPropType == PT_UNICODE && (ulFuzzyLevel & FL_IGNORECASE) == 0 && lpwSearchData != NULL && lpwSearchString != NULL && wcs_contains(lpwSearchData, lpwSearchString, locale)) ||
			    (ulPropType == PT_BINARY && lpSearchData != NULL && lpSearchString != NULL && memsubstr(lpSearchData, ulSearchDataSize, lpSearchString, ulSearchStringSize) == 0))
				fMatch = true;
			break;
		}
		break;
	}
	case RES_PROPERTY:
		if(PROP_TYPE(lpCondition->res.resProperty.ulPropTag) != PROP_TYPE(lpCondition->res.resProperty.lpProp->ulPropTag)) {
			// cannot compare two different types
			hr = MAPI_E_TOO_COMPLEX;
			break;
		}
		hr = HrGetOneProp(lpMessage, lpCondition->res.resProperty.ulPropTag, &~lpProp);
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
		hr = HrGetOneProp(lpMessage, lpCondition->res.resCompareProps.ulPropTag1, &~lpProp);
		if (hr != hrSuccess)
			break;
		hr = HrGetOneProp(lpMessage, lpCondition->res.resCompareProps.ulPropTag2, &~lpProp2);
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
		hr = HrGetOneProp(lpMessage, lpCondition->res.resBitMask.ulPropTag, &~lpProp);
		if (hr != hrSuccess)
			break;
		fMatch = (lpProp->Value.ul & lpCondition->res.resBitMask.ulMask) == 0;
		if (lpCondition->res.resBitMask.relBMR == BMR_NEZ)
			fMatch = !fMatch;
		break;
	case RES_SIZE:
		hr = HrGetOneProp(lpMessage, lpCondition->res.resSize.ulPropTag, &~lpProp);
		if (hr != hrSuccess)
			break;
		ulSize = Util::PropSize(lpProp);
		result = ulSize - lpCondition->res.resSize.cb;
		hr = TestRelop(lpCondition->res.resSize.relop, result, &fMatch);
		break;
	case RES_EXIST:
		hr = HrGetOneProp(lpMessage, lpCondition->res.resExist.ulPropTag, &~lpProp);
		if (hr != hrSuccess)
			break;
		fMatch = true;
		break;
	case RES_SUBRESTRICTION:
		// A subrestriction is basically an OR restriction over all the rows in a specific
		// table. We currently support the attachment table (PR_MESSAGE_ATTACHMENTS) and the 
		// recipient table (PR_MESSAGE_RECIPIENTS) here.
		hr = lpMessage->OpenProperty(lpCondition->res.resSub.ulSubObject, &IID_IMAPITable, 0, 0, &~lpTable);
		if(hr != hrSuccess) {
			hr = MAPI_E_TOO_COMPLEX;
			goto exit;
		}
		// Get a list of properties we may be needing
		hr = GetRestrictTags(lpCondition->res.resSub.lpRes, &~ lpTags);
		if(hr != hrSuccess)
			goto exit;

		hr = lpTable->SetColumns(lpTags, 0);
		if(hr != hrSuccess)
			goto exit;

		while(1) {
			rowset_ptr lpRowSet;
			hr = lpTable->QueryRows(1, 0, &~lpRowSet);
			if(hr != hrSuccess)
				goto exit;

			if(lpRowSet->cRows != 1)
				break;

			// Wrap the row into an IMAPIProp compatible object so we can recursively call
			// this function (which obviously itself doesn't support RES_SUBRESTRICTION as 
			// there aren't any subobjects under the subobjects .. unless we count
			// messages in PR_ATTACH_DATA_OBJ under attachments... Well we don't support
			// that in any case ...)
			hr = TestRestriction(lpCondition->res.resSub.lpRes, lpRowSet[0].cValues, lpRowSet[0].lpProps, locale, ulLevel+1);
			if(hr == hrSuccess) {
				fMatch = true;
				break;
			}
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
	if (fMatch)
		return hrSuccess;
	else if (hr == hrSuccess)
		return MAPI_E_NOT_FOUND;
	return hr;
}

HRESULT GetClientVersion(unsigned int* ulVersion)
{
	*ulVersion = CLIENT_VERSION_LATEST;
	return hrSuccess;
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
static HRESULT FindFolder(IMAPITable *lpTable, const wchar_t *folder,
    SPropValue **lppFolderProp)
{
	ULONG nValues;
	static constexpr const SizedSPropTagArray(2, sptaName) =
		{2, {PR_DISPLAY_NAME_W, PR_ENTRYID}};
	auto hr = lpTable->SetColumns(sptaName, 0);
	if (hr != hrSuccess)
		return hr;

	while (TRUE) {
		rowset_ptr lpRowSet;
		hr = lpTable->QueryRows(1, 0, &~lpRowSet);
		if (hr != hrSuccess)
			break;
		if (lpRowSet->cRows == 0)
			return MAPI_E_NOT_FOUND;
		if (wcscasecmp(lpRowSet[0].lpProps[0].Value.lpszW, folder) == 0)
			// found the folder
			return Util::HrCopyPropertyArray(&lpRowSet[0].lpProps[1], 1, lppFolderProp, &nValues);
	}
	return hr;
}

/**
 * Opens any subfolder from the name at any depth. The folder
 * separator character is also passed. You can also open the IPM
 * subtree not by passing the foldername.
 *
 * @param[in]	lpMDB	A store to open the folder in. If you pass the public store, set the matching bool to true.
 * @param[in]	folder	The name of the folder you want to open. Can be at any depth, e.g. INBOX/folder name1/folder name2. Pass / as separator.
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
	memory_ptr<SPropValue> lpPropIPMSubtree, lpPropFolder;
	ULONG			ulObjType;
	object_ptr<IMAPIFolder> lpFoundFolder;
	LPMAPIFOLDER	lpNewFolder = NULL;
	const WCHAR*	ptr = NULL;

	if(bIsPublic)
	{
		auto hr = HrGetOneProp(lpMDB, PR_IPM_PUBLIC_FOLDERS_ENTRYID, &~lpPropIPMSubtree);
		if (hr != hrSuccess)
			return kc_perror("Unable to find PR_IPM_PUBLIC_FOLDERS_ENTRYID object", hr);
	}
	else
	{
		auto hr = HrGetOneProp(lpMDB, PR_IPM_SUBTREE_ENTRYID, &~lpPropIPMSubtree);
		if (hr != hrSuccess)
			return kc_perror("Unable to find IPM_SUBTREE object", hr);
	}

	auto hr = lpMDB->OpenEntry(lpPropIPMSubtree->Value.bin.cb,
	          reinterpret_cast<ENTRYID *>(lpPropIPMSubtree->Value.bin.lpb),
	          &IID_IMAPIFolder, 0, &ulObjType, &~lpFoundFolder);
	if (hr != hrSuccess || ulObjType != MAPI_FOLDER)
		return kc_perror("Unable to open IPM_SUBTREE object", hr);
	// correctly return IPM subtree as found folder
	if (!folder)
		goto found;

	// Loop through the folder string to find the wanted folder in the store
	do {
		object_ptr<IMAPITable> lpTable;
		std::wstring subfld;

		ptr = wcschr(folder, psep);
		if (ptr)
			subfld.assign(folder, ptr - folder);
		else
			subfld = folder;
		folder = ptr ? ptr+1 : NULL;
		hr = lpFoundFolder->GetHierarchyTable(0, &~lpTable);
		if (hr != hrSuccess)
			return kc_perror("Unable to view folder", hr);
		hr = FindFolder(lpTable, subfld.c_str(), &~lpPropFolder);
		if (hr == MAPI_E_NOT_FOUND && bCreateFolder) {
			hr = lpFoundFolder->CreateFolder(FOLDER_GENERIC, (LPTSTR)subfld.c_str(), (LPTSTR)L"Auto-created by Kopano", &IID_IMAPIFolder, MAPI_UNICODE | OPEN_IF_EXISTS, &lpNewFolder);
			if (hr != hrSuccess) {
				ec_log_err("Unable to create folder \"%ls\": %s (%x)",
					subfld.c_str(), GetMAPIErrorMessage(hr), hr);
				return hr;
			}
		} else if (hr != hrSuccess)
			return hr;

		if (lpNewFolder) {
			lpFoundFolder.reset(lpNewFolder, false);
			lpNewFolder = NULL;
		} else {
			hr = lpMDB->OpenEntry(lpPropFolder->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpPropFolder->Value.bin.lpb),
			     &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpFoundFolder);
			if (hr != hrSuccess) {
				ec_log_err("Unable to open folder \"%ls\": %s (%x)",
					subfld.c_str(), GetMAPIErrorMessage(hr), hr);
				return hr;
			}
		}
	} while (ptr);

found:
	if (lpFoundFolder) {
		lpFoundFolder->AddRef();
		*lppSubFolder = lpFoundFolder;
	}
	return hr;
}

HRESULT HrOpenUserMsgStore(IMAPISession *lpSession, const wchar_t *lpszUser,
    IMsgStore **lppStore)
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
static HRESULT HrOpenUserMsgStore(IMAPISession *lpSession, IMsgStore *lpStore,
    const wchar_t *lpszUser, IMsgStore **lppStore)
{
	object_ptr<IMsgStore> lpDefaultStore, lpMsgStore;
	object_ptr<IExchangeManageStore> lpExchManageStore;
	ULONG					cbStoreEntryID = 0;
	memory_ptr<ENTRYID> lpStoreEntryID;

	if (lpStore == NULL) {
		auto hr = HrOpenDefaultStore(lpSession, &~lpDefaultStore);
		if (hr != hrSuccess)
			return hr;
		lpStore = lpDefaultStore;
	}

	// Find and open the store for lpszUser.
	auto hr = lpStore->QueryInterface(IID_IExchangeManageStore, &~lpExchManageStore);
	if (hr != hrSuccess)
		return hr;
	hr = lpExchManageStore->CreateStoreEntryID(nullptr, reinterpret_cast<const TCHAR *>(lpszUser), MAPI_UNICODE, &cbStoreEntryID, &~lpStoreEntryID);
	if (hr != hrSuccess)
		return hr;
	hr = lpSession->OpenMsgStore(0, cbStoreEntryID, lpStoreEntryID, &IID_IMsgStore, MDB_WRITE, &~lpMsgStore);
	if (hr != hrSuccess)
		return hr;
	return lpMsgStore->QueryInterface(IID_IMsgStore, reinterpret_cast<void **>(lppStore));
}

/* NAMED PROPERTY util functions (used with PROPMAP_* macros) */

ECPropMapEntry::ECPropMapEntry(GUID guid, ULONG ulId) :
	m_sGuid(guid)
{
    m_sMAPINameId.ulKind = MNID_ID; 
    m_sMAPINameId.lpguid = &m_sGuid; 
    m_sMAPINameId.Kind.lID = ulId; 
}
    
ECPropMapEntry::ECPropMapEntry(GUID guid, const char *strId) :
	m_sGuid(guid)
{
    m_sMAPINameId.ulKind = MNID_STRING; 
    m_sMAPINameId.lpguid = &m_sGuid; 
    m_sMAPINameId.Kind.lpwstrName = new WCHAR[strlen(strId)+1];
    mbstowcs(m_sMAPINameId.Kind.lpwstrName, strId, strlen(strId)+1);
}
    
ECPropMapEntry::ECPropMapEntry(const ECPropMapEntry &other) :
	m_sGuid(other.m_sGuid)
{
    m_sMAPINameId.ulKind = other.m_sMAPINameId.ulKind;
    m_sGuid = other.m_sGuid;
    m_sMAPINameId.lpguid = &m_sGuid;
    if(other.m_sMAPINameId.ulKind == MNID_ID) {
        m_sMAPINameId.Kind.lID = other.m_sMAPINameId.Kind.lID;
		return;
	}
        m_sMAPINameId.Kind.lpwstrName = new WCHAR[wcslen( other.m_sMAPINameId.Kind.lpwstrName )+1];
        wcscpy(m_sMAPINameId.Kind.lpwstrName, other.m_sMAPINameId.Kind.lpwstrName);
}

ECPropMapEntry::ECPropMapEntry(ECPropMapEntry &&other) :
	m_sGuid(other.m_sGuid)
{
	m_sMAPINameId.ulKind = other.m_sMAPINameId.ulKind;
	m_sMAPINameId.lpguid = &m_sGuid;
	if (other.m_sMAPINameId.ulKind == MNID_ID) {
		m_sMAPINameId.Kind.lID = other.m_sMAPINameId.Kind.lID;
		return;
	}
	m_sMAPINameId.Kind.lpwstrName = other.m_sMAPINameId.Kind.lpwstrName;
	other.m_sMAPINameId.Kind.lpwstrName = nullptr;
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
	lstNames.emplace_back(entry);
	lstVars.emplace_back(lpId);
	lstTypes.emplace_back(ulType);
}
    
HRESULT ECPropMap::Resolve(IMAPIProp *lpMAPIProp) {
    int n = 0;
	memory_ptr<SPropTagArray> lpPropTags;

	if (lpMAPIProp == nullptr)
		return MAPI_E_INVALID_PARAMETER;
    
    // Do GetIDsFromNames() and store result in correct places
	auto lppNames = make_unique_nt<MAPINAMEID *[]>(lstNames.size());
	if (lppNames == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	for (auto &mapent : lstNames)
		lppNames[n++] = mapent.GetMAPINameId();
	auto hr = lpMAPIProp->GetIDsFromNames(n, lppNames.get(), MAPI_CREATE, &~lpPropTags);
    if(hr != hrSuccess)
		return hr;
    
    n = 0;
	auto k = lstTypes.begin();
	for (auto j = lstVars.begin(); j != lstVars.end(); ++j, ++k)
		*(*j) = CHANGE_PROP_TYPE(lpPropTags->aulPropTag[n++], *k);
	return hrSuccess;
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
	memory_ptr<SPropValue> lpPropDefFld;
	object_ptr<IMAPIFolder> lpRootFld, lpDefaultFolder;
	ULONG ulType = 0;
	
	//open Root Container.
	auto hr = lpMsgStore->OpenEntry(0, nullptr, &iid_of(lpRootFld), 0, &ulType, &~lpRootFld);
	if (hr != hrSuccess || ulType != MAPI_FOLDER) 
		return kc_perror("Unable to open root container", hr);
	//retrive Entryid of Default Calendar Folder.
	hr = HrGetOneProp(lpRootFld, PR_IPM_APPOINTMENT_ENTRYID, &~lpPropDefFld);
	if (hr != hrSuccess) 
		return kc_perror("Unable to find PR_IPM_APPOINTMENT_ENTRYID", hr);
	hr = lpMsgStore->OpenEntry(lpPropDefFld->Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(lpPropDefFld->Value.bin.lpb),
	     &iid_of(lpDefaultFolder), MAPI_MODIFY, &ulType, &~lpDefaultFolder);
	if (hr != hrSuccess || ulType != MAPI_FOLDER) 
		return kc_perror("Unable to open IPM_SUBTREE object", hr);
	*lppFolder = lpDefaultFolder.release();
	return hrSuccess;
}

/**
 * Update an SPropValue array in-place with large properties.
 *
 * (To be called after IMAPIProp::GetProps.)
 */
HRESULT spv_postload_large_props(IMAPIProp *lpProp,
    const SPropTagArray *lpTags, unsigned int cValues, SPropValue *lpProps)
{
	HRESULT hr = hrSuccess;
	StreamPtr lpStream;	
	void *lpData = NULL;
	bool had_err = false;
	
	for (unsigned int i = 0; i < cValues; ++i) {
		if (PROP_TYPE(lpProps[i].ulPropTag) != PT_ERROR)
			continue;
		if (lpProps[i].Value.err != MAPI_E_NOT_ENOUGH_MEMORY) {
			had_err = true;
			continue;
		}
		if (PROP_TYPE(lpTags->aulPropTag[i]) != PT_STRING8 && PROP_TYPE(lpTags->aulPropTag[i]) != PT_UNICODE && PROP_TYPE(lpTags->aulPropTag[i]) != PT_BINARY)
			continue;
		if (lpProp->OpenProperty(lpTags->aulPropTag[i], &IID_IStream, 0, 0, &~lpStream) != hrSuccess)
			continue;
				
		std::string strData;
		if (Util::HrStreamToString(lpStream.get(), strData) != hrSuccess)
			continue;
		if ((hr = MAPIAllocateMore(strData.size() + sizeof(WCHAR), lpProps, (void **)&lpData)) != hrSuccess)
			return hr;
		memcpy(lpData, strData.data(), strData.size());
		lpProps[i].ulPropTag = lpTags->aulPropTag[i];
		switch (PROP_TYPE(lpTags->aulPropTag[i])) {
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
	return had_err ? MAPI_W_ERRORS_RETURNED : hrSuccess;
}
	
/**
 * Gets all properties for passed object
 *
 * This includes properties that are normally returned from GetProps() as MAPI_E_NOT_ENOUGH_MEMORY. The
 * rest of the semantics of this call are equal to those of calling IMAPIProp::GetProps() with NULL as the
 * property tag array.
 *
 * @prop:	IMAPIProp object to get properties from
 * @flags:	MAPI_UNICODE or 0
 * @lpcValues:	Number of properties saved in @lppProps
 * @lppProps:	Output properties
 */
HRESULT HrGetAllProps(IMAPIProp *prop, unsigned int flags,
    unsigned int *lpcValues, SPropValue **lppProps)
{
	memory_ptr<SPropTagArray> tags;
	memory_ptr<SPropValue> lpProps;
	unsigned int cValues = 0;
	auto ret = prop->GetPropList(flags, &~tags);
	if (ret != hrSuccess)
		return ret;
	ret = prop->GetProps(tags, flags, &cValues, &~lpProps);
	if (FAILED(ret))
		return ret;
	ret = spv_postload_large_props(prop, tags, cValues, lpProps);
	if (FAILED(ret))
		return ret;
	*lppProps = lpProps.release();
	*lpcValues = cValues;
	return ret;
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
HRESULT UnWrapStoreEntryID(ULONG cbOrigEntry, const ENTRYID *lpOrigEntry,
    ULONG *lpcbUnWrappedEntry, ENTRYID **lppUnWrappedEntry)
{
	memory_ptr<ENTRYID> lpEntryID;

	if (lpOrigEntry == nullptr || lpcbUnWrappedEntry == nullptr ||
	    lppUnWrappedEntry == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	
	// Check if this a wrapped store entryid
	if (cbOrigEntry < (4 + sizeof(GUID) + 3) ||
	    memcmp(lpOrigEntry->ab, &muidStoreWrap, sizeof(GUID)) != 0)
		return MAPI_E_INVALID_ENTRYID;

	unsigned int cbRemove = 4; // Flags
	cbRemove+= sizeof(GUID); //Wrapped identifier
	cbRemove+= 2; // Unknown, Unicode flag?

	// Dllname size
	auto cbDLLName = strlen(reinterpret_cast<const char *>(lpOrigEntry) + cbRemove) + 1;
	cbRemove+= cbDLLName;

	cbRemove += (4 - (cbRemove & 0x03)) & 0x03;; // padding to 4byte block
	if (cbOrigEntry <= cbRemove)
		return MAPI_E_INVALID_ENTRYID;

	// Create Unwrap entryid
	auto hr = MAPIAllocateBuffer(cbOrigEntry - cbRemove, &~lpEntryID);
	if (hr != hrSuccess)
		return hr;
	memcpy(lpEntryID, ((LPBYTE)lpOrigEntry)+cbRemove, cbOrigEntry - cbRemove);

	*lpcbUnWrappedEntry = cbOrigEntry - cbRemove;
	*lppUnWrappedEntry = lpEntryID.release();
	return hrSuccess;
}

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
	object_ptr<IMessage> lpMessage;
	SPropValue sPropValMessage[6] = {0};

	if (lpFolder == nullptr || lppMessage == nullptr || (ulFlags & ~MAPI_ASSOCIATED) != 0)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpFolder->CreateMessage(&IID_IMessage, ulFlags & MAPI_ASSOCIATED, &~lpMessage);
	if(hr != hrSuccess)
		return hr;

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
		return hr;
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		return hr;
	return lpMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppMessage));
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
HRESULT OpenLocalFBMessage(DGMessageType eDGMsgType,
    IMsgStore *lpMsgStore, bool bCreateIfMissing, IMessage **lppFBMessage)
{
	object_ptr<IMAPIFolder> lpRoot, lpInbox;
	IMAPIFolder *lpFolder = NULL;
	IMessage *lpMessage = NULL;
	memory_ptr<SPropValue> lpPropFB, lpPropFBNew, lpEntryID, lpAppEntryID;
	SPropValue *lpPVFBFolder = nullptr, *lpPropFBRef = nullptr;
	ULONG ulType = 0, cbEntryIDInbox = 0;
	memory_ptr<ENTRYID> lpEntryIDInbox;
	memory_ptr<TCHAR> lpszExplicitClass;

	auto hr = lpMsgStore->OpenEntry(0, nullptr, &IID_IMAPIFolder, MAPI_MODIFY, &ulType, &~lpRoot);
	if(hr != hrSuccess)
		return hr;

	// Check if the freebusydata folder and LocalFreeBusy is exist. Create the folder and message if it is request.
	if((HrGetOneProp(lpRoot, PR_FREEBUSY_ENTRYIDS, &~lpPropFB) != hrSuccess ||
		lpPropFB->Value.MVbin.cValues < 2 ||
		lpPropFB->Value.MVbin.lpbin[eDGMsgType].lpb == NULL ||
		lpMsgStore->OpenEntry(lpPropFB->Value.MVbin.lpbin[eDGMsgType].cb, (LPENTRYID)lpPropFB->Value.MVbin.lpbin[eDGMsgType].lpb, &IID_IMessage, MAPI_MODIFY, &ulType, (IUnknown **) &lpMessage) != hrSuccess)
	   && bCreateIfMissing) {
		// Open the inbox
		hr = lpMsgStore->GetReceiveFolder(reinterpret_cast<const TCHAR *>(""), 0, &cbEntryIDInbox, &~lpEntryIDInbox, &~lpszExplicitClass);
		if(hr != hrSuccess)
			return hr;
		hr = lpMsgStore->OpenEntry(cbEntryIDInbox, lpEntryIDInbox, &IID_IMAPIFolder, MAPI_MODIFY, &ulType, &~lpInbox);
		if(hr != hrSuccess)
			return hr;

		if (eDGMsgType == dgFreebusydata) {
			// Create freebusydata Folder
			hr = lpRoot->CreateFolder(FOLDER_GENERIC, (LPTSTR)"Freebusy Data", (LPTSTR)"", &IID_IMAPIFolder, OPEN_IF_EXISTS, &lpFolder);
			if(hr != hrSuccess)
				return hr;

			// Get entryid of freebusydata
			hr = HrGetOneProp(lpFolder, PR_ENTRYID, &lpPVFBFolder);
			if(hr != hrSuccess)
				return hr;
		} else if (eDGMsgType == dgAssociated) {
			//Open default calendar
			hr = HrGetOneProp(lpInbox, PR_IPM_APPOINTMENT_ENTRYID, &~lpAppEntryID);
			if(hr != hrSuccess)
				return hr;
			hr = lpMsgStore->OpenEntry(lpAppEntryID->Value.bin.cb, (LPENTRYID)lpAppEntryID->Value.bin.lpb, &IID_IMAPIFolder, MAPI_MODIFY, &ulType, (IUnknown **) &lpFolder);
			if(hr != hrSuccess)
				return hr;
		}

		hr = CreateLocalFreeBusyMessage(lpFolder, (eDGMsgType == dgAssociated)?MAPI_ASSOCIATED : 0, &lpMessage);
		if(hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(lpMessage, PR_ENTRYID, &~lpEntryID);
		if(hr != hrSuccess)
			return hr;

		// Update Free/Busy entryid
		if(lpPropFB == NULL || lpPropFB->Value.MVbin.cValues < 2) {
			hr = MAPIAllocateBuffer(sizeof(SPropValue), &~lpPropFBNew);
			if(hr != hrSuccess)
				return hr;
			lpPropFBNew->ulPropTag = PR_FREEBUSY_ENTRYIDS;
			hr = MAPIAllocateMore(sizeof(SBinary) * 4, lpPropFB, (void **)&lpPropFBNew->Value.MVbin.lpbin);
			if(hr != hrSuccess)
				return hr;

			memset(lpPropFBNew->Value.MVbin.lpbin, 0, sizeof(SBinary) * 4);

			if (eDGMsgType == dgFreebusydata) {
				if(lpPropFB && lpPropFB->Value.MVbin.cValues > 0) {
					lpPropFBNew->Value.MVbin.lpbin[0] = lpPropFB->Value.MVbin.lpbin[0];

					if (lpPropFB->Value.MVbin.cValues > 2)
						lpPropFBNew->Value.MVbin.lpbin[2] = lpPropFB->Value.MVbin.lpbin[2];
				}

				lpPropFBNew->Value.MVbin.lpbin[1] = lpEntryID->Value.bin;
				lpPropFBNew->Value.MVbin.lpbin[3] = lpPVFBFolder->Value.bin;
			} else if(eDGMsgType == dgAssociated) {
				lpPropFBNew->Value.MVbin.lpbin[0] = lpEntryID->Value.bin;
			}

			lpPropFBNew->Value.MVbin.cValues = 4; // no problem if the data is NULL

			lpPropFBRef = lpPropFBNew; // use this one later on
		} else {
			lpPropFB->Value.MVbin.lpbin[eDGMsgType] = lpEntryID->Value.bin;
			lpPropFBRef = lpPropFB; // use this one later on
		}

		// Put the MV property in the root folder
		hr = lpRoot->SetProps(1, lpPropFBRef, NULL);
		if(hr != hrSuccess)
			return hr;

		// Put the MV property in the inbox folder
		hr = lpInbox->SetProps(1, lpPropFBRef, NULL);
		if(hr != hrSuccess)
			return hr;
	}

	if (lpMessage == nullptr)
		return MAPI_E_NOT_FOUND;

	// We now have a message lpMessage which is the LocalFreeBusy message.
	*lppFBMessage = lpMessage;
	return hrSuccess;
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
	object_ptr<IMessage> lpLocalFBMessage;
	SPropValue FBProps[6];

	// Meaning of these values are unknown, but are always TRUE in cases seen until now
	FBProps[0].ulPropTag = PR_SCHDINFO_BOSS_WANTS_COPY;
	FBProps[0].Value.b = TRUE;
	FBProps[1].ulPropTag = PR_SCHDINFO_DONT_MAIL_DELEGATES;
	FBProps[1].Value.b = TRUE;
	FBProps[2].ulPropTag = PR_SCHDINFO_BOSS_WANTS_INFO;
	FBProps[2].Value.b = TRUE;

	FBProps[3].ulPropTag = PR_PROCESS_MEETING_REQUESTS;
	FBProps[3].Value.b = bAutoAccept;
	FBProps[4].ulPropTag = PR_DECLINE_CONFLICTING_MEETING_REQUESTS;
	FBProps[4].Value.b = bDeclineConflict;
	FBProps[5].ulPropTag = PR_DECLINE_RECURRING_MEETING_REQUESTS;
	FBProps[5].Value.b = bDeclineRecurring;

	// Save localfreebusy settings
	auto hr = OpenLocalFBMessage(dgFreebusydata, lpMsgStore, true, &~lpLocalFBMessage);
	if(hr != hrSuccess)
		return hr;
	hr = lpLocalFBMessage->SetProps(6, FBProps, NULL);
	if(hr != hrSuccess)
		return hr;
	hr = lpLocalFBMessage->SaveChanges(0);
	if(hr != hrSuccess)
		return hr;

	// Hack to support outlook 2000/2002 with resources
	hr = OpenLocalFBMessage(dgAssociated, lpMsgStore, true, &~lpLocalFBMessage);
	if(hr != hrSuccess)
		return hr;
	hr = lpLocalFBMessage->SetProps(6, FBProps, NULL);
	if(hr != hrSuccess)
		return hr;
	return lpLocalFBMessage->SaveChanges(0);
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
HRESULT GetAutoAcceptSettings(IMsgStore *lpMsgStore, bool *lpbAutoAccept, bool *lpbDeclineConflict, bool *lpbDeclineRecurring, bool *autoprocess_ptr)
{
	object_ptr<IMessage> lpLocalFBMessage;
	memory_ptr<SPropValue> lpProps;
	ULONG cValues = 0;
	bool bAutoAccept = false, bDeclineConflict = false;
	bool bDeclineRecurring = false, autoprocess = true;

	auto hr = OpenLocalFBMessage(dgFreebusydata, lpMsgStore, false, &~lpLocalFBMessage);
	if(hr == hrSuccess) {
		MAPINAMEID name, *namep = &name;
		memory_ptr<SPropTagArray> proptagarr;
		name.lpguid = const_cast<GUID *>(&PSETID_KC);
		name.ulKind = MNID_ID;
		name.Kind.lID = dispidAutoProcess;
		hr = lpLocalFBMessage->GetIDsFromNames(1, &namep, MAPI_CREATE, &~proptagarr);
		if (FAILED(hr))
			return hr;

		auto proptag = CHANGE_PROP_TYPE(proptagarr->aulPropTag[0], PT_BOOLEAN);
		static const SizedSPropTagArray(4, sptaFBProps) =
			{4, {PR_PROCESS_MEETING_REQUESTS,
			PR_DECLINE_CONFLICTING_MEETING_REQUESTS,
			PR_DECLINE_RECURRING_MEETING_REQUESTS, proptag}};

		hr = lpLocalFBMessage->GetProps(sptaFBProps, 0, &cValues, &~lpProps);
		if(FAILED(hr))
			return hr;
		if(lpProps[0].ulPropTag == PR_PROCESS_MEETING_REQUESTS)
			bAutoAccept = lpProps[0].Value.b;
		if(lpProps[1].ulPropTag == PR_DECLINE_CONFLICTING_MEETING_REQUESTS)
			bDeclineConflict = lpProps[1].Value.b;
		if(lpProps[2].ulPropTag == PR_DECLINE_RECURRING_MEETING_REQUESTS)
			bDeclineRecurring = lpProps[2].Value.b;
		if(lpProps[3].ulPropTag == proptag)
			autoprocess = lpProps[3].Value.b;
	}
	// else, hr != hrSuccess: no FB -> all settings are FALSE
	if (lpbAutoAccept != nullptr)
		*lpbAutoAccept = bAutoAccept;
	if (lpbDeclineConflict != nullptr)
		*lpbDeclineConflict = bDeclineConflict;
	if (lpbDeclineRecurring != nullptr)
		*lpbDeclineRecurring = bDeclineRecurring;
	if (autoprocess_ptr != nullptr)
		*autoprocess_ptr = autoprocess;
	return hrSuccess;
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
	HRESULT hr = lpMsgStore->QueryInterface(iid_of(ptrEMS), &~ptrEMS);
	if (hr != hrSuccess)
		return hr;
	if (ulFlags & MAPI_UNICODE) {
		std::wstring strMsgStoreDN = L"cn="s + (LPCWSTR)lpszServerName + L"/cn=Microsoft Private MDB";
		hr = ptrEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(strMsgStoreDN.c_str()), reinterpret_cast<const TCHAR *>(L"SYSTEM"), MAPI_UNICODE | OPENSTORE_OVERRIDE_HOME_MDB, &cbStoreId, &~ptrStoreId);
	} else {
		std::string strMsgStoreDN = "cn="s + (LPCSTR)lpszServerName + "/cn=Microsoft Private MDB";
		hr = ptrEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(strMsgStoreDN.c_str()), reinterpret_cast<const TCHAR *>("SYSTEM"), OPENSTORE_OVERRIDE_HOME_MDB, &cbStoreId, &~ptrStoreId);
	}
	if (hr != hrSuccess)
		return hr;
	hr = lpMAPISession->OpenMsgStore(0, cbStoreId, ptrStoreId, &iid_of(ptrMsgStore), ulFlags & MDB_WRITE, &~ptrMsgStore);
	if (hr != hrSuccess)
		return hr;
	return ptrMsgStore->QueryInterface(IID_IMsgStore,
	       reinterpret_cast<LPVOID *>(lppMsgStore));
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
	SPropArrayPtr ptrEntryIDs;
	MAPIFolderPtr ptrFolder;
	unsigned int cValues, ulType;
	MAPITablePtr ptrTable;
	SPropValue propSubject;
	SRowSetPtr ptrRows;
	MessagePtr ptrMessage;
	static constexpr const SizedSPropTagArray(2, sptaTreeProps) =
		{2, {PR_NON_IPM_SUBTREE_ENTRYID, PR_IPM_SUBTREE_ENTRYID}};

	HRESULT hr = lpStore->GetProps(sptaTreeProps, 0, &cValues, &~ptrEntryIDs);
	if (FAILED(hr))
		return hr;

	// NON_IPM on a public store, IPM on a normal store
	if (ptrEntryIDs[0].ulPropTag == sptaTreeProps.aulPropTag[0])
		hr = lpStore->OpenEntry(ptrEntryIDs[0].Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(ptrEntryIDs[0].Value.bin.lpb),
		     &iid_of(ptrFolder), MAPI_MODIFY, &ulType, &~ptrFolder);
	else if (ptrEntryIDs[1].ulPropTag == sptaTreeProps.aulPropTag[1])
		hr = lpStore->OpenEntry(ptrEntryIDs[1].Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(ptrEntryIDs[1].Value.bin.lpb),
		     &iid_of(ptrFolder), MAPI_MODIFY, &ulType, &~ptrFolder);
	else
		hr = MAPI_E_INVALID_PARAMETER;
	if (hr != hrSuccess)
		return hr;
	hr = ptrFolder->GetContentsTable(MAPI_DEFERRED_ERRORS | MAPI_ASSOCIATED, &~ptrTable);
	if (hr != hrSuccess)
		return hr;

	propSubject.ulPropTag = PR_SUBJECT_A;
	propSubject.Value.lpszA = (char*)szMessageName;

	hr = ECPropertyRestriction(RELOP_EQ, PR_SUBJECT_A, &propSubject, ECRestriction::Cheap)
	     .FindRowIn(ptrTable, BOOKMARK_BEGINNING, 0);
	if (hr == hrSuccess) {
		hr = ptrTable->QueryRows(1, 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;
	}

	if (!ptrRows.empty()) {
		// message found, open it
		auto lpEntryID = ptrRows[0].cfind(PR_ENTRYID);
		if (lpEntryID == NULL)
			return MAPI_E_INVALID_ENTRYID;
		hr = ptrFolder->OpenEntry(lpEntryID->Value.bin.cb,
		     reinterpret_cast<ENTRYID *>(lpEntryID->Value.bin.lpb),
		     &iid_of(ptrMessage), MAPI_MODIFY, &ulType, &~ptrMessage);
		if (hr != hrSuccess)
			return hr;
	} else {
		// not found in folder, create new message
		hr = ptrFolder->CreateMessage(&IID_IMessage, MAPI_ASSOCIATED, &~ptrMessage);
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

HRESULT GetECObject(IMAPIProp *obj, const IID &intf, void **iup)
{
	memory_ptr<SPropValue> pv;
	auto ret = HrGetOneProp(obj, PR_EC_OBJECT, &~pv);
	if (ret != hrSuccess)
		return ret;
	auto ecobj = reinterpret_cast<IUnknown *>(pv->Value.lpszA);
	return ecobj->QueryInterface(intf, iup);
}

HRESULT KServerContext::logon(const char *user, const char *pass)
{
	auto ret = m_mapi.Initialize();
	if (ret != hrSuccess)
		return kc_perror("MAPIInitialize", ret);
	if (user == nullptr)
		user = pass = KOPANO_SYSTEM_USER;
	ret = HrOpenECSession(&~m_session, m_app_misc, PROJECT_VERSION,
	      user, pass, m_host == nullptr ? "default:" : m_host,
	      m_ses_flags, m_ssl_keyfile, m_ssl_keypass);
	if (ret != hrSuccess)
		return kc_perror("OpenECSession", ret);
	ret = HrOpenDefaultStore(m_session, &~m_admstore);
	if (ret != hrSuccess)
		return kc_perror("HrOpenDefaultStore", ret);
	memory_ptr<SPropValue> props;
	ret = HrGetOneProp(m_admstore, PR_EC_OBJECT, &~props);
	if (ret != hrSuccess)
		return kc_perror("HrGetOneProp PR_EC_OBJECT", ret);
	m_ecobject.reset(reinterpret_cast<IUnknown *>(props->Value.lpszA));
	ret = m_ecobject->QueryInterface(IID_IECServiceAdmin, &~m_svcadm);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface IECServiceAdmin", ret);
	return hrSuccess;
}

HRESULT KServerContext::inbox(IMAPIFolder **f) const
{
	unsigned int eid_size = 0, objtype;
	memory_ptr<ENTRYID> eid;
	auto ret = m_admstore->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &eid_size, &~eid, nullptr);
	if (ret != hrSuccess)
		return ret;
	return m_admstore->OpenEntry(eid_size, eid, &iid_of(*f), MAPI_MODIFY, &objtype, reinterpret_cast<IUnknown **>(f));
}

} /* namespace */
