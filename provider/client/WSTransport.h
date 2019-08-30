/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef WSTRANSPORT_H
#define WSTRANSPORT_H

#include <mapi.h>
#include <mapispi.h>
#include <map>
#include <mutex>
#include "kcore.hpp"
#include "ECMAPIProp.h"
#include <kopano/kcodes.h>
#include <kopano/Util.h>
#include <kopano/zcdefs.h>
#include "WSStoreTableView.h"
#include "WSMAPIFolderOps.h"
#include "WSMAPIPropStorage.h"
#include "ECParentStorage.h"
#include "ECABContainer.h"
#include "ics_client.hpp"
#include <ECCache.h>

namespace KC {
class utf8string;
}

class KCmdProxy;
class WSMessageStreamExporter;
class WSMessageStreamImporter;

typedef HRESULT (*SESSIONRELOADCALLBACK)(void *parm, KC::ECSESSIONID new_id);
typedef std::map<ULONG, std::pair<void *, SESSIONRELOADCALLBACK> > SESSIONRELOADLIST;

class ECsResolveResult final : public KC::ECsCacheEntry {
public:
	HRESULT	hr;
	std::string serverPath;
	bool isPeer;
};
typedef std::map<std::string, ECsResolveResult> ECMapResolveResults;

// Array offsets for Receive folder table
enum
{
    RFT_ROWID,
    RFT_INST_KEY,
    RFT_ENTRYID,
    RFT_RECORD_KEY,
    RFT_MSG_CLASS,
    NUM_RFT_PROPS
};

class ECABLogon;

class WSTransport KC_FINAL_OPG : public KC::ECUnknown, public WSSoap {
protected:
	WSTransport();
	virtual ~WSTransport();

public:
	static HRESULT Create(WSTransport **);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	HRESULT HrLogon(const struct sGlobalProfileProps &);
	HRESULT HrReLogon();
	HRESULT HrClone(WSTransport **out);
	HRESULT HrLogOff();
	HRESULT logoff_nd();
	HRESULT HrSetRecvTimeout(unsigned int seconds);
	HRESULT CreateAndLogonAlternate(const char *server, WSTransport **out) const;
	HRESULT CloneAndRelogon(WSTransport **out) const;
	HRESULT HrGetStore(unsigned int meid_size, const ENTRYID *master_eid, unsigned int *seid_size, ENTRYID **store_eid, unsigned int *reid_size, ENTRYID **root_eid, std::string *redir_srv = nullptr);
	HRESULT HrGetStoreName(unsigned int seid_size, const ENTRYID *store_eid, unsigned int flags, TCHAR **store_name);
	HRESULT HrGetStoreType(unsigned int seid_size, const ENTRYID *store_eid, unsigned int *store_type);
	HRESULT HrGetPublicStore(unsigned int flags, unsigned int *store_siz, ENTRYID **store_eid, std::string *redir_srv = nullptr);

	// Check item exist with flags
	HRESULT HrCheckExistObject(unsigned int eid_size, const ENTRYID *eid, unsigned int flags);

	// Interface to get/set properties
	HRESULT HrOpenPropStorage(unsigned int parent_eid_size, const ENTRYID *parent, unsigned int eid_size, const ENTRYID *eid, unsigned int flags, IECPropStorage **);
	HRESULT HrOpenParentStorage(ECGenericProp *parent, unsigned int unique_id, unsigned int obj_id, IECPropStorage *srv_storage, IECPropStorage **prop_storage);
	HRESULT HrOpenABPropStorage(unsigned int eid_size, const ENTRYID *eid, IECPropStorage **);

	// Interface for folder operations (create/delete)
	HRESULT HrOpenFolderOps(unsigned int eid_size, const ENTRYID *eid, WSMAPIFolderOps **);
	HRESULT HrExportMessageChangesAsStream(unsigned int flags, unsigned int proptag, const ICSCHANGE *changes, unsigned int start, unsigned int nchanges, const SPropTagArray *props, WSMessageStreamExporter **);
	HRESULT HrGetMessageStreamImporter(unsigned int flags, unsigned int sync_id, unsigned int eid_size, const ENTRYID *eid, unsigned int feid_size, const ENTRYID *folder_eid, bool newmsg, const SPropValue *conflict, WSMessageStreamImporter **);

	// Interface for table operations
	HRESULT HrOpenTableOps(unsigned int type, unsigned int flags, unsigned int eid_size, const ENTRYID *eid, ECMsgStore *, WSTableView **);
	HRESULT HrOpenABTableOps(unsigned int type, unsigned int flags, unsigned int eid_size, const ENTRYID *eid, ECABLogon *, WSTableView **);
	HRESULT HrOpenMailBoxTableOps(unsigned int flags, ECMsgStore *, WSTableView **tblops);

