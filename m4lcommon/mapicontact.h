/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __MAPI_CONTACT_H
#define __MAPI_CONTACT_H

namespace KC {

// http://blogs.msdn.com/stephen_griffin/archive/2005/10/25/new-outlook-documentation-part-1-contact-linking.aspx

// example usage:
// Used in MAPIToVMIME: PR_REPLY_RECIPIENT_ENTRIES when you choose a contact from your personal folders
// Used in Spooler: when sending a Fax

struct CONTAB_ENTRYID {
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
};
typedef struct CONTAB_ENTRYID *LPCONTAB_ENTRYID;

} /* namespace */

#endif
