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

#ifndef ECENTRYID_H
#define ECENTRYID_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>

extern "C" {

extern _kc_export unsigned char *g_lpDefaultEid;
extern _kc_export const unsigned int g_cbDefaultEid;
extern _kc_export unsigned char *g_lpEveryoneEid;
extern _kc_export const unsigned int g_cbEveryoneEid;
extern _kc_export unsigned char *g_lpSystemEid;
extern _kc_export const unsigned int g_cbSystemEid;

HRESULT EntryIdIsDefault(unsigned int cbEntryId, const ENTRYID *lpEntryId, bool *lpbResult);
HRESULT EntryIdIsSystem(unsigned int cbEntryId, const ENTRYID *lpEntryId, bool *lpbResult);
extern _kc_export HRESULT EntryIdIsEveryone(unsigned int eid_size, const ENTRYID *eid, bool *result);
HRESULT GetNonPortableObjectId(unsigned int cbEntryId, const ENTRYID *lpEntryId, unsigned int *lpulObjectId);
extern _kc_export HRESULT GetNonPortableObjectType(unsigned int eid_size, const ENTRYID *eid, ULONG *obj_type);
extern _kc_export HRESULT GeneralizeEntryIdInPlace(unsigned int eid_size, ENTRYID *eid);

} /* extern "C" */

#endif
