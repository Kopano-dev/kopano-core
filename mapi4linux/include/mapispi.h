/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
/*
 * mapispi.h – Defines flags and interfaces that MAPI implements for service
 * providers and message services.
 */
#pragma once
#include <kopano/platform.h>
#include <initializer_list>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiguid.h>
#include <mapitags.h>

/*  The MAPI SPI has a version number.  MAPIX.DLL knows and supports
 *  one or more versions of the SPI.  Each provider supports one or
 *  more versions of the SPI.  Checks are performed in both MAPIX.DLL
 *  and in the provider to ensure that they agree to use exactly one
 *  version of the MAPI SPI.
 *
 *  The SPI version number is composed of a major (8-bit) version,
 *  minor (8-bit) version, and micro (16-bit) version.  The first
 *  retail ship of MAPI 1.0 is expected to be version 1.0.0.
 *  The major version number changes rarely.
 *  The minor version number changes opon each retail ship of
 *  MAPI if the SPI has been modified.
 *  The micro version number changes internally at Microsoft
 *  during development of MAPI.
 *
 *  The version of the SPI documented by this set of header files
 *  is ALWAYS known as "CURRENT_SPI_VERSION".  If you write a
 *  service provider, and get a new set of header files, and update
 *  your code to the new interface, you'll be at the "current" version.
 */
#define CURRENT_SPI_VERSION 0x00010010L

/*  Here are some well-known SPI version numbers:
 *  (These will eventually be useful for provider-writers who
 *  might choose to make provider DLLs that support more than
 *  one version of the MAPI SPI.
 */
#define PDK1_SPI_VERSION    0x00010000L /* 0.1.0  MAPI PDK1 Spring 1993 */
#define PDK2_SPI_VERSION    0x00010008L /* 0.1.8  MAPI PDK2 Spring 1994 */
#define PDK3_SPI_VERSION    0x00010010L /* 0.1.16 MAPI PDK3 Fall 1994   */

/*
 * Forward declaration of interface pointers specific to the service
 * provider interface.
 */
class IMAPISupport;
typedef IMAPISupport* LPMAPISUP;

/*
 * IMAPISupport Interface
 */

/* Notification key structure for the MAPI notification engine */
struct NOTIFKEY {
	NOTIFKEY() = delete;
	template<typename T> NOTIFKEY(std::initializer_list<T>) = delete;
    ULONG       cb;             /* How big the key is */
    BYTE        ab[MAPI_DIM];   /* Key contents */
};
typedef struct NOTIFKEY *LPNOTIFKEY;

#define CbNewNOTIFKEY(cb) (offsetof(NOTIFKEY, ab) + (cb))
#define CbNOTIFKEY(lpkey) (offsetof(NOTIFKEY, ab) + (lpkey)->cb)
#define SizedNOTIFKEY(cb, name) \
struct _NOTIFKEY_ ## name { \
    ULONG       cb; \
	BYTE ab[cb]; \
} name

/* For Subscribe() */
#define NOTIFY_SYNC             ((ULONG) 0x40000000)

/* For Notify() */
#define NOTIFY_CANCELED         ((ULONG) 0x80000000)

/* From the Notification Callback function (well, this is really a ulResult) */
#define CALLBACK_DISCONTINUE    ((ULONG) 0x80000000)

/* For Transport's SpoolerNotify() */
#define NOTIFY_NEWMAIL          ((ULONG) 0x00000001)
#define NOTIFY_READYTOSEND      ((ULONG) 0x00000002)
#define NOTIFY_SENTDEFERRED     ((ULONG) 0x00000004)
#define NOTIFY_CRITSEC          ((ULONG) 0x00001000)
#define NOTIFY_NONCRIT          ((ULONG) 0x00002000)
#define NOTIFY_CONFIG_CHANGE    ((ULONG) 0x00004000)
#define NOTIFY_CRITICAL_ERROR   ((ULONG) 0x10000000)

/* For Message Store's SpoolerNotify() */
#define NOTIFY_NEWMAIL_RECEIVED ((ULONG) 0x20000000)

/* For ModifyStatusRow() */
#define STATUSROW_UPDATE        ((ULONG) 0x10000000)

