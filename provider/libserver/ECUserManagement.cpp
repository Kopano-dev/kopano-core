/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <mapidefs.h>
#include <mapitags.h>
#include <kopano/MAPIErrors.h>
#include <kopano/mapiext.h>
#include <kopano/scope.hpp>
#include <kopano/tie.hpp>
#include <map>
#include <memory>
#include <algorithm>
#include "kcore.hpp"
#include <kopano/stringutil.h>
#include "ECUserManagement.h"
#include "ECSessionManager.h"
#include "ECPluginFactory.h"
#include "ECSecurity.h"
#include "ECKrbAuth.h"
#include <kopano/UnixUtil.h>
#include "ECICS.h"
#include "ics.h"
#include <kopano/ECGuid.h>
#include <kopano/ECDefs.h>
#include "ECFeatureList.h"
#include <kopano/EMSAbTag.h>
#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>
#ifndef AB_UNICODE_OK
#define AB_UNICODE_OK ((ULONG) 0x00000040)
#endif

using namespace std::string_literals;

namespace KC {

extern ECSessionManager*	g_lpSessionManager;

static bool execute_script(const char *scriptname, ...)
{
	va_list v;
	const char *env[32];
	std::list<std::string> lstEnv;
	std::string strEnv;
	int n = 0;

	va_start(v, scriptname);
	/* Set environment */
	for (;;) {
		auto envname = va_arg(v, char *);
		if (!envname)
			break;
		auto envval = va_arg(v, char *);
		if (!envval)
			break;
		strEnv = envname;
		strEnv += '=';
		strEnv += envval;
		lstEnv.emplace_back(std::move(strEnv));
	}
	va_end(v);

	ec_log_debug("Running script `%s`", scriptname);
	for (const auto &s : lstEnv)
		env[n++] = s.c_str();
	env[n] = NULL;
	return unix_system(scriptname, {scriptname}, env);
}

static const char *ObjectClassToName(objectclass_t objclass)
{
	switch (objclass) {
	case OBJECTCLASS_UNKNOWN:
		return "unknown";
	case OBJECTCLASS_USER:
		return "unknown user";
	case ACTIVE_USER:
		return "user";
	case NONACTIVE_USER:
		return "nonactive user";
	case NONACTIVE_ROOM:
		return "room";
	case NONACTIVE_EQUIPMENT:
		return "equipment";
	case NONACTIVE_CONTACT:
		return "contact";
	case OBJECTCLASS_DISTLIST:
		return "unknown group";
	case DISTLIST_GROUP:
		return "group";
	case DISTLIST_SECURITY:
		return "security group";
	case DISTLIST_DYNAMIC:
		return "dynamic group";
	case OBJECTCLASS_CONTAINER:
		return "unknown container";
	case CONTAINER_COMPANY:
		return "company";
	case CONTAINER_ADDRESSLIST:
		return "address list";
	default:
		return "Unknown object";
	};
}

static const char *RelationTypeToName(userobject_relation_t type)
{
	switch(type) {
	case OBJECTRELATION_GROUP_MEMBER:
		return "groupmember";
	case OBJECTRELATION_COMPANY_VIEW:
		return "company-view";
	case OBJECTRELATION_COMPANY_ADMIN:
		return "company-admin";
	case OBJECTRELATION_QUOTA_USERRECIPIENT:
		return "userquota-recipient";
	case OBJECTRELATION_QUOTA_COMPANYRECIPIENT:
		return "companyquota-recipient";
	case OBJECTRELATION_USER_SENDAS:
		return "sendas-user-allowed";
	case OBJECTRELATION_ADDRESSLIST_MEMBER:
		return "addressbook-member";
	};

	return "Unknown relation type";
}

ECUserManagement::ECUserManagement(BTSession *s,
    ECPluginFactory *f, std::shared_ptr<ECConfig> c) :
	m_lpPluginFactory(f), m_lpSession(s), m_lpConfig(std::move(c))
{}

// Authenticate a user (NOTE: ECUserManagement will never authenticate SYSTEM unless it is actually
// authenticated by the remote server, which normally never happens)
ECRESULT ECUserManagement::AuthUserAndSync(const char* szLoginname, const char* szPassword, unsigned int *lpulUserId) {
	objectsignature_t external;
	UserPlugin *lpPlugin = NULL;
	std::string username, companyname, password, error;
	bool bHosted = m_lpSession->GetSessionManager()->IsHostedSupported();
	objectid_t sCompany(CONTAINER_COMPANY);
	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	/*
	 * GetUserAndCompanyFromLoginName() will return KCWARN_PARTIAL_COMPLETION
	 * when only the username was resolved and not the companyname.
	 * When that happens continue like nothing happens because perhaps the
	 * LDAP plugin is used and the login attribute is the email address.
	 * We will resolve the company later when the session is created and the
	 * company is requested through getDetails().
	 */
	er = GetUserAndCompanyFromLoginName(szLoginname, &username, &companyname);
	if (er != erSuccess && er != KCWARN_PARTIAL_COMPLETION)
		return er;
	if (SymmetricIsCrypted(szPassword))
		password = SymmetricDecrypt(szPassword);
	else
		password = szPassword;

	if (bHosted && !companyname.empty()) {
		er = ResolveObject(CONTAINER_COMPANY, companyname, objectid_t(), &sCompany);
		if (er != erSuccess || sCompany.objclass != CONTAINER_COMPANY) {
			ec_log_warn("Company \"%s\" not found for authentication by plugin failed for user \"%s\"", companyname.c_str(), szLoginname);
			return er;
		}
	}

	auto szAuthMethod = m_lpConfig->GetSetting("auth_method");
	if (szAuthMethod && strcmp(szAuthMethod, "pam") == 0) {
		// authenticate through pam
		er = ECPAMAuthenticateUser(m_lpConfig->GetSetting("pam_service"), username, password, &error);
		if (er != erSuccess) {
			ec_log_warn("Authentication through PAM failed for user \"%s\": %s", szLoginname, error.c_str());
			return er;
		}
		try {
			external = lpPlugin->resolveName(ACTIVE_USER, username, sCompany);
		} catch (const objectnotfound &e) {
			ec_log_warn("Authentication through PAM succeeded for \"%s\", but not found by plugin: %s", szLoginname, e.what());
			return KCERR_LOGON_FAILED;
		} catch (const std::exception &e) {
			ec_log_warn("Authentication through PAM succeeded for \"%s\", but plugin returned an error: %s", szLoginname, e.what());
			return KCERR_LOGON_FAILED;
		}
	} else if (szAuthMethod && strcmp(szAuthMethod, "kerberos") == 0) {
		// authenticate through kerberos
		er = ECKrb5AuthenticateUser(username, password, &error);
		if (er != erSuccess) {
			ec_log_warn("Authentication through Kerberos failed for user \"%s\": %s", szLoginname, error.c_str());
			return er;
		}
		try {
			external = lpPlugin->resolveName(ACTIVE_USER, username, sCompany);
		} catch (const objectnotfound &e) {
			ec_log_warn("Authentication through Kerberos succeeded for \"%s\", but not found by plugin: %s", szLoginname, e.what());
			return KCERR_LOGON_FAILED;
		} catch (const std::exception &e) {
			ec_log_warn("Authentication through Kerberos succeeded for \"%s\", but plugin returned an error: %s", szLoginname, e.what());
			return KCERR_LOGON_FAILED;
		}
	} else
	{
		// default method is plugin
		try {
			external = lpPlugin->authenticateUser(username, password, sCompany);
		} catch (const login_error &e) {
			ec_log_warn("Authentication by plugin failed for user \"%s\": %s", szLoginname, e.what());
			return KCERR_LOGON_FAILED;
		} catch (const objectnotfound &e) {
			ec_log_warn("Authentication by plugin failed for user \"%s\": %s", szLoginname, e.what());
			return KCERR_NOT_FOUND;
		} catch (const std::exception &e) {
			ec_log_warn("Authentication error by plugin for user \"%s\": %s", szLoginname, e.what());
			return KCERR_PLUGIN_ERROR;
		}
	}
	return GetLocalObjectIdOrCreate(external, lpulUserId);
}

/*
 * I would prefer the next method to have
 * bool ECUserManagement::MustHide(ECSecurity& security, unsigned int ulFlags, const objectdetails_t details)
 * as a prototype, but that would mean percolating constness in class ECSecurity's methods, which proved to
 * be a task beyond a simple edit. NS 11 fFebruary 2014
 */
bool ECUserManagement::MustHide(/*const*/ ECSecurity &security,
    unsigned int ulFlags, const objectdetails_t &details) const
{
	return	(security.GetUserId() != KOPANO_UID_SYSTEM) &&
		(security.GetAdminLevel() == 0) &&
		((ulFlags & USERMANAGEMENT_SHOW_HIDDEN) == 0) &&
		details.GetPropBool(OB_PROP_B_AB_HIDDEN);
}

// Get details for an object
ECRESULT ECUserManagement::GetObjectDetails(unsigned int ulObjectId, objectdetails_t *lpDetails)
{
	if (IsInternalObject(ulObjectId))
		return GetLocalObjectDetails(ulObjectId, lpDetails);
	return GetExternalObjectDetails(ulObjectId, lpDetails);
	}

ECRESULT ECUserManagement::GetLocalObjectListFromSignatures(const std::list<objectsignature_t> &lstSignatures,
    const std::map<objectid_t, unsigned int> &mapExternToLocal,
    unsigned int ulFlags, std::list<localobjectdetails_t> *lpDetails) const
{
	ECSecurity *lpSecurity = NULL;
	// Extern details
	std::list<objectid_t> lstExternIds;
	std::map<objectid_t, objectdetails_t> lpExternDetails;
	objectdetails_t details;
	unsigned int ulObjectId = 0;
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	er = GetSecurity(&lpSecurity);
	if (er != erSuccess)
		return er;

	auto cache = m_lpSession->GetSessionManager()->GetCacheManager();
	for (const auto &sig : lstSignatures) {
		auto iterExternLocal = mapExternToLocal.find(sig.id);
		if (iterExternLocal == mapExternToLocal.cend())
			continue;
		ulObjectId = iterExternLocal->second;
		// Check if the cache contains the details or if we are going to request
		// it from the plugin.
		er = cache->GetUserDetails(ulObjectId, &details);
		if (er != erSuccess) {
			lstExternIds.emplace_back(sig.id);
			er = erSuccess;
			continue;
		}
		if (ulFlags & USERMANAGEMENT_ADDRESSBOOK &&
		    MustHide(*lpSecurity, ulFlags, details))
			continue;
		// remove details, but keep the class information
		if (ulFlags & USERMANAGEMENT_IDS_ONLY)
			details = objectdetails_t(details.GetClass());
		lpDetails->emplace_back(ulObjectId, details);
	}

	if (lstExternIds.empty())
		return hrSuccess;
	// We have a list of all objects which still require details from plugin
	try {
		// Get all pending details
		lpExternDetails = lpPlugin->getObjectDetails(lstExternIds);
	} catch (const notsupported &) {
		return KCERR_NO_SUPPORT;
	} catch (const objectnotfound &) {
		return KCERR_NOT_FOUND;
	} catch (const std::exception &e) {
		ec_log_warn("K-1501: Unable to retrieve details from external user source: %s", e.what());
		return KCERR_PLUGIN_ERROR;
	}

	for (const auto &ext_det : lpExternDetails) {
		auto iterExternLocal = mapExternToLocal.find(ext_det.first);
		if (iterExternLocal == mapExternToLocal.cend())
			continue;
		ulObjectId = iterExternLocal->second;
		if (ulFlags & USERMANAGEMENT_ADDRESSBOOK)
			if (MustHide(*lpSecurity, ulFlags, ext_det.second))
				continue;
		if (ulFlags & USERMANAGEMENT_IDS_ONLY)
			lpDetails->emplace_back(ulObjectId, objectdetails_t(ext_det.second.GetClass()));
		else
			lpDetails->emplace_back(ulObjectId, ext_det.second);
		cache->SetUserDetails(ulObjectId, ext_det.second);
	}
	return erSuccess;
}

/**
 * Get object list
 *
 * Retrieves the object list with local object IDs for the specified object class. In hosted mode, ulCompanyId can
 * specify which company to retrieve objects for.
 *
 * @param objclass Object class to return list of
 * @param ulCompanyId Company ID to retrieve list for (may be 0 for non-hosted mode, or when getting company list)
 * @param lppObjects returned object list
 * @param ulFlags USERMANAGEMENT_IDS_ONLY: return only IDs of objects, not details, USERMANAGEMENT_ADDRESSBOOK: returned
 *                objects will be used in the addressbook, so filter 'hidden' items, USERMANAGEMENT_FORCE_SYNC: always
 *                perform a sync with the plugin, even if sync_gab_realtime is set to 'no'
 * @return result
 */
ECRESULT ECUserManagement::GetCompanyObjectListAndSync(objectclass_t objclass, unsigned int ulCompanyId, std::list<localobjectdetails_t> **lppObjects, unsigned int ulFlags)
{
	bool bSync = ulFlags & USERMANAGEMENT_FORCE_SYNC || parseBool(m_lpConfig->GetSetting("sync_gab_realtime"));
	bool bIsSafeMode = parseBool(m_lpConfig->GetSetting("user_safe_mode"));
	// Local ids
	std::unique_ptr<std::list<unsigned int> > lpLocalIds;
	// Extern ids
	signatures_t lpExternSignatures;
	// Extern -> Local
	std::map<objectid_t, unsigned int> mapExternIdToLocal;
	std::map<objectid_t, std::pair<unsigned int, std::string> > mapSignatureIdToLocal;
	objectid_t extcompany, externid;
	std::string signature;
	unsigned ulObjectId = 0;
	bool bMoved = false;
	ECSecurity *lpSecurity = NULL;
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if (er != erSuccess)
		return er;
	er = GetSecurity(&lpSecurity);
	if (er != erSuccess)
		return er;

	if (m_lpSession->GetSessionManager()->IsHostedSupported()) {
		/* When hosted is enabled, the companyid _must_ have an external id,
		 * unless we are requesting the companylist in which case the companyid is 0 and doesn't
		 * need to be resolved at all.*/
		if (objclass != CONTAINER_COMPANY && !IsInternalObject(ulCompanyId)) {
			er = GetExternalId(ulCompanyId, &extcompany);
			if (er != erSuccess)
				return er;
		}
	} else {
		if (objclass == CONTAINER_COMPANY)
			return KCERR_NO_SUPPORT;
		/* When hosted is disabled, use company id 0 */
		ulCompanyId = 0;
	}

	// Get all the items of the requested type
	er = GetLocalObjectIdList(objclass, ulCompanyId, &unique_tie(lpLocalIds));
	if (er != erSuccess)
		return er;

	auto lpObjects = std::make_unique<std::list<localobjectdetails_t>>();
	for (const auto &loc_id : *lpLocalIds) {
		if (IsInternalObject(loc_id)) {
			// Local user, add it to the result array directly
			objectdetails_t details;
			er = GetLocalObjectDetails(loc_id, &details);
			if(er != erSuccess)
				return er;
			if (ulFlags & USERMANAGEMENT_ADDRESSBOOK)
				if (MustHide(*lpSecurity, ulFlags, details))
					continue;
			// Reset details, this saves time copying unwanted data, but keep the correct class
			if (ulFlags & USERMANAGEMENT_IDS_ONLY)
				details = objectdetails_t(details.GetClass());
			lpObjects->emplace_back(loc_id, details);
		} else if (GetExternalId(loc_id, &externid, NULL, &signature) == erSuccess) {
			mapSignatureIdToLocal.emplace(externid, std::pair<unsigned int, std::string>(loc_id, signature));
		} else {
			// cached externid not found for local object id
		}
	}

	if (bSync && !bIsSafeMode) {
		// We now have a map, mapping external IDs to local user IDs (and their signatures)
		try {
			// Get full user list
			lpExternSignatures = lpPlugin->getAllObjects(extcompany, objclass);
			// TODO: check requested 'objclass'
		} catch (const notsupported &) {
			return KCERR_NO_SUPPORT;
		} catch (const objectnotfound &) {
			return KCERR_NOT_FOUND;
		} catch (const std::exception &e) {
			ec_log_warn("K-1502: Unable to retrieve list from external user source: %s", e.what());
			return KCERR_PLUGIN_ERROR;
		}

		// Loop through all the external signatures, adding them to the lpUsers list which we're going to be returning
		for (const auto &ext_sig : lpExternSignatures) {
			auto iterSignatureIdToLocal = mapSignatureIdToLocal.find(ext_sig.id);
			if (iterSignatureIdToLocal == mapSignatureIdToLocal.cend()) {
				// User is in external user database, but not in local, so add
				er = MoveOrCreateLocalObject(ext_sig, &ulObjectId, &bMoved);
				if(er != erSuccess) {
					// Create failed, so skip this entry
					er = erSuccess;
					continue;
				}

				// Entry was moved rather then created, this means that in our localIdList we have
				// an entry which matches this object.
				if (bMoved)
					for (auto i = mapSignatureIdToLocal.begin();
					     i != mapSignatureIdToLocal.cend(); ++i)
						if (i->second.first == ulObjectId) {
							mapSignatureIdToLocal.erase(i);
							break;
						}
			} else {
				ulObjectId = iterSignatureIdToLocal->second.first;
				er = CheckObjectModified(ulObjectId,							// Object id
										 iterSignatureIdToLocal->second.second,	// local signature
										 ext_sig.signature);		// remote signature
				if (er != erSuccess)
					return er;
				// Remove the external user from the list of internal IDs,
				// since we don't need this entry for the plugin details match
				mapSignatureIdToLocal.erase(iterSignatureIdToLocal);
			}
			// Add to conversion map so we can obtain the details
			mapExternIdToLocal.emplace(ext_sig.id, ulObjectId);
		}
	} else {
		if (bIsSafeMode)
			ec_log_info("user_safe_mode: skipping retrieve/sync users from LDAP");
		lpExternSignatures.clear();
		// Dont sync, just use whatever is in the local user database
		for (const auto &sil : mapSignatureIdToLocal) {
			lpExternSignatures.emplace_back(sil.first, sil.second.second);
			mapExternIdToLocal.emplace(sil.first, sil.second.first);
		}
	}

	er = GetLocalObjectListFromSignatures(lpExternSignatures, mapExternIdToLocal, ulFlags, lpObjects.get());
	if (er != erSuccess)
		return er;

	// mapSignatureIdToLocal is now a map of objects that were NOT in the external user database
	if(bSync) {
		if (bIsSafeMode)
			ec_log_err("user_safe_mode: would normally now delete %zu local users (you may see this message more often as the delete is now omitted)", mapExternIdToLocal.size() - lpExternSignatures.size());
		else
		for (const auto &sil : mapSignatureIdToLocal)
			/* second == map value, first == id */
			MoveOrDeleteLocalObject(sil.second.first, sil.first.objclass);
	}

	// Convert details for client usage
	if (!(ulFlags & USERMANAGEMENT_IDS_ONLY)) {
		for (auto &obj : *lpObjects) {
			if (IsInternalObject(obj.ulId))
				continue;
			er = UpdateUserDetailsToClient(&obj);
			if (er != erSuccess)
				return er;
		}
	}

	if (lppObjects) {
		lpObjects->sort();
		*lppObjects = lpObjects.release();
	}
	return erSuccess;
}

ECRESULT ECUserManagement::GetSubObjectsOfObjectAndSync(userobject_relation_t relation, unsigned int ulParentId,
														std::list<localobjectdetails_t> **lppObjects, unsigned int ulFlags)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpCompanies;
	// Extern ids
	signatures_t lpSignatures;
	// Extern -> Local
	std::map<objectid_t, unsigned int> mapExternIdToLocal;
	objectid_t objectid;
	objectdetails_t details;
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	if (!m_lpSession->GetSessionManager()->IsHostedSupported() &&
	    (relation == OBJECTRELATION_COMPANY_VIEW ||
	    relation == OBJECTRELATION_COMPANY_ADMIN))
		return KCERR_NO_SUPPORT;

