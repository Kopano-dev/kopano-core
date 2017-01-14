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

using namespace std;

class M4LMsgServiceAdmin;

struct providerEntry {
	MAPIUID uid;
	string servicename; // this provider belongs to service 'servicename'
	M4LProfSect *profilesection;
};

struct serviceEntry {
    MAPIUID muid;
    string servicename;
	string displayname;
	M4LProviderAdmin *provideradmin;
	bool bInitialize;
	SVCService* service;
};

struct profEntry {
    string profname;
    string password;
    M4LMsgServiceAdmin *serviceadmin;
};

class M4LProfAdmin _kc_final : public M4LUnknown, public IProfAdmin {
private:
    // variables
    list<profEntry*> profiles;
	std::recursive_mutex m_mutexProfiles;

    // functions
    list<profEntry*>::iterator findProfile(LPTSTR lpszProfileName);

public:
    virtual ~M4LProfAdmin();

    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError);
    virtual HRESULT __stdcall GetProfileTable(ULONG ulFlags, LPMAPITABLE* lppTable);
    virtual HRESULT __stdcall CreateProfile(LPTSTR lpszProfileName, LPTSTR lpszPassword, ULONG ulUIParam, ULONG ulFlags);
    virtual HRESULT __stdcall DeleteProfile(LPTSTR lpszProfileName, ULONG ulFlags);
    virtual HRESULT __stdcall ChangeProfilePassword(LPTSTR lpszProfileName, LPTSTR lpszOldPassword, LPTSTR lpszNewPassword, ULONG ulFlags);
    virtual HRESULT __stdcall CopyProfile(LPTSTR lpszOldProfileName, LPTSTR lpszOldPassword, LPTSTR lpszNewProfileName, ULONG ulUIParam,
				ULONG ulFlags);
    virtual HRESULT __stdcall RenameProfile(LPTSTR lpszOldProfileName, LPTSTR lpszOldPassword, LPTSTR lpszNewProfileName, ULONG ulUIParam,
				  ULONG ulFlags);
    virtual HRESULT __stdcall SetDefaultProfile(LPTSTR lpszProfileName, ULONG ulFlags);
    virtual HRESULT __stdcall AdminServices(LPTSTR lpszProfileName, LPTSTR lpszPassword, ULONG ulUIParam, ULONG ulFlags,
				  LPSERVICEADMIN* lppServiceAdmin);

    // iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;
};

class M4LMsgServiceAdmin _kc_final : public M4LUnknown, public IMsgServiceAdmin {
private:

	list<providerEntry*> providers;
    list<serviceEntry*> services;

	M4LProfSect	*profilesection;  // Global Profile Section
	std::recursive_mutex m_mutexserviceadmin;

    // functions
    serviceEntry* findServiceAdmin(LPTSTR lpszServiceName);
    serviceEntry* findServiceAdmin(LPMAPIUID lpMUID);
	providerEntry* findProvider(LPMAPIUID lpUid);

public:
    M4LMsgServiceAdmin(M4LProfSect *profilesection);
    virtual ~M4LMsgServiceAdmin();

    virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError);
    virtual HRESULT __stdcall GetMsgServiceTable(ULONG ulFlags, LPMAPITABLE* lppTable);
    virtual HRESULT __stdcall CreateMsgService(LPTSTR lpszService, LPTSTR lpszDisplayName, ULONG ulUIParam, ULONG ulFlags);
    virtual HRESULT __stdcall DeleteMsgService(LPMAPIUID lpUID);
    virtual HRESULT __stdcall CopyMsgService(LPMAPIUID lpUID, LPTSTR lpszDisplayName, LPCIID lpInterfaceToCopy, LPCIID lpInterfaceDst,
								   LPVOID lpObjectDst, ULONG ulUIParam, ULONG ulFlags);
    virtual HRESULT __stdcall RenameMsgService(LPMAPIUID lpUID, ULONG ulFlags, LPTSTR lpszDisplayName);
    virtual HRESULT __stdcall ConfigureMsgService(LPMAPIUID lpUID, ULONG ulUIParam, ULONG ulFlags, ULONG cValues, LPSPropValue lpProps);
    virtual HRESULT __stdcall OpenProfileSection(LPMAPIUID lpUID, LPCIID lpInterface, ULONG ulFlags, LPPROFSECT* lppProfSect);
    virtual HRESULT __stdcall MsgServiceTransportOrder(ULONG cUID, LPMAPIUID lpUIDList, ULONG ulFlags);
    virtual HRESULT __stdcall AdminProviders(LPMAPIUID lpUID, ULONG ulFlags, LPPROVIDERADMIN* lppProviderAdmin);
    virtual HRESULT __stdcall SetPrimaryIdentity(LPMAPIUID lpUID, ULONG ulFlags);
    virtual HRESULT __stdcall GetProviderTable(ULONG ulFlags, LPMAPITABLE* lppTable);

    // iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
	virtual HRESULT __stdcall QueryInterface(REFIID refiid, void **lpvoid) _kc_override;

	friend class M4LProviderAdmin;
	friend class M4LMAPISession;
};

