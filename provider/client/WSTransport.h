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

class WSTransport final : public KC::ECUnknown, public WSSoap {
protected:
	WSTransport(ULONG ulUIFlags);
	virtual ~WSTransport();

public:
	static HRESULT Create(ULONG ulUIFlags, WSTransport **lppTransport);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT HrLogon2(const struct sGlobalProfileProps &);
	virtual HRESULT HrLogon(const struct sGlobalProfileProps &);
	virtual HRESULT HrReLogon();
	virtual HRESULT HrClone(WSTransport **lppTransport);
	virtual HRESULT HrLogOff();
	HRESULT logoff_nd(void);
	virtual HRESULT HrSetRecvTimeout(unsigned int ulSeconds);
	virtual HRESULT CreateAndLogonAlternate(LPCSTR szServer, WSTransport **lppTransport) const;
	virtual HRESULT CloneAndRelogon(WSTransport **lppTransport) const;
	virtual HRESULT HrGetStore(ULONG meid_size, const ENTRYID *master_eid, ULONG *seid_size, ENTRYID **store_eid, ULONG *reid_size, ENTRYID **root_eid, std::string *redir_srv = nullptr);
	virtual HRESULT HrGetStoreName(ULONG seid_size, const ENTRYID *store_eid, ULONG flags, TCHAR **store_name);
	virtual HRESULT HrGetStoreType(ULONG seid_size, const ENTRYID *store_eid, ULONG *store_type);
	virtual HRESULT HrGetPublicStore(ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID, std::string *lpstrRedirServer = NULL);

	// Check item exist with flags
	virtual HRESULT HrCheckExistObject(ULONG eid_size, const ENTRYID *eid, ULONG flags);

	// Interface to get/set properties
	virtual HRESULT HrOpenPropStorage(ULONG parent_eid_size, const ENTRYID *parent, ULONG eid_size, const ENTRYID *eid, ULONG flags, IECPropStorage **);
	virtual HRESULT HrOpenParentStorage(ECGenericProp *lpParentObject, ULONG ulUniqueId, ULONG ulObjId, IECPropStorage *lpServerStorage, IECPropStorage **lppPropStorage);
	virtual HRESULT HrOpenABPropStorage(ULONG eid_size, const ENTRYID *eid, IECPropStorage **);

	// Interface for folder operations (create/delete)
	virtual HRESULT HrOpenFolderOps(ULONG eid_size, const ENTRYID *eid, WSMAPIFolderOps **);
	virtual HRESULT HrExportMessageChangesAsStream(ULONG ulFlags, ULONG ulPropTag, const ICSCHANGE *lpChanges, ULONG ulStart, ULONG ulChanges, const SPropTagArray *lpsProps, WSMessageStreamExporter **lppsStreamExporter);
	virtual HRESULT HrGetMessageStreamImporter(ULONG flags, ULONG sync_id, ULONG eid_size, const ENTRYID *eid, ULONG feid_size, const ENTRYID *folder_eid, bool newmsg, const SPropValue *conflict, WSMessageStreamImporter **);

	// Interface for table operations
	virtual HRESULT HrOpenTableOps(ULONG type, ULONG flags, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTableView **);
	virtual HRESULT HrOpenABTableOps(ULONG type, ULONG flags, ULONG eid_size, const ENTRYID *eid, ECABLogon *, WSTableView **);
	virtual HRESULT HrOpenMailBoxTableOps(ULONG ulFlags, ECMsgStore *lpMsgStore, WSTableView **lppTableOps);

	//Interface for outgoigqueue
	virtual HRESULT HrOpenTableOutGoingQueueOps(ULONG seid_size, const ENTRYID *store_eid, ECMsgStore *, WSTableOutGoingQueue **);

	// Delete objects
	virtual HRESULT HrDeleteObjects(ULONG flags, const ENTRYLIST *msglist, ULONG sync_id);

	// Notification
	virtual HRESULT HrSubscribe(ULONG cbKey, LPBYTE lpKey, ULONG ulConnection, ULONG ulEventMask);
	virtual HRESULT HrSubscribe(ULONG ulSyncId, ULONG ulChangeId, ULONG ulConnection, ULONG ulEventMask);
	virtual HRESULT HrSubscribeMulti(const ECLISTSYNCADVISE &lstSyncAdvises, ULONG ulEventMask);
	virtual HRESULT HrUnSubscribe(ULONG ulConnection);
	virtual HRESULT HrUnSubscribeMulti(const ECLISTCONNECTION &lstConnections);
	virtual	HRESULT HrNotify(const NOTIFICATION *);