	// The 'everyone' group contains all visible users for the currently logged in user.
	auto lpObjects = std::make_unique<std::list<localobjectdetails_t>>();
	if (relation == OBJECTRELATION_GROUP_MEMBER && ulParentId == KOPANO_UID_EVERYONE) {
		ECSecurity *lpSecurity = NULL;

		er = GetSecurity(&lpSecurity);
		if (er != erSuccess)
			return er;
		er = lpSecurity->GetViewableCompanyIds(ulFlags, &unique_tie(lpCompanies));
		if (er != erSuccess)
			return er;

		/* Fallback in case hosted is not supported */
		if (lpCompanies->empty())
			lpCompanies->emplace_back(0, CONTAINER_COMPANY);

		for (const auto &obj : *lpCompanies) {
			std::unique_ptr<std::list<localobjectdetails_t> > lpObjectsTmp;
			er = GetCompanyObjectListAndSync(OBJECTCLASS_UNKNOWN,
			     obj.ulId, &unique_tie(lpObjectsTmp),
			     ulFlags | USERMANAGEMENT_SHOW_HIDDEN);
			if (er != erSuccess)
				return er;
			lpObjects->merge(*lpObjectsTmp);
		}
		// TODO: remove excessive objects from lpObjects ? seems that this list is going to contain a lot... maybe too much?
	} else {
		er = GetExternalId(ulParentId, &objectid);
		if(er != erSuccess)
			return er;
		try {
			lpSignatures = lpPlugin->getSubObjectsForObject(relation, objectid);
		} catch (const objectnotfound &) {
			MoveOrDeleteLocalObject(ulParentId, objectid.objclass);
			return KCERR_NOT_FOUND;
		} catch (const notsupported &) {
			return KCERR_NO_SUPPORT;
		} catch (const std::exception &e) {
			ec_log_warn("K-1503: Unable to retrieve members for %s relation %d: %s.",
				RelationTypeToName(relation), ulParentId, e.what());
			return KCERR_PLUGIN_ERROR;
		}
		er = GetLocalObjectsIdsOrCreate(lpSignatures, &mapExternIdToLocal);
		if (er != erSuccess)
			return er;
		er = GetLocalObjectListFromSignatures(lpSignatures, mapExternIdToLocal, ulFlags | USERMANAGEMENT_SHOW_HIDDEN, lpObjects.get());
		if (er != erSuccess)
			return er;
	}

	// Convert details for client usage
	if (!(ulFlags & USERMANAGEMENT_IDS_ONLY)) {
		for (auto &obj : *lpObjects) {
			if (IsInternalObject(obj.ulId))
				continue;
			er = UpdateUserDetailsToClient(&obj);
			if (er != erSuccess)
				return er;
		}
	}
	/*
	 * We now have the full list of objects coming from the plugin.
	 * NOTE: This list is not fool proof, the caller should check
	 * which entries are visible to the user and which ones are not.
	 * We cannot do that here since ECSecurity calls this function
	 * when determining if an object is visible. And endless recursive
	 * loops are not really a pretty sight.
	 */
	if (lppObjects) {
		lpObjects->sort();
		lpObjects->unique();
		*lppObjects = lpObjects.release();
	}
	return er;
}

/**
 * Get parents for an object, with on-the-fly deletion of the specified parent
 * object.
 */
ECRESULT ECUserManagement::GetParentObjectsOfObjectAndSync(userobject_relation_t relation, unsigned int ulChildId,
														   std::list<localobjectdetails_t> **lppObjects, unsigned int ulFlags) {
	// Extern ids
	signatures_t lpSignatures;
	// Extern -> Local
	std::map<objectid_t, unsigned int> mapExternIdToLocal;
	objectid_t objectid;
	objectdetails_t details;
	unsigned int ulObjectId = 0;
	ECSecurity *lpSecurity = NULL;
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if (er != erSuccess)
		return er;
	er = GetSecurity(&lpSecurity);
	if (er != erSuccess)
		return er;
	if (!m_lpSession->GetSessionManager()->IsHostedSupported() &&
	    (relation == OBJECTRELATION_COMPANY_VIEW ||
	    relation == OBJECTRELATION_COMPANY_ADMIN))
		return KCERR_NO_SUPPORT;

	auto lpObjects = std::make_unique<std::list<localobjectdetails_t>>();
	if (relation == OBJECTRELATION_GROUP_MEMBER && ulChildId == KOPANO_UID_SYSTEM) {
		// System has no objects
	} else {
		er = GetExternalId(ulChildId, &objectid);
		if (er != erSuccess)
			return er;
		try {
			lpSignatures = lpPlugin->getParentObjectsForObject(relation, objectid);
		} catch (const objectnotfound &) {
			MoveOrDeleteLocalObject(ulChildId, objectid.objclass);
			return KCERR_NOT_FOUND;
		} catch (const notsupported &) {
			return KCERR_NO_SUPPORT;
		} catch (const std::exception &e) {
			ec_log_warn("K-1504: Unable to retrieve parents for %s relation %d: %s.",
				RelationTypeToName(relation), ulChildId, e.what());
			return KCERR_PLUGIN_ERROR;
		}
		er = GetLocalObjectsIdsOrCreate(lpSignatures, &mapExternIdToLocal);
		if (er != erSuccess)
			return er;
		er = GetLocalObjectListFromSignatures(lpSignatures, mapExternIdToLocal, ulFlags, lpObjects.get());
		if (er != erSuccess)
			return er;
	}

	// If we are requesting group membership we should always insert the everyone, since everyone is member of EVERYONE except for SYSTEM
	if (relation == OBJECTRELATION_GROUP_MEMBER && ulObjectId != KOPANO_UID_SYSTEM) {
		if ((!(ulFlags & USERMANAGEMENT_IDS_ONLY)) || (ulFlags & USERMANAGEMENT_ADDRESSBOOK)) {
			er = GetLocalObjectDetails(KOPANO_UID_EVERYONE, &details);
			if(er != erSuccess)
				return er;
			if (!(ulFlags & USERMANAGEMENT_ADDRESSBOOK) ||
				(lpSecurity->GetUserId() != KOPANO_UID_SYSTEM &&
				 !details.GetPropBool(OB_PROP_B_AB_HIDDEN)))
			{
				if (ulFlags & USERMANAGEMENT_IDS_ONLY)
					details = objectdetails_t(details.GetClass());
				lpObjects->emplace_back(KOPANO_UID_EVERYONE, details);
			}
		} else
			lpObjects->emplace_back(KOPANO_UID_EVERYONE, objectdetails_t(DISTLIST_SECURITY));
	}

	// Convert details for client usage
	if (!(ulFlags & USERMANAGEMENT_IDS_ONLY)) {
		for (auto &obj : *lpObjects) {
			if (IsInternalObject(obj.ulId))
				continue;
			er = UpdateUserDetailsToClient(&obj);
			if (er != erSuccess)
				return er;
		}
	}
	/*
	 * We now have the full list of objects coming from the plugin.
	 * NOTE: This list is not fool proof, the caller should check
	 * which entries are visible to the user and which ones are not.
	 * We cannot do that here since ECSecurity calls this function
	 * when determining if an object is visible. And endless recursive
	 * loops are not really a pretty sight.
	 */
	if (lppObjects) {
		lpObjects->sort();
		lpObjects->unique();
		*lppObjects = lpObjects.release();
	}
	return er;
}

/**
 * Set data for a single object, with on-the-fly deletion of the specified
 * object id.
 */
ECRESULT ECUserManagement::SetObjectDetailsAndSync(unsigned int ulObjectId, const objectdetails_t &sDetails, std::list<std::string> *lpRemoveProps) {
	objectid_t objectid;
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;

	// Resolve the objectname and sync the object into local database
	er = GetExternalId(ulObjectId, &objectid);
	if (er != erSuccess)
		return er;

	try {
		lpPlugin->changeObject(objectid, sDetails, lpRemoveProps);
	} catch (const objectnotfound &) {
		MoveOrDeleteLocalObject(ulObjectId, objectid.objclass);
		return KCERR_NOT_FOUND;
	} catch (const notimplemented &e) {
		ec_log_warn("K-1505: Unable to set details for external %s %u: %s",
			ObjectClassToName(objectid.objclass), ulObjectId, e.what());
		return KCERR_NOT_IMPLEMENTED;
	} catch (const std::exception &e) {
		ec_log_warn("K-1506: Unable to set details for external %s %u: %s",
			ObjectClassToName(objectid.objclass), ulObjectId, e.what());
		return KCERR_PLUGIN_ERROR;
	}

	// Purge cache
	m_lpSession->GetSessionManager()->GetCacheManager()->UpdateUser(ulObjectId);
	return erSuccess;
}

// Create an object with specific ID or modify an existing object. Used for sync purposes
ECRESULT ECUserManagement::CreateOrModifyObject(const objectid_t &sExternId, const objectdetails_t &sDetails, unsigned int ulPreferredId, std::list<std::string> *lpRemoveProps) {
	objectsignature_t signature;
	UserPlugin *lpPlugin = NULL;
	unsigned int ulObjectId = 0;

	// Local items have no external id
	if (sExternId.id.empty())
		return erSuccess;
	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;

	// Resolve the objectname and sync the object into local database
	if (GetLocalId(sExternId, &ulObjectId, &signature.signature) == erSuccess) {
		try {
			lpPlugin->changeObject(sExternId, sDetails, lpRemoveProps);
		} catch (const objectnotfound &) {
			MoveOrDeleteLocalObject(ulObjectId, sExternId.objclass);
			return KCERR_NOT_FOUND;
		} catch (const notimplemented &e) {
			ec_log_warn("K-1507: Unable to set details for external %s \"%s\": %s",
				ObjectClassToName(sExternId.objclass),
				sExternId.tostring().c_str(), e.what());
			return KCERR_NOT_IMPLEMENTED;
		} catch (const std::exception &e) {
			ec_log_warn("K-1508: Unable to set details for external %s \"%s\": %s",
				ObjectClassToName(sExternId.objclass),
				sExternId.tostring().c_str(), e.what());
			return KCERR_PLUGIN_ERROR;
		}
		return erSuccess;
	}

	// Object does not exist yet, create in external database
	try {
		signature = lpPlugin->createObject(sDetails);
	} catch (const notimplemented &e) {
		ec_log_warn("K-1509: Unable to create %s \"%s\" in external database: %s",
			ObjectClassToName(sExternId.objclass),
			sExternId.tostring().c_str(), e.what());
		return KCERR_NOT_IMPLEMENTED;
	} catch (const collision_error &e) {
		ec_log_warn("K-1510: Unable to create %s \"%s\" in external database: %s",
			ObjectClassToName(sExternId.objclass),
			sExternId.tostring().c_str(), e.what());
		return KCERR_COLLISION;
	} catch (const std::exception &e) {
		ec_log_warn("K-1511: Unable to create %s \"%s\" in external database: %s",
			ObjectClassToName(sExternId.objclass),
			sExternId.tostring().c_str(), e.what());
		return KCERR_PLUGIN_ERROR;
	}

	er = CreateLocalObjectSimple(signature, ulPreferredId);
	if (er == erSuccess)
		return erSuccess;
	/*
	 * Failed to create object locally, it should be removed from external DB.
	 * Otherwise it will remain present in the external DB and synced later while
	 * ECSync will also perform a retry to create the object. Obviously this
	 * can lead to unexpected behavior because the Offline server should never(!)
	 * sync between plugin and ECUsermanagement somewhere else other than this
	 * function
	 */
	try {
		lpPlugin->deleteObject(signature.id);
	} catch (const std::exception &e) {
		ec_log_warn("K-1512: Unable to delete %s \"%s\" from external database after failed (partial) creation: %s",
			ObjectClassToName(sExternId.objclass),
			sExternId.tostring().c_str(), e.what());
	}
	return er;
}

/**
 * Add a member to a group, with on-the-fly deletion of the specified group id.
 */
ECRESULT ECUserManagement::AddSubObjectToObjectAndSync(userobject_relation_t relation, unsigned int ulParentId, unsigned int ulChildId) {
	ABEID eid(MAPI_ABCONT, MUIDECSAB, 1);
	objectid_t parentid, childid;
	SOURCEKEY sSourceKey;
	UserPlugin *lpPlugin = NULL;

	/* We don't support operations on local items */
	if (IsInternalObject(ulParentId) || IsInternalObject(ulChildId))
		return KCERR_INVALID_PARAMETER;
	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	er = GetExternalId(ulParentId, &parentid);
	if(er != erSuccess)
		return er;
	er = GetExternalId(ulChildId, &childid);
	if(er != erSuccess)
		return er;

	try {
		lpPlugin->addSubObjectRelation(relation, parentid, childid);
	} catch (const objectnotfound &) {
		MoveOrDeleteLocalObject(ulParentId, parentid.objclass);
		return KCERR_NOT_FOUND;
	} catch (const notimplemented &) {
		return KCERR_NOT_IMPLEMENTED;
	} catch (const collision_error &) {
		ec_log_crit("ECUserManagement::AddSubObjectToObjectAndSync(): addSubObjectRelation failed with a collision error");
		return KCERR_COLLISION;
	} catch (const std::exception &e) {
		ec_log_warn("K-1513: Unable to add %s relation (%u,%u) to external user database: %s",
			RelationTypeToName(relation),
			ulParentId, ulChildId, e.what());
		return KCERR_PLUGIN_ERROR;
	}
	/*
	 * Add addressbook change, note that setting the objectrelation won't update the signature
	 * which means we have to run this update manually. Also note that when adding a child to a
	 * group the _group_ has changed, but when adding a child to a company the _child_ has changed.
	 * This will cause the sync to recreate the correct object with the updated rights.
	 */
	if (relation == OBJECTRELATION_GROUP_MEMBER)
		er = GetABSourceKeyV1(ulParentId, &sSourceKey);
	else if (relation == OBJECTRELATION_COMPANY_ADMIN || relation == OBJECTRELATION_COMPANY_VIEW)
		er = GetABSourceKeyV1(ulChildId, &sSourceKey);

	if (relation == OBJECTRELATION_GROUP_MEMBER || relation == OBJECTRELATION_COMPANY_ADMIN || relation == OBJECTRELATION_COMPANY_VIEW)
	{
		if (er != erSuccess)
			return er;
		AddABChange(m_lpSession, ICS_AB_CHANGE, sSourceKey, SOURCEKEY(CbABEID(&eid), (char *)&eid));
	}
	m_lpSession->GetSessionManager()->GetCacheManager()->UpdateUser(ulParentId);
	return er;
}

ECRESULT ECUserManagement::DeleteSubObjectFromObjectAndSync(userobject_relation_t relation, unsigned int ulParentId, unsigned int ulChildId) {
	ABEID eid(MAPI_ABCONT, MUIDECSAB, 1);
	objectid_t parentid, childid;
	SOURCEKEY sSourceKey;
	UserPlugin *lpPlugin = NULL;

	/* We don't support operations on local items */
	if (IsInternalObject(ulParentId) || IsInternalObject(ulChildId))
		return KCERR_INVALID_PARAMETER;
	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	er = GetExternalId(ulParentId, &parentid);
	if(er != erSuccess)
		return er;
	er = GetExternalId(ulChildId, &childid);
	if(er != erSuccess)
		return er;

	try {
		lpPlugin->deleteSubObjectRelation(relation, parentid, childid);
	} catch (const objectnotfound &) {
		// We can't delete the parent because we don't know whether the child or the entire parent was not found
		return KCERR_NOT_FOUND;
	} catch (const notimplemented &) {
		return KCERR_NOT_IMPLEMENTED;
	} catch (const std::exception &e) {
		ec_log_warn("K-1514: Unable to remove %s relation (%u,%u) from external user database: %s.",
			RelationTypeToName(relation),
			ulParentId, ulChildId, e.what());
		return KCERR_PLUGIN_ERROR;
	}
	/*
	 * Add addressbook change, note that setting the objectrelation won't update the signature
	 * which means we have to run this update manually. Also note that when deleting a child from a
	 * group the _group_ has changed, but when deleting a child from a company the _child_ has changed.
	 * This will cause the sync to recreate the correct object with the updated rights.
	 */
	if (relation == OBJECTRELATION_GROUP_MEMBER)
		er = GetABSourceKeyV1(ulParentId, &sSourceKey);
	else if (relation == OBJECTRELATION_COMPANY_ADMIN || relation == OBJECTRELATION_COMPANY_VIEW)
		er = GetABSourceKeyV1(ulChildId, &sSourceKey);

	if (relation == OBJECTRELATION_GROUP_MEMBER || relation == OBJECTRELATION_COMPANY_ADMIN || relation == OBJECTRELATION_COMPANY_VIEW)
	{
		if (er != erSuccess)
			return er;

		AddABChange(m_lpSession, ICS_AB_CHANGE, sSourceKey, SOURCEKEY(CbABEID(&eid), (char *)&eid));
	}
	m_lpSession->GetSessionManager()->GetCacheManager()->UpdateUser(ulParentId);
	return er;
}

// TODO: cache these values ?
ECRESULT ECUserManagement::ResolveObject(objectclass_t objclass,
    const std::string &strName, const objectid_t &sCompany,
    objectid_t *lpsExternId) const
{
	objectid_t sObjectId;
	UserPlugin *lpPlugin = NULL;
	objectsignature_t objectsignature;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;

	try {
		objectsignature = lpPlugin->resolveName(objclass, strName, sCompany);
	} catch (const objectnotfound &) {
		return KCERR_NOT_FOUND;
	} catch (const std::exception &) {
		return KCERR_PLUGIN_ERROR;
	}
	*lpsExternId = std::move(objectsignature.id);
	return erSuccess;
}

/**
 * Resolve an object name to an object id, with on-the-fly creation of the
 * specified object class.
 */
ECRESULT ECUserManagement::ResolveObjectAndSync(objectclass_t objclass, const char* szName, unsigned int* lpulObjectId) {
	objectsignature_t objectsignature;
	std::string username, companyname;
	UserPlugin *lpPlugin = NULL;
	bool bHosted = m_lpSession->GetSessionManager()->IsHostedSupported();
	objectid_t sCompany(CONTAINER_COMPANY);

	if (!szName) {
		ec_log_crit("Invalid argument szName in call to ECUserManagement::ResolveObjectAndSync()");
		return KCERR_INVALID_PARAMETER;
	}
	if (!lpulObjectId) {
		ec_log_crit("Invalid argument lpulObjectId call to ECUserManagement::ResolveObjectAndSync()");
		return KCERR_INVALID_PARAMETER;
	}

	if ((OBJECTCLASS_TYPE(objclass) == OBJECTTYPE_UNKNOWN ||
		 objclass == OBJECTCLASS_USER ||
		 objclass == ACTIVE_USER) &&
		strcasecmp(szName, KOPANO_ACCOUNT_SYSTEM) == 0)
	{
		*lpulObjectId = KOPANO_UID_SYSTEM;
		return erSuccess;
	} else if ((OBJECTCLASS_TYPE(objclass) == OBJECTTYPE_UNKNOWN ||
				objclass == OBJECTCLASS_DISTLIST ||
				objclass == DISTLIST_SECURITY) &&
			   strcasecmp(szName, KOPANO_FULLNAME_EVERYONE) == 0)
	{
		*lpulObjectId = KOPANO_UID_EVERYONE;
		return erSuccess;
	} else if ((OBJECTCLASS_TYPE(objclass) == OBJECTTYPE_UNKNOWN ||
				objclass == OBJECTCLASS_CONTAINER ||
				objclass == CONTAINER_COMPANY) &&
			   strlen(szName) == 0)
	{
		*lpulObjectId = 0;
		return erSuccess;
	}

	if (bHosted &&
		(OBJECTCLASS_TYPE(objclass) == OBJECTTYPE_UNKNOWN ||
		 OBJECTCLASS_TYPE(objclass) == OBJECTTYPE_MAILUSER ||
		 OBJECTCLASS_TYPE(objclass) == OBJECTTYPE_DISTLIST) &&
		objclass != NONACTIVE_CONTACT) {
			/*
			 * GetUserAndCompanyFromLoginName() will return KCWARN_PARTIAL_COMPLETION
			 * when the companyname could not be determined from the username. This isn't
			 * something bad since perhaps the username is unique between all companies.
			 */
			auto er = GetUserAndCompanyFromLoginName(szName, &username, &companyname);
			if (er == KCWARN_PARTIAL_COMPLETION)
				er = erSuccess;
			else if (er != erSuccess)
				return er;
	} else {
		username = szName;
		companyname.clear();
	}

	if (bHosted && !companyname.empty()) {
		auto er = ResolveObject(CONTAINER_COMPANY, companyname, objectid_t(), &sCompany);
		if (er == KCERR_NOT_FOUND) {
			ec_log_warn("Search company error by plugin for company \"%s\"", companyname.c_str());
			return er;
		}
	}

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;

	try {
		objectsignature = lpPlugin->resolveName(objclass, username, sCompany);
	} catch (const notsupported &) {
		return KCERR_NO_SUPPORT;
	} catch (const objectnotfound &e) {
		ec_log_warn("K-1515: Object not found %s \"%s\": %s",
			ObjectClassToName(objclass), szName, e.what());
		return KCERR_NOT_FOUND;
	} catch (const std::exception &e) {
		ec_log_warn("K-1516: Unable to resolve %s \"%s\": %s",
			ObjectClassToName(objclass), szName, e.what());
		return KCERR_NOT_FOUND;
	}
	return GetLocalObjectIdOrCreate(objectsignature, lpulObjectId);
}

/**
 * Get MAPI property data for a group or user/group id, with on-the-fly
 * deletion of the specified user/group.
 */
ECRESULT ECUserManagement::GetProps(struct soap *soap, unsigned int ulId, struct propTagArray *lpPropTagArray, struct propValArray *lpPropValArray) {
	objectdetails_t objectdetails;
	auto er = GetObjectDetails(ulId, &objectdetails);
	if (er != erSuccess)
		return KCERR_NOT_FOUND;
	return ConvertObjectDetailsToProps(soap, ulId, &objectdetails, lpPropTagArray, lpPropValArray);
}

