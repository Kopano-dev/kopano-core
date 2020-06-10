/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
/* mapix.h – Defines flags and interfaces that MAPI implements for clients */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <string>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiguid.h>
#include <mapitags.h>

struct IProfAdmin;
struct IMsgServiceAdmin;
struct IMAPISession;

typedef IProfAdmin* LPPROFADMIN;
typedef IMsgServiceAdmin* LPSERVICEADMIN;
typedef IMAPISession* LPMAPISESSION;


/* uhhh... already in mapi.h ? */
/* MAPILogon() flags.       */

//#define MAPI_LOGON_UI           0x00000001  /* Display logon UI                 */
//#define MAPI_NEW_SESSION        0x00000002  /* Don't use shared session         */
#define MAPI_ALLOW_OTHERS       0x00000008  /* Make this a shared session       */
#define MAPI_EXPLICIT_PROFILE   0x00000010  /* Don't use default profile        */
//#define MAPI_EXTENDED           0x00000020  /* Extended MAPI Logon              */
//#define MAPI_FORCE_DOWNLOAD     0x00001000  /* Get new mail before return       */
#define MAPI_SERVICE_UI_ALWAYS  0x00002000  /* Do logon UI in all providers     */
#define MAPI_NO_MAIL            0x00008000  /* Do not activate transports       */
/* #define MAPI_NT_SERVICE          0x00010000  Allow logon from an NT service  */
/* #ifndef MAPI_PASSWORD_UI */
/* #define MAPI_PASSWORD_UI        0x00020000  /\* Display password UI only         *\/ */
/* #endif */
#define MAPI_TIMEOUT_SHORT      0x00100000  /* Minimal wait for logon resources */

#define MAPI_SIMPLE_DEFAULT (MAPI_LOGON_UI | MAPI_FORCE_DOWNLOAD | MAPI_ALLOW_OTHERS)
#define MAPI_SIMPLE_EXPLICIT (MAPI_NEW_SESSION | MAPI_FORCE_DOWNLOAD | MAPI_EXPLICIT_PROFILE)

/* Structure passed to MAPIInitialize(), and its ulFlags values */
struct MAPIINIT_0 {
	ULONG ulVersion, ulFlags;
};
typedef struct MAPIINIT_0 *LPMAPIINIT_0;
typedef MAPIINIT_0 MAPIINIT;
typedef MAPIINIT *LPMAPIINIT;

#define MAPI_INIT_VERSION               0

#define MAPI_MULTITHREAD_NOTIFICATIONS  0x00000001
/* Reserved for MAPI                    0x40000000 */
#define MAPI_NT_SERVICE              0x00010000  /* Use from NT service */

/* MAPI base functions */

extern "C" {

typedef HRESULT (MAPIINITIALIZE)(LPVOID lpMapiInit);
typedef MAPIINITIALIZE* LPMAPIINITIALIZE;

typedef void (MAPIUNINITIALIZE)(void);
typedef MAPIUNINITIALIZE* LPMAPIUNINITIALIZE;
extern KC_EXPORT MAPIINITIALIZE MAPIInitialize;
extern KC_EXPORT MAPIUNINITIALIZE MAPIUninitialize;

/*  Extended MAPI Logon function */

typedef HRESULT (MAPILOGONEX)(ULONG_PTR ui_param, const TCHAR *profname, const TCHAR *password, ULONG flags, IMAPISession **);
typedef MAPILOGONEX* LPMAPILOGONEX;
extern KC_EXPORT MAPILOGONEX MAPILogonEx;

typedef SCODE (MAPIALLOCATEBUFFER)(
    ULONG           cbSize,
    LPVOID *    lppBuffer
);
typedef SCODE (MAPIALLOCATEMORE)(
    ULONG           cbSize,
    LPVOID          lpObject,
    LPVOID *    lppBuffer
);
typedef ULONG (MAPIFREEBUFFER)(
    LPVOID          lpBuffer
);
typedef MAPIALLOCATEBUFFER  *LPMAPIALLOCATEBUFFER;
typedef MAPIALLOCATEMORE    *LPMAPIALLOCATEMORE;
typedef MAPIFREEBUFFER      *LPMAPIFREEBUFFER;
extern KC_EXPORT MAPIALLOCATEBUFFER MAPIAllocateBuffer;
extern KC_EXPORT MAPIALLOCATEMORE MAPIAllocateMore;
extern KC_EXPORT MAPIFREEBUFFER MAPIFreeBuffer;

typedef HRESULT (MAPIADMINPROFILES)(
    ULONG ulFlags,
    LPPROFADMIN *lppProfAdmin
);
typedef MAPIADMINPROFILES *LPMAPIADMINPROFILES;
extern KC_EXPORT MAPIADMINPROFILES MAPIAdminProfiles;

} // EXTERN "C"

/*
 * IMAPISession Interface
 */

/* Flags for OpenEntry and others */
/*#define MAPI_MODIFY               ((ULONG) 0x00000001) */

