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

#ifndef ECMSGSTORE_H
#define ECMSGSTORE_H

#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <mapispi.h>

//NOTE: windows in $TOP/common/windows, linux in $TOP/mapi4linux/include
#include <edkmdb.h>

#include <kopano/ECUnknown.h>
#include "ECMAPIProp.h"
#include "WSTransport.h"
#include "ECNotifyClient.h"
#include "ECNamedProp.h"

#include <kopano/IECServiceAdmin.h>
#include "IECSpooler.h"
#include "IECMultiStoreTable.h"
#include <kopano/IECLicense.h>
#include "IECTestProtocol.h"

#include "IMAPIOffline.h"

#include <set>

class convstring;
class utf8string;
class ECMessage;
class ECMAPIFolder;

class IMessageFactory {
public:
	virtual HRESULT Create(ECMsgStore *lpMsgStore, BOOL fNew, BOOL fModify, ULONG ulFlags, BOOL bEmbedded, ECMAPIProp *lpRoot, ECMessage **lppMessage) const = 0;
};


class ECMsgStore : public ECMAPIProp {
	typedef void (* RELEASECALLBACK)(ECUnknown *lpObject, ECMsgStore *lpMsgStore);
protected:
	ECMsgStore(const char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore);
	virtual ~ECMsgStore();

