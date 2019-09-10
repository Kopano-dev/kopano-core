/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <list>
#include <memory>
#include <utility>
#include "ECDatabaseUtils.h"
#include "ECDatabase.h"
#include "ECSessionManager.h"
#include "ECSession.h"
#include <kopano/ECDefs.h>
#include "ECSecurity.h"
#include <kopano/stringutil.h>
#include "kcore.hpp"
#include <mapidefs.h>
#include <mapicode.h>
#include <mapitags.h>
#include <kopano/mapiext.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/bio.h>
#include <algorithm>
#include "ECUserManagement.h"
#include "SOAPUtils.h"
#include <edkmdb.h>
#include "ECDBDef.h"
#include "cmdutil.hpp"

namespace KC {

#define MAX_PARENT_LIMIT 64

static const char *RightsToString(unsigned int ulecRights)
{
	switch (ulecRights) {
	case ecSecurityRead: return "read";
	case ecSecurityCreate: return "create";
	case ecSecurityEdit: return "edit";
	case ecSecurityDelete: return "delete";
	case ecSecurityCreateFolder: return "change hierarchy";
	case ecSecurityFolderVisible: return "view";
	case ecSecurityFolderAccess: return "folder permissions";
	case ecSecurityOwner: return "owner";
	case ecSecurityAdmin: return "admin";
	default: return "none";
	}
}

/**
 * @param[in] lpSession user session
 * @param[in] lpConfig config object
 * @param[in] lpLogger log object for normal logging
 * @param[in] lpAudit optional log object for auditing
 */
ECSecurity::ECSecurity(ECSession *lpSession, std::shared_ptr<ECConfig> c,
    std::shared_ptr<ECLogger> lpAudit) :
	m_lpSession(lpSession), m_lpAudit(std::move(lpAudit)), m_lpConfig(std::move(c)),
	m_bRestrictedAdmin(parseBool(m_lpConfig->GetSetting("restrict_admin_permissions"))),
	m_bOwnerAutoFullAccess(parseBool(m_lpConfig->GetSetting("owner_auto_full_access")))
{}

/**
 * Called once for each login after the object was constructed. Since
 * this function can return errors, this is not done in the
 * constructor.
 *
 * @param[in] ulUserId current logged in user
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::SetUserContext(unsigned int ulUserId, unsigned int ulImpersonatorID)
{
	ECUserManagement *lpUserManagement = m_lpSession->GetUserManagement();

	m_ulUserID = ulUserId;
	m_ulImpersonatorID = ulImpersonatorID;
	auto er = lpUserManagement->GetObjectDetails(m_ulUserID, &m_details);
	if(er != erSuccess)
		return er;

	// Get the company we're assigned to
	auto sesmgr = m_lpSession->GetSessionManager();
	if (sesmgr->IsHostedSupported())
		m_ulCompanyID = m_details.GetPropInt(OB_PROP_I_COMPANYID);
	else
		m_ulCompanyID = 0;

	if (m_ulImpersonatorID == EC_NO_IMPERSONATOR)
		/* Don't initialize… (see below) */
		return erSuccess;
	er = lpUserManagement->GetObjectDetails(m_ulImpersonatorID, &m_impersonatorDetails);
	if (er != erSuccess)
		return er;
	unsigned int ulAdminLevel = m_impersonatorDetails.GetPropInt(OB_PROP_I_ADMINLEVEL);
	if (ulAdminLevel == 0) {
		return KCERR_NO_ACCESS;
	} else if (sesmgr->IsHostedSupported() && ulAdminLevel < ADMIN_LEVEL_SYSADMIN) {
		unsigned int ulCompanyID = m_impersonatorDetails.GetPropInt(OB_PROP_I_COMPANYID);
		if (ulCompanyID != m_ulCompanyID)
			return KCERR_NO_ACCESS;
	}
	/*
	 * Don't initialize m_lpGroups, m_lpViewCompanies and m_lpAdminCompanies
	 * We should wait with that until the first time we actually use it,
	 * this will save quite a lot of LDAP queries since often we don't
	 * even need the list at all.
	 */
	return erSuccess;
}

// helper class to remember groups we've seen to break endless loops
class cUniqueGroup {
public:
	bool operator()(const localobjectdetails_t &obj) const
	{
		return m_seen.find(obj) != m_seen.end();
	}

	std::set<localobjectdetails_t> m_seen;
};
/**
 * This function returns a list of security groups the user is a
 * member of. If a group contains a group, it will be appended to the
 * list. The list will be a unique list of groups in the end.
 *
 * @param[in]  ulUserId A user or group to query the grouplist for.
 * @param[out] lppGroups The unique list of group ids
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetGroupsForUser(unsigned int ulUserId,
    std::unique_ptr<std::list<localobjectdetails_t>> &group_out)
{
	std::list<localobjectdetails_t> groups;
	cUniqueGroup cSeenGroups;

	/* Gets the current user's membership information.
	 * This means you will be in the same groups until you login again */
	auto usrmgt = m_lpSession->GetUserManagement();
	auto er = usrmgt->GetParentObjectsOfObjectAndSync(OBJECTRELATION_GROUP_MEMBER,
		ulUserId, groups, USERMANAGEMENT_IDS_ONLY);
	if (er != erSuccess)
		return er;

	/* A user is only member of a group when he can also view the group */
	for (auto iterGroups = groups.begin(); iterGroups != groups.cend(); ) {
		/*
		 * Since this function is only used by ECSecurity, we can only
		 * test for security groups here. However, normal groups were
		 * used to be security enabled, so only check for dynamic
		 * groups here to exclude.
		 */
		if (IsUserObjectVisible(iterGroups->ulId) != erSuccess || iterGroups->GetClass() == DISTLIST_DYNAMIC) {
			iterGroups = groups.erase(iterGroups);
			continue;
		}
		cSeenGroups.m_seen.emplace(*iterGroups);

		std::list<localobjectdetails_t> gig; /* groups in groups */
		er = usrmgt->GetParentObjectsOfObjectAndSync(OBJECTRELATION_GROUP_MEMBER,
		     iterGroups->ulId, gig, USERMANAGEMENT_IDS_ONLY);
		if (er == erSuccess)
			// Adds all groups from lpGroupInGroups to the main lpGroups list, except when already in cSeenGroups
			std::remove_copy_if(gig.cbegin(), gig.cend(), std::back_inserter(groups), cSeenGroups);
		// Ignore error (e.g. cannot use that function on group Everyone)
		++iterGroups;
	}
	group_out = make_unique_nt<decltype(groups)>(std::move(groups));
	if (group_out == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	return erSuccess;
}

