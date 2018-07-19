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
#include <list>
#include <memory>
#include <string>
#include <kopano/scope.hpp>
#include <kopano/tie.hpp>

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
#include <kopano/mapiext.h>
#include <sys/types.h>
#if 1 /* change to HAVE_REGEX_H */
#include <regex.h>
#endif
#include <iostream>
#include "kcore.hpp"
#include "pcutil.hpp"
#include "ECSecurity.h"
#include "ECDatabaseUtils.h"
#include <kopano/ECKeyTable.h>
#include "ECGenProps.h"
#include "ECGenericObjectTable.h"
#include "SOAPUtils.h"
#include <kopano/stringutil.h>
#include "ECSessionManager.h"
#include "ECSession.h"

namespace KC {

static struct sortOrderArray sDefaultSortOrder;
static const ULONG sANRProps[] = {
	PR_DISPLAY_NAME, PR_SMTP_ADDRESS, PR_ACCOUNT, PR_DEPARTMENT_NAME,
	PR_OFFICE_TELEPHONE_NUMBER, PR_OFFICE_LOCATION, PR_PRIMARY_FAX_NUMBER,
	PR_SURNAME
};

#define ISMINMAX(x) ((x) == EC_TABLE_SORT_CATEG_MIN || (x) == EC_TABLE_SORT_CATEG_MAX)

/**
 * Apply RELOP_* rules to equality value from CompareProp
 *
 * 'equality' is a value from CompareProp which can be -1, 0 or 1. This function
 * returns TRUE when the passed relop matches the equality value. (Eg equality=0
 * and RELOP_EQ, then returns TRUE)
 * @param relop RELOP
 * @param equality Equality value from CompareProp
 * @return TRUE if the relop matches
 */
static inline bool match(unsigned int relop, int equality)
{
	switch(relop) {
	case RELOP_GE:
		return equality >= 0;
	case RELOP_GT:
		return equality > 0;
	case RELOP_LE:
		return equality <= 0;
	case RELOP_LT:
		return equality < 0;
	case RELOP_NE:
		return equality != 0;
	case RELOP_RE:
		return false; // FIXME ?? how should this work ??
	case RELOP_EQ:
		return equality == 0;
	default:
		return false;
	}
}

/**
 * @param[in] lpSession
 *					Reference to a session object; cannot be NULL.
 * @param[in] ulObjType
 *					The Object type of the objects in the table
 */
ECGenericObjectTable::ECGenericObjectTable(ECSession *ses,
    unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale) :
	lpSession(ses), lpKeyTable(new ECKeyTable),
	m_ulObjType(ulObjType), m_ulFlags(ulFlags), m_locale(locale)
{
	// No columns by default
	lpsPropTagArray = s_alloc<propTagArray>(nullptr);
	lpsPropTagArray->__size = 0;
	lpsPropTagArray->__ptr = nullptr;
}

ECGenericObjectTable::~ECGenericObjectTable()
{
	FreePropTagArray(lpsPropTagArray);
	FreeSortOrderArray(lpsSortOrderArray);
	FreeRestrictTable(lpsRestrict);
	for (const auto &p : m_mapCategories)
		delete p.second;
}

/**
 * Moves the cursor to a specific row in the table.
 *
 * @param[in] ulBookmark
 *				Identifying the starting position for the seek action. A bookmark can be created with 
 *				ECGenericObjectTable::CreateBookmark call, or use one of the following bookmark predefines:
 *				\arg BOOKMARK_BEGINNING		Start seeking from the beginning of the table.
 *				\arg BOOKMARK_CURRENT		Start seeking from the current position of the table.
 *				\arg BOOKMARK_END			Start seeking from the end of the table.
 * @param[in] lSeekTo
 *				Positive or negative number of rows moved starting from the bookmark.
 * @param[in] lplRowsSought
 *				Pointer to the number or rows that were processed in the seek action. If lplRowsSought is NULL, 
 *				the caller iss not interested in the returned output.
 *
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::SeekRow(unsigned int ulBookmark, int lSeekTo, int *lplRowsSought)
{
	scoped_rlock biglock(m_hLock);
	ECRESULT er = Populate();
	if(er != erSuccess)
		return er;

	if(lpsSortOrderArray == NULL) {
		er = SetSortOrder(&sDefaultSortOrder, 0, 0);

		if(er != erSuccess)
			return er;
	}
	return lpKeyTable->SeekRow(ulBookmark, lSeekTo, lplRowsSought);
}

/**
 * Finds the next row in the table that matches specific search criteria.
 *
 *
 * @param[in] lpsRestrict
 * @param[in] ulBookmark
 * @param[in] ulFlags
 *
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::FindRow(struct restrictTable *rt,
    unsigned int ulBookmark, unsigned int ulFlags)
{
	bool			fMatch = false;
	int ulSeeked = 0, ulTraversed = 0;
	unsigned int ulRow = 0, ulCount = 0;
	struct propTagArray	*lpPropTags = NULL;

	ECObjectTableList	ecRowList;
	sObjectTableKey		sRowItem;

	entryId				sEntryId;
	auto cache = lpSession->GetSessionManager()->GetCacheManager();
	ulock_rec biglock(m_hLock);

	ECRESULT er = Populate();
	if(er != erSuccess)
		return er;
	/* We may need the table position later (ulCount is not used) */
	er = lpKeyTable->GetRowCount(&ulCount, &ulRow);
	if (er != erSuccess)
		return er;
	// Start searching at the right place
	if (ulBookmark == BOOKMARK_END && ulFlags & DIR_BACKWARD)
		er = SeekRow(ulBookmark, -1, NULL);
	else
		er = SeekRow(ulBookmark, 0, NULL);
	if (er != erSuccess)
		return er;

	// Special optimisation case: if you're searching the PR_INSTANCE_KEY, we can
	// look this up directly!
	if( ulBookmark == BOOKMARK_BEGINNING &&
	    rt->ulType == RES_PROPERTY && rt->lpProp->ulType == RELOP_EQ &&
	    rt->lpProp->lpProp && rt->lpProp->ulPropTag == PR_INSTANCE_KEY &&
	    rt->lpProp->lpProp->ulPropTag == PR_INSTANCE_KEY &&
	    rt->lpProp->lpProp->Value.bin && rt->lpProp->lpProp->Value.bin->__size == sizeof(unsigned int) * 2)
	{
		uint32_t tmp4;
		memcpy(&tmp4, lpsRestrict->lpProp->lpProp->Value.bin->__ptr, sizeof(tmp4));
		sRowItem.ulObjId = le32_to_cpu(tmp4);
		memcpy(&tmp4, lpsRestrict->lpProp->lpProp->Value.bin->__ptr + sizeof(tmp4), sizeof(tmp4));
		sRowItem.ulOrderId = le32_to_cpu(tmp4);
		return lpKeyTable->SeekId(&sRowItem);
	}

	// We can do the same with PR_ENTRYID
	if( ulBookmark == BOOKMARK_BEGINNING && 
	    rt->ulType == RES_PROPERTY && rt->lpProp->ulType == RELOP_EQ &&
	    rt->lpProp->lpProp && rt->lpProp->ulPropTag == PR_ENTRYID &&
	    rt->lpProp->lpProp->ulPropTag == PR_ENTRYID &&
	    rt->lpProp->lpProp->Value.bin && IsKopanoEntryId(rt->lpProp->lpProp->Value.bin->__size, rt->lpProp->lpProp->Value.bin->__ptr))
	{
		sEntryId.__ptr = rt->lpProp->lpProp->Value.bin->__ptr;
		sEntryId.__size = rt->lpProp->lpProp->Value.bin->__size;
		er = cache->GetObjectFromEntryId(&sEntryId, &sRowItem.ulObjId);
		if(er != erSuccess)
			return er;
		sRowItem.ulOrderId = 0; // FIXME: this is incorrect when MV_INSTANCE is specified on a column, but this won't happen often.
		return lpKeyTable->SeekId(&sRowItem);
	}

	// Get the columns we will be needing for this search
	auto cleanup = make_scope_success([&]() { FreePropTagArray(lpPropTags); });
	er = GetRestrictPropTags(rt, nullptr, &lpPropTags);
	if(er != erSuccess)
		return er;

	// Loop through the rows, matching it with the search criteria
	while(1) {
		ecRowList.clear();

		// Get the row ID of the next row
		er = lpKeyTable->QueryRows(20, &ecRowList, (ulFlags & DIR_BACKWARD)?true:false, TBL_NOADVANCE);

		if(er != erSuccess)
			return er;
		if(ecRowList.empty())
			break;

		// Get the rowdata from the QueryRowData function
		struct rowSet *lpRowSet = nullptr;
		auto rowset_clean = make_scope_success([&]() { FreeRowSet(lpRowSet, true); });
		er = m_lpfnQueryRowData(this, NULL, lpSession, &ecRowList, lpPropTags, m_lpObjectData, &lpRowSet, true, false);
		if(er != erSuccess)
			return er;
		SUBRESTRICTIONRESULTS sub_results;
		er = RunSubRestrictions(lpSession, m_lpObjectData, rt, &ecRowList, m_locale, sub_results);
        if(er != erSuccess)
			return er;

		assert(lpRowSet->__size == static_cast<gsoap_size_t>(ecRowList.size()));
		for (gsoap_size_t i = 0; i < lpRowSet->__size; ++i) {
			// Match the row
			er = MatchRowRestrict(cache, &lpRowSet->__ptr[i], rt, &sub_results, m_locale, &fMatch);
			if(er != erSuccess)
				return er;
			if(fMatch)
			{
				// A Match, seek the cursor
				lpKeyTable->SeekRow(BOOKMARK_CURRENT, ulFlags & DIR_BACKWARD ? -i : i, &ulSeeked);
				break;
			}
		}
		if(fMatch)
			break;

		// No match, then advance the cursor
		lpKeyTable->SeekRow(BOOKMARK_CURRENT, ulFlags & DIR_BACKWARD ? -(int)ecRowList.size() : ecRowList.size(), &ulSeeked);

		// No advance possible, break the loop
		if(ulSeeked == 0)
			break;
	}

	if(!fMatch) {
		er = KCERR_NOT_FOUND;
		lpKeyTable->SeekRow(ECKeyTable::EC_SEEK_SET, ulRow, &ulTraversed);
    }
	return er;
}

/**
 * Returns the total number of rows in the table.
 *
 * @param[out] lpulRowCount
 *					Pointer to the number of rows in the table.
 * @param[out] lpulCurrentRow
 *					Pointer to the current row id in the table.
 *
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::GetRowCount(unsigned int *lpulRowCount, unsigned int *lpulCurrentRow)
{
	scoped_rlock biglock(m_hLock);
	ECRESULT er = Populate();
	if(er != erSuccess)
		return er;
	    
	if(lpsSortOrderArray == NULL) {
		er = SetSortOrder(&sDefaultSortOrder, 0, 0);

		if(er != erSuccess)
			return er;
	}
	return lpKeyTable->GetRowCount(lpulRowCount, lpulCurrentRow);
}

ECRESULT ECGenericObjectTable::ReloadTableMVData(ECObjectTableList* lplistRows, ECListInt* lplistMVPropTag)
{
	// default ignore MV-view, show as an normal view
	return erSuccess;
}

/**
 * Returns a list of columns for the table.
 *
 * If the function is not overridden, it returns always an empty column set.
 *
 * @param[in,out] lplstProps
 *						a list of columns for the table
 *
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::GetColumnsAll(ECListInt* lplstProps)
{
	return erSuccess;
}

/**
 * Reload the table objects.
 *
 * Rebuild the whole table with the current restriction and sort order. If the sort order 
 * includes a multi-valued property, a single row appearing in multiple rows. ReloadTable
 * may expand or contract expanded MVI rows if the sort order or column set have changed. If there
 * is no change in MVI-related expansion, it will call ReloadKeyTable which only does a
 * resort/refilter of the existing rows.
 *
 * @param[in] eType
 *				The reload type determines how it should reload.
 *
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::ReloadTable(enumReloadType eType)
{
	ECRESULT			er = erSuccess;
	bool bMVColsNew = false, bMVSortNew = false;
	ECObjectTableList			listRows;
	ECListInt					listMVPropTag;
	scoped_rlock biglock(m_hLock);

	//Scan for MVI columns
	for (gsoap_size_t i = 0; lpsPropTagArray != NULL && i < lpsPropTagArray->__size; ++i) {
		if ((PROP_TYPE(lpsPropTagArray->__ptr[i]) &MVI_FLAG) != MVI_FLAG)
			continue;
		if (bMVColsNew)
			assert(false); //FIXME: error 1 mv prop set!!!
		bMVColsNew = true;
		listMVPropTag.emplace_back(lpsPropTagArray->__ptr[i]);
	}
	
	//Check for mvi props
	for (gsoap_size_t i = 0; lpsSortOrderArray != NULL && i < lpsSortOrderArray->__size; ++i) {
		if ((PROP_TYPE(lpsSortOrderArray->__ptr[i].ulPropTag) & MVI_FLAG) != MVI_FLAG)
			continue;
		if (bMVSortNew)
			assert(false);
		bMVSortNew = true;
		listMVPropTag.emplace_back(lpsSortOrderArray->__ptr[i].ulPropTag);
	}

	listMVPropTag.sort();
	listMVPropTag.unique();

	if ((!m_bMVCols && !m_bMVSort && !bMVColsNew && !bMVSortNew) ||
		(listMVPropTag == m_listMVSortCols && (m_bMVCols == bMVColsNew || m_bMVSort == bMVSortNew)) )
	{
		if(eType == RELOAD_TYPE_SORTORDER)
			er = ReloadKeyTable();
		/* No MVprops or already sorted, skip MV sorts. */
		return er;
	}

	m_listMVSortCols = listMVPropTag;

	// Get all the Single Row IDs from the ID map
	for (const auto &p : mapObjects)
		if (p.first.ulOrderId == 0)
			listRows.emplace_back(p.first);
	if(mapObjects.empty())
		goto skip;
	if (bMVColsNew || bMVSortNew) {
		// Expand rows to contain all MVI expansions (listRows is appended to)
		er = ReloadTableMVData(&listRows, &listMVPropTag);
		if(er != erSuccess)
			return er;
	}

	// Clear row data	
	Clear();

	//Add items
	for (const auto &row : listRows)
		mapObjects[row] = 1;

	// Load the keys with sort data from the table
	er = AddRowKey(&listRows, NULL, 0, true, false, NULL);

skip:
	m_bMVCols = bMVColsNew;
	m_bMVSort = bMVSortNew;
	return er;
}

