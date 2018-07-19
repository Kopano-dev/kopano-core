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

#ifndef MAPIPROPHELPER_INCLUDED
#define MAPIPROPHELPER_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include <mapix.h>
#include <kopano/mapi_ptr.h>
#include <kopano/CommonUtil.h>
#include <kopano/archiver-common.h>
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr

namespace KC { namespace helpers {

class MAPIPropHelper;
typedef std::unique_ptr<MAPIPropHelper> MAPIPropHelperPtr;

class MessageState;

/**
 * The MAPIPropHelper class provides some common utility functions that relate to IMAPIProp
 * objects in the archiver context.
 */
class _kc_export MAPIPropHelper {
public:
	static HRESULT Create(MAPIPropPtr ptrMapiProp, MAPIPropHelperPtr *lpptrMAPIPropHelper);
	_kc_hidden virtual ~MAPIPropHelper(void) = default;
	HRESULT GetMessageState(ArchiverSessionPtr ptrSession, MessageState *lpState);
	HRESULT GetArchiveList(ObjectEntryList *lplstArchives, bool bIgnoreSourceKey = false);
	HRESULT SetArchiveList(const ObjectEntryList &lstArchives, bool bExplicitCommit = false);
	HRESULT SetReference(const SObjectEntry &sEntry, bool bExplicitCommit = false);
	_kc_hidden HRESULT GetReference(SObjectEntry *entry);
	HRESULT ClearReference(bool bExplicitCommit = false);
	HRESULT ReferencePrevious(const SObjectEntry &sEntry);
	HRESULT OpenPrevious(ArchiverSessionPtr ptrSession, LPMESSAGE *lppMessage);
	_kc_hidden HRESULT RemoveStub(void);
	HRESULT SetClean();
	HRESULT DetachFromArchives();
	virtual HRESULT GetParentFolder(ArchiverSessionPtr ptrSession, LPMAPIFOLDER *lppFolder);

protected:
	_kc_hidden MAPIPropHelper(MAPIPropPtr);
	_kc_hidden HRESULT Init(void);

private:
	MAPIPropPtr m_ptrMapiProp;

	PROPMAP_DECL()
	PROPMAP_DEF_NAMED_ID(ARCHIVE_STORE_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ORIGINAL_SOURCEKEY)
	PROPMAP_DEF_NAMED_ID(STUBBED)
	PROPMAP_DEF_NAMED_ID(DIRTY)
	PROPMAP_DEF_NAMED_ID(REF_STORE_ENTRYID)
	PROPMAP_DEF_NAMED_ID(REF_ITEM_ENTRYID)
	PROPMAP_DEF_NAMED_ID(REF_PREV_ENTRYID)
};

class MessageState _kc_final {
public:
	bool isStubbed() const { return m_ulState & msStubbed; }
	bool isDirty() const { return m_ulState & msDirty; }
	bool isCopy() const { return m_ulState & msCopy; }
	bool isMove() const { return m_ulState & msMove; }

private:
	enum msFlags {
		msStubbed	= 0x01,	//<	The message is stubbed, mutual exlusive with msDirty
		msDirty		= 0x02,	//<	The message is dirty, mutual exclusive with msStubbed
		msCopy		= 0x04,	//< The message is copied, mutual exclusive with msMove
		msMove		= 0x08	//< The message is moved, mutual exclusive with msCopy
	};

	ULONG m_ulState = 0;
friend class MAPIPropHelper;
};

}} /* namespace */

#endif // !defined MAPIPROPHELPER_INCLUDED
