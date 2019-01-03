/* SPDX-License-Identifier: AGPL-3.0-only */
%module mapidefs

%{
#undef LOCK_WRITE
#undef LOCK_EXCLUSIVE
#include <mapidefs.h>
%}

#define MAPI_MODIFY			(0x00000001)

#define MAPI_ACCESS_MODIFY		(0x00000001)
#define MAPI_ACCESS_READ		(0x00000002)
#define MAPI_ACCESS_DELETE		(0x00000004)
#define MAPI_ACCESS_CREATE_HIERARCHY	(0x00000008)
#define MAPI_ACCESS_CREATE_CONTENTS	(0x00000010)
#define MAPI_ACCESS_CREATE_ASSOCIATED	(0x00000020)

%constant unsigned int MAPI_UNICODE = 0x80000000;

#define hrSuccess		0

#define MAPI_SHORTTERM		0x80
#define MAPI_NOTRECIP		0x40
#define MAPI_THISSESSION	0x20
#define MAPI_NOW		0x10
#define MAPI_NOTRESERVED	0x08

#define MAPI_COMPOUND		0x80

#define MAPI_ONE_OFF_UID { 0x81, 0x2b, 0x1f, 0xa4, 0xbe, 0xa3, 0x10, 0x19, \
                           0x9d, 0x6e, 0x00, 0xdd, 0x01, 0x0f, 0x54, 0x02 }
#define MAPI_ONE_OFF_UNICODE        0x8000
#define MAPI_ONE_OFF_NO_RICH_INFO   0x0001

#define MAPI_STORE      (0x00000001)
#define MAPI_ADDRBOOK   (0x00000002)
#define MAPI_FOLDER     (0x00000003)
#define MAPI_ABCONT     (0x00000004)
#define MAPI_MESSAGE    (0x00000005)
#define MAPI_MAILUSER   (0x00000006)
#define MAPI_ATTACH     (0x00000007)
#define MAPI_DISTLIST   (0x00000008)
#define MAPI_PROFSECT   (0x00000009)
#define MAPI_STATUS     (0x0000000A)
#define MAPI_SESSION    (0x0000000B)
#define MAPI_FORMINFO   (0x0000000C)

#define MV_FLAG         0x1000

#define PT_UNSPECIFIED  (0x0000)
#define PT_NULL         (0x0001)
#define PT_SHORT        (0x0002)
#define PT_LONG         (0x0003)
#define PT_FLOAT        (0x0004)
#define PT_DOUBLE       (0x0005)
#define PT_CURRENCY     (0x0006)
#define PT_APPTIME      (0x0007)
#define PT_ERROR        (0x000A)
#define PT_BOOLEAN      (0x000B)
#define PT_OBJECT       (0x000D)
#define PT_LONGLONG     (0x0014)
#define PT_STRING8      (0x001E)
#define PT_UNICODE      (0x001F)
#define PT_SYSTIME      (0x0040)
#define PT_CLSID        (0x0048)
#define PT_SVREID       (0x00FB)
#define PT_SRESTRICT    (0x00FD)
#define PT_ACTIONS      (0x00FE)
#define PT_BINARY       (0x0102)

#define PT_I2	PT_SHORT
#define PT_I4	PT_LONG
#define PT_R4	PT_FLOAT
#define PT_R8	PT_DOUBLE
#define PT_I8	PT_LONGLONG

#define PT_MV_SHORT     (MV_FLAG|PT_SHORT)
#define PT_MV_LONG      (MV_FLAG|PT_LONG)
#define PT_MV_FLOAT     (MV_FLAG|PT_FLOAT)
#define PT_MV_DOUBLE    (MV_FLAG|PT_DOUBLE)
#define PT_MV_CURRENCY  (MV_FLAG|PT_CURRENCY)
#define PT_MV_APPTIME   (MV_FLAG|PT_APPTIME)
#define PT_MV_SYSTIME   (MV_FLAG|PT_SYSTIME)
#define PT_MV_STRING8   (MV_FLAG|PT_STRING8)
#define PT_MV_BINARY    (MV_FLAG|PT_BINARY)
#define PT_MV_UNICODE   (MV_FLAG|PT_UNICODE)
#define PT_MV_CLSID     (MV_FLAG|PT_CLSID)
#define PT_MV_LONGLONG  (MV_FLAG|PT_LONGLONG)

#define PT_MV_I2     PT_MV_SHORT
#define PT_MV_I4     PT_MV_LONG
#define PT_MV_R4     PT_MV_FLOAT
#define PT_MV_R8     PT_MV_DOUBLE
#define PT_MV_I8     PT_MV_LONGLONG

#define MV_INSTANCE     0x2000
#define MVI_FLAG        (MV_FLAG | MV_INSTANCE)
#define MVI_PROP(tag)   ((tag) | MVI_FLAG)

#define MAPI_ERROR_VERSION      0x00000000L

#define KEEP_OPEN_READONLY      (0x00000001)
#define KEEP_OPEN_READWRITE     (0x00000002)
#define FORCE_SAVE              (0x00000004)
#define MAPI_CREATE             (0x00000002)
#define STREAM_APPEND           (0x00000004)
#define MAPI_MOVE               (0x00000001)
#define MAPI_NOREPLACE          (0x00000002)
#define MAPI_DECLINE_OK         (0x00000004)

#define MAPI_NO_STRINGS         (0x00000001)
#define MAPI_NO_IDS             (0x00000002)

#define MNID_ID                 0
#define MNID_STRING             1

