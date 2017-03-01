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

#include <kopano/platform.h>
#include <kopano/ECInterfaceDefs.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <mapiutil.h>
#include <edkguid.h>
#include <list>

#include <kopano/ECGetText.h>

#include "Mem.h"
#include "ECMessage.h"
#include "ECMsgStore.h"
#include "ECMAPITable.h"
#include "ECMAPIFolder.h"
#include "ECMAPIProp.h"
#include "WSTransport.h"

#include <kopano/ECTags.h>

#include <kopano/ECGuid.h>
#include <kopano/ECDebug.h>
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
typedef KCHL::memory_ptr<char> MAPIStringPtr;
typedef KCHL::object_ptr<WSTransport> WSTransportPtr;
typedef KCHL::object_ptr<ECMessage, IID_ECMessage> ECMessagePtr;

#include <kopano/charset/convstring.h>

using namespace std;
using namespace KCHL;

// FIXME: from libserver/ECMAPI.h
#define MSGFLAG_DELETED                           ((ULONG) 0x00000400)

static constexpr const SizedSPropTagArray(NUM_RFT_PROPS, sPropRFTColumns) =
{
	NUM_RFT_PROPS,
	{
		PR_ROWID,
		PR_INSTANCE_KEY,
		PR_ENTRYID,
		PR_RECORD_KEY,
		PR_MESSAGE_CLASS_A
	}
};

/**
 * ECMsgStore
 **/
