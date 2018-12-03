/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <algorithm>
#include <new>
#include <string>
#include <kopano/platform.h>
#include <kopano/scope.hpp>

/* Returns the rows for a contents- or hierarchytable
 *
 * objtype == MAPI_MESSAGE, then contents table
 * objtype == MAPI_MESSAGE, flags == MAPI_ASSOCIATED, then associated contents table
 * objtype == MAPI_FOLDER, then hierarchy table
 *
 * Tables are generated from SQL in the following way:
 *
 * Tables are constructed by joining the hierarchy table with the property table multiple
 * times, once for each requested property (column). Because each column of each row can always have
 * only one or zero records, a unique index is created on the property table, indexing (hierarchyid, type, tag).
 *
 * This means that for each cell that we request, the index needs to be accessed by the SQL
 * engine only once, which makes the actual query extremely fast.
 *
 * In tests, this has shown to required around 60ms for 30 rows and 10 columns from a table of 10000
 * rows. Also, this is a O(n log n) operation and therefore not prone to large scaling problems. (Yay!)
 * (with respect to the amount of columns, it is O(n), but that's quite constant, and so is the
 * actual amount of rows requested per query (also O(n)).
 *
 */
#include "soapH.h"
#include <kopano/kcodes.h>
#include <mapidefs.h>
#include <mapitags.h>
#include <edkmdb.h>
#include <kopano/mapiext.h>
#include "kcore.hpp"
#include "ECDatabaseUtils.h"
#include <kopano/ECKeyTable.h>
#include <kopano/Util.h>
#include "ECGenProps.h"
#include "ECStoreObjectTable.h"
#include "StatsClient.h"
#include "ECSecurity.h"
#include "ECIndexer.h"
#include "ECSearchClient.h"
#include "ECTPropsPurge.h"
#include "SOAPUtils.h"
#include <kopano/stringutil.h>
#include <kopano/charset/utf8string.h>
#include <kopano/charset/convert.h>
#include "ECSessionManager.h"
#include "ECSession.h"
#include <map>

namespace KC {

static bool IsTruncatableType(unsigned int ulTag)
{
    switch(PROP_TYPE(ulTag)) {
        case PT_STRING8:
        case PT_UNICODE:
        case PT_BINARY:
            return true;
        default:
            return false;
    }

    return false;
}

static bool IsTruncated(const struct propVal *lpsPropVal)
{
	if(!IsTruncatableType(lpsPropVal->ulPropTag))
		return false;

	switch(PROP_TYPE(lpsPropVal->ulPropTag)) {
	case PT_STRING8:
	case PT_UNICODE:
		return u8_len(lpsPropVal->Value.lpszA) == TABLE_CAP_STRING;
	case PT_BINARY:
		// previously we capped on 255 bytes, upgraded to 511.
		return lpsPropVal->Value.bin->__size == TABLE_CAP_STRING || lpsPropVal->Value.bin->__size == TABLE_CAP_BINARY;
	}

	return false;
}

ECStoreObjectTable::ECStoreObjectTable(ECSession *ses,
    unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId,
    unsigned int ulObjType, unsigned int ulFlags, unsigned int ulTableFlags,
    const ECLocale &locale) :
	ECGenericObjectTable(ses, ulObjType, ulFlags, locale)
{
	auto lpODStore = new ECODStore;
	lpODStore->ulStoreId = ulStoreId;
	lpODStore->ulFolderId = ulFolderId;
	lpODStore->ulObjType = ulObjType;
	lpODStore->ulFlags = ulFlags;
	lpODStore->ulTableFlags = ulTableFlags;

	if(lpGuid) {
		lpODStore->lpGuid = new GUID;
		*lpODStore->lpGuid = *lpGuid;
	} else {
		lpODStore->lpGuid = NULL; // When NULL is specified, we get the store ID & guid for each separate row
	}

	// Set dataobject
	m_lpObjectData = lpODStore;

	// Set callback function for queryrowdata
	m_lpfnQueryRowData = QueryRowData;
}

ECStoreObjectTable::~ECStoreObjectTable()
{
	if (m_lpObjectData == nullptr)
		return;
	auto lpODStore = static_cast<ECODStore *>(const_cast<void *>(m_lpObjectData));
	delete lpODStore->lpGuid;
	delete lpODStore;
}

ECRESULT ECStoreObjectTable::Create(ECSession *lpSession, unsigned int ulStoreId, GUID *lpGuid, unsigned int ulFolderId, unsigned int ulObjType, unsigned int ulFlags, unsigned int ulTableFlags, const ECLocale &locale, ECStoreObjectTable **lppTable)
{
	return alloc_wrap<ECStoreObjectTable>(lpSession, ulStoreId, lpGuid,
	       ulFolderId, ulObjType, ulFlags, ulTableFlags, locale).put(lppTable);
}

ECRESULT ECStoreObjectTable::GetColumnsAll(ECListInt* lplstProps)
{
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow = NULL;
	std::string		strQuery;
	ECDatabase*		lpDatabase = NULL;
	auto lpODStore = static_cast<const ECODStore *>(m_lpObjectData);
	ULONG			ulPropID = 0;

	assert(lplstProps != NULL);
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	//List always emtpy
	lplstProps->clear();
	ulock_rec biglock(m_hLock);
	bool mo_has_content = !mapObjects.empty();
	biglock.unlock();

	if (mo_has_content && lpODStore->ulFolderId != 0) {
		// Properties
		strQuery = "SELECT DISTINCT tproperties.tag, tproperties.type FROM tproperties WHERE folderid = " + stringify(lpODStore->ulFolderId);

		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			return er;
		// Put the results into a STL list
		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
			if(lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL)
				continue;

			ulPropID = atoi(lpDBRow[0]);
			lplstProps->emplace_back(PROP_TAG(atoi(lpDBRow[1]), ulPropID));
		}
	}