/* For IStorageFromStream() */
#define STGSTRM_RESET           ((ULONG) 0x00000000)
#define STGSTRM_CURRENT         ((ULONG) 0x10000000)
#define STGSTRM_MODIFY          ((ULONG) 0x00000002)
#define STGSTRM_CREATE          ((ULONG) 0x00001000)

/* For ReadReceipt() */
#define MAPI_NON_READ           ((ULONG) 0x00000001)

/* Preprocessor calls: */

/* PreprocessMessage, first ordinal in RegisterPreprocessor(). */
typedef HRESULT (PREPROCESSMESSAGE)(
                    LPVOID lpvSession,
                    LPMESSAGE lpMessage,
                    LPADRBOOK lpAdrBook,
                    LPMAPIFOLDER lpFolder,
                    ALLOCATEBUFFER *,
                    ALLOCATEMORE *,
                    FREEBUFFER *,
                    ULONG* lpcOutbound,
                    LPMESSAGE** lpppMessage,
                    LPADRLIST* lppRecipList);

/* RemovePreprocessInfo, second ordinal in RegisterPreprocessor(). */
typedef HRESULT (REMOVEPREPROCESSINFO)(LPMESSAGE lpMessage);

class IMAPISupport : public virtual IUnknown {
public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT GetMemAllocRoutines(ALLOCATEBUFFER **, ALLOCATEMORE **, FREEBUFFER **) = 0;
	virtual HRESULT Subscribe(const NOTIFKEY *key, ULONG evt_mask, ULONG flags, IMAPIAdviseSink *, ULONG *conn) = 0;
    virtual HRESULT Unsubscribe(ULONG ulConnection) = 0; 
	virtual HRESULT Notify(const NOTIFKEY *key, ULONG nnotifs, NOTIFICATION *, ULONG *flags) = 0;
	virtual HRESULT ModifyStatusRow(ULONG nvals, const SPropValue *, ULONG flags) = 0;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, ULONG flags, IProfSect **) = 0; 
	virtual HRESULT RegisterPreprocessor(const MAPIUID *, const TCHAR *addrtype, const TCHAR *dllname, const char *preprocess, const char *remove_pp_info, ULONG flags) { return MAPI_E_NO_SUPPORT; }
    virtual HRESULT NewUID(LPMAPIUID lpMuid) = 0; 
	virtual HRESULT MakeInvalid(ULONG ulFlags, LPVOID lpObject, ULONG ulRefCount, ULONG cMethods) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT SpoolerYield(ULONG ulFlags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT SpoolerNotify(ULONG ulFlags, LPVOID lpvData) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT CreateOneOff(const TCHAR *name, const TCHAR *addrtype, const TCHAR *addr, ULONG flags, ULONG *eid_size, ENTRYID **) = 0;
	virtual HRESULT SetProviderUID(const MAPIUID *, ULONG flags) = 0;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) = 0;
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) = 0;
	virtual HRESULT GetOneOffTable(ULONG ulFlags, LPMAPITABLE *lppTable) { return MAPI_E_NOT_FOUND; }
	virtual HRESULT Address(ULONG *lpulUIParam, LPADRPARM lpAdrParms, LPADRLIST *lppAdrList) { return MAPI_E_NOT_FOUND; }
	virtual HRESULT Details(ULONG_PTR *ui_param, DISMISSMODELESS *, void *dismiss_ctx, ULONG cbEntryID, const ENTRYID *lpEntryID, LPFNBUTTON callback, void *btn_ctx, const TCHAR *btn_text, ULONG flags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT NewEntry(ULONG_PTR ui_param, ULONG flags, ULONG cbEIDContainer, const ENTRYID *lpEIDContainer, ULONG cbEIDNewEntryTpl, const ENTRYID *lpEIDNewEntryTpl, ULONG *lpcbEIDNewEntry, ENTRYID **lppEIDNewEntry) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT DoConfigPropsheet(ULONG_PTR ui_param, ULONG flags, const TCHAR *title, IMAPITable *disp_tbl, IMAPIProp *cfg_data, ULONG top_page) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT CopyMessages(const IID *src_intf, void *src_fld, const ENTRYLIST *msglist, const IID *dst_intf, void *dst_fld, ULONG_PTR ui_param, IMAPIProgress *, ULONG flags) = 0;
	virtual HRESULT CopyFolder(const IID *src_intf, void *src_fld, ULONG eid_size, const ENTRYID *eid, const IID *dst_intf, void *dst_fld, const TCHAR *newname, ULONG_PTR ui_param, IMAPIProgress *, ULONG flags) = 0;
	virtual HRESULT DoCopyTo(LPCIID lpSrcInterface, LPVOID lpSrcObj, ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, IMAPIProgress *, LPCIID lpDestInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) = 0;
	virtual HRESULT DoCopyProps(LPCIID lpSrcInterface, LPVOID lpSrcObj, const SPropTagArray *lpIncludeProps, ULONG ulUIParam, IMAPIProgress *, LPCIID lpDestInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) = 0;
	virtual HRESULT DoProgressDialog(ULONG ulUIParam, ULONG ulFlags, IMAPIProgress **) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT ReadReceipt(ULONG ulFlags, LPMESSAGE lpReadMessage, LPMESSAGE *lppEmptyMessage) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT PrepareSubmit(LPMESSAGE lpMessage, ULONG *lpulFlags) { return MAPI_E_NO_SUPPORT; }
    virtual HRESULT ExpandRecips(LPMESSAGE lpMessage, ULONG * lpulFlags) = 0; 
	virtual HRESULT UpdatePAB(ULONG ulFlags, LPMESSAGE lpMessage) { return MAPI_E_NO_SUPPORT; }
    virtual HRESULT DoSentMail(ULONG ulFlags, LPMESSAGE lpMessage) = 0; 
	virtual HRESULT OpenAddressBook(LPCIID lpInterface, ULONG ulFlags, LPADRBOOK *lppAdrBook) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT Preprocess(ULONG flags, ULONG cbEntryID, const ENTRYID *lpEntryID) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT CompleteMsg(ULONG flags, ULONG cbEntryID, const ENTRYID *lpEntryID) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT StoreLogoffTransports(ULONG *lpulFlags) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT StatusRecips(IMessage *, const ADRLIST *recips) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT WrapStoreEntryID(ULONG cbOrigEntry, const ENTRYID *lpOrigEntry, ULONG *lpcbWrappedEntry,ENTRYID **lppWrappedEntry) = 0;
    virtual HRESULT ModifyProfile(ULONG ulFlags) = 0; 
	virtual HRESULT IStorageFromStream(IUnknown *, const IID *intf, unsigned int flags, IStorage **) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT GetSvcConfigSupportObj(unsigned int flags, IMAPISupport **) { return MAPI_E_NO_SUPPORT; }
};

