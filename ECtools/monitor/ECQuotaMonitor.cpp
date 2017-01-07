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

// Mapi includes
#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>
#include <edkguid.h>
#include <edkmdb.h>

// Kopano includes
#include <kopano/ECDefs.h>
#include <kopano/ECABEntryID.h>
#include <kopano/IECUnknown.h>
#include <kopano/Util.h>
#include <kopano/ecversion.h>
#include <kopano/charset/convert.h>
#include <kopano/mapi_ptr.h>
#include <kopano/MAPIErrors.h>

//#include <kopano/IECSecurity.h>
#include <kopano/ECGuid.h>
#include <kopano/ECTags.h>
#include <kopano/IECServiceAdmin.h>
#include <kopano/CommonUtil.h>
#include <kopano/stringutil.h>
#include <kopano/mapiext.h>
#include <kopano/restrictionutil.h>

// Other
#include "ECMonitorDefs.h"
#include "ECQuotaMonitor.h"

#include <iterator>
#include <set>
#include <string>
#include <vector>
using namespace std;

#define QUOTA_CONFIG_MSG "Kopano.Quota"

/**
 * Takes an extra reference to the passed MAPI objects which have refcounting.
 */
ECQuotaMonitor::ECQuotaMonitor(ECTHREADMONITOR *lpThreadMonitor,
    LPMAPISESSION lpMAPIAdminSession, LPMDB lpMDBAdmin)
{
	m_lpThreadMonitor = lpThreadMonitor;

	m_lpMAPIAdminSession = lpMAPIAdminSession;
	m_lpMDBAdmin = lpMDBAdmin;
	
	m_ulProcessed = 0;
	m_ulFailed = 0;

	if(lpMAPIAdminSession)
		lpMAPIAdminSession->AddRef();
	if(lpMDBAdmin)
		lpMDBAdmin->AddRef();
}

/**
 * Releases references to passed MAPI objects.
 */
ECQuotaMonitor::~ECQuotaMonitor()
{
	if(m_lpMDBAdmin)
		m_lpMDBAdmin->Release();

	if(m_lpMAPIAdminSession)
		m_lpMAPIAdminSession->Release();
}

/** Creates ECQuotaMonitor object and calls
 * ECQuotaMonitor::CheckQuota(). Entry point for this class.
 *
 * @param[in] lpVoid ECTHREADMONITOR struct
 * @return NULL
 */
void* ECQuotaMonitor::Create(void* lpVoid)
{
	HRESULT				hr = hrSuccess;
	ECTHREADMONITOR *lpThreadMonitor = reinterpret_cast<ECTHREADMONITOR *>(lpVoid);
	ECQuotaMonitor*		lpecQuotaMonitor = NULL;

	LPMAPISESSION		lpMAPIAdminSession = NULL;
	LPMDB				lpMDBAdmin = NULL;
	time_t				tmStart = 0;
	time_t				tmEnd = 0;

	const char *lpPath = lpThreadMonitor->lpConfig->GetSetting("server_socket");

	lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_INFO, "Quota monitor starting");

	//Open admin session
	hr = HrOpenECAdminSession(lpThreadMonitor->lpLogger, &lpMAPIAdminSession, "kopano-monitor:create", PROJECT_SVN_REV_STR, lpPath, 0, lpThreadMonitor->lpConfig->GetSetting("sslkey_file","",NULL), lpThreadMonitor->lpConfig->GetSetting("sslkey_pass","",NULL));
	if (hr != hrSuccess) {
		lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open an admin session. Error 0x%X", hr);
		goto exit;
	}

	lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_INFO, "Connection to storage server succeeded");

	// Open admin store
	hr = HrOpenDefaultStore(lpMAPIAdminSession, &lpMDBAdmin);
	if (hr != hrSuccess) {
		lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open default store for system account");
		goto exit;
	}

	lpecQuotaMonitor = new ECQuotaMonitor(lpThreadMonitor, lpMAPIAdminSession, lpMDBAdmin);

	// Release session and store (Reference on ECQuotaMonitor)
	if(lpMDBAdmin){ lpMDBAdmin->Release(); lpMDBAdmin = NULL;}
	if(lpMAPIAdminSession){ lpMAPIAdminSession->Release(); lpMAPIAdminSession = NULL;}

	// Check the quota of allstores
	tmStart = GetProcessTime();
	hr = lpecQuotaMonitor->CheckQuota();
	tmEnd = GetProcessTime();

	if(hr != hrSuccess)
		lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Quota monitor failed");
	else
		lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_INFO, "Quota monitor done in %lu seconds. Processed: %u, Failed: %u", tmEnd - tmStart, lpecQuotaMonitor->m_ulProcessed, lpecQuotaMonitor->m_ulFailed);
		
exit:
	delete lpecQuotaMonitor;
	if(lpMDBAdmin)
		lpMDBAdmin->Release();

	if(lpMAPIAdminSession)
		lpMAPIAdminSession->Release();

	return NULL;
}

/** Gets a list of companies and checks the quota. Then it calls
 * ECQuotaMonitor::CheckCompanyQuota() to check quota of the users
 * in the company. If the server is not running in hosted mode,
 * the default company 0 will be used.
 *
 * @return hrSuccess or any MAPI error code.
 */
