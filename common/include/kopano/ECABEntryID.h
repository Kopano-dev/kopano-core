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

#include <mapidefs.h>

extern unsigned char		*g_lpDefaultEid;
extern const unsigned int	g_cbDefaultEid;

extern unsigned char		*g_lpEveryoneEid;
extern const unsigned int	g_cbEveryoneEid;

extern unsigned char		*g_lpSystemEid;
extern const unsigned int	g_cbSystemEid;

HRESULT EntryIdIsDefault(unsigned int cbEntryId, const ENTRYID *lpEntryId, bool *lpbResult);
HRESULT EntryIdIsSystem(unsigned int cbEntryId, const ENTRYID *lpEntryId, bool *lpbResult);
HRESULT EntryIdIsEveryone(unsigned int cbEntryId, const ENTRYID *lpEntryId, bool *lpbResult);
HRESULT GetNonPortableObjectId(unsigned int cbEntryId, const ENTRYID *lpEntryId, unsigned int *lpulObjectId);
HRESULT GetNonPortableObjectType(unsigned int cbEntryId, const ENTRYID *lpEntryId, ULONG *lpulObjectType);
extern HRESULT GeneralizeEntryIdInPlace(unsigned int eid_size, ENTRYID *eid);

#endif
