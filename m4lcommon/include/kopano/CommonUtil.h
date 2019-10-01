/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef COMMONUTIL_H
#define COMMONUTIL_H

#include <kopano/zcdefs.h>
#include <vector>
#include <mapidefs.h>
#include <mapix.h>
#include <string>
#include <kopano/ECTags.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/automapi.hpp>
#include <kopano/memory.hpp>
#include <kopano/ustringutil.h>

// Version of GetClientVersion
#define CLIENT_VERSION_OLK2000			9
#define CLIENT_VERSION_OLK2002			10
#define CLIENT_VERSION_OLK2003			11
#define CLIENT_VERSION_OLK2007			12
#define CLIENT_VERSION_OLK2010			14
#define CLIENT_VERSION_LATEST			CLIENT_VERSION_OLK2010 /* UPDATE ME */

/**
 * An enumeration for getting the localfreebusy from the calendar or from the free/busy data folder.
 *
 * @note it's also the array position of property PR_FREEBUSY_ENTRYIDS
 */
enum DGMessageType {
	dgAssociated = 0,	/**< Localfreebusy message in default associated calendar folder */
	dgFreebusydata = 1	/**< Localfreebusy message in Free/busy data folder */
};

/* darn, no sane place because of depend include on mapidefs.h */
extern KC_EXPORT bool operator==(const SBinary &, const SBinary &) noexcept;
extern KC_EXPORT bool operator<(const SBinary &, const SBinary &) noexcept;