HRESULT ECQuotaMonitor::CheckQuota()
{
	HRESULT 			hr = hrSuccess;

	/* Service object */
	IECServiceAdmin		*lpServiceAdmin = NULL;
	LPSPropValue		lpsObject = NULL;
	IExchangeManageStore *lpIEMS = NULL;

	/* Companylist */
	ECCOMPANY *lpsCompanyList = NULL;
	ECCOMPANY *lpsCompanyListAlloc = NULL;
	ECCOMPANY			sRootCompany = {{g_cbSystemEid, g_lpSystemEid}, (LPTSTR)"Default", NULL, {0, NULL}};
    ULONG				cCompanies = 0;

	/* Company store */
	LPMDB				lpMsgStore = NULL;

	/* Quota information */
	ECQUOTA *lpsQuota = NULL;
	ECQUOTASTATUS *lpsQuotaStatus = NULL;

	/* Obtain Service object */
	hr = HrGetOneProp(m_lpMDBAdmin, PR_EC_OBJECT, &lpsObject);
	if(hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get internal object, error code: 0x%08X", hr);
		goto exit;
	}

	hr = reinterpret_cast<IECUnknown *>(lpsObject->Value.lpszA)->QueryInterface(IID_IECServiceAdmin, reinterpret_cast<void **>(&lpServiceAdmin));
	if(hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get service admin, error code: 0x%08X", hr);
		goto exit;
	}

	/* Get companylist */
	hr = lpServiceAdmin->GetCompanyList(0, &cCompanies, &lpsCompanyListAlloc);
	if (hr == MAPI_E_NO_SUPPORT) {
		lpsCompanyList = &sRootCompany;
		cCompanies = 1;
		hr = hrSuccess;
	} else if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get companylist, error code 0x%08X", hr);
		goto exit;
	} else
		lpsCompanyList = lpsCompanyListAlloc;

	hr = m_lpMDBAdmin->QueryInterface(IID_IExchangeManageStore, (void **)&lpIEMS);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get admin interface, error code 0x%08X", hr);
		goto exit;
	}

	for (ULONG i = 0; i < cCompanies; ++i) {
		/* Check company quota for non-default company */
		if (lpsCompanyList[i].sCompanyId.cb != 0 && lpsCompanyList[i].sCompanyId.lpb != NULL) {
			++m_ulProcessed;
		
			hr = lpServiceAdmin->GetQuota(lpsCompanyList[i].sCompanyId.cb, (LPENTRYID)lpsCompanyList[i].sCompanyId.lpb, false, &lpsQuota);
			if (hr != hrSuccess) {
				m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get quota information for company %s, error code: 0x%08X", (LPSTR)lpsCompanyList[i].lpszCompanyname, hr);
				hr = hrSuccess;
				++m_ulFailed;
				goto check_stores;
			}

			hr = OpenUserStore(lpsCompanyList[i].lpszCompanyname, CONTAINER_COMPANY, &lpMsgStore);
			if (hr != hrSuccess) {
				hr = hrSuccess;
				++m_ulFailed;
				goto check_stores;
			}

			hr = Util::HrGetQuotaStatus(lpMsgStore, lpsQuota, &lpsQuotaStatus);
			if (hr != hrSuccess) {
				m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get quotastatus for company %s, error code: 0x%08X", (LPSTR)lpsCompanyList[i].lpszCompanyname, hr);
				hr = hrSuccess;
				++m_ulFailed;
				goto check_stores;
			}

			if (lpsQuotaStatus->quotaStatus != QUOTA_OK) {
				m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Storage size of company %s has exceeded one or more size limits", (LPSTR)lpsCompanyList[i].lpszCompanyname);
				Notify(NULL, &lpsCompanyList[i], lpsQuotaStatus, lpMsgStore);
			}
		}

check_stores:
		/* Whatever the status of the company quota, we should also check the quota of the users */
		CheckCompanyQuota(&lpsCompanyList[i]);
		MAPIFreeBuffer(lpsQuotaStatus);
		lpsQuotaStatus = NULL;
		if (lpMsgStore) {
			lpMsgStore->Release();
			lpMsgStore = NULL;
		}
		MAPIFreeBuffer(lpsQuota);
		lpsQuota = NULL;
	}

exit:
	if (lpIEMS)
		lpIEMS->Release();

	if (lpServiceAdmin)
		lpServiceAdmin->Release();
	MAPIFreeBuffer(lpsObject);
	MAPIFreeBuffer(lpsCompanyListAlloc);
	MAPIFreeBuffer(lpsQuotaStatus);
	if (lpMsgStore) 
		lpMsgStore->Release();
	MAPIFreeBuffer(lpsQuota);
	return hr;
}

/** Uses the ECServiceAdmin to get a list of all users within a
 * given company and groups those per kopano-server instance. Per
 * server it calls ECQuotaMonitor::CheckServerQuota().
 *
 * @param[in] company lpecCompany ECCompany struct
 * @return hrSuccess or any MAPI error code.
 */
HRESULT ECQuotaMonitor::CheckCompanyQuota(ECCOMPANY *lpecCompany)
{
	HRESULT				hr = hrSuccess;
	/* Service object */
	IECServiceAdmin		*lpServiceAdmin = NULL;
	LPSPropValue		lpsObject = NULL;
	/* Userlist */
	ECUSER *lpsUserList = NULL;
	ULONG				cUsers = 0;
	/* 2nd Server connection */
	LPMAPISESSION		lpSession = NULL;
	LPMDB				lpAdminStore = NULL;

	set<string> setServers;
	const char *lpszServersConfig;
	std::set<string, strcasecmp_comparison> setServersConfig;
	set<string>::const_iterator iServers;
	char *lpszConnection = NULL;
	bool bIsPeer = false;

	m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_INFO, "Checking quota for company %s", (char*)lpecCompany->lpszCompanyname);

	/* Obtain Service object */
	hr = HrGetOneProp(m_lpMDBAdmin, PR_EC_OBJECT, &lpsObject);
	if(hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get internal object, error code: 0x%08X", hr);
		goto exit;
	}

	hr = reinterpret_cast<IECUnknown *>(lpsObject->Value.lpszA)->QueryInterface(IID_IECServiceAdmin, reinterpret_cast<void **>(&lpServiceAdmin));
	if(hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get service admin, error code: 0x%08X", hr);
		goto exit;
	}

	/* Get userlist */
	hr = lpServiceAdmin->GetUserList(lpecCompany->sCompanyId.cb, (LPENTRYID)lpecCompany->sCompanyId.lpb, 0, &cUsers, &lpsUserList);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get userlist for company %s, error code 0x%08X", (LPSTR)lpecCompany->lpszCompanyname, hr);
		goto exit;
	}

	for (ULONG i = 0; i < cUsers; ++i)
		if (lpsUserList[i].lpszServername && lpsUserList[i].lpszServername[0] != '\0')
			setServers.insert((char*)lpsUserList[i].lpszServername);

	if (setServers.empty()) {
		// call server function with current lpMDBAdmin / lpServiceAdmin
		
		hr = CheckServerQuota(cUsers, lpsUserList, lpecCompany, m_lpMDBAdmin);
		if (hr != hrSuccess) {
			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to check server quota, error code 0x%08X", hr);
			goto exit;
		}

	} else {
		lpszServersConfig = m_lpThreadMonitor->lpConfig->GetSetting("servers","",NULL);
		if(lpszServersConfig) {
			// split approach taken from varafa-backup/backup.cpp
			std::vector<std::string> ddv = tokenize(lpszServersConfig, "\t ");
			std::move(ddv.begin(), ddv.end(), std::inserter(setServersConfig, setServersConfig.begin()));
		}

		for (iServers = setServers.begin(); iServers != setServers.end(); ++iServers)
		{
                        if(!setServersConfig.empty() && setServersConfig.find((*iServers).c_str()) == setServersConfig.end())
                                continue;
 
			hr = lpServiceAdmin->ResolvePseudoUrl((char*)string("pseudo://"+ (*iServers)).c_str(), &lpszConnection, &bIsPeer);
			if (hr != hrSuccess) {
				m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to resolve servername %s, error code 0x%08X", iServers->c_str(), hr);
				++m_ulFailed;
				goto next;
			}

			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_INFO, "Connecting to server %s using url %s", iServers->c_str(), lpszConnection);

			// call server function with new lpMDBAdmin / lpServiceAdmin
			if (bIsPeer) {
				// query interface
				hr = m_lpMDBAdmin->QueryInterface(IID_IMsgStore, (void**)&lpAdminStore);
				if (hr != hrSuccess) {
					m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get service interface again, error code 0x%08X", hr);
					++m_ulFailed;
					goto next;
				}
			} else {				
				hr = HrOpenECAdminSession(m_lpThreadMonitor->lpLogger, &lpSession, "kopano-monitor:check-company", PROJECT_SVN_REV_STR, lpszConnection, 0, m_lpThreadMonitor->lpConfig->GetSetting("sslkey_file","",NULL), m_lpThreadMonitor->lpConfig->GetSetting("sslkey_pass","",NULL));
				if (hr != hrSuccess) {
					m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to connect to server %s, error code 0x%08X", lpszConnection, hr);
					++m_ulFailed;
					goto next;
				}

				hr = HrOpenDefaultStore(lpSession, &lpAdminStore);
				if (hr != hrSuccess) {
					m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open admin store on server %s, error code 0x%08X", lpszConnection, hr);
					++m_ulFailed;
					goto next;
				}
			}

			hr = CheckServerQuota(cUsers, lpsUserList, lpecCompany, lpAdminStore);
			if (hr != hrSuccess) {
				m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to check quota on server %s, error code 0x%08X", lpszConnection, hr);
				++m_ulFailed;
			}

next:
			MAPIFreeBuffer(lpszConnection);
			lpszConnection = NULL;

			if (lpSession)
				lpSession->Release();
			lpSession = NULL;

			if (lpAdminStore)
				lpAdminStore->Release();
			lpAdminStore = NULL;
		}
	}

