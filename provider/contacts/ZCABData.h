/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ZCABDATA_H
#define ZCABDATA_H

struct cabEntryID {
	ULONG ulVersion;			/* always 0 */
	MAPIUID muid;				/* provider uid */
	ULONG ulObjType;			/* ab object types */
	ULONG ulOffset;				/* offset in m_lpFolders or email address of contact object */
	BYTE origEntryID[1];		/* entryid of wrapped object */
};
#define CbNewCABENTRYID(cb) (offsetof(cabEntryID, origEntryID) + (cb))

#endif