ECRESULT ECUserManagement::GetContainerProps(struct soap *soap, unsigned int ulObjectId, struct propTagArray *lpPropTagArray, struct propValArray *lpPropValArray)
{
	ECSecurity *lpSecurity = NULL;
	objectdetails_t objectdetails;

	if (ulObjectId == KOPANO_UID_ADDRESS_BOOK ||
	    ulObjectId == KOPANO_UID_GLOBAL_ADDRESS_BOOK ||
	    ulObjectId == KOPANO_UID_GLOBAL_ADDRESS_LISTS)
		return ConvertABContainerToProps(soap, ulObjectId, lpPropTagArray, lpPropValArray);
	auto er = GetObjectDetails(ulObjectId, &objectdetails);
	if (er != erSuccess)
		return KCERR_NOT_FOUND;
	er = GetSecurity(&lpSecurity);
	if (er != erSuccess)
		return er;
	return ConvertContainerObjectDetailsToProps(soap, ulObjectId, &objectdetails, lpPropTagArray, lpPropValArray);
}

// Get local details
ECRESULT ECUserManagement::GetLocalObjectDetails(unsigned int ulId,
    objectdetails_t *lpDetails) const
{
	ECRESULT er = erSuccess;
	objectdetails_t sDetails, sPublicStoreDetails;
	ECSecurity *lpSecurity = NULL;

	if(ulId == KOPANO_UID_SYSTEM) {
		sDetails = objectdetails_t(ACTIVE_USER);
		sDetails.SetPropString(OB_PROP_S_LOGIN, KOPANO_ACCOUNT_SYSTEM);
		sDetails.SetPropString(OB_PROP_S_PASSWORD, "");
		sDetails.SetPropString(OB_PROP_S_FULLNAME, KOPANO_FULLNAME_SYSTEM);
		sDetails.SetPropString(OB_PROP_S_EMAIL, m_lpConfig->GetSetting("system_email_address"));
		sDetails.SetPropInt(OB_PROP_I_ADMINLEVEL, ADMIN_LEVEL_SYSADMIN);
		sDetails.SetPropBool(OB_PROP_B_AB_HIDDEN, parseBool(m_lpConfig->GetSetting("hide_system")));
		sDetails.SetPropString(OB_PROP_S_SERVERNAME, m_lpConfig->GetSetting("server_name", "", "Unknown"));
	} else if (ulId == KOPANO_UID_EVERYONE) {
		er = GetSecurity(&lpSecurity);
		if (er != erSuccess)
			return er;

		sDetails = objectdetails_t(DISTLIST_SECURITY);
		sDetails.SetPropString(OB_PROP_S_LOGIN, KOPANO_ACCOUNT_EVERYONE);
		sDetails.SetPropString(OB_PROP_S_FULLNAME, KOPANO_FULLNAME_EVERYONE);
		sDetails.SetPropBool(OB_PROP_B_AB_HIDDEN, parseBool(m_lpConfig->GetSetting("hide_everyone")) && lpSecurity->GetAdminLevel() == 0);

		if (m_lpSession->GetSessionManager()->IsDistributedSupported() &&
		    GetPublicStoreDetails(&sPublicStoreDetails) == erSuccess)
			sDetails.SetPropString(OB_PROP_S_SERVERNAME, sPublicStoreDetails.GetPropString(OB_PROP_S_SERVERNAME));
	}

	*lpDetails = std::move(sDetails);
	return erSuccess;
}

// Get remote details
ECRESULT ECUserManagement::GetExternalObjectDetails(unsigned int ulId, objectdetails_t *lpDetails)
{
	objectdetails_t details, detailscached;
	objectid_t externid;
	UserPlugin *lpPlugin = NULL;

	auto er = GetExternalId(ulId, &externid);
	if (er != erSuccess)
		return er;

	auto cache = m_lpSession->GetSessionManager()->GetCacheManager();
	er = cache->GetUserDetails(ulId, &detailscached);
	if (er == erSuccess) {
		// @todo should compare signature, and see if we need new details for this user
		er = UpdateUserDetailsToClient(&detailscached);
		if (er != erSuccess)
			return er;
		*lpDetails = std::move(detailscached);
		return erSuccess;
	}

	er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;

	try {
		details = lpPlugin->getObjectDetails(externid);
	} catch (const objectnotfound &) {
		/*
		 * MoveOrDeleteLocalObject() will call this function to determine if the
		 * object has been moved. So do _not_ call MoveOrDeleteLocalObject() from
		 * this location!.
		 */
		DeleteLocalObject(ulId, externid.objclass);
		return KCERR_NOT_FOUND;
	} catch (const notsupported &) {
		return KCERR_NO_SUPPORT;
	} catch (const std::exception &e) {
		ec_log_warn("K-1517: Unable to get %s details for object id %d: %s",
			ObjectClassToName(externid.objclass), ulId, e.what());
		return KCERR_NOT_FOUND;
	}

	/* Update cache so we don't have to bug the plugin until the data has changed.
	 * Note that we don't care if the update succeeded, if it fails we will retry
	 * when the user details are requested for a second time. */
	cache->SetUserDetails(ulId, details);
	if (! IsInternalObject(ulId)) {
		er = UpdateUserDetailsToClient(&details);
		if (er != erSuccess)
			return er;
	}
	*lpDetails = std::move(details);
	return erSuccess;
}

// **************************************************************************************************************************************
//
// Functions that actually do the SQL
//
// **************************************************************************************************************************************

ECRESULT ECUserManagement::GetExternalId(unsigned int ulId,
    objectid_t *lpExternId, unsigned int *lpulCompanyId,
    std::string *lpSignature) const
{
	if (IsInternalObject(ulId))
		return KCERR_INVALID_PARAMETER;
	auto mgr = m_lpSession->GetSessionManager()->GetCacheManager();
	return mgr->GetUserObject(ulId, lpExternId, lpulCompanyId, lpSignature);
}

ECRESULT ECUserManagement::GetLocalId(const objectid_t &sExternId,
    unsigned int *lpulId, std::string *lpSignature) const
{
	return m_lpSession->GetSessionManager()->GetCacheManager()->GetUserObject(sExternId, lpulId, NULL, lpSignature);
}

ECRESULT ECUserManagement::GetLocalObjectIdOrCreate(const objectsignature_t &sSignature, unsigned int *lpulObjectId)
{
	unsigned ulObjectId = 0;

	auto er = m_lpSession->GetSessionManager()->GetCacheManager()->GetUserObject(sSignature.id, &ulObjectId, NULL, NULL);
	if (er == KCERR_NOT_FOUND)
		er = MoveOrCreateLocalObject(sSignature, &ulObjectId, NULL);
	if (er != erSuccess)
		return er;
	if (lpulObjectId)
		*lpulObjectId = ulObjectId;
	return erSuccess;
}

ECRESULT ECUserManagement::GetLocalObjectsIdsOrCreate(const std::list<objectsignature_t> &lstSignatures,
    std::map<objectid_t, unsigned int> *lpmapLocalObjIds)
{
	std::list<objectid_t> lstExternObjIds;
	unsigned int ulObjectId;

	for (const auto &sig : lstSignatures)
		lstExternObjIds.emplace_back(sig.id);
	auto er = m_lpSession->GetSessionManager()->GetCacheManager()->GetUserObjects(lstExternObjIds, lpmapLocalObjIds);
	if (er != erSuccess)
		return er;

	for (const auto &sig : lstSignatures) {
		auto result = lpmapLocalObjIds->emplace(sig.id, 0);
		if (!result.second)
			// object already exists
			continue;
		er = MoveOrCreateLocalObject(sig, &ulObjectId, NULL);
		if (er != erSuccess) {
			lpmapLocalObjIds->erase(result.first);
			return er;
		}
		result.first->second = ulObjectId;
	}
	return erSuccess;
}

ECRESULT ECUserManagement::GetLocalObjectIdList(objectclass_t objclass,
    unsigned int ulCompanyId, std::list<unsigned int> **lppObjects) const
{
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpResult;

	auto er = m_lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	std::string strQuery =
		"SELECT id FROM users "
		"WHERE " + OBJECTCLASS_COMPARE_SQL("objectclass", objclass);
	/* As long as the Offline server has partial hosted support,
	 * we must comment out this additional where statement... */
	if (m_lpSession->GetSessionManager()->IsHostedSupported())
		/* Everyone and SYSTEM don't have a company but must be
		 * included by the query, so write exception case for them */
		strQuery +=
			" AND ("
				"company = " + stringify(ulCompanyId) + " "
				"OR id = " + stringify(ulCompanyId) + " "
				"OR id = " + stringify(KOPANO_UID_SYSTEM) + " "
				"OR id = " + stringify(KOPANO_UID_EVERYONE) + ")";
	er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return er;

	auto lpObjects = std::make_unique<std::list<unsigned int>>();
	while(1) {
		auto lpRow = lpResult.fetch_row();
		if(lpRow == NULL)
			break;
		if(lpRow[0] == NULL)
			continue;
		lpObjects->emplace_back(atoi(lpRow[0]));
	}
	*lppObjects = lpObjects.release();
	return erSuccess;
}

ECRESULT ECUserManagement::CreateObjectAndSync(const objectdetails_t &details, unsigned int *lpulId)
{
	objectsignature_t objectsignature;
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;

	try {
		objectsignature = lpPlugin->createObject(details);
	} catch (const notimplemented &e) {
		ec_log_warn("K-1518: Unable to create %s in user database: %s",
			ObjectClassToName(details.GetClass()), e.what());
		return KCERR_NOT_IMPLEMENTED;
	} catch (const collision_error &e) {
		ec_log_warn("K-1519: Unable to create %s in user database: %s",
			ObjectClassToName(details.GetClass()), e.what());
		return KCERR_COLLISION;
	} catch (const std::exception &e) {
		ec_log_warn("K-1520: Unable to create %s in user database: %s",
			ObjectClassToName(details.GetClass()), e.what());
		return KCERR_PLUGIN_ERROR;
	}

	er = MoveOrCreateLocalObject(objectsignature, lpulId, NULL);
	if (er == erSuccess)
		return erSuccess;

	// We could not create the object in the local database. This means that we have to rollback
	// our object creation in the plugin, because otherwise the database and the plugin would stay
	// out-of-sync until you add new licenses. Also, it would be impossible for the user to manually
	// rollback the object creation, as you need a Kopano object id to delete an object, and we don't have
	// one of those, because CreateLocalObject() failed.
	try {
		lpPlugin->deleteObject(objectsignature.id);
	} catch (const std::exception &e) {
		ec_log_warn("K-1521: Unable to delete %s from plugin database after failed creation: %s",
			ObjectClassToName(details.GetClass()), e.what());
		return KCERR_PLUGIN_ERROR;
	}
	return er;
}

ECRESULT ECUserManagement::DeleteObjectAndSync(unsigned int ulId)
{
	objectid_t objectid;
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	er = GetExternalId(ulId, &objectid);
	if (er != erSuccess)
		return er;

	try {
		lpPlugin->deleteObject(objectid);
	} catch (const objectnotfound &) {
		return KCERR_NOT_FOUND;
	} catch (const notimplemented &e) {
		ec_log_warn("K-1522: Unable to delete %s in user database: %s",
			ObjectClassToName(objectid.objclass), e.what());
		return KCERR_NOT_IMPLEMENTED;
	} catch (const std::exception &e) {
		ec_log_warn("K-1523: Unable to delete %s in user database: %s",
			ObjectClassToName(objectid.objclass), e.what());
		return KCERR_PLUGIN_ERROR;
	}
	return DeleteLocalObject(ulId, objectid.objclass);
}

ECRESULT ECUserManagement::SetQuotaDetailsAndSync(unsigned int ulId, const quotadetails_t &details)
{
	objectid_t externid;
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	er = GetExternalId(ulId, &externid);
	if (er != erSuccess)
		return er;

	try {
		lpPlugin->setQuota(externid, details);
	} catch (const objectnotfound &) {
		MoveOrDeleteLocalObject(ulId, externid.objclass);
		return KCERR_NOT_FOUND;
	} catch (const notimplemented &e) {
		ec_log_warn("K-1524: Unable to set quota for %s %u: %s",
			ObjectClassToName(externid.objclass), ulId, e.what());
		return KCERR_NOT_IMPLEMENTED;
	} catch (const std::exception &e) {
		ec_log_warn("K-1525: Unable to set quota for %s %u: %s",
			ObjectClassToName(externid.objclass), ulId, e.what());
		return KCERR_PLUGIN_ERROR;
	}

	m_lpSession->GetSessionManager()->GetCacheManager()->UpdateUser(ulId);
	return erSuccess;
}

ECRESULT ECUserManagement::GetQuotaDetailsAndSync(unsigned int ulId, quotadetails_t* lpDetails, bool bGetUserDefault)
{
	ECRESULT er = erSuccess;
	objectid_t userid;
	quotadetails_t details;
	UserPlugin *lpPlugin = NULL;

	 if(IsInternalObject(ulId)) {
		if (bGetUserDefault)
			er = KCERR_NO_SUPPORT;
		else {
			lpDetails->bUseDefaultQuota = false;
			lpDetails->bIsUserDefaultQuota = false;
			lpDetails->llWarnSize = 0;
			lpDetails->llSoftSize = 0;
			lpDetails->llHardSize = 0;
		}
		return er;
	}

	auto sesmgr = m_lpSession->GetSessionManager();
	auto cache = sesmgr->GetCacheManager();
	er = cache->GetQuota(ulId, bGetUserDefault, lpDetails);
	if (er == erSuccess)
		return erSuccess; /* Cache contained requested information, we're done. */
	er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	er = GetExternalId(ulId, &userid);
	if (er != erSuccess)
		return er;
	if ((userid.objclass == CONTAINER_COMPANY && !sesmgr->IsHostedSupported()) ||
	    (userid.objclass != CONTAINER_COMPANY && bGetUserDefault))
		return KCERR_NO_SUPPORT;

	try {
		details = lpPlugin->getQuota(userid, bGetUserDefault);
	} catch (const objectnotfound &) {
		MoveOrDeleteLocalObject(ulId, userid.objclass);
		return KCERR_NOT_FOUND;
	} catch (const std::exception &e) {
		ec_log_warn("K-1526: Unable to get quota for %s %u: %s",
			ObjectClassToName(userid.objclass), ulId, e.what());
		return KCERR_PLUGIN_ERROR;
	}
	/* Update cache so we don't have to bug the plugin until the data has changed.
	 * Note that we don't care if the update succeeded, if it fails we will retry
	 * when the quota is requested for a second time. */
	cache->SetQuota(ulId, bGetUserDefault, details);
	*lpDetails = std::move(details);
	return erSuccess;
}

ECRESULT ECUserManagement::SearchObjectAndSync(const char* szSearchString, unsigned int ulFlags, unsigned int *lpulID)
{
	objectsignature_t objectsignature;
	signatures_t lpObjectsignatures;
	unsigned int ulId = 0;
	std::string strUsername, strCompanyname;
	ECSecurity *lpSecurity = NULL;
	UserPlugin *lpPlugin = NULL;
	objectid_t sCompanyId;
	std::map<unsigned int, std::list<unsigned int> > mapMatches;

	auto er = GetSecurity(&lpSecurity);
	if (er != erSuccess)
		return er;

	// Special case: SYSTEM
	if (strcasecmp(szSearchString, KOPANO_ACCOUNT_SYSTEM) == 0) {
		// Hide user SYSTEM when requested
		if (lpSecurity->GetUserId() != KOPANO_UID_SYSTEM) {
			auto szHideSystem = m_lpConfig->GetSetting("hide_system");
			if (szHideSystem && parseBool(szHideSystem))
				return KCERR_NOT_FOUND;
		}
		*lpulID = KOPANO_UID_SYSTEM;
		return erSuccess;
	} else if (strcasecmp(szSearchString, KOPANO_ACCOUNT_EVERYONE) == 0) {
		// Hide group everyone when requested
		if (lpSecurity->GetUserId() != KOPANO_UID_SYSTEM) {
			auto szHideEveryone = m_lpConfig->GetSetting("hide_everyone");
			if (szHideEveryone && parseBool(szHideEveryone) && lpSecurity->GetAdminLevel() == 0)
				return KCERR_NOT_FOUND;
		}
		*lpulID = KOPANO_UID_EVERYONE;
		return erSuccess;
	}

	er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	/*
	 * The search string might be the following:
	 *  1) Portion of the fullname
	 *  2) Portion of the username
	 *  3) Portion of the username including company name
	 *  4) Portion of the email address
	 *
	 *  Regular search in the plugin will correctly handle (1), (2) and (4),
	 *  but not (3) since the username, company name combination depends on the
	 *  server.cfg settings. Because of that we need to check if we can convert
	 *  the search string into a username & companyname which will tell us if
	 *  we are indeed received a string of type (3) and should use resolveName().
	 *  However it is common that '@' is used as separation character, which could
	 *  mean that email addresses are also identified as (3). So as fallback we
	 *  should still call searchObject() if the search string contains an '@'.
	 */
	/*
	 * We don't care about the return argument, when the call failed
	 * strCompanyname will remain empty and we will catch that when
	 * deciding if we are going to call resolveName() or searchObject().
	 */
	GetUserAndCompanyFromLoginName(szSearchString, &strUsername, &strCompanyname);
	if (!strCompanyname.empty()) {
		/*
		 * We need to resolve the company first
		 * if we can't, just search for the complete string,
		 * otherwise we can try to go for a full resolve
		 */
		er = ResolveObject(CONTAINER_COMPANY, strCompanyname, objectid_t(), &sCompanyId);
		if (er == erSuccess) {
			objectsignature_t resolved;
			try {
				// plugin should try all types to match 1 exact
				resolved = lpPlugin->resolveName(OBJECTCLASS_UNKNOWN, strUsername, sCompanyId);

				/* Loginname correctly resolved, this is an exact match so we don't
				 * have to do anything else for this object type.
				 * TODO: Optimize this further by sending message to caller that this
				 * entry is a 100% match and doesn't need to try to resolve any other
				 * object type. (IMPORTANT: when doing this, make sure we still check
				 * if the returned object is actually visible to the user or not!) */
				lpObjectsignatures.clear();
				lpObjectsignatures.emplace_back(resolved);
				goto done;
			}
			catch (...) {
				// retry with search object on any error
			}
		}
	}

	try {
		lpObjectsignatures = lpPlugin->searchObject(szSearchString, ulFlags);
	} catch (const notimplemented &) {
		return KCERR_NOT_FOUND;
	} catch (const objectnotfound &) {
		return KCERR_NOT_FOUND;
	} catch (const notsupported &) {
		return KCERR_NO_SUPPORT;
	} catch (const std::exception &e) {
		ec_log_warn("K-1527: Unable to perform string search for \"%s\" on user database: %s",
			szSearchString, e.what());
		return KCERR_PLUGIN_ERROR;
	}

	if (lpObjectsignatures.empty())
		return KCERR_NOT_FOUND;
	lpObjectsignatures.sort();
	lpObjectsignatures.unique();

done:
	/* Check each returned entry to see which one we are allowed to view
	 * TODO: check with a point system,
	 * if you have 2 objects, one have a match of 99% and one 50%
	 * use the one with 99% */
	for (const auto &sig : lpObjectsignatures) {
		unsigned int ulIdTmp = 0;

		er = GetLocalObjectIdOrCreate(sig, &ulIdTmp);
		if (er != erSuccess)
			return er;
		if (lpSecurity->IsUserObjectVisible(ulIdTmp) != erSuccess)
			continue;

		if (!(ulFlags & EMS_AB_ADDRESS_LOOKUP)) {
			/* We can only report a single (visible) entry, when we find multiple entries report a collision */
			if (ulId == 0) {
				ulId = ulIdTmp;
			} else if (ulId != ulIdTmp) {
				ec_log_err("K-1215: unexpected id %u/%u", ulId, ulIdTmp);
				return KCERR_COLLISION;
			}
		} else {
			/* EMS_AB_ADDRESS_LOOKUP specified. We use a different algorithm for disambiguating: We look at the object types
			 * and order them by USER, GROUP, OTHER. If there is only one match for any type, we return that as the matching
			 * entry, starting with OBJECTTYPE_MAILUSER, then OBJECTTYPE_DISTLIST, then OBJECTTYPE_CONTAINER, then
			 * NONACTIVE_CONTACT (full type).
			 *
			 * The reason for this is that various parts of the software use EMS_AB_ADDRESS_LOOKUP to specifically locate a
			 * user entryid, which should not be ambiguated by a contact or group name. However, unique group names should
			 * also be resolvable by this method.
			 */
			// combine on object class, and place contacts in their own place in the map, so they don't conflict on users.
			// since they're indexed on the full object type, they'll be at the end of the map.
			ULONG combine = OBJECTCLASS_TYPE(sig.id.objclass);
			if (sig.id.objclass == NONACTIVE_CONTACT)
				combine = sig.id.objclass;
			// Store the matching entry for later analysis
			mapMatches[combine].emplace_back(ulIdTmp);
		}
	}

	if(ulFlags & EMS_AB_ADDRESS_LOOKUP) {
		if (mapMatches.empty())
			return KCERR_NOT_FOUND;
		// mapMatches is already sorted numerically, so it's OBJECTTYPE_MAILUSER, OBJECTTYPE_DISTLIST, OBJECTTYPE_CONTAINER, NONACTIVE_CONTACT
		if(mapMatches.begin()->second.size() != 1) {
			// size() cannot be 0. Otherwise, it would not be there at all. So apparently, it is > 1, so it is ambiguous.
			ec_log_info("Resolved multiple users for search \"%s\".", szSearchString);
			return KCERR_COLLISION;
		}
		ulId = mapMatches.begin()->second.front();
	}

	if(ulId == 0)
		// Nothing matched..
		return KCERR_NOT_FOUND;
	*lpulID = ulId;
	return er;
}

