/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef deleter_INCLUDED
#define deleter_INCLUDED

#include <memory>
#include "operations.h"
#include <kopano/archiver-common.h>
#include <list>

namespace KC { namespace operations {

/**
 * Performs the delete part of the archive operation.
 */
class Deleter final : public ArchiveOperationBaseEx {
public:
	Deleter(std::shared_ptr<ECArchiverLogger>, int ulAge, bool bProcessUnread);
	~Deleter();

private:
	HRESULT EnterFolder(IMAPIFolder *) override { return hrSuccess; }
	HRESULT LeaveFolder() override;
	HRESULT DoProcessEntry(const SRow &proprow) override;
	HRESULT PurgeQueuedMessages();

	std::list<entryid_t> m_lstEntryIds;
};

}} /* namespace */

#endif // ndef deleter_INCLUDED