exit:
	MAPIFreeBuffer(lpszConnection);
	if(lpServiceAdmin)
		lpServiceAdmin->Release();
	MAPIFreeBuffer(lpsObject);
	MAPIFreeBuffer(lpsUserList);
	return hr;
}

/**
 * Checks in the ECStatsTable PR_EC_STATSTABLE_USERS for quota
 * information per connected server given in lpAdminStore.
 *
 * @param[in]	cUsers		number of users in lpsUserList
 * @param[in]	lpsUserList	array of ECUser struct, containing all Kopano from all companies, on any server
 * @param[in]	lpecCompany	same company struct as in ECQuotaMonitor::CheckCompanyQuota()
 * @param[in]	lpAdminStore IMsgStore of SYSTEM user on a specific server instance.
 * @return hrSuccess or any MAPI error code.
 */
HRESULT ECQuotaMonitor::CheckServerQuota(ULONG cUsers, ECUSER *lpsUserList,
    ECCOMPANY *lpecCompany, LPMDB lpAdminStore)
{
	HRESULT hr = hrSuccess;
	LPSRestriction lpsRestriction = NULL;
	SPropValue sRestrictProp;
	LPMAPITABLE lpTable = NULL;
	LPSRowSet lpRowSet = NULL;
	ECQUOTASTATUS sQuotaStatus;
	ULONG i, u;
	SizedSPropTagArray(5, sCols) = {
		5, {
			PR_EC_USERNAME_A,
			PR_MESSAGE_SIZE_EXTENDED,
			PR_QUOTA_WARNING_THRESHOLD,
			PR_QUOTA_SEND_THRESHOLD,
			PR_QUOTA_RECEIVE_THRESHOLD,
		}
	};

	hr = lpAdminStore->OpenProperty(PR_EC_STATSTABLE_USERS, &IID_IMAPITable, 0, 0, (LPUNKNOWN*)&lpTable);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open stats table for quota sizes, error 0x%08X", hr);
		goto exit;
	}

	hr = lpTable->SetColumns((LPSPropTagArray)&sCols, MAPI_DEFERRED_ERRORS);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to set columns on stats table for quota sizes, error 0x%08X", hr);
		goto exit;
	}

	if (lpecCompany->sCompanyId.cb != 0 && lpecCompany->sCompanyId.lpb != NULL) {
		sRestrictProp.ulPropTag = PR_EC_COMPANY_NAME_A;
		sRestrictProp.Value.lpszA = (char*)lpecCompany->lpszCompanyname;

		CREATE_RESTRICTION(lpsRestriction);
		CREATE_RES_OR(lpsRestriction, lpsRestriction, 2);
		  CREATE_RES_NOT(lpsRestriction, &lpsRestriction->res.resOr.lpRes[0]);
		    DATA_RES_EXIST(lpsRestriction, lpsRestriction->res.resOr.lpRes[0].res.resNot.lpRes[0], PR_EC_COMPANY_NAME_A);
		  DATA_RES_PROPERTY(lpsRestriction, lpsRestriction->res.resOr.lpRes[1], RELOP_EQ, PR_EC_COMPANY_NAME_A, &sRestrictProp);

		hr = lpTable->Restrict(lpsRestriction, MAPI_DEFERRED_ERRORS);
		if (hr != hrSuccess) {
			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to restrict stats table, error 0x%08X", hr);
			goto exit;
		}
	}

	while (TRUE) {
		hr = lpTable->QueryRows(50, 0, &lpRowSet);
		if (hr != hrSuccess) {
			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to receive stats table data, error 0x%08X", hr);
			goto exit;
		}

		if (lpRowSet->cRows == 0)
			break;

		for (i = 0; i < lpRowSet->cRows; ++i) {
			LPSPropValue lpUsername = NULL;
			LPSPropValue lpStoreSize = NULL;
			LPSPropValue lpQuotaWarn = NULL;
			LPSPropValue lpQuotaSoft = NULL;
			LPSPropValue lpQuotaHard = NULL;
			MsgStorePtr ptrStore;

			lpUsername = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_EC_USERNAME_A);
			lpStoreSize = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_MESSAGE_SIZE_EXTENDED);
			lpQuotaWarn = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_QUOTA_WARNING_THRESHOLD);
			lpQuotaSoft = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_QUOTA_SEND_THRESHOLD);
			lpQuotaHard = PpropFindProp(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues, PR_QUOTA_RECEIVE_THRESHOLD);

			if (!lpUsername || !lpStoreSize)
				continue;		// don't log error: could be for several valid reasons (contacts, other server, etc)

			if (lpStoreSize->Value.li.QuadPart == 0)
				continue;

			++m_ulProcessed;

			memset(&sQuotaStatus, 0, sizeof(ECQUOTASTATUS));

			sQuotaStatus.llStoreSize = lpStoreSize->Value.li.QuadPart;
			sQuotaStatus.quotaStatus = QUOTA_OK;
			if (lpQuotaHard && lpQuotaHard->Value.ul > 0 && lpStoreSize->Value.li.QuadPart > ((long long)lpQuotaHard->Value.ul * 1024))
				sQuotaStatus.quotaStatus = QUOTA_HARDLIMIT;
			else if (lpQuotaSoft && lpQuotaSoft->Value.ul > 0 && lpStoreSize->Value.li.QuadPart > ((long long)lpQuotaSoft->Value.ul * 1024))
				sQuotaStatus.quotaStatus = QUOTA_SOFTLIMIT;
			else if (lpQuotaWarn && lpQuotaWarn->Value.ul > 0 && lpStoreSize->Value.li.QuadPart > ((long long)lpQuotaWarn->Value.ul * 1024))
				sQuotaStatus.quotaStatus = QUOTA_WARN;

			if (sQuotaStatus.quotaStatus == QUOTA_OK)
				continue;

			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Mailbox of user %s has exceeded its %s limit", lpUsername->Value.lpszA, sQuotaStatus.quotaStatus == QUOTA_WARN ? "warning" : sQuotaStatus.quotaStatus == QUOTA_SOFTLIMIT ? "soft" : "hard");

			// find the user in the full users list
			for (u = 0; u < cUsers; ++u) {
				if (strcmp((char*)lpsUserList[u].lpszUsername, lpUsername->Value.lpszA) == 0)
					break;
			}
			if (u == cUsers) {
				m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to find user %s in userlist", lpUsername->Value.lpszA);
				++m_ulFailed;
				continue;
			}
			hr = OpenUserStore(lpsUserList[u].lpszUsername, ACTIVE_USER, &ptrStore);
			if (hr != hrSuccess) {
				hr = hrSuccess;
				continue;
			}
			hr = Notify(&lpsUserList[u], lpecCompany, &sQuotaStatus, ptrStore);
			if (hr != hrSuccess)
				++m_ulFailed;
		}

		if (lpRowSet)
			FreeProws(lpRowSet);
		lpRowSet = NULL;
	}

