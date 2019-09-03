/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <chrono>
#include <memory>
#include <new>
#include <kopano/tie.hpp>
#include "ECStatsTables.h"
#include "SOAPUtils.h"
#include "ECSession.h"
#include "ECSessionManager.h"
#include "ECUserManagement.h"
#include "ECSecurity.h"
#include <mapidefs.h>
#include <mapicode.h>
#include <mapitags.h>
#include <kopano/mapiext.h>
#include <edkmdb.h>
#include <kopano/ECTags.h>
#include <kopano/stringutil.h>
#include <kopano/Util.h>
#include "StatsClient.h"
#if defined(HAVE_GPERFTOOLS_MALLOC_EXTENSION_H)
#	include <gperftools/malloc_extension_c.h>
#	define HAVE_TCMALLOC 1
#elif defined(HAVE_GOOGLE_MALLOC_EXTENSION_H)
#	include <google/malloc_extension_c.h>
#	define HAVE_TCMALLOC 1
#endif
#ifdef HAVE_MALLOC_H
#	include <malloc.h>
#endif

/*
  System stats
  - cache stats
  - license
*/

namespace KC {

ECSystemStatsTable::ECSystemStatsTable(ECSession *ses, unsigned int ulFlags,
    const ECLocale &locale) :
	ECGenericObjectTable(ses, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
	id = 0;
}

ECRESULT ECSystemStatsTable::Create(ECSession *lpSession, unsigned int ulFlags,
    const ECLocale &locale, ECGenericObjectTable **lppTable)
{
	return alloc_wrap<ECSystemStatsTable>(lpSession, ulFlags, locale).put(lppTable);
}

void server_stats::update_tcmalloc_stats()
{
#ifdef HAVE_TCMALLOC
	size_t value = 0;
	auto gnp = reinterpret_cast<decltype(MallocExtension_GetNumericProperty) *>(dlsym(NULL, "MallocExtension_GetNumericProperty"));
	if (gnp == NULL)
		return;

	gnp("generic.current_allocated_bytes", &value);
	setg("tc_allocated", "Current allocated memory by TCMalloc", value);
	value = 0;
	gnp("generic.heap_size", &value);
	setg("tc_reserved", "Bytes of system memory reserved by TCMalloc", value);
	value = 0;
	gnp("tcmalloc.pageheap_free_bytes", &value);
	setg("tc_page_map_free", "Number of bytes in free, mapped pages in page heap", value);
	value = 0;
	gnp("tcmalloc.pageheap_unmapped_bytes", &value);
	setg("tc_page_unmap_free", "Number of bytes in free, unmapped pages in page heap (released to OS)", value);
	value = 0;
	gnp("tcmalloc.max_total_thread_cache_bytes", &value);
	setg("tc_threadcache_max", "A limit to how much memory TCMalloc dedicates for small objects", value);
	value = 0;
	gnp("tcmalloc.current_total_thread_cache_bytes", &value);
	setg("tc_threadcache_cur", "Current allocated memory in bytes for thread cache", value);
#ifdef KNOB144
	char test[2048] = {0};
	auto getstat = reinterpret_cast<decltype(MallocExtension_GetStats) *>(dlsym(NULL, "MallocExtension_GetStats"));
	if (getstat != NULL) {
		getstat(test, sizeof(test));
		set("tc_stats_string", "TCMalloc memory debug data", test);
	}
#endif
#endif
}

void server_stats::fill_odm()
{
	update_tcmalloc_stats();
#ifdef HAVE_MALLINFO
	/* parallel threaded allocator */
	struct mallinfo malloc_info = mallinfo();
	setg("pt_allocated", "Current allocated memory by libc ptmalloc, in bytes", malloc_info.uordblks);
#endif

	unsigned int qlen = 0, nthr = 0, ithr = 0;
	KC::time_duration qage;

	kopano_get_server_stats(&qlen, &qage, &nthr, &ithr);
	setg("queuelen", "Current queue length", qlen);
	setg_dbl("queueage", "Age of the front queue item", dur2dbl(qage));
	setg("threads", "Number of threads running to process items", nthr);
	setg("threads_idle", "Number of idle threads", ithr);

	if (g_lpSessionManager == nullptr)
		return;
	assert(g_lpSessionManager->m_stats.get() == this);

	usercount_t uc;
	g_lpSessionManager->get_user_count_cached(&uc);
	setg("usercnt_active", "Number of active users", uc[usercount_t::ucActiveUser]);
	setg("usercnt_nonactive", "Number of total non-active objects", uc[usercount_t::ucNonActiveTotal]);
	setg("usercnt_na_user", "Number of non-active users", uc[usercount_t::ucNonActiveUser]);
	setg("usercnt_room", "Number of rooms", uc[usercount_t::ucRoom]);
	setg("usercnt_equipment", "Number of equipment", uc[usercount_t::ucEquipment]);
	setg("usercnt_contact", "Number of contacts", uc[usercount_t::ucContact]);
	g_lpSessionManager->update_extra_stats();
}

ECRESULT ECSystemStatsTable::Load()
{
	id = 0;
	g_lpSessionManager->m_stats->fill_odm();
	g_lpSessionManager->m_stats->ForEachStat(GetStatsCollectorData, this);

	// add all items to the keytable
	for (unsigned int i = 0; i < id; ++i)
		// Use MAPI_STATUS as ulObjType for the key table .. this param may be removed in the future..?
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, i, 0);
	return erSuccess;
}

