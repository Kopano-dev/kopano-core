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

#include "ECStatsCollector.h"

#if defined(HAVE_GPERFTOOLS_MALLOC_EXTENSION_H)
#	include <gperftools/malloc_extension_c.h>
#elif defined(HAVE_GOOGLE_MALLOC_EXTENSION_H)
#	include <google/malloc_extension_c.h>
#endif
#ifdef HAVE_MALLOC_H
#	include <malloc.h>
#endif

/*
  System stats
  - cache stats
  - license
*/

ECSystemStatsTable::ECSystemStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale) : ECGenericObjectTable(lpSession, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
	id = 0;
}

ECRESULT ECSystemStatsTable::Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECSystemStatsTable **lppTable)
{
	*lppTable = new ECSystemStatsTable(lpSession, ulFlags, locale);

	(*lppTable)->AddRef();

	return erSuccess;
}

ECRESULT ECSystemStatsTable::Load()
{
	ECRESULT er = erSuccess;
	sObjectTableKey sRowItem;
	unsigned int i;
	unsigned int ulQueueLen = 0;
	double dblAge = 0;
	unsigned int ulThreads = 0;
	unsigned int ulIdleThreads = 0;
	unsigned int ulLicensedUsers = 0;
	usercount_t userCount;

	id = 0;
	g_lpStatsCollector->ForEachString(this->GetStatsCollectorData, (void*)this);
	g_lpStatsCollector->ForEachStat(this->GetStatsCollectorData, (void*)this);
	lpSession->GetSessionManager()->GetCacheManager()->ForEachCacheItem(this->GetStatsCollectorData, (void*)this);

	// Receive session stats
	lpSession->GetSessionManager()->GetStats(this->GetStatsCollectorData, (void*)this);

	kopano_get_server_stats(&ulQueueLen, &dblAge, &ulThreads, &ulIdleThreads);

	GetStatsCollectorData("queuelen", "Current queue length", stringify(ulQueueLen), this);
	GetStatsCollectorData("queueage", "Age of the front queue item", stringify_double(dblAge,3), this);
	GetStatsCollectorData("threads", "Number of threads running to process items", stringify(ulThreads), this);
	GetStatsCollectorData("threads_idle", "Number of idle threads", stringify(ulIdleThreads), this);

	lpSession->GetSessionManager()->GetLicensedUsers(0 /*SERVICE_TYPE_ZCP*/, &ulLicensedUsers);
	lpSession->GetUserManagement()->GetCachedUserCount(&userCount);

	GetStatsCollectorData("usercnt_licensed", "Number of allowed users", stringify(ulLicensedUsers), this);
	GetStatsCollectorData("usercnt_active", "Number of active users", stringify(userCount[usercount_t::ucActiveUser]), this);
	GetStatsCollectorData("usercnt_nonactive", "Number of total non-active objects", stringify(userCount[usercount_t::ucNonActiveTotal]), this);
	GetStatsCollectorData("usercnt_na_user", "Number of non-active users", stringify(userCount[usercount_t::ucNonActiveUser]), this);
	GetStatsCollectorData("usercnt_room", "Number of rooms", stringify(userCount[usercount_t::ucRoom]), this);
	GetStatsCollectorData("usercnt_equipment", "Number of equipment", stringify(userCount[usercount_t::ucEquipment]), this);
	GetStatsCollectorData("usercnt_contact", "Number of contacts", stringify(userCount[usercount_t::ucContact]), this);

	// @todo report licensed archived users and current archieved users
	//lpSession->GetSessionManager()->GetLicensedUsers(1/*SERVICE_TYPE_ARCHIVE*/, &ulLicensedArchivedUsers);
	//GetStatsCollectorData("??????????", "Number of allowed archive users", stringify(ulLicensedArchivedUsers), this);

#ifdef HAVE_TCMALLOC
	size_t value = 0;
	auto gnp = reinterpret_cast<decltype(MallocExtension_GetNumericProperty) *>(dlsym(NULL, "MallocExtension_GetNumericProperty"));
	if (gnp != NULL) {
		gnp("generic.current_allocated_bytes", &value);
		GetStatsCollectorData("tc_allocated", "Current allocated memory by TCMalloc", stringify_int64(value), this); // Bytes in use by application

		value = 0;
		gnp("generic.heap_size", &value);
		GetStatsCollectorData("tc_reserved", "Bytes of system memory reserved by TCMalloc", stringify_int64(value), this);

		value = 0;
		gnp("tcmalloc.pageheap_free_bytes", &value);
		GetStatsCollectorData("tc_page_map_free", "Number of bytes in free, mapped pages in page heap", stringify_int64(value), this); 

		value = 0;
		gnp("tcmalloc.pageheap_unmapped_bytes", &value);
		GetStatsCollectorData("tc_page_unmap_free", "Number of bytes in free, unmapped pages in page heap (released to OS)", stringify_int64(value), this);

		value = 0;
		gnp("tcmalloc.max_total_thread_cache_bytes", &value);
		GetStatsCollectorData("tc_threadcache_max", "A limit to how much memory TCMalloc dedicates for small objects", stringify_int64(value), this);

		value = 0;
		gnp("tcmalloc.current_total_thread_cache_bytes", &value);
		GetStatsCollectorData("tc_threadcache_cur", "Current allocated memory in bytes for thread cache", stringify_int64(value), this);
	}
#ifdef HAVE_MALLINFO
	/* parallel threaded allocator */
	struct mallinfo malloc_info = mallinfo();
	GetStatsCollectorData("pt_allocated", "Current allocated memory by libc ptmalloc, in bytes", stringify_int64(malloc_info.uordblks), this);
#endif

#ifdef DEBUG
	char test[2048] = {0};
	auto getstat = reinterpret_cast<decltype(MallocExtension_GetStats) *>(dlsym(NULL, "MallocExtension_GetStats"));
	if (getstat != NULL) {
		getstat(test, sizeof(test));
		GetStatsCollectorData("tc_stats_string", "TCMalloc memory debug data", test, this);
	}
#endif

#endif

	// add all items to the keytable
	for (i = 0; i < id; ++i)
		// Use MAPI_STATUS as ulObjType for the key table .. this param may be removed in the future..?
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, i, 0);
	return er;
}

