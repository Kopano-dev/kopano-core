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

class EntryId _kc_final {
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
    ~EntryId() { 
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

	unsigned int ulFlags;
	unsigned int ulType;
};

class PARENTINFO _kc_final {
public:
	int lItems = 0, lFolders = 0, lAssoc = 0;
	int lDeleted = 0, lDeletedFolders = 0, lDeletedAssoc = 0;
	int lUnread = 0;
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

void FreeDeleteItem(DELETEITEM *src);
void FreeDeletedItems(ECListDeleteItems *lplstDeleteItems);

class ECAttachmentStorage;

ECRESULT GetSourceKey(unsigned int ulObjId, SOURCEKEY *lpSourceKey);

ECRESULT ValidateDeleteObject(ECSession *lpSession, bool bCheckPermission, unsigned int ulFlags, const DELETEITEM &sItem);
ECRESULT ExpandDeletedItems(ECSession *lpSession, ECDatabase *lpDatabase, ECListInt *lpsObjectList, unsigned int ulFlags, bool bCheckPermission, ECListDeleteItems *lplstDeleteItems);
ECRESULT DeleteObjectUpdateICS(ECSession *lpSession, unsigned int ulFlags, ECListDeleteItems &lstDeleted, unsigned int ulSyncId);
ECRESULT DeleteObjectSoft(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulFlags, ECListDeleteItems &lstDeleteItems, ECListDeleteItems &lstDeleted);
ECRESULT DeleteObjectHard(ECSession *lpSession, ECDatabase *lpDatabase, ECAttachmentStorage *lpAttachmentStorage, unsigned int ulFlags, ECListDeleteItems &lstDeleteItems, bool bNoTransaction, ECListDeleteItems &lstDeleted);
ECRESULT DeleteObjectStoreSize(ECSession *lpSession, ECDatabase *lpDatabase, unsigned int ulFlags, ECListDeleteItems &lstDeleted);
ECRESULT DeleteObjectCacheUpdate(ECSession *lpSession, unsigned int ulFlags, ECListDeleteItems &lstDeleted);
ECRESULT DeleteObjectNotifications(ECSession *lpSession, unsigned int ulFlags, ECListDeleteItems &lstDeleted);
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

ECRESULT WriteSingleProp(ECDatabase *lpDatabase, unsigned int ulObjId, unsigned int ulFolderId, struct propVal *lpPropVal, bool bColumnProp, unsigned int ulMaxQuerySize, std::string &strInsertQuery);
ECRESULT WriteProp(ECDatabase *lpDatabase, unsigned int ulObjId, unsigned int ulParentId, struct propVal *lpPropVal);

ECRESULT GetNamesFromIDs(struct soap *soap, ECDatabase *lpDatabase, struct propTagArray *lpPropTags, struct namedPropArray *lpsNames);
ECRESULT ResetFolderCount(ECSession *lpSession, unsigned int ulObjId, unsigned int *lpulUpdates = NULL);

ECRESULT RemoveStaleIndexedProp(ECDatabase *lpDatabase, unsigned int ulPropTag, unsigned char *lpData, unsigned int cbSize);

ECRESULT ApplyFolderCounts(ECDatabase *lpDatabase, unsigned int ulFolderId, const PARENTINFO &pi);
ECRESULT ApplyFolderCounts(ECDatabase *lpDatabase, const std::map<unsigned int, PARENTINFO> &mapFolderCounts);

#define LOCK_SHARED 	0x00000001
#define LOCK_EXCLUSIVE	0x00000002

// Lock folders and start transaction: 
ECRESULT BeginLockFolders(ECDatabase *lpDatabase, const std::set<SOURCEKEY>& setObjects, unsigned int ulFlags); // may be mixed list of folders and messages
ECRESULT BeginLockFolders(ECDatabase *lpDatabase, const std::set<EntryId>& setObjects, unsigned int ulFlags);	// may be mixed list of folders and messages
ECRESULT BeginLockFolders(ECDatabase *lpDatabase, const EntryId &entryid, unsigned int ulFlags);				// single entryid, folder or message
ECRESULT BeginLockFolders(ECDatabase *lpDatabase, const SOURCEKEY &sourcekey, unsigned int ulFlags);			// single sourcekey, folder or message

struct NAMEDPROPDEF {
    GUID			guid;
    unsigned int	ulKind;
    unsigned int	ulId;
    std::string		strName;
};
typedef std::map<unsigned int, NAMEDPROPDEF> NamedPropDefMap;

struct CHILDPROPS {
    DynamicPropValArray *lpPropVals;
    DynamicPropTagArray *lpPropTags;
};
typedef std::map<unsigned int, CHILDPROPS> ChildPropsMap;


ECRESULT PrepareReadProps(struct soap *soap, ECDatabase *lpDatabase, bool fDoQuery, bool fUnicode, unsigned int ulObjId, unsigned int ulParentId, unsigned int ulMaxSize, ChildPropsMap *lpChildProps, NamedPropDefMap *lpNamedProps);
ECRESULT FreeChildProps(std::map<unsigned int, CHILDPROPS> *lpChildProps);

} /* namespace */

#endif /* KC_CMDUTIL_HPP */