	static HRESULT GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT SetPropHandler(ULONG ulPropTag, void* lpProvider, LPSPropValue lpsPropValue, void *lpParam);

public:
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface);
	virtual HRESULT QueryInterfaceProxy(REFIID refiid, void **lppInterface);

	static HRESULT Create(const char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL bIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore, ECMsgStore **lppECMsgStore);

	virtual HRESULT SaveChanges(ULONG ulFlags);
	virtual HRESULT SetProps(ULONG cValues, LPSPropValue lpPropArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT DeleteProps(LPSPropTagArray lpPropTagArray, LPSPropProblemArray *lppProblems);

	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk);
	virtual HRESULT Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection);
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult);
	virtual HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk);
	virtual HRESULT SetReceiveFolder(LPTSTR lpszMessageClass, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT GetReceiveFolder(LPTSTR lpszMessageClass, ULONG ulFlags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID, LPTSTR *lppszExplicitClass);
	virtual HRESULT GetReceiveFolderTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT StoreLogoff(ULONG *lpulFlags);
	virtual HRESULT AbortSubmit(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags);
	virtual HRESULT GetOutgoingQueue(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT SetLockState(LPMESSAGE lpMessage,ULONG ulLockState);
	virtual HRESULT FinishedMsg(ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT NotifyNewMail(LPNOTIFICATION lpNotification);


	virtual HRESULT CreateStoreEntryID(LPTSTR lpszMsgStoreDN, LPTSTR lpszMailboxDN, ULONG ulFlags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	virtual HRESULT CreateStoreEntryID2(ULONG cValues, LPSPropValue lpProps, ULONG ulFlags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	virtual HRESULT EntryIDFromSourceKey(ULONG cFolderKeySize, BYTE *lpFolderSourceKey,	ULONG cMessageKeySize, BYTE *lpMessageSourceKey, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	virtual HRESULT GetRights(ULONG cbUserEntryID, LPENTRYID lpUserEntryID, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *lpulRights);
	virtual HRESULT GetMailboxTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG ulFlags);
	virtual HRESULT GetPublicFolderTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG ulFlags);

	virtual HRESULT SetEntryId(ULONG cbEntryId, LPENTRYID lpEntryId);

	virtual ULONG	Release();
	virtual HRESULT HrSetReleaseCallback(ECUnknown *lpObject, RELEASECALLBACK lpfnCallback);

	// IECSpooler
	virtual HRESULT GetMasterOutgoingTable(ULONG ulFlags, IMAPITable ** lppOutgoingTable);
	virtual HRESULT DeleteFromMasterOutgoingTable(ULONG cbEntryId, const ENTRYID *lpEntryId, ULONG ulFlags);

	// IECServiceAdmin
	virtual HRESULT CreateStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG* lpcbStoreId, LPENTRYID* lppStoreId, ULONG* lpcbRootId, LPENTRYID *lppRootId);
	virtual HRESULT CreateEmptyStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG* lpcbStoreId, LPENTRYID* lppStoreId, ULONG* lpcbRootId, LPENTRYID* lppRootId);
	virtual HRESULT HookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, LPGUID lpGuid);
	virtual HRESULT UnhookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId);
	virtual HRESULT RemoveStore(LPGUID lpGuid);
	virtual HRESULT ResolveStore(LPGUID lpGuid, ULONG *lpulUserID, ULONG* lpcbStoreID, LPENTRYID* lppStoreID);
	virtual HRESULT CreateUser(ECUSER *lpECUser, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId);
	virtual HRESULT DeleteUser(ULONG cbUserId, LPENTRYID lpUserId);
	virtual HRESULT SetUser(ECUSER *lpECUser, ULONG ulFlags);
	virtual HRESULT GetUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ECUSER **lppECUser);
	virtual HRESULT ResolveUserName(LPCTSTR lpszUserName, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId);
	// virtual HRESULT GetUserList(ULONG *lpcUsers, ECUSER **lppsUsers); // inherited from ECMAPIProp
	virtual HRESULT GetSendAsList(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *lpcSenders, ECUSER **lppSenders);
	virtual HRESULT AddSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId);
	virtual HRESULT DelSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId);
	virtual HRESULT GetUserClientUpdateStatus(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ECUSERCLIENTUPDATESTATUS **lppECUCUS);
	virtual HRESULT RemoveAllObjects(ULONG cbUserId, LPENTRYID lpUserId);
	virtual HRESULT CreateGroup(ECGROUP *lpECGroup, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId);
	virtual HRESULT DeleteGroup(ULONG cbGroupId, LPENTRYID lpGroupId);
	virtual HRESULT SetGroup(ECGROUP *lpECGroup, ULONG ulFlags);
	virtual HRESULT GetGroup(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG ulFlags, ECGROUP **lppECGroup);
	virtual HRESULT ResolveGroupName(LPCTSTR lpszGroupName, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId);
	// virtual HRESULT GetGroupList(ULONG *lpcGroups, ECGROUP **lppsGroups); // inherited from ECMAPIProp
	virtual HRESULT DeleteGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId);
	virtual HRESULT AddGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId);
	virtual HRESULT GetUserListOfGroup(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers);
	virtual HRESULT GetGroupListOfUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups);
	virtual HRESULT CreateCompany(ECCOMPANY *lpECCompany, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId);
	virtual HRESULT DeleteCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId);
	virtual HRESULT SetCompany(ECCOMPANY *lpECCompany, ULONG ulFlags);
	virtual HRESULT GetCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ECCOMPANY **lppECCompany);
	virtual HRESULT ResolveCompanyName(LPCTSTR lpszCompanyName, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId);
	virtual HRESULT GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies);
	virtual HRESULT AddCompanyToRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId);
	virtual HRESULT DelCompanyFromRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId);
	virtual HRESULT GetRemoteViewList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies);
	virtual HRESULT AddUserToRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId);
	virtual HRESULT DelUserFromRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId);
	virtual HRESULT GetRemoteAdminList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers);
	virtual HRESULT SyncUsers(ULONG cbCompanyId, LPENTRYID lpCompanyId);
	virtual HRESULT GetQuota(ULONG cbUserId, LPENTRYID lpUserId, bool bGetUserDefault, ECQUOTA **lppsQuota);
	virtual HRESULT SetQuota(ULONG cbUserId, LPENTRYID lpUserId, ECQUOTA *lpsQuota);
	virtual HRESULT AddQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType);
	virtual HRESULT DeleteQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCmopanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType);
	virtual HRESULT GetQuotaRecipients(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers);
	virtual HRESULT GetQuotaStatus(ULONG cbUserId, LPENTRYID lpUserId, ECQUOTASTATUS **lppsQuotaStatus);
	virtual HRESULT PurgeCache(ULONG ulFlags);
	virtual HRESULT PurgeSoftDelete(ULONG ulDays);	
	virtual HRESULT PurgeDeferredUpdates(ULONG *lpulDeferredRemaining);
	virtual HRESULT GetServerDetails(ECSVRNAMELIST *lpServerNameList, ULONG ulFlags, ECSERVERLIST **lppsServerList);
	virtual HRESULT OpenUserStoresTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT ResolvePseudoUrl(const char *url, char **pathp, bool *ispeer);
	virtual HRESULT GetPublicStoreEntryID(ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID);
	virtual HRESULT GetArchiveStoreEntryID(LPCTSTR lpszUserName, LPCTSTR lpszServerName, ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID);
	virtual HRESULT ResetFolderCount(ULONG cbEntryId, LPENTRYID lpEntryId, ULONG *lpulUpdates);
	
	// MAPIOfflineMgr
	virtual HRESULT SetCurrentState(ULONG ulFlags, ULONG ulMask, ULONG ulState, void* pReserved);
	virtual HRESULT GetCapabilities(ULONG *pulCapabilities);
	virtual HRESULT GetCurrentState(ULONG* pulState);

	virtual HRESULT Advise(ULONG ulFlags, MAPIOFFLINE_ADVISEINFO* pAdviseInfo, ULONG* pulAdviseToken);
	virtual HRESULT Unadvise(ULONG ulFlags,ULONG ulAdviseToken);

	virtual HRESULT UnwrapNoRef(LPVOID *ppvObject);

	// ECMultiStoreTable
	virtual HRESULT OpenMultiStoreTable(LPENTRYLIST lpMsgList, ULONG ulFlags, LPMAPITABLE *lppTable);

    // ECLicense
    virtual HRESULT LicenseAuth(unsigned char *lpData, unsigned int ulSize, unsigned char **lppResponse, unsigned int * lpulResponseData);
    virtual HRESULT LicenseCapa(unsigned int ulServiceType, char ***lppszChars, unsigned int *lpulSize);
	virtual HRESULT LicenseUsers(unsigned int ulServiceType, unsigned int *lpulUsers);

    // ECTestProtocol
	virtual HRESULT TestPerform(char *szCommand, unsigned int ulArgs, char **szArgs);
	virtual HRESULT TestSet(char *szName, char *szValue);
	virtual HRESULT TestGet(char *szName, char **szValue);

	// Called when session is reloaded
	static HRESULT Reload(void *lpParam, ECSESSIONID sessionid);

	static HRESULT TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType);

	// ICS Streaming
	virtual HRESULT ExportMessageChangesAsStream(ULONG ulFlags, ULONG ulPropTag, std::vector<ICSCHANGE> &sChanges, ULONG ulStart, ULONG ulCount, LPSPropTagArray lpsProps, WSMessageStreamExporter **lppsStreamExporter);
	
