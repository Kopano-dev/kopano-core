/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include "transaction_fwd.h"
#include "ArchiverSessionPtr.h"     // For ArchiverSessionPtr
#include <kopano/archiver-common.h>
#include "postsaveaction.h"
#include <kopano/memory.hpp>

namespace KC { namespace operations {

class Transaction final {
public:
	Transaction(const SObjectEntry &objectEntry);
	HRESULT SaveChanges(ArchiverSessionPtr ptrSession, RollbackPtr *lpptrRollback);
	HRESULT PurgeDeletes(ArchiverSessionPtr ptrSession, TransactionPtr ptrDeferredTransaction = TransactionPtr());
	const SObjectEntry& GetObjectEntry() const;

	HRESULT Save(IMessage *lpMessage, bool bDeleteOnFailure, const PostSaveActionPtr &ptrPSAction = PostSaveActionPtr());
	HRESULT Delete(const SObjectEntry &objectEntry, bool bDeferredDelete = false);

private:
	struct SaveEntry {
		object_ptr<IMessage> ptrMessage;
		bool bDeleteOnFailure;
		PostSaveActionPtr ptrPSAction;
	};

	struct DelEntry {
		SObjectEntry objectEntry;
		bool bDeferredDelete;
	};

	const SObjectEntry	m_objectEntry;
	std::list<SaveEntry> m_lstSave;
	std::list<DelEntry> m_lstDelete;
};

inline const SObjectEntry& Transaction::GetObjectEntry() const
{
	return m_objectEntry;
}

class Rollback final {
public:
	HRESULT Delete(ArchiverSessionPtr ptrSession, IMessage *lpMessage);
	HRESULT Execute(ArchiverSessionPtr ptrSession);

private:
	struct DelEntry {
		object_ptr<IMAPIFolder> ptrFolder;
		entryid_t eidMessage;
	};
	std::list<DelEntry> m_lstDelete;
};

}} /* namespace */
