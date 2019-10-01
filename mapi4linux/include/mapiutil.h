/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
/* mapiutil.h – Defines utility interfaces and functions */
#ifndef M4L_MAPIUTIL_H
#define M4L_MAPIUTIL_H
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
extern KC_EXPORT HRESULT HrAllocAdviseSink(NOTIFCALLBACK *, void *ctx, IMAPIAdviseSink **);

/* General utility functions */

/* Related to the MAPI interface */

extern KC_EXPORT HRESULT HrGetOneProp(IMAPIProp *mprop, unsigned int tag, SPropValue **ret);
extern KC_EXPORT HRESULT HrSetOneProp(IMAPIProp *mprop, const SPropValue *prop);
extern KC_EXPORT BOOL FPropExists(IMAPIProp *mprop, unsigned int tag);
extern KC_EXPORT SPropValue *PpropFindProp(SPropValue *props, unsigned int vals, unsigned int tag);
extern KC_EXPORT const SPropValue *PCpropFindProp(const SPropValue *, unsigned int vals, unsigned int tag);
extern KC_EXPORT void FreePadrlist(ADRLIST *);
extern KC_EXPORT void FreeProws(SRowSet *);
extern KC_EXPORT HRESULT HrQueryAllRows(IMAPITable *table, const SPropTagArray *tags, SRestriction *, const SSortOrderSet *, int rows_max, SRowSet **);

/* 64-bit arithmetic with times */
extern FILETIME FtSubFt(const FILETIME &ftMinuend, const FILETIME &ftSubtrahend);

/* Message composition */
extern KC_EXPORT SCODE ScCreateConversationIndex(unsigned int parent_size, BYTE *parent, unsigned int *conv_index_size, BYTE **conv_index);

/* Store support */
extern KC_EXPORT HRESULT WrapStoreEntryID(unsigned int flags, const TCHAR *dllname, unsigned int eid_size, const ENTRYID *eid, unsigned int *ret_size, ENTRYID **ret);

/* RTF Sync Utilities */

#define RTF_SYNC_RTF_CHANGED    ((ULONG) 0x00000001)
#define RTF_SYNC_BODY_CHANGED   ((ULONG) 0x00000002)

extern KC_EXPORT HRESULT RTFSync(IMessage *, unsigned int flags, BOOL *msg_updated);

/* Flags for WrapCompressedRTFStream() */

/****** MAPI_MODIFY             ((ULONG) 0x00000001) mapidefs.h */
/****** STORE_UNCOMPRESSED_RTF  ((ULONG) 0x00008000) mapidefs.h */
extern KC_EXPORT HRESULT WrapCompressedRTFStream(IStream *compr_rtf_strm, unsigned int flags, IStream **uncompr_rtf_strm);

/* and this is from ol2e.h */
extern KC_EXPORT HRESULT CreateStreamOnHGlobal(void *global, BOOL delete_on_release, IStream **);

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