exit:
	if (lpRowSet)
		FreeProws(lpRowSet);
	MAPIFreeBuffer(lpsRestriction);
	if (lpTable)
		lpTable->Release();

	return hr;
}

/**
 * Returns an email body and subject string with template variables replaced.
 *
 * @param[in]	lpVars	structure with all template variable strings
 * @param[out]	lpstrSubject	the filled in subject string
 * @param[out]	lpstrBody		the filled in mail body
 * @retval	MAPI_E_NOT_FOUND	the template file set in the config was not found
 */
HRESULT ECQuotaMonitor::CreateMailFromTemplate(TemplateVariables *lpVars, string *lpstrSubject, string *lpstrBody)
{
	string strTemplateConfig;
	const char *lpszTemplate = NULL;
	FILE *fp = NULL;
	char cBuffer[TEMPLATE_LINE_LENGTH];
	string strLine;
	string strSubject;
	string strBody;
	size_t pos;

	string strVariables[7][2] = {
		{ "${KOPANO_QUOTA_NAME}", "unknown" },
		{ "${KOPANO_QUOTA_FULLNAME}" , "unknown" },
		{ "${KOPANO_QUOTA_COMPANY}", "unknown" },
		{ "${KOPANO_QUOTA_STORE_SIZE}", "unknown" },
		{ "${KOPANO_QUOTA_WARN_SIZE}", "unlimited" },
		{ "${KOPANO_QUOTA_SOFT_SIZE}", "unlimited" },
		{ "${KOPANO_QUOTA_HARD_SIZE}", "unlimited" },
	};

	enum enumVariables {
		KOPANO_QUOTA_NAME,
		KOPANO_QUOTA_FULLNAME,
		KOPANO_QUOTA_COMPANY,
		KOPANO_QUOTA_STORE_SIZE,
		KOPANO_QUOTA_WARN_SIZE,
		KOPANO_QUOTA_SOFT_SIZE,
		KOPANO_QUOTA_HARD_SIZE,
		KOPANO_QUOTA_LAST_ITEM /* KEEP LAST! */
	};

	if (lpVars->ulClass == CONTAINER_COMPANY) {
		// Company public stores only support QUOTA_WARN.
		strTemplateConfig = "companyquota_warning_template";
	} else {
		switch (lpVars->ulStatus) {
		case QUOTA_WARN:
				strTemplateConfig = "userquota_warning_template";
			break;
		case QUOTA_SOFTLIMIT:
				strTemplateConfig = "userquota_soft_template";
			break;
		case QUOTA_HARDLIMIT:
			strTemplateConfig = "userquota_warning_template";
			break;
		case QUOTA_OK:
		default:
			return hrSuccess;
		}
	}

	lpszTemplate = m_lpThreadMonitor->lpConfig->GetSetting(strTemplateConfig.c_str());

	/* Start reading the template mail */
	if((fp = fopen(lpszTemplate, "rt")) == NULL) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed to open template email: %s", lpszTemplate);
		return MAPI_E_NOT_FOUND;
	}

	while(!feof(fp)) {
		memset(&cBuffer, 0, sizeof(cBuffer));

		if (!fgets(cBuffer, sizeof(cBuffer), fp))
			break;

		strLine = string(cBuffer);

		/* If this is the subject line, don't attach it to the mail */
		if (strLine.compare(0, strlen("Subject:"), "Subject:") == 0)
			strSubject = strLine.substr(strLine.find_first_not_of(" ", strlen("Subject:")));
		else
			strBody += strLine;
	}

	fclose(fp);
	fp = NULL;

	if (!lpVars->strUserName.empty())
		strVariables[KOPANO_QUOTA_NAME][1] = lpVars->strUserName;
	if (!lpVars->strFullName.empty())
		strVariables[KOPANO_QUOTA_FULLNAME][1] = lpVars->strFullName;
	if (!lpVars->strCompany.empty())
		strVariables[KOPANO_QUOTA_COMPANY][1] = lpVars->strCompany;
	if (!lpVars->strStoreSize.empty())
		strVariables[KOPANO_QUOTA_STORE_SIZE][1] = lpVars->strStoreSize;
	if (!lpVars->strWarnSize.empty())
		strVariables[KOPANO_QUOTA_WARN_SIZE][1] = lpVars->strWarnSize;
	if (!lpVars->strSoftSize.empty())
		strVariables[KOPANO_QUOTA_SOFT_SIZE][1] = lpVars->strSoftSize;
	if (!lpVars->strHardSize.empty())
		strVariables[KOPANO_QUOTA_HARD_SIZE][1] = lpVars->strHardSize;

	for (unsigned int i = 0; i < KOPANO_QUOTA_LAST_ITEM; ++i) {
		pos = 0;
		while ((pos = strSubject.find(strVariables[i][0], pos)) != string::npos) {
			strSubject.replace(pos, strVariables[i][0].size(), strVariables[i][1]);
			pos += strVariables[i][1].size();
		}

		pos = 0;
		while ((pos = strBody.find(strVariables[i][0], pos)) != string::npos) {
			strBody.replace(pos, strVariables[i][0].size(), strVariables[i][1]);
			pos += strVariables[i][1].size();
		}
	}

	/* Clear end-of-line characters from subject */
	pos = strSubject.find('\n');
	if (pos != string::npos)
		strSubject.erase(pos);
	pos = strSubject.find('\r');
	if (pos != string::npos)
		strSubject.erase(pos);

	/* Clear starting blank lines from body */
	while (strBody[0] == '\r' || strBody[0] == '\n')
		strBody.erase(0, 1);

	*lpstrSubject = strSubject;
	*lpstrBody = strBody;
	return hrSuccess;
}

/**
 * Creates a set of properties which need to be set on an IMessage
 * which becomes the MAPI quota message.
 *
 * @param[in]	lpecToUser		User which is set in the received properties
 * @param[in]	lpecFromUser	User which is set in the sender properties
 * @param[in]	strSubject		The subject field
 * @param[in]	strBody			The plain text body
 * @param[out]	lpcPropSize		The number of properties in lppPropArray
 * @param[out]	lppPropArray	The properties to write to the IMessage
 * @retval	MAPI_E_NOT_ENOUGH_MEMORY	unable to allocate more memory
 */