	// Named properties
	virtual HRESULT HrGetIDsFromNames(LPMAPINAMEID *lppPropNamesUnresolved, ULONG cUnresolved, ULONG ulFlags, ULONG **lpServerIDs);
	virtual HRESULT HrGetNamesFromIDs(SPropTagArray *tags, MAPINAMEID ***names, ULONG *resolved);

	// ReceiveFolder
	virtual HRESULT HrGetReceiveFolder(ULONG store_eid_size, const ENTRYID *store_eid, const KC::utf8string &cls, ULONG *eid_size, ENTRYID **folder_eid, KC::utf8string *exp_class);
	virtual HRESULT HrSetReceiveFolder(ULONG store_eid_size, const ENTRYID *store_eid, const KC::utf8string &cls, ULONG eid_size, const ENTRYID *folder_eid);
	virtual HRESULT HrGetReceiveFolderTable(ULONG flags, ULONG store_eid_size, const ENTRYID *store_eid, SRowSet **);

	// Read / Unread
	virtual HRESULT HrSetReadFlag(ULONG eid_size, const ENTRYID *eid, ULONG flags, ULONG sync_id);

	// Add message into the Outgoing Queue
	virtual HRESULT HrSubmitMessage(ULONG eid_size, const ENTRYID *msg_eid, ULONG flags);

	// Outgoing Queue Finished message
	virtual HRESULT HrFinishedMessage(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG ulFlags);
	virtual HRESULT HrAbortSubmit(ULONG eid_size, const ENTRYID *);

	// Get user information
	virtual HRESULT HrResolveUserStore(const KC::utf8string &username, ULONG flags, ULONG *user_id, ULONG *eid_size, ENTRYID **store_eid, std::string *redir_srv = nullptr);
	virtual HRESULT HrResolveTypedStore(const KC::utf8string &username, ULONG store_type, ULONG *eid_size, ENTRYID **store_eid);

