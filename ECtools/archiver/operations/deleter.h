/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef deleter_INCLUDED
#define deleter_INCLUDED

#include <kopano/zcdefs.h>
#include "operations.h"
#include <kopano/archiver-common.h>
#include <list>

namespace KC { namespace operations {

/**
 * Performs the delete part of the archive operation.
 */
class Deleter _kc_final : public ArchiveOperationBaseEx {
public:
	Deleter(ECArchiverLogger *lpLogger, int ulAge, bool bProcessUnread);
	~Deleter();

private:
	HRESULT EnterFolder(LPMAPIFOLDER)_kc_override { return hrSuccess; }
	HRESULT LeaveFolder(void) _kc_override;
	HRESULT DoProcessEntry(const SRow &proprow) override;
	HRESULT PurgeQueuedMessages();
	
	std::list<entryid_t> m_lstEntryIds;
};

}} /* namespace */

#endif // ndef deleter_INCLUDED
