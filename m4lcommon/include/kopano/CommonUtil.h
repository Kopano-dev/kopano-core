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

#ifndef COMMONUTIL_H
#define COMMONUTIL_H

#include <kopano/zcdefs.h>
#include <vector>
#include <mapidefs.h>
#include <mapix.h>
#include <string>
#include <kopano/ECTags.h>

#include <kopano/ustringutil.h>

// Version of GetClientVersion
#define CLIENT_VERSION_OLK2000			9
#define CLIENT_VERSION_OLK2002			10
#define CLIENT_VERSION_OLK2003			11
#define CLIENT_VERSION_OLK2007			12
#define CLIENT_VERSION_OLK2010			14
#define CLIENT_VERSION_LATEST			CLIENT_VERSION_OLK2010 /* UPDATE ME */

/* darn, no sane place because of depend include on mapidefs.h */
bool operator ==(const SBinary &a, const SBinary &b);
bool operator <(const SBinary &a, const SBinary &b);

const char *GetServerUnixSocket(const char *szPreferred = NULL);
std::string GetServerFQDN();

extern HRESULT HrOpenECAdminSession(IMAPISession **lppSession, const char *const app_version, const char *const app_misc, const char *szPath = NULL, ULONG ulProfileFlags = 0, const char *sslkey_file = NULL, const char *sslkey_password = NULL);
extern HRESULT HrOpenECSession(IMAPISession **lppSession, const char *const app_version, const char *const app_misc, const wchar_t *szUsername, const wchar_t *szPassword, const char *szPath = NULL, ULONG ulProfileFlags = 0, const char *sslkey_file = NULL, const char *sslkey_password = NULL, const char *profname = NULL);
HRESULT HrSearchECStoreEntryId(IMAPISession *lpMAPISession, BOOL bPublic, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);

HRESULT HrGetECProviderAdmin(LPMAPISESSION lpSession, LPPROVIDERADMIN *lppProviderAdmin);

HRESULT HrOpenDefaultStoreOffline(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore);
HRESULT HrOpenDefaultStoreOnline(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore);
HRESULT HrOpenStoreOnline(IMAPISession *lpMAPISession, ULONG cbEntryID, LPENTRYID lpEntryID, IMsgStore **lppMsgStore);
HRESULT HrOpenECPublicStoreOnline(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore);

HRESULT GetProxyStoreObject(IMsgStore *lpMsgStore, IMsgStore **lppMsgStore);


HRESULT HrAddArchiveMailBox(LPPROVIDERADMIN lpProviderAdmin, LPCWSTR lpszUserName, LPCWSTR lpszServerName, LPMAPIUID lpProviderUID);


HRESULT ECCreateOneOff(LPTSTR lpszName, LPTSTR lpszAdrType, LPTSTR lpszAddress, ULONG ulFlags, ULONG* lpcbEntryID, LPENTRYID* lppEntryID);
HRESULT ECParseOneOff(const ENTRYID *lpEntryID, ULONG cbEntryID, std::wstring &strWName, std::wstring &strWType, std::wstring &strWAddress);


std::string ToQuotedPrintable(const std::string &input, std::string charset, bool header = true, bool imap = false);

HRESULT HrNewMailNotification(IMsgStore* lpMDB, IMessage* lpMessage);
HRESULT HrCreateEmailSearchKey(const char *lpszEmailType, const char *lpszEmail, ULONG *cb, LPBYTE *lppByte);

HRESULT DoSentMail(IMAPISession *lpSession, IMsgStore *lpMDB, ULONG ulFlags, IMessage *lpMessage);