class IMAPIProp : public virtual IUnknown {
public:
    //    virtual ~IMAPIProp() = 0;

    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
    virtual HRESULT SaveChanges(ULONG ulFlags) = 0;
	virtual HRESULT GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags, ULONG *OUTPUTC /*lpcValues*/, LPSPropValue *OUTPUTP /*lppPropArray*/) = 0;
    virtual HRESULT GetPropList(ULONG ulFlags, LPSPropTagArray* OUTPUT /*lppPropTagArray*/) = 0;
    virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID USE_IID_FOR_OUTPUT, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN* OUTPUT_USE_IID /*lppUnk*/) = 0;
	virtual HRESULT SetProps(ULONG cValues, const SPropValue *lpProps, LPSPropProblemArray *OUTPUT /*lppProblems*/) = 0;
	virtual HRESULT DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *OUTPUT /*lppProblems*/) = 0;
	virtual HRESULT CopyTo(ULONG cInterfaces, LPCIID lpInterfaces, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, IMAPIProgress *lpProgress, LPCIID lpInterface, void *lpDestObj, ULONG ulFlags, LPSPropProblemArray *OUTPUT /*lppProblems*/) = 0;
	virtual HRESULT CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, IMAPIProgress *lpProgress, LPCIID lpInterface, void *lpDestObj, ULONG ulFlags, LPSPropProblemArray *OUTPUT /*lppProblems*/) = 0;
	virtual HRESULT GetNamesFromIDs(LPSPropTagArray *lppPropTags, const GUID *lpPropSetGuid, ULONG ulFlags, ULONG *OUTPUTC, LPMAPINAMEID **OUTPUTP /*lpppPropNames*/) = 0;
    virtual HRESULT GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID* lppPropNames, ULONG ulFlags, LPSPropTagArray* OUTPUT /*lppPropTags*/) = 0;
	%extend {
		~IMAPIProp() { self->Release(); }
	}
};

#define MAPI_BEST_ACCESS        (0x00000010)
#define CONVENIENT_DEPTH        (0x00000001)
#define SEARCH_RUNNING          (0x00000001)
#define SEARCH_REBUILD          (0x00000002)
#define SEARCH_RECURSIVE        (0x00000004)
#define SEARCH_FOREGROUND       (0x00000008)
#define STOP_SEARCH             (0x00000001)
#define RESTART_SEARCH          (0x00000002)
#define RECURSIVE_SEARCH        (0x00000004)
#define SHALLOW_SEARCH          (0x00000008)
#define FOREGROUND_SEARCH       (0x00000010)
#define BACKGROUND_SEARCH       (0x00000020)

class IMAPIContainer : public virtual IMAPIProp {
public:
    //    virtual ~IMAPIContainer() = 0;

    virtual HRESULT GetContentsTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
    virtual HRESULT GetHierarchyTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
    virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* OUTPUT /* lpulObjType */,
			      IUnknown ** OUTPUT /*lppUnk*/) = 0;
    virtual HRESULT SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags) = 0;
    virtual HRESULT GetSearchCriteria(ULONG ulFlags, LPSRestriction* OUTPUT /*lppRestriction*/, LPENTRYLIST* OUTPUT /*lppContainerList*/,
				      ULONG* OUTPUT /*lpulSearchState*/) = 0;
	%extend {
		~IMAPIContainer() { self->Release(); }
	}
};

#define fnevCriticalError           (0x00000001)
#define fnevNewMail                 (0x00000002)
#define fnevObjectCreated           (0x00000004)
#define fnevObjectDeleted           (0x00000008)
#define fnevObjectModified          (0x00000010)
#define fnevObjectMoved             (0x00000020)
#define fnevObjectCopied            (0x00000040)
#define fnevSearchComplete          (0x00000080)
#define fnevTableModified           (0x00000100)
#define fnevStatusObjectModified    (0x00000200)
#define fnevReservedForMapi         (0x40000000)
#define fnevExtended                (0x80000000)

#define TABLE_CHANGED       1
#define TABLE_ERROR         2
#define TABLE_ROW_ADDED     3
#define TABLE_ROW_DELETED   4
#define TABLE_ROW_MODIFIED  5
#define TABLE_SORT_DONE     6
#define TABLE_RESTRICT_DONE 7
#define TABLE_SETCOL_DONE   8
#define TABLE_RELOAD        9

/* Interface used for registering and issuing notification callbacks. */
class IMAPIAdviseSink : public IUnknown {
public:
    virtual ULONG OnNotify(ULONG cNotif, LPNOTIFICATION lpNotifications) = 0;
	%extend {
		~IMAPIAdviseSink() { self->Release(); }
	}
};

#if SWIGPYTHON

%{
#include <kopano/swig_iunknown.h>
typedef IUnknownImplementor<IMAPIAdviseSink> MAPIAdviseSink;
typedef IUnknownImplementor<IMAPIProp> MAPIProp;
typedef IUnknownImplementor<IMessage> Message;
typedef IUnknownImplementor<IAttach> Attach;
typedef IUnknownImplementor<IMAPITable> MAPITable;
%}

%feature("director") MAPIAdviseSink;
%feature("nodirector") MAPIAdviseSink::QueryInterface;
class MAPIAdviseSink : public IMAPIAdviseSink {
public:
	MAPIAdviseSink(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~MAPIAdviseSink() { delete self; }
	}
};

#endif // SWIGPYTHON