ECMsgStore::ECMsgStore(const char *lpszProfname, LPMAPISUP lpSupport,
    WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags,
    BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore) :
	ECMAPIProp(NULL, MAPI_STORE, fModify, NULL, "IMsgStore"),
	m_ulProfileFlags(ulProfileFlags), m_fIsSpooler(fIsSpooler),
	m_fIsDefaultStore(fIsDefaultStore), m_bOfflineStore(bOfflineStore)
{
	TRACE_MAPI(TRACE_ENTRY, "ECMsgStore::ECMsgStore","");

	this->lpSupport = lpSupport;
	lpSupport->AddRef();

	this->lpTransport = lpTransport;
	lpTransport->AddRef();

	// Add our property handlers
	HrAddPropHandlers(PR_ENTRYID,			GetPropHandler,			DefaultSetPropComputed, (void *)this);
	HrAddPropHandlers(PR_RECORD_KEY,		GetPropHandler,			DefaultSetPropComputed, (void *)this);
	HrAddPropHandlers(PR_SEARCH_KEY,		GetPropHandler,			DefaultSetPropComputed, (void *)this);
	HrAddPropHandlers(PR_USER_NAME	,		GetPropHandler,			DefaultSetPropComputed, (void *)this);
	HrAddPropHandlers(PR_USER_ENTRYID,		GetPropHandler,			DefaultSetPropComputed, (void *)this);

	HrAddPropHandlers(PR_MAILBOX_OWNER_NAME,	GetPropHandler,		DefaultSetPropComputed, (void *)this);
	HrAddPropHandlers(PR_MAILBOX_OWNER_ENTRYID,	GetPropHandler,		DefaultSetPropComputed, (void *)this);
	HrAddPropHandlers(PR_USER_NAME,				GetPropHandler,		DefaultSetPropComputed, (void *)this);
	HrAddPropHandlers(PR_USER_ENTRYID,			GetPropHandler,		DefaultSetPropComputed, (void *)this);

	HrAddPropHandlers(PR_RECEIVE_FOLDER_SETTINGS,	GetPropHandler,		DefaultSetPropIgnore,	(void*) this, FALSE, FALSE);

	HrAddPropHandlers(PR_MESSAGE_SIZE,				GetPropHandler,		DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	HrAddPropHandlers(PR_MESSAGE_SIZE_EXTENDED,		GetPropHandler,		DefaultSetPropComputed,	(void*) this, FALSE, FALSE);

	HrAddPropHandlers(PR_QUOTA_WARNING_THRESHOLD,	GetPropHandler,		DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	HrAddPropHandlers(PR_QUOTA_SEND_THRESHOLD,		GetPropHandler,		DefaultSetPropComputed,	(void*) this, FALSE, FALSE);
	HrAddPropHandlers(PR_QUOTA_RECEIVE_THRESHOLD,	GetPropHandler,		DefaultSetPropComputed,	(void*) this, FALSE, FALSE);

	HrAddPropHandlers(PR_STORE_OFFLINE,	GetPropHandler,		DefaultSetPropComputed, (void *)this);

	// only on admin store? how? .. now checked on server in ECTableManager
	HrAddPropHandlers(PR_EC_STATSTABLE_SYSTEM,		GetPropHandler,		DefaultSetPropComputed, (void*) this, FALSE, TRUE);
	HrAddPropHandlers(PR_EC_STATSTABLE_SESSIONS,	GetPropHandler,		DefaultSetPropComputed, (void*) this, FALSE, TRUE);
	HrAddPropHandlers(PR_EC_STATSTABLE_USERS,		GetPropHandler,		DefaultSetPropComputed, (void*) this, FALSE, TRUE);
	HrAddPropHandlers(PR_EC_STATSTABLE_COMPANY,     GetPropHandler,     DefaultSetPropComputed, (void*) this, FALSE, TRUE);
	HrAddPropHandlers(PR_EC_STATSTABLE_SERVERS,     GetPropHandler,     DefaultSetPropComputed, (void*) this, FALSE, TRUE);

	HrAddPropHandlers(PR_TEST_LINE_SPEED,			GetPropHandler,		DefaultSetPropComputed, (void*) this, FALSE, TRUE);
	HrAddPropHandlers(PR_EMSMDB_SECTION_UID,		GetPropHandler,		DefaultSetPropComputed, (void*) this, FALSE, TRUE);

	HrAddPropHandlers(PR_ACL_DATA,					GetPropHandler,		SetPropHandler,			(void*) this, FALSE, TRUE);

	// Basically a workaround because we can't pass 'this' in the superclass constructor.
	SetProvider(this);

	this->lpNamedProp = new ECNamedProp(lpTransport);
	this->isTransactedObject = FALSE;
	GetClientVersion(&this->m_ulClientVersion); //Ignore errors
	assert(lpszProfname != NULL);
	if(lpszProfname)
		this->m_strProfname = lpszProfname;
}

ECMsgStore::~ECMsgStore() {

	TRACE_MAPI(TRACE_ENTRY, "ECMsgStore::~ECMsgStore","");

	if(lpTransport)
		lpTransport->HrLogOff();

	// remove all advices
	if(m_lpNotifyClient)
		m_lpNotifyClient->ReleaseAll();

	// destruct
	if(m_lpNotifyClient)
		m_lpNotifyClient->Release();

	delete lpNamedProp;
	if(lpStorage) {
		// Release our propstorage since it is registered on lpTransport
		lpStorage->Release();
		/* needed because base (~ECGenericProp) also tries to release it */
		lpStorage = NULL;
	}

	if(lpTransport)
		lpTransport->Release();

	if(lpSupport)
		lpSupport->Release();

	TRACE_MAPI(TRACE_RETURN, "ECMsgStore::~ECMsgStore","");
}

//FIXME: remove duplicate profilename
static HRESULT GetIMsgStoreObject(BOOL bOffline, std::string strProfname,
    BOOL bModify, ECMapProvider *lpmapProviders, IMAPISupport *lpMAPISup,
    ULONG cbEntryId, LPENTRYID lpEntryId, LPMDB *lppIMsgStore)
{
	PROVIDER_INFO sProviderInfo;
	object_ptr<IProfSect> lpProfSect;
	memory_ptr<SPropValue> lpsPropValue;
	char *lpszProfileName = NULL;

	HRESULT hr = lpMAPISup->OpenProfileSection((LPMAPIUID)&MUID_PROFILE_INSTANCE, 0, &~lpProfSect);
	if(hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(lpProfSect, PR_PROFILE_NAME_A, &~lpsPropValue);
	if(hr != hrSuccess)
		return hr;

	// Set ProfileName
	lpszProfileName = lpsPropValue->Value.lpszA;

	hr = GetProviders(lpmapProviders, lpMAPISup, lpszProfileName, 0, &sProviderInfo);
	if (hr != hrSuccess)
		return hr;
	return sProviderInfo.lpMSProviderOnline->Logon(lpMAPISup, 0,
	       (LPTSTR)lpszProfileName, cbEntryId, lpEntryId,
	       bModify ? MAPI_BEST_ACCESS | MDB_NO_DIALOG : MDB_NO_DIALOG,
	       nullptr, nullptr, nullptr, nullptr, nullptr, lppIMsgStore);
}

HRESULT ECMsgStore::QueryInterface(REFIID refiid, void **lppInterface)
{
	HRESULT hr = hrSuccess;

	REGISTER_INTERFACE2(ECMsgStore, this);
	REGISTER_INTERFACE2(ECMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMsgStore, &this->m_xMsgStore);
	REGISTER_INTERFACE2(IMAPIProp, &this->m_xMsgStore);
	REGISTER_INTERFACE2(IUnknown, &this->m_xMsgStore);
	REGISTER_INTERFACE3(ISelectUnicode, IUnknown, &this->m_xUnknown);

	if (refiid == IID_IExchangeManageStore || refiid == IID_IExchangeManageStore6 || refiid == IID_IExchangeManageStoreEx) {
		if (m_bOfflineStore == FALSE) {			
			REGISTER_INTERFACE2(IExchangeManageStore, &this->m_xExchangeManageStore);
			REGISTER_INTERFACE2(IExchangeManageStore6, &this->m_xExchangeManageStore6);
			REGISTER_INTERFACE2(IExchangeManageStoreEx, &this->m_xExchangeManageStoreEx);
		}
	}

	REGISTER_INTERFACE2(IECServiceAdmin, &this->m_xECServiceAdmin);
	REGISTER_INTERFACE2(IECSpooler, &this->m_xECSpooler);
	REGISTER_INTERFACE2(IECSecurity, &this->m_xECSecurity);
	REGISTER_INTERFACE2(IProxyStoreObject, &this->m_xProxyStoreObject);

	if (refiid == IID_ECMsgStoreOnline)
	{
		if (m_bOfflineStore == FALSE) {
			*lppInterface = &this->m_xMsgStore;
			AddRef();
			return hrSuccess;
		} 

		hr = GetIMsgStoreObject(FALSE, this->m_strProfname, fModify, &g_mapProviders, lpSupport, m_cbEntryId, m_lpEntryId, (LPMDB*)lppInterface);
		if (hr != hrSuccess)
			return hr;

		// Add the child because mapi lookto the ref count if you work through ProxyStoreObject
		object_ptr<ECMsgStore> lpChild;
		if (((IMsgStore *)*lppInterface)->QueryInterface(IID_ECMsgStore, &~lpChild) != hrSuccess) {
			((LPMDB)*lppInterface)->Release();
			return MAPI_E_INTERFACE_NOT_SUPPORTED;
		}
		
		AddChild(lpChild);
		return hrSuccess;
	}
	// is admin store?
	REGISTER_INTERFACE2(IECMultiStoreTable, &this->m_xECMultiStoreTable);
	REGISTER_INTERFACE2(IECLicense, &this->m_xECLicense);
	REGISTER_INTERFACE2(IECTestProtocol, &this->m_xECTestProtocol);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECMsgStore::QueryInterfaceProxy(REFIID refiid, void **lppInterface)
{
	if (refiid == IID_IProxyStoreObject) // block recusive proxy calls
		return MAPI_E_INTERFACE_NOT_SUPPORTED;

	REGISTER_INTERFACE2(IMsgStore, &this->m_xMsgStoreProxy);
	REGISTER_INTERFACE2(IMAPIProp, &this->m_xMsgStoreProxy);
	REGISTER_INTERFACE2(IUnknown, &this->m_xMsgStoreProxy);
	return QueryInterface(refiid, lppInterface);
}

ULONG ECMsgStore::Release()
{
	// If a parent has requested a callback when we're going down, do it now.
	if (m_cRef == 1 && this->lpfnCallback != nullptr)
		lpfnCallback(this->lpCallbackObject, this);
	return ECUnknown::Release();
}

HRESULT ECMsgStore::HrSetReleaseCallback(ECUnknown *lpObject, RELEASECALLBACK lpfnCallback)
{
	this->lpCallbackObject = lpObject;
	this->lpfnCallback = lpfnCallback;

	return hrSuccess;
}

HRESULT	ECMsgStore::Create(const char *lpszProfname, LPMAPISUP lpSupport,
    WSTransport *lpTransport, BOOL fModify, ULONG ulProfileFlags,
    BOOL fIsSpooler, BOOL fIsDefaultStore, BOOL bOfflineStore,
    ECMsgStore **lppECMsgStore)
{
	auto lpStore = new ECMsgStore(lpszProfname, lpSupport, lpTransport,
	               fModify, ulProfileFlags, fIsSpooler, fIsDefaultStore,
	               bOfflineStore);
	HRESULT hr = lpStore->QueryInterface(IID_ECMsgStore, (void **)lppECMsgStore);

	if(hr != hrSuccess)
		delete lpStore;

	return hr;
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
	HRESULT hr = MAPI_E_INTERFACE_NOT_SUPPORTED;

	if (lpiid == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if(ulPropTag == PR_RECEIVE_FOLDER_SETTINGS) {
		if (*lpiid == IID_IMAPITable && IsPublicStore() == false)
			// Non supported function for publicfolder
			hr = GetReceiveFolderTable(0, (LPMAPITABLE*)lppUnk);
	} else if(ulPropTag == PR_HIERARCHY_SYNCHRONIZER) {
		hr = ECExchangeExportChanges::Create(this, *lpiid, std::string(), L"store hierarchy", ICS_SYNC_HIERARCHY, (LPEXCHANGEEXPORTCHANGES*) lppUnk);
	} else if(ulPropTag == PR_CONTENTS_SYNCHRONIZER) {
	    if (*lpiid == IID_IECExportAddressbookChanges) {
			auto lpEEAC = new ECExportAddressbookChanges(this);
	        hr = lpEEAC->QueryInterface(*lpiid, (void **)lppUnk);
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
	HRESULT hr = hrSuccess;
	object_ptr<WSTableView> lpTableView;
	object_ptr<ECMAPITable> lpTable;

	if (lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// notifications? set 1st param: m_lpNotifyClient
	hr = ECMAPITable::Create("Stats table", NULL, 0, &~lpTable);
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
HRESULT ECMsgStore::InternalAdvise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection){
	HRESULT hr = hrSuccess;
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	if(m_ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS) {
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	if(lpAdviseSink == NULL || lpulConnection == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	assert(m_lpNotifyClient != NULL && (lpEntryID != NULL || this->m_lpEntryId != NULL));
	if(lpEntryID == NULL) {

		// never sent the client store entry
		hr = UnWrapServerClientStoreEntry(this->m_cbEntryId, this->m_lpEntryId, &cbUnWrapStoreID, &lpUnWrapStoreID);
		if(hr != hrSuccess)
			goto exit;

		cbEntryID = cbUnWrapStoreID;
		lpEntryID = lpUnWrapStoreID;
	}

	if(m_lpNotifyClient->RegisterAdvise(cbEntryID, (LPBYTE)lpEntryID, ulEventMask, true, lpAdviseSink, lpulConnection) != S_OK)
		hr = MAPI_E_NO_SUPPORT;

	if(hr != hrSuccess)
		goto exit;

	m_setAdviseConnections.insert(*lpulConnection);

exit:
	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

	return hr;
}

HRESULT ECMsgStore::Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection){
	HRESULT hr = hrSuccess;
	LPENTRYID	lpUnWrapStoreID = NULL;
	ULONG		cbUnWrapStoreID = 0;

	if(m_ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS) {
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	if(lpAdviseSink == NULL || lpulConnection == NULL) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	assert(m_lpNotifyClient != NULL && (lpEntryID != NULL || this->m_lpEntryId != NULL));
	if(lpEntryID == NULL) {

		// never sent the client store entry
		hr = UnWrapServerClientStoreEntry(this->m_cbEntryId, this->m_lpEntryId, &cbUnWrapStoreID, &lpUnWrapStoreID);
		if(hr != hrSuccess)
			goto exit;

		cbEntryID = cbUnWrapStoreID;
		lpEntryID = lpUnWrapStoreID;
	} else {
		// check that the given lpEntryID belongs to the store in m_lpEntryId
		if (memcmp(&this->GetStoreGuid(), &((PEID)lpEntryID)->guid, sizeof(GUID)) != 0) {
			hr = MAPI_E_NO_SUPPORT;
			goto exit;
		}
	}

	if(m_lpNotifyClient->Advise(cbEntryID, (LPBYTE)lpEntryID, ulEventMask, lpAdviseSink, lpulConnection) != S_OK)
		hr = MAPI_E_NO_SUPPORT;

	m_setAdviseConnections.insert(*lpulConnection);

exit:
	if(lpUnWrapStoreID)
		ECFreeBuffer(lpUnWrapStoreID);

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
	HRESULT hr = hrSuccess;
	auto lpThis = static_cast<ECMsgStore *>(lpParam);

	for (auto conn_id : lpThis->m_setAdviseConnections)
		lpThis->m_lpNotifyClient->Reregister(conn_id);
	return hr;
}

HRESULT ECMsgStore::TableRowGetProp(void* lpProvider, struct propVal *lpsPropValSrc, LPSPropValue lpsPropValDst, void **lpBase, ULONG ulType)
{
	HRESULT hr = hrSuccess;
	auto lpMsgStore = static_cast<ECMsgStore *>(lpProvider);

	switch (lpsPropValSrc->ulPropTag) {
	case PR_ENTRYID:
	{				
		ULONG cbWrapped = 0;
		memory_ptr<ENTRYID> lpWrapped;

		hr = lpMsgStore->GetWrappedServerStoreEntryID(lpsPropValSrc->Value.bin->__size, lpsPropValSrc->Value.bin->__ptr, &cbWrapped, &~lpWrapped);
		if (hr != hrSuccess)
			return hr;
		ECAllocateMore(cbWrapped, lpBase, (void **)&lpsPropValDst->Value.bin.lpb);
		memcpy(lpsPropValDst->Value.bin.lpb, lpWrapped, cbWrapped);
		lpsPropValDst->Value.bin.cb = cbWrapped;
		lpsPropValDst->ulPropTag = PROP_TAG(PT_BINARY,PROP_ID(lpsPropValSrc->ulPropTag));
		break;
	}	
	default:
		return MAPI_E_NOT_FOUND;
	}
	return hr;
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
HRESULT ECMsgStore::CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	PEID peid1 = (PEID)lpEntryID1;
	PEID peid2 = (PEID)lpEntryID2;
	PEID lpStoreId = (PEID)m_lpEntryId;

	if (lpulResult != nullptr)
		*lpulResult = false;
	// Apparently BlackBerry CALHelper.exe needs this
	if ((cbEntryID1 == 0 && cbEntryID2 != 0) || (cbEntryID1 != 0 && cbEntryID2 == 0))
		return hrSuccess;
	if (lpEntryID1 == nullptr || lpEntryID2 == nullptr ||
	    lpulResult == nullptr)
		return MAPI_E_INVALID_PARAMETER;

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

HRESULT ECMsgStore::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	return OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, ECMessageFactory(), lpulObjType, lppUnk);
}

HRESULT ECMsgStore::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, const IMessageFactory &refMessageFactory, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	HRESULT				hr = hrSuccess;
	memory_ptr<ENTRYID> lpRootEntryID;
	ULONG				cbRootEntryID = 0;
	BOOL				fModifyObject = FALSE;
	object_ptr<ECMAPIFolder> lpMAPIFolder;
	object_ptr<ECMessage> lpMessage;
	object_ptr<IECPropStorage> lpPropStorage;
	object_ptr<WSMAPIFolderOps> lpFolderOps;
	unsigned int		ulObjType = 0;

	// Check input/output variables
	if (lpulObjType == nullptr || lppUnk == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	
	if(ulFlags & MAPI_MODIFY) {
		if (!fModify)
			return MAPI_E_NO_ACCESS;
		else
			fModifyObject = TRUE;
	}

	if(ulFlags & MAPI_BEST_ACCESS)
		fModifyObject = fModify;

	if(cbEntryID == 0) {
		hr = lpTransport->HrGetStore(m_cbEntryId, m_lpEntryId, 0, NULL, &cbRootEntryID, &~lpRootEntryID);
		if(hr != hrSuccess)
			return hr;
		lpEntryID = lpRootEntryID;
		cbEntryID = cbRootEntryID;

	}else {

		hr = HrCompareEntryIdWithStoreGuid(cbEntryID, lpEntryID, &GetStoreGuid());
		if(hr != hrSuccess)
			return hr;
		if(!(ulFlags & MAPI_DEFERRED_ERRORS)) {
	        hr = lpTransport->HrCheckExistObject(cbEntryID, lpEntryID, (ulFlags&(SHOW_SOFT_DELETES))); 
		    if(hr != hrSuccess) 
				return hr;
		}
	}

	hr = HrGetObjTypeFromEntryId(cbEntryID, (LPBYTE)lpEntryID, &ulObjType);
	if(hr != hrSuccess)
		return hr;

	switch( ulObjType ) {
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
		hr = refMessageFactory.Create(this, FALSE, fModifyObject, 0, FALSE, NULL, &~lpMessage);
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

HRESULT ECMsgStore::SetReceiveFolder(LPTSTR lpszMessageClass, ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID)
{
	// Non supported function for publicfolder
	if (IsPublicStore() == TRUE)
		return MAPI_E_NO_SUPPORT;

	return lpTransport->HrSetReceiveFolder(this->m_cbEntryId, this->m_lpEntryId, convstring(lpszMessageClass, ulFlags), cbEntryID, lpEntryID);
}

// If the open store a publicstore
BOOL ECMsgStore::IsPublicStore()
{
	BOOL fPublicStore = FALSE;

	if(CompareMDBProvider(&this->m_guidMDB_Provider, &KOPANO_STORE_PUBLIC_GUID))
		fPublicStore = TRUE;

	return fPublicStore;
}

// As the store is a delegate store
BOOL ECMsgStore::IsDelegateStore()
{
	BOOL fDelegateStore = FALSE;

	if(CompareMDBProvider(&this->m_guidMDB_Provider, &KOPANO_STORE_DELEGATE_GUID))
		fDelegateStore = TRUE;

	return fDelegateStore;
}

HRESULT ECMsgStore::GetReceiveFolder(LPTSTR lpszMessageClass, ULONG ulFlags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID, LPTSTR *lppszExplicitClass)
{
	HRESULT hr;
	ULONG		cbEntryID = 0;
	LPENTRYID	lpEntryID = NULL;
	utf8string	strExplicitClass;

	// Non supported function for publicfolder
	if (IsPublicStore() == TRUE)
		return MAPI_E_NO_SUPPORT;
	// Check input/output variables
	if (lpcbEntryID == NULL || lppEntryID == NULL) // lppszExplicitClass may NULL
		return MAPI_E_INVALID_PARAMETER;

	hr = lpTransport->HrGetReceiveFolder(this->m_cbEntryId, this->m_lpEntryId, convstring(lpszMessageClass, ulFlags), &cbEntryID, &lpEntryID, lppszExplicitClass ? &strExplicitClass : NULL);
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
	HRESULT			hr = hrSuccess;
	object_ptr<ECMemTableView> lpView = NULL;
	object_ptr<ECMemTable> lpMemTable;
	rowset_ptr lpsRowSet;
	unsigned int	i;
	memory_ptr<SPropTagArray> lpPropTagArray;

	// Non supported function for publicfolder
	if (IsPublicStore())
		return MAPI_E_NO_SUPPORT;

	// Check input/output variables
	if (lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	hr = Util::HrCopyUnicodePropTagArray(ulFlags, sPropRFTColumns, &~lpPropTagArray);
	if(hr != hrSuccess)
		return hr;
	hr = ECMemTable::Create(lpPropTagArray, PR_ROWID, &~lpMemTable); // PR_INSTANCE_KEY
	if(hr != hrSuccess)
		return hr;

	//Get the receivefolder list from the server
	hr = lpTransport->HrGetReceiveFolderTable(ulFlags, this->m_cbEntryId, this->m_lpEntryId, &~lpsRowSet);
	if(hr != hrSuccess)
		return hr;

	for (i = 0; i < lpsRowSet->cRows; ++i) {
		hr = lpMemTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, lpsRowSet->aRow[i].lpProps, NUM_RFT_PROPS);
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

HRESULT ECMsgStore::AbortSubmit(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags)
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
	HRESULT			hr = hrSuccess;
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableOutGoingQueue> lpTableOps;

	// Only supported by the MAPI spooler
	/* if (this->IsSpooler() == false)
		return MAPI_E_NO_SUPPORT;
	*/

	// Check input/output variables
	if (lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	hr = ECMAPITable::Create("Outgoing queue", this->m_lpNotifyClient, 0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = this->lpTransport->HrOpenTableOutGoingQueueOps(this->m_cbEntryId, this->m_lpEntryId, this, &~lpTableOps);
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
	HRESULT			hr = hrSuccess;
	LPSPropValue	lpsPropArray = NULL;
	ULONG			cValue = 0;
	ULONG			ulSubmitFlag = 0;
	ECMessagePtr	ptrECMessage;
	static constexpr const SizedSPropTagArray(2, sptaMessageProps) =
		{2, {PR_SUBMIT_FLAGS, PR_ENTRYID}};
	enum {IDX_SUBMIT_FLAGS, IDX_ENTRYID};

	// Only supported by the MAPI spooler
	/*if(this->IsSpooler() == false) {
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}*/

	if (!lpMessage) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = lpMessage->GetProps(sptaMessageProps, 0, &cValue, &lpsPropArray);
	if(HR_FAILED(hr))
		goto exit;

	if (PROP_TYPE(lpsPropArray[IDX_ENTRYID].ulPropTag) == PT_ERROR) {
		hr = lpsPropArray[IDX_ENTRYID].Value.err;
		goto exit;
	}
	if (PROP_TYPE(lpsPropArray[IDX_SUBMIT_FLAGS].ulPropTag) != PT_ERROR)
		ulSubmitFlag = lpsPropArray->Value.l;

	// set the lock state, if the message is already in the correct state
	// just get outta here
	if (ulLockState & MSG_LOCKED)
	{
		if (!(ulSubmitFlag & SUBMITFLAG_LOCKED))
			ulSubmitFlag |= SUBMITFLAG_LOCKED;
		else
			goto exit;
	} else { // unlock
		if (ulSubmitFlag & SUBMITFLAG_LOCKED)
			ulSubmitFlag &= ~SUBMITFLAG_LOCKED;
		else
			goto exit;
	}

	hr = lpMessage->QueryInterface(ptrECMessage.iid(), &~ptrECMessage);
	if (hr != hrSuccess)
		goto exit;

	if (ptrECMessage->IsReadOnly()) {
		hr = MAPI_E_NO_ACCESS;
		goto exit;
	}
	
	hr = lpTransport->HrSetLockState(lpsPropArray[IDX_ENTRYID].Value.bin.cb, (LPENTRYID)lpsPropArray[IDX_ENTRYID].Value.bin.lpb, (ulSubmitFlag & SUBMITFLAG_LOCKED) == SUBMITFLAG_LOCKED);
	if (hr != hrSuccess)
		goto exit;

	ECFreeBuffer(lpsPropArray);
	lpsPropArray = NULL;

	hr = ECAllocateBuffer(sizeof(SPropValue)*1, (void**)&lpsPropArray);
	if (hr != hrSuccess)
		goto exit;

	lpsPropArray[0].ulPropTag = PR_SUBMIT_FLAGS;
	lpsPropArray[0].Value.l = ulSubmitFlag;

	hr = lpMessage->SetProps(1, lpsPropArray, NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		goto exit;

exit:
	if(lpsPropArray)
		ECFreeBuffer(lpsPropArray);

	return hr;
}

HRESULT ECMsgStore::FinishedMsg(ULONG ulFlags, ULONG cbEntryID, LPENTRYID lpEntryID)
{
	HRESULT		hr = hrSuccess;
	ULONG		ulObjType = 0;
	object_ptr<IMessage> lpMessage;

	// Only supported by the MAPI spooler
	/* ifthis->IsSpooler() == false)
		return MAPI_E_NO_SUPPORT;
	*/

	// Check input/output variables
	if (lpEntryID == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// Delete the message from the local outgoing queue
	hr = lpTransport->HrFinishedMessage(cbEntryID, lpEntryID, EC_SUBMIT_LOCAL);
	if(hr != hrSuccess)
		return hr;

	/**
	 * @todo: Do we need this or can we let OpenEntry succeed if this is the same session on which the
	 *        message was locked.
	 */
	hr = lpTransport->HrSetLockState(cbEntryID, lpEntryID, false);
	if (hr != hrSuccess)
		return hr;
	hr = OpenEntry(cbEntryID, lpEntryID, &IID_IMessage, MAPI_MODIFY,  &ulObjType, &~lpMessage);
	if(hr != hrSuccess)
		return hr;

	// Unlock the message
	hr = SetLockState(lpMessage, MSG_UNLOCKED);
	if(hr != hrSuccess)
		return hr;

	// Information: DoSentMail released object lpMessage
	hr = lpSupport->DoSentMail(0, lpMessage);

	if(hr != hrSuccess)
		return hr;
	lpMessage.release(); /* was fed into DoSentMail */
	return hrSuccess;
}

HRESULT ECMsgStore::NotifyNewMail(LPNOTIFICATION lpNotification)
{
	HRESULT hr;

	// Only supported by the MAPI spooler
	/* if (this->IsSpooler() == false)
		return MAPI_E_NO_SUPPORT;
	*/

	// Check input/output variables
	if (lpNotification == NULL || lpNotification->info.newmail.lpParentID == NULL || lpNotification->info.newmail.lpEntryID == NULL)
		return MAPI_E_INVALID_PARAMETER;

	hr = HrCompareEntryIdWithStoreGuid(lpNotification->info.newmail.cbEntryID, lpNotification->info.newmail.lpEntryID, &GetStoreGuid());
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
			if(hr != hrSuccess)
				return hr;
			hr = HrGetOneProp(lpProfSect, PR_SERVICE_UID, &~lpProp);
			if(hr != hrSuccess)
				return hr;
			hr = lpStore->lpSupport->OpenProfileSection((LPMAPIUID)lpProp->Value.bin.lpb, 0, &~lpProfSect);
			if(hr != hrSuccess)
				return hr;
			hr = HrGetOneProp(lpProfSect, PR_EMSMDB_SECTION_UID, &~lpProp);
			if(hr != hrSuccess)
				return hr;
			lpsPropValue->ulPropTag = PR_EMSMDB_SECTION_UID;
			if ((hr = MAPIAllocateMore(sizeof(GUID), lpBase, (void **) &lpsPropValue->Value.bin.lpb)) != hrSuccess)
				return hr;
			memcpy(lpsPropValue->Value.bin.lpb, lpProp->Value.bin.lpb, sizeof(GUID));
			lpsPropValue->Value.bin.cb = sizeof(GUID);
			break;
			}
		case PROP_ID(PR_SEARCH_KEY):
		case PROP_ID(PR_ENTRYID):
			{
				ULONG cbWrapped = 0;
				memory_ptr<ENTRYID> lpWrapped;

				lpsPropValue->ulPropTag = ulPropTag;
				hr = lpStore->GetWrappedStoreEntryID(&cbWrapped, &~lpWrapped);
				if(hr == hrSuccess) {
					ECAllocateMore(cbWrapped, lpBase, (LPVOID *)&lpsPropValue->Value.bin.lpb);
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
			ECAllocateMore(sizeof(MAPIUID), lpBase, (void **)&lpsPropValue->Value.bin.lpb);
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
			if(lpStore->IsPublicStore() == TRUE || lpStore->IsOfflineStore() == TRUE) {
				hr = MAPI_E_NOT_FOUND;
				break;
			}
			lpsPropValue->ulPropTag = ulPropTag;
			hr = lpStore->HrGetRealProp(ulPropTag, ulFlags, lpBase, lpsPropValue);
			break;
		case PROP_ID(PR_MAILBOX_OWNER_ENTRYID):
			if(lpStore->IsPublicStore() == TRUE || lpStore->IsOfflineStore() == TRUE) {
				hr = MAPI_E_NOT_FOUND;
				break;
			}
			lpsPropValue->ulPropTag = PR_MAILBOX_OWNER_ENTRYID;
			hr = lpStore->HrGetRealProp(PR_MAILBOX_OWNER_ENTRYID, ulFlags, lpBase, lpsPropValue);
			break;
		case PROP_ID(PR_STORE_OFFLINE):
			// Delegate stores are always online, so ignore this property
			if(lpStore->IsDelegateStore() == TRUE) {
				hr = MAPI_E_NOT_FOUND;
				break;
			}
			lpsPropValue->ulPropTag = PR_STORE_OFFLINE;
			lpsPropValue->Value.b = !!lpStore->IsOfflineStore();
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

		default:
			hr = MAPI_E_NOT_FOUND;
			break;
	}
	return hr;
}

HRESULT	ECMsgStore::SetPropHandler(ULONG ulPropTag, void *lpProvider,
    const SPropValue *lpsPropValue, void *lpParam)
{
	HRESULT hr = hrSuccess;
	auto lpStore = static_cast<ECMsgStore *>(lpParam);

	switch(ulPropTag) {
	case PR_ACL_DATA:
		hr = lpStore->SetSerializedACLData(lpsPropValue);
		break;
	default:
		hr = MAPI_E_NOT_FOUND;
		break;
	}

	return hr;
}

HRESULT ECMsgStore::SetEntryId(ULONG cbEntryId, LPENTRYID lpEntryId)
{
	assert(m_lpNotifyClient == NULL);
	HRESULT hr = ECGenericProp::SetEntryId(cbEntryId, lpEntryId);
	if(hr != hrSuccess)
		return hr;

	if(! (m_ulProfileFlags & EC_PROFILE_FLAGS_NO_NOTIFICATIONS)) {
		// Create Notifyclient
		hr = ECNotifyClient::Create(MAPI_STORE, this, m_ulProfileFlags, lpSupport, &m_lpNotifyClient);
		assert(m_lpNotifyClient != NULL);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

const GUID& ECMsgStore::GetStoreGuid()
{
	return ((PEID)m_lpEntryId)->guid;
}

HRESULT ECMsgStore::GetWrappedStoreEntryID(ULONG* lpcbWrapped, LPENTRYID* lppWrapped)
{
	//hr = WrapStoreEntryID(0, WCLIENT_DLL_NAME, CbEID(peid), (LPENTRYID)peid, &cbWrapped, &lpWrapped);
	return lpSupport->WrapStoreEntryID(this->m_cbEntryId, this->m_lpEntryId, lpcbWrapped, lppWrapped);
}

// This is a function special for the spooler to get the right store entryid
// This is a problem of the master outgoing queue
HRESULT ECMsgStore::GetWrappedServerStoreEntryID(ULONG cbEntryId, LPBYTE lpEntryId, ULONG* lpcbWrapped, LPENTRYID* lppWrapped)
{
	HRESULT		hr = hrSuccess;
	ULONG		cbStoreID = 0;
	LPENTRYID	lpStoreID = NULL;

	entryId		sEntryId;
	sEntryId.__ptr = lpEntryId;
	sEntryId.__size = cbEntryId;

	hr = WrapServerClientStoreEntry(lpTransport->GetServerName(), &sEntryId, &cbStoreID, &lpStoreID);
	if(hr != hrSuccess)
		goto exit;

	hr = lpSupport->WrapStoreEntryID(cbStoreID, lpStoreID, lpcbWrapped, lppWrapped);

exit:
	if (lpStoreID)
		ECFreeBuffer(lpStoreID);

	return hr;
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
HRESULT ECMsgStore::CreateStoreEntryID(LPTSTR lpszMsgStoreDN, LPTSTR lpszMailboxDN, ULONG ulFlags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	HRESULT		hr = hrSuccess;
	ULONG		cbStoreEntryID = 0;
	memory_ptr<ENTRYID> lpStoreEntryID;
	object_ptr<WSTransport> lpTmpTransport;
	convstring		tstrMsgStoreDN(lpszMsgStoreDN, ulFlags);
	convstring		tstrMailboxDN(lpszMailboxDN, ulFlags);

	if (tstrMsgStoreDN.null_or_empty()) {
		// No messagestore DN provided. Just try the current server and let it redirect us if neeeded.
		string		strRedirServer;

		hr = lpTransport->HrResolveUserStore(tstrMailboxDN, ulFlags, NULL, &cbStoreEntryID, &~lpStoreEntryID, &strRedirServer);
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
		MAPIStringPtr ptrServerPath;
		bool bIsPeer;

		hr = MsgStoreDnToPseudoUrl(tstrMsgStoreDN, &strPseudoUrl);
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
	return WrapStoreEntryID(0, (LPTSTR)WCLIENT_DLL_NAME, cbStoreEntryID, lpStoreEntryID, lpcbEntryID, lppEntryID);
}

HRESULT ECMsgStore::CreateStoreEntryID2(ULONG cValues, LPSPropValue lpProps, ULONG ulFlags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	auto lpMsgStoreDN = PCpropFindProp(lpProps, cValues, PR_PROFILE_MDB_DN);
	auto lpMailboxDN = PCpropFindProp(lpProps, cValues, PR_PROFILE_MAILBOX);

	if (lpMsgStoreDN == NULL || lpMailboxDN == NULL)
		return MAPI_E_INVALID_PARAMETER;

	return CreateStoreEntryID((LPTSTR)lpMsgStoreDN->Value.lpszA, (LPTSTR)lpMailboxDN->Value.lpszA, ulFlags & ~MAPI_UNICODE, lpcbEntryID, lppEntryID);
}

HRESULT ECMsgStore::EntryIDFromSourceKey(ULONG cFolderKeySize, BYTE *lpFolderSourceKey,	ULONG cMessageKeySize, BYTE *lpMessageSourceKey, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	return lpTransport->HrEntryIDFromSourceKey(this->m_cbEntryId, this->m_lpEntryId, cFolderKeySize, lpFolderSourceKey, cMessageKeySize, lpMessageSourceKey, lpcbEntryID, lppEntryID);
}

HRESULT ECMsgStore::GetRights(ULONG cbUserEntryID, LPENTRYID lpUserEntryID, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *lpulRights)
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
HRESULT ECMsgStore::GetMailboxTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG ulFlags)
{
	HRESULT			hr = hrSuccess;
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableView> lpTableOps;
	object_ptr<WSTransport> lpTmpTransport;
	object_ptr<ECMsgStore> lpMsgStore;
	object_ptr<IMsgStore> lpMsgStoreOtherServer;
	ULONG			cbEntryId = 0;
	memory_ptr<ENTRYID> lpEntryId;
	bool			bIsPeer = true;
	MAPIStringPtr	ptrServerPath;
	std::string		strPseudoUrl;
	convstring		tstrServerName(lpszServerName, ulFlags);

	const utf8string strUserName = convert_to<utf8string>("SYSTEM");
	
	if (!tstrServerName.null_or_empty()) {
	
		strPseudoUrl = "pseudo://"; 
		strPseudoUrl += tstrServerName;
		hr = lpTransport->HrResolvePseudoUrl(strPseudoUrl.c_str(), &~ptrServerPath, &bIsPeer);
		if (hr != hrSuccess)
			return hr;
		if (!bIsPeer) {
			hr = lpTransport->CreateAndLogonAlternate(ptrServerPath, &~lpTmpTransport);
			if (hr != hrSuccess)
				return hr;
			hr = lpTmpTransport->HrResolveUserStore(strUserName, 0, NULL, &cbEntryId, &~lpEntryId);
			if (hr != hrSuccess)
				return hr;
			hr = GetIMsgStoreObject(FALSE, this->m_strProfname, fModify, &g_mapProviders, lpSupport, cbEntryId, lpEntryId, &~lpMsgStoreOtherServer);
			if (hr != hrSuccess)
				return hr;
			hr = lpMsgStoreOtherServer->QueryInterface(IID_ECMsgStore, &~lpMsgStore);
			if (hr != hrSuccess)
				return hr;
		}
	}

	if (bIsPeer) {
		hr = this->QueryInterface(IID_ECMsgStore, &~lpMsgStore);
		if (hr != hrSuccess)
			return hr;
	}

	assert(lpMsgStore != NULL);
	hr = ECMAPITable::Create("Mailbox table", lpMsgStore->GetMsgStore()->m_lpNotifyClient, 0, &~lpTable);
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
HRESULT ECMsgStore::GetPublicFolderTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG ulFlags)
{
	return MAPI_E_NOT_FOUND;
}

// IECServiceAdmin

// Create the freebusy folder on a private store
static HRESULT CreatePrivateFreeBusyData(LPMAPIFOLDER lpRootFolder,
    LPMAPIFOLDER lpInboxFolder, LPMAPIFOLDER lpCalendarFolder)
{
	// Global
	HRESULT				hr = hrSuccess;
	LPSPropValue		lpPropValue = NULL;
	LPSPropValue		lpFBPropValue = NULL;
	ULONG				cValues = 0;
	ULONG				cCurValues = 0;
	object_ptr<IMAPIFolder> lpFBFolder;
	object_ptr<IMessage> lpFBMessage;

	// Freebusy mv propery
	// This property will be fill in on another place
	hr = ECAllocateBuffer(sizeof(SPropValue), (void**)&lpFBPropValue);
	if(hr != hrSuccess)
		goto exit;

	memset(lpFBPropValue, 0, sizeof(SPropValue));

	lpFBPropValue->ulPropTag = PR_FREEBUSY_ENTRYIDS;
	lpFBPropValue->Value.MVbin.cValues = 4;
	hr = ECAllocateMore(sizeof(SBinary)*lpFBPropValue->Value.MVbin.cValues, lpFBPropValue, (void**)&lpFBPropValue->Value.MVbin.lpbin);
	if(hr != hrSuccess)
		goto exit;

	memset(lpFBPropValue->Value.MVbin.lpbin, 0, sizeof(SBinary)*lpFBPropValue->Value.MVbin.cValues);

	// Create Freebusy Data into the rootfolder
	// Folder for "freebusy data" settings
	// Create the folder
	hr = lpRootFolder->CreateFolder(FOLDER_GENERIC , (LPTSTR)"Freebusy Data", nullptr, &IID_IMAPIFolder, OPEN_IF_EXISTS, &~lpFBFolder);
	if(hr != hrSuccess)
		goto exit;

	// Get entryid of localfreebusy
	hr = HrGetOneProp(lpFBFolder, PR_ENTRYID, &lpPropValue);
	if(hr != hrSuccess)
		goto exit;

	//Fill in position 3 of the FBProperty, with free/busy data folder entryid
	hr = ECAllocateMore(lpPropValue->Value.bin.cb, lpFBPropValue, (void**)&lpFBPropValue->Value.MVbin.lpbin[3].lpb);
	if(hr != hrSuccess)
		goto exit;

	lpFBPropValue->Value.MVbin.lpbin[3].cb = lpPropValue->Value.bin.cb;
	memcpy(lpFBPropValue->Value.MVbin.lpbin[3].lpb, lpPropValue->Value.bin.lpb, lpPropValue->Value.bin.cb);

	ECFreeBuffer(lpPropValue); lpPropValue = NULL;

	// Create localfreebusy message
	// Default setting of free/busy
	hr = lpFBFolder->CreateMessage(&IID_IMessage, 0, &~lpFBMessage);
	if(hr != hrSuccess)
		goto exit;

	cValues = 6;
	cCurValues = 0;
	hr = ECAllocateBuffer(sizeof(SPropValue)*cValues, (void**)&lpPropValue);
	if(hr != hrSuccess)
		goto exit;

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
		goto exit;

	hr = lpFBMessage->SaveChanges(KEEP_OPEN_READONLY);
	if(hr != hrSuccess)
		goto exit;

	ECFreeBuffer(lpPropValue); lpPropValue = NULL;

	// Get entryid of localfreebusy
	hr = HrGetOneProp(lpFBMessage, PR_ENTRYID, &lpPropValue);
	if(hr != hrSuccess)
		goto exit;

	//Fill in position 1 of the FBProperty, with free/busy message entryid
	hr = ECAllocateMore(lpPropValue->Value.bin.cb, lpFBPropValue, (void**)&lpFBPropValue->Value.MVbin.lpbin[1].lpb);
	if(hr != hrSuccess)
		goto exit;

	lpFBPropValue->Value.MVbin.lpbin[1].cb = lpPropValue->Value.bin.cb;
	memcpy(lpFBPropValue->Value.MVbin.lpbin[1].lpb, lpPropValue->Value.bin.lpb, lpPropValue->Value.bin.cb);

	ECFreeBuffer(lpPropValue); lpPropValue = NULL;

	// Create Associated localfreebusy message
	hr = lpCalendarFolder->CreateMessage(&IID_IMessage, MAPI_ASSOCIATED, &~lpFBMessage);
	if(hr != hrSuccess)
		goto exit;

	cValues = 3;
	cCurValues = 0;
	hr = ECAllocateBuffer(sizeof(SPropValue)*cValues, (void**)&lpPropValue);
	if(hr != hrSuccess)
		goto exit;

	lpPropValue[cCurValues].ulPropTag = PR_MESSAGE_CLASS_A;
	lpPropValue[cCurValues++].Value.lpszA = const_cast<char *>("IPM.Microsoft.ScheduleData.FreeBusy");

	lpPropValue[cCurValues].ulPropTag = PR_SUBJECT_A;
	lpPropValue[cCurValues++].Value.lpszA = const_cast<char *>("LocalFreebusy");

	lpPropValue[cCurValues].ulPropTag = PR_FREEBUSY_NUM_MONTHS;
	lpPropValue[cCurValues++].Value.ul = ECFREEBUSY_DEFAULT_PUBLISH_MONTHS;

	hr = lpFBMessage->SetProps(cCurValues, lpPropValue, NULL);
	if(hr != hrSuccess)
		goto exit;

	ECFreeBuffer(lpPropValue); lpPropValue = NULL;

	hr = lpFBMessage->SaveChanges(KEEP_OPEN_READONLY);
	if(hr != hrSuccess)
		goto exit;

	// Get entryid of associated localfreebusy
	hr = HrGetOneProp(lpFBMessage, PR_ENTRYID, &lpPropValue);
	if(hr != hrSuccess)
		goto exit;

	//Fill in position 0 of the FBProperty, with associated localfreebusy message entryid
	hr = ECAllocateMore(lpPropValue->Value.bin.cb, lpFBPropValue, (void**)&lpFBPropValue->Value.MVbin.lpbin[0].lpb);
	if(hr != hrSuccess)
		goto exit;

	lpFBPropValue->Value.MVbin.lpbin[0].cb = lpPropValue->Value.bin.cb;
	memcpy(lpFBPropValue->Value.MVbin.lpbin[0].lpb, lpPropValue->Value.bin.lpb, lpPropValue->Value.bin.cb);

	ECFreeBuffer(lpPropValue); lpPropValue = NULL;

	// Add freebusy entryid on Inbox folder
	hr = lpInboxFolder->SetProps(1, lpFBPropValue, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpInboxFolder->SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		goto exit;

	// Add freebusy entryid on the root folder
	hr = lpRootFolder->SetProps(1, lpFBPropValue, NULL);
	if(hr != hrSuccess)
		goto exit;

	hr = lpRootFolder->SaveChanges(KEEP_OPEN_READWRITE);
	if(hr != hrSuccess)
		goto exit;

exit:
	//Global
	if(lpPropValue)
		ECFreeBuffer(lpPropValue);

	if(lpFBPropValue)
		ECFreeBuffer(lpFBPropValue);
	return hr;
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
	if(strBuffer.size() >= 4 && strBuffer.compare(strBuffer.size()-4, 4, "\0\0\0\0", 4) == 0) 
		strBuffer.resize(strBuffer.size()-4);
		
	strBuffer.append((char *)&ulType, 2); //RSS Feeds type
	strBuffer.append(1, ((lpEntryID->cb+4)&0xFF));
	strBuffer.append(1, ((lpEntryID->cb+4)>>8)&0xFF);
	strBuffer.append((char *)&ulBlockType, 2);
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

HRESULT ECMsgStore::CreateStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG* lpcbStoreId, LPENTRYID* lppStoreId, ULONG* lpcbRootId, LPENTRYID *lppRootId)
{
	HRESULT				hr				= hrSuccess;
	object_ptr<WSTransport> lpTempTransport;
	object_ptr<ECMsgStore> lpecMsgStore;
	object_ptr<ECMAPIFolder> lpMapiFolderRoot;
	/* Root container, IPM_SUBTREE and NON_IPM_SUBTREE */
	object_ptr<IMAPIFolder> lpFolderRoot, lpFolderRootST, lpFolderRootNST;
	object_ptr<IMAPIFolder> lpMAPIFolder, lpMAPIFolder2, lpMAPIFolder3;
	object_ptr<IECPropStorage> lpStorage;
	object_ptr<ECMAPIFolder> lpECMapiFolderInbox;
	object_ptr<IMAPIFolder> lpInboxFolder, lpCalendarFolder;
	LPSPropValue		lpPropValue = NULL;
	ULONG				cValues = 0;
	ULONG				ulObjType = 0;
	object_ptr<IECSecurity> lpECSecurity;
	ECPERMISSION		sPermission;

	ECUSER *lpECUser = NULL;
	ECCOMPANY *lpECCompany = NULL;
	ECGROUP *lpECGroup = NULL;

	std::string			strBuffer;

	ULONG				cbStoreId = 0;
	LPENTRYID			lpStoreId = NULL;
	ULONG				cbRootId = 0;
	LPENTRYID			lpRootId = NULL;

	hr = CreateEmptyStore(ulStoreType, cbUserId, lpUserId, 0, &cbStoreId, &lpStoreId, &cbRootId, &lpRootId);
	if (hr != hrSuccess)
		goto exit;

	/*
	 * Create a temporary transport, because the new ECMsgStore object will
	 * logoff the transport, even if the refcount is not 0 yet. That would
	 * cause the current instance to lose its transport.
	 */
	hr = lpTransport->CloneAndRelogon(&~lpTempTransport);
	if (hr != hrSuccess)
		goto exit;

	// Open the created messagestore
	hr = ECMsgStore::Create("", lpSupport, lpTempTransport, true, MAPI_BEST_ACCESS, false, false, false, &~lpecMsgStore);
	if (hr != hrSuccess)
		//FIXME: wat te doen met de aangemaakte store ?
		goto exit;
	if (ulStoreType == ECSTORE_TYPE_PRIVATE)
		memcpy(&lpecMsgStore->m_guidMDB_Provider, &KOPANO_SERVICE_GUID, sizeof(MAPIUID));
	else
		memcpy(&lpecMsgStore->m_guidMDB_Provider, &KOPANO_STORE_PUBLIC_GUID, sizeof(MAPIUID));

	// Get user or company information depending on the store type.
	if (ulStoreType == ECSTORE_TYPE_PRIVATE) {
		hr = lpTransport->HrGetUser(cbUserId, lpUserId, 0, &lpECUser);
		if (hr != hrSuccess)
			goto exit;
	} else if (ABEID_ID(lpUserId) == 1) {
		/* Public store, ownership set to group EVERYONE*/
		hr = lpTransport->HrGetGroup(cbUserId, lpUserId, 0, &lpECGroup);
		if (hr != hrSuccess)
			goto exit;
	} else {
		/* Public store, ownership set to company */
		hr = lpTransport->HrGetCompany(cbUserId, lpUserId, 0, &lpECCompany);
		if (hr != hrSuccess)
			goto exit;
	}

	// Get a propstorage for the message store
	hr = lpTransport->HrOpenPropStorage(0, NULL, cbStoreId, lpStoreId, 0, &~lpStorage);
	if(hr != hrSuccess)
		goto exit;

	// Set up the message store to use this storage
	hr = lpecMsgStore->HrSetPropStorage(lpStorage, TRUE);
	if(hr != hrSuccess)
		goto exit;

	hr = lpecMsgStore->SetEntryId(cbStoreId, lpStoreId);
	if(hr != hrSuccess)
		goto exit;

	// Open rootfolder
	hr = lpecMsgStore->OpenEntry(cbRootId, lpRootId, &IID_ECMAPIFolder, MAPI_MODIFY , &ulObjType, &~lpMapiFolderRoot);
	if(hr != hrSuccess)
		goto exit;

	// Set IPC folder
	if(ulStoreType == ECSTORE_TYPE_PRIVATE) {
		hr = lpecMsgStore->SetReceiveFolder((LPTSTR)"IPC", 0, cbRootId, lpRootId);
		if(hr != hrSuccess)
			goto exit;
	}

	// Create Folder IPM_SUBTREE into the rootfolder
	hr = lpMapiFolderRoot->QueryInterface(IID_IMAPIFolder, &~lpFolderRoot);
	if(hr != hrSuccess)
		goto exit;
	hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, _T("IPM_SUBTREE"), _T(""), PR_IPM_SUBTREE_ENTRYID, 0, NULL, &~lpFolderRootST);
	if(hr != hrSuccess)
		goto exit;

	if(ulStoreType == ECSTORE_TYPE_PUBLIC) { // Public folder action
		// Create Folder NON_IPM_SUBTREE into the rootfolder
		hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, _T("NON_IPM_SUBTREE"), _T(""), PR_NON_IPM_SUBTREE_ENTRYID, 0, NULL, &~lpFolderRootNST);
		if (hr != hrSuccess)
			goto exit;

		// Create Folder FINDER_ROOT into the rootfolder
		hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, _T("FINDER_ROOT"), _T(""), PR_FINDER_ENTRYID, 0, NULL, &~lpMAPIFolder3);
		if (hr != hrSuccess)
			goto exit;

		sPermission.ulRights = ecRightsFolderVisible|ecRightsReadAny|ecRightsCreateSubfolder|ecRightsEditOwned|ecRightsDeleteOwned;
		sPermission.ulState = RIGHT_NEW|RIGHT_AUTOUPDATE_DENIED;
		sPermission.ulType = ACCESS_TYPE_GRANT;
		sPermission.sUserId.cb = g_cbEveryoneEid;
		sPermission.sUserId.lpb = g_lpEveryoneEid;
		hr = lpMAPIFolder3->QueryInterface(IID_IECSecurity, &~lpECSecurity);
		if(hr != hrSuccess)
			goto exit;
		hr = lpECSecurity->SetPermissionRules(1, &sPermission);
		if (hr != hrSuccess)
			goto exit;

		//Free busy time folder
		hr = CreateSpecialFolder(lpFolderRootNST, lpecMsgStore,_T( "SCHEDULE+ FREE BUSY"), _T(""), PR_SPLUS_FREE_BUSY_ENTRYID, 0, NULL, &~lpMAPIFolder);
		if(hr != hrSuccess)
			goto exit;

		// Set acl's on the folder
		sPermission.ulRights = ecRightsReadAny|ecRightsFolderVisible;
		sPermission.ulState = RIGHT_NEW|RIGHT_AUTOUPDATE_DENIED;
		sPermission.ulType = ACCESS_TYPE_GRANT;
		sPermission.sUserId.cb = cbUserId;
		sPermission.sUserId.lpb = (unsigned char*)lpUserId;
		hr = lpMAPIFolder->QueryInterface(IID_IECSecurity, &~lpECSecurity);
		if(hr != hrSuccess)
			goto exit;

		hr = lpECSecurity->SetPermissionRules(1, &sPermission);
		if(hr != hrSuccess)
			goto exit;
		hr = CreateSpecialFolder(lpMAPIFolder, lpecMsgStore, _T("Zarafa 1"), _T(""), PR_FREE_BUSY_FOR_LOCAL_SITE_ENTRYID, 0, NULL, &~lpMAPIFolder2);
		if(hr != hrSuccess)
			goto exit;

		// Set acl's on the folder
		sPermission.ulRights = ecRightsAll;//ecRightsReadAny| ecRightsCreate | ecRightsEditOwned| ecRightsDeleteOwned | ecRightsCreateSubfolder | ecRightsFolderVisible;
		sPermission.ulState = RIGHT_NEW|RIGHT_AUTOUPDATE_DENIED;
		sPermission.ulType = ACCESS_TYPE_GRANT;
		sPermission.sUserId.cb = cbUserId;
		sPermission.sUserId.lpb = (unsigned char*)lpUserId;
		hr = lpMAPIFolder2->QueryInterface(IID_IECSecurity, &~lpECSecurity);
		if(hr != hrSuccess)
			goto exit;

		hr = lpECSecurity->SetPermissionRules(1, &sPermission);
		if(hr != hrSuccess)
			goto exit;

		// Set acl's on the IPM subtree folder
		sPermission.ulRights = ecRightsReadAny| ecRightsCreate | ecRightsEditOwned| ecRightsDeleteOwned | ecRightsCreateSubfolder | ecRightsFolderVisible;
		sPermission.ulState = RIGHT_NEW|RIGHT_AUTOUPDATE_DENIED;
		sPermission.ulType = ACCESS_TYPE_GRANT;
		sPermission.sUserId.cb = cbUserId;
		sPermission.sUserId.lpb = (unsigned char*)lpUserId;
		hr = lpFolderRootST->QueryInterface(IID_IECSecurity, &~lpECSecurity);
		if (hr != hrSuccess)
			goto exit;
		hr = lpECSecurity->SetPermissionRules(1, &sPermission);
		if (hr != hrSuccess)
			goto exit;

		// indicate, validity of the entry identifiers of the folders in a message store
		// Public folder have default the mask
		cValues = 2;
		ECAllocateBuffer(sizeof(SPropValue)*cValues, (void**)&lpPropValue);
		lpPropValue->ulPropTag = PR_VALID_FOLDER_MASK;
		lpPropValue->Value.ul = FOLDER_FINDER_VALID | FOLDER_IPM_SUBTREE_VALID | FOLDER_IPM_INBOX_VALID | FOLDER_IPM_OUTBOX_VALID | FOLDER_IPM_WASTEBASKET_VALID | FOLDER_IPM_SENTMAIL_VALID | FOLDER_VIEWS_VALID | FOLDER_COMMON_VIEWS_VALID;

		// Set store displayname
		lpPropValue[1].ulPropTag = PR_DISPLAY_NAME_W;
		lpPropValue[1].Value.lpszW = const_cast<wchar_t *>(L"Public folder"); //FIXME: set the right public folder name here?

		// Set the property into the store
		hr = lpecMsgStore->SetProps(cValues, lpPropValue, NULL);
		if(hr != hrSuccess)
			goto exit;
		ECFreeBuffer(lpPropValue);
		lpPropValue = NULL;
	}else if(ulStoreType == ECSTORE_TYPE_PRIVATE) { //Private folder

		// Create Folder COMMON_VIEWS into the rootfolder
		// folder holds views that are standard for the message store
		hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, _T("IPM_COMMON_VIEWS"), _T(""), PR_COMMON_VIEWS_ENTRYID, 0, NULL, NULL);
		if(hr != hrSuccess)
			goto exit;

		// Create Folder VIEWS into the rootfolder
		// Personal: folder holds views that are defined by a particular user
		hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, _T("IPM_VIEWS"), _T(""), PR_VIEWS_ENTRYID, 0, NULL, NULL);
		if(hr != hrSuccess)
			goto exit;

		// Create Folder FINDER_ROOT into the rootfolder
		hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, _T("FINDER_ROOT"), _T(""), PR_FINDER_ENTRYID, 0, NULL, &~lpMAPIFolder3);
		if(hr != hrSuccess)
			goto exit;

		sPermission.ulRights = ecRightsFolderVisible|ecRightsReadAny|ecRightsCreateSubfolder|ecRightsEditOwned|ecRightsDeleteOwned;
		sPermission.ulState = RIGHT_NEW|RIGHT_AUTOUPDATE_DENIED;
		sPermission.ulType = ACCESS_TYPE_GRANT;
		sPermission.sUserId.cb = g_cbEveryoneEid;
		sPermission.sUserId.lpb = g_lpEveryoneEid;
		hr = lpMAPIFolder3->QueryInterface(IID_IECSecurity, &~lpECSecurity);
		if(hr != hrSuccess)
			goto exit;

		hr = lpECSecurity->SetPermissionRules(1, &sPermission);
		if(hr != hrSuccess)
			goto exit;

		// Create Shortcuts
		// Shortcuts for the favorites
		hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, _("Shortcut"), _T(""), PR_IPM_FAVORITES_ENTRYID, 0, NULL, NULL);
		if(hr != hrSuccess)
			goto exit;

		// Create Schedule folder
		hr = CreateSpecialFolder(lpFolderRoot, lpecMsgStore, _T("Schedule"), _T(""), PR_SCHEDULE_FOLDER_ENTRYID, 0, NULL, NULL);
		if(hr != hrSuccess)
			goto exit;

		// Create folders into IPM_SUBTREE
		// Folders like: Inbox, outbox, tasks, agenda, notes, trashcan, send items, contacts,concepts

		// Create Inbox
		hr = CreateSpecialFolder(lpFolderRootST, NULL, _("Inbox"), _T(""), 0, 0, NULL, &~lpInboxFolder);
		if(hr != hrSuccess)
			goto exit;

		// Get entryid of the folder
		hr = HrGetOneProp(lpInboxFolder, PR_ENTRYID, &lpPropValue);
		if(hr != hrSuccess)
			goto exit;

		hr = lpecMsgStore->SetReceiveFolder(NULL, 0, lpPropValue->Value.bin.cb, (LPENTRYID)lpPropValue->Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;

		hr = lpecMsgStore->SetReceiveFolder((LPTSTR)"IPM", 0, lpPropValue->Value.bin.cb, (LPENTRYID)lpPropValue->Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;

		hr = lpecMsgStore->SetReceiveFolder((LPTSTR)"REPORT.IPM", 0, lpPropValue->Value.bin.cb, (LPENTRYID)lpPropValue->Value.bin.lpb);
		if(hr != hrSuccess)
			goto exit;

		ECFreeBuffer(lpPropValue);
		lpPropValue = NULL;
		hr = lpInboxFolder->QueryInterface(IID_ECMAPIFolder, &~lpECMapiFolderInbox);
		if(hr != hrSuccess)
			goto exit;

		// Create Outbox
		hr = CreateSpecialFolder(lpFolderRootST, lpecMsgStore, _("Outbox"), _T(""), PR_IPM_OUTBOX_ENTRYID, 0, NULL, NULL);
		if(hr != hrSuccess)
			goto exit;

		// Create Trashcan
		hr = CreateSpecialFolder(lpFolderRootST, lpecMsgStore, _("Deleted Items"), _T(""), PR_IPM_WASTEBASKET_ENTRYID, 0, NULL, NULL);
		if(hr != hrSuccess)
			goto exit;

		// Create Sent Items
		hr = CreateSpecialFolder(lpFolderRootST, lpecMsgStore, _("Sent Items"), _T(""), PR_IPM_SENTMAIL_ENTRYID, 0, NULL, NULL);
		if(hr != hrSuccess)
			goto exit;

		// Create Contacts
		hr = CreateSpecialFolder(lpFolderRootST, lpECMapiFolderInbox, _("Contacts"), _T(""), PR_IPM_CONTACT_ENTRYID, 0, _T("IPF.Contact"), &~lpMAPIFolder);
		if(hr != hrSuccess)
			goto exit;

		hr = SetSpecialEntryIdOnFolder(lpMAPIFolder, lpMapiFolderRoot, PR_IPM_CONTACT_ENTRYID, 0);
		if(hr != hrSuccess)
			goto exit;

		// Create calendar
		hr = CreateSpecialFolder(lpFolderRootST, lpECMapiFolderInbox, _("Calendar"), _T(""), PR_IPM_APPOINTMENT_ENTRYID, 0, _T("IPF.Appointment"), &~lpCalendarFolder);
		if(hr != hrSuccess)
			goto exit;

		hr = SetSpecialEntryIdOnFolder(lpCalendarFolder, lpMapiFolderRoot, PR_IPM_APPOINTMENT_ENTRYID, 0);
		if(hr != hrSuccess)
			goto exit;

		// Create Drafts
		hr = CreateSpecialFolder(lpFolderRootST, lpECMapiFolderInbox, _("Drafts"), _T(""), PR_IPM_DRAFTS_ENTRYID, 0, _T("IPF.Note"), &~lpMAPIFolder);
		if(hr != hrSuccess)
			goto exit;

		hr = SetSpecialEntryIdOnFolder(lpMAPIFolder, lpMapiFolderRoot, PR_IPM_DRAFTS_ENTRYID, 0);
		if(hr != hrSuccess)
			goto exit;

		// Create journal
		hr = CreateSpecialFolder(lpFolderRootST, lpECMapiFolderInbox, _("Journal"), _T(""), PR_IPM_JOURNAL_ENTRYID, 0, _T("IPF.Journal"), &~lpMAPIFolder);
		if(hr != hrSuccess)
			goto exit;

		hr = SetSpecialEntryIdOnFolder(lpMAPIFolder, lpMapiFolderRoot, PR_IPM_JOURNAL_ENTRYID, 0);
		if(hr != hrSuccess)
			goto exit;

		// Create Notes
		hr = CreateSpecialFolder(lpFolderRootST, lpECMapiFolderInbox, _("Notes"), _T(""), PR_IPM_NOTE_ENTRYID, 0, _T("IPF.StickyNote"), &~lpMAPIFolder);
		if(hr != hrSuccess)
			goto exit;

		hr = SetSpecialEntryIdOnFolder(lpMAPIFolder, lpMapiFolderRoot, PR_IPM_NOTE_ENTRYID, 0);
		if(hr != hrSuccess)
			goto exit;

		// Create Tasks
		hr = CreateSpecialFolder(lpFolderRootST, lpECMapiFolderInbox, _("Tasks"), _T(""), PR_IPM_TASK_ENTRYID, 0, _T("IPF.Task"), &~lpMAPIFolder);
		if(hr != hrSuccess)
			goto exit;

		hr = SetSpecialEntryIdOnFolder(lpMAPIFolder, lpMapiFolderRoot, PR_IPM_TASK_ENTRYID, 0);
		if(hr != hrSuccess)
			goto exit;

		// Create Junk mail (position 5(4 in array) in the mvprop PR_ADDITIONAL_REN_ENTRYIDS)
		hr = CreateSpecialFolder(lpFolderRootST, lpECMapiFolderInbox, _("Junk E-mail"), _T(""), PR_ADDITIONAL_REN_ENTRYIDS, 4, _T("IPF.Note"), &~lpMAPIFolder);
		if(hr != hrSuccess)
			goto exit;

		hr = SetSpecialEntryIdOnFolder(lpMAPIFolder, lpMapiFolderRoot, PR_ADDITIONAL_REN_ENTRYIDS, 4);
		if(hr != hrSuccess)
			goto exit;

		// Create Freebusy folder data
		hr = CreatePrivateFreeBusyData(lpFolderRoot, lpInboxFolder, lpCalendarFolder);
		if(hr != hrSuccess)
			goto exit;

		// Create Outlook 2007/2010 Additional folders
		hr = CreateAdditionalFolder(lpFolderRoot, lpInboxFolder, lpFolderRootST, RSF_PID_RSS_SUBSCRIPTION, _("RSS Feeds"), _("RSS Feed comment"), _T("IPF.Note.OutlookHomepage"), false);
		if(hr != hrSuccess)
			goto exit;

		hr = CreateAdditionalFolder(lpFolderRoot, lpInboxFolder, lpFolderRootST, RSF_PID_CONV_ACTIONS, _("Conversation Action Settings"), _T(""), _T("IPF.Configuration"), true);
		if(hr != hrSuccess)
			goto exit;

		hr = CreateAdditionalFolder(lpFolderRoot, lpInboxFolder, lpFolderRootST, RSF_PID_COMBINED_ACTIONS, _("Quick Step Settings"), _T(""), _T("IPF.Configuration"), true);
		if(hr != hrSuccess)
			goto exit;

		hr = CreateAdditionalFolder(lpFolderRoot, lpInboxFolder, lpFolderRootST, RSF_PID_SUGGESTED_CONTACTS, _("Suggested Contacts"), _T(""), _T("IPF.Contact"), false);
		if(hr != hrSuccess)
			goto exit;
		

		// indicate, validity of the entry identifiers of the folders in a message store
		cValues = 1;
		ECAllocateBuffer(sizeof(SPropValue)*cValues, (void**)&lpPropValue);
		lpPropValue[0].ulPropTag = PR_VALID_FOLDER_MASK;
		lpPropValue[0].Value.ul = FOLDER_VIEWS_VALID | FOLDER_COMMON_VIEWS_VALID|FOLDER_FINDER_VALID | FOLDER_IPM_INBOX_VALID | FOLDER_IPM_OUTBOX_VALID | FOLDER_IPM_SENTMAIL_VALID | FOLDER_IPM_SUBTREE_VALID | FOLDER_IPM_WASTEBASKET_VALID;

		// Set the property into the store
		hr = lpecMsgStore->SetProps(cValues, lpPropValue, NULL);
		if(hr != hrSuccess)
			goto exit;
		ECFreeBuffer(lpPropValue);
		lpPropValue = NULL;
	}

	*lpcbStoreId = cbStoreId;
	*lppStoreId = lpStoreId;

	*lpcbRootId = cbRootId;
	*lppRootId = lpRootId;

exit:
	if(lpECUser)
		ECFreeBuffer(lpECUser);

	if(lpECGroup)
		ECFreeBuffer(lpECGroup);

	if(lpECCompany)
		ECFreeBuffer(lpECCompany);

	if(lpPropValue)
		ECFreeBuffer(lpPropValue);
	return hr;
}

/**
 * Create a new store that contains nothing boot the root folder.
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
HRESULT ECMsgStore::CreateEmptyStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags, ULONG* lpcbStoreId, LPENTRYID* lppStoreId, ULONG* lpcbRootId, LPENTRYID* lppRootId)
{
	HRESULT hr = hrSuccess;
	ULONG cbStoreId = 0;
	LPENTRYID lpStoreId = NULL;
	ULONG cbRootId = 0;
	LPENTRYID lpRootId = NULL;
	GUID guidStore;

	// Check requested store type
	if (!ECSTORE_TYPE_ISVALID(ulStoreType) ||
		(ulFlags != 0 && ulFlags != EC_OVERRIDE_HOMESERVER))
	{
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	// Check passed store and root entry ids.
	if (!lpcbStoreId || !lppStoreId || !lpcbRootId || !lppRootId) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	if (!*lpcbStoreId != !*lppStoreId)	{	// One set, one unset
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	if (!*lpcbRootId != !*lppRootId)	{	// One set, one unset
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	if (*lppRootId && !*lppStoreId) {		// Root id set, but storeid unset
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}
	if ((*lpcbStoreId == 0 || *lpcbRootId == 0) && CoCreateGuid(&guidStore) != S_OK) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	if (*lpcbStoreId == 0) {
		// Create store entryid
		hr = HrCreateEntryId(guidStore, MAPI_STORE, &cbStoreId, &lpStoreId);
		if (hr != hrSuccess)
			goto exit;
	} else {
		ULONG cbTmp = 0;
		LPENTRYID lpTmp = NULL;

		hr = UnWrapStoreEntryID(*lpcbStoreId, *lppStoreId, &cbTmp, &lpTmp);
		if (hr == MAPI_E_INVALID_ENTRYID) {	// Could just be a non-wrapped entryid
			cbTmp = *lpcbStoreId;
			lpTmp = *lppStoreId;
		}
		hr = UnWrapServerClientStoreEntry(cbTmp, lpTmp, &cbStoreId, &lpStoreId);
		if (hr != hrSuccess) {
			if (lpTmp != *lppStoreId)
				MAPIFreeBuffer(lpTmp);
			goto exit;
		}
	}

	if (*lpcbRootId == 0) {
		// create root entryid
		hr = HrCreateEntryId(guidStore, MAPI_FOLDER, &cbRootId, &lpRootId);
		if (hr != hrSuccess)
			goto exit;
	} else {
		cbRootId = *lpcbRootId;
		lpRootId = *lppRootId;
	}

	// Create the messagestore
	hr = lpTransport->HrCreateStore(ulStoreType, cbUserId, lpUserId, cbStoreId, lpStoreId, cbRootId, lpRootId, ulFlags);
	if (hr != hrSuccess)
		goto exit;

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

exit:
	if (lpcbStoreId != NULL && *lpcbStoreId == 0)
		MAPIFreeBuffer(lpStoreId);
	if (lpcbStoreId != NULL && *lpcbStoreId == 0)
		MAPIFreeBuffer(lpRootId);

	return hr;
}

HRESULT ECMsgStore::HookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId, LPGUID lpGuid)
{
	return lpTransport->HrHookStore(ulStoreType, cbUserId, lpUserId, lpGuid, 0);
}

HRESULT ECMsgStore::UnhookStore(ULONG ulStoreType, ULONG cbUserId, LPENTRYID lpUserId)
{
	return lpTransport->HrUnhookStore(ulStoreType, cbUserId, lpUserId, 0);
}

HRESULT ECMsgStore::RemoveStore(LPGUID lpGuid)
{
	return lpTransport->HrRemoveStore(lpGuid, 0);
}

HRESULT ECMsgStore::ResolveStore(LPGUID lpGuid, ULONG *lpulUserID, ULONG* lpcbStoreID, LPENTRYID* lppStoreID)
{
	ULONG			cbStoreEntryID = 0;
	memory_ptr<ENTRYID> lpStoreEntryID;

	HRESULT hr = lpTransport->HrResolveStore(lpGuid, lpulUserID, &cbStoreEntryID, &~lpStoreEntryID);
	if (hr != hrSuccess)
		return hr;
	return WrapStoreEntryID(0, (LPTSTR)WCLIENT_DLL_NAME, cbStoreEntryID,
	       lpStoreEntryID, lpcbStoreID, lppStoreID);
}

HRESULT ECMsgStore::SetSpecialEntryIdOnFolder(LPMAPIFOLDER lpFolder, ECMAPIProp *lpFolderPropSet, unsigned int ulPropTag, unsigned int ulMVPos)
{
	LPSPropValue	lpPropValue = NULL;
	LPSPropValue	lpPropMVValue = NULL;
	LPSPropValue	lpPropMVValueNew = NULL;

	// Get entryid of the folder
	HRESULT hr = HrGetOneProp(lpFolder, PR_ENTRYID, &lpPropValue);
	if(hr != hrSuccess)
		goto exit;

	if (PROP_TYPE(ulPropTag) & MV_FLAG) {
		ECAllocateBuffer(sizeof(SPropValue), (void**)&lpPropMVValueNew);
		memset(lpPropMVValueNew, 0, sizeof(SPropValue));

		hr = HrGetOneProp(lpFolder, ulPropTag, &lpPropMVValue);
		if(hr != hrSuccess) {
			lpPropMVValueNew->Value.MVbin.cValues = (ulMVPos+1);
			ECAllocateMore(sizeof(SBinary)*lpPropMVValueNew->Value.MVbin.cValues, lpPropMVValueNew, (void**)&lpPropMVValueNew->Value.MVbin.lpbin);
			memset(lpPropMVValueNew->Value.MVbin.lpbin, 0, sizeof(SBinary)*lpPropMVValueNew->Value.MVbin.cValues);

			for (unsigned int i = 0; i <lpPropMVValueNew->Value.MVbin.cValues; ++i)
				if(ulMVPos == i)
					lpPropMVValueNew->Value.MVbin.lpbin[i] = lpPropValue->Value.bin;
		}else{
			lpPropMVValueNew->Value.MVbin.cValues = (lpPropMVValue->Value.MVbin.cValues < ulMVPos)? lpPropValue->Value.bin.cb : ulMVPos+1;
			ECAllocateMore(sizeof(SBinary)*lpPropMVValueNew->Value.MVbin.cValues, lpPropMVValueNew, (void**)&lpPropMVValueNew->Value.MVbin.lpbin);

			memset(lpPropMVValueNew->Value.MVbin.lpbin, 0, sizeof(SBinary)*lpPropMVValueNew->Value.MVbin.cValues);

			for (unsigned int i = 0; i < lpPropMVValueNew->Value.MVbin.cValues; ++i)
				if(ulMVPos == i)
					lpPropMVValueNew->Value.MVbin.lpbin[i] = lpPropValue->Value.bin;
				else
					lpPropMVValueNew->Value.MVbin.lpbin[i] = lpPropMVValue->Value.MVbin.lpbin[i];
		}

		lpPropMVValueNew->ulPropTag = ulPropTag;

		// Set the property into the right folder
		hr = lpFolderPropSet->SetProps(1, lpPropMVValueNew, NULL);
		ECFreeBuffer(lpPropMVValueNew);
		if (hr != hrSuccess)
			goto exit;
	}else{
		// Set the property tag value
		lpPropValue->ulPropTag = ulPropTag;

		// Set the property into the right folder
		hr = lpFolderPropSet->SetProps(1, lpPropValue, NULL);
		if(hr != hrSuccess)
			goto exit;
	}

exit:
	if(lpPropValue)
		ECFreeBuffer(lpPropValue);

	return hr;
}

HRESULT ECMsgStore::CreateSpecialFolder(LPMAPIFOLDER lpFolderParent,
    ECMAPIProp *lpFolderPropSet, const TCHAR *lpszFolderName,
    const TCHAR *lpszFolderComment, unsigned int ulPropTag,
    unsigned int ulMVPos, const TCHAR *lpszContainerClass,
    LPMAPIFOLDER *lppMAPIFolder)
{
	HRESULT 		hr = hrSuccess;
	object_ptr<IMAPIFolder> lpMAPIFolder;
	LPSPropValue	lpPropValue = NULL;

	if (lpFolderParent == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// Add a referention at the folders
	lpFolderParent->AddRef();
	if (lpFolderPropSet != nullptr)
		lpFolderPropSet->AddRef();

	// Create the folder
	hr = lpFolderParent->CreateFolder(FOLDER_GENERIC,
	     const_cast<LPTSTR>(lpszFolderName),
	     const_cast<LPTSTR>(lpszFolderComment), &IID_IMAPIFolder,
	     OPEN_IF_EXISTS | fMapiUnicode, &~lpMAPIFolder);
	if(hr != hrSuccess)
		goto exit;

	// Set the special property
	if(lpFolderPropSet) {
		hr = SetSpecialEntryIdOnFolder(lpMAPIFolder, lpFolderPropSet, ulPropTag, ulMVPos);
		if(hr != hrSuccess)
			goto exit;

	}

	if (lpszContainerClass && _tcslen(lpszContainerClass) > 0) {
		ECAllocateBuffer(sizeof(SPropValue), (void**)&lpPropValue);
		
		lpPropValue[0].ulPropTag = PR_CONTAINER_CLASS;
		ECAllocateMore((_tcslen(lpszContainerClass) + 1) * sizeof(TCHAR), lpPropValue, (void**)&lpPropValue[0].Value.LPSZ);
		_tcscpy(lpPropValue[0].Value.LPSZ, lpszContainerClass);

		// Set the property
		hr = lpMAPIFolder->SetProps(1, lpPropValue, NULL);
		if(hr != hrSuccess)
			goto exit;
			
		ECFreeBuffer(lpPropValue); 
		lpPropValue = NULL; 
	}

	if(lppMAPIFolder) {
		hr = lpMAPIFolder->QueryInterface(IID_IMAPIFolder, (void**)lppMAPIFolder);
		if(hr != hrSuccess)
			goto exit;
	}

exit:
	if(lpPropValue)
		ECFreeBuffer(lpPropValue);
	if(lpFolderParent)
		lpFolderParent->Release();

	if(lpFolderPropSet)
		lpFolderPropSet->Release();

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

HRESULT ECMsgStore::GetUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG ulFlags,
    ECUSER **lppECUser)
{
	return lpTransport->HrGetUser(cbUserId, lpUserId, ulFlags, lppECUser);
}

HRESULT ECMsgStore::DeleteUser(ULONG cbUserId, LPENTRYID lpUserId)
{
	return lpTransport->HrDeleteUser(cbUserId, lpUserId);
}

HRESULT ECMsgStore::ResolveUserName(LPCTSTR lpszUserName, ULONG ulFlags, ULONG *lpcbUserId, LPENTRYID *lppUserId)
{
	return lpTransport->HrResolveUserName(lpszUserName, ulFlags, lpcbUserId, lppUserId);
}

HRESULT ECMsgStore::GetSendAsList(ULONG cbUserId, LPENTRYID lpUserId,
    ULONG ulFlags, ULONG *lpcSenders, ECUSER **lppSenders)
{
	return lpTransport->HrGetSendAsList(cbUserId, lpUserId, ulFlags, lpcSenders, lppSenders);
}

HRESULT ECMsgStore::AddSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId)
{
	return lpTransport->HrAddSendAsUser(cbUserId, lpUserId, cbSenderId, lpSenderId);
}

HRESULT ECMsgStore::DelSendAsUser(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbSenderId, LPENTRYID lpSenderId)
{
	return lpTransport->HrDelSendAsUser(cbUserId, lpUserId, cbSenderId, lpSenderId);
}

HRESULT ECMsgStore::GetUserClientUpdateStatus(ULONG cbUserId,
    LPENTRYID lpUserId, ULONG ulFlags, ECUSERCLIENTUPDATESTATUS **lppECUCUS)
{
	return lpTransport->HrGetUserClientUpdateStatus(cbUserId, lpUserId, ulFlags, lppECUCUS);
}

HRESULT ECMsgStore::RemoveAllObjects(ULONG cbUserId, LPENTRYID lpUserId)
{
	return lpTransport->HrRemoveAllObjects(cbUserId, lpUserId);
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

HRESULT ECMsgStore::GetGroup(ULONG cbGroupId, LPENTRYID lpGroupId,
    ULONG ulFlags, ECGROUP **lppECGroup)
{
	return lpTransport->HrGetGroup(cbGroupId, lpGroupId, ulFlags, lppECGroup);
}

HRESULT ECMsgStore::DeleteGroup(ULONG cbGroupId, LPENTRYID lpGroupId)
{
	return lpTransport->HrDeleteGroup(cbGroupId, lpGroupId);
}

//Group and user functions
HRESULT ECMsgStore::DeleteGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId)
{
	return lpTransport->HrDeleteGroupUser(cbGroupId, lpGroupId, cbUserId, lpUserId);
}

HRESULT ECMsgStore::AddGroupUser(ULONG cbGroupId, LPENTRYID lpGroupId, ULONG cbUserId, LPENTRYID lpUserId)
{
	return lpTransport->HrAddGroupUser(cbGroupId, lpGroupId, cbUserId, lpUserId);
}

HRESULT ECMsgStore::GetUserListOfGroup(ULONG cbGroupId, LPENTRYID lpGroupId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	return lpTransport->HrGetUserListOfGroup(cbGroupId, lpGroupId, ulFlags, lpcUsers, lppsUsers);
}

HRESULT ECMsgStore::GetGroupListOfUser(ULONG cbUserId, LPENTRYID lpUserId,
    ULONG ulFlags, ULONG *lpcGroups, ECGROUP **lppsGroups)
{
	return lpTransport->HrGetGroupListOfUser(cbUserId, lpUserId, ulFlags, lpcGroups, lppsGroups);
}

HRESULT ECMsgStore::CreateCompany(ECCOMPANY *lpECCompany, ULONG ulFlags,
    ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId)
{
	return lpTransport->HrCreateCompany(lpECCompany, ulFlags, lpcbCompanyId, lppCompanyId);
}

HRESULT ECMsgStore::DeleteCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	return lpTransport->HrDeleteCompany(cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::SetCompany(ECCOMPANY *lpECCompany, ULONG ulFlags)
{
	return lpTransport->HrSetCompany(lpECCompany, ulFlags);
}

HRESULT ECMsgStore::GetCompany(ULONG cbCompanyId, LPENTRYID lpCompanyId,
    ULONG ulFlags, ECCOMPANY **lppECCompany)
{
	return lpTransport->HrGetCompany(cbCompanyId, lpCompanyId, ulFlags, lppECCompany);
}

HRESULT ECMsgStore::ResolveCompanyName(LPCTSTR lpszCompanyName, ULONG ulFlags, ULONG *lpcbCompanyId, LPENTRYID *lppCompanyId)
{
	return lpTransport->HrResolveCompanyName(lpszCompanyName, ulFlags, lpcbCompanyId, lppCompanyId);
}

HRESULT ECMsgStore::GetCompanyList(ULONG ulFlags, ULONG *lpcCompanies,
    ECCOMPANY **lppsCompanies)
{
	return lpTransport->HrGetCompanyList(ulFlags, lpcCompanies, lppsCompanies);
}

HRESULT ECMsgStore::AddCompanyToRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	return lpTransport->HrAddCompanyToRemoteViewList(cbSetCompanyId, lpSetCompanyId, cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::DelCompanyFromRemoteViewList(ULONG cbSetCompanyId, LPENTRYID lpSetCompanyId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	return lpTransport->HrDelCompanyFromRemoteViewList(cbSetCompanyId, lpSetCompanyId, cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::GetRemoteViewList(ULONG cbCompanyId, LPENTRYID lpCompanyId,
    ULONG ulFlags, ULONG *lpcCompanies, ECCOMPANY **lppsCompanies)
{
	return lpTransport->HrGetRemoteViewList(cbCompanyId, lpCompanyId, ulFlags, lpcCompanies, lppsCompanies);
}

HRESULT ECMsgStore::AddUserToRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	return lpTransport->HrAddUserToRemoteAdminList(cbUserId, lpUserId, cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::DelUserFromRemoteAdminList(ULONG cbUserId, LPENTRYID lpUserId, ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	return lpTransport->HrDelUserFromRemoteAdminList(cbUserId, lpUserId, cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::GetRemoteAdminList(ULONG cbCompanyId,
    LPENTRYID lpCompanyId, ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	return lpTransport->HrGetRemoteAdminList(cbCompanyId, lpCompanyId, ulFlags, lpcUsers, lppsUsers);
}

HRESULT ECMsgStore::SyncUsers(ULONG cbCompanyId, LPENTRYID lpCompanyId)
{
	return lpTransport->HrSyncUsers(cbCompanyId, lpCompanyId);
}

HRESULT ECMsgStore::GetQuota(ULONG cbUserId, LPENTRYID lpUserId,
    bool bGetUserDefault, ECQUOTA **lppsQuota)
{
	return lpTransport->GetQuota(cbUserId, lpUserId, bGetUserDefault, lppsQuota);
}

HRESULT ECMsgStore::SetQuota(ULONG cbUserId, LPENTRYID lpUserId,
    ECQUOTA *lpsQuota)
{
	return lpTransport->SetQuota(cbUserId, lpUserId, lpsQuota);
}

HRESULT ECMsgStore::AddQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType)
{
	return lpTransport->AddQuotaRecipient(cbCompanyId, lpCompanyId, cbRecipientId, lpRecipientId, ulType);
}

HRESULT ECMsgStore::DeleteQuotaRecipient(ULONG cbCompanyId, LPENTRYID lpCmopanyId, ULONG cbRecipientId, LPENTRYID lpRecipientId, ULONG ulType)
{
	return lpTransport->DeleteQuotaRecipient(cbCompanyId, lpCmopanyId, cbRecipientId, lpRecipientId, ulType);
}

HRESULT ECMsgStore::GetQuotaRecipients(ULONG cbUserId, LPENTRYID lpUserId,
    ULONG ulFlags, ULONG *lpcUsers, ECUSER **lppsUsers)
{
	return lpTransport->GetQuotaRecipients(cbUserId, lpUserId, ulFlags, lpcUsers, lppsUsers);
}

HRESULT ECMsgStore::GetQuotaStatus(ULONG cbUserId, LPENTRYID lpUserId,
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
	HRESULT hr = hrSuccess;
	object_ptr<WSTableView> lpTableView;
	object_ptr<ECMAPITable> lpTable;

	if (lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// notifications? set 1st param: m_lpNotifyClient
	hr = ECMAPITable::Create("Userstores table", NULL, 0, &~lpTable);
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

HRESULT ECMsgStore::GetPublicStoreEntryID(ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID)
{
	ULONG cbStoreID;
	EntryIdPtr ptrStoreID;
	std::string strRedirServer;

	HRESULT hr = lpTransport->HrGetPublicStore(ulFlags, &cbStoreID, &~ptrStoreID, &strRedirServer);
	if (hr == MAPI_E_UNABLE_TO_COMPLETE) {
		WSTransportPtr ptrTransport;

		hr = lpTransport->CreateAndLogonAlternate(strRedirServer.c_str(), &~ptrTransport);
		if (hr != hrSuccess)
			return hr;
		hr = ptrTransport->HrGetPublicStore(ulFlags, &cbStoreID, &~ptrStoreID);
	}
	if (hr != hrSuccess)
		return hr;

	return lpSupport->WrapStoreEntryID(cbStoreID, ptrStoreID, lpcbStoreID, lppStoreID);
}

HRESULT ECMsgStore::GetArchiveStoreEntryID(LPCTSTR lpszUserName, LPCTSTR lpszServerName, ULONG ulFlags, ULONG* lpcbStoreID, LPENTRYID* lppStoreID)
{
	HRESULT hr;
	ULONG cbStoreID;
	EntryIdPtr ptrStoreID;

	if (lpszUserName == NULL || lpcbStoreID == NULL || lppStoreID == NULL)
		return MAPI_E_INVALID_PARAMETER;

	if (lpszServerName != NULL) {
		WSTransportPtr ptrTransport;

		hr = GetTransportToNamedServer(lpTransport, lpszServerName, ulFlags, &~ptrTransport);
		if (hr != hrSuccess)
			return hr;
		hr = ptrTransport->HrResolveTypedStore(convstring(lpszUserName, ulFlags), ECSTORE_TYPE_ARCHIVE, &cbStoreID, &~ptrStoreID);
		if (hr != hrSuccess)
			return hr;
	} else {
		hr = lpTransport->HrResolveTypedStore(convstring(lpszUserName, ulFlags), ECSTORE_TYPE_ARCHIVE, &cbStoreID, &~ptrStoreID);
		if (hr != hrSuccess)
			return hr;
	}

	return lpSupport->WrapStoreEntryID(cbStoreID, ptrStoreID, lpcbStoreID, lppStoreID);
}

HRESULT ECMsgStore::ResetFolderCount(ULONG cbEntryId, LPENTRYID lpEntryId, ULONG *lpulUpdates)
{
	return lpTransport->HrResetFolderCount(cbEntryId, lpEntryId, lpulUpdates);
}

// This is almost the same as getting a 'normal' outgoing table, except we pass NULL as PEID for the store
HRESULT ECMsgStore::GetMasterOutgoingTable(ULONG ulFlags, IMAPITable ** lppOutgoingTable)
{
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableOutGoingQueue> lpTableOps;

	HRESULT hr = ECMAPITable::Create("Master outgoing queue", this->m_lpNotifyClient, 0, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	hr = this->lpTransport->HrOpenTableOutGoingQueueOps(0, NULL, this, &~lpTableOps);
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

	return this->lpTransport->HrFinishedMessage(cbEntryId, lpEntryId, EC_SUBMIT_MASTER | ulFlags);
}

// MAPIOfflineMgr
HRESULT ECMsgStore::SetCurrentState(ULONG ulFlags, ULONG ulMask, ULONG ulState, void* pReserved)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMsgStore::GetCapabilities(ULONG *pulCapabilities)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMsgStore::GetCurrentState(ULONG* pulState)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMsgStore::Advise(ULONG ulFlags, MAPIOFFLINE_ADVISEINFO* pAdviseInfo, ULONG* pulAdviseToken)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECMsgStore::Unadvise(ULONG ulFlags,ULONG ulAdviseToken)
{
	return MAPI_E_NO_SUPPORT;
}

// ProxyStoreObject
HRESULT ECMsgStore::UnwrapNoRef(LPVOID *ppvObject) 
{
	if (ppvObject == NULL)
		return MAPI_E_INVALID_PARAMETER;

	// Because the function UnwrapNoRef return a non referenced object, QueryInterface isn't needed.
	*ppvObject = &this->m_xMsgStoreProxy;	
	return hrSuccess;
}

// ECMultiStoreTable

// open a table with given entryids and columns.
// entryids can be from any store
HRESULT ECMsgStore::OpenMultiStoreTable(LPENTRYLIST lpMsgList, ULONG ulFlags, LPMAPITABLE *lppTable) {
	HRESULT		hr = hrSuccess;
	object_ptr<ECMAPITable> lpTable;
	object_ptr<WSTableView> lpTableOps;

	if (lpMsgList == nullptr || lppTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	// no notifications on this table
	hr = ECMAPITable::Create("Multistore table", NULL, ulFlags, &~lpTable);
	if (hr != hrSuccess)
		return hr;

	// open a table on the server, with content specified in lpMsgList
	// TODO: my entryid ?
	hr = lpTransport->HrOpenMultiStoreTable(lpMsgList, ulFlags, 0, NULL, this, &~lpTableOps);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->HrSetTableOps(lpTableOps, !(ulFlags & MAPI_DEFERRED_ERRORS));
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->QueryInterface(IID_IMAPITable, (void **)lppTable);

	// add child really needed?
	AddChild(lpTable);
	return hr;
}

HRESULT ECMsgStore::LicenseAuth(unsigned char *lpData, unsigned int ulSize, unsigned char **lppResponse, unsigned int * lpulResponseData)
{
	return lpTransport->HrLicenseAuth(lpData, ulSize, lppResponse, lpulResponseData);
}

HRESULT ECMsgStore::LicenseCapa(unsigned int ulServiceType, char ***lppszCapas, unsigned int *lpulSize)
{
	return lpTransport->HrLicenseCapa(ulServiceType, lppszCapas, lpulSize);
}

HRESULT ECMsgStore::LicenseUsers(unsigned int ulServiceType, unsigned int *lpulUsers)
{
	return lpTransport->HrLicenseUsers(ulServiceType, lpulUsers);
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
	vector<string> parts;
	vector<string>::const_reverse_iterator riPart;

	parts = tokenize(strMsgStoreDN.str(), "/");

	// We need at least 2 parts.
	if (parts.size() < 2)
		return MAPI_E_INVALID_PARAMETER;

	// Check if the last part equals 'cn=Microsoft Private MDB'
	riPart = parts.crbegin();
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
	WSMessageStreamExporterPtr ptrStreamExporter;
	WSTransportPtr ptrTransport;

	if (ulStart > sChanges.size())
		return MAPI_E_INVALID_PARAMETER;
	if (ulStart + ulCount > sChanges.size())
		ulCount = sChanges.size() - ulStart;
	if (ulCount == 0)
		return MAPI_E_UNABLE_TO_COMPLETE;

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

// IMsgStore interface
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, Advise, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG, ulEventMask), (LPMAPIADVISESINK, lpAdviseSink), (ULONG *, lpulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, Unadvise, (ULONG, ulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, CompareEntryIDs, (ULONG, cbEntryID1), (LPENTRYID, lpEntryID1), (ULONG, cbEntryID2), (LPENTRYID, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, OpenEntry, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, SetReceiveFolder, (LPTSTR, lpszMessageClass), (ULONG, ulFlags), (ULONG, cbEntryID), (LPENTRYID, lpEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, GetReceiveFolder, (LPTSTR, lpszMessageClass), (ULONG, ulFlags), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID), (LPTSTR *, lppszExplicitClass))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, GetReceiveFolderTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, StoreLogoff, (ULONG *, lpulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, AbortSubmit, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, GetOutgoingQueue, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, SetLockState, (LPMESSAGE, lpMessage), (ULONG, ulLockState))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, FinishedMsg, (ULONG, ulFlags), (ULONG, cbEntryID), (LPENTRYID, lpEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, NotifyNewMail, (LPNOTIFICATION, lpNotification))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, GetProps, (const SPropTagArray *, lpPropTagArray), (ULONG, ulFlags), (ULONG *, lpcValues), (SPropValue **, lppPropArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, GetPropList, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, SetProps, (ULONG, cValues), (const SPropValue *, lpPropArray), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, DeleteProps, (const SPropTagArray *, lpPropTagArray), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, CopyTo, (ULONG, ciidExclude), (LPCIID, rgiidExclude), (const SPropTagArray *, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (void *, lpDestObj), (ULONG, ulFlags), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, CopyProps, (const SPropTagArray *, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (void *, lpDestObj), (ULONG, ulFlags), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, GetNamesFromIDs, (LPSPropTagArray *, pptaga), (LPGUID, lpguid), (ULONG, ulFlags), (ULONG *, pcNames), (LPMAPINAMEID **, pppNames))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStore, GetIDsFromNames, (ULONG, cNames), (MAPINAMEID **, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))

/*
 * IExchangeManageStore interface
 */
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore, CreateStoreEntryID, (LPTSTR, lpszMsgStoreDN), (LPTSTR, lpszMailboxDN), (ULONG, ulFlags), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore, EntryIDFromSourceKey, (ULONG, cFolderKeySize), (BYTE *, lpFolderSourceKey), (ULONG, cMessageKeySize), (BYTE *, lpMessageSourceKey), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore, GetRights, (ULONG, cbUserEntryID), (LPENTRYID, lpUserEntryID), (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG *, lpulRights))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore, GetMailboxTable, (LPTSTR, lpszServerName), (LPMAPITABLE *, lppTable), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore, GetPublicFolderTable, (LPTSTR, lpszServerName), (LPMAPITABLE *, lppTable), (ULONG, ulFlags))

// IECServiceAdmin
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, CreateStore, (ULONG, ulStoreType), (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG*, lpcbStoreId), (LPENTRYID*, lppStoreId), (ULONG*, lpcbRootId), (LPENTRYID *, lppRootId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, CreateEmptyStore, (ULONG, ulStoreType), (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, ulFlags), (ULONG*, lpcbStoreId), (LPENTRYID*, lppStoreId), (ULONG*, lpcbRootId), (LPENTRYID *, lppRootId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, HookStore, (ULONG, ulStoreType), (ULONG, cbUserId), (LPENTRYID, lpUserId), (LPGUID, lpGuid))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, UnhookStore, (ULONG, ulStoreType), (ULONG, cbUserId), (LPENTRYID, lpUserId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, RemoveStore, (LPGUID, lpGuid))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, ResolveStore, (LPGUID, lpGuid), (ULONG *, lpulUserID), (ULONG*, lpcbStoreID), (LPENTRYID*, lppStoreID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, CreateUser, (ECUSER *, lpECUser), (ULONG, ulFlags), (ULONG *, lpcbUserId), (LPENTRYID *, lppUserId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, SetUser, (ECUSER *, lpECUser), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetUser, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, ulFlags), (ECUSER **, lppECUser))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, DeleteUser, (ULONG, cbUserId), (LPENTRYID, lpUserId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetUserList, (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId), (ULONG, ulFlags), (ULONG *, lpcUsers), (ECUSER **, lpsUsers))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetSendAsList, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, ulFlags), (ULONG *, lpcSenders), (ECUSER **, lppSenders))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, AddSendAsUser, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, cbSenderId), (LPENTRYID, lpSenderId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, DelSendAsUser, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, cbSenderId), (LPENTRYID, lpSenderId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetUserClientUpdateStatus, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, ulFlags), (ECUSERCLIENTUPDATESTATUS **, lppECUCUS))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, RemoveAllObjects, (ULONG, cbUserId), (LPENTRYID, lpUserId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, ResolveGroupName, (LPCTSTR, lpszGroupName), (ULONG, ulFlags), (ULONG *, lpcbGroupId), (LPENTRYID *, lppGroupId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, ResolveUserName, (LPCTSTR, lpszUserName), (ULONG, ulFlags), (ULONG *, lpcbUserId), (LPENTRYID *, lppUserId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, CreateGroup, (ECGROUP *, lpECGroup), (ULONG, ulFlags), (ULONG *, lpcbGroupId), (LPENTRYID *, lppGroupId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, SetGroup, (ECGROUP *, lpECGroup), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetGroup, (ULONG, cbGroupId), (LPENTRYID, lpGroupId), (ULONG, ulFlags), (ECGROUP **, lppECGroup))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, DeleteGroup, (ULONG, cbGroupId), (LPENTRYID, lpGroupId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetGroupList, (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId), (ULONG, ulFlags), (ULONG *, lpcGroups), (ECGROUP **, lppsGroups))

//Group and user functions
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, DeleteGroupUser, (ULONG, cbGroupId), (LPENTRYID, lpGroupId), (ULONG, cbUserId), (LPENTRYID, lpUserId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, AddGroupUser, (ULONG, cbGroupId), (LPENTRYID, lpGroupId), (ULONG, cbUserId), (LPENTRYID, lpUserId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetUserListOfGroup, (ULONG, cbGroupId), (LPENTRYID, lpGroupId), (ULONG, ulFlags), (ULONG *, lpcUsers), (ECUSER **, lppsUsers))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetGroupListOfUser, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, ulFlags), (ULONG *, lpcGroups), (ECGROUP **, lppsGroups))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, CreateCompany, (ECCOMPANY *, lpECCompany), (ULONG, ulFlags), (ULONG *, lpcbCompanyId), (LPENTRYID *, lppCompanyId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, DeleteCompany, (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, SetCompany, (ECCOMPANY *, lpECCompany), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetCompany, (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId), (ULONG, ulFlags), (ECCOMPANY **, lppECCompany))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, ResolveCompanyName, (LPCTSTR, lpszCompanyName), (ULONG, ulFlags), (ULONG *, lpcbCompanyId), (LPENTRYID *, lppCompanyId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetCompanyList, (ULONG, ulFlags), (ULONG *, lpcCompanies), (ECCOMPANY **, lppsCompanies))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, AddCompanyToRemoteViewList, (ULONG, cbSetCompanyId), (LPENTRYID, lpSetCompanyId), (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, DelCompanyFromRemoteViewList, (ULONG, cbSetCompanyId), (LPENTRYID, lpSetCompanyId), (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetRemoteViewList, (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId), (ULONG, ulFlags), (ULONG *, lpcCompanies), (ECCOMPANY **, lppsCompanies))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, AddUserToRemoteAdminList, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, DelUserFromRemoteAdminList, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetRemoteAdminList, (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId), (ULONG, ulFlags), (ULONG *, lpcUsers), (ECUSER **, lppsUsers))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, SyncUsers, (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetQuota, (ULONG, cbUserId), (LPENTRYID, lpUserId), (bool, bGetUserDefault), (ECQUOTA **, lppsQuota))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, SetQuota, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ECQUOTA *, lpsQuota))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, AddQuotaRecipient, (ULONG, cbCompanyId), (LPENTRYID, lpCompanyId), (ULONG, cbRecipientId), (LPENTRYID, lpRecipientId), (ULONG, ulType))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, DeleteQuotaRecipient, (ULONG, cbCompanyId), (LPENTRYID, lpCmopanyId), (ULONG, cbRecipientId), (LPENTRYID, lpRecipientId), (ULONG, ulType))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetQuotaRecipients, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ULONG, ulFlags), (ULONG *, lpcUsers), (ECUSER **, lppsUsers))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetQuotaStatus, (ULONG, cbUserId), (LPENTRYID, lpUserId), (ECQUOTASTATUS **, lppsQuotaStatus))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, PurgeCache, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, PurgeSoftDelete, (ULONG, ulDays))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, PurgeDeferredUpdates, (ULONG *, lpulRemaining))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetServerDetails, (ECSVRNAMELIST *, lpServerNameList), (ULONG, ulFlags), (ECSERVERLIST **, lppsServerList))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, OpenUserStoresTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, ResolvePseudoUrl, (const char *, lpszPseudoUrl), (char **, lppszServerPath), (bool *, lpbIsPeer))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetPublicStoreEntryID, (ULONG, ulFlags), (ULONG*, lpcbStoreID), (LPENTRYID*, lppStoreID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, GetArchiveStoreEntryID, (LPCTSTR, lpszUserName), (LPCTSTR, lpszServerName), (ULONG, ulFlags), (ULONG*, lpcbStoreID), (LPENTRYID*, lppStoreID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECServiceAdmin, ResetFolderCount, (ULONG, cbEntryId), (LPENTRYID, lpEntryId), (ULONG *, lpulUpdates))

// IECSpooler
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECSpooler, QueryInterface, (REFIID, refiid), (void**, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECSpooler, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECSpooler, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECSpooler, GetMasterOutgoingTable, (ULONG, ulFlags), (IMAPITable **, lppOutgoingTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECSpooler, DeleteFromMasterOutgoingTable, (ULONG, cbEntryID), (const ENTRYID *, lpEntryID), (ULONG, ulFlags))

// Interface IMAPIOfflineMgr
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, MAPIOfflineMgr, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, MAPIOfflineMgr, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MAPIOfflineMgr, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MAPIOfflineMgr, SetCurrentState, (ULONG, ulFlags), (ULONG, ulMask), (ULONG, ulState), (void*, pReserved))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MAPIOfflineMgr, GetCapabilities, (ULONG *, pulCapabilities))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MAPIOfflineMgr, GetCurrentState, (ULONG*, pulState))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MAPIOfflineMgr, Advise, (ULONG, ulFlags), (MAPIOFFLINE_ADVISEINFO*, pAdviseInfo), (ULONG*, pulAdviseToken))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MAPIOfflineMgr, Unadvise, (ULONG, ulFlags), (ULONG, ulAdviseToken))

