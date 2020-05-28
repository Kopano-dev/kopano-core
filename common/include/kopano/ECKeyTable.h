/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once

/*
 * How this works
 *
 * Basically, we only have to keep a table in-memory of the object IDs of the objects
 * in a table, so when data is requested, we can give a list of all the object IDs
 * and the actual data can be retrieved from the database.
 *
 * The table class handles this, by having a table of rows, with per-row a TableRow
 * object. Each row contains an object ID and a sort key. This makes it very fast
 * to add new rows into the table, because we can pretty easily add (or remove) the
 * row into the sorted tree.
 *
 * The caller has to make sure that the sort key is correct.
 *
 * The sorting is done by a binary compare of the sortKeys.
 *
 * Memory considerations:
 *
 * Say we have 100 users, with each having 10 tables open with in each table, 10000 rows,
 * this means we have 10000*100*10*sizeof(TableRow) data in memory. Assume we're sorting on
 * two columns, both ints, then this is another 8 bytes of data per row, which would
 * give us a mem. requirement of 10000*100*10*(12+8) = 190 Mb of memory. This is pretty
 * good for 100 (heavy) users.
 *
 * Of course, when we're sorting on strings, which are 50 bytes each, the memory usage goes
 * up dramatically; sorting on 1 column with 50 bytes of sort data uses 476 Mb of mem.
 *
 * This could optimised by only sorting the first X characters, and expanding the sort key
 * when more precision is required.
 *
 * This structure will be hogging the largest amount of memory of all the server-side components,
 * that's for sure.
 *
 */
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>
#define BOOKMARK_LIMIT		100

namespace KC {

struct sObjectTableKey {
    sObjectTableKey(unsigned int obj_id, unsigned int order_id) : ulObjId(obj_id), ulOrderId(order_id) {}
	sObjectTableKey(void) = default;
	unsigned int ulObjId = 0, ulOrderId = 0;

	bool operator==(const sObjectTableKey &o) const noexcept
	{
		return ulObjId == o.ulObjId && ulOrderId == o.ulOrderId;
	}

	bool operator!=(const sObjectTableKey &o) const noexcept { return !operator==(o); }

	bool operator<(const sObjectTableKey &o) const noexcept
	{
		return std::tie(ulObjId, ulOrderId) < std::tie(o.ulObjId, o.ulOrderId);
	}

	bool operator>(const sObjectTableKey &o) const noexcept
	{
		return std::tie(ulObjId, ulOrderId) > std::tie(o.ulObjId, o.ulOrderId);
	}
};

typedef std::map<sObjectTableKey, unsigned int> ECObjectTableMap;
typedef std::list<sObjectTableKey> ECObjectTableList;

#define TABLEROW_FLAG_DESC		0x00000001
#define TABLEROW_FLAG_FLOAT		0x00000002
#define TABLEROW_FLAG_STRING	0x00000004

struct ECSortCol {
	public:
	uint8_t flags = 0;
	bool isnull = false; /* go use std::optional with C++17 */
	std::string key;
};

class ECSortColView {
	private:
	const size_t m_off = 0, m_len = -1;
	const std::vector<ECSortCol> &m_vec;

	public:
	ECSortColView(const std::vector<ECSortCol> &v, size_t o = 0, size_t l = -1) :
		m_off(o), m_len(l), m_vec(v) {}
	const ECSortCol &operator[](size_t i) const { return m_vec[m_off+i]; }
	size_t size() const
	{
		if (m_len != -1)
			return m_len;
		size_t z = m_vec.size();
		if (m_off <= z)
			return z - m_off;
		return 0;
	}
};

class KC_EXPORT ECTableRow KC_FINAL {
public:
	ECTableRow(const sObjectTableKey &, const std::vector<ECSortCol> &, bool hidden);
	ECTableRow(const sObjectTableKey &, std::vector<ECSortCol> &&, bool hidden);
	ECTableRow(sObjectTableKey &&, std::vector<ECSortCol> &&, bool hidden);
	ECTableRow(const ECTableRow &other);
	KC_HIDDEN size_t GetObjectSize() const;
	KC_HIDDEN static bool rowcompare(const ECTableRow *, const ECTableRow *);
	KC_HIDDEN static bool rowcompare(const ECSortColView &, const ECSortColView &, bool ignore_order = false);
	KC_HIDDEN static bool rowcompareprefix(size_t prefix, const std::vector<ECSortCol> &, const std::vector<ECSortCol> &);
	bool operator < (const ECTableRow &other) const;

	sObjectTableKey	sKey;
	std::vector<ECSortCol> m_cols;

	// b-tree data
	ECTableRow *lpParent = nullptr;
	ECTableRow *lpLeft = nullptr; // All nodes in left are such that *left < *this
	ECTableRow *lpRight = nullptr; // All nodes in right are such that *this <= *right
	unsigned int ulBranchCount = 0; // Count of all nodes in this branch (including this node)
	unsigned int ulHeight = 0; // For AVL
	unsigned int fLeft = 0; // 1 if this is a left node
	bool fRoot = false; // Only the root node has TRUE here
	bool		fHidden;		// The row is hidden (is it non-existent for all purposes)
};

typedef std::map<sObjectTableKey, ECTableRow *> ECTableRowMap;

struct sBookmarkPosition {
	unsigned int	ulFirstRowPosition;
	ECTableRow*		lpPosition;
};

typedef std::map<unsigned int, sBookmarkPosition> ECBookmarkMap;

class KC_EXPORT ECKeyTable KC_FINAL {
public:
	/* this MUST be the same definitions as TABLE_NOTIFICATION event types passed in ulTableEvent */

