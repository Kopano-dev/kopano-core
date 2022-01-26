/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef KC_CMDUTIL_HPP
#define KC_CMDUTIL_HPP 1

#include <kopano/zcdefs.h>
#include <stdexcept>
#include <utility>
#include "ECICS.h"
#include "SOAPUtils.h"
#include <map>
#include <set>
#include <list>
#include <string>

namespace KC {

// Above EC_TABLE_CHANGE_THRESHOLD, a TABLE_CHANGE notification is sent instead of individual notifications
#define EC_TABLE_CHANGE_THRESHOLD 10

class EntryId final {
public:
    EntryId() {
        updateStruct();
    }
    EntryId(const EntryId &s) {
        m_data = s.m_data;
        updateStruct();
    }
	EntryId(EntryId &&o) :
	    m_data(std::move(o.m_data))
	{
		updateStruct();
	}
    EntryId(const entryId *entryid) {
        if(entryid)
            m_data = std::string((char *)entryid->__ptr, entryid->__size);
        else
            m_data.clear();
        updateStruct();
    }
    EntryId(const entryId& entryid) {
        m_data = std::string((char *)entryid.__ptr, entryid.__size);
        updateStruct();
    }
    EntryId(const std::string& data) {
        m_data = data;
        updateStruct();
    }
    EntryId&  operator= (const EntryId &s) {
        m_data = s.m_data;
        updateStruct();
        return *this;
    }

	EntryId &operator=(EntryId &&s)
	{
		m_data = std::move(s.m_data);
		updateStruct();
		return *this;
	}

	/* EID_V0 is marked packed, so direct-access w/o memcpy is ok */
    unsigned int type() const {
		auto d = reinterpret_cast<EID_V0 *>(const_cast<char *>(m_data.data()));
		if (m_data.size() >= offsetof(EID_V0, usType) + sizeof(d->usType))
			return d->usType;
		ec_log_err("K-1570: %s: entryid has size %zu; not enough for EID_V0.usType (%zu)",
			__func__, m_data.size(), offsetof(EID_V0, usType) + sizeof(d->usType));
		throw std::runtime_error("K-1570: entryid is not of type EID_V0");
    }

    void setFlags(unsigned int ulFlags) {
		auto d = reinterpret_cast<EID_V0 *>(const_cast<char *>(m_data.data()));
		if (m_data.size() < offsetof(EID_V0, usFlags) + sizeof(d->usFlags)) {
			ec_log_err("K-1571: %s: entryid has size %zu; not enough for EID_V0.usFlags (%zu)",
				__func__, m_data.size(), offsetof(EID_V0, usFlags) + sizeof(d->usFlags));
			throw std::runtime_error("K-1571: entryid is not of type EID_V0");
		}
		d->usFlags = ulFlags;
    }

	bool operator==(const EntryId &s) const noexcept { return m_data == s.m_data; }
	bool operator<(const EntryId &s) const noexcept { return m_data < s.m_data; }
    operator const std::string& () const { return m_data; }
    operator unsigned char *() const { return (unsigned char *)m_data.data(); }
	operator void *() { return const_cast<char *>(m_data.data()); }
    operator entryId *() { return &m_sEntryId; }
    unsigned int 	size() const { return m_data.size(); }
	bool			empty() const { return m_data.empty(); }

private:
    void updateStruct() {
        m_sEntryId.__size = m_data.size();
        m_sEntryId.__ptr = (unsigned char *)m_data.data();
    }

    entryId m_sEntryId;
    std::string m_data;
};

// this belongs to the DeleteObjects function
struct DELETEITEM {
	unsigned int ulId;
	unsigned int ulParent;
	bool fRoot;
	bool fInOGQueue;
	short ulObjType;
	short ulParentType;
	unsigned int ulFlags;
	unsigned int ulStoreId;
	unsigned int ulObjSize;
	unsigned int ulMessageFlags;
	SOURCEKEY sSourceKey;
	SOURCEKEY sParentSourceKey;
	entryId sEntryId; //fixme: auto destroy entryid
};

// Used to group notify flags and object type.
struct TABLECHANGENOTIFICATION {
	TABLECHANGENOTIFICATION(unsigned int t, unsigned int f): ulFlags(f), ulType(t) {}
	bool operator<(const TABLECHANGENOTIFICATION &rhs) const noexcept
	{
		return ulFlags < rhs.ulFlags || (ulFlags == rhs.ulFlags && ulType < rhs.ulType);
	}