	// Add some generated and standard properties
	lplstProps->emplace_back(PR_ENTRYID);
	lplstProps->emplace_back(PR_INSTANCE_KEY);
	lplstProps->emplace_back(PR_RECORD_KEY);
	lplstProps->emplace_back(PR_OBJECT_TYPE);
	lplstProps->emplace_back(PR_LAST_MODIFICATION_TIME);
	lplstProps->emplace_back(PR_CREATION_TIME);
	lplstProps->emplace_back(PR_PARENT_ENTRYID);
	lplstProps->emplace_back(PR_MAPPING_SIGNATURE);

	// Add properties only in the contents tables
	if(lpODStore->ulObjType == MAPI_MESSAGE && (lpODStore->ulFlags&MSGFLAG_ASSOCIATED) == 0)
		lplstProps->emplace_back(PR_MSG_STATUS);
	lplstProps->emplace_back(PR_ACCESS_LEVEL);

	//FIXME: only in folder or message table
	lplstProps->emplace_back(PR_ACCESS);
	return er;
}

ECRESULT ECStoreObjectTable::ReloadTableMVData(ECObjectTableList* lplistRows, ECListInt* lplistMVPropTag)
{
	DB_RESULT lpDBResult;
	ECDatabase*			lpDatabase = NULL;
	sObjectTableKey		sRowItem;

	ECRESULT er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	assert(lplistMVPropTag->size() < 2); //FIXME: Limit of one 1 MV column
	// scan for MV-props and add rows
	std::string strQuery = "SELECT h.id, orderid FROM hierarchy as h";
	int j = 0;
	for (auto proptag : *lplistMVPropTag) {
		std::string strColName = "col" + stringify(proptag);
		strQuery += " LEFT JOIN mvproperties as " + strColName + " ON h.id=" +
			strColName + ".hierarchyid AND " + strColName +
			".tag=" + stringify(PROP_ID(proptag)) + " AND " +
			strColName + ".type=" +
			stringify(PROP_TYPE(NormalizeDBPropTag(proptag) & ~MV_INSTANCE));
		++j;
		break; //FIXME: limit 1 column, multi MV cols
	} // for iterListMVPropTag
		
	strQuery += " WHERE h.id IN(" +
		kc_join(*lplistRows, ",", [](const auto &row) { return stringify(row.ulObjId); }) +
		") ORDER BY id, orderid";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;

	while(1)
	{
		auto lpDBRow = lpDBResult.fetch_row();
		if(lpDBRow == NULL)
			break;

		sRowItem.ulObjId = atoui(lpDBRow[0]);
		sRowItem.ulOrderId = (lpDBRow[1] == NULL)? 0 : atoi(lpDBRow[1]);

		if(sRowItem.ulOrderId > 0)
			lplistRows->emplace_back(sRowItem);
	}
	return erSuccess;
}

// Interface to main row engine (bSubObjects is false)
ECRESULT ECStoreObjectTable::QueryRowData(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	return ECStoreObjectTable::QueryRowData(lpThis, soap, lpSession, lpRowList, lpsPropTagArray, lpObjectData, lppRowSet, bCacheTableData, bTableLimit, false);
}

