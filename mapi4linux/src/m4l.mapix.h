/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef M4L_MAPIX_IMPL_H
#define M4L_MAPIX_IMPL_H

#include <memory>
#include <mutex>
#include "m4l.common.h"
#include "m4l.mapidefs.h"
#include "m4l.mapisvc.h"
#include <mapix.h>
#include <mapispi.h>
#include <string>
#include <list>
#include <map>
#include <kopano/ECConfig.h>
#include <kopano/memory.hpp>
#include <kopano/zcdefs.h>

class M4LMsgServiceAdmin;
namespace KC {
class SessionRestorer;
}

struct providerEntry {
	MAPIUID uid;
	std::string servicename; // this provider belongs to service 'servicename'
	KC::object_ptr<M4LProfSect> profilesection;
};

struct serviceEntry {
    MAPIUID muid;
	std::string servicename, displayname;
	KC::object_ptr<M4LProviderAdmin> provideradmin;
	bool bInitialize;
	SVCService* service;
};

struct profEntry {
	std::string profname, password;
	KC::object_ptr<M4LMsgServiceAdmin> serviceadmin;
};

class M4LProfAdmin final : public M4LUnknown, public IProfAdmin {
private:
    // variables
	std::list<std::unique_ptr<profEntry> > profiles;
	std::recursive_mutex m_mutexProfiles;

    // functions
    decltype(profiles)::iterator findProfile(const TCHAR *name);

public:
	virtual HRESULT GetLastError(HRESULT, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT GetProfileTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT CreateProfile(const TCHAR *name, const TCHAR *password, ULONG_PTR ui_param, unsigned int flags) override;
	virtual HRESULT DeleteProfile(const TCHAR *name, unsigned int flags) override;
	virtual HRESULT ChangeProfilePassword(const TCHAR *name, const TCHAR *oldpw, const TCHAR *newpw, unsigned int flags) override;
	virtual HRESULT CopyProfile(const TCHAR *oldname, const TCHAR *oldpw, const TCHAR *newpw, ULONG_PTR ui_param, unsigned int flags) override;
	virtual HRESULT RenameProfile(const TCHAR *oldname, const TCHAR *oldpw, const TCHAR *newname, ULONG_PTR ui_param, unsigned int flags) override;
	virtual HRESULT SetDefaultProfile(const TCHAR *name, unsigned int flags) override;
	virtual HRESULT AdminServices(const TCHAR *name, const TCHAR *password, ULONG_PTR ui_param, unsigned int flags, IMsgServiceAdmin **) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;

	friend class KC::SessionRestorer;
};

class M4LMsgServiceAdmin KC_FINAL_OPG :
    public M4LUnknown, public IMsgServiceAdmin2 {
private:
	std::list<std::unique_ptr<providerEntry> > providers;
	std::list<std::unique_ptr<serviceEntry> > services;
	KC::object_ptr<M4LProfSect> profilesection; // Global Profile Section
	/* guards content in service (and provider) list */
	std::recursive_mutex m_mutexserviceadmin;

    // functions
	serviceEntry *findServiceAdmin(const TCHAR *name);
	serviceEntry *findServiceAdmin(const MAPIUID *id);
	providerEntry *findProvider(const MAPIUID *id);

public:
    M4LMsgServiceAdmin(M4LProfSect *profilesection);
	~M4LMsgServiceAdmin();
	virtual HRESULT GetLastError(HRESULT, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT GetMsgServiceTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT CreateMsgService(const TCHAR *service, const TCHAR *display_name, ULONG_PTR ui_param, unsigned int flags) override;
	virtual HRESULT CreateMsgServiceEx(const char *service, const char *display_name, ULONG_PTR ui_param, unsigned int flags, MAPIUID *out) override;
	virtual HRESULT DeleteMsgService(const MAPIUID *uid) override;
	virtual HRESULT CopyMsgService(const MAPIUID *uid, const TCHAR *display_name, const IID *ifsrc, const IID *ifdst, void *obj_dst, ULONG_PTR ui_param, unsigned int flags) override;
	virtual HRESULT RenameMsgService(const MAPIUID *uid, unsigned int flags, const TCHAR *display_name) override;
	virtual HRESULT ConfigureMsgService(const MAPIUID *uid, ULONG_PTR ui_param, unsigned int flags, unsigned int nvals, const SPropValue *props) override;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, const IID *intf, unsigned int flags, IProfSect **) override;
	virtual HRESULT MsgServiceTransportOrder(unsigned int nuids, const MAPIUID *uids, unsigned int flags) override;
	virtual HRESULT AdminProviders(const MAPIUID *uid, unsigned int flags, IProviderAdmin **) override;
	virtual HRESULT SetPrimaryIdentity(const MAPIUID *uid, unsigned int flags) override;
	virtual HRESULT GetProviderTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;

	friend class M4LProviderAdmin;
	friend class M4LMAPISession;
	friend class KC::SessionRestorer;
};

inline bool operator<(const GUID &a, const GUID &b) noexcept
{
    return memcmp(&a, &b, sizeof(GUID)) < 0;
}

class M4LMAPISession KC_FINAL_OPG : public M4LUnknown, public IMAPISession {
private:
	// variables
	std::string profileName;
	KC::object_ptr<M4LMsgServiceAdmin> serviceAdmin;

public:
	M4LMAPISession(const TCHAR *profname, M4LMsgServiceAdmin *);
	virtual HRESULT GetLastError(HRESULT, unsigned int flags, MAPIERROR **) override;
	virtual HRESULT GetMsgStoresTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT OpenMsgStore(ULONG_PTR ui_param, ULONG eid_size, const ENTRYID *, const IID *intf, ULONG flags, IMsgStore **) override;
	virtual HRESULT OpenAddressBook(ULONG_PTR ui_param, const IID *intf, unsigned int flags, IAddrBook **) override;
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, const IID *intf, unsigned int flags, IProfSect **) override;
	virtual HRESULT GetStatusTable(unsigned int flags, IMAPITable **) override;
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT CompareEntryIDs(unsigned int asize, const ENTRYID *a, unsigned int bsize, const ENTRYID *b, unsigned int cmp_flags, unsigned int *result) override;
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *eid, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn_id) override;
	virtual HRESULT Unadvise(unsigned int conn) override;
	virtual HRESULT MessageOptions(ULONG_PTR ui_param, ULONG flags, const TCHAR *addrtype, IMessage *) override;
	virtual HRESULT QueryDefaultMessageOpt(const TCHAR *addrtype, ULONG flags, ULONG *nvals, SPropValue **opts) override;
	virtual HRESULT EnumAdrTypes(ULONG flags, ULONG *ntyps, TCHAR ***) override;
	virtual HRESULT QueryIdentity(unsigned int *eid_size, ENTRYID **) override;
	virtual HRESULT Logoff(ULONG_PTR ui_param, unsigned int flags, unsigned int unused) override;
	virtual HRESULT SetDefaultStore(ULONG flags, ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT AdminServices(unsigned int flags, IMsgServiceAdmin **) override;
	virtual HRESULT ShowForm(ULONG_PTR ui_param, IMsgStore *, IMAPIFolder *parent, const IID *intf, ULONG msg_token, IMessage *sesnt, ULONG flags, ULONG msg_status, ULONG msg_flags, ULONG access, const char *msg_class) override;
	virtual HRESULT PrepareForm(const IID *intf, IMessage *, unsigned int *token) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;

private:
	std::mutex m_storemap_mtx;
	std::map<GUID, KC::object_ptr<IMsgStore>> mapStores;
	/* @todo need a status row per provider */
	ULONG m_cValuesStatus = 0;
	KC::memory_ptr<SPropValue> m_lpPropsStatus;
	std::mutex m_mutexStatusRow;

public:
	HRESULT setStatusRow(ULONG nvals, const SPropValue *);
};

