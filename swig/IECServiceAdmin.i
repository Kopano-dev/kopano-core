/* SPDX-License-Identifier: AGPL-3.0-only */
%include "typemaps.i"

%apply (ULONG cbEntryID, LPENTRYID lpEntryID) {(ULONG cbUserId, LPENTRYID lpUserId), (ULONG cbStoreId, LPENTRYID lpStoreId), (ULONG cbRootId, LPENTRYID lpRootId), (ULONG cbCompanyId, LPENTRYID lpCompanyId), (ULONG cbCompanyId, ENTRYID *lpCompanyId), (ULONG cbGroupId, LPENTRYID lpGroupId), (ULONG cbSenderId, LPENTRYID lpSenderId), (ULONG cbRecipientId, LPENTRYID lpRecipientId), (ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId)};

%apply (ULONG *, MAPIARRAY *) {(ULONG *OUTPUT, LPECUSER *OUTPUT), (ULONG *OUTPUT, LPECGROUP *OUTPUT), (ULONG *OUTPUT, LPECCOMPANY *OUTPUT)};
%apply MAPISTRUCT_W_FLAGS {LPECUSER, LPECCOMPANY, LPECGROUP};
%apply MAPISTRUCT * {LPECUSER *OUTPUT, LPECQUOTA *OUTPUT, LPECCOMPANY *OUTPUT, LPECGROUP *OUTPUT, LPECQUOTASTATUS *OUTPUT};
%apply MAPISTRUCT {LPECQUOTA};

%apply MAPILIST {LPECSVRNAMELIST};
%apply MAPILIST * {LPECSERVERLIST* OUTPUT};

%typemap(in,numinputs=0) (bool *OUTPUT) (bool bBool = NULL) {
  $1 = &bBool;
}
%typemap(argout, fragment="SWIG_From_int") bool *OUTPUT{
    %append_output(SWIG_From_int((int)*$1));
}

%typemap(argout) (LPECUSER *OUTPUT) {
    %append_output(Object_from_LPECUSER(*($1), ulFlags));
}
%typemap(argout) (ULONG *OUTPUT, LPECUSER *OUTPUT) {
    %append_output(List_from_LPECUSER(*($2),*($1), ulFlags));
}

%typemap(argout) (LPECCOMPANY *OUTPUT) {
    %append_output(Object_from_LPECCOMPANY(*($1), ulFlags));
}
%typemap(argout) (ULONG *OUTPUT, LPECCOMPANY *OUTPUT) {
    %append_output(List_from_LPECCOMPANY(*($2),*($1), ulFlags));
}

%typemap(argout) (LPECGROUP *OUTPUT) {
    %append_output(Object_from_LPECGROUP(*($1), ulFlags));
}
%typemap(argout) (ULONG *OUTPUT, LPECGROUP *OUTPUT) {
    %append_output(List_from_LPECGROUP(*($2),*($1), ulFlags));
}

#define ECSTORE_TYPE_PRIVATE      			0
#define ECSTORE_TYPE_PUBLIC               	1
#define ECSTORE_TYPE_ARCHIVE				2