inline bool operator <(const GUID &a, const GUID &b) {
    return memcmp(&a, &b, sizeof(GUID)) < 0;
}

class M4LMAPISession _kc_final : public M4LUnknown, public IMAPISession {
private:
	// variables
	string profileName;
	M4LMsgServiceAdmin *serviceAdmin;

public:
	M4LMAPISession(LPTSTR new_profileName, M4LMsgServiceAdmin *new_serviceAdmin);
	virtual ~M4LMAPISession();

	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR* lppMAPIError);
	virtual HRESULT __stdcall GetMsgStoresTable(ULONG ulFlags, LPMAPITABLE* lppTable);
	virtual HRESULT __stdcall OpenMsgStore(ULONG ulUIParam, ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags,
								 LPMDB* lppMDB);
	virtual HRESULT __stdcall OpenAddressBook(ULONG ulUIParam, LPCIID lpInterface, ULONG ulFlags, LPADRBOOK* lppAdrBook);
	virtual HRESULT __stdcall OpenProfileSection(LPMAPIUID lpUID, LPCIID lpInterface, ULONG ulFlags, LPPROFSECT* lppProfSect);
	virtual HRESULT __stdcall GetStatusTable(ULONG ulFlags, LPMAPITABLE* lppTable);
	virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG* lpulObjType,
							  LPUNKNOWN* lppUnk);
	virtual HRESULT __stdcall CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags,
									ULONG* lpulResult);
	virtual HRESULT __stdcall Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink,
						   ULONG* lpulConnection);
	virtual HRESULT __stdcall Unadvise(ULONG ulConnection);
	virtual HRESULT __stdcall MessageOptions(ULONG ulUIParam, ULONG ulFlags, LPTSTR lpszAdrType, LPMESSAGE lpMessage);
	virtual HRESULT __stdcall QueryDefaultMessageOpt(LPTSTR lpszAdrType, ULONG ulFlags, ULONG* lpcValues, LPSPropValue* lppOptions);
	virtual HRESULT __stdcall EnumAdrTypes(ULONG ulFlags, ULONG* lpcAdrTypes, LPTSTR** lpppszAdrTypes);
	virtual HRESULT __stdcall QueryIdentity(ULONG* lpcbEntryID, LPENTRYID* lppEntryID);
	virtual HRESULT __stdcall Logoff(ULONG ulUIParam, ULONG ulFlags, ULONG ulReserved);
	virtual HRESULT __stdcall SetDefaultStore(ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT __stdcall AdminServices(ULONG ulFlags, LPSERVICEADMIN* lppServiceAdmin);
	virtual HRESULT __stdcall ShowForm(ULONG ulUIParam, LPMDB lpMsgStore, LPMAPIFOLDER lpParentFolder, LPCIID lpInterface, ULONG ulMessageToken,
							 LPMESSAGE lpMessageSent, ULONG ulFlags, ULONG ulMessageStatus, ULONG ulMessageFlags, ULONG ulAccess,
							 LPSTR lpszMessageClass);
	virtual HRESULT __stdcall PrepareForm(LPCIID lpInterface, LPMESSAGE lpMessage, ULONG* lpulMessageToken);

    // iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
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
	virtual HRESULT __stdcall NewEntry(ULONG ulUIParam, ULONG ulFlags, ULONG cbEIDContainer, LPENTRYID lpEIDContainer,
									   ULONG cbEIDNewEntryTpl, LPENTRYID lpEIDNewEntryTpl, ULONG* lpcbEIDNewEntry,
									   LPENTRYID* lppEIDNewEntry);
	virtual HRESULT __stdcall ResolveName(ULONG ulUIParam, ULONG ulFlags, LPTSTR lpszNewEntryTitle, LPADRLIST lpAdrList);
	virtual HRESULT __stdcall Address(ULONG* lpulUIParam, LPADRPARM lpAdrParms, LPADRLIST* lppAdrList);
	virtual HRESULT __stdcall Details(ULONG* lpulUIParam, LPFNDISMISS lpfnDismiss, LPVOID lpvDismissContext, ULONG cbEntryID,
									  LPENTRYID lpEntryID, LPFNBUTTON lpfButtonCallback, LPVOID lpvButtonContext,
									  LPTSTR lpszButtonText, ULONG ulFlags);
	virtual HRESULT __stdcall RecipOptions(ULONG ulUIParam, ULONG ulFlags, LPADRENTRY lpRecip);
	virtual HRESULT __stdcall QueryDefaultRecipOpt(LPTSTR lpszAdrType, ULONG ulFlags, ULONG* lpcValues, LPSPropValue* lppOptions);
	virtual HRESULT __stdcall GetPAB(ULONG* lpcbEntryID, LPENTRYID* lppEntryID);
	virtual HRESULT __stdcall SetPAB(ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT __stdcall GetDefaultDir(ULONG* lpcbEntryID, LPENTRYID* lppEntryID);
	virtual HRESULT __stdcall SetDefaultDir(ULONG cbEntryID, LPENTRYID lpEntryID);
	virtual HRESULT __stdcall GetSearchPath(ULONG ulFlags, LPSRowSet* lppSearchPath);
	virtual HRESULT __stdcall SetSearchPath(ULONG ulFlags, LPSRowSet lpSearchPath);
	virtual HRESULT __stdcall PrepareRecips(ULONG ulFlags, const SPropTagArray *lpPropTagArray, LPADRLIST lpRecipList);

	// imapiprop passthru
	virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError) _kc_override;
	virtual HRESULT __stdcall SaveChanges(ULONG ulFlags) _kc_override;
	virtual HRESULT __stdcall GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags, ULONG *lpcValues, LPSPropValue *lppPropArray) _kc_override;
	virtual HRESULT __stdcall GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray) _kc_override;
	virtual HRESULT __stdcall OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk) _kc_override;
	virtual HRESULT __stdcall SetProps(ULONG cValues, const SPropValue *lpPropArray, LPSPropProblemArray *lppProblems) _kc_override;
	virtual HRESULT __stdcall DeleteProps(const SPropTagArray *lpPropTagArray, LPSPropProblemArray *lppProblems) _kc_override;
	virtual HRESULT __stdcall CopyTo(ULONG ciidExclude, LPCIID rgiidExclude, const SPropTagArray *lpExcludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _kc_override;
	virtual HRESULT __stdcall CopyProps(const SPropTagArray *lpIncludeProps, ULONG ulUIParam, LPMAPIPROGRESS lpProgress, LPCIID lpInterface, LPVOID lpDestObj, ULONG ulFlags, LPSPropProblemArray *lppProblems) _kc_override;
	virtual HRESULT __stdcall GetNamesFromIDs(LPSPropTagArray *lppPropTags, LPGUID lpPropSetGuid, ULONG ulFlags, ULONG *lpcPropNames, LPMAPINAMEID **lpppPropNames) _kc_override;
	virtual HRESULT __stdcall GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID *lppPropNames, ULONG ulFlags, LPSPropTagArray *lppPropTags) _kc_override;

	// iunknown passthru
	virtual ULONG __stdcall AddRef(void) _kc_override;
	virtual ULONG __stdcall Release(void) _kc_override;
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