void ECSystemStatsTable::GetStatsCollectorData(const std::string &name, const std::string &description, const std::string &value, void *obj)
{
	auto lpThis = static_cast<ECSystemStatsTable *>(obj);
	statstrings ss;

	ss.name = name;
	ss.description = description;
	ss.value = value;

	lpThis->m_mapStatData[lpThis->id] = ss;
	++lpThis->id;
}

ECRESULT ECSystemStatsTable::QueryRowData(ECGenericObjectTable *lpGenericThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	auto lpThis = static_cast<ECSystemStatsTable *>(lpGenericThis);
	auto lpsRowSet = soap_new_rowSet(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr  = soap_new_propValArray(soap, lpsRowSet->__size);

	// Allocate memory for all rows
	for (gsoap_size_t i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr  = soap_new_propVal(soap, lpsPropTagArray->__size);
	}

	gsoap_size_t i = 0;
	for (const auto &row : *lpRowList) {
		auto iterSD = lpThis->m_mapStatData.find(row.ulObjId);
		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			if (iterSD == lpThis->m_mapStatData.cend())
				continue;		// broken .. should never happen

			// default is error prop
			lpsRowSet->__ptr[i].__ptr[k].ulPropTag = CHANGE_PROP_TYPE(lpsPropTagArray->__ptr[k], PT_ERROR);
			lpsRowSet->__ptr[i].__ptr[k].Value.ul = KCERR_NOT_FOUND;
			lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_bin;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.bin = soap_new_xsd__base64Binary(soap);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__size = sizeof(sObjectTableKey);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr  = soap_new_unsignedByte(soap, sizeof(sObjectTableKey));
				memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr, &row, sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_DISPLAY_NAME):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = soap_strdup(soap, iterSD->second.name.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SYSTEM_DESCRIPTION):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = soap_strdup(soap, iterSD->second.description.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SYSTEM_VALUE):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = soap_strdup(soap, iterSD->second.value.c_str());
				break;
			}
		}
		++i;
	}

	*lppRowSet = lpsRowSet;
	return erSuccess;
}

/*
  Session stats
  - session object:
    - ipaddress
    - session (idle) time
    - capabilities (compression enabled)
    - session lock
    - ecsecurity object:
      - username
    - busystates
*/

ECSessionStatsTable::ECSessionStatsTable(ECSession *ses, unsigned int ulFlags,
    const ECLocale &locale) :
	ECGenericObjectTable(ses, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
	id = 0;
}

ECRESULT ECSessionStatsTable::Create(ECSession *lpSession, unsigned int ulFlags,
    const ECLocale &locale, ECGenericObjectTable **lppTable)
{
	return alloc_wrap<ECSessionStatsTable>(lpSession, ulFlags, locale).put(lppTable);
}

