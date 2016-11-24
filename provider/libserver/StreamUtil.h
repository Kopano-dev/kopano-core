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
 *
 */

#ifndef STREAMUTIL_H
#define STREAMUTIL_H

#include <kopano/kcodes.h>
#include "ECDatabase.h"
#include "ECDatabaseUtils.h"
#include "ECSession.h"
#include "cmdutil.hpp"

#include <SOAPUtils.h>

#ifdef DEBUG
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
ECRESULT SerializeDatabasePropVal(LPCSTREAMCAPS lpStreamInfo, DB_ROW lpRow, DB_LENGTHS lpLen, ECSerializer *lpSink);
ECRESULT SerializePropVal(LPCSTREAMCAPS lpStreamInfo, const struct propVal &sPropVal, ECSerializer *lpSink, const NamedPropDefMap *lpNamedPropDefs);
ECRESULT SerializeProps(ECSession *lpecSession, ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamInfo, unsigned int ulObjId, unsigned int ulObjType, unsigned int ulParentId, unsigned int ulStoreId, GUID *lpsGuid, ULONG ulFlags, ECSerializer *lpSink);
ECRESULT SerializeMessage(ECSession *lpecSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamInfo, unsigned int ulObjId, unsigned int ulObjType, unsigned int ulStoreId, GUID *lpsGuid, ULONG ulFlags, ECSerializer *lpSink, bool bTop);

ECRESULT DeserializePropVal(struct soap *soap, LPCSTREAMCAPS lpStreamInfo, propVal **lppsPropval, ECSerializer *lpSource);
ECRESULT DeserializeProps(ECSession *lpecSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamInfo, unsigned int ulObjId, unsigned int ulObjType, unsigned int ulStoreId, GUID *lpsGuid, bool bNewItem, ECSerializer *lpSource, struct propValArray **lppPropValArray);
ECRESULT DeserializeObject(ECSession *lpecSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, LPCSTREAMCAPS lpStreamInfo, unsigned int ulObjId, unsigned int ulStoreId, GUID *lpsGuid, bool bNewItem, unsigned long long ullIMAP, ECSerializer *lpSource, struct propValArray **lppPropValArray);

ECRESULT GetValidatedPropType(DB_ROW lpRow, unsigned int *lpulType);

} /* namespace */

#endif // ndef STREAMUTIL_H