/********************************************************************/
/*                                                                  */
/*                          ADDRESS BOOK SPI                        */
/*                                                                  */
/********************************************************************/

/* Address Book Provider ------------------------------------------------- */

/* OpenTemplateID() */
#define FILL_ENTRY              ((ULONG) 0x00000001)

/* For Logon() */

class IABProvider;
typedef IABProvider* LPABPROVIDER;

class IABLogon;
typedef IABLogon* LPABLOGON;

class IABProvider : public virtual IUnknown {
public:
    virtual HRESULT Shutdown(ULONG * lpulFlags) = 0; 
	virtual HRESULT Logon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profname, unsigned int flags, unsigned int *secsize, BYTE **sec, MAPIERROR **, IABLogon **) = 0;
};

class IABLogon : public virtual IUnknown {
public: 
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR * lppMAPIError) = 0; 
    virtual HRESULT Logoff(ULONG ulFlags) = 0; 
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) = 0;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT Advise(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) = 0;
	virtual HRESULT Unadvise(ULONG ulConnection) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPMAPISTATUS *lppEntry) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT OpenTemplateID(ULONG tpl_size, const ENTRYID *tpl_eid, ULONG tpl_flags, IMAPIProp *propdata, const IID *intf, IMAPIProp **propnew, IMAPIProp *sibling) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT GetOneOffTable(ULONG ulFlags, LPMAPITABLE *lppTable) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT PrepareRecips(ULONG ulFlags, const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList) = 0;
};