	//Interface for outgoigqueue
	HRESULT HrOpenTableOutGoingQueueOps(unsigned int seid_size, const ENTRYID *store_eid, ECMsgStore *, WSTableOutGoingQueue **);

	// Delete objects
	HRESULT HrDeleteObjects(unsigned int flags, const ENTRYLIST *msglist, unsigned int sync_id);

	// Notification
	HRESULT HrSubscribe(unsigned int key_size, BYTE *key, unsigned int conn, unsigned int evt_mask);
	HRESULT HrSubscribe(unsigned int sync_id, unsigned int change_id, unsigned int conn, unsigned int evt_mask);
	HRESULT HrSubscribeMulti(const ECLISTSYNCADVISE &, unsigned int evt_mask);
	HRESULT HrUnSubscribe(unsigned int conn);
	HRESULT HrUnSubscribeMulti(const ECLISTCONNECTION &);
	HRESULT HrNotify(const NOTIFICATION *);

	// Named properties
	HRESULT HrGetIDsFromNames(MAPINAMEID **unresolved_names, unsigned int unres_count, unsigned int flags, unsigned int **server_ids);
	HRESULT HrGetNamesFromIDs(SPropTagArray *tags, MAPINAMEID ***names, unsigned int *resolved);

	// ReceiveFolder
	HRESULT HrGetReceiveFolder(unsigned int store_eid_size, const ENTRYID *store_eid, const KC::utf8string &cls, unsigned int *eid_size, ENTRYID **folder_eid, KC::utf8string *exp_class);
	HRESULT HrSetReceiveFolder(unsigned int store_eid_size, const ENTRYID *store_eid, const KC::utf8string &cls, unsigned int eid_size, const ENTRYID *folder_eid);
	HRESULT HrGetReceiveFolderTable(unsigned int flags, unsigned int store_eid_size, const ENTRYID *store_eid, SRowSet **);

	// Read / Unread
	HRESULT HrSetReadFlag(unsigned int eid_size, const ENTRYID *eid, unsigned int flags, unsigned int sync_id);

	// Add message into the Outgoing Queue
	HRESULT HrSubmitMessage(unsigned int eid_size, const ENTRYID *msg_eid, unsigned int flags);

	// Outgoing Queue Finished message
	HRESULT HrFinishedMessage(unsigned int eid_size, const ENTRYID *, unsigned int flags);
	HRESULT HrAbortSubmit(unsigned int eid_size, const ENTRYID *);

	// Get user information
	HRESULT HrResolveUserStore(const KC::utf8string &username, unsigned int flags, unsigned int *user_id, unsigned int *eid_size, ENTRYID **store_eid, std::string *redir_srv = nullptr);
	HRESULT HrResolveTypedStore(const KC::utf8string &username, unsigned int store_type, unsigned int *eid_size, ENTRYID **store_eid);

	// IECServiceAdmin functions
	HRESULT HrCreateUser(KC::ECUSER *, unsigned int flags, unsigned int *eid_size, ENTRYID **user_eid);
	HRESULT HrDeleteUser(unsigned int eid_size, const ENTRYID *user_eid);
	HRESULT HrSetUser(KC::ECUSER *, unsigned int flags);
	HRESULT HrGetUser(unsigned int eid_size, const ENTRYID *user_eid, unsigned int flags, KC::ECUSER **);
	HRESULT HrCreateStore(unsigned int store_type, unsigned int user_size, const ENTRYID *user_eid, unsigned int store_size, const ENTRYID *store_eid, unsigned int root_size, const ENTRYID *root_eid, unsigned int flags);
	HRESULT HrHookStore(unsigned int store_type, unsigned int user_size, const ENTRYID *user_eid, const GUID *, unsigned int sync_id);
	HRESULT HrUnhookStore(unsigned int store_type, unsigned int user_size, const ENTRYID *user_eid, unsigned int sync_id);
	HRESULT HrRemoveStore(const GUID *, unsigned int sync_id);
	HRESULT HrGetUserList(unsigned int eid_size, const ENTRYID *comp_eid, unsigned int flags, unsigned int *nusers, KC::ECUSER **);
	HRESULT HrResolveUserName(const TCHAR *name, unsigned int flags, unsigned int *eid_size, ENTRYID **user_id);

	HRESULT HrGetSendAsList(unsigned int ueid_size, const ENTRYID *user_eid, unsigned int flags, unsigned int *nsenders, KC::ECUSER **senders);
	HRESULT HrAddSendAsUser(unsigned int ueid_size, const ENTRYID *user_eid, unsigned int seid_size, const ENTRYID *sender_eid);
	HRESULT HrDelSendAsUser(unsigned int ueid_size, const ENTRYID *user_eid, unsigned int seid_size, const ENTRYID *sender_eid);