protected:
	HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, const IMessageFactory &refMessageFactory, ULONG *lpulObjType, LPUNKNOWN *lppUnk);

public:
	BOOL IsSpooler(){ return m_fIsSpooler; }
	BOOL IsDefaultStore() { return m_fIsDefaultStore; }
	BOOL IsPublicStore();
	BOOL IsDelegateStore();
	BOOL IsOfflineStore() { return m_bOfflineStore; }
	LPCSTR GetProfileName() const { return m_strProfname.c_str(); }

public:
	const GUID& GetStoreGuid();
	HRESULT GetWrappedStoreEntryID(ULONG* lpcbWrapped, LPENTRYID* lppWrapped);
	//Special wrapper for the spooler vs outgoing queue
	HRESULT GetWrappedServerStoreEntryID(ULONG cbEntryId, LPBYTE lpEntryId, ULONG* lpcbWrapped, LPENTRYID* lppWrapped);

	HRESULT InternalAdvise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection);
private:
	HRESULT CreateSpecialFolder(LPMAPIFOLDER lpFolderParent, ECMAPIProp *lpFolderPropSet, const TCHAR *lpszFolderName, const TCHAR *lpszFolderComment, unsigned int ulPropTag, unsigned int ulMVPos, const TCHAR *lpszContainerClass, LPMAPIFOLDER *lppMAPIFolder);
	HRESULT SetSpecialEntryIdOnFolder(LPMAPIFOLDER lpFolder, ECMAPIProp *lpFolderPropSet, unsigned int ulPropTag, unsigned int ulMVPos);
	HRESULT OpenStatsTable(unsigned int ulTableType, LPMAPITABLE *lppTable);
	HRESULT CreateAdditionalFolder(IMAPIFolder *lpRootFolder, IMAPIFolder *lpInboxFolder, IMAPIFolder *lpSubTreeFolder, ULONG ulType, const TCHAR *lpszFolderName, const TCHAR *lpszComment, const TCHAR *lpszContainerType, bool fHidden);
	HRESULT AddRenAdditionalFolder(IMAPIFolder *lpFolder, ULONG ulType, SBinary *lpEntryID);



	static HRESULT MsgStoreDnToPseudoUrl(const utf8string &strMsgStoreDN, utf8string *lpstrPseudoUrl);

