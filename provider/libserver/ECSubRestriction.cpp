/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <utility>
#include <cassert>
#include "ECSubRestriction.h"
#include <mapidefs.h>
#include <mapitags.h>
#include <kopano/stringutil.h>
#include "ECSession.h"
#include "ECStoreObjectTable.h"
#include "ECGenericObjectTable.h"
#include "SOAPUtils.h"
#include "ECSessionManager.h"

namespace KC {

static ECRESULT RunSubRestriction(ECSession *, const void *ecod_store, const struct restrictSub *, const ECObjectTableList *, const ECLocale &, SUBRESTRICTIONRESULT &);

static ECRESULT GetSubRestrictionRecursive(const struct restrictTable *lpRestrict,
    unsigned int *lpulCount, unsigned int ulSubRestriction,
    struct restrictSub **lppSubRestrict, unsigned int maxdepth)
{
    ECRESULT er;
    unsigned int ulCount = 0;

    if(maxdepth == 0)
        return KCERR_TOO_COMPLEX;

    if(lpulCount == NULL) // If the caller didn't want to count restrictions, we still want to count internally
        lpulCount = &ulCount;
    assert(lpRestrict != NULL);

    switch(lpRestrict->ulType) {
        case RES_AND:
            for (gsoap_size_t i = 0; i < lpRestrict->lpAnd->__size; ++i) {
                er = GetSubRestrictionRecursive(lpRestrict->lpAnd->__ptr[i], lpulCount, ulSubRestriction, lppSubRestrict, maxdepth-1);
                if(er != erSuccess)
                    return er;
            }
            break;
        case RES_OR:
            for (gsoap_size_t i = 0; i < lpRestrict->lpOr->__size; ++i) {
                er = GetSubRestrictionRecursive(lpRestrict->lpOr->__ptr[i], lpulCount, ulSubRestriction, lppSubRestrict, maxdepth-1);
                if(er != erSuccess)
                    return er;
            }
            break;
        case RES_NOT:
            er = GetSubRestrictionRecursive(lpRestrict->lpNot->lpNot, lpulCount, ulSubRestriction, lppSubRestrict, maxdepth-1);
            break;
        case RES_CONTENT:
        case RES_PROPERTY:
        case RES_COMPAREPROPS:
        case RES_BITMASK:
        case RES_SIZE:
        case RES_EXIST:
        case RES_COMMENT:
            break;
        case RES_SUBRESTRICTION:
            if (lpulCount != nullptr && lppSubRestrict != nullptr &&
                /* Looking for a subrestriction */
                *lpulCount == ulSubRestriction)
                    *lppSubRestrict = lpRestrict->lpSub;
            // Counting subrestrictions
            if(lpulCount)
			++*lpulCount;

            break;
    }
	return erSuccess;
}

ECRESULT GetSubRestrictionCount(const struct restrictTable *lpRestrict,
    unsigned int *lpulCount)
{
	// Recursively get the amount of subqueries in the given restriction
	return GetSubRestrictionRecursive(lpRestrict, lpulCount, 0, NULL, SUBRESTRICTION_MAXDEPTH);
}

ECRESULT GetSubRestriction(const struct restrictTable *lpBase,
    unsigned int ulCount, struct restrictSub **lppSubRestrict)
{
	return GetSubRestrictionRecursive(lpBase, NULL, ulCount, lppSubRestrict, SUBRESTRICTION_MAXDEPTH);
}

// Get results for all subqueries for a set of objects
ECRESULT RunSubRestrictions(ECSession *lpSession, const void *lpECODStore,
    const struct restrictTable *lpRestrict, const ECObjectTableList *lpObjects,
    const ECLocale &locale, SUBRESTRICTIONRESULTS &results)
{
    unsigned int ulCount = 0;
    struct restrictSub *lpSubRestrict = NULL;

	results.clear();
	auto er = GetSubRestrictionCount(lpRestrict, &ulCount);
    if(er != erSuccess)
		return er;

	for (unsigned int i = 0; i < ulCount; ++i) {
        er = GetSubRestriction(lpRestrict, i, &lpSubRestrict);
        if(er != erSuccess)
			return er;
		SUBRESTRICTIONRESULT result;
		er = RunSubRestriction(lpSession, lpECODStore, lpSubRestrict, lpObjects, locale, result);
        if(er != erSuccess)
			return er;
		results.emplace_back(std::move(result));
    }
	return erSuccess;
}

// Run a single subquery on a set of objects
static ECRESULT RunSubRestriction(ECSession *lpSession, const void *lpECODStore,
    const struct restrictSub *lpRestrict, const ECObjectTableList *lpObjects,
    const ECLocale &locale, SUBRESTRICTIONRESULT &result)
{
    unsigned int ulType = 0;
    std::string strQuery;
	DB_RESULT lpDBResult;
    DB_ROW lpRow = NULL;
    struct propTagArray *lpPropTags = NULL;
    ECObjectTableList::const_iterator iterObject;
    ECObjectTableList lstSubObjects;
    std::map<unsigned int, unsigned int> mapParent;
	auto lpResult = std::make_unique<SUBRESTRICTIONRESULT>();
    struct rowSet *lpRowSet = NULL;
    bool fMatch = false;
    unsigned int ulSubObject = 0;
    unsigned int ulParent = 0;
    sObjectTableKey sKey;
    ECDatabase *lpDatabase = NULL;

	auto cache = lpSession->GetSessionManager()->GetCacheManager();
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	if (lpObjects->empty())
		goto exit;				// nothing to search in, return success.

    assert(lpRestrict != NULL);
    switch(lpRestrict->ulSubObject) {
        case PR_MESSAGE_RECIPIENTS:
            ulType = MAPI_MAILUSER;
            break;
        case PR_MESSAGE_ATTACHMENTS:
            ulType = MAPI_ATTACH;
            break;
        default:
            er = KCERR_INVALID_PARAMETER;
            goto exit;
    }

    // Get property tags we'll be needing to evaluate the restriction
    er = ECGenericObjectTable::GetRestrictPropTags(lpRestrict->lpSubObject, NULL, &lpPropTags);
    if(er != erSuccess)
        goto exit;

    // Get the subobject IDs we are querying from the database
    strQuery = "SELECT hierarchy.parent, hierarchy.id FROM hierarchy WHERE hierarchy.type = " + stringify(ulType) + " AND hierarchy.parent IN (";

    for (const auto &ob : *lpObjects) {
        strQuery += stringify(ob.ulObjId);
        strQuery += ",";
    }

    // Remove trailing comma
    strQuery.resize(strQuery.size()-1);

    strQuery += ")";

    er = lpDatabase->DoSelect(strQuery, &lpDBResult);
    if(er != erSuccess)
        goto exit;

    while(1) {
		lpRow = lpDBResult.fetch_row();
        if(lpRow == NULL)
            break;

        if(lpRow[0] == NULL || lpRow[1] == NULL)
            break;

        ulParent = atoi(lpRow[0]);
        ulSubObject = atoi(lpRow[1]);

        // Remember which subobject belongs to which object
        mapParent[ulSubObject] = ulParent;

        // Add an item to the rows we want to be querying
        sKey.ulObjId = ulSubObject;
        sKey.ulOrderId = 0;
		lstSubObjects.emplace_back(sKey);
    }

	if (lstSubObjects.empty())
		goto exit;				// no objects found, return success

    // lstSubObjects contains list of objects to match, mapParent contains mapping to parent

    // Get the actual row data from the database
    er = ECStoreObjectTable::QueryRowData(NULL, NULL, lpSession, &lstSubObjects, lpPropTags, lpECODStore, &lpRowSet, false, false, true);
    if(er != erSuccess)
        goto exit;

    iterObject = lstSubObjects.cbegin();
    // Loop through all the rows, see if they match
    for (gsoap_size_t i = 0; i < lpRowSet->__size; ++i) {
		er = ECGenericObjectTable::MatchRowRestrict(cache, &lpRowSet->__ptr[i], lpRestrict->lpSubObject, nullptr, locale, &fMatch);
        if(er != erSuccess)
            goto exit;

        if(fMatch) {
            auto iterParent = mapParent.find(iterObject->ulObjId);
            if (iterParent != mapParent.cend())
                // Remember the id of the message one of whose subobjects matched
				result.emplace(iterParent->second);
        }

        // Optimisation possibility: if one of the subobjects matches, we shouldn't bother checking
        // other subobjects. This is a rather minor optimisation though.

        ++iterObject;
        // lstSubObjects will always be in the same order as lpRowSet
    }

exit:
    if(lpRowSet)
        FreeRowSet(lpRowSet, true);
    if(lpPropTags)
        FreePropTagArray(lpPropTags);

    return er;
}

} /* namespace */