void ECSystemStatsTable::GetStatsCollectorData(const std::string &name, const std::string &description, const std::string &value, void *obj)
{
	ECSystemStatsTable *lpThis = (ECSystemStatsTable*)obj;
	statstrings ss;

	ss.name = name;
	ss.description = description;
	ss.value = value;

	lpThis->m_mapStatData[lpThis->id] = ss;
	++lpThis->id;
}

ECRESULT ECSystemStatsTable::QueryRowData(ECGenericObjectTable *lpGenericThis, struct soap *soap, ECSession *lpSession, ECObjectTableList *lpRowList, struct propTagArray *lpsPropTagArray, void *lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	struct rowSet *lpsRowSet = NULL;
	ECSystemStatsTable *lpThis = (ECSystemStatsTable *)lpGenericThis;
	ECObjectTableList::const_iterator iterRowList;
	std::map<unsigned int, statstrings>::const_iterator iterSD;
	gsoap_size_t i;

	lpsRowSet = s_alloc<rowSet>(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpsRowSet->__size);
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpsRowSet->__size);

	// Allocate memory for all rows
	for (i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr = s_alloc<propVal>(soap, lpsPropTagArray->__size);
		memset(lpsRowSet->__ptr[i].__ptr, 0, sizeof(propVal) * lpsPropTagArray->__size);
	}

	for (i = 0, iterRowList = lpRowList->begin();
	     iterRowList != lpRowList->end(); ++iterRowList, ++i)
	{
		iterSD = lpThis->m_mapStatData.find(iterRowList->ulObjId);
		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			if (iterSD == lpThis->m_mapStatData.end())
				continue;		// broken .. should never happen

			// default is error prop
			lpsRowSet->__ptr[i].__ptr[k].ulPropTag = PROP_TAG(PROP_TYPE(PT_ERROR), PROP_ID(lpsPropTagArray->__ptr[k]));
			lpsRowSet->__ptr[i].__ptr[k].Value.ul = KCERR_NOT_FOUND;
			lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key 
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_bin;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.bin = s_alloc<xsd__base64Binary>(soap);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__size = sizeof(sObjectTableKey);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(sObjectTableKey));
				memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr, (void*)&(*iterRowList), sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_DISPLAY_NAME):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, iterSD->second.name.length()+1);
				strcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, iterSD->second.name.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SYSTEM_DESCRIPTION):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, iterSD->second.description.length()+1);
				strcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, iterSD->second.description.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SYSTEM_VALUE):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, iterSD->second.value.length()+1);
				strcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, iterSD->second.value.c_str());
				break;
			}

		}
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

ECSessionStatsTable::ECSessionStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale) : ECGenericObjectTable(lpSession, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
	id = 0;
}

ECRESULT ECSessionStatsTable::Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECSessionStatsTable **lppTable)
{
	*lppTable = new ECSessionStatsTable(lpSession, ulFlags, locale);

	(*lppTable)->AddRef();

	return erSuccess;
}

ECRESULT ECSessionStatsTable::Load()
{
	ECRESULT er = erSuccess;
	sObjectTableKey sRowItem;
	ECSessionManager *lpSessionManager = lpSession->GetSessionManager();
	unsigned int i;

	id = 0;
	// get all data items available
	// since the table is too volatile, collect all the data at once, and not in QueryRowData
	lpSessionManager->ForEachSession(this->GetSessionData, (void*)this);

	// add all items to the keytable
	for (i = 0; i < id; ++i)
		// Use MAPI_STATUS as ulObjType for the key table .. this param may be removed in the future..?
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, i, 0);
	return er;
}

