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
#include <kopano/platform.h>
#include <kopano/MAPIErrors.h>
#include "ECArchiverLogger.h"
#include "deleter.h"

namespace KC { namespace operations {

/**
 * @param[in]	lpLogger
 *					Pointer to the logger.
 */
Deleter::Deleter(ECArchiverLogger *lpLogger, int ulAge, bool bProcessUnread)
: ArchiveOperationBaseEx(lpLogger, ulAge, bProcessUnread, ARCH_NEVER_DELETE)
{ }

Deleter::~Deleter()
{
	PurgeQueuedMessages();
}

HRESULT Deleter::LeaveFolder()
{
	// Folder is still available, purge now!
	return PurgeQueuedMessages();
}

HRESULT Deleter::DoProcessEntry(const SRow &proprow)
{
	auto lpEntryId = proprow.cfind(PR_ENTRYID);
	if (lpEntryId == NULL) {
		Logger()->Log(EC_LOGLEVEL_FATAL, "PR_ENTRYID missing");
		return MAPI_E_NOT_FOUND;
	}
	if (m_lstEntryIds.size() >= 50) {
		HRESULT hr = PurgeQueuedMessages();
		if (hr != hrSuccess)
			return hr;
	}
	m_lstEntryIds.emplace_back(lpEntryId->Value.bin);
	return hrSuccess;
}

/**
 * Delete the messages that are queued for deletion.
 */
HRESULT Deleter::PurgeQueuedMessages()
{
	EntryListPtr ptrEntryList;
	ULONG ulIdx = 0;
	
	if (m_lstEntryIds.empty())
		return hrSuccess;
	auto hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~ptrEntryList);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(m_lstEntryIds.size() * sizeof(SBinary), ptrEntryList, (LPVOID*)&ptrEntryList->lpbin);
	if (hr != hrSuccess)
		return hr;
		
	ptrEntryList->cValues = m_lstEntryIds.size();
	for (const auto &e : m_lstEntryIds) {
		ptrEntryList->lpbin[ulIdx].cb = e.size();
		ptrEntryList->lpbin[ulIdx++].lpb = e;
	}
	
	hr = CurrentFolder()->DeleteMessages(ptrEntryList, 0, NULL, 0);
	if (hr != hrSuccess) {
		Logger()->logf(EC_LOGLEVEL_FATAL, "Failed to delete %u messages: %s (%x)",
			ptrEntryList->cValues, GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	
	m_lstEntryIds.clear();
	return hrSuccess;
}

}} /* namespace */