/**
 * Returns the total number of multi value rows of a specific object.
 *
 * This methode should be overridden and should return the total number of multi value rows of a specific object.
 *
 * @param[in] ulObjId
 *					Object id to receive the number of multi value rows
 * @param[out] lpulCount
 *					Pointer to the number of multi value rows of the object ulObjId
 *
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::GetMVRowCount(std::list<unsigned int> &&ids,
    std::map<unsigned int, unsigned int> &count)
{
	return KCERR_NO_SUPPORT;
}

/**
 * Defines the properties and order of properties to appear as columns in the table.
 *
 * @param[in]	lpsPropTags
 *					Pointer to an array of property tags with a specific order.
 *					The lpsPropTags parameter cannot be set to NULL; table must have at least one column.
 *
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::SetColumns(const struct propTagArray *lpsPropTags,
    bool bDefaultSet)
{
	//FIXME: check the lpsPropTags array, 0x????xxxx -> xxxx must be checked

	// Remember the columns for later use (in QueryRows)
	// This is a very very quick operation, as we only save the information.

	scoped_rlock biglock(m_hLock);

	// Delete the old column set
	FreePropTagArray(lpsPropTagArray);
	lpsPropTagArray = s_alloc<propTagArray>(nullptr);
	lpsPropTagArray->__size = lpsPropTags->__size;
	lpsPropTagArray->__ptr = s_alloc<unsigned int>(nullptr, lpsPropTags->__size);
	if (bDefaultSet) {
		for (gsoap_size_t n = 0; n < lpsPropTags->__size; ++n) {
			if (PROP_TYPE(lpsPropTags->__ptr[n]) == PT_STRING8 || PROP_TYPE(lpsPropTags->__ptr[n]) == PT_UNICODE)
				lpsPropTagArray->__ptr[n] = CHANGE_PROP_TYPE(lpsPropTags->__ptr[n], ((m_ulFlags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8));
			else if (PROP_TYPE(lpsPropTags->__ptr[n]) == PT_MV_STRING8 || PROP_TYPE(lpsPropTags->__ptr[n]) == PT_MV_UNICODE)
				lpsPropTagArray->__ptr[n] = CHANGE_PROP_TYPE(lpsPropTags->__ptr[n], ((m_ulFlags & MAPI_UNICODE) ? PT_MV_UNICODE : PT_MV_STRING8));
			else
				lpsPropTagArray->__ptr[n] = lpsPropTags->__ptr[n];
		}
	} else
		memcpy(lpsPropTagArray->__ptr, lpsPropTags->__ptr, sizeof(unsigned int) * lpsPropTags->__size);
	
	return ReloadTable(RELOAD_TYPE_SETCOLUMNS);
}

ECRESULT ECGenericObjectTable::GetColumns(struct soap *soap, ULONG ulFlags, struct propTagArray **lppsPropTags)
{
	ECListInt			lstProps;
	struct propTagArray *lpsPropTags;
	scoped_rlock biglock(m_hLock);

	if(ulFlags & TBL_ALL_COLUMNS) {
		// All columns were requested. Simply get a unique list of all the proptags used in all the objects in this table
		auto er = Populate();
        if(er != erSuccess)
			return er;

		er = GetColumnsAll(&lstProps);
		if(er != erSuccess)
			return er;
	
		// Make sure we have a unique list
		lstProps.sort();
		lstProps.unique();

		// Convert them all over to a struct propTagArray
        lpsPropTags = s_alloc<propTagArray>(soap);
        lpsPropTags->__size = lstProps.size();
        lpsPropTags->__ptr = s_alloc<unsigned int>(soap, lstProps.size());

		size_t n = 0;
		for (auto prop_int : lstProps) {
			lpsPropTags->__ptr[n] = prop_int;
			if (PROP_TYPE(lpsPropTags->__ptr[n]) == PT_STRING8 || PROP_TYPE(lpsPropTags->__ptr[n]) == PT_UNICODE)
				lpsPropTags->__ptr[n] = CHANGE_PROP_TYPE(lpsPropTags->__ptr[n], ((m_ulFlags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8));
			else if (PROP_TYPE(lpsPropTags->__ptr[n]) == PT_MV_STRING8 || PROP_TYPE(lpsPropTags->__ptr[n]) == PT_MV_UNICODE)
				lpsPropTags->__ptr[n] = CHANGE_PROP_TYPE(lpsPropTags->__ptr[n], ((m_ulFlags & MAPI_UNICODE) ? PT_MV_UNICODE : PT_MV_STRING8));
			++n;
		}
	} else {
		lpsPropTags = s_alloc<propTagArray>(soap);

		if(lpsPropTagArray) {
			lpsPropTags->__size = lpsPropTagArray->__size;

			lpsPropTags->__ptr = s_alloc<unsigned int>(soap, lpsPropTagArray->__size);
			memcpy(lpsPropTags->__ptr, lpsPropTagArray->__ptr, sizeof(unsigned int) * lpsPropTagArray->__size);
		} else {
			lpsPropTags->__size = 0;
			lpsPropTags->__ptr = NULL;
		}
	}

	*lppsPropTags = lpsPropTags;
	return erSuccess;
}

ECRESULT ECGenericObjectTable::ReloadKeyTable()
{
	ECObjectTableList listRows;
	scoped_rlock biglock(m_hLock);

	// Get all the Row IDs from the ID map
	for (const auto &p : mapObjects)
		listRows.emplace_back(p.first);

	// Reset the key table
	lpKeyTable->Clear();
	m_mapLeafs.clear();

	for (const auto &p : m_mapCategories)
		delete p.second;
	m_mapCategories.clear();
	m_mapSortedCategories.clear();

	// Load the keys with sort data from the table
	return AddRowKey(&listRows, NULL, 0, true, false, NULL);
}

ECRESULT ECGenericObjectTable::SetSortOrder(const struct sortOrderArray *lpsSortOrder,
    unsigned int ulCategories, unsigned int ulExpanded)
{
	// Set the sort order, re-read the data from the database, and reset the current row
	// The current row is reset to point to the row it was pointing to in the first place.
	// This is pretty easy as it is pointing at the same object ID as it was before we
	// reloaded.
	scoped_rlock biglock(m_hLock);

	if (m_ulCategories == ulCategories && m_ulExpanded == ulExpanded &&
	    lpsSortOrderArray != nullptr &&
	    CompareSortOrderArray(lpsSortOrderArray, lpsSortOrder) == 0) {
		// Sort requested was already set, return OK
		SeekRow(BOOKMARK_BEGINNING, 0, nullptr);
		return erSuccess;
	}
	
	// Check validity of tags
	for (gsoap_size_t i = 0; i < lpsSortOrder->__size; ++i)
		if ((PROP_TYPE(lpsSortOrder->__ptr[i].ulPropTag) & MVI_FLAG) == MV_FLAG)
			return KCERR_TOO_COMPLEX;
	
	m_ulCategories = ulCategories;
	m_ulExpanded = ulExpanded;

	// Save the sort order requested
	FreeSortOrderArray(lpsSortOrderArray);
	lpsSortOrderArray = s_alloc<sortOrderArray>(nullptr);
	lpsSortOrderArray->__size = lpsSortOrder->__size;
	if(lpsSortOrder->__size == 0 ) {
		lpsSortOrderArray->__ptr = nullptr;
	} else {
		lpsSortOrderArray->__ptr = s_alloc<sortOrder>(nullptr, lpsSortOrder->__size);
		memcpy(lpsSortOrderArray->__ptr, lpsSortOrder->__ptr, sizeof(struct sortOrder) * lpsSortOrder->__size);
	}

	auto er = ReloadTable(RELOAD_TYPE_SORTORDER);
	if(er != erSuccess)
		return er;

	// FIXME When you change the sort order, current row should be equal to previous row ID
	return lpKeyTable->SeekRow(0, 0, NULL);
}

ECRESULT ECGenericObjectTable::GetBinarySortKey(struct propVal *lpsPropVal,
    ECSortCol &sc)
{
#define R(x) reinterpret_cast<const char *>(x)

	switch(PROP_TYPE(lpsPropVal->ulPropTag)) {
	case PT_BOOLEAN:
	case PT_I2: {
		unsigned short tmp = lpsPropVal->Value.b;
		sc.key.assign(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_LONG: {
		unsigned int tmp = htonl(lpsPropVal->Value.ul);
		sc.key.assign(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_R4: {
		double tmp = lpsPropVal->Value.flt;
		sc.key.assign(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_APPTIME:
	case PT_DOUBLE:
		sc.key.assign(R(&lpsPropVal->Value.dbl), sizeof(double));
	    break;
	case PT_CURRENCY:
		sc.isnull = true;
		break;
	case PT_SYSTIME: {
		unsigned int tmp = htonl(lpsPropVal->Value.hilo->hi);
		sc.key.reserve(sizeof(tmp) * 2);
		sc.key.assign(R(&tmp), sizeof(tmp));
		tmp = htonl(lpsPropVal->Value.hilo->lo);
		sc.key.append(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_I8: {
		unsigned int tmp = htonl(static_cast<unsigned int>(lpsPropVal->Value.li >> 32));
		sc.key.reserve(sizeof(tmp) * 2);
		sc.key.assign(R(&tmp), sizeof(tmp));
		tmp = htonl(static_cast<unsigned int>(lpsPropVal->Value.li));
		sc.key.append(R(&tmp), sizeof(tmp));
		break;
	}
	case PT_STRING8:
	case PT_UNICODE:
		// is this check needed here, or is it already checked 50 times along the way?
		if (!lpsPropVal->Value.lpszA) {
			sc.key.clear();
			sc.isnull = true;
			break;
		}
		sc.key = createSortKeyDataFromUTF8(lpsPropVal->Value.lpszA, 255, m_locale);
		break;
	case PT_CLSID:
	case PT_BINARY:
		sc.key.assign(reinterpret_cast<char *>(lpsPropVal->Value.bin->__ptr), lpsPropVal->Value.bin->__size);
		break;
	case PT_ERROR:
		sc.isnull = true;
		break;
	default:
		return KCERR_INVALID_TYPE;
	}
	return erSuccess;
#undef R
}

/**
 * The ECGenericObjectTable::GetSortFlags method gets tablerow flags for a property.
 * 
 * This flag alters the comparison behaviour of the ECKeyTable. This behaviour only needs
 * to be altered for float/double values and strings.
 * 
 * @param[in]	ulPropTag	The PropTag for which to get the flags.
 * @param[out]	lpFlags		The flags needed to properly compare properties for the provided PropTag.
 * 
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::GetSortFlags(unsigned int ulPropTag, unsigned char *lpFlags)
{
    switch(PROP_TYPE(ulPropTag)) {
        case PT_DOUBLE:
        case PT_APPTIME:
        case PT_R4:
		*lpFlags = TABLEROW_FLAG_FLOAT;
		return erSuccess;
        case PT_STRING8:
        case PT_UNICODE:
		*lpFlags = TABLEROW_FLAG_STRING;
		return erSuccess;
        default:
		*lpFlags = 0;
		return erSuccess;
    }
}

/**
 * The ECGenericObjectTable::Restrict methode applies a filter to a table
 *
 * The ECGenericObjectTable::Restrict methode applies a filter to a table, reducing 
 * the row set to only those rows matching the specified criteria.
 *
 * @param[in] lpsRestrict
 *				Pointer to a restrictTable structure defining the conditions of the filter. 
 *				Passing NULL in the lpsRestrict parameter removes the current filter.
 *
 * @return Kopano error code
 */
ECRESULT ECGenericObjectTable::Restrict(struct restrictTable *rt)
{
	ECRESULT er = erSuccess;
	scoped_rlock biglock(m_hLock);

	if(lpsSortOrderArray == NULL) {
		er = SetSortOrder(&sDefaultSortOrder, 0, 0);
		if(er != erSuccess)
			return er;
	}

	// No point turning off a restriction that's already off
	if (lpsRestrict == nullptr && rt == nullptr)
		return er;

	// Copy the restriction so we can remember it
	FreeRestrictTable(lpsRestrict);
	lpsRestrict = nullptr;
	if (rt != nullptr) {
		er = CopyRestrictTable(nullptr, rt, &lpsRestrict);
		if(er != erSuccess)
			return er;
	}

	er = ReloadKeyTable();
	if(er != erSuccess)
		return er;

	// Seek to row 0 (according to spec)
	SeekRow(BOOKMARK_BEGINNING, 0, nullptr);
	return er;
}

/*
 * Adds a set of rows to the key table, with the correct sort keys
 *
 * This function attempts to add the set of rows passed in lpRows to the table. The rows are added according
 * to sorting currently set and are tested against the current restriction. If bFilter is FALSE, then rows are not
 * tested against the current restriction and always added. A row may also not be added if data for the row is no
 * longer available, in which case the row is silently ignored.
 *
 * The bLoad parameter is not used here, but may be used be subclasses to determine if the rows are being added
 * due to an initial load of the table, or due to a later update.
 *
 * @param[in] lpRows Candidate rows to add to the table
 * @param[out] lpulLoaded Number of rows added to the table
 * @param[in] ulFlags Type of rows being added (May be 0, MSGFLAG_ASSOCIATED, MSGFLAG_DELETED or combination)
 * @param[in] bLoad TRUE if the rows being added are being added for an initial load or reload of the table, false for an update
 * @param[in] bOverride TRUE if the restriction set by Restrict() must be ignored, and the rows in lpRows must be filtered with lpOverrideRestrict. lpOverrideRestrict
 *                      MAY be NULL indicating that all rows in lpRows are to be added without filtering.
 * @param[in] lpOverrideRestrict Overrides the set restriction, using this one instead; the rows passed in lpRows are filtered with this restriction
 */