ECRESULT ECSessionStatsTable::Load()
{
	ECSessionManager *lpSessionManager = lpSession->GetSessionManager();
	id = 0;
	// get all data items available
	// since the table is too volatile, collect all the data at once, and not in QueryRowData
	lpSessionManager->ForEachSession(GetSessionData, this);
	// add all items to the keytable
	for (unsigned int i = 0; i < id; ++i)
		// Use MAPI_STATUS as ulObjType for the key table .. this param may be removed in the future..?
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, i, 0);
	return erSuccess;
}

void ECSessionStatsTable::GetSessionData(ECSession *lpSession, void *obj)
{
	auto lpThis = static_cast<ECSessionStatsTable *>(obj);
	sessiondata sd;

	if (lpSession == nullptr)
		// dynamic_cast failed
		return;

	sd.sessionid = lpSession->GetSessionId();
	sd.sessiongroupid = lpSession->GetSessionGroupId();
	sd.peerpid = lpSession->GetConnectingPid();
	sd.srcaddress = lpSession->GetSourceAddr();
	sd.idletime = lpSession->GetIdleTime();
	sd.capability = lpSession->GetCapabilities();
	sd.locked = lpSession->IsLocked();
	lpSession->GetClocks(&sd.dblUser, &sd.dblSystem, &sd.dblReal);
	lpSession->GetSecurity()->GetUsername(&sd.username);
	lpSession->GetBusyStates(&sd.busystates);
	lpSession->GetClientVersion(&sd.version);
	lpSession->GetClientApp(&sd.clientapp);
	sd.port = lpSession->GetClientPort();
	sd.url = lpSession->GetRequestURL();
	sd.proxyhost = lpSession->GetProxyHost();
	lpSession->GetClientApplicationVersion(&sd.client_application_version);
	lpSession->GetClientApplicationMisc(&sd.client_application_misc);
	sd.requests = lpSession->GetRequests();

	// To get up-to-date CPU stats, check each of the active threads on the session
	// for their CPU usage, and add that to the already-logged time on the session
	for (const auto &bs : sd.busystates) {
		clockid_t clock;
		struct timespec now;
		if (pthread_getcpuclockid(bs.threadid, &clock) != 0)
			continue;

		clock_gettime(clock, &now);
		sd.dblUser += timespec2dbl(now) - timespec2dbl(bs.threadstart);
		sd.dblReal += dur2dbl(decltype(bs.start)::clock::now() - bs.start);
	}
	lpThis->m_mapSessionData[lpThis->id] = sd;
	++lpThis->id;
}

