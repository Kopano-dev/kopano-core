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

// IECServiceAdmin.h: interface for the IECServiceAdmin class.
//
//////////////////////////////////////////////////////////////////////

#ifndef IECSERVICEADMIN
#define IECSERVICEADMIN

#include <kopano/ECDefs.h>

namespace KC {

class IECServiceAdmin : public IUnknown {
public:
	// Create/Delete stores
	virtual HRESULT __stdcall CreateStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG* lpcbStoreId, LPENTRYID* lppStoreId, ULONG* lpcbRootId, LPENTRYID *lppRootId) = 0;
	virtual HRESULT __stdcall CreateEmptyStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG* lpcbStoreId, LPENTRYID* lppStoreId, ULONG* lpcbRootId, LPENTRYID *lppRootId) = 0;
	virtual HRESULT __stdcall ResolveStore(LPGUID lpGuid, ULONG *lpulUserID, ULONG* lpcbStoreID, LPENTRYID* lppStoreID) = 0;
	virtual HRESULT __stdcall HookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, LPGUID lpGuid) = 0;
	virtual HRESULT __stdcall UnhookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId) = 0;
	virtual HRESULT __stdcall RemoveStore(LPGUID lpGuid) = 0;

	// User functions
	virtual HRESULT __stdcall CreateUser(ECUSER *lpECUser, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId) = 0;
	virtual HRESULT __stdcall DeleteUser(ULONG cbUserId, LPENTRYID lpUserId) = 0;
	virtual HRESULT __stdcall SetUser(ECUSER *lpECUser, ULONG ulFlags) = 0;
	virtual HRESULT __stdcall GetUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ECUSER **lppECUser) = 0;
	virtual HRESULT __stdcall ResolveUserName(LPCTSTR lpszUserName, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId) = 0;
	virtual HRESULT __stdcall GetUserList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers) = 0;
	virtual HRESULT __stdcall GetSendAsList(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *lpcSenders, ECUSER **lppSenders) = 0;
	virtual HRESULT __stdcall AddSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId) = 0;
	virtual HRESULT __stdcall DelSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId) = 0;
	virtual HRESULT __stdcall GetUserClientUpdateStatus(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ECUSERCLIENTUPDATESTATUS **lppECUCUS) = 0;
	
	// Remove all users EXCEPT the passed user
	virtual HRESULT __stdcall RemoveAllObjects(ULONG cbUserId, LPENTRYID lpUserId) = 0;

	// Group functions
	virtual HRESULT __stdcall CreateGroup(ECGROUP *lpECGroup, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId) = 0;
	virtual HRESULT __stdcall DeleteGroup(ULONG cbGroupId, LPENTRYID lpGroupId) = 0;
	virtual HRESULT __stdcall SetGroup(ECGROUP *lpECGroup, ULONG ulFlags) = 0;
	virtual HRESULT __stdcall GetGroup(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG ulFlags, ECGROUP **lppECGroup) = 0;
	virtual HRESULT __stdcall ResolveGroupName(LPCTSTR lpszGroupName, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId) = 0;
	virtual HRESULT __stdcall GetGroupList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups) = 0;

	virtual HRESULT __stdcall DeleteGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId) = 0;
	virtual HRESULT __stdcall AddGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId) = 0;
	virtual HRESULT __stdcall GetUserListOfGroup(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers) = 0;
	virtual HRESULT __stdcall GetGroupListOfUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups) = 0;

	// Company functions
	virtual HRESULT __stdcall CreateCompany(ECCOMPANY *lpECCompany, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId) = 0;
	virtual HRESULT __stdcall DeleteCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT __stdcall SetCompany(ECCOMPANY *lpECCompany, ULONG ulFlags) = 0;
	virtual HRESULT __stdcall GetCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ECCOMPANY **lppECCompany) = 0;
	virtual HRESULT __stdcall ResolveCompanyName(LPCTSTR lpszCompanyName, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId) = 0;
	virtual HRESULT __stdcall GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies) = 0;

	virtual HRESULT __stdcall AddCompanyToRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT __stdcall DelCompanyFromRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT __stdcall GetRemoteViewList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies) = 0;
	virtual HRESULT __stdcall AddUserToRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT __stdcall DelUserFromRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId) = 0;
	virtual HRESULT __stdcall GetRemoteAdminList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers) = 0;

	virtual HRESULT __stdcall SyncUsers(ULONG cbCompanyId, LPENTRYID lpCOmpanyId) = 0;

	// Quota functions
	
	virtual HRESULT __stdcall GetQuota(ULONG cbUserId, LPENTRYID lpUserId, bool bGetUserDefaultQuota, ECQUOTA **lppsQuota) = 0;
	virtual HRESULT __stdcall SetQuota(ULONG cbUserId, LPENTRYID lpUserId, ECQUOTA *lpsQuota) = 0;

	virtual HRESULT __stdcall AddQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType) = 0;
	virtual HRESULT __stdcall DeleteQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCmopanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType) = 0;
	virtual HRESULT __stdcall GetQuotaRecipients(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers) = 0;

	virtual HRESULT __stdcall GetQuotaStatus(ULONG cbUserId, LPENTRYID lpUserId, ECQUOTASTATUS **lppsQuotaStatus) = 0;
	
	virtual HRESULT __stdcall PurgeSoftDelete(ULONG ulDays) = 0;
	virtual HRESULT __stdcall PurgeCache(ULONG ulFlags) = 0;
	virtual HRESULT __stdcall OpenUserStoresTable(ULONG ulFlags, LPMAPITABLE *lppTable) = 0;
	virtual HRESULT __stdcall PurgeDeferredUpdates(ULONG *lpulDeferredRemaining) = 0;

	// Multiserver functions
	virtual HRESULT __stdcall GetServerDetails(ECSVRNAMELIST *lpServerNameList, ULONG ulFlags, ECSERVERLIST **lppsServerList) = 0;
	virtual HRESULT __stdcall ResolvePseudoUrl(const char *url, char **path, bool *ispeer) = 0;

	// Public store function(s)
	virtual HRESULT __stdcall GetPublicStoreEntryID(ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID) = 0;

	// Archive store function(s)
	virtual HRESULT __stdcall GetArchiveStoreEntryID(LPCTSTR lpszUserName, LPCTSTR lpszServerName, ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID) = 0;

	virtual HRESULT __stdcall ResetFolderCount(ULONG cbEntryId, LPENTRYID lpEntryId, ULONG *lpulUpdates) = 0;
};

} /* namespace */

#endif // #ifndef IECSERVICEADMIN