public:
	class xMsgStore _kc_final : public IMsgStore {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IMsgStore.hpp>
		#include <kopano/xclsfrag/IMAPIProp.hpp>
	} m_xMsgStore;

	class xExchangeManageStore _kc_final : public IExchangeManageStore {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IExchangeManageStore.hpp>
	} m_xExchangeManageStore;

	class xExchangeManageStore6 _kc_final : public IExchangeManageStore6 {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IExchangeManageStore.hpp>
		virtual HRESULT __stdcall CreateStoreEntryIDEx(LPTSTR lpszMsgStoreDN, LPTSTR lpszEmail, LPTSTR lpszMailboxDN, ULONG flags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID) _kc_override;
	} m_xExchangeManageStore6;

	class xExchangeManageStoreEx _kc_final : public IExchangeManageStoreEx {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IExchangeManageStore.hpp>
		virtual HRESULT __stdcall CreateStoreEntryID2(ULONG cValues, LPSPropValue lpProps, ULONG flags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID) _kc_override;
	} m_xExchangeManageStoreEx;

	class xECServiceAdmin _kc_final : public IECServiceAdmin {
		#include <kopano/xclsfrag/IUnknown.hpp>

		// <kopano/xclsfrag/IECServiceAdmin.hpp>
		virtual HRESULT __stdcall CreateStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG *lpcbStoreId, LPENTRYID *lppStoreId, ULONG *lpcbRootId, LPENTRYID *lppRootId) _kc_override;
		virtual HRESULT __stdcall CreateEmptyStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG flags, ULONG *lpcbStoreId, LPENTRYID *lppStoreId, ULONG *lpcbRootId, LPENTRYID *lppRootId) _kc_override;
		virtual HRESULT __stdcall HookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, LPGUID lpGuid) _kc_override;
		virtual HRESULT __stdcall UnhookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId) _kc_override;
		virtual HRESULT __stdcall RemoveStore(LPGUID lpGuid) _kc_override;
		virtual HRESULT __stdcall ResolveStore(LPGUID lpGuid, ULONG *lpulUserID, ULONG *lpcbStoreID, LPENTRYID *lppStoreID) _kc_override;
		virtual HRESULT __stdcall CreateUser(ECUSER *lpECUser, ULONG flags, ULONG *lpcbUserId, LPENTRYID *lppUserId) _kc_override;
		virtual HRESULT __stdcall DeleteUser(ULONG cbUserId, LPENTRYID lpUserId) _kc_override;
		virtual HRESULT __stdcall SetUser(ECUSER *lpECUser, ULONG flags) _kc_override;
		virtual HRESULT __stdcall GetUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG flags, ECUSER **lppECUser) _kc_override;
		virtual HRESULT __stdcall ResolveUserName(LPCTSTR lpszUserName, ULONG flags, ULONG *lpcbUserId, LPENTRYID *lppUserId) _kc_override;
		virtual HRESULT __stdcall GetUserList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG flags, ULONG *lpcUsers, ECUSER **lppsUsers) _kc_override;
		virtual HRESULT __stdcall GetSendAsList(ULONG cbUserId, LPENTRYID lpUserId, ULONG flags, ULONG *lpcSenders, ECUSER **lppSenders) _kc_override;
		virtual HRESULT __stdcall AddSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId) _kc_override;
		virtual HRESULT __stdcall DelSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId) _kc_override;
		virtual HRESULT __stdcall GetUserClientUpdateStatus(ULONG cbUserId, LPENTRYID lpUserId, ULONG flags, ECUSERCLIENTUPDATESTATUS **lppECUCUS) _kc_override;
		virtual HRESULT __stdcall RemoveAllObjects(ULONG cbUserId, LPENTRYID lpUserId) _kc_override;
		virtual HRESULT __stdcall CreateGroup(ECGROUP *lpECGroup, ULONG flags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId) _kc_override;
		virtual HRESULT __stdcall DeleteGroup(ULONG cbGroupId, LPENTRYID lpGroupId) _kc_override;
		virtual HRESULT __stdcall SetGroup(ECGROUP *lpECGroup, ULONG flags) _kc_override;
		virtual HRESULT __stdcall GetGroup(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG flags, ECGROUP **lppECGroup) _kc_override;
		virtual HRESULT __stdcall ResolveGroupName(LPCTSTR lpszGroupName, ULONG flags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId) _kc_override;
		virtual HRESULT __stdcall GetGroupList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG flags, ULONG *lpcGroups, ECGROUP **lppsGroups) _kc_override;
		virtual HRESULT __stdcall DeleteGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId) _kc_override;
		virtual HRESULT __stdcall AddGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId) _kc_override;
		virtual HRESULT __stdcall GetUserListOfGroup(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG flags, ULONG *lpcUsers, ECUSER **lppsUsers) _kc_override;
		virtual HRESULT __stdcall GetGroupListOfUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG flags, ULONG *lpcGroups, ECGROUP **lppsGroups) _kc_override;
		virtual HRESULT __stdcall CreateCompany(ECCOMPANY *lpECCompany, ULONG flags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId) _kc_override;
		virtual HRESULT __stdcall DeleteCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId) _kc_override;
		virtual HRESULT __stdcall SetCompany(ECCOMPANY *lpECCompany, ULONG flags) _kc_override;
		virtual HRESULT __stdcall GetCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG flags, ECCOMPANY **lppECCompany) _kc_override;
		virtual HRESULT __stdcall ResolveCompanyName(LPCTSTR lpszCompanyName, ULONG flags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId) _kc_override;
		virtual HRESULT __stdcall GetCompanyList(ULONG flags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies) _kc_override;
		virtual HRESULT __stdcall AddCompanyToRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId) _kc_override;
		virtual HRESULT __stdcall DelCompanyFromRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId) _kc_override;
		virtual HRESULT __stdcall GetRemoteViewList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG flags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies) _kc_override;
		virtual HRESULT __stdcall AddUserToRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId) _kc_override;
		virtual HRESULT __stdcall DelUserFromRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId) _kc_override;
		virtual HRESULT __stdcall GetRemoteAdminList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG flags, ULONG *lpcUsers, ECUSER **lppsUsers) _kc_override;
		virtual HRESULT __stdcall SyncUsers(ULONG cbCompanyId, LPENTRYID lpCompanyId) _kc_override;
		virtual HRESULT __stdcall GetQuota(ULONG cbUserId, LPENTRYID lpUserId, bool bGetUserDefault, ECQUOTA **lppsQuota) _kc_override;
		virtual HRESULT __stdcall SetQuota(ULONG cbUserId, LPENTRYID lpUserId, ECQUOTA *lpsQuota) _kc_override;
		virtual HRESULT __stdcall AddQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType) _kc_override;
		virtual HRESULT __stdcall DeleteQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCmopanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType) _kc_override;
		virtual HRESULT __stdcall GetQuotaRecipients(ULONG cbUserId, LPENTRYID lpUserId, ULONG flags, ULONG *lpcUsers, ECUSER **lppsUsers) _kc_override;
		virtual HRESULT __stdcall GetQuotaStatus(ULONG cbUserId, LPENTRYID lpUserId, ECQUOTASTATUS **lppsQuotaStatus) _kc_override;
		virtual HRESULT __stdcall PurgeCache(ULONG flags) _kc_override;
		virtual HRESULT __stdcall PurgeSoftDelete(ULONG ulDays) _kc_override;
		virtual HRESULT __stdcall PurgeDeferredUpdates(ULONG *lpulDeferredRemaining) _kc_override;
		virtual HRESULT __stdcall GetServerDetails(ECSVRNAMELIST *lpServerNameList, ULONG flags, ECSERVERLIST **lppsServerList) _kc_override;
		virtual HRESULT __stdcall OpenUserStoresTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
		virtual HRESULT __stdcall ResolvePseudoUrl(const char *url, char **pathp, bool *ispeer) _kc_override;
		virtual HRESULT __stdcall GetPublicStoreEntryID(ULONG flags, ULONG *lpcbStoreID, LPENTRYID *lppStoreID) _kc_override;
		virtual HRESULT __stdcall GetArchiveStoreEntryID(LPCTSTR lpszUserName, LPCTSTR lpszServerName, ULONG flags, ULONG *lpcbStoreID, LPENTRYID *lppStoreID) _kc_override;
		virtual HRESULT __stdcall ResetFolderCount(ULONG cbEntryId, LPENTRYID lpEntryId, ULONG *lpulUpdates) _kc_override;
	} m_xECServiceAdmin;

	class xECSpooler _kc_final : public IECSpooler {
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IECSpooler.hpp>
		virtual HRESULT __stdcall GetMasterOutgoingTable(ULONG flags, IMAPITable **lppOutgoingTable) _kc_override;
		virtual HRESULT __stdcall DeleteFromMasterOutgoingTable(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG flags) _kc_override;
	} m_xECSpooler;

	class xMAPIOfflineMgr _kc_final : public IMAPIOfflineMgr {
		#include <kopano/xclsfrag/IUnknown.hpp>

		// <kopano/xclsfrag/IMAPIOffline.hpp>
		virtual HRESULT __stdcall SetCurrentState(ULONG flags, ULONG ulMask, ULONG ulState, void *pReserved) _kc_override;
		virtual HRESULT __stdcall GetCapabilities(ULONG *pulCapabilities) _kc_override;
		virtual HRESULT __stdcall GetCurrentState(ULONG *pulState) _kc_override;

		// <kopano/xclsfrag/IMAPIOfflineMgr.hpp>
		virtual HRESULT __stdcall Advise(ULONG flags, MAPIOFFLINE_ADVISEINFO *pAdviseInfo, ULONG *pulAdviseToken) _kc_override;
		virtual HRESULT __stdcall Unadvise(ULONG flags, ULONG ulAdviseToken) _kc_override;
	} m_xMAPIOfflineMgr;

	class xProxyStoreObject _kc_final : public IProxyStoreObject {
		#include <kopano/xclsfrag/IUnknown.hpp>

		// <kopano/xclsfrag/IProxyStoreObject.hpp>
		virtual HRESULT __stdcall PlaceHolder1(void) _kc_override;
		virtual HRESULT __stdcall UnwrapNoRef(LPVOID *ppvObject) _kc_override;
		virtual HRESULT __stdcall PlaceHolder2(void) _kc_override;
	} m_xProxyStoreObject;

	class xMsgStoreProxy _kc_final : public IMsgStore {
		#include <kopano/xclsfrag/IUnknown.hpp>
		#include <kopano/xclsfrag/IMsgStore.hpp>
		#include <kopano/xclsfrag/IMAPIProp.hpp>
	} m_xMsgStoreProxy;
	
	class xECMultiStoreTable _kc_final : public IECMultiStoreTable {
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IECMultiStoreTable.hpp>
		virtual HRESULT __stdcall OpenMultiStoreTable(LPENTRYLIST lpMsgList, ULONG flags, LPMAPITABLE *lppTable) _kc_override;
	} m_xECMultiStoreTable;

	class xECLicense _kc_final : public IECLicense {
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IECLicense.hpp>
		virtual HRESULT __stdcall LicenseAuth(unsigned char *lpData, unsigned int ulSize, unsigned char **lpResponseData, unsigned int *lpulResponseSize) _kc_override;
		virtual HRESULT __stdcall LicenseCapa(unsigned int ulServiceType, char ***lppszCapas, unsigned int *lpulSize) _kc_override;
		virtual HRESULT __stdcall LicenseUsers(unsigned int ulServiceType, unsigned int *lpulUsers) _kc_override;
    } m_xECLicense;

	class xECTestProtocol _kc_final : public IECTestProtocol {
    public:
		#include <kopano/xclsfrag/IUnknown.hpp>
		// <kopano/xclsfrag/IECTestProtocol.hpp>
		virtual HRESULT __stdcall TestPerform(char *szCommand, unsigned int ulArgs, char **szArgs) _kc_override;
		virtual HRESULT __stdcall TestSet(char *szName, char *szValue) _kc_override;
		virtual HRESULT __stdcall TestGet(char *szName, char **szValue) _kc_override;
    } m_xECTestProtocol;
	
