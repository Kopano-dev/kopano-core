/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECGENERIC_OBJECTTABLE_H
#define ECGENERIC_OBJECTTABLE_H

#define MAX_SORTKEY_LEN 4096

#include <kopano/zcdefs.h>
#include <mutex>
#include "soapH.h"
#include <list>
#include <map>
#include "ECSubRestriction.h"
#include <kopano/ECKeyTable.h>
#include "ECDatabase.h"
#include <kopano/ustringutil.h>
#include <kopano/ECUnknown.h>

struct soap;

namespace KC {

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

class ECCategory _kc_final {
public:
    ECCategory(unsigned int ulCategory, struct propVal *lpProps, unsigned int cProps, unsigned int nProps, ECCategory *lpParent, unsigned int ulDepth, bool fExpanded, const ECLocale &locale);
    virtual ~ECCategory();
	void IncLeaf() { ++m_ulLeafs; }
	void DecLeaf() { --m_ulLeafs; }
	void IncUnread() { ++m_ulUnread; }
	void DecUnread() { --m_ulUnread; }
	unsigned int GetCount(void) const { return m_ulLeafs; }
    ECRESULT GetProp(struct soap* soap, unsigned int ulPropTag, struct propVal *lpPropVal);
    ECRESULT SetProp(unsigned int i, struct propVal *lpPropVal);
    ECRESULT UpdateMinMax(const sObjectTableKey &sKey, unsigned int i, struct propVal *lpPropVal, bool fMax, bool *lpfModified);
    ECRESULT UpdateMinMaxRemove(const sObjectTableKey &sKey, unsigned int i, bool fMax, bool *lpfModified);
	size_t GetObjectSize(void) const;

    struct propVal *m_lpProps;
    ECCategory *m_lpParent;
	unsigned int m_cProps, m_ulDepth, m_ulCategory;
	unsigned int m_ulUnread = 0, m_ulLeafs = 0;
	bool m_fExpanded;
	const ECLocale& m_locale;
	std::map<sObjectTableKey, struct propVal *> m_mapMinMax;
	ECSortedCategoryMap::iterator iSortedCategory;
	sObjectTableKey m_sCurMinMax;
};

struct LEAFINFO {
    bool fUnread;
    ECCategory *lpCategory;
};

typedef std::map<sObjectTableKey, ECCategory *> ECCategoryMap;
typedef std::map<sObjectTableKey, LEAFINFO> ECLeafMap;
class ECGenericObjectTable;
typedef ECRESULT (*QueryRowDataCallBack)(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **, bool table_data, bool table_limit);

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
	ECRESULT	SetColumns(const struct propTagArray *lpsPropTags, bool bDefaultSet);
	ECRESULT	GetColumns(struct soap *soap, ULONG ulFlags, struct propTagArray **lpsPropTags);
	ECRESULT SetSortOrder(const struct sortOrderArray *, unsigned int categ, unsigned int expanded) __attribute__((nonnull));
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
	static ECRESULT	GetRestrictPropTagsRecursive(const struct restrictTable *, std::list<ULONG> *tags, ULONG level);
	static ECRESULT	GetRestrictPropTags(const struct restrictTable *, std::list<ULONG> *tags, struct propTagArray **);
	static ECRESULT	MatchRowRestrict(ECCacheManager *, struct propValArray *, const struct restrictTable *, const SUBRESTRICTIONRESULTS *, const ECLocale &, bool *match, unsigned int *nsubr = nullptr);
	virtual ECRESULT GetComputedDepth(struct soap *soap, ECSession *lpSession, unsigned int ulObjId, struct propVal *lpProp);

	bool IsMVSet();
	void SetTableId(unsigned int ulTableId);
	virtual	ECRESULT	GetPropCategory(struct soap *soap, unsigned int ulPropTag, sObjectTableKey sKey, struct propVal *lpPropVal);
	virtual unsigned int GetCategories() { return m_ulCategories; }
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
	ECRESULT 	UpdateCategoryMinMax(sObjectTableKey& lpKey, ECCategory *lpCategory, size_t i, struct propVal *lpProps, size_t cProps, bool *lpfModified);

	virtual ECRESULT	ReloadKeyTable();
	ECRESULT GetBinarySortKey(struct propVal *in, ECSortCol &out);
	ECRESULT	GetSortFlags(unsigned int ulPropTag, unsigned char *lpFlags);
	virtual ECRESULT GetMVRowCount(std::list<unsigned int> &&ids, std::map<unsigned int, unsigned int> &count);
	virtual ECRESULT ReloadTable(enumReloadType eType);
	virtual ECRESULT	Load();
	virtual ECRESULT CheckPermissions(unsigned int objid) { return hrSuccess; } /* normally overridden by subclass */
	const ECLocale &GetLocale() const { return m_locale; }

	// Constants
	ECSession*					lpSession;
	std::unique_ptr<ECKeyTable> lpKeyTable;
	unsigned int m_ulTableId = -1; /* id of the table from ECTableManager */
	const void *m_lpObjectData = nullptr;
	std::recursive_mutex m_hLock; /* Lock for locked internals */

	// Locked internals
	struct sortOrderArray *lpsSortOrderArray = nullptr; /* Stored sort order */
	struct propTagArray *lpsPropTagArray = nullptr; /* Stored column set */
	struct restrictTable *lpsRestrict = nullptr; /* Stored restriction */
	ECObjectTableMap			mapObjects;			// Map of all objects in this table
	ECListInt					m_listMVSortCols;	// List of MV sort columns
	bool m_bMVCols = false; /* Are there MV props in the column list */
	bool m_bMVSort = false; /* Are there MV props in the sort order */
	unsigned int				m_ulObjType;
	unsigned int				m_ulFlags;			//< flags from client
	QueryRowDataCallBack m_lpfnQueryRowData = nullptr;

	virtual ECRESULT			AddRowKey(ECObjectTableList* lpRows, unsigned int *lpulLoaded, unsigned int ulFlags, bool bInitialLoad, bool bOverride, struct restrictTable *lpOverrideRestrict);
    virtual ECRESULT			AddCategoryBeforeAddRow(sObjectTableKey sObjKey, struct propVal *lpProps, unsigned int cProps, unsigned int ulFlags, bool fUnread, bool *lpfHidden, ECCategory **lppCategory);
    virtual ECRESULT			RemoveCategoryAfterRemoveRow(sObjectTableKey sObjKey, unsigned int ulFlags);

	ECCategoryMap				m_mapCategories;	// Map between instance key of category and category struct
	ECSortedCategoryMap			m_mapSortedCategories; // Map between category sort keys and instance key. This is where we track which categories we have
	ECLeafMap					m_mapLeafs;			// Map between object instance key and LEAFINFO (contains unread flag and category pointer)
	unsigned int m_ulCategory = 1, m_ulCategories = 0;
	unsigned int m_ulExpanded = 0;
	bool m_bPopulated = false;
	ECLocale					m_locale;
};

} /* namespace */

#endif // ECGENERIC_OBJECTTABLE_H