void ECSessionStatsTable::GetSessionData(ECSession *lpSession, void *obj)
{
	ECSessionStatsTable *lpThis = (ECSessionStatsTable*)obj;
	sessiondata sd;
	std::list<BUSYSTATE>::const_iterator iterBS;

	if (!lpSession) {
		// dynamic_cast failed
		return;
	}

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
	lpSession->GetClientPort(&sd.port);
	lpSession->GetRequestURL(&sd.url);
	lpSession->GetProxyHost(&sd.proxyhost);
	lpSession->GetClientApplicationVersion(&sd.client_application_version);
	lpSession->GetClientApplicationMisc(&sd.client_application_misc);
	sd.requests = lpSession->GetRequests();

	// To get up-to-date CPU stats, check each of the active threads on the session
	// for their CPU usage, and add that to the already-logged time on the session
	for (iterBS = sd.busystates.begin(); iterBS != sd.busystates.end(); ++iterBS) {
		clockid_t clock;
		struct timespec now;
		
		if(pthread_getcpuclockid(iterBS->threadid, &clock) != 0)
			continue;
			
		clock_gettime(clock, &now);
		
		sd.dblUser += timespec2dbl(now) - timespec2dbl(iterBS->threadstart);
		sd.dblReal += GetTimeOfDay() - iterBS->start;
	}
	lpThis->m_mapSessionData[lpThis->id] = sd;
	++lpThis->id;
}

ECRESULT ECSessionStatsTable::QueryRowData(ECGenericObjectTable *lpGenericThis, struct soap *soap, ECSession *lpSession, ECObjectTableList *lpRowList, struct propTagArray *lpsPropTagArray, void *lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	struct rowSet *lpsRowSet = NULL;
	ECObjectTableList::const_iterator iterRowList;
	ECSessionStatsTable *lpThis = (ECSessionStatsTable *)lpGenericThis;
	gsoap_size_t i;
	std::string strTemp;
	std::map<unsigned int, sessiondata>::const_iterator iterSD;
	std::list<BUSYSTATE>::const_iterator iterBS;

	lpsRowSet = s_alloc<rowSet>(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpsRowSet->__size);
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpsRowSet->__size);

	// Allocate memory for all rows
	for (i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr = s_alloc<propVal>(soap, lpsPropTagArray->__size);
		memset(lpsRowSet->__ptr[i].__ptr, 0, sizeof(propVal) * lpsPropTagArray->__size);
	}

	for (i = 0, iterRowList = lpRowList->begin();
	     iterRowList != lpRowList->end(); ++iterRowList, ++i)
	{
		iterSD = lpThis->m_mapSessionData.find(iterRowList->ulObjId);
		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			gsoap_size_t j;
			// default is error prop
			lpsRowSet->__ptr[i].__ptr[k].ulPropTag = PROP_TAG(PROP_TYPE(PT_ERROR), PROP_ID(lpsPropTagArray->__ptr[k]));
			lpsRowSet->__ptr[i].__ptr[k].Value.ul = KCERR_NOT_FOUND;
			lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;

			if (iterSD == lpThis->m_mapSessionData.end())
				continue;		// broken; should never happen

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key 
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_bin;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.bin = s_alloc<xsd__base64Binary>(soap);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__size = sizeof(sObjectTableKey);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(sObjectTableKey));
				memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr, &(*iterRowList), sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_EC_USERNAME):
				strTemp = iterSD->second.username;
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, strTemp.length()+1);
				strcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, strTemp.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_ID):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_li;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.li = iterSD->second.sessionid;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_GROUP_ID):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_li;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.li = iterSD->second.sessiongroupid;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_PEER_PID):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = iterSD->second.peerpid;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CLIENT_VERSION):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, iterSD->second.version.c_str());
				break;
		        case PROP_ID(PR_EC_STATS_SESSION_CLIENT_APPLICATION):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, iterSD->second.clientapp.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_IPADDRESS):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, iterSD->second.srcaddress.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_PORT):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = iterSD->second.port;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_URL):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, iterSD->second.url.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_PROXY):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, iterSD->second.proxyhost.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_IDLETIME):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = iterSD->second.idletime;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CAPABILITY):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = iterSD->second.capability;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_LOCKED):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_b;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.b = iterSD->second.locked;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CPU_USER):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_dbl;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.dbl = iterSD->second.dblUser;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CPU_SYSTEM):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_dbl;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.dbl = iterSD->second.dblSystem;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CPU_REAL):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_dbl;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.dbl = iterSD->second.dblReal;
				break;

			case PROP_ID(PR_EC_STATS_SESSION_BUSYSTATES):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_mvszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];

				lpsRowSet->__ptr[i].__ptr[k].Value.mvszA.__size = iterSD->second.busystates.size();
				lpsRowSet->__ptr[i].__ptr[k].Value.mvszA.__ptr = s_alloc<char*>(soap, iterSD->second.busystates.size());

				for (j = 0, iterBS = iterSD->second.busystates.begin();
				     iterBS != iterSD->second.busystates.end(); ++j, ++iterBS)
					lpsRowSet->__ptr[i].__ptr[k].Value.mvszA.__ptr[j] = s_strcpy(soap, iterBS->fname);
				break;
			case PROP_ID(PR_EC_STATS_SESSION_PROCSTATES):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_mvszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];

				lpsRowSet->__ptr[i].__ptr[k].Value.mvszA.__size = iterSD->second.busystates.size();
				lpsRowSet->__ptr[i].__ptr[k].Value.mvszA.__ptr = s_alloc<char*>(soap, iterSD->second.busystates.size());

				for (j = 0, iterBS = iterSD->second.busystates.begin();
				     iterBS != iterSD->second.busystates.end(); ++j, ++iterBS) {
					const char *szState = "";
					if(iterBS->state == SESSION_STATE_PROCESSING)
						szState = "P";
					else if(iterBS->state == SESSION_STATE_SENDING)
						szState = "S";
					else ASSERT(false);
					
					lpsRowSet->__ptr[i].__ptr[k].Value.mvszA.__ptr[j] = s_strcpy(soap, szState);
				}
				break;
			case PROP_ID(PR_EC_STATS_SESSION_REQUESTS):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = iterSD->second.requests;
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CLIENT_APPLICATION_VERSION):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, iterSD->second.client_application_version.c_str());
				break;
			case PROP_ID(PR_EC_STATS_SESSION_CLIENT_APPLICATION_MISC):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, iterSD->second.client_application_misc.c_str());
				break;
			}
		}
	}

	*lppRowSet = lpsRowSet;
	return erSuccess;
}

