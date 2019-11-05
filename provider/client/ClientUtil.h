/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef CLIENTUTIL_H
#define CLIENTUTIL_H

#include <mutex>
#include <mapispi.h>
#include <string>
#include <kopano/ECTags.h>
#include <edkmdb.h>
#include <kopano/zcdefs.h>

class KCmdProxy2;
class WSTransport;
struct sGlobalProfileProps;

namespace ClientUtil {

extern HRESULT HrInitializeStatusRow(const char *provider_display, unsigned int res_type, IMAPISupport *, SPropValue *identity, unsigned int flags);
extern HRESULT HrSetIdentity(WSTransport *, IMAPISupport *, SPropValue **idprops);
extern HRESULT ReadReceipt(unsigned int flags, IMessage *read_msg, IMessage **empty_msg);

/* Get the global properties */
extern HRESULT GetGlobalProfileProperties(IProfSect *global, struct sGlobalProfileProps *);
extern HRESULT GetGlobalProfileProperties(IMAPISupport *, struct sGlobalProfileProps *);

/* Get the delegate stores from the global profile. */
extern HRESULT GetGlobalProfileDelegateStoresProp(IProfSect *global, unsigned int *ndelegates, BYTE **dlg_stores);

}

class WSSoap {
	public:
	std::unique_ptr<KCmdProxy2> m_lpCmd;
	std::recursive_mutex m_hDataLock;
};

class soap_lock_guard {
	public:
	soap_lock_guard(WSSoap &);
	soap_lock_guard(const soap_lock_guard &) = delete;
	~soap_lock_guard();
	void operator=(const soap_lock_guard &) = delete;
	void unlock();

	private:
	WSSoap &m_parent;
	std::unique_lock<std::recursive_mutex> m_dg;
	bool m_done = false;
};

extern HRESULT HrCreateEntryId(const GUID &store_guid, unsigned int obj_type, ULONG *eid_size, ENTRYID **eid);
extern HRESULT HrGetServerURLFromStoreEntryId(ULONG eid_size, const ENTRYID *eid, std::string &srv_path, bool *pseudo);
HRESULT HrResolvePseudoUrl(WSTransport *lpTransport, const char *lpszUrl, std::string& serverPath, bool *lpbIsPeer);
extern HRESULT HrCompareEntryIdWithStoreGuid(ULONG eid_size, const ENTRYID *eid, const GUID *store_guid);

enum enumPublicEntryID { ePE_None, ePE_IPMSubtree, ePE_Favorites, ePE_PublicFolders, ePE_FavoriteSubFolder };

extern HRESULT GetPublicEntryId(enumPublicEntryID, const GUID &store_guid, void *base, ULONG *eid_size, ENTRYID **eid);
extern BOOL CompareMDBProvider(const BYTE *guid, const GUID *kopano_guid);

#endif