// Direct interface
ECRESULT ECStoreObjectTable::QueryRowData(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit,
    bool bSubObjects)
{
	gsoap_size_t i = 0, k = 0;
	unsigned int ulFolderId, ulRowStoreId = 0;
	GUID			sRowGuid;
	auto lpODStore = static_cast<const ECODStore *>(lpObjectData);
	ECDatabase		*lpDatabase = NULL;

	std::map<unsigned int, std::map<sObjectTableKey, unsigned int> > mapStoreIdObjIds;
	std::map<sObjectTableKey, unsigned int> mapIncompleteRows, mapRows;
	std::multimap<unsigned int, unsigned int> mapColumns;
	std::list<unsigned int> lstDeferred, propList;
	std::set<unsigned int> setColumnIDs;
	ECObjectTableList lstRowOrder;
	std::map<sObjectTableKey, ECsObjects> mapObjects;

	ECListInt			listMVSortCols;//Other mvprops then normal column set
	ECListInt			listMVIColIds;
	std::string strCol;
    sObjectTableKey sKey;

	std::set<std::pair<unsigned int, unsigned int> > setCellDone;

	assert(lpRowList != NULL);
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	auto cache = lpSession->GetSessionManager()->GetCacheManager();
	auto lpsRowSet = s_alloc<rowSet>(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;
	auto cleanup = make_scope_success([&] {
		if (soap == nullptr)
			FreeRowSet(lpsRowSet, true);
	});

	if (lpRowList == nullptr || lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		lpsRowSet = NULL;
		return erSuccess;
	}

    if (lpODStore->ulFlags & EC_TABLE_NOCAP)
            bTableLimit = false;

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpsRowSet->__size);
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpsRowSet->__size);

	// Allocate memory for all rows
	for (i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr = s_alloc<propVal>(soap, lpsPropTagArray->__size);
		memset(lpsRowSet->__ptr[i].__ptr, 0, sizeof(propVal) * lpsPropTagArray->__size);
	}

	// Scan cache for anything that we can find, and generate any properties that don't come from normal database queries.
	i = 0;
	for (const auto &row : *lpRowList) {
	    bool bRowComplete = true;

	    for (k = 0; k < lpsPropTagArray->__size; ++k) {
	    	unsigned int ulPropTag;

			if (row.ulObjId == 0 || ECGenProps::GetPropSubstitute(lpODStore->ulObjType, lpsPropTagArray->__ptr[k], &ulPropTag) != erSuccess)
	    		ulPropTag = lpsPropTagArray->__ptr[k];

            // Get StoreId if needed
            if (lpODStore->lpGuid == NULL)
                // No store specified, so determine the store ID & guid from the object id
				cache->GetStore(row.ulObjId, &ulRowStoreId, &sRowGuid);
            else
                ulRowStoreId = lpODStore->ulStoreId;

            // Handle category header rows
            if (row.ulObjId == 0) {
		if (lpThis->GetPropCategory(soap, lpsPropTagArray->__ptr[k], row, &lpsRowSet->__ptr[i].__ptr[k]) != erSuccess) {
            		// Other properties are not found
            		lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
            		lpsRowSet->__ptr[i].__ptr[k].Value.ul = KCERR_NOT_FOUND;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_ERROR);
            	}
				setCellDone.emplace(i, k);
            	continue;
            }

			if (ECGenProps::IsPropComputedUncached(ulPropTag, lpODStore->ulObjType) == erSuccess) {
				if (ECGenProps::GetPropComputedUncached(soap, lpODStore, lpSession, ulPropTag, row.ulObjId, row.ulOrderId, ulRowStoreId, lpODStore->ulFolderId, lpODStore->ulObjType, &lpsRowSet->__ptr[i].__ptr[k]) != erSuccess)
					CopyEmptyCellToSOAPPropVal(soap, ulPropTag, &lpsRowSet->__ptr[i].__ptr[k]);
				setCellDone.emplace(i, k);
				continue;
			}

			// Handle PR_DEPTH
			if(ulPropTag == PR_DEPTH) {
				if (lpThis->GetComputedDepth(soap, lpSession, row.ulObjId, &lpsRowSet->__ptr[i].__ptr[k]) != erSuccess) {
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = PR_DEPTH;
					lpsRowSet->__ptr[i].__ptr[k].Value.ul = 0;
				}
				setCellDone.emplace(i, k);
				continue;
			}

    	    if(ulPropTag == PR_PARENT_DISPLAY_A || ulPropTag == PR_PARENT_DISPLAY_W || ulPropTag == PR_EC_OUTGOING_FLAGS || ulPropTag == PR_EC_PARENT_HIERARCHYID || ulPropTag == PR_ASSOCIATED) {
    	    	bRowComplete = false;
    	    	continue; // These are not in cache, even if cache is complete for an item.
			}

			if ((ulPropTag & MVI_FLAG) == MVI_FLAG) {
				bRowComplete = false;
				continue;
			}
    	    // FIXME bComputed always false
    	    // FIXME optimisation possible to GetCell: much more efficient to get all cells in one row at once
			if (cache->GetCell(&row, ulPropTag, &lpsRowSet->__ptr[i].__ptr[k], soap, false) == erSuccess &&
			    PROP_TYPE(lpsRowSet->__ptr[i].__ptr[k].ulPropTag) != PT_NULL) {
				setCellDone.emplace(i, k);
	            continue;
			}

			// None of the property generation tactics helped us, we need to get at least something from the database for this row.
            bRowComplete = false;
        }

        // Remember if we didn't have all cells in this row in the cache, and remember the row number
		if (!bRowComplete)
	        mapIncompleteRows[row] = i;
		++i;
	}

	if(!bSubObjects) {
		// Query from contents or hierarchy table, only do row-order processing for rows that have to be done via the row engine

		if(!mapIncompleteRows.empty()) {
			// Find rows that are in the deferred processing queue, and we have rows to be processed
			if (lpODStore->ulFolderId)
				er = GetDeferredTableUpdates(lpDatabase, lpODStore->ulFolderId, &lstDeferred);
			else
				er = GetDeferredTableUpdates(lpDatabase, lpRowList, &lstDeferred);
			if(er != erSuccess)
				return er;

			// Build list of rows that are incomplete (not in cache) AND deferred
			for (auto dfr : lstDeferred) {
				sKey.ulObjId = dfr;
				sKey.ulOrderId = 0;
				for (auto iterIncomplete = mapIncompleteRows.lower_bound(sKey);
				     iterIncomplete != mapIncompleteRows.cend() && iterIncomplete->first.ulObjId == dfr;
				     ++iterIncomplete) {
					g_lpSessionManager->m_stats->inc(SCN_DATABASE_DEFERRED_FETCHES);
					mapRows[iterIncomplete->first] = iterIncomplete->second;
				}
			}
		}
    } else {
		// Do row-order query for all incomplete rows
        mapRows = mapIncompleteRows;
    }

    if(!mapRows.empty()) {
        // Get rows from row order engine
        for (const auto &rowp : mapRows) {
            mapColumns.clear();

            // Find out which columns we need
            for (k = 0; k < lpsPropTagArray->__size; ++k) {
				if (setCellDone.count({rowp.second, k}) != 0)
					continue;
				// Not done yet, remember that we need to get this column
				unsigned int ulPropTag;
				if (ECGenProps::GetPropSubstitute(lpODStore->ulObjType, lpsPropTagArray->__ptr[k], &ulPropTag) != erSuccess)
					ulPropTag = lpsPropTagArray->__ptr[k];
				mapColumns.emplace(ulPropTag, k);
				setCellDone.emplace(rowp.second, k); // Done now
            }

            // Get actual data
            er = QueryRowDataByRow(lpThis, soap, lpSession, const_cast<sObjectTableKey &>(rowp.first), rowp.second, mapColumns, bTableLimit, lpsRowSet);
            if(er != erSuccess)
				return er;
        }
    }

    if(setCellDone.size() != (unsigned int)i*k) {
        // Some cells are not done yet, do them in column-order.
       	mapStoreIdObjIds.clear();

       	// Get parent info if needed
		if(lpODStore->ulFolderId == 0)
			cache->GetObjects(*lpRowList, mapObjects);

        // Split requests into same-folder blocks
        for (k = 0; k < lpsPropTagArray->__size; ++k) {
			i = 0;
			for (const auto &row : *lpRowList) {
				if (setCellDone.count({i, k}) != 0) {
					++i;
					continue; /* already done */
				}

            	// Remember that there was some data int this column that we need to get
				setColumnIDs.emplace(k);
                if(lpODStore->ulFolderId == 0) {
					auto iterObjects = mapObjects.find(row);
					if (iterObjects != mapObjects.cend())
                		ulFolderId = iterObjects->second.ulParent;
					else
                    	/* This will cause the request to fail, since no items are in folder id 0. However, this is what we want since
                    	 * the only thing we can do is return NOT_FOUND for each cell.*/
						ulFolderId = 0;
                } else {
                	ulFolderId = lpODStore->ulFolderId;
                }
                mapStoreIdObjIds[ulFolderId][row] = i;
				setCellDone.emplace(i, k);
                ++i;
            }
        }

        // FIXME could fill setColumns per set of items instead of the whole table request

        mapColumns.clear();
        // Convert the set of column IDs to a map from property tag to column ID
		for (auto col_id : setColumnIDs) {
        	unsigned int ulPropTag;

			if (ECGenProps::GetPropSubstitute(lpODStore->ulObjType, lpsPropTagArray->__ptr[col_id], &ulPropTag) != erSuccess)
				ulPropTag = lpsPropTagArray->__ptr[col_id];
			mapColumns.emplace(ulPropTag, col_id);
        }
		for (const auto &sio : mapStoreIdObjIds)
			er = QueryRowDataByColumn(lpThis, soap, lpSession, mapColumns, sio.first, sio.second, lpsRowSet);
    }

    if(!bTableLimit) {
    	/* If no table limit was specified (so entire string requested, not just < 255 bytes), we have to do some more processing:
    	 *
    	 * - Check each column to see if it is truncatable at all (only string and binary columns are truncatable)
    	 * - Check each output value that we have already retrieved to see if it was truncated (value may have come from cache or column engine)
    	 * - Get any additional data via QueryRowDataByRow() if needed since that is the only method to get > 255 bytes
    	 */
		for (k = 0; k < lpsPropTagArray->__size; ++k) {
			if (!IsTruncatableType(lpsPropTagArray->__ptr[k]))
				continue;
			i = 0;
			for (const auto &row : *lpRowList) {
				if (!IsTruncated(&lpsRowSet->__ptr[i].__ptr[k])) {
					++i;
					continue;
				}
				// We only want one column in this row
				mapColumns.clear();
				mapColumns.emplace(lpsPropTagArray->__ptr[k], k);
				// Un-truncate this value
				er = QueryRowDataByRow(lpThis, soap, lpSession, row, i, mapColumns, false, lpsRowSet);
				if (er != erSuccess)
					return er;
				++i;
			}
		}
    }

	for (k = 0; k < lpsPropTagArray->__size; ++k) {
		// Do any post-processing operations
		if (ECGenProps::IsPropComputed(lpsPropTagArray->__ptr[k],
		    lpODStore->ulObjType) != erSuccess)
			continue;
		i = 0;
		for (const auto &row : *lpRowList) {
			if (row.ulObjId == 0) {
				++i;
				continue;
			}
			// Do not change category values!
			ECGenProps::GetPropComputed(soap, lpODStore->ulObjType,
				lpsPropTagArray->__ptr[k], row.ulObjId,
				&lpsRowSet->__ptr[i].__ptr[k]);
			++i;
		}
	}

    *lppRowSet = lpsRowSet;
	lpsRowSet = NULL;
    return er;
}