namespace KC {

extern KC_EXPORT const char *GetServerUnixSocket(const char *pref = nullptr);
extern KC_EXPORT HRESULT HrOpenECAdminSession(IMAPISession **, const char *const app_ver, const char *app_misc, const char *path = nullptr, unsigned int profflags = 0, const char *sslkey_file = nullptr, const char *sslkey_password = nullptr);
extern KC_EXPORT HRESULT HrOpenECSession(IMAPISession **ses, const char *app_ver, const char *app_misc, const char *user, const char *pass, const char *path = nullptr, unsigned int profile_flags = 0, const char *sslkey_file = nullptr, const char *sslkey_password = nullptr, const char *profname = nullptr);
extern KC_EXPORT HRESULT HrOpenECSession(IMAPISession **ses, const char *app_ver, const char *app_misc, const wchar_t *user, const wchar_t *pass, const char *path = nullptr, unsigned int profile_flags = 0, const char *sslkey_file = nullptr, const char *sslkey_password = nullptr, const char *profname = nullptr);
extern KC_EXPORT HRESULT HrOpenECPublicStoreOnline(IMAPISession *, IMsgStore **ret);
extern KC_EXPORT HRESULT ECCreateOneOff(const TCHAR * name, const TCHAR * addrtype, const TCHAR * addr, unsigned int flags, unsigned int *eid_size, ENTRYID **eid);
extern KC_EXPORT HRESULT ECParseOneOff(const ENTRYID *eid, unsigned int eid_size, std::wstring &name, std::wstring &type, std::wstring &addr);
extern KC_EXPORT HRESULT HrNewMailNotification(IMsgStore *, IMessage *);
extern KC_EXPORT HRESULT HrCreateEmailSearchKey(const char *type, const char *addr, unsigned int *size, BYTE **out);
extern KC_EXPORT HRESULT DoSentMail(IMAPISession *, IMsgStore *, unsigned int flags, object_ptr<IMessage>);
extern KC_EXPORT HRESULT GetClientVersion(unsigned int *);
extern KC_EXPORT HRESULT OpenSubFolder(IMsgStore *, const wchar_t *folder, wchar_t psep, bool is_public, bool create_folder, IMAPIFolder **subfolder);
extern KC_EXPORT HRESULT spv_postload_large_props(IMAPIProp *, const SPropTagArray *, unsigned int, SPropValue *);
extern KC_EXPORT HRESULT HrOpenDefaultCalendar(IMsgStore *, IMAPIFolder **default_folder);
extern KC_EXPORT HRESULT HrGetFullProp(IMAPIProp *prop, unsigned int tag, SPropValue **);
extern KC_EXPORT HRESULT HrGetAllProps(IMAPIProp *prop, unsigned int flags, unsigned int *nvals, SPropValue **props);
extern KC_EXPORT HRESULT UnWrapStoreEntryID(unsigned int eid_size, const ENTRYID *eid, unsigned int *ret_size, ENTRYID **ret);
extern KC_EXPORT HRESULT GetECObject(IMAPIProp *, const IID &, void **);

// Auto-accept settings
extern KC_EXPORT HRESULT HrGetRemoteAdminStore(IMAPISession *, IMsgStore *, const TCHAR *server, unsigned int flags, IMsgStore **ret);

extern KC_EXPORT HRESULT HrOpenDefaultStore(IMAPISession *, IMsgStore **ret);
extern KC_EXPORT HRESULT HrOpenDefaultStore(IMAPISession *, unsigned int flags, IMsgStore **ret);
extern KC_EXPORT HRESULT HrOpenECPublicStore(IMAPISession *, IMsgStore **ret);
extern KC_EXPORT HRESULT HrGetAddress(IAddrBook *, IMessage *, unsigned int tag_eid, unsigned int tag_name, unsigned int tag_type, unsigned int tag_addr, std::wstring &name, std::wstring &type, std::wstring &addr);
extern KC_EXPORT HRESULT HrGetAddress(IAddrBook *, const SPropValue *props, unsigned int nvals, unsigned int tag_eid, unsigned int tag_name, unsigned int tag_type, unsigned int tag_addr, std::wstring &name, std::wstring &type, std::wstring &addr);
extern KC_EXPORT HRESULT HrGetAddress(IAddrBook *, const ENTRYID *eid, unsigned int eid_size, std::wstring &name, std::wstring &type, std::wstring &addr);
extern KC_EXPORT std::string ToQuotedBase64Header(const std::wstring &);
extern HRESULT TestRestriction(const SRestriction *cond, ULONG nvals, const SPropValue *props, const ECLocale &, ULONG level = 0);
extern KC_EXPORT HRESULT TestRestriction(const SRestriction *cond, IMAPIProp *msg, const ECLocale &, unsigned int level = 0);
extern KC_EXPORT HRESULT HrOpenUserMsgStore(IMAPISession *, const wchar_t *user, IMsgStore **store);
extern KC_EXPORT HRESULT OpenLocalFBMessage(DGMessageType eDGMsgType, IMsgStore *lpMsgStore, bool bCreateIfMissing, IMessage **lppFBMessage);

// Auto-accept settings
extern KC_EXPORT HRESULT SetAutoAcceptSettings(IMsgStore *, bool auto_accept, bool decline_conflict, bool decline_recurring, bool autoprocess_ptr);
extern KC_EXPORT HRESULT GetAutoAcceptSettings(IMsgStore *, bool *auto_accept, bool *decline_conflict, bool *decline_recurring, bool *autoprocess_ptr = nullptr);

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
class KC_EXPORT ECPropMapEntry KC_FINAL {
public:
    ECPropMapEntry(GUID guid, ULONG ulId);
    ECPropMapEntry(GUID guid, const char *strName);
	_kc_hidden ECPropMapEntry(const ECPropMapEntry &);
	_kc_hidden ECPropMapEntry(ECPropMapEntry &&);
    ~ECPropMapEntry();
	_kc_hidden MAPINAMEID *GetMAPINameId(void);
private:
    MAPINAMEID m_sMAPINameId;
    GUID m_sGuid;
};

class KC_EXPORT ECPropMap KC_FINAL {
public:
    ECPropMap(size_t = 0);
    void AddProp(ULONG *lpId, ULONG ulType, const ECPropMapEntry &entry);
    HRESULT Resolve(IMAPIProp *lpMAPIProp);
private:
    std::vector<ECPropMapEntry> lstNames;
    std::vector<ULONG *> lstVars;
    std::vector<ULONG> lstTypes;
};

#define PROPMAP_DECL() KC::ECPropMap m_propmap;
#define PROPMAP_START(hint) KC::ECPropMap m_propmap(hint);
#define PROPMAP_NAMED_ID(name, type, guid, id) ULONG PROP_##name; m_propmap.AddProp(&PROP_##name, type, KC::ECPropMapEntry(guid, id));
#define PROPMAP_INIT(lpObject) do { auto propmap_hr = m_propmap.Resolve(lpObject); if (propmap_hr != hrSuccess) return propmap_hr; } while (false);
#define PROPMAP_DEF_NAMED_ID(name) ULONG PROP_##name = 0;
#define PROPMAP_INIT_NAMED_ID(name, type, guid, id) m_propmap.AddProp(&PROP_##name, type, KC::ECPropMapEntry(guid, id));

class KC_EXPORT KServerContext {
	public:
	HRESULT logon(const char *user = nullptr, const char *password = nullptr);
	HRESULT inbox(IMAPIFolder **) const;

	const char *m_app_misc = nullptr, *m_host = nullptr;
	const char *m_ssl_keyfile = nullptr, *m_ssl_keypass = nullptr;
	unsigned int m_ses_flags = EC_PROFILE_FLAGS_NO_NOTIFICATIONS;

	private:
	AutoMAPI m_mapi;

	public:
	object_ptr<IMAPISession> m_session;
	object_ptr<IMsgStore> m_admstore;
	object_ptr<IUnknown> m_ecobject;
	object_ptr<IECServiceAdmin> m_svcadm;
};

} /* namespace */

#endif // COMMONUTIL_H
