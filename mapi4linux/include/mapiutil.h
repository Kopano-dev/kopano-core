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
 */

/* mapiutil.h – Defines utility interfaces and functions */

#ifndef __M4L_MAPIUTIL_H_
#define __M4L_MAPIUTIL_H_
#define MAPIUTIL_H

#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <mapix.h>

/* IMAPITable in memory */

/*
 * ITableData Interface
 */
class ITableData;
typedef ITableData* LPTABLEDATA;

typedef void (CALLERRELEASE)(
    ULONG       ulCallerData,
    LPTABLEDATA lpTblData,
    LPMAPITABLE lpVue
);

class ITableData : public IUnknown {
public:
    virtual HRESULT HrGetView(LPSSortOrderSet lpSSortOrderSet, CALLERRELEASE* lpfCallerRelease, ULONG ulCallerData,
			      LPMAPITABLE* lppMAPITable) = 0;
    virtual HRESULT HrModifyRow(LPSRow) = 0;
    virtual HRESULT HrDeleteRow(LPSPropValue lpSPropValue) = 0;
    virtual HRESULT HrQueryRow(LPSPropValue lpsPropValue, LPSRow* lppSRow, ULONG* lpuliRow) = 0;
    virtual HRESULT HrEnumRow(ULONG ulRowNumber, LPSRow* lppSRow) = 0;
    virtual HRESULT HrNotify(ULONG ulFlags, ULONG cValues, LPSPropValue lpSPropValue) = 0;
    virtual HRESULT HrInsertRow(ULONG uliRow, LPSRow lpSRow) = 0;
    virtual HRESULT HrModifyRows(ULONG ulFlags, LPSRowSet lpSRowSet) = 0;
    virtual HRESULT HrDeleteRows(ULONG ulFlags, LPSRowSet lprowsetToDelete, ULONG* cRowsDeleted) = 0;
};



/* Entry Point for in memory ITable */


SCODE CreateTable(LPCIID            lpInterface,
		  ALLOCATEBUFFER*   lpAllocateBuffer,
		  ALLOCATEMORE*     lpAllocateMore,
		  FREEBUFFER*       lpFreeBuffer,
		  LPVOID            lpvReserved,
		  ULONG             ulTableType,
		  ULONG             ulPropTagIndexColumn,
		  LPSPropTagArray   lpSPropTagArrayColumns,
		  LPTABLEDATA*      lppTableData );

#define TAD_ALL_ROWS    1


/* IMAPIProp in memory */

/*
 * IPropData Interface
 */
class IPropData : public IMAPIProp {
public:
    virtual HRESULT HrSetObjAccess(ULONG ulAccess) = 0;
	virtual HRESULT HrSetPropAccess(const SPropTagArray *lpPropTagArray, ULONG *rgulAccess) = 0;
    virtual HRESULT HrGetPropAccess(LPSPropTagArray* lppPropTagArray, ULONG** lprgulAccess) = 0;
	virtual HRESULT HrAddObjProps(const SPropTagArray *lppPropTagArray, SPropProblemArray **lprgulAccess) = 0;
};
typedef IPropData* LPPROPDATA;


