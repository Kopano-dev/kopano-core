/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include <memory>
#include <kopano/archiver-common.h>
#include "postsaveaction.h"
#include <kopano/memory.hpp>

namespace KC {

class ArchiverSession;

namespace operations {

class Rollback;
class Transaction final {
public:
	Transaction(const SObjectEntry &objectEntry);
	HRESULT SaveChanges(std::shared_ptr<ArchiverSession>, std::shared_ptr<Rollback> *);
	HRESULT PurgeDeletes(std::shared_ptr<ArchiverSession>, std::shared_ptr<Transaction> deferred_tx = nullptr);
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
	HRESULT Delete(std::shared_ptr<ArchiverSession>, IMessage *);
	HRESULT Execute(std::shared_ptr<ArchiverSession>);

private:
	struct DelEntry {
		object_ptr<IMAPIFolder> ptrFolder;
		entryid_t eidMessage;
	};
	std::list<DelEntry> m_lstDelete;
};

}} /* namespace */