ECRESULT ECGenericObjectTable::AddRowKey(ECObjectTableList* lpRows, unsigned int *lpulLoaded, unsigned int ulFlags, bool bLoad, bool bOverride, struct restrictTable *lpOverrideRestrict)
{
	ECRESULT		er = erSuccess;
	gsoap_size_t ulFirstCol = 0, n = 0;
	unsigned int	ulLoaded = 0;
	bool bExist, fMatch = true, fHidden = false;
	ECObjectTableList sQueryRows;

	struct propTagArray	sPropTagArray = {0, 0};
	struct rowSet		*lpRowSet = NULL;
	struct propTagArray	*lpsRestrictPropTagArray = NULL;
	struct restrictTable *rt = nullptr;
	sObjectTableKey					sRowItem;
	
	ECCategory		*lpCategory = NULL;
	ulock_rec biglock(m_hLock);

	if (lpRows->empty()) {
		// nothing todo
		if(lpulLoaded)
			*lpulLoaded = 0;

		goto exit;
	}

	rt = bOverride ? lpOverrideRestrict : lpsRestrict;
	// We want all columns of the sort data, plus all the columns needed for restriction, plus the ID of the row
	if (lpsSortOrderArray != nullptr)
		sPropTagArray.__size = lpsSortOrderArray->__size; // sort columns
	else
		sPropTagArray.__size = 0;
	if (rt != nullptr) {
		er = GetRestrictPropTags(rt, nullptr, &lpsRestrictPropTagArray);
		if(er != erSuccess)
			goto exit;

		sPropTagArray.__size += lpsRestrictPropTagArray->__size; // restrict columns
	}
	
	++sPropTagArray.__size;	// for PR_INSTANCE_KEY
	++sPropTagArray.__size; // for PR_MESSAGE_FLAGS
	sPropTagArray.__ptr = s_alloc<unsigned int>(nullptr, sPropTagArray.__size);
	sPropTagArray.__ptr[n++]= PR_INSTANCE_KEY;
	if(m_ulCategories > 0)
		sPropTagArray.__ptr[n++]= PR_MESSAGE_FLAGS;

	ulFirstCol = n;
	
	// Put all the proptags of the sort columns in a proptag array
	if(lpsSortOrderArray)
		for (gsoap_size_t i = 0; i < lpsSortOrderArray->__size; ++i)
			sPropTagArray.__ptr[n++] = lpsSortOrderArray->__ptr[i].ulPropTag;

	// Same for restrict columns
	// Check if an item already exist
	if(lpsRestrictPropTagArray) {
		for (gsoap_size_t i = 0; i < lpsRestrictPropTagArray->__size; ++i) {
			bExist = false;
			for (gsoap_size_t j = 0; j < n; ++j)
				if(sPropTagArray.__ptr[j] == lpsRestrictPropTagArray->__ptr[i])
					bExist = true;
			if (!bExist)
				sPropTagArray.__ptr[n++] = lpsRestrictPropTagArray->__ptr[i];
		}
	}

	sPropTagArray.__size = n;

	for (auto iterRows = lpRows->cbegin(); iterRows != lpRows->cend(); ) {
		sQueryRows.clear();

		// if we use a restriction, memory usage goes up, so only fetch 20 rows at a time
		for (size_t i = 0; i < (lpsRestrictPropTagArray ? 20 : 256) && iterRows != lpRows->cend(); ++i) {
			sQueryRows.emplace_back(*iterRows);
			++iterRows;
		}

		// Now, query the database for the actual data
		er = m_lpfnQueryRowData(this, NULL, lpSession, &sQueryRows, &sPropTagArray, m_lpObjectData, &lpRowSet, true, lpsRestrictPropTagArray ? false : true /* FIXME */);
		if(er != erSuccess)
			goto exit;
			
		SUBRESTRICTIONRESULTS sub_results;
		if (rt != nullptr) {
			er = RunSubRestrictions(lpSession, m_lpObjectData, rt, &sQueryRows, m_locale, sub_results);
			if(er != erSuccess)
				goto exit;
		}

		// Send all this data to the internal key table
		auto cache = lpSession->GetSessionManager()->GetCacheManager();
		for (gsoap_size_t i = 0; i < lpRowSet->__size; ++i) {
			lpCategory = NULL;

			if (lpRowSet->__ptr[i].__ptr[0].ulPropTag != PR_INSTANCE_KEY) // Row completely not found
				continue;

			// is PR_INSTANCE_KEY
			memcpy(&sRowItem.ulObjId, lpRowSet->__ptr[i].__ptr[0].Value.bin->__ptr, sizeof(ULONG));
			memcpy(&sRowItem.ulOrderId, lpRowSet->__ptr[i].__ptr[0].Value.bin->__ptr+sizeof(ULONG), sizeof(ULONG));

			// Match the row with the restriction, if any
			if (rt != nullptr) {
				MatchRowRestrict(cache, &lpRowSet->__ptr[i], rt, &sub_results, m_locale, &fMatch);
				if (!fMatch) {
					// this row isn't in the table, as it does not match the restrict criteria. Remove it as if it had
					// been deleted if it was already in the table.
					DeleteRow(sRowItem, ulFlags);
					
					RemoveCategoryAfterRemoveRow(sRowItem, ulFlags);
					continue;
				}
			}

			if(m_ulCategories > 0) {
				bool bUnread = false;
				
				if((lpRowSet->__ptr[i].__ptr[1].Value.ul & MSGFLAG_READ) == 0)
					bUnread = true;

				// Update category for this row if required, and send notification if required
				AddCategoryBeforeAddRow(sRowItem, lpRowSet->__ptr[i].__ptr+ulFirstCol, lpsSortOrderArray->__size, ulFlags, bUnread, &fHidden, &lpCategory);
			}

			// Put the row into the key table and send notification if required
			AddRow(sRowItem, lpRowSet->__ptr[i].__ptr+ulFirstCol, lpsSortOrderArray->__size, ulFlags, fHidden, lpCategory);

			// Loaded one row
			++ulLoaded;
		}

		FreeRowSet(lpRowSet, true);
		lpRowSet = NULL;
	}

	if(lpulLoaded)
		*lpulLoaded = ulLoaded;

exit:
	biglock.unlock();
	FreeRowSet(lpRowSet, true);
	if(lpsRestrictPropTagArray != NULL)
		s_free(nullptr, lpsRestrictPropTagArray->__ptr);
	s_free(nullptr, lpsRestrictPropTagArray);
	s_free(nullptr, sPropTagArray.__ptr);
	return er;
}

// Actually add a row to the table
ECRESULT ECGenericObjectTable::AddRow(sObjectTableKey sRowItem, struct propVal *lpProps, unsigned int cProps, unsigned int ulFlags, bool fHidden, ECCategory *lpCategory)
{
	ECRESULT er = erSuccess;
    ECKeyTable::UpdateType ulAction;
    sObjectTableKey sPrevRow;

	UpdateKeyTableRow(lpCategory, &sRowItem, lpProps, cProps, fHidden, &sPrevRow, &ulAction);

    // Send notification if required
	if (ulAction != 0 && !fHidden && (ulFlags & OBJECTTABLE_NOTIFY))
		er = AddTableNotif(ulAction, sRowItem, &sPrevRow);
	return er;
}

// Actually remove a row from the table
ECRESULT ECGenericObjectTable::DeleteRow(sObjectTableKey sRow, unsigned int ulFlags)
{
    ECKeyTable::UpdateType ulAction;

    // Delete the row from the key table    
	auto er = lpKeyTable->UpdateRow(ECKeyTable::TABLE_ROW_DELETE, &sRow, {}, nullptr, false, &ulAction);
    if(er != erSuccess)
		return er;
    
    // Send notification if required
	if ((ulFlags & OBJECTTABLE_NOTIFY) && ulAction == ECKeyTable::TABLE_ROW_DELETE)
		AddTableNotif(ulAction, sRow, NULL);
	return erSuccess;
}

// Add a table notification by getting row data and sending it
ECRESULT ECGenericObjectTable::AddTableNotif(ECKeyTable::UpdateType ulAction, sObjectTableKey sRowItem, sObjectTableKey *lpsPrevRow)
{
    ECRESULT er = erSuccess;
    std::list<sObjectTableKey> lstItems;
	struct rowSet		*lpRowSetNotif = NULL;
    
    if(ulAction == ECKeyTable::TABLE_ROW_ADD || ulAction == ECKeyTable::TABLE_ROW_MODIFY) {
		lstItems.emplace_back(sRowItem);
		auto cleanup = make_scope_success([&]() { FreeRowSet(lpRowSetNotif, true); });
		er = m_lpfnQueryRowData(this, nullptr, lpSession, &lstItems, lpsPropTagArray, m_lpObjectData, &lpRowSetNotif, true, true);
        if(er != erSuccess)
			return er;
		if (lpRowSetNotif->__size != 1)
			return KCERR_NOT_FOUND;
        lpSession->AddNotificationTable(ulAction, m_ulObjType, m_ulTableId, &sRowItem, lpsPrevRow, &lpRowSetNotif->__ptr[0]);
    } else if(ulAction == ECKeyTable::TABLE_ROW_DELETE) {
        lpSession->AddNotificationTable(ulAction, m_ulObjType, m_ulTableId, &sRowItem, NULL, NULL);
    } else {
		return KCERR_NOT_FOUND;
    }
    return erSuccess;
}

ECRESULT ECGenericObjectTable::QueryRows(struct soap *soap, unsigned int ulRowCount, unsigned int ulFlags, struct rowSet **lppRowSet)
{
	// Retrieve the keyset from our KeyTable, and use that to retrieve the other columns
	// specified by SetColumns
	struct rowSet	*lpRowSet = NULL;
	ECObjectTableList	ecRowList;
	scoped_rlock biglock(m_hLock);

	ECRESULT er = Populate();
	if (er != erSuccess)
		return er;

	if(lpsSortOrderArray == NULL) {
		er = SetSortOrder(&sDefaultSortOrder, 0, 0);

		if(er != erSuccess)
			return er;
	}

	// Get the keys per row
	er = lpKeyTable->QueryRows(ulRowCount, &ecRowList, false, ulFlags);

	if(er != erSuccess)
		return er;
	assert(ecRowList.size() <= mapObjects.size() + m_mapCategories.size());
	if(ecRowList.empty()) {
		lpRowSet = s_alloc<rowSet>(soap);
		lpRowSet->__size = 0;
		lpRowSet->__ptr = NULL;
	} else {
		// We now have the ordering of the rows, all we have to do now is get the data. 
		er = m_lpfnQueryRowData(this, soap, lpSession, &ecRowList, lpsPropTagArray, m_lpObjectData, &lpRowSet, true, true);
	}

	if(er != erSuccess)
		return er;
	*lppRowSet = lpRowSet;
	return er;
}

ECRESULT ECGenericObjectTable::CreateBookmark(unsigned int* lpulbkPosition)
{
	scoped_rlock biglock(m_hLock);
	return lpKeyTable->CreateBookmark(lpulbkPosition);
}

ECRESULT ECGenericObjectTable::FreeBookmark(unsigned int ulbkPosition)
{
	scoped_rlock biglock(m_hLock);
	return lpKeyTable->FreeBookmark(ulbkPosition);
}

// Expand the category identified by sInstanceKey
ECRESULT ECGenericObjectTable::ExpandRow(struct soap *soap, xsd__base64Binary sInstanceKey, unsigned int ulRowCount, unsigned int ulFlags, struct rowSet **lppRowSet, unsigned int *lpulRowsLeft)
{
	sObjectTableKey sKey, sPrevRow;
    ECCategoryMap::const_iterator iterCategory;
    ECCategory *lpCategory = NULL;
    ECObjectTableList lstUnhidden;
    unsigned int ulRowsLeft = 0;
    struct rowSet *lpRowSet = NULL;
	scoped_rlock biglock(m_hLock);
    
	ECRESULT er = Populate();
    if(er != erSuccess)
		return er;

	if (sInstanceKey.__size != sizeof(sObjectTableKey))
		return KCERR_INVALID_PARAMETER;
	uint32_t tmp4;
	memcpy(&tmp4, sInstanceKey.__ptr, sizeof(tmp4));
	sKey.ulObjId = le32_to_cpu(tmp4);
	memcpy(&tmp4, sInstanceKey.__ptr + sizeof(tmp4), sizeof(tmp4));
	sKey.ulOrderId = le32_to_cpu(tmp4);
    iterCategory = m_mapCategories.find(sKey);
	if (iterCategory == m_mapCategories.cend())
		return KCERR_NOT_FOUND;

    lpCategory = iterCategory->second;

    // Unhide all rows under this category
    er = lpKeyTable->UnhideRows(&sKey, &lstUnhidden);
    if(er != erSuccess)
		return er;

    // Only return a maximum of ulRowCount rows
    if(ulRowCount < lstUnhidden.size()) {
        ulRowsLeft = lstUnhidden.size() - ulRowCount;
        lstUnhidden.resize(ulRowCount);
        
        // Put the keytable cursor just after the rows we will be returning, so the next queryrows() would return the remaining rows
        lpKeyTable->SeekRow(1, -ulRowsLeft, NULL);
    }
    
    // Get the row data to return, if required
    if(lppRowSet) {
        if(lstUnhidden.empty()){
    		lpRowSet = s_alloc<rowSet>(soap);
    		lpRowSet->__size = 0;
    		lpRowSet->__ptr = NULL;
    	} else {
    	    // Get data for unhidden rows
			er = m_lpfnQueryRowData(this, soap, lpSession, &lstUnhidden, lpsPropTagArray, m_lpObjectData, &lpRowSet, true, true);
    	}

    	if(er != erSuccess)
			return er;
    }

    lpCategory->m_fExpanded = true;

    if(lppRowSet)
        *lppRowSet = lpRowSet;
    if(lpulRowsLeft)
        *lpulRowsLeft = ulRowsLeft;
    return er;
}

// Collapse the category row identified by sInstanceKey
ECRESULT ECGenericObjectTable::CollapseRow(xsd__base64Binary sInstanceKey, unsigned int ulFlags, unsigned int *lpulRows)
{
	sObjectTableKey sKey, sPrevRow;
    ECCategoryMap::const_iterator iterCategory;
    ECCategory *lpCategory = NULL;
    ECObjectTableList lstHidden;
	scoped_rlock biglock(m_hLock);
    
	if (sInstanceKey.__size != sizeof(sObjectTableKey))
		return KCERR_INVALID_PARAMETER;
    
	ECRESULT er = Populate();
    if(er != erSuccess)
		return er;
	uint32_t tmp4;
	memcpy(&tmp4, sInstanceKey.__ptr, sizeof(tmp4));
	sKey.ulObjId = le32_to_cpu(tmp4);
	memcpy(&tmp4, sInstanceKey.__ptr + sizeof(tmp4), sizeof(tmp4));
	sKey.ulOrderId = le32_to_cpu(tmp4);
    iterCategory = m_mapCategories.find(sKey);
	if (iterCategory == m_mapCategories.cend())
		return KCERR_NOT_FOUND;

    lpCategory = iterCategory->second;

    // Hide the rows under this category
    er = lpKeyTable->HideRows(&sKey, &lstHidden);
    if(er != erSuccess)
		return er;
    
    // Mark the category as collapsed
    lpCategory->m_fExpanded = false;
    
    // Loop through the hidden rows to see if we have hidden any categories. If so, mark them as
    // collapsed
    for (auto iterHidden = lstHidden.cbegin(); iterHidden != lstHidden.cend(); ++iterHidden) {
        iterCategory = m_mapCategories.find(*iterHidden);
        
        if (iterCategory != m_mapCategories.cend())
            iterCategory->second->m_fExpanded = false;
    }

    if(lpulRows)
        *lpulRows = lstHidden.size();
    return er;
}