/*
  User stats
*/

ECUserStatsTable::ECUserStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale) : ECGenericObjectTable(lpSession, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
}

ECRESULT ECUserStatsTable::Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECUserStatsTable **lppTable)
{
	*lppTable = new ECUserStatsTable(lpSession, ulFlags, locale);

	(*lppTable)->AddRef();

	return erSuccess;
}

ECRESULT ECUserStatsTable::Load()
{
	ECRESULT er = erSuccess;
	std::list<localobjectdetails_t> *lpCompanies = NULL;
	std::list<localobjectdetails_t>::const_iterator iCompanies;

	// load all active and non-active users
	// FIXME: group/company quota already possible?

	// get company list if hosted and is sysadmin
	er = lpSession->GetSecurity()->GetViewableCompanyIds(0, &lpCompanies);
	if (er != erSuccess)
		goto exit;

	if (lpCompanies->empty()) {
		er = LoadCompanyUsers(0);
		if (er != erSuccess)
			goto exit;
	} else {
		for (iCompanies = lpCompanies->begin();
		     iCompanies != lpCompanies->end(); ++iCompanies) {
			er = LoadCompanyUsers(iCompanies->ulId);
			if (er != erSuccess)
				goto exit;
		}
	}

exit:
	delete lpCompanies;
	return er;
}

ECRESULT ECUserStatsTable::LoadCompanyUsers(ULONG ulCompanyId)
{
	ECRESULT er = erSuccess;
	std::list<localobjectdetails_t> *lpObjects = NULL;
	sObjectTableKey sRowItem;
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	std::list<localobjectdetails_t>::const_iterator iObjects;
	bool bDistrib = lpSession->GetSessionManager()->IsDistributedSupported();
	const char* server = lpSession->GetSessionManager()->GetConfig()->GetSetting("server_name");
	std::list<unsigned int> lstObjId;

	er = lpUserManagement->GetCompanyObjectListAndSync(OBJECTCLASS_USER, ulCompanyId, &lpObjects, 0);
	if (FAILED(er))
		goto exit;
	er = erSuccess;

	for (iObjects = lpObjects->begin(); iObjects != lpObjects->end(); ++iObjects) {
		// we only return users present on this server
		if (bDistrib && iObjects->GetPropString(OB_PROP_S_SERVERNAME).compare(server) != 0)
			continue;

		lstObjId.push_back(iObjects->ulId);
	}

	UpdateRows(ECKeyTable::TABLE_ROW_ADD, &lstObjId, 0, false);

exit:
	delete lpObjects;
	return er;
}