	// Quota
	HRESULT GetQuota(unsigned int ueid_size, const ENTRYID *user_eid, bool get_dfl, KC::ECQUOTA **);
	HRESULT SetQuota(unsigned int ueid_size, const ENTRYID *user_eid, KC::ECQUOTA *);
	HRESULT AddQuotaRecipient(unsigned int ceid_size, const ENTRYID *com_eid, unsigned int reid_size, const ENTRYID *recip_eid, unsigned int type);
	HRESULT DeleteQuotaRecipient(unsigned int ceid_size, const ENTRYID *com_eid, unsigned int reid_size, const ENTRYID *recip_eid, unsigned int type);
	HRESULT GetQuotaRecipients(unsigned int ueid_size, const ENTRYID *user_eid, unsigned int flags, unsigned int *nusers, KC::ECUSER **);
	HRESULT GetQuotaStatus(unsigned int ueid_size, const ENTRYID *user_eid, KC::ECQUOTASTATUS **);

	HRESULT HrPurgeSoftDelete(unsigned int days);
	HRESULT HrPurgeCache(unsigned int flags);
	HRESULT HrPurgeDeferredUpdates(unsigned int *remain);

	// MultiServer
	HRESULT HrResolvePseudoUrl(const char *url, char **path, bool *is_peer);
	HRESULT HrGetServerDetails(KC::ECSVRNAMELIST *, unsigned int flags, KC::ECSERVERLIST **out);

	// IECServiceAdmin group functions
	HRESULT HrResolveGroupName(const TCHAR *name, unsigned int flags, unsigned int *eid_size, ENTRYID **grp_eid);
	HRESULT HrCreateGroup(KC::ECGROUP *, unsigned int flags, unsigned int *eid_size, ENTRYID **grp_eid);
	HRESULT HrSetGroup(KC::ECGROUP *, unsigned int flags);
	HRESULT HrGetGroup(unsigned int grp_size, const ENTRYID *grp_eid, unsigned int flags, KC::ECGROUP **);
	HRESULT HrDeleteGroup(unsigned int eid_size, const ENTRYID *grp_eid);
	HRESULT HrGetGroupList(unsigned int eid_size, const ENTRYID *comp_eid, unsigned int flags, unsigned int *ngrp, KC::ECGROUP **);

	// IECServiceAdmin Group and user functions
	HRESULT HrDeleteGroupUser(unsigned int geid_size, const ENTRYID *group_eid, unsigned int ueid_size, const ENTRYID *user_eid);
	HRESULT HrAddGroupUser(unsigned int geid_size, const ENTRYID *group_eid, unsigned int ueid_size, const ENTRYID *user_eid);
	HRESULT HrGetUserListOfGroup(unsigned int geid_size, const ENTRYID *group_eid, unsigned int flags, unsigned int *nusers, KC::ECUSER **);
	HRESULT HrGetGroupListOfUser(unsigned int ueid_size, const ENTRYID *user_eid, unsigned int flags, unsigned int *ngroups, KC::ECGROUP **);

	// IECServiceAdmin company functions
	HRESULT HrCreateCompany(KC::ECCOMPANY *, unsigned int flags, unsigned int *eid_size, ENTRYID **comp_eid);
	HRESULT HrDeleteCompany(unsigned int ceid_size, const ENTRYID *comp_eid);
	HRESULT HrSetCompany(KC::ECCOMPANY *, unsigned int flags);
	HRESULT HrGetCompany(unsigned int cmp_size, const ENTRYID *cmp_eid, unsigned int flags, KC::ECCOMPANY **);
	HRESULT HrResolveCompanyName(const TCHAR *name, unsigned int flags, unsigned int *eid_size, ENTRYID **comp_eid);
	HRESULT HrGetCompanyList(unsigned int flags, unsigned int *ncomp, KC::ECCOMPANY **companies);
	HRESULT HrAddCompanyToRemoteViewList(unsigned int sc_size, const ENTRYID *scom, unsigned int ceid_size, const ENTRYID *com_eid);
	HRESULT HrDelCompanyFromRemoteViewList(unsigned int sc_size, const ENTRYID *scom, unsigned int ceid_size, const ENTRYID *com_eid);
	HRESULT HrGetRemoteViewList(unsigned int ceid_size, const ENTRYID *com_eid, unsigned int flags, unsigned int *ncom, KC::ECCOMPANY **);
	HRESULT HrAddUserToRemoteAdminList(unsigned int ueid_size, const ENTRYID *user_eid, unsigned int ceid_size, const ENTRYID *com_eid);
	HRESULT HrDelUserFromRemoteAdminList(unsigned int ueid_size, const ENTRYID *user_eid, unsigned int ceid_size, const ENTRYID *com_eid);
	HRESULT HrGetRemoteAdminList(unsigned int ceid_size, const ENTRYID *com_eid, unsigned int flags, unsigned int *nusers, KC::ECUSER **);