ECRESULT ECGenericObjectTable::GetCollapseState(struct soap *soap, struct xsd__base64Binary sBookmark, struct xsd__base64Binary *lpsCollapseState)
{
    struct collapseState sCollapseState;
    int n = 0;
    std::ostringstream os;
    sObjectTableKey sKey;
    struct rowSet *lpsRowSet = NULL;
    
    struct soap xmlsoap;	// static, so c++ inits struct, no need for soap init
	ulock_rec biglock(m_hLock);
    
	auto er = Populate();
    if(er != erSuccess)
        goto exit;

    memset(&sCollapseState, 0, sizeof(sCollapseState));

    // Generate a binary collapsestate which is simply an XML stream of all categories with their collapse state
    sCollapseState.sCategoryStates.__size = m_mapCategories.size();
    sCollapseState.sCategoryStates.__ptr = s_alloc<struct categoryState>(soap, sCollapseState.sCategoryStates.__size);

    memset(sCollapseState.sCategoryStates.__ptr, 0, sizeof(struct categoryState) * sCollapseState.sCategoryStates.__size);

	for (const auto &p : m_mapCategories) {
		sCollapseState.sCategoryStates.__ptr[n].fExpanded = p.second->m_fExpanded;
		sCollapseState.sCategoryStates.__ptr[n].sProps.__ptr = s_alloc<struct propVal>(soap, p.second->m_cProps);
		memset(sCollapseState.sCategoryStates.__ptr[n].sProps.__ptr, 0, sizeof(struct propVal) * p.second->m_cProps);
		for (unsigned int i = 0; i < p.second->m_cProps; ++i) {
			er = CopyPropVal(&p.second->m_lpProps[i], &sCollapseState.sCategoryStates.__ptr[n].sProps.__ptr[i], soap);
            if (er != erSuccess)
                goto exit;
        }
		sCollapseState.sCategoryStates.__ptr[n].sProps.__size = p.second->m_cProps;
        ++n;
    }

    // We also need to save the sort keys for the given bookmark, so that we can return a bookmark when SetCollapseState is called
    if(sBookmark.__size == 8) {
		uint32_t tmp4;
		memcpy(&tmp4, sBookmark.__ptr, sizeof(tmp4));
		sKey.ulObjId = le32_to_cpu(tmp4);
		memcpy(&tmp4, sBookmark.__ptr + sizeof(tmp4), sizeof(tmp4));
		sKey.ulOrderId = le32_to_cpu(tmp4);
        
        // Go the the row requested
        if(lpKeyTable->SeekId(&sKey) == erSuccess) {
            // If the row exists, we simply get the data from the properties of this row, including all properties used
            // in the current sort.
            ECObjectTableList list;
			list.emplace_back(sKey);
            er = m_lpfnQueryRowData(this, &xmlsoap, lpSession, &list, lpsPropTagArray, m_lpObjectData, &lpsRowSet, false, true);
            if(er != erSuccess)
                goto exit;
                
            // Copy row 1 from rowset into our bookmark props.
            sCollapseState.sBookMarkProps = lpsRowSet->__ptr[0];
            
            // Free of lpsRowSet coupled to xmlsoap so not explicitly needed
        }
    }
    
	soap_set_mode(&xmlsoap, SOAP_XML_TREE | SOAP_C_UTFSTRING);
    xmlsoap.os = &os;
    
    soap_serialize_collapseState(&xmlsoap, &sCollapseState);
    soap_begin_send(&xmlsoap);
    soap_put_collapseState(&xmlsoap, &sCollapseState, "CollapseState", NULL);
    soap_end_send(&xmlsoap);
    
    // os.str() now contains serialized objects, copy into return structure
    lpsCollapseState->__size = os.str().size();
    lpsCollapseState->__ptr = s_alloc<unsigned char>(soap, os.str().size());
    memcpy(lpsCollapseState->__ptr, os.str().c_str(), os.str().size());

exit:
	soap_destroy(&xmlsoap);
	soap_end(&xmlsoap);
	// static struct, so c++ destructor frees memory
	biglock.unlock();
    return er;
}

ECRESULT ECGenericObjectTable::SetCollapseState(struct xsd__base64Binary sCollapseState, unsigned int *lpulBookmark)
{
    struct soap xmlsoap;
	struct collapseState cst;
    std::istringstream is(std::string((const char *)sCollapseState.__ptr, sCollapseState.__size));
	sObjectTableKey sKey;
    struct xsd__base64Binary sInstanceKey;
	ulock_rec giblock(m_hLock);
    
	auto er = Populate();
    if(er != erSuccess)
        goto exit;

    // The collapse state is the serialized collapse state as returned by GetCollapseState(), which we need to parse here
	soap_set_mode(&xmlsoap, SOAP_XML_TREE | SOAP_C_UTFSTRING);
    xmlsoap.is = &is;
    
	soap_default_collapseState(&xmlsoap, &cst);
    if (soap_begin_recv(&xmlsoap) != 0) {
		er = KCERR_NETWORK_ERROR;
		goto exit;
    }
	soap_get_collapseState(&xmlsoap, &cst, "CollapseState", NULL);
    if(xmlsoap.error) {
		er = KCERR_DATABASE_ERROR;
		ec_log_crit("ECGenericObjectTable::SetCollapseState(): xmlsoap error %d", xmlsoap.error);
		goto exit;
    }
    
	/* @cst now contains the collapse state for all categories, apply them now. */
	for (gsoap_size_t i = 0; i < cst.sCategoryStates.__size; ++i) {
		const auto &catprop = cst.sCategoryStates.__ptr[i].sProps;
		std::vector<ECSortCol> zort(catprop.__size);

		// Get the binary sortkeys for all properties
		for (gsoap_size_t n = 0; n < catprop.__size; ++n) {
			if (GetBinarySortKey(&catprop.__ptr[n], zort[n]) != erSuccess)
				goto next;
			if (GetSortFlags(catprop.__ptr[n].ulPropTag, &zort[n].flags) != erSuccess)
				goto next;
		}

		// Find the category and expand or collapse it. If it's not there anymore, just ignore it.
		if (lpKeyTable->Find(zort, &sKey) == erSuccess) {
            sInstanceKey.__size = 8;
			sInstanceKey.__ptr = (unsigned char *)&sKey;
            
			if (cst.sCategoryStates.__ptr[i].fExpanded)
				ExpandRow(NULL, sInstanceKey, 0, 0, NULL, NULL);
			else
				CollapseRow(sInstanceKey, 0, NULL);
		}
 next: ;
    }
    
    // There is also a row stored in the collapse state which we have to create a bookmark at and return that. If it is not found,
    // we return a bookmark to the nearest next row.
	if (cst.sBookMarkProps.__size > 0) {
		std::vector<ECSortCol> zort(cst.sBookMarkProps.__size);
		gsoap_size_t n;

		for (n = 0; n < cst.sBookMarkProps.__size; ++n) {
			if (GetBinarySortKey(&cst.sBookMarkProps.__ptr[n], zort[n]) != erSuccess)
				break;
			if (GetSortFlags(cst.sBookMarkProps.__ptr[n].ulPropTag, &zort[n].flags) != erSuccess)
				break;
		}
    
        // If an error occurred in the previous loop, just ignore the whole bookmark thing, just return bookmark 0 (BOOKMARK_BEGINNING)    
		if (n == cst.sBookMarkProps.__size) {
			lpKeyTable->LowerBound(zort);
            lpKeyTable->CreateBookmark(lpulBookmark);
        }
    }
    
	/*
	 * We do not generate notifications for this event, just like
	 * ExpandRow and CollapseRow. You just need to reload the table
	 * yourself.
	 */
	if (soap_end_recv(&xmlsoap) != 0)
		er = KCERR_NETWORK_ERROR;
    
exit:
	soap_destroy(&xmlsoap);
	soap_end(&xmlsoap);
	giblock.unlock();
	return er;
}

ECRESULT ECGenericObjectTable::UpdateRow(unsigned int ulType, unsigned int ulObjId, unsigned int ulFlags)
{
    std::list<unsigned int> lstObjId;
    
	lstObjId.emplace_back(ulObjId);
	return UpdateRows(ulType, &lstObjId, ulFlags, false);
}

/**
 * Load a set of rows into the table
 *
 * This is called to populate a table initially, it is functionally equivalent to calling UpdateRow() repeatedly
 * for each item in lstObjId with ulType set to ECKeyTable::TABLE_ROW_ADD.
 *
 * @param lstObjId List of hierarchy IDs for the objects to load
 * @param ulFlags 0, MSGFLAG_DELETED, MAPI_ASSOCIATED or combination
 */
ECRESULT ECGenericObjectTable::LoadRows(std::list<unsigned int> *lstObjId, unsigned int ulFlags)
{
	return UpdateRows(ECKeyTable::TABLE_ROW_ADD, lstObjId, ulFlags, true);
}

/**
 * Update one or more rows in a table
 *
 * This function adds, modifies or removes objects from a table. The normal function of this is that normally
 * either multiple objects are added, or a single object is removed or updated. The result of such an update can
 * be complex, for example adding an item to a table may cause multiple rows to be added when using categorization
 * or multi-valued properties. In the same way, an update may generate multiple notifications if category headers are
 * involved or when the update modifies the sorting position of the row.
 *
 * Rows are also checked for read permissions here before being added to the table.
 *
 * The bLoad parameter is not used in the ECGenericObjectTable implementation, but simply passed to AddRowKey to indicate
 * whether the rows are being updated due to a change or due to initial loading. The parameter is also not used in AddRowKey
 * but can be used by subclasses to generate different behaviour on the initial load compared to later updates.
 *
 * @param ulType ECKeyTable::TABLE_ROW_ADD, TABLE_ROW_DELETE or TABLE_ROW_MODIFY
 * @param lstObjId List of objects to add, modify or delete
 * @param ulFlags Flags for the objects in lstObjId (0, MSGFLAG_DELETED, MAPI_ASSOCIATED)
 * @param bLoad Indicates that this is the initial load or reload of the table, and not an update
 */
ECRESULT ECGenericObjectTable::UpdateRows(unsigned int ulType, std::list<unsigned int> *lstObjId, unsigned int ulFlags, bool bLoad)
{
	ECRESULT				er = erSuccess;
	unsigned int ulRead = 0;
	std::list<unsigned int> lstFilteredIds;
	ECObjectTableList ecRowsItem, ecRowsDeleted;
	sObjectTableKey		sRow;
	scoped_rlock biglock(m_hLock);

	// Perform security checks for this object
	switch(ulType) {
    case ECKeyTable::TABLE_CHANGE:
        // Accept table change in all cases
        break;
    case ECKeyTable::TABLE_ROW_MODIFY:
    case ECKeyTable::TABLE_ROW_ADD:
        // Filter out any item we cannot access (for example, in search-results tables)
		for (const auto &obj_id : *lstObjId)
			if (CheckPermissions(obj_id) == erSuccess)
				lstFilteredIds.emplace_back(obj_id);
        // Use our filtered list now
        lstObjId = &lstFilteredIds;
    	break;
    case ECKeyTable::TABLE_ROW_DELETE:
	    // You may always delete a row
        break;
    }

	if(lpsSortOrderArray == NULL) {
		er = SetSortOrder(&sDefaultSortOrder, 0, 0);

		if(er != erSuccess)
			return er;
	}

	// Update a row in the keyset as having changed. Get the data from the DB and send it to the KeyTable.

	switch(ulType) {
	case ECKeyTable::TABLE_ROW_DELETE:
		// Delete the object ID from our object list, and all items with that object ID (including various order IDs)
		for (const auto &obj_id : *lstObjId) {
			for (auto mo = mapObjects.find(sObjectTableKey(obj_id, 0));
			     mo != mapObjects.cend() && mo->first.ulObjId == obj_id; ++mo)
				ecRowsItem.emplace_back(mo->first);
            
			for (const auto &row : ecRowsItem) {
				mapObjects.erase(row);
			/* Delete the object from the active keyset */
			DeleteRow(row, ulFlags);
			RemoveCategoryAfterRemoveRow(row, ulFlags);
            }
        }
		break;

	case ECKeyTable::TABLE_ROW_MODIFY:
	case ECKeyTable::TABLE_ROW_ADD: {
		std::list<unsigned int> ids;
		std::map<unsigned int, unsigned int> count;

		for (const auto &obj_id : *lstObjId) {
			/* Add the object to our list of objects */
			ecRowsItem.emplace_back(obj_id, 0);
			if (!IsMVSet())
				continue;
			ids.emplace_back(obj_id);
		}

		// get new mvprop count
		if (ids.size() > 0) {
			er = GetMVRowCount(std::move(ids), count);
			if (er != erSuccess)
				return er;
		}

		for (const auto &pair : count) {
			auto obj_id = pair.first;
			auto cMVNew = pair.second;
			// get old mvprops count
			auto cMVOld = 0;
			for (auto iterMapObject = mapObjects.find(sObjectTableKey(obj_id, 0));
			     iterMapObject != mapObjects.cend();
			     ++iterMapObject) {
				if (iterMapObject->first.ulObjId != obj_id)
					break;
				++cMVOld;
				if (cMVOld <= cMVNew || !(ulFlags & OBJECTTABLE_NOTIFY))
					continue;
				auto iterToDelete = iterMapObject;
				--iterMapObject;
				sRow = iterToDelete->first;
				// Delete of map
				mapObjects.erase(iterToDelete->first);
				DeleteRow(sRow, ulFlags);
				RemoveCategoryAfterRemoveRow(sRow, ulFlags);
			}
			sRow = sObjectTableKey(obj_id, 0);
			for (unsigned int i = 1; i < cMVNew; ++i) { // 0 already added
				sRow.ulOrderId = i;
				ecRowsItem.emplace_back(sRow);
			}
		}
        
        // Remember that the specified row is available		
		for (const auto &row : ecRowsItem)
			mapObjects[row] = 1;
		// Add/modify the key in the keytable
		er = AddRowKey(&ecRowsItem, &ulRead, ulFlags, bLoad, false, NULL);
		if(er != erSuccess)
			return er;
		break;
	}
	case ECKeyTable::TABLE_CHANGE:
		// The whole table needs to be reread
		Clear();
		er = Load();
		lpSession->AddNotificationTable(ulType, m_ulObjType, m_ulTableId, NULL, NULL, NULL);

		break;
	}
	return er;
}