	// IECServiceAdmin functions
	virtual HRESULT HrCreateUser(KC::ECUSER *, ULONG flags, ULONG *eid_size, ENTRYID **user_eid);
	virtual HRESULT HrDeleteUser(ULONG eid_size, const ENTRYID *user_eid);
	virtual HRESULT HrSetUser(KC::ECUSER *, ULONG flags);
	virtual HRESULT HrGetUser(ULONG eid_size, const ENTRYID *user_eid, ULONG flags, KC::ECUSER **);
	virtual HRESULT HrCreateStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, ULONG store_size, const ENTRYID *store_eid, ULONG root_size, const ENTRYID *root_eid, ULONG flags);
	virtual HRESULT HrHookStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, const GUID *, ULONG sync_id);
	virtual HRESULT HrUnhookStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, ULONG sync_id);
	virtual HRESULT HrRemoveStore(const GUID *, ULONG sync_id);
	virtual HRESULT HrGetUserList(ULONG eid_size, const ENTRYID *comp_eid, ULONG flags, ULONG *nusers, KC::ECUSER **);
	virtual HRESULT HrResolveUserName(LPCTSTR lpszUserName, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId);

	virtual HRESULT HrGetSendAsList(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *nsenders, KC::ECUSER **senders);
	virtual HRESULT HrAddSendAsUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG seid_size, const ENTRYID *sender_eid);
	virtual HRESULT HrDelSendAsUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG seid_size, const ENTRYID *sender_eid);

	// Quota
	virtual HRESULT GetQuota(ULONG ueid_size, const ENTRYID *user_eid, bool get_dfl, KC::ECQUOTA **);
	virtual HRESULT SetQuota(ULONG ueid_size, const ENTRYID *user_eid, KC::ECQUOTA *);
	virtual HRESULT AddQuotaRecipient(ULONG ceid_size, const ENTRYID *com_eid, ULONG reid_size, const ENTRYID *recip_eid, ULONG type);
	virtual HRESULT DeleteQuotaRecipient(ULONG ceid_size, const ENTRYID *com_eid, ULONG reid_size, const ENTRYID *recip_eid, ULONG type);
	virtual HRESULT GetQuotaRecipients(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *nusers, KC::ECUSER **);
	virtual HRESULT GetQuotaStatus(ULONG ueid_size, const ENTRYID *user_eid, KC::ECQUOTASTATUS **);

	virtual HRESULT HrPurgeSoftDelete(ULONG ulDays);
	virtual HRESULT HrPurgeCache(ULONG ulFlags);
	virtual HRESULT HrPurgeDeferredUpdates(ULONG *lpulRemaining);

	// MultiServer
	virtual HRESULT HrResolvePseudoUrl(const char *lpszPseudoUrl, char **lppszServerPath, bool *lpbIsPeer);
	virtual HRESULT HrGetServerDetails(KC::ECSVRNAMELIST *, ULONG flags, KC::ECSERVERLIST **out);

	// IECServiceAdmin group functions
	virtual HRESULT HrResolveGroupName(LPCTSTR lpszGroupName, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId);
	virtual HRESULT HrCreateGroup(KC::ECGROUP *, ULONG flags, ULONG *eid_size, ENTRYID **grp_eid);
	virtual HRESULT HrSetGroup(KC::ECGROUP *, ULONG flags);
	virtual HRESULT HrGetGroup(ULONG grp_size, const ENTRYID *grp_eid, ULONG flags, KC::ECGROUP **);
	virtual HRESULT HrDeleteGroup(ULONG eid_size, const ENTRYID *grp_eid);
	virtual HRESULT HrGetGroupList(ULONG eid_size, const ENTRYID *comp_eid, ULONG flags, ULONG *ngrp, KC::ECGROUP **);

	// IECServiceAdmin Group and user functions
	virtual HRESULT HrDeleteGroupUser(ULONG geid_size, const ENTRYID *group_eid, ULONG ueid_size, const ENTRYID *user_eid);
	virtual HRESULT HrAddGroupUser(ULONG geid_size, const ENTRYID *group_eid, ULONG ueid_size, const ENTRYID *user_eid);
	virtual HRESULT HrGetUserListOfGroup(ULONG geid_size, const ENTRYID *group_eid, ULONG flags, ULONG *nusers, KC::ECUSER **);
	virtual HRESULT HrGetGroupListOfUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *ngroups, KC::ECGROUP **);

	// IECServiceAdmin company functions
	virtual HRESULT HrCreateCompany(KC::ECCOMPANY *, ULONG flags, ULONG *eid_size, ENTRYID **comp_eid);
	virtual HRESULT HrDeleteCompany(ULONG ceid_size, const ENTRYID *comp_eid);
	virtual HRESULT HrSetCompany(KC::ECCOMPANY *, ULONG flags);
	virtual HRESULT HrGetCompany(ULONG cmp_size, const ENTRYID *cmp_eid, ULONG flags, KC::ECCOMPANY **);
	virtual HRESULT HrResolveCompanyName(LPCTSTR lpszCompanyName, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId);
	virtual HRESULT HrGetCompanyList(ULONG flags, ULONG *ncomp, KC::ECCOMPANY **companies);
	virtual HRESULT HrAddCompanyToRemoteViewList(ULONG sc_size, const ENTRYID *scom, ULONG ceid_size, const ENTRYID *com_eid);
	virtual HRESULT HrDelCompanyFromRemoteViewList(ULONG sc_size, const ENTRYID *scom, ULONG ceid_size, const ENTRYID *com_eid);
	virtual HRESULT HrGetRemoteViewList(ULONG ceid_size, const ENTRYID *com_eid, ULONG flags, ULONG *ncom, KC::ECCOMPANY **);
	virtual HRESULT HrAddUserToRemoteAdminList(ULONG ueid_size, const ENTRYID *user_eid, ULONG ceid_size, const ENTRYID *com_eid);
	virtual HRESULT HrDelUserFromRemoteAdminList(ULONG ueid_size, const ENTRYID *user_eid, ULONG ceid_size, const ENTRYID *com_eid);
	virtual HRESULT HrGetRemoteAdminList(ULONG ceid_size, const ENTRYID *com_eid, ULONG flags, ULONG *nusers, KC::ECUSER **);

	// IECServiceAdmin company and user functions
	// Get the object rights
	virtual HRESULT HrGetPermissionRules(int type, ULONG eid_size, const ENTRYID *, ULONG *nperm, KC::ECPERMISSION **);
	// Set the object rights
	virtual HRESULT HrSetPermissionRules(ULONG eid_size, const ENTRYID *eid, ULONG nperm, const KC::ECPERMISSION *);
	// Get owner information
	virtual HRESULT HrGetOwner(ULONG eid_size, const ENTRYID *, ULONG *ouid_size, ENTRYID **owner_eid);
	//Addressbook function
	virtual HRESULT HrResolveNames(const SPropTagArray *lpPropTagArray, ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList);
	virtual HRESULT HrSyncUsers(ULONG ceid_size, const ENTRYID *com_eid);

	// Incremental Change Synchronization
	virtual HRESULT HrGetChanges(const std::string &sourcekey, ULONG sync_id, ULONG change_id, ULONG sync_type, ULONG flags, const SRestriction *, ULONG *max_change, ULONG *nchanges, ICSCHANGE **);
	virtual HRESULT HrSetSyncStatus(const std::string& sourcekey, ULONG ulSyncId, ULONG ulChangeId, ULONG ulSyncType, ULONG ulFlags, ULONG* lpulSyncId);
	virtual HRESULT HrEntryIDFromSourceKey(ULONG seid_size, const ENTRYID *store, ULONG fsk_size, BYTE *folder_sk, ULONG msk_size, BYTE *msg_sk, ULONG *eid_size, ENTRYID **eid);
	virtual HRESULT HrGetSyncStates(const ECLISTSYNCID &lstSyncId, ECLISTSYNCSTATE *lplstSyncState);

	virtual const char* GetServerName();

	/* statistics tables (system, threads, users), ulTableType is proto.h TABLETYPE_STATS_... */
	/* userstores table TABLETYPE_USERSTORE */
	virtual HRESULT HrOpenMiscTable(ULONG table_type, ULONG flags, ULONG eid_size, const ENTRYID *eid, ECMsgStore *, WSTableView **);

	/* Message locking */
	virtual HRESULT HrSetLockState(ULONG eid_size, const ENTRYID *, bool locked);

	/* expose capabilities */
	virtual HRESULT HrCheckCapabilityFlags(ULONG ulFlags, BOOL *lpbResult);

	/* Test protocol */
	virtual HRESULT HrTestPerform(const char *cmd, unsigned int argc, char **args);
	virtual HRESULT HrTestSet(const char *szName, const char *szValue);
	virtual HRESULT HrTestGet(const char *szName, char **szValue);

	/* Return Session information */
	virtual HRESULT HrGetSessionId(KC::ECSESSIONID *, KC::ECSESSIONGROUPID *);

	/* Get profile properties (connect info) */
	virtual sGlobalProfileProps GetProfileProps();

	/* Get the server GUID obtained at logon */
	virtual HRESULT GetServerGUID(LPGUID lpsServerGuid);

	/* These are called by other WS* classes to register themselves for session changes */
	virtual HRESULT AddSessionReloadCallback(void *lpParam, SESSIONRELOADCALLBACK callback, ULONG * lpulId);
	virtual HRESULT RemoveSessionReloadCallback(ULONG ulId);

	/* notifications */
	virtual HRESULT HrGetNotify(struct notificationArray **lppsArrayNotifications);
	virtual HRESULT HrCancelIO();

	virtual HRESULT HrResetFolderCount(ULONG eid_size, const ENTRYID *eid, ULONG *nupdates);

	std::string m_server_version;

