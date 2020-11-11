/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <string>

namespace KC {

/* The Archiver API can return either a MAPI RIGHTS code (below) or the following special magic values. */
/* Error during retrieval */
#define ARCHIVE_RIGHTS_ERROR	static_cast<unsigned int>(-1)
/* Used for entities that are not security objects */
#define ARCHIVE_RIGHTS_ABSENT	static_cast<unsigned int>(-2)
/* ...did not even make it to read the "acl" table */
#define ARCHIVE_RIGHTS_UNKNOWN	static_cast<unsigned int>(-3)
/* "acl" SQL table has no row */
#define ARCHIVE_RIGHTS_MISSING	static_cast<unsigned int>(-4)

/* These are the values used by and for the IExchangeModifyTable API. */
enum ACLRIGHTS {
	RIGHTS_NONE              =     0,
	RIGHTS_READ_ITEMS        =   0x1,
	RIGHTS_CREATE_ITEMS      =   0x2,
	RIGHTS_EDIT_OWN          =   0x8,
	RIGHTS_DELETE_OWN        =  0x10,
	RIGHTS_EDIT_ALL          =  0x20,
	RIGHTS_DELETE_ALL        =  0x40,
	RIGHTS_CREATE_SUBFOLDERS =  0x80,
	RIGHTS_FOLDER_OWNER      = 0x100,
	RIGHTS_FOLDER_CONTACT    = 0x200,
	RIGHTS_FOLDER_VISIBLE    = 0x400,
	ROLE_NONE =
		RIGHTS_FOLDER_VISIBLE,
	ROLE_REVIEWER =
		ROLE_NONE | RIGHTS_READ_ITEMS,
	ROLE_CONTRIBUTOR =
		ROLE_NONE | RIGHTS_CREATE_ITEMS,

	ROLE_NONEDITING_AUTHOR =
		ROLE_REVIEWER | ROLE_CONTRIBUTOR | RIGHTS_DELETE_OWN,
	ROLE_AUTHOR =
		ROLE_NONEDITING_AUTHOR | RIGHTS_EDIT_OWN,
	ROLE_PUBLISH_AUTHOR =
		ROLE_AUTHOR | RIGHTS_CREATE_SUBFOLDERS,

	ROLE_EDITOR =
		ROLE_REVIEWER | ROLE_CONTRIBUTOR |
		RIGHTS_EDIT_ALL | RIGHTS_DELETE_ALL,
	ROLE_PUBLISH_EDITOR =
		ROLE_EDITOR | RIGHTS_CREATE_SUBFOLDERS,

	ROLE_SECRETARY =
		ROLE_EDITOR | ROLE_AUTHOR,
	ROLE_OWNER =
		ROLE_SECRETARY | RIGHTS_CREATE_SUBFOLDERS | RIGHTS_FOLDER_OWNER,
};

static_assert(ROLE_OWNER == 0x5fb);
static_assert(ROLE_PUBLISH_EDITOR == 0x4e3);
static_assert(ROLE_EDITOR == 0x463);
static_assert(ROLE_PUBLISH_AUTHOR == 0x49b);
static_assert(ROLE_AUTHOR == 0x41b);
static_assert(ROLE_NONEDITING_AUTHOR == 0x413);
static_assert(ROLE_REVIEWER == 0x401);
static_assert(ROLE_CONTRIBUTOR == 0x402);
static_assert(ROLE_NONE == 0x400);
static_assert(ROLE_SECRETARY == 0x47b);

extern KC_EXPORT std::string AclRightsToString(unsigned int rights);

} /* namespace */