/* Flags for Logoff */
#define MAPI_LOGOFF_SHARED      0x00000001  /* Close all shared sessions    */
#define MAPI_LOGOFF_UI          0x00000002  /* It's OK to present UI        */

/* Flags for SetDefaultStore. They are mutually exclusive. */
#define MAPI_DEFAULT_STORE          0x00000001  /* for incoming messages */
#define MAPI_SIMPLE_STORE_TEMPORARY 0x00000002  /* for simple MAPI and CMC */
#define MAPI_SIMPLE_STORE_PERMANENT 0x00000003  /* for simple MAPI and CMC */
#define MAPI_PRIMARY_STORE          0x00000004  /* Used by some clients */
#define MAPI_SECONDARY_STORE        0x00000005  /* Used by some clients */

/* Flags for ShowForm. */
#define MAPI_POST_MESSAGE       0x00000001  /* Selects post/send semantics */
#define MAPI_NEW_MESSAGE        0x00000002  /* Governs copying during submission */

struct IMAPISession : public virtual IUnknown {
public:
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError) = 0;
    virtual HRESULT GetMsgStoresTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
	virtual HRESULT OpenMsgStore(ULONG_PTR ui_param, ULONG eid_size, const ENTRYID *, const IID *intf, ULONG flags, IMsgStore **) = 0;
	virtual HRESULT OpenAddressBook(ULONG_PTR ulUIParam, LPCIID lpInterface, ULONG ulFlags, LPADRBOOK *lppAdrBook) = 0;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, const IID *intf, ULONG flags, IProfSect **) = 0;
	virtual HRESULT GetStatusTable(ULONG ulFlags, LPMAPITABLE *lppTable) { return MAPI_E_NOT_FOUND; }
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) = 0;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) = 0;
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *eid, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn_id) = 0;
    virtual HRESULT Unadvise(ULONG ulConnection) = 0;
	virtual HRESULT MessageOptions(ULONG_PTR ui_param, ULONG flags, const TCHAR *addrtype, IMessage *) { return MAPI_E_NOT_FOUND; }
	virtual HRESULT QueryDefaultMessageOpt(const TCHAR *addrtype, ULONG flags, ULONG *nvals, SPropValue **) { return MAPI_E_NOT_FOUND; }
	virtual HRESULT EnumAdrTypes(ULONG flags, ULONG *ntypes, TCHAR ***) { return MAPI_E_NOT_FOUND; }
    virtual HRESULT QueryIdentity(ULONG* lpcbEntryID, LPENTRYID* lppEntryID) = 0;
	virtual HRESULT Logoff(ULONG_PTR ulUIParam, ULONG ulFlags, ULONG ulReserved) = 0;
	virtual HRESULT SetDefaultStore(ULONG flags, ULONG cbEntryID, const ENTRYID *lpEntryID) { return MAPI_E_NOT_FOUND; }
    virtual HRESULT AdminServices(ULONG ulFlags, LPSERVICEADMIN* lppServiceAdmin) = 0;
	virtual HRESULT ShowForm(ULONG_PTR ui_param, IMsgStore *, IMAPIFolder *parent, const IID *intf, ULONG msg_token, IMessage *sent, ULONG flags, ULONG msg_status, ULONG msg_flags, ULONG access, const char *msg_class) { return MAPI_E_NOT_FOUND; }
	virtual HRESULT PrepareForm(LPCIID lpInterface, LPMESSAGE lpMessage, ULONG *lpulMessageToken) { return MAPI_E_NOT_FOUND; }
};
IID_OF(IMAPISession)

/*DECLARE_MAPI_INTERFACE_PTR(IMAPISession, LPMAPISESSION);*/

