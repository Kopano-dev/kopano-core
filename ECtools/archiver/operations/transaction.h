/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef TRANSACTION_INCLUDED
#define TRANSACTION_INCLUDED

#include <kopano/zcdefs.h>
#include "transaction_fwd.h"
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr
#include <kopano/archiver-common.h>
#include "postsaveaction.h"
#include <kopano/mapi_ptr.h>

namespace KC { namespace operations {

class Transaction _kc_final {
public:
	Transaction(const SObjectEntry &objectEntry);
	HRESULT SaveChanges(ArchiverSessionPtr ptrSession, RollbackPtr *lpptrRollback);
	HRESULT PurgeDeletes(ArchiverSessionPtr ptrSession, TransactionPtr ptrDeferredTransaction = TransactionPtr());
	const SObjectEntry& GetObjectEntry() const;

	HRESULT Save(IMessage *lpMessage, bool bDeleteOnFailure, const PostSaveActionPtr &ptrPSAction = PostSaveActionPtr());
	HRESULT Delete(const SObjectEntry &objectEntry, bool bDeferredDelete = false);

private:
	struct SaveEntry {
		MessagePtr	ptrMessage;
		bool bDeleteOnFailure;
		PostSaveActionPtr ptrPSAction;
	};
	typedef std::list<SaveEntry>	MessageList;

	struct DelEntry {
		SObjectEntry objectEntry;
		bool bDeferredDelete;
	};
	typedef std::list<DelEntry>	ObjectList;

	const SObjectEntry	m_objectEntry;
	MessageList m_lstSave;
	ObjectList m_lstDelete;
};

inline const SObjectEntry& Transaction::GetObjectEntry() const
{
	return m_objectEntry;
}

class Rollback _kc_final {
public:
	HRESULT Delete(ArchiverSessionPtr ptrSession, IMessage *lpMessage);
	HRESULT Execute(ArchiverSessionPtr ptrSession);

private:
	struct DelEntry {
		MAPIFolderPtr ptrFolder;
		entryid_t eidMessage;
	};
	typedef std::list<DelEntry>	MessageList;
	MessageList	m_lstDelete;
};

}} /* namespace */

#endif // !defined TRANSACTION_INCLUDED
