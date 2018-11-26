/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <mapiutil.h>
#include <edkguid.h>
#include <list>
#include <new>
#include <string>
#include <vector>
#include <cstdint>
#include <kopano/ECGetText.h>
#include <kopano/Util.h>
#include "Mem.h"
#include "ECMessage.h"
#include "ECMsgStore.h"
#include "ECMAPITable.h"
#include "ECMAPIFolder.h"
#include "ECMAPIProp.h"
#include "WSTransport.h"
#include <kopano/ECTags.h>
#include <kopano/ECGuid.h>
#include <kopano/ECABEntryID.h>
#include <kopano/mapiext.h>
#include "freebusytags.h"
#include <kopano/CommonUtil.h>
#include "ClientUtil.h"
#include "pcutil.hpp"
#include "WSUtil.h" // used for UnWrapServerClientStoreEntry
#include "ECExportAddressbookChanges.h"
#include "ics.h"
#include "ECExchangeExportChanges.h"
#include "ECChangeAdvisor.h"
#include "ProviderUtil.h"
#include "EntryPoint.h"
#include <kopano/stringutil.h>
#include "ECExchangeModifyTable.h"
#include <kopano/mapi_ptr.h>
#include <kopano/charset/convstring.h>

#define N_ARGS(...) N_ARGS_HELPER1((__VA_ARGS__, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
#define N_ARGS_HELPER1(tuple) N_ARGS_HELPER2 tuple
#define N_ARGS_HELPER2(x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17, x18, x19, x20, n, ...) n
#define ARGLISTSET(...) ARGLISTSET_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define ARGLISTSET_HELPER1(n, ...) ARGLISTSET_HELPER2(n, (__VA_ARGS__))
#define ARGLISTSET_HELPER2(n, tuple) ARGLISTSET_HELPER3(n, tuple)
#define ARGLISTSET_HELPER3(n, tuple) ARGLISTSET_##n tuple
#define ARGLISTSET_1(v)
#define ARGLISTSET_2(t1, a1) t1 a1
#define ARGLISTSET_4(t1, a1, t2, a2) t1 a1, t2 a2
#define ARGLIST(...) ARGLIST_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define ARGLIST_HELPER1(n, ...) ARGLIST_HELPER2(n, (__VA_ARGS__))
#define ARGLIST_HELPER2(n, tuple) ARGLIST_HELPER3(n, tuple)
#define ARGLIST_HELPER3(n, tuple) ARGLIST_ ##n tuple
#define ARGLIST_1(s1) ARGLISTSET s1
#define ARGLIST_2(s1, s2) ARGLISTSET s1, ARGLISTSET s2
#define ARGLIST_3(s1, s2, s3) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3
#define ARGLIST_4(s1, s2, s3, s4) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4
#define ARGLIST_5(s1, s2, s3, s4, s5) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5
#define ARGLIST_6(s1, s2, s3, s4, s5, s6) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6
#define ARGLIST_7(s1, s2, s3, s4, s5, s6, s7) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7
#define ARGLIST_8(s1, s2, s3, s4, s5, s6, s7, s8) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7, ARGLISTSET s8
#define ARGLIST_9(s1, s2, s3, s4, s5, s6, s7, s8, s9) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7, ARGLISTSET s8, ARGLISTSET s9
#define ARGLIST_10(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7, ARGLISTSET s8, ARGLISTSET s9, ARGLISTSET s10
#define ARGLIST_11(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11) ARGLISTSET s1, ARGLISTSET s2, ARGLISTSET s3, ARGLISTSET s4, ARGLISTSET s5, ARGLISTSET s6, ARGLISTSET s7, ARGLISTSET s8, ARGLISTSET s9, ARGLISTSET s10, ARGLISTSET s11
#define ARGSSET(...) ARGSSET_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define ARGSSET_HELPER1(n, ...) ARGSSET_HELPER2(n, (__VA_ARGS__))
#define ARGSSET_HELPER2(n, tuple) ARGSSET_HELPER3(n, tuple)
#define ARGSSET_HELPER3(n, tuple) ARGSSET_##n tuple
#define ARGSSET_1(v)
#define ARGSSET_2(t1, a1) a1
#define ARGSSET_4(t1, a1, t2, a2) a1, a2
#define ARGS(...) ARGS_HELPER1(N_ARGS(__VA_ARGS__), __VA_ARGS__)
#define ARGS_HELPER1(n, ...) ARGS_HELPER2(n, (__VA_ARGS__))
#define ARGS_HELPER2(n, tuple) ARGS_HELPER3(n, tuple)
#define ARGS_HELPER3(n, tuple) ARGS_##n tuple
#define ARGS_1(s1) ARGSSET s1
#define ARGS_2(s1, s2) ARGSSET s1, ARGSSET s2
#define ARGS_3(s1, s2, s3) ARGSSET s1, ARGSSET s2, ARGSSET s3
#define ARGS_4(s1, s2, s3, s4) ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4
#define ARGS_5(s1, s2, s3, s4, s5) ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5
#define ARGS_6(s1, s2, s3, s4, s5, s6) ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6
#define ARGS_7(s1, s2, s3, s4, s5, s6, s7) ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7
#define ARGS_8(s1, s2, s3, s4, s5, s6, s7, s8) ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7, ARGSSET s8
#define ARGS_9(s1, s2, s3, s4, s5, s6, s7, s8, s9) ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7, ARGSSET s8, ARGSSET s9
#define ARGS_10(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10) ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7, ARGSSET s8, ARGSSET s9, ARGSSET s10
#define ARGS_11(s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11) ARGSSET s1, ARGSSET s2, ARGSSET s3, ARGSSET s4, ARGSSET s5, ARGSSET s6, ARGSSET s7, ARGSSET s8, ARGSSET s9, ARGSSET s10, ARGSSET s11
#define XCLASS(_iface) x ##_iface
#define CLASSMETHOD(_class, _method) _class::_method
#define METHODSTR_HELPER1(_method) METHODSTR_HELPER2(_method)
#define METHODSTR_HELPER2(_method) #_method
#define DEF_ULONGMETHOD1(_trace, _class, _iface, _method, ...) \
ULONG CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__)) \
{ \
	METHOD_PROLOGUE_(_class, _iface); \
	return pThis->_method(ARGS(__VA_ARGS__)); \
}

/* without exception passthrough */
#define DEF_HRMETHOD1(_trace, _class, _iface, _method, ...) \
HRESULT CLASSMETHOD(_class, CLASSMETHOD(XCLASS(_iface), _method))(ARGLIST(__VA_ARGS__)) \
{ \
	METHOD_PROLOGUE_(_class, _iface); \
	return pThis->_method(ARGS(__VA_ARGS__)); \
}

using namespace KC;
typedef object_ptr<WSTransport> WSTransportPtr;

// FIXME: from libserver/ECMAPI.h
#define MSGFLAG_DELETED                           ((ULONG) 0x00000400)

/**
 * ECMsgStore
 **/
ECMsgStore::ECMsgStore(const char *lpszProfname, IMAPISupport *sup,
    WSTransport *tp, BOOL modify, ULONG ulProfileFlags,
    BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore) :
	ECMAPIProp(nullptr, MAPI_STORE, modify, nullptr, "IMsgStore"),
	lpSupport(sup), lpTransport(tp), lpNamedProp(tp),
	m_ulProfileFlags(ulProfileFlags), m_fIsSpooler(fIsSpooler),
	m_fIsDefaultStore(fIsDefaultStore),
	m_strProfname((lpszProfname != nullptr) ? lpszProfname : "")
{
	// Add our property handlers
	HrAddPropHandlers(PR_ENTRYID, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_RECORD_KEY, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_SEARCH_KEY, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_USER_NAME, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_USER_ENTRYID, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_MAILBOX_OWNER_NAME, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_MAILBOX_OWNER_ENTRYID, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_USER_NAME, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_USER_ENTRYID, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_RECEIVE_FOLDER_SETTINGS, GetPropHandler, DefaultSetPropIgnore, this, false, false);
	HrAddPropHandlers(PR_MESSAGE_SIZE, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_MESSAGE_SIZE_EXTENDED, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_QUOTA_WARNING_THRESHOLD, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_QUOTA_SEND_THRESHOLD, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_QUOTA_RECEIVE_THRESHOLD, GetPropHandler, DefaultSetPropComputed, this, false, false);
	HrAddPropHandlers(PR_STORE_OFFLINE, GetPropHandler, DefaultSetPropComputed, this);
	HrAddPropHandlers(PR_EC_SERVER_VERSION, GetPropHandler, DefaultSetPropComputed, this);

	// only on admin store? how? .. now checked on server in ECTableManager
	HrAddPropHandlers(PR_EC_STATSTABLE_SYSTEM, GetPropHandler, DefaultSetPropComputed, this, false, true);
	HrAddPropHandlers(PR_EC_STATSTABLE_SESSIONS, GetPropHandler, DefaultSetPropComputed, this, false, true);
	HrAddPropHandlers(PR_EC_STATSTABLE_USERS, GetPropHandler, DefaultSetPropComputed, this, false, true);
	HrAddPropHandlers(PR_EC_STATSTABLE_COMPANY, GetPropHandler, DefaultSetPropComputed, this, false, true);
	HrAddPropHandlers(PR_EC_STATSTABLE_SERVERS, GetPropHandler, DefaultSetPropComputed, this, false, true);
	HrAddPropHandlers(PR_TEST_LINE_SPEED, GetPropHandler, DefaultSetPropComputed, this, false, true);
	HrAddPropHandlers(PR_EMSMDB_SECTION_UID, GetPropHandler, DefaultSetPropComputed, this, false, true);
	HrAddPropHandlers(PR_ACL_DATA, GetPropHandler, SetPropHandler, this, false, true);

	// Basically a workaround because we can't pass 'this' in the superclass constructor.
	SetProvider(this);
	isTransactedObject = false;
	GetClientVersion(&m_ulClientVersion); //Ignore errors
	assert(lpszProfname != NULL);
}

ECMsgStore::~ECMsgStore() {
	if(lpTransport)
		lpTransport->HrLogOff();
	// remove all advices
	if(m_lpNotifyClient)
		m_lpNotifyClient->ReleaseAll();
	// Release this->ECMAPIProp::lpStorage since it is registered on lpTransport.
	lpStorage.reset();
}

//FIXME: remove duplicate profilename
static HRESULT GetIMsgStoreObject(BOOL bOffline,
    BOOL bModify, ECMapProvider *lpmapProviders, IMAPISupport *lpMAPISup,
    ULONG cbEntryId, LPENTRYID lpEntryId, LPMDB *lppIMsgStore)
{
	PROVIDER_INFO sProviderInfo;
	object_ptr<IProfSect> lpProfSect;
	memory_ptr<SPropValue> lpsPropValue;

	HRESULT hr = lpMAPISup->OpenProfileSection((LPMAPIUID)&MUID_PROFILE_INSTANCE, 0, &~lpProfSect);
	if(hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(lpProfSect, PR_PROFILE_NAME_A, &~lpsPropValue);
	if(hr != hrSuccess)
		return hr;

	// Set ProfileName
	auto lpszProfileName = lpsPropValue->Value.lpszA;
	hr = GetProviders(lpmapProviders, lpMAPISup, lpszProfileName, 0, &sProviderInfo);
	if (hr != hrSuccess)
		return hr;
	return sProviderInfo.lpMSProviderOnline->Logon(lpMAPISup, 0,
	       reinterpret_cast<const TCHAR *>(lpszProfileName), cbEntryId, lpEntryId,
	       bModify ? MAPI_BEST_ACCESS : 0,
	       nullptr, nullptr, nullptr, nullptr, nullptr, lppIMsgStore);
}

HRESULT ECMsgStore::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMsgStore, this);
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMsgStore, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);

	if (refiid == IID_IExchangeManageStore)
		REGISTER_INTERFACE2(IExchangeManageStore, this);

	REGISTER_INTERFACE2(IECServiceAdmin, this);
	REGISTER_INTERFACE2(IECSpooler, this);
	REGISTER_INTERFACE2(IECSecurity, this);
	REGISTER_INTERFACE2(IProxyStoreObject, this);

	if (refiid == IID_ECMsgStoreOnline)
	{
		*lppInterface = static_cast<IMsgStore *>(this);
		AddRef();
		return hrSuccess;
	}
	// is admin store?
	REGISTER_INTERFACE2(IECTestProtocol, &m_xMsgStoreProxy);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMsgStore::QueryInterfaceProxy(REFIID refiid, void **lppInterface)
{
	if (refiid == IID_IProxyStoreObject) // block recusive proxy calls
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	REGISTER_INTERFACE2(IMsgStore, &m_xMsgStoreProxy);
	REGISTER_INTERFACE2(IMAPIProp, &m_xMsgStoreProxy);
	REGISTER_INTERFACE2(IUnknown, &m_xMsgStoreProxy);
	return QueryInterface(refiid, lppInterface);
}

ULONG ECMsgStore::Release()
{
	return ECUnknown::Release();
}

HRESULT	ECMsgStore::Create(const char *lpszProfname, LPMAPISUP lpSupport,
    WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags,
    BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore,
    ECMsgStore **lppECMsgStore)
{
	return alloc_wrap<ECMsgStore>(lpszProfname, lpSupport, lpTransport,
	       fModify, ulProfileFlags, fIsSpooler, fIsDefaultStore,
	       bOfflineStore).put(lppECMsgStore);
}