// Interface IProxyStoreObject
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ProxyStoreObject, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ProxyStoreObject, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ProxyStoreObject, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD_NOSUPPORT(TRACE_MAPI, ECMsgStore, ProxyStoreObject, PlaceHolder1, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ProxyStoreObject, UnwrapNoRef, (LPVOID *, ppvObject))
DEF_HRMETHOD_NOSUPPORT(TRACE_MAPI, ECMsgStore, ProxyStoreObject, PlaceHolder2, (void))

// IMsgStoreProxy interface
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, Release, (void))

HRESULT ECMsgStore::xMsgStoreProxy::QueryInterface(REFIID refiid, void **lppInterface) {
	TRACE_MAPI(TRACE_ENTRY, "IMsgStoreProxy::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECMsgStore, MsgStoreProxy);
	HRESULT hr = pThis->QueryInterfaceProxy(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IMsgStoreProxy::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, Advise, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG, ulEventMask), (LPMAPIADVISESINK, lpAdviseSink), (ULONG *, lpulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, Unadvise, (ULONG, ulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, CompareEntryIDs, (ULONG, cbEntryID1), (LPENTRYID, lpEntryID1), (ULONG, cbEntryID2), (LPENTRYID, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, OpenEntry, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, SetReceiveFolder, (LPTSTR, lpszMessageClass), (ULONG, ulFlags), (ULONG, cbEntryID), (LPENTRYID, lpEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetReceiveFolder, (LPTSTR, lpszMessageClass), (ULONG, ulFlags), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID), (LPTSTR *, lppszExplicitClass))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetReceiveFolderTable, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, StoreLogoff, (ULONG *, lpulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, AbortSubmit, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetOutgoingQueue, (ULONG, ulFlags), (LPMAPITABLE *, lppTable))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, SetLockState, (LPMESSAGE, lpMessage), (ULONG, ulLockState))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, FinishedMsg, (ULONG, ulFlags), (ULONG, cbEntryID), (LPENTRYID, lpEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, NotifyNewMail, (LPNOTIFICATION, lpNotification))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetLastError, (HRESULT, hError), (ULONG, ulFlags), (LPMAPIERROR *, lppMapiError))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, SaveChanges, (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetProps, (const SPropTagArray *, lpPropTagArray), (ULONG, ulFlags), (ULONG *, lpcValues), (SPropValue **, lppPropArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetPropList, (ULONG, ulFlags), (LPSPropTagArray *, lppPropTagArray))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, OpenProperty, (ULONG, ulPropTag), (LPCIID, lpiid), (ULONG, ulInterfaceOptions), (ULONG, ulFlags), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, SetProps, (ULONG, cValues), (const SPropValue *, lpPropArray), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, DeleteProps, (const SPropTagArray *, lpPropTagArray), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, CopyTo, (ULONG, ciidExclude), (LPCIID, rgiidExclude), (const SPropTagArray *, lpExcludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (void *, lpDestObj), (ULONG, ulFlags), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, CopyProps, (const SPropTagArray *, lpIncludeProps), (ULONG, ulUIParam), (LPMAPIPROGRESS, lpProgress), (LPCIID, lpInterface), (void *, lpDestObj), (ULONG, ulFlags), (SPropProblemArray **, lppProblems))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetNamesFromIDs, (LPSPropTagArray *, pptaga), (LPGUID, lpguid), (ULONG, ulFlags), (ULONG *, pcNames), (LPMAPINAMEID **, pppNames))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, MsgStoreProxy, GetIDsFromNames, (ULONG, cNames), (LPMAPINAMEID *, ppNames), (ULONG, ulFlags), (LPSPropTagArray *, pptaga))

// IECMultiStoreTable interface
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECMultiStoreTable, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECMultiStoreTable, Release, (void))

