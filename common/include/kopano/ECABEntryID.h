/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECENTRYID_H
#define ECENTRYID_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>

namespace KC {

extern _kc_export unsigned char *g_lpEveryoneEid;
extern _kc_export const unsigned int g_cbEveryoneEid;
extern _kc_export unsigned char *g_lpSystemEid;
extern _kc_export const unsigned int g_cbSystemEid;
extern _kc_export HRESULT EntryIdIsEveryone(unsigned int eid_size, const ENTRYID *eid, bool *result);
extern _kc_export HRESULT GetNonPortableObjectType(unsigned int eid_size, const ENTRYID *eid, ULONG *obj_type);

} /* namespace */

#endif