ECRESULT ECStoreObjectTable::QueryRowDataByRow(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession, const sObjectTableKey &sKey,
    unsigned int ulRowNum,
    std::multimap<unsigned int, unsigned int> &mapColumns, bool bTableLimit,
    struct rowSet *lpsRowSet)
{
	DB_RESULT lpDBResult;
	DB_ROW			lpDBRow = NULL;
	std::string strQuery, strSubQuery, strCol;
	ECDatabase		*lpDatabase = NULL;
	bool bNeedProps = false, bNeedMVProps = false;
	bool bNeedMVIProps = false, bNeedSubQueries = false;
    std::set<unsigned int> setSubQueries;
	// Select correct property column query according to whether we want to truncate or not
	std::string strPropColOrder = bTableLimit ? PROPCOLORDER_TRUNCATED : PROPCOLORDER;
	std::string strMVIPropColOrder = bTableLimit ? MVIPROPCOLORDER_TRUNCATED : MVIPROPCOLORDER;

	assert(lpsRowSet != NULL);
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	g_lpSessionManager->m_stats->inc(SCN_DATABASE_ROW_READS);
	auto cache = lpSession->GetSessionManager()->GetCacheManager();

    for (const auto &col : mapColumns) {
        unsigned int ulPropTag = col.first;

        // Remember the types of columns being requested for later use
        if(ECGenProps::GetPropSubquery(ulPropTag, strSubQuery) == erSuccess) {
			setSubQueries.emplace(ulPropTag);
            bNeedSubQueries = true;
        } else if(ulPropTag & MV_FLAG) {
            if(ulPropTag & MV_INSTANCE)
                bNeedMVIProps = true;
            else
                bNeedMVProps = true;
        } else {
            bNeedProps = true;
        }
    }

    // Do normal properties
    if(bNeedProps) {
        // mapColumns now contains the data that we still need to request from the database for this row.
        strQuery = "SELECT " + strPropColOrder + " FROM properties WHERE hierarchyid="+stringify(sKey.ulObjId) + " AND properties.tag IN (";

		for (const auto &col : mapColumns) {
            // A subquery is defined for this type, we don't have to get the data in the normal way.
			if (setSubQueries.find(col.first) != setSubQueries.cend())
                continue;
			if ((col.first & MV_FLAG) == 0) {
				strQuery += stringify(PROP_ID(col.first));
                strQuery += ",";
            }
        }

        // Remove trailing ,
        strQuery.resize(strQuery.size()-1);
        strQuery += ")";
    }

    // Do MV properties
    if(bNeedMVProps) {
        if(!strQuery.empty())
            strQuery += " UNION ";

        strQuery += "SELECT " + (std::string)MVPROPCOLORDER + " FROM mvproperties WHERE hierarchyid="+stringify(sKey.ulObjId) + " AND mvproperties.tag IN (";

		for (const auto &col : mapColumns) {
			if (setSubQueries.find(col.first) != setSubQueries.cend())
                continue;
			if ((col.first & MV_FLAG) && !(col.first & MV_INSTANCE)) {
				strQuery += stringify(PROP_ID(col.first));
                strQuery += ",";
            }
        }

        strQuery.resize(strQuery.size()-1);
        strQuery += ")";
        strQuery += "  GROUP BY hierarchyid, tag";
    }

