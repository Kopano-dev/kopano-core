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

#include <kopano/platform.h>
#include "ECACL.h"
#include <kopano/CommonUtil.h>
#include <kopano/stringutil.h>

#include <sstream>
#include <algorithm>

namespace KC {

// The data in this array must be sorted on the ulRights field.
struct AclRightName {
	unsigned ulRight;
	const char *szRight;

	bool operator<(const AclRightName &r) const { return ulRight < r.ulRight; }
};

static const AclRightName g_rights[] = {
	{RIGHTS_READ_ITEMS, "item read"},
	{RIGHTS_CREATE_ITEMS, "item create"},
	{RIGHTS_EDIT_OWN, "edit own"},
	{RIGHTS_DELETE_OWN, "delete own"},
	{RIGHTS_EDIT_ALL, "edit all"},
	{RIGHTS_DELETE_ALL, "delete all"},
	{RIGHTS_CREATE_SUBFOLDERS, "create sub"},
	{RIGHTS_FOLDER_OWNER, "own"},
	{RIGHTS_FOLDER_CONTACT, "contact"},
	{RIGHTS_FOLDER_VISIBLE, "view"}
};

// The data in this array must be sorted on the ulRights field.
struct AclRoleName {
	unsigned ulRights;
	const char *szRole;

	bool operator<(const AclRoleName &r) const { return ulRights < r.ulRights; }
};

static const AclRoleName g_roles[] = {
	{RIGHTS_NONE, "none"},	// Actually a right, but not seen as such by IsRight
	{ROLE_NONE, "none"},	// This might be confusing
	{ROLE_REVIEWER, "reviewer"},
	{ROLE_CONTRIBUTOR, "contributor"},
	{ROLE_NONEDITING_AUTHOR, "non-editting author"},
	{ROLE_AUTHOR, "author"},
	{ROLE_EDITOR, "editor"},
	{ROLE_PUBLISH_EDITOR, "publish editor"},
	{ROLE_PUBLISH_AUTHOR, "publish author"},
	{ROLE_OWNER, "owner"}
};

static inline bool IsRight(unsigned ulRights) {
	// A right has exactly 1 bit set. Otherwise it's a role
	return (ulRights ^ (ulRights - 1)) == 0;
}

static const AclRightName *FindAclRight(unsigned ulRights) {
	const AclRightName rn = {ulRights, NULL};
	const AclRightName *lpRightName = std::lower_bound(g_rights, ARRAY_END(g_rights), rn);
	if (lpRightName != ARRAY_END(g_rights) && lpRightName->ulRight == ulRights)
		return lpRightName;

	return NULL;
}

static const AclRoleName *FindAclRole(unsigned ulRights) {
	const AclRoleName rn = {ulRights, NULL};
	const AclRoleName *lpRoleName = std::lower_bound(g_roles, ARRAY_END(g_roles), rn);
	if (lpRoleName != ARRAY_END(g_roles) && lpRoleName->ulRights == ulRights)
		return lpRoleName;

	return NULL;
}

std::string AclRightsToString(unsigned ulRights)
{
	if (ulRights == unsigned(-1))
		return "missing or invalid";
	
	if (IsRight(ulRights)) {
		const AclRightName *lpRightName = FindAclRight(ulRights);
		if (lpRightName == NULL)
			return stringify(ulRights, true);
		return lpRightName->szRight;
	}

	const AclRoleName *lpRoleName = FindAclRole(ulRights);
	if (lpRoleName != NULL)
		return lpRoleName->szRole;

	std::ostringstream ostr;
	bool empty = true;
	for (unsigned bit = 0, mask = 1; bit < 32; ++bit, mask <<= 1) {
		if (ulRights & mask) {
			if (!empty)
				ostr << ",";
			empty = false;
			ostr << AclRightsToString(mask);
		}
	}
	return ostr.str();
}

} /* namespace */
