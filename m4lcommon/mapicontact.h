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

#ifndef __MAPI_CONTACT_H
#define __MAPI_CONTACT_H

// http://blogs.msdn.com/stephen_griffin/archive/2005/10/25/new-outlook-documentation-part-1-contact-linking.aspx

// example usage:
// Used in MAPIToVMIME: PR_REPLY_RECIPIENT_ENTRIES when you choose a contact from your personal folders
// Used in Spooler: when sending a Fax

typedef struct _s_CONTAB_ENTRYID
{
	BYTE misc1[4];
	MAPIUID muid;
	ULONG misc3;
	ULONG misc4;

	// 0..2 == reply to email offsets
	// 3..5 == fax email offsets
	ULONG email_offset;				// email address offset in contact (address 1, 2 or 3)

	// EntryID of contact in store.
	ULONG cbeid;
	BYTE abeid[1];
	BYTE padding[3];			/* not mentioned, but it's there */
} CONTAB_ENTRYID, *LPCONTAB_ENTRYID;

#endif