/**
 * Return the bitmask of permissions for an object
 *
 * @param[in] ulObjId hierarchy object to get permission mask for
 * @param[out] lpulRights permission mask
 *
 * @return always erSuccess
 */
ECRESULT ECSecurity::GetObjectPermission(unsigned int ulObjId, unsigned int* lpulRights)
{
	struct rightsArray *lpRights = NULL;
	unsigned int ulCurObj = ulObjId, ulDepth = 0;
	bool 			bFoundACL = false;

	*lpulRights = 0;

	// Get the deepest GRANT ACL that applies to this user or groups that this user is in
	// WARNING we totally ignore DENY ACLs here. This means that the deepest GRANT counts. In practice
	// this doesn't matter because GRANTmask = ~DENYmask.
	auto cache = m_lpSession->GetSessionManager()->GetCacheManager();
	while(true)
	{
		if (cache->GetACLs(ulCurObj, &lpRights) == erSuccess) {
			/* This object has ACLs, check if any of them are for this user. */
			for (gsoap_size_t i = 0; i < lpRights->__size; ++i)
				if(lpRights->__ptr[i].ulType == ACCESS_TYPE_GRANT && lpRights->__ptr[i].ulUserid == m_ulUserID) {
					*lpulRights |= lpRights->__ptr[i].ulRights;
					bFoundACL = true;
				}
			// Check for the company we are in and add the permissions
			for (gsoap_size_t i = 0; i < lpRights->__size; ++i)
				if (lpRights->__ptr[i].ulType == ACCESS_TYPE_GRANT && lpRights->__ptr[i].ulUserid == m_ulCompanyID) {
					*lpulRights |= lpRights->__ptr[i].ulRights;
					bFoundACL = true;
				}
			// Also check for groups that we are in, and add those permissions
			if (m_lpGroups != nullptr || GetGroupsForUser(m_ulUserID, m_lpGroups) == erSuccess)
				for (const auto &grp : *m_lpGroups)
					for (gsoap_size_t i = 0; i < lpRights->__size; ++i)
						if (lpRights->__ptr[i].ulType == ACCESS_TYPE_GRANT &&
						    lpRights->__ptr[i].ulUserid == grp.ulId) {
							*lpulRights |= lpRights->__ptr[i].ulRights;
							bFoundACL = true;
						}
		}

		FreeRightsArray(lpRights);
		lpRights = nullptr;
		if (bFoundACL)
			// If any of the ACLs at this level were for us, then use these ACLs.
			break;
		// There were no ACLs or no ACLs for us, go to the parent and try there
		auto er = cache->GetParent(ulCurObj, &ulCurObj);
		if (er != erSuccess)
			// No more parents, break (with ulRights = 0)
			return erSuccess;
		// This can really only happen if you have a broken tree in the database, eg a record which has
		// parent == id. To break out of the loop we limit the depth to 64 which is very deep in practice. This means
		// that you never have any rights for folders that are more than 64 levels of folders away from their ACL ..
		if (++ulDepth > MAX_PARENT_LIMIT) {
			ec_log_err("Maximum depth reached for object %d, deepest object: %d", ulObjId, ulCurObj);
			return erSuccess;
		}
	}
	return erSuccess;
}

/**
 * Check permission for a certain object id
 *
 * This function checks if ANY of the passed permissions are granted on the passed object
 * for the currently logged-in user.
 *
 * @param[in] ulObjId Object ID for which the permission should be checked
 * @param[in] ulACLMask Mask of permissions to be checked
 * @return Kopano error code
 * @retval erSuccess if permission is granted
 * @retval KCERR_NO_ACCESS if permission is denied
 */
ECRESULT ECSecurity::HaveObjectPermission(unsigned int ulObjId, unsigned int ulACLMask)
{
	unsigned int	ulRights = 0;
	GetObjectPermission(ulObjId, &ulRights);
	return (ulRights & ulACLMask) ? erSuccess : KCERR_NO_ACCESS;
}

/**
 * Checks if you are the owner of the given object id. This can return
 * no access, since other people may have created an object in the
 * owner's store (or true if you're that someone).
 *
 * @param[in] ulObjId hierarchy object to check ownership of
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::IsOwner(unsigned int ulObjId) const
{
	unsigned int	ulOwner = 0;
	auto er = GetOwner(ulObjId, &ulOwner);
	return er != erSuccess || ulOwner != m_ulUserID ? KCERR_NO_ACCESS : erSuccess;
}

/**
 * Get the original creator of an object
 *
 * @param[in] ulObjId hierarchy object to get ownership of
 * @param[out] lpulOwnerId owner userid (may not even exist anymore)
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetOwner(unsigned int ulObjId,
    unsigned int *lpulOwnerId) const
{
	// Default setting
	*lpulOwnerId = 0;
	auto er = m_lpSession->GetSessionManager()->GetCacheManager()->GetOwner(ulObjId, lpulOwnerId);
	return er != erSuccess ? KCERR_NOT_FOUND : er;
}

/**
 * Check for a deleted folder as parent of ulId, max folder depth as
 * defined (64).
 *
 * @param[in] ulId object id to start checking from
 *
 * @return KCERR_NOT_FOUND Error if a parent has the delete flag
 */
ECRESULT ECSecurity::CheckDeletedParent(unsigned int ulId) const
{
	ECRESULT er = erSuccess;
	unsigned int ulParentObjId = 0, ulObjFlags = 0, ulObjType = 0, ulDepth = 0;
	ECCacheManager *lpCache = m_lpSession->GetSessionManager()->GetCacheManager();

	do {
		er = lpCache->GetObject(ulId, &ulParentObjId, NULL, &ulObjFlags, &ulObjType);
		if (er != erSuccess)
			return er;
		if (ulObjFlags & MSGFLAG_DELETED)
			return KCERR_NOT_FOUND;
		ulId = ulParentObjId;
		++ulDepth;
	} while (ulObjType != MAPI_STORE && ulParentObjId != CACHE_NO_PARENT && ulDepth <= MAX_PARENT_LIMIT);

	// return error when max depth is reached, so we don't create folders and messages deeper than the limit
	if (ulDepth == MAX_PARENT_LIMIT)
		er = KCERR_NOT_FOUND;
	return er;
}

/**
 * For the current user context, check the permissions on a given object
 *
 * @param[in] ulObjId hierarchy object to check permissions on
 * @param[in] ulecRights minimal permission required on object to succeed
 *
 * @return Kopano error code
 * @retval erSuccess requested access on object allowed
 * @retval KCERR_NO_ACCESS requested access on object denied
 */