ECRESULT ECUserStatsTable::QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList *lpRowList, struct propTagArray *lpsPropTagArray, void *lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	ECRESULT er;
	gsoap_size_t i;
	struct rowSet *lpsRowSet = NULL;
	ECObjectTableList::const_iterator iterRowList;
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	ECDatabase *lpDatabase = NULL;
	long long llStoreSize = 0;
	objectdetails_t objectDetails;
	objectdetails_t companyDetails;
	quotadetails_t quotaDetails;
	bool bNoObjectDetails = false;
	bool bNoQuotaDetails = false;
	std::string strData;
	DB_ROW lpDBRow = NULL;
	DB_RESULT lpDBResult = NULL;
	std::string strQuery;

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	lpsRowSet = s_alloc<rowSet>(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpsRowSet->__size);
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpsRowSet->__size);

	// Allocate memory for all rows
	for (i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr = s_alloc<propVal>(soap, lpsPropTagArray->__size);
		memset(lpsRowSet->__ptr[i].__ptr, 0, sizeof(propVal) * lpsPropTagArray->__size);
	}

	for (i = 0, iterRowList = lpRowList->begin();
	     iterRowList != lpRowList->end(); ++iterRowList, ++i)
	{
		bNoObjectDetails = bNoQuotaDetails = false;

		if (lpUserManagement->GetObjectDetails(iterRowList->ulObjId, &objectDetails) != erSuccess)
			// user gone missing since first list, all props should be set to ignore
			bNoObjectDetails = bNoQuotaDetails = true;
		else if (lpSession->GetSecurity()->GetUserQuota(iterRowList->ulObjId, false, &quotaDetails) != erSuccess)
			// user gone missing since last call, all quota props should be set to ignore
			bNoQuotaDetails = true;

		if (lpSession->GetSecurity()->GetUserSize(iterRowList->ulObjId, &llStoreSize) != erSuccess)
			llStoreSize = 0;

		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			// default is error prop
			lpsRowSet->__ptr[i].__ptr[k].ulPropTag = PROP_TAG(PROP_TYPE(PT_ERROR), PROP_ID(lpsPropTagArray->__ptr[k]));
			lpsRowSet->__ptr[i].__ptr[k].Value.ul = KCERR_NOT_FOUND;
			lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key 
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_bin;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.bin = s_alloc<xsd__base64Binary>(soap);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__size = sizeof(sObjectTableKey);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(sObjectTableKey));
				memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr, &(*iterRowList), sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_EC_COMPANY_NAME):
				if (!bNoObjectDetails && lpUserManagement->GetObjectDetails(objectDetails.GetPropInt(OB_PROP_I_COMPANYID), &companyDetails) == erSuccess) {
					strData = companyDetails.GetPropString(OB_PROP_S_FULLNAME);
					// do we have a default copy function??
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, strData.length()+1);
					memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, strData.data(), strData.length()+1);
				}
				break;
			case PROP_ID(PR_EC_USERNAME):
				if (!bNoObjectDetails) {
					strData = objectDetails.GetPropString(OB_PROP_S_LOGIN);
					// do we have a default copy function??
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, strData.length()+1);
					memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, strData.data(), strData.length()+1);
				}
				break;
			case PROP_ID(PR_DISPLAY_NAME):
				if (!bNoObjectDetails) {
					strData = objectDetails.GetPropString(OB_PROP_S_FULLNAME);
					// do we have a default copy function??
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, strData.length()+1);
					memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, strData.data(), strData.length()+1);
				}
				break;
			case PROP_ID(PR_SMTP_ADDRESS):
				if (!bNoObjectDetails) {
					strData = objectDetails.GetPropString(OB_PROP_S_EMAIL);
					// do we have a default copy function??
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, strData.length()+1);
					memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, strData.data(), strData.length()+1);
				}
				break;
			case PROP_ID(PR_EC_NONACTIVE):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_b;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.b =
					(OBJECTCLASS_TYPE(objectDetails.GetClass()) == OBJECTTYPE_MAILUSER) &&
					(objectDetails.GetClass() != ACTIVE_USER);
				break;
			case PROP_ID(PR_EC_ADMINISTRATOR):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = objectDetails.GetPropInt(OB_PROP_I_ADMINLEVEL);
				break;
			case PROP_ID(PR_EC_HOMESERVER_NAME):
				// should always be this servername, see ::Load()
				if (!bNoObjectDetails && lpSession->GetSessionManager()->IsDistributedSupported()) {
					strData = objectDetails.GetPropString(OB_PROP_S_SERVERNAME);
					// do we have a default copy function??
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_alloc<char>(soap, strData.length()+1);
					memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.lpszA, strData.data(), strData.length()+1);
				}
				break;
			case PROP_ID(PR_MESSAGE_SIZE_EXTENDED):
				if (llStoreSize > 0) {
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_li;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.li = llStoreSize;
				}
				break;
			case PROP_ID(PR_QUOTA_WARNING_THRESHOLD):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = quotaDetails.llWarnSize / 1024;
				break;
			case PROP_ID(PR_QUOTA_SEND_THRESHOLD):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = quotaDetails.llSoftSize / 1024;
				break;
			case PROP_ID(PR_QUOTA_RECEIVE_THRESHOLD):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = quotaDetails.llHardSize / 1024;
				break;
			case PROP_ID(PR_LAST_LOGON_TIME):
			case PROP_ID(PR_LAST_LOGOFF_TIME):
			case PROP_ID(PR_EC_QUOTA_MAIL_TIME):
				// last mail time ... property in the store of the user...
				strQuery = "SELECT val_hi, val_lo FROM properties JOIN hierarchy ON properties.hierarchyid=hierarchy.id JOIN stores ON hierarchy.id=stores.hierarchy_id WHERE stores.user_id="+stringify(iterRowList->ulObjId)+" AND properties.tag="+stringify(PROP_ID(lpsPropTagArray->__ptr[k]))+" AND properties.type="+stringify(PROP_TYPE(lpsPropTagArray->__ptr[k]));
				er = lpDatabase->DoSelect(strQuery, &lpDBResult);
				if (er != erSuccess) {
					// database error .. ignore for now
					er = erSuccess;
					break;
				}
				if (lpDatabase->GetNumRows(lpDBResult) > 0) {
					lpDBRow = lpDatabase->FetchRow(lpDBResult);
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_hilo;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.hilo = s_alloc<hiloLong>(soap);
					lpsRowSet->__ptr[i].__ptr[k].Value.hilo->hi = atoi(lpDBRow[0]);
					lpsRowSet->__ptr[i].__ptr[k].Value.hilo->lo = atoi(lpDBRow[1]);
				}
				lpDatabase->FreeResult(lpDBResult);
				lpDBResult = NULL;
				break;
			case PROP_ID(PR_EC_OUTOFOFFICE):
				strQuery = "SELECT val_ulong FROM properties JOIN stores ON properties.hierarchyid=stores.hierarchy_id WHERE stores.user_id="+stringify(iterRowList->ulObjId)+" AND properties.tag="+stringify(PROP_ID(PR_EC_OUTOFOFFICE))+" AND properties.type="+stringify(PROP_TYPE(PR_EC_OUTOFOFFICE));
				er = lpDatabase->DoSelect(strQuery, &lpDBResult);
				if (er != erSuccess) {
					// database error .. ignore for now
					er = erSuccess;
					break;
				}
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_b;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				if (lpDatabase->GetNumRows(lpDBResult) > 0) {
					lpDBRow = lpDatabase->FetchRow(lpDBResult);
					lpsRowSet->__ptr[i].__ptr[k].Value.b = atoi(lpDBRow[0]);
				} else {
					lpsRowSet->__ptr[i].__ptr[k].Value.b = 0;
				}
				lpDatabase->FreeResult(lpDBResult);
				lpDBResult = NULL;
				break;
			};
		}
	}	

	*lppRowSet = lpsRowSet;
	return er;
}

