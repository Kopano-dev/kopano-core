/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef STORAGEUTIL_H
#define STORAGEUTIL_H

#include <kopano/kcodes.h>
#include <mapidefs.h>

namespace KC {

class ECDatabase;
class ECAttachmentStorage;
class ECSession;

ECRESULT CreateObject(ECSession *lpecSession, ECDatabase *lpDatabase, unsigned int ulParentObjId, unsigned int ulParentType, unsigned int ulObjType, unsigned int ulFlags, unsigned int *lpulObjId);

enum eSizeUpdateAction{ UPDATE_SET, UPDATE_ADD, UPDATE_SUB };
ECRESULT GetObjectSize(ECDatabase* lpDatabase, unsigned int ulObjId, unsigned int* lpulSize);	
ECRESULT CalculateObjectSize(ECDatabase* lpDatabase, unsigned int objid, unsigned int ulObjType, unsigned int* lpulSize);
ECRESULT UpdateObjectSize(ECDatabase* lpDatabase, unsigned int ulObjId, unsigned int ulObjType, eSizeUpdateAction updateAction, long long llSize);

extern void sync_logon_times(ECDatabase *);
extern void record_logon_time(ECSession *, bool);

/**
 * Get the corrected object type used to determine course of action.
 * MAPI_MESSAGE objects can contain only MAPI_ATTACH, MAPI_MAILUSER and MAPI_DISTLIST sub objects,
 * but in practice the object type can be different. This function will return MAPI_MAILUSER for
 * any MAPI_MESSAGE subtype that does not match the mentioned types.
 */
static inline unsigned int RealObjType(unsigned int ulObjType, unsigned int ulParentType) {
    if (ulParentType == MAPI_MESSAGE && ulObjType != MAPI_MAILUSER && ulObjType != MAPI_DISTLIST && ulObjType != MAPI_ATTACH)
        return MAPI_MAILUSER;
    return ulObjType;
}

} /* namespace */

#endif // ndef STORAGEUTIL_H