ECRESULT ECSecurity::CheckPermission(unsigned int ulObjId, unsigned int ulecRights)
{
	ECRESULT		er = KCERR_NO_ACCESS;
	bool			bOwnerFound = false;
	unsigned int ulStoreOwnerId = 0, ulStoreType = 0, ulObjectOwnerId = 0;
	unsigned int ulACL = 0, ulObjType, ulParentId, ulParentType;
	int				nCheckType = 0;
	auto cache = m_lpSession->GetSessionManager()->GetCacheManager();

	if(m_ulUserID == KOPANO_UID_SYSTEM) {
		// SYSTEM is always allowed everything
		er = erSuccess;
		goto exit;
	}

	// special case: stores and root containers are always allowed to be opened
	if (ulecRights == ecSecurityFolderVisible || ulecRights == ecSecurityRead) {
		er = cache->GetObject(ulObjId, &ulParentId, nullptr, nullptr, &ulObjType);
		if (er != erSuccess)
			goto exit;
		if (ulObjType == MAPI_STORE)
			goto exit;
		er = cache->GetObject(ulParentId, nullptr, nullptr, nullptr, &ulParentType);
		if (er != erSuccess)
			goto exit;
		if (ulObjType == MAPI_FOLDER && ulParentType == MAPI_STORE)
			goto exit;
	}

	// Is the current user the owner of the store
	if (GetStoreOwnerAndType(ulObjId, &ulStoreOwnerId, &ulStoreType) == erSuccess && ulStoreOwnerId == m_ulUserID) {
		if (ulStoreType != ECSTORE_TYPE_ARCHIVE) {
			er = erSuccess;
			goto exit;
		} else if (ulecRights == ecSecurityFolderVisible || ulecRights == ecSecurityRead) {
			er = erSuccess;
			goto exit;
		}
	}

	// is current user the owner of the object
	if (GetOwner(ulObjId, &ulObjectOwnerId) == erSuccess && ulObjectOwnerId == m_ulUserID && m_bOwnerAutoFullAccess)
	{
		bOwnerFound = true;
		if (ulStoreType == ECSTORE_TYPE_ARCHIVE) {
			if(ulecRights == ecSecurityFolderVisible || ulecRights == ecSecurityRead) {
				er = erSuccess;
				goto exit;
			}
		} else if(ulecRights == ecSecurityFolderVisible || ulecRights == ecSecurityRead || ulecRights == ecSecurityCreate) {
			er = erSuccess;
			goto exit;
		}
	}

	// Since this is the most complicated check, do this one last
	if(IsAdminOverOwnerOfObject(ulObjId) == erSuccess) {
		if(!m_bRestrictedAdmin) {
			er = erSuccess;
			goto exit;
		}
		// If restricted admin mode is set, admins only receive folder permissions.
		if(ulecRights == ecSecurityFolderVisible || ulecRights == ecSecurityFolderAccess || ulecRights == ecSecurityCreateFolder) {
			er = erSuccess;
			goto exit;
		}
	}

	ulACL = 0;
	switch(ulecRights){
	case ecSecurityRead: // 1
		ulACL |= ecRightsReadAny;
		nCheckType = 1;
		break;
	case ecSecurityCreate: // 2
		ulACL |= ecRightsCreate;
		nCheckType = 1;
		break;
	case ecSecurityEdit: // 3
		ulACL |= ecRightsEditAny;
		if(bOwnerFound)
			ulACL |= ecRightsEditOwned;
		nCheckType = 1;
		break;
	case ecSecurityDelete: // 4
		ulACL |= ecRightsDeleteAny;
		if(bOwnerFound)
			ulACL |= ecRightsDeleteOwned;
		nCheckType = 1;
		break;
	case ecSecurityCreateFolder: // 5
		ulACL |= ecRightsCreateSubfolder;
		nCheckType = 1;
		break;
	case ecSecurityFolderVisible: // 6
		ulACL |= ecRightsFolderVisible;
		nCheckType = 1;
		break;
	case ecSecurityFolderAccess: // 7
		if (!bOwnerFound || ulStoreType == ECSTORE_TYPE_ARCHIVE)
			ulACL |= ecRightsFolderAccess;
		nCheckType = 1;
		break;
	case ecSecurityOwner: // 8
		nCheckType = 2;
		break;
	case ecSecurityAdmin: // 9
		nCheckType = 3;
		break;
	default:
		nCheckType = 0; // No rights
		break;
	}

	if(nCheckType == 1) { //Get the acl of the object
		if(ulACL == 0) {
			// No ACLs required, so grant access
			er = erSuccess;
			goto exit;
		}
		er = HaveObjectPermission(ulObjId, ulACL);
	} else if(nCheckType == 2) {// Is owner ?
		if (bOwnerFound)
			er = erSuccess;
	} else if(nCheckType == 3) { // Is admin?
		// We already checked IsAdminOverOwnerOfObject() above, so we'll never get here.
		er = erSuccess;
	}

exit:
	if (er == erSuccess && (ulecRights == ecSecurityCreate || ulecRights == ecSecurityEdit || ulecRights == ecSecurityCreateFolder))
		// writing in a deleted parent is not allowed
		er = CheckDeletedParent(ulObjId);

	if (m_lpAudit && m_ulUserID != KOPANO_UID_SYSTEM) {
		unsigned int ulType = 0;
		objectdetails_t sStoreDetails;
		std::string strStoreOwner, strUsername;

		cache->GetObject(ulObjId, nullptr, nullptr, nullptr, &ulType);
		if (er == KCERR_NO_ACCESS || ulStoreOwnerId != m_ulUserID) {
			GetUsername(&strUsername);
			if (ulStoreOwnerId == m_ulUserID)
				strStoreOwner = strUsername;
			else if (m_lpSession->GetUserManagement()->GetObjectDetails(ulStoreOwnerId, &sStoreDetails) != erSuccess)
				// should not really happen on store owners?
				strStoreOwner = "<non-existing>";
			else
				strStoreOwner = sStoreDetails.GetPropString(OB_PROP_S_LOGIN);
		}

		if (er == KCERR_NO_ACCESS)
			m_lpAudit->logf(EC_LOGLEVEL_FATAL, "access denied objectid=%d type=%d ownername=\"%s\" username=\"%s\" rights=\"%s\"",
						   ulObjId, ulType, strStoreOwner.c_str(), strUsername.c_str(), RightsToString(ulecRights));
		else if (ulStoreOwnerId != m_ulUserID)
			m_lpAudit->logf(EC_LOGLEVEL_FATAL, "access allowed objectid=%d type=%d ownername=\"%s\" username=\"%s\" rights=\"%s\"",
						   ulObjId, ulType, strStoreOwner.c_str(), strUsername.c_str(), RightsToString(ulecRights));
		else
			// you probably do not want to log all what a user does in their own store, do you?
			m_lpAudit->logf(EC_LOGLEVEL_INFO, "access allowed objectid=%d type=%d userid=%d", ulObjId, ulType, m_ulUserID);
	}

	return er;
}