HRESULT ECMsgStore::xECMultiStoreTable::QueryInterface(REFIID refiid, void **lppInterface) {
	TRACE_MAPI(TRACE_ENTRY, "IECMultiStoreTable::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECMsgStore, ECMultiStoreTable);
	HRESULT hr = pThis->QueryInterfaceProxy(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IECMultiStoreTable::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECMultiStoreTable, OpenMultiStoreTable, (LPENTRYLIST, lpMsgList), (ULONG, ulFlags), (LPMAPITABLE *, lppTable))

// IECLicense interface
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECLicense, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECLicense, Release, (void))

HRESULT ECMsgStore::xECLicense::QueryInterface(REFIID refiid, void **lppInterface) {
	TRACE_MAPI(TRACE_ENTRY, "IECLicense::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECMsgStore, ECLicense);
	HRESULT hr = pThis->QueryInterfaceProxy(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IECLicense::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECLicense, LicenseAuth, (unsigned char *, lpData), (unsigned int, ulSize), (unsigned char **, lppResponse), (unsigned int *, lpulResponseSize))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECLicense, LicenseCapa, (unsigned int, ulServiceType), (char ***, lppszData), (unsigned int *, lpulSize))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECLicense, LicenseUsers, (unsigned int, ulServiceType), (unsigned int *, lpulUsers))

// IECTestProtocol interface
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECTestProtocol, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ECTestProtocol, Release, (void))

