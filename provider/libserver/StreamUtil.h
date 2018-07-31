/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef STREAMUTIL_H
#define STREAMUTIL_H

#include <kopano/kcodes.h>
#include "ECDatabase.h"
#include "ECDatabaseUtils.h"
#include "ECSession.h"
#include "cmdutil.hpp"
#include <SOAPUtils.h>
#ifdef KNOB144
#include <cstdio>
#endif

struct soap;

namespace KC {

class ECFifoBuffer;
class ECSerializer;
class ECAttachmentStorage;
struct StreamCaps;
typedef const StreamCaps* LPCSTREAMCAPS;

// Utility Functions
ECRESULT SerializeMessage(ECSession *lpecSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamInfo, unsigned int ulObjId, unsigned int ulObjType, unsigned int ulStoreId, GUID *lpsGuid, ULONG ulFlags, ECSerializer *lpSink, bool bTop);
ECRESULT DeserializeObject(ECSession *lpecSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamInfo, unsigned int ulObjId, unsigned int ulStoreId, GUID *lpsGuid, bool bNewItem, unsigned long long ullIMAP, ECSerializer *lpSource, struct propValArray **lppPropValArray);

} /* namespace */

#endif // ndef STREAMUTIL_H
