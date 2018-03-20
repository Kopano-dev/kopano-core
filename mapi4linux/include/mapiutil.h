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

extern "C" {

/* Notification utilities */

/*
 *  Function that creates an advise sink object given a notification
 *  callback function and context.
 */
extern _kc_export HRESULT HrAllocAdviseSink(LPNOTIFCALLBACK, LPVOID ctx, LPMAPIADVISESINK *);

/* General utility functions */

/* Related to the MAPI interface */

extern _kc_export HRESULT HrGetOneProp(LPMAPIPROP mprop, ULONG tag, LPSPropValue *ret);
extern _kc_export HRESULT HrSetOneProp(LPMAPIPROP mprop, const SPropValue *prop);
extern _kc_export BOOL FPropExists(LPMAPIPROP mprop, ULONG tag);
extern _kc_export LPSPropValue PpropFindProp(LPSPropValue props, ULONG vals, ULONG tag);
extern _kc_export const SPropValue *PCpropFindProp(const SPropValue *, ULONG vals, ULONG tag);
extern _kc_export void FreePadrlist(LPADRLIST);
extern _kc_export void FreeProws(LPSRowSet rows);
extern _kc_export HRESULT HrQueryAllRows(LPMAPITABLE table, const SPropTagArray *tags, LPSRestriction, const SSortOrderSet *, LONG rows_max, LPSRowSet *rows);

/* 64-bit arithmetic with times */
extern FILETIME FtSubFt(const FILETIME &ftMinuend, const FILETIME &ftSubtrahend);

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

/* and this is from ol2e.h */
extern _kc_export HRESULT CreateStreamOnHGlobal(void *global, BOOL delete_on_release, LPSTREAM *);

} // EXTERN "C"

inline const SPropValue *ADRENTRY::cfind(ULONG tag) const
{
	return PCpropFindProp(rgPropVals, cValues, tag);
}

inline SPropValue *SRow::find(ULONG tag) const
{
	return PpropFindProp(lpProps, cValues, tag);
}

inline const SPropValue *SRow::cfind(ULONG tag) const
{
	return PCpropFindProp(lpProps, cValues, tag);
}

#endif /* _MAPIUTIL_H_ */