#define STORE_ENTRYID_UNIQUE    (0x00000001)
#define STORE_READONLY          (0x00000002)
#define STORE_SEARCH_OK         (0x00000004)
#define STORE_MODIFY_OK         (0x00000008)
#define STORE_CREATE_OK         (0x00000010)
#define STORE_ATTACH_OK         (0x00000020)
#define STORE_OLE_OK            (0x00000040)
#define STORE_SUBMIT_OK         (0x00000080)
#define STORE_NOTIFY_OK         (0x00000100)
#define STORE_MV_PROPS_OK       (0x00000200)
#define STORE_CATEGORIZE_OK     (0x00000400)
#define STORE_RTF_OK            (0x00000800)
#define STORE_RESTRICTION_OK    (0x00001000)
#define STORE_SORT_OK           (0x00002000)
#define STORE_PUBLIC_FOLDERS    (0x00004000)
#define STORE_UNCOMPRESSED_RTF  (0x00008000)
#define STORE_HAS_SEARCHES      (0x01000000)
#define LOGOFF_NO_WAIT          (0x00000001)
#define LOGOFF_ORDERLY          (0x00000002)
#define LOGOFF_PURGE            (0x00000004)
#define LOGOFF_ABORT            (0x00000008)
#define LOGOFF_QUIET            (0x00000010)

#define LOGOFF_COMPLETE         (0x00010000)
#define LOGOFF_INBOUND          (0x00020000)
#define LOGOFF_OUTBOUND         (0x00040000)
#define LOGOFF_OUTBOUND_QUEUE   (0x00080000)
#define MSG_LOCKED              (0x00000001)
#define MSG_UNLOCKED            (0x00000000)
#define FOLDER_IPM_SUBTREE_VALID        (0x00000001)
#define FOLDER_IPM_INBOX_VALID          (0x00000002)
#define FOLDER_IPM_OUTBOX_VALID         (0x00000004)
#define FOLDER_IPM_WASTEBASKET_VALID    (0x00000008)
#define FOLDER_IPM_SENTMAIL_VALID       (0x00000010)
#define FOLDER_VIEWS_VALID              (0x00000020)
#define FOLDER_COMMON_VIEWS_VALID       (0x00000040)
#define FOLDER_FINDER_VALID             (0x00000080)

class IMsgStore : public IMAPIProp {
public:
    //    virtual ~IMsgStore() = 0;

    virtual HRESULT Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink,
			   ULONG* OUTPUT /*lpulConnection*/) = 0;
    virtual HRESULT Unadvise(ULONG ulConnection) = 0;
	virtual HRESULT CompareEntryIDs(ULONG cbEntryID1, const ENTRYID *lpEntryID1, ULONG cbEntryID2, const ENTRYID *lpEntryID2, ULONG ulFlags, ULONG *OUTPUT /*lpulResult*/) = 0;
    virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* OUTPUT /*lpulObjType*/,
			      IUnknown ** OUTPUT /*lppUnk*/) = 0;
    virtual HRESULT SetReceiveFolder(LPTSTR lpszMessageClass, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID) = 0;
    virtual HRESULT GetReceiveFolder(LPTSTR lpszMessageClass, ULONG ulFlags, ULONG *OUTPUT, LPENTRYID* OUTPUT /*lppEntryID*/,
				     LPTSTR* OUTPUT /*lppszExplicitClass*/) = 0;
    virtual HRESULT GetReceiveFolderTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
    virtual HRESULT StoreLogoff(ULONG * INOUT /*lpulFlags*/) = 0;
    virtual HRESULT AbortSubmit(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags) = 0;
    virtual HRESULT GetOutgoingQueue(ULONG ulFlags, IMAPITable ** OUTPUT/*lppTable*/) = 0;
    virtual HRESULT SetLockState(IMessage *lpMessage, ULONG ulLockState) = 0;
    virtual HRESULT FinishedMsg(ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID) = 0;
    virtual HRESULT NotifyNewMail(LPNOTIFICATION lpNotification) = 0;

	%extend {
		~IMsgStore() { self->Release(); }
	}
};

class IProxyStoreObject : public virtual IUnknown {
public:
    %extend {
        ~IProxyStoreObject() { self->Release(); }

        virtual HRESULT UnwrapNoRef(IUnknown **OUTPUT /*ppvObject*/) {
            HRESULT hr = hrSuccess;
			hr = self->UnwrapNoRef(reinterpret_cast<void **>(OUTPUT));
			if (hr == hrSuccess)
                (*OUTPUT)->AddRef();
			return hr;
        };

    }
};

#define FOLDER_ROOT             (0x00000000)
#define FOLDER_GENERIC          (0x00000001)
#define FOLDER_SEARCH           (0x00000002)
#define MESSAGE_MOVE            (0x00000001)
#define MESSAGE_DIALOG          (0x00000002)
#define OPEN_IF_EXISTS          (0x00000001)
#define DEL_MESSAGES            (0x00000001)
#define FOLDER_DIALOG           (0x00000002)
#define DEL_FOLDERS             (0x00000004)
#define DEL_ASSOCIATED          (0x00000008)
#define FOLDER_MOVE             (0x00000001)
#define COPY_SUBFOLDERS         (0x00000010)
#define MSGSTATUS_HIGHLIGHTED   (0x00000001)
#define MSGSTATUS_TAGGED        (0x00000002)
#define MSGSTATUS_HIDDEN        (0x00000004)
#define MSGSTATUS_DELMARKED     (0x00000008)
#define MSGSTATUS_REMOTE_DOWNLOAD   (0x00001000)
#define MSGSTATUS_REMOTE_DELETE     (0x00002000)
#define RECURSIVE_SORT          (0x00000002)
#define FLDSTATUS_HIGHLIGHTED   (0x00000001)
#define FLDSTATUS_TAGGED        (0x00000002)
#define FLDSTATUS_HIDDEN        (0x00000004)
#define FLDSTATUS_DELMARKED     (0x00000008)