/**
 * Get the ACLs on a given object in a protocol struct to send to the client
 *
 * @param[in] objid hierarchy object to get the ACLs for
 * @param[in] ulType rights access type, denied or grant
 * @param[out] lpsRightsArray structure with the current rights on objid
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetRights(unsigned int objid, int ulType,
    struct rightsArray *lpsRightsArray) const
{
	DB_RESULT lpDBResult;
	ECDatabase			*lpDatabase = NULL;
	objectid_t			sExternId;

	auto er = m_lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	if (lpsRightsArray == nullptr)
		return KCERR_INVALID_PARAMETER;

	auto strQuery = "SELECT a.id, a.type, a.rights FROM acl AS a WHERE a.hierarchy_id=" + stringify(objid);
	if(ulType == ACCESS_TYPE_DENIED)
		strQuery += " AND a.type="+stringify(ACCESS_TYPE_DENIED);
	else if(ulType == ACCESS_TYPE_GRANT)
		strQuery += " AND a.type="+stringify(ACCESS_TYPE_GRANT);
	// else ACCESS_TYPE_DENIED and ACCESS_TYPE_GRANT

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	unsigned int ulCount = lpDBResult.get_num_rows();
	if (ulCount <= 0) {
		lpsRightsArray->__ptr = nullptr;
		lpsRightsArray->__size = 0;
		return erSuccess;
	}

	lpsRightsArray->__ptr = s_alloc<rights>(nullptr, ulCount);
	lpsRightsArray->__size = ulCount;
	auto usrmgt = m_lpSession->GetUserManagement();
	for (unsigned int i = 0; i < ulCount; ++i) {
		auto lpDBRow = lpDBResult.fetch_row();
		if (lpDBRow == NULL) {
			ec_log_err("ECSecurity::GetRights(): row is null");
			return KCERR_DATABASE_ERROR;
		}

		lpsRightsArray->__ptr[i].ulUserid = atoi(lpDBRow[0]);
		lpsRightsArray->__ptr[i].ulType   = atoi(lpDBRow[1]);
		lpsRightsArray->__ptr[i].ulRights = atoi(lpDBRow[2]);
		lpsRightsArray->__ptr[i].ulState  = RIGHT_NORMAL;

		// do not use internal IDs with the cache
		if (lpsRightsArray->__ptr[i].ulUserid == KOPANO_UID_SYSTEM) {
			sExternId = objectid_t(ACTIVE_USER);
		} else if (lpsRightsArray->__ptr[i].ulUserid == KOPANO_UID_EVERYONE) {
			sExternId = objectid_t(DISTLIST_GROUP);
		} else {
			er = usrmgt->GetExternalId(lpsRightsArray->__ptr[i].ulUserid, &sExternId);
			if (er != erSuccess)
				return er;
		}
		er = ABIDToEntryID(NULL, lpsRightsArray->__ptr[i].ulUserid, sExternId, &lpsRightsArray->__ptr[i].sUserId);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

/**
 * Update the rights on a given object
 *
 * @param[in] objid hierarchy object id to set rights on
 * @param[in] lpsRightsArray protocol struct containing new rights for this object
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::SetRights(unsigned int objid, struct rightsArray *lpsRightsArray)
{
	unsigned int ulUserId = 0;
	ECDatabase			*lpDatabase = NULL;
	objectid_t			sExternId;
	objectdetails_t		sDetails;
	size_t ulErrors = 0;

	auto er = m_lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	if (lpsRightsArray == NULL)
		return KCERR_INVALID_PARAMETER;

	// Invalidate cache for this object
	m_lpSession->GetSessionManager()->GetCacheManager()->Update(fnevObjectModified, objid);
	auto usrmgt = m_lpSession->GetUserManagement();

	for (gsoap_size_t i = 0; i < lpsRightsArray->__size; ++i) {
		// FIXME: check for each object if it belongs to the store we're logged into (except for admin)

		// Get the correct local id
		if (lpsRightsArray->__ptr[i].sUserId.__size > 0 && lpsRightsArray->__ptr[i].sUserId.__ptr != NULL)
		{
			er = ABEntryIDToID(&lpsRightsArray->__ptr[i].sUserId, &ulUserId, &sExternId, NULL);
			if (er != erSuccess)
				return er;

			// internal user/group doesn't have an externid
			if (!sExternId.id.empty())
			{
				// Get real ulUserId on this server
				er = usrmgt->GetLocalId(sExternId, &ulUserId, NULL);
				if (er != erSuccess)
					return er;
			}
		}
		else
			ulUserId = lpsRightsArray->__ptr[i].ulUserid;

		er = usrmgt->GetObjectDetails(ulUserId, &sDetails);
		if (er != erSuccess)
			return er;

		// You can only set (delete is ok) permissions on active users, and security groups
		// Outlook 2007 blocks this client side, other clients get this error.
		if ((lpsRightsArray->__ptr[i].ulState & ~(RIGHT_DELETED | RIGHT_AUTOUPDATE_DENIED)) != 0 &&
			sDetails.GetClass() != ACTIVE_USER &&
			sDetails.GetClass() != DISTLIST_SECURITY &&
			sDetails.GetClass() != CONTAINER_COMPANY) {
				++ulErrors;
				continue;
		}

		// Auto create denied rules
		unsigned int ulDeniedRights = lpsRightsArray->__ptr[i].ulRights ^ ecRightsAllMask;
		if(lpsRightsArray->__ptr[i].ulRights & ecRightsEditAny)
			ulDeniedRights&=~ecRightsEditOwned;
		else if(lpsRightsArray->__ptr[i].ulRights & ecRightsEditOwned){
			ulDeniedRights&=~ecRightsEditOwned;
			ulDeniedRights|=ecRightsEditAny;
		}

		if(lpsRightsArray->__ptr[i].ulRights & ecRightsDeleteAny)
			ulDeniedRights&=~ecRightsDeleteOwned;
		else if(lpsRightsArray->__ptr[i].ulRights & ecRightsDeleteOwned)
		{
			ulDeniedRights&=~ecRightsDeleteOwned;
			ulDeniedRights|=ecRightsDeleteAny;
		}

		if(lpsRightsArray->__ptr[i].ulState == RIGHT_NORMAL)
		{
			// Do nothing...
		}
		else if((lpsRightsArray->__ptr[i].ulState & RIGHT_NEW) || (lpsRightsArray->__ptr[i].ulState & RIGHT_MODIFY))
		{
			auto strQueryNew = "REPLACE INTO acl (id, hierarchy_id, type, rights) VALUES "
				"(" + stringify(ulUserId) + "," + stringify(objid) + "," + stringify(lpsRightsArray->__ptr[i].ulType) + "," + stringify(lpsRightsArray->__ptr[i].ulRights) + ")";
			er = lpDatabase->DoInsert(strQueryNew);
			if(er != erSuccess)
				return er;
			if(lpsRightsArray->__ptr[i].ulState & RIGHT_AUTOUPDATE_DENIED){
				strQueryNew = "REPLACE INTO acl (id, hierarchy_id, type, rights) VALUES "
					" (" + stringify(ulUserId) + "," + stringify(objid) + "," + stringify(ACCESS_TYPE_DENIED) + "," + stringify(ulDeniedRights) + ")";
				er = lpDatabase->DoInsert(strQueryNew);
				if(er != erSuccess)
					return er;
			}
		}
		else if(lpsRightsArray->__ptr[i].ulState & RIGHT_DELETED)
		{
			auto strQueryDelete = "DELETE FROM acl WHERE "
				"(hierarchy_id=" + stringify(objid) + " AND id=" + stringify(ulUserId) + " AND type=" + stringify(lpsRightsArray->__ptr[i].ulType) + ")";
			er = lpDatabase->DoDelete(strQueryDelete);
			if(er != erSuccess)
				return er;
			if(lpsRightsArray->__ptr[i].ulState & RIGHT_AUTOUPDATE_DENIED) {
				strQueryDelete = "DELETE FROM acl WHERE ";
				strQueryDelete+="(hierarchy_id="+stringify(objid)+" AND id="+stringify(ulUserId)+" AND type="+stringify(ACCESS_TYPE_DENIED)+")";

				er = lpDatabase->DoDelete(strQueryDelete);
				if(er != erSuccess)
					return er;
			}
		}else{
			// a Hacker ?
		}
	}

	if (lpsRightsArray->__size >= 0 && ulErrors == static_cast<size_t>(lpsRightsArray->__size))
		er = KCERR_INVALID_PARAMETER; /* all ACLs failed */
	else if (ulErrors != 0)
		er = KCWARN_PARTIAL_COMPLETION; /* some ACLs failed */
	else
		er = erSuccess;
	return er;
}