/* IAddrBook Interface ----------------------------------------------------- */
struct IAddrBook : public virtual IMAPIProp {
public:
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) = 0;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) = 0;
	virtual HRESULT Advise(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT Unadvise(ULONG ulConnection) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT CreateOneOff(const TCHAR *name, const TCHAR *addrtype, const TCHAR *addr, ULONG flags, ULONG *eid_size, ENTRYID **) = 0;
	virtual HRESULT NewEntry(ULONG_PTR ui_param, ULONG flags, ULONG cbEIDContainer, const ENTRYID *lpEIDContainer, ULONG cbEIDNewEntryTpl, const ENTRYID *lpEIDNewEntryTpl, ULONG *lpcbEIDNewEntry, ENTRYID **lppEIDNewEntry) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT ResolveName(ULONG_PTR ui_param, ULONG flags, const TCHAR *new_title, ADRLIST *lpAdrList) = 0;
	virtual HRESULT Address(ULONG_PTR *lpulUIParam, LPADRPARM lpAdrParms, LPADRLIST *lppAdrList) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT Details(ULONG_PTR *ui_param, DISMISSMODELESS *, void *dism_ctx, ULONG cbEntryID, const ENTRYID *lpEntryID, LPFNBUTTON callback, void *btn_ctx, const TCHAR *btn_text, ULONG flags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT RecipOptions(ULONG_PTR ui_param, ULONG flags, const ADRENTRY *recip) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT QueryDefaultRecipOpt(const TCHAR *addrtype, ULONG flags, ULONG *nvals, SPropValue **opts) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT GetPAB(ULONG *lpcbEntryID, LPENTRYID *lppEntryID) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT SetPAB(ULONG cbEntryID, const ENTRYID *lpEntryID) { return MAPI_E_NO_SUPPORT; }
    virtual HRESULT GetDefaultDir(ULONG* lpcbEntryID, LPENTRYID* lppEntryID) = 0;
	virtual HRESULT SetDefaultDir(ULONG cbEntryID, const ENTRYID *lpEntryID) { return MAPI_E_NO_SUPPORT; }
    virtual HRESULT GetSearchPath(ULONG ulFlags, LPSRowSet* lppSearchPath) = 0;
	virtual HRESULT SetSearchPath(ULONG flags, const SRowSet *lpSearchPath) = 0;
	virtual HRESULT PrepareRecips(ULONG ulFlags, const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList) = 0;
};
IID_OF(IAddrBook)

typedef IAddrBook* LPADRBOOK;

/*
 * IProfAdmin Interface
 */
#define MAPI_DEFAULT_SERVICES           0x00000001

struct IProfAdmin : public virtual IUnknown {
public:
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError) = 0;
    virtual HRESULT GetProfileTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
	virtual HRESULT CreateProfile(const TCHAR *name, const TCHAR *password, ULONG_PTR ui_param, ULONG flags) = 0;
	virtual HRESULT DeleteProfile(const TCHAR *name, ULONG flags) = 0;
	virtual HRESULT ChangeProfilePassword(const TCHAR *name, const TCHAR *oldpw, const TCHAR *newpw, ULONG flags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT CopyProfile(const TCHAR *oldname, const TCHAR *oldpw, const TCHAR *newname, ULONG_PTR ui_param, ULONG flags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT RenameProfile(const TCHAR *oldname, const TCHAR *oldpw, const TCHAR *newname, ULONG_PTR ui_param, ULONG flags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT SetDefaultProfile(const TCHAR *name, ULONG flags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT AdminServices(const TCHAR *name, const TCHAR *password, ULONG_PTR ui_param, ULONG flags, IMsgServiceAdmin **) = 0;
};
IID_OF(IProfAdmin)

/*
 * IMsgServiceAdmin Interface
 */

/* Values for PR_RESOURCE_FLAGS in message service table */
#define SERVICE_DEFAULT_STORE       0x00000001
#define SERVICE_SINGLE_COPY         0x00000002
#define SERVICE_CREATE_WITH_STORE   0x00000004
#define SERVICE_PRIMARY_IDENTITY    0x00000008
#define SERVICE_NO_PRIMARY_IDENTITY 0x00000020

struct IMsgServiceAdmin : public virtual IUnknown {
public:
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError) = 0;
    virtual HRESULT GetMsgServiceTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
	virtual HRESULT CreateMsgService(const TCHAR *service, const TCHAR *display_name, ULONG_PTR ui_param, ULONG flags) = 0;
	virtual HRESULT DeleteMsgService(const MAPIUID *uid) = 0;
	virtual HRESULT CopyMsgService(const MAPIUID *uid, const TCHAR *display_name, const IID *ifsrc, const IID *ifdst, void *object_dst, ULONG_PTR ui_param, ULONG flags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT RenameMsgService(const MAPIUID *uid, ULONG flags, const TCHAR *display_name) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT ConfigureMsgService(const MAPIUID *uid, ULONG_PTR ui_param, ULONG flags, ULONG nvals, const SPropValue *props) = 0;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, const IID *intf, ULONG flags, IProfSect **) = 0;
	virtual HRESULT MsgServiceTransportOrder(ULONG nuids, const MAPIUID *uids, ULONG flags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT AdminProviders(const MAPIUID *uid, ULONG flags, IProviderAdmin **) = 0;
	virtual HRESULT SetPrimaryIdentity(const MAPIUID *uid, ULONG flags) { return MAPI_E_NO_SUPPORT; }
    virtual HRESULT GetProviderTable(ULONG ulFlags, LPMAPITABLE* lppTable) = 0;
};
IID_OF(IMsgServiceAdmin)

struct IMsgServiceAdmin2 : public IMsgServiceAdmin {
	public:
	virtual HRESULT CreateMsgServiceEx(const char *service, const char *display_name, ULONG_PTR ui_param, ULONG flags, MAPIUID *out) = 0;
};
IID_OF(IMsgServiceAdmin2)

namespace KC {

extern KC_EXPORT HRESULT kc_session_save(IMAPISession *, std::string &);
extern KC_EXPORT HRESULT kc_session_restore(const std::string &, IMAPISession **);
extern KC_EXPORT SCODE KAllocCopy(const void *src, size_t z, void **dst, void *base = nullptr);

}
