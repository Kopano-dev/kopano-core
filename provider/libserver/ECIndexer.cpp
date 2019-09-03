/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/CommonUtil.h>
#include <kopano/MAPIErrors.h>
#include "ECGenericObjectTable.h"
#include <kopano/stringutil.h>
#include <kopano/scope.hpp>
#include "ECSearchClient.h"
#include "ECCacheManager.h"
#include "cmdutil.hpp"
#include <kopano/Util.h>
#include "StatsClient.h"
#include "ECIndexer.h"
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <list>
#include <sys/time.h>
#include "ECSessionManager.h"

namespace KC {

/**
 * Returns TRUE if the restriction is always FALSE.
 *
 * Currently this is only detected for the simple case 'AND(EXIST(proptag), NOT(EXIST(proptag)), ...)', with
 * any of the first level subparts in any order.
 *
 * @param lpRestrict[in]
 * @return result
 */

static BOOL NormalizeRestrictionIsFalse(const struct restrictTable *lpRestrict)
{
	std::set<unsigned int> setExist, setNotExist, setBoth;
    if(lpRestrict->ulType != RES_AND)
		return false;

    for (gsoap_size_t i = 0; i < lpRestrict->lpAnd->__size; ++i)
        if (lpRestrict->lpAnd->__ptr[i]->ulType == RES_EXIST)
			setExist.emplace(lpRestrict->lpAnd->__ptr[i]->lpExist->ulPropTag);
        else if (lpRestrict->lpAnd->__ptr[i]->ulType == RES_NOT &&
            lpRestrict->lpAnd->__ptr[i]->lpNot->lpNot->ulType == RES_EXIST)
			setNotExist.emplace(lpRestrict->lpAnd->__ptr[i]->lpNot->lpNot->lpExist->ulPropTag);

    set_intersection(setExist.begin(), setExist.end(), setNotExist.begin(), setNotExist.end(), inserter(setBoth, setBoth.begin()));
	return !setBoth.empty();
}

/**
 * Normalize nested AND clauses in a restriction
 *
 * Recursively normalize nested AND clauses:
 *
 * ((A & B) & C) => (A & B & C)
 *
 * and (recursive)
 *
 * (((A & B) & C) & D) => (A & B & C & D)
 *
 * @param lpRestrict[in,out] Restriction to normalize
 * @return result
 */
static ECRESULT NormalizeRestrictionNestedAnd(struct restrictTable *lpRestrict)
{
    std::list<struct restrictTable *> lstClauses;
	auto und = lpRestrict->lpAnd;
    bool bModified = false;

    if(lpRestrict->ulType != RES_AND)
		return erSuccess;

	/*
	 * There is some raw pointer juggling going on. @bModified also
	 * indicates who of @und or @lstClauses "owns" the subrestrictions. All
	 * the subres end up in @lstClauses, so if bModified is set, @und or
	 * parts thereof must not be recursively deleted, just the tops.
	 */
	for (gsoap_size_t i = 0; i < und->__size; ++i) {
		if (und->__ptr[i]->ulType != RES_AND) {
			lstClauses.emplace_back(und->__ptr[i]);
			/* do not set to nullptr, ownership of rawptrs may still be in flux */
			continue;
		}
		// First, flatten our subchild
		auto ptr = und->__ptr[i];
		auto er = NormalizeRestrictionNestedAnd(ptr);
		if (er != erSuccess)
			return er;
		// Now, get all the clauses from the child AND-clause and push them to this AND-clause
		for (gsoap_size_t j = 0; j < ptr->lpAnd->__size; ++j) {
			lstClauses.emplace_back(ptr->lpAnd->__ptr[j]);
			/* ownership of all rawptrs will change, so nullptr is ok */
			ptr->lpAnd->__ptr[j] = nullptr;
		}
		ptr->lpAnd->__size = 0; /* speed up soap_del */
		soap_del_PointerTorestrictTable(&ptr);
		bModified = true;
    }

	if (!bModified)
		return erSuccess;
	// We changed something, free the previous toplevel data and create a new list of children
	SOAP_FREE(nullptr, und->__ptr);
	und->__ptr = static_cast<restrictTable **>(soap_malloc(nullptr, sizeof(restrictTable *) * lstClauses.size()));
	int n = 0;
	for (const auto rt : lstClauses)
		und->__ptr[n++] = rt;
	und->__size = n;
	return erSuccess;
}

/**
 * Creates a multisearch from and substring search or an OR with substring searches
 *
 * This works for OR restrictions containing CONTENT restrictions with FL_SUBSTRING and FL_IGNORECASE
 * options enabled, or standalone CONTENT restrictions. Nested ORs are also supported as long as all the
 * CONTENT restrictions in an OR (and sub-ORs) contain the same search term.
 *
 * e.g.:
 *
 * 1. (OR (OR (OR (f1: t1), f2: t1, f3: t1 ) ) ) => f1 f2 f3 : t1
 *
 * 2. (OR f1: t1, f2: t2) => FAIL (term mismatch)
 *
 * 3. (OR f1: t1, (AND f2 : t1, f3 : t1 ) ) => FAIL (AND not supported)
 *
 * @param lpRestrict[in] Restriction to parse
 * @param sMultiSearch[out] Found search terms for the entire lpRestrict tree
 * @param setExcludeProps Set of properties that are not indexed (prop id only)
 * @return result
 */
static ECRESULT NormalizeGetMultiSearch(struct restrictTable *lpRestrict,
    const std::set<unsigned int> &setExcludeProps, SIndexedTerm &sMultiSearch)
{
    sMultiSearch.strTerm.clear();
    sMultiSearch.setFields.clear();

    if(lpRestrict->ulType == RES_OR) {
        for (gsoap_size_t i = 0; i < lpRestrict->lpOr->__size; ++i) {
            SIndexedTerm terms;

            if(NormalizeRestrictionIsFalse(lpRestrict->lpOr->__ptr[i]))
                continue;
			auto er = NormalizeGetMultiSearch(lpRestrict->lpOr->__ptr[i], setExcludeProps, terms);
            if (er != erSuccess)
                return er;
			if (sMultiSearch.strTerm.empty())
                // This is the first term, copy it
                sMultiSearch = std::move(terms);
			else if (sMultiSearch.strTerm == terms.strTerm)
				// Add the search fields from the subrestriction into ours
				sMultiSearch.setFields.insert(std::make_move_iterator(terms.setFields.begin()), std::make_move_iterator(terms.setFields.end()));
			else
				// There are different search terms in this OR (case 2)
				return KCERR_INVALID_PARAMETER;
        }
    } else if(lpRestrict->ulType == RES_CONTENT && (lpRestrict->lpContent->ulFuzzyLevel & (FL_SUBSTRING | FL_IGNORECASE)) == (FL_SUBSTRING | FL_IGNORECASE)) {
		if (setExcludeProps.find(PROP_ID(lpRestrict->lpContent->ulPropTag)) != setExcludeProps.end())
			// The property cannot be searched from the indexer since it has been excluded from indexing
			return KCERR_NOT_FOUND;
		// Only support looking for string-type properties
		if (PROP_TYPE(lpRestrict->lpContent->lpProp->ulPropTag) != PT_STRING8 &&
		    PROP_TYPE(lpRestrict->lpContent->lpProp->ulPropTag) != PT_UNICODE)
			return KCERR_INVALID_PARAMETER;
        sMultiSearch.strTerm = lpRestrict->lpContent->lpProp->Value.lpszA;
		sMultiSearch.setFields.emplace(PROP_ID(lpRestrict->lpContent->ulPropTag));
    } else {
        // Some other restriction type, unsupported (case 3)
        return KCERR_INVALID_PARAMETER;
    }
	return erSuccess;
}

/**
 * Normalizes the given restriction to a multi-field text search and the rest of the restriction
 *
 * Given a restriction R, modifies the restriction R and returns multi-field search terms so that the intersection
 * of the multi-field search and the new restriction R' produces the same result as restriction R
 *
 * Currently, this only works for restrictions that have the following structure (other restrictions are left untouched,
 * R' = R):
 *
 * AND(
 *  ...
 *  OR(f1: t1, ..., fN: t1)
 *  ...
 *  OR(f1: t2, ..., fN: t2)
 *  ... other restriction parts
 * )
 *
 * in which 'f: t' represents a RES_CONTENT FL_SUBSTRING search (other match types are skipped)
 *
 * In this case the output will be:
 *
 * AND(
 *  ...
 *  ...
 * )
 * +
 * multisearch: f1 .. fN : t1 .. tN
 *
 * (eg subject body from: word1 word2)
 *
 * If there are multiple OR clauses inside the initial AND clause, and the search fields DIFFER, then the FIRST 'OR'
 * field is used for the multifield search.
 */
static ECRESULT NormalizeRestrictionMultiFieldSearch(
    struct restrictTable *lpRestrict,
    const std::set<unsigned int> &setExcludeProps,
    std::list<SIndexedTerm> *lpMultiSearches)
{
    SIndexedTerm sMultiSearch;

    lpMultiSearches->clear();

    if (lpRestrict->ulType == RES_AND) {
        for (gsoap_size_t i = 0; i < lpRestrict->lpAnd->__size;) {
            if (NormalizeGetMultiSearch(lpRestrict->lpAnd->__ptr[i], setExcludeProps, sMultiSearch) != erSuccess) {
	        ++i;
	        continue;
	    }
			lpMultiSearches->emplace_back(sMultiSearch);
            // Remove it from the restriction since it is now handled as a multisearch
			soap_del_PointerTorestrictTable(&lpRestrict->lpAnd->__ptr[i]);
            memmove(&lpRestrict->lpAnd->__ptr[i], &lpRestrict->lpAnd->__ptr[i+1], sizeof(struct restrictTable *) * (lpRestrict->lpAnd->__size - i - 1));
            --lpRestrict->lpAnd->__size;
        }
    } else if (NormalizeGetMultiSearch(lpRestrict, setExcludeProps, sMultiSearch) == erSuccess) {
        // Direct RES_CONTENT
		lpMultiSearches->emplace_back(sMultiSearch);

        // We now have to remove the entire restriction since the top-level restriction here is
        // now obsolete. Since the above loop will generate an empty AND clause, we will do that here as well.
	// Do not delete the lpRestrict itself, since we place new content in it.
		soap_del_restrictTable(lpRestrict);
		soap_default_restrictTable(nullptr, lpRestrict);
        lpRestrict->ulType = RES_AND;
		lpRestrict->lpAnd = soap_new_restrictAnd(nullptr);
    }
	return erSuccess;
}

/**
 * Process the given restriction, and convert into multi-field searches and a restriction
 *
 * This function attempts create a multi-field search and restriction in such a way that
 * as much as possible multi-field searches are returned without changing the result of the
 * restriction.
 *
 * This is done by applying the following transformations:
 *
 * - Flatten nested ANDs recursively
 * - Remove always-false terms in ORs in the top level
 * - Derive multi-field searches from top-level AND clause (from OR clauses with substring searches, or direct substring searches)
 */

static ECRESULT NormalizeGetOptimalMultiFieldSearch(
    struct restrictTable *lpRestrict,
    const std::set<unsigned int> &setExcludeProps,
    std::list<SIndexedTerm> *lpMultiSearches)
{
    // Normalize nested ANDs, if any
	auto er = NormalizeRestrictionNestedAnd(lpRestrict);
    if (er != erSuccess)
		return er;
	/* Convert a series of ANDs or a single text search into a new restriction and the multisearch terms. */
	return NormalizeRestrictionMultiFieldSearch(lpRestrict, setExcludeProps, lpMultiSearches);
}

/**
 * Try to run the restriction using the indexer instead of slow
 * database queries. Will fail if the restriction is unable to run by
 * the indexer.
 *
 * @param[in] lpConfig config object
 * @param[in] lpLogger log object
 * @param[in] lpCacheManager cachemanager object
 * @param[in] guidServer current server guid
 * @param[in] ulStoreId store id to search in
 * @param[in] lstFolders list of folders to search in
 * @param[in] lpRestrict restriction to search against
 * @param[out] lppNewRestrict restriction that should be applied to lppIndexerResults (part of the restriction that could not be handled by the indexer).
 *							  May be NULL if no restriction is needed (all results in lppIndexerResults match the original restriction)
 * @param[out] lppIndexerResults results found by indexer
 *
 * @return Kopano error code
 */
ECRESULT GetIndexerResults(ECDatabase *lpDatabase, ECConfig *lpConfig,
    ECCacheManager *lpCacheManager, GUID *guidServer, GUID *guidStore,
    ECListInt &lstFolders, struct restrictTable *lpRestrict,
    struct restrictTable **lppNewRestrict, std::list<unsigned int> &lstMatches,
    std::string &suggestion)
{
    ECRESULT er = erSuccess;
	std::unique_ptr<ECSearchClient> lpSearchClient;
	std::set<unsigned int> setExcludePropTags;
	KC::time_point tstart;
	LONGLONG llelapsedtime;
	struct restrictTable *lpOptimizedRestrict = NULL;
	std::list<SIndexedTerm> lstMultiSearches;
	const char* szSocket = lpConfig->GetSetting("search_socket");

	auto laters = make_scope_success([&]() {
		soap_del_PointerTorestrictTable(&lpOptimizedRestrict);
		if (er != erSuccess)
			g_lpSessionManager->m_stats->inc(SCN_DATABASE_SEARCHES);
		else
			g_lpSessionManager->m_stats->inc(SCN_INDEXED_SEARCHES);
	});

	if (!lpDatabase) {
		ec_log_err("GetIndexerResults(): no database");
		return KCERR_DATABASE_ERROR;
	}
	lstMatches.clear();
	auto stype = lpConfig->GetSetting("search_enabled");
	if (stype != nullptr && strcmp(stype, "internal") == 0)
		lpSearchClient.reset(new(std::nothrow) ECSearchClientMM);
	else if (!parseBool(stype) || szSocket[0] == '\0')
		return KCERR_NOT_FOUND;
	else
		lpSearchClient.reset(new(std::nothrow) ECSearchClientNET(szSocket, atoui(lpConfig->GetSetting("search_timeout"))));
	if (!lpSearchClient)
		return KCERR_NOT_ENOUGH_MEMORY;

	if (lpCacheManager->GetExcludedIndexProperties(setExcludePropTags) != erSuccess) {
		er = lpSearchClient->GetProperties(setExcludePropTags);
		if (er == KCERR_NETWORK_ERROR)
			ec_log_err("Error while connecting to search on \"%s\"", szSocket);
		else if (er != erSuccess)
			ec_log_err("Error while querying search on \"%s\": %s (%x)",
				szSocket, GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
		if (er != erSuccess)
			return er;
		er = lpCacheManager->SetExcludedIndexProperties(setExcludePropTags);
		if (er != erSuccess)
			return er;
	}

	er = CopyRestrictTable(NULL, lpRestrict, &lpOptimizedRestrict);
	if (er != erSuccess)
		return er;
	er = NormalizeGetOptimalMultiFieldSearch(lpOptimizedRestrict, setExcludePropTags, &lstMultiSearches);
	if (er != erSuccess)
		return er; // Note this will happen if the restriction cannot be handled by the indexer
	if (lstMultiSearches.empty())
		// Although the restriction was strictly speaking indexer-compatible, no index queries could
		// be found, so bail out
		return KCERR_NOT_FOUND;

	ec_log_debug("Using index, %zu index queries", lstMultiSearches.size());
	tstart = decltype(tstart)::clock::now();
	er = lpSearchClient->Query(guidServer, guidStore, lstFolders, lstMultiSearches, lstMatches, suggestion);
	llelapsedtime = std::chrono::duration_cast<std::chrono::microseconds>(decltype(tstart)::clock::now() - tstart).count();
	g_lpSessionManager->m_stats->Max(SCN_INDEXER_SEARCH_MAX, llelapsedtime);
	g_lpSessionManager->m_stats->avg(SCN_INDEXER_SEARCH_AVG, llelapsedtime);

	if (er != erSuccess) {
		g_lpSessionManager->m_stats->inc(SCN_INDEXER_SEARCH_ERRORS);
		ec_log_err("Error while querying search on \"%s\": %s (%x)",
			szSocket, GetMAPIErrorMessage(kcerr_to_mapierr(er)), er);
	} else
		ec_log_debug("Indexed query results found in %u ms", static_cast<unsigned int>(llelapsedtime));

	ec_log_debug("%zu indexed matches found", lstMatches.size());
	*lppNewRestrict = lpOptimizedRestrict;
    lpOptimizedRestrict = NULL;
    return er;
}

} /* namespace */
