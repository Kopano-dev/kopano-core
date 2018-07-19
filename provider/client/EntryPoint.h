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

#ifndef ENTRYPOINT_H
#define ENTRYPOINT_H

#include <kopano/zcdefs.h>
#include <mapispi.h>
#include "ProviderUtil.h"

extern "C" {

extern _kc_export HRESULT MSProviderInit(HINSTANCE, LPMALLOC, LPALLOCATEBUFFER, LPALLOCATEMORE, LPFREEBUFFER, ULONG flags, ULONG mapi_ver, ULONG *mdb_ver, LPMSPROVIDER *);
extern _kc_export HRESULT MSGServiceEntry(HINSTANCE, IMalloc *, IMAPISupport *, ULONG ui_param, ULONG se_flags, ULONG ctx, ULONG nprops, const SPropValue *props, IProviderAdmin *, MAPIERROR **);
extern _kc_export HRESULT  ABProviderInit(HINSTANCE, LPMALLOC, LPALLOCATEBUFFER, LPALLOCATEMORE, LPFREEBUFFER, ULONG flags, ULONG mapi_ver, ULONG *prov_ver, LPABPROVIDER *);

}

class WSTransport;
extern HRESULT InitializeProvider(IProviderAdmin *, IProfSect *, const sGlobalProfileProps &, ULONG *eid_size, ENTRYID **store_eid);

// Global values
extern ECMapProvider g_mapProviders;
extern tstring		g_strProductName;

#endif // ENTRYPOINT_H
