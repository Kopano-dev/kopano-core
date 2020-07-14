/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/kcodes.h>
#include "ECDatabase.h"
#include "ECDatabaseUtils.h"
#include "ECSession.h"
#include "cmdutil.hpp"
#include <SOAPUtils.h>

struct soap;

namespace KC {

class ECFifoBuffer;
class ECSerializer;
class ECAttachmentStorage;
struct StreamCaps;

// Utility Functions
extern ECRESULT SerializeMessage(ECSession *, ECDatabase *, ECAttachmentStorage *, const StreamCaps *, unsigned int objid, unsigned int objtype, unsigned int storeid, GUID *, unsigned int flags, ECSerializer *sink, bool top);
extern ECRESULT DeserializeObject(ECSession *, ECDatabase *, ECAttachmentStorage *, const StreamCaps *, unsigned int objid, unsigned int storeid, GUID *, bool newitem, unsigned long long imap, ECSerializer *src, struct propValArray **);

} /* namespace */