/**
 * Return the company id of the current user context
 *
 * @param[out] lpulCompanyId company id of user
 *
 * @return always erSuccess
 */
ECRESULT ECSecurity::GetUserCompany(unsigned int *lpulCompanyId) const
{
	*lpulCompanyId = m_ulCompanyID;
	return erSuccess;
}

/**
 * Get a list of company ids that may be viewed by the current user
 *
 * @param[in] ulFlags Usermanagemnt flags
 * @param[out] lppObjects New allocated list of company details
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetViewableCompanyIds(unsigned int ulFlags,
    std::list<localobjectdetails_t> &objs)
{
	/*
	 * We have the viewable companies stored in our cache,
	 * if it is present use that, otherwise just create a
	 * new one.
	 * NOTE: We always request GetViewableCompanies with 0 as ulFlags,
	 * this because we are caching the list here and some callers might
	 * want all details while others will only want the IDs.
	 */
	if (!m_lpViewCompanies) {
		auto er = GetViewableCompanies(0, m_lpViewCompanies);
		if (er != erSuccess)
			return er;
	}

	/*
	 * Because of the difference in flags it is possible we have
	 * too many entries in the list. We need to filter those out now.
	 */
	objs.clear();
	for (const auto &i : *m_lpViewCompanies) {
		if (m_ulUserID != 0 && (ulFlags & USERMANAGEMENT_ADDRESSBOOK) &&
		    i.GetPropBool(OB_PROP_B_AB_HIDDEN))
			continue;
		if (ulFlags & USERMANAGEMENT_IDS_ONLY)
			objs.emplace_back(i.ulId, i.GetClass());
		else
			objs.emplace_back(i.ulId, i);
	}
	return erSuccess;
}

/**
 * Check if the given user id is readable by the current user
 *
 * @param[in] ulUserObjectId internal user id
 *
 * @return Kopano error code
 * @retval erSuccess viewable
 * @retval KCERR_NOT_FOUND not viewable
 */
ECRESULT ECSecurity::IsUserObjectVisible(unsigned int ulUserObjectId)
{
	objectid_t sExternId;
	unsigned int ulCompanyId;

	if (ulUserObjectId == 0 ||
	    ulUserObjectId == m_ulUserID ||
	    ulUserObjectId == m_ulCompanyID ||
	    ulUserObjectId == KOPANO_UID_SYSTEM ||
	    ulUserObjectId == KOPANO_UID_EVERYONE ||
	    m_details.GetPropInt(OB_PROP_I_ADMINLEVEL) == ADMIN_LEVEL_SYSADMIN ||
	    !m_lpSession->GetSessionManager()->IsHostedSupported())
		return erSuccess;
	auto er = m_lpSession->GetUserManagement()->GetExternalId(ulUserObjectId, &sExternId, &ulCompanyId);
	if (er != erSuccess)
		return er;

	// still needed?
	if (sExternId.objclass == CONTAINER_COMPANY)
		ulCompanyId = ulUserObjectId;
	if (!m_lpViewCompanies) {
		er = GetViewableCompanies(0, m_lpViewCompanies);
		if (er != erSuccess)
			return er;
	}
	for (const auto &company : *m_lpViewCompanies)
		if (company.ulId == ulCompanyId)
			return erSuccess;
	/* Item was not found */
	return KCERR_NOT_FOUND;
}

/**
 * Internal helper function to get a list of viewable company details
 *
 * @todo won't this bug when we cache only the IDs in a first call,
 * and then when we need the full details, the m_lpViewCompanies will
 * only contain the IDs?
 *
 * @param[in] ulFlags usermanagement flags
 * @param[in] lppObjects new allocated list with company details
 *
 * @return
 */