HRESULT GetClientVersion(unsigned int* ulVersion);
extern HRESULT CreateProfileTemp(const wchar_t *username, const wchar_t *password, const char *path, const char* szProfName, ULONG ulProfileFlags, const char *sslkey_file, const char *sslkey_password, const char *app_version, const char *app_misc);
HRESULT DeleteProfileTemp(char *szProfName);
extern HRESULT OpenSubFolder(LPMDB lpMDB, const wchar_t *folder, wchar_t psep, bool bIsPublic, bool bCreateFolder, LPMAPIFOLDER *lppSubFolder);
HRESULT FindFolder(LPMAPITABLE lpTable, const WCHAR *folder, LPSPropValue *lppFolderProp);

HRESULT HrOpenDefaultCalendar(LPMDB lpMsgStore, LPMAPIFOLDER *lpDefFolder);

HRESULT HrGetPropTags(char **names, IMAPIProp *lpProp, LPSPropTagArray *lppPropTagArray);

HRESULT HrGetAllProps(IMAPIProp *lpProp, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppProps);

HRESULT __stdcall UnWrapStoreEntryID(ULONG cbOrigEntry, LPENTRYID lpOrigEntry, ULONG *lpcbUnWrappedEntry, LPENTRYID *lppUnWrappedEntry);

HRESULT DoAddress(IAddrBook *lpAdrBook, ULONG* hWnd, LPADRPARM lpAdrParam, LPADRLIST *lpResult);

// Auto-accept settings
HRESULT HrGetRemoteAdminStore(IMAPISession *lpMAPISession, IMsgStore *lpMsgStore, LPCTSTR lpszServerName, ULONG ulFlags, IMsgStore **lppMsgStore);

HRESULT GetConfigMessage(LPMDB lpStore, const char* szMessageName, IMessage **lppMessage);

HRESULT HrOpenDefaultStore(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore);
HRESULT HrOpenDefaultStore(IMAPISession *lpMAPISession, ULONG ulFlags, IMsgStore **lppMsgStore);
HRESULT HrOpenECPublicStore(IMAPISession *lpMAPISession, IMsgStore **lppMsgStore);
HRESULT HrOpenECPublicStore(IMAPISession *lpMAPISession, ULONG ulFlags, IMsgStore **lppMsgStore);
HRESULT HrAddECMailBox(LPMAPISESSION lpSession, LPCWSTR lpszUserName);
HRESULT HrAddECMailBox(LPPROVIDERADMIN lpProviderAdmin, LPCWSTR lpszUserName);
HRESULT HrRemoveECMailBox(LPMAPISESSION lpSession, LPMAPIUID lpsProviderUID);
HRESULT HrRemoveECMailBox(LPPROVIDERADMIN lpProviderAdmin, LPMAPIUID lpsProviderUID);
HRESULT HrGetAddress(IMAPISession *lpSession, LPSPropValue lpProps, ULONG cValues, ULONG ulPropTagEntryID, ULONG ulPropTagName, ULONG ulPropTagType, ULONG ulPropTagEmailAddress, std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress);
HRESULT HrGetAddress(IMAPISession *lpSession, IMessage *lpMessage, ULONG ulPropTagEntryID, ULONG ulPropTagName, ULONG ulPropTagType, ULONG ulPropTagEmailAddress, std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress);
HRESULT HrGetAddress(LPADRBOOK lpAdrBook, IMessage *lpMessage, ULONG ulPropTagEntryID, ULONG ulPropTagName, ULONG ulPropTagType, ULONG ulPropTagEmailAddress, std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress);
HRESULT HrGetAddress(LPADRBOOK lpAdrBook, LPSPropValue lpProps, ULONG cValues, ULONG ulPropTagEntryID, ULONG ulPropTagName, ULONG ulPropTagType, ULONG ulPropTagEmailAddress, std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress);
HRESULT HrGetAddress(LPADRBOOK lpAdrBook, LPENTRYID lpEntryID, ULONG cbEntryID, std::wstring &strName, std::wstring &strType, std::wstring &strEmailAddress);
std::string ToQuotedBase64Header(const std::string &input, std::string charset);
std::string ToQuotedBase64Header(const std::wstring &input);
HRESULT TestRestriction(LPSRestriction lpCondition, ULONG cValues, LPSPropValue lpPropVals, const ECLocale &locale, ULONG ulLevel = 0);
HRESULT TestRestriction(LPSRestriction lpCondition, IMAPIProp *lpMessage, const ECLocale &locale, ULONG ulLevel = 0);
HRESULT HrOpenUserMsgStore(LPMAPISESSION lpSession, WCHAR *lpszWUser, LPMDB *lppStore);
HRESULT HrOpenUserMsgStore(LPMAPISESSION lpSession, LPMDB lpStore, WCHAR *lpszUser, LPMDB *lppStore);
// Auto-accept settings
HRESULT SetAutoAcceptSettings(IMsgStore *lpMsgStore, bool bAutoAccept, bool bDeclineConflict, bool bDeclineRecurring);
HRESULT GetAutoAcceptSettings(IMsgStore *lpMsgStore, bool *lpbAutoAccept, bool *lpbDeclineConflict, bool *lpbDeclineRecurring);
HRESULT HrGetGAB(LPMAPISESSION lpSession, LPABCONT *lppGAB);
HRESULT HrGetGAB(LPADRBOOK lpAddrBook, LPABCONT *lppGAB);

