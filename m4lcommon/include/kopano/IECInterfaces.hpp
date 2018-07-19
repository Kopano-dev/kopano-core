/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef IECINTERFACES_HPP
#define IECINTERFACES_HPP 1

#include <edkmdb.h>
#include <kopano/ECDefs.h>
#include <kopano/ECGuid.h>
#include <kopano/platform.h>
#include <mapidefs.h>

namespace KC {

class IECChangeAdviseSink : public virtual IUnknown {
	public:
	virtual ULONG OnNotify(ULONG ulFLags, LPENTRYLIST lpEntryList) = 0;
};

/**
 * IECChangeAdvisor: Interface for registering change notifications on folders.
 */
class IECChangeAdvisor : public virtual IUnknown {
	public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;

	/**
	 * Configure the change change advisor based on a stream.
	 *
	 * @param[in]	lpStream
	 *					The stream containing the state of the state of the change advisor. This stream
	 *					is obtained by a call to UpdateState.
	 *					If lpStream is NULL, an empty state is assumed.
	 * @param[in]	lpGuid
	 *					Unused. Set to NULL.
	 * @param[in]	lpAdviseSink
	 *					The advise sink that will receive the change notifications.
	 * @param[in]	ulFlags
	 *					- SYNC_CATCHUP	Update the internal state, but don't perform any operations
	 *									on the server.
	 */
	virtual HRESULT Config(LPSTREAM lpStream, LPGUID lpGUID, IECChangeAdviseSink *lpAdviseSink, ULONG ulFlags) = 0;

	/**
	 * Store the current internal state in the stream pointed to by lpStream.
	 *
	 * @param[in]	lpStream
	 *					The stream in which the current state will be stored.
	 */
	virtual HRESULT UpdateState(LPSTREAM lpStream) = 0;

	/**
	 * Register one or more folders for change notifications through this change advisor.
	 *
	 * @param[in]	lpEntryList
	 *					A list of keys specifying the folders to monitor. A key is an 8 byte
	 *					block of data. The first 4 bytes specify the sync id of the folder to
	 *					monitor. The second 4 bytes apecify the change id of the folder to monitor.
	 *					Use the SSyncState structure to easily create and access this data.
	 */
	virtual HRESULT AddKeys(LPENTRYLIST lpEntryList) = 0;

	/**
	 * Unregister one or more folder for change notifications.
	 *
	 * @param[in]	lpEntryList
	 *					A list of keys specifying the folders to monitor. See AddKeys for
	 *					information about the key format.
	 */
	virtual HRESULT RemoveKeys(LPENTRYLIST lpEntryList) = 0;

	/**
	 * Check if the change advisor is monitoring the folder specified by a particular sync id.
	 *
	 * @param[in]	ulSyncId
	 *					The sync id of the folder.
	 * @return hrSuccess if the folder is being monitored.
	 */
	virtual HRESULT IsMonitoringSyncId(ULONG ulSyncId) = 0;