ECCompanyStatsTable::ECCompanyStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale) : ECGenericObjectTable(lpSession, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
	m_lpObjectData = this;
}

ECRESULT ECCompanyStatsTable::Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECCompanyStatsTable **lppTable)
{
	*lppTable = new ECCompanyStatsTable(lpSession, ulFlags, locale);

	(*lppTable)->AddRef();

	return erSuccess;
}

ECRESULT ECCompanyStatsTable::Load()
{
	ECRESULT er = erSuccess;
	std::list<localobjectdetails_t> *lpCompanies = NULL;
	std::list<localobjectdetails_t>::const_iterator iCompanies;
	sObjectTableKey sRowItem;

	er = lpSession->GetSecurity()->GetViewableCompanyIds(0, &lpCompanies);
	if (er != erSuccess)
		goto exit;

	for (iCompanies = lpCompanies->begin();
	     iCompanies != lpCompanies->end(); ++iCompanies)
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, iCompanies->ulId, 0);
exit:
	delete lpCompanies;
	return er;
}

ECRESULT ECCompanyStatsTable::QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	ECRESULT er;
	gsoap_size_t i;
	struct rowSet *lpsRowSet = NULL;
	ECObjectTableList::const_iterator iterRowList;
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	ECDatabase *lpDatabase = NULL;
	long long llStoreSize = 0;
	objectdetails_t companyDetails;
	quotadetails_t quotaDetails;
	bool bNoCompanyDetails = false;
	bool bNoQuotaDetails = false;
	std::string strData;
	DB_ROW lpDBRow = NULL;
	DB_RESULT lpDBResult = NULL;
	std::string strQuery;

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		return er;

	lpsRowSet = s_alloc<rowSet>(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpsRowSet->__size);
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpsRowSet->__size);

	// Allocate memory for all rows
	for (i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr = s_alloc<propVal>(soap, lpsPropTagArray->__size);
		memset(lpsRowSet->__ptr[i].__ptr, 0, sizeof(propVal) * lpsPropTagArray->__size);
	}

	for (i = 0, iterRowList = lpRowList->begin();
	     iterRowList != lpRowList->end(); ++iterRowList, ++i)
	{
		bNoCompanyDetails = bNoQuotaDetails = false;

		if (lpUserManagement->GetObjectDetails(iterRowList->ulObjId, &companyDetails) != erSuccess)
			bNoCompanyDetails = true;
		else if (lpUserManagement->GetQuotaDetailsAndSync(iterRowList->ulObjId, &quotaDetails) != erSuccess)
			// company gone missing since last call, all quota props should be set to ignore
			bNoQuotaDetails = true;

		if (lpSession->GetSecurity()->GetUserSize(iterRowList->ulObjId, &llStoreSize) != erSuccess)
			llStoreSize = 0;

		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			// default is error prop
			lpsRowSet->__ptr[i].__ptr[k].ulPropTag = PROP_TAG(PROP_TYPE(PT_ERROR), PROP_ID(lpsPropTagArray->__ptr[k]));
			lpsRowSet->__ptr[i].__ptr[k].Value.ul = KCERR_NOT_FOUND;
			lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key 
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_bin;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.bin = s_alloc<xsd__base64Binary>(soap);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__size = sizeof(sObjectTableKey);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(sObjectTableKey));
				memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr, (void*)&(*iterRowList), sizeof(sObjectTableKey));
				break;

			case PROP_ID(PR_EC_COMPANY_NAME):
				if (!bNoCompanyDetails) {
					strData = companyDetails.GetPropString(OB_PROP_S_FULLNAME);
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, strData.data());
				}
				break;
			case PROP_ID(PR_EC_COMPANY_ADMIN):
				strData = companyDetails.GetPropObject(OB_PROP_O_SYSADMIN).id;
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, strData.data());
				break;
			case PROP_ID(PR_MESSAGE_SIZE_EXTENDED):
				if (llStoreSize > 0) {
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_li;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.li = llStoreSize;
				}
				break;

			case PROP_ID(PR_QUOTA_WARNING_THRESHOLD):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_li;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k]; // set type to I8 ?
				lpsRowSet->__ptr[i].__ptr[k].Value.li = quotaDetails.llWarnSize;
				break;
			case PROP_ID(PR_QUOTA_SEND_THRESHOLD):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_li;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.li = quotaDetails.llSoftSize;
				break;
			case PROP_ID(PR_QUOTA_RECEIVE_THRESHOLD):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_li;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.li = quotaDetails.llHardSize;
				break;
			case PROP_ID(PR_EC_QUOTA_MAIL_TIME):
				// last mail time ... property in the store of the company (=public)...
				strQuery = "SELECT val_hi, val_lo FROM properties JOIN hierarchy ON properties.hierarchyid=hierarchy.id JOIN stores ON hierarchy.id=stores.hierarchy_id WHERE stores.user_id="+stringify(iterRowList->ulObjId)+" AND properties.tag="+stringify(PROP_ID(PR_EC_QUOTA_MAIL_TIME))+" AND properties.type="+stringify(PROP_TYPE(PR_EC_QUOTA_MAIL_TIME));
				er = lpDatabase->DoSelect(strQuery, &lpDBResult);
				if (er != erSuccess) {
					// database error .. ignore for now
					er = erSuccess;
					break;
				}
				if (lpDatabase->GetNumRows(lpDBResult) > 0) {
					lpDBRow = lpDatabase->FetchRow(lpDBResult);
					lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_hilo;
					lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
					lpsRowSet->__ptr[i].__ptr[k].Value.hilo = s_alloc<hiloLong>(soap);
					lpsRowSet->__ptr[i].__ptr[k].Value.hilo->hi = atoi(lpDBRow[0]);
					lpsRowSet->__ptr[i].__ptr[k].Value.hilo->lo = atoi(lpDBRow[1]);
				}
				lpDatabase->FreeResult(lpDBResult);
				lpDBResult = NULL;
				break;
			};
		}
	}	

	*lppRowSet = lpsRowSet;
	return er;
}