class IMAPIFolder : public virtual IMAPIContainer {
public:
    //    virtual ~IMAPIFolder() = 0;

    virtual HRESULT CreateMessage(LPCIID lpInterface, ULONG ulFlags, IMessage ** OUTPUT /*lppMessage*/) = 0;
	virtual HRESULT CopyMessages(LPENTRYLIST lpMsgList, LPCIID lpInterface, void *lpDestFolder, ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT DeleteMessages(LPENTRYLIST lpMsgList, ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT CreateFolder(ULONG ulFolderType, LPTSTR lpszFolderName, LPTSTR lpszFolderComment, LPCIID lpInterface,
				 ULONG ulFlags, IMAPIFolder** OUTPUT /*lppFolder*/) = 0;
    virtual HRESULT CopyFolder(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, void *lpDestFolder, LPTSTR lpszNewFolderName,
			       ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT DeleteFolder(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT SetReadFlags(LPENTRYLIST lpMsgList, ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT GetMessageStatus(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags, ULONG* OUTPUT /*lpulMessageStatus*/) = 0;
    virtual HRESULT SetMessageStatus(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulNewStatus, ULONG ulNewStatusMask,
				     ULONG* OUTPUT /*lpulOldStatus*/) = 0;
    virtual HRESULT SaveContentsSort(const SSortOrderSet *lpSortCriteria, ULONG ulFlags) = 0;
    virtual HRESULT EmptyFolder(ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
	%extend {
		~IMAPIFolder() { self->Release(); }
	}
};

#define FORCE_SUBMIT                (0x00000001)
#define MSGFLAG_READ            (0x00000001)
#define MSGFLAG_UNMODIFIED      (0x00000002)
#define MSGFLAG_SUBMIT          (0x00000004)
#define MSGFLAG_UNSENT          (0x00000008)
#define MSGFLAG_HASATTACH       (0x00000010)
#define MSGFLAG_FROMME          (0x00000020)
#define MSGFLAG_ASSOCIATED      (0x00000040)
#define MSGFLAG_RESEND          (0x00000080)
#define MSGFLAG_RN_PENDING      (0x00000100)
#define MSGFLAG_NRN_PENDING     (0x00000200)
#define SUBMITFLAG_LOCKED       (0x00000001)
#define SUBMITFLAG_PREPROCESS   (0x00000002)
#define MODRECIP_ADD            (0x00000002)
#define MODRECIP_MODIFY         (0x00000004)
#define MODRECIP_REMOVE         (0x00000008)
#define SUPPRESS_RECEIPT        (0x00000001)
#define CLEAR_READ_FLAG         (0x00000004)
#define GENERATE_RECEIPT_ONLY   (0x00000010)
#define CLEAR_RN_PENDING        (0x00000020)
#define CLEAR_NRN_PENDING       (0x00000040)
#define ATTACH_DIALOG           (0x00000001)
#define SECURITY_SIGNED         (0x00000001)
#define SECURITY_ENCRYPTED      (0x00000002)
#define PRIO_URGENT             ( 1)
#define PRIO_NORMAL             ( 0)
#define PRIO_NONURGENT          (-1)
#define SENSITIVITY_NONE                    (0x00000000)
#define SENSITIVITY_PERSONAL                (0x00000001)
#define SENSITIVITY_PRIVATE                 (0x00000002)
#define SENSITIVITY_COMPANY_CONFIDENTIAL    (0x00000003)
#define IMPORTANCE_LOW          (0)
#define IMPORTANCE_NORMAL       (1)
#define IMPORTANCE_HIGH         (2)

class IMessage : public virtual IMAPIProp {
public:
    //    virtual ~IMessage() = 0;

    virtual HRESULT GetAttachmentTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
    virtual HRESULT OpenAttach(ULONG ulAttachmentNum, LPCIID lpInterface, ULONG ulFlags, IAttach** OUTPUT /*lppAttach*/) = 0;
    virtual HRESULT CreateAttach(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulAttachmentNum, IAttach** OUTPUT /*lppAttach*/) = 0;
    virtual HRESULT DeleteAttach(ULONG ulAttachmentNum, ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT GetRecipientTable(ULONG ulFlags, IMAPITable ** OUTPUT /*lppTable*/) = 0;
	virtual HRESULT ModifyRecipients(ULONG ulFlags, const ADRLIST *INPUT /*lpMods*/) = 0;
    virtual HRESULT SubmitMessage(ULONG ulFlags) = 0;
    virtual HRESULT SetReadFlag(ULONG ulFlags) = 0;
	%extend {
		~IMessage() { self->Release(); }
	}
};

#define NO_ATTACHMENT           (0x00000000)
#define ATTACH_BY_VALUE         (0x00000001)
#define ATTACH_BY_REFERENCE     (0x00000002)
#define ATTACH_BY_REF_RESOLVE   (0x00000003)
#define ATTACH_BY_REF_ONLY      (0x00000004)
#define ATTACH_EMBEDDED_MSG     (0x00000005)
#define ATTACH_OLE              (0x00000006)

class IAttach : public virtual IMAPIProp {
public:
	%extend {
		~IAttach() { self->Release(); }
	}
};

#define AB_RECIPIENTS           (0x00000001)
#define AB_SUBCONTAINERS        (0x00000002)
#define AB_MODIFIABLE           (0x00000004)
#define AB_UNMODIFIABLE         (0x00000008)
#define AB_FIND_ON_OPEN         (0x00000010)
#define AB_NOT_DEFAULT          (0x00000020)
#define CREATE_CHECK_DUP_STRICT (0x00000001)
#define CREATE_CHECK_DUP_LOOSE  (0x00000002)
#define CREATE_REPLACE          (0x00000004)
#define MAPI_UNRESOLVED         (0x00000000)
#define MAPI_AMBIGUOUS          (0x00000001)
#define MAPI_RESOLVED           (0x00000002)

class IABContainer : public virtual IMAPIContainer {
public:
	virtual HRESULT CreateEntry(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG flags, IMAPIProp **OUTPUT /*lppMAPIPropEntry*/) = 0;
    virtual HRESULT CopyEntries(LPENTRYLIST lpEntries, ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT DeleteEntries(LPENTRYLIST lpEntries, ULONG ulFlags) = 0;
	virtual HRESULT ResolveNames(const SPropTagArray *lpPropTagArray, ULONG ulFlags, LPADRLIST INOUT /*lpAdrList*/, LPFlagList INOUT /*lpFlagList*/) = 0;
	%extend {
		~IABContainer() { self->Release(); }
	}
};

#define MAPI_SEND_NO_RICH_INFO      (0x00010000)

#define MAPI_DIAG(_code)    ((LONG) _code)

#define MAPI_DIAG_NO_DIAGNOSTIC                     MAPI_DIAG( -1 )
#define MAPI_DIAG_OR_NAME_UNRECOGNIZED              MAPI_DIAG( 0 )
#define MAPI_DIAG_OR_NAME_AMBIGUOUS                 MAPI_DIAG( 1 )
#define MAPI_DIAG_MTS_CONGESTED                     MAPI_DIAG( 2 )
#define MAPI_DIAG_LOOP_DETECTED                     MAPI_DIAG( 3 )
#define MAPI_DIAG_RECIPIENT_UNAVAILABLE             MAPI_DIAG( 4 )
#define MAPI_DIAG_MAXIMUM_TIME_EXPIRED              MAPI_DIAG( 5 )
#define MAPI_DIAG_EITS_UNSUPPORTED                  MAPI_DIAG( 6 )
#define MAPI_DIAG_CONTENT_TOO_LONG                  MAPI_DIAG( 7 )
#define MAPI_DIAG_IMPRACTICAL_TO_CONVERT            MAPI_DIAG( 8 )
#define MAPI_DIAG_PROHIBITED_TO_CONVERT             MAPI_DIAG( 9 )
#define MAPI_DIAG_CONVERSION_UNSUBSCRIBED           MAPI_DIAG( 10 )
#define MAPI_DIAG_PARAMETERS_INVALID                MAPI_DIAG( 11 )
#define MAPI_DIAG_CONTENT_SYNTAX_IN_ERROR           MAPI_DIAG( 12 )
#define MAPI_DIAG_LENGTH_CONSTRAINT_VIOLATD         MAPI_DIAG( 13 )
#define MAPI_DIAG_NUMBER_CONSTRAINT_VIOLATD         MAPI_DIAG( 14 )
#define MAPI_DIAG_CONTENT_TYPE_UNSUPPORTED          MAPI_DIAG( 15 )
#define MAPI_DIAG_TOO_MANY_RECIPIENTS               MAPI_DIAG( 16 )
#define MAPI_DIAG_NO_BILATERAL_AGREEMENT            MAPI_DIAG( 17 )
#define MAPI_DIAG_CRITICAL_FUNC_UNSUPPORTED         MAPI_DIAG( 18 )
#define MAPI_DIAG_CONVERSION_LOSS_PROHIB            MAPI_DIAG( 19 )
#define MAPI_DIAG_LINE_TOO_LONG                     MAPI_DIAG( 20 )
#define MAPI_DIAG_PAGE_TOO_LONG                     MAPI_DIAG( 21 )
#define MAPI_DIAG_PICTORIAL_SYMBOL_LOST             MAPI_DIAG( 22 )
#define MAPI_DIAG_PUNCTUATION_SYMBOL_LOST           MAPI_DIAG( 23 )
#define MAPI_DIAG_ALPHABETIC_CHARACTER_LOST         MAPI_DIAG( 24 )
#define MAPI_DIAG_MULTIPLE_INFO_LOSSES              MAPI_DIAG( 25 )
#define MAPI_DIAG_REASSIGNMENT_PROHIBITED           MAPI_DIAG( 26 )
#define MAPI_DIAG_REDIRECTION_LOOP_DETECTED         MAPI_DIAG( 27 )
#define MAPI_DIAG_EXPANSION_PROHIBITED              MAPI_DIAG( 28 )
#define MAPI_DIAG_SUBMISSION_PROHIBITED             MAPI_DIAG( 29 )
#define MAPI_DIAG_EXPANSION_FAILED                  MAPI_DIAG( 30 )
#define MAPI_DIAG_RENDITION_UNSUPPORTED             MAPI_DIAG( 31 )
#define MAPI_DIAG_MAIL_ADDRESS_INCORRECT            MAPI_DIAG( 32 )
#define MAPI_DIAG_MAIL_OFFICE_INCOR_OR_INVD         MAPI_DIAG( 33 )
#define MAPI_DIAG_MAIL_ADDRESS_INCOMPLETE           MAPI_DIAG( 34 )
#define MAPI_DIAG_MAIL_RECIPIENT_UNKNOWN            MAPI_DIAG( 35 )
#define MAPI_DIAG_MAIL_RECIPIENT_DECEASED           MAPI_DIAG( 36 )
#define MAPI_DIAG_MAIL_ORGANIZATION_EXPIRED         MAPI_DIAG( 37 )
#define MAPI_DIAG_MAIL_REFUSED                      MAPI_DIAG( 38 )
#define MAPI_DIAG_MAIL_UNCLAIMED                    MAPI_DIAG( 39 )
#define MAPI_DIAG_MAIL_RECIPIENT_MOVED              MAPI_DIAG( 40 )
#define MAPI_DIAG_MAIL_RECIPIENT_TRAVELLING         MAPI_DIAG( 41 )
#define MAPI_DIAG_MAIL_RECIPIENT_DEPARTED           MAPI_DIAG( 42 )
#define MAPI_DIAG_MAIL_NEW_ADDRESS_UNKNOWN          MAPI_DIAG( 43 )
#define MAPI_DIAG_MAIL_FORWARDING_UNWANTED          MAPI_DIAG( 44 )
#define MAPI_DIAG_MAIL_FORWARDING_PROHIB            MAPI_DIAG( 45 )
#define MAPI_DIAG_SECURE_MESSAGING_ERROR            MAPI_DIAG( 46 )
#define MAPI_DIAG_DOWNGRADING_IMPOSSIBLE            MAPI_DIAG( 47 )
#define MAPI_MH_DP_PUBLIC_UA                        (0)
#define MAPI_MH_DP_PRIVATE_UA                       (1)
#define MAPI_MH_DP_MS                               (2)
#define MAPI_MH_DP_ML                               (3)
#define MAPI_MH_DP_PDAU                             (4)
#define MAPI_MH_DP_PDS_PATRON                       (5)
#define MAPI_MH_DP_OTHER_AU                         (6)

class IMailUser : public virtual IMAPIProp {
public:
	%extend {
		~IMailUser() { self->Release(); }
	}
};

class IDistList : public virtual IMAPIContainer {
public:
	virtual HRESULT CreateEntry(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG create_flags, IMAPIProp **OUTPUT /*lppMAPIPropEntry*/) = 0;
    virtual HRESULT CopyEntries(LPENTRYLIST lpEntries, ULONG ulUIParam, IMAPIProgress * lpProgress, ULONG ulFlags) = 0;
    virtual HRESULT DeleteEntries(LPENTRYLIST lpEntries, ULONG ulFlags) = 0;
	virtual HRESULT ResolveNames(const SPropTagArray *lpPropTagArray, ULONG ulFlags, LPADRLIST INOUT /*lpAdrList*/, LPFlagList INOUT /*lpFlagList*/) = 0;
	%extend {
		~IDistList() { self->Release(); }
	}
};


class IMAPIStatus : public IMAPIProp {
public:
    //    virtual ~IMAPIStatus() = 0;
	virtual HRESULT ValidateState(ULONG ulUIParam, ULONG ulFlags) = 0;
    virtual HRESULT SettingsDialog(ULONG ulUIParam, ULONG ulFlags) = 0;
    virtual HRESULT ChangePassword(LPTSTR lpOldPass, LPTSTR lpNewPass, ULONG ulFlags) = 0;
    virtual HRESULT FlushQueues(ULONG ulUIParam, ULONG cbTargetTransport, LPENTRYID lpTargetTransport, ULONG ulFlags) = 0;
	%extend {
		~IMAPIStatus() { self->Release(); }
	}
};

#define TBLSTAT_COMPLETE            0
#define TBLSTAT_QCHANGED            7
#define TBLSTAT_SORTING             9
#define TBLSTAT_SORT_ERROR          10
#define TBLSTAT_SETTING_COLS        11
#define TBLSTAT_SETCOL_ERROR        13
#define TBLSTAT_RESTRICTING         14
#define TBLSTAT_RESTRICT_ERROR      15

#define TBLTYPE_SNAPSHOT            0
#define TBLTYPE_KEYSET              1
#define TBLTYPE_DYNAMIC             2

#define TABLE_SORT_ASCEND       0x00000000
#define TABLE_SORT_DESCEND      0x00000001
#define TABLE_SORT_COMBINE      0x00000002
#define TABLE_SORT_CATEG_MAX	0x00000004
#define TABLE_SORT_CATEG_MIN	0x00000008

typedef ULONG       BOOKMARK;

#define BOOKMARK_BEGINNING  0      /* Before first row */
#define BOOKMARK_CURRENT    1      /* Before current row */
#define BOOKMARK_END        2      /* After last row */

#define FL_FULLSTRING       0x00000000
#define FL_SUBSTRING        0x00000001
#define FL_PREFIX           0x00000002

#define FL_IGNORECASE       0x00010000
#define FL_IGNORENONSPACE   0x00020000
#define FL_LOOSE            0x00040000

#define RES_AND             0x00000000
#define RES_OR              0x00000001
#define RES_NOT             0x00000002
#define RES_CONTENT         0x00000003
#define RES_PROPERTY        0x00000004
#define RES_COMPAREPROPS    0x00000005
#define RES_BITMASK         0x00000006
#define RES_SIZE            0x00000007
#define RES_EXIST           0x00000008
#define RES_SUBRESTRICTION  0x00000009
#define RES_COMMENT         0x0000000A

#define RELOP_LT        0     /* <  */
#define RELOP_LE        1     /* <= */
#define RELOP_GT        2     /* >  */
#define RELOP_GE        3     /* >= */
#define RELOP_EQ        4     /* == */
#define RELOP_NE        5     /* != */
#define RELOP_RE        6     /* LIKE Regular expression */

#define BMR_EQZ     0     /* ==0 */
#define BMR_NEZ     1     /* !=0 */

#define TBL_ALL_COLUMNS     0x00000001

#define TBL_LEAF_ROW            1
#define TBL_EMPTY_CATEGORY      2
#define TBL_EXPANDED_CATEGORY   3
#define TBL_COLLAPSED_CATEGORY  4

#define TBL_NOWAIT          0x00000001
#define TBL_ASYNC           0x00000001
#define TBL_BATCH           0x00000002

#define DIR_BACKWARD        0x00000001

#define TBL_NOADVANCE       0x00000001

class IMAPITable : public virtual IUnknown {
public:
    //    virtual ~IMAPITable() = 0;

    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
    virtual HRESULT Advise(ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink, ULONG* OUTPUT /*lpulConnection*/) = 0;
    virtual HRESULT Unadvise(ULONG ulConnection) = 0;
    virtual HRESULT GetStatus(ULONG *lpulTableStatus, ULONG* OUTPUT /*lpulTableType*/) = 0;
    virtual HRESULT SetColumns(const SPropTagArray *lpPropTagArray, ULONG ulFlags) = 0;
    virtual HRESULT QueryColumns(ULONG ulFlags, LPSPropTagArray* OUTPUT /*lpPropTagArray*/) = 0;
    virtual HRESULT GetRowCount(ULONG ulFlags, ULONG* OUTPUT /*lpulCount*/) = 0;
    virtual HRESULT SeekRow(BOOKMARK bkOrigin, LONG lRowCount, LONG* OUTPUT /*lplRowsSought*/) = 0;
    virtual HRESULT SeekRowApprox(ULONG ulNumerator, ULONG ulDenominator) = 0;
    virtual HRESULT QueryPosition(ULONG *lpulRow, ULONG* OUTPUT1 /*lpulNumerator*/, ULONG* OUTPUT2 /*lpulDenominator*/) = 0;
	virtual HRESULT FindRow(const SRestriction *, BOOKMARK origin, ULONG flags) = 0;
	virtual HRESULT Restrict(const SRestriction *, ULONG flags) = 0;
    virtual HRESULT CreateBookmark(BOOKMARK* OUTPUT /*lpbkPosition*/) = 0;
    virtual HRESULT FreeBookmark(BOOKMARK bkPosition) = 0;
    virtual HRESULT SortTable(const SSortOrderSet *c, ULONG flags) = 0;
    virtual HRESULT QuerySortOrder(LPSSortOrderSet* OUTPUT /*lppSortCriteria*/) = 0;
    virtual HRESULT QueryRows(LONG lRowCount, ULONG ulFlags, LPSRowSet* OUTPUT /*lppRows*/) = 0;
    virtual HRESULT Abort() = 0;
    virtual HRESULT ExpandRow(ULONG cbInstanceKey, BYTE *pbInstanceKey, ULONG ulRowCount,
			      ULONG ulFlags, LPSRowSet* OUTPUT /*lppRows*/, ULONG* OUTPUT2 /*lpulMoreRows*/) = 0;
    virtual HRESULT CollapseRow(ULONG cbInstanceKey, BYTE *pbInstanceKey, ULONG ulFlags, ULONG* OUTPUT /*lpulRowCount*/) = 0;
    virtual HRESULT WaitForCompletion(ULONG ulFlags, ULONG ulTimeout, ULONG* OUTPUT /*lpulTableStatus*/) = 0;
    virtual HRESULT GetCollapseState(ULONG ulFlags, ULONG cbInstanceKey, BYTE *pbInstanceKey, ULONG* lpulOutput /*lpcbCollapseState*/,
				     LPBYTE* lpOutput /*lppbCollapseState*/) = 0;
    virtual HRESULT SetCollapseState(ULONG ulFlags, ULONG cbCollapseState, BYTE *pbCollapseState, BOOKMARK* OUTPUT /*lpbkLocation*/) = 0;
	%extend {
		~IMAPITable() { self->Release(); }
	}
};

#define PS_PROFILE_PROPERTIES_INIT \
{   0x98, 0x15, 0xAC, 0x08, 0xAA, 0xB0, 0x10, 0x1A, \
    0x8C, 0x93, 0x08, 0x00, 0x2B, 0x2A, 0x56, 0xC2  }

#define MAPI_STORE_PROVIDER     (33)
#define MAPI_AB                 (34)
#define MAPI_AB_PROVIDER        (35)
#define MAPI_TRANSPORT_PROVIDER (36)
#define MAPI_SPOOLER            (37)
#define MAPI_PROFILE_PROVIDER   (38)
#define MAPI_SUBSYSTEM          (39)
#define MAPI_HOOK_PROVIDER      (40)

#define STATUS_VALIDATE_STATE   (0x00000001)
#define STATUS_SETTINGS_DIALOG  (0x00000002)
#define STATUS_CHANGE_PASSWORD  (0x00000004)
#define STATUS_FLUSH_QUEUES     (0x00000008)

#define STATUS_DEFAULT_OUTBOUND (0x00000001)
#define STATUS_DEFAULT_STORE    (0x00000002)
#define STATUS_PRIMARY_IDENTITY (0x00000004)
#define STATUS_SIMPLE_STORE     (0x00000008)
#define STATUS_XP_PREFER_LAST   (0x00000010)
#define STATUS_NO_PRIMARY_IDENTITY (0x00000020)
#define STATUS_NO_DEFAULT_STORE (0x00000040)
#define STATUS_TEMP_SECTION     (0x00000080)
#define STATUS_OWN_STORE        (0x00000100)
#define STATUS_NEED_IPM_TREE    (0x00000800)
#define STATUS_PRIMARY_STORE    (0x00001000)
#define STATUS_SECONDARY_STORE  (0x00002000)

#define STATUS_AVAILABLE        (0x00000001)
#define STATUS_OFFLINE          (0x00000002)
#define STATUS_FAILURE          (0x00000004)

#define STATUS_INBOUND_ENABLED  (0x00010000)
#define STATUS_INBOUND_ACTIVE   (0x00020000)
#define STATUS_INBOUND_FLUSH    (0x00040000)
#define STATUS_OUTBOUND_ENABLED (0x00100000)
#define STATUS_OUTBOUND_ACTIVE  (0x00200000)
#define STATUS_OUTBOUND_FLUSH   (0x00400000)
#define STATUS_REMOTE_ACCESS    (0x00800000)

#define SUPPRESS_UI                 (0x00000001)
#define REFRESH_XP_HEADER_CACHE     (0x00010000)
#define PROCESS_XP_HEADER_CACHE     (0x00020000)
#define FORCE_XP_CONNECT            (0x00040000)
#define FORCE_XP_DISCONNECT         (0x00080000)
#define CONFIG_CHANGED              (0x00100000)
#define ABORT_XP_HEADER_OPERATION   (0x00200000)
#define SHOW_XP_SESSION_UI          (0x00400000)

#define UI_READONLY     (0x00000001)

#define FLUSH_UPLOAD        (0x00000002)
#define FLUSH_DOWNLOAD      (0x00000004)
#define FLUSH_FORCE         (0x00000008)
#define FLUSH_NO_UI         (0x00000010)
#define FLUSH_ASYNC_OK      (0x00000020)

class IProfSect : public virtual IMAPIProp {
public:
	%extend {
		~IProfSect() { self->Release(); }
	}
};

#define MAPI_TOP_LEVEL      (0x00000001)

class IMAPIProgress : public IUnknown {
public:
    virtual HRESULT Progress(ULONG ulValue, ULONG ulCount, ULONG ulTotal) = 0;
    virtual HRESULT GetFlags(ULONG* lpulFlags) = 0;
    virtual HRESULT GetMax(ULONG* lpulMax) = 0;
    virtual HRESULT GetMin(ULONG* lpulMin) = 0;
    virtual HRESULT SetLimits(ULONG* lpulMin, ULONG* lpulMax, ULONG* lpulFlags) = 0;
	%extend {
		~IMAPIProgress() { self->Release(); }
	}
};

#define UI_SERVICE                  0x00000002
#define SERVICE_UI_ALWAYS           0x00000002
#define SERVICE_UI_ALLOWED          0x00000010
#define UI_CURRENT_PROVIDER_FIRST   0x00000004

class IProviderAdmin : public IUnknown {
public:
    virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* OUTPUT /*lppMAPIError*/) = 0;
    virtual HRESULT GetProviderTable(ULONG ulFlags, LPMAPITABLE* OUTPUT /*lppTable*/) = 0;
    virtual HRESULT CreateProvider(LPTSTR lpszProvider, ULONG cValues, LPSPropValue lpProps, ULONG ulUIParam,
				   ULONG ulFlags, LPMAPIUID OUTPUT /*lpUID*/) = 0;
    virtual HRESULT DeleteProvider(LPMAPIUID lpUID) = 0;
    virtual HRESULT OpenProfileSection(LPMAPIUID lpUID, LPCIID lpInterface, ULONG ulFlags, IProfSect** OUTPUT /*lppProfSect*/) = 0;
	%extend {
		~IProviderAdmin() { self->Release(); }
	}
};

#define ADRPARM_HELP_CTX        (0x00000000)

#define DIALOG_MODAL            (0x00000001)
#define DIALOG_SDI              (0x00000002)
#define DIALOG_OPTIONS          (0x00000004)
#define ADDRESS_ONE             (0x00000008)
#define AB_SELECTONLY           (0x00000010)
#define AB_RESOLVE              (0x00000020)

#define DT_MAILUSER         (0x00000000)
#define DT_DISTLIST         (0x00000001)
#define DT_FORUM            (0x00000002)
#define DT_AGENT            (0x00000003)
#define DT_ORGANIZATION     (0x00000004)
#define DT_PRIVATE_DISTLIST (0x00000005)
#define DT_REMOTE_MAILUSER  (0x00000006)

#define DT_MODIFIABLE       (0x00010000)
#define DT_GLOBAL           (0x00020000)
#define DT_LOCAL            (0x00030000)
#define DT_WAN              (0x00040000)
#define DT_NOT_SPECIFIC     (0x00050000)

#define DT_FOLDER           (0x01000000)
#define DT_FOLDER_LINK      (0x02000000)
#define DT_FOLDER_SPECIAL   (0x04000000)

#define MAPI_DEFERRED_ERRORS    (0x00000008)

#define MAPI_ASSOCIATED         (0x00000040)

#define MDB_NO_DIALOG           (0x00000001)
#define MDB_WRITE               (0x00000004)
#define MDB_TEMPORARY           (0x00000020)
#define MDB_NO_MAIL             (0x00000080)

#define AB_NO_DIALOG            (0x00000001)

#define EC_OVERRIDE_HOMESERVER	(0x00000001)

#if SWIGPYTHON

%feature("director") Message;
%feature("nodirector") Message::QueryInterface;
%feature("director") MAPIProp;
%feature("nodirector") MAPIProp::QueryInterface;
%feature("director") Attach;
%feature("nodirector") Attach::QueryInterface;
%feature("director") MAPITable;
%feature("nodirector") MAPITable::QueryInterface;

class MAPIProp : public IMAPIProp {
public:
	MAPIProp(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~MAPIProp() { self->Release(); };
	}
};

class Message : public IMessage {
public:
	Message(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~Message() { self->Release(); };
	}
};

class Attach : public IAttach {
public:
	Attach(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~Attach() { self->Release(); };
	}
};

class MAPITable : public IMAPITable {
public:
	MAPITable(ULONG cInterfaces, LPCIID lpInterfaces);
	%extend {
		virtual ~MAPITable() { self->Release(); };
	}
};

#endif
