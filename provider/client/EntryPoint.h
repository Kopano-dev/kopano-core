/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ENTRYPOINT_H
#define ENTRYPOINT_H

#include <kopano/zcdefs.h>
#include <mapispi.h>
#include "ProviderUtil.h"

extern "C" {

extern KC_EXPORT HRESULT MSGServiceEntry(HINSTANCE, IMalloc *, IMAPISupport *, unsigned int ui_param, unsigned int se_flags, unsigned int ctx, unsigned int nprops, const SPropValue *props, IProviderAdmin *, MAPIERROR **);

}

class WSTransport;
extern HRESULT InitializeProvider(IProviderAdmin *, IProfSect *, const sGlobalProfileProps &, ULONG *eid_size, ENTRYID **store_eid);

// Global values
extern ECMapProvider g_mapProviders;
extern KC::tstring g_strProductName;

#endif // ENTRYPOINT_H
