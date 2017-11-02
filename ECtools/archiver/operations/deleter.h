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