extern "C" {
/* Entry Point for in memory IMAPIProp */


SCODE CreateIProp(LPCIID             lpInterface,
		  ALLOCATEBUFFER *   lpAllocateBuffer,
		  ALLOCATEMORE *     lpAllocateMore,
		  FREEBUFFER *       lpFreeBuffer,
		  LPVOID             lpvReserved,
		  LPPROPDATA *       lppPropData );

/*
 *  Defines for prop/obj access
 */
#define IPROP_READONLY      ((ULONG) 0x00000001)
#define IPROP_READWRITE     ((ULONG) 0x00000002)
#define IPROP_CLEAN         ((ULONG) 0x00010000)
#define IPROP_DIRTY         ((ULONG) 0x00020000)

/*
 -  HrSetPropAccess
 -
 *  Sets access right attributes on a per-property basis.  By default,
 *  all properties are read/write.
 *
 */

/*
 -  HrSetObjAccess
 -
 *  Sets access rights for the object itself.  By default, the object has
 *  read/write access.
 *
 */

/* IMalloc Utilities */

LPMALLOC MAPIGetDefaultMalloc(void);


/* StreamOnFile (SOF) */

/*
 *  Methods and #define's for implementing an OLE 2.0 storage stream
 *  (as defined in the OLE 2.0 specs) on top of a system file.
 */

#define SOF_UNIQUEFILENAME  ((ULONG) 0x80000000)

HRESULT OpenStreamOnFile(
    LPALLOCATEBUFFER    lpAllocateBuffer,
    LPFREEBUFFER        lpFreeBuffer,
    ULONG               ulFlags,
    LPTSTR              lpszFileName,
    LPTSTR              lpszPrefix,
    LPSTREAM *          lppStream);

// uh?
typedef HRESULT (*LPOPENSTREAMONFILE) (
    LPALLOCATEBUFFER    lpAllocateBuffer,
    LPFREEBUFFER        lpFreeBuffer,
    ULONG               ulFlags,
    LPTSTR              lpszFileName,
    LPTSTR              lpszPrefix,
    LPSTREAM *          lppStream);

#define OPENSTREAMONFILE "OpenStreamOnFile"


/* Property interface utilities */

/*
 *  Copies a single SPropValue from Src to Dest.  Handles all the various
 *  types of properties and will link its allocations given the master
 *  allocation object and an allocate more function.
 */
SCODE PropCopyMore(LPSPropValue      lpSPropValueDest,
		   LPSPropValue      lpSPropValueSrc,
		   ALLOCATEMORE *    lpfAllocMore,
		   LPVOID            lpvObject );

/*
 *  Returns the size in bytes of structure at lpSPropValue, including the
 *  Value.
 */
ULONG UlPropSize(LPSPropValue    lpSPropValue);


BOOL FEqualNames( LPMAPINAMEID lpName1, LPMAPINAMEID lpName2 );

void GetInstance(LPSPropValue lpPropMv, LPSPropValue lpPropSv, ULONG uliInst);

extern char rgchCsds[];
extern char rgchCids[];
extern char rgchCsdi[];
extern char rgchCidi[];

BOOL
FPropContainsProp( LPSPropValue lpSPropValueDst,
                   LPSPropValue lpSPropValueSrc,
                   ULONG        ulFuzzyLevel );

BOOL
FPropCompareProp( LPSPropValue  lpSPropValue1,
                  ULONG         ulRelOp,
                  LPSPropValue  lpSPropValue2 );

LONG
LPropCompareProp( LPSPropValue  lpSPropValueA,
                  LPSPropValue  lpSPropValueB );

extern HRESULT HrAddColumns(LPMAPITABLE lptbl, const SPropTagArray *lpproptagColumnsNew, LPALLOCATEBUFFER lpAllocateBuffer, LPFREEBUFFER lpFreeBuffer);
extern HRESULT HrAddColumnsEx(LPMAPITABLE lptbl, const SPropTagArray *lpproptagColumnsNew, LPALLOCATEBUFFER lpAllocateBuffer, LPFREEBUFFER lpFreeBuffer, void (*)(const SPropTagArray *ptaga));

/* Notification utilities */

/*
 *  Function that creates an advise sink object given a notification
 *  callback function and context.
 */
extern _kc_export HRESULT HrAllocAdviseSink(LPNOTIFCALLBACK, LPVOID ctx, LPMAPIADVISESINK *);

/*
 *  Wraps an existing advise sink with another one which guarantees
 *  that the original advise sink will be called in the thread on
 *  which it was created.
 */

HRESULT
HrThisThreadAdviseSink( LPMAPIADVISESINK lpAdviseSink,
                        LPMAPIADVISESINK *lppAdviseSink);



/*
 *  Allows a client and/or provider to force notifications
 *  which are currently queued in the MAPI notification engine
 *  to be dispatched without doing a message dispatch.
 */

HRESULT HrDispatchNotifications (ULONG ulFlags);


/* Service Provider Utilities */

/*
 *  Structures and utility function for building a display table
 *  from resources.
 */

struct DTCTL {
    ULONG           ulCtlType;          /* DTCT_LABEL, etc. */
    ULONG           ulCtlFlags;         /* DT_REQUIRED, etc. */
    LPBYTE          lpbNotif;           /*  pointer to notification data */
    ULONG           cbNotif;            /* count of bytes of notification data */
    LPTSTR          lpszFilter;         /* character filter for edit/combobox */
    ULONG           ulItemID;           /* to validate parallel dlg template entry */
    union {                             /* ulCtlType discriminates */
        LPVOID          lpv;            /* Initialize this to avoid warnings */
        LPDTBLLABEL     lplabel;
        LPDTBLEDIT      lpedit;
        LPDTBLLBX       lplbx;
        LPDTBLCOMBOBOX  lpcombobox;
        LPDTBLDDLBX     lpddlbx;
        LPDTBLCHECKBOX  lpcheckbox;
        LPDTBLGROUPBOX  lpgroupbox;
        LPDTBLBUTTON    lpbutton;
        LPDTBLRADIOBUTTON lpradiobutton;
        LPDTBLMVLISTBOX lpmvlbx;
        LPDTBLMVDDLBX   lpmvddlbx;
        LPDTBLPAGE      lppage;
    } ctl;
};
typedef struct DTCTL *LPDTCTL;

struct DTPAGE {
    ULONG           cctl;
    LPTSTR          lpszResourceName;   /* as usual, may be an integer ID */
    union {                             /* as usual, may be an integer ID */
        LPTSTR          lpszComponent;
        ULONG           ulItemID;
    };
    LPDTCTL         lpctl;
};
typedef struct DTPAGE *LPDTPAGE;

HRESULT
BuildDisplayTable(  LPALLOCATEBUFFER    lpAllocateBuffer,
                    LPALLOCATEMORE      lpAllocateMore,
                    LPFREEBUFFER        lpFreeBuffer,
                    LPMALLOC            lpMalloc,
                    HINSTANCE           hInstance,
                    UINT                cPages,
                    LPDTPAGE            lpPage,
                    ULONG               ulFlags,
                    LPMAPITABLE *       lppTable,
                    LPTABLEDATA *       lppTblData );


/* MAPI structure validation/copy utilities */

/*
 *  Validate, copy, and adjust pointers in MAPI structures:
 *      notification
 *      property value array
 *      option data
 */
SCODE
ScCountNotifications(int cNotifications, LPNOTIFICATION lpNotifications,
        ULONG *lpcb);

SCODE
ScCopyNotifications(int cNotification, LPNOTIFICATION lpNotifications,
        LPVOID lpvDst, ULONG *lpcb);

SCODE
ScRelocNotifications(int cNotification, LPNOTIFICATION lpNotifications,
        LPVOID lpvBaseOld, LPVOID lpvBaseNew, ULONG *lpcb);


SCODE
ScCountProps(int cValues, LPSPropValue lpPropArray, ULONG *lpcb);

LPSPropValue
LpValFindProp(ULONG ulPropTag, ULONG cValues, LPSPropValue lpPropArray);

SCODE
ScCopyProps(int cValues, LPSPropValue lpPropArray, LPVOID lpvDst,
        ULONG *lpcb);

SCODE
ScRelocProps(int cValues, LPSPropValue lpPropArray,
        LPVOID lpvBaseOld, LPVOID lpvBaseNew, ULONG *lpcb);

SCODE
ScDupPropset(int cValues, LPSPropValue lpPropArray,
        LPALLOCATEBUFFER lpAllocateBuffer, LPSPropValue *lppPropArray);


/* General utility functions */

/* Related to the OLE Component object model */

ULONG UlAddRef(LPVOID lpunk);
ULONG UlRelease(LPVOID lpunk);

/* Related to the MAPI interface */

extern _kc_export HRESULT HrGetOneProp(LPMAPIPROP mprop, ULONG tag, LPSPropValue *ret);
extern _kc_export HRESULT HrSetOneProp(LPMAPIPROP mprop, const SPropValue *prop);
extern _kc_export BOOL FPropExists(LPMAPIPROP mprop, ULONG tag);
extern _kc_export LPSPropValue PpropFindProp(LPSPropValue props, ULONG vals, ULONG tag);
extern _kc_export const SPropValue *PCpropFindProp(const SPropValue *, ULONG vals, ULONG tag);
extern _kc_export void FreePadrlist(LPADRLIST);
extern _kc_export void FreeProws(LPSRowSet rows);
extern _kc_export HRESULT HrQueryAllRows(LPMAPITABLE table, const SPropTagArray *tags, LPSRestriction, const SSortOrderSet *, LONG rows_max, LPSRowSet *rows);

/* Create or validate the IPM folder tree in a message store */

#define MAPI_FORCE_CREATE   1
#define MAPI_FULL_IPM_TREE  2

HRESULT HrValidateIPMSubtree(LPMDB lpMDB, ULONG ulFlags,
			     ULONG *lpcValues, LPSPropValue *lppValues,
			     LPMAPIERROR *lpperr);

/* Encoding and decoding strings */

BOOL FBinFromHex(LPTSTR lpsz, LPBYTE lpb);
SCODE ScBinFromHexBounded(LPTSTR lpsz, LPBYTE lpb, ULONG cb);
void HexFromBin(LPBYTE lpb, int cb, LPTSTR lpsz);
ULONG UlFromSzHex(LPCTSTR lpsz);

/* Encoding and decoding entry IDs */
HRESULT HrEntryIDFromSz(LPTSTR lpsz, ULONG *lpcb,
			LPENTRYID *lppEntryID);
HRESULT HrSzFromEntryID(ULONG cb, LPENTRYID lpEntryID,
			LPTSTR *lpsz);
HRESULT HrComposeEID(LPMAPISESSION lpSession,
		     ULONG cbStoreRecordKey, LPBYTE lpStoreRecordKey,
		     ULONG cbMsgEntryID, LPENTRYID lpMsgEntryID,
		     ULONG *lpcbEID, LPENTRYID *lppEntryID);
HRESULT HrDecomposeEID(LPMAPISESSION lpSession,
		       ULONG cbEntryID, LPENTRYID lpEntryID,
		       ULONG *lpcbStoreEntryID,
		       LPENTRYID *lppStoreEntryID,
		       ULONG *lpcbMsgEntryID,
		       LPENTRYID *lppMsgEntryID);
HRESULT HrComposeMsgID(LPMAPISESSION lpSession,
		       ULONG cbStoreSearchKey, LPBYTE pStoreSearchKey,
		       ULONG cbMsgEntryID, LPENTRYID lpMsgEntryID,
		       LPTSTR *lpszMsgID);
HRESULT HrDecomposeMsgID(LPMAPISESSION lpSession,
			 LPTSTR lpszMsgID,
			 ULONG *lpcbStoreEntryID,
			 LPENTRYID *lppStoreEntryID,
			 ULONG *lppcbMsgEntryID,
			 LPENTRYID *lppMsgEntryID);

/* Other encodings */
ULONG CbOfEncoded(LPCSTR lpszEnc);
ULONG CchOfEncoding(LPCSTR lpszEnd);
LPWSTR EncodeID(ULONG cbEID, LPENTRYID rgbID, LPWSTR *lpWString);
void FDecodeID(LPCSTR lpwEncoded, LPENTRYID *lpDecoded, ULONG *cbEncoded);

/* C runtime substitutes */


LPTSTR SzFindCh(LPCTSTR lpsz, USHORT ch);      /* strchr */
LPTSTR SzFindLastCh(LPCTSTR lpsz, USHORT ch);  /* strrchr */
LPTSTR SzFindSz(LPCTSTR lpsz, LPCTSTR lpszKey); /*strstr */
unsigned int UFromSz(LPCTSTR lpsz);                  /* atoi */

SCODE ScUNCFromLocalPath(LPSTR lpszLocal, LPSTR lpszUNC,
			 UINT cchUNC);
SCODE ScLocalPathFromUNC(LPSTR lpszUNC, LPSTR lpszLocal,
			 UINT cchLocal);

/* Windows Unicode string functions */
extern int MNLS_CompareStringW(LCID Locale, DWORD dwCmpFlags, LPCWSTR lpString1, int cchCount1, LPCWSTR lpString2, int cchCount2);
extern int MNLS_lstrlenW(LPCWSTR lpString);
extern int MNLS_lstrlen(LPCSTR lpString);
extern int MNLS_lstrcmpW(LPCWSTR lpString1, LPCWSTR lpString2);
extern LPWSTR MNLS_lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2);