ECRESULT ECSecurity::GetViewableCompanies(unsigned int ulFlags,
    std::unique_ptr<std::list<localobjectdetails_t>> &obj_out) const
{
	std::list<localobjectdetails_t> objs;
	ECRESULT er = erSuccess;
	objectdetails_t details;
	auto usrmgt = m_lpSession->GetUserManagement();

	if (m_details.GetPropInt(OB_PROP_I_ADMINLEVEL) == ADMIN_LEVEL_SYSADMIN)
		er = usrmgt->GetCompanyObjectListAndSync(CONTAINER_COMPANY, 0, nullptr, objs, ulFlags);
	else
		er = usrmgt->GetParentObjectsOfObjectAndSync(OBJECTRELATION_COMPANY_VIEW,
		     m_ulCompanyID, objs, ulFlags);
	if (er != erSuccess)
		/* Whatever the error might be, it only indicates we
		 * are not allowed to view _other_ companyspaces.
		 * It doesn't restrict us from viewing our own... */
		objs.clear();

	/* We are going to insert the requested companyID to the list as well,
	 * this way we guarantee that _all_ viewable companies are in the list.
	 * And we can use sort() and unique() to prevent duplicate entries while
	 * making sure the we can safely handle things when the current company
	 * is either added or not present in the RemoteViewableCompanies. */
	if (m_ulCompanyID != 0) {
		if (!(ulFlags & USERMANAGEMENT_IDS_ONLY)) {
			er = usrmgt->GetObjectDetails(m_ulCompanyID, &details);
			if (er != erSuccess)
				return er;
		} else {
			details = objectdetails_t(CONTAINER_COMPANY);
		}
		objs.emplace_back(m_ulCompanyID, details);
	}
	objs.sort();
	objs.unique();
	obj_out = make_unique_nt<decltype(objs)>(std::move(objs));
	if (obj_out == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	return erSuccess;
}

/**
 * Get a list of company details which the current user is admin over
 *
 * @param[in] ulFlags usermanagement flags
 * @param[out] lppObjects company list
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetAdminCompanies(unsigned int ulFlags,
    std::unique_ptr<std::list<localobjectdetails_t>> &obj_out)
{
	std::list<localobjectdetails_t> objs;
	ECRESULT er = erSuccess;
	auto usrmgt = m_lpSession->GetUserManagement();

	if (m_details.GetPropInt(OB_PROP_I_ADMINLEVEL) == ADMIN_LEVEL_SYSADMIN)
		er = usrmgt->GetCompanyObjectListAndSync(CONTAINER_COMPANY, 0,
		     nullptr, objs, ulFlags);
	else
		er = usrmgt->GetParentObjectsOfObjectAndSync(OBJECTRELATION_COMPANY_ADMIN,
		     m_ulUserID, objs, ulFlags);
	if (er != erSuccess)
		return er;

	/* A user is only admin over a company when he has privileges to view the company */
	for (auto iterObjects = objs.begin(); iterObjects != objs.cend(); )
		if (IsUserObjectVisible(iterObjects->ulId) != erSuccess)
			iterObjects = objs.erase(iterObjects);
		else
			++iterObjects;

	obj_out = make_unique_nt<decltype(objs)>(std::move(objs));
	if (obj_out == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	return erSuccess;
}

/**
 * Return the current logged in UserID _OR_ if you're an administrator
 * over user that is set as owner of the given object, return the
 * owner of the object.
 *
 * @param[in] ulObjId object to get ownership of if admin, defaults to 0 to get the current UserID
 *
 * @return user id of object
 */
unsigned int ECSecurity::GetUserId(unsigned int ulObjId)
{
	unsigned int ulUserId = m_ulUserID;
	if (ulObjId != 0 && IsAdminOverOwnerOfObject(ulObjId) == erSuccess) {
		auto er = GetOwner(ulObjId, &ulUserId);
		if (er != erSuccess)
			ulUserId = m_ulUserID;
	}
	return ulUserId;
}

/**
 * Check if the given object id is in your own store
 *
 * @param[in] ulObjId hierarchy object id of object to check
 *
 * @return Kopano error code
 * @retval erSuccess object is in the current user's store
 */
ECRESULT ECSecurity::IsStoreOwner(unsigned int ulObjId) const
{
	unsigned int ulStoreId = 0;
	auto er = m_lpSession->GetSessionManager()->GetCacheManager()->GetStore(ulObjId, &ulStoreId, NULL);
	if(er != erSuccess)
		return er;
	return IsOwner(ulStoreId);
}

/**
 * Return the current user's admin level
 *
 * @return admin level of user
 */
int ECSecurity::GetAdminLevel(void) const
{
	return m_details.GetPropInt(OB_PROP_I_ADMINLEVEL);
}

/**
 * Get the owner of the store in which the given objectid resides in
 *
 * @param[in] ulObjId hierarchy object id to get store owner of
 * @param[in] lpulOwnerId user id of store in which ulObjId resides
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetStoreOwner(unsigned int ulObjId,
    unsigned int *lpulOwnerId) const
{
	return GetStoreOwnerAndType(ulObjId, lpulOwnerId, NULL);
}

/**
 * Get the owner and type of the store in which the given objectid resides in
 *
 * @param[in] ulObjId hierarchy object id to get store owner of
 * @param[out] lpulOwnerId user id of store in which ulObjId resides
 * @param[out] lpulStoreType type of store in which ulObjId resides
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetStoreOwnerAndType(unsigned int ulObjId,
    unsigned int *lpulOwnerId, unsigned int *lpulStoreType) const
{
	unsigned int ulStoreId = 0;

	if (lpulOwnerId || lpulStoreType) {
		auto er = m_lpSession->GetSessionManager()->GetCacheManager()->GetStoreAndType(ulObjId, &ulStoreId, NULL, lpulStoreType);
		if (er != erSuccess)
			return er;
	}
	if (lpulOwnerId) {
		auto er = GetOwner(ulStoreId, lpulOwnerId);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

/**
 * Check if the current user is admin over a given user id (user/group/company/...)
 *
 * @todo this function should be renamed to IsAdminOfUserObject(id) or something like that
 *
 * @param[in] ulUserObjectId id of user
 *
 * @return Kopano error code
 * @retval erSuccess Yes, admin
 * @retval KCERR_NO_ACCESS No, not admin
 */
ECRESULT ECSecurity::IsAdminOverUserObject(unsigned int ulUserObjectId)
{
	unsigned int ulCompanyId;
	objectdetails_t objectdetails;
	objectid_t sExternId;

	/* Hosted disabled: When admin level is not zero, then the user
	 * is the administrator, otherwise the user isn't and we don't need
	 * to look any further. */
	if (!m_lpSession->GetSessionManager()->IsHostedSupported()) {
		if (m_details.GetPropInt(OB_PROP_I_ADMINLEVEL) != 0)
			return erSuccess;
		return KCERR_NO_ACCESS;
	}

	/* If hosted is enabled, system administrators are administrator over all users. */
	if (m_details.GetPropInt(OB_PROP_I_ADMINLEVEL) == ADMIN_LEVEL_SYSADMIN)
		return erSuccess;
	/*
	 * Determine to which company the user belongs
	 */
	auto er = m_lpSession->GetUserManagement()->GetExternalId(ulUserObjectId, &sExternId, &ulCompanyId);
	if (er != erSuccess)
		return er;

	// still needed?
	if (sExternId.objclass == CONTAINER_COMPANY)
		ulCompanyId = ulUserObjectId;
	/*
	 * If ulCompanyId is the company where the logged in user belongs to,
	 * then the only thing we need to check is the "isadmin" boolean.
	 */
	if (m_ulCompanyID == ulCompanyId) {
		if (m_details.GetPropInt(OB_PROP_I_ADMINLEVEL) != 0)
			return erSuccess;
		return KCERR_NO_ACCESS;
	}
	if (!m_lpAdminCompanies) {
		er = GetAdminCompanies(USERMANAGEMENT_IDS_ONLY, m_lpAdminCompanies);
		if (er != erSuccess)
			return er;
	}
	for (const auto &obj : *m_lpAdminCompanies)
		if (obj.ulId == ulCompanyId)
			return erSuccess;
	/* Item was not found, so no access */
	return KCERR_NO_ACCESS;
}

/**
 * Check if we're admin over the user who owns the given object id
 *
 * @param ulObjectId hierarchy object id
 *
 * @return Kopano error code
 * @retval erSuccess Yes
 * @retval KCERR_NO_ACCESS No
 */
ECRESULT ECSecurity::IsAdminOverOwnerOfObject(unsigned int ulObjectId)
{
	unsigned int ulOwner;
	/*
	 * Request the ownership if the object.
	 */
	auto er = GetStoreOwner(ulObjectId, &ulOwner);
	if (er != erSuccess)
		return er;
	return IsAdminOverUserObject(ulOwner);
}

/**
 * Get the size of the store in which the given ulObjId resides in
 *
 * @param[in] ulObjId hierarchy object id
 * @param[out] lpllStoreSize size of store
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetStoreSize(unsigned int ulObjId,
    long long *lpllStoreSize) const
{
	ECDatabase		*lpDatabase = NULL;
	DB_RESULT lpDBResult;
	unsigned int	ulStore;

	auto er = m_lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	er = m_lpSession->GetSessionManager()->GetCacheManager()->GetStore(ulObjId, &ulStore, NULL);
	if(er != erSuccess)
		return er;
	auto strQuery = "SELECT val_longint FROM properties WHERE tag=" + stringify(PROP_ID(PR_MESSAGE_SIZE_EXTENDED)) + " AND type=" + stringify(PROP_TYPE(PR_MESSAGE_SIZE_EXTENDED)) + " AND hierarchyid=" + stringify(ulStore);
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	if (lpDBResult.get_num_rows() != 1) {
		// This mostly happens when we're creating a new store, so return 0 sized store
		*lpllStoreSize = 0;
		return erSuccess;
	}
	auto lpDBRow = lpDBResult.fetch_row();
	if(lpDBRow == NULL || lpDBRow[0] == NULL) {
		ec_log_err("ECSecurity::GetStoreSize(): row is null");
		return KCERR_DATABASE_ERROR;
	}
	*lpllStoreSize = atoll(lpDBRow[0]);
	return erSuccess;
}

/**
 * Get the store size of a given user
 *
 * @param[in] ulUserId internal user id
 * @param[out] lpllUserSize store size of user
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetUserSize(unsigned int ulUserId,
    long long *lpllUserSize) const
{
	ECDatabase		*lpDatabase = NULL;
	DB_RESULT lpDBResult;
	auto er = m_lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	auto strQuery =
		"SELECT p.val_longint "
		"FROM properties AS p "
		"JOIN stores AS s "
			"ON s.hierarchy_id = p.hierarchyid "
		"WHERE "
			"s.user_id = " + stringify(ulUserId) + " " +
			"AND p.tag = " + stringify(PROP_ID(PR_MESSAGE_SIZE_EXTENDED)) + " "
			"AND p.type = " + stringify(PROP_TYPE(PR_MESSAGE_SIZE_EXTENDED));
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	if (lpDBResult.get_num_rows() != 1) {
		*lpllUserSize = 0;
		return erSuccess;
	}
	auto lpDBRow = lpDBResult.fetch_row();
	if (lpDBRow == NULL) {
		ec_log_err("ECSecurity::GetUserSize(): row is null");
		return KCERR_DATABASE_ERROR;
	}
	*lpllUserSize = (lpDBRow[0] == nullptr) ? 0 : atoll(lpDBRow[0]);
	return erSuccess;
}

/**
 * Gets the quota status value (ok, warn, soft, hard) for a given
 * store and store size
 *
 * @note if you already know the owner of the store, it's better to
 * call ECSecurity::CheckUserQuota
 *
 * @param[in] ulStoreId store to check quota for
 * @param[in] llStoreSize current store size of the store
 * @param[out] lpQuotaStatus quota status value
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::CheckQuota(unsigned int ulStoreId, long long llStoreSize,
    eQuotaStatus *lpQuotaStatus) const
{
	unsigned int ulOwnerId = 0, ulStoreType = 0;
	auto er = m_lpSession->GetSessionManager()->GetCacheManager()->GetStoreAndType(ulStoreId, nullptr, nullptr, &ulStoreType);
	if (er != erSuccess)
		return er;
	if(GetAdminLevel() == ADMIN_LEVEL_SYSADMIN || ulStoreType != ECSTORE_TYPE_PRIVATE) {
		*lpQuotaStatus = QUOTA_OK;
		return er; // all is good for admin user and/or non-private stores.
	}
	// Get the store owner
	er = GetStoreOwner(ulStoreId, &ulOwnerId);
	if(er != erSuccess)
		return er;
	return CheckUserQuota(ulOwnerId, llStoreSize, lpQuotaStatus);
}

/**
 * Gets the quota status value (ok, warn, soft, hard) for a given
 * store and store size
 *
 * @param[in] ulUserId user to check quota for
 * @param[in] llStoreSize current store size of the store
 * @param[out] lpQuotaStatus quota status
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::CheckUserQuota(unsigned int ulUserId,
    long long llStoreSize, eQuotaStatus *lpQuotaStatus) const
{
	quotadetails_t	quotadetails;

	if (ulUserId == KOPANO_UID_EVERYONE) {
		/* Publicly owned stores are never over quota.
		 * But do publicly owned stores actually exist since the owner is either a user or company */
		*lpQuotaStatus = QUOTA_OK;
		return erSuccess;
	}
	auto er = GetUserQuota(ulUserId, false, &quotadetails);
	if (er != erSuccess)
		return er;

	// check the options
	if(quotadetails.llHardSize > 0 && llStoreSize >= quotadetails.llHardSize)
		*lpQuotaStatus = QUOTA_HARDLIMIT;
	else if(quotadetails.llSoftSize > 0 && llStoreSize >= quotadetails.llSoftSize)
		*lpQuotaStatus = QUOTA_SOFTLIMIT;
	else if(quotadetails.llWarnSize > 0 && llStoreSize >= quotadetails.llWarnSize)
		*lpQuotaStatus = QUOTA_WARN;
	else
		*lpQuotaStatus = QUOTA_OK;
	return erSuccess;
}

