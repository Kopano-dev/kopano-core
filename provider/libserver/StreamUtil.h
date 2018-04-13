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