ECRESULT ECSessionStatsTable::QueryRowData(ECGenericObjectTable *lpGenericThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	auto lpThis = static_cast<ECSessionStatsTable *>(lpGenericThis);
	auto lpsRowSet = soap_new_rowSet(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr  = soap_new_propValArray(soap, lpsRowSet->__size);

	// Allocate memory for all rows
	for (gsoap_size_t i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr  = soap_new_propVal(soap, lpsPropTagArray->__size);
	}

	gsoap_size_t i = 0;
	for (const auto &row : *lpRowList) {
		auto iterSD = lpThis->m_mapSessionData.find(row.ulObjId);
		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			// default is error prop
			auto &m = lpsRowSet->__ptr[i].__ptr[k];
			m.ulPropTag = CHANGE_PROP_TYPE(lpsPropTagArray->__ptr[k], PT_ERROR);
			m.Value.ul = KCERR_NOT_FOUND;
			m.__union = SOAP_UNION_propValData_ul;
			if (iterSD == lpThis->m_mapSessionData.cend())
				continue;		// broken; should never happen

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key
				m.__union = SOAP_UNION_propValData_bin;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.bin = soap_new_xsd__base64Binary(soap);
				m.Value.bin->__size = sizeof(sObjectTableKey);
				m.Value.bin->__ptr  = soap_new_unsignedByte(soap, sizeof(sObjectTableKey));
				memcpy(m.Value.bin->__ptr, &row, sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_EC_USERNAME):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, iterSD->second.username.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_ID):
				m.__union = SOAP_UNION_propValData_li;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.li = iterSD->second.sessionid;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_GROUP_ID):
				m.__union = SOAP_UNION_propValData_li;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.li = iterSD->second.sessiongroupid;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_PEER_PID):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = iterSD->second.peerpid;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CLIENT_VERSION):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, iterSD->second.version.c_str());
				break;
		        case PROP_ID(PR_EC_STATS_SESSION_CLIENT_APPLICATION):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, iterSD->second.clientapp.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_IPADDRESS):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, iterSD->second.srcaddress.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_PORT):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = iterSD->second.port;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_URL):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, iterSD->second.url.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_PROXY):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, iterSD->second.proxyhost.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_IDLETIME):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = iterSD->second.idletime;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CAPABILITY):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = iterSD->second.capability;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_LOCKED):
				m.__union = SOAP_UNION_propValData_b;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.b = iterSD->second.locked;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CPU_USER):
				m.__union = SOAP_UNION_propValData_dbl;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.dbl = iterSD->second.dblUser;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CPU_SYSTEM):
				m.__union = SOAP_UNION_propValData_dbl;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.dbl = iterSD->second.dblSystem;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CPU_REAL):
				m.__union = SOAP_UNION_propValData_dbl;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.dbl = iterSD->second.dblReal;
				break;

			case PROP_ID(PR_EC_STATS_SESSION_BUSYSTATES): {
				m.__union = SOAP_UNION_propValData_mvszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.mvszA.__size = iterSD->second.busystates.size();
				m.Value.mvszA.__ptr  = soap_new_string(soap, iterSD->second.busystates.size());

				gsoap_size_t j = 0;
				for (const auto &bs : iterSD->second.busystates)
					m.Value.mvszA.__ptr[j++] = soap_strdup(soap, bs.fname);
				break;
			}
			case PROP_ID(PR_EC_STATS_SESSION_PROCSTATES): {
				m.__union = SOAP_UNION_propValData_mvszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.mvszA.__size = iterSD->second.busystates.size();
				m.Value.mvszA.__ptr  = soap_new_string(soap, iterSD->second.busystates.size());

				gsoap_size_t j = 0;
				for (const auto &bs : iterSD->second.busystates) {
					const char *szState = "";
					if (bs.state == SESSION_STATE_PROCESSING)
						szState = "P";
					else if (bs.state == SESSION_STATE_SENDING)
						szState = "S";
					else
						assert(false);
					m.Value.mvszA.__ptr[j++] = soap_strdup(soap, szState);
				}
				break;
			}
			case PROP_ID(PR_EC_STATS_SESSION_REQUESTS):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = iterSD->second.requests;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, iterSD->second.client_application_version.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, iterSD->second.client_application_misc.c_str());
				break;
			}
		}
		++i;
	}

	*lppRowSet = lpsRowSet;
	return erSuccess;
}

/*
  User stats
*/
ECUserStatsTable::ECUserStatsTable(ECSession *ses, unsigned int ulFlags,
    const ECLocale &locale) :
	ECGenericObjectTable(ses, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
}

ECRESULT ECUserStatsTable::Create(ECSession *lpSession, unsigned int ulFlags,
    const ECLocale &locale, ECGenericObjectTable **lppTable)
{
	return alloc_wrap<ECUserStatsTable>(lpSession, ulFlags, locale).put(lppTable);
}