ECRESULT ECGenericObjectTable::GetRestrictPropTagsRecursive(const struct restrictTable *lpsRestrict,
    std::list<ULONG> *lpPropTags, ULONG ulLevel)
{
	ECRESULT		er = erSuccess;

	if (ulLevel > RESTRICT_MAX_DEPTH)
		return KCERR_TOO_COMPLEX;

	switch(lpsRestrict->ulType) {
	case RES_COMMENT:
	    er = GetRestrictPropTagsRecursive(lpsRestrict->lpComment->lpResTable, lpPropTags, ulLevel+1);
	    if(er != erSuccess)
			return er;
	    break;
	    
	case RES_OR:
		for (gsoap_size_t i = 0; i < lpsRestrict->lpOr->__size; ++i) {
			er = GetRestrictPropTagsRecursive(lpsRestrict->lpOr->__ptr[i], lpPropTags, ulLevel+1);

			if(er != erSuccess)
				return er;
		}
		break;	
		
	case RES_AND:
		for (gsoap_size_t i = 0; i < lpsRestrict->lpAnd->__size; ++i) {
			er = GetRestrictPropTagsRecursive(lpsRestrict->lpAnd->__ptr[i], lpPropTags, ulLevel+1);

			if(er != erSuccess)
				return er;
		}
		break;	

	case RES_NOT:
		er = GetRestrictPropTagsRecursive(lpsRestrict->lpNot->lpNot, lpPropTags, ulLevel+1);
		if(er != erSuccess)
			return er;
		break;

	case RES_CONTENT:
		lpPropTags->emplace_back(lpsRestrict->lpContent->ulPropTag);
		break;

	case RES_PROPERTY:
		if(PROP_ID(lpsRestrict->lpProp->ulPropTag) == PROP_ID(PR_ANR))
			lpPropTags->insert(lpPropTags->end(), sANRProps, sANRProps + ARRAY_SIZE(sANRProps));
			
		else {
			lpPropTags->emplace_back(lpsRestrict->lpProp->lpProp->ulPropTag);
			lpPropTags->emplace_back(lpsRestrict->lpProp->ulPropTag);
		}
		break;

	case RES_COMPAREPROPS:
		lpPropTags->emplace_back(lpsRestrict->lpCompare->ulPropTag1);
		lpPropTags->emplace_back(lpsRestrict->lpCompare->ulPropTag2);
		break;

	case RES_BITMASK:
		lpPropTags->emplace_back(lpsRestrict->lpBitmask->ulPropTag);
		break;

	case RES_SIZE:
		lpPropTags->emplace_back(lpsRestrict->lpSize->ulPropTag);
		break;

	case RES_EXIST:
		lpPropTags->emplace_back(lpsRestrict->lpExist->ulPropTag);
		break;

	case RES_SUBRESTRICTION:
		lpPropTags->emplace_back(PR_ENTRYID); // we need the entryid in subrestriction searches, because we need to know which object to subsearch
		break;
	}
	return erSuccess;
}

/**
 * Generate a list of all properties required to evaluate a restriction
 *
 * The list of properties returned are the maximum set of properties required to evaluate the given restriction. Additionally
 * a list of properties can be added to the front of the property set. If the property is required both through the prefix list
 * and through the restriction, it is included only once in the property list.
 *
 * The order of the first N properties in the returned proptag array are guaranteed to be equal to the N items in lstPrefix
 *
 * @param[in] lpsRestrict Restriction tree to evaluate
 * @param[in] lstPrefix NULL or list of property tags to prefix
 * @param[out] lppPropTags PropTagArray with proptags from lpsRestrict and lstPrefix
 * @return ECRESULT
 */
ECRESULT ECGenericObjectTable::GetRestrictPropTags(const struct restrictTable *lpsRestrict,
    std::list<ULONG> *lstPrefix, struct propTagArray **lppPropTags)
{
	struct propTagArray *lpPropTagArray;

	std::list<ULONG> 	lstPropTags;

	// Just go through all the properties, adding the properties one-by-one 
	auto er = GetRestrictPropTagsRecursive(lpsRestrict, &lstPropTags, 0);
	if (er != erSuccess)
		return er;

	// Sort and unique-ize the properties (order is not important in the returned array)
	lstPropTags.sort();
	lstPropTags.unique();
	
	// Prefix if needed
	if(lstPrefix)
		lstPropTags.insert(lstPropTags.begin(), lstPrefix->begin(), lstPrefix->end());
	lpPropTagArray = s_alloc<propTagArray>(nullptr);
	// Put the data into an array
	lpPropTagArray->__size = lstPropTags.size();
	lpPropTagArray->__ptr = s_alloc<unsigned int>(nullptr, lpPropTagArray->__size);
	copy(lstPropTags.begin(), lstPropTags.end(), lpPropTagArray->__ptr);
	*lppPropTags = lpPropTagArray;
	return erSuccess;
}

// Simply matches the restriction with the given data. Make sure you pass all the data
// needed for the restriction in lpPropVals. (missing columns do not match, ever.)
ECRESULT ECGenericObjectTable::MatchRowRestrict(ECCacheManager *lpCacheManager,
    propValArray *lpPropVals, const struct restrictTable *lpsRestrict,
    const SUBRESTRICTIONRESULTS *lpSubResults, const ECLocale &locale,
    bool *lpfMatch, unsigned int *lpulSubRestriction)
{
	ECRESULT		er = erSuccess;
	bool			fMatch = false;
	int				lCompare = 0;
	unsigned int	ulSize = 0;
	struct propVal	*lpProp = NULL;
	struct propVal	*lpProp2 = NULL;

	char* lpSearchString;
	char* lpSearchData;
	unsigned int ulSearchDataSize;
	unsigned int ulSearchStringSize;
	ULONG ulPropType;
	ULONG ulFuzzyLevel;
	unsigned int ulSubRestrict = 0;
	entryId sEntryId;
	unsigned int ulResId = 0;
	unsigned int ulPropTagRestrict;
	unsigned int ulPropTagValue;
	
	if(lpulSubRestriction == NULL) // called externally
	    lpulSubRestriction = &ulSubRestrict;
	    
	switch(lpsRestrict->ulType) {
	case RES_COMMENT:
		if (lpsRestrict->lpComment == NULL)
			return KCERR_INVALID_TYPE;
        er = MatchRowRestrict(lpCacheManager, lpPropVals, lpsRestrict->lpComment->lpResTable, lpSubResults, locale, &fMatch, lpulSubRestriction);
        break;
        
	case RES_OR:
		if (lpsRestrict->lpOr == NULL)
			return KCERR_INVALID_TYPE;
		fMatch = false;

		for (gsoap_size_t i = 0; i < lpsRestrict->lpOr->__size; ++i) {
			er = MatchRowRestrict(lpCacheManager, lpPropVals, lpsRestrict->lpOr->__ptr[i], lpSubResults, locale, &fMatch, lpulSubRestriction);

			if(er != erSuccess)
				return er;
			if(fMatch) // found a restriction in an OR which matches, ignore the rest of the query
				break;
		}
		break;
	case RES_AND:
		if (lpsRestrict->lpAnd == NULL)
			return KCERR_INVALID_TYPE;
		fMatch = true;

		for (gsoap_size_t i = 0; i < lpsRestrict->lpAnd->__size; ++i) {
			er = MatchRowRestrict(lpCacheManager, lpPropVals, lpsRestrict->lpAnd->__ptr[i], lpSubResults, locale, &fMatch, lpulSubRestriction);

			if(er != erSuccess)
				return er;
			if(!fMatch) // found a restriction in an AND which doesn't match, ignore the rest of the query
				break;
		}
		break;

	case RES_NOT:
		if (lpsRestrict->lpNot == NULL)
			return KCERR_INVALID_TYPE;
		er = MatchRowRestrict(lpCacheManager, lpPropVals, lpsRestrict->lpNot->lpNot, lpSubResults, locale, &fMatch, lpulSubRestriction);
		if(er != erSuccess)
			return er;

		fMatch = !fMatch;
		break;

	case RES_CONTENT: {
		if (lpsRestrict->lpContent == NULL ||
		    lpsRestrict->lpContent->lpProp == NULL)
			return KCERR_INVALID_TYPE;
		// FIXME: FL_IGNORENONSPACE and FL_LOOSE are ignored
		ulPropTagRestrict = lpsRestrict->lpContent->ulPropTag;
		ulPropTagValue = lpsRestrict->lpContent->lpProp->ulPropTag;

		// use the same string type in compares
		if ((PROP_TYPE(ulPropTagRestrict) & PT_MV_STRING8) == PT_STRING8)
			ulPropTagRestrict = CHANGE_PROP_TYPE(ulPropTagRestrict, PT_TSTRING);
		else if ((PROP_TYPE(ulPropTagRestrict) & PT_MV_STRING8) == PT_MV_STRING8)
			ulPropTagRestrict = CHANGE_PROP_TYPE(ulPropTagRestrict, PT_MV_TSTRING);

		// @todo are MV properties in the compare prop allowed?
		if ((PROP_TYPE(ulPropTagValue) & PT_MV_STRING8) == PT_STRING8)
			ulPropTagValue = CHANGE_PROP_TYPE(ulPropTagValue, PT_TSTRING);
		else if ((PROP_TYPE(ulPropTagValue) & PT_MV_STRING8) == PT_MV_STRING8)
			ulPropTagValue = CHANGE_PROP_TYPE(ulPropTagValue, PT_MV_TSTRING);

		if( PROP_TYPE(ulPropTagRestrict) != PT_TSTRING && 
			PROP_TYPE(ulPropTagRestrict) != PT_BINARY &&
			PROP_TYPE(ulPropTagRestrict) != PT_MV_TSTRING &&
			PROP_TYPE(ulPropTagRestrict) != PT_MV_BINARY &&
			lpsRestrict->lpContent->lpProp != NULL)
		{
			assert(false);
			fMatch = false;
			break;
		}

		// find using original proptag from restriction
		lpProp = FindProp(lpPropVals, lpsRestrict->lpContent->ulPropTag);

		if(lpProp == NULL) {
			fMatch = false;
			break;
		}
		unsigned int ulScan = 1;
		if (ulPropTagRestrict & MV_FLAG)
		{
			if (PROP_TYPE(ulPropTagRestrict) == PT_MV_TSTRING)
				ulScan = lpProp->Value.mvszA.__size;
			else
				ulScan = lpProp->Value.mvbin.__size;
		}
		ulPropType = PROP_TYPE(ulPropTagRestrict) & ~MVI_FLAG;
		if (PROP_TYPE(ulPropTagValue) == PT_TSTRING) {
			lpSearchString = lpsRestrict->lpContent->lpProp->Value.lpszA;
			ulSearchStringSize = (lpSearchString) ? strlen(lpSearchString) : 0;
		} else {
			lpSearchString = (char *)lpsRestrict->lpContent->lpProp->Value.bin->__ptr;
			ulSearchStringSize = lpsRestrict->lpContent->lpProp->Value.bin->__size;
		}

		// Default match is false
		fMatch = false;
		for (unsigned int ulPos = 0; ulPos < ulScan; ++ulPos) {
			if (ulPropTagRestrict & MV_FLAG)
			{
				if (PROP_TYPE(ulPropTagRestrict) == PT_MV_TSTRING) {
					lpSearchData = lpProp->Value.mvszA.__ptr[ulPos];
					ulSearchDataSize = (lpSearchData) ? strlen(lpSearchData) : 0;
				} else {
					lpSearchData = (char *)lpProp->Value.mvbin.__ptr[ulPos].__ptr;
					ulSearchDataSize = lpProp->Value.mvbin.__ptr[ulPos].__size;
				}
			} else if (PROP_TYPE(ulPropTagRestrict) == PT_TSTRING) {
				lpSearchData = lpProp->Value.lpszA;
				ulSearchDataSize = (lpSearchData) ? strlen(lpSearchData) : 0;
			} else {
				lpSearchData = (char *)lpProp->Value.bin->__ptr;
				ulSearchDataSize = lpProp->Value.bin->__size;
			}

			ulFuzzyLevel = lpsRestrict->lpContent->ulFuzzyLevel;
			switch (ulFuzzyLevel & 0xFFFF) {
			case FL_FULLSTRING:
				if (ulSearchDataSize != ulSearchStringSize)
					break;
				if ((ulPropType == PT_TSTRING && (ulFuzzyLevel & FL_IGNORECASE) && u8_iequals(lpSearchData, lpSearchString, locale)) ||
				    (ulPropType == PT_TSTRING && ((ulFuzzyLevel & FL_IGNORECASE) == 0) && u8_equals(lpSearchData, lpSearchString, locale)) ||
				    (ulPropType != PT_TSTRING && memcmp(lpSearchData, lpSearchString, ulSearchDataSize) == 0))
					fMatch = true;
				break;
			case FL_PREFIX:
				if (ulSearchDataSize < ulSearchStringSize)
					break;
				if ((ulPropType == PT_TSTRING && (ulFuzzyLevel & FL_IGNORECASE) && u8_istartswith(lpSearchData, lpSearchString, locale)) ||
				    (ulPropType == PT_TSTRING && ((ulFuzzyLevel & FL_IGNORECASE) == 0) && u8_startswith(lpSearchData, lpSearchString, locale)) ||
				    (ulPropType != PT_TSTRING && memcmp(lpSearchData, lpSearchString, ulSearchStringSize) == 0))
					fMatch = true;
				break;
			case FL_SUBSTRING:
				if ((ulPropType == PT_TSTRING && (ulFuzzyLevel & FL_IGNORECASE) && u8_icontains(lpSearchData, lpSearchString, locale)) ||
				    (ulPropType == PT_TSTRING && ((ulFuzzyLevel & FL_IGNORECASE) == 0) && u8_contains(lpSearchData, lpSearchString, locale)) ||
				    (ulPropType != PT_TSTRING && memsubstr(lpSearchData, ulSearchDataSize, lpSearchString, ulSearchStringSize) == 0))
					fMatch = true;
				break;
			}
			if (fMatch)
				break;
		}
		break;
	}
	case RES_PROPERTY:
		if (lpsRestrict->lpProp == NULL ||
		    lpsRestrict->lpProp->lpProp == NULL)
			return KCERR_INVALID_TYPE;

		ulPropTagRestrict = lpsRestrict->lpProp->ulPropTag;
		ulPropTagValue = lpsRestrict->lpProp->lpProp->ulPropTag;

		// use the same string type in compares
		if ((PROP_TYPE(ulPropTagRestrict) & PT_MV_STRING8) == PT_STRING8)
			ulPropTagRestrict = CHANGE_PROP_TYPE(ulPropTagRestrict, PT_TSTRING);
		else if ((PROP_TYPE(ulPropTagRestrict) & PT_MV_STRING8) == PT_MV_STRING8)
			ulPropTagRestrict = CHANGE_PROP_TYPE(ulPropTagRestrict, PT_MV_TSTRING);

		if (PROP_TYPE(ulPropTagValue) == PT_STRING8)
			ulPropTagValue = CHANGE_PROP_TYPE(ulPropTagValue, PT_TSTRING);

		if((PROP_TYPE(ulPropTagRestrict) & ~MV_FLAG) != PROP_TYPE(ulPropTagValue))
			// cannot compare two different types, except mvprop -> prop
			return KCERR_INVALID_TYPE;
#if 1 /* HAVE_REGEX_H */
		if(lpsRestrict->lpProp->ulType == RELOP_RE) {
		    regex_t reg;

			// find using original restriction proptag
			lpProp = FindProp(lpPropVals, lpsRestrict->lpProp->ulPropTag);
			if(lpProp == NULL) {
				fMatch = false;
				break;
			}

			// @todo add support for ulPropTagRestrict PT_MV_TSTRING
			if (PROP_TYPE(ulPropTagValue) != PT_TSTRING ||
			    PROP_TYPE(ulPropTagRestrict) != PT_TSTRING)
				return KCERR_INVALID_TYPE;
            
            if(regcomp(&reg, lpsRestrict->lpProp->lpProp->Value.lpszA, REG_NOSUB | REG_NEWLINE | REG_ICASE) != 0) {
                fMatch = false;
                break;
            }
            
            if(regexec(&reg, lpProp->Value.lpszA, 0, NULL, 0) == 0)
                fMatch = true;
                
            regfree(&reg);
            
            // Finished for this restriction
            break;
        }
#endif

		if(PROP_ID(ulPropTagRestrict) == PROP_ID(PR_ANR))
		{
			for (size_t j = 0; j < ARRAY_SIZE(sANRProps); ++j) {
				lpProp = FindProp(lpPropVals, sANRProps[j]);

                // We need this because CompareProp will fail if the types are not the same
				if (lpProp == nullptr)
					continue;
				lpProp->ulPropTag = lpsRestrict->lpProp->lpProp->ulPropTag;
				CompareProp(lpProp, lpsRestrict->lpProp->lpProp, locale, &lCompare); // IGNORE error
                	
				// PR_ANR has special semantics, lCompare is 1 if the substring is found, 0 if not
				
				// Note that RELOP_EQ will work as expected, but RELOP_GT and RELOP_LT will
				// not work. Use of these is undefined anyway. RELOP_NE is useless since one of the
				// strings will definitely not match, so RELOP_NE will almost match.
				lCompare = lCompare ? 0 : -1;
                    
                fMatch = match(lpsRestrict->lpProp->ulType, lCompare);
                
                if(fMatch)
                    break;
            }
            
            // Finished for this restriction
            break;
		}

		// find using original restriction proptag
		lpProp = FindProp(lpPropVals, lpsRestrict->lpProp->ulPropTag);
		if (lpProp == NULL) {
			if (lpsRestrict->lpProp->ulType == RELOP_NE)
				fMatch = true;
			else
				fMatch = false;
			break;
		}

		if ((ulPropTagRestrict & MV_FLAG)) {
			er = CompareMVPropWithProp(lpProp, lpsRestrict->lpProp->lpProp, lpsRestrict->lpProp->ulType, locale, &fMatch);
			if (er != erSuccess)
			{
				assert(false);
				er = erSuccess;
				fMatch = false;
				break;
			}
			break;
		}
		er = CompareProp(lpProp, lpsRestrict->lpProp->lpProp, locale, &lCompare);
		if (er != erSuccess)
		{
			assert(false);
			er = erSuccess;
			fMatch = false;
			break;
		}
		fMatch = match(lpsRestrict->lpProp->ulType, lCompare);
		break;
		
	case RES_COMPAREPROPS:
		if (lpsRestrict->lpCompare == NULL)
			return KCERR_INVALID_TYPE;

		unsigned int ulPropTag1;
		unsigned int ulPropTag2;

		ulPropTag1 = lpsRestrict->lpCompare->ulPropTag1;
		ulPropTag2 = lpsRestrict->lpCompare->ulPropTag2;

		// use the same string type in compares
		if ((PROP_TYPE(ulPropTag1) & PT_MV_STRING8) == PT_STRING8)
			ulPropTag1 = CHANGE_PROP_TYPE(ulPropTag1, PT_TSTRING);
		else if ((PROP_TYPE(ulPropTag1) & PT_MV_STRING8) == PT_MV_STRING8)
			ulPropTag1 = CHANGE_PROP_TYPE(ulPropTag1, PT_MV_TSTRING);

		// use the same string type in compares
		if ((PROP_TYPE(ulPropTag2) & PT_MV_STRING8) == PT_STRING8)
			ulPropTag2 = CHANGE_PROP_TYPE(ulPropTag2, PT_TSTRING);
		else if ((PROP_TYPE(ulPropTag2) & PT_MV_STRING8) == PT_MV_STRING8)
			ulPropTag2 = CHANGE_PROP_TYPE(ulPropTag2, PT_MV_TSTRING);

		// FIXME: Is this check correct, PT_STRING8 vs PT_ERROR == false and not an error? (RELOP_NE == true)
		if (PROP_TYPE(ulPropTag1) != PROP_TYPE(ulPropTag2))
			// cannot compare two different types
			return KCERR_INVALID_TYPE;

		// find using original restriction proptag
		lpProp = FindProp(lpPropVals, lpsRestrict->lpCompare->ulPropTag1);
		lpProp2 = FindProp(lpPropVals, lpsRestrict->lpCompare->ulPropTag2);

		if(lpProp == NULL || lpProp2 == NULL) {
			fMatch = false;
			break;
		}

		er = CompareProp(lpProp, lpProp2, locale, &lCompare);
		if(er != erSuccess)
		{
			assert(false);
			er = erSuccess;
			fMatch = false;
			break;
		}

		switch(lpsRestrict->lpCompare->ulType) {
		case RELOP_GE:
			fMatch = lCompare >= 0;
			break;
		case RELOP_GT:
			fMatch = lCompare > 0;
			break;
		case RELOP_LE:
			fMatch = lCompare <= 0;
			break;
		case RELOP_LT:
			fMatch = lCompare < 0;
			break;
		case RELOP_NE:
			fMatch = lCompare != 0;
			break;
		case RELOP_RE:
			fMatch = false; // FIXME ?? how should this work ??
			break;
		case RELOP_EQ:
			fMatch = lCompare == 0;
			break;
		}
		break;

	case RES_BITMASK:
		if (lpsRestrict->lpBitmask == NULL)
			return KCERR_INVALID_TYPE;

		// We can only bitmask 32-bit LONG values (aka ULONG)
		if (PROP_TYPE(lpsRestrict->lpBitmask->ulPropTag) != PT_LONG)
			return KCERR_INVALID_TYPE;
		lpProp = FindProp(lpPropVals, lpsRestrict->lpBitmask->ulPropTag);

		if(lpProp == NULL) {
			fMatch = false;
			break;
		}

		fMatch = (lpProp->Value.ul & lpsRestrict->lpBitmask->ulMask) > 0;

		if(lpsRestrict->lpBitmask->ulType == BMR_EQZ)
			fMatch = !fMatch;

		break;
		
	case RES_SIZE:
		if (lpsRestrict->lpSize == NULL)
			return KCERR_INVALID_TYPE;
		lpProp = FindProp(lpPropVals, lpsRestrict->lpSize->ulPropTag);
		if (lpProp == NULL)
			return KCERR_INVALID_TYPE;
		ulSize = PropSize(lpProp);

		lCompare = ulSize - lpsRestrict->lpSize->cb;

		switch(lpsRestrict->lpSize->ulType) {
		case RELOP_GE:
			fMatch = lCompare >= 0;
			break;
		case RELOP_GT:
			fMatch = lCompare > 0;
			break;
		case RELOP_LE:
			fMatch = lCompare <= 0;
			break;
		case RELOP_LT:
			fMatch = lCompare < 0;
			break;
		case RELOP_NE:
			fMatch = lCompare != 0;
			break;
		case RELOP_RE:
			fMatch = false; // FIXME ?? how should this work ??
			break;
		case RELOP_EQ:
			fMatch = lCompare == 0;
			break;
		}
		break;

	case RES_EXIST:
		if (lpsRestrict->lpExist == NULL)
			return KCERR_INVALID_TYPE;
		lpProp = FindProp(lpPropVals, lpsRestrict->lpExist->ulPropTag);

		fMatch = (lpProp != NULL);
		break;
	case RES_SUBRESTRICTION:
	    lpProp = FindProp(lpPropVals, PR_ENTRYID);
		if (lpProp == NULL)
			return KCERR_INVALID_TYPE;
	    if(lpSubResults == NULL) {
	        fMatch = false;
			break;
		}
		// Find out if this object matches this subrestriction with the passed
		// subrestriction results.
		if (lpSubResults->size() <= ulSubRestrict) {
			fMatch = false; // No results in the results list for this subquery ??
			break;
		}
		fMatch = false;
		sEntryId.__ptr = lpProp->Value.bin->__ptr;
		sEntryId.__size = lpProp->Value.bin->__size;
		if (lpCacheManager->GetObjectFromEntryId(&sEntryId, &ulResId) == erSuccess)
		{
			auto r = (*lpSubResults)[ulSubRestrict].find(ulResId); // If the item is in the set, it matched
			if (r != (*lpSubResults)[ulSubRestrict].cend())
				fMatch = true;
		}
		break;

	default:
		return KCERR_INVALID_TYPE;
	}

	*lpfMatch = fMatch;
	return er;
}