HRESULT ECQuotaMonitor::CreateMessageProperties(ECUSER *lpecToUser,
    ECUSER *lpecFromUser, const std::string &strSubject,
    const std::string &strBody, ULONG *lpcPropSize, LPSPropValue *lppPropArray)
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpPropArray = NULL;
	ULONG cbFromEntryid = 0;
	LPENTRYID lpFromEntryid = NULL;
    ULONG cbToEntryid = 0;
	LPENTRYID lpToEntryid = NULL;
	ULONG cbFromSearchKey = 0;
	LPBYTE lpFromSearchKey = NULL;
	ULONG cbToSearchKey = 0;
	LPBYTE lpToSearchKey = NULL;
	ULONG ulPropArrayMax = 50;
	ULONG ulPropArrayCur = 0;
	FILETIME ft;
	wstring name, email;
	convert_context converter;

	/* We are almost there, we have the mail and the recipients. Now we should create the Message */
	hr = MAPIAllocateBuffer(sizeof(SPropValue)  * ulPropArrayMax, (void**)&lpPropArray);
	if (hr != hrSuccess)
		goto exit;
	
	if (TryConvert(converter, (char*)lpecToUser->lpszFullName, name) != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to convert To name %s to widechar, using empty name in entryid", (char*)lpecToUser->lpszFullName);
		name.clear();
	}
	if (TryConvert(converter, (char*)lpecToUser->lpszMailAddress, email) != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to convert To email %s to widechar, using empty name in entryid", (char*)lpecToUser->lpszMailAddress);
		email.clear();
	}

	hr = ECCreateOneOff((LPTSTR)name.c_str(), (LPTSTR)L"SMTP", (LPTSTR)email.c_str(), MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbToEntryid, &lpToEntryid);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed creating one-off address: 0x%08X", hr);
		goto exit;
	}

	hr = HrCreateEmailSearchKey("SMTP", (char*)lpecToUser->lpszMailAddress, &cbToSearchKey, &lpToSearchKey);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed creating email searchkey: 0x%08X", hr);
		goto exit;
	}

	if (TryConvert(converter, (char*)lpecFromUser->lpszFullName, name) != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to convert From name %s to widechar, using empty name in entryid", (char*)lpecFromUser->lpszFullName);
		name.clear();
	}
	if (TryConvert(converter, (char*)lpecFromUser->lpszMailAddress, email) != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to convert From email %s to widechar, using empty name in entryid", (char*)lpecFromUser->lpszMailAddress);
		email.clear();
	}

	hr = ECCreateOneOff((LPTSTR)name.c_str(), (LPTSTR)L"SMTP", (LPTSTR)email.c_str(), MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbFromEntryid, &lpFromEntryid);
	if(hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed creating one-off address: 0x%08X", hr);
		goto exit;
	}

	hr = HrCreateEmailSearchKey("SMTP", (char*)lpecFromUser->lpszMailAddress, &cbFromSearchKey, &lpFromSearchKey);
	if(hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed creating email searchkey: 0x%08X", hr);
		goto exit;
	}

	/* Messageclass */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_MESSAGE_CLASS_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = const_cast<char *>("IPM.Note.StorageQuotaWarning");

	/* Priority */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_PRIORITY;
	lpPropArray[ulPropArrayCur++].Value.ul = PRIO_URGENT;

	/* Importance */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_IMPORTANCE;
	lpPropArray[ulPropArrayCur++].Value.ul = IMPORTANCE_HIGH;

	/* Set message flags */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_MESSAGE_FLAGS;
	lpPropArray[ulPropArrayCur++].Value.ul = MSGFLAG_UNMODIFIED;

	/* Subject */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_SUBJECT_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (LPSTR)strSubject.c_str();

	/* PR_CONVERSATION_TOPIC */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_CONVERSATION_TOPIC_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (LPSTR)strSubject.c_str();

	/* Body */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_BODY_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (LPSTR)strBody.c_str();

	/* RCVD_REPRESENTING_* properties */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_RCVD_REPRESENTING_ADDRTYPE_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = const_cast<char *>("SMTP");

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RCVD_REPRESENTING_EMAIL_ADDRESS_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecToUser->lpszMailAddress ? (LPSTR)lpecToUser->lpszMailAddress : (LPSTR)"");

	hr = MAPIAllocateMore(cbToEntryid, lpPropArray,
						  (void**)&lpPropArray[ulPropArrayCur].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RCVD_REPRESENTING_ENTRYID;
	lpPropArray[ulPropArrayCur].Value.bin.cb = cbToEntryid;
	memcpy(lpPropArray[ulPropArrayCur++].Value.bin.lpb,
		   lpToEntryid, cbToEntryid);

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RCVD_REPRESENTING_NAME_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecToUser->lpszFullName ? (LPSTR)lpecToUser->lpszFullName : (LPSTR)"");

	hr = MAPIAllocateMore(cbToSearchKey, lpPropArray,
						  (void**)&lpPropArray[ulPropArrayCur].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RCVD_REPRESENTING_SEARCH_KEY;
	lpPropArray[ulPropArrayCur].Value.bin.cb = cbToSearchKey;
	memcpy(lpPropArray[ulPropArrayCur++].Value.bin.lpb,
		   lpToSearchKey, cbToSearchKey);

	/* RECEIVED_BY_* properties */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_ADDRTYPE_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = const_cast<char *>("SMTP");

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_EMAIL_ADDRESS_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecToUser->lpszMailAddress ? (LPSTR)lpecToUser->lpszMailAddress : (LPSTR)"");

	hr = MAPIAllocateMore(cbToEntryid, lpPropArray,
						  (void**)&lpPropArray[ulPropArrayCur].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_ENTRYID;
	lpPropArray[ulPropArrayCur].Value.bin.cb = cbToEntryid;
	memcpy(lpPropArray[ulPropArrayCur++].Value.bin.lpb,
		   lpToEntryid, cbToEntryid);

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_NAME_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecToUser->lpszFullName ? (LPSTR)lpecToUser->lpszFullName : (LPSTR)"");

	hr = MAPIAllocateMore(cbToSearchKey, lpPropArray,
						  (void**)&lpPropArray[ulPropArrayCur].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_SEARCH_KEY;
	lpPropArray[ulPropArrayCur].Value.bin.cb = cbToSearchKey;
	memcpy(lpPropArray[ulPropArrayCur++].Value.bin.lpb,
		   lpToSearchKey, cbToSearchKey);

	/* System user, PR_SENDER* */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_ADDRTYPE_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = const_cast<char *>("SMTP");

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_EMAIL_ADDRESS_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecFromUser->lpszMailAddress ? (LPSTR)lpecFromUser->lpszMailAddress : (LPSTR)"");

	hr = MAPIAllocateMore(cbFromEntryid, lpPropArray,
						  (void**)&lpPropArray[ulPropArrayCur].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_ENTRYID;
	lpPropArray[ulPropArrayCur].Value.bin.cb = cbFromEntryid;
	memcpy(lpPropArray[ulPropArrayCur++].Value.bin.lpb,
		   lpFromEntryid, cbFromEntryid);

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_NAME_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecFromUser->lpszFullName ? (LPSTR)lpecFromUser->lpszFullName : (LPSTR)"kopano-system");

	hr = MAPIAllocateMore(cbFromSearchKey, lpPropArray,
						  (void**)&lpPropArray[ulPropArrayCur].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_SEARCH_KEY;
	lpPropArray[ulPropArrayCur].Value.bin.cb = cbFromSearchKey;
	memcpy(lpPropArray[ulPropArrayCur++].Value.bin.lpb,
		   lpFromSearchKey, cbFromSearchKey);

	/* System user, PR_SENT_REPRESENTING* */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = const_cast<char *>("SMTP");

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecFromUser->lpszMailAddress ? (LPSTR)lpecFromUser->lpszMailAddress : (LPSTR)"");

	hr = MAPIAllocateMore(cbFromEntryid, lpPropArray,
						  (void**)&lpPropArray[ulPropArrayCur].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
	lpPropArray[ulPropArrayCur].Value.bin.cb = cbFromEntryid;
	memcpy(lpPropArray[ulPropArrayCur++].Value.bin.lpb,
		   lpFromEntryid, cbFromEntryid);

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_NAME_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecFromUser->lpszFullName ? (LPSTR)lpecFromUser->lpszFullName : (LPSTR)"kopano-system");

	hr = MAPIAllocateMore(cbFromSearchKey, lpPropArray,
						  (void**)&lpPropArray[ulPropArrayCur].Value.bin.lpb);
	if (hr != hrSuccess)
		goto exit;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_SEARCH_KEY;
	lpPropArray[ulPropArrayCur].Value.bin.cb = cbFromSearchKey;
	memcpy(lpPropArray[ulPropArrayCur++].Value.bin.lpb,
		   lpFromSearchKey, cbFromSearchKey);

	/* Get the time to add to the message as PR_CLIENT_SUBMIT_TIME */
	GetSystemTimeAsFileTime(&ft);

	/* Submit time */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_CLIENT_SUBMIT_TIME;
	lpPropArray[ulPropArrayCur++].Value.ft = ft;

	/* Delivery time */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_MESSAGE_DELIVERY_TIME;
	lpPropArray[ulPropArrayCur++].Value.ft = ft;

	ASSERT(ulPropArrayCur <= ulPropArrayMax);

	*lppPropArray = lpPropArray;
	*lpcPropSize = ulPropArrayCur;