class M4LAddrBook KC_FINAL_OPG : public M4LMAPIProp, public IAddrBook {
public:
	M4LAddrBook(M4LMsgServiceAdmin *new_serviceAdmin, LPMAPISUP newlpMAPISup);
	virtual ~M4LAddrBook();
	virtual HRESULT OpenEntry(unsigned int eid_size, const ENTRYID *eid, const IID *intf, unsigned int flags, unsigned int *obj_type, IUnknown **) override;
	virtual HRESULT CompareEntryIDs(unsigned int asize, const ENTRYID *a, unsigned int bsize, const ENTRYID *b, unsigned int cmp_flags, unsigned int *result) override;
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(unsigned int conn) override;
	virtual HRESULT CreateOneOff(const TCHAR *name, const TCHAR *addrtype, const TCHAR *addr, ULONG flags, ULONG *eid_size, ENTRYID **) override;
	virtual HRESULT NewEntry(ULONG_PTR ui_param, ULONG flags, ULONG eid_size, const ENTRYID *eid_cont, ULONG tpl_size, const ENTRYID *tpl, ULONG *new_size, ENTRYID **new_eid) override;
	virtual HRESULT ResolveName(ULONG_PTR ui_param, ULONG flags, const TCHAR *new_title, ADRLIST *adrlist) override;
	virtual HRESULT Address(ULONG_PTR *ui_param, ADRPARM *, ADRLIST **) override;
	virtual HRESULT Details(ULONG_PTR *ui_param, DISMISSMODELESS *, void *dismiss_ctx, ULONG eid_size, const ENTRYID *, LPFNBUTTON callback, void *btn_ctx, const TCHAR *btn_text, ULONG flags) override;
	virtual HRESULT RecipOptions(ULONG_PTR ui_param, ULONG flags, const ADRENTRY *recip) override;
	virtual HRESULT QueryDefaultRecipOpt(const TCHAR *addrtype, ULONG flags, ULONG *nvals, SPropValue **opts) override;
	virtual HRESULT GetPAB(unsigned int *eid_size, ENTRYID **) override;
	virtual HRESULT SetPAB(ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT GetDefaultDir(unsigned int *eid_size, ENTRYID **) override;
	virtual HRESULT SetDefaultDir(ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT GetSearchPath(unsigned int flags, SRowSet **) override;
	virtual HRESULT SetSearchPath(ULONG flags, const SRowSet *) override;
	virtual HRESULT PrepareRecips(unsigned int flags, const SPropTagArray *, ADRLIST *recips) override;
	virtual HRESULT QueryInterface(const IID &, void **) override;

private:
	// variables
	KC::object_ptr<IMAPISupport> m_lpMAPISup;
	std::list<abEntry> m_lABProviders;
	SRowSet *m_lpSavedSearchPath = nullptr;
	HRESULT getDefaultSearchPath(ULONG ulFlags, LPSRowSet* lppSearchPath);

public:
	HRESULT addProvider(const std::string &profilename, const std::string &displayname, LPMAPIUID lpUID, LPABPROVIDER newProvider);
};

extern KC::ECConfig *m4l_lpConfig;

#endif