extern "C" {
typedef HRESULT (ABPROVIDERINIT)(
    HINSTANCE           hInstance,
    IMalloc *,
    ALLOCATEBUFFER *,
    ALLOCATEMORE *,
    FREEBUFFER *,
    ULONG               ulFlags,
    ULONG               ulMAPIVer,
    ULONG *         lpulProviderVer,
    IABProvider **
);

KC_EXPORT ABPROVIDERINIT ABProviderInit;
}

/********************************************************************/
/*                                                                  */
/*                          TRANSPORT SPI                           */
/*                                                                  */
/********************************************************************/

/* For DeinitTransport */
#define DEINIT_NORMAL               ((ULONG) 0x00000001)
#define DEINIT_HURRY                ((ULONG) 0x80000000)

/* For TransportLogon */
/* Flags that the Spooler may pass to the transport: */
#define LOGON_NO_DIALOG             ((ULONG) 0x00000001)
#define LOGON_NO_CONNECT            ((ULONG) 0x00000004)
#define LOGON_NO_INBOUND            ((ULONG) 0x00000008)
#define LOGON_NO_OUTBOUND           ((ULONG) 0x00000010)
/*#define MAPI_UNICODE              ((ULONG) 0x80000000) in mapidefs.h */

/* Flags that the transport may pass to the Spooler: */
#define LOGON_SP_IDLE               ((ULONG) 0x00010000)
#define LOGON_SP_POLL               ((ULONG) 0x00020000)
#define LOGON_SP_RESOLVE            ((ULONG) 0x00040000)

class IXPProvider;
typedef IXPProvider* LPXPPROVIDER;

class IXPLogon;
typedef IXPLogon* LPXPLOGON;

class IXPProvider : public virtual IUnknown {
public: 
    virtual HRESULT Shutdown(ULONG * lpulFlags) = 0; 
	virtual HRESULT TransportLogon(IMAPISupport *, unsigned int ui_param, const TCHAR *profile, unsigned int *flags, MAPIERROR **, IXPLogon **) = 0;
};

/* OptionData returned from call to RegisterOptions */
#define OPTION_TYPE_RECIPIENT       ((ULONG) 0x00000001)
#define OPTION_TYPE_MESSAGE         ((ULONG) 0x00000002)

struct OPTIONDATA {
    ULONG           ulFlags;        /* MAPI_RECIPIENT, MAPI_MESSAGE */
    LPGUID          lpRecipGUID;    /* Same as returned by AddressTypes() */
    LPTSTR          lpszAdrType;    /* Same as returned by AddressTypes() */
    LPTSTR          lpszDLLName;    /* Options DLL */
    ULONG           ulOrdinal;      /* Ordinal in that DLL */
    ULONG           cbOptionsData;  /* Count of bytes in lpbOptionsData */
    LPBYTE          lpbOptionsData; /* Providers per [recip|message] option data */
    ULONG           cOptionsProps;  /* Count of Options default prop values */
    LPSPropValue    lpOptionsProps; /* Default Options property values */
};
typedef struct OPTIONDATA *LPOPTIONDATA;

typedef SCODE (OPTIONCALLBACK)(
            HINSTANCE           hInst,
            IMalloc *,
            ULONG               ulFlags,
            ULONG               cbOptionData,
            LPBYTE              lpbOptionData,
            IMAPISupport *,
            LPMAPIPROP          lpDataSource,
            LPMAPIPROP *    lppWrappedSource,
            LPMAPIERROR *   lppMAPIError);

/* For TransportNotify */
#define NOTIFY_ABORT_DEFERRED       ((ULONG) 0x40000000)
#define NOTIFY_CANCEL_MESSAGE       ((ULONG) 0x80000000)
#define NOTIFY_BEGIN_INBOUND        ((ULONG) 0x00000001)
#define NOTIFY_END_INBOUND          ((ULONG) 0x00010000)
#define NOTIFY_BEGIN_OUTBOUND       ((ULONG) 0x00000002)
#define NOTIFY_END_OUTBOUND         ((ULONG) 0x00020000)
#define NOTIFY_BEGIN_INBOUND_FLUSH  ((ULONG) 0x00000004)
#define NOTIFY_END_INBOUND_FLUSH    ((ULONG) 0x00040000)
#define NOTIFY_BEGIN_OUTBOUND_FLUSH ((ULONG) 0x00000008)
#define NOTIFY_END_OUTBOUND_FLUSH   ((ULONG) 0x00080000)

