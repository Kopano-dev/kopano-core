/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef WSMAPIFOLDEROPS_H
#define WSMAPIFOLDEROPS_H

#include <kopano/ECUnknown.h>
#include <mutex>
#include "kcore.hpp"
#include <kopano/kcodes.h>
#include <kopano/Util.h>
#include <kopano/zcdefs.h>
#include "ics_client.hpp"
#include <vector>
#include <mapi.h>
#include <mapispi.h>
#include "soapStub.h"
#include <kopano/charset/utf8string.h>

class WSTransport;

class WSMAPIFolderOps KC_FINAL_OPG : public KC::ECUnknown {
protected:
	WSMAPIFolderOps(KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, WSTransport *);
	virtual ~WSMAPIFolderOps();

public:
	struct WSFolder {
		unsigned int folder_type;
		KC::utf8string name, comment;
		const SBinary *sourcekey;
		unsigned int open_if_exists;
		unsigned int sync_id;
		unsigned int m_cbNewEntryId;
		unsigned int *m_lpcbEntryId;
		ENTRYID *m_lpNewEntryId;
		ENTRYID **m_lppEntryId;
	};

	static HRESULT Create(KC::ECSESSIONID, ULONG eid_size, const ENTRYID *, WSTransport *, WSMAPIFolderOps **);
	virtual HRESULT QueryInterface(const IID &, void **) override;

	// Creates a folder object with only a PR_DISPLAY_NAME and type
	virtual HRESULT HrCreateFolder(ULONG fl_type, const KC::utf8string &name, const KC::utf8string &comment, BOOL fOpenIfExists, ULONG sync_id, const SBinary *srckey, ULONG neweid_size, ENTRYID *neweid, ULONG *eid_size, ENTRYID **eid);

	/**
	 * Create the specified 'batch' of folders with the specified 'count'
	 * of folders.
	 *
	 * @param batch Batch of folders to create
	 * @param count The number of folders in the batch
	 * @return HRESULT
	 */
	virtual HRESULT create_folders(std::vector<WSFolder> &batch);

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
	WSTransport *	m_lpTransport;
	ALLOC_WRAP_FRIEND;
};

#endif