ECRESULT ECUserStatsTable::Load()
{
	std::list<localobjectdetails_t> companies;

	// load all active and non-active users
	// FIXME: group/company quota already possible?

	// get company list if hosted and is sysadmin
	auto er = lpSession->GetSecurity()->GetViewableCompanyIds(0, companies);
	if (er != erSuccess)
		return er;
	if (companies.empty())
		return LoadCompanyUsers(0);
	for (const auto &com : companies) {
		er = LoadCompanyUsers(com.ulId);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

ECRESULT ECUserStatsTable::LoadCompanyUsers(ULONG ulCompanyId)
{
	std::list<localobjectdetails_t> objs;
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	auto sesmgr = lpSession->GetSessionManager();
	bool bDistrib = sesmgr->IsDistributedSupported();
	auto server = sesmgr->GetConfig()->GetSetting("server_name");
	std::list<unsigned int> lstObjId;

	auto er = lpUserManagement->GetCompanyObjectListAndSync(OBJECTCLASS_USER,
	          ulCompanyId, lpsRestrict, objs, 0);
	if (FAILED(er))
		return er;
	for (const auto &obj : objs) {
		// we only return users present on this server
		if (bDistrib && obj.GetPropString(OB_PROP_S_SERVERNAME).compare(server) != 0)
			continue;
		lstObjId.emplace_back(obj.ulId);
	}

	UpdateRows(ECKeyTable::TABLE_ROW_ADD, &lstObjId, 0, false);
	return erSuccess;
}

ECRESULT ECUserStatsTable::QueryRowData(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	ECDatabase *lpDatabase = NULL;
	long long llStoreSize = 0;
	objectdetails_t objectDetails, companyDetails;
	quotadetails_t quotaDetails;
	DB_RESULT lpDBResult;

	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	auto lpsRowSet = soap_new_rowSet(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr  = soap_new_propValArray(soap, lpsRowSet->__size);

	// Allocate memory for all rows
	for (gsoap_size_t i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr  = soap_new_propVal(soap, lpsPropTagArray->__size);
	}

	gsoap_size_t i = 0;
	for (const auto &row : *lpRowList) {
		bool bNoObjectDetails = false, bNoQuotaDetails = false;

		if (lpUserManagement->GetObjectDetails(row.ulObjId, &objectDetails) != erSuccess)
			// user gone missing since first list, all props should be set to ignore
			bNoObjectDetails = bNoQuotaDetails = true;
		else if (lpSession->GetSecurity()->GetUserQuota(row.ulObjId, false, &quotaDetails) != erSuccess)
			// user gone missing since last call, all quota props should be set to ignore
			bNoQuotaDetails = true;

		if (lpSession->GetSecurity()->GetUserSize(row.ulObjId, &llStoreSize) != erSuccess)
			llStoreSize = 0;

		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			// default is error prop
			auto &m = lpsRowSet->__ptr[i].__ptr[k];
			m.ulPropTag = CHANGE_PROP_TYPE(lpsPropTagArray->__ptr[k], PT_ERROR);
			m.Value.ul = KCERR_NOT_FOUND;
			m.__union = SOAP_UNION_propValData_ul;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key
				m.__union = SOAP_UNION_propValData_bin;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.bin = soap_new_xsd__base64Binary(soap);
				m.Value.bin->__size = sizeof(sObjectTableKey);
				m.Value.bin->__ptr  = soap_new_unsignedByte(soap, sizeof(sObjectTableKey));
				memcpy(m.Value.bin->__ptr, &row, sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_EC_COMPANY_NAME):
				if (bNoObjectDetails || lpUserManagement->GetObjectDetails(objectDetails.GetPropInt(OB_PROP_I_COMPANYID), &companyDetails) != erSuccess)
					break;
				// do we have a default copy function??
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, companyDetails.GetPropString(OB_PROP_S_FULLNAME).c_str());
				break;
			case PROP_ID(PR_EC_USERNAME):
				if (bNoObjectDetails)
					break;
				// do we have a default copy function??
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, objectDetails.GetPropString(OB_PROP_S_LOGIN).c_str());
				break;
			case PROP_ID(PR_DISPLAY_NAME):
				if (bNoObjectDetails)
					break;
				// do we have a default copy function??
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, objectDetails.GetPropString(OB_PROP_S_FULLNAME).c_str());
				break;
			case PROP_ID(PR_SMTP_ADDRESS):
				if (bNoObjectDetails)
					break;
				// do we have a default copy function??
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, objectDetails.GetPropString(OB_PROP_S_EMAIL).c_str());
				break;
			case PROP_ID(PR_EC_NONACTIVE):
				m.__union = SOAP_UNION_propValData_b;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.b =
					(OBJECTCLASS_TYPE(objectDetails.GetClass()) == OBJECTTYPE_MAILUSER) &&
					(objectDetails.GetClass() != ACTIVE_USER);
				break;
			case PROP_ID(PR_EC_ADMINISTRATOR):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = objectDetails.GetPropInt(OB_PROP_I_ADMINLEVEL);
				break;
			case PROP_ID(PR_EC_HOMESERVER_NAME):
				// should always be this servername, see ::Load()
				if (bNoObjectDetails || !lpSession->GetSessionManager()->IsDistributedSupported())
					break;
				// do we have a default copy function??
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, objectDetails.GetPropString(OB_PROP_S_SERVERNAME).c_str());
				break;
			case PROP_ID(PR_MESSAGE_SIZE_EXTENDED):
				if (llStoreSize <= 0)
					break;
				m.__union = SOAP_UNION_propValData_li;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.li = llStoreSize;
				break;
			case PROP_ID(PR_QUOTA_WARNING_THRESHOLD):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = quotaDetails.llWarnSize / 1024;
				break;
			case PROP_ID(PR_QUOTA_SEND_THRESHOLD):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = quotaDetails.llSoftSize / 1024;
				break;
			case PROP_ID(PR_QUOTA_RECEIVE_THRESHOLD):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = quotaDetails.llHardSize / 1024;
				break;
			case PROP_ID(PR_LAST_LOGON_TIME):
			case PROP_ID(PR_LAST_LOGOFF_TIME):
			case PROP_ID(PR_EC_QUOTA_MAIL_TIME): {
				// last mail time ... property in the store of the user...
				auto strQuery = "SELECT val_hi, val_lo FROM properties JOIN hierarchy ON properties.hierarchyid=hierarchy.id JOIN stores ON hierarchy.id=stores.hierarchy_id WHERE stores.user_id=" +
				           stringify(row.ulObjId) + " AND properties.tag=" +
				           stringify(PROP_ID(lpsPropTagArray->__ptr[k])) +
				           " AND properties.type=" +
				           stringify(PROP_TYPE(lpsPropTagArray->__ptr[k]));
				er = lpDatabase->DoSelect(strQuery, &lpDBResult);
				if (er != erSuccess) {
					// database error .. ignore for now
					er = erSuccess;
					break;
				}
				if (lpDBResult.get_num_rows() == 0)
					break;
				auto lpDBRow = lpDBResult.fetch_row();
				m.__union = SOAP_UNION_propValData_hilo;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.hilo = soap_new_hiloLong(soap);
				m.Value.hilo->hi = atoi(lpDBRow[0]);
				m.Value.hilo->lo = atoi(lpDBRow[1]);
				break;
			}
			case PROP_ID(PR_EC_OUTOFOFFICE): {
				auto strQuery = "SELECT val_ulong FROM properties JOIN stores ON properties.hierarchyid=stores.hierarchy_id WHERE stores.user_id=" +
				           stringify(row.ulObjId) +
				           " AND properties.tag=" +
				           stringify(PROP_ID(PR_EC_OUTOFOFFICE)) +
				           " AND properties.type=" +
				           stringify(PROP_TYPE(PR_EC_OUTOFOFFICE));
				er = lpDatabase->DoSelect(strQuery, &lpDBResult);
				if (er != erSuccess) {
					// database error .. ignore for now
					er = erSuccess;
					break;
				}
				m.__union = SOAP_UNION_propValData_b;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				if (lpDBResult.get_num_rows() > 0) {
					auto lpDBRow = lpDBResult.fetch_row();
					m.Value.b = atoi(lpDBRow[0]);
				} else {
					m.Value.b = 0;
				}
				break;
			}
			};
		}
		++i;
	}

	*lppRowSet = lpsRowSet;
	return er;
}