/**
 * Get the quota details of a user
 *
 * @param[in] ulUserId internal user id
 * @param[out] lpDetails quota details
 *
 * @return Kopano error code
 */
ECRESULT ECSecurity::GetUserQuota(unsigned int ulUserId, bool bGetUserDefault,
    quotadetails_t *lpDetails) const
{
	const char *lpszWarnQuota = nullptr, *lpszSoftQuota = nullptr, *lpszHardQuota = nullptr;
	quotadetails_t quotadetails;
	objectid_t sExternId;
	unsigned int ulCompanyId;

	if (lpDetails == nullptr)
		return KCERR_INVALID_PARAMETER;
	auto usrmgt = m_lpSession->GetUserManagement();
	auto er = usrmgt->GetExternalId(ulUserId, &sExternId, &ulCompanyId);
	if (er != erSuccess)
		return er;

	assert(!bGetUserDefault || sExternId.objclass == CONTAINER_COMPANY);
	auto cfg = m_lpSession->GetSessionManager()->GetConfig();

	/* Not all objectclasses support quota */
	if ((sExternId.objclass == NONACTIVE_CONTACT) ||
	    (OBJECTCLASS_TYPE(sExternId.objclass) == OBJECTTYPE_DISTLIST) ||
	    (sExternId.objclass == CONTAINER_ADDRESSLIST))
		goto exit;
	er = usrmgt->GetQuotaDetailsAndSync(ulUserId, &quotadetails, bGetUserDefault);
	if (er != erSuccess)
		return er;

	/* When the default quota boolean is set, we need to look at the next quota level */
	if (!quotadetails.bUseDefaultQuota)
		goto exit;

	/* Request default quota values from company level if enabled */
	if ((OBJECTCLASS_TYPE(sExternId.objclass) == OBJECTTYPE_MAILUSER) && ulCompanyId) {
		er = usrmgt->GetQuotaDetailsAndSync(ulCompanyId, &quotadetails, true);
		if (er == erSuccess && !quotadetails.bUseDefaultQuota)
			goto exit; /* On failure, or when we should use the default, we're done */

		er = erSuccess;
	}

	/* No information from company, the last level we can check is the configuration file */
	if (OBJECTCLASS_TYPE(sExternId.objclass) == OBJECTTYPE_MAILUSER) {
		lpszWarnQuota = cfg->GetSetting("quota_warn");
		lpszSoftQuota = cfg->GetSetting("quota_soft");
		lpszHardQuota = cfg->GetSetting("quota_hard");
	} else if (sExternId.objclass == CONTAINER_COMPANY) {
		lpszWarnQuota = cfg->GetSetting("companyquota_warn");
	}

	quotadetails.bUseDefaultQuota = true;
	quotadetails.bIsUserDefaultQuota = false;
	if (lpszWarnQuota)
		quotadetails.llWarnSize = atoll(lpszWarnQuota) * 1024 * 1024;
	if (lpszSoftQuota)
		quotadetails.llSoftSize = atoll(lpszSoftQuota) * 1024 * 1024;
	if (lpszHardQuota)
		quotadetails.llHardSize = atoll(lpszHardQuota) * 1024 * 1024;
exit:
	if (er == erSuccess)
		*lpDetails = std::move(quotadetails);
	return er;
}