ECRESULT ECUserManagement::QueryContentsRowData(struct soap *soap,
    const ECObjectTableList *lpRowList,
    const struct propTagArray *lpPropTagArray, struct rowSet **lppRowSet)
{
	int i = 0;
	struct rowSet *lpsRowSet = NULL;
	objectid_t externid;
	std::list<objectid_t> lstObjects;
	std::map<objectid_t, objectdetails_t> mapAllObjectDetails, mapExternObjectDetails;
	std::map<objectid_t, unsigned int> mapExternIdToRowId, mapExternIdToObjectId;
	UserPlugin *lpPlugin = NULL;
	std::string signature;
	auto cache = m_lpSession->GetSessionManager()->GetCacheManager();
	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if (er != erSuccess)
		goto exit;

	assert(lpRowList != NULL);
	lpsRowSet = s_alloc<struct rowSet>(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;
	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		goto exit; // success
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpRowList->size());
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpRowList->size());

	// Get Extern ID and Types for all items
	i = 0;
	for (const auto &row : *lpRowList) {
		er = GetExternalId(row.ulObjId, &externid);
		if (er != erSuccess) {
			++i;
			er = erSuccess; /* Skip entry, but don't complain */
			continue;
		}
		// See if the item data is cached
		if (cache->GetUserDetails(row.ulObjId, &mapAllObjectDetails[externid]) != erSuccess) {
			// Item needs to be retrieved from the plugin
			lstObjects.emplace_back(externid);
			// remove from all map, since the address reference added an empty entry in the map
			mapAllObjectDetails.erase(externid);
		}
		mapExternIdToRowId.emplace(externid, i);
		mapExternIdToObjectId.emplace(externid, row.ulObjId);
		++i;
	}

	// Do one request to the plugin for each type of object requested
	try {
		// Copy each item over
		for (const auto &eod : lpPlugin->getObjectDetails(lstObjects)) {
			mapAllObjectDetails.emplace(eod.first, eod.second);
			// Get the local object id for the item
			auto iterObjectId = mapExternIdToObjectId.find(eod.first);
			if (iterObjectId == mapExternIdToObjectId.cend())
				continue;
			// Add data to the cache
			cache->SetUserDetails(iterObjectId->second, eod.second);
		}
		/* We convert user and companyname to loginname later this function */
	} catch (const objectnotfound &) {
		er = KCERR_NOT_FOUND;
		goto exit;
	} catch (const notsupported &) {
		er = KCERR_NO_SUPPORT;
		goto exit;
	} catch (const std::exception &e) {
		ec_log_warn("K-1528: Unable to retrieve details map from external user source: %s", e.what());
		er = KCERR_PLUGIN_ERROR;
		goto exit;
	}

	// mapAllObjectDetails now contains the details per type of all objects that we need.
	// Loop through user data and fill in data in rowset
	for (auto &eod : mapAllObjectDetails) {
		objectid_t objectid = eod.first;
		objectdetails_t &objectdetails = eod.second; // reference, no need to copy

		// If the objectid is not found, the plugin returned too many "hits"
		auto iterRowId = mapExternIdToRowId.find(objectid);
		if (iterRowId == mapExternIdToRowId.cend())
			continue;
		// Get the local object id for the item
		auto iterObjectId = mapExternIdToObjectId.find(objectid);
		if (iterObjectId == mapExternIdToObjectId.cend())
			continue;
		// objectdetails can only be from an extern object here, no need to check for SYSTEM or EVERYONE
		er = UpdateUserDetailsToClient(&objectdetails);
		if (er != erSuccess)
			goto exit;
		ConvertObjectDetailsToProps(soap, iterObjectId->second, &objectdetails, lpPropTagArray, &lpsRowSet->__ptr[iterRowId->second]);
		// Ignore the error, missing rows will be filled in automatically later (assumes lpsRowSet is untouched on error!!)
	}

	// Loop through items, set local data and set other non-found data to NOT FOUND
	i = 0;
	for (const auto &row : *lpRowList) {
		// Fill in any missing data with KCERR_NOT_FOUND
		if (IsInternalObject(row.ulObjId)) {
			objectdetails_t details;
			er = GetLocalObjectDetails(row.ulObjId, &details);
			if (er != erSuccess)
				goto exit;
			er = ConvertObjectDetailsToProps(soap, row.ulObjId, &details, lpPropTagArray, &lpsRowSet->__ptr[i]);
			if(er != erSuccess)
				goto exit;
		} else if (lpsRowSet->__ptr[i].__ptr == NULL) {
			lpsRowSet->__ptr[i].__ptr = s_alloc<propVal>(soap, lpPropTagArray->__size);
			lpsRowSet->__ptr[i].__size = lpPropTagArray->__size;
			for (gsoap_size_t j = 0; j < lpPropTagArray->__size; ++j) {
				lpsRowSet->__ptr[i].__ptr[j].ulPropTag = CHANGE_PROP_TYPE(lpPropTagArray->__ptr[j], PT_ERROR);
				lpsRowSet->__ptr[i].__ptr[j].Value.ul = KCERR_NOT_FOUND;
				lpsRowSet->__ptr[i].__ptr[j].__union = SOAP_UNION_propValData_ul;
			}
		}
		++i;
	}

	// All done
	*lppRowSet = lpsRowSet;
exit:
	if (er != erSuccess && lpsRowSet != NULL) {
		s_free(soap, lpsRowSet->__ptr);
		s_free(soap, lpsRowSet);
	}
	return er;
}

ECRESULT ECUserManagement::QueryHierarchyRowData(struct soap *soap,
    const ECObjectTableList *lpRowList,
    const struct propTagArray *lpPropTagArray, struct rowSet **lppRowSet)
{
	ECRESULT er = erSuccess;
	unsigned int i = 0;

	assert(lpRowList != NULL);
	auto lpsRowSet = s_alloc_nothrow<struct rowSet>(soap);
	if (lpsRowSet == NULL)
		return KCERR_NOT_ENOUGH_MEMORY;
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;
	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		goto exit; // success
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpRowList->size());
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpRowList->size());

	for (const auto &row : *lpRowList) {
		/* Although it propbably doesn't make a lot of sense, we need to check for company containers here.
		 * this is because with convenient depth tables, the children of the Kopano Address Book will not
		 * only be the Global Address Book, but also the company containers. */
		if (row.ulObjId == KOPANO_UID_ADDRESS_BOOK ||
		    row.ulObjId == KOPANO_UID_GLOBAL_ADDRESS_BOOK ||
		    row.ulObjId == KOPANO_UID_GLOBAL_ADDRESS_LISTS) {
			er = ConvertABContainerToProps(soap, row.ulObjId, lpPropTagArray, &lpsRowSet->__ptr[i]);
			if (er != erSuccess)
				goto exit;
			++i;
			continue;
		}
		objectdetails_t details;
		objectid_t sExternId;
		er = GetObjectDetails(row.ulObjId, &details);
		if (er != erSuccess)
			goto exit;
		er = GetExternalId(row.ulObjId, &sExternId);
		if (er != erSuccess)
			goto exit;
		er = ConvertContainerObjectDetailsToProps(soap, row.ulObjId, &details, lpPropTagArray, &lpsRowSet->__ptr[i]);
		if (er != erSuccess)
			goto exit;
		++i;
	}

	// All done
	*lppRowSet = lpsRowSet;
exit:
	if (er != erSuccess) {
		s_free(soap, lpsRowSet->__ptr);
		s_free(soap, lpsRowSet);
	}
	return er;
}

ECRESULT ECUserManagement::GetUserAndCompanyFromLoginName(const std::string &strLoginName,
    std::string *username, std::string *companyname) const
{
	ECRESULT er = erSuccess;
	std::string format = m_lpConfig->GetSetting("loginname_format");
	bool bHosted = m_lpSession->GetSessionManager()->IsHostedSupported();
	size_t pos_u = format.find("%u"), pos_c = format.find("%c");

	if (!bHosted || pos_u == std::string::npos || pos_c == std::string::npos) {
		/* When hosted is enabled, return a warning. Otherwise,
		 * this call was successful. */
		if (bHosted)
			er = KCWARN_PARTIAL_COMPLETION;
		*username = strLoginName;
		companyname->clear();
		return er;
	}

	size_t pos_a = (pos_u < pos_c) ? pos_u : pos_c;
	size_t pos_b = (pos_u < pos_c) ? pos_c : pos_u;
	/*
	 * Read strLoginName to determine the fields, this check should
	 * keep in mind that there can be characters before, inbetween and
	 * after the different fields.
	 */
	auto start = format.substr(0, pos_a);
	auto middle = format.substr(pos_a + 2, pos_b - pos_a - 2);
	auto end = format.substr(pos_b + 2, std::string::npos);
	/* There must be some sort of separator between username and companyname. */
	if (middle.empty())
		return KCERR_INVALID_PARAMETER;

	size_t pos_s = !start.empty() ? strLoginName.find(start, 0) : 0;
	size_t pos_m = strLoginName.find(middle, pos_s + start.size());
	size_t pos_e = !end.empty() ? strLoginName.find(end, pos_m + middle.size()) : std::string::npos;

	if ((!start.empty() && pos_s == std::string::npos) ||
	    (!middle.empty() && pos_m == std::string::npos) ||
	    (!end.empty() && pos_e == std::string::npos)) {
		/* When hosted is enabled, return a warning. Otherwise,
		 * this call was successful. */
		if (strLoginName != KOPANO_ACCOUNT_SYSTEM &&
			strLoginName != KOPANO_ACCOUNT_EVERYONE)
				er = KCERR_INVALID_PARAMETER;
		*username = strLoginName;
		companyname->clear();
		return er;
	}

	if (pos_m == std::string::npos)
		pos_m = pos_b;
	if (pos_u < pos_c) {
		*username = strLoginName.substr(pos_a, pos_m - pos_a);
		*companyname = strLoginName.substr(pos_m + middle.size(), pos_e - pos_m - middle.size());
	} else {
		*username = strLoginName.substr(pos_m + middle.size(), pos_e - pos_m - middle.size());
		*companyname = strLoginName.substr(pos_a, pos_m - pos_a);
	}
	/* Neither username or companyname are allowed to be empty */
	if (username->empty() || companyname->empty())
		return KCERR_NO_ACCESS;
	return er;
}

ECRESULT ECUserManagement::ConvertLoginToUserAndCompany(objectdetails_t *lpDetails)
{
	bool bHosted = m_lpSession->GetSessionManager()->IsHostedSupported();
	std::string loginname, companyname;
	unsigned int ulCompanyId = 0;
	objectid_t sCompanyId;

	if ((OBJECTCLASS_TYPE(lpDetails->GetClass()) != OBJECTTYPE_MAILUSER &&
		 OBJECTCLASS_TYPE(lpDetails->GetClass()) != OBJECTTYPE_DISTLIST) ||
		lpDetails->GetClass() == NONACTIVE_CONTACT || lpDetails->GetPropString(OB_PROP_S_LOGIN).empty())
			return erSuccess;

	auto er = GetUserAndCompanyFromLoginName(lpDetails->GetPropString(OB_PROP_S_LOGIN), &loginname, &companyname);
	/* GetUserAndCompanyFromLoginName() uses KCWARN_PARTIAL_COMPLETION to indicate
	 * it could not fully convert the loginname. This means the user provided an
	 * invalid parameter and we should give the proper code back to the user */
	if (er == KCWARN_PARTIAL_COMPLETION)
		er = KCERR_INVALID_PARAMETER;
	if (er != erSuccess)
		return er;

	if (bHosted) {
		er = ResolveObject(CONTAINER_COMPANY, companyname, objectid_t(), &sCompanyId);
		if (er != erSuccess)
			return er;
		er = GetLocalId(sCompanyId, &ulCompanyId);
		if (er != erSuccess)
			return er;
		lpDetails->SetPropObject(OB_PROP_O_COMPANYID, sCompanyId);
		lpDetails->SetPropInt(OB_PROP_I_COMPANYID, ulCompanyId);
	}

	lpDetails->SetPropString(OB_PROP_S_LOGIN, loginname);
	/* Groups require fullname to be updated as well */
	if (OBJECTCLASS_TYPE(lpDetails->GetClass()) == OBJECTTYPE_DISTLIST)
		lpDetails->SetPropString(OB_PROP_S_FULLNAME, loginname);
	return erSuccess;
}

ECRESULT ECUserManagement::ConvertUserAndCompanyToLogin(objectdetails_t *lpDetails)
{
	bool bHosted = m_lpSession->GetSessionManager()->IsHostedSupported();
	unsigned int ulCompanyId;
	objectdetails_t sCompanyDetails;
	/*
	 * We don't have to do anything when hosted is disabled,
	 * when we perform this operation on SYSTEM or EVERYONE (since they don't belong to a company),
	 * or when the user objecttype does not need any conversions.
	 */
	auto login = lpDetails->GetPropString(OB_PROP_S_LOGIN);
	if (!bHosted ||	login == KOPANO_ACCOUNT_SYSTEM || login == KOPANO_ACCOUNT_EVERYONE || lpDetails->GetClass() == CONTAINER_COMPANY)
		return erSuccess;
	/*
	 * Since we already need the company name here, we convert the
	 * OB_PROP_O_COMPANYID to the internal ID too
	 */
	objectid_t sCompany = lpDetails->GetPropObject(OB_PROP_O_COMPANYID);
	auto er = GetLocalId(sCompany, &ulCompanyId);
	if (er != erSuccess) {
		ec_log_crit("K-1529: Unable to find company id for object \"%s\"", lpDetails->GetPropString(OB_PROP_S_FULLNAME).c_str());
		return er;
	}

	// update company local id and company name
	lpDetails->SetPropInt(OB_PROP_I_COMPANYID, ulCompanyId);
	// skip login conversion for non-login objects
	if ((OBJECTCLASS_TYPE(lpDetails->GetClass()) != OBJECTTYPE_MAILUSER && OBJECTCLASS_TYPE(lpDetails->GetClass()) != OBJECTTYPE_DISTLIST) ||
		(lpDetails->GetClass() == NONACTIVE_CONTACT))
		return er;
	// find company name for object
	er = GetObjectDetails(ulCompanyId, &sCompanyDetails);
	if (er != erSuccess)
		return er;

	std::string company = sCompanyDetails.GetPropString(OB_PROP_S_FULLNAME);
	/*
	 * This really shouldn't happen, the loginname must contain *something* under any circumstance,
	 * company must contain *something* when hosted is enabled, and we already confirmed that this
	 * is the case.
	 */
	if (login.empty() || company.empty())
		return KCERR_UNABLE_TO_COMPLETE;
	std::string format = m_lpConfig->GetSetting("loginname_format");
	auto pos = format.find("%u");
	if (pos != std::string::npos)
		format.replace(pos, 2, login);
	pos = format.find("%c");
	if (pos != std::string::npos)
		format.replace(pos, 2, company);

	lpDetails->SetPropString(OB_PROP_S_LOGIN, format);
	/* Groups require fullname to be updated as well */
	if (OBJECTCLASS_TYPE(lpDetails->GetClass()) == OBJECTTYPE_DISTLIST)
		lpDetails->SetPropString(OB_PROP_S_FULLNAME, format);

	return erSuccess;
}

ECRESULT ECUserManagement::ConvertExternIDsToLocalIDs(objectdetails_t *lpDetails)
{
	ECRESULT er;
	std::list<objectid_t> lstExternIDs;
	std::map<objectid_t, unsigned int> mapLocalIDs;
	objectid_t sExternID;
	unsigned int ulLocalID = 0;

	// details == info, list contains 1) active_users or 2) groups
	switch (lpDetails->GetClass()) {
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
		lstExternIDs = lpDetails->GetPropListObject(OB_PROP_LO_SENDAS);
		if (lstExternIDs.empty())
			break;
		er = m_lpSession->GetSessionManager()->GetCacheManager()->GetUserObjects(lstExternIDs, &mapLocalIDs);
		if (er != erSuccess)
			return er;

		for (const auto &loc_id : mapLocalIDs) {
			if (loc_id.first.objclass != ACTIVE_USER &&
			    loc_id.first.objclass != NONACTIVE_USER &&
			    OBJECTCLASS_TYPE(loc_id.first.objclass) != OBJECTTYPE_DISTLIST)
				continue;
			lpDetails->AddPropInt(OB_PROP_LI_SENDAS, loc_id.second);
		}
		break;
	case CONTAINER_COMPANY:
		sExternID = lpDetails->GetPropObject(OB_PROP_O_SYSADMIN);

		// avoid cache and SQL query when using internal sysadmin object
		if (sExternID.id == "SYSTEM")
			ulLocalID = KOPANO_UID_SYSTEM;
		else if (GetLocalId(sExternID, &ulLocalID) != erSuccess)
			ulLocalID = KOPANO_UID_SYSTEM;

		lpDetails->SetPropInt(OB_PROP_I_SYSADMIN, ulLocalID);
		break;
	default:
		// Nothing to do
		break;
	}
	return erSuccess;
}

ECRESULT ECUserManagement::ConvertLocalIDsToExternIDs(objectdetails_t *lpDetails) const
{
	ECRESULT er;
	objectid_t sExternID;
	unsigned int ulLocalID = 0;

	switch (lpDetails->GetClass()) {
	case CONTAINER_COMPANY:
		ulLocalID = lpDetails->GetPropInt(OB_PROP_I_SYSADMIN);
		if (!ulLocalID || IsInternalObject(ulLocalID))
			break;
		er = GetExternalId(ulLocalID, &sExternID);
		if (er != erSuccess)
			return er;
		lpDetails->SetPropObject(OB_PROP_O_SYSADMIN, sExternID);
		break;
	default:
		// Nothing to do
		break;
	}
	return erSuccess;
}

/**
 * Return a set of all available kopano features.
 *
 * @return unique set of feature names
 */
static inline std::set<std::string> getFeatures()
{
	return {kopano_features, kopano_features + ARRAY_SIZE(kopano_features)};
}

/**
 * Add default server settings of enabled/disabled features to the
 * user explicit feature lists.
 *
 * @param[in,out] lpDetails plugin feature list to edit
 *
 * @return erSuccess
 */
ECRESULT ECUserManagement::ComplementDefaultFeatures(objectdetails_t *lpDetails) const
{
	if (OBJECTCLASS_TYPE(lpDetails->GetClass()) != OBJECTTYPE_MAILUSER) {
		// clear settings for anything but users
		std::list<std::string> e;
		lpDetails->SetPropListString((property_key_t)PR_EC_ENABLED_FEATURES_A, e);
		lpDetails->SetPropListString((property_key_t)PR_EC_DISABLED_FEATURES_A, e);
		return erSuccess;
	}

	auto defaultEnabled = getFeatures();
	auto userEnabled = lpDetails->GetPropListString((property_key_t)PR_EC_ENABLED_FEATURES_A);
	auto userDisabled = lpDetails->GetPropListString((property_key_t)PR_EC_DISABLED_FEATURES_A);
	std::vector<std::string> ddv = tokenize(m_lpConfig->GetSetting("disabled_features"), "\t ");
	std::set<std::string> defaultDisabled(std::make_move_iterator(ddv.begin()), std::make_move_iterator(ddv.end()));

	for (auto i = defaultDisabled.cbegin(); i != defaultDisabled.cend(); ) {
		if (i->empty()) {
			// nasty side effect of boost split, when input consists only of a split predicate.
			i = defaultDisabled.erase(i);
			continue;
		}
		defaultEnabled.erase(*i);
		++i;
	}

	// explicit enable remove from default disable, and add in defaultEnabled
	for (const auto &s : userEnabled) {
		defaultDisabled.erase(s);
		defaultEnabled.emplace(s);
	}
	// explicit disable remove from default enable, and add in defaultDisabled
	for (const auto &s : userDisabled) {
		defaultEnabled.erase(s);
		defaultDisabled.emplace(s);
	}

	userEnabled.assign(std::make_move_iterator(defaultEnabled.begin()), std::make_move_iterator(defaultEnabled.end()));
	userDisabled.assign(std::make_move_iterator(defaultDisabled.begin()), std::make_move_iterator(defaultDisabled.end()));
	// save lists back to user details
	lpDetails->SetPropListString((property_key_t)PR_EC_ENABLED_FEATURES_A, userEnabled);
	lpDetails->SetPropListString((property_key_t)PR_EC_DISABLED_FEATURES_A, userDisabled);
	return erSuccess;
}

/**
 * Make the enabled and disabled feature list of user details an explicit list.
 * This way, changing the default in the server will have a direct effect.
 *
 * @param[in,out] lpDetails incoming user details to fix
 *
 * @return erSuccess
 */