ECServerStatsTable::ECServerStatsTable(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale) : ECGenericObjectTable(lpSession, MAPI_STATUS, ulFlags, locale)
{
	m_lpfnQueryRowData = QueryRowData;
	m_lpObjectData = this;
}

ECRESULT ECServerStatsTable::Create(ECSession *lpSession, unsigned int ulFlags, const ECLocale &locale, ECServerStatsTable **lppTable)
{
	*lppTable = new ECServerStatsTable(lpSession, ulFlags, locale);

	(*lppTable)->AddRef();

	return erSuccess;
}

ECRESULT ECServerStatsTable::Load()
{
	ECRESULT er;
	sObjectTableKey sRowItem;
	serverlist_t servers;
	unsigned int i = 1;

	er = lpSession->GetUserManagement()->GetServerList(&servers);
	if (er != erSuccess)
		return er;
		
	// Assign an ID to each server which is usable from QueryRowData
	for (const auto &srv : servers) {
		m_mapServers.insert(std::make_pair(i, srv));
		// For each server, add a row in the table
		UpdateRow(ECKeyTable::TABLE_ROW_ADD, i, 0);
		++i;
	}
	return erSuccess;
}

ECRESULT ECServerStatsTable::QueryRowData(ECGenericObjectTable *lpThis, struct soap *soap, ECSession *lpSession, ECObjectTableList* lpRowList, struct propTagArray *lpsPropTagArray, void* lpObjectData, struct rowSet **lppRowSet, bool bCacheTableData, bool bTableLimit)
{
	gsoap_size_t i;
	struct rowSet *lpsRowSet = NULL;
	ECObjectTableList::const_iterator iterRowList;
	ECUserManagement *lpUserManagement = lpSession->GetUserManagement();
	serverdetails_t details;
	
	ECServerStatsTable *lpStats = (ECServerStatsTable *)lpThis;

	lpsRowSet = s_alloc<rowSet>(soap);
	lpsRowSet->__size = 0;
	lpsRowSet->__ptr = NULL;

	if (lpRowList->empty()) {
		*lppRowSet = lpsRowSet;
		return erSuccess;
	}

	// We return a square array with all the values
	lpsRowSet->__size = lpRowList->size();
	lpsRowSet->__ptr = s_alloc<propValArray>(soap, lpsRowSet->__size);
	memset(lpsRowSet->__ptr, 0, sizeof(propValArray) * lpsRowSet->__size);

	// Allocate memory for all rows
	for (i = 0; i < lpsRowSet->__size; ++i) {
		lpsRowSet->__ptr[i].__size = lpsPropTagArray->__size;
		lpsRowSet->__ptr[i].__ptr = s_alloc<propVal>(soap, lpsPropTagArray->__size);
		memset(lpsRowSet->__ptr[i].__ptr, 0, sizeof(propVal) * lpsPropTagArray->__size);
	}

	for (i = 0, iterRowList = lpRowList->begin();
	     iterRowList != lpRowList->end(); ++iterRowList, ++i)
	{
		if(lpUserManagement->GetServerDetails(lpStats->m_mapServers[iterRowList->ulObjId], &details) != erSuccess)
			details = serverdetails_t();
		
		for (gsoap_size_t k = 0; k < lpsPropTagArray->__size; ++k) {
			// default is error prop
			lpsRowSet->__ptr[i].__ptr[k].ulPropTag = PROP_TAG(PROP_TYPE(PT_ERROR), PROP_ID(lpsPropTagArray->__ptr[k]));
			lpsRowSet->__ptr[i].__ptr[k].Value.ul = KCERR_NOT_FOUND;
			lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;

			switch (PROP_ID(lpsPropTagArray->__ptr[k])) {
			case PROP_ID(PR_INSTANCE_KEY):
				// generate key 
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_bin;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.bin = s_alloc<xsd__base64Binary>(soap);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__size = sizeof(sObjectTableKey);
				lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(sObjectTableKey));
				memcpy(lpsRowSet->__ptr[i].__ptr[k].Value.bin->__ptr, (void*)&(*iterRowList), sizeof(sObjectTableKey));
				break;
			case PROP_ID(PR_EC_STATS_SERVER_NAME):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, lpStats->m_mapServers[iterRowList->ulObjId].c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_HTTPPORT):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = details.GetHttpPort();
				break;
			case PROP_ID(PR_EC_STATS_SERVER_SSLPORT):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_ul;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.ul = details.GetSslPort();
				break;
			case PROP_ID(PR_EC_STATS_SERVER_HOST):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, details.GetHostAddress().c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_PROXYURL):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, details.GetProxyPath().c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_HTTPURL):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, details.GetHttpPath().c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_HTTPSURL):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, details.GetSslPath().c_str());
				break;
			case PROP_ID(PR_EC_STATS_SERVER_FILEURL):
				lpsRowSet->__ptr[i].__ptr[k].__union = SOAP_UNION_propValData_lpszA;
				lpsRowSet->__ptr[i].__ptr[k].ulPropTag = lpsPropTagArray->__ptr[k];
				lpsRowSet->__ptr[i].__ptr[k].Value.lpszA = s_strcpy(soap, details.GetFilePath().c_str());
				break;
			};
		}
	}	

	*lppRowSet = lpsRowSet;
	return erSuccess;
}