/* 64-bit arithmetic with times */

FILETIME FtAddFt(FILETIME ftAddend1, FILETIME ftAddend2);
FILETIME FtMulDwDw(DWORD ftMultiplicand, DWORD ftMultiplier);
FILETIME FtMulDw(DWORD ftMultiplier, FILETIME ftMultiplicand);
FILETIME FtSubFt(FILETIME ftMinuend, FILETIME ftSubtrahend);
FILETIME FtNegFt(FILETIME ft);
FILETIME FtDivFtBogus(FILETIME f, FILETIME f2, DWORD n);

/* Message composition */
extern _kc_export SCODE ScCreateConversationIndex(ULONG parent_size, LPBYTE parent, ULONG *conv_index_size, LPBYTE *conv_index);

/* Store support */
extern _kc_export HRESULT WrapStoreEntryID(ULONG flags, const TCHAR *dllname, ULONG eid_size, const ENTRYID *eid, ULONG *ret_size, LPENTRYID *ret);

/* RTF Sync Utilities */

#define RTF_SYNC_RTF_CHANGED    ((ULONG) 0x00000001)
#define RTF_SYNC_BODY_CHANGED   ((ULONG) 0x00000002)

extern _kc_export HRESULT RTFSync(LPMESSAGE, ULONG flags, BOOL *msg_updated);