bool ECGenericObjectTable::IsMVSet()
{
	return (m_bMVSort | m_bMVCols);
}

void ECGenericObjectTable::SetTableId(unsigned int ulTableId)
{
	m_ulTableId = ulTableId;
}

ECRESULT ECGenericObjectTable::Clear()
{
	scoped_rlock biglock(m_hLock);

	// Clear old entries
	mapObjects.clear();
	lpKeyTable->Clear();
	m_mapLeafs.clear();

	for (const auto &p : m_mapCategories)
		delete p.second;
	m_mapCategories.clear();
	m_mapSortedCategories.clear();
	return hrSuccess;
}

ECRESULT ECGenericObjectTable::Load()
{
    return hrSuccess;
}

ECRESULT ECGenericObjectTable::Populate()
{
	scoped_rlock biglock(m_hLock);
	if(m_bPopulated)
		return erSuccess;
	m_bPopulated = true;
	return Load();
}

// Sort functions, overide this functions as you used a caching system

ECRESULT ECGenericObjectTable::IsSortKeyExist(const sObjectTableKey* lpsRowItem, unsigned int ulPropTag)
{
	return KCERR_NOT_FOUND;
}

ECRESULT ECGenericObjectTable::GetSortKey(sObjectTableKey* lpsRowItem, unsigned int ulPropTag, unsigned int *lpSortLen, unsigned char **lppSortData)
{
	assert(false);
	return KCERR_NO_SUPPORT;
}

ECRESULT ECGenericObjectTable::SetSortKey(sObjectTableKey* lpsRowItem, unsigned int ulPropTag, unsigned int ulSortLen, unsigned char *lpSortData)
{
	return KCERR_NO_SUPPORT;
}

// Category handling functions

/*
 * GENERAL workings of categorization
 *
 * Due to min/max categories, the order of rows in the key table (m_lpKeyTable) in not predictable by looking
 * only at a new row, since we don't know what the min/max value for the category is. We therefore have a second
 * sorted list of categories which is purely for looking up if a category exists, and what its min/max values are.
 * This list is m_mapSortedCategories.
 *
 * The order of rows in m_lpKeyTable is the actual order of rows that will be seen by the MAPI Client.
 *
 * quick overview:
 *
 * When a new row is added, we do the following:
 * - Look in m_mapSortedCategories with the categorized properties to see if we already have the category
 * -on new:
 *   - Add category to both mapSortedCategories and mapCategories
 *   - Initialize counters and possibly min/max value
 *   -on existing:
 *   - Update counters (unread,count)
 *   - Update min/max value
 *   -on change of min/max value:
 *     - reorder *all* rows of the category
 * - Track the row in m_mapLeafs
 *
 * When a row is removed, we do the following
 * - Find the row in m_mapLeafs
 * - Get the category of the row
 * - Update counters of the category
 * - Update min/max value of the category
 * -on change of min/max value and non-empty category:
 *   - reorder *all* rows of the category
 * - If count == 0, remove category
 *
 * We currently support only one min/max category in the table. This is actually only enforced in ECCategory which
 * tracks only one sCurMinMax, the rest of the code should be pretty much able to handle multiple levels of min/max
 * categories.
 */

/**
 * Add or modify a category row
 *
 * Called just before the actual message row is added to the table.
 *
 * Due to min/max handling, this function may modify MANY rows in the table because the entire category needed to be relocated.
 *
 * @param sObjKey Object key of new (non-category) row
 * @param lpProps Properties of the new or modified row
 * @param cProps Number of properties in lpProps
 * @param ulFlags Notification flags
 * @param fUnread TRUE if the new state of the object in sObjKey is UNREAD
 * @param lpfHidden Returns if the new row should be hidden because the category is collapsed
 * @param lppCategory Returns a reference to the new or existing category that the item sObjKey should be added to
 */
