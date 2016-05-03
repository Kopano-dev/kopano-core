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

#ifndef ECGENERIC_OBJECTTABLE_H
#define ECGENERIC_OBJECTTABLE_H

#define MAX_SORTKEY_LEN 4096

#include <kopano/zcdefs.h>
#include "soapH.h"

#include <list>
#include <map>

#include "ECSubRestriction.h"
#include <kopano/ECKeyTable.h>
#include "ECDatabase.h"
#include <kopano/ustringutil.h>
#include <kopano/ECUnknown.h>

/*
 * This object is an actual table, with a cursor in-memory. We also keep the complete
 * keyset of the table in memory, so seeks and queries can be really fast. Also, we
 * sort the table once on loading, and simply use that keyset when the rows are actually
 * required. The actual data is always loaded directly from the database.
 */


typedef std::list<unsigned int>				ECListInt;
typedef std::list<unsigned int>::const_iterator ECListIntIterator;

#define RESTRICT_MAX_DEPTH 16

#define OBJECTTABLE_NOTIFY		1

class ECSession;
class ECCacheManager;

typedef std::map<ECTableRow, sObjectTableKey> ECSortedCategoryMap;

class ECCategory _zcp_final {
public:
    ECCategory(unsigned int ulCategory, struct propVal *lpProps, unsigned int cProps, unsigned int nProps, ECCategory *lpParent, unsigned int ulDepth, bool fExpanded, const ECLocale &locale);
    virtual ~ECCategory();

    void IncLeaf();
    void DecLeaf();
    void IncUnread();
    void DecUnread();
    
    unsigned int GetCount();
    
    ECCategory* GetParent();
    
    ECRESULT GetProp(struct soap* soap, unsigned int ulPropTag, struct propVal *lpPropVal);
    ECRESULT SetProp(unsigned int i, struct propVal *lpPropVal);
    ECRESULT UpdateMinMax(const sObjectTableKey &sKey, unsigned int i, struct propVal *lpPropVal, bool fMax, bool *lpfModified);
    ECRESULT UpdateMinMaxRemove(const sObjectTableKey &sKey, unsigned int i, bool fMax, bool *lpfModified);
	
	size_t GetObjectSize(void) const;

    struct propVal *m_lpProps;
    unsigned int m_cProps;
    ECCategory *m_lpParent;
    unsigned int m_ulDepth;
    unsigned int m_ulCategory;
    bool m_fExpanded;
    
    unsigned int m_ulUnread;
    unsigned int m_ulLeafs;

	const ECLocale& m_locale;

	std::map<sObjectTableKey, struct propVal *> m_mapMinMax;
	ECSortedCategoryMap::iterator iSortedCategory;
	sObjectTableKey m_sCurMinMax;
};

typedef struct {
    bool fUnread;
    ECCategory *lpCategory;
} LEAFINFO;

typedef std::map<sObjectTableKey, ECCategory *, ObjectTableKeyCompare> ECCategoryMap;
typedef std::map<sObjectTableKey, LEAFINFO, ObjectTableKeyCompare> ECLeafMap;

class ECGenericObjectTable;
typedef ECRESULT (* QueryRowDataCallBack)(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bTableData, bool bTableLimit);

/*
 * ECGenericObjectTable
 *
 * Some things about adding rows:
 *
 * There are a few functions that are used to add rows, each at a different level. Here is a quick description of each
 * function, ordered in call-stack-order (top calls bottom)
 *
 * UpdateRow(type, objectid, flags)
 * - Checks security for the object
 * - Updates ALL rows for a specific object ID (usually one row since MVI is not being used)
 * AddRowKey(list_of_row_ids, number_of_rows_added *, flags)
 * - Retrieves the sort data for all rows to be added
 * - Applies restriction
 * AddRow(row_id, <sort data>, flags, hidden)
 * - Adds an actual row to the table
 * - Updates category headers
 *
 * During a row deletion, the second layer is skipped since you don't need to retrieve the sort info or do restrictions
 * on deleted rows, and UpdateRow() directly calls DeleteRow(), which is analogous to AddRow().
 *
 * Doing a re-sort deletes all rows and re-adds them from the 'AddRowKey' level.
 *
 * @todo Support more then one multi-value instance property. (Never saw this from Outlook)
 */

class ECGenericObjectTable : public ECUnknown {
public:
	ECGenericObjectTable(ECSession *lpSession, unsigned int ulObjType, unsigned int ulFlags, const ECLocale &locale);
	virtual ~ECGenericObjectTable();

