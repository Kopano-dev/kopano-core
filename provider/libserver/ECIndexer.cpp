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

#include <kopano/CommonUtil.h>
#include "ECGenericObjectTable.h"
#include <kopano/stringutil.h>
#include "ECSearchClient.h"
#include "ECCacheManager.h"
#include "SOAPDebug.h"
#include "cmdutil.hpp"
#include <kopano/Util.h>
#include "ECStatsCollector.h"
#include "ECIndexer.h"
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <list>
#include <sys/time.h>

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
    std::set<unsigned int> setExist;
    std::set<unsigned int> setNotExist;
    std::set<unsigned int> setBoth;
    
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
    bool bModified = false;
    
    if(lpRestrict->ulType != RES_AND)
		return erSuccess;
        
    for (gsoap_size_t i = 0; i < lpRestrict->lpAnd->__size; ++i) {
		if (lpRestrict->lpAnd->__ptr[i]->ulType != RES_AND) {
			lstClauses.emplace_back(lpRestrict->lpAnd->__ptr[i]);
			continue;
		}
		// First, flatten our subchild
		auto er = NormalizeRestrictionNestedAnd(lpRestrict->lpAnd->__ptr[i]);
		if (er != erSuccess)
			return er;
		// Now, get all the clauses from the child AND-clause and push them to this AND-clause
		for (gsoap_size_t j = 0; j < lpRestrict->lpAnd->__ptr[i]->lpAnd->__size; ++j)
			lstClauses.emplace_back(lpRestrict->lpAnd->__ptr[i]->lpAnd->__ptr[j]);
		s_free(nullptr, lpRestrict->lpAnd->__ptr[i]->lpAnd->__ptr);
		s_free(nullptr, lpRestrict->lpAnd->__ptr[i]->lpAnd);
		s_free(nullptr, lpRestrict->lpAnd->__ptr[i]);
		bModified = true;
    }
        
	if (!bModified)
		return erSuccess;
	// We changed something, free the previous toplevel data and create a new list of children
	s_free(nullptr, lpRestrict->lpAnd->__ptr);
	lpRestrict->lpAnd->__ptr = s_alloc<restrictTable *>(NULL, lstClauses.size());
	int n = 0;
	for (const auto rt : lstClauses)
		lpRestrict->lpAnd->__ptr[n++] = rt;
	lpRestrict->lpAnd->__size = n;
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
				sMultiSearch.setFields.insert(gcc5_make_move_iterator(terms.setFields.begin()), gcc5_make_move_iterator(terms.setFields.end()));
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
            FreeRestrictTable(lpRestrict->lpAnd->__ptr[i]);
            memmove(&lpRestrict->lpAnd->__ptr[i], &lpRestrict->lpAnd->__ptr[i+1], sizeof(struct restrictTable *) * (lpRestrict->lpAnd->__size - i - 1));
            --lpRestrict->lpAnd->__size;
        }
    } else if (NormalizeGetMultiSearch(lpRestrict, setExcludeProps, sMultiSearch) == erSuccess) {
        // Direct RES_CONTENT
		lpMultiSearches->emplace_back(sMultiSearch);

        // We now have to remove the entire restriction since the top-level restriction here is
        // now obsolete. Since the above loop will generate an empty AND clause, we will do that here as well.
	// Do not delete the lpRestrict itself, since we place new content in it.
	FreeRestrictTable(lpRestrict, false);
	memset(lpRestrict, 0, sizeof(struct restrictTable));
        lpRestrict->ulType = RES_AND;
		lpRestrict->lpAnd = s_alloc<restrictAnd>(nullptr);
        lpRestrict->lpAnd->__size = 0;
        lpRestrict->lpAnd->__ptr = NULL;
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
	LONGLONG	llelapsedtime;
	struct restrictTable *lpOptimizedRestrict = NULL;
	std::list<SIndexedTerm> lstMultiSearches;
	const char* szSocket = lpConfig->GetSetting("search_socket");

	if (!lpDatabase) {
		er = KCERR_DATABASE_ERROR;
                ec_log_err("GetIndexerResults(): no database");
		goto exit;
	}
	
	lstMatches.clear();

	if (!parseBool(lpConfig->GetSetting("search_enabled")) || szSocket[0] == '\0') {
		er = KCERR_NOT_FOUND;
		goto exit;
	}
	lpSearchClient.reset(new(std::nothrow) ECSearchClient(szSocket, atoui(lpConfig->GetSetting("search_timeout"))));
	if (!lpSearchClient) {
		er = KCERR_NOT_ENOUGH_MEMORY;
		goto exit;
	}
	if (lpCacheManager->GetExcludedIndexProperties(setExcludePropTags) != erSuccess) {
		er = lpSearchClient->GetProperties(setExcludePropTags);
		if (er == KCERR_NETWORK_ERROR)
			ec_log_err("Error while connecting to search on \"%s\"", szSocket);
		else if (er != erSuccess)
			ec_log_err("Error while querying search on \"%s\", 0x%08x", szSocket, er);
		if (er != erSuccess)
			goto exit;
		er = lpCacheManager->SetExcludedIndexProperties(setExcludePropTags);
		if (er != erSuccess)
			goto exit;
	}

	er = CopyRestrictTable(NULL, lpRestrict, &lpOptimizedRestrict);
	if (er != erSuccess)
		goto exit;
	er = NormalizeGetOptimalMultiFieldSearch(lpOptimizedRestrict, setExcludePropTags, &lstMultiSearches);
	if (er != erSuccess)
		goto exit; // Note this will happen if the restriction cannot be handled by the indexer
	if (lstMultiSearches.empty()) {
		// Although the restriction was strictly speaking indexer-compatible, no index queries could
		// be found, so bail out
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	ec_log_debug("Using index, %zu index queries", lstMultiSearches.size());
	tstart = decltype(tstart)::clock::now();
	er = lpSearchClient->Query(guidServer, guidStore, lstFolders, lstMultiSearches, lstMatches, suggestion);
	llelapsedtime = std::chrono::duration_cast<std::chrono::milliseconds>(decltype(tstart)::clock::now() - tstart).count();
	g_lpStatsCollector->Max(SCN_INDEXER_SEARCH_MAX, llelapsedtime);
	g_lpStatsCollector->Avg(SCN_INDEXER_SEARCH_AVG, llelapsedtime);

	if (er != erSuccess) {
		g_lpStatsCollector->Increment(SCN_INDEXER_SEARCH_ERRORS);
		ec_log_err("Error while querying search on \"%s\", 0x%08x", szSocket, er);
	} else
		ec_log_debug("Indexed query results found in %.4f ms", llelapsedtime/1000.0);

	ec_log_debug("%zu indexed matches found", lstMatches.size());
	*lppNewRestrict = lpOptimizedRestrict;
    lpOptimizedRestrict = NULL;
    
exit:
	if (lpOptimizedRestrict != NULL)
		FreeRestrictTable(lpOptimizedRestrict);
	if (er != erSuccess)
		g_lpStatsCollector->Increment(SCN_DATABASE_SEARCHES);
	else
		g_lpStatsCollector->Increment(SCN_INDEXED_SEARCHES);
    
    return er;
}

} /* namespace */