ECRESULT ECGenericObjectTable::AddCategoryBeforeAddRow(sObjectTableKey sObjKey, struct propVal *lpProps, unsigned int cProps, unsigned int ulFlags, bool fUnread, bool *lpfHidden, ECCategory **lppCategory)
{
    ECRESULT er = erSuccess;
    bool fPrevUnread = false;
    bool fNewLeaf = false;
    unsigned int i = 0;
    sObjectTableKey sPrevRow(0,0);
    ECCategory *lpCategory = NULL;
    LEAFINFO sLeafInfo;
	ECCategory *parent = nullptr;
    ECKeyTable::UpdateType ulAction;
    sObjectTableKey sCatRow;
    ECLeafMap::const_iterator iterLeafs;
    int fResult = 0;
    bool fCollapsed = false;
    bool fHidden = false;
    
    if(m_ulCategories == 0)
		return erSuccess;
    
    // Build binary sort keys
    
    // +1 because we may have a trailing category followed by a MINMAX column
	std::vector<ECSortCol> zort(cProps);
    for (i = 0; i < m_ulCategories + 1 && i < cProps; ++i) {
		if (GetBinarySortKey(&lpProps[i], zort[i]) != erSuccess)
			zort[i].isnull = true;
		if (GetSortFlags(lpProps[i].ulPropTag, &zort[i].flags) != erSuccess)
			zort[i].flags = 0;
    }

    // See if we're dealing with a changed row, not a new row
    iterLeafs = m_mapLeafs.find(sObjKey);
    if (iterLeafs != m_mapLeafs.cend()) {
    	fPrevUnread = iterLeafs->second.fUnread;
        // The leaf was already in the table, compare new properties of the leaf with those of the category it
        // was in.
        for (i = 0; i < iterLeafs->second.lpCategory->m_cProps && i < cProps; ++i) {
            // If the type is different (ie PT_ERROR first, PT_STRING8 now, then it has definitely changed ..)
            if(PROP_TYPE(lpProps[i].ulPropTag) != PROP_TYPE(iterLeafs->second.lpCategory->m_lpProps[i].ulPropTag))
                break;
                
            // Otherwise, compare the properties
            er = CompareProp(&iterLeafs->second.lpCategory->m_lpProps[i], &lpProps[i], m_locale, &fResult);
            if (er != erSuccess)
				return er;
            if(fResult != 0)
                break;
        }
            
        if(iterLeafs->second.lpCategory->m_cProps && i < cProps) {
            // One of the category properties has changed, remove the row from the old category
            RemoveCategoryAfterRemoveRow(sObjKey, ulFlags);
            fNewLeaf = true; // We're re-adding the leaf
        } else if (fUnread == iterLeafs->second.fUnread) {
	            // Nothing to do, the leaf was already in the correct category, and the readstate has not changed
			return erSuccess;
        }
    } else {
    	fNewLeaf = true;
	}
    
    // For each level, check if category already exists in key table (LowerBound), gives sObjectTableKey
    for (i = 0; i < m_ulCategories && i < cProps; ++i) {
    	unsigned int ulDepth = i;
        bool fCategoryMoved = false; // TRUE if the entire category has moved somewhere (due to CATEG_MIN / CATEG_MAX change)
		ECTableRow row(sObjectTableKey(0, 0), std::vector<ECSortCol>(&zort[0], &zort[i+1]), false);

        // Find the actual category in our sorted category map
	auto iterCategoriesSorted = m_mapSortedCategories.find(row);

        // Include the next sort order if it s CATEG_MIN or CATEG_MAX
        if(lpsSortOrderArray->__size > (int)i+1 && ISMINMAX(lpsSortOrderArray->__ptr[i+1].ulOrder))
		++i;
    	
        if (iterCategoriesSorted == m_mapSortedCategories.cend()) {
		assert(fNewLeaf); // The leaf must be new if the category is new       

            // Category not available yet, add it now
            sCatRow.ulObjId = 0;
            sCatRow.ulOrderId = m_ulCategory;
            
            // We are hidden if our parent was collapsed
            fHidden = fCollapsed;
            
            // This category is itself collapsed if our parent was collapsed, or if we should be collapsed due to m_ulExpanded
            fCollapsed = fCollapsed || (ulDepth >= m_ulExpanded);
            
			lpCategory = new ECCategory(m_ulCategory, lpProps, ulDepth + 1, i + 1, parent, ulDepth, !fCollapsed, m_locale);
            ++m_ulCategory;
            lpCategory->IncLeaf(); // New category has 1 leaf
            
            // Make sure the category has the current row as min/max value
            er = UpdateCategoryMinMax(sObjKey, lpCategory, i, lpProps, cProps, NULL);
            if(er != erSuccess)
				return er;
            if(fUnread)
            	lpCategory->IncUnread();

			// Add the category into our sorted-category list and numbered-category list
            assert(m_mapSortedCategories.find(row) == m_mapSortedCategories.end());
            m_mapCategories[sCatRow] = lpCategory;
			lpCategory->iSortedCategory = m_mapSortedCategories.emplace(row, sCatRow).first;
			// Update the keytable with the effective sort columns
			er = UpdateKeyTableRow(lpCategory, &sCatRow, lpProps, i+1, fHidden, &sPrevRow, &ulAction);
			if (er != erSuccess)
				return er;
			parent = lpCategory;
        } else {
            // Category already available
            sCatRow = iterCategoriesSorted->second;

            // Get prev row for notification purposes
            if(lpKeyTable->GetPreviousRow(&sCatRow, &sPrevRow) != erSuccess) {
                sPrevRow.ulObjId = 0;
                sPrevRow.ulOrderId = 0;
            }
            
			auto iterCategory = m_mapCategories.find(sCatRow);
			if (iterCategory == m_mapCategories.cend()) {
				assert(false);
				return KCERR_NOT_FOUND;
			}

			lpCategory = iterCategory->second;

            // Increase leaf count of category (each level must be increased by one) for a new leaf
            if(fNewLeaf) {
	            lpCategory->IncLeaf();
	            if(fUnread)
	            	lpCategory->IncUnread();
			} else {
				// Increase or decrease unread counter depending on change of the leaf's unread state
				if(fUnread && !fPrevUnread)
					lpCategory->IncUnread();
				
				if(!fUnread && fPrevUnread)
					lpCategory->DecUnread(); 
			}
			            
            // This category was hidden if the parent was collapsed
            fHidden = fCollapsed;
            // Remember if this category was collapsed
            fCollapsed = !lpCategory->m_fExpanded;
            
            // Update category min/max values
            er = UpdateCategoryMinMax(sObjKey, lpCategory, i, lpProps, cProps, &fCategoryMoved); 
            if(er != erSuccess)
				return er;
			ulAction = ECKeyTable::TABLE_ROW_MODIFY;
        }

		if (fCategoryMoved) {
			ECObjectTableList lstObjects;
			// The min/max value of this category has changed. We have to move all the rows in the category
			// somewhere else in the table.
			
			// Get the rows that are affected
			er = lpKeyTable->GetRowsBySortPrefix(&sCatRow, &lstObjects);
			if (er != erSuccess)
				return er;
				
			// Update the keytable to reflect the new change
			for (auto &obj : lstObjects) {
				// Update the keytable with the new effective sort data for this column
				
				bool bDescend = lpsSortOrderArray->__ptr[ulDepth].ulOrder == EC_TABLE_SORT_DESCEND; // Column we're updating is descending
				auto oldflags = zort[i].flags;
				zort[i].flags |= bDescend ? TABLEROW_FLAG_DESC : 0;
				er = lpKeyTable->UpdatePartialSortKey(&obj,
				     ulDepth, zort[i], &sPrevRow, &fHidden, &ulAction);
				zort[i].flags = oldflags;
				if (er != erSuccess)
					return er;
				if ((ulFlags & OBJECTTABLE_NOTIFY) && !fHidden)
					AddTableNotif(ulAction, obj, &sPrevRow);
			}
		}
	        // Send notification if required (only the category header has changed)
		else if((ulFlags & OBJECTTABLE_NOTIFY) && !fHidden) {
			AddTableNotif(ulAction, sCatRow, &sPrevRow);
		}
    }
    
    // lpCategory is now the deepest category, and therefore the category we're adding the leaf to

    // Add sObjKey to leaf list via LEAFINFO

    sLeafInfo.lpCategory = lpCategory;
    sLeafInfo.fUnread = fUnread;
    
    m_mapLeafs[sObjKey] = sLeafInfo;
    
	// The item that the request was for is hidden if the deepest category was collapsed
	if (lpfHidden != NULL)
		*lpfHidden = fCollapsed;
        
	if(lppCategory)
		*lppCategory = lpCategory;
	assert(m_mapCategories.size() == m_mapSortedCategories.size());
	return er;
}

/**
 * Update a category min/max value if needed
 *
 * This function updates the min/max value if the category is part of a min/max sorting scheme.
 *
 * @param sKey Key of the category
 * @param lpCategory Category to update
 * @param i Column id of the possible min/max sort
 * @param lpProps Properties for the category
 * @param cProps Number of properties in lpProps
 * @param lpfModified Returns whether the category min/max value was changed
 * @return result
 */
ECRESULT ECGenericObjectTable::UpdateCategoryMinMax(sObjectTableKey &sKey,
    ECCategory *lpCategory, size_t i, struct propVal *lpProps, size_t cProps,
    bool *lpfModified)
{
	if (lpsSortOrderArray->__size < 0 ||
	    static_cast<size_t>(lpsSortOrderArray->__size) <= i ||
	    !ISMINMAX(lpsSortOrderArray->__ptr[i].ulOrder))
		return erSuccess;
	lpCategory->UpdateMinMax(sKey, i, &lpProps[i], lpsSortOrderArray->__ptr[i].ulOrder == EC_TABLE_SORT_CATEG_MAX, lpfModified);
	return erSuccess;
}

/**
 * Creates a row in the key table
 *
 * The only complexity of this function is when doing min/max categorization; consider the sort order
 *
 * CONVERSATION_TOPIC ASC, DATE CATEG_MAX, DATE DESC
 * with ulCategories = 1
 *
 * This effectively generates the following category row in the key table:
 *
 * MAX_DATE, CONVERSATION_TOPIC 				for the category and
 * MAX_DATE, CONVERSATION_TOPIC, DATE			for the data row
 *
 * This means we have to get the 'max date' part, generate a sortkey, and switch the order for the first
 * two columns, and add that to the key table.
 *
 * @param lpCategory For a normal row, the category to which it belongs
 * @param lpsRowKey The row key of the row
 * @param ulDepth Number properties in lpProps/cValues to process. For normal rows, ulDepth == cValues
 * @param lpProps Properties from the database of the row
 * @param cValues Number of properties in lpProps
 * @param fHidden TRUE if the row is to be hidden
 * @param sPrevRow Previous row ID
 * @param lpulAction Action performed
 * @return result
 */
ECRESULT ECGenericObjectTable::UpdateKeyTableRow(ECCategory *lpCategory, sObjectTableKey *lpsRowKey, struct propVal *lpProps, unsigned int cValues, bool fHidden, sObjectTableKey *lpsPrevRow, ECKeyTable::UpdateType *lpulAction)
{
	ECRESULT er = erSuccess;
    struct propVal sProp;
	struct sortOrderArray *soa = lpsSortOrderArray;
	struct sortOrder sSortHierarchy;
	sSortHierarchy.ulPropTag = PR_EC_HIERARCHYID;
	sSortHierarchy.ulOrder   = EC_TABLE_SORT_DESCEND;
	struct sortOrderArray sSortSimple;
	sSortSimple.__ptr = &sSortHierarchy;
	sSortSimple.__size = 1;
    int n = 0;
    
	assert(cValues <= static_cast<unsigned int>(soa->__size));
    if(cValues == 0) {
		// No sort columns. We use the object ID as the sorting
		// key. This is fairly arbitrary as any sort order would be okay seen as no sort order was specified. However, sorting
		// in this way makes sure that new items are sorted FIRST by default, which is a logical view when debugging. Also, if
		// any algorithm does assumptions based on the first row, it is best to have the first row be the newest row; this is what
		// happens when you export messages from OLK to a PST and it does a memory calculation of nItems * size_of_first_entryid
		// for the memory requirements of all entryids. (which crashes if you don't do it properly)
		sProp.ulPropTag = PR_EC_HIERARCHYID;
		sProp.Value.ul = lpsRowKey->ulObjId;
		sProp.__union = SOAP_UNION_propValData_ul;
		
		cValues = 1;
		lpProps = &sProp;
		soa = &sSortSimple;
    }
	
	std::unique_ptr<struct propVal[]> lpOrderedProps(new struct propVal[cValues]);
	std::vector<ECSortCol> zort(cValues);
	memset(lpOrderedProps.get(), 0, sizeof(struct propVal) * cValues);

	for (unsigned int i = 0; i < cValues; ++i) {
		if (ISMINMAX(soa->__ptr[i].ulOrder)) {
			if (n == 0 || !lpCategory)
				// Min/max ignored if the row is not in a category
				continue;
			
			// Swap around the current and the previous sorting order
			lpOrderedProps[n] = lpOrderedProps[n-1];
			// Get actual sort order from category
			if(lpCategory->GetProp(NULL, lpsSortOrderArray->__ptr[n].ulPropTag, &lpOrderedProps[n-1]) != erSuccess) {
				lpOrderedProps[n-1].ulPropTag = CHANGE_PROP_TYPE(lpsSortOrderArray->__ptr[n].ulPropTag, PT_ERROR);
				lpOrderedProps[n-1].Value.ul = KCERR_NOT_FOUND;
				lpOrderedProps[n-1].__union = SOAP_UNION_propValData_ul;
			}
		} else {
			er = CopyPropVal(&lpProps[n], &lpOrderedProps[n], NULL, false);
			if(er != erSuccess)
				goto exit;
		}
		++n;
	}
	
    // Build binary sort keys from updated data
    for (int i = 0; i < n; ++i) {
		if (GetBinarySortKey(&lpOrderedProps[i], zort[i]) != erSuccess)
			zort[i].isnull = true;
		if (GetSortFlags(lpOrderedProps[i].ulPropTag, &zort[i].flags) != erSuccess)
			zort[i].flags = 0;
		if (soa->__ptr[i].ulOrder == EC_TABLE_SORT_DESCEND)
			zort[i].flags |= TABLEROW_FLAG_DESC;
    }

    // Update row
	er = lpKeyTable->UpdateRow(ECKeyTable::TABLE_ROW_ADD, lpsRowKey,
	     std::move(zort), lpsPrevRow, fHidden, lpulAction);
exit:
	if (lpOrderedProps != nullptr)
		for (unsigned int i = 0; i < cValues; ++i)
			FreePropVal(&lpOrderedProps[i], false);
	return er;
}

/**
 * Updates a category after a non-category row has been removed
 *
 * This function updates the category to contain the correct counters, and possibly removes the category if it is empty.
 *
 * Many row changes may be generated in a min/max category when the min/max row is removed from the category, which triggers
 * a reorder of the category in the table.
 *
 * @param sOjbKey Object that was deleted
 * @param ulFlags Notification flags
 * @return result
 */
