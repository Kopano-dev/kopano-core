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
	KCHL::object_ptr<M4LProfSect> profilesection;
};

struct serviceEntry {
    MAPIUID muid;
	std::string servicename, displayname;
	KCHL::object_ptr<M4LProviderAdmin> provideradmin;
	bool bInitialize;
	SVCService* service;
};

struct profEntry {
	std::string profname, password;
	KCHL::object_ptr<M4LMsgServiceAdmin> serviceadmin;
};

class M4LProfAdmin _kc_final : public M4LUnknown, public IProfAdmin {
private:
    // variables
	std::list<std::unique_ptr<profEntry> > profiles;
	std::recursive_mutex m_mutexProfiles;

    // functions
    decltype(profiles)::iterator findProfile(const TCHAR *name);

public:
    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError);
    virtual HRESULT __stdcall GetProfileTable(ULONG ulFlags, LPMAPITABLE* lppTable);
	virtual HRESULT __stdcall CreateProfile(const TCHAR *name, const TCHAR *password, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT __stdcall DeleteProfile(const TCHAR *name, ULONG flags);
	virtual HRESULT __stdcall ChangeProfilePassword(const TCHAR *name, const TCHAR *oldpw, const TCHAR *newpw, ULONG flags);
	virtual HRESULT __stdcall CopyProfile(const TCHAR *oldname, const TCHAR *oldpw, const TCHAR *newpw, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT __stdcall RenameProfile(const TCHAR *oldname, const TCHAR *oldpw, const TCHAR *newname, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT __stdcall SetDefaultProfile(const TCHAR *name, ULONG flags);
	virtual HRESULT __stdcall AdminServices(const TCHAR *name, const TCHAR *password, ULONG_PTR ui_param, ULONG flags, IMsgServiceAdmin **);
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

	friend class KC::SessionRestorer;
};

class M4LMsgServiceAdmin _kc_final : public M4LUnknown, public IMsgServiceAdmin2 {
private:
	std::list<std::unique_ptr<providerEntry> > providers;
	std::list<std::unique_ptr<serviceEntry> > services;
	KCHL::object_ptr<M4LProfSect> profilesection; // Global Profile Section
	std::recursive_mutex m_mutexserviceadmin;

    // functions
	serviceEntry *findServiceAdmin(const TCHAR *name);
	serviceEntry *findServiceAdmin(const MAPIUID *id);
	providerEntry *findProvider(const MAPIUID *id);

public:
    M4LMsgServiceAdmin(M4LProfSect *profilesection);
    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError);
    virtual HRESULT __stdcall GetMsgServiceTable(ULONG ulFlags, LPMAPITABLE* lppTable);
	virtual HRESULT __stdcall CreateMsgService(const TCHAR *service, const TCHAR *display_name, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT __stdcall CreateMsgServiceEx(const char *service, const char *display_name, ULONG_PTR ui_param, ULONG flags, MAPIUID *out);
	virtual HRESULT __stdcall DeleteMsgService(const MAPIUID *uid);
	virtual HRESULT __stdcall CopyMsgService(const MAPIUID *uid, const TCHAR *display_name, const IID *ifsrc, const IID *ifdst, void *obj_dst, ULONG_PTR ui_param, ULONG flags);
	virtual HRESULT __stdcall RenameMsgService(const MAPIUID *uid, ULONG flags, const TCHAR *display_name);
	virtual HRESULT __stdcall ConfigureMsgService(const MAPIUID *uid, ULONG_PTR ui_param, ULONG flags, ULONG nvals, const SPropValue *props);
	virtual HRESULT __stdcall OpenProfileSection(const MAPIUID *uid, const IID *intf, ULONG flags, IProfSect **);
	virtual HRESULT __stdcall MsgServiceTransportOrder(ULONG nuids, const MAPIUID *uids, ULONG flags);
	virtual HRESULT __stdcall AdminProviders(const MAPIUID *uid, ULONG flags, IProviderAdmin **);
	virtual HRESULT __stdcall SetPrimaryIdentity(const MAPIUID *uid, ULONG flags);
    virtual HRESULT __stdcall GetProviderTable(ULONG ulFlags, LPMAPITABLE* lppTable);
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

	friend class M4LProviderAdmin;
	friend class M4LMAPISession;
	friend class KC::SessionRestorer;
};

inline bool operator <(const GUID &a, const GUID &b) {
    return memcmp(&a, &b, sizeof(GUID)) < 0;
}

class M4LMAPISession _kc_final : public M4LUnknown, public IMAPISession {
private:
	// variables
	std::string profileName;
	KCHL::object_ptr<M4LMsgServiceAdmin> serviceAdmin;

public:
	M4LMAPISession(const TCHAR *profname, M4LMsgServiceAdmin *);
	virtual ~M4LMAPISession();

	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError);
	virtual HRESULT __stdcall GetMsgStoresTable(ULONG ulFlags, LPMAPITABLE* lppTable);
	virtual HRESULT __stdcall OpenMsgStore(ULONG_PTR ulUIParam, ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, LPMDB *lppMDB);
	virtual HRESULT __stdcall OpenAddressBook(ULONG_PTR ulUIParam, LPCIID lpInterface, ULONG ulFlags, LPADRBOOK *lppAdrBook);
	virtual HRESULT __stdcall OpenProfileSection(const MAPIUID *uid, const IID *intf, ULONG flags, IProfSect **);
	virtual HRESULT __stdcall GetStatusTable(ULONG ulFlags, LPMAPITABLE* lppTable);
	virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* lpulObjType,
							  LPUNKNOWN* lppUnk);
	virtual HRESULT __stdcall CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags,
									ULONG* lpulResult);
	virtual HRESULT __stdcall Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink,
						   ULONG* lpulConnection);
	virtual HRESULT __stdcall Unadvise(ULONG ulConnection);
	virtual HRESULT __stdcall MessageOptions(ULONG_PTR ulUIParam, ULONG ulFlags, LPTSTR lpszAdrType, LPMESSAGE lpMessage);
	virtual HRESULT __stdcall QueryDefaultMessageOpt(LPTSTR lpszAdrType, ULONG ulFlags, ULONG* lpcValues, LPSPropValue* lppOptions);
	virtual HRESULT __stdcall EnumAdrTypes(ULONG ulFlags, ULONG* lpcAdrTypes, LPTSTR** lpppszAdrTypes);
	virtual HRESULT __stdcall QueryIdentity(ULONG* lpcbEntryID, LPENTRYID* lppEntryID);
	virtual HRESULT __stdcall Logoff(ULONG_PTR ulUIParam, ULONG ulFlags, ULONG ulReserved);
	virtual HRESULT __stdcall SetDefaultStore(ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT __stdcall AdminServices(ULONG ulFlags, LPSERVICEADMIN* lppServiceAdmin);
	virtual HRESULT __stdcall ShowForm(ULONG_PTR ulUIParam, LPMDB lpMsgStore, LPMAPIFOLDER lpParentFolder, LPCIID lpInterface, ULONG ulMessageToken, LPMESSAGE lpMessageSent, ULONG ulFlags, ULONG ulMessageStatus, ULONG ulMessageFlags, ULONG ulAccess, LPSTR lpszMessageClass);
	virtual HRESULT __stdcall PrepareForm(LPCIID lpInterface, LPMESSAGE lpMessage, ULONG* lpulMessageToken);
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

private:
    std::map<GUID, IMsgStore *> mapStores;
	/* @todo need a status row per provider */
	ULONG m_cValuesStatus = 0;
	SPropValue *m_lpPropsStatus = nullptr;
	std::mutex m_mutexStatusRow;

public:
	HRESULT __stdcall setStatusRow(ULONG cValues, LPSPropValue lpProps);
};