/**
 * NAMED PROPERTY utilities
 *
 * HOW TO USE
 * Make sure you have an IMAPIProp interface to pass to PROPMAP_INIT
 * All properties are allocated an ULONG with name PROP_XXXXX (XXXXX passed in first param of PROPMAP_NAMED_ID
 *
 * EXAMPLE
 *
 * PROPMAP_START(2)
 *  PROPMAP_NAMED_ID(RECURRING, PT_BOOLEAN, PSETID_Appointment, dispidRecurring)
 *  PROPMAP_NAMED_ID(START, 	PT_SYSTIME, PSETID_Appointment, dispidStart)
 * PROPMAP_INIT(lpMessage)
 *
 * printf("%X %X\n", PROP_RECURRING, PROP_START);
 *
 */
class ECPropMapEntry _kc_final {
public:
    ECPropMapEntry(GUID guid, ULONG ulId);
    ECPropMapEntry(GUID guid, const char *strName);
    ECPropMapEntry(const ECPropMapEntry &other);
	ECPropMapEntry(ECPropMapEntry &&);
    ~ECPropMapEntry();
    
    MAPINAMEID* GetMAPINameId();
private:
    MAPINAMEID m_sMAPINameId;
    GUID m_sGuid;
};

class ECPropMap _kc_final {
public:
    ECPropMap(size_t = 0);
    void AddProp(ULONG *lpId, ULONG ulType, const ECPropMapEntry &entry);
    HRESULT Resolve(IMAPIProp *lpMAPIProp);
private:
    std::vector<ECPropMapEntry> lstNames;
    std::vector<ULONG *> lstVars;
    std::vector<ULONG> lstTypes;
};

#define PROPMAP_DECL() ECPropMap __propmap;
#define PROPMAP_START(hint) ECPropMap __propmap(hint);
#define PROPMAP_NAMED_ID(name,type,guid,id) ULONG PROP_##name; __propmap.AddProp(&PROP_##name, type, ECPropMapEntry(guid, id));
#define PROPMAP_INIT(lpObject) hr = __propmap.Resolve(lpObject); if(hr != hrSuccess) goto exitpm;

#define PROPMAP_DEF_NAMED_ID(name) ULONG PROP_##name = 0;
#define PROPMAP_INIT_NAMED_ID(name,type,guid,id) __propmap.AddProp(&PROP_##name, type, ECPropMapEntry(guid, id));

// Determine the size of an array
template <typename T, unsigned N>
inline unsigned  arraySize(T (&)[N])   { return N; }

// Get the one-past-end item of an array
template <typename T, unsigned N>
inline T* arrayEnd(T (&array)[N])	{ return array + N; }

#endif // COMMONUTIL_H
