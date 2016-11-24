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

#ifndef ECACL_INCLUDED
#define ECACL_INCLUDED

#include <kopano/zcdefs.h>
#include <string>

namespace KC {

enum ACLRIGHTS { 
	RIGHTS_EDIT_OWN = 0x8,
	RIGHTS_EDIT_ALL = 0x20,
	RIGHTS_DELETE_OWN = 0x10,
	RIGHTS_DELETE_ALL = 0x40,
	RIGHTS_READ_ITEMS = 0x1,
	RIGHTS_CREATE_ITEMS = 0x2,
	RIGHTS_CREATE_SUBFOLDERS = 0x80,
	RIGHTS_FOLDER_OWNER = 0x100,
	RIGHTS_FOLDER_CONTACT = 0x200,
	RIGHTS_FOLDER_VISIBLE = 0x400,
	RIGHTS_NONE = 0,
	ROLE_OWNER = 0x5fb,
	ROLE_PUBLISH_EDITOR = 0x4e3,
	ROLE_EDITOR = 0x463,
	ROLE_PUBLISH_AUTHOR = 0x49b,
	ROLE_AUTHOR = 0x41b,
	ROLE_NONEDITING_AUTHOR = 0x413,
	ROLE_REVIEWER = 0x401,
	ROLE_CONTRIBUTOR = 0x402,
	ROLE_NONE = 0x400
};

extern _kc_export std::string AclRightsToString(unsigned int rights);

} /* namespace */

#endif // ndef ECACL_INCLUDED