	// FIXME this is rather ugly, the names must differ from those in mapi.h, as they clash !
	enum UpdateType {
		TABLE_CHANGE=1, TABLE_ERR, TABLE_ROW_ADD,
					TABLE_ROW_DELETE, TABLE_ROW_MODIFY,TABLE_SORT,
					TABLE_RESTRICT, TABLE_SETCOL, TABLE_DO_RELOAD,
	};

	enum { EC_SEEK_SET=0, EC_SEEK_CUR, EC_SEEK_END };

	ECKeyTable();
	~ECKeyTable();
	ECRESULT UpdateRow(UpdateType ulType, const sObjectTableKey *lpsRowItem, std::vector<ECSortCol> &&, sObjectTableKey *lpsPrevRow, bool fHidden = false, UpdateType *lpulAction = nullptr);
	ECRESULT UpdateRow_Delete(const sObjectTableKey *row, std::vector<ECSortCol> &&, sObjectTableKey *prev_row, bool hidden = false, UpdateType *action = nullptr);
	ECRESULT UpdateRow_Modify(const sObjectTableKey *row, std::vector<ECSortCol> &&, sObjectTableKey *prev_row, bool hidden = false, UpdateType *action = nullptr);
	ECRESULT	GetPreviousRow(const sObjectTableKey *lpsRowItem, sObjectTableKey *lpsPrevItem);
	ECRESULT	SeekRow(unsigned int ulBookmark, int lSeekTo, int *lplRowsSought);
	ECRESULT	SeekId(const sObjectTableKey *lpsRowItem);
	ECRESULT	GetRowCount(unsigned int *ulRowCount, unsigned int *ulCurrentRow);
	ECRESULT	QueryRows(unsigned int ulRows, ECObjectTableList* lpRowList, bool bDirBackward, unsigned int ulFlags, bool bShowHidden = false);
	ECRESULT	Clear();
	KC_HIDDEN ECRESULT GetBookmark(unsigned int p1, int *p2);
	ECRESULT	CreateBookmark(unsigned int* lpulbkPosition);
	ECRESULT	FreeBookmark(unsigned int ulbkPosition);
	ECRESULT	GetRowsBySortPrefix(sObjectTableKey *lpsRowItem, ECObjectTableList *lpRowList);
	ECRESULT	HideRows(sObjectTableKey *lpsRowItem, ECObjectTableList *lpHiddenList);
	ECRESULT	UnhideRows(sObjectTableKey *lpsRowItem, ECObjectTableList *lpUnhiddenList);
	// Returns the first row where the sort columns are not less than the specified sortkey
	ECRESULT LowerBound(const std::vector<ECSortCol> &);
	ECRESULT Find(const std::vector<ECSortCol> &, sObjectTableKey *);
	ECRESULT UpdatePartialSortKey(sObjectTableKey *lpsRowItem, size_t ulColumn, const ECSortCol &, sObjectTableKey *lpsPrevRow, bool *lpfHidden, ECKeyTable::UpdateType *lpulAction);
	ECRESULT 	GetRow(sObjectTableKey *lpsRowItem, ECTableRow **lpRow);
	size_t GetObjectSize();

private:
	KC_HIDDEN ECRESULT UpdateCounts(ECTableRow *);
	KC_HIDDEN ECRESULT CurrentRow(ECTableRow *, unsigned int *current_row);
	KC_HIDDEN ECRESULT InvalidateBookmark(ECTableRow *);

	// Functions for implementation AVL balancing
	KC_HIDDEN void RotateL(ECTableRow *pivot);
	KC_HIDDEN void RotateR(ECTableRow *pivot);
	KC_HIDDEN void RotateLR(ECTableRow *pivot);
	KC_HIDDEN void RotateRL(ECTableRow *pivot);
	KC_HIDDEN unsigned int GetHeight(ECTableRow *root) const { return root->ulHeight; }
	KC_HIDDEN int GetBalance(ECTableRow *root);
	KC_HIDDEN void Restructure(ECTableRow *pivot);
	KC_HIDDEN void RestructureRecursive(ECTableRow *);

	// Advance / reverse cursor by one position
	KC_HIDDEN void Next();
	KC_HIDDEN void Prev();

	std::recursive_mutex mLock; /* Locks the entire b-tree */
	ECTableRow				*lpRoot;		// The root node, which is infinitely 'low', ie all nodes are such that *node > *root
	ECTableRow				*lpCurrent;		// The current node
	ECTableRowMap			mapRow;
	ECBookmarkMap			m_mapBookmarks;
	unsigned int			m_ulBookmarkPosition;
};

#define EC_TABLE_NOADVANCE 1

} /* namespace */
