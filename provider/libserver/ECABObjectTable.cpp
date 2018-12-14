/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <list>
#include <memory>
#include <new>
#include "kcore.hpp"
#include <kopano/kcodes.h>
#include <kopano/tie.hpp>
#include <mapidefs.h>
#include <mapitags.h>
#include <kopano/mapiext.h>
#include <kopano/EMSAbTag.h>
#include <kopano/Util.h>
#include "SOAPUtils.h"
#include "ECABObjectTable.h"
#include "ECSecurity.h"
#include "ECSession.h"
#include "ECSessionManager.h"
#include <kopano/stringutil.h>

namespace KC {

ECABObjectTable::ECABObjectTable(ECSession *ses, unsigned int ulABId,
    unsigned int ulABType, unsigned int ulABParentId,
    unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale) :
	ECGenericObjectTable(ses, ulABType, ulFlags, locale)
{
	auto lpODAB = new ECODAB;
	lpODAB->ulABId = ulABId;
	lpODAB->ulABType = ulABType;
	lpODAB->ulABParentId = ulABParentId;
	lpODAB->ulABParentType = ulABParentType;

	// Set dataobject
	m_lpObjectData = lpODAB;

	// Set callback function for queryrowdata
	m_lpfnQueryRowData = QueryRowData;

	// Filter out all objects which are hidden from addressbook. We don't need details either.
	m_ulUserManagementFlags = USERMANAGEMENT_IDS_ONLY | USERMANAGEMENT_ADDRESSBOOK;

	// Set the default sort order for Address Book entries
	/*
	 * The Global Address Book must be the first entry in
	 * getHierarchyTable() otherwise it will not be set as default
	 * directory!
	 *
	 * To support future hierarchy additions like company subcontainers, or
	 * other global containers besides the GAB, we should sort the table
	 * using the PR_EMS_AB_HIERARCHY_PATH property which is formatted as
	 * followed for the GAB and Address Lists:
	 *
	 * /Global Address Book
	 * /Global Address Book/Company A
	 * /Global Address Book/Company B
	 * /Global Address Lists/Addresslist A
	 * /Global Address Lists/Addresslist B
	 */
	struct sortOrder sObjectType;
	sObjectType.ulPropTag = PR_EMS_AB_HIERARCHY_PATH;
	sObjectType.ulOrder   = EC_TABLE_SORT_ASCEND;
	struct sortOrderArray sDefaultSortOrder;
	sDefaultSortOrder.__ptr = &sObjectType;
	sDefaultSortOrder.__size = 1;
	SetSortOrder(&sDefaultSortOrder, 0, 0);

	if (!parseBool(ses->GetSessionManager()->GetConfig()->GetSetting("abtable_initially_empty")))
		return;
	restrictOr ro;
	ro.__size = 0;
	ro.__ptr = nullptr;
	restrictTable rt;
	rt.ulType = RES_OR;
	rt.lpOr = &ro;
	CopyRestrictTable(nullptr, &rt, &lpsRestrict);
}

ECABObjectTable::~ECABObjectTable()
{
	delete (ECODAB *)m_lpObjectData;
}

ECRESULT ECABObjectTable::Create(ECSession *lpSession, unsigned int ulABId, unsigned int ulABType, unsigned int ulABParentId, unsigned int ulABParentType, unsigned int ulFlags, const ECLocale &locale, ECABObjectTable **lppTable)
{
	return alloc_wrap<ECABObjectTable>(lpSession, ulABId, ulABType,
	       ulABParentId, ulABParentType, ulFlags, locale).put(lppTable);
}

ECRESULT ECABObjectTable::GetColumnsAll(ECListInt* lplstProps)
{
	auto lpODAB = static_cast<const ECODAB *>(m_lpObjectData);
	assert(lplstProps != NULL);

	//List always empty
	lplstProps->clear();

	// Add some generated and standard properties
	lplstProps->emplace_back(PR_DISPLAY_NAME);
	lplstProps->emplace_back(PR_ENTRYID);
	lplstProps->emplace_back(PR_DISPLAY_TYPE);
	lplstProps->emplace_back(PR_DISPLAY_TYPE_EX);
	lplstProps->emplace_back(PR_INSTANCE_KEY);
	lplstProps->emplace_back(PR_RECORD_KEY);
	lplstProps->emplace_back(PR_OBJECT_TYPE);
	lplstProps->emplace_back(PR_AB_PROVIDER_ID);
	lplstProps->emplace_back(PR_LAST_MODIFICATION_TIME);
	lplstProps->emplace_back(PR_CREATION_TIME);

	if(lpODAB->ulABType == MAPI_ABCONT) {
	    // Hierarchy table
		lplstProps->emplace_back(PR_CONTAINER_FLAGS);
		lplstProps->emplace_back(PR_DEPTH);
		lplstProps->emplace_back(PR_EMS_AB_CONTAINERID);
	} else {
	    // Contents table
		lplstProps->emplace_back(PR_SMTP_ADDRESS);
	}
	return erSuccess;
}

ECRESULT ECABObjectTable::ReloadTableMVData(ECObjectTableList* lplistRows, ECListInt* lplistMVPropTag)
{
	assert(lplistMVPropTag->size() < 2); //FIXME: Limit of one 1 MV column
	// scan for MV-props and add rows
	// Add items to list
	return erSuccess;
}

ECRESULT ECABObjectTable::GetMVRowCount(std::list<unsigned int> &&ids,
    std::map<unsigned int, unsigned int> &lpulCount)
{
	lpulCount.clear();
	return erSuccess;
}

ECRESULT ECABObjectTable::QueryRowData(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bTableData, bool bTableLimit)
{
	ECRESULT er = erSuccess;
	auto lpODAB = static_cast<const ECODAB *>(lpObjectData);
	struct rowSet	*lpsRowSet = NULL;
	assert(lpRowList != NULL);

	if (lpODAB->ulABType == MAPI_ABCONT)
		er = lpSession->GetUserManagement()->QueryHierarchyRowData(soap, lpRowList, lpsPropTagArray, &lpsRowSet);
	else if (lpODAB->ulABType == MAPI_MAILUSER || lpODAB->ulABType == MAPI_DISTLIST)
		er = lpSession->GetUserManagement()->QueryContentsRowData(soap, lpRowList, lpsPropTagArray, &lpsRowSet);
	else
		er = KCERR_INVALID_TYPE;
	if(er != erSuccess)
		return er;

	*lppRowSet = lpsRowSet;
	return erSuccess;
}

struct filter_objects {
	unsigned int m_ulFlags;
	filter_objects(unsigned int ulFlags) : m_ulFlags(ulFlags) {}

