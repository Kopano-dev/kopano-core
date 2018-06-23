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

#include <memory>
#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <mapispi.h>

//NOTE: windows in $TOP/common/windows, linux in $TOP/mapi4linux/include
#include <edkmdb.h>

#include <kopano/ECUnknown.h>
#include <kopano/Util.h>
#include "ECMAPIProp.h"
#include "WSTransport.h"
#include "ECNotifyClient.h"
#include "ECNamedProp.h"
#include <kopano/IECInterfaces.hpp>
#include <set>

namespace KC {
class convstring;
class utf8string;
}

using namespace KC;
class ECMessage;
class ECMAPIFolder;

class IMessageFactory {
public:
	virtual HRESULT Create(ECMsgStore *, BOOL fnew, BOOL modify, ULONG flags, BOOL embedded, const ECMAPIProp *root, ECMessage **) const = 0;
};

class ECMsgStore :
    public ECMAPIProp, public IMsgStore, public IExchangeManageStore,
    public IECServiceAdmin, public IProxyStoreObject, public IECSpooler {
protected:
	ECMsgStore(const char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore);
	virtual ~ECMsgStore();

	static HRESULT GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);

public:
	virtual HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	virtual HRESULT QueryInterfaceProxy(REFIID refiid, void **lppInterface);

	static HRESULT Create(const char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL bIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore, ECMsgStore **lppECMsgStore);

	virtual HRESULT SaveChanges(ULONG ulFlags);
	virtual HRESULT SetProps(ULONG cValues, const SPropValue *lpPropArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *lppProblems);
	virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk);
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result);
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT SetReceiveFolder(const TCHAR *cls, ULONG flags, ULONG eid_size, const ENTRYID *eid) override;
	virtual HRESULT GetReceiveFolder(const TCHAR *cls, ULONG flags, ULONG *eid_size, ENTRYID **eid, TCHAR **exp_class) override;
	virtual HRESULT GetReceiveFolderTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT StoreLogoff(ULONG *lpulFlags);
	virtual HRESULT AbortSubmit(ULONG eid_size, const ENTRYID *, ULONG flags) override;
	virtual HRESULT GetOutgoingQueue(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT SetLockState(LPMESSAGE lpMessage,ULONG ulLockState);
	virtual HRESULT FinishedMsg(ULONG flags, ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT NotifyNewMail(LPNOTIFICATION lpNotification);

	virtual HRESULT CreateStoreEntryID(const TCHAR *store_dn, const TCHAR *mbox_dn, ULONG flags, ULONG *eid_size, ENTRYID **eid);
	virtual HRESULT EntryIDFromSourceKey(ULONG cFolderKeySize, BYTE *lpFolderSourceKey,	ULONG cMessageKeySize, BYTE *lpMessageSourceKey, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	virtual HRESULT GetRights(ULONG ueid_size, const ENTRYID *user_eid, ULONG eid_size, const ENTRYID *eid, ULONG *rights) override;
	virtual HRESULT GetMailboxTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG ulFlags);
	virtual HRESULT GetPublicFolderTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG ulFlags);
	virtual HRESULT SetEntryId(ULONG eid_size, const ENTRYID *eid);
	virtual ULONG Release(void) _kc_override;

	// IECSpooler
	virtual HRESULT GetMasterOutgoingTable(ULONG ulFlags, IMAPITable ** lppOutgoingTable);
	virtual HRESULT DeleteFromMasterOutgoingTable(ULONG cbEntryId, const ENTRYID *lpEntryId, ULONG ulFlags);

	// IECServiceAdmin
	virtual HRESULT CreateStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, ULONG *newstore_size, ENTRYID **newstore_eid, ULONG *root_size, ENTRYID **root_eid) override;
	virtual HRESULT CreateEmptyStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, ULONG flags, ULONG *newstore_size, ENTRYID **newstore_eid, ULONG *root_size, ENTRYID **root_eid) override;
	virtual HRESULT ResolveStore(const GUID *, ULONG *user_id, ULONG *store_size, ENTRYID **store_eid) override;
	virtual HRESULT HookStore(ULONG store_type, ULONG eid_size, const ENTRYID *eid, const GUID *guid) override;
	virtual HRESULT UnhookStore(ULONG store_type, ULONG eid_size, const ENTRYID *eid) override;
	virtual HRESULT RemoveStore(const GUID *) override;
	virtual HRESULT CreateUser(ECUSER *lpECUser, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId);
	virtual HRESULT DeleteUser(ULONG ueid_size, const ENTRYID *user_eid) override;
	virtual HRESULT SetUser(ECUSER *lpECUser, ULONG ulFlags);
	virtual HRESULT GetUser(ULONG eid_size, const ENTRYID *user_eid, ULONG flags, ECUSER **) override;
	virtual HRESULT ResolveUserName(LPCTSTR lpszUserName, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId);
	virtual HRESULT GetSendAsList(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *nsenders, ECUSER **senders) override;
	virtual HRESULT AddSendAsUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG seid_size, const ENTRYID *sender_eid) override;
	virtual HRESULT DelSendAsUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG seid_size, const ENTRYID *sender_eid) override;
	virtual HRESULT GetUserClientUpdateStatus(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ECUSERCLIENTUPDATESTATUS **) override;
	virtual HRESULT RemoveAllObjects(ULONG ueid_size, const ENTRYID *user_eid) override;
	virtual HRESULT CreateGroup(ECGROUP *lpECGroup, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId);
	virtual HRESULT DeleteGroup(ULONG geid_size, const ENTRYID *grp_eid) override;
	virtual HRESULT SetGroup(ECGROUP *lpECGroup, ULONG ulFlags);
	virtual HRESULT GetGroup(ULONG grp_size, const ENTRYID *grp_eid, ULONG flags, ECGROUP **) override;
	virtual HRESULT ResolveGroupName(LPCTSTR lpszGroupName, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId);
	virtual HRESULT DeleteGroupUser(ULONG geid_size, const ENTRYID *grp_eid, ULONG ueid_size, const ENTRYID *user_eid) override;
	virtual HRESULT AddGroupUser(ULONG geid_size, const ENTRYID *grp_eid, ULONG ueid_size, const ENTRYID *user_eid) override;
	virtual HRESULT GetUserListOfGroup(ULONG geid_size, const ENTRYID *grp_eid, ULONG flags, ULONG *nusers, ECUSER **) override;
	virtual HRESULT GetGroupListOfUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *ngrps, ECGROUP **) override;
	virtual HRESULT CreateCompany(ECCOMPANY *lpECCompany, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId);
	virtual HRESULT DeleteCompany(ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT SetCompany(ECCOMPANY *lpECCompany, ULONG ulFlags);
	virtual HRESULT GetCompany(ULONG cmp_size, const ENTRYID *cmp_eid, ULONG flags, ECCOMPANY **) override;
	virtual HRESULT ResolveCompanyName(LPCTSTR lpszCompanyName, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId);
	virtual HRESULT AddCompanyToRemoteViewList(ULONG sc_size, const ENTRYID *scom_eid, ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT DelCompanyFromRemoteViewList(ULONG sc_size, const ENTRYID *scom_eid, ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT GetRemoteViewList(ULONG ceid_size, const ENTRYID *com_eid, ULONG flags, ULONG *ncomps, ECCOMPANY **) override;
	virtual HRESULT AddUserToRemoteAdminList(ULONG ueid_size, const ENTRYID *user_eid, ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT DelUserFromRemoteAdminList(ULONG ueid_size, const ENTRYID *user_eid, ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT GetRemoteAdminList(ULONG ceid_size, const ENTRYID *com_eid, ULONG flags, ULONG *nusers, ECUSER **) override;
	virtual HRESULT SyncUsers(ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT GetQuota(ULONG ueid_size, const ENTRYID *user_eid, bool get_dfl, ECQUOTA **) override;
	virtual HRESULT SetQuota(ULONG ueid_size, const ENTRYID *user_eid, ECQUOTA *) override;
	virtual HRESULT AddQuotaRecipient(ULONG ceid_size, const ENTRYID *com_eid, ULONG reid_size, const ENTRYID *recip_eid, ULONG type) override;
	virtual HRESULT DeleteQuotaRecipient(ULONG ceid_size, const ENTRYID *com_eid, ULONG reid_size, const ENTRYID *recip_eid, ULONG type) override;
	virtual HRESULT GetQuotaRecipients(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *nusers, ECUSER **) override;
	virtual HRESULT GetQuotaStatus(ULONG ueid_size, const ENTRYID *user_eid, ECQUOTASTATUS **) override;
	virtual HRESULT PurgeCache(ULONG ulFlags);
	virtual HRESULT PurgeSoftDelete(ULONG ulDays);	
	virtual HRESULT PurgeDeferredUpdates(ULONG *lpulDeferredRemaining);
	virtual HRESULT GetServerDetails(ECSVRNAMELIST *lpServerNameList, ULONG ulFlags, ECSERVERLIST **lppsServerList);
	virtual HRESULT OpenUserStoresTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT ResolvePseudoUrl(const char *url, char **pathp, bool *ispeer);
	virtual HRESULT GetArchiveStoreEntryID(LPCTSTR lpszUserName, LPCTSTR lpszServerName, ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID);
	virtual HRESULT ResetFolderCount(ULONG eid_size, const ENTRYID *eid, ULONG *nupdates) override;
	virtual HRESULT UnwrapNoRef(LPVOID *ppvObject);

	// ECMultiStoreTable
	virtual HRESULT OpenMultiStoreTable(const ENTRYLIST *msglist, ULONG flags, IMAPITable **);

    // ECTestProtocol
	virtual HRESULT TestPerform(const char *cmd, unsigned int argc, char **argv);
	virtual HRESULT TestSet(const char *name, const char *value);
	virtual HRESULT TestGet(const char *name, char **value);

	// Called when session is reloaded
	static HRESULT Reload(void *lpParam, ECSESSIONID sessionid);

	static HRESULT TableRowGetProp(void *prov, const struct propVal *src, SPropValue *dst, void **base, ULONG type);

	// ICS Streaming
	virtual HRESULT ExportMessageChangesAsStream(ULONG ulFlags, ULONG ulPropTag, const std::vector<ICSCHANGE> &sChanges, ULONG ulStart, ULONG ulCount, const SPropTagArray *lpsProps, WSMessageStreamExporter **lppsStreamExporter);
	
protected:
	HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, const IMessageFactory &, ULONG *obj_type, IUnknown **);

public:
	BOOL IsSpooler() const { return m_fIsSpooler; }
	BOOL IsDefaultStore() const { return m_fIsDefaultStore; }
	BOOL IsPublicStore() const;
	BOOL IsDelegateStore() const;
	BOOL IsOfflineStore() const { return false; }
	LPCSTR GetProfileName() const { return m_strProfname.c_str(); }

	const GUID& GetStoreGuid();
	HRESULT GetWrappedStoreEntryID(ULONG* lpcbWrapped, LPENTRYID* lppWrapped);
	//Special wrapper for the spooler vs outgoing queue
	HRESULT GetWrappedServerStoreEntryID(ULONG cbEntryId, LPBYTE lpEntryId, ULONG* lpcbWrapped, LPENTRYID* lppWrapped);
	HRESULT InternalAdvise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn);
private:
	HRESULT CreateSpecialFolder(LPMAPIFOLDER lpFolderParent, ECMAPIProp *lpFolderPropSet, const TCHAR *lpszFolderName, const TCHAR *lpszFolderComment, unsigned int ulPropTag, unsigned int ulMVPos, const TCHAR *lpszContainerClass, LPMAPIFOLDER *lppMAPIFolder);
	HRESULT SetSpecialEntryIdOnFolder(LPMAPIFOLDER lpFolder, ECMAPIProp *lpFolderPropSet, unsigned int ulPropTag, unsigned int ulMVPos);
	HRESULT OpenStatsTable(unsigned int ulTableType, LPMAPITABLE *lppTable);
	HRESULT CreateAdditionalFolder(IMAPIFolder *lpRootFolder, IMAPIFolder *lpInboxFolder, IMAPIFolder *lpSubTreeFolder, ULONG ulType, const TCHAR *lpszFolderName, const TCHAR *lpszComment, const TCHAR *lpszContainerType, bool fHidden);
	HRESULT AddRenAdditionalFolder(IMAPIFolder *lpFolder, ULONG ulType, SBinary *lpEntryID);



	static HRESULT MsgStoreDnToPseudoUrl(const utf8string &strMsgStoreDN, utf8string *lpstrPseudoUrl);

public:
	class xMsgStoreProxy _kc_final :
	    public IMsgStore, public IECMultiStoreTable, public IECTestProtocol {
		virtual ULONG AddRef(void) _kc_override;
		virtual ULONG Release(void) _kc_override;
		virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
		virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
		virtual HRESULT Unadvise(ULONG ulConnection) _kc_override;
		virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result);
		virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
		virtual HRESULT SetReceiveFolder(const TCHAR *cls, ULONG flags, ULONG eid_size, const ENTRYID *eid) override;
		virtual HRESULT GetReceiveFolder(const TCHAR *cls, ULONG flags, ULONG *eid_size, ENTRYID **eid, TCHAR **exp_class) override;
		virtual HRESULT GetReceiveFolderTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
		virtual HRESULT StoreLogoff(ULONG *lpulFlags) _kc_override;
		virtual HRESULT AbortSubmit(ULONG eid_size, const ENTRYID *, ULONG flags) override;
		virtual HRESULT GetOutgoingQueue(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
		virtual HRESULT SetLockState(LPMESSAGE lpMessage,ULONG ulLockState) _kc_override;
		virtual HRESULT FinishedMsg(ULONG flags, ULONG eid_size, const ENTRYID *) override;
		virtual HRESULT NotifyNewMail(LPNOTIFICATION lpNotification) _kc_override;
		virtual HRESULT GetLastError(HRESULT hError, ULONG flags, LPMAPIERROR *lppMapiError) _kc_override;
		virtual HRESULT SaveChanges(ULONG flags) _kc_override;
		virtual HRESULT GetProps(const SPropTagArray *lpPropTagArray, ULONG flags, ULONG *lpcValues, LPSPropValue *lppPropArray) _kc_override;
		virtual HRESULT GetPropList(ULONG flags, LPSPropTagArray *lppPropTagArray) _kc_override;
		virtual HRESULT OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG flags, LPUNKNOWN *lppUnk) _kc_override __attribute__((nonnull(3)));
		virtual HRESULT SetProps(ULONG cValues, const SPropValue *lpPropArray, LPSPropProblemArray *lppProblems) _kc_override;
		virtual HRESULT DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *lppProblems) _kc_override;
		virtual HRESULT CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *exclprop, ULONG ui_param, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG flags, LPSPropProblemArray *lppProblems) _kc_override;
		virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG flags, LPSPropProblemArray *lppProblems) _kc_override;
		virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names) override;
		virtual HRESULT GetIDsFromNames(ULONG cNames, LPMAPINAMEID *ppNames, ULONG flags, LPSPropTagArray *pptaga) _kc_override;
		virtual HRESULT OpenMultiStoreTable(const ENTRYLIST *msglist, ULONG flags, IMAPITable **table) override;
		virtual HRESULT TestPerform(const char *cmd, unsigned int argc, char **args) _kc_override;
		virtual HRESULT TestSet(const char *name, const char *value) _kc_override;
		virtual HRESULT TestGet(const char *name, char **value) _kc_override;
	} m_xMsgStoreProxy;
	
public:
	KC::object_ptr<IMAPISupport> lpSupport;
	KC::object_ptr<WSTransport> lpTransport;
	ECNamedProp lpNamedProp;
	KC::object_ptr<ECNotifyClient> m_lpNotifyClient;
	ULONG				m_ulProfileFlags;
	MAPIUID				m_guidMDB_Provider;
	unsigned int m_ulClientVersion = 0;

private:
	BOOL m_fIsSpooler, m_fIsDefaultStore;
	std::string			m_strProfname;
	std::set<ULONG>		m_setAdviseConnections;
	ALLOC_WRAP_FRIEND;
};

class ECMSLogon _kc_final : public ECUnknown, public IMSLogon {
private:
	ECMSLogon(ECMsgStore *lpStore);
	ECMsgStore *m_lpStore;
	
public:
	static HRESULT Create(ECMsgStore *lpStore, ECMSLogon **lppECMSLogon);
	HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	HRESULT Logoff(ULONG *lpulFlags);
	HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result);
	HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	HRESULT Unadvise(ULONG ulConnection);
	HRESULT OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPVOID *lppEntry);
	ALLOC_WRAP_FRIEND;
};

#endif // ECMSGSTORE_H