	/**
	 * Update the changeid for a particular syncid.
	 *
	 * This is used to update the state of the changeadvisor. This is also vital information
	 * when a reconnect is required.
	 *
	 * @param[in]	ulSyncId
	 *					The syncid for which to update the changeid.
	 * @param[in]	ulChangeId
	 *					The new changeid for the specified syncid.
	 */
	virtual HRESULT UpdateSyncState(ULONG ulSyncId, ULONG ulChangeId) = 0;
};

class IECExchangeModifyTable : public virtual IExchangeModifyTable {
	public:
	virtual HRESULT DisablePushToServer() = 0;
};

class IECImportAddressbookChanges;

class IECExportAddressbookChanges : public virtual IUnknown {
	public:
	virtual HRESULT Config(LPSTREAM lpState, ULONG ulFlags, IECImportAddressbookChanges *lpCollector) = 0;
	virtual HRESULT Synchronize(ULONG *lpulSteps, ULONG *lpulProgress) = 0;
	virtual HRESULT UpdateState(LPSTREAM lpState) = 0;
};

class ECLogger;

class IECExportChanges : public IExchangeExportChanges {
	public:
	virtual HRESULT ConfigSelective(ULONG ulPropTag, LPENTRYLIST lpEntries, LPENTRYLIST lpParents, ULONG ulFlags, LPUNKNOWN lpCollector, LPSPropTagArray lpIncludeProps, LPSPropTagArray lpExcludeProps, ULONG ulBufferSize) = 0;
	virtual HRESULT GetChangeCount(ULONG *lpcChanges) = 0;
	virtual HRESULT SetMessageInterface(REFIID refiid) = 0;
	virtual HRESULT SetLogger(ECLogger *lpLogger) = 0;
};

class IECImportAddressbookChanges : public virtual IUnknown {
	public:
	virtual HRESULT GetLastError(HRESULT hr, ULONG ulFlags, LPMAPIERROR *lppMAPIError) = 0;
	virtual HRESULT Config(LPSTREAM lpState, ULONG ulFlags) = 0;
	virtual HRESULT UpdateState(LPSTREAM lpState) = 0;
	virtual HRESULT ImportABChange(ULONG type, ULONG cbObjId, LPENTRYID lpObjId) = 0;
	virtual HRESULT ImportABDeletion(ULONG type, ULONG cbObjId, LPENTRYID lpObjId) = 0;
};

class IECImportContentsChanges : public IExchangeImportContentsChanges {
	public:
	virtual HRESULT ConfigForConversionStream(LPSTREAM lpStream, ULONG ulFlags, ULONG cValuesConversion, LPSPropValue lpPropArrayConversion) = 0;
	virtual HRESULT ImportMessageChangeAsAStream(ULONG cpvalChanges, LPSPropValue ppvalChanges, ULONG ulFlags, LPSTREAM *lppstream) = 0;
};

class IECImportHierarchyChanges : public IExchangeImportHierarchyChanges {
	public:
	virtual HRESULT ImportFolderChangeEx(ULONG cValues, LPSPropValue lpPropArray, BOOL fNew) = 0;
};

class IECMultiStoreTable : public virtual IUnknown {
	public:
	/* ulFlags is currently unused */
	virtual HRESULT OpenMultiStoreTable(const ENTRYLIST *msglist, ULONG flags, IMAPITable **) = 0;
};

class IECSecSvcAdm_base : public virtual IUnknown {
	public:
	virtual HRESULT GetUserList(ULONG cbCompanyId, const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers) = 0;
	virtual HRESULT GetGroupList(ULONG cbCompanyId, const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups) = 0;
	virtual HRESULT GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies) = 0;
};

class IECSecurity : public virtual IECSecSvcAdm_base {
	public:
	virtual HRESULT GetOwner(ULONG *lpcbOwner, LPENTRYID *lppOwner) = 0;
	virtual HRESULT GetPermissionRules(int ulType, ULONG* lpcPermissions, ECPERMISSION **lppECPermissions) = 0;
	virtual HRESULT SetPermissionRules(ULONG nperm, const ECPERMISSION *) = 0;
};