class M4LAddrBook _kc_final : public M4LMAPIProp, public IAddrBook {
public:
	M4LAddrBook(M4LMsgServiceAdmin *new_serviceAdmin, LPMAPISUP newlpMAPISup);
	virtual ~M4LAddrBook();

	virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) _kc_override;
	virtual HRESULT __stdcall CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult) _kc_override;
	virtual HRESULT __stdcall Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection) _kc_override;
	virtual HRESULT __stdcall Unadvise(ULONG ulConnection);
	virtual HRESULT __stdcall CreateOneOff(LPTSTR lpszName, LPTSTR lpszAdrType, LPTSTR lpszAddress, ULONG ulFlags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID) _kc_override;
	virtual HRESULT __stdcall NewEntry(ULONG_PTR ulUIParam, ULONG ulFlags, ULONG cbEIDContainer, LPENTRYID lpEIDContainer, ULONG cbEIDNewEntryTpl, LPENTRYID lpEIDNewEntryTpl, ULONG *lpcbEIDNewEntry, LPENTRYID *lppEIDNewEntry);
	virtual HRESULT __stdcall ResolveName(ULONG_PTR ulUIParam, ULONG ulFlags, LPTSTR lpszNewEntryTitle, LPADRLIST lpAdrList);
	virtual HRESULT __stdcall Address(ULONG_PTR *lpulUIParam, LPADRPARM lpAdrParms, LPADRLIST *lppAdrList);
	virtual HRESULT __stdcall Details(ULONG* lpulUIParam, LPFNDISMISS lpfnDismiss, LPVOID lpvDismissContext, ULONG cbEntryID,
									  LPENTRYID lpEntryID, LPFNBUTTON lpfButtonCallback, LPVOID lpvButtonContext,
									  LPTSTR lpszButtonText, ULONG ulFlags);
	virtual HRESULT __stdcall RecipOptions(ULONG_PTR ulUIParam, ULONG ulFlags, LPADRENTRY lpRecip);
	virtual HRESULT __stdcall QueryDefaultRecipOpt(LPTSTR lpszAdrType, ULONG ulFlags, ULONG* lpcValues, LPSPropValue* lppOptions);
	virtual HRESULT __stdcall GetPAB(ULONG* lpcbEntryID, LPENTRYID* lppEntryID);
	virtual HRESULT __stdcall SetPAB(ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT __stdcall GetDefaultDir(ULONG* lpcbEntryID, LPENTRYID* lppEntryID);
	virtual HRESULT __stdcall SetDefaultDir(ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT __stdcall GetSearchPath(ULONG ulFlags, LPSRowSet* lppSearchPath);
	virtual HRESULT __stdcall SetSearchPath(ULONG ulFlags, LPSRowSet lpSearchPath);
	virtual HRESULT __stdcall PrepareRecips(ULONG ulFlags, const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList);
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

private:
	// variables
	LPMAPISUP m_lpMAPISup;

	std::list<abEntry> m_lABProviders;
	SRowSet *m_lpSavedSearchPath = nullptr;
	HRESULT getDefaultSearchPath(ULONG ulFlags, LPSRowSet* lppSearchPath);

public:
	HRESULT __stdcall addProvider(const std::string &profilename, const std::string &displayname, LPMAPIUID lpUID, LPABPROVIDER newProvider);
};

extern ECConfig *m4l_lpConfig;

#endif