	unsigned int ulFlags, ulType;
};

class PARENTINFO final {
public:
	int lItems = 0, lFolders = 0, lAssoc = 0, lUnread = 0;
	int lDeleted = 0, lDeletedFolders = 0, lDeletedAssoc = 0;
	unsigned int ulStoreId = 0;
};

#define EC_DELETE_FOLDERS		0x00000001
#define EC_DELETE_MESSAGES		0x00000002
#define EC_DELETE_RECIPIENTS	0x00000004
#define EC_DELETE_ATTACHMENTS	0x00000008
#define EC_DELETE_CONTAINER		0x00000010
#define EC_DELETE_HARD_DELETE	0x00000020
#define	EC_DELETE_STORE			0x00000040
#define EC_DELETE_NOT_ASSOCIATED_MSG	0x00000080

typedef std::list<DELETEITEM>	ECListDeleteItems;
typedef std::set<TABLECHANGENOTIFICATION>	ECSetTableChangeNotifications;
typedef std::map<unsigned int, ECSetTableChangeNotifications> ECMapTableChangeNotifications;

void FreeDeletedItems(ECListDeleteItems *lplstDeleteItems);

class ECAttachmentStorage;

ECRESULT GetSourceKey(unsigned int ulObjId, SOURCEKEY *lpSourceKey);
ECRESULT ExpandDeletedItems(ECSession *lpSession, ECDatabase *lpDatabase, ECListInt *lpsObjectList, unsigned int ulFlags, bool bCheckPermission, ECListDeleteItems *lplstDeleteItems);
ECRESULT DeleteObjectHard(ECSession *lpSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, unsigned int ulFlags, ECListDeleteItems &lstDeleteItems, bool bNoTransaction, ECListDeleteItems &lstDeleted);
ECRESULT DeleteObjectStoreSize(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulFlags, ECListDeleteItems &lstDeleted);
ECRESULT DeleteObjectCacheUpdate(ECSession *lpSession, unsigned int ulFlags, ECListDeleteItems &lstDeleted);
ECRESULT DeleteObjects(ECSession *lpSession, ECDatabase *lpDatabase, ECListInt *lpsObjectList, unsigned int ulFlags, unsigned int ulSyncId, bool bNoTransaction, bool bCheckPermission);
ECRESULT DeleteObjects(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulObjectId, unsigned int ulFlags, unsigned int ulSyncId, bool bNoTransaction, bool bCheckPermission);
ECRESULT MarkStoreAsDeleted(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulStoreHierarchyId, unsigned int ulSyncId);
ECRESULT WriteLocalCommitTimeMax(struct soap *soap, ECDatabase *lpDatabase, unsigned int ulFolderId, propVal **ppvTime);
ECRESULT UpdateTProp(ECDatabase *lpDatabase, unsigned int ulPropTag, unsigned int ulFolderId, ECListInt *lpObjectIDs);
ECRESULT UpdateTProp(ECDatabase *lpDatabase, unsigned int ulPropTag, unsigned int ulFolderId, unsigned int ulObjId);
ECRESULT UpdateFolderCount(ECDatabase *lpDatabase, unsigned int ulFolderId, unsigned int ulPropTag, int lDelta);
ECRESULT CheckQuota(ECSession *lpecSession, ULONG ulStoreId);
ECRESULT MapEntryIdToObjectId(ECSession *lpecSession, ECDatabase *lpDatabase, ULONG ulObjId, const entryId &sEntryId);
ECRESULT UpdateFolderCounts(ECDatabase *lpDatabase, ULONG ulParentId, ULONG ulFlags, propValArray *lpModProps);
ECRESULT ProcessSubmitFlag(ECDatabase *lpDatabase, ULONG ulSyncId, ULONG ulStoreId, ULONG ulObjId, bool bNewItem, propValArray *lpModProps);
ECRESULT CreateNotifications(ULONG ulObjId, ULONG ulObjType, ULONG ulParentId, ULONG ulGrandParentId, bool bNewItem, propValArray *lpModProps, struct propVal *lpvCommitTime);
ECRESULT WriteSingleProp(ECDatabase *lpDatabase, unsigned int ulObjId, unsigned int ulFolderId, struct propVal *lpPropVal, bool bColumnProp, unsigned int ulMaxQuerySize, std::string &strInsertQuery, bool replace = true);
ECRESULT WriteProp(ECDatabase *lpDatabase, unsigned int ulObjId, unsigned int ulParentId, struct propVal *lpPropVal, bool replace = true);
ECRESULT GetNamesFromIDs(struct soap *soap, ECDatabase *lpDatabase, struct propTagArray *lpPropTags, struct namedPropArray *lpsNames);
ECRESULT ResetFolderCount(ECSession *lpSession, unsigned int ulObjId, unsigned int *lpulUpdates = NULL);
extern ECRESULT RemoveStaleIndexedProp(ECDatabase *, unsigned int tag, const unsigned char *data, unsigned int size);
ECRESULT ApplyFolderCounts(ECDatabase *lpDatabase, const std::map<unsigned int, PARENTINFO> &mapFolderCounts);

#define LOCK_SHARED 	0x00000001
#define LOCK_EXCLUSIVE	0x00000002

// Lock folders and start transaction:
extern ECRESULT BeginLockFolders(ECDatabase *, unsigned int proptag, const std::set<std::string> &ids, unsigned int flags, kd_trans &, ECRESULT &);
extern ECRESULT BeginLockFolders(ECDatabase *, const EntryId &, unsigned int flags, kd_trans &, ECRESULT &); /* single entryid, folder or message */

struct NAMEDPROPDEF {
    GUID			guid;
    unsigned int	ulKind;
    unsigned int	ulId;
    std::string		strName;
};
typedef std::map<unsigned int, NAMEDPROPDEF> NamedPropDefMap;

struct CHILDPROPS {
	CHILDPROPS(struct soap *soap, unsigned int hint = 20);
	/* There may be fewer values cached in @lpPropVals than tags are present in @lpPropTags. */
	std::unique_ptr<DynamicPropTagArray> lpPropTags;
	std::unique_ptr<DynamicPropValArray> lpPropVals;
};
typedef std::map<unsigned int, CHILDPROPS> ChildPropsMap;

ECRESULT PrepareReadProps(struct soap *soap, ECDatabase *lpDatabase, bool fDoQuery, bool fUnicode, unsigned int ulObjId, unsigned int ulParentId, unsigned int ulMaxSize, ChildPropsMap *lpChildProps, NamedPropDefMap *lpNamedProps);

} /* namespace */

#endif /* KC_CMDUTIL_HPP */