/* Flags for WrapCompressedRTFStream() */

/****** MAPI_MODIFY             ((ULONG) 0x00000001) mapidefs.h */
/****** STORE_UNCOMPRESSED_RTF  ((ULONG) 0x00008000) mapidefs.h */
extern _kc_export HRESULT WrapCompressedRTFStream(LPSTREAM compr_rtf_strm, ULONG flags, LPSTREAM *uncompr_rtf_strm);

/*
 * Setup and cleanup. 
 *
 * Providers never need to make these calls.
 *
 * Test applications and the like which do not call MAPIInitialize
 * may want to call them, so that the few utility functions which
 * need MAPI allocators (and do not ask for them explicitly)
 * will work.
 */

/* All flags are reserved for ScInitMAPIUtil. */

SCODE ScInitMAPIUtil(ULONG ulFlags);
void DeinitMAPIUtil(void);
extern SCODE ScInitMapiUtil(ULONG ulFlags);
extern void DeinitMapiUtil(void);

/*
 *  Entry point names.
 *  
 *  These are for new entry points defined since MAPI first shipped
 *  in Windows 95. Using these names in a GetProcAddress call makes
 *  it easier to write code which uses them optionally.
 */

#define szHrDispatchNotifications "_HrDispatchNotifications@4"

typedef HRESULT (DISPATCHNOTIFICATIONS)(ULONG ulFlags);
typedef DISPATCHNOTIFICATIONS* LPDISPATCHNOTIFICATIONS;

#define szScCreateConversationIndex "_ScCreateConversationIndex@16"

typedef SCODE (CREATECONVERSATIONINDEX)(ULONG cbParent,
					LPBYTE lpbParent,
					ULONG *lpcbConvIndex,
					LPBYTE *lppbConvIndex);
typedef CREATECONVERSATIONINDEX* LPCREATECONVERSATIONINDEX;

/* ********************************************************* */

/* and this is from ol2e.h */
extern _kc_export HRESULT CreateStreamOnHGlobal(void *global, BOOL delete_on_release, LPSTREAM *);
HRESULT BuildDisplayTable(LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore,
							LPFREEBUFFER lpFreeBuffer, LPMALLOC lpMalloc,
							HINSTANCE hInstance, UINT cPages,
							LPDTPAGE lpPage, ULONG ulFlags,
							LPMAPITABLE * lppTable, LPTABLEDATA * lppTblData);

} // EXTERN "C"

#endif /* _MAPIUTIL_H_ */