exit:
	if (hr != hrSuccess)
		MAPIFreeBuffer(lpPropArray);
	MAPIFreeBuffer(lpToEntryid);
	MAPIFreeBuffer(lpFromEntryid);
	MAPIFreeBuffer(lpToSearchKey);
	MAPIFreeBuffer(lpFromSearchKey);
	return hr;
}

/**
 * Creates a recipient list to set in the IMessage as recipient table.
 *
 * @param[in]	cToUsers	Number of users in lpToUsers
 * @param[in]	lpToUsers	Structs of Kopano user information to write in the addresslist
 * @param[out]	lppAddrList	Addresslist for the recipient table
 * @retval	MAPI_E_NOT_ENOUGH_MEMORY	unable to allocate more memory
 */
HRESULT ECQuotaMonitor::CreateRecipientList(ULONG cToUsers, ECUSER *lpToUsers,
    LPADRLIST *lppAddrList)
{
	HRESULT hr = hrSuccess;
	LPADRLIST lpAddrList = NULL;
	ULONG cbUserEntryid = 0;
	LPENTRYID lpUserEntryid = NULL;
	ULONG cbUserSearchKey = 0;
	LPBYTE lpUserSearchKey = NULL;

	hr = MAPIAllocateBuffer(CbNewADRLIST(cToUsers), (void**)&lpAddrList);
	if(hr != hrSuccess)
		goto exit;

	lpAddrList->cEntries = cToUsers;
	for (ULONG i = 0; i < cToUsers; ++i) {
		lpAddrList->aEntries[i].cValues = 7;

		hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpAddrList->aEntries[i].cValues,
						  (void**)&lpAddrList->aEntries[i].rgPropVals);
		if (hr != hrSuccess)
			goto exit;

		hr = ECCreateOneOff((LPTSTR)lpToUsers[i].lpszFullName, (LPTSTR)"SMTP", (LPTSTR)lpToUsers[i].lpszMailAddress,
							MAPI_SEND_NO_RICH_INFO, &cbUserEntryid, &lpUserEntryid);
		if (hr != hrSuccess) {
			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed creating one-off address: 0x%08X", hr);
			goto exit;
		}

		hr = HrCreateEmailSearchKey("SMTP",
		     reinterpret_cast<const char *>(lpToUsers[i].lpszMailAddress),
		     &cbUserSearchKey, &lpUserSearchKey);
		if (hr != hrSuccess) {
			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Failed creating email searchkey: 0x%08X", hr);
			goto exit;
		}

		lpAddrList->aEntries[i].rgPropVals[0].ulPropTag = PR_ROWID;
		lpAddrList->aEntries[i].rgPropVals[0].Value.l = 0;

		lpAddrList->aEntries[i].rgPropVals[1].ulPropTag = PR_RECIPIENT_TYPE;
		lpAddrList->aEntries[i].rgPropVals[1].Value.l = ((i == 0) ? MAPI_TO : MAPI_CC);

		lpAddrList->aEntries[i].rgPropVals[2].ulPropTag = PR_DISPLAY_NAME_A;
		lpAddrList->aEntries[i].rgPropVals[2].Value.lpszA = (LPSTR)(lpToUsers[i].lpszFullName ? lpToUsers[i].lpszFullName : lpToUsers[i].lpszUsername);

		lpAddrList->aEntries[i].rgPropVals[3].ulPropTag = PR_EMAIL_ADDRESS_A;
		lpAddrList->aEntries[i].rgPropVals[3].Value.lpszA = (lpToUsers[i].lpszMailAddress ? (LPSTR)lpToUsers[i].lpszMailAddress : (LPSTR)"");

		lpAddrList->aEntries[i].rgPropVals[4].ulPropTag = PR_ADDRTYPE_A;
		lpAddrList->aEntries[i].rgPropVals[4].Value.lpszA = const_cast<char *>("SMTP");

		hr = MAPIAllocateMore(cbUserEntryid, lpAddrList->aEntries[i].rgPropVals,
							  (void**)&lpAddrList->aEntries[i].rgPropVals[5].Value.bin.lpb);
		if (hr != hrSuccess)
			goto exit;

		lpAddrList->aEntries[i].rgPropVals[5].ulPropTag = PR_ENTRYID;
		lpAddrList->aEntries[i].rgPropVals[5].Value.bin.cb = cbUserEntryid;
		memcpy(lpAddrList->aEntries[i].rgPropVals[5].Value.bin.lpb,
			   lpUserEntryid, cbUserEntryid);

		hr = MAPIAllocateMore(cbUserSearchKey, lpAddrList->aEntries[i].rgPropVals,
							  (void**)&lpAddrList->aEntries[i].rgPropVals[6].Value.bin.lpb);
		if (hr != hrSuccess)
			goto exit;
		lpAddrList->aEntries[i].rgPropVals[6].ulPropTag = PR_SEARCH_KEY;
		lpAddrList->aEntries[i].rgPropVals[6].Value.bin.cb = cbUserSearchKey;
		memcpy(lpAddrList->aEntries[i].rgPropVals[6].Value.bin.lpb,
			   lpUserSearchKey, cbUserSearchKey);
		MAPIFreeBuffer(lpUserEntryid);
		lpUserEntryid = NULL;
		MAPIFreeBuffer(lpUserSearchKey);
		lpUserSearchKey = NULL;
	}

	*lppAddrList = lpAddrList;

