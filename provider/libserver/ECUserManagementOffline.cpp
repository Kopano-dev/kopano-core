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

#include "ECUserManagementOffline.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static const char THIS_FILE[] = __FILE__;
#endif

ECUserManagementOffline::ECUserManagementOffline(ECSession *lpSession,
    ECPluginFactory *lpPluginFactory, ECConfig *lpConfig) :
	ECUserManagement(lpSession, lpPluginFactory, lpConfig)
{
}

ECUserManagementOffline::~ECUserManagementOffline(void)
{
}

ECRESULT ECUserManagementOffline::GetUserQuotaDetailsAndSync(unsigned int ulId, quotadetails_t *lpDetails)
{
	ECRESULT er = erSuccess;

	lpDetails->bUseDefaultQuota = false;
	lpDetails->llWarnSize = 0;
	lpDetails->llSoftSize = 0;
	lpDetails->llHardSize = 0;

	return er; 
}

/*
Unused functions

ECRESULT ECUserManagementOffline::SetUserQuotaDetailsAndSync(unsigned int ulId, quotadetails_t details)
{
	ECRESULT er = erSuccess;
	// Nothing to set
	return er;
}

ECRESULT ECUserManagementOffline::GetGroupDetailsAndSync(unsigned int ulGroupId, groupdetails_t *lpDetails)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::GetUserListAndSync(std::list<localuserdetails_t> **lppUsers, unsigned int ulFlags)
{
	ECRESULT er = erSuccess;
	userdetails_t details;

	std::list<localuserdetails_t> *lpUsers = new std::list<localuserdetails_t>;

	if(! (ulFlags & USERMANAGEMENT_IDS_ONLY)) {
		er = GetUserDetailsAndSync(m_ulUserId, &details);
		if(er != erSuccess)
			goto exit;
	}
	
	lpUsers->push_back(localuserdetails_t(m_ulUserId, details));


	*lppUsers = lpUsers;

exit:
	return er;
}

ECRESULT ECUserManagementOffline::GetGroupListAndSync(std::list<localgroupdetails> **lppGroups, unsigned int ulFlags)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::GetMembersOfGroupAndSync(unsigned int ulGroupId, std::list<localuserdetails_t> **lppUsers, unsigned int ulFlags)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::GetUserGroupListAndSync(std::list<localuserobjectdetails> **lppUserGroups, unsigned int ulFlags)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::SetUserDetailsAndSync(unsigned int ulUserId, userdetails_t sDetails, int update)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::GetGroupMembershipAndSync(unsigned int ulUserId, std::list<localgroupdetails> **lppGroups, unsigned int ulFlags)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::SetGroupDetailsAndSync(unsigned int ulGroupId, groupdetails_t sDetails, int update)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::AddMemberToGroupAndSync(unsigned int ulGroupId, unsigned int ulUserId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::DeleteMemberFromGroupAndSync(unsigned int ulGroupId, unsigned int ulUserId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::ResolveUserAndSync(char *szUsername, unsigned int *lpulUserId, bool *lpbIsNonActive)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	//if(lpbIsNonActive)
	//	*lpbIsNonActive = false;

	// *lpulUserId = m_ulUserId;

	return er;
}

ECRESULT ECUserManagementOffline::ResolveGroupAndSync(char *szGroupname, unsigned int *lpulGroupId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::SearchPartialUserAndSync(char *szSearchString, unsigned int *lpulId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::SearchPartialGroupAndSync(char *szSearchString, unsigned int *lpulId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::CreateUserAndSync(userdetails_t details, unsigned int *lpulId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::DeleteUserAndSync(unsigned int ulId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::CreateGroupAndSync(groupdetails_t details, unsigned int *ulId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::DeleteGroupAndSync(unsigned int ulId)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}

ECRESULT ECUserManagementOffline::GetProps(struct soap *soap, unsigned int ulId, struct propTagArray *lpPropTagArray, struct propValArray *lppPropValArray)
{
	ECRESULT er = erSuccess;

	er = KCERR_NO_SUPPORT;

	return er;
}
*/