    // Do MVI properties. Output from this part is handled exactly the same as normal properties
    if(bNeedMVIProps) {
        if(!strQuery.empty())
            strQuery += " UNION ";

        strQuery += "SELECT " + strMVIPropColOrder + " FROM mvproperties WHERE hierarchyid="+stringify(sKey.ulObjId);

		for (const auto &col : mapColumns) {
			if (setSubQueries.find(col.first) != setSubQueries.cend())
                continue;
			if ((col.first & MV_FLAG) && (col.first & MV_INSTANCE)) {
                strQuery += " AND (";
                strQuery += "orderid = " + stringify(sKey.ulOrderId) + " AND ";
				strQuery += "tag = " + stringify(PROP_ID(col.first));
                strQuery += ")";
            }
        }
    }

    // Do generated properties
    if(bNeedSubQueries) {
        for (const auto &col : mapColumns) {
			if (ECGenProps::GetPropSubquery(col.first, strSubQuery) != erSuccess)
				continue;
			auto co = GetPropColOrder(col.first, strSubQuery);
			if (!strQuery.empty())
				strQuery += " UNION ";
			strQuery += " SELECT " + co +
				" FROM hierarchy WHERE hierarchy.id = " +
				stringify(sKey.ulObjId);
        }
    }

    if(!strQuery.empty()) {
        er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if(er != erSuccess)
			return er;

		while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
            if(lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
                assert(false);
                continue;
            }
			auto lpDBLen = lpDBResult.fetch_row_lengths();
            unsigned int ulPropTag = PROP_TAG(atoui(lpDBRow[2]), atoui(lpDBRow[1]));

            // The same column may have been requested multiple times. If that is the case, SQL will give us one result for all columns. This
            // means we have to loop through all the same-property columns and add the same data everywhere.
            for (auto iterColumns = mapColumns.lower_bound(NormalizeDBPropTag(ulPropTag));
                 iterColumns != mapColumns.cend() && CompareDBPropTag(iterColumns->first, ulPropTag); ) {
				// free prop if we're not allocing by soap
				if(soap == NULL && lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second].ulPropTag != 0) {
					FreePropVal(&lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second], false);
					memset(&lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second], 0, sizeof(lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second]));
				}

                if(CopyDatabasePropValToSOAPPropVal(soap, lpDBRow, lpDBLen, &lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second]) != erSuccess) {
                	// This can happen if a subquery returned a NULL field or if your database contains bad data (eg a NULL field where there shouldn't be)
			++iterColumns;
			continue;
                }

                // Update property tag to requested property tag; requested type may have been PT_UNICODE while database contains PT_STRING8
                lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second].ulPropTag = iterColumns->first;

				// Cache value
				if ((lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second].ulPropTag & MVI_FLAG) == MVI_FLAG)
					// Get rid of the MVI_FLAG
					lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second].ulPropTag &= ~MVI_FLAG;
				else
					cache->SetCell(&sKey, iterColumns->first, &lpsRowSet->__ptr[ulRowNum].__ptr[iterColumns->second]);


                // Remove from mapColumns so we know that we got a response from SQL
				iterColumns = mapColumns.erase(iterColumns);
            }
        }
    }

	for (const auto &col : mapColumns) {
		assert(lpsRowSet->__ptr[ulRowNum].__ptr[col.second].ulPropTag == 0);
		CopyEmptyCellToSOAPPropVal(soap, col.first, &lpsRowSet->__ptr[ulRowNum].__ptr[col.second]);
		cache->SetCell(&sKey, col.first, &lpsRowSet->__ptr[ulRowNum].__ptr[col.second]);
	}
	return erSuccess;
}

/**
 * Read rows from tproperties table
 *
 * Since data in tproperties is truncated, this engine can only provide truncated property values.
 *
 *
 * @param[in] lpThis Pointer to main table object, optional (needed for categorization)
 * @param[in] soap Soap object to use for memory allocations, may be NULL for malloc() allocations
 * @param[in] lpSession Pointer to session for security context
 * @param[in] mapColumns Map of columns to retrieve with key = ulPropTag, value = column number
 * @param[in] ulFolderId Folder ID in which the rows exist
 * @param[in] mapObjIds Map of objects to retrieve with key = sObjectTableKey, value = row number
 * @param[out] lpsRowSet Row set where data will be written. Must be pre-allocated to hold all columns and rows requested
 */
ECRESULT ECStoreObjectTable::QueryRowDataByColumn(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession,
    const std::multimap<unsigned int, unsigned int> &mapColumns,
    unsigned int ulFolderId,
    const std::map<sObjectTableKey, unsigned int> &mapObjIds,
    struct rowSet *lpsRowSet)
{
	std::string strQuery, strTags, strMVTags, strMVITags;
    std::set<std::pair<unsigned int, unsigned int> > setDone;
    sObjectTableKey key;
	DB_RESULT lpDBResult;
    DB_ROW lpDBRow = NULL;
    ECDatabase      *lpDatabase = NULL;
    std::set<unsigned int> setSubQueries;
    std::string strSubquery, strPropColOrder;

	if (mapColumns.empty() || mapObjIds.empty())
		return erSuccess;
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	ULONG ulMin = PROP_ID(mapColumns.begin()->first), ulMax = ulMin;

    // Split columns into MV columns and non-MV columns
	for (const auto &col : mapColumns) {
		if ((col.first & MVI_FLAG) == MVI_FLAG) {
			if (!strMVITags.empty())
				strMVITags += ",";
			strMVITags += stringify(PROP_ID(col.first));
    	}
		if ((col.first & MVI_FLAG) == MV_FLAG) {
			if (!strMVTags.empty())
				strMVTags += ",";
			strMVTags += stringify(PROP_ID(col.first));
    	}
		if ((col.first & MVI_FLAG) == 0) {
			if (ECGenProps::GetPropSubquery(col.first, strSubquery) == erSuccess) {
				setSubQueries.emplace(col.first);
			} else {
				if (!strTags.empty())
					strTags += ",";
				strTags += stringify(PROP_ID(col.first));
			}
    	}

		ulMin = std::min(ulMin, PROP_ID(col.first));
		ulMax = std::max(ulMax, PROP_ID(col.first));
    }

