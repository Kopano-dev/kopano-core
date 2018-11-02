/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECMSGSTORE_H
#define ECMSGSTORE_H

#include <memory>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <mapispi.h>
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

class ECMessage;
class ECMAPIFolder;

class IMessageFactory {
public:
	virtual HRESULT Create(ECMsgStore *, BOOL fnew, BOOL modify, ULONG flags, BOOL embedded, const ECMAPIProp *root, ECMessage **) const = 0;
};

class ECMsgStore :
    public ECMAPIProp, public IMsgStore, public IExchangeManageStore,
    public KC::IECServiceAdmin, public IProxyStoreObject,
    public KC::IECSpooler {
protected:
	ECMsgStore(const char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore);
	virtual ~ECMsgStore();
	static HRESULT GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase);
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);

public:
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT QueryInterfaceProxy(REFIID refiid, void **lppInterface);
	static HRESULT Create(const char *lpszProfname, LPMAPISUP lpSupport, WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags, BOOL bIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore, ECMsgStore **lppECMsgStore);
	virtual HRESULT SaveChanges(ULONG flags) override;
	virtual HRESULT SetProps(ULONG nvals, const SPropValue *, SPropProblemArray **) override;
	virtual HRESULT DeleteProps(const SPropTagArray *, SPropProblemArray **) override;
	virtual HRESULT OpenProperty(ULONG proptag, const IID *intf, ULONG iface_opts, ULONG flags, IUnknown **) override;
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(ULONG conn) override;
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) override;
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) override;
	virtual HRESULT SetReceiveFolder(const TCHAR *cls, ULONG flags, ULONG eid_size, const ENTRYID *eid) override;
	virtual HRESULT GetReceiveFolder(const TCHAR *cls, ULONG flags, ULONG *eid_size, ENTRYID **eid, TCHAR **exp_class) override;
	virtual HRESULT GetReceiveFolderTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT StoreLogoff(ULONG *flags) override;
	virtual HRESULT AbortSubmit(ULONG eid_size, const ENTRYID *, ULONG flags) override;
	virtual HRESULT GetOutgoingQueue(ULONG flags, IMAPITable **) override;
	virtual HRESULT SetLockState(IMessage *, ULONG lock_state) override;
	virtual HRESULT FinishedMsg(ULONG flags, ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT NotifyNewMail(const NOTIFICATION *) override;
	virtual HRESULT CreateStoreEntryID(const TCHAR *store_dn, const TCHAR *mbox_dn, ULONG flags, ULONG *eid_size, ENTRYID **eid) override;
	virtual HRESULT EntryIDFromSourceKey(ULONG fk_size, BYTE *folder_sourcekey, ULONG mk_size, BYTE *msg_sourcekey, ULONG *eid_size, ENTRYID **) override;
	virtual HRESULT GetRights(ULONG ueid_size, const ENTRYID *user_eid, ULONG eid_size, const ENTRYID *eid, ULONG *rights) override;
	virtual HRESULT GetMailboxTable(const TCHAR *server, IMAPITable **, ULONG flags) override;
	virtual HRESULT GetPublicFolderTable(const TCHAR *server, IMAPITable **, ULONG flags) override;
	virtual HRESULT SetEntryId(ULONG eid_size, const ENTRYID *eid);
	virtual ULONG Release() override;

	// IECSpooler
	virtual HRESULT GetMasterOutgoingTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT DeleteFromMasterOutgoingTable(ULONG eid_size, const ENTRYID *eid, ULONG flags) override;

	// IECServiceAdmin
	virtual HRESULT CreateStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, ULONG *newstore_size, ENTRYID **newstore_eid, ULONG *root_size, ENTRYID **root_eid) override;
	virtual HRESULT CreateEmptyStore(ULONG store_type, ULONG user_size, const ENTRYID *user_eid, ULONG flags, ULONG *newstore_size, ENTRYID **newstore_eid, ULONG *root_size, ENTRYID **root_eid) override;
	virtual HRESULT ResolveStore(const GUID *, ULONG *user_id, ULONG *store_size, ENTRYID **store_eid) override;
	virtual HRESULT HookStore(ULONG store_type, ULONG eid_size, const ENTRYID *eid, const GUID *guid) override;
	virtual HRESULT UnhookStore(ULONG store_type, ULONG eid_size, const ENTRYID *eid) override;
	virtual HRESULT RemoveStore(const GUID *) override;
	virtual HRESULT CreateUser(KC::ECUSER *, ULONG flags, ULONG *ueid_size, ENTRYID **user_eid) override;
	virtual HRESULT DeleteUser(ULONG ueid_size, const ENTRYID *user_eid) override;
	virtual HRESULT SetUser(KC::ECUSER *, ULONG flags) override;
	virtual HRESULT GetUser(ULONG eid_size, const ENTRYID *user_eid, ULONG flags, KC::ECUSER **) override;
	virtual HRESULT ResolveUserName(const TCHAR *user, ULONG flags, ULONG *ueid_size, ENTRYID **user_eid) override;
	virtual HRESULT GetSendAsList(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *nsenders, KC::ECUSER **senders) override;
	virtual HRESULT AddSendAsUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG seid_size, const ENTRYID *sender_eid) override;
	virtual HRESULT DelSendAsUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG seid_size, const ENTRYID *sender_eid) override;
	virtual HRESULT RemoveAllObjects(ULONG ueid_size, const ENTRYID *user_eid) override;
	virtual HRESULT CreateGroup(KC::ECGROUP *, ULONG flags, ULONG *geid_eisze, ENTRYID **grp_eid) override;
	virtual HRESULT DeleteGroup(ULONG geid_size, const ENTRYID *grp_eid) override;
	virtual HRESULT SetGroup(KC::ECGROUP *, ULONG flags) override;
	virtual HRESULT GetGroup(ULONG grp_size, const ENTRYID *grp_eid, ULONG flags, KC::ECGROUP **) override;
	virtual HRESULT ResolveGroupName(const TCHAR *group, ULONG flags, ULONG *geid_size, ENTRYID **grp_eid) override;
	virtual HRESULT DeleteGroupUser(ULONG geid_size, const ENTRYID *grp_eid, ULONG ueid_size, const ENTRYID *user_eid) override;
	virtual HRESULT AddGroupUser(ULONG geid_size, const ENTRYID *grp_eid, ULONG ueid_size, const ENTRYID *user_eid) override;
	virtual HRESULT GetUserListOfGroup(ULONG geid_size, const ENTRYID *grp_eid, ULONG flags, ULONG *nusers, KC::ECUSER **) override;
	virtual HRESULT GetGroupListOfUser(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *ngrps, KC::ECGROUP **) override;
	virtual HRESULT CreateCompany(KC::ECCOMPANY *, ULONG flags, ULONG *ceid_size, ENTRYID **com_eid) override;
	virtual HRESULT DeleteCompany(ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT SetCompany(KC::ECCOMPANY *, ULONG flags) override;
	virtual HRESULT GetCompany(ULONG cmp_size, const ENTRYID *cmp_eid, ULONG flags, KC::ECCOMPANY **) override;
	virtual HRESULT ResolveCompanyName(const TCHAR *company, ULONG flags, ULONG *ceid_size, ENTRYID **com_eid) override;
	virtual HRESULT AddCompanyToRemoteViewList(ULONG sc_size, const ENTRYID *scom_eid, ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT DelCompanyFromRemoteViewList(ULONG sc_size, const ENTRYID *scom_eid, ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT GetRemoteViewList(ULONG ceid_size, const ENTRYID *com_eid, ULONG flags, ULONG *ncomps, KC::ECCOMPANY **) override;
	virtual HRESULT AddUserToRemoteAdminList(ULONG ueid_size, const ENTRYID *user_eid, ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT DelUserFromRemoteAdminList(ULONG ueid_size, const ENTRYID *user_eid, ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT GetRemoteAdminList(ULONG ceid_size, const ENTRYID *com_eid, ULONG flags, ULONG *nusers, KC::ECUSER **) override;
	virtual HRESULT SyncUsers(ULONG ceid_size, const ENTRYID *com_eid) override;
	virtual HRESULT GetQuota(ULONG ueid_size, const ENTRYID *user_eid, bool get_dfl, KC::ECQUOTA **) override;
	virtual HRESULT SetQuota(ULONG ueid_size, const ENTRYID *user_eid, KC::ECQUOTA *) override;
	virtual HRESULT AddQuotaRecipient(ULONG ceid_size, const ENTRYID *com_eid, ULONG reid_size, const ENTRYID *recip_eid, ULONG type) override;
	virtual HRESULT DeleteQuotaRecipient(ULONG ceid_size, const ENTRYID *com_eid, ULONG reid_size, const ENTRYID *recip_eid, ULONG type) override;
	virtual HRESULT GetQuotaRecipients(ULONG ueid_size, const ENTRYID *user_eid, ULONG flags, ULONG *nusers, KC::ECUSER **) override;
	virtual HRESULT GetQuotaStatus(ULONG ueid_size, const ENTRYID *user_eid, KC::ECQUOTASTATUS **) override;
	virtual HRESULT PurgeCache(ULONG flags) override;
	virtual HRESULT PurgeSoftDelete(ULONG days) override;
	virtual HRESULT PurgeDeferredUpdates(ULONG *remain) override;
	virtual HRESULT GetServerDetails(KC::ECSVRNAMELIST *, ULONG flags, KC::ECSERVERLIST **) override;
	virtual HRESULT OpenUserStoresTable(ULONG flags, IMAPITable **) override;
	virtual HRESULT ResolvePseudoUrl(const char *url, char **pathp, bool *ispeer) override;
	virtual HRESULT GetArchiveStoreEntryID(const TCHAR *user, const TCHAR *server, ULONG flags, ULONG *store_size, ENTRYID **store_eid) override;
	virtual HRESULT ResetFolderCount(ULONG eid_size, const ENTRYID *eid, ULONG *nupdates) override;
	virtual HRESULT UnwrapNoRef(void **obj) override;

    // ECTestProtocol
	virtual HRESULT TestPerform(const char *cmd, unsigned int argc, char **argv);
	virtual HRESULT TestSet(const char *name, const char *value);
	virtual HRESULT TestGet(const char *name, char **value);

	// Called when session is reloaded
	static HRESULT Reload(void *parm, KC::ECSESSIONID);

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
	HRESULT create_store_public(ECMsgStore *, IMAPIFolder *, IMAPIFolder *, const ENTRYID *, size_t);
	HRESULT create_store_private(ECMsgStore *, ECMAPIFolder *, IMAPIFolder *, IMAPIFolder *);
	HRESULT CreateSpecialFolder(LPMAPIFOLDER lpFolderParent, ECMAPIProp *lpFolderPropSet, const TCHAR *lpszFolderName, const TCHAR *lpszFolderComment, unsigned int ulPropTag, unsigned int ulMVPos, const TCHAR *lpszContainerClass, LPMAPIFOLDER *lppMAPIFolder);
	HRESULT SetSpecialEntryIdOnFolder(LPMAPIFOLDER lpFolder, ECMAPIProp *lpFolderPropSet, unsigned int ulPropTag, unsigned int ulMVPos);
	HRESULT OpenStatsTable(unsigned int ulTableType, LPMAPITABLE *lppTable);
	HRESULT CreateAdditionalFolder(IMAPIFolder *lpRootFolder, IMAPIFolder *lpInboxFolder, IMAPIFolder *lpSubTreeFolder, ULONG ulType, const TCHAR *lpszFolderName, const TCHAR *lpszComment, const TCHAR *lpszContainerType, bool fHidden);
	HRESULT AddRenAdditionalFolder(IMAPIFolder *lpFolder, ULONG ulType, SBinary *lpEntryID);
	static HRESULT MsgStoreDnToPseudoUrl(const KC::utf8string &store_dn, KC::utf8string *pseudo_url);

public:
	class xMsgStoreProxy final :
	    public IMsgStore,
	    public KC::IECTestProtocol {
		virtual ULONG AddRef() override;
		virtual ULONG Release() override;
		virtual HRESULT QueryInterface(const IID &, void **) override;
		virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
		virtual HRESULT Unadvise(unsigned int conn) override;
		virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) override;
		virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) override;
		virtual HRESULT SetReceiveFolder(const TCHAR *cls, ULONG flags, ULONG eid_size, const ENTRYID *eid) override;
		virtual HRESULT GetReceiveFolder(const TCHAR *cls, ULONG flags, ULONG *eid_size, ENTRYID **eid, TCHAR **exp_class) override;
		virtual HRESULT GetReceiveFolderTable(unsigned int flags, IMAPITable **) override;
		virtual HRESULT StoreLogoff(unsigned int *flags) override;
		virtual HRESULT AbortSubmit(ULONG eid_size, const ENTRYID *, ULONG flags) override;
		virtual HRESULT GetOutgoingQueue(unsigned int flags, IMAPITable **) override;
		virtual HRESULT SetLockState(IMessage *, unsigned int lock_state) override;
		virtual HRESULT FinishedMsg(ULONG flags, ULONG eid_size, const ENTRYID *) override;
		virtual HRESULT NotifyNewMail(const NOTIFICATION *) override;
		virtual HRESULT GetLastError(HRESULT error, unsigned int flags, MAPIERROR **) override;
		virtual HRESULT SaveChanges(unsigned int flags) override;
		virtual HRESULT GetProps(const SPropTagArray *, unsigned int flags, unsigned int *nprop, SPropValue **props) override;
		virtual HRESULT GetPropList(unsigned int flags, SPropTagArray **) override;
		virtual HRESULT OpenProperty(unsigned int tag, const IID *, unsigned int intf_opts, unsigned int flags, IUnknown **) override __attribute__((nonnull(3)));
		virtual HRESULT SetProps(unsigned int nprops, const SPropValue *props, SPropProblemArray **) override;
		virtual HRESULT DeleteProps(const SPropTagArray *, SPropProblemArray **) override;
		virtual HRESULT CopyTo(unsigned int nexcl, const IID *excliid, const SPropTagArray *exclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest_obj, unsigned int flags, SPropProblemArray **) override;
		virtual HRESULT CopyProps(const SPropTagArray *inclprop, ULONG ui_param, IMAPIProgress *, const IID *intf, void *dest_obj, unsigned int flags, SPropProblemArray **) override;
		virtual HRESULT GetNamesFromIDs(SPropTagArray **tags, const GUID *propset, ULONG flags, ULONG *nvals, MAPINAMEID ***names) override;
		virtual HRESULT GetIDsFromNames(unsigned int nelem, MAPINAMEID **, unsigned int flags, SPropTagArray **) override;
		virtual HRESULT TestPerform(const char *cmd, unsigned int argc, char **args) override;
		virtual HRESULT TestSet(const char *name, const char *value) override;
		virtual HRESULT TestGet(const char *name, char **value) override;
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

class ECMSLogon final : public KC::ECUnknown, public IMSLogon {
private:
	ECMSLogon(ECMsgStore *lpStore);
	ECMsgStore *m_lpStore;

public:
	static HRESULT Create(ECMsgStore *lpStore, ECMSLogon **lppECMSLogon);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	HRESULT GetLastError(HRESULT, ULONG flags, MAPIERROR **) override;
	HRESULT Logoff(ULONG *flags) override;
	HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **) override;
	HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result) override;
	HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	HRESULT Unadvise(ULONG conn) override;
	HRESULT OpenStatusEntry(const IID *intf, ULONG flags, ULONG *obj_type, void **entry) override;
	ALLOC_WRAP_FRIEND;
};

#endif // ECMSGSTORE_H