HRESULT ECMsgStore::xECTestProtocol::QueryInterface(REFIID refiid, void **lppInterface) {
	TRACE_MAPI(TRACE_ENTRY, "IECTestProtocol::QueryInterface", "%s", DBGGUIDToString(refiid).c_str());
	METHOD_PROLOGUE_(ECMsgStore, ECTestProtocol);
	HRESULT hr = pThis->QueryInterfaceProxy(refiid, lppInterface);
	TRACE_MAPI(TRACE_RETURN, "IECTestProtocol::QueryInterface", "%s", GetMAPIErrorDescription(hr).c_str());
	return hr;
}

DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECTestProtocol, TestPerform, (const char *, cmd), (unsigned int, argc), (char **, args))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECTestProtocol, TestSet, (const char *, name), (const char *, value))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ECTestProtocol, TestGet, (const char *, name), (char **, value))

ECMSLogon::ECMSLogon(ECMsgStore *lpStore)
{
	// Note we cannot AddRef() the store. This is because the reference order is:
	// ECMsgStore -> IMAPISupport -> ECMSLogon
	// Therefore AddRef()'ing the store from here would create a circular reference 
	m_lpStore = lpStore;
}

HRESULT ECMSLogon::Create(ECMsgStore *lpStore, ECMSLogon **lppECMSLogon)
{
	auto lpLogon = new ECMSLogon(lpStore);
	return lpLogon->QueryInterface(IID_ECMSLogon, (void **)lppECMSLogon);
}