class IECServiceAdmin : public virtual IUnknown {
public:
	// Create/Delete stores
	virtual HRESULT CreateStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG* lpcbStoreId, LPENTRYID* lppStoreId, ULONG* lpcbRootId, LPENTRYID *lppRootId) = 0;
	virtual HRESULT CreateEmptyStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG* lpcbStoreId_oio, LPENTRYID* lppStoreId_oio, ULONG* lpcbRootId_oio, LPENTRYID *lppRootId_oio) = 0;
	virtual HRESULT ResolveStore(LPGUID lpGuid, ULONG *lpulUserID, ULONG* OUTPUT, LPENTRYID* OUTPUT) = 0;
	virtual HRESULT HookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, LPGUID lpGuid) = 0;
	virtual HRESULT UnhookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId) = 0;
	virtual HRESULT RemoveStore(LPGUID lpGuid) = 0;

	// User functions
	virtual HRESULT CreateUser(LPECUSER lpECUser, ULONG ulFlags, ULONG *OUTPUT, LPENTRYID *OUTPUT) = 0;
	virtual HRESULT DeleteUser(ULONG cbUserId, LPENTRYID lpUserId) = 0;
	virtual HRESULT SetUser(LPECUSER lpECUser, ULONG ulFlags) = 0;
	virtual HRESULT GetUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, LPECUSER *OUTPUT /*lppECUser*/) = 0;
	virtual HRESULT ResolveUserName(LPTSTR lpszUserName, ULONG ulFlags, ULONG *OUTPUT, LPENTRYID *OUTPUT) = 0;
	virtual HRESULT GetUserList(ULONG cbCompanyId, const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *OUTPUT /*lpcUsers*/, LPECUSER *OUTPUT /*lppsUsers*/) = 0;
	virtual HRESULT GetSendAsList(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *OUTPUT /*lpcSenders*/, LPECUSER *OUTPUT /*lppSenders*/) = 0;
	virtual HRESULT AddSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId) = 0;
	virtual HRESULT DelSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId) = 0;
    virtual HRESULT RemoveAllObjects(ULONG cbUserId, LPENTRYID lpUserId) = 0;

	// Group functions
	virtual HRESULT CreateGroup(LPECGROUP lpECGroup, ULONG ulFlags, ULONG *OUTPUT, LPENTRYID *OUTPUT) = 0;
	virtual HRESULT DeleteGroup(ULONG cbGroupId, LPENTRYID lpGroupId) = 0;
	virtual HRESULT SetGroup(LPECGROUP lpECGroup, ULONG ulFlags) = 0;
	virtual HRESULT GetGroup(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG ulFlags, LPECGROUP *OUTPUT/*lppECGroup*/) = 0;
	virtual HRESULT ResolveGroupName(LPTSTR lpszGroupName, ULONG ulFlags, ULONG *OUTPUT, LPENTRYID *OUTPUT) = 0;
	virtual HRESULT GetGroupList(ULONG cbCompanyId, const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *OUTPUT /*lpcGroups*/, LPECGROUP *OUTPUT /*lppsGroups*/) = 0;

	virtual HRESULT DeleteGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId) = 0;
	virtual HRESULT AddGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId) = 0;
	virtual HRESULT GetUserListOfGroup(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG ulFlags, ULONG *OUTPUT /*lpcUsers*/, LPECUSER *OUTPUT /*lppsUsers*/) = 0;
	virtual HRESULT GetGroupListOfUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *OUTPUT /*lpcGroups*/, LPECGROUP *OUTPUT /*lppsGroups*/) = 0;

	// Company functions
	virtual HRESULT CreateCompany(LPECCOMPANY lpECCompany, ULONG ulFlags, ULONG *OUTPUT, LPENTRYID *OUTPUT) = 0;
	virtual HRESULT DeleteCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT SetCompany(LPECCOMPANY lpECCompany, ULONG ulFlags) = 0;
	virtual HRESULT GetCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, LPECCOMPANY *OUTPUT/*lppECCompany*/) = 0;
	virtual HRESULT ResolveCompanyName(LPTSTR lpszCompanyName, ULONG ulFlags, ULONG *OUTPUT, LPENTRYID *OUTPUT) = 0;
	virtual HRESULT GetCompanyList(ULONG ulFlags, ULONG *OUTPUT /*lpcCompanies*/, LPECCOMPANY *OUTPUT /*lppsCompanies*/) = 0;

	virtual HRESULT AddCompanyToRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT DelCompanyFromRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT GetRemoteViewList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *OUTPUT /*lpcCompanies*/, LPECCOMPANY *OUTPUT /*lppsCompanies*/) = 0;
	virtual HRESULT AddUserToRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT DelUserFromRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT GetRemoteAdminList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *OUTPUT /*lpcUsers*/, LPECUSER *OUTPUT /*lppsUsers*/) = 0;

	virtual HRESULT SyncUsers(ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;

	// Quota functions
	virtual HRESULT GetQuota(ULONG cbUserId, LPENTRYID lpUserId, bool bGetUserDefault, LPECQUOTA* OUTPUT) = 0;
	virtual HRESULT SetQuota(ULONG cbUserId, LPENTRYID lpUserId, LPECQUOTA lpsQuota) = 0;

	virtual HRESULT AddQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType) = 0;
	virtual HRESULT DeleteQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType) = 0;
	virtual HRESULT GetQuotaRecipients(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *OUTPUT /*lpcUsers*/, LPECUSER *OUTPUT /*lppsUsers*/) = 0;

	virtual HRESULT GetQuotaStatus(ULONG cbUserId, LPENTRYID lpUserId, LPECQUOTASTATUS* OUTPUT/*lppsQuotaStatus*/) = 0;
	
	virtual HRESULT PurgeSoftDelete(ULONG ulDays) = 0;
	virtual HRESULT PurgeCache(ULONG ulFlags) = 0;
	virtual HRESULT PurgeDeferredUpdates(ULONG *OUTPUT) = 0;
	virtual HRESULT OpenUserStoresTable(ULONG ulFlags, IMAPITable **OUTPUT /*lppTable*/) = 0;

	// Multiserver functions
	virtual HRESULT GetServerDetails(LPECSVRNAMELIST lpServerNameList, ULONG ulFlags, LPECSERVERLIST* OUTPUT/*lppsServerList*/) = 0;
	virtual HRESULT ResolvePseudoUrl(const char *url, char** OUTMAPICHAR/*lppszServerPath*/, bool *OUTPUT /*lpbIsPeer*/) = 0;
	
	// Archive store function(s)
	virtual HRESULT GetArchiveStoreEntryID(LPTSTR lpszUserName, LPTSTR lpszServerName, ULONG ulFlags, ULONG *OUTPUT /*lpcbStoreID*/, LPENTRYID *OUTPUT /*lppStoreID*/) = 0;

	virtual HRESULT ResetFolderCount(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *OUTPUT /*lpulUpdates*/) = 0;

	%extend {
		~IECServiceAdmin() { self->Release(); }
	}
};