	auto strHierarchyIds = kc_join(mapObjIds, ",", [](const auto &ob) { return stringify(ob.first.ulObjId); });
	// Get data
	if (!strTags.empty())
		strQuery = "SELECT " PROPCOLORDER ", hierarchyid, 0 FROM tproperties AS properties WHERE folderid=" + stringify(ulFolderId) + " AND hierarchyid IN(" + strHierarchyIds + ") AND tag IN (" + strTags +") AND tag >= " + stringify(ulMin) + " AND tag <= " + stringify(ulMax);
	if(!strMVTags.empty()) {
		if(!strQuery.empty())
			strQuery += " UNION ";
		strQuery += "SELECT " MVPROPCOLORDER ", hierarchyid, 0 FROM mvproperties WHERE hierarchyid IN(" + strHierarchyIds + ") AND tag IN (" + strMVTags +") GROUP BY hierarchyid, tag";
	}
	if(!strMVITags.empty()) {
		if(!strQuery.empty())
			strQuery += " UNION ";
		strQuery += "SELECT " MVIPROPCOLORDER ", hierarchyid, orderid FROM mvproperties WHERE hierarchyid IN(" + strHierarchyIds + ") AND tag IN (" +strMVITags +")";
	}
	if(!setSubQueries.empty()) {
		for (const auto &sq : setSubQueries) {
			if(!strQuery.empty())
				strQuery += " UNION ";
			if (ECGenProps::GetPropSubquery(sq, strSubquery) != erSuccess)
				continue;
			strPropColOrder = GetPropColOrder(sq, strSubquery);
			strQuery += " SELECT " + strPropColOrder + ", hierarchy.id, 0 FROM hierarchy WHERE hierarchy.id IN (" + strHierarchyIds + ")";
		}
	}

	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;

	auto cache = lpSession->GetSessionManager()->GetCacheManager();
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr) {
		auto lpDBLen = lpDBResult.fetch_row_lengths();
		if(lpDBRow[FIELD_NR_MAX] == NULL || lpDBRow[FIELD_NR_MAX+1] == NULL || lpDBRow[FIELD_NR_TAG] == NULL || lpDBRow[FIELD_NR_TYPE] == NULL)
			continue; // No hierarchyid, tag or orderid (?)

		key.ulObjId = atoui(lpDBRow[FIELD_NR_MAX]);
		key.ulOrderId = atoui(lpDBRow[FIELD_NR_MAX+1]);
		auto ulTag = atoui(lpDBRow[FIELD_NR_TAG]);
		auto ulType = atoui(lpDBRow[FIELD_NR_TYPE]);

		// Find the right place to put things.

		// In an MVI column, we need to find the exact row to put the data in. We could get order id 0 from the database
		// while it was not requested. If that happens, then we should just discard the data.
		// In a non-MVI column, orderID from the DB is 0, and we should write that value into all rows with this object ID.
		// The lower_bound makes sure that if the requested row had order ID 1, we can still find it.
		std::map<sObjectTableKey, unsigned int>::const_iterator iterObjIds;
		if(ulType & MVI_FLAG)
			iterObjIds = mapObjIds.find(key);
		else {
			assert(key.ulOrderId == 0);
			iterObjIds = mapObjIds.lower_bound(key);
		}

		if (iterObjIds == mapObjIds.cend())
			continue; // Got data for a row we didn't request ? (Possible for MVI queries)

		while (iterObjIds != mapObjIds.cend() &&
		       iterObjIds->first.ulObjId == key.ulObjId) {
			// WARNING. For PT_UNICODE columns, ulTag contains PT_STRING8, since that is the tag in the database. We rely
			// on PT_UNICODE = PT_STRING8 + 1 here since we do a lower_bound to scan for either PT_STRING8 or PT_UNICODE
			// and then use CompareDBPropTag to check the actual type in the while loop later. Same goes for PT_MV_UNICODE.
			for (auto iterColumns = mapColumns.lower_bound(PROP_TAG(ulType, ulTag));
			     iterColumns != mapColumns.cend() && CompareDBPropTag(iterColumns->first, PROP_TAG(ulType, ulTag));
			     ++iterColumns) {
				// free prop if we're not allocing by soap
				auto &m = lpsRowSet->__ptr[iterObjIds->second].__ptr[iterColumns->second];
				if (soap == nullptr && m.ulPropTag != 0) {
					FreePropVal(&m, false);
					memset(&m, 0, sizeof(m));
				}

				// Handle requesting the same tag multiple times; the data is returned only once, so we need to copy it to all the columns in which it was
				// requested. Note that requesting the same ROW more than once is not supported (it is a map, not a multimap)
				if (CopyDatabasePropValToSOAPPropVal(soap, lpDBRow, lpDBLen, &m) != erSuccess)
					CopyEmptyCellToSOAPPropVal(soap, iterColumns->first, &m);

				// Update propval to correct value. We have to do this because we may have requested PT_UNICODE while the database
				// contains PT_STRING8.
				if (PROP_TYPE(m.ulPropTag) == PT_ERROR)
					m.ulPropTag = CHANGE_PROP_TYPE(iterColumns->first, PT_ERROR);
				else
					m.ulPropTag = iterColumns->first;
				if ((m.ulPropTag & MVI_FLAG) == MVI_FLAG)
					m.ulPropTag &= ~MVI_FLAG;
				else
					cache->SetCell(const_cast<sObjectTableKey *>(&iterObjIds->first), iterColumns->first, &m);

				setDone.emplace(iterObjIds->second, iterColumns->second);
			}

			// We may have more than one row to fill in an MVI table; if we're handling a non-MVI property, then we have to duplicate that
			// value on each row
			if(ulType & MVI_FLAG)
				break; // For the MVI column, we should get one result for each row
			else
				++iterObjIds; // For non-MVI columns, we get one result for all expanded rows
		}
	}

	for (const auto &col : mapColumns)
		for (const auto &ob : mapObjIds)
			if (setDone.count({ob.second, col.second}) == 0) {
				// We may be overwriting a value that was retrieved from the cache before.
				if (soap == NULL && lpsRowSet->__ptr[ob.second].__ptr[col.second].ulPropTag != 0)
					FreePropVal(&lpsRowSet->__ptr[ob.second].__ptr[col.second], false);
				CopyEmptyCellToSOAPPropVal(soap, col.first, &lpsRowSet->__ptr[ob.second].__ptr[col.second]);
				cache->SetCell(const_cast<sObjectTableKey *>(&ob.first),
					col.first, &lpsRowSet->__ptr[ob.second].__ptr[col.second]);
			}
	return erSuccess;
}

