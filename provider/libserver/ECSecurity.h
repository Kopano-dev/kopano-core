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

// ECSecurity.h: interface for the ECSecurity class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ECSECURITY
#define ECSECURITY

#include "ECUserManagement.h"
#include "plugin.h"

#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include <kopano/ECDefs.h>

class ECSession;

#define EC_NO_IMPERSONATOR		((unsigned int)-1)

class ECSecurity  
{

public:
	ECSecurity(ECSession *lpSession, ECConfig *lpConfig, ECLogger *lpAudit);
	virtual ~ECSecurity();

	/* must be called once the object is created */
	virtual ECRESULT SetUserContext(unsigned int ulUserId, unsigned int ulImpersonatorID);

	virtual ECRESULT CheckDeletedParent(unsigned int ulId);
	virtual ECRESULT CheckPermission(unsigned int ulObjId, unsigned int ulCheckRights);

	virtual ECRESULT GetRights(unsigned int objid, int ulType, struct rightsArray *lpsRightsArray);
	virtual ECRESULT SetRights(unsigned int objid, struct rightsArray *lpsRightsArray);

	virtual ECRESULT GetUserCompany(unsigned int *lpulCompanyId);

	// Functions to determine which companies are visible, and which userobjects are
	// visible to the currently logged in user.
	virtual ECRESULT GetViewableCompanyIds(unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects);
	virtual ECRESULT IsUserObjectVisible(unsigned int ulUserObjectId);

	// Get the owner of an object
	virtual ECRESULT GetOwner(unsigned int ulObjId, unsigned int *lpulOwnerId);

	// get the store owner, you can give every object id
	virtual ECRESULT GetStoreOwner(unsigned int ulObjId, unsigned int* lpulOwnerId);
	virtual ECRESULT GetStoreOwnerAndType(unsigned int ulObjId, unsigned int* lpulOwnerId, unsigned int* lpulStoreType);
	
	virtual ECRESULT GetObjectPermission(unsigned int ulObjId, unsigned int* lpulRights);

	virtual unsigned int GetUserId(unsigned int ulObjId = 0);
	virtual ECRESULT IsOwner(unsigned int ulObjId);
	virtual ECRESULT IsStoreOwner(unsigned int ulStoreId);
	virtual int GetAdminLevel();

	// Functions to determine if the user is the Administrator
	// over the company to which the user/object/store belongs.
	virtual ECRESULT IsAdminOverUserObject(unsigned int ulUserObjectId);
	virtual ECRESULT IsAdminOverOwnerOfObject(unsigned int ulObjectId);

	// Quota functions
	virtual ECRESULT CheckQuota(unsigned int ulStoreId, long long llStoreSize, eQuotaStatus* lpQuotaStatus);
	virtual ECRESULT CheckUserQuota(unsigned int ulUserId, long long llStoreSize, eQuotaStatus *lpQuotaStatus);
	virtual ECRESULT GetStoreSize(unsigned int ulObjId, long long* lpllStoreSize);
	virtual ECRESULT GetUserSize(unsigned int ulUserId, long long* lpllUserSize);
	virtual ECRESULT GetUserQuota(unsigned int ulUserId, bool bGetUserDefault, quotadetails_t *lpDetails);

	// information for ECSessionStatsTable
	virtual ECRESULT GetUsername(std::string *lpstrUsername);
    virtual ECRESULT GetImpersonator(std::string *lpstrUsername);

	virtual unsigned int GetObjectSize();

private:
	ECRESULT GetGroupsForUser(unsigned int ulUserId, std::list<localobjectdetails_t> **lppGroups);
	ECRESULT GetViewableCompanies(unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects);
	ECRESULT GetAdminCompanies(unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects);
	ECRESULT HaveObjectPermission(unsigned int ulObjId, unsigned int ulACLMask);

protected:
	ECSession			*m_lpSession;
	ECLogger			*m_lpAudit;
	ECConfig			*m_lpConfig;

	unsigned int		m_ulUserID; // current user id
    unsigned int        m_ulImpersonatorID; // id of user that is impersonating the current user
	unsigned int		m_ulCompanyID; // Company to which the user belongs to
	objectdetails_t		m_details;
    objectdetails_t 	m_impersonatorDetails;
	bool				m_bRestrictedAdmin; // True if restricted admin permissions enabled
	bool 				m_bOwnerAutoFullAccess;
	std::list<localobjectdetails_t> *m_lpGroups; // current user groups
	std::list<localobjectdetails_t> *m_lpViewCompanies; // current visible companies
	std::list<localobjectdetails_t> *m_lpAdminCompanies; // Companies where the user has admin rights on
};

#endif // #ifndef ECSECURITY
