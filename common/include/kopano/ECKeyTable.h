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

#include <pthread.h>

#define BOOKMARK_LIMIT		100

struct sObjectTableKey {
    sObjectTableKey(unsigned int ulObjId, unsigned int ulOrderId) { this->ulObjId = ulObjId; this->ulOrderId = ulOrderId; };
    sObjectTableKey() { ulObjId = 0; ulOrderId = 0; }
	unsigned int ulObjId;
	unsigned int ulOrderId;
};

struct ObjectTableKeyCompare
{
	bool operator()(const sObjectTableKey& a, const sObjectTableKey& b) const
	{
		return a.ulObjId < b.ulObjId || (a.ulObjId == b.ulObjId && a.ulOrderId < b.ulOrderId);
	}
};

bool operator!=(const sObjectTableKey& a, const sObjectTableKey& b);
bool operator==(const sObjectTableKey& a, const sObjectTableKey& b);
bool operator<(const sObjectTableKey& a, const sObjectTableKey& b);
bool operator>(const sObjectTableKey& a, const sObjectTableKey& b);

typedef std::map<sObjectTableKey, unsigned int, ObjectTableKeyCompare>  ECObjectTableMap;
typedef std::list<sObjectTableKey> ECObjectTableList;

#define TABLEROW_FLAG_DESC		0x00000001
#define TABLEROW_FLAG_FLOAT		0x00000002
#define TABLEROW_FLAG_STRING	0x00000004

class ECTableRow _zcp_final {
public:
	ECTableRow(sObjectTableKey sKey, unsigned int ulSortCols, const unsigned int *lpSortLen, const unsigned char *lpFlags, unsigned char **lppSortData, bool fHidden);
	ECTableRow(const ECTableRow &other);
	~ECTableRow();

	unsigned int GetObjectSize(void) const;

	static bool rowcompare(const ECTableRow *a, const ECTableRow *b);
	static bool rowcompare(unsigned int ulSortColsA, const int *lpSortLenA, unsigned char **lppSortKeysA, const unsigned char *lpSortFlagsA, unsigned int ulSortColsB, const int *lpSortLenB, unsigned char **lppSortKeysB, const unsigned char *lpSortFlagsB, bool fIgnoreOrder = false);
	static bool rowcompareprefix(unsigned int ulSortColPrefix, unsigned int ulSortColsA, const int *lpSortLenA, unsigned char **lppSortKeysA, const unsigned char *lpSortFlagsA, unsigned int ulSortColsB, const int *lpSortLenB, unsigned char **lppSortKeysB, const unsigned char *lpSortFlagsB);

	bool operator < (const ECTableRow &other) const;
	

private:
	void initSortCols(unsigned int ulSortCols, const int *lpSortLen, const unsigned char *lpFlags, unsigned char ** lppSortData);
	void freeSortCols();
	ECTableRow& operator = (const ECTableRow &other);
public:
	sObjectTableKey	sKey;

	unsigned int ulSortCols;
	int *lpSortLen;
	unsigned char **lppSortKeys;
	unsigned char *lpFlags;

	// b-tree data
	ECTableRow *lpParent;
	ECTableRow *lpLeft;			// All nodes in left are such that *left < *this
	ECTableRow *lpRight;		// All nodes in right are such that *this <= *right
	unsigned int ulBranchCount;	// Count of all nodes in this branch (including this node)
	unsigned int ulHeight;		// For AVL
	unsigned int fLeft;			// 1 if this is a left node
	bool		fRoot;			// Only the root node has TRUE here
	bool		fHidden;		// The row is hidden (is it non-existent for all purposes)
};

typedef std::map<sObjectTableKey, ECTableRow*, ObjectTableKeyCompare>  ECTableRowMap;

typedef struct {
	unsigned int	ulFirstRowPosition;
	ECTableRow*		lpPosition;
}sBookmarkPosition;

typedef std::map<unsigned int, sBookmarkPosition> ECBookmarkMap;

class ECKeyTable _zcp_final {
public:
	/* this MUST be the same definitions as TABLE_NOTIFICATION event types passed in ulTableEvent */