ECRESULT ECUserManagement::RemoveDefaultFeatures(objectdetails_t *lpDetails) const
{
	if (lpDetails->GetClass() != ACTIVE_USER)
		return erSuccess;
	auto defaultEnabled = getFeatures();
	auto userEnabled = lpDetails->GetPropListString((property_key_t)PR_EC_ENABLED_FEATURES_A);
	auto userDisabled = lpDetails->GetPropListString((property_key_t)PR_EC_DISABLED_FEATURES_A);
	std::vector<std::string> ddv = tokenize(m_lpConfig->GetSetting("disabled_features"), "\t ");
	std::set<std::string> defaultDisabled(std::make_move_iterator(ddv.begin()), std::make_move_iterator(ddv.end()));

	// remove all default disabled from enabled and user explicit list
	for (const auto &s : defaultDisabled) {
		defaultEnabled.erase(s);
		userDisabled.remove(s);
	}
	// remove all default enabled features from explicit list
	userEnabled.remove_if([&](const std::string &x) {
		return defaultEnabled.find(x) != defaultEnabled.end();
	});
	// save lists back to user details
	lpDetails->SetPropListString((property_key_t)PR_EC_ENABLED_FEATURES_A, userEnabled);
	lpDetails->SetPropListString((property_key_t)PR_EC_DISABLED_FEATURES_A, userDisabled);
	return erSuccess;
}

/**
 * Convert some properties in the details object from plugin info to
 * client info.
 *
 * @todo this function is called *very* often, and that should be
 * reduced.
 *
 * @param[in,out] lpDetails details to update
 *
 * @return Kopano error code
 */
ECRESULT ECUserManagement::UpdateUserDetailsToClient(objectdetails_t *lpDetails)
{
	auto er = ConvertUserAndCompanyToLogin(lpDetails);
	if (er != erSuccess)
		return er;
	er = ConvertExternIDsToLocalIDs(lpDetails);
	if (er != erSuccess)
		return er;
	return ComplementDefaultFeatures(lpDetails);
}

/**
 * Update client details to db-plugin info
 *
 * @param[in,out] lpDetails user details to update
 *
 * @return Kopano error code
 */
ECRESULT ECUserManagement::UpdateUserDetailsFromClient(objectdetails_t *lpDetails)
{
	auto er = ConvertLoginToUserAndCompany(lpDetails);
	if (er != erSuccess)
		return er;
	er = ConvertLocalIDsToExternIDs(lpDetails);
	if (er != erSuccess)
		return er;
	return RemoveDefaultFeatures(lpDetails);
}

// ******************************************************************************************************
//
// Create/Delete routines
//
// ******************************************************************************************************

// Create a local user corresponding to the given userid on the external database
ECRESULT ECUserManagement::CreateLocalObject(const objectsignature_t &signature, unsigned int *lpulObjectId) {
	ECDatabase *lpDatabase = NULL;
	objectdetails_t details;
	unsigned int ulId;
	ABEID eid(MAPI_ABCONT, MUIDECSAB, 1);
	SOURCEKEY sSourceKey;
	UserPlugin *lpPlugin = NULL;
	std::string strUserServer, strThisServer = m_lpConfig->GetSetting("server_name");
	bool bDistributed = m_lpSession->GetSessionManager()->IsDistributedSupported();

	auto er = m_lpSession->GetDatabase(&lpDatabase);
	if(er != erSuccess)
		return er;
	er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	ec_log_info("Auto-creating %s from external source", ObjectClassToName(signature.id.objclass));

	try {
		details = lpPlugin->getObjectDetails(signature.id);
		/*
		 * The property OB_PROP_S_LOGIN is mandatory, when that property is an empty string
		 * somebody (aka: system administrator) has messed up its LDAP tree or messed around
		 * with the Database.
		 */
		if (signature.id.objclass != NONACTIVE_CONTACT && details.GetPropString(OB_PROP_S_LOGIN).empty()) {
			ec_log_warn("K-1534: Unable to create object in local database: %s has no name", ObjectClassToName(signature.id.objclass));
			return KCERR_UNKNOWN_OBJECT;
		}
		// details is from externid, no need to check for SYSTEM or EVERYONE
		er = UpdateUserDetailsToClient(&details);
		if (er != erSuccess)
			return er;
	} catch (const objectnotfound &) {
		return KCERR_NOT_FOUND;
	} catch (const notsupported &) {
		return KCERR_NO_SUPPORT;
	} catch (const std::exception &e) {
		ec_log_warn("K-1535: Unable to get object data while adding new %s: %s", ObjectClassToName(signature.id.objclass), e.what());
		return KCERR_PLUGIN_ERROR;
	}

	if (parseBool(m_lpConfig->GetSetting("user_safe_mode"))) {
		ec_log_crit("user_safe_mode: Would create new %s with name \"%s\"", ObjectClassToName(signature.id.objclass), details.GetPropString(OB_PROP_S_FULLNAME).c_str());
		return er;
	}
	/*
	 * 1) we create the object in the database, for users/groups we set the company to 0
	 * 2) Resolve companyid for users/groups to a companyid
	 * 3) Update object in the database to correctly set the company column
	 *
	 * Now why do we use this approach:
	 * Suppose we have user A who is member of Company B and is the sysadmin for B.
	 * The following will happen when we insert all information in a single shot:
	 * 1) GetLocalObjectIdOrCreate(A) is called
	 * 2) Database does not contain A, so CreateLocalObject(A) is called
	 * 3) CreateLocalObject(A) calls ResolveObjectAndSync(B) for COMPANYID
	 *   3.1) B is resolved, GetLocalObjectIdOrCreate(B) is called
	 *   3.2) Database does not contain B, so CreateLocalObject(B) is called
	 *   3.3) B is inserted into the database, createcompany script is called
	 *   3.4) createcompany script calls getCompany(B)
	 *   3.5) Server obtains details for B, finds A as sysadmin for B
	 *   3.6) Server calls GetLocalObjectIdOrCreate(A)
	 *   3.7) Database does not contain A, so CreateLocalObject(A) is called
	 *   3.8) CreateLocalObject(A) inserts A into the database
	 *   3.9) createcompany script does its work and completes
	 * 4) CreateLocalObject(A) inserts A into the database
	 * 5) BOOM! Primary key violation since A is already present in the database
	 */
	std::string strQuery =
		"INSERT INTO users (externid, objectclass, signature) "
		"VALUES("
			"'" + lpDatabase->Escape(signature.id.id) + "', " +
			stringify(signature.id.objclass) + ", " +
			"'" + lpDatabase->Escape(signature.signature) + "')";
	er = lpDatabase->DoInsert(strQuery, &ulId);
	if(er != erSuccess)
		return er;

	unsigned int ulCompanyId = 0;
	if (signature.id.objclass != CONTAINER_COMPANY)
		ulCompanyId = details.GetPropInt(OB_PROP_I_COMPANYID);
	if (ulCompanyId) {
		strQuery =
			"UPDATE users "
			"SET company = " + stringify(ulCompanyId) + " "
			"WHERE id = " + stringify(ulId);
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			return er;
	}

	switch(signature.id.objclass) {
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
		strUserServer = details.GetPropString(OB_PROP_S_SERVERNAME);
		if (!bDistributed || strcasecmp(strUserServer.c_str(), strThisServer.c_str()) == 0)
			execute_script(m_lpConfig->GetSetting("createuser_script"),
						   "KOPANO_USER", details.GetPropString(OB_PROP_S_LOGIN).c_str(),
						   NULL);
		break;
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
		execute_script(m_lpConfig->GetSetting("creategroup_script"),
					   "KOPANO_GROUP", details.GetPropString(OB_PROP_S_LOGIN).c_str(),
					   NULL);
		break;
	case CONTAINER_COMPANY:
		strUserServer = details.GetPropString(OB_PROP_S_SERVERNAME);
		if (!bDistributed || strcasecmp(strUserServer.c_str(), strThisServer.c_str()) == 0)
			execute_script(m_lpConfig->GetSetting("createcompany_script"),
						   "KOPANO_COMPANY", details.GetPropString(OB_PROP_S_FULLNAME).c_str(),
						   NULL);
		break;
	default:
		break;
	}

	// Log the change to ICS
	er = GetABSourceKeyV1(ulId, &sSourceKey);
	if (er != erSuccess)
		return er;
	AddABChange(m_lpSession, ICS_AB_NEW, sSourceKey, SOURCEKEY(CbABEID(&eid), (char *)&eid));
	*lpulObjectId = ulId;
	return erSuccess;
}

// Creates a local object under a specific object ID
ECRESULT ECUserManagement::CreateLocalObjectSimple(const objectsignature_t &signature, unsigned int ulPreferredId) {
	ECDatabase *lpDatabase = NULL;
	objectdetails_t details;
	std::string strQuery, strUserId;
	unsigned int ulCompanyId = 0;
	UserPlugin *lpPlugin = NULL;
	DB_RESULT lpResult;
	bool bLocked = false;

	auto er = m_lpSession->GetDatabase(&lpDatabase);
	if(er != erSuccess)
		goto exit;
	// No user count checking or script starting in this function; it is only used in for addressbook synchronization in the offline server
	er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		goto exit;

	try {
		details = lpPlugin->getObjectDetails(signature.id);
		/* No need to convert the user and company name to login, since we are not using
		 * the loginname in this function. */
	} catch (const objectnotfound &) {
		er = KCERR_NOT_FOUND;
		goto exit;
	} catch (const notsupported &) {
		er = KCERR_NO_SUPPORT;
		goto exit;
	} catch (const std::exception &e) {
		ec_log_warn("K-1536: Unable to get details while adding new %s: %s", ObjectClassToName(signature.id.objclass), e.what());
		er = KCERR_PLUGIN_ERROR;
		goto exit;
	}

	if (signature.id.objclass != CONTAINER_COMPANY)
		ulCompanyId = details.GetPropInt(OB_PROP_I_COMPANYID);

	strQuery = "LOCK TABLES users WRITE";
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		goto exit;
	bLocked = true;

	// Check if we can use the preferred id.
	strQuery = "SELECT 0 FROM users WHERE id=" + stringify(ulPreferredId);
	er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		goto exit;
	if (lpResult.get_num_rows() == 1)
		strUserId = "NULL";						// Let mysql insert the userid with auto increment
	else
		strUserId = stringify(ulPreferredId);	// force userid on preferred id

	strQuery =
		"INSERT INTO users (id, externid, objectclass, company, signature) "
		"VALUES ("
			+ strUserId + ", " +
			"'" + lpDatabase->Escape(signature.id.id) + "', " +
			stringify(signature.id.objclass) + ", " +
			stringify(ulCompanyId) + ", " +
			"'" + lpDatabase->Escape(signature.signature) + "')";
	er = lpDatabase->DoInsert(strQuery);
exit:
	if (bLocked)
		lpDatabase->DoInsert("UNLOCK TABLES");
	return er;
}

/**
 * Modify the objectclass of a user in the database, or delete the user if the modification is not allowed
 *
 * This function modifies the objectclass of the object in the database to match the external object type. This
 * allows switching from an active to a non-active user for example. However, some switches are not allowed like
 * changing from a user to a group; In this case the object is deleted and will be re-created as the other type
 * later, since the object is then missing from the locale database.
 *
 * Conversion are allowed if the OBJECTCLASS_TYPE of the object remains unchanged. Exception is converting to/from
 * a CONTACT type since we want to do store initialization or deletion in that case (contact is the only USER type
 * that doesn't have a store)
 *
 * @param[in] sExternId Extern ID of the object to be updated (the externid contains the object type)
 * @param[out] lpulObjectId Object ID of object if not deleted
 * @return ECRESULT KCERR_NOT_FOUND if the user is not found OR must be deleted due to type change
 */
ECRESULT ECUserManagement::UpdateObjectclassOrDelete(const objectid_t &sExternId, unsigned int *lpulObjectId)
{
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpResult;
	SOURCEKEY sSourceKey;
	ABEID eid(MAPI_ABCONT, MUIDECSAB, 1);

	auto er = m_lpSession->GetDatabase(&lpDatabase);
	if(er != erSuccess)
		return er;
	std::string strQuery = "SELECT id, objectclass FROM users WHERE externid='" + lpDatabase->Escape(sExternId.id) + "' AND " +
		OBJECTCLASS_COMPARE_SQL("objectclass", OBJECTCLASS_CLASSTYPE(sExternId.objclass));
	er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return er;
	auto lpRow = lpResult.fetch_row();
	if (lpRow == nullptr)
		return KCERR_NOT_FOUND;
	auto ulObjectId = atoui(lpRow[0]);
	objectclass_t objClass = (objectclass_t)atoui(lpRow[1]);

	/* Exception: converting from contact to other user type or other user type to contact is NOT allowed -> this is to force store create/remove */
	bool illegal = objClass == NONACTIVE_CONTACT && sExternId.objclass != NONACTIVE_CONTACT;
	illegal     |= objClass != NONACTIVE_CONTACT && sExternId.objclass == NONACTIVE_CONTACT;

	if (OBJECTCLASS_TYPE(objClass) != OBJECTCLASS_TYPE(sExternId.objclass) || illegal) {
		// type of object changed, so we must fully delete the object first.
		er = DeleteLocalObject(ulObjectId, objClass);
		if (er != erSuccess)
			return er;
		return KCERR_NOT_FOUND;
	}
	if (parseBool(m_lpConfig->GetSetting("user_safe_mode"))) {
		ec_log_crit("user_safe_mode: Would update %d from %s to %s",
			ulObjectId, ObjectClassToName(objClass),
			ObjectClassToName(sExternId.objclass));
		return er;
	}
	/*
	 * Probable situation: change ACTIVE_USER to NONACTIVE_USER (or
	 * room/equipment), or change group to security group.
	 */
	strQuery = "UPDATE users SET objectclass = " + stringify(sExternId.objclass) + " WHERE id = " + stringify(ulObjectId);
	er = lpDatabase->DoUpdate(strQuery);
	if (er != erSuccess)
		return er;
	/* Log the change to ICS */
	er = GetABSourceKeyV1(ulObjectId, &sSourceKey);
	if (er != erSuccess)
		return er;
	AddABChange(m_lpSession, ICS_AB_CHANGE, sSourceKey, SOURCEKEY(CbABEID(&eid), reinterpret_cast<char *>(&eid)));
	if (lpulObjectId != nullptr)
		*lpulObjectId = ulObjectId;
	return erSuccess;
}

// Check if an object has moved to a new company, or if it was created as new
ECRESULT ECUserManagement::MoveOrCreateLocalObject(const objectsignature_t &signature, unsigned int *lpulObjectId, bool *lpbMoved)
{
	ECRESULT er;
	objectdetails_t details;
	unsigned int ulObjectId = 0, ulNewCompanyId = 0;
	bool bMoved = false;
	/*
	 * This function is called when an object with an external id has appeared
	 * there are various reasons why this might happen:
	 * 1) Object was created, and we should create the local item.
	 * 2) Object was moved to different company, we should move the local item
	 * 3) Object hasn't changed, but multicompany support was toggled, we should move the local item
	 *
	 * The obvious benefit from moving an user will be that it preserves the store, when
	 * deleting and recreating a user when it has moved the same will happen for the store
	 * belonging to that user. When we perform a move operation we will only update some
	 * columns in the database and the entire store is moved along with the user, preserving
	 * all messages in that store.
	 *
	 * How to determine if an object was created or moved:
	 * We arrived here when a search for an object within its currently known company succeeded,
	 * and we found a (until then unknown) objectid. What we need now is to perform a search
	 * in the database without announcing the company.
	 * If that does produce a result, then the user has moved, and we need to determine
	 * the new company.
	 */
	/* We don't support moving entire companies, create it. */
	if (signature.id.objclass == CONTAINER_COMPANY) {
		er = CreateLocalObject(signature, &ulObjectId);
		if (er != erSuccess)
			return er;
		goto done;
	}

	/* If object doesn't have a local id, we should create it. */
	er = GetLocalId(signature.id, &ulObjectId);
	if (er != erSuccess) {
		// special case: if the objectclass of an object changed, we need to update or delete the previous version!
		// function returns not found if object is deleted or really not found (same thing)
		er = UpdateObjectclassOrDelete(signature.id, &ulObjectId);
		if (er == KCERR_NOT_FOUND) {
			er = CreateLocalObject(signature, &ulObjectId);
			if (er != erSuccess)
				return er;
		} else if (er == erSuccess)
			bMoved = true;
		goto done;
	}

	/* Delete cache, to force call to plugin */
	er = m_lpSession->GetSessionManager()->GetCacheManager()->UpdateUser(ulObjectId);
	if(er != erSuccess)
		return er;
	/* Result new object details */
	er = GetObjectDetails(ulObjectId, &details);
	if (er != erSuccess)
		return er;

	ulNewCompanyId = details.GetPropInt(OB_PROP_I_COMPANYID);
	er = MoveLocalObject(ulObjectId, signature.id.objclass, ulNewCompanyId, details.GetPropString(OB_PROP_S_LOGIN));
	if (er != erSuccess)
		return er;
	bMoved = true;
done:
	if (lpulObjectId)
		*lpulObjectId = ulObjectId;
	if (lpbMoved)
		*lpbMoved = bMoved;
	return er;
}

// Check if an object has moved to a new company, or if it was deleted completely
ECRESULT ECUserManagement::MoveOrDeleteLocalObject(unsigned int ulObjectId, objectclass_t objclass)
{
	objectdetails_t details;
	unsigned int ulOldCompanyId = 0;

	if (IsInternalObject(ulObjectId))
		return KCERR_NO_ACCESS;
	/*
	 * This function is called when an object with an external id has disappeared,
	 * there are various reasons why this might happen:
	 * 1) Object was deleted, and we should delete the local item.
	 * 2) Object was moved to different company, we should move the local item
	 * 3) Object hasn't changed, but multicompany support was toggled, we should move the local item
	 *
	 * The obvious benefit from moving an user will be that it preserves the store, when
	 * deleting and recreating a user when it has moved the same will happen for the store
	 * belonging to that user. When we perform a move operation we will only update some
	 * columns in the database and the entire store is moved along with the user, preserving
	 * all messages in that store.
	 *
	 * How to determine if an object was deleted or moved:
	 * We arrived here when a search for an object within its currently known company failed,
	 * what we need now is to perform a search in the plugin without announcing the company.
	 * If that does produce a result, then the user has moved, and we need to determine
	 * the new company. Fortunately, the best call to determine if a user still exists
	 * is GetObjectDetailsAndSync() which automatically gives us the new company.
	 */
	/* We don't support moving entire companies, delete it. */
	if (objclass == CONTAINER_COMPANY)
		return DeleteLocalObject(ulObjectId, objclass);
	/* Request old company */
	auto er = GetExternalId(ulObjectId, NULL, &ulOldCompanyId);
	if (er != erSuccess)
		return er;
	/* Delete cache, to force call to plugin */
	er = m_lpSession->GetSessionManager()->GetCacheManager()->UpdateUser(ulObjectId);
	if(er != erSuccess)
		return er;
	/* Result new object details */
	er = GetObjectDetails(ulObjectId, &details);
	if (er == KCERR_NOT_FOUND)
		/* Object was indeed deleted, GetObjectDetails() has called DeleteLocalObject() */
		return erSuccess;
	else if (er != erSuccess)
		return er;

	auto ulNewCompanyId = details.GetPropInt(OB_PROP_I_COMPANYID);
	/*
	 * We got the object details, if the company is different,
	 * the the object was moved. Otherwise there is a bug, since
	 * the object was not deleted, but was not moved either...
	 */
	if (ulOldCompanyId != ulNewCompanyId) {
		er = MoveLocalObject(ulObjectId, objclass, ulNewCompanyId, details.GetPropString(OB_PROP_S_LOGIN));
		if (er != erSuccess)
			return er;
	} else
		ec_log_err("K-1537: Unable to move object %s \"%s\" (id=%d)", ObjectClassToName(objclass), details.GetPropString(OB_PROP_S_LOGIN).c_str(), ulObjectId);

	return erSuccess;
}

// Move a local user with the specified id to a new company
ECRESULT ECUserManagement::MoveLocalObject(unsigned int ulObjectId,
    objectclass_t objclass, unsigned int ulCompanyId,
    const std::string &strNewUserName)
{
	ECRESULT er = erSuccess;
	ECDatabase *lpDatabase = NULL;
	std::string strQuery;
	ABEID eid(MAPI_ABCONT, MUIDECSAB, 1);
	SOURCEKEY sSourceKey;

	if (IsInternalObject(ulObjectId))
		return KCERR_NO_ACCESS;
	if (objclass == CONTAINER_COMPANY)
		return KCERR_NO_ACCESS;
	if (parseBool(m_lpConfig->GetSetting("user_safe_mode"))) {
		ec_log_crit("user_safe_mode: Would move %s %d to company %d", ObjectClassToName(objclass), ulObjectId, ulCompanyId);
		return erSuccess;
	}

	ec_log_info("Auto-moving %s to different company from external source", ObjectClassToName(objclass));
	er = m_lpSession->GetDatabase(&lpDatabase);
	if(er != erSuccess)
		return er;
	auto dtx = lpDatabase->Begin(er);
	if(er != erSuccess)
		return er;
	/*
	 * Moving a user to a different company consists of the following tasks:
	 * 1) Change 'company' column in 'users' table
	 * 2) Change 'company' column in 'stores' table
	 * 3) Change 'user_name' column in 'stores table
	 */
	strQuery =
		"UPDATE users "
		"SET company=" + stringify(ulCompanyId) + " "
		"WHERE id=" + stringify(ulObjectId);
	er = lpDatabase->DoUpdate(strQuery);
	if(er != erSuccess)
		return er;
	strQuery =
		"UPDATE stores "
		"SET company=" + stringify(ulCompanyId) + ", "
			"user_name='" + lpDatabase->Escape(strNewUserName) + "' "
		"WHERE user_id=" + stringify(ulObjectId);
	er = lpDatabase->DoUpdate(strQuery);
	if(er != erSuccess)
		return er;
	/* Log the change to ICS */
	er = GetABSourceKeyV1(ulObjectId, &sSourceKey);
	if (er != erSuccess)
		return er;
	er = AddABChange(m_lpSession, ICS_AB_CHANGE, sSourceKey, SOURCEKEY(CbABEID(&eid), (char *)&eid));
	if(er != erSuccess)
		return er;
	er = dtx.commit();
	if(er != erSuccess)
		return er;
	return m_lpSession->GetSessionManager()->GetCacheManager()->UpdateUser(ulObjectId);
}

