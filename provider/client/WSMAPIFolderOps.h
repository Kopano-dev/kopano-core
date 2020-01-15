/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef WSMAPIFOLDEROPS_H
#define WSMAPIFOLDEROPS_H

#include <mutex>
#include <kopano/ECUnknown.h>
#include <kopano/memory.hpp>
#include "kcore.hpp"
#include <kopano/kcodes.h>
#include <kopano/Util.h>
#include "ics_client.hpp"
#include <vector>
#include <mapi.h>
#include <mapispi.h>
#include "soapStub.h"

namespace KC {
class utf8string;
}

class WSTransport;

class WSMAPIFolderOps final : public KC::ECUnknown {
protected:
	WSMAPIFolderOps(KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, WSTransport *);
	virtual ~WSMAPIFolderOps();

public:
	static HRESULT Create(KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, WSTransport *, WSMAPIFolderOps **);
	virtual HRESULT QueryInterface(const IID &, void **) override;

	// Creates a folder object with only a PR_DISPLAY_NAME and type
	virtual HRESULT HrCreateFolder(ULONG fl_type, const KC::utf8string &name, const KC::utf8string &comment, BOOL fOpenIfExists, ULONG sync_id, const SBinary *srckey, ULONG neweid_size, ENTRYID *neweid, ULONG *eid_size, ENTRYID **eid);

	// Completely remove a folder, the messages in it, the folders in it or any combination
	virtual HRESULT HrDeleteFolder(ULONG eid_size, const ENTRYID *, ULONG flags, ULONG sync_id);

	// Empty folder (ie delete all folders and messages in folder)
	virtual HRESULT HrEmptyFolder(ULONG ulFlags, ULONG ulSyncId);

	// Set read/unread flags on messages
	virtual HRESULT HrSetReadFlags(ENTRYLIST *lpMsgList, ULONG ulFlags, ULONG ulSyncId);

	// Set / Get search criteria
	virtual HRESULT HrSetSearchCriteria(const ENTRYLIST *msglist, const SRestriction *, ULONG flags);
	virtual HRESULT HrGetSearchCriteria(ENTRYLIST **lppMsgList, LPSRestriction *lppRestriction, ULONG *lpulFlags);

	// Move or copy a folder
	virtual HRESULT HrCopyFolder(ULONG srceid_size, const ENTRYID *srceid, ULONG dsteid_size, const ENTRYID *dsteid, const KC::utf8string &newname, ULONG flags, ULONG sync_id);

	// Move or copy a message
	virtual HRESULT HrCopyMessage(ENTRYLIST *msglist, ULONG eid_size, const ENTRYID *dest_eid, ULONG flags, ULONG sync_id);

	// Message status
	virtual HRESULT HrGetMessageStatus(ULONG eid_size, const ENTRYID *, ULONG flags, ULONG *status);
	virtual HRESULT HrSetMessageStatus(ULONG eid_size, const ENTRYID *, ULONG new_status, ULONG stmask, ULONG sync_id, ULONG *old_status);

	// Streaming Support
	virtual HRESULT HrGetChangeInfo(ULONG eid_size, const ENTRYID *, SPropValue **pcl, SPropValue **ck);

	// Reload callback
	static HRESULT Reload(void *lpParam, KC::ECSESSIONID);

private:
	entryId			m_sEntryId;		// Entryid of the folder
	KC::ECSESSIONID ecSessionId;
	ULONG			m_ulSessionReloadCallback;
	KC::object_ptr<WSTransport> m_lpTransport;
	ALLOC_WRAP_FRIEND;
};

#endif