exit:		
	MAPIFreeBuffer(lpUserEntryid);
	MAPIFreeBuffer(lpUserSearchKey);
	if (hr != hrSuccess && lpAddrList)
		FreePadrlist(lpAddrList);

	return hr;
}

/**
 * Creates an email in the inbox of the given store with given properties and addresslist for recipients
 *
 * @param[in]	lpMDB		The store of a user to create the mail in the inbox folder
 * @param[in]	cPropSize	The number of properties in lpPropArray
 * @param[in]	lpPropArray	The properties to set in the quota mail
 * @param[in]	lpAddrList	The recipients to save in the recipients table
 * @return MAPI error code
 */
HRESULT ECQuotaMonitor::SendQuotaWarningMail(IMsgStore* lpMDB, ULONG cPropSize, LPSPropValue lpPropArray, LPADRLIST lpAddrList)
{
	HRESULT hr = hrSuccess;
	IMessage *lpMessage = NULL;
	ULONG cbEntryID = 0;
	LPENTRYID lpEntryID = NULL;
	IMAPIFolder *lpInbox = NULL;
	ULONG ulObjType;

	/* Get the entry id of the inbox */
	hr = lpMDB->GetReceiveFolder((LPTSTR)"IPM", 0, &cbEntryID, &lpEntryID, NULL);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to resolve incoming folder, error code: 0x%08X", hr);
		goto exit;
	}

	/* Open the inbox */
	hr = lpMDB->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, reinterpret_cast<LPUNKNOWN *>(&lpInbox));
	if (hr != hrSuccess || ulObjType != MAPI_FOLDER) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open inbox folder, error code: 0x%08X", hr);
		if(ulObjType != MAPI_FOLDER)
			hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	/* Create a new message in the correct folder */
	hr = lpInbox->CreateMessage(NULL, 0, &lpMessage);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to create new message, error code: %08X", hr);
		goto exit;
	}

	hr = lpMessage->SetProps(cPropSize, lpPropArray, NULL);
	if(hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to set properties, error code: 0x%08X", hr);
		goto exit;
	}

	hr = lpMessage->ModifyRecipients(MODRECIP_ADD, lpAddrList);
	if (hr != hrSuccess)
		goto exit;

	/* Save Message */
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		goto exit;

	hr = HrNewMailNotification(lpMDB, lpMessage);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to send 'New Mail' notification, error code: 0x%08X", hr);
		goto exit;
	}

	hr = lpMDB->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		goto exit;

exit:
	if (lpInbox)
		lpInbox->Release();
	MAPIFreeBuffer(lpEntryID);
	if(lpMessage)
		lpMessage->Release();

	return hr;
}

/**
 * Creates one quota mail
 *
 * @param[in]	lpVars	Template variables values
 * @param[in]	lpMDB	The store of the user in lpecToUser to create the mail in
 * @param[in]	lpecToUser		Kopano user information, will be placed in the To of the mail
 * @param[in]	lpecFromUser	Kopano user information, will be placed in the From of the mail
 * @param[in]	lpAddrList		Rows to set in the recipient table of the mail
 * @return MAPI error code
 */
HRESULT ECQuotaMonitor::CreateQuotaWarningMail(TemplateVariables *lpVars,
    IMsgStore* lpMDB, ECUSER *lpecToUser, ECUSER *lpecFromUser,
    LPADRLIST lpAddrList)
{
	HRESULT hr = hrSuccess;
	ULONG cPropSize = 0;
	LPSPropValue lpPropArray = NULL;
	string strSubject;
	string strBody;

	hr = CreateMailFromTemplate(lpVars, &strSubject, &strBody);
	if (hr != hrSuccess)
		goto exit;

	hr = CreateMessageProperties(lpecToUser, lpecFromUser, strSubject, strBody, &cPropSize, &lpPropArray);
	if (hr != hrSuccess)
		goto exit;

	hr = SendQuotaWarningMail(lpMDB, cPropSize, lpPropArray, lpAddrList);
	if (hr != hrSuccess)
		goto exit;

	m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_NOTICE, "Mail delivered to user %s", (LPSTR)lpecToUser->lpszUsername);

exit:
	MAPIFreeBuffer(lpPropArray);
	return hr;
}

/** 
 * Opens the store of a company or user
 * 
 * @param[in] szStoreName company or user name
 * @param[out] lppStore opened store
 * 
 * @return MAPI Error code
 */
HRESULT ECQuotaMonitor::OpenUserStore(LPTSTR szStoreName, objectclass_t objclass, LPMDB *lppStore)
{
	HRESULT hr;
	ExchangeManageStorePtr ptrEMS;
	ULONG cbUserStoreEntryID = 0;
	EntryIdPtr ptrUserStoreEntryID;
	MsgStorePtr ptrStore;

	hr = m_lpMDBAdmin->QueryInterface(IID_IExchangeManageStore, &ptrEMS);
	if (hr != hrSuccess)
		return hr;

	hr = ptrEMS->CreateStoreEntryID((LPTSTR)"", szStoreName, OPENSTORE_HOME_LOGON, &cbUserStoreEntryID, &ptrUserStoreEntryID);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_NOT_FOUND)
			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_INFO, "Store of %s \"%s\" not found", (objclass == CONTAINER_COMPANY) ? "company" : "user", reinterpret_cast<const char *>(szStoreName));
		else
			m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get store entry id for \"%s\": %s (0x%08X)", reinterpret_cast<const char *>(szStoreName), GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	hr = m_lpMAPIAdminSession->OpenMsgStore(0, cbUserStoreEntryID, ptrUserStoreEntryID, NULL, MDB_WRITE, lppStore);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open store for '%s', error code: 0x%08X", (LPSTR)szStoreName, hr);
		return hr;
	}
	return hrSuccess;
}

/** 
 * Check the last mail time for the quota message.
 * 
 * @param lpStore Store that is over quota
 * 
 * @retval hrSuccess User should not receive quota message
 * @retval MAPI_E_TIMEOUT User should receive quota message
 * @return MAPI Error code
 */
HRESULT ECQuotaMonitor::CheckQuotaInterval(LPMDB lpStore, LPMESSAGE *lppMessage, bool *lpbTimeout)
{
	HRESULT hr;
	MessagePtr ptrMessage;
	SPropValuePtr ptrProp;
	const char *lpResendInterval = NULL;
	ULONG ulResendInterval = 0;
	FILETIME ft;
	FILETIME ftNextRun;

	hr = GetConfigMessage(lpStore, QUOTA_CONFIG_MSG, &ptrMessage);
	if (hr != hrSuccess)
		return hr;

	hr = HrGetOneProp(ptrMessage, PR_EC_QUOTA_MAIL_TIME, &ptrProp);
	if (hr == MAPI_E_NOT_FOUND) {
		*lppMessage = ptrMessage.release();
		*lpbTimeout = true;
		return hrSuccess;
	}
	if (hr != hrSuccess)
		return hr;

	/* Determine when the last warning mail was send, and if a new one should be send. */
	lpResendInterval = m_lpThreadMonitor->lpConfig->GetSetting("mailquota_resend_interval");
	ulResendInterval = (lpResendInterval && atoui(lpResendInterval) > 0) ? atoui(lpResendInterval) : 1;
	GetSystemTimeAsFileTime(&ft);

	UnixTimeToFileTime(FileTimeToUnixTime(ptrProp->Value.ft.dwHighDateTime, ptrProp->Value.ft.dwLowDateTime) +
					   (ulResendInterval * 60 * 60 * 24) -(2 * 60), &ftNextRun);

	*lppMessage = ptrMessage.release();
	*lpbTimeout = (ft > ftNextRun);
	return hrSuccess;
}