HRESULT ECMSLogon::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ECMSLogon, this);
	REGISTER_INTERFACE2(IMSLogon, &this->m_xMSLogon);
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

HRESULT ECMSLogon::OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG ulFlags, ULONG *lpulObjType, LPUNKNOWN *lppUnk)
{
	return m_lpStore->OpenEntry(cbEntryID, lpEntryID, lpInterface, ulFlags, lpulObjType, lppUnk);
}

HRESULT ECMSLogon::CompareEntryIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG ulFlags, ULONG *lpulResult)
{
	return m_lpStore->CompareEntryIDs(cbEntryID1, lpEntryID1, cbEntryID2, lpEntryID2, ulFlags, lpulResult);
}

HRESULT ECMSLogon::Advise(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulEventMask, LPMAPIADVISESINK lpAdviseSink, ULONG *lpulConnection)
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

DEF_HRMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, GetLastError, (HRESULT, hResult), (ULONG, ulFlags), (LPMAPIERROR *, lppMAPIError))
DEF_HRMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, Logoff, (ULONG *, lpulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, OpenEntry, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPUNKNOWN *, lppUnk))
DEF_HRMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, CompareEntryIDs, (ULONG, cbEntryID1), (LPENTRYID, lpEntryID1), (ULONG, cbEntryID2), (LPENTRYID, lpEntryID2), (ULONG, ulFlags), (ULONG *, lpulResult))
DEF_HRMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, Advise, (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG, ulEventMask), (LPMAPIADVISESINK, lpAdviseSink), (ULONG *, lpulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, Unadvise, (ULONG, ulConnection))
DEF_HRMETHOD1(TRACE_MAPI, ECMSLogon, MSLogon, OpenStatusEntry, (LPCIID, lpInterface), (ULONG, ulFlags), (ULONG *, lpulObjType), (LPVOID *, lppEntry))

