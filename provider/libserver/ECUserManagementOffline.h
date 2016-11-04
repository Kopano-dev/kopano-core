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

#include <kopano/zcdefs.h>
#include "ECUserManagement.h"

class ECUserManagementOffline _kc_final : public ECUserManagement {
public:
	ECUserManagementOffline(ECSession *lpSession, ECPluginFactory *lpPluginFactory, ECConfig *lpConfig);
	//virtual ECRESULT	AuthUserAndSync(char *szUsername, char *szPassword, unsigned int *lpulUserId);
	//virtual ECRESULT	GetUserDetailsAndSync(unsigned int ulUserId, userdetails_t *lpDetails);
	virtual ECRESULT	GetUserQuotaDetailsAndSync(unsigned int ulUserId, quotadetails_t *lpDetails);
	/*
	virtual ECRESULT	SetUserQuotaDetailsAndSync(unsigned int ulUserId, quotadetails_t sDetails);
	virtual ECRESULT	GetGroupDetailsAndSync(unsigned int ulGroupId, groupdetails_t *lpDetails);
	virtual ECRESULT	GetUserListAndSync(std::list<localuserdetails> **lppUsers, unsigned int ulFlags = 0);
	virtual ECRESULT	GetGroupListAndSync(std::list<localgroupdetails> **lppGroups, unsigned int ulFlags = 0);
	virtual ECRESULT	GetUserGroupListAndSync(std::list<localuserobjectdetails> **lppUserGroups, unsigned int ulFlags = 0);
	virtual ECRESULT	GetMembersOfGroupAndSync(unsigned int ulGroupId, std::list<localuserdetails> **lppUsers, unsigned int ulFlags = 0);
	virtual ECRESULT	GetGroupMembershipAndSync(unsigned int ulUserId, std::list<localgroupdetails> **lppGroups, unsigned int ulFlags = 0);
	virtual ECRESULT	SetUserDetailsAndSync(unsigned int ulUserId, userdetails_t sDetails, int update);
	virtual ECRESULT	SetGroupDetailsAndSync(unsigned int ulGroupId, groupdetails_t sDetails, int update);
	virtual ECRESULT	AddMemberToGroupAndSync(unsigned int ulGroupId, unsigned int ulUserId);
	virtual ECRESULT	DeleteMemberFromGroupAndSync(unsigned int ulGroupId, unsigned int ulUserId);


	virtual ECRESULT	ResolveUserAndSync(char *szUsername, unsigned int *lpulUserId, bool *lpbIsNonActive = NULL);
	virtual ECRESULT	ResolveGroupAndSync(char *szGroupname, unsigned int *lpulGroupId);
	virtual ECRESULT	SearchPartialUserAndSync(char *szSearchString, unsigned int *lpulId);
	virtual ECRESULT	SearchPartialGroupAndSync(char *szSearchString, unsigned int *lpulId);

	// Create a user
	virtual ECRESULT	CreateUserAndSync(userdetails_t details, unsigned int *ulId);
	// Delete a user
	virtual ECRESULT	DeleteUserAndSync(unsigned int ulId);
	// Create a group
	virtual ECRESULT	CreateGroupAndSync(groupdetails_t details, unsigned int *ulId);
	// Delete a group
	virtual ECRESULT	DeleteGroupAndSync(unsigned int ulId);

	// Get MAPI property data for a group or user/group id, with on-the-fly delete of the specified user/group
	virtual ECRESULT	GetProps(struct soap *soap, unsigned int ulId, struct propTagArray *lpPropTagArray, struct propValArray *lppPropValArray);
	*/

	/* override QueryRowData() and GetUserList() ? */

	/*
	// AddressBook functions -- override?
	//virtual ECRESULT GetUserIDList(unsigned int ulParentID, unsigned int ulObjType, unsigned int** lppulUserList, unsigned int* lpulSize);
	//virtual ECRESULT ResolveUser(struct soap *soap, struct propValArray* lpPropValArraySearch, struct propTagArray* lpsPropTagArray, struct propValArray* lpPropValArrayDst, unsigned int* lpulFlag);
	//virtual ECRESULT ResolveGroup(struct soap *soap, struct propValArray* lpPropValArraySearch, struct propTagArray* lpsPropTagArray, struct propValArray* lpPropValArrayDst, unsigned int* lpulFlag);
	*/
};
