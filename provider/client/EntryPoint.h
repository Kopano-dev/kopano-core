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
#include <kopano/tstring.h>

extern "C" {

extern _kc_export HRESULT __cdecl MSProviderInit(HINSTANCE, LPMALLOC, LPALLOCATEBUFFER, LPALLOCATEMORE, LPFREEBUFFER, ULONG flags, ULONG mapi_ver, ULONG *mdb_ver, LPMSPROVIDER *);
extern _kc_export HRESULT __stdcall MSGServiceEntry(HINSTANCE, LPMALLOC, LPMAPISUP, ULONG ui_param, ULONG se_flags, ULONG ctx, ULONG cvals, LPSPropValue pvals, LPPROVIDERADMIN admprovs, LPMAPIERROR *);
extern _kc_export HRESULT __cdecl XPProviderInit(HINSTANCE, LPMALLOC, LPALLOCATEBUFFER, LPALLOCATEMORE, LPFREEBUFFER, ULONG flags, ULONG mapi_ver, ULONG *prov_ver, LPXPPROVIDER *);
extern _kc_export HRESULT  __cdecl ABProviderInit(HINSTANCE, LPMALLOC, LPALLOCATEBUFFER, LPALLOCATEMORE, LPFREEBUFFER, ULONG flags, ULONG mapi_ver, ULONG *prov_ver, LPABPROVIDER *);

}

class WSTransport;
HRESULT InitializeProvider(LPPROVIDERADMIN lpAdminProvider, IProfSect *lpProfSect, const sGlobalProfileProps &, ULONG *lpcStoreID, LPENTRYID *lppStoreID, WSTransport * = NULL);

// Global values
extern tstring	g_strCommonFilesKopano;
extern tstring	g_strUserLocalAppDataKopano;
extern tstring	g_strKopanoDirectory;
extern ECMapProvider g_mapProviders;
extern tstring		g_strManufacturer;
extern tstring		g_strProductName;
extern tstring		g_strProductNameShort;
extern bool g_isOEM;
extern ULONG g_ulLoadsim;


#endif // ENTRYPOINT_H