/**
 * Writes the new timestamp in the store to now() when the last quota
 * mail was send to this store.  The store must have been opened as
 * SYSTEM user, which can always write, eventhough the store is over
 * quota.
 *
 * @param[in]	lpMDB	Store to update the last quota send timestamp in
 * @return MAPI error code
 */
HRESULT ECQuotaMonitor::UpdateQuotaTimestamp(LPMESSAGE lpMessage)
{
	HRESULT hr;
	SPropValue sPropTime;
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);

	sPropTime.ulPropTag = PR_EC_QUOTA_MAIL_TIME;
	sPropTime.Value.ft = ft;

	hr = HrSetOneProp(lpMessage, &sPropTime);
	if (hr != hrSuccess)
		return hr;
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to save config message, error code: 0x%08X", hr);
		return hr;
	}
	return hrSuccess;
}

/**
 * Sends the quota mail to the user, and to any administrator listed
 * to receive quota information within the company space.
 *
 * @param[in]	lpecUser	The Kopano user who is over quota, NULL if the company is over quota
 * @param[in]	lpecCompany	The Kopano company of the lpecUser (default company if non-hosted), or the over quota company if lpecUser is NULL
 * @param[in]	lpecQuotaStatus	The quota status values of lpecUser or lpecCompany
 * @param[in]	lpStore The store that is over quota
 * @return MAPI error code
 */
HRESULT ECQuotaMonitor::Notify(ECUSER *lpecUser, ECCOMPANY *lpecCompany,
    ECQUOTASTATUS *lpecQuotaStatus, LPMDB lpStore)
{
	HRESULT hr = hrSuccess;
	IECServiceAdmin *lpServiceAdmin = NULL;
	MsgStorePtr ptrRecipStore;
	MAPIFolderPtr ptrRoot;
	MessagePtr ptrQuotaTSMessage;
	bool bTimeout;
	LPSPropValue lpsObject = NULL;
	LPADRLIST lpAddrList = NULL;
	ECUSER *lpecFromUser = NULL;
	ULONG cToUsers = 0;
	ECUSER *lpToUsers = NULL;
	ECQUOTA *lpecQuota = NULL;
	ULONG cbUserId = 0;
	LPENTRYID lpUserId = NULL;
	struct TemplateVariables sVars;

	// check if we need to send the actual email
	hr = CheckQuotaInterval(lpStore, &ptrQuotaTSMessage, &bTimeout);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to query mail timeout value: 0x%08X", hr);
		goto exit;
	}
	if (!bTimeout) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_INFO, "Not sending message since the warning mail has already been sent in the past time interval");
		goto exit;
	}

	hr = HrGetOneProp(m_lpMDBAdmin, PR_EC_OBJECT, &lpsObject);
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get internal object, error code: 0x%08X", hr);
		goto exit;
	}

	hr = reinterpret_cast<IECUnknown *>(lpsObject->Value.lpszA)->QueryInterface(IID_IECServiceAdmin, reinterpret_cast<void **>(&lpServiceAdmin));
	if (hr != hrSuccess) {
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to get service admin, error code: 0x%08X", hr);
		goto exit;
	}

	hr = lpServiceAdmin->GetUser(lpecCompany->sAdministrator.cb, (LPENTRYID)lpecCompany->sAdministrator.lpb, 0, &lpecFromUser);
	if (hr != hrSuccess)
		goto exit;

	if (lpecUser) {
		cbUserId = lpecUser->sUserId.cb;
		lpUserId = (LPENTRYID)lpecUser->sUserId.lpb;
		sVars.ulClass = ACTIVE_USER;
		sVars.strUserName = (LPSTR)lpecUser->lpszUsername;
		sVars.strFullName = (LPSTR)lpecUser->lpszFullName;
		sVars.strCompany = (LPSTR)lpecCompany->lpszCompanyname;
	} else {
		cbUserId = lpecCompany->sCompanyId.cb;
		lpUserId = (LPENTRYID)lpecCompany->sCompanyId.lpb;
		sVars.ulClass = CONTAINER_COMPANY;
		sVars.strUserName = (LPSTR)lpecCompany->lpszCompanyname;
		sVars.strFullName = (LPSTR)lpecCompany->lpszCompanyname;
		sVars.strCompany = (LPSTR)lpecCompany->lpszCompanyname;
	}

	hr = lpServiceAdmin->GetQuota(cbUserId, lpUserId, false, &lpecQuota);
	if (hr != hrSuccess)
		goto exit;

	sVars.ulStatus = lpecQuotaStatus->quotaStatus;
	sVars.strStoreSize = str_storage(lpecQuotaStatus->llStoreSize);
	sVars.strWarnSize = str_storage(lpecQuota->llWarnSize);
	sVars.strSoftSize = str_storage(lpecQuota->llSoftSize);
	sVars.strHardSize = str_storage(lpecQuota->llHardSize);

	hr = lpServiceAdmin->GetQuotaRecipients(cbUserId, lpUserId, 0, &cToUsers, &lpToUsers);
	if (hr != hrSuccess)
		goto exit;

	hr = CreateRecipientList(cToUsers, lpToUsers, &lpAddrList);
	if (hr != hrSuccess)
		goto exit;

	/* Go through all stores to deliver the mail to all recipients.
	 *
	 * Note that we will parse the template for each recipient separately,
	 * this is done to support better language support later on where each user
	 * will get a notification mail in his prefered language.
	 */
	for (ULONG i = 0; i < cToUsers; ++i) {
		/* Company quota's shouldn't deliver to the first entry since that is the public store. */
		if (i == 0 && sVars.ulClass == CONTAINER_COMPANY) {
			if (cToUsers == 1)
				m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_ERROR, "No quota recipients for over quota company %s", (LPSTR)lpecCompany->lpszCompanyname);
			continue;
		}

		if (OpenUserStore(lpToUsers[i].lpszUsername, sVars.ulClass, &ptrRecipStore) != hrSuccess)
			continue;

		CreateQuotaWarningMail(&sVars, ptrRecipStore, &lpToUsers[i], lpecFromUser, lpAddrList);
	}

	if (UpdateQuotaTimestamp(ptrQuotaTSMessage) != hrSuccess)
		m_lpThreadMonitor->lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to update last mail quota timestamp: 0x%08X", hr);

exit:
	MAPIFreeBuffer(lpecFromUser);
	MAPIFreeBuffer(lpToUsers);
	MAPIFreeBuffer(lpecQuota);
	if (lpAddrList)
		FreePadrlist(lpAddrList);

	if (lpServiceAdmin)
		lpServiceAdmin->Release();
	MAPIFreeBuffer(lpsObject);
	return hr;
}