/* For TransportLogoff */
#define LOGOFF_NORMAL               ((ULONG) 0x00000001)
#define LOGOFF_HURRY                ((ULONG) 0x80000000)

/* For SubmitMessage */
#define BEGIN_DEFERRED              ((ULONG) 0x00000001)

/* For EndMessage */
/* Flags that the Spooler may pass to the Transport: */

/* Flags that the transport may pass to the Spooler: */
#define END_RESEND_NOW              ((ULONG) 0x00010000)
#define END_RESEND_LATER            ((ULONG) 0x00020000)
#define END_DONT_RESEND             ((ULONG) 0x00040000)

class IXPLogon : public virtual IUnknown {
public: 
    virtual HRESULT AddressTypes(ULONG * lpulFlags, ULONG * lpcAdrType, LPTSTR** lpppAdrTypeArray,
				   ULONG * lpcMAPIUID, LPMAPIUID * * lpppUIDArray) = 0; 
    virtual HRESULT RegisterOptions(ULONG * lpulFlags, ULONG * lpcOptions, LPOPTIONDATA * lppOptions) = 0; 
    virtual HRESULT TransportNotify(ULONG * lpulFlags, LPVOID * lppvData) = 0; 
    virtual HRESULT Idle(ULONG ulFlags) = 0; 
    virtual HRESULT TransportLogoff(ULONG ulFlags) = 0; 
    virtual HRESULT SubmitMessage(ULONG ulFlags, LPMESSAGE lpMessage, ULONG * lpulMsgRef, ULONG * lpulReturnParm) = 0; 
    virtual HRESULT EndMessage(ULONG ulMsgRef, ULONG * lpulFlags) = 0; 
    virtual HRESULT Poll(ULONG * lpulIncoming) = 0; 
    virtual HRESULT StartMessage(ULONG ulFlags, LPMESSAGE lpMessage, ULONG * lpulMsgRef) = 0; 
    virtual HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG * lpulObjType, LPMAPISTATUS * lppEntry) = 0; 
    virtual HRESULT ValidateState(ULONG ulUIParam, ULONG ulFlags) = 0; 
    virtual HRESULT FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags) = 0;
};

/* Transport Provider Entry Point */
typedef HRESULT (XPPROVIDERINIT)(
    HINSTANCE           hInstance,
    IMalloc *,
    ALLOCATEBUFFER *,
    ALLOCATEMORE *,
    FREEBUFFER *,
    ULONG               ulFlags,
    ULONG               ulMAPIVer,
    ULONG *         lpulProviderVer,
    LPXPPROVIDER *  lppXPProvider);

/********************************************************************/
/*                                                                  */
/*                          MESSAGE STORE SPI                       */
/*                                                                  */
/********************************************************************/

/* Flags and enums */
/* GetCredentials, SetCredentials */
#define LOGON_SP_TRANSPORT      ((ULONG) 0x00000001)
#define LOGON_SP_PROMPT         ((ULONG) 0x00000002)
#define LOGON_SP_NEWPW          ((ULONG) 0x00000004)
#define LOGON_CHANGED           ((ULONG) 0x00000008)

/* DoMCDialog */
#define DIALOG_FOLDER           ((ULONG) 0x00000001)
#define DIALOG_MESSAGE          ((ULONG) 0x00000002)
#define DIALOG_PROP             ((ULONG) 0x00000004)
#define DIALOG_ATTACH           ((ULONG) 0x00000008)

#define DIALOG_MOVE             ((ULONG) 0x00000010)
#define DIALOG_COPY             ((ULONG) 0x00000020)
#define DIALOG_DELETE           ((ULONG) 0x00000040)

#define DIALOG_ALLOW_CANCEL     ((ULONG) 0x00000080)
#define DIALOG_CONFIRM_CANCEL   ((ULONG) 0x00000100)

