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

#include <kopano/ECDefs.h>
#include "ECSecurityOffline.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

ECSecurityOffline::ECSecurityOffline(ECSession *lpSession, ECConfig *lpConfig) :
	ECSecurity(lpSession, lpConfig, NULL)
{
}

ECSecurityOffline::~ECSecurityOffline(void)
{
}

int ECSecurityOffline::GetAdminLevel()
{
	return 2;	// System admin. Highest admin level in multi-tenan environment, which is the case for offline servers.
}

ECRESULT ECSecurityOffline::IsAdminOverUserObject(unsigned int ulUserObjectId)
{
	return erSuccess;
}

ECRESULT ECSecurityOffline::IsAdminOverOwnerOfObject(unsigned int ulObjectId)
{
	return erSuccess;
}

ECRESULT ECSecurityOffline::IsUserObjectVisible(unsigned int ulUserObjectId)
{
	return erSuccess;
}

ECRESULT ECSecurityOffline::GetViewableCompanyIds(std::list<localobjectdetails_t> **lppObjects, unsigned int ulFlags)
{
	/*
	 * When using offline we return all companies within the offline database, this is correct behavior
	 * since the user will only have companies visible to him inside his database. Remember, during
	 * the sync getCompany() is used on the server and that will return MAPI_E_NOT_FOUND when the
	 * was not visible.
	 * When the rights change, the server will put that update into the sync list and the sync
	 * will try again to create the object in the offline database or delete it.
	 */
	return m_lpSession->GetUserManagement()->GetCompanyObjectListAndSync(CONTAINER_COMPANY, 0, lppObjects, ulFlags);
}

ECRESULT ECSecurityOffline::GetUserQuota(unsigned int ulUserId, quotadetails_t *lpDetails)
{
	if (lpDetails == NULL)
		return KCERR_INVALID_PARAMETER;
	lpDetails->bIsUserDefaultQuota = false;
	lpDetails->bUseDefaultQuota = false;
	lpDetails->llHardSize = 0;
	lpDetails->llSoftSize = 0;
	lpDetails->llWarnSize = 0;
	return erSuccess;
}