public:
	LPMAPISUP			lpSupport;
	WSTransport*		lpTransport;
	ECNotifyClient *m_lpNotifyClient = nullptr;
	ECNamedProp*		lpNamedProp;
	ULONG				m_ulProfileFlags;
	MAPIUID				m_guidMDB_Provider;
	unsigned int m_ulClientVersion = 0;

private:
	BOOL				m_fIsSpooler;
	BOOL				m_fIsDefaultStore;
	BOOL				m_bOfflineStore;
	RELEASECALLBACK lpfnCallback = nullptr;
	ECUnknown *lpCallbackObject = nullptr;
	std::string			m_strProfname;
	std::set<ULONG>		m_setAdviseConnections;
};

class ECMSLogon _kc_final : public ECUnknown {
private:
	ECMSLogon(ECMsgStore *lpStore);
	ECMsgStore *m_lpStore;
	
public:
	static HRESULT Create(ECMsgStore *lpStore, ECMSLogon **lppECMSLogon);
	
	HRESULT QueryInterface(REFIID refiid, void **lppInterface);
	HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	HRESULT Logoff(ULONG *lpulFlags);
	HRESULT OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk);
	HRESULT CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult);
	HRESULT Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection);
	HRESULT Unadvise(ULONG ulConnection);
	HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPVOID *lppEntry);
	
	class xMSLogon _kc_final : public IMSLogon {
		#include <kopano/xclsfrag/IUnknown.hpp>

		// <kopano/xclsfrag/IMSLogon.hpp>
		virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
		virtual HRESULT __stdcall Logoff(ULONG *flags) _kc_override;
		virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG flags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) _kc_override;
		virtual HRESULT __stdcall CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG flags, ULONG *lpulResult) _kc_override;
		virtual HRESULT __stdcall Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection) _kc_override;
		virtual HRESULT __stdcall Unadvise(ULONG ulConnection) _kc_override;
		virtual HRESULT __stdcall OpenStatusEntry(LPCIID lpInterface, ULONG flags, ULONG *lpulObjType, LPVOID *lppEntry) _kc_override;
	} m_xMSLogon;
};

#endif // ECMSGSTORE_H