/* ExpandRecips */
#define NEEDS_PREPROCESSING     ((ULONG) 0x00000001)
#define NEEDS_SPOOLER           ((ULONG) 0x00000002)

/* PrepareSubmit */
#define CHECK_SENDER            ((ULONG) 0x00000001)
#define NON_STANDARD            ((ULONG) 0x00010000)

class IMSLogon;
typedef IMSLogon* LPMSLOGON;

class IMSProvider;
typedef IMSProvider* LPMSPROVIDER;

/* Message Store Provider Interface (IMSPROVIDER) */
class IMSProvider : public virtual IUnknown {
public: 
    virtual HRESULT Shutdown(ULONG * lpulFlags) = 0; 
	virtual HRESULT Logon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG eid_size, const ENTRYID *eid, ULONG flags, const IID *intf, ULONG *ssec_size, BYTE **spool_sec, MAPIERROR **, IMSLogon **, IMsgStore **) = 0;
	virtual HRESULT SpoolerLogon(IMAPISupport *, ULONG_PTR ui_param, const TCHAR *profile, ULONG eid_size, const ENTRYID *eid, ULONG flags, const IID *intf, ULONG ssec_size, const BYTE *spool_sec, MAPIERROR **, IMSLogon **, IMsgStore **) { return MAPI_E_NO_SUPPORT; }
	virtual HRESULT CompareStoreIDs(ULONG eid1_size, const ENTRYID *eid1, ULONG eid2_size, const ENTRYID *eid2, ULONG flags, ULONG *result) = 0;
};

/* The MSLOGON object is returned by the Logon() method of the
 * MSPROVIDER interface.  This object is for use by MAPIX.DLL.
 */
class IMSLogon : public virtual IUnknown {
public: 
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR * lppMAPIError) = 0; 
    virtual HRESULT Logoff(ULONG * lpulFlags) = 0; 
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) = 0;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) = 0;
	virtual HRESULT Advise(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) = 0;
    virtual HRESULT Unadvise(ULONG ulConnection) = 0; 
    virtual HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG * lpulObjType, LPVOID * lppEntry) = 0;
};

/* Message Store Provider Entry Point */

extern "C" {
typedef HRESULT (MSPROVIDERINIT)(
    HINSTANCE               hInstance,
    IMalloc *, /* AddRef() if you keep it */
    ALLOCATEBUFFER *, /* -> AllocateBuffer */
    ALLOCATEMORE *, /* -> AllocateMore */
    FREEBUFFER *, /* -> FreeBuffer */
    ULONG                   ulFlags,
    ULONG                   ulMAPIVer,
    ULONG *             lpulProviderVer,
	IMSProvider **
);

KC_EXPORT MSPROVIDERINIT MSProviderInit;
}

/********************************************************************/
/*                                                                  */
/*                    MESSAGE SERVICE CONFIGURATION                 */
/*                                                                  */
/********************************************************************/

/* Flags for service configuration entry point */
#define MSG_SERVICE_UI_READ_ONLY     0x00000008 /* display parameters only */
#define SERVICE_LOGON_FAILED         0x00000020 /* reconfigure provider */

/* Contexts for service configuration entry point */
#define MSG_SERVICE_INSTALL         0x00000001
#define MSG_SERVICE_CREATE          0x00000002
#define MSG_SERVICE_CONFIGURE       0x00000003
#define MSG_SERVICE_DELETE          0x00000004
#define MSG_SERVICE_UNINSTALL       0x00000005
#define MSG_SERVICE_PROVIDER_CREATE 0x00000006
#define MSG_SERVICE_PROVIDER_DELETE 0x00000007

/* Prototype for service configuration entry point */
extern "C" {
typedef HRESULT (MSGSERVICEENTRY)(
    HINSTANCE       hInstance,
    IMalloc *,
    IMAPISupport *,
    ULONG           ulUIParam,
    ULONG           ulFlags,
    ULONG           ulContext,
    ULONG           cValues,
    LPSPropValue    lpProps,
    IProviderAdmin *,
    LPMAPIERROR *lppMapiError
);

typedef MSGSERVICEENTRY *LPMSGSERVICEENTRY;
}
