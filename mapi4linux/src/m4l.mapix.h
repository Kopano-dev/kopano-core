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

#ifndef __M4L_MAPIX_IMPL_H
#define __M4L_MAPIX_IMPL_H

#include <kopano/zcdefs.h>
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

class M4LProfAdmin _kc_final : public M4LUnknown, public IProfAdmin {
private:
    // variables
	std::list<std::unique_ptr<profEntry> > profiles;
	std::recursive_mutex m_mutexProfiles;

    // functions
    decltype(profiles)::iterator findProfile(const TCHAR *name);

public:
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT GetProfileTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT CreateProfile(const TCHAR *name, const TCHAR *password, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT DeleteProfile(const TCHAR *name, ULONG flags);
	virtual HRESULT ChangeProfilePassword(const TCHAR *name, const TCHAR *oldpw, const TCHAR *newpw, ULONG flags);
	virtual HRESULT CopyProfile(const TCHAR *oldname, const TCHAR *oldpw, const TCHAR *newpw, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT RenameProfile(const TCHAR *oldname, const TCHAR *oldpw, const TCHAR *newname, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT SetDefaultProfile(const TCHAR *name, ULONG flags);
	virtual HRESULT AdminServices(const TCHAR *name, const TCHAR *password, ULONG_PTR ui_param, ULONG flags, IMsgServiceAdmin **);
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

	friend class KC::SessionRestorer;
};

class M4LMsgServiceAdmin _kc_final : public M4LUnknown, public IMsgServiceAdmin2 {
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
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT GetMsgServiceTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT CreateMsgService(const TCHAR *service, const TCHAR *display_name, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT CreateMsgServiceEx(const char *service, const char *display_name, ULONG_PTR ui_param, ULONG flags, MAPIUID *out);
	virtual HRESULT DeleteMsgService(const MAPIUID *uid);
	virtual HRESULT CopyMsgService(const MAPIUID *uid, const TCHAR *display_name, const IID *ifsrc, const IID *ifdst, void *obj_dst, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT RenameMsgService(const MAPIUID *uid, ULONG flags, const TCHAR *display_name);
	virtual HRESULT ConfigureMsgService(const MAPIUID *uid, ULONG_PTR ui_param, ULONG flags, ULONG nvals, const SPropValue *props);
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, const IID *intf, ULONG flags, IProfSect **);
	virtual HRESULT MsgServiceTransportOrder(ULONG nuids, const MAPIUID *uids, ULONG flags);
	virtual HRESULT AdminProviders(const MAPIUID *uid, ULONG flags, IProviderAdmin **);
	virtual HRESULT SetPrimaryIdentity(const MAPIUID *uid, ULONG flags);
	virtual HRESULT GetProviderTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

	friend class M4LProviderAdmin;
	friend class M4LMAPISession;
	friend class KC::SessionRestorer;
};

inline bool operator<(const GUID &a, const GUID &b) noexcept
{
    return memcmp(&a, &b, sizeof(GUID)) < 0;
}

class M4LMAPISession _kc_final : public M4LUnknown, public IMAPISession {
private:
	// variables
	std::string profileName;
	KC::object_ptr<M4LMsgServiceAdmin> serviceAdmin;

public:
	M4LMAPISession(const TCHAR *profname, M4LMsgServiceAdmin *);
	virtual HRESULT GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError);
	virtual HRESULT GetMsgStoresTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT OpenMsgStore(ULONG_PTR ui_param, ULONG eid_size, const ENTRYID *, const IID *intf, ULONG flags, IMsgStore **) override;
	virtual HRESULT OpenAddressBook(ULONG_PTR ulUIParam, LPCIID lpInterface, ULONG ulFlags, LPADRBOOK *lppAdrBook);
	virtual HRESULT OpenProfileSection(const MAPIUID *uid, const IID *intf, ULONG flags, IProfSect **);
	virtual HRESULT GetStatusTable(ULONG ulFlags, LPMAPITABLE *lppTable);
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result);
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *eid, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn_id) override;
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT MessageOptions(ULONG_PTR ui_param, ULONG flags, const TCHAR *addrtype, IMessage *) override;
	virtual HRESULT QueryDefaultMessageOpt(const TCHAR *addrtype, ULONG flags, ULONG *nvals, SPropValue **opts) override;
	virtual HRESULT EnumAdrTypes(ULONG flags, ULONG *ntyps, TCHAR ***) override;
	virtual HRESULT QueryIdentity(ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	virtual HRESULT Logoff(ULONG_PTR ulUIParam, ULONG ulFlags, ULONG ulReserved);
	virtual HRESULT SetDefaultStore(ULONG flags, ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT AdminServices(ULONG ulFlags, LPSERVICEADMIN *lppServiceAdmin);
	virtual HRESULT ShowForm(ULONG_PTR ulUIParam, LPMDB lpMsgStore, LPMAPIFOLDER lpParentFolder, LPCIID lpInterface, ULONG ulMessageToken, LPMESSAGE lpMessageSent, ULONG ulFlags, ULONG ulMessageStatus, ULONG ulMessageFlags, ULONG ulAccess, LPSTR lpszMessageClass);
	virtual HRESULT PrepareForm(LPCIID lpInterface, LPMESSAGE lpMessage, ULONG *lpulMessageToken);
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

private:
	std::map<GUID, KC::object_ptr<IMsgStore>> mapStores;
	/* @todo need a status row per provider */
	ULONG m_cValuesStatus = 0;
	KC::memory_ptr<SPropValue> m_lpPropsStatus;
	std::mutex m_mutexStatusRow;

public:
	HRESULT setStatusRow(ULONG cValues, LPSPropValue lpProps);
};

class M4LAddrBook _kc_final : public M4LMAPIProp, public IAddrBook {
public:
	M4LAddrBook(M4LMsgServiceAdmin *new_serviceAdmin, LPMAPISUP newlpMAPISup);
	virtual ~M4LAddrBook();
	virtual HRESULT OpenEntry(ULONG eid_size, const ENTRYID *eid, const IID *intf, ULONG flags, ULONG *obj_type, IUnknown **);
	virtual HRESULT CompareEntryIDs(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b, ULONG cmp_flags, ULONG *result);
	virtual HRESULT Advise(ULONG eid_size, const ENTRYID *, ULONG evt_mask, IMAPIAdviseSink *, ULONG *conn) override;
	virtual HRESULT Unadvise(ULONG ulConnection);
	virtual HRESULT CreateOneOff(const TCHAR *name, const TCHAR *addrtype, const TCHAR *addr, ULONG flags, ULONG *eid_size, ENTRYID **) override;
	virtual HRESULT NewEntry(ULONG_PTR ui_param, ULONG flags, ULONG eid_size, const ENTRYID *eid_cont, ULONG tpl_size, const ENTRYID *tpl, ULONG *new_size, ENTRYID **new_eid) override;
	virtual HRESULT ResolveName(ULONG_PTR ulUIParam, ULONG ulFlags, LPTSTR lpszNewEntryTitle, LPADRLIST lpAdrList);
	virtual HRESULT Address(ULONG_PTR *lpulUIParam, LPADRPARM lpAdrParms, LPADRLIST *lppAdrList);
	virtual HRESULT Details(ULONG *lpulUIParam, LPFNDISMISS lpfnDismiss, LPVOID lpvDismissContext, ULONG cbEntryID, LPENTRYID lpEntryID, LPFNBUTTON lpfButtonCallback, LPVOID lpvButtonContext, LPTSTR lpszButtonText, ULONG ulFlags);
	virtual HRESULT RecipOptions(ULONG_PTR ulUIParam, ULONG ulFlags, LPADRENTRY lpRecip);
	virtual HRESULT QueryDefaultRecipOpt(const TCHAR *addrtype, ULONG flags, ULONG *nvals, SPropValue **opts) override;
	virtual HRESULT GetPAB(ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	virtual HRESULT SetPAB(ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT GetDefaultDir(ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	virtual HRESULT SetDefaultDir(ULONG eid_size, const ENTRYID *) override;
	virtual HRESULT GetSearchPath(ULONG ulFlags, LPSRowSet *lppSearchPath);
	virtual HRESULT SetSearchPath(ULONG ulFlags, LPSRowSet lpSearchPath);
	virtual HRESULT PrepareRecips(ULONG ulFlags, const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList);
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

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