private:
	static SOAP_SOCKET RefuseConnect(struct soap*, const char*, const char*, int);
	static KC::ECRESULT KCOIDCLogon(KCmdProxy *, const char *server, const KC::utf8string &user, const KC::utf8string &imp_user, const KC::utf8string &password, unsigned int caps, KC::ECSESSIONGROUPID, const char *app_name, KC::ECSESSIONID *, unsigned int *srv_caps, unsigned long long *flags, GUID *srv_guid, const std::string &cl_app_ver, const std::string &cl_app_misc);
	//TODO: Move this function to the right file
	static KC::ECRESULT TrySSOLogon(KCmdProxy *, const char *server, const KC::utf8string &user, const KC::utf8string &imp_user, unsigned int caps, KC::ECSESSIONGROUPID, const char *app_name, KC::ECSESSIONID *, unsigned int *srv_caps, unsigned long long *flags, GUID *srv_guid, const std::string &cl_app_ver, const std::string &cl_app_misc);

	// Returns name of calling application (eg 'program.exe' or 'httpd')
	std::string GetAppName();

protected:
	KC::ECSESSIONID m_ecSessionId = 0;
	KC::ECSESSIONGROUPID m_ecSessionGroupId = 0;
	SESSIONRELOADLIST m_mapSessionReload;
	std::recursive_mutex m_mutexSessionReload;
	unsigned int m_ulReloadId = 1;
	unsigned int m_ulServerCapabilities = 0;
	unsigned long long m_llFlags = 0; // license flags
	ULONG			m_ulUIFlags;	// UI flags for logon
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