class IECServiceAdmin : public virtual IECSecSvcAdm_base {
	public:
	/* Create/Delete stores */
	virtual HRESULT CreateStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, ULONG *newstore_size, ENTRYID **newstore_eid, ULONG *root_size, ENTRYID **root_eid) = 0;
	virtual HRESULT CreateEmptyStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, ULONG flags, ULONG *newstore_size, ENTRYID **newstore_eid, ULONG *root_size, ENTRYID **root_eid) = 0;
	virtual HRESULT ResolveStore(const GUID *, ULONG *user_id, ULONG *store_size, ENTRYID **store_eid) = 0;
	virtual HRESULT HookStore(ULONG store_type, ULONG eid_size, const ENTRYID *eid, const GUID *guid) = 0;
	virtual HRESULT UnhookStore(ULONG store_type, ULONG eid_size, const ENTRYID *eid) = 0;
	virtual HRESULT RemoveStore(const GUID *) = 0;

	/* User functions */
	virtual HRESULT CreateUser(ECUSER *lpECUser, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId) = 0;
	virtual HRESULT DeleteUser(ULONG ueid_size, const ENTRYID *user_eid) = 0;
	virtual HRESULT SetUser(ECUSER *lpECUser, ULONG ulFlags) = 0;
	virtual HRESULT GetUser(ULONG eid_size, const ENTRYID *user_eid, ULONG flags, ECUSER **) = 0;
	virtual HRESULT ResolveUserName(LPCTSTR lpszUserName, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId) = 0;
	virtual HRESULT GetSendAsList(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *nsenders, ECUSER **) = 0;
	virtual HRESULT AddSendAsUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG seid_size, const ENTRYID *sender_eid) = 0;
	virtual HRESULT DelSendAsUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG seid_size, const ENTRYID *sender_eid) = 0;
	virtual HRESULT GetUserClientUpdateStatus(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ECUSERCLIENTUPDATESTATUS **) = 0;

	/* Remove all users EXCEPT the passed user */
	virtual HRESULT RemoveAllObjects(ULONG ueid_size, const ENTRYID *user_eid) = 0;

	/* Group functions */
	virtual HRESULT CreateGroup(ECGROUP *lpECGroup, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId) = 0;
	virtual HRESULT DeleteGroup(ULONG geid_size, const ENTRYID *grp_eid) = 0;
	virtual HRESULT SetGroup(ECGROUP *lpECGroup, ULONG ulFlags) = 0;
	virtual HRESULT GetGroup(ULONG grp_size, const ENTRYID *grp_eid, ULONG flags, ECGROUP **) = 0;
	virtual HRESULT ResolveGroupName(LPCTSTR lpszGroupName, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId) = 0;
	virtual HRESULT DeleteGroupUser(ULONG geid_size, const ENTRYID *grp_eid, ULONG ueid_size, const ENTRYID *user_eid) = 0;
	virtual HRESULT AddGroupUser(ULONG geid_size, const ENTRYID *grp_eid, ULONG ueid_size, const ENTRYID *user_eid) = 0;
	virtual HRESULT GetUserListOfGroup(ULONG geid_size, const ENTRYID *grp_eid, ULONG flags, ULONG *nusers, ECUSER **) = 0;
	virtual HRESULT GetGroupListOfUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *ngrps, ECGROUP **) = 0;

	/* Company functions */
	virtual HRESULT CreateCompany(ECCOMPANY *lpECCompany, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId) = 0;
	virtual HRESULT DeleteCompany(ULONG ceid_size, const ENTRYID *com_eid) = 0;
	virtual HRESULT SetCompany(ECCOMPANY *lpECCompany, ULONG ulFlags) = 0;
	virtual HRESULT GetCompany(ULONG cmp_size, const ENTRYID *cmp_eid, ULONG flags, ECCOMPANY **) = 0;
	virtual HRESULT ResolveCompanyName(LPCTSTR lpszCompanyName, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId) = 0;
	virtual HRESULT AddCompanyToRemoteViewList(ULONG sc_size, const ENTRYID *scom_eid, ULONG ceid_size, const ENTRYID *com_eid) = 0;
	virtual HRESULT DelCompanyFromRemoteViewList(ULONG sc_size, const ENTRYID *scom_eid, ULONG ceid_size, const ENTRYID *com_eid) = 0;
	virtual HRESULT GetRemoteViewList(ULONG ceid_size, const ENTRYID *com_eid, ULONG flags, ULONG *ncomps, ECCOMPANY **) = 0;
	virtual HRESULT AddUserToRemoteAdminList(ULONG ueid_size, const ENTRYID *user_eid, ULONG ceid_size, const ENTRYID *com_eid) = 0;
	virtual HRESULT DelUserFromRemoteAdminList(ULONG ueid_size, const ENTRYID *user_eid, ULONG ceid_size, const ENTRYID *com_eid) = 0;
	virtual HRESULT GetRemoteAdminList(ULONG ceid_size, const ENTRYID *com_eid, ULONG flags, ULONG *nusers, ECUSER **) = 0;
	virtual HRESULT SyncUsers(ULONG ceid_size, const ENTRYID *com_eid) = 0;

	/* Quota functions */
	virtual HRESULT GetQuota(ULONG ueid_size, const ENTRYID *user_eid, bool get_dfl, ECQUOTA **) = 0;
	virtual HRESULT SetQuota(ULONG ueid_size, const ENTRYID *user_eid, ECQUOTA *) = 0;
	virtual HRESULT AddQuotaRecipient(ULONG ceid_size, const ENTRYID *com_eid, ULONG reid_size, const ENTRYID *recip_eid, ULONG type) = 0;
	virtual HRESULT DeleteQuotaRecipient(ULONG ceid_size, const ENTRYID *com_eid, ULONG reid_size, const ENTRYID *recip_eid, ULONG type) = 0;
	virtual HRESULT GetQuotaRecipients(ULONG ceid_size, const ENTRYID *user_eid, ULONG flags, ULONG *nusers, ECUSER **) = 0;
	virtual HRESULT GetQuotaStatus(ULONG ueid_size, const ENTRYID *user_eid, ECQUOTASTATUS **) = 0;
	virtual HRESULT PurgeSoftDelete(ULONG ulDays) = 0;
	virtual HRESULT PurgeCache(ULONG ulFlags) = 0;
	virtual HRESULT OpenUserStoresTable(ULONG ulFlags, LPMAPITABLE *lppTable) = 0;
	virtual HRESULT PurgeDeferredUpdates(ULONG *lpulDeferredRemaining) = 0;

	/* Multiserver functions */
	virtual HRESULT GetServerDetails(ECSVRNAMELIST *lpServerNameList, ULONG ulFlags, ECSERVERLIST **lppsServerList) = 0;
	virtual HRESULT ResolvePseudoUrl(const char *url, char **path, bool *ispeer) = 0;

	/* Archive store function(s) */
	virtual HRESULT GetArchiveStoreEntryID(LPCTSTR lpszUserName, LPCTSTR lpszServerName, ULONG ulFlags, ULONG *lpcbStoreID, LPENTRYID *lppStoreID) = 0;
	virtual HRESULT ResetFolderCount(ULONG eid_size, const ENTRYID *eid, ULONG *nupdates) = 0;
};