	// IECServiceAdmin company and user functions
	// Get the object rights
	HRESULT HrGetPermissionRules(int type, unsigned int eid_size, const ENTRYID *, unsigned int *nperm, KC::ECPERMISSION **);
	// Set the object rights
	HRESULT HrSetPermissionRules(unsigned int eid_size, const ENTRYID *eid, unsigned int nperm, const KC::ECPERMISSION *);
	// Get owner information
	HRESULT HrGetOwner(unsigned int eid_size, const ENTRYID *, unsigned int *ouid_size, ENTRYID **owner_eid);
	//Addressbook function
	HRESULT HrResolveNames(const SPropTagArray *tagarr, unsigned int flags, ADRLIST *, FlagList *);
	HRESULT HrSyncUsers(unsigned int ceid_size, const ENTRYID *com_eid);

	// Incremental Change Synchronization
	HRESULT HrGetChanges(const std::string &sourcekey, unsigned int sync_id, unsigned int change_id, unsigned int sync_type, unsigned int flags, const SRestriction *, unsigned int *max_change, unsigned int *nchanges, ICSCHANGE **);
	HRESULT HrSetSyncStatus(const std::string &sourcekey, unsigned int sync_id, unsigned int change_id, unsigned int sync_type, unsigned int flags, unsigned int *syncid_out);
	HRESULT HrEntryIDFromSourceKey(unsigned int seid_size, const ENTRYID *store, unsigned int fsk_size, BYTE *folder_sk, unsigned int msk_size, BYTE *msg_sk, unsigned int *eid_size, ENTRYID **eid);
	HRESULT HrGetSyncStates(const ECLISTSYNCID &, ECLISTSYNCSTATE *);

	const char *GetServerName() const;

	/* statistics tables (system, threads, users), ulTableType is proto.h TABLETYPE_STATS_... */
	/* userstores table TABLETYPE_USERSTORE */
	HRESULT HrOpenMiscTable(unsigned int table_type, unsigned int flags, unsigned int eid_size, const ENTRYID *eid, ECMsgStore *, WSTableView **);

	/* Message locking */
	HRESULT HrSetLockState(unsigned int eid_size, const ENTRYID *, bool locked);

	/* expose capabilities */
	HRESULT HrCheckCapabilityFlags(unsigned int flags, BOOL *result);

	/* Test protocol */
	HRESULT HrTestPerform(const char *cmd, unsigned int argc, char **args);
	HRESULT HrTestSet(const char *name, const char *value);
	HRESULT HrTestGet(const char *name, char **value);

	/* Return Session information */
	HRESULT HrGetSessionId(KC::ECSESSIONID *, KC::ECSESSIONGROUPID *);

	/* Get profile properties (connect info) */
	const sGlobalProfileProps &GetProfileProps() const;

	/* Get the server GUID obtained at logon */
	HRESULT GetServerGUID(GUID *) const;

	/* These are called by other WS* classes to register themselves for session changes */
	HRESULT AddSessionReloadCallback(void *param, SESSIONRELOADCALLBACK, unsigned int *id);
	HRESULT RemoveSessionReloadCallback(unsigned int id);

	/* notifications */
	HRESULT HrGetNotify(struct notificationArray **out);
	HRESULT HrCancelIO();

	HRESULT HrResetFolderCount(unsigned int eid_size, const ENTRYID *eid, unsigned int *nupdates);

	std::string m_server_version;

private:
	HRESULT HrLogon2(const struct sGlobalProfileProps &);
	// Returns name of calling application (eg 'program.exe' or 'httpd')
	std::string GetAppName();

protected:
	KC::ECSESSIONID m_ecSessionId = 0;
	KC::ECSESSIONGROUPID m_ecSessionGroupId = 0;
	SESSIONRELOADLIST m_mapSessionReload;
	std::recursive_mutex m_mutexSessionReload;
	unsigned int m_ulReloadId = 1;
	unsigned int m_ulServerCapabilities = 0;
	sGlobalProfileProps m_sProfileProps;
	std::string		m_strAppName;
	GUID			m_sServerGuid;

private:
	std::recursive_mutex m_ResolveResultCacheMutex;
	KC::ECCache<ECMapResolveResults> m_ResolveResultCache;
	bool m_has_session;

friend class WSMessageStreamExporter;
friend class WSMessageStreamImporter;
	ALLOC_WRAP_FRIEND;
};

#endif // WSTRANSPORT_H