ECCompanyStatsTable::ECCompanyStatsTable(ECSession *ses, unsigned int ulFlags,
    const ECLocale &locale) :
	ECGenericObjectTable(ses, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
	m_lpObjectData = this;
}

ECRESULT ECCompanyStatsTable::Create(ECSession *lpSession, unsigned int ulFlags,
    const ECLocale &locale, ECGenericObjectTable **lppTable)
{
	return alloc_wrap<ECCompanyStatsTable>(lpSession, ulFlags, locale).put(lppTable);
}

ECRESULT ECCompanyStatsTable::Load()
{
	std::list<localobjectdetails_t> companies;

	auto er = lpSession->GetSecurity()->GetViewableCompanyIds(0, companies);
	if (er != erSuccess)
		return er;
	for (const auto &com : companies)
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, com.ulId, 0);
	return erSuccess;
}

ECRESULT ECCompanyStatsTable::QueryRowData(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	ECDatabase *lpDatabase = NULL;
	long long llStoreSize = 0;
	objectdetails_t companyDetails;
	quotadetails_t quotaDetails;
	DB_RESULT lpDBResult;

	auto er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	auto lpsRowSet = soap_new_rowSet(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr  = soap_new_propValArray(soap, lpsRowSet->__size);

	// Allocate memory for all rows
	for (gsoap_size_t i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr  = soap_new_propVal(soap, lpsPropTagArray->__size);
	}

	gsoap_size_t i = 0;
	for (const auto &row : *lpRowList) {
		bool bNoCompanyDetails = false, bNoQuotaDetails = false;

		if (lpUserManagement->GetObjectDetails(row.ulObjId, &companyDetails) != erSuccess)
			bNoCompanyDetails = true;
		else if (lpUserManagement->GetQuotaDetailsAndSync(row.ulObjId, &quotaDetails) != erSuccess)
			// company gone missing since last call, all quota props should be set to ignore
			bNoQuotaDetails = true;

		if (lpSession->GetSecurity()->GetUserSize(row.ulObjId, &llStoreSize) != erSuccess)
			llStoreSize = 0;

		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			// default is error prop
			auto &m = lpsRowSet->__ptr[i].__ptr[k];
			m.ulPropTag = CHANGE_PROP_TYPE(lpsPropTagArray->__ptr[k], PT_ERROR);
			m.Value.ul = KCERR_NOT_FOUND;
			m.__union = SOAP_UNION_propValData_ul;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key
				m.__union = SOAP_UNION_propValData_bin;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.bin = soap_new_xsd__base64Binary(soap);
				m.Value.bin->__size = sizeof(sObjectTableKey);
				m.Value.bin->__ptr  = soap_new_unsignedByte(soap, sizeof(sObjectTableKey));
				memcpy(m.Value.bin->__ptr, &row, sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_EC_COMPANY_NAME): {
				if (bNoCompanyDetails)
					break;
				auto strData = companyDetails.GetPropString(OB_PROP_S_FULLNAME);
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, strData.data());
				break;
			}
			case PROP_ID(PR_EC_COMPANY_ADMIN): {
				auto strData = companyDetails.GetPropObject(OB_PROP_O_SYSADMIN).id;
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, strData.data());
				break;
			}
			case PROP_ID(PR_MESSAGE_SIZE_EXTENDED):
				if (llStoreSize <= 0)
					break;
				m.__union = SOAP_UNION_propValData_li;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.li = llStoreSize;
				break;

			case PROP_ID(PR_QUOTA_WARNING_THRESHOLD):
				if (bNoQuotaDetails)
					break;
				m.__union = SOAP_UNION_propValData_li;
				m.ulPropTag = lpsPropTagArray->__ptr[k]; // set type to I8 ?
				m.Value.li = quotaDetails.llWarnSize;
				break;
			case PROP_ID(PR_QUOTA_SEND_THRESHOLD):
				if (bNoQuotaDetails)
					break;
				m.__union = SOAP_UNION_propValData_li;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.li = quotaDetails.llSoftSize;
				break;
			case PROP_ID(PR_QUOTA_RECEIVE_THRESHOLD):
				if (bNoQuotaDetails)
					break;
				m.__union = SOAP_UNION_propValData_li;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.li = quotaDetails.llHardSize;
				break;
			case PROP_ID(PR_EC_QUOTA_MAIL_TIME): {
				if (bNoQuotaDetails)
					break;
				// last mail time ... property in the store of the company (=public)...
				auto strQuery = "SELECT val_hi, val_lo FROM properties JOIN hierarchy ON properties.hierarchyid=hierarchy.id JOIN stores ON hierarchy.id=stores.hierarchy_id WHERE stores.user_id=" +
				           stringify(row.ulObjId) +
				           " AND properties.tag=" +
				           stringify(PROP_ID(PR_EC_QUOTA_MAIL_TIME)) +
				           " AND properties.type=" +
				           stringify(PROP_TYPE(PR_EC_QUOTA_MAIL_TIME));
				er = lpDatabase->DoSelect(strQuery, &lpDBResult);
				if (er != erSuccess) {
					// database error .. ignore for now
					er = erSuccess;
					break;
				}
				if (lpDBResult.get_num_rows() == 0)
					break;
				auto lpDBRow = lpDBResult.fetch_row();
				m.__union = SOAP_UNION_propValData_hilo;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.hilo = soap_new_hiloLong(soap);
				m.Value.hilo->hi = atoi(lpDBRow[0]);
				m.Value.hilo->lo = atoi(lpDBRow[1]);
				break;
			}
			};
		}
		++i;
	}

	*lppRowSet = lpsRowSet;
	return er;
}