class IECSingleInstance : public virtual IUnknown {
	public:
	virtual HRESULT GetSingleInstanceId(ULONG *lpcbInstanceID, LPENTRYID *lppInstanceID) = 0;
	virtual HRESULT SetSingleInstanceId(ULONG eid_size, const ENTRYID *eid) = 0;
};

// This is our special spooler interface
class IECSpooler : public virtual IUnknown {
	public:
	// Gets an IMAPITable containing all the outgoing messages on the server
	virtual HRESULT GetMasterOutgoingTable(ULONG ulFlags, IMAPITable **lppTable) = 0;

	// Removes a message from the master outgoing table
	virtual HRESULT DeleteFromMasterOutgoingTable(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG ulFlags) = 0;
};

class IECTestProtocol : public virtual IUnknown {
	public:
	virtual HRESULT TestPerform(const char *cmd, unsigned int argc, char **args) = 0;
	virtual HRESULT TestSet(const char *name, const char *value) = 0;
	virtual HRESULT TestGet(const char *name, char **value) = 0;
};

} /* namespace */

IID_OF2(KC::IECChangeAdvisor, IECChangeAdvisor)
IID_OF2(KC::IECMultiStoreTable, IECMultiStoreTable)
IID_OF2(KC::IECSecurity, IECSecurity)
IID_OF2(KC::IECServiceAdmin, IECServiceAdmin)
IID_OF2(KC::IECSingleInstance, IECSingleInstance)
IID_OF2(KC::IECSpooler, IECSpooler)

#endif /* IECINTERFACES_HPP */