HRESULT ECMsgStore::SetProps(ULONG cValues, const SPropValue *lpPropArray,
    SPropProblemArray **lppProblems)
{
	HRESULT hr = ECMAPIProp::SetProps(cValues, lpPropArray, lppProblems);
	if (hr != hrSuccess)
		return hr;
	return ECMAPIProp::SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT ECMsgStore::DeleteProps(const SPropTagArray *lpPropTagArray,
    SPropProblemArray **lppProblems)
{
	HRESULT hr = ECMAPIProp::DeleteProps(lpPropTagArray, lppProblems);
	if (hr != hrSuccess)
		return hr;
	return ECMAPIProp::SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT ECMsgStore::SaveChanges(ULONG ulFlags)
{
	return hrSuccess;
}

HRESULT ECMsgStore::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN *lppUnk)
{
	if (lpiid == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = MAPI_E_INTERFACE_NOT_SUPPORTED;
	if(ulPropTag == PR_RECEIVE_FOLDER_SETTINGS) {
		if (*lpiid == IID_IMAPITable && !IsPublicStore())
			// Non supported function for publicfolder
			hr = GetReceiveFolderTable(0, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_HIERARCHY_SYNCHRONIZER) {
		hr = ECExchangeExportChanges::Create(this, *lpiid, std::string(), L"store hierarchy", ICS_SYNC_HIERARCHY, (LPEXCHANGEEXPORTCHANGES*) lppUnk);
	} else if(ulPropTag == PR_CONTENTS_SYNCHRONIZER) {
	    if (*lpiid == IID_IECExportAddressbookChanges) {
			hr = alloc_wrap<ECExportAddressbookChanges>(this).as(*lpiid, lppUnk);
			if (hr != hrSuccess)
				return hr;
	    }
		else
			hr = ECExchangeExportChanges::Create(this, *lpiid, std::string(), L"store contents", ICS_SYNC_CONTENTS, (LPEXCHANGEEXPORTCHANGES*) lppUnk);
	} else if (ulPropTag == PR_EC_CHANGE_ADVISOR) {
		object_ptr<ECChangeAdvisor> lpChangeAdvisor;
		hr = ECChangeAdvisor::Create(this, &~lpChangeAdvisor);
		if (hr == hrSuccess)
			hr = lpChangeAdvisor->QueryInterface(*lpiid, (void**)lppUnk);
	} else if(ulPropTag == PR_EC_STATSTABLE_SYSTEM) {
		if (*lpiid == IID_IMAPITable)
			hr = OpenStatsTable(TABLETYPE_STATS_SYSTEM, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_EC_STATSTABLE_SESSIONS) {
		if (*lpiid == IID_IMAPITable)
			hr = OpenStatsTable(TABLETYPE_STATS_SESSIONS, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_EC_STATSTABLE_USERS) {
		if (*lpiid == IID_IMAPITable)
			hr = OpenStatsTable(TABLETYPE_STATS_USERS, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_EC_STATSTABLE_COMPANY) {
		if (*lpiid == IID_IMAPITable)
			hr = OpenStatsTable(TABLETYPE_STATS_COMPANY, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_EC_STATSTABLE_SERVERS) {
		if (*lpiid == IID_IMAPITable)
			hr = OpenStatsTable(TABLETYPE_STATS_SERVERS, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_ACL_TABLE) {
		if(*lpiid == IID_IExchangeModifyTable)
			hr = ECExchangeModifyTable::CreateACLTable(this, ulInterfaceOptions, (LPEXCHANGEMODIFYTABLE*)lppUnk);
	} else
		hr = ECMAPIProp::OpenProperty(ulPropTag, lpiid, ulInterfaceOptions, ulFlags, lppUnk);

	return hr;
}

HRESULT ECMsgStore::OpenStatsTable(unsigned int ulTableType, LPMAPITABLE *lppTable)
{
	if (lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<WSTableView> lpTableView;
	object_ptr<ECMAPITable> lpTable;
	// notifications? set 1st param: m_lpNotifyClient
	auto hr = ECMAPITable::Create("Stats table", nullptr, 0, &~lpTable);
	if (hr != hrSuccess)
		return hr;

	// open store table view, no entryid req.
	hr = lpTransport->HrOpenMiscTable(ulTableType, 0, 0, NULL, this, &~lpTableView);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableView, true);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);
	if (hr != hrSuccess)
		return hr;
    AddChild(lpTable);
	return hrSuccess;
}

/**
 * Register an internal advise.
 *
 * This means that the subscribe request should be sent to the server by some other means, but the
 * rest of the client-side handling is done just the same as a normal 'Advise()' call. Also, notifications
 * registered this way are marked 'synchronous' which means that they will not be offloaded to other threads
 * or passed to MAPI's notification system in win32; the notification callback will be called as soon as it
 * is received from the server, and other notifications will not be sent until the handler is done.
 *
 * @param[in] cbEntryID Size of data in lpEntryID
 * @param[in] lpEntryID EntryID of item to subscribe to events to
 * @param[in] ulEventMask Bitmask of events to susbcribe to
 * @param[in] lpAdviseSink Sink to send notification events to
 * @param[out] lpulConnection Connection ID of the registered subscription
 * @return result
 */
HRESULT ECMsgStore::InternalAdvise(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink, ULONG *lpulConnection)
{
	if (m_ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS)
		return MAPI_E_NO_SUPPORT;
	if (lpAdviseSink == nullptr || lpulConnection == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = hrSuccess;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	assert(m_lpNotifyClient != nullptr && (lpEntryID != nullptr || m_lpEntryId != nullptr));
	if(lpEntryID == NULL) {
		// never sent the client store entry
		hr = UnWrapServerClientStoreEntry(m_cbEntryId, m_lpEntryId, &cbUnWrapStoreID, &~lpUnWrapStoreID);
		if(hr != hrSuccess)
			return hr;
		cbEntryID = cbUnWrapStoreID;
		lpEntryID = lpUnWrapStoreID;
	}

	if(m_lpNotifyClient->RegisterAdvise(cbEntryID, (LPBYTE)lpEntryID, ulEventMask, true, lpAdviseSink, lpulConnection) != S_OK)
		hr = MAPI_E_NO_SUPPORT;
	if(hr != hrSuccess)
		return hr;
	m_setAdviseConnections.emplace(*lpulConnection);
	return hrSuccess;
}

HRESULT ECMsgStore::Advise(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink, ULONG *lpulConnection)
{
	if (m_ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS)
		return MAPI_E_NO_SUPPORT;
	if (lpAdviseSink == nullptr || lpulConnection == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = hrSuccess;
	ecmem_ptr<ENTRYID> lpUnWrapStoreID;
	ULONG		cbUnWrapStoreID = 0;

	assert(m_lpNotifyClient != nullptr && (lpEntryID != nullptr || m_lpEntryId != nullptr));
	if(lpEntryID == NULL) {
		// never sent the client store entry
		hr = UnWrapServerClientStoreEntry(m_cbEntryId, m_lpEntryId, &cbUnWrapStoreID, &~lpUnWrapStoreID);
		if(hr != hrSuccess)
			return hr;
		cbEntryID = cbUnWrapStoreID;
		lpEntryID = lpUnWrapStoreID;
	} else {
		// check that the given lpEntryID belongs to the store in m_lpEntryId
		if (memcmp(&GetStoreGuid(), &reinterpret_cast<const EID *>(lpEntryID)->guid, sizeof(GUID)) != 0)
			return MAPI_E_NO_SUPPORT;
	}

	if(m_lpNotifyClient->Advise(cbEntryID, (LPBYTE)lpEntryID, ulEventMask, lpAdviseSink, lpulConnection) != S_OK)
		hr = MAPI_E_NO_SUPPORT;
	m_setAdviseConnections.emplace(*lpulConnection);
	return hr;
}

HRESULT ECMsgStore::Unadvise(ULONG ulConnection) {
	if (m_ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS)
		return MAPI_E_NO_SUPPORT;
	assert(m_lpNotifyClient != NULL);
	m_lpNotifyClient->Unadvise(ulConnection);
	return hrSuccess;
}

HRESULT ECMsgStore::Reload(void *lpParam, ECSESSIONID sessionid)
{
	auto lpThis = static_cast<ECMsgStore *>(lpParam);

	for (auto conn_id : lpThis->m_setAdviseConnections)
		lpThis->m_lpNotifyClient->Reregister(conn_id);
	return hrSuccess;
}

HRESULT ECMsgStore::TableRowGetProp(void *lpProvider,
    const struct propVal *lpsPropValSrc, SPropValue *lpsPropValDst,
    void **lpBase, ULONG ulType)
{
	auto lpMsgStore = static_cast<ECMsgStore *>(lpProvider);

	if (lpsPropValSrc->ulPropTag != PR_ENTRYID)
		return MAPI_E_NOT_FOUND;

	ULONG cbWrapped = 0;
	memory_ptr<ENTRYID> lpWrapped;
	auto hr = lpMsgStore->GetWrappedServerStoreEntryID(lpsPropValSrc->Value.bin->__size, lpsPropValSrc->Value.bin->__ptr, &cbWrapped, &~lpWrapped);
	if (hr != hrSuccess)
		return hr;
	hr = ECAllocateMore(cbWrapped, lpBase, reinterpret_cast<void **>(&lpsPropValDst->Value.bin.lpb));
	if (hr != hrSuccess)
		return hr;
	memcpy(lpsPropValDst->Value.bin.lpb, lpWrapped, cbWrapped);
	lpsPropValDst->Value.bin.cb = cbWrapped;
	lpsPropValDst->ulPropTag = CHANGE_PROP_TYPE(lpsPropValSrc->ulPropTag, PT_BINARY);
	return hrSuccess;
}

/**
 * Compares two entry identifiers.
 *
 * Compares two entry identifiers which must match with the store MAPIUID.
 *
 * @param[in] cbEntryID1	The size in bytes of the lpEntryID1 parameter.
 * @param[in] lpEntryID1	A pointer to the first entry identifier.
 * @param[in] cbEntryID2	The size in bytes of the lpEntryID2 parameter.
 * @param[in] lpEntryID2	A pointer to the second entry identifier.
 * @param[in] ulFlags		Unknown flags; MSDN says it must be zero.
 * @param[out] lpulResult	A pointer to the result. TRUE if the two entry identifiers matching to the same object; otherwise, FALSE.
 *
 * @retval S_OK
				The comparison was successful.
 * @retval MAPI_E_INVALID_PARAMETER
 *				One or more values are NULL.
 *
 */
HRESULT ECMsgStore::CompareEntryIDs(ULONG cbEntryID1, const ENTRYID *lpEntryID1,
    ULONG cbEntryID2, const ENTRYID *lpEntryID2, ULONG ulFlags,
    ULONG *lpulResult)
{
	if (lpulResult != nullptr)
		*lpulResult = false;
	// Apparently BlackBerry CALHelper.exe needs this
	if ((cbEntryID1 == 0 && cbEntryID2 != 0) || (cbEntryID1 != 0 && cbEntryID2 == 0))
		return hrSuccess;
	if (lpEntryID1 == nullptr || lpEntryID2 == nullptr ||
	    lpulResult == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	PEID peid1 = (PEID)lpEntryID1;
	PEID peid2 = (PEID)lpEntryID2;
	auto lpStoreId = reinterpret_cast<const EID *>(m_lpEntryId.get());

	// Check if one or both of the entry identifiers contains the store guid.
	if (cbEntryID1 != cbEntryID2)
		return hrSuccess;
	if (cbEntryID1 < offsetof(EID, usFlags) + sizeof(peid1->usFlags) ||
	    cbEntryID2 < offsetof(EID, usFlags) + sizeof(peid2->usFlags))
		return hrSuccess;
	if (memcmp(&lpStoreId->guid, &peid1->guid, sizeof(GUID)) != 0 ||
	    memcmp(&lpStoreId->guid, &peid2->guid, sizeof(GUID)) != 0)
		return hrSuccess;
	if(memcmp(peid1->abFlags, peid2->abFlags, 4) != 0)
		return hrSuccess;
	if(peid1->ulVersion != peid2->ulVersion)
		return hrSuccess;
	if(peid1->usType != peid2->usType)
		return hrSuccess;

	if(peid1->ulVersion == 0) {
		if(cbEntryID1 != sizeof(EID_V0))
			return hrSuccess;
		if( ((EID_V0*)lpEntryID1)->ulId != ((EID_V0*)lpEntryID2)->ulId )
			return hrSuccess;
	}else {
		if(cbEntryID1 != CbNewEID(""))
			return hrSuccess;
		if(peid1->uniqueId != peid2->uniqueId) //comp. with the old ulId
			return hrSuccess;
	}
	*lpulResult = true;
	return hrSuccess;
}

HRESULT ECMsgStore::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	return OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, ECMessageFactory(), lpulObjType, lppUnk);
}

HRESULT ECMsgStore::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, const IMessageFactory &msgfac,
    ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	if (lpulObjType == nullptr || lppUnk == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<ENTRYID> lpRootEntryID;
	ULONG				cbRootEntryID = 0;
	BOOL				fModifyObject = FALSE;
	object_ptr<ECMAPIFolder> lpMAPIFolder;
	object_ptr<ECMessage> lpMessage;
	object_ptr<IECPropStorage> lpPropStorage;
	object_ptr<WSMAPIFolderOps> lpFolderOps;
	unsigned int objtype = 0;

	if(ulFlags & MAPI_MODIFY) {
		if (!fModify)
			return MAPI_E_NO_ACCESS;
		fModifyObject = TRUE;
	}

	if(ulFlags & MAPI_BEST_ACCESS)
		fModifyObject = fModify;

	if (cbEntryID == 0 || lpEntryID == nullptr) {
		auto hr = lpTransport->HrGetStore(m_cbEntryId, m_lpEntryId, 0, nullptr, &cbRootEntryID, &~lpRootEntryID);
		if(hr != hrSuccess)
			return hr;
		lpEntryID = lpRootEntryID;
		cbEntryID = cbRootEntryID;
	}else {
		auto hr = HrCompareEntryIdWithStoreGuid(cbEntryID, lpEntryID, &GetStoreGuid());
		if(hr != hrSuccess)
			return hr;
		if(!(ulFlags & MAPI_DEFERRED_ERRORS)) {
			hr = lpTransport->HrCheckExistObject(cbEntryID, lpEntryID, (ulFlags & (SHOW_SOFT_DELETES)));
			if (hr != hrSuccess)
				return hr;
		}
	}

	auto hr = HrGetObjTypeFromEntryId(cbEntryID, lpEntryID, &objtype);
	if(hr != hrSuccess)
		return hr;

	switch (objtype) {
	case MAPI_FOLDER:
		hr = lpTransport->HrOpenFolderOps(cbEntryID, lpEntryID, &~lpFolderOps);
		if(hr != hrSuccess)
			return hr;
		hr = ECMAPIFolder::Create(this, fModifyObject, lpFolderOps, &~lpMAPIFolder);
		if(hr != hrSuccess)
			return hr;
		hr = lpTransport->HrOpenPropStorage(m_cbEntryId, m_lpEntryId, cbEntryID, lpEntryID, (ulFlags & SHOW_SOFT_DELETES) ? MSGFLAG_DELETED : 0, &~lpPropStorage);
		if(hr != hrSuccess)
			return hr;
		hr = lpMAPIFolder->HrSetPropStorage(lpPropStorage, !(ulFlags & MAPI_DEFERRED_ERRORS));
		if(hr != hrSuccess)
			return hr;
		hr = lpMAPIFolder->SetEntryId(cbEntryID, lpEntryID);
		if(hr != hrSuccess)
			return hr;
		AddChild(lpMAPIFolder);

		if(lpInterface)
			hr = lpMAPIFolder->QueryInterface(*lpInterface,(void **)lppUnk);
		else
			hr = lpMAPIFolder->QueryInterface(IID_IMAPIFolder, (void **)lppUnk);
		if(lpulObjType)
			*lpulObjType = MAPI_FOLDER;
		break;
	case MAPI_MESSAGE:
		hr = msgfac.Create(this, FALSE, fModifyObject, 0, FALSE, nullptr, &~lpMessage);
		if(hr != hrSuccess)
			return hr;

		// SC: TODO: null or my entryid ?? (this is OpenEntry(), so we don't need it?)
		// parent only needed on create new item .. ohwell
		hr = lpTransport->HrOpenPropStorage(m_cbEntryId, m_lpEntryId, cbEntryID, lpEntryID, (ulFlags & SHOW_SOFT_DELETES) ? MSGFLAG_DELETED : 0, &~lpPropStorage);
		if(hr != hrSuccess)
			return hr;
		hr = lpMessage->SetEntryId(cbEntryID, lpEntryID);
		if(hr != hrSuccess)
			return hr;
		hr = lpMessage->HrSetPropStorage(lpPropStorage, false);
		if(hr != hrSuccess)
			return hr;
		AddChild(lpMessage);

		if(lpInterface)
			hr = lpMessage->QueryInterface(*lpInterface,(void **)lppUnk);
		else
			hr = lpMessage->QueryInterface(IID_IMessage, (void **)lppUnk);
		if(lpulObjType)
			*lpulObjType = MAPI_MESSAGE;
		break;
	default:
		return MAPI_E_NOT_FOUND;
	}
	return hr;
}

HRESULT ECMsgStore::SetReceiveFolder(const TCHAR *lpszMessageClass,
    ULONG ulFlags, ULONG cbEntryID, const ENTRYID *lpEntryID)
{
	// Non supported function for publicfolder
	if (IsPublicStore() == TRUE)
		return MAPI_E_NO_SUPPORT;
	return lpTransport->HrSetReceiveFolder(m_cbEntryId, m_lpEntryId,
	       convstring(lpszMessageClass, ulFlags), cbEntryID, lpEntryID);
}

// If the open store a publicstore
BOOL ECMsgStore::IsPublicStore() const
{
	return CompareMDBProvider(&m_guidMDB_Provider, &KOPANO_STORE_PUBLIC_GUID);
}

// As the store is a delegate store
BOOL ECMsgStore::IsDelegateStore() const
{
	return CompareMDBProvider(&m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID);
}

HRESULT ECMsgStore::GetReceiveFolder(const TCHAR *lpszMessageClass,
    ULONG ulFlags, ULONG *lpcbEntryID, ENTRYID **lppEntryID,
    TCHAR **lppszExplicitClass)
{
	if (IsPublicStore())
		return MAPI_E_NO_SUPPORT;
	if (lpcbEntryID == nullptr || lppEntryID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ULONG		cbEntryID = 0;
	LPENTRYID	lpEntryID = NULL;
	utf8string	strExplicitClass;
	auto hr = lpTransport->HrGetReceiveFolder(m_cbEntryId, m_lpEntryId,
	          convstring(lpszMessageClass, ulFlags), &cbEntryID,
	          &lpEntryID, lppszExplicitClass ? &strExplicitClass : nullptr);
	if(hr != hrSuccess)
		return hr;

	if(lpEntryID) {
		*lpcbEntryID = cbEntryID;
		*lppEntryID = lpEntryID;
	} else {
		*lpcbEntryID = 0;
		*lppEntryID = NULL;
	}

	if (lppszExplicitClass == nullptr)
		return hrSuccess;
	if (ulFlags & MAPI_UNICODE) {
		std::wstring dst = convert_to<std::wstring>(strExplicitClass);

		hr = MAPIAllocateBuffer(sizeof(std::wstring::value_type) * (dst.length() + 1), (void **)lppszExplicitClass);
		if (hr != hrSuccess)
			return hr;
		wcscpy((wchar_t *)*lppszExplicitClass, dst.c_str());
		return hrSuccess;
	}

	std::string dst = convert_to<std::string>(strExplicitClass);
	hr = MAPIAllocateBuffer(dst.length() + 1, (void **)lppszExplicitClass);
	if (hr != hrSuccess)
		return hr;
	strcpy((char *)*lppszExplicitClass, dst.c_str());
	return hrSuccess;
}

HRESULT ECMsgStore::GetReceiveFolderTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	if (IsPublicStore())
		return MAPI_E_NO_SUPPORT;
	if (lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	SizedSPropTagArray(NUM_RFT_PROPS, sPropRFTColumns) =
		{NUM_RFT_PROPS, {PR_ROWID, PR_INSTANCE_KEY, PR_ENTRYID,
		PR_RECORD_KEY,PR_MESSAGE_CLASS_A}};
	object_ptr<ECMemTableView> lpView = NULL;
	object_ptr<ECMemTable> lpMemTable;
	rowset_ptr lpsRowSet;
	memory_ptr<SPropTagArray> lpPropTagArray;

	Util::proptag_change_unicode(ulFlags, sPropRFTColumns);
	auto hr = ECMemTable::Create(sPropRFTColumns, PR_ROWID, &~lpMemTable); // PR_INSTANCE_KEY
	if(hr != hrSuccess)
		return hr;

	//Get the receivefolder list from the server
	hr = lpTransport->HrGetReceiveFolderTable(ulFlags, m_cbEntryId, m_lpEntryId, &~lpsRowSet);
	if(hr != hrSuccess)
		return hr;
	for (unsigned int i = 0; i < lpsRowSet->cRows; ++i) {
		hr = lpMemTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, nullptr, lpsRowSet[i].lpProps, NUM_RFT_PROPS);
		if(hr != hrSuccess)
			return hr;
	}
	hr = lpMemTable->HrGetView(createLocaleFromName(""), ulFlags & MAPI_UNICODE, &~lpView);
	if(hr != hrSuccess)
		return hr;
	return lpView->QueryInterface(IID_IMAPITable, (void **)lppTable);
}

HRESULT ECMsgStore::StoreLogoff(ULONG *lpulFlags) {
	return hrSuccess;
}

HRESULT ECMsgStore::AbortSubmit(ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG ulFlags)
{
	// Non supported function for publicfolder
	if (IsPublicStore() == TRUE)
		return MAPI_E_NO_SUPPORT;
	// Check input/output variables
	if (lpEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;
	return lpTransport->HrAbortSubmit(cbEntryID, lpEntryID);
}

HRESULT ECMsgStore::GetOutgoingQueue(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	/* Only supported by the MAPI spooler */
	/* if (!IsSpooler())
		return MAPI_E_NO_SUPPORT;
	*/
	if (lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableOutGoingQueue> lpTableOps;
	auto hr = ECMAPITable::Create("Outgoing queue", m_lpNotifyClient, 0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = lpTransport->HrOpenTableOutGoingQueueOps(m_cbEntryId, m_lpEntryId, this, &~lpTableOps);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);
	AddChild(lpTable);
	return hr;
}

HRESULT ECMsgStore::SetLockState(LPMESSAGE lpMessage, ULONG ulLockState)
{
	/* Only supported by the MAPI spooler */
	/* if (!IsSpooler())
		return MAPI_E_NO_SUPPORT; */
	if (lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ecmem_ptr<SPropValue> lpsPropArray;
	unsigned int cValue = 0, ulSubmitFlag = 0;
	object_ptr<ECMessage> ptrECMessage;
	static constexpr const SizedSPropTagArray(2, sptaMessageProps) =
		{2, {PR_SUBMIT_FLAGS, PR_ENTRYID}};
	enum {IDX_SUBMIT_FLAGS, IDX_ENTRYID};
	auto hr = lpMessage->GetProps(sptaMessageProps, 0, &cValue, &~lpsPropArray);
	if(HR_FAILED(hr))
		return hr;
	if (PROP_TYPE(lpsPropArray[IDX_ENTRYID].ulPropTag) == PT_ERROR)
		return lpsPropArray[IDX_ENTRYID].Value.err;
	if (PROP_TYPE(lpsPropArray[IDX_SUBMIT_FLAGS].ulPropTag) != PT_ERROR)
		ulSubmitFlag = lpsPropArray->Value.l;

	// set the lock state, if the message is already in the correct state
	// just get outta here
	if (ulLockState & MSG_LOCKED)
	{
		if (ulSubmitFlag & SUBMITFLAG_LOCKED)
			return hr;
		ulSubmitFlag |= SUBMITFLAG_LOCKED;
	} else { // unlock
		if (!(ulSubmitFlag & SUBMITFLAG_LOCKED))
			return hr;
		ulSubmitFlag &= ~SUBMITFLAG_LOCKED;
	}

	hr = lpMessage->QueryInterface(iid_of(ptrECMessage), &~ptrECMessage);
	if (hr != hrSuccess)
		return hr;
	if (ptrECMessage->IsReadOnly())
		return MAPI_E_NO_ACCESS;
	hr = lpTransport->HrSetLockState(lpsPropArray[IDX_ENTRYID].Value.bin.cb, (LPENTRYID)lpsPropArray[IDX_ENTRYID].Value.bin.lpb, (ulSubmitFlag & SUBMITFLAG_LOCKED) == SUBMITFLAG_LOCKED);
	if (hr != hrSuccess)
		return hr;
	hr = ECAllocateBuffer(sizeof(SPropValue), &~lpsPropArray);
	if (hr != hrSuccess)
		return hr;

	lpsPropArray[0].ulPropTag = PR_SUBMIT_FLAGS;
	lpsPropArray[0].Value.l = ulSubmitFlag;
	hr = lpMessage->SetProps(1, lpsPropArray, NULL);
	if (hr != hrSuccess)
		return hr;
	return lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
}

HRESULT ECMsgStore::FinishedMsg(ULONG ulFlags, ULONG cbEntryID, const ENTRYID *lpEntryID)
{
	/* Only supported by the MAPI spooler */
	/* if (!IsSpooler())
		return MAPI_E_NO_SUPPORT;
	*/
	if (lpEntryID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	unsigned int objtype = 0;
	object_ptr<IMessage> lpMessage;
	// Delete the message from the local outgoing queue
	auto hr = lpTransport->HrFinishedMessage(cbEntryID, lpEntryID, EC_SUBMIT_LOCAL);
	if(hr != hrSuccess)
		return hr;

	/**
	 * @todo: Do we need this or can we let OpenEntry succeed if this is the same session on which the
	 *        message was locked.
	 */
	hr = lpTransport->HrSetLockState(cbEntryID, lpEntryID, false);
	if (hr != hrSuccess)
		return hr;
	hr = OpenEntry(cbEntryID, lpEntryID, &IID_IMessage, MAPI_MODIFY, &objtype, &~lpMessage);
	if(hr != hrSuccess)
		return hr;
	// Unlock the message
	hr = SetLockState(lpMessage, MSG_UNLOCKED);
	if(hr != hrSuccess)
		return hr;
	return lpSupport->DoSentMail(0, lpMessage);
}

HRESULT ECMsgStore::NotifyNewMail(const NOTIFICATION *lpNotification)
{
	// Only supported by the MAPI spooler
	/* if (!IsSpooler())
		return MAPI_E_NO_SUPPORT;
	*/
	// Check input/output variables
	if (lpNotification == NULL || lpNotification->info.newmail.lpParentID == NULL || lpNotification->info.newmail.lpEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = HrCompareEntryIdWithStoreGuid(lpNotification->info.newmail.cbEntryID, lpNotification->info.newmail.lpEntryID, &GetStoreGuid());
	if(hr != hrSuccess)
		return hr;
	hr = HrCompareEntryIdWithStoreGuid(lpNotification->info.newmail.cbParentID, lpNotification->info.newmail.lpParentID, &GetStoreGuid());
	if(hr != hrSuccess)
		return hr;
	return lpTransport->HrNotify(lpNotification);
}

HRESULT	ECMsgStore::GetPropHandler(ULONG ulPropTag, void* lpProvider, ULONG ulFlags, LPSPropValue lpsPropValue, void *lpParam, void *lpBase)
{
	HRESULT hr = hrSuccess;
	object_ptr<IProfSect> lpProfSect;
	memory_ptr<SPropValue> lpProp;
	auto lpStore = static_cast<ECMsgStore *>(lpParam);

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_EMSMDB_SECTION_UID): {
		hr = lpStore->lpSupport->OpenProfileSection(NULL, 0, &~lpProfSect);
		if (hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(lpProfSect, PR_SERVICE_UID, &~lpProp);
		if (hr != hrSuccess)
			return hr;
		hr = lpStore->lpSupport->OpenProfileSection((LPMAPIUID)lpProp->Value.bin.lpb, 0, &~lpProfSect);
		if (hr != hrSuccess)
			return hr;
		hr = HrGetOneProp(lpProfSect, PR_EMSMDB_SECTION_UID, &~lpProp);
		if (hr != hrSuccess)
			return hr;
		lpsPropValue->ulPropTag = PR_EMSMDB_SECTION_UID;
		lpsPropValue->Value.bin.cb = sizeof(GUID);
		return KAllocCopy(lpProp->Value.bin.lpb, sizeof(GUID), reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb), lpBase);
	}
	case PROP_ID(PR_SEARCH_KEY):
	case PROP_ID(PR_ENTRYID): {
		ULONG cbWrapped = 0;
		memory_ptr<ENTRYID> lpWrapped;

		lpsPropValue->ulPropTag = ulPropTag;
		hr = lpStore->GetWrappedStoreEntryID(&cbWrapped, &~lpWrapped);
		if (hr == hrSuccess) {
			hr = ECAllocateMore(cbWrapped, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
			if (hr != hrSuccess)
				break;
			memcpy(lpsPropValue->Value.bin.lpb, lpWrapped, cbWrapped);
			lpsPropValue->Value.bin.cb = cbWrapped;
		} else {
			hr = MAPI_E_NOT_FOUND;
		}
		break;
	}
	case PROP_ID(PR_RECORD_KEY):
		lpsPropValue->ulPropTag = PR_RECORD_KEY;
		lpsPropValue->Value.bin.cb = sizeof(MAPIUID);
		hr = ECAllocateMore(sizeof(MAPIUID), lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.bin.lpb));
		if (hr != hrSuccess)
			break;
		memcpy(lpsPropValue->Value.bin.lpb, &lpStore->GetStoreGuid(), sizeof(MAPIUID));
		break;
	case PROP_ID(PR_RECEIVE_FOLDER_SETTINGS):
		lpsPropValue->ulPropTag = PR_RECEIVE_FOLDER_SETTINGS;
		lpsPropValue->Value.x = 1;
		break;
	case PROP_ID(PR_MESSAGE_SIZE_EXTENDED):
		hr = lpStore->HrGetRealProp(PR_MESSAGE_SIZE_EXTENDED, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_QUOTA_WARNING_THRESHOLD):
		lpsPropValue->ulPropTag = PR_QUOTA_WARNING_THRESHOLD;
		hr = lpStore->HrGetRealProp(PR_QUOTA_WARNING_THRESHOLD, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_QUOTA_SEND_THRESHOLD):
		lpsPropValue->ulPropTag = PR_QUOTA_SEND_THRESHOLD;
		hr = lpStore->HrGetRealProp(PR_QUOTA_SEND_THRESHOLD, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_QUOTA_RECEIVE_THRESHOLD):
		lpsPropValue->ulPropTag = PR_QUOTA_RECEIVE_THRESHOLD;
		hr = lpStore->HrGetRealProp(PR_QUOTA_RECEIVE_THRESHOLD, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_MAILBOX_OWNER_NAME):
		if (lpStore->IsPublicStore() == TRUE) {
			hr = MAPI_E_NOT_FOUND;
			break;
		}
		lpsPropValue->ulPropTag = ulPropTag;
		hr = lpStore->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_MAILBOX_OWNER_ENTRYID):
		if (lpStore->IsPublicStore() == TRUE) {
			hr = MAPI_E_NOT_FOUND;
			break;
		}
		lpsPropValue->ulPropTag = PR_MAILBOX_OWNER_ENTRYID;
		hr = lpStore->HrGetRealProp(PR_MAILBOX_OWNER_ENTRYID, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_STORE_OFFLINE):
		// Delegate stores are always online, so ignore this property
		if (lpStore->IsDelegateStore() == TRUE) {
			hr = MAPI_E_NOT_FOUND;
			break;
		}
		lpsPropValue->ulPropTag = PR_STORE_OFFLINE;
		lpsPropValue->Value.b = false;
		break;
	case PROP_ID(PR_USER_NAME):
		lpsPropValue->ulPropTag = PR_USER_NAME;
		hr = lpStore->HrGetRealProp(PR_USER_NAME, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_USER_ENTRYID):
		lpsPropValue->ulPropTag = PR_USER_ENTRYID;
		hr = lpStore->HrGetRealProp(PR_USER_ENTRYID, ulFlags, lpBase, lpsPropValue);
		break;
	case PROP_ID(PR_EC_STATSTABLE_SYSTEM):
	case PROP_ID(PR_EC_STATSTABLE_SESSIONS):
	case PROP_ID(PR_EC_STATSTABLE_USERS):
	case PROP_ID(PR_EC_STATSTABLE_COMPANY):
		lpsPropValue->ulPropTag = ulPropTag;
		lpsPropValue->Value.x = 1;
		break;
	case PROP_ID(PR_TEST_LINE_SPEED):
		lpsPropValue->ulPropTag = ulPropTag;
		lpsPropValue->Value.bin.lpb = NULL;
		lpsPropValue->Value.bin.cb = 0;
		break;
	case PROP_ID(PR_ACL_DATA):
		hr = lpStore->GetSerializedACLData(lpBase, lpsPropValue);
		if (hr == hrSuccess)
			lpsPropValue->ulPropTag = PR_ACL_DATA;
		else {
			lpsPropValue->ulPropTag = CHANGE_PROP_TYPE(PR_ACL_DATA, PT_ERROR);
			lpsPropValue->Value.err = hr;
		}
		break;
	case PROP_ID(PR_EC_SERVER_VERSION): {
		lpsPropValue->ulPropTag = ulPropTag;
		auto &ver = lpStore->lpTransport->m_server_version;
		if (PROP_TYPE(ulPropTag) == PT_STRING8) {
			hr = ECAllocateMore(ver.size() + 1, lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.lpszA));
			if (hr != hrSuccess)
				return hr;
			strcpy(lpsPropValue->Value.lpszA, ver.c_str());
			break;
		}
		const auto tmp = convert_to<std::wstring>(ver);
		hr = ECAllocateMore((tmp.size() + 1) * sizeof(wchar_t), lpBase, reinterpret_cast<void **>(&lpsPropValue->Value.lpszW));
		if (hr != hrSuccess)
			return hr;
		wcscpy(lpsPropValue->Value.lpszW, tmp.c_str());
		break;
	}
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}
	return hr;
}

HRESULT	ECMsgStore::SetPropHandler(ULONG ulPropTag, void *lpProvider,
    const SPropValue *lpsPropValue, void *lpParam)
{
	if (ulPropTag == PR_ACL_DATA)
		return static_cast<ECMsgStore *>(lpParam)->SetSerializedACLData(lpsPropValue);
	return MAPI_E_NOT_FOUND;
}

HRESULT ECMsgStore::SetEntryId(ULONG cbEntryId, const ENTRYID *lpEntryId)
{
	assert(m_lpNotifyClient == NULL);
	HRESULT hr = ECGenericProp::SetEntryId(cbEntryId, lpEntryId);
	if(hr != hrSuccess)
		return hr;
	if (m_ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS)
		return hrSuccess;
	// Create Notifyclient
	hr = ECNotifyClient::Create(MAPI_STORE, this, m_ulProfileFlags, lpSupport, &~m_lpNotifyClient);
	assert(m_lpNotifyClient != NULL);
	return hr;
}

const GUID& ECMsgStore::GetStoreGuid()
{
	return reinterpret_cast<const EID *>(m_lpEntryId.get())->guid;
}

HRESULT ECMsgStore::GetWrappedStoreEntryID(ULONG* lpcbWrapped, LPENTRYID* lppWrapped)
{
	//hr = WrapStoreEntryID(0, WCLIENT_DLL_NAME, CbEID(peid), (LPENTRYID)peid, &cbWrapped, &lpWrapped);
	return lpSupport->WrapStoreEntryID(m_cbEntryId, m_lpEntryId, lpcbWrapped, lppWrapped);
}

// This is a function special for the spooler to get the right store entryid
// This is a problem of the master outgoing queue
HRESULT ECMsgStore::GetWrappedServerStoreEntryID(ULONG cbEntryId, LPBYTE lpEntryId, ULONG* lpcbWrapped, LPENTRYID* lppWrapped)
{
	ULONG		cbStoreID = 0;
	ecmem_ptr<ENTRYID> lpStoreID;
	entryId		sEntryId;
	sEntryId.__ptr = lpEntryId;
	sEntryId.__size = cbEntryId;

	auto hr = WrapServerClientStoreEntry(lpTransport->GetServerName(), &sEntryId, &cbStoreID, &~lpStoreID);
	if(hr != hrSuccess)
		return hr;
	return lpSupport->WrapStoreEntryID(cbStoreID, lpStoreID, lpcbWrapped, lppWrapped);
}

/**
 * Implementation of the IExchangeManageStore interface
 *
 * This method creates a store entryid for a specific user optionaly on a specific server.
 *
 * @param[in]	lpszMsgStoreDN
 *					The DN of the server on which the store should be searched for. The default redirect system will be
 *					used if this parameter is set to NULL or an empty string.
 * @param[in]	lpszMailboxDN
 *					The username for whom to find the store.
 * @param[in]	ulFlags
 *					OPENSTORE_OVERRIDE_HOME_MDB - Don't fall back to resolving the server if the user can't be found on the specified server.
 *					MAPI_UNICODE - All passed strings are in Unicode.
 * @param[out]	lpcbEntryID
 *					Pointer to a ULONG variable, which will be set to the size of the returned entry id.
 * @param[out]	lppEntryID
 *					Pointer to a LPENTRYID variable, which will be set to point to the newly created entry id.
 *
 * @return	HRESULT
 * @retval	MAPI_E_NOT_FOUND			The mailbox was not found. User not present on server, or doesn't have a store.
 * @retval	MAPI_E_UNABLE_TO_COMPLETE	A second redirect was returned. This usually indicates a system configuration error.
 * @retval	MAPI_E_INVALID_PARAMETER	One of the parameters is invalid. This includes absent for obligatory parameters.
 */
HRESULT ECMsgStore::CreateStoreEntryID(const TCHAR *lpszMsgStoreDN,
    const TCHAR *lpszMailboxDN, ULONG ulFlags, ULONG *lpcbEntryID,
    ENTRYID **lppEntryID)
{
	ULONG		cbStoreEntryID = 0;
	memory_ptr<ENTRYID> lpStoreEntryID;
	object_ptr<WSTransport> lpTmpTransport;
	convstring		tstrMsgStoreDN(lpszMsgStoreDN, ulFlags);
	convstring		tstrMailboxDN(lpszMailboxDN, ulFlags);

	if (tstrMsgStoreDN.null_or_empty()) {
		// No messagestore DN provided. Just try the current server and let it redirect us if neeeded.
		std::string strRedirServer;
		auto hr = lpTransport->HrResolveUserStore(tstrMailboxDN, ulFlags, nullptr, &cbStoreEntryID, &~lpStoreEntryID, &strRedirServer);
		if (hr == MAPI_E_UNABLE_TO_COMPLETE) {
			hr = lpTransport->CreateAndLogonAlternate(strRedirServer.c_str(), &~lpTmpTransport);
			if (hr != hrSuccess)
				return hr;
			hr = lpTmpTransport->HrResolveUserStore(tstrMailboxDN, ulFlags, NULL, &cbStoreEntryID, &~lpStoreEntryID);
			if (hr != hrSuccess)
				return hr;
			hr = lpTmpTransport->HrLogOff();
		}
		if(hr != hrSuccess)
			return hr;
	} else {
		utf8string strPseudoUrl;
		memory_ptr<char> ptrServerPath;
		bool bIsPeer;

		auto hr = MsgStoreDnToPseudoUrl(tstrMsgStoreDN, &strPseudoUrl);
		if (hr == MAPI_E_NO_SUPPORT && (ulFlags & OPENSTORE_OVERRIDE_HOME_MDB) == 0)
			// Try again old style since the MsgStoreDn contained Unknown as the server name.
			return CreateStoreEntryID(nullptr, lpszMailboxDN, ulFlags, lpcbEntryID, lppEntryID);
		else if (hr != hrSuccess)
			return hr;

		// MsgStoreDN successfully converted
		hr = lpTransport->HrResolvePseudoUrl(strPseudoUrl.c_str(), &~ptrServerPath, &bIsPeer);
		if (hr == MAPI_E_NOT_FOUND && (ulFlags & OPENSTORE_OVERRIDE_HOME_MDB) == 0)
			// Try again old style since the MsgStoreDN contained an unknown server name or the server doesn't support multi server.
			return CreateStoreEntryID(nullptr, lpszMailboxDN, ulFlags, lpcbEntryID, lppEntryID);
		else if (hr != hrSuccess)
			return hr;

		// Pseudo URL successfully resolved
		if (bIsPeer) {
			hr = lpTransport->HrResolveUserStore(tstrMailboxDN, OPENSTORE_OVERRIDE_HOME_MDB, NULL, &cbStoreEntryID, &~lpStoreEntryID);
			if (hr != hrSuccess)
				return hr;
		} else {
			hr = lpTransport->CreateAndLogonAlternate(ptrServerPath, &~lpTmpTransport);
			if (hr != hrSuccess)
				return hr;
			hr = lpTmpTransport->HrResolveUserStore(tstrMailboxDN, OPENSTORE_OVERRIDE_HOME_MDB, NULL, &cbStoreEntryID, &~lpStoreEntryID);
			if (hr != hrSuccess)
				return hr;
			lpTmpTransport->HrLogOff();
		}
	}
	return WrapStoreEntryID(0, reinterpret_cast<const TCHAR *>(WCLIENT_DLL_NAME),
	       cbStoreEntryID, lpStoreEntryID, lpcbEntryID, lppEntryID);
}

HRESULT ECMsgStore::EntryIDFromSourceKey(ULONG cFolderKeySize, BYTE *lpFolderSourceKey,	ULONG cMessageKeySize, BYTE *lpMessageSourceKey, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	return lpTransport->HrEntryIDFromSourceKey(m_cbEntryId, m_lpEntryId,
	       cFolderKeySize, lpFolderSourceKey, cMessageKeySize,
	       lpMessageSourceKey, lpcbEntryID, lppEntryID);
}

HRESULT ECMsgStore::GetRights(ULONG cbUserEntryID, const ENTRYID *lpUserEntryID,
    ULONG cbEntryID, const ENTRYID *lpEntryID, ULONG *lpulRights)
{
	return MAPI_E_NOT_FOUND;
}

/**
 * Get information about all of the mailboxes on a server.
 *
 * @param[in] lpszServerName	Points to a string that contains the name of the server
 * @param[out] lppTable			Points to a IMAPITable interface containing the information about all of
 *								the mailboxes on the server specified in lpszServerName
 * param[in] ulFlags			Specifies whether the server name is Unicode or not. The server name is
 *								default in ASCII format. With flags MAPI_UNICODE the server name is in Unicode format.
 * @return	Mapi error codes
 *
 * @remark	If named properties are used in the column set of the lppTable ensure you have the right named property id.
 *			The id can be different on each server.
 */
HRESULT ECMsgStore::GetMailboxTable(const TCHAR *lpszServerName,
    IMAPITable **lppTable, ULONG ulFlags)
{
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableView> lpTableOps;
	object_ptr<WSTransport> lpTmpTransport;
	object_ptr<ECMsgStore> lpMsgStore;
	object_ptr<IMsgStore> lpMsgStoreOtherServer;
	ULONG			cbEntryId = 0;
	memory_ptr<ENTRYID> lpEntryId;
	bool			bIsPeer = true;
	memory_ptr<char> ptrServerPath;
	std::string		strPseudoUrl;
	convstring		tstrServerName(lpszServerName, ulFlags);
	const utf8string strUserName = convert_to<utf8string>("SYSTEM");

	if (!tstrServerName.null_or_empty()) {
		strPseudoUrl = "pseudo://";
		strPseudoUrl += tstrServerName;
		auto hr = lpTransport->HrResolvePseudoUrl(strPseudoUrl.c_str(), &~ptrServerPath, &bIsPeer);
		if (hr != hrSuccess)
			return hr;
		if (!bIsPeer) {
			hr = lpTransport->CreateAndLogonAlternate(ptrServerPath, &~lpTmpTransport);
			if (hr != hrSuccess)
				return hr;
			hr = lpTmpTransport->HrResolveUserStore(strUserName, 0, NULL, &cbEntryId, &~lpEntryId);
			if (hr != hrSuccess)
				return hr;
			hr = GetIMsgStoreObject(FALSE, fModify, &g_mapProviders, lpSupport, cbEntryId, lpEntryId, &~lpMsgStoreOtherServer);
			if (hr != hrSuccess)
				return hr;
			hr = lpMsgStoreOtherServer->QueryInterface(IID_ECMsgStore, &~lpMsgStore);
			if (hr != hrSuccess)
				return hr;
		}
	}

	if (bIsPeer) {
		auto hr = QueryInterface(IID_ECMsgStore, &~lpMsgStore);
		if (hr != hrSuccess)
			return hr;
	}

	assert(lpMsgStore != NULL);
	auto hr = ECMAPITable::Create("Mailbox table", lpMsgStore->GetMsgStore()->m_lpNotifyClient, 0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = lpMsgStore->lpTransport->HrOpenMailBoxTableOps(ulFlags & MAPI_UNICODE, lpMsgStore->GetMsgStore(), &~lpTableOps);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);
	if(hr != hrSuccess)
		return hr;
	lpMsgStore->AddChild(lpTable);
	return hrSuccess;
}

/**
 * Get information about all of the public mailboxes on a server. (not implement)
 *
 * @param[in] lpszServerName	Points to a string that contains the name of the server
 * @param[out] lppTable			Points to a IMAPITable interface containing the information about all of
 *								the mailboxes on the server specified in lpszServerName
 * param[in] ulFlags			Specifies whether the server name is Unicode or not. The server name is
 *								default in ASCII format. With flags MAPI_UNICODE the server name is in Unicode format.
 *								Flag MDB_IPM		The public folders are interpersonal message (IPM) folders.
								Flag MDB_NON_IPM	The public folders are non-IPM folders.
 *
 * @return	Mapi error codes
 *
 *
 * @todo Implement this function like GetMailboxTable
 */
HRESULT ECMsgStore::GetPublicFolderTable(const TCHAR *server,
    IMAPITable **, ULONG flags)
{
	return MAPI_E_NOT_FOUND;
}

// IECServiceAdmin

// Create the freebusy folder on a private store
static HRESULT CreatePrivateFreeBusyData(LPMAPIFOLDER lpRootFolder,
    LPMAPIFOLDER lpInboxFolder, LPMAPIFOLDER lpCalendarFolder)
{
	// Global
	ecmem_ptr<SPropValue> lpPropValue, lpFBPropValue;
	object_ptr<IMAPIFolder> lpFBFolder;
	object_ptr<IMessage> lpFBMessage;

	// Freebusy mv property
	// This property will be fill in on another place
	auto hr = ECAllocateBuffer(sizeof(SPropValue), &~lpFBPropValue);
	if(hr != hrSuccess)
		return hr;

	memset(lpFBPropValue, 0, sizeof(SPropValue));
	lpFBPropValue->ulPropTag = PR_FREEBUSY_ENTRYIDS;
	lpFBPropValue->Value.MVbin.cValues = 4;
	hr = ECAllocateMore(sizeof(SBinary)*lpFBPropValue->Value.MVbin.cValues, lpFBPropValue, (void**)&lpFBPropValue->Value.MVbin.lpbin);
	if(hr != hrSuccess)
		return hr;

	memset(lpFBPropValue->Value.MVbin.lpbin, 0, sizeof(SBinary)*lpFBPropValue->Value.MVbin.cValues);

	// Create Freebusy Data into the rootfolder
	// Folder for "freebusy data" settings
	// Create the folder
	hr = lpRootFolder->CreateFolder(FOLDER_GENERIC , (LPTSTR)"Freebusy Data", nullptr, &IID_IMAPIFolder, OPEN_IF_EXISTS, &~lpFBFolder);
	if(hr != hrSuccess)
		return hr;

	// Get entryid of localfreebusy
	hr = HrGetOneProp(lpFBFolder, PR_ENTRYID, &~lpPropValue);
	if(hr != hrSuccess)
		return hr;

	//Fill in position 3 of the FBProperty, with free/busy data folder entryid
	hr = ECAllocateMore(lpPropValue->Value.bin.cb, lpFBPropValue, (void**)&lpFBPropValue->Value.MVbin.lpbin[3].lpb);
	if(hr != hrSuccess)
		return hr;
	lpFBPropValue->Value.MVbin.lpbin[3].cb = lpPropValue->Value.bin.cb;
	memcpy(lpFBPropValue->Value.MVbin.lpbin[3].lpb, lpPropValue->Value.bin.lpb, lpPropValue->Value.bin.cb);

	// Create localfreebusy message
	// Default setting of free/busy
	hr = lpFBFolder->CreateMessage(&IID_IMessage, 0, &~lpFBMessage);
	if(hr != hrSuccess)
		return hr;
	unsigned int cValues = 6, cCurValues = 0;
	hr = ECAllocateBuffer(sizeof(SPropValue) * cValues, &~lpPropValue);
	if(hr != hrSuccess)
		return hr;

	lpPropValue[cCurValues].ulPropTag = PR_MESSAGE_CLASS_A;
	lpPropValue[cCurValues++].Value.lpszA = const_cast<char *>("IPM.Microsoft.ScheduleData.FreeBusy");
	lpPropValue[cCurValues].ulPropTag = PR_SUBJECT_A;
	lpPropValue[cCurValues++].Value.lpszA = const_cast<char *>("LocalFreebusy");
	lpPropValue[cCurValues].ulPropTag = PR_FREEBUSY_NUM_MONTHS;
	lpPropValue[cCurValues++].Value.ul = ECFREEBUSY_DEFAULT_PUBLISH_MONTHS;
	lpPropValue[cCurValues].ulPropTag = PR_DECLINE_RECURRING_MEETING_REQUESTS;
	lpPropValue[cCurValues++].Value.b = false;
	lpPropValue[cCurValues].ulPropTag = PR_DECLINE_CONFLICTING_MEETING_REQUESTS;
	lpPropValue[cCurValues++].Value.b = false;
	lpPropValue[cCurValues].ulPropTag = PR_PROCESS_MEETING_REQUESTS;
	lpPropValue[cCurValues++].Value.b = false;

	hr = lpFBMessage->SetProps(cCurValues, lpPropValue, NULL);
	if(hr != hrSuccess)
		return hr;
	hr = lpFBMessage->SaveChanges(KEEP_OPEN_READONLY);
	if(hr != hrSuccess)
		return hr;
	// Get entryid of localfreebusy
	hr = HrGetOneProp(lpFBMessage, PR_ENTRYID, &~lpPropValue);
	if(hr != hrSuccess)
		return hr;

	//Fill in position 1 of the FBProperty, with free/busy message entryid
	hr = ECAllocateMore(lpPropValue->Value.bin.cb, lpFBPropValue, (void**)&lpFBPropValue->Value.MVbin.lpbin[1].lpb);
	if(hr != hrSuccess)
		return hr;
	lpFBPropValue->Value.MVbin.lpbin[1].cb = lpPropValue->Value.bin.cb;
	memcpy(lpFBPropValue->Value.MVbin.lpbin[1].lpb, lpPropValue->Value.bin.lpb, lpPropValue->Value.bin.cb);

	// Create Associated localfreebusy message
	hr = lpCalendarFolder->CreateMessage(&IID_IMessage, MAPI_ASSOCIATED, &~lpFBMessage);
	if(hr != hrSuccess)
		return hr;
	cValues = 3;
	cCurValues = 0;
	hr = ECAllocateBuffer(sizeof(SPropValue) * cValues, &~lpPropValue);
	if(hr != hrSuccess)
		return hr;

	lpPropValue[cCurValues].ulPropTag = PR_MESSAGE_CLASS_A;
	lpPropValue[cCurValues++].Value.lpszA = const_cast<char *>("IPM.Microsoft.ScheduleData.FreeBusy");
	lpPropValue[cCurValues].ulPropTag = PR_SUBJECT_A;
	lpPropValue[cCurValues++].Value.lpszA = const_cast<char *>("LocalFreebusy");
	lpPropValue[cCurValues].ulPropTag = PR_FREEBUSY_NUM_MONTHS;
	lpPropValue[cCurValues++].Value.ul = ECFREEBUSY_DEFAULT_PUBLISH_MONTHS;

	hr = lpFBMessage->SetProps(cCurValues, lpPropValue, NULL);
	if(hr != hrSuccess)
		return hr;
	hr = lpFBMessage->SaveChanges(KEEP_OPEN_READONLY);
	if(hr != hrSuccess)
		return hr;

	// Get entryid of associated localfreebusy
	hr = HrGetOneProp(lpFBMessage, PR_ENTRYID, &~lpPropValue);
	if(hr != hrSuccess)
		return hr;
	//Fill in position 0 of the FBProperty, with associated localfreebusy message entryid
	hr = ECAllocateMore(lpPropValue->Value.bin.cb, lpFBPropValue, (void**)&lpFBPropValue->Value.MVbin.lpbin[0].lpb);
	if(hr != hrSuccess)
		return hr;

	lpFBPropValue->Value.MVbin.lpbin[0].cb = lpPropValue->Value.bin.cb;
	memcpy(lpFBPropValue->Value.MVbin.lpbin[0].lpb, lpPropValue->Value.bin.lpb, lpPropValue->Value.bin.cb);

	// Add freebusy entryid on Inbox folder
	hr = lpInboxFolder->SetProps(1, lpFBPropValue, NULL);
	if(hr != hrSuccess)
		return hr;
	hr = lpInboxFolder->SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		return hr;

	// Add freebusy entryid on the root folder
	hr = lpRootFolder->SetProps(1, lpFBPropValue, NULL);
	if(hr != hrSuccess)
		return hr;
	return lpRootFolder->SaveChanges(KEEP_OPEN_READWRITE);
}

/**
 * Add a folder to the PR_IPM_OL2007_ENTRYIDS struct
 *
 * This appends the folder with the given type ID to the struct. No checking is performed on the
 * data already in the property, it is just appended before the "\0\0\0\0" placeholder.
 *
 * @param lpFolder Folder to update the property in
 * @param ulType Type ID to add
 * @param lpEntryID EntryID of the folder to add
 * @return result
 */
HRESULT ECMsgStore::AddRenAdditionalFolder(IMAPIFolder *lpFolder, ULONG ulType, SBinary *lpEntryID)
{
	memory_ptr<SPropValue> lpRenEntryIDs;
	SPropValue sPropValue;
	std::string strBuffer;
	ULONG ulBlockType = RSF_ELID_ENTRYID;

	if (HrGetOneProp(lpFolder, PR_IPM_OL2007_ENTRYIDS, &~lpRenEntryIDs) == hrSuccess)
		strBuffer.assign((char *)lpRenEntryIDs->Value.bin.lpb, lpRenEntryIDs->Value.bin.cb);
	// Remove trailing \0\0\0\0 if it's there
	if (strBuffer.size() >= 4 && strBuffer.compare(strBuffer.size() - 4, 4, "\0\0\0\0", 4) == 0)
		strBuffer.resize(strBuffer.size()-4);

	uint16_t tmp2 = cpu_to_le16(ulType);
	strBuffer.append(reinterpret_cast<const char *>(&tmp2), sizeof(tmp2)); /* RSS Feeds type */
	strBuffer.append(1, ((lpEntryID->cb+4)&0xFF));
	strBuffer.append(1, ((lpEntryID->cb+4)>>8)&0xFF);
	tmp2 = cpu_to_le16(ulBlockType);
	strBuffer.append(reinterpret_cast<const char *>(&tmp2), sizeof(tmp2));
	strBuffer.append(1, (lpEntryID->cb&0xFF));
	strBuffer.append(1, (lpEntryID->cb>>8)&0xFF);
	strBuffer.append((char*)lpEntryID->lpb, lpEntryID->cb);
	strBuffer.append("\x00\x00\x00\x00", 4);

	sPropValue.ulPropTag = PR_IPM_OL2007_ENTRYIDS;
	sPropValue.Value.bin.cb = strBuffer.size();
	sPropValue.Value.bin.lpb = (LPBYTE)strBuffer.data();

	// Set on root folder
	return lpFolder->SetProps(1, &sPropValue, NULL);
}

/**
 * Create an outlook 2007/2010 additional folder
 *
 * This function creates an additional folder, and adds the folder entryid in the root folder
 * and the inbox in the additional folders property.
 *
 * @param lpRootFolder Root folder of the store to set the property on
 * @param lpInboxFolder Inbox folder of the store to set the property on
 * @param lpSubTreeFolder Folder to create the folder in
 * @param ulType Type ID For the additional folders struct
 * @param lpszFolderName Folder name
 * @param lpszComment Comment for the folder
 * @param lpszContainerType Container type for the folder
 * @param fHidden TRUE if the folder must be marked hidden with PR_ATTR_HIDDEN
 * @return result
 */
HRESULT ECMsgStore::CreateAdditionalFolder(IMAPIFolder *lpRootFolder,
    IMAPIFolder *lpInboxFolder, IMAPIFolder *lpSubTreeFolder, ULONG ulType,
    const TCHAR *lpszFolderName, const TCHAR *lpszComment,
    const TCHAR *lpszContainerType, bool fHidden)
{
	object_ptr<IMAPIFolder> lpMAPIFolder;
	memory_ptr<SPropValue> lpPropValueEID;
	SPropValue sPropValue;

	HRESULT hr = lpSubTreeFolder->CreateFolder(FOLDER_GENERIC,
	     const_cast<LPTSTR>(lpszFolderName),
	     const_cast<LPTSTR>(lpszComment), &IID_IMAPIFolder,
	     OPEN_IF_EXISTS | fMapiUnicode, &~lpMAPIFolder);
	if(hr != hrSuccess)
		return hr;

	// Get entryid of the folder
	hr = HrGetOneProp(lpMAPIFolder, PR_ENTRYID, &~lpPropValueEID);
	if(hr != hrSuccess)
		return hr;
	sPropValue.ulPropTag = PR_CONTAINER_CLASS;
	sPropValue.Value.LPSZ = const_cast<LPTSTR>(lpszContainerType);

	// Set container class
	hr = HrSetOneProp(lpMAPIFolder, &sPropValue);
	if(hr != hrSuccess)
		return hr;

	if(fHidden) {
		sPropValue.ulPropTag = PR_ATTR_HIDDEN;
		sPropValue.Value.b = true;
		hr = HrSetOneProp(lpMAPIFolder, &sPropValue);
		if(hr != hrSuccess)
			return hr;
	}

	hr = AddRenAdditionalFolder(lpRootFolder, ulType, &lpPropValueEID->Value.bin);
	if (hr != hrSuccess)
		return hr;
	return AddRenAdditionalFolder(lpInboxFolder, ulType, &lpPropValueEID->Value.bin);
}

HRESULT ECMsgStore::create_store_public(ECMsgStore *store,
    IMAPIFolder *root, IMAPIFolder *st, const ENTRYID *user, size_t usize)
{
	object_ptr<IMAPIFolder> nst, fld, fld2, fld3;
	auto ret = CreateSpecialFolder(root, store, KC_T("NON_IPM_SUBTREE"),
	           KC_T(""), PR_NON_IPM_SUBTREE_ENTRYID, 0, nullptr, &~nst);
	if (ret != hrSuccess)
		return ret;
	ret = CreateSpecialFolder(root, store, KC_T("FINDER_ROOT"), KC_T(""),
	      PR_FINDER_ENTRYID, 0, nullptr, &~fld3);
	if (ret != hrSuccess)
		return ret;

	ECPERMISSION perm;
	object_ptr<IECSecurity> sec;
	perm.ulRights    = ecRightsFolderVisible | ecRightsReadAny | ecRightsCreateSubfolder | ecRightsEditOwned | ecRightsDeleteOwned;
	perm.ulState     = RIGHT_NEW | RIGHT_AUTOUPDATE_DENIED;
	perm.ulType      = ACCESS_TYPE_GRANT;
	perm.sUserId.cb  = g_cbEveryoneEid;
	perm.sUserId.lpb = g_lpEveryoneEid;
	ret = fld3->QueryInterface(IID_IECSecurity, &~sec);
	if (ret != hrSuccess)
		return ret;
	ret = sec->SetPermissionRules(1, &perm);
	if (ret != hrSuccess)
		return ret;
	ret = CreateSpecialFolder(nst, store, KC_T("SCHEDULE+ FREE BUSY"),
	      KC_T(""), PR_SPLUS_FREE_BUSY_ENTRYID, 0, nullptr, &~fld);
	if (ret != hrSuccess)
		return ret;

	/* Set ACLs on the folder */
	perm.ulRights    = ecRightsReadAny | ecRightsFolderVisible;
	perm.ulState     = RIGHT_NEW | RIGHT_AUTOUPDATE_DENIED;
	perm.ulType      = ACCESS_TYPE_GRANT;
	perm.sUserId.cb  = usize;
	perm.sUserId.lpb = reinterpret_cast<unsigned char *>(const_cast<ENTRYID *>(user));
	ret = fld->QueryInterface(IID_IECSecurity, &~sec);
	if (ret != hrSuccess)
		return ret;
	ret = sec->SetPermissionRules(1, &perm);
	if (ret != hrSuccess)
		return ret;
	ret = CreateSpecialFolder(fld, store, KC_T("Zarafa 1"), KC_T(""),
	      PR_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID, 0, nullptr, &~fld2);
	if (ret != hrSuccess)
		return ret;

	/* Set ACLs on the folder */
	perm.ulRights    = ecRightsAll; /* ecRightsReadAny | ecRightsCreate | ecRightsEditOwned | ecRightsDeleteOwned | ecRightsCreateSubfolder | ecRightsFolderVisible; */
	perm.ulState     = RIGHT_NEW | RIGHT_AUTOUPDATE_DENIED;
	perm.ulType      = ACCESS_TYPE_GRANT;
	perm.sUserId.cb  = usize;
	perm.sUserId.lpb = reinterpret_cast<unsigned char *>(const_cast<ENTRYID *>(user));
	ret = fld2->QueryInterface(IID_IECSecurity, &~sec);
	if (ret != hrSuccess)
		return ret;
	ret = sec->SetPermissionRules(1, &perm);
	if (ret != hrSuccess)
		return ret;

	/* Set ACLs on the IPM subtree folder */
	perm.ulRights    = ecRightsReadAny | ecRightsCreate | ecRightsEditOwned | ecRightsDeleteOwned | ecRightsCreateSubfolder | ecRightsFolderVisible;
	perm.ulState     = RIGHT_NEW | RIGHT_AUTOUPDATE_DENIED;
	perm.ulType      = ACCESS_TYPE_GRANT;
	perm.sUserId.cb  = usize;
	perm.sUserId.lpb = reinterpret_cast<unsigned char *>(const_cast<ENTRYID *>(user));
	ret = st->QueryInterface(IID_IECSecurity, &~sec);
	if (ret != hrSuccess)
		return ret;
	ret = sec->SetPermissionRules(1, &perm);
	if (ret != hrSuccess)
		return ret;

	/* indicate validity of the entry identifiers of the folders in a message store */
	/* Public folder have default the mask */
	memory_ptr<SPropValue> pv;
	ret = ECAllocateBuffer(sizeof(SPropValue) * 2, &~pv);
	if (ret != hrSuccess)
		return ret;
	pv[0].ulPropTag   = PR_VALID_FOLDER_MASK;
	pv[0].Value.ul    = FOLDER_FINDER_VALID | FOLDER_IPM_SUBTREE_VALID | FOLDER_IPM_INBOX_VALID | FOLDER_IPM_OUTBOX_VALID | FOLDER_IPM_WASTEBASKET_VALID | FOLDER_IPM_SENTMAIL_VALID | FOLDER_VIEWS_VALID | FOLDER_COMMON_VIEWS_VALID;
	pv[1].ulPropTag   = PR_DISPLAY_NAME_W;
	pv[1].Value.lpszW = const_cast<wchar_t *>(L"Public folder"); /* FIXME: set the right public folder name here? */
	return store->SetProps(2, pv, nullptr);
}

HRESULT ECMsgStore::create_store_private(ECMsgStore *store,
    ECMAPIFolder *ecroot, IMAPIFolder *root, IMAPIFolder *st)
{
	object_ptr<IMAPIFolder> fld, fld3, inbox, cal;

	/* folder holds views that are standard for the message store */
	auto ret = CreateSpecialFolder(root, store, KC_T("IPM_COMMON_VIEWS"),
	           KC_T(""), PR_COMMON_VIEWS_ENTRYID, 0, nullptr, nullptr);
	if (ret != hrSuccess)
		return ret;

	/* Personal: folder holds views that are defined by a particular user */
	ret = CreateSpecialFolder(root, store, KC_T("IPM_VIEWS"), KC_T(""),
	      PR_VIEWS_ENTRYID, 0, nullptr, nullptr);
	if (ret != hrSuccess)
		return ret;
	ret = CreateSpecialFolder(root, store, KC_T("FINDER_ROOT"), KC_T(""),
	      PR_FINDER_ENTRYID, 0, nullptr, &~fld3);
	if (ret != hrSuccess)
		return ret;

	object_ptr<IECSecurity> sec;
	ECPERMISSION perm;
	perm.ulRights    = ecRightsFolderVisible | ecRightsReadAny | ecRightsCreateSubfolder | ecRightsEditOwned | ecRightsDeleteOwned;
	perm.ulState     = RIGHT_NEW|RIGHT_AUTOUPDATE_DENIED;
	perm.ulType      = ACCESS_TYPE_GRANT;
	perm.sUserId.cb  = g_cbEveryoneEid;
	perm.sUserId.lpb = g_lpEveryoneEid;
	ret = fld3->QueryInterface(IID_IECSecurity, &~sec);
	if (ret != hrSuccess)
		return ret;
	ret = sec->SetPermissionRules(1, &perm);
	if (ret != hrSuccess)
		return ret;

	/* Shortcuts for the favorites */
	ret = CreateSpecialFolder(root, store, KC_TX("Shortcut"), KC_T(""),
	      PR_IPM_FAVORITES_ENTRYID, 0, nullptr, nullptr);
	if (ret != hrSuccess)
		return ret;
	ret = CreateSpecialFolder(root, store, KC_TX("Schedule"), KC_T(""),
	      PR_SCHEDULE_FOLDER_ENTRYID, 0, nullptr, nullptr);
	if (ret != hrSuccess)
		return ret;
	ret = CreateSpecialFolder(st, nullptr, KC_TX("Inbox"), KC_T(""),
	      0, 0, nullptr, &~inbox);
	if (ret != hrSuccess)
		return ret;

	memory_ptr<SPropValue> pv;
	ret = HrGetOneProp(inbox, PR_ENTRYID, &~pv);
	if (ret != hrSuccess)
		return ret;
	ret = store->SetReceiveFolder(nullptr, 0, pv->Value.bin.cb, reinterpret_cast<ENTRYID *>(pv->Value.bin.lpb));
	if (ret != hrSuccess)
		return ret;
	ret = store->SetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0,
	      pv->Value.bin.cb, reinterpret_cast<ENTRYID *>(pv->Value.bin.lpb));
	if (ret != hrSuccess)
		return ret;
	ret = store->SetReceiveFolder(reinterpret_cast<const TCHAR *>("REPORT.IPM"),
	      0, pv->Value.bin.cb, reinterpret_cast<ENTRYID *>(pv->Value.bin.lpb));
	if (ret != hrSuccess)
		return ret;
	object_ptr<ECMAPIFolder> ecinbox;
	ret = inbox->QueryInterface(IID_ECMAPIFolder, &~ecinbox);
	if (ret != hrSuccess)
		return ret;

	ret = CreateSpecialFolder(st, store, KC_TX("Outbox"), KC_T(""),
	      PR_IPM_OUTBOX_ENTRYID, 0, nullptr, nullptr);
	if (ret != hrSuccess)
		return ret;
	ret = CreateSpecialFolder(st, store, KC_TX("Deleted Items"), KC_T(""),
	      PR_IPM_WASTEBASKET_ENTRYID, 0, nullptr, nullptr);
	if (ret != hrSuccess)
		return ret;
	ret = CreateSpecialFolder(st, store, KC_TX("Sent Items"), KC_T(""),
	      PR_IPM_SENTMAIL_ENTRYID, 0, nullptr, nullptr);
	if (ret != hrSuccess)
		return ret;

	ret = CreateSpecialFolder(st, ecinbox, KC_TX("Contacts"), KC_T(""),
	      PR_IPM_CONTACT_ENTRYID, 0, KC_T("IPF.Contact"), &~fld);
	if (ret != hrSuccess)
		return ret;
	ret = SetSpecialEntryIdOnFolder(fld, ecroot, PR_IPM_CONTACT_ENTRYID, 0);
	if (ret != hrSuccess)
		return ret;

	ret = CreateSpecialFolder(st, ecinbox, KC_TX("Calendar"), KC_T(""),
	      PR_IPM_APPOINTMENT_ENTRYID, 0, KC_T("IPF.Appointment"), &~cal);
	if (ret != hrSuccess)
		return ret;
	ret = SetSpecialEntryIdOnFolder(cal, ecroot, PR_IPM_APPOINTMENT_ENTRYID, 0);
	if (ret != hrSuccess)
		return ret;

	ret = CreateSpecialFolder(st, ecinbox, KC_TX("Drafts"), KC_T(""),
	      PR_IPM_DRAFTS_ENTRYID, 0, KC_T("IPF.Note"), &~fld);
	if (ret != hrSuccess)
		return ret;
	ret = SetSpecialEntryIdOnFolder(fld, ecroot, PR_IPM_DRAFTS_ENTRYID, 0);
	if (ret != hrSuccess)
		return ret;

	ret = CreateSpecialFolder(st, ecinbox, KC_TX("Journal"), KC_T(""),
	      PR_IPM_JOURNAL_ENTRYID, 0, KC_T("IPF.Journal"), &~fld);
	if (ret != hrSuccess)
		return ret;
	ret = SetSpecialEntryIdOnFolder(fld, ecroot, PR_IPM_JOURNAL_ENTRYID, 0);
	if (ret != hrSuccess)
		return ret;

	ret = CreateSpecialFolder(st, ecinbox, KC_TX("Notes"), KC_T(""),
	      PR_IPM_NOTE_ENTRYID, 0, KC_T("IPF.StickyNote"), &~fld);
	if (ret != hrSuccess)
		return ret;
	ret = SetSpecialEntryIdOnFolder(fld, ecroot, PR_IPM_NOTE_ENTRYID, 0);
	if (ret != hrSuccess)
		return ret;

	ret = CreateSpecialFolder(st, ecinbox, KC_TX("Tasks"), KC_T(""),
	      PR_IPM_TASK_ENTRYID, 0, KC_T("IPF.Task"), &~fld);
	if (ret != hrSuccess)
		return ret;
	ret = SetSpecialEntryIdOnFolder(fld, ecroot, PR_IPM_TASK_ENTRYID, 0);
	if (ret != hrSuccess)
		return ret;

	/* Create Junk mail (position 5(4 in array) in the mvprop PR_ADDITIONAL_REN_ENTRYIDS) */
	ret = CreateSpecialFolder(st, ecinbox, KC_TX("Junk E-mail"), KC_T(""),
	      PR_ADDITIONAL_REN_ENTRYIDS, 4, KC_T("IPF.Note"), &~fld);
	if (ret != hrSuccess)
		return ret;
	ret = SetSpecialEntryIdOnFolder(fld, ecroot, PR_ADDITIONAL_REN_ENTRYIDS, 4);
	if (ret != hrSuccess)
		return ret;
	ret = CreatePrivateFreeBusyData(root, inbox, cal);
	if (ret != hrSuccess)
		return ret;

	/* Create Outlook 2007/2010 additional folders */
	ret = CreateAdditionalFolder(root, inbox, st, RSF_PID_RSS_SUBSCRIPTION,
	      KC_TX("RSS Feeds"), KC_TX("RSS Feed comment"), KC_T("IPF.Note.OutlookHomepage"), false);
	if (ret != hrSuccess)
		return ret;
	ret = CreateAdditionalFolder(root, inbox, st, RSF_PID_CONV_ACTIONS,
	      KC_TX("Conversation Action Settings"), KC_T(""), KC_T("IPF.Configuration"), true);
	if (ret != hrSuccess)
		return ret;
	ret = CreateAdditionalFolder(root, inbox, st, RSF_PID_COMBINED_ACTIONS,
	      KC_TX("Quick Step Settings"), KC_T(""), KC_T("IPF.Configuration"), true);
	if (ret != hrSuccess)
		return ret;
	ret = CreateAdditionalFolder(root, inbox, st, RSF_PID_SUGGESTED_CONTACTS,
	      KC_TX("Suggested Contacts"), KC_T(""), KC_T("IPF.Contact"), false);
	if (ret != hrSuccess)
		return ret;

	/* indicate validity of the entry identifiers of the folders in a message store */
	ret = ECAllocateBuffer(sizeof(SPropValue), &~pv);
	if (ret != hrSuccess)
		return ret;
	pv[0].ulPropTag = PR_VALID_FOLDER_MASK;
	pv[0].Value.ul = FOLDER_VIEWS_VALID | FOLDER_COMMON_VIEWS_VALID |
	                 FOLDER_FINDER_VALID | FOLDER_IPM_INBOX_VALID |
	                 FOLDER_IPM_OUTBOX_VALID | FOLDER_IPM_SENTMAIL_VALID |
	                 FOLDER_IPM_SUBTREE_VALID | FOLDER_IPM_WASTEBASKET_VALID;
	return store->SetProps(1, pv, nullptr);
}

HRESULT ECMsgStore::CreateStore(ULONG ulStoreType, ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG *lpcbStoreId, ENTRYID **lppStoreId,
    ULONG *lpcbRootId, ENTRYID **lppRootId)
{
	object_ptr<WSTransport> lpTempTransport;
	object_ptr<ECMsgStore> lpecMsgStore;
	object_ptr<ECMAPIFolder> lpMapiFolderRoot;
	/* Root container, IPM_SUBTREE and NON_IPM_SUBTREE */
	object_ptr<IMAPIFolder> lpFolderRoot, lpFolderRootST;
	object_ptr<IECPropStorage> storage;
	unsigned int objtype = 0, cbStoreId = 0, cbRootId = 0;
	ecmem_ptr<ECUSER> lpECUser;
	ecmem_ptr<ECCOMPANY> lpECCompany;
	ecmem_ptr<ECGROUP> lpECGroup;
	ENTRYID *lpStoreId = nullptr, *lpRootId = nullptr;

	auto hr = CreateEmptyStore(ulStoreType, cbUserId, lpUserId, 0, &cbStoreId, &lpStoreId, &cbRootId, &lpRootId);
	if (hr != hrSuccess)
		return hr;
	/*
	 * Create a temporary transport, because the new ECMsgStore object will
	 * logoff the transport, even if the refcount is not 0 yet. That would
	 * cause the current instance to lose its transport.
	 */
	hr = lpTransport->CloneAndRelogon(&~lpTempTransport);
	if (hr != hrSuccess)
		return hr;

	// Open the created messagestore
	hr = ECMsgStore::Create("", lpSupport, lpTempTransport, true, MAPI_BEST_ACCESS, false, false, false, &~lpecMsgStore);
	if (hr != hrSuccess)
		//FIXME: what todo with created store?
		return hr;
	if (ulStoreType == ECSTORE_TYPE_PRIVATE)
		memcpy(&lpecMsgStore->m_guidMDB_Provider, &KOPANO_SERVICE_GUID, sizeof(MAPIUID));
	else
		memcpy(&lpecMsgStore->m_guidMDB_Provider, &KOPANO_STORE_PUBLIC_GUID, sizeof(MAPIUID));

	// Get user or company information depending on the store type.
	if (ulStoreType == ECSTORE_TYPE_PRIVATE)
		hr = lpTransport->HrGetUser(cbUserId, lpUserId, 0, &~lpECUser);
	else if (ABEID_ID(lpUserId) == 1)
		/* Public store, ownership set to group EVERYONE*/
		hr = lpTransport->HrGetGroup(cbUserId, lpUserId, 0, &~lpECGroup);
	else
		/* Public store, ownership set to company */
		hr = lpTransport->HrGetCompany(cbUserId, lpUserId, 0, &~lpECCompany);
	if (hr != hrSuccess)
		return hr;

	// Get a propstorage for the message store
	hr = lpTransport->HrOpenPropStorage(0, nullptr, cbStoreId, lpStoreId, 0, &~storage);
	if(hr != hrSuccess)
		return hr;
	// Set up the message store to use this storage
	hr = lpecMsgStore->HrSetPropStorage(storage, true);
	if(hr != hrSuccess)
		return hr;
	hr = lpecMsgStore->SetEntryId(cbStoreId, lpStoreId);
	if(hr != hrSuccess)
		return hr;
	// Open rootfolder
	hr = lpecMsgStore->OpenEntry(cbRootId, lpRootId, &IID_ECMAPIFolder, MAPI_MODIFY, &objtype, &~lpMapiFolderRoot);
	if(hr != hrSuccess)
		return hr;

	// Set IPC folder
	if(ulStoreType == ECSTORE_TYPE_PRIVATE) {
		hr = lpecMsgStore->SetReceiveFolder(LPCTSTR("IPC"), 0, cbRootId, lpRootId);
		if(hr != hrSuccess)
			return hr;
	}

	// Create Folder IPM_SUBTREE into the rootfolder
	hr = lpMapiFolderRoot->QueryInterface(IID_IMAPIFolder, &~lpFolderRoot);
	if(hr != hrSuccess)
		return hr;
	hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, KC_T("IPM_SUBTREE"), KC_T(""), PR_IPM_SUBTREE_ENTRYID, 0, NULL, &~lpFolderRootST);
	if(hr != hrSuccess)
		return hr;

	if (ulStoreType == ECSTORE_TYPE_PUBLIC)
		hr = create_store_public(lpecMsgStore, lpFolderRoot, lpFolderRootST, lpUserId, cbUserId);
	else if (ulStoreType == ECSTORE_TYPE_PRIVATE)
		hr = create_store_private(lpecMsgStore, lpMapiFolderRoot, lpFolderRoot, lpFolderRootST);
	if (hr != hrSuccess)
		return hr;

	*lpcbStoreId = cbStoreId;
	*lppStoreId = lpStoreId;

	*lpcbRootId = cbRootId;
	*lppRootId = lpRootId;
	return hrSuccess;
}

/**
 * Create a new store that contains nothing but the root folder.
 *
 * @param[in]		ulStoreType
 * 						The required store type to create. Valid values are ECSTORE_TYPE_PUBLIC and ECSTORE_TYPE_PRIVATE.
 * @param[in]		cbUserId
 * 						The size of the user entryid of the user for whom the store will be created.
 * @param[in]		lpUserId
 * 						Pointer to the entryid of the user for whom the store will be created.
 * @param[in]		ulFlags
 * 						Flags passed to HrCreateStore
 * @param[in,out]	lpcbStoreId
 * 						Pointer to a ULONG value that contains the size of the store entryid.
 * @param[in,out]	lppStoreId
 * 						Pointer to a an entryid pointer that points to the store entryid.
 * @param[in,out]	lpcbRootId
 * 						Pointer to a ULONG value that contains the size of the root entryid.
 * @param[in,out]	lppRootId
 * 						Pointer to an entryid pointer that points to the root entryid.
 *
 * @remarks
 * lpcbStoreId, lppStoreId, lpcbRootId and lppRootId are optional. But if a root id is specified, the store id must
 * also be specified. A store id however may be passed without passing a root id.
 */
HRESULT ECMsgStore::CreateEmptyStore(ULONG ulStoreType, ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG ulFlags, ULONG *lpcbStoreId,
    ENTRYID **lppStoreId, ULONG *lpcbRootId, ENTRYID **lppRootId)
{
	if (!ECSTORE_TYPE_ISVALID(ulStoreType) ||
	    (ulFlags != 0 && ulFlags != EC_OVERRIDE_HOMESERVER))
		return MAPI_E_INVALID_PARAMETER;
	if (lpcbStoreId == nullptr || lppStoreId == nullptr ||
	    lpcbRootId == nullptr || lppRootId == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if ((*lpcbStoreId == 0) != (*lppStoreId == nullptr))
		/* One set, one unset */
		return MAPI_E_INVALID_PARAMETER;
	if ((*lpcbRootId == 0) != (*lppRootId == nullptr))
		/* One set, one unset */
		return MAPI_E_INVALID_PARAMETER;
	if (*lppRootId != nullptr && *lppStoreId == nullptr) /* Root id set, but storeid unset */
		return MAPI_E_INVALID_PARAMETER;

	unsigned int cbStoreId = 0, cbRootId = 0;
	ENTRYID *lpStoreId = nullptr, *lpRootId = nullptr;
	GUID guidStore;

	if ((*lpcbStoreId == 0 || *lpcbRootId == 0) && CoCreateGuid(&guidStore) != S_OK)
		return MAPI_E_CALL_FAILED;

	auto laters = make_scope_success([&]() {
		if (lpcbStoreId != nullptr && *lpcbStoreId == 0)
			MAPIFreeBuffer(lpStoreId);
		if (lpcbRootId != nullptr && *lpcbRootId == 0)
			MAPIFreeBuffer(lpRootId);
	});

	if (*lpcbStoreId == 0) {
		// Create store entryid
		auto hr = HrCreateEntryId(guidStore, MAPI_STORE, &cbStoreId, &lpStoreId);
		if (hr != hrSuccess)
			return hr;
	} else {
		ULONG cbTmp = 0;
		LPENTRYID lpTmp = NULL;

		auto hr = UnWrapStoreEntryID(*lpcbStoreId, *lppStoreId, &cbTmp, &lpTmp);
		if (hr == MAPI_E_INVALID_ENTRYID) {	// Could just be a non-wrapped entryid
			cbTmp = *lpcbStoreId;
			lpTmp = *lppStoreId;
		}
		hr = UnWrapServerClientStoreEntry(cbTmp, lpTmp, &cbStoreId, &lpStoreId);
		if (hr != hrSuccess) {
			if (lpTmp != *lppStoreId)
				MAPIFreeBuffer(lpTmp);
			return hr;
		}
	}

	if (*lpcbRootId == 0) {
		// create root entryid
		auto hr = HrCreateEntryId(guidStore, MAPI_FOLDER, &cbRootId, &lpRootId);
		if (hr != hrSuccess)
			return hr;
	} else {
		cbRootId = *lpcbRootId;
		lpRootId = *lppRootId;
	}

	// Create the messagestore
	auto hr = lpTransport->HrCreateStore(ulStoreType, cbUserId, lpUserId, cbStoreId, lpStoreId, cbRootId, lpRootId, ulFlags);
	if (hr != hrSuccess)
		return hr;

	if (*lppStoreId == 0) {
		*lpcbStoreId = cbStoreId;
		*lppStoreId = lpStoreId;
		lpStoreId = NULL;
	}

	if (*lpcbRootId == 0) {
		*lpcbRootId = cbRootId;
		*lppRootId = lpRootId;
		lpRootId = NULL;
	}
	return hrSuccess;
}

HRESULT ECMsgStore::HookStore(ULONG ulStoreType, ULONG cbUserId,
    const ENTRYID *lpUserId, const GUID *lpGuid)
{
	return lpTransport->HrHookStore(ulStoreType, cbUserId, lpUserId, lpGuid, 0);
}

HRESULT ECMsgStore::UnhookStore(ULONG ulStoreType, ULONG cbUserId,
    const ENTRYID *lpUserId)
{
	return lpTransport->HrUnhookStore(ulStoreType, cbUserId, lpUserId, 0);
}

HRESULT ECMsgStore::RemoveStore(const GUID *lpGuid)
{
	return lpTransport->HrRemoveStore(lpGuid, 0);
}

HRESULT ECMsgStore::SetSpecialEntryIdOnFolder(LPMAPIFOLDER lpFolder, ECMAPIProp *lpFolderPropSet, unsigned int ulPropTag, unsigned int ulMVPos)
{
	ecmem_ptr<SPropValue> lpPropValue, lpPropMVValueNew;
	LPSPropValue	lpPropMVValue = NULL;

	// Get entryid of the folder
	HRESULT hr = HrGetOneProp(lpFolder, PR_ENTRYID, &~lpPropValue);
	if(hr != hrSuccess)
		return hr;

	if (!(PROP_TYPE(ulPropTag) & MV_FLAG)) {
		lpPropValue->ulPropTag = ulPropTag;
		return lpFolderPropSet->SetProps(1, lpPropValue, nullptr);
	}

	hr = ECAllocateBuffer(sizeof(SPropValue), &~lpPropMVValueNew);
	if (hr != hrSuccess)
		return hr;
	memset(lpPropMVValueNew, 0, sizeof(SPropValue));

	hr = HrGetOneProp(lpFolder, ulPropTag, &lpPropMVValue);
	if(hr != hrSuccess) {
		lpPropMVValueNew->Value.MVbin.cValues = (ulMVPos+1);
		hr = ECAllocateMore(sizeof(SBinary) * lpPropMVValueNew->Value.MVbin.cValues, lpPropMVValueNew,
		     reinterpret_cast<void **>(&lpPropMVValueNew->Value.MVbin.lpbin));
		if (hr != hrSuccess)
			return hr;
		memset(lpPropMVValueNew->Value.MVbin.lpbin, 0, sizeof(SBinary)*lpPropMVValueNew->Value.MVbin.cValues);

		for (unsigned int i = 0; i <lpPropMVValueNew->Value.MVbin.cValues; ++i)
			if(ulMVPos == i)
				lpPropMVValueNew->Value.MVbin.lpbin[i] = lpPropValue->Value.bin;
	}else{
		lpPropMVValueNew->Value.MVbin.cValues = (lpPropMVValue->Value.MVbin.cValues < ulMVPos)? lpPropValue->Value.bin.cb : ulMVPos+1;
		hr = ECAllocateMore(sizeof(SBinary) * lpPropMVValueNew->Value.MVbin.cValues, lpPropMVValueNew,
		     reinterpret_cast<void **>(&lpPropMVValueNew->Value.MVbin.lpbin));
		if (hr != hrSuccess)
			return hr;
		memset(lpPropMVValueNew->Value.MVbin.lpbin, 0, sizeof(SBinary)*lpPropMVValueNew->Value.MVbin.cValues);

		for (unsigned int i = 0; i < lpPropMVValueNew->Value.MVbin.cValues; ++i)
			if(ulMVPos == i)
				lpPropMVValueNew->Value.MVbin.lpbin[i] = lpPropValue->Value.bin;
			else
				lpPropMVValueNew->Value.MVbin.lpbin[i] = lpPropMVValue->Value.MVbin.lpbin[i];
	}

	lpPropMVValueNew->ulPropTag = ulPropTag;
	return lpFolderPropSet->SetProps(1, lpPropMVValueNew, nullptr);
}

HRESULT ECMsgStore::CreateSpecialFolder(IMAPIFolder *folder_parent_in,
    ECMAPIProp *folder_propset_in, const TCHAR *lpszFolderName,
    const TCHAR *lpszFolderComment, unsigned int ulPropTag,
    unsigned int ulMVPos, const TCHAR *lpszContainerClass,
    LPMAPIFOLDER *lppMAPIFolder)
{
	if (folder_parent_in == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<IMAPIFolder> lpMAPIFolder;
	ecmem_ptr<SPropValue> lpPropValue;
	/* Add a reference to the folders */
	object_ptr<IMAPIFolder> lpFolderParent(folder_parent_in);
	object_ptr<ECMAPIProp> lpFolderPropSet(folder_propset_in);

	// Create the folder
	auto hr = lpFolderParent->CreateFolder(FOLDER_GENERIC,
	          const_cast<TCHAR *>(lpszFolderName),
	          const_cast<TCHAR *>(lpszFolderComment), &IID_IMAPIFolder,
	          OPEN_IF_EXISTS | fMapiUnicode, &~lpMAPIFolder);
	if(hr != hrSuccess)
		return hr;

	// Set the special property
	if(lpFolderPropSet) {
		hr = SetSpecialEntryIdOnFolder(lpMAPIFolder, lpFolderPropSet, ulPropTag, ulMVPos);
		if(hr != hrSuccess)
			return hr;
	}

	if (lpszContainerClass && _tcslen(lpszContainerClass) > 0) {
		hr = ECAllocateBuffer(sizeof(SPropValue), &~lpPropValue);
		if (hr != hrSuccess)
			return hr;
		lpPropValue[0].ulPropTag = PR_CONTAINER_CLASS;
		hr = ECAllocateMore((_tcslen(lpszContainerClass) + 1) * sizeof(TCHAR), lpPropValue,
		     reinterpret_cast<void **>(&lpPropValue[0].Value.LPSZ));
		if (hr != hrSuccess)
			return hr;
		_tcscpy(lpPropValue[0].Value.LPSZ, lpszContainerClass);

		// Set the property
		hr = lpMAPIFolder->SetProps(1, lpPropValue, NULL);
		if(hr != hrSuccess)
			return hr;
	}

	if (lppMAPIFolder != nullptr)
		hr = lpMAPIFolder->QueryInterface(IID_IMAPIFolder, (void**)lppMAPIFolder);
	return hr;
}

HRESULT ECMsgStore::CreateUser(ECUSER *lpECUser, ULONG ulFlags,
    ULONG *lpcbUserId, LPENTRYID *lppUserId)
{
	return lpTransport->HrCreateUser(lpECUser, ulFlags, lpcbUserId, lppUserId);
}

HRESULT ECMsgStore::SetUser(ECUSER *lpECUser, ULONG ulFlags)
{
	return lpTransport->HrSetUser(lpECUser, ulFlags);
}

HRESULT ECMsgStore::GetUser(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG ulFlags, ECUSER **lppECUser)
{
	return lpTransport->HrGetUser(cbUserId, lpUserId, ulFlags, lppECUser);
}

HRESULT ECMsgStore::DeleteUser(ULONG cbUserId, const ENTRYID *lpUserId)
{
	return lpTransport->HrDeleteUser(cbUserId, lpUserId);
}

HRESULT ECMsgStore::ResolveUserName(LPCTSTR lpszUserName, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId)
{
	return lpTransport->HrResolveUserName(lpszUserName, ulFlags, lpcbUserId, lppUserId);
}

HRESULT ECMsgStore::GetSendAsList(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG ulFlags, ULONG *lpcSenders, ECUSER **lppSenders)
{
	return lpTransport->HrGetSendAsList(cbUserId, lpUserId, ulFlags, lpcSenders, lppSenders);
}

HRESULT ECMsgStore::AddSendAsUser(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG cbSenderId, const ENTRYID *lpSenderId)
{
	return lpTransport->HrAddSendAsUser(cbUserId, lpUserId, cbSenderId, lpSenderId);
}

HRESULT ECMsgStore::DelSendAsUser(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG cbSenderId, const ENTRYID *lpSenderId)
{
	return lpTransport->HrDelSendAsUser(cbUserId, lpUserId, cbSenderId, lpSenderId);
}

HRESULT ECMsgStore::ResolveGroupName(LPCTSTR lpszGroupName, ULONG ulFlags, ULONG *lpcbGroupId, LPENTRYID *lppGroupId)
{
	return lpTransport->HrResolveGroupName(lpszGroupName, ulFlags, lpcbGroupId, lppGroupId);
}

HRESULT ECMsgStore::CreateGroup(ECGROUP *lpECGroup, ULONG ulFlags,
    ULONG *lpcbGroupId, LPENTRYID *lppGroupId)
{
	return lpTransport->HrCreateGroup(lpECGroup, ulFlags, lpcbGroupId, lppGroupId);
}

HRESULT ECMsgStore::SetGroup(ECGROUP *lpECGroup, ULONG ulFlags)
{
	return lpTransport->HrSetGroup(lpECGroup, ulFlags);
}

HRESULT ECMsgStore::GetGroup(ULONG cbGroupId, const ENTRYID *lpGroupId,
    ULONG ulFlags, ECGROUP **lppECGroup)
{
	return lpTransport->HrGetGroup(cbGroupId, lpGroupId, ulFlags, lppECGroup);
}

HRESULT ECMsgStore::DeleteGroup(ULONG cbGroupId, const ENTRYID *lpGroupId)
{
	return lpTransport->HrDeleteGroup(cbGroupId, lpGroupId);
}

//Group and user functions
HRESULT ECMsgStore::DeleteGroupUser(ULONG cbGroupId, const ENTRYID *lpGroupId,
    ULONG cbUserId, const ENTRYID *lpUserId)
{
	return lpTransport->HrDeleteGroupUser(cbGroupId, lpGroupId, cbUserId, lpUserId);
}

HRESULT ECMsgStore::AddGroupUser(ULONG cbGroupId, const ENTRYID *lpGroupId,
    ULONG cbUserId, const ENTRYID *lpUserId)
{
	return lpTransport->HrAddGroupUser(cbGroupId, lpGroupId, cbUserId, lpUserId);
}

HRESULT ECMsgStore::GetUserListOfGroup(ULONG cbGroupId,
    const ENTRYID *lpGroupId, ULONG ulFlags, ULONG *lpcUsers,
    ECUSER **lppsUsers)
{
	return lpTransport->HrGetUserListOfGroup(cbGroupId, lpGroupId, ulFlags, lpcUsers, lppsUsers);
}

HRESULT ECMsgStore::GetGroupListOfUser(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups)
{
	return lpTransport->HrGetGroupListOfUser(cbUserId, lpUserId, ulFlags, lpcGroups, lppsGroups);
}

HRESULT ECMsgStore::CreateCompany(ECCOMPANY *lpECCompany, ULONG ulFlags,
    ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId)
{
	return lpTransport->HrCreateCompany(lpECCompany, ulFlags, lpcbCompanyId, lppCompanyId);
}

HRESULT ECMsgStore::DeleteCompany(ULONG cbCompanyId,
    const ENTRYID *lpCompanyId)
{
	return lpTransport->HrDeleteCompany(cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::SetCompany(ECCOMPANY *lpECCompany, ULONG ulFlags)
{
	return lpTransport->HrSetCompany(lpECCompany, ulFlags);
}

HRESULT ECMsgStore::GetCompany(ULONG cbCompanyId, const ENTRYID *lpCompanyId,
    ULONG ulFlags, ECCOMPANY **lppECCompany)
{
	return lpTransport->HrGetCompany(cbCompanyId, lpCompanyId, ulFlags, lppECCompany);
}

HRESULT ECMsgStore::ResolveCompanyName(LPCTSTR lpszCompanyName, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId)
{
	return lpTransport->HrResolveCompanyName(lpszCompanyName, ulFlags, lpcbCompanyId, lppCompanyId);
}

HRESULT ECMsgStore::AddCompanyToRemoteViewList(ULONG cbSetCompanyId,
    const ENTRYID *lpSetCompanyId, ULONG cbCompanyId,
    const ENTRYID *lpCompanyId)
{
	return lpTransport->HrAddCompanyToRemoteViewList(cbSetCompanyId, lpSetCompanyId, cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::DelCompanyFromRemoteViewList(ULONG cbSetCompanyId,
    const ENTRYID *lpSetCompanyId, ULONG cbCompanyId,
    const ENTRYID *lpCompanyId)
{
	return lpTransport->HrDelCompanyFromRemoteViewList(cbSetCompanyId, lpSetCompanyId, cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::GetRemoteViewList(ULONG cbCompanyId,
    const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *lpcCompanies,
    ECCOMPANY **lppsCompanies)
{
	return lpTransport->HrGetRemoteViewList(cbCompanyId, lpCompanyId, ulFlags, lpcCompanies, lppsCompanies);
}

HRESULT ECMsgStore::AddUserToRemoteAdminList(ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG cbCompanyId, const ENTRYID *lpCompanyId)
{
	return lpTransport->HrAddUserToRemoteAdminList(cbUserId, lpUserId, cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::DelUserFromRemoteAdminList(ULONG cbUserId,
    const ENTRYID *lpUserId, ULONG cbCompanyId, const ENTRYID *lpCompanyId)
{
	return lpTransport->HrDelUserFromRemoteAdminList(cbUserId, lpUserId, cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::GetRemoteAdminList(ULONG cbCompanyId,
    const ENTRYID *lpCompanyId, ULONG ulFlags, ULONG *lpcUsers,
    ECUSER **lppsUsers)
{
	return lpTransport->HrGetRemoteAdminList(cbCompanyId, lpCompanyId, ulFlags, lpcUsers, lppsUsers);
}

HRESULT ECMsgStore::SyncUsers(ULONG cbCompanyId, const ENTRYID *lpCompanyId)
{
	return lpTransport->HrSyncUsers(cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::GetQuota(ULONG cbUserId, const ENTRYID *lpUserId,
    bool bGetUserDefault, ECQUOTA **lppsQuota)
{
	return lpTransport->GetQuota(cbUserId, lpUserId, bGetUserDefault, lppsQuota);
}

HRESULT ECMsgStore::SetQuota(ULONG cbUserId, const ENTRYID *lpUserId,
    ECQUOTA *lpsQuota)
{
	return lpTransport->SetQuota(cbUserId, lpUserId, lpsQuota);
}

HRESULT ECMsgStore::AddQuotaRecipient(ULONG cbCompanyId,
    const ENTRYID *lpCompanyId, ULONG cbRecipientId,
    const ENTRYID *lpRecipientId, ULONG ulType)
{
	return lpTransport->AddQuotaRecipient(cbCompanyId, lpCompanyId, cbRecipientId, lpRecipientId, ulType);
}

HRESULT ECMsgStore::DeleteQuotaRecipient(ULONG cbCompanyId,
    const ENTRYID *lpCmopanyId, ULONG cbRecipientId,
    const ENTRYID *lpRecipientId, ULONG ulType)
{
	return lpTransport->DeleteQuotaRecipient(cbCompanyId, lpCmopanyId, cbRecipientId, lpRecipientId, ulType);
}

HRESULT ECMsgStore::GetQuotaRecipients(ULONG cbUserId, const ENTRYID *lpUserId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	return lpTransport->GetQuotaRecipients(cbUserId, lpUserId, ulFlags, lpcUsers, lppsUsers);
}

HRESULT ECMsgStore::GetQuotaStatus(ULONG cbUserId, const ENTRYID *lpUserId,
    ECQUOTASTATUS **lppsQuotaStatus)
{
	return lpTransport->GetQuotaStatus(cbUserId, lpUserId, lppsQuotaStatus);
}

HRESULT ECMsgStore::PurgeSoftDelete(ULONG ulDays)
{
	return lpTransport->HrPurgeSoftDelete(ulDays);
}

HRESULT ECMsgStore::PurgeCache(ULONG ulFlags)
{
	return lpTransport->HrPurgeCache(ulFlags);
}

HRESULT ECMsgStore::PurgeDeferredUpdates(ULONG *lpulRemaining)
{
	return lpTransport->HrPurgeDeferredUpdates(lpulRemaining);
}

HRESULT ECMsgStore::GetServerDetails(ECSVRNAMELIST *lpServerNameList,
    ULONG ulFlags, ECSERVERLIST **lppsServerList)
{
	return lpTransport->HrGetServerDetails(lpServerNameList, ulFlags, lppsServerList);
}

HRESULT ECMsgStore::OpenUserStoresTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	if (lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<WSTableView> lpTableView;
	object_ptr<ECMAPITable> lpTable;
	// notifications? set 1st param: m_lpNotifyClient
	auto hr = ECMAPITable::Create("Userstores table", nullptr, 0, &~lpTable);
	if (hr != hrSuccess)
		return hr;

	// open store table view, no entryid req.
	hr = lpTransport->HrOpenMiscTable(TABLETYPE_USERSTORES, ulFlags, 0, NULL, this, &~lpTableView);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableView, true);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);
	if (hr != hrSuccess)
		return hr;
	AddChild(lpTable);
	return hrSuccess;
}

HRESULT ECMsgStore::ResolvePseudoUrl(const char *lpszPseudoUrl,
    char **lppszServerPath, bool *lpbIsPeer)
{
	return lpTransport->HrResolvePseudoUrl(lpszPseudoUrl, lppszServerPath, lpbIsPeer);
}

HRESULT ECMsgStore::GetArchiveStoreEntryID(LPCTSTR lpszUserName, LPCTSTR lpszServerName, ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID)
{
	if (lpszUserName == nullptr || lpcbStoreID == nullptr ||
	    lppStoreID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	ULONG cbStoreID;
	EntryIdPtr ptrStoreID;

	if (lpszServerName != NULL) {
		WSTransportPtr ptrTransport;
		auto hr = GetTransportToNamedServer(lpTransport, lpszServerName, ulFlags, &~ptrTransport);
		if (hr != hrSuccess)
			return hr;
		hr = ptrTransport->HrResolveTypedStore(convstring(lpszUserName, ulFlags), ECSTORE_TYPE_ARCHIVE, &cbStoreID, &~ptrStoreID);
		if (hr != hrSuccess)
			return hr;
	} else {
		auto hr = lpTransport->HrResolveTypedStore(convstring(lpszUserName, ulFlags), ECSTORE_TYPE_ARCHIVE, &cbStoreID, &~ptrStoreID);
		if (hr != hrSuccess)
			return hr;
	}

	return lpSupport->WrapStoreEntryID(cbStoreID, ptrStoreID, lpcbStoreID, lppStoreID);
}

HRESULT ECMsgStore::ResetFolderCount(ULONG cbEntryId, const ENTRYID *lpEntryId,
    ULONG *lpulUpdates)
{
	return lpTransport->HrResetFolderCount(cbEntryId, lpEntryId, lpulUpdates);
}

// This is almost the same as getting a 'normal' outgoing table, except we pass NULL as PEID for the store
HRESULT ECMsgStore::GetMasterOutgoingTable(ULONG ulFlags, IMAPITable ** lppOutgoingTable)
{
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableOutGoingQueue> lpTableOps;

	auto hr = ECMAPITable::Create("Master outgoing queue", m_lpNotifyClient, 0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = lpTransport->HrOpenTableOutGoingQueueOps(0, nullptr, this, &~lpTableOps);
	if(hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));

	if(hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppOutgoingTable);

	AddChild(lpTable);
	return hr;
}

HRESULT ECMsgStore::DeleteFromMasterOutgoingTable(ULONG cbEntryId,
    const ENTRYID *lpEntryId, ULONG ulFlags)
{
	// Check input/output variables
	if (lpEntryId == NULL)
		return MAPI_E_INVALID_PARAMETER;
	return lpTransport->HrFinishedMessage(cbEntryId, lpEntryId, EC_SUBMIT_MASTER | ulFlags);
}

// ProxyStoreObject
HRESULT ECMsgStore::UnwrapNoRef(LPVOID *ppvObject)
{
	if (ppvObject == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// Because the function UnwrapNoRef return a non referenced object, QueryInterface isn't needed.
	*ppvObject = static_cast<IMsgStore *>(&m_xMsgStoreProxy);
	return hrSuccess;
}

HRESULT ECMsgStore::TestPerform(const char *szCommand, unsigned int ulArgs,
    char **lpszArgs)
{
	return lpTransport->HrTestPerform(szCommand, ulArgs, lpszArgs);
}

HRESULT ECMsgStore::TestSet(const char *szName, const char *szValue)
{
	return lpTransport->HrTestSet(szName, szValue);
}

HRESULT ECMsgStore::TestGet(const char *szName, char **szValue)
{
	return lpTransport->HrTestGet(szName, szValue);
}

/**
 * Convert a message store DN to a pseudo URL.
 * A message store DN looks like the following: /o=Domain/ou=Location/cn=Configuration/cn=Servers/cn=<servername>/cn=Microsoft Private MDB
 *
 * This function checks if the last part is valid. This means that we have a 'cn=<servername>' followed by 'cn=Microsoft Private MDB'. The rest
 * of the DN is ignored. The returned pseudo url will look like: 'pseudo://<servername>'
 *
 * @param[in]	strMsgStoreDN
 *					The message store DN from which to extract the servername.
 * @param[out]	lpstrPseudoUrl
 *					Pointer to a std::string object that will be set to the resulting pseudo URL.
 *
 * @return	HRESULT
 * @retval	hrSuccess						Conversion succeeded.
 * @retval	MAPI_E_INVALID_PARAMETER		The provided message store DN does not match the minimum requirements needed
 *											to successfully parse it.
 * @retval	MAPI_E_NO_SUPPORT				If a server is not operating in multi server mode, the default servername is
 *											'Unknown'. This cannot be resolved. In that case MAPI_E_NO_SUPPORT is returned
 *											So a different strategy can be selected by the calling method.
 */
HRESULT ECMsgStore::MsgStoreDnToPseudoUrl(const utf8string &strMsgStoreDN, utf8string *lpstrPseudoUrl)
{
	auto parts = tokenize(strMsgStoreDN.str(), "/");

	// We need at least 2 parts.
	if (parts.size() < 2)
		return MAPI_E_INVALID_PARAMETER;

	// Check if the last part equals 'cn=Microsoft Private MDB'
	auto riPart = parts.crbegin();
	if (strcasecmp(riPart->c_str(), "cn=Microsoft Private MDB") != 0)
		return MAPI_E_INVALID_PARAMETER;

	// Check if the for last part starts with 'cn='
	++riPart;
	if (strncasecmp(riPart->c_str(), "cn=", 3) != 0)
		return MAPI_E_INVALID_PARAMETER;

	// If the server has no home server information for a user, the servername will be set to 'Unknown'
	// Return MAPI_E_NO_SUPPORT in that case.
	if (strcasecmp(riPart->c_str(), "cn=Unknown") == 0)
		return MAPI_E_NO_SUPPORT;

	*lpstrPseudoUrl = utf8string::from_string("pseudo://" + riPart->substr(3));
	return hrSuccess;
}

/**
 * Export a set of messages as stream.
 *
 * @param[in]	ulFlags		Flags used to determine which messages and what data is to be exported.
 * @param[in]	ulPropTag	PR_ENTRYID or PR_SOURCE_KEY. Specifies the identifier used in sChanges->sSourceKey
 * @param[in]	sChanges	The complete set of changes available.
 * @param[in]	ulStart		The index in sChanges that specifies the first message to export.
 * @param[in]	ulCount		The number of messages to export, starting at ulStart. This number will be decreased if less messages are available.
 * @param[in]	lpsProps	The set of proptags that will be returned as regular properties outside the stream.
 * @param[out]	lppsStreamExporter	The streamexporter that must be used to get the individual streams.
 *
 * @retval	MAPI_E_INVALID_PARAMETER	ulStart is larger than the number of changes available.
 * @retval	MAPI_E_UNABLE_TO_COMPLETE	ulCount is 0 after trunctation.
 */
HRESULT ECMsgStore::ExportMessageChangesAsStream(ULONG ulFlags, ULONG ulPropTag,
    const std::vector<ICSCHANGE> &sChanges, ULONG ulStart, ULONG ulCount,
    const SPropTagArray *lpsProps, WSMessageStreamExporter **lppsStreamExporter)
{
	if (ulStart > sChanges.size())
		return MAPI_E_INVALID_PARAMETER;
	if (ulStart + ulCount > sChanges.size())
		ulCount = sChanges.size() - ulStart;
	if (ulCount == 0)
		return MAPI_E_UNABLE_TO_COMPLETE;

	WSMessageStreamExporterPtr ptrStreamExporter;
	WSTransportPtr ptrTransport;
	// Need to clone the transport since we want to be able to use our own transport for other things
	// while the streaming is going on; you should be able to intermix Synchronize() calls on the exporter
	// with other MAPI calls which would normally be impossible since the stream is kept open between
	// Synchronize() calls.
	HRESULT hr = GetMsgStore()->lpTransport->CloneAndRelogon(&~ptrTransport);
	if (hr != hrSuccess)
		return hr;
	hr = ptrTransport->HrExportMessageChangesAsStream(ulFlags, ulPropTag, &sChanges.front(), ulStart, ulCount, lpsProps, &~ptrStreamExporter);
	if (hr != hrSuccess)
		return hr;

	*lppsStreamExporter = ptrStreamExporter.release();
	return hrSuccess;
}

// IMsgStoreProxy interface
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, Release, (void))

HRESULT ECMsgStore::xMsgStoreProxy::QueryInterface(REFIID refiid, void **lppInterface) {
	METHOD_PROLOGUE_(ECMsgStore, MsgStoreProxy);
	return pThis->QueryInterfaceProxy(refiid, lppInterface);
}

DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, Advise, (ULONG, eid_size), (const ENTRYID *, eid), (ULONG, evt_mask), (IMAPIAdviseSink *, sink), (ULONG *, conn))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, Unadvise, (ULONG, ulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, CompareEntryIDs, (ULONG, cbEntryID1), (const ENTRYID *, lpEntryID1), (ULONG, cbEntryID2), (const ENTRYID *, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, OpenEntry, (ULONG, cbEntryID), (const ENTRYID *, lpEntryID), (const IID *, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (IUnknown **, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, SetReceiveFolder, (const TCHAR *, cls), (ULONG, flags), (ULONG, eid_size), (const ENTRYID *, eid))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetReceiveFolder, (const TCHAR *, cls), (ULONG, flags), (ULONG *, eid_size), (ENTRYID **, eid), (TCHAR **, exp_class))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetReceiveFolderTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, StoreLogoff, (ULONG *, lpulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, AbortSubmit, (ULONG, eid_size), (const ENTRYID *, eid), (ULONG, flags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetOutgoingQueue, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, SetLockState, (LPMESSAGE, lpMessage), (ULONG, ulLockState))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, FinishedMsg, (ULONG, flags), (ULONG, eid_size), (const ENTRYID *, eid))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, NotifyNewMail, (const NOTIFICATION *, lpNotification))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetProps, (const SPropTagArray *, lpPropTagArray), (ULONG, ulFlags), (ULONG *, lpcValues), (SPropValue **, lppPropArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetPropList, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, SetProps, (ULONG, cValues), (const SPropValue *, lpPropArray), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, DeleteProps, (const SPropTagArray *, lpPropTagArray), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, CopyTo, (ULONG, ciidExclude), (LPCIID, rgiidExclude), (const SPropTagArray *, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (void *, lpDestObj), (ULONG, ulFlags), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, CopyProps, (const SPropTagArray *, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (void *, lpDestObj), (ULONG, ulFlags), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetNamesFromIDs, (SPropTagArray **, tags), (const GUID *, propset), (ULONG, flags), (ULONG *, nvals), (MAPINAMEID ***, names))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetIDsFromNames, (ULONG, cNames), (LPMAPINAMEID *, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))

// IECTestProtocol interface
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, TestPerform, (const char *, cmd), (unsigned int, argc), (char **, args))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, TestSet, (const char *, name), (const char *, value))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, TestGet, (const char *, name), (char **, value))

ECMSLogon::ECMSLogon(ECMsgStore *lpStore) :
	m_lpStore(lpStore)
{
	// Note we cannot AddRef() the store. This is because the reference order is:
	// ECMsgStore -> IMAPISupport -> ECMSLogon
	// Therefore AddRef()'ing the store from here would create a circular reference
}

HRESULT ECMSLogon::Create(ECMsgStore *lpStore, ECMSLogon **lppECMSLogon)
{
	return alloc_wrap<ECMSLogon>(lpStore).put(lppECMSLogon);
}

HRESULT ECMSLogon::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMSLogon, this);
	REGISTER_INTERFACE2(IMSLogon, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMSLogon::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_CALL_FAILED;
}

HRESULT ECMSLogon::Logoff(ULONG *lpulFlags)
{
	return hrSuccess;
}

HRESULT ECMSLogon::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	return m_lpStore->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
}

HRESULT ECMSLogon::CompareEntryIDs(ULONG cbEntryID1, const ENTRYID *lpEntryID1,
    ULONG cbEntryID2, const ENTRYID *lpEntryID2, ULONG ulFlags,
    ULONG *lpulResult)
{
	return m_lpStore->CompareEntryIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
}

HRESULT ECMSLogon::Advise(ULONG cbEntryID, const ENTRYID *lpEntryID,
    ULONG ulEventMask, IMAPIAdviseSink *lpAdviseSink, ULONG *lpulConnection)
{
	return m_lpStore->Advise(cbEntryID, lpEntryID, ulEventMask, lpAdviseSink, lpulConnection);
}

HRESULT ECMSLogon::Unadvise(ULONG ulConnection)
{
	return m_lpStore->Unadvise(ulConnection);
}

HRESULT ECMSLogon::OpenStatusEntry(LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPVOID *lppEntry)
{
	return MAPI_E_NO_SUPPORT;
}
