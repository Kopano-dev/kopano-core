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
extern _kc_export bool operator==(const SBinary &, const SBinary &) noexcept;
extern _kc_export bool operator<(const SBinary &, const SBinary &) noexcept;

namespace KC {

extern _kc_export const char *GetServerUnixSocket(const char *pref = nullptr);
extern _kc_export HRESULT HrOpenECAdminSession(IMAPISession **, const char *const app_ver, const char *app_misc, const char *path = nullptr, ULONG profflags = 0, const char *sslkey_file = nullptr, const char *sslkey_password = nullptr);
extern _kc_export HRESULT HrOpenECSession(IMAPISession **ses, const char *app_ver, const char *app_misc, const char *user, const char *pass, const char *path = nullptr, ULONG profile_flags = 0, const char *sslkey_file = nullptr, const char *sslkey_password = nullptr, const char *profname = nullptr);
extern _kc_export HRESULT HrOpenECSession(IMAPISession **ses, const char *app_ver, const char *app_misc, const wchar_t *user, const wchar_t *pass, const char *path = nullptr, ULONG profile_flags = 0, const char *sslkey_file = nullptr, const char *sslkey_password = nullptr, const char *profname = nullptr);
extern _kc_export HRESULT HrOpenECPublicStoreOnline(IMAPISession *, IMsgStore **ret);
extern _kc_export HRESULT ECCreateOneOff(const TCHAR * name, const TCHAR * addrtype, const TCHAR * addr, ULONG flags, ULONG *eid_size, LPENTRYID *eid);
extern _kc_export HRESULT ECParseOneOff(const ENTRYID *eid, ULONG eid_size, std::wstring &name, std::wstring &type, std::wstring &addr);
extern _kc_export HRESULT HrNewMailNotification(IMsgStore *, IMessage *);
extern _kc_export HRESULT HrCreateEmailSearchKey(const char *type, const char *addr, ULONG *size, LPBYTE *out);
extern _kc_export HRESULT DoSentMail(IMAPISession *, IMsgStore *, ULONG flags, object_ptr<IMessage>);
extern _kc_export HRESULT GetClientVersion(unsigned int *);
extern _kc_export HRESULT OpenSubFolder(LPMDB, const wchar_t *folder, wchar_t psep, bool is_public, bool create_folder, LPMAPIFOLDER *subfolder);
extern _kc_export HRESULT spv_postload_large_props(IMAPIProp *, const SPropTagArray *, unsigned int, SPropValue *);
extern _kc_export HRESULT HrOpenDefaultCalendar(LPMDB, LPMAPIFOLDER *default_folder);
extern _kc_export HRESULT HrGetFullProp(IMAPIProp *prop, unsigned int tag, SPropValue **);
extern _kc_export HRESULT HrGetAllProps(IMAPIProp *prop, ULONG flags, ULONG *nvals, LPSPropValue *props);
extern _kc_export HRESULT UnWrapStoreEntryID(ULONG eid_size, const ENTRYID *eid, ULONG *ret_size, ENTRYID **ret);
extern _kc_export HRESULT GetECObject(IMAPIProp *, const IID &, void **);

// Auto-accept settings
extern _kc_export HRESULT HrGetRemoteAdminStore(IMAPISession *, IMsgStore *, LPCTSTR server, ULONG flags, IMsgStore **ret);

extern _kc_export HRESULT HrOpenDefaultStore(IMAPISession *, IMsgStore **ret);
extern _kc_export HRESULT HrOpenDefaultStore(IMAPISession *, ULONG flags, IMsgStore **ret);
extern _kc_export HRESULT HrOpenECPublicStore(IMAPISession *, IMsgStore **ret);
extern _kc_export HRESULT HrGetAddress(LPADRBOOK, IMessage *, ULONG tag_eid, ULONG tag_name, ULONG tag_type, ULONG tag_addr, std::wstring &name, std::wstring &type, std::wstring &addr);
extern _kc_export HRESULT HrGetAddress(IAddrBook *, const SPropValue *props, ULONG nvals, ULONG tag_eid, ULONG tag_name, ULONG tag_type, ULONG tag_addr, std::wstring &name, std::wstring &type, std::wstring &addr);
extern _kc_export HRESULT HrGetAddress(IAddrBook *, const ENTRYID *eid, ULONG eid_size, std::wstring &name, std::wstring &type, std::wstring &addr);
extern _kc_export std::string ToQuotedBase64Header(const std::wstring &);
extern HRESULT TestRestriction(const SRestriction *cond, ULONG nvals, const SPropValue *props, const ECLocale &, ULONG level = 0);
extern _kc_export HRESULT TestRestriction(const SRestriction *cond, IMAPIProp *msg, const ECLocale &, ULONG level = 0);
extern _kc_export HRESULT HrOpenUserMsgStore(LPMAPISESSION, const wchar_t *user, LPMDB *store);
extern _kc_export HRESULT OpenLocalFBMessage(DGMessageType eDGMsgType, IMsgStore *lpMsgStore, bool bCreateIfMissing, IMessage **lppFBMessage);

// Auto-accept settings
extern _kc_export HRESULT SetAutoAcceptSettings(IMsgStore *, bool auto_accept, bool decline_conflict, bool decline_recurring);
extern _kc_export HRESULT GetAutoAcceptSettings(IMsgStore *, bool *auto_accept, bool *decline_conflict, bool *decline_recurring, bool *autoprocess_ptr = nullptr);

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
class _kc_export ECPropMapEntry KC_FINAL {
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

class _kc_export ECPropMap KC_FINAL {
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

class _kc_export KServerContext {
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