/**
 * Get the username of the current user context
 *
 * @param[out] lpstrUsername login name of the user
 *
 * @return always erSuccess
 */
ECRESULT ECSecurity::GetUsername(std::string *lpstrUsername) const
{
	if (m_ulUserID)
		*lpstrUsername = m_details.GetPropString(OB_PROP_S_LOGIN);
	else
		*lpstrUsername = KOPANO_SYSTEM_USER;
	return erSuccess;
}

/**
 * Get the username of the user impersonating the current user
 *
 * @param[out] lpstrImpersonator login name of the impersonator
 *
 * @return always erSuccess
 */
ECRESULT ECSecurity::GetImpersonator(std::string *lpstrImpersonator) const
{
	if (m_ulImpersonatorID == EC_NO_IMPERSONATOR)
		return KCERR_NOT_FOUND;
	else if (m_ulImpersonatorID)
		*lpstrImpersonator = m_impersonatorDetails.GetPropString(OB_PROP_S_LOGIN);
	else
		*lpstrImpersonator = KOPANO_SYSTEM_USER;
	return erSuccess;
}

/**
 * Get memory size of this object
 *
 * @return Object size in bytes
 */
size_t ECSecurity::GetObjectSize(void) const
{
	size_t ulSize = sizeof(*this);
	ulSize += m_details.GetObjectSize();
	ulSize += m_impersonatorDetails.GetObjectSize();

	if (m_lpGroups) {
		for (const auto &i : *m_lpGroups)
			ulSize += i.GetObjectSize();
		ulSize += MEMORY_USAGE_LIST(m_lpGroups->size(), std::list<localobjectdetails_t>);
	}

	if (m_lpViewCompanies)
	{
		for (const auto &i : *m_lpViewCompanies)
			ulSize += i.GetObjectSize();
		ulSize += MEMORY_USAGE_LIST(m_lpViewCompanies->size(), std::list<localobjectdetails_t>);
	}

	if (m_lpAdminCompanies)
	{
		for (const auto &i : *m_lpAdminCompanies)
			ulSize += i.GetObjectSize();
		ulSize += MEMORY_USAGE_LIST(m_lpAdminCompanies->size(), std::list<localobjectdetails_t>);
	}

	return ulSize;
}

} /* namespace */