// IExchangeManageStore6
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore6, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore6, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore6, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore6, CreateStoreEntryID, (LPTSTR, lpszMsgStoreDN), (LPTSTR, lpszMailboxDN), (ULONG, ulFlags), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore6, EntryIDFromSourceKey, (ULONG, cFolderKeySize), (BYTE *, lpFolderSourceKey), (ULONG, cMessageKeySize), (BYTE *, lpMessageSourceKey), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore6, GetRights, (ULONG, cbUserEntryID), (LPENTRYID, lpUserEntryID), (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG *, lpulRights))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore6, GetMailboxTable, (LPTSTR, lpszServerName), (LPMAPITABLE *, lppTable), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStore6, GetPublicFolderTable, (LPTSTR, lpszServerName), (LPMAPITABLE *, lppTable), (ULONG, ulFlags))

HRESULT ECMsgStore::xExchangeManageStore6::CreateStoreEntryIDEx(LPTSTR lpszMsgStoreDN, LPTSTR lpszEmail, LPTSTR lpszMailboxDN, ULONG ulFlags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	TRACE_MAPI(TRACE_ENTRY, "IExchangeManageStore6::CreateStoreEntryIDEx","msgStoreDN=%s , MailboxDN=%s , flags=0x%08X", (lpszMsgStoreDN)?(char*)lpszMsgStoreDN: "NULL", (lpszMailboxDN)?(char*)lpszMailboxDN:"NULL", ulFlags);
	METHOD_PROLOGUE_(ECMsgStore, ExchangeManageStore6);
	HRESULT hr = pThis->CreateStoreEntryID(lpszMsgStoreDN, lpszMailboxDN, ulFlags, lpcbEntryID, lppEntryID);
	TRACE_MAPI(TRACE_RETURN, "IExchangeManageStore6::CreateStoreEntryIDEx","%s, cb=%d, data=%s", GetMAPIErrorDescription(hr).c_str(), (lpcbEntryID)?*lpcbEntryID:0, (lppEntryID)?bin2hex(*lpcbEntryID, (BYTE*)*lppEntryID).c_str():"NULL");
	return hr;
}