	// FIXME this is rather ugly, the names must differ from those in mapi.h, as they clash !
	typedef enum { TABLE_CHANGE=1, TABLE_ERR, TABLE_ROW_ADD, 
					TABLE_ROW_DELETE, TABLE_ROW_MODIFY,TABLE_SORT, 
					TABLE_RESTRICT, TABLE_SETCOL, TABLE_DO_RELOAD } UpdateType;

	enum { EC_SEEK_SET=0, EC_SEEK_CUR, EC_SEEK_END };
	
	ECKeyTable();
	~ECKeyTable();

	ECRESULT	UpdateRow(UpdateType ulType, const sObjectTableKey *lpsRowItem, unsigned int ulSortCols, const unsigned int *lpSortLen, const unsigned char *lpFlags, unsigned char **lppSortData, sObjectTableKey *lpsPrevRow, bool fHidden = false, UpdateType *lpulAction = NULL);
	ECRESULT	GetPreviousRow(const sObjectTableKey *lpsRowItem, sObjectTableKey *lpsPrevItem);
	ECRESULT	SeekRow(unsigned int ulBookmark, int lSeekTo, int *lplRowsSought);
	ECRESULT	SeekId(const sObjectTableKey *lpsRowItem);
	ECRESULT	GetRowCount(unsigned int *ulRowCount, unsigned int *ulCurrentRow);
	ECRESULT	QueryRows(unsigned int ulRows, ECObjectTableList* lpRowList, bool bDirBackward, unsigned int ulFlags, bool bShowHidden = false);
	ECRESULT	Clear();

	ECRESULT	GetBookmark(unsigned int ulbkPosition, int* lpbkPosition);
	ECRESULT	CreateBookmark(unsigned int* lpulbkPosition);
	ECRESULT	FreeBookmark(unsigned int ulbkPosition);

	ECRESULT	GetRowsBySortPrefix(sObjectTableKey *lpsRowItem, ECObjectTableList *lpRowList);
	ECRESULT	HideRows(sObjectTableKey *lpsRowItem, ECObjectTableList *lpHiddenList);
	ECRESULT	UnhideRows(sObjectTableKey *lpsRowItem, ECObjectTableList *lpUnhiddenList);

	// Returns the first row where the sort columns are not less than the specified sortkey
	ECRESULT	LowerBound(unsigned int ulSortColPrefixLen, int *lpSortLen, unsigned char **lppSortData, unsigned char *lpFlags);
	ECRESULT 	Find(unsigned int ulSortCols, int *lpSortLen, unsigned char **lppSortData, unsigned char *lpFlags, sObjectTableKey *lpsKey);

	ECRESULT	UpdatePartialSortKey(sObjectTableKey *lpsRowItem, unsigned int ulColumn, unsigned char *lpSortData, unsigned int ulSortLen, unsigned char ulFlags, sObjectTableKey *lpsPrevRow,  bool *lpfHidden,  ECKeyTable::UpdateType *lpulAction);

	ECRESULT 	GetRow(sObjectTableKey *lpsRowItem, ECTableRow **lpRow);
	

	unsigned int GetObjectSize();

private:
	ECRESULT	UpdateCounts(ECTableRow *lpRow);
	ECRESULT	CurrentRow(ECTableRow *lpRow, unsigned int *lpulCurrentRow);
	ECRESULT	InvalidateBookmark(ECTableRow *lpRow);

	// Functions for implemention AVL balancing
	void		RotateL(ECTableRow *lpPivot);
	void		RotateR(ECTableRow *lpPivot);
	void		RotateLR(ECTableRow *lpPivot);
	void		RotateRL(ECTableRow *lpPivot);
	unsigned int GetHeight(ECTableRow *root) { return root->ulHeight; }
	int 		GetBalance(ECTableRow *lpRoot);
	void		Restructure(ECTableRow *lpPivot);
	void		RestructureRecursive(ECTableRow *lpRow);
	

	// Advance / reverse cursor by one position
	void		Next();
	void		Prev();

	pthread_mutex_t			mLock;			// Locks the entire b-tree
	ECTableRow				*lpRoot;		// The root node, which is infinitely 'low', ie all nodes are such that *node > *root
	ECTableRow				*lpCurrent;		// The current node
	ECTableRowMap			mapRow;
	ECBookmarkMap			m_mapBookmarks;
	unsigned int			m_ulBookmarkPosition;
};

#define EC_TABLE_NOADVANCE 1

#endif // TABLE_H