// Delete a local user with the specified id
ECRESULT ECUserManagement::DeleteLocalObject(unsigned int ulObjectId, objectclass_t objclass) {
	ECRESULT er = erSuccess;
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpResult;
	DB_ROW lpRow = NULL;
	unsigned int ulDeletedRows = 0;
	std::string strQuery;
	ABEID eid(MAPI_ABCONT, MUIDECSAB, 1);
	SOURCEKEY sSourceKey;
	auto cache = m_lpSession->GetSessionManager()->GetCacheManager();
	auto cleanup = make_scope_success([&]() {
		if (er != 0)
			ec_log_info("Auto-deleting %s %d done: %s (%x)", ObjectClassToName(objclass), ulObjectId, GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
		else
			ec_log_info("Auto-deleting %s %d done.", ObjectClassToName(objclass), ulObjectId);
	});
	if (IsInternalObject(ulObjectId))
		return er = KCERR_NO_ACCESS;
	if (parseBool(m_lpConfig->GetSetting("user_safe_mode"))) {
		ec_log_crit("user_safe_mode: Would delete %s %d", ObjectClassToName(objclass), ulObjectId);
		if (objclass == CONTAINER_COMPANY)
			ec_log_crit("user_safe_mode: Would delete all objects from company %d", ulObjectId);
		return er = erSuccess;
	}

	er = m_lpSession->GetDatabase(&lpDatabase);
	if(er != erSuccess)
		return er;
	ec_log_info("Auto-deleting %s %d from external source", ObjectClassToName(objclass), ulObjectId);

	if (objclass == CONTAINER_COMPANY) {
		/* We are removing a company, delete all company members as well since
		 * those members no longer exist either and we won't get a delete event
		 * for those users since we already get a KCERR_NOT_FOUND for the
		 * company... */
		strQuery =
			"SELECT id, objectclass FROM users "
			"WHERE company = " + stringify(ulObjectId);
		er = lpDatabase->DoSelect(strQuery, &lpResult);
		if (er != erSuccess)
			return er;
		ec_log_info("Start auto-deleting %s members", ObjectClassToName(objclass));

		while (1) {
			lpRow = lpResult.fetch_row();
			if (lpRow == NULL || lpRow[0] == NULL || lpRow[1] == NULL)
				break;

			/* Perhaps the user was moved to a different company */
			er = MoveOrDeleteLocalObject(atoui(lpRow[0]), (objectclass_t)atoui(lpRow[1]));
			if (er != erSuccess)
				return er;
		}
		lpRow = NULL;
		ec_log_info("Done auto-deleting %s members", ObjectClassToName(objclass));
	}

	auto dtx = lpDatabase->Begin(er);
	if(er != erSuccess)
		return er;
	// Request source key before deleting the entry
	er = GetABSourceKeyV1(ulObjectId, &sSourceKey);
	if (er != erSuccess)
		return er;
	strQuery =
		"DELETE FROM users "
		"WHERE id=" + stringify(ulObjectId) + " "
		"AND " + OBJECTCLASS_COMPARE_SQL("objectclass", objclass);
	er = lpDatabase->DoDelete(strQuery, &ulDeletedRows);
	if(er != erSuccess)
		return er;
	/* Delete the user ACLs */
	strQuery =
		"DELETE FROM acl "
		"WHERE id=" + stringify(ulObjectId);
	er = lpDatabase->DoDelete(strQuery);
	if(er != erSuccess)
		return er;

	// Object didn't exist locally, so no delete has occurred
	if (ulDeletedRows == 0) {
		er = dtx.commit();
		if (er != erSuccess)
			return er;
		return cache->UpdateUser(ulObjectId);
		// Done, no ICS change, no userscript action required
	}

	// Log the change to ICS
	er = AddABChange(m_lpSession, ICS_AB_DELETE, sSourceKey, SOURCEKEY(CbABEID(&eid), (char *)&eid));
	if(er != erSuccess)
		return er;
	er = dtx.commit();
	if(er != erSuccess)
		return er;

	// Purge the usercache because we also need to remove sendas relations
	er = cache->PurgeCache(PURGE_CACHE_USEROBJECT | PURGE_CACHE_EXTERNID | PURGE_CACHE_USERDETAILS);
	if(er != erSuccess)
		return er;

	switch (objclass) {
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
		strQuery = "SELECT HEX(guid) FROM stores WHERE user_id=" + stringify(ulObjectId);
		er = lpDatabase->DoSelect(strQuery, &lpResult);
		if(er != erSuccess)
			return er;
		lpRow = lpResult.fetch_row();
		if(lpRow == NULL) {
			ec_log_info("User script not executed. No store exists.");
			return erSuccess;
		} else if (lpRow[0] == NULL) {
			ec_log_err("ECUserManagement::DeleteLocalObject(): column null");
			return KCERR_DATABASE_ERROR; /* setting er, has effect for ~kd_trans */
		}
		execute_script(m_lpConfig->GetSetting("deleteuser_script"),
					   "KOPANO_STOREGUID", lpRow[0],
					   NULL);
		break;
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
		execute_script(m_lpConfig->GetSetting("deletegroup_script"),
					   "KOPANO_GROUPID", stringify(ulObjectId).c_str(),
					   NULL);
		break;
	case CONTAINER_COMPANY:
		execute_script(m_lpConfig->GetSetting("deletecompany_script"),
					   "KOPANO_COMPANYID", stringify(ulObjectId).c_str(),
					   NULL);
		break;
	default:
		break;
	}
	return erSuccess;
}

bool ECUserManagement::IsInternalObject(unsigned int ulUserId) const
{
	return ulUserId == KOPANO_UID_SYSTEM || ulUserId == KOPANO_UID_EVERYONE;
}

/**
 * Returns property values for "anonymous" properties, which are tags
 * from plugin::getExtraAddressbookProperties().
 *
 * @param[in] soap soap struct for memory allocation
 * @param[in] lpDetails details object of an addressbook object to get the values from
 * @param[in] ulPropTag property to fetch from the details
 * @param[out] lpPropVal soap propVal to return to the caller
 *
 * @return internal error code
 * @retval erSuccess value is set
 * @retval KCERR_UNKNOWN value for proptag is not found
 */
ECRESULT ECUserManagement::ConvertAnonymousObjectDetailToProp(struct soap *soap,
    const objectdetails_t *lpDetails, unsigned int ulPropTag,
    struct propVal *lpPropVal) const
{
	ECRESULT er;
	struct propVal sPropVal;
	std::string strValue;
	std::list<std::string> lstrValues;
	std::list<objectid_t> lobjValues;
	unsigned int ulGetPropTag;

	// Force PT_STRING8 versions for anonymous getprops
	if ((PROP_TYPE(ulPropTag) & PT_MV_STRING8) == PT_STRING8)
		ulGetPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_STRING8);
	else if ((PROP_TYPE(ulPropTag) & PT_MV_STRING8) == PT_MV_STRING8)
		ulGetPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_MV_STRING8);
	else
		ulGetPropTag = ulPropTag;

	if (PROP_TYPE(ulPropTag) & MV_FLAG) {
		lstrValues = lpDetails->GetPropListString((property_key_t)ulGetPropTag);
		if (lstrValues.empty())
			return KCERR_UNKNOWN;
	} else {
		strValue = lpDetails->GetPropString((property_key_t)ulGetPropTag);
		if (strValue.empty())
			return KCERR_UNKNOWN;
	}

	/* Special properties which cannot be copied blindly */
	switch (ulPropTag) {
	case 0x80050102: /* PR_EMS_AB_MANAGER	| PT_BINARY */
	case 0x800C0102: /* PR_EMS_AB_OWNER		| PT_BINARY */
		er = CreateABEntryID(soap, lpDetails->GetPropObject((property_key_t)ulPropTag), lpPropVal);
		if (er != erSuccess)
			return er;
		lpPropVal->ulPropTag = ulPropTag;
		return erSuccess;
	case 0x80081102: /* PR_EMS_AB_IS_MEMBER_OF_DL	| PT_MV_BINARY */
	case 0x800E1102: { /* PR_EMS_AB_REPORTS | PT_MV_BINARY */
		lobjValues = lpDetails->GetPropListObject((property_key_t)ulPropTag);
		lpPropVal->__union = SOAP_UNION_propValData_mvbin;
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.mvbin.__size = 0;
		lpPropVal->Value.mvbin.__ptr = s_alloc<struct xsd__base64Binary>(soap, lobjValues.size());

		unsigned int i = 0;
		for (const auto &obj : lobjValues) {
			er = CreateABEntryID(soap, obj, &sPropVal);
			if (er != erSuccess)
				continue;
			lpPropVal->Value.mvbin.__ptr[i].__ptr = sPropVal.Value.bin->__ptr;
			lpPropVal->Value.mvbin.__ptr[i++].__size = sPropVal.Value.bin->__size;
		}
		lpPropVal->Value.mvbin.__size = i;
		return erSuccess;
	}
	default:
		break;
	}

	switch (PROP_TYPE(ulPropTag)) {
	case PT_BOOLEAN:
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.b = parseBool(strValue.c_str());
		lpPropVal->__union = SOAP_UNION_propValData_b;
		break;
	case PT_SHORT:
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.i = atoi(strValue.c_str());
		lpPropVal->__union = SOAP_UNION_propValData_i;
		break;
	case PT_LONG:
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.ul = atoi(strValue.c_str());
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PT_LONGLONG:
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.li = atol(strValue.c_str());
		lpPropVal->__union = SOAP_UNION_propValData_li;
		break;
	case PT_STRING8:
	case PT_UNICODE:
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.lpszA = s_strcpy(soap, strValue.c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE: {
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.mvszA.__size = lstrValues.size();
		lpPropVal->Value.mvszA.__ptr = s_alloc<char *>(soap, lstrValues.size());
		unsigned int i = 0;
		for (const auto &val : lstrValues)
			lpPropVal->Value.mvszA.__ptr[i++] = s_strcpy(soap, val.c_str());
		lpPropVal->__union = SOAP_UNION_propValData_mvszA;
		break;
	}
	case PT_BINARY:
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__size = strValue.size();
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, strValue.size());
		memcpy(lpPropVal->Value.bin->__ptr, strValue.data(), strValue.size());
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		break;
	case PT_MV_BINARY: {
		lpPropVal->ulPropTag = ulPropTag;
		lpPropVal->Value.mvbin.__size = lstrValues.size();
		lpPropVal->Value.mvbin.__ptr = s_alloc<struct xsd__base64Binary>(soap, lstrValues.size());
		unsigned int i = 0;
		for (const auto &val : lstrValues) {
			lpPropVal->Value.mvbin.__ptr[i].__size = val.size();
			lpPropVal->Value.mvbin.__ptr[i].__ptr = s_alloc<unsigned char>(soap, val.size());
			memcpy(lpPropVal->Value.mvbin.__ptr[i++].__ptr, val.data(), val.size());
		}
		lpPropVal->__union = SOAP_UNION_propValData_mvbin;
		break;
	}
	default:
		return KCERR_UNKNOWN;
	}
	return erSuccess;
}

ECRESULT ECUserManagement::cvt_user_to_props(struct soap *soap,
    unsigned int ulId, unsigned int ulMapiType, unsigned int proptag,
    const objectdetails_t *lpDetails, struct propVal *lpPropVal)
{
	ECRESULT er = erSuccess;
	switch (NormalizePropTag(proptag)) {
	case PR_ENTRYID: {
		er = CreateABEntryID(soap, ulId, ulMapiType, lpPropVal);
		if (er != erSuccess)
			return er;
		break;
	}
	case PR_EC_NONACTIVE:
		lpPropVal->Value.b = (lpDetails->GetClass() != ACTIVE_USER);
		lpPropVal->__union = SOAP_UNION_propValData_b;
		break;
	case PR_EC_ADMINISTRATOR:
		lpPropVal->Value.ul = lpDetails->GetPropInt(OB_PROP_I_ADMINLEVEL);
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_EC_COMPANY_NAME: {
		if (IsInternalObject(ulId) || (! m_lpSession->GetSessionManager()->IsHostedSupported())) {
			lpPropVal->Value.lpszA = s_strcpy(soap, "");
		} else {
			objectdetails_t sCompanyDetails;
			er = GetObjectDetails(lpDetails->GetPropInt(OB_PROP_I_COMPANYID), &sCompanyDetails);
			if (er != erSuccess)
				return er;
			lpPropVal->Value.lpszA = s_strcpy(soap, sCompanyDetails.GetPropString(OB_PROP_S_FULLNAME).c_str());
		}
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	}
	case PR_SMTP_ADDRESS:
		lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_EMAIL).c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_MESSAGE_CLASS:
		lpPropVal->Value.lpszA = s_strcpy(soap, "IPM.Contact");
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_NORMALIZED_SUBJECT:
	case PR_7BIT_DISPLAY_NAME:
	case PR_DISPLAY_NAME:
	case PR_TRANSMITABLE_DISPLAY_NAME:
		lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_FULLNAME).c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_INSTANCE_KEY: {
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, 2 * sizeof(ULONG));
		lpPropVal->Value.bin->__size = 2*sizeof(ULONG);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		memcpy(lpPropVal->Value.bin->__ptr, &ulId, sizeof(ULONG));
		uint32_t tmp4 = cpu_to_le32(0); /* "ulOrder" */
		memcpy(lpPropVal->Value.bin->__ptr + sizeof(ULONG), &tmp4, sizeof(tmp4));
		break;
	}
	case PR_OBJECT_TYPE:
		lpPropVal->Value.ul = MAPI_MAILUSER;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_DISPLAY_TYPE:
		if (lpDetails->GetClass() != NONACTIVE_CONTACT)
			lpPropVal->Value.ul = DT_MAILUSER;
		else
			lpPropVal->Value.ul = DT_REMOTE_MAILUSER;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_DISPLAY_TYPE_EX:
		switch(lpDetails->GetClass()) {
		default:
		case ACTIVE_USER:
			lpPropVal->Value.ul = DT_MAILUSER | DTE_FLAG_ACL_CAPABLE;
			break;
		case NONACTIVE_USER:
			lpPropVal->Value.ul = DT_MAILUSER;
			break;
		case NONACTIVE_ROOM:
			lpPropVal->Value.ul = DT_ROOM;
			break;
		case NONACTIVE_EQUIPMENT:
			lpPropVal->Value.ul = DT_EQUIPMENT;
			break;
		case NONACTIVE_CONTACT:
			lpPropVal->Value.ul = DT_REMOTE_MAILUSER;
			break;
		};
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_EMS_AB_ROOM_CAPACITY:
		lpPropVal->Value.ul = lpDetails->GetPropInt(OB_PROP_I_RESOURCE_CAPACITY);
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_EMS_AB_ROOM_DESCRIPTION: {
		if (lpDetails->GetClass() == ACTIVE_USER) {
			lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
			lpPropVal->Value.ul = KCERR_NOT_FOUND;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		}
		std::string strDesc = lpDetails->GetPropString(OB_PROP_S_RESOURCE_DESCRIPTION);
		if (strDesc.empty()) {
			switch (lpDetails->GetClass()) {
			case NONACTIVE_ROOM:
				strDesc = "Room";
				break;
			case NONACTIVE_EQUIPMENT:
				strDesc = "Equipment";
				break;
			default:
				// actually to keep the compiler happy
				strDesc = "Invalid";
				break;
			}
		}
		lpPropVal->ulPropTag = proptag;
		lpPropVal->Value.lpszA = s_strcpy(soap, strDesc.c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	}
	case PR_SEARCH_KEY: {
		std::string strSearchKey = "ZARAFA:"s + strToUpper(lpDetails->GetPropString(OB_PROP_S_EMAIL));
		lpPropVal->Value.bin = s_alloc<xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, strSearchKey.size()+1);
		lpPropVal->Value.bin->__size = strSearchKey.size()+1;
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		strcpy((char *)lpPropVal->Value.bin->__ptr, strSearchKey.c_str());
		break;
	}
	case PR_ADDRTYPE:
		lpPropVal->Value.lpszA = s_strcpy(soap, "ZARAFA");
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_RECORD_KEY:
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ULONG));
		lpPropVal->Value.bin->__size = sizeof(ULONG);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		memcpy(lpPropVal->Value.bin->__ptr, &ulId, sizeof(ULONG));
		break;
	case PR_EMS_AB_HOME_MDB: {
		/* Make OL2010 happy */
		auto serverName = lpDetails->GetPropString(OB_PROP_S_SERVERNAME);
		if (serverName.empty())
			serverName = "Unknown";
		auto hostname = "/o=Domain/ou=Location/cn=Configuration/cn=Servers/cn=" + serverName + "/cn=Microsoft Private MDB";
		lpPropVal->Value.lpszA = s_strcpy(soap, hostname.c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	}
	case PR_ACCOUNT:
	case PR_EMAIL_ADDRESS:
		// Dont use login name for NONACTIVE_CONTACT since it doesn't have a login name
		if (lpDetails->GetClass() != NONACTIVE_CONTACT)
			lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_LOGIN).c_str());
		else
			lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_FULLNAME).c_str());

		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_SEND_INTERNET_ENCODING:
		lpPropVal->Value.ul = 0;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_SEND_RICH_INFO:
		lpPropVal->Value.b = true;
		lpPropVal->__union = SOAP_UNION_propValData_b;
		break;
	case PR_AB_PROVIDER_ID:
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(GUID));
		lpPropVal->Value.bin->__size = sizeof(GUID);

		lpPropVal->__union = SOAP_UNION_propValData_bin;
		memcpy(lpPropVal->Value.bin->__ptr, &MUIDECSAB, sizeof(GUID));
		break;
	case PR_EMS_AB_X509_CERT: {
		auto strCerts = lpDetails->GetPropListString(OB_PROP_LS_CERTIFICATE);
		if (strCerts.empty())
			/* This is quite annoying. The LDAP plugin loads it in a specific location, while the db plugin
			  saves it through the anonymous map into the proptag location.
			 */
			strCerts = lpDetails->GetPropListString(static_cast<property_key_t>(proptag));
		if (strCerts.empty()) {
			lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
			lpPropVal->Value.ul = KCERR_NOT_FOUND;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		}
		unsigned int j = 0;
		lpPropVal->__union = SOAP_UNION_propValData_mvbin;
		lpPropVal->Value.mvbin.__size = strCerts.size();
		lpPropVal->Value.mvbin.__ptr = s_alloc<struct xsd__base64Binary>(soap, strCerts.size());
		for (const auto &cert : strCerts) {
			lpPropVal->Value.mvbin.__ptr[j].__size = cert.size();
			lpPropVal->Value.mvbin.__ptr[j].__ptr = s_alloc<unsigned char>(soap, cert.size());
			memcpy(lpPropVal->Value.mvbin.__ptr[j++].__ptr, cert.data(), cert.size());
		}
		break;
	}
	case PR_EC_SENDAS_USER_ENTRYIDS: {
		auto userIds = lpDetails->GetPropListObject(OB_PROP_LO_SENDAS);
		if (userIds.empty()) {
			lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
			lpPropVal->Value.ul = KCERR_NOT_FOUND;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		}
		unsigned int j = 0;
		struct propVal sPropVal;
		lpPropVal->__union = SOAP_UNION_propValData_mvbin;
		lpPropVal->Value.mvbin.__size = 0;
		lpPropVal->Value.mvbin.__ptr = s_alloc<struct xsd__base64Binary>(soap, userIds.size());
		for (const auto &uid : userIds) {
			er = CreateABEntryID(soap, uid, &sPropVal);
			if (er != erSuccess)
				continue;
			lpPropVal->Value.mvbin.__ptr[j].__ptr = sPropVal.Value.bin->__ptr;
			lpPropVal->Value.mvbin.__ptr[j++].__size = sPropVal.Value.bin->__size;
		}
		lpPropVal->Value.mvbin.__size = j;
		break;
	}
	case PR_EMS_AB_PROXY_ADDRESSES: {
		// Use upper-case 'SMTP' for primary
		std::string strPrefix("SMTP:");
		std::string address(lpDetails->GetPropString(OB_PROP_S_EMAIL));
		std::list<std::string> lstAliases = lpDetails->GetPropListString(OB_PROP_LS_ALIASES);
		unsigned int nAliases = lstAliases.size(), j = 0;

		lpPropVal->__union = SOAP_UNION_propValData_mvszA;
		lpPropVal->Value.mvszA.__ptr = s_alloc<char *>(soap, 1 + nAliases);
		if (!address.empty()) {
			address = strPrefix + address;
			lpPropVal->Value.mvszA.__ptr[j++] = s_strcpy(soap, address.c_str());
		}
		// Use lower-case 'smtp' prefix for aliases
		strPrefix = "smtp:";
		for (const auto &alias : lstAliases)
			lpPropVal->Value.mvszA.__ptr[j++] = s_strcpy(soap, (strPrefix + alias).c_str());
		lpPropVal->Value.mvszA.__size = j;
		break;
	}
	case PR_EC_EXCHANGE_DN: {
		std::string exchangeDN = lpDetails->GetPropString(OB_PROP_S_EXCH_DN);
		if (!exchangeDN.empty()) {
			lpPropVal->Value.lpszA = s_strcpy(soap, exchangeDN.c_str());
			lpPropVal->__union = SOAP_UNION_propValData_lpszA;
			break;
		}
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
		lpPropVal->Value.ul = KCERR_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	}
	case PR_EC_HOMESERVER_NAME: {
		std::string serverName = lpDetails->GetPropString(OB_PROP_S_SERVERNAME);
		if (!serverName.empty()) {
			lpPropVal->Value.lpszA = s_strcpy(soap, serverName.c_str());
			lpPropVal->__union = SOAP_UNION_propValData_lpszA;
			break;
		}
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
		lpPropVal->Value.ul = KCERR_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	}
	default:
		/* Property not handled in switch, try checking if user has mapped the property personally */
		if (ConvertAnonymousObjectDetailToProp(soap, lpDetails, proptag, lpPropVal) == erSuccess)
			break;
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
		lpPropVal->Value.ul = KCERR_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	}
	return erSuccess;
}