	/**
	 * An enumeration for the table reload type.
	 * The table reload type determines how it should changed
	 */
	enum enumReloadType { 
		RELOAD_TYPE_SETCOLUMNS,		/**< Reload table because changed columns */
		RELOAD_TYPE_SORTORDER		/**< Reload table because changed sort order */
	};

	// Client operations
	ECRESULT	SeekRow(unsigned int ulBookmark, int lSeekTo, int *lplRowsSought);
	ECRESULT	FindRow(struct restrictTable *lpsRestrict, unsigned int ulBookmark, unsigned int ulFlags);
	ECRESULT	GetRowCount(unsigned int *lpulRowCount, unsigned int *lpulCurrentRow);
	ECRESULT	SetColumns(struct propTagArray *lpsPropTags, bool bDefaultSet);
	ECRESULT	GetColumns(struct soap *soap, ULONG ulFlags, struct propTagArray **lpsPropTags);
	ECRESULT	SetSortOrder(struct sortOrderArray *lpsSortOrder, unsigned int ulCategories, unsigned int ulExpanded);
	ECRESULT	Restrict(struct restrictTable *lpsRestrict);
	ECRESULT	QueryRows(struct soap *soap, unsigned int ulRowCount, unsigned int ulFlags, struct rowSet **lppRowSet);
	ECRESULT	CreateBookmark(unsigned int* lpulbkPosition);
	ECRESULT	FreeBookmark(unsigned int ulbkPosition);
	ECRESULT	ExpandRow(struct soap *soap, xsd__base64Binary sInstanceKey, unsigned int ulRowCount, unsigned int ulFlags, struct rowSet **lppRowSet, unsigned int *lpulRowsLeft);
	ECRESULT	CollapseRow(xsd__base64Binary sInstanceKey, unsigned int ulFlags, unsigned int *lpulRows);
	ECRESULT	GetCollapseState(struct soap *soap, struct xsd__base64Binary sBookmark, struct xsd__base64Binary *lpsCollapseState);
	ECRESULT	SetCollapseState(struct xsd__base64Binary sCollapseState, unsigned int *lpulBookmark);

	virtual ECRESULT GetColumnsAll(ECListInt* lplstProps);
	virtual ECRESULT ReloadTableMVData(ECObjectTableList* lplistRows, ECListInt* lplistMVPropTag);

	// Sort functions
	virtual ECRESULT IsSortKeyExist(const sObjectTableKey* lpsRowItem, unsigned int ulPropTag);
	virtual ECRESULT GetSortKey(sObjectTableKey* lpsRowItem, unsigned int ulPropTag, unsigned int *lpSortLen, unsigned char **lppSortData);
	virtual ECRESULT SetSortKey(sObjectTableKey* lpsRowItem, unsigned int ulPropTag, unsigned int ulSortLen, unsigned char *lpSortData);

	// Server operations
	virtual ECRESULT	Clear();
	virtual ECRESULT	Populate();
	virtual ECRESULT	UpdateRow(unsigned int ulType, unsigned int ulObjId, unsigned int ulFlags);
	virtual ECRESULT	UpdateRows(unsigned int ulType, std::list<unsigned int> *lstObjId, unsigned int ulFlags, bool bInitialLoad);
	virtual ECRESULT	LoadRows(std::list<unsigned int> *lstObjId, unsigned int ulFlags);

	static ECRESULT	GetRestrictPropTagsRecursive(struct restrictTable *lpsRestrict, std::list<ULONG> *lpPropTags, ULONG ulLevel);
	static ECRESULT	GetRestrictPropTags(struct restrictTable *lpsRestrict, std::list<ULONG> *lpPrefixTags, struct propTagArray **lppPropTags);
	static ECRESULT	MatchRowRestrict(ECCacheManager* lpCacheManager, struct propValArray *lpPropVals, struct restrictTable *lpsRestruct, SUBRESTRICTIONRESULTS *lpSubResults, const ECLocale &locale, bool *fMatch, unsigned int *lpulSubRestriction = NULL);

	virtual ECRESULT GetComputedDepth(struct soap *soap, ECSession *lpSession, unsigned int ulObjId, struct propVal *lpProp);

	bool IsMVSet();
	void SetTableId(unsigned int ulTableId);
	
