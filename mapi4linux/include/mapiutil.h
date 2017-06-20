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

/* 64-bit arithmetic with times */
FILETIME FtSubFt(FILETIME ftMinuend, FILETIME ftSubtrahend);

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

} // EXTERN "C"

#endif /* _MAPIUTIL_H_ */