ECRESULT ECStoreObjectTable::CopyEmptyCellToSOAPPropVal(struct soap *soap, unsigned int ulPropTag, struct propVal *lpPropVal)
{
	lpPropVal->ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_ERROR);
	lpPropVal->__union = SOAP_UNION_propValData_ul;
	lpPropVal->Value.ul = KCERR_NOT_FOUND;
	return erSuccess;
}

ECRESULT ECStoreObjectTable::GetMVRowCountHelper(ECDatabase *db, std::string query, std::list<unsigned int> &ids, std::map<unsigned int, unsigned int> &count) {
	while (!ids.empty()) {
		query += stringify(ids.front()) + ",";
		ids.pop_front();
		if (query.size() > KC_DFL_MAX_PACKET_SIZE - 1024)
			break;
	}
	query.resize(query.size()-1);
	query += ") GROUP BY h.id ORDER by h.id, orderid";

	DB_RESULT result;
	auto er = db->DoSelect(query, &result);
	if (er != erSuccess)
		return er;

	auto row = result.fetch_row();
	if (row == nullptr || row[0] == nullptr) {
		ec_log_err("ECStoreObjectTable::GetMVRowCount(): row or column null");
		return KCERR_DATABASE_ERROR;
	}

	while (row != nullptr) {
		if (row[0] == nullptr || row[1] == nullptr)
			continue;

		count[atoi(row[0])] = atoi(row[1]);
		row = result.fetch_row();
	}

	return er;
}

ECRESULT ECStoreObjectTable::GetMVRowCount(std::list<unsigned int> &&ids,
    std::map<unsigned int, unsigned int> &count)
{
	ECDatabase *lpDatabase = NULL;

	if (ids.size() == 0)
		return erSuccess;

	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	// scan for MV-props and generate the base query
	std::string query = "SELECT h.id, count(h.id) FROM hierarchy as h";

	ulock_rec biglock(m_hLock);

	for (auto tag : m_listMVSortCols) {
		auto colname = "col" + stringify(tag);
		query += " LEFT JOIN mvproperties as " + colname +
			" ON h.id=" + colname + ".hierarchyid AND " +
			colname + ".tag=" +
			stringify(PROP_ID(tag)) + " AND " + colname +
			".type=" + stringify(PROP_TYPE(NormalizeDBPropTag(tag) &~MV_INSTANCE));
	}

	biglock.unlock();

	query += " WHERE h.id IN ( ";

	while (ids.size() > 0) {
		er = GetMVRowCountHelper(lpDatabase, query, ids, count);
		if (er != erSuccess)
			return er;
	}

	return er;
}

ECRESULT ECStoreObjectTable::Load()
{
    ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult;
	auto lpData = static_cast<const ECODStore *>(m_lpObjectData);
	unsigned int ulFlags = lpData->ulFlags, ulFolderId = lpData->ulFolderId;
    unsigned int ulObjType = lpData->ulObjType;

    unsigned int ulMaxItems = atoui(lpSession->GetSessionManager()->GetConfig()->GetSetting("folder_max_items"));

    std::list<unsigned int> lstObjIds;

	auto cache = lpSession->GetSessionManager()->GetCacheManager();
	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;
	if (ulFolderId == 0)
		return erSuccess;

        // Clear old entries
        Clear();

        // Load the table with all the objects of type ulObjType and flags ulFlags in container ulParent
	std::string strQuery = "SELECT hierarchy.id, hierarchy.parent, hierarchy.owner, hierarchy.flags, hierarchy.type FROM hierarchy WHERE hierarchy.parent=" + stringify(ulFolderId);

        if(ulObjType == MAPI_MESSAGE)
        {
			strQuery += " AND hierarchy.type = " +  stringify(ulObjType);

            if((ulFlags&MSGFLAG_DELETED) == 0)// Normal message and associated message
                strQuery += " AND hierarchy.flags & "+stringify(MSGFLAG_ASSOCIATED)+" = " + stringify(ulFlags&MSGFLAG_ASSOCIATED) + " AND hierarchy.flags & "+stringify(MSGFLAG_DELETED)+" = 0";
            else
                strQuery += " AND hierarchy.flags & "+stringify(MSGFLAG_ASSOCIATED)+" = " + stringify(ulFlags&MSGFLAG_ASSOCIATED) + " AND hierarchy.flags & "+stringify(MSGFLAG_DELETED)+" = " + stringify(MSGFLAG_DELETED);
        }
		else if(ulObjType == MAPI_FOLDER) {
            strQuery += " AND hierarchy.type = " +  stringify(ulObjType);
			strQuery += " AND hierarchy.flags & "+stringify(MSGFLAG_DELETED)+" = " + stringify(ulFlags&MSGFLAG_DELETED);
		}else if(ulObjType == MAPI_MAILUSER) { //Read MAPI_MAILUSER and MAPI_DISTLIST
			strQuery += " AND (hierarchy.type = " +  stringify(ulObjType) + " OR hierarchy.type = " +  stringify(MAPI_DISTLIST) + ")";
		}else {
			 strQuery += " AND hierarchy.type = " +  stringify(ulObjType);
		}

        er = lpDatabase->DoSelect(strQuery, &lpDBResult);
        if(er != erSuccess)
		return er;

	unsigned int i = 0;
        while(1) {
		auto lpDBRow = lpDBResult.fetch_row();
            if(lpDBRow == NULL)
                break;

			if (lpDBRow[0] == nullptr || lpDBRow[1] == nullptr || lpDBRow[2] == nullptr || lpDBRow[3] == nullptr || lpDBRow[4] == nullptr)
				continue;

			cache->SetObject(atoui(lpDBRow[0]), atoui(lpDBRow[1]), atoui(lpDBRow[2]), atoui(lpDBRow[3]), atoui(lpDBRow[4]));
            // Altough we don't want more than ulMaxItems entries, keep looping to get all the results from MySQL. We need to do this
            // because otherwise we can get out of sync with mysql which is still sending us results while we have already stopped
            // reading data.
            if(i > ulMaxItems)
                continue;
			lstObjIds.emplace_back(atoi(lpDBRow[0]));
			++i;
        }

        LoadRows(&lstObjIds, 0);
	return erSuccess;
}