ECRESULT ECUserManagement::cvt_distlist_to_props(struct soap *soap,
    unsigned int ulId, unsigned int ulMapiType, unsigned int proptag,
    const objectdetails_t *lpDetails, struct propVal *lpPropVal)
{
	ECRESULT er = erSuccess;
	switch (NormalizePropTag(proptag)) {
	case PR_EC_COMPANY_NAME: {
		if (IsInternalObject(ulId) || (! m_lpSession->GetSessionManager()->IsHostedSupported())) {
			lpPropVal->Value.lpszA = s_strcpy(soap, "");
		} else {
			objectdetails_t sCompanyDetails;
			er = GetObjectDetails(lpDetails->GetPropInt(OB_PROP_I_COMPANYID), &sCompanyDetails);
			if (er != erSuccess)
				return er;
			lpPropVal->Value.lpszA = s_strcpy(soap, sCompanyDetails.GetPropString(OB_PROP_S_FULLNAME).c_str());
		}
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	}
	case PR_SEARCH_KEY: {
		std::string strSearchKey = "ZARAFA:"s + strToUpper(lpDetails->GetPropString(OB_PROP_S_FULLNAME));
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, strSearchKey.size()+1);
		lpPropVal->Value.bin->__size = strSearchKey.size()+1;
		lpPropVal->__union = SOAP_UNION_propValData_bin;

		strcpy((char *)lpPropVal->Value.bin->__ptr, strSearchKey.c_str());
		break;
	}
	case PR_ADDRTYPE:
		lpPropVal->Value.lpszA = s_strcpy(soap, "ZARAFA");
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_EMAIL_ADDRESS:
		lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_LOGIN).c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_MESSAGE_CLASS:
		lpPropVal->Value.lpszA = s_strcpy(soap, "IPM.DistList");
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_ENTRYID:
		er = CreateABEntryID(soap, ulId, ulMapiType, lpPropVal);
		if (er != erSuccess)
			return er;
		break;
	case PR_NORMALIZED_SUBJECT:
	case PR_DISPLAY_NAME:
	case PR_TRANSMITABLE_DISPLAY_NAME:
		lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_FULLNAME).c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_SMTP_ADDRESS:
		if (lpDetails->HasProp(OB_PROP_S_EMAIL)) {
			lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_EMAIL).c_str());
			lpPropVal->__union = SOAP_UNION_propValData_lpszA;
			break;
		}
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(lpPropVal->ulPropTag, PT_ERROR);
		lpPropVal->Value.ul = MAPI_E_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_INSTANCE_KEY: {
		lpPropVal->Value.bin = s_alloc<xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, 2 * sizeof(ULONG));
		lpPropVal->Value.bin->__size = 2*sizeof(ULONG);
		lpPropVal->__union = SOAP_UNION_propValData_bin;

		memcpy(lpPropVal->Value.bin->__ptr, &ulId, sizeof(ULONG));
		uint32_t tmp4 = cpu_to_le32(0); /* "ulOrder" */
		memcpy(lpPropVal->Value.bin->__ptr + sizeof(ULONG), &tmp4, sizeof(tmp4));
		break;
	}
	case PR_OBJECT_TYPE:
		lpPropVal->Value.ul = MAPI_DISTLIST;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_DISPLAY_TYPE:
		if (lpDetails->GetClass() == CONTAINER_COMPANY)
			lpPropVal->Value.ul = DT_ORGANIZATION;
		else
			lpPropVal->Value.ul = DT_DISTLIST;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_DISPLAY_TYPE_EX:
		switch (lpDetails->GetClass()) {
		case DISTLIST_GROUP:
			lpPropVal->Value.ul = DT_DISTLIST;
			break;
		case DISTLIST_SECURITY:
			lpPropVal->Value.ul = DT_SEC_DISTLIST | DTE_FLAG_ACL_CAPABLE;
			break;
		case DISTLIST_DYNAMIC:
			lpPropVal->Value.ul = DT_AGENT;
			break;
		case CONTAINER_COMPANY:
			lpPropVal->Value.ul = DT_ORGANIZATION | DTE_FLAG_ACL_CAPABLE;
			break;
		default:
			lpPropVal->ulPropTag = CHANGE_PROP_TYPE(lpPropVal->ulPropTag, PT_ERROR);
			lpPropVal->Value.ul = MAPI_E_NOT_FOUND;
			break;
		}
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_RECORD_KEY:
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ULONG));
		lpPropVal->Value.bin->__size = sizeof(ULONG);
		lpPropVal->__union = SOAP_UNION_propValData_bin;

		memcpy(lpPropVal->Value.bin->__ptr, &ulId, sizeof(ULONG));
		break;
	case PR_ACCOUNT:
		lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_LOGIN).c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_AB_PROVIDER_ID:
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(GUID));
		lpPropVal->Value.bin->__size = sizeof(GUID);

		lpPropVal->__union = SOAP_UNION_propValData_bin;
		memcpy(lpPropVal->Value.bin->__ptr, &MUIDECSAB, sizeof(GUID));
		break;
	case PR_EC_SENDAS_USER_ENTRYIDS: {
		auto userIds = lpDetails->GetPropListObject(OB_PROP_LO_SENDAS);
		if (userIds.empty()) {
			lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
			lpPropVal->Value.ul = KCERR_NOT_FOUND;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		}
		unsigned int j = 0;
		struct propVal sPropVal;
		lpPropVal->__union = SOAP_UNION_propValData_mvbin;
		lpPropVal->Value.mvbin.__size = 0;
		lpPropVal->Value.mvbin.__ptr = s_alloc<struct xsd__base64Binary>(soap, userIds.size());
		for (const auto &uid : userIds) {
			er = CreateABEntryID(soap, uid, &sPropVal);
			if (er != erSuccess)
				continue;
			lpPropVal->Value.mvbin.__ptr[j].__ptr = sPropVal.Value.bin->__ptr;
			lpPropVal->Value.mvbin.__ptr[j++].__size = sPropVal.Value.bin->__size;
		}
		lpPropVal->Value.mvbin.__size = j;
		break;
	}
	case PR_EMS_AB_PROXY_ADDRESSES: {
		// Use upper-case 'SMTP' for primary
		std::string strPrefix("SMTP:");
		std::string address(lpDetails->GetPropString(OB_PROP_S_EMAIL));
		std::list<std::string> lstAliases = lpDetails->GetPropListString(OB_PROP_LS_ALIASES);
		unsigned int nAliases = lstAliases.size(), j = 0;

		lpPropVal->__union = SOAP_UNION_propValData_mvszA;
		lpPropVal->Value.mvszA.__ptr = s_alloc<char *>(soap, 1 + nAliases);

		if (!address.empty()) {
			address = strPrefix + address;
			lpPropVal->Value.mvszA.__ptr[j++] = s_strcpy(soap, address.c_str());
		}

		// Use lower-case 'smtp' prefix for aliases
		strPrefix = "smtp:";
		for (const auto &alias : lstAliases)
			lpPropVal->Value.mvszA.__ptr[j++] = s_strcpy(soap, (strPrefix + alias).c_str());
		lpPropVal->Value.mvszA.__size = j;
		break;
	}
	case PR_EC_HOMESERVER_NAME: {
		std::string serverName = lpDetails->GetPropString(OB_PROP_S_SERVERNAME);
		if (!serverName.empty()) {
			lpPropVal->Value.lpszA = s_strcpy(soap, serverName.c_str());
			lpPropVal->__union = SOAP_UNION_propValData_lpszA;
			break;
		}
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
		lpPropVal->Value.ul = KCERR_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	}
	default:
		if (ConvertAnonymousObjectDetailToProp(soap, lpDetails, proptag, lpPropVal) == erSuccess)
			break;
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
		lpPropVal->Value.ul = KCERR_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	}
	return erSuccess;
}

/**
 * Convert objectdetails_t to a set of MAPI properties
 *
 * Creates properties for a row in the addressbook or resolvenames
 * entries. Input proptags is what the client requested, and may
 * contain PT_UNSPECIFIED. String properties are returned in UTF-8,
 * but ulPropTag is set to the correct proptype.
 *
 * @param[in]	soap			Pointer to soap struct, used for s_alloc
 * @param[in]	ulId			Internal user id
 * @param[in]	lpDetails		Pointer to details of the user
 * @param[in]	lpPropTags		Array of properties the client requested
 * @param[out]	lpPropValsRet	Property values to return to client
 * @return		ECRESULT		Kopano error code
 *
 * @todo	make sure all strings in lpszA are valid UTF-8
 */
ECRESULT ECUserManagement::ConvertObjectDetailsToProps(struct soap *soap,
    unsigned int ulId, const objectdetails_t *lpDetails,
    const struct propTagArray *lpPropTags, struct propValArray *lpPropValsRet)
{
	struct propVal *lpPropVal;
	ECSecurity *lpSecurity = NULL;
	struct propValArray sPropVals;
	struct propValArray *lpPropVals = &sPropVals;
	ULONG ulMapiType = 0;

	auto er = GetSecurity(&lpSecurity);
	if (er != erSuccess)
		goto exit;
	er = lpSecurity->IsUserObjectVisible(ulId);
	if (er != erSuccess)
		goto exit;
	er = TypeToMAPIType(lpDetails->GetClass(), &ulMapiType);
	if (er != erSuccess)
		goto exit;
	lpPropVals->__ptr = s_alloc<struct propVal>(soap, lpPropTags->__size);
	lpPropVals->__size = lpPropTags->__size;

	for (gsoap_size_t i = 0; i < lpPropTags->__size; ++i) {
		lpPropVal = &lpPropVals->__ptr[i];
		lpPropVal->ulPropTag = lpPropTags->__ptr[i];

		switch(lpDetails->GetClass()) {
		default:
			ec_log_err("Details failed for object id %d (type %d)", ulId, lpDetails->GetClass());
			er = KCERR_NOT_FOUND;
			goto exit;
		case ACTIVE_USER:
		case NONACTIVE_USER:
		case NONACTIVE_ROOM:
		case NONACTIVE_EQUIPMENT:
		case NONACTIVE_CONTACT:
			er = cvt_user_to_props(soap, ulId, ulMapiType, lpPropTags->__ptr[i], lpDetails, lpPropVal);
			if (er != erSuccess)
				goto exit;
			break;

		case CONTAINER_COMPANY:
		case DISTLIST_GROUP:
		case DISTLIST_SECURITY:
		case DISTLIST_DYNAMIC:
			er = cvt_distlist_to_props(soap, ulId, ulMapiType, lpPropTags->__ptr[i], lpDetails, lpPropVal);
			if (er != erSuccess)
				goto exit;
			break;
		} // end switch(objclass)
	}

	// copies __size and __ptr values into return struct
	*lpPropValsRet = *lpPropVals;

exit:
	if (er != erSuccess && soap == NULL)
		s_free(nullptr, lpPropVals->__ptr);
	return er;
}

ECRESULT ECUserManagement::cvt_adrlist_to_props(struct soap *soap,
    unsigned int ulId, unsigned int ulMapiType, unsigned int proptag,
    const objectdetails_t *lpDetails, struct propVal *lpPropVal) const
{
	ECRESULT er = erSuccess;
	uint32_t tmp4;
	switch (NormalizePropTag(proptag)) {
	case PR_ENTRYID: {
		er = CreateABEntryID(soap, ulId, ulMapiType, lpPropVal);
		if (er != erSuccess)
			return er;
		break;
	}
	case PR_PARENT_ENTRYID: {
		ABEID abeid;
		abeid.ulType = MAPI_ABCONT;
		abeid.ulId = 1;
		memcpy(&abeid.guid, &MUIDECSAB, sizeof(GUID));
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ABEID));
		lpPropVal->Value.bin->__size = sizeof(ABEID);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		memcpy(lpPropVal->Value.bin->__ptr, &abeid, sizeof(abeid));
		break;
	}
	case PR_NORMALIZED_SUBJECT:
	case PR_DISPLAY_NAME:
	case 0x3A20001E:// PR_TRANSMITABLE_DISPLAY_NAME
		lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_FULLNAME).c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_INSTANCE_KEY:
		lpPropVal->Value.bin = s_alloc<xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, 2 * sizeof(ULONG));
		lpPropVal->Value.bin->__size = 2*sizeof(ULONG);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		tmp4 = cpu_to_le32(ulId);
		memcpy(lpPropVal->Value.bin->__ptr, &tmp4, sizeof(tmp4));
		tmp4 = cpu_to_le32(0); /* "ulOrder" */
		memcpy(lpPropVal->Value.bin->__ptr + sizeof(tmp4), &tmp4, sizeof(tmp4));
		break;
	case PR_OBJECT_TYPE:
		lpPropVal->Value.ul = MAPI_ABCONT;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_CONTAINER_FLAGS:
		lpPropVal->Value.ul = AB_RECIPIENTS | AB_UNMODIFIABLE | AB_UNICODE_OK;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_DISPLAY_TYPE:
		lpPropVal->Value.ul = DT_FOLDER;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_RECORD_KEY:
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ULONG));
		lpPropVal->Value.bin->__size = sizeof(ULONG);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		tmp4 = cpu_to_le32(ulId);
		memcpy(lpPropVal->Value.bin->__ptr, &tmp4, sizeof(tmp4));
		break;
	case PR_AB_PROVIDER_ID:
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(GUID));
		lpPropVal->Value.bin->__size = sizeof(GUID);

		lpPropVal->__union = SOAP_UNION_propValData_bin;
		memcpy(lpPropVal->Value.bin->__ptr, &MUIDECSAB, sizeof(GUID));
		break;
	default:
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
		lpPropVal->Value.ul = KCERR_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	}
	return erSuccess;
}

ECRESULT ECUserManagement::cvt_company_to_props(struct soap *soap,
    unsigned int ulId, unsigned int ulMapiType, unsigned int proptag,
    const objectdetails_t *lpDetails, struct propVal *lpPropVal) const
{
	ECRESULT er = erSuccess;
	uint32_t tmp4;
	switch (NormalizePropTag(proptag)) {
	case PR_CONTAINER_CLASS:
		lpPropVal->Value.lpszA = s_strcpy(soap, "IPM.Contact");
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_ENTRYID: {
		er = CreateABEntryID(soap, ulId, ulMapiType, lpPropVal);
		if (er != erSuccess)
			return er;
		break;
	}
	case PR_EMS_AB_PARENT_ENTRYID:
	case PR_PARENT_ENTRYID: {
		ABEID abeid;
		abeid.ulType = MAPI_ABCONT;
		abeid.ulId = 1;
		memcpy(&abeid.guid, &MUIDECSAB, sizeof(GUID));
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ABEID));
		lpPropVal->Value.bin->__size = sizeof(ABEID);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		memcpy(lpPropVal->Value.bin->__ptr, &abeid, sizeof(abeid));
		break;
	}
	case PR_ACCOUNT:
	case PR_NORMALIZED_SUBJECT:
	case PR_DISPLAY_NAME:
	case 0x3A20001E:// PR_TRANSMITABLE_DISPLAY_NAME
		lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_FULLNAME).c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_EC_COMPANY_NAME:
		lpPropVal->Value.lpszA = s_strcpy(soap, lpDetails->GetPropString(OB_PROP_S_FULLNAME).c_str());
		lpPropVal->__union = SOAP_UNION_propValData_lpszA;
		break;
	case PR_INSTANCE_KEY:
		lpPropVal->Value.bin = s_alloc<xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, 2 * sizeof(ULONG));
		lpPropVal->Value.bin->__size = 2 * sizeof(ULONG);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		tmp4 = cpu_to_le32(ulId);
		memcpy(lpPropVal->Value.bin->__ptr, &tmp4, sizeof(tmp4));
		tmp4 = cpu_to_le32(0); /* "ulOrder" */
		memcpy(lpPropVal->Value.bin->__ptr + sizeof(tmp4), &tmp4, sizeof(tmp4));
		break;
	case PR_OBJECT_TYPE:
		lpPropVal->Value.ul = MAPI_ABCONT;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_DISPLAY_TYPE:
		lpPropVal->Value.ul = DT_ORGANIZATION;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_RECORD_KEY:
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ULONG));
		lpPropVal->Value.bin->__size = sizeof(ULONG);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		tmp4 = cpu_to_le32(ulId);
		memcpy(lpPropVal->Value.bin->__ptr, &tmp4, sizeof(tmp4));
		break;
	case PR_CONTAINER_FLAGS:
		lpPropVal->Value.ul = AB_RECIPIENTS | AB_UNMODIFIABLE | AB_UNICODE_OK;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_AB_PROVIDER_ID:
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(GUID));
		lpPropVal->Value.bin->__size = sizeof(GUID);
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		memcpy(lpPropVal->Value.bin->__ptr, &MUIDECSAB, sizeof(GUID));
		break;
	case PR_EMS_AB_IS_MASTER:
		lpPropVal->Value.b = false;
		lpPropVal->__union = SOAP_UNION_propValData_b;
		break;
	case PR_EMS_AB_CONTAINERID:
		lpPropVal->Value.ul = ulId;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	case PR_EC_HOMESERVER_NAME: {
		std::string serverName = lpDetails->GetPropString(OB_PROP_S_SERVERNAME);
		if (!serverName.empty()) {
			lpPropVal->Value.lpszA = s_strcpy(soap, serverName.c_str());
			lpPropVal->__union = SOAP_UNION_propValData_lpszA;
			break;
		}
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
		lpPropVal->Value.ul = KCERR_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	}
	default:
		if (ConvertAnonymousObjectDetailToProp(soap, lpDetails, proptag, lpPropVal) == erSuccess)
			break;
		lpPropVal->ulPropTag = CHANGE_PROP_TYPE(proptag, PT_ERROR);
		lpPropVal->Value.ul = KCERR_NOT_FOUND;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		break;
	}
	return erSuccess;
}

// Convert a userdetails_t to a set of MAPI properties
ECRESULT ECUserManagement::ConvertContainerObjectDetailsToProps(struct soap *soap,
    unsigned int ulId, const objectdetails_t *lpDetails,
    const struct propTagArray *lpPropTags, struct propValArray *lpPropVals) const
{
	struct propVal *lpPropVal;
	ECSecurity *lpSecurity = NULL;
	ULONG ulMapiType = 0;

	auto er = GetSecurity(&lpSecurity);
	if (er != erSuccess)
		return er;
	er = lpSecurity->IsUserObjectVisible(ulId);
	if (er != erSuccess)
		return er;
	er = TypeToMAPIType(lpDetails->GetClass(), &ulMapiType);
	if (er != erSuccess)
		return er;
	lpPropVals->__ptr = s_alloc<struct propVal>(soap, lpPropTags->__size);
	lpPropVals->__size = lpPropTags->__size;

	for (gsoap_size_t i = 0; i < lpPropTags->__size; ++i) {
		lpPropVal = &lpPropVals->__ptr[i];
		lpPropVal->ulPropTag = lpPropTags->__ptr[i];

		switch(lpDetails->GetClass()) {
		default:
			er = KCERR_NOT_FOUND;
			break;

		case CONTAINER_ADDRESSLIST:
			er = cvt_adrlist_to_props(soap, ulId, ulMapiType, lpPropTags->__ptr[i], lpDetails, lpPropVal);
			if (er != erSuccess)
				return er;
			break;

		case CONTAINER_COMPANY:
			er = cvt_company_to_props(soap, ulId, ulMapiType, lpPropTags->__ptr[i], lpDetails, lpPropVal);
			if (er != erSuccess)
				return er;
			break;
		} // end switch(objclass)
	}
	return er;
}

