/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECENTRYID_H
#define ECENTRYID_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>

namespace KC {

extern KC_EXPORT unsigned char *g_lpEveryoneEid;
extern KC_EXPORT const unsigned int g_cbEveryoneEid;
extern KC_EXPORT unsigned char *g_lpSystemEid;
extern KC_EXPORT const unsigned int g_cbSystemEid;
extern KC_EXPORT HRESULT EntryIdIsEveryone(unsigned int eid_size, const ENTRYID *eid, bool *result);
extern KC_EXPORT HRESULT GetNonPortableObjectType(unsigned int eid_size, const ENTRYID *eid, unsigned int *obj_type);

} /* namespace */

#endif