	virtual	ECRESULT	GetPropCategory(struct soap *soap, unsigned int ulPropTag, sObjectTableKey sKey, struct propVal *lpPropVal);
	virtual unsigned int GetCategories();

	virtual size_t GetObjectSize(void);

protected:
	// Add an actual row to the table, and send a notification if required. If you add an existing row, the row is modified and the notification is send as a modification
	ECRESULT AddRow(sObjectTableKey sRowItem, struct propVal *lpProps, unsigned int cProps, unsigned int ulFlags, bool fHidden, ECCategory *lpCategory);
	// Remove an actual row from the table, and send a notification if required. You may try to delete non-existing rows, in which case nothing happens
	ECRESULT 	DeleteRow(sObjectTableKey sRow, unsigned int ulFlags);
	// Send a notification by getting row data and adding the notification
	ECRESULT 	AddTableNotif(ECKeyTable::UpdateType, sObjectTableKey sRowItem, sObjectTableKey *lpsPrevRow);
	// Add data to key table
	ECRESULT 	UpdateKeyTableRow(ECCategory *lpCategory, sObjectTableKey *lpsRowKey, struct propVal *lpProps, unsigned int cValues, bool fHidden, sObjectTableKey *sPrevRow, ECKeyTable::UpdateType *lpulAction);
	// Update min/max field for category
	ECRESULT 	UpdateCategoryMinMax(sObjectTableKey& lpKey, ECCategory *lpCategory, unsigned int i, struct propVal *lpProps, unsigned int cProps, bool *lpfModified);

	virtual ECRESULT	ReloadKeyTable();
	ECRESULT	GetBinarySortKey(struct propVal *lpsPropVal, unsigned int *lpSortLen, unsigned char **lppSortData);
	ECRESULT	GetSortFlags(unsigned int ulPropTag, unsigned char *lpFlags);

	virtual ECRESULT GetMVRowCount(unsigned int ulObjId, unsigned int *lpulCount);
	virtual ECRESULT ReloadTable(enumReloadType eType);
	virtual ECRESULT	Load();

	virtual ECRESULT	CheckPermissions(unsigned int ulObjId); // normally overridden by subclass

	const ECLocale &GetLocale() const { return m_locale; }

	// Constants
	ECSession*					lpSession;
	ECKeyTable*					lpKeyTable;
	unsigned int				m_ulTableId;	// Id of the table from ECTableManager
	void*						m_lpObjectData;

	pthread_mutex_t				m_hLock;		// Lock for locked internals

	// Locked internals
	struct sortOrderArray*		lpsSortOrderArray;	// Stored sort order
	struct propTagArray*		lpsPropTagArray;	// Stored column set
	struct restrictTable*		lpsRestrict;		// Stored restriction
	ECObjectTableMap			mapObjects;			// Map of all objects in this table
	ECListInt					m_listMVSortCols;	// List of MV sort columns
	bool						m_bMVCols;			// Are there MV props in the column list
	bool						m_bMVSort;			// Are there MV props in the sort order
	unsigned int				m_ulObjType;
	unsigned int				m_ulFlags;			//< flags from client
	QueryRowDataCallBack		m_lpfnQueryRowData;
	
protected:
	virtual ECRESULT			AddRowKey(ECObjectTableList* lpRows, unsigned int *lpulLoaded, unsigned int ulFlags, bool bInitialLoad, bool bOverride, struct restrictTable *lpOverrideRestrict);
    virtual ECRESULT			AddCategoryBeforeAddRow(sObjectTableKey sObjKey, struct propVal *lpProps, unsigned int cProps, unsigned int ulFlags, bool fUnread, bool *lpfHidden, ECCategory **lppCategory);
    virtual ECRESULT			RemoveCategoryAfterRemoveRow(sObjectTableKey sObjKey, unsigned int ulFlags);
	
	ECCategoryMap				m_mapCategories;	// Map between instance key of category and category struct
	ECSortedCategoryMap			m_mapSortedCategories; // Map between category sort keys and instance key. This is where we track which categories we have
	ECLeafMap					m_mapLeafs;			// Map between object instance key and LEAFINFO (contains unread flag and category pointer)
	unsigned int				m_ulCategory;
	unsigned int				m_ulCategories;
	unsigned int				m_ulExpanded;
	bool						m_bPopulated;
	
	ECLocale					m_locale;
};

#endif // ECGENERIC_OBJECTTABLE_H