ECRESULT ECUserManagement::ConvertABContainerToProps(struct soap *soap,
    unsigned int ulId, const struct propTagArray *lpPropTagArray,
    struct propValArray *lpPropValArray) const
{
	std::string strName;
	ABEID abeid;

	lpPropValArray->__ptr = s_alloc<struct propVal>(soap, lpPropTagArray->__size);
	lpPropValArray->__size = lpPropTagArray->__size;
	abeid.ulType = MAPI_ABCONT;
	memcpy(&abeid.guid, &MUIDECSAB, sizeof(GUID));
	abeid.ulId = ulId;

	// FIXME: Should this name be hardcoded like this?
	// Are there any other values that might be passed as name?
	if (ulId == KOPANO_UID_ADDRESS_BOOK)
		strName = KOPANO_FULLNAME_ADDRESS_BOOK;
	else if (ulId == KOPANO_UID_GLOBAL_ADDRESS_BOOK)
		strName = KOPANO_FULLNAME_GLOBAL_ADDRESS_BOOK;
	else if (ulId == KOPANO_UID_GLOBAL_ADDRESS_LISTS)
		strName = KOPANO_FULLNAME_GLOBAL_ADDRESS_LISTS;
	else
		return KCERR_INVALID_PARAMETER;

	for (gsoap_size_t i = 0; i < lpPropTagArray->__size; ++i) {
		auto lpPropVal = &lpPropValArray->__ptr[i];
		lpPropVal->ulPropTag = lpPropTagArray->__ptr[i];

		switch (NormalizePropTag(lpPropTagArray->__ptr[i])) {
		uint32_t tmp4;
		case PR_SEARCH_KEY:
			lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
			lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ABEID));
			lpPropVal->Value.bin->__size = sizeof(ABEID);
			lpPropVal->__union = SOAP_UNION_propValData_bin;
			memcpy(lpPropVal->Value.bin->__ptr, &abeid, sizeof(ABEID));
			break;
		case PR_CONTAINER_CLASS:
			lpPropVal->Value.lpszA = s_strcpy(soap, "IPM.Contact");
			lpPropVal->__union = SOAP_UNION_propValData_lpszA;
			break;
		case PR_ENTRYID:
			lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
			lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ABEID));
			lpPropVal->Value.bin->__size = sizeof(ABEID);
			lpPropVal->__union = SOAP_UNION_propValData_bin;
			memcpy(lpPropVal->Value.bin->__ptr, &abeid, sizeof(ABEID));
			break;
		case PR_ACCOUNT:
		case PR_NORMALIZED_SUBJECT:
		case PR_DISPLAY_NAME:
		case 0x3A20001E:// PR_TRANSMITABLE_DISPLAY_NAME
			lpPropVal->Value.lpszA = s_strcpy(soap, strName.c_str());
			lpPropVal->__union = SOAP_UNION_propValData_lpszA;
			break;
		case PR_INSTANCE_KEY:
			lpPropVal->Value.bin = s_alloc<xsd__base64Binary>(soap);
			lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, 2 * sizeof(ULONG));
			lpPropVal->Value.bin->__size = 2 * sizeof(ULONG);
			lpPropVal->__union = SOAP_UNION_propValData_bin;
			tmp4 = cpu_to_le32(ulId);
			memcpy(lpPropVal->Value.bin->__ptr, &tmp4, sizeof(tmp4));
			memcpy(lpPropVal->Value.bin->__ptr + sizeof(tmp4), &tmp4, sizeof(tmp4));
			break;
		case PR_OBJECT_TYPE:
			lpPropVal->Value.ul = MAPI_ABCONT;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		case PR_DISPLAY_TYPE:
			/*
			 * Outlook 2000 requires DT_GLOBAL
			 * Outlook 2003 requires DT_GLOBAL
			 * Outlook 2007 requires something which is not DT_GLOBAL,
			 * allowed values is DT_CONTAINER (0x00000100), DT_LOCAL or
			 * DT_NOT_SPECIFIC
			 *
			 * Only Outlook 2007 really complains when it gets a value
			 * which is different then what it epxects.
			 */
			lpPropVal->Value.ul = (ulId == KOPANO_UID_ADDRESS_BOOK) ? DT_GLOBAL : DT_NOT_SPECIFIC;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		case PR_RECORD_KEY:
			lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
			lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ABEID));
			lpPropVal->Value.bin->__size = sizeof(ABEID);
			lpPropVal->__union = SOAP_UNION_propValData_bin;
			memcpy(lpPropVal->Value.bin->__ptr, &abeid, sizeof(ABEID));
			break;
		case PR_CONTAINER_FLAGS:
			/*
			 * We support subcontainers, but don't even consider setting the
			 * flag AB_SUBCONTAINERS for the Global Address Book since that
			 * will cause Outlook 2007 to bug() which then doesn't place
			 * the Global Address Book in the SearchPath.
			 */
			lpPropVal->Value.ul = AB_RECIPIENTS | AB_UNMODIFIABLE | AB_UNICODE_OK;
			if (ulId == KOPANO_UID_ADDRESS_BOOK)
				lpPropVal->Value.ul |= AB_SUBCONTAINERS;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		case PR_AB_PROVIDER_ID: {
			std::string strApp;
			ECSession *lpSession = NULL;

			lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
			lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(GUID));
			lpPropVal->Value.bin->__size = sizeof(GUID);
			lpPropVal->__union = SOAP_UNION_propValData_bin;
			lpSession = dynamic_cast<ECSession *>(m_lpSession);
			if (lpSession)
				lpSession->GetClientApp(&strApp);
			memcpy(lpPropVal->Value.bin->__ptr, &MUIDECSAB, sizeof(GUID));
			break;
		}
		case PR_EMS_AB_IS_MASTER:
			lpPropVal->Value.b = (ulId == KOPANO_UID_ADDRESS_BOOK);
			lpPropVal->__union = SOAP_UNION_propValData_b;
			break;
		case PR_EMS_AB_CONTAINERID:
			// 'Global Address Book' should be container ID 0, rest follows
			if (ulId != KOPANO_UID_ADDRESS_BOOK) {
			    // 'All Address Lists' has ID 7000 in MSEX
				lpPropVal->Value.ul = ulId == KOPANO_UID_GLOBAL_ADDRESS_LISTS ? 7000 : KOPANO_UID_ADDRESS_BOOK;
				lpPropVal->__union = SOAP_UNION_propValData_ul;
				break;
			}
			// id 0 (kopano address book) has no container id
			lpPropVal->ulPropTag = CHANGE_PROP_TYPE(lpPropTagArray->__ptr[i], PT_ERROR);
			lpPropVal->Value.ul = KCERR_NOT_FOUND;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		case PR_EMS_AB_PARENT_ENTRYID:
		case PR_PARENT_ENTRYID: {
			if (ulId == KOPANO_UID_ADDRESS_BOOK) {
				lpPropVal->ulPropTag = CHANGE_PROP_TYPE(lpPropTagArray->__ptr[i], PT_ERROR);
				lpPropVal->Value.ul = KCERR_NOT_FOUND;
				lpPropVal->__union = SOAP_UNION_propValData_ul;
				break;
			}
			ABEID abeid2;
			abeid2.ulType = MAPI_ABCONT;
			abeid2.ulId = KOPANO_UID_ADDRESS_BOOK;
			memcpy(&abeid2.guid, &MUIDECSAB, sizeof(GUID));
			lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
			lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ABEID));
			lpPropVal->Value.bin->__size = sizeof(ABEID);
			lpPropVal->__union = SOAP_UNION_propValData_bin;
			memcpy(lpPropVal->Value.bin->__ptr, &abeid2, sizeof(abeid2));
			break;
		}
		default:
			lpPropVal->ulPropTag = CHANGE_PROP_TYPE(lpPropTagArray->__ptr[i], PT_ERROR);
			lpPropVal->Value.ul = KCERR_NOT_FOUND;
			lpPropVal->__union = SOAP_UNION_propValData_ul;
			break;
		}
	}
	return erSuccess;
}

ECRESULT ECUserManagement::GetPublicStoreDetails(objectdetails_t *lpDetails) const
{
	objectdetails_t details;
	UserPlugin *lpPlugin = NULL;

	/* We pretend that the Public store is a company. So request (and later store) it as such. */
	// type passed was , CONTAINER_COMPANY .. still working?
	auto cache = m_lpSession->GetSessionManager()->GetCacheManager();
	auto er = cache->GetUserDetails(KOPANO_UID_EVERYONE, lpDetails);
	if (er == erSuccess)
		return erSuccess; /* Cache contained requested information, we're done.*/
	er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;

	try {
		details = lpPlugin->getPublicStoreDetails();
	} catch (const objectnotfound &) {
		return KCERR_NOT_FOUND;
	} catch (const notsupported &) {
		return KCERR_NO_SUPPORT;
	} catch (const std::exception &e) {
		ec_log_warn("K-1538: Unable to get %s details for object id %d: %s", ObjectClassToName(CONTAINER_COMPANY), KOPANO_UID_EVERYONE, e.what());
		return KCERR_NOT_FOUND;
	}
	/* Update cache so we don't have to bug the plugin until the data has changed.
	 * Note that we don't care if the update succeeded, if it fails we will retry
	 * when the user details are requested for a second time. */
	cache->SetUserDetails(KOPANO_UID_EVERYONE, details);
	*lpDetails = std::move(details);
	return erSuccess;
}

ECRESULT ECUserManagement::GetServerDetails(const std::string &strServer, serverdetails_t *lpDetails)
{
	serverdetails_t details;
	UserPlugin *lpPlugin = NULL;

	// Try the cache first
	auto cache = m_lpSession->GetSessionManager()->GetCacheManager();
	auto er = cache->GetServerDetails(strServer, lpDetails);
	if (er == erSuccess)
		return erSuccess;
	er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;

	try {
		details = lpPlugin->getServerDetails(strServer);
		cache->SetServerDetails(strServer, details);
	} catch (const objectnotfound &e) {
		ec_log_warn("K-1543: Unable to get server details for \"%s\": %s", strServer.c_str(), e.what());
		return KCERR_NOT_FOUND;
	} catch (const notsupported &) {
		return KCERR_NO_SUPPORT;
	} catch (const std::exception &e) {
		ec_log_warn("K-1539: Unable to get server details for \"%s\": %s", strServer.c_str(), e.what());
		return KCERR_NOT_FOUND;
	}

	*lpDetails = std::move(details);
	return erSuccess;
}

/**
 * Get list of servers from LDAP
 *
 * Note that the list of servers may include servers that are completely idle since
 * they are not listed as a home server for a user or as a public store server.
 *
 * The server names returned are suitable for passing to GetServerDetails()
 *
 * @param[out] lpServerList List of servers
 * @return result
 */
ECRESULT ECUserManagement::GetServerList(serverlist_t *lpServerList)
{
	UserPlugin *lpPlugin = NULL;

	auto er = GetThreadLocalPlugin(m_lpPluginFactory, &lpPlugin);
	if(er != erSuccess)
		return er;
	try {
		*lpServerList = lpPlugin->getServers();
	} catch (const std::exception &e) {
		ec_log_warn("K-1540: Unable to get server list: %s", e.what());
		return KCERR_NOT_FOUND;
	}
	return erSuccess;
}

ECRESULT ECUserManagement::CheckObjectModified(unsigned int ulObjectId,
    const std::string &localsignature, const std::string &remotesignature)
{
	int localChange = 0, remoteChange = 0;
	char *end = NULL;

	remoteChange = strtoul(remotesignature.c_str(), &end, 10);
	if (end && *end == '\0') {
		// ADS uses uSNChanged incrementing attribute
		// whenChanged and modifyTimestamp attributes are not replicated, giving too many changes
		localChange = strtoul(localsignature.c_str(), &end, 10);
		if (!end || *end != '\0' || localChange < remoteChange)
			ProcessModification(ulObjectId, remotesignature);
	} else if (localsignature != remotesignature) {
		ProcessModification(ulObjectId, remotesignature);
	}
	return erSuccess;
}

ECRESULT ECUserManagement::ProcessModification(unsigned int ulId,
    const std::string &newsignature)
{
	ECDatabase *lpDatabase = NULL;
	ABEID eid(MAPI_ABCONT, MUIDECSAB, 1);
	SOURCEKEY sSourceKey;

	// Log the change to ICS
	auto er = GetABSourceKeyV1(ulId, &sSourceKey);
	if (er != erSuccess)
		return er;

	AddABChange(m_lpSession, ICS_AB_CHANGE, sSourceKey, SOURCEKEY(CbABEID(&eid), (char *)&eid));
	// Ignore ICS error
	// Save the new signature
	er = m_lpSession->GetDatabase(&lpDatabase);
	if(er != erSuccess)
		return er;
	er = lpDatabase->DoUpdate("UPDATE users SET signature=" + lpDatabase->EscapeBinary(newsignature) + " WHERE id=" + stringify(ulId));
	if(er != erSuccess)
		return er;
	return m_lpSession->GetSessionManager()->GetCacheManager()->UpdateUser(ulId);
}

ECRESULT ECUserManagement::GetABSourceKeyV1(unsigned int ulUserId, SOURCEKEY *lpsSourceKey)
{
	objectid_t			sExternId;
	ULONG				ulType = 0;

	ECRESULT er = GetExternalId(ulUserId, &sExternId);
	if (er != erSuccess)
		return er;
	auto strEncExId = base64_encode(sExternId.id.c_str(), sExternId.id.size());
	er = TypeToMAPIType(sExternId.objclass, &ulType);
	if (er != erSuccess)
		return er;

	unsigned int ulLen = CbNewABEID(strEncExId.c_str());
	auto abchar = std::make_unique<char[]>(ulLen);
	memset(abchar.get(), 0, ulLen);
	auto lpAbeid = reinterpret_cast<ABEID *>(abchar.get());
	lpAbeid->ulId = ulUserId;
	lpAbeid->ulType = ulType;
	memcpy(&lpAbeid->guid, &MUIDECSAB, sizeof(GUID));
	if (!strEncExId.empty())
	{
		lpAbeid->ulVersion = 1;
		// avoid FORTIFY_SOURCE checks in strcpy to an address that the compiler thinks is 1 size large
		memcpy(lpAbeid->szExId, strEncExId.c_str(), strEncExId.length()+1);
	}

	*lpsSourceKey = SOURCEKEY(ulLen, abchar.get());
	return erSuccess;
}

ECRESULT ECUserManagement::CreateABEntryID(struct soap *soap,
    const objectid_t &sExternId, struct propVal *lpPropVal) const
{
	unsigned int ulObjId;
	ULONG ulObjType;

	auto er = GetLocalId(sExternId, &ulObjId);
	if (er != erSuccess)
		return er;
	er = TypeToMAPIType(sExternId.objclass, &ulObjType);
	if (er != erSuccess)
		return er;
	return CreateABEntryID(soap, ulObjId, ulObjType, lpPropVal);
}

ECRESULT ECUserManagement::CreateABEntryID(struct soap *soap,
    unsigned int ulVersion, unsigned int ulObjId, unsigned int ulType,
    objectid_t *lpExternId, gsoap_size_t *lpcbEID, ABEID **lppEid) const
{
	ABEID *lpEid = NULL;
	gsoap_size_t ulSize = 0;
	std::string	strEncExId;

	if (IsInternalObject(ulObjId)) {
		if (ulVersion != 0)
			throw std::runtime_error("Internal objects must always have v0 ABEIDs");
		lpEid = reinterpret_cast<ABEID *>(s_alloc<unsigned char>(soap, sizeof(ABEID)));
		memset(lpEid, 0, sizeof(ABEID));
		ulSize = sizeof(ABEID);
	} else if (ulVersion == 0) {
		lpEid = reinterpret_cast<ABEID *>(s_alloc<unsigned char>(soap, sizeof(ABEID)));
		memset(lpEid, 0, sizeof(ABEID));
		ulSize = sizeof(ABEID);
	} else if(ulVersion == 1) {
		if (lpExternId == NULL)
			return KCERR_INVALID_PARAMETER;
		strEncExId = base64_encode(lpExternId->id.c_str(), lpExternId->id.size());
		ulSize = CbNewABEID(strEncExId.c_str());
		lpEid = reinterpret_cast<ABEID *>(s_alloc<unsigned char>(soap, ulSize));
		memset(lpEid, 0, ulSize);
		// avoid FORTIFY_SOURCE checks in strcpy to an address that the compiler thinks is 1 size large
		memcpy(lpEid->szExId, strEncExId.c_str(), strEncExId.length()+1);
	} else {
		throw std::runtime_error("Unknown user entry version " + stringify(ulVersion));
	}

	lpEid->ulVersion = ulVersion;
	lpEid->ulType = ulType;
	lpEid->ulId = ulObjId;
	memcpy(&lpEid->guid, &MUIDECSAB, sizeof(lpEid->guid));
	*lppEid = lpEid;
	*lpcbEID = ulSize;
	return erSuccess;
}

/**
 * Create an AB entryid
 *
 * @param soap SOAP struct for memory allocations
 * @param ulObjId Object ID to make entryid for
 * @param ulType MAPI type for the object (MAPI_MAILUSER, MAPI_DISTLIST, etc)
 * @param lpPropVal Output of the entryid will be stored as binary data in lpPropVal
 * @return result
 */
ECRESULT ECUserManagement::CreateABEntryID(struct soap *soap,
    unsigned int ulObjId, unsigned int ulType, struct propVal *lpPropVal) const
{
	objectid_t 	sExternId;
	unsigned int ulVersion = 0;

	if (!IsInternalObject(ulObjId)) {
		auto er = GetExternalId(ulObjId, &sExternId);
		if (er != erSuccess)
			return er;
		ulVersion = 1;
	}

	lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
	lpPropVal->__union = SOAP_UNION_propValData_bin;
	return CreateABEntryID(soap, ulVersion, ulObjId, ulType, &sExternId,
	       &lpPropVal->Value.bin->__size,
	       reinterpret_cast<ABEID **>(&lpPropVal->Value.bin->__ptr));
}

ECRESULT ECUserManagement::GetSecurity(ECSecurity **lppSecurity) const
{
	auto lpecSession = dynamic_cast<ECSession *>(m_lpSession);
	if (lpecSession == NULL)
		return KCERR_INVALID_PARAMETER;
	if (lppSecurity)
		*lppSecurity = lpecSession->GetSecurity();
	return erSuccess;
}

ECRESULT ECUserManagement::SyncAllObjects()
{
	ECCacheManager *lpCacheManager = m_lpSession->GetSessionManager()->GetCacheManager();	// Don't delete
	std::unique_ptr<std::list<localobjectdetails_t>> lplstCompanyObjects, lplstUserObjects;
	static const unsigned int ulFlags = USERMANAGEMENT_IDS_ONLY | USERMANAGEMENT_FORCE_SYNC;
	/*
	 * When syncing the users we first start emptying the cache, this makes sure the
	 * second step won't be accidently "optimized" by caching.
	 * The second step is requesting all user objects from the plugin, ECUserManagement
	 * will then sync all results into the user database. And because the cache was
	 * cleared all signatures in the database will be checked against the signatures
	 * from the plugin.
	 * When this function has completed we can be sure that the cache has been repopulated
	 * with the userobject types, external ids and signatures of all user objects. This
	 * means that we have only "lost" the user details which will be repopulated later.
	 */
	auto er = lpCacheManager->PurgeCache(PURGE_CACHE_USEROBJECT | PURGE_CACHE_EXTERNID | PURGE_CACHE_USERDETAILS | PURGE_CACHE_SERVER);
	if (er != erSuccess)
		return er;

	// request all companies
	er = GetCompanyObjectListAndSync(CONTAINER_COMPANY, 0, &unique_tie(lplstCompanyObjects), ulFlags);
	if (er == KCERR_NO_SUPPORT)
		er = erSuccess;
	else if (er != erSuccess)
		return ec_perror("Error synchronizing company list", er);
	else
		ec_log_info("Synchronized company list");

	if (!lplstCompanyObjects || lplstCompanyObjects->empty()) {
		// get all users of server
		er = GetCompanyObjectListAndSync(OBJECTCLASS_UNKNOWN, 0, &unique_tie(lplstUserObjects), ulFlags);
		if (er != erSuccess)
			return ec_perror("Error synchronizing user list", er);
		ec_log_info("Synchronized user list");
		return erSuccess;
	}
	// per company, get all users
	for (const auto &com : *lplstCompanyObjects) {
		er = GetCompanyObjectListAndSync(OBJECTCLASS_UNKNOWN, com.ulId, &unique_tie(lplstUserObjects), ulFlags);
		if (er != erSuccess) {
			ec_log_err("Error synchronizing user list for company %d: %s (%x)", com.ulId, GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
			return er;
		}
		ec_log_info("Synchronized list for company %d", com.ulId);
	}
	return erSuccess;
}

} /* namespace */