ECRESULT ECStoreObjectTable::CheckPermissions(unsigned int ulObjId)
{
    unsigned int ulParent = 0;
	auto lpData = static_cast<const ECODStore *>(m_lpObjectData);
	auto sec = lpSession->GetSecurity();

	if (m_ulObjType == MAPI_FOLDER)
		return sec->CheckPermission(ulObjId, ecSecurityFolderVisible);
	if (m_ulObjType != MAPI_MESSAGE)
		return erSuccess;
	if (lpData->ulFolderId) {
		// We can either see all messages or none at all. Do this check once only as an optimisation.
		if (!fPermissionRead) {
			ulPermission = sec->CheckPermission(lpData->ulFolderId, ecSecurityRead);
			fPermissionRead = true;
		}
		return ulPermission;
	}
	// Get the parent id of the row we're inserting (this is very probably cached from Load())
	auto er = lpSession->GetSessionManager()->GetCacheManager()->GetParent(ulObjId, &ulParent);
	if (er != erSuccess)
		return er;
	// This will be cached after the first row in the table is added
	return sec->CheckPermission(ulParent, ecSecurityRead);
}

ECRESULT ECStoreObjectTable::AddRowKey(ECObjectTableList* lpRows, unsigned int *lpulLoaded, unsigned int ulFlags, bool bLoad, bool bOverride, struct restrictTable *lpOverride)
{
	auto lpODStore = static_cast<const ECODStore *>(m_lpObjectData);
	assert(!bOverride); // Default implementation never has override enabled, so we should never see this
	assert(lpOverride == NULL);
	ulock_rec biglock(m_hLock);

    // Use normal table update code if:
    //  - not an initial load (but a table update)
    //  - no restriction
    //  - not a restriction on a folder (eg searchfolder)
	if (!bLoad || lpsRestrict == nullptr || lpODStore->ulFolderId == 0 ||
	    lpODStore->ulStoreId == 0 || (lpODStore->ulFlags & MAPI_ASSOCIATED))
		return ECGenericObjectTable::AddRowKey(lpRows, lpulLoaded, ulFlags, bLoad, false, nullptr);

        // Attempt to use the indexer
	ECDatabase *lpDatabase;
	GUID guidServer;
	auto er = lpSession->GetSessionManager()->GetServerGUID(&guidServer);
        if(er != erSuccess)
		return er;
        er = lpSession->GetDatabase(&lpDatabase);
        if(er != erSuccess)
		return er;

	struct restrictTable *lpNewRestrict = nullptr;
	std::string suggestion;
	std::list<unsigned int> lstIndexerResults, lstFolders{lpODStore->ulFolderId};
	std::set<unsigned int> setMatches;
	ECObjectTableList sMatchedRows;

	auto sesmgr = lpSession->GetSessionManager();
	if (GetIndexerResults(lpDatabase, sesmgr->GetConfig().get(),
	    sesmgr->GetCacheManager(), &guidServer, lpODStore->lpGuid,
	    lstFolders, lpsRestrict, &lpNewRestrict, lstIndexerResults,
	    suggestion) != erSuccess) {
    	    // Cannot handle this restriction with the indexer, use 'normal' restriction code
    	    // Reasons can be:
    	    //  - restriction too complex
    	    //  - folder not indexed
    	    //  - indexer not running / not configured
    	    er = ECGenericObjectTable::AddRowKey(lpRows, lpulLoaded, ulFlags, bLoad, false, NULL);
    	    goto exit;
        }

        // Put the results in setMatches
        std::copy(lstIndexerResults.begin(), lstIndexerResults.end(), std::inserter(setMatches, setMatches.begin()));

    	/* Filter the incoming row set with the matches.
    	 *
    	 * Actually, we should not have to do this. We could just take the result set from the indexer and
    	 * feed that directly to AddRowKey. However, the indexer may be slightly 'behind' in time, and the
    	 * list of objects in lpRows is more up-to-date. To make sure that we will not generate 'phantom' rows
    	 * of deleted items we take the cross section of lpRows and setMatches. In most cases the result
    	 * will be equal to setMatches though.
    	 */
	for (const auto &row : *lpRows)
		if (setMatches.find(row.ulObjId) != setMatches.end())
			sMatchedRows.emplace_back(row);

    	// Pass filtered results to AddRowKey, which will perform any further filtering required
    	er = ECGenericObjectTable::AddRowKey(&sMatchedRows, lpulLoaded, ulFlags, bLoad, true, lpNewRestrict);
exit:
	biglock.unlock();
	FreeRestrictTable(lpNewRestrict);
	return er;
}

/**
 * Get all deferred changes for a specific folder
 */
ECRESULT GetDeferredTableUpdates(ECDatabase *lpDatabase, unsigned int ulFolderId, std::list<unsigned int> *lpDeferred)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;

	lpDeferred->clear();

	std::string strQuery = "SELECT hierarchyid FROM deferredupdate WHERE folderid = " + stringify(ulFolderId);
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr)
		lpDeferred->emplace_back(atoui(lpDBRow[0]));
	return erSuccess;
}

/**
 * Get all deferred changes for a specific folder
 */
ECRESULT GetDeferredTableUpdates(ECDatabase *lpDatabase,
    const ECObjectTableList *lpRowList, std::list<unsigned int> *lpDeferred)
{
	DB_RESULT lpDBResult;
	DB_ROW lpDBRow = NULL;

	lpDeferred->clear();
	auto strQuery = "SELECT hierarchyid FROM deferredupdate WHERE hierarchyid IN(" +
		kc_join(*lpRowList, ",", [](const auto &row) { return stringify(row.ulObjId); }) + ")";
	auto er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		return er;
	while ((lpDBRow = lpDBResult.fetch_row()) != nullptr)
		lpDeferred->emplace_back(atoui(lpDBRow[0]));
	return erSuccess;
}

} /* namespace */