ECRESULT ECGenericObjectTable::RemoveCategoryAfterRemoveRow(sObjectTableKey sObjKey, unsigned int ulFlags)
{
    ECRESULT er = erSuccess;
	sObjectTableKey sCatRow, sPrevRow(0,0);
    ECLeafMap::const_iterator iterLeafs;
    ECKeyTable::UpdateType ulAction;
	ECCategory *parent = nullptr;
	bool fModified = false, fHidden = false;
    unsigned int ulDepth = 0;
	struct propVal sProp;
	
	sProp.ulPropTag = PR_NULL;
    
    // Find information for the deleted leaf
    iterLeafs = m_mapLeafs.find(sObjKey);
    if (iterLeafs == m_mapLeafs.cend()) {
        er = KCERR_NOT_FOUND;
        goto exit;
    }
    
    // Loop through this category and all its parents
	for (auto lpCategory = iterLeafs->second.lpCategory;
	     lpCategory != nullptr; lpCategory = parent) {
    	ulDepth = lpCategory->m_ulDepth;
        parent = lpCategory->m_lpParent;
        // Build the row key for this category
        sCatRow.ulObjId = 0;
        sCatRow.ulOrderId = lpCategory->m_ulCategory;
        
        // Decrease the number of leafs in the category    
        lpCategory->DecLeaf();    
        if(iterLeafs->second.fUnread)
            lpCategory->DecUnread();
            
		if(ulDepth+1 < lpsSortOrderArray->__size && ISMINMAX(lpsSortOrderArray->__ptr[ulDepth+1].ulOrder)) {
			// Removing from a min/max category
			er = lpCategory->UpdateMinMaxRemove(sObjKey, ulDepth+1, lpsSortOrderArray->__ptr[ulDepth+1].ulOrder == EC_TABLE_SORT_CATEG_MAX, &fModified);
			if(er != erSuccess) {
				assert(false);
				goto exit;
			}
			
			if(fModified && lpCategory->GetCount() > 0) {
				// We have removed the min or max value for the category, so reorder is needed (unless category is empty, since it will be removed)
				ECObjectTableList lstObjects;
				
				// Get the rows that are affected
				er = lpKeyTable->GetRowsBySortPrefix(&sCatRow, &lstObjects);
				if (er != erSuccess)
					goto exit;
					
				// Update the keytable to reflect the new change
				for (auto &obj : lstObjects) {
					// Update the keytable with the new effective sort data for this column
					
					if(lpCategory->GetProp(NULL, lpsSortOrderArray->__ptr[ulDepth+1].ulPropTag, &sProp) != erSuccess) {
						sProp.ulPropTag = CHANGE_PROP_TYPE(lpsSortOrderArray->__ptr[ulDepth+1].ulPropTag, PT_ERROR);
						sProp.Value.ul = KCERR_NOT_FOUND;
					}

					ECSortCol scol;
					if (GetBinarySortKey(&sProp, scol) != erSuccess)
						scol.isnull = true;
					if (GetSortFlags(sProp.ulPropTag, &scol.flags) != erSuccess)
						scol.flags = 0;
					
					scol.flags |= lpsSortOrderArray->__ptr[ulDepth].ulOrder == EC_TABLE_SORT_DESCEND ? TABLEROW_FLAG_DESC : 0;
					er = lpKeyTable->UpdatePartialSortKey(&obj, ulDepth, scol, &sPrevRow, &fHidden, &ulAction);
					if (er != erSuccess)
						goto exit;
					if ((ulFlags & OBJECTTABLE_NOTIFY) && !fHidden)
						AddTableNotif(ulAction, obj, &sPrevRow);
					FreePropVal(&sProp, false);
					sProp.ulPropTag = PR_NULL;
				}
			}
		}
            
		if (lpCategory->GetCount() != 0) {
			if (ulFlags & OBJECTTABLE_NOTIFY) {
				// The category row has changed; the counts have updated, send a notification
				if (lpKeyTable->GetPreviousRow(&sCatRow, &sPrevRow) != erSuccess)
					sPrevRow.ulOrderId = sPrevRow.ulObjId = 0;
				AddTableNotif(ECKeyTable::TABLE_ROW_MODIFY, sCatRow, &sPrevRow);
			}
			continue;
		}
		// The category row is empty and must be removed
		ECTableRow *lpRow = NULL; // reference to the row in the keytable
		er = lpKeyTable->GetRow(&sCatRow, &lpRow);
		if (er != erSuccess) {
			assert(false);
			goto exit;
		}

		// Remove the category from the sorted categories map
		m_mapSortedCategories.erase(lpCategory->iSortedCategory);

		// Remove the category from the keytable
		lpKeyTable->UpdateRow(ECKeyTable::TABLE_ROW_DELETE, &sCatRow, {}, nullptr, false, &ulAction);

		// Remove the category from the category map
		assert(m_mapCategories.find(sCatRow) != m_mapCategories.end());
		m_mapCategories.erase(sCatRow);

		// Free the category itself
		delete lpCategory;

		// Send the notification
		if (ulAction == ECKeyTable::TABLE_ROW_DELETE && (ulFlags & OBJECTTABLE_NOTIFY))
                AddTableNotif(ulAction, sCatRow, NULL);
    }

    // Remove the leaf from the leaf map
    m_mapLeafs.erase(iterLeafs);
        
    // All done
	assert(m_mapCategories.size() == m_mapSortedCategories.size());
exit:
	FreePropVal(&sProp, false);
	sProp.ulPropTag = PR_NULL;
	return er;
}

/**
 * Get a table properties for a category
 *
 * @param soap SOAP object for memory allocation of data in lpPropVal
 * @param ulPropTag Requested property tag
 * @param sKey Key of the category to be retrieved
 * @param lpPropVal Output location of the property
 * @return result
 */
ECRESULT ECGenericObjectTable::GetPropCategory(struct soap *soap, unsigned int ulPropTag, sObjectTableKey sKey, struct propVal *lpPropVal)
{
    ECRESULT er = erSuccess;
    unsigned int i = 0;
	uint32_t tmp4;
    
	auto iterCategories = m_mapCategories.find(sKey);
	if (iterCategories == m_mapCategories.cend())
		return KCERR_NOT_FOUND;
    
    switch(ulPropTag) {
        case PR_INSTANCE_KEY:
            lpPropVal->__union = SOAP_UNION_propValData_bin;
            lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
            lpPropVal->Value.bin->__size = sizeof(unsigned int) * 2;
            lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(unsigned int) * 2);
			tmp4 = cpu_to_le32(sKey.ulObjId);
			memcpy(lpPropVal->Value.bin->__ptr, &tmp4, sizeof(tmp4));
			tmp4 = cpu_to_le32(sKey.ulOrderId);
			memcpy(lpPropVal->Value.bin->__ptr + sizeof(tmp4), &tmp4, sizeof(tmp4));
            lpPropVal->ulPropTag = PR_INSTANCE_KEY;
            break;
        case PR_ROW_TYPE:
            lpPropVal->__union = SOAP_UNION_propValData_ul;
            lpPropVal->Value.ul = iterCategories->second->m_fExpanded ? TBL_EXPANDED_CATEGORY : TBL_COLLAPSED_CATEGORY;
            lpPropVal->ulPropTag = PR_ROW_TYPE;
            break;
        case PR_DEPTH:
            lpPropVal->__union = SOAP_UNION_propValData_ul;
            lpPropVal->Value.ul = iterCategories->second->m_ulDepth;
            lpPropVal->ulPropTag = PR_DEPTH;
            break;
        case PR_CONTENT_COUNT:
            lpPropVal->__union = SOAP_UNION_propValData_ul;
            lpPropVal->Value.ul = iterCategories->second->m_ulLeafs;
            lpPropVal->ulPropTag = PR_CONTENT_COUNT;
            break;
        case PR_CONTENT_UNREAD:
            lpPropVal->__union = SOAP_UNION_propValData_ul;
            lpPropVal->Value.ul = iterCategories->second->m_ulUnread;
            lpPropVal->ulPropTag = PR_CONTENT_UNREAD;
            break;
        default:
            for (i = 0; i < iterCategories->second->m_cProps; ++i)
                // If MVI is set, search for the property as non-MV property, as this is how we will have
                // received it when the category was added.
                if (NormalizePropTag(iterCategories->second->m_lpProps[i].ulPropTag) == NormalizePropTag(ulPropTag & ~MVI_FLAG))
                    if(CopyPropVal(&iterCategories->second->m_lpProps[i], lpPropVal, soap) == erSuccess) {
						lpPropVal->ulPropTag = (ulPropTag & ~MVI_FLAG);
                        break;
					}
            
            if(i == iterCategories->second->m_cProps)
                er = KCERR_NOT_FOUND;
        }
	return er;
}

unsigned int ECGenericObjectTable::GetCategories()
{
	return m_ulCategories;
}

// Normally overridden by subclasses
ECRESULT ECGenericObjectTable::CheckPermissions(unsigned int ulObjId)
{
    return hrSuccess;
}

/**
 * Get object size
 *
 * @return Object size in bytes
 */
size_t ECGenericObjectTable::GetObjectSize(void)
{
	size_t ulSize = sizeof(*this);
	scoped_rlock biglock(m_hLock);

	ulSize += SortOrderArraySize(lpsSortOrderArray);
	ulSize += PropTagArraySize(lpsPropTagArray);
	ulSize += RestrictTableSize(lpsRestrict);
	ulSize += MEMORY_USAGE_LIST(m_listMVSortCols.size(), ECListInt);

	ulSize += MEMORY_USAGE_MAP(mapObjects.size(), ECObjectTableMap);
	ulSize += lpKeyTable->GetObjectSize();

	ulSize += MEMORY_USAGE_MAP(m_mapCategories.size(), ECCategoryMap);
	for (const auto &p : m_mapCategories)
		ulSize += p.second->GetObjectSize();
	
	ulSize += MEMORY_USAGE_MAP(m_mapLeafs.size(), ECLeafMap);
	return ulSize;
}

ECCategory::ECCategory(unsigned int ulCategory, struct propVal *lpProps,
    unsigned int cProps, unsigned int nProps, ECCategory *lpParent,
    unsigned int ulDepth, bool fExpanded, const ECLocale &locale) :
	m_lpParent(lpParent), m_cProps(nProps), m_ulDepth(ulDepth),
	m_ulCategory(ulCategory), m_fExpanded(fExpanded), m_locale(locale)
{
    unsigned int i;

	m_lpProps = s_alloc<propVal>(nullptr, nProps);
	for (i = 0; i < cProps; ++i)
		CopyPropVal(&lpProps[i], &m_lpProps[i]);
	for (; i < nProps; ++i) {
    	m_lpProps[i].ulPropTag = PR_NULL;
    	m_lpProps[i].Value.ul = 0;
    	m_lpProps[i].__union = SOAP_UNION_propValData_ul;
    }
}

ECCategory::~ECCategory()
{
    unsigned int i;
    
	for (i = 0; i < m_cProps; ++i)
		FreePropVal(&m_lpProps[i], false);
	for (const auto &p : m_mapMinMax)
		FreePropVal(p.second, true);
	s_free(nullptr, m_lpProps);
}

ECRESULT ECCategory::GetProp(struct soap *soap, unsigned int ulPropTag, struct propVal* lpPropVal)
{
    ECRESULT er = erSuccess;
    unsigned int i;
    
	for (i = 0; i < m_cProps; ++i)
		if (m_lpProps[i].ulPropTag == ulPropTag) {
            er = CopyPropVal(&m_lpProps[i], lpPropVal, soap);
            break;
		}
    
    if(i == m_cProps)
        er = KCERR_NOT_FOUND;
    
    return er;
}

ECRESULT ECCategory::SetProp(unsigned int i, struct propVal* lpPropVal)
{
    assert(i < m_cProps);
    FreePropVal(&m_lpProps[i], false);
	return CopyPropVal(lpPropVal, &m_lpProps[i], nullptr);
}

/**
 * Updates a min/max value:
 *
 * Checks if the value passed is more or less than the current min/max value. Currently we treat 
 * an error value as a 'worse' value than ANY new value. This means that min(ERROR, 1) == 1, and max(ERROR, 1) == 1.
 *
 * The new value is also tracked in a list of min/max value so that UpdateMinMaxRemove() (see below) can update
 * the min/max value when a row is removed.
 *
 * @param sKey Key of the new row
 * @param i Column id of the min/max value
 * @param lpNewValue New value for the column (may also be PT_ERROR)
 * @param bool fMax TRUE if the column is a EC_TABLE_SORT_CATEG_MAX, FALSE if the column is EC_TABLE_SORT_CATEG_MIN
 * @param lpfModified Returns TRUE if the new value updated the min/max value, false otherwise
 * @return result
 */
ECRESULT ECCategory::UpdateMinMax(const sObjectTableKey &sKey, unsigned int i, struct propVal *lpNewValue, bool fMax, bool *lpfModified)
{
	bool fModified = false;
	int result = 0;
	struct propVal *lpOldValue = &m_lpProps[i], *lpNew;
	
	if(PROP_TYPE(lpOldValue->ulPropTag) != PT_ERROR && PROP_TYPE(lpOldValue->ulPropTag) != PT_NULL) {
		// Compare old with new
		auto er = CompareProp(lpOldValue, lpNewValue, m_locale, &result);
		if (er != erSuccess)
			return er;
	}
	
	// Copy the value so we can track it for later (in UpdateMinMaxRemove) if we didn't have it yet
	auto er = CopyPropVal(lpNewValue, &lpNew);
	if(er != erSuccess)
		return er;
		
	auto iterMinMax = m_mapMinMax.find(sKey);
	if (iterMinMax == m_mapMinMax.cend()) {
		m_mapMinMax.emplace(sKey, lpNew);
	} else {
		FreePropVal(iterMinMax->second, true); // NOTE this may free lpNewValue, so you can't use that anymore now
		iterMinMax->second = lpNew;
	}
	
	if(PROP_TYPE(lpOldValue->ulPropTag) == PT_ERROR || PROP_TYPE(lpOldValue->ulPropTag) == PT_NULL || (!fMax && result > 0) || (fMax && result < 0)) {
		// Either there was no old value, or the new value is larger or smaller than the old one
		er = SetProp(i, lpNew);
		if(er != erSuccess)
			return er;
		m_sCurMinMax = sKey;
					
		fModified = true;
	}
	
	if(lpfModified)
		*lpfModified = fModified;
	return erSuccess;
}

/**
 * Update the min/max value to a row removal
 *
 * This function removes the value from the internal list of values, and checks if the new min/max value
 * differs from the last. It updates the category properties accordingly if needed.
 *
 * @param sKey Key of row that was removed
 * @param i Column id of min/max value
 * @param fMax TRUE if the column is a EC_TABLE_SORT_CATEG_MAX, FALSE if the column is EC_TABLE_SORT_CATEG_MIN
 * @param lpfModified TRUE if a new min/max value came into play due to the deletion
 * @return result
 */
ECRESULT ECCategory::UpdateMinMaxRemove(const sObjectTableKey &sKey, unsigned int i, bool fMax, bool *lpfModified)
{
	bool fModified = false;
	auto iterMinMax = m_mapMinMax.find(sKey);
	if (iterMinMax == m_mapMinMax.cend())
		return KCERR_NOT_FOUND;
	
	FreePropVal(iterMinMax->second, true);
	m_mapMinMax.erase(iterMinMax);
	
	if(m_sCurMinMax == sKey) {
		fModified = true;
		
		// Reset old value
		FreePropVal(&m_lpProps[i], false);
		m_lpProps[i].ulPropTag = PR_NULL;
		// The min/max value until now was updated. Find the next min/max value.
		for (iterMinMax = m_mapMinMax.begin();
		     iterMinMax != m_mapMinMax.end(); ++iterMinMax)
			// Re-feed the values we had until now
			UpdateMinMax(iterMinMax->first, i, iterMinMax->second, fMax, NULL); // FIXME this 
	}
	
	if(lpfModified)
		*lpfModified = fModified;
	return erSuccess;
}

/**
 * Get object size
 *
 * @return Object size in bytes
 */
size_t ECCategory::GetObjectSize(void) const
{
	size_t ulSize = 0;
	
	if (m_cProps > 0) {
		ulSize += sizeof(struct propVal) * m_cProps;
		for (unsigned int i = 0; i < m_cProps; ++i)
			ulSize += PropSize(&m_lpProps[i]);
	}

	if (m_lpParent)
		ulSize += m_lpParent->GetObjectSize();

	return sizeof(*this) + ulSize;
}

/**
 * Get PR_DEPTH for an object in the table
 *
 * @param lpThis Pointer to generic object table instance
 * @param soap SOAP object for memory allocation
 * @param lpSession Session assiociated with the table
 * @param ulObjId Object ID of the object to get PR_DEPTH for
 * @param lpProp PropVal to write to
 * @return result
 */
ECRESULT ECGenericObjectTable::GetComputedDepth(struct soap *soap,
    ECSession *ses, unsigned int ulObjId, struct propVal *lpProp)
{
	lpProp->__union = SOAP_UNION_propValData_ul;
	lpProp->ulPropTag = PR_DEPTH;

	if (m_ulObjType == MAPI_MESSAGE)
		// For contents tables, depth is equal to number of categories
		lpProp->Value.ul = GetCategories();
	else
		// For hierarchy tables, depth is 1 (see ECConvenientDepthTable.cpp for exception)
		lpProp->Value.ul = 1;
		
	return erSuccess;
}

} /* namespace */
