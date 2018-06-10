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

#ifndef TABLE_H
#define TABLE_H

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
#include <vector>

#define BOOKMARK_LIMIT		100

namespace KC {

struct sObjectTableKey {
    sObjectTableKey(unsigned int obj_id, unsigned int order_id) : ulObjId(obj_id), ulOrderId(order_id) {}
	sObjectTableKey(void) = default;
	unsigned int ulObjId = 0;
	unsigned int ulOrderId = 0;

	bool operator==(const sObjectTableKey &o) const noexcept
	{
		return ulObjId == o.ulObjId && ulOrderId == o.ulOrderId;
	}

	bool operator!=(const sObjectTableKey &o) const noexcept { return !operator==(o); }

	bool operator<(const sObjectTableKey &o) const noexcept
	{
		return ulObjId < o.ulObjId || (ulObjId == o.ulObjId && ulOrderId < o.ulOrderId);
	}

	bool operator>(const sObjectTableKey &o) const noexcept
	{
		return ulObjId > o.ulObjId || (ulObjId == o.ulObjId && ulOrderId > o.ulOrderId);
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

class _kc_export ECTableRow _kc_final {
public:
	ECTableRow(const sObjectTableKey &, const std::vector<ECSortCol> &, bool hidden);
	ECTableRow(const sObjectTableKey &, std::vector<ECSortCol> &&, bool hidden);
	ECTableRow(sObjectTableKey &&, std::vector<ECSortCol> &&, bool hidden);
	ECTableRow(const ECTableRow &other);
	_kc_hidden unsigned int GetObjectSize(void) const;
	_kc_hidden static bool rowcompare(const ECTableRow *, const ECTableRow *);
	_kc_hidden static bool rowcompare(const ECSortColView &, const ECSortColView &, bool ignore_order = false);
	_kc_hidden static bool rowcompareprefix(size_t prefix, const std::vector<ECSortCol> &, const std::vector<ECSortCol> &);
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

class _kc_export ECKeyTable _kc_final {
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

	_kc_hidden ECRESULT GetBookmark(unsigned int p1, int *p2);
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
	

	unsigned int GetObjectSize();

private:
	_kc_hidden ECRESULT UpdateCounts(ECTableRow *);
	_kc_hidden ECRESULT CurrentRow(ECTableRow *, unsigned int *current_row);
	_kc_hidden ECRESULT InvalidateBookmark(ECTableRow *);

	// Functions for implemention AVL balancing
	_kc_hidden void RotateL(ECTableRow *pivot);
	_kc_hidden void RotateR(ECTableRow *pivot);
	_kc_hidden void RotateLR(ECTableRow *pivot);
	_kc_hidden void RotateRL(ECTableRow *pivot);
	_kc_hidden unsigned int GetHeight(ECTableRow *root) { return root->ulHeight; }
	_kc_hidden int GetBalance(ECTableRow *root);
	_kc_hidden void Restructure(ECTableRow *pivot);
	_kc_hidden void RestructureRecursive(ECTableRow *);

	// Advance / reverse cursor by one position
	_kc_hidden void Next(void);
	_kc_hidden void Prev(void);

	std::recursive_mutex mLock; /* Locks the entire b-tree */
	ECTableRow				*lpRoot;		// The root node, which is infinitely 'low', ie all nodes are such that *node > *root
	ECTableRow				*lpCurrent;		// The current node
	ECTableRowMap			mapRow;
	ECBookmarkMap			m_mapBookmarks;
	unsigned int			m_ulBookmarkPosition;
};

#define EC_TABLE_NOADVANCE 1

} /* namespace */

#endif // TABLE_H
