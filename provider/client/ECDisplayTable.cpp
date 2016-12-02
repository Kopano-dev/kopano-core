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
#include <kopano/ECGetText.h>

#include <kopano/CommonUtil.h>

#include "Mem.h"
#include <kopano/ECMemTable.h>

// This function is NOT used, but is used as dummy for the xgettext parser so it
// can find the translation strings. **DO NOT REMOVE THIS FUNCTION**
static UNUSED_VAR void dummy(void)
{
	LPCTSTR a UNUSED_VAR;

	/* User General tab */
	a = _("General");
	a = _("Name");
	a = _("First:");
	a = _("Initials:");
	a = _("Last:");
	a = _("Display:");
	a = _("Alias:");
	a = _("Address:");
	a = _("City:");
	a = _("State:");
	a = _("Zip code:");
	a = _("Country/Region:");
	a = _("Title:");
	a = _("Company:");
	a = _("Department:");
	a = _("Office:");
	a = _("Assistant:");
	a = _("Phone:");

	/* User Phone/Notes tab */
	a = _("Phone/Notes");
	a = _("Phone numbers");
	a = _("Business:");
	a = _("Business 2:");
	a = _("Fax:");
	a = _("Assistant:");
	a = _("Home:");
	a = _("Home 2:");
	a = _("Mobile:");
	a = _("Pager:");
	a = _("Notes:");

	/* User Organization tab */
	a = _("Organization");
	a = _("Manager:");
	a = _("Direct reports:");

	/* User Member Of tab */
	a = _("Member Of");
	a = _("Group membership:");

	/* User E-mail Addresses tab */
	a = _("E-mail Addresses");
	a = _("E-mail addresses:");

	/* Distlist General tab */
	a = _("General");
	a = _("Display name:");
	a = _("Alias name:");
	a = _("Owner:");
	a = _("Notes:");
	a = _("Members");
	a = _("Modify members...");

	/* Distlist Member Of tab */
	a = _("Member Of");
	a = _("Group membership:");
}