	bool operator()(const localobjectdetails_t &details) const
	{
		return
			((m_ulFlags & AB_FILTER_SYSTEM) &&
			 (details.GetClass() == DISTLIST_SECURITY && details.ulId == KOPANO_UID_EVERYONE)) ||
			((m_ulFlags & AB_FILTER_SYSTEM) &&
			 (details.GetClass() == ACTIVE_USER && details.ulId == KOPANO_UID_SYSTEM)) ||
			((m_ulFlags & AB_FILTER_ADDRESSLIST) &&
			 (details.GetClass() == CONTAINER_ADDRESSLIST)) ||
			((m_ulFlags & AB_FILTER_CONTACTS) &&
			 (details.GetClass() == NONACTIVE_CONTACT));
	}
};

ECRESULT ECABObjectTable::LoadHierarchyAddressList(unsigned int ulObjectId,
    unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpObjects;

	if (ulObjectId == KOPANO_UID_GLOBAL_ADDRESS_BOOK ||
		ulObjectId == KOPANO_UID_GLOBAL_ADDRESS_LISTS)
	{
		/* Global Address Book, load addresslist of the users own company */
		auto er = lpSession->GetSecurity()->GetUserCompany(&ulObjectId);
		if (er != erSuccess)
			return er;
	}

	auto er = lpSession->GetUserManagement()->GetCompanyObjectListAndSync(CONTAINER_ADDRESSLIST,
	          ulObjectId, &unique_tie(lpObjects), m_ulUserManagementFlags);
	if (er != erSuccess)
		return er;

	/* Filter objects */
	if (ulFlags)
		lpObjects->remove_if(filter_objects(ulFlags));
	if (lppObjects != nullptr)
		*lppObjects = lpObjects.release();
	return erSuccess;
}

ECRESULT ECABObjectTable::LoadHierarchyCompany(unsigned int ulObjectId,
    unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpObjects;
	ECSecurity *lpSecurity = lpSession->GetSecurity();

	auto er = lpSecurity->GetViewableCompanyIds(m_ulUserManagementFlags, &unique_tie(lpObjects));
	if (er != erSuccess)
		return er;
	/*
	 * This looks a bit hackish, and it probably can be considered as such.
	 * If the size of lpObjects is 1 then the user can only view a single company space (namely his own).
	 * There are 3 reasons this could happen.
	 * 1. When working in a non-hosted environment.
	 * 2. When the company does not have enough permissions to view any other company spaces.
	 * 3. There's only one company.
	 *
	 * Having a Global Address Book _and_ a container for the company will only look strange and
	 * confusing for the user. So lets delete the entry altogether for everybody except SYSTEM
	 * and SYSADMIN users.
	 */
	if (lpObjects->size() == 1 && lpSecurity->GetAdminLevel() < ADMIN_LEVEL_SYSADMIN)
		lpObjects->clear();
	if (lppObjects != nullptr)
		*lppObjects = lpObjects.release();
	return erSuccess;
}

ECRESULT ECABObjectTable::LoadHierarchyContainer(unsigned int ulObjectId,
    unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpObjects;
	objectid_t objectid;

	if (ulObjectId == KOPANO_UID_ADDRESS_BOOK) {
		/*
		 * Kopano Address Book
		 * The Kopano Address Book contains 2 containers,
		 * the first container is the Global Address Book,
		 * the second is the Global Address Lists container.
		 */
		lpObjects.reset(new std::list<localobjectdetails_t>());
		lpObjects->emplace_back(KOPANO_UID_GLOBAL_ADDRESS_BOOK, CONTAINER_COMPANY);
		if (!(m_ulUserManagementFlags & USERMANAGEMENT_IDS_ONLY))
			lpObjects->back().SetPropString(OB_PROP_S_LOGIN, KOPANO_ACCOUNT_GLOBAL_ADDRESS_BOOK);
		lpObjects->emplace_back(KOPANO_UID_GLOBAL_ADDRESS_LISTS, CONTAINER_ADDRESSLIST);
		if (!(m_ulUserManagementFlags & USERMANAGEMENT_IDS_ONLY))
			lpObjects->back().SetPropString(OB_PROP_S_LOGIN, KOPANO_ACCOUNT_GLOBAL_ADDRESS_LISTS);
	} else if (ulObjectId == KOPANO_UID_GLOBAL_ADDRESS_BOOK) {
		/*
		 * Global Address Book
		 * The hierarchy for the Global Address Book contains all visible
		 * companies and the addresslists belonging to this company.
		 *
		 * When in offline mode we only support the Global Address Book container,
		 * there are 2 reasons for this:
		 * 1) Currently offline mode does not assign users to companies, which means the lists are empty
		 *    except for the company itself and the System user.
		 * 2) Outlook generates the hierarchy view once during startup, when permissions change and
		 *    during sync one of the companies becomes unavailable, it will still be present
		 *    in the hierarchy view. The user will not be allowed to open it, so it isn't a real security risk,
		 *    but since we still have issue (1) open, we might as well disable the hierarchy view
		 *    containers completely. */
		auto er = LoadHierarchyCompany(ulObjectId, ulFlags, &unique_tie(lpObjects));
		if (er != erSuccess)
			return er;
	} else if (ulObjectId == KOPANO_UID_GLOBAL_ADDRESS_LISTS) {
		if (lpSession->GetSecurity()->GetUserId() == KOPANO_UID_SYSTEM)
			return KCERR_INVALID_PARAMETER;
		auto er = LoadHierarchyAddressList(ulObjectId, ulFlags, &unique_tie(lpObjects));
		if (er != erSuccess)
			return er;
	} else {
		/*
		 * Normal container
		 * Containers don't have additional subcontainers
		 */
		lpObjects.reset(new std::list<localobjectdetails_t>());
	}
	if (lppObjects != nullptr)
		*lppObjects = lpObjects.release();
	return erSuccess;
}

ECRESULT ECABObjectTable::LoadContentsAddressList(unsigned int ulObjectId,
    unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpObjects;

	auto er = lpSession->GetUserManagement()->GetSubObjectsOfObjectAndSync(OBJECTRELATION_ADDRESSLIST_MEMBER,
	          ulObjectId, &unique_tie(lpObjects), m_ulUserManagementFlags);
	if (er != erSuccess)
		return er;

	/* Filter objects */
	if (ulFlags)
		lpObjects->remove_if(filter_objects(ulFlags));
	if (lppObjects != nullptr)
		*lppObjects = lpObjects.release();
	return erSuccess;
}

ECRESULT ECABObjectTable::LoadContentsCompany(unsigned int ulObjectId,
    unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpObjects;

	auto er = lpSession->GetUserManagement()->GetCompanyObjectListAndSync(OBJECTCLASS_UNKNOWN,
	          ulObjectId, &unique_tie(lpObjects), m_ulUserManagementFlags);
	if (er != erSuccess)
		return er;

	/* Filter objects */
	if (ulFlags)
		lpObjects->remove_if(filter_objects(ulFlags));
	if (lppObjects != nullptr)
		*lppObjects = lpObjects.release();
	return erSuccess;
}

ECRESULT ECABObjectTable::LoadContentsDistlist(unsigned int ulObjectId,
    unsigned int ulFlags, std::list<localobjectdetails_t> **lppObjects)
{
	std::unique_ptr<std::list<localobjectdetails_t> > lpObjects;

	auto er = lpSession->GetUserManagement()->GetSubObjectsOfObjectAndSync(OBJECTRELATION_GROUP_MEMBER,
	          ulObjectId, &unique_tie(lpObjects), m_ulUserManagementFlags);
	if (er != erSuccess)
		return er;

	/* Filter objects */
	if (ulFlags)
		lpObjects->remove_if(filter_objects(ulFlags));
	if (lppObjects != nullptr)
		*lppObjects = lpObjects.release();
	return erSuccess;
}

ECRESULT ECABObjectTable::Load()
{
	auto lpODAB = static_cast<const ECODAB *>(m_lpObjectData);

	std::unique_ptr<std::list<localobjectdetails_t> > lpObjects;
	std::list<unsigned int> lstObjects;
	unsigned int ulObjectId = 0, ulObjectFilter = 0;
	objectid_t objectid;

	// If the GAB is disabled, don't show any entries except the top-level object
	auto sesmgr = lpSession->GetSessionManager();
	auto sec = lpSession->GetSecurity();
	if (lpODAB->ulABParentId != 0 &&
	    !parseBool(sesmgr->GetConfig()->GetSetting("enable_gab")) &&
	    sec->GetAdminLevel() == 0)
		return erSuccess;
	auto er = sec->IsUserObjectVisible(lpODAB->ulABParentId);
	if (er != erSuccess)
		return er;
	/*
	 * Check if we are loading the contents or hierarchy
	 */
	if (lpODAB->ulABType == MAPI_ABCONT) {
		/*
		 * Load hierarchy containing companies and addresslists
		 */
		if (lpODAB->ulABParentType != MAPI_ABCONT)
			return KCERR_INVALID_PARAMETER;
		er = LoadHierarchyContainer(lpODAB->ulABParentId, 0, &unique_tie(lpObjects));
		if (er != erSuccess)
			return er;
	} else if (lpODAB->ulABParentId == KOPANO_UID_GLOBAL_ADDRESS_BOOK && lpODAB->ulABParentType == MAPI_ABCONT) {
		/*
		 * Load contents of Global Address Book
		 */
		er = sec->GetUserCompany(&ulObjectId);
		if (er != erSuccess)
			return er;
		er = LoadContentsCompany(ulObjectId, AB_FILTER_ADDRESSLIST, &unique_tie(lpObjects));
		if (er != erSuccess)
			return er;
		/*
		 * The company container always contains itself as distlist,
		 * unless ulCompanyId is 0 which is the default company and
		 * that one isn't visible.
		 */
		if (!ulObjectId)
			lpObjects->emplace_front(ulObjectId, CONTAINER_COMPANY);
	} else if (lpODAB->ulABParentId == KOPANO_UID_GLOBAL_ADDRESS_LISTS && lpODAB->ulABParentType == MAPI_ABCONT) {
		/*
		 * Load contents of Global Address Lists
		 */
		return KCERR_INVALID_PARAMETER;
	} else {
		/*
		 * Load contents of distlist, company or addresslist
		 */
		auto usrmgt = lpSession->GetUserManagement();
		if (!usrmgt->IsInternalObject(lpODAB->ulABParentId)) {
			er = usrmgt->GetExternalId(lpODAB->ulABParentId, &objectid);
			if (er != erSuccess)
				return er;

			/* Security distribution lists (i.e. companies) should not contain contacts. Note that containers
			 * *do* contain contacts
			 */
			if (objectid.objclass == CONTAINER_COMPANY && lpODAB->ulABParentType == MAPI_DISTLIST)
				ulObjectFilter |= AB_FILTER_CONTACTS;
		} else if (lpODAB->ulABParentId == KOPANO_UID_SYSTEM) {
			objectid.objclass = ACTIVE_USER;
		} else if (lpODAB->ulABParentId == KOPANO_UID_EVERYONE) {
			objectid.objclass = DISTLIST_SECURITY;
			/* Security distribution lists should not contain contacts */
			ulObjectFilter |= AB_FILTER_CONTACTS;
		}

		/*
		 * System objects don't belong in container/distlist contents.
		 * Addresslists should only appear in hierarchy lists.
		 */
		ulObjectFilter |= AB_FILTER_SYSTEM | AB_FILTER_ADDRESSLIST;

		switch (objectid.objclass) {
		case DISTLIST_GROUP:
		case DISTLIST_SECURITY:
		case DISTLIST_DYNAMIC:
			er = LoadContentsDistlist(lpODAB->ulABParentId, ulObjectFilter, &unique_tie(lpObjects));
			if (er != erSuccess)
				return er;
			break;
		case CONTAINER_COMPANY:
			er = LoadContentsCompany(lpODAB->ulABParentId, ulObjectFilter, &unique_tie(lpObjects));
			if (er != erSuccess)
				return er;
			break;
		case CONTAINER_ADDRESSLIST:
			er = LoadContentsAddressList(lpODAB->ulABParentId, ulObjectFilter, &unique_tie(lpObjects));
			if (er != erSuccess)
				return er;
			break;
		default:
			return KCERR_INVALID_PARAMETER;
		}
	}

	for (const auto &obj : *lpObjects) {
		/* Only add visible items */
		if (sec->IsUserObjectVisible(obj.ulId) != erSuccess)
			continue;
		lstObjects.emplace_back(obj.ulId);
	}
	return LoadRows(&lstObjects, 0);
}

} /* namespace */