ECServerStatsTable::ECServerStatsTable(ECSession *ses, unsigned int ulFlags,
    const ECLocale &locale) :
	ECGenericObjectTable(ses, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
	m_lpObjectData = this;
}

ECRESULT ECServerStatsTable::Create(ECSession *lpSession, unsigned int ulFlags,
    const ECLocale &locale, ECGenericObjectTable **lppTable)
{
	return alloc_wrap<ECServerStatsTable>(lpSession, ulFlags, locale).put(lppTable);
}

ECRESULT ECServerStatsTable::Load()
{
	serverlist_t servers;
	unsigned int i = 1;

	auto er = lpSession->GetUserManagement()->GetServerList(&servers);
	if (er != erSuccess)
		return er;

	// Assign an ID to each server which is usable from QueryRowData
	for (const auto &srv : servers) {
		m_mapServers.emplace(i, srv);
		// For each server, add a row in the table
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, i, 0);
		++i;
	}
	return erSuccess;
}

ECRESULT ECServerStatsTable::QueryRowData(ECGenericObjectTable *lpThis,
    struct soap *soap, ECSession *lpSession, const ECObjectTableList *lpRowList,
    const struct propTagArray *lpsPropTagArray, const void *lpObjectData,
    struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	struct rowSet *lpsRowSet = NULL;
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	serverdetails_t details;
	auto lpStats = static_cast<ECServerStatsTable *>(lpThis);

	lpsRowSet = soap_new_rowSet(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr  = soap_new_propValArray(soap, lpsRowSet->__size);

	// Allocate memory for all rows
	for (gsoap_size_t i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr  = soap_new_propVal(soap, lpsPropTagArray->__size);
	}

	gsoap_size_t i = 0;
	for (const auto &row : *lpRowList) {
		if (lpUserManagement->GetServerDetails(lpStats->m_mapServers[row.ulObjId], &details) != erSuccess)
			details = serverdetails_t();

		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			// default is error prop
			auto &m = lpsRowSet->__ptr[i].__ptr[k];
			m.ulPropTag = CHANGE_PROP_TYPE(lpsPropTagArray->__ptr[k], PT_ERROR);
			m.Value.ul = KCERR_NOT_FOUND;
			m.__union = SOAP_UNION_propValData_ul;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key
				m.__union = SOAP_UNION_propValData_bin;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.bin = soap_new_xsd__base64Binary(soap);
				m.Value.bin->__size = sizeof(sObjectTableKey);
				m.Value.bin->__ptr  = soap_new_unsignedByte(soap, sizeof(sObjectTableKey));
				memcpy(m.Value.bin->__ptr, &row, sizeof(sObjectTableKey));
				break;
			case PROP_ID(PR_EC_STATS_SERVER_NAME):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, lpStats->m_mapServers[row.ulObjId].c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_HTTPPORT):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = details.GetHttpPort();
				break;
			case PROP_ID(PR_EC_STATS_SERVER_SSLPORT):
				m.__union = SOAP_UNION_propValData_ul;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.ul = details.GetSslPort();
				break;
			case PROP_ID(PR_EC_STATS_SERVER_HOST):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, details.GetHostAddress().c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_PROXYURL):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, details.GetProxyPath().c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_HTTPURL):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, details.GetHttpPath().c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_HTTPSURL):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, details.GetSslPath().c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_FILEURL):
				m.__union = SOAP_UNION_propValData_lpszA;
				m.ulPropTag = lpsPropTagArray->__ptr[k];
				m.Value.lpszA = soap_strdup(soap, details.GetFilePath().c_str());
				break;
			};
		}
		++i;
	}

	*lppRowSet = lpsRowSet;
	return erSuccess;
}

} /* namespace */