// IExchangeManageStoreEx
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, AddRef, (void))
DEF_ULONGMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, Release, (void))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, QueryInterface, (REFIID, refiid), (void **, lppInterface))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, CreateStoreEntryID, (LPTSTR, lpszMsgStoreDN), (LPTSTR, lpszMailboxDN), (ULONG, ulFlags), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, EntryIDFromSourceKey, (ULONG, cFolderKeySize), (BYTE *, lpFolderSourceKey), (ULONG, cMessageKeySize), (BYTE *, lpMessageSourceKey), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, GetRights, (ULONG, cbUserEntryID), (LPENTRYID, lpUserEntryID), (ULONG, cbEntryID), (LPENTRYID, lpEntryID), (ULONG *, lpulRights))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, GetMailboxTable, (LPTSTR, lpszServerName), (LPMAPITABLE *, lppTable), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, GetPublicFolderTable, (LPTSTR, lpszServerName), (LPMAPITABLE *, lppTable), (ULONG, ulFlags))
DEF_HRMETHOD1(TRACE_MAPI, ECMsgStore, ExchangeManageStoreEx, CreateStoreEntryID2, (ULONG, cValues), (LPSPropValue, lpProps), (ULONG, ulFlags), (ULONG *, lpcbEntryID), (LPENTRYID *, lppEntryID))
