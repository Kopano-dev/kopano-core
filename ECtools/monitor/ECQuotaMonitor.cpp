/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <kopano/memory.hpp>
#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>
#include <edkguid.h>
#include <edkmdb.h>
#include <kopano/ECDefs.h>
#include <kopano/ECRestriction.h>
#include <kopano/ECABEntryID.h>
#include <kopano/Util.h>
#include <kopano/ecversion.h>
#include <kopano/charset/convert.h>
#include <kopano/mapi_ptr.h>
#include <kopano/MAPIErrors.h>
#include <kopano/ECGuid.h>
#include <kopano/ECTags.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/stringutil.h>
#include <kopano/timeutil.hpp>
#include <kopano/mapiext.h>
#include "ECMonitorDefs.h"
#include "ECQuotaMonitor.h"
#include <set>
#include <string>

using namespace KC;
using std::set;
using std::string;

#define QUOTA_CONFIG_MSG "Kopano.Quota"

/**
 * Takes an extra reference to the passed MAPI objects which have refcounting.
 */
ECQuotaMonitor::ECQuotaMonitor(ECTHREADMONITOR *lpThreadMonitor,
    LPMAPISESSION lpMAPIAdminSession, LPMDB lpMDBAdmin) :
	m_lpThreadMonitor(lpThreadMonitor),
	m_lpMAPIAdminSession(lpMAPIAdminSession), m_lpMDBAdmin(lpMDBAdmin)
{
}

/** Creates ECQuotaMonitor object and calls
 * ECQuotaMonitor::CheckQuota(). Entry point for this class.
 *
 * @param[in] lpVoid ECTHREADMONITOR struct
 * @return NULL
 */
void* ECQuotaMonitor::Create(void* lpVoid)
{
	auto lpThreadMonitor = static_cast<ECTHREADMONITOR *>(lpVoid);
	std::unique_ptr<ECQuotaMonitor> lpecQuotaMonitor;
	object_ptr<IMAPISession> lpMAPIAdminSession;
	object_ptr<IMsgStore> lpMDBAdmin;

	const char *lpPath = lpThreadMonitor->lpConfig->GetSetting("server_socket");
	ec_log_info("Quota monitor starting");

	//Open admin session
	auto hr = HrOpenECAdminSession(&~lpMAPIAdminSession, PROJECT_VERSION,
	          "monitor:create", lpPath, 0,
	          lpThreadMonitor->lpConfig->GetSetting("sslkey_file", "", nullptr),
	          lpThreadMonitor->lpConfig->GetSetting("sslkey_pass", "", nullptr));
	if (hr != hrSuccess) {
		kc_perror("Unable to open an admin session", hr);
		return NULL;
	}

	ec_log_info("Connection to storage server succeeded");
	// Open admin store
	hr = HrOpenDefaultStore(lpMAPIAdminSession, &~lpMDBAdmin);
	if (hr != hrSuccess) {
		ec_log_err("Unable to open default store for system account");
		return NULL;
	}

	lpecQuotaMonitor.reset(new ECQuotaMonitor(lpThreadMonitor, lpMAPIAdminSession, lpMDBAdmin));

	// Check the quota of all stores
	auto tmStart = GetProcessTime();
	hr = lpecQuotaMonitor->CheckQuota();
	auto tmEnd = GetProcessTime();
	if(hr != hrSuccess)
		kc_perror("Quota monitor failed", hr);
	else
		ec_log_info("Quota monitor done in %lu seconds. Processed: %u, Failed: %u", tmEnd - tmStart, lpecQuotaMonitor->m_ulProcessed, lpecQuotaMonitor->m_ulFailed);
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
	/* Service object */
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<SPropValue> lpsObject;
	object_ptr<IExchangeManageStore> lpIEMS;

	/* Companylist */
	ECCOMPANY *lpsCompanyList = NULL;
	memory_ptr<ECCOMPANY> lpsCompanyListAlloc;
	ECCOMPANY			sRootCompany = {{g_cbSystemEid, g_lpSystemEid}, (LPTSTR)"Default", NULL, {0, NULL}};
    ULONG				cCompanies = 0;

	/* Quota information */
	memory_ptr<ECQUOTA> lpsQuota;
	memory_ptr<ECQUOTASTATUS> lpsQuotaStatus;

	/* Obtain Service object */
	auto hr = HrGetOneProp(m_lpMDBAdmin, PR_EC_OBJECT, &~lpsObject);
	if (hr != hrSuccess)
		return kc_perror("Unable to get internal object", hr);
	hr = reinterpret_cast<IUnknown *>(lpsObject->Value.lpszA)->QueryInterface(IID_IECServiceAdmin, &~lpServiceAdmin);
	if (hr != hrSuccess)
		return kc_perror("Unable to get service admin", hr);
	/* Get companylist */
	hr = lpServiceAdmin->GetCompanyList(0, &cCompanies, &~lpsCompanyListAlloc);
	if (hr == MAPI_E_NO_SUPPORT) {
		lpsCompanyList = &sRootCompany;
		cCompanies = 1;
	} else if (hr != hrSuccess) {
		return kc_perror("Unable to get companylist", hr);
	} else
		lpsCompanyList = lpsCompanyListAlloc;

	hr = m_lpMDBAdmin->QueryInterface(IID_IExchangeManageStore, &~lpIEMS);
	if (hr != hrSuccess)
		return kc_perror("Unable to get admin interface", hr);

	for (ULONG i = 0; i < cCompanies; ++i) {
		/* Check company quota for non-default company */
		if (lpsCompanyList[i].sCompanyId.cb != 0 && lpsCompanyList[i].sCompanyId.lpb != NULL) {
			/* Company store */
			object_ptr<IMsgStore> lpMsgStore;

			++m_ulProcessed;
			hr = lpServiceAdmin->GetQuota(lpsCompanyList[i].sCompanyId.cb, (LPENTRYID)lpsCompanyList[i].sCompanyId.lpb, false, &~lpsQuota);
			if (hr != hrSuccess) {
				ec_log_err("Unable to get quota information for company \"%s\": %s (%x)",
					reinterpret_cast<const char *>(lpsCompanyList[i].lpszCompanyname),
					GetMAPIErrorMessage(hr), hr);
				++m_ulFailed;
				goto check_stores;
			}
			hr = OpenUserStore(lpsCompanyList[i].lpszCompanyname, CONTAINER_COMPANY, &~lpMsgStore);
			if (hr != hrSuccess) {
				++m_ulFailed;
				goto check_stores;
			}
			hr = Util::HrGetQuotaStatus(lpMsgStore, lpsQuota, &~lpsQuotaStatus);
			if (hr != hrSuccess) {
				ec_log_err("Unable to get quotastatus for company \"%s\": %s (%x)",
					reinterpret_cast<const char *>(lpsCompanyList[i].lpszCompanyname),
					GetMAPIErrorMessage(hr), hr);
				++m_ulFailed;
				goto check_stores;
			}

			if (lpsQuotaStatus->quotaStatus != QUOTA_OK) {
				ec_log_err("Storage size of company \"%s\": %s (%x)",
					reinterpret_cast<const char *>(lpsCompanyList[i].lpszCompanyname),
					GetMAPIErrorMessage(hr), hr);
				Notify(NULL, &lpsCompanyList[i], lpsQuotaStatus, lpMsgStore);
			}
		}

check_stores:
		/* Whatever the status of the company quota, we should also check the quota of the users */
		CheckCompanyQuota(&lpsCompanyList[i]);
	}
	return hrSuccess;
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
	/* Service object */
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<SPropValue> lpsObject;
	/* Userlist */
	memory_ptr<ECUSER> lpsUserList;
	ULONG				cUsers = 0;

	set<string> setServers;
	std::set<string, strcasecmp_comparison> setServersConfig;
	memory_ptr<char> lpszConnection;
	bool bIsPeer = false;
	ec_log_info("Checking quota for company \"%s\"", reinterpret_cast<const char *>(lpecCompany->lpszCompanyname));

	/* Obtain Service object */
	auto hr = HrGetOneProp(m_lpMDBAdmin, PR_EC_OBJECT, &~lpsObject);
	if (hr != hrSuccess)
		return kc_perror("Unable to get internal object", hr);
	hr = reinterpret_cast<IUnknown *>(lpsObject->Value.lpszA)->QueryInterface(IID_IECServiceAdmin, &~lpServiceAdmin);
	if (hr != hrSuccess)
		return kc_perror("Unable to get service admin", hr);
	/* Get userlist */
	hr = lpServiceAdmin->GetUserList(lpecCompany->sCompanyId.cb, (LPENTRYID)lpecCompany->sCompanyId.lpb, 0, &cUsers, &~lpsUserList);
	if (hr != hrSuccess) {
		ec_log_err("Unable to get userlist for company \"%s\": %s (%x)",
			reinterpret_cast<const char *>(lpecCompany->lpszCompanyname),
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	for (ULONG i = 0; i < cUsers; ++i)
		if (lpsUserList[i].lpszServername && lpsUserList[i].lpszServername[0] != '\0')
			setServers.emplace(reinterpret_cast<const char *>(lpsUserList[i].lpszServername));

	if (setServers.empty()) {
		// call server function with current lpMDBAdmin / lpServiceAdmin
		hr = CheckServerQuota(cUsers, lpsUserList, lpecCompany, m_lpMDBAdmin);
		if (hr != hrSuccess)
			return kc_perror("Unable to check server quota", hr);
		return hrSuccess;
	}

	auto lpszServersConfig = m_lpThreadMonitor->lpConfig->GetSetting("servers", "", nullptr);
	if(lpszServersConfig) {
		// split approach taken from kopano-backup/backup.cpp
		std::vector<std::string> ddv = tokenize(lpszServersConfig, "\t ");
		setServersConfig.insert(std::make_move_iterator(ddv.begin()), std::make_move_iterator(ddv.end()));
	}

	for (const auto &server : setServers) {
		if (!setServersConfig.empty() &&
		    setServersConfig.find(server.c_str()) == setServersConfig.cend())
			continue;
		hr = lpServiceAdmin->ResolvePseudoUrl(std::string("pseudo://" + server).c_str(), &~lpszConnection, &bIsPeer);
		if (hr != hrSuccess) {
			ec_log_err("Unable to resolve servername \"%s\": %s (%x)",
				server.c_str(), GetMAPIErrorMessage(hr), hr);
			++m_ulFailed;
			continue;
		}

		ec_log_info("Connecting to server \"%s\" using URL \"%s\"", server.c_str(), lpszConnection.get());

		// call server function with new lpMDBAdmin / lpServiceAdmin
		/* 2nd Server connection */
		object_ptr<IMAPISession> lpSession;
		object_ptr<IMsgStore> lpAdminStore;

		if (bIsPeer) {
			// query interface
			hr = m_lpMDBAdmin->QueryInterface(IID_IMsgStore, &~lpAdminStore);
			if (hr != hrSuccess) {
				kc_perror("Unable to get service interface again", hr);
				++m_ulFailed;
				continue;
			}
		} else {
			hr = HrOpenECAdminSession(&~lpSession, PROJECT_VERSION,
			     "monitor:check-company", lpszConnection, 0,
			     m_lpThreadMonitor->lpConfig->GetSetting("sslkey_file", "", nullptr),
			     m_lpThreadMonitor->lpConfig->GetSetting("sslkey_pass", "", nullptr));
			if (hr != hrSuccess) {
				ec_log_err("Unable to connect to server \"%s\": %s (%x)",
					lpszConnection.get(), GetMAPIErrorMessage(hr), hr);
				++m_ulFailed;
				continue;
			}
			hr = HrOpenDefaultStore(lpSession, &~lpAdminStore);
			if (hr != hrSuccess) {
				ec_log_err("Unable to open admin store on server \"%s\": %s (%x)",
					lpszConnection.get(), GetMAPIErrorMessage(hr), hr);
				++m_ulFailed;
				continue;
			}
		}

		hr = CheckServerQuota(cUsers, lpsUserList, lpecCompany, lpAdminStore);
		if (hr != hrSuccess) {
			ec_log_err("Unable to check quota on server \"%s\": %s (%x)",
				lpszConnection.get(), GetMAPIErrorMessage(hr), hr);
			++m_ulFailed;
		}
	}
	return hrSuccess;
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
	SPropValue sRestrictProp;
	object_ptr<IMAPITable> lpTable;
	ECQUOTASTATUS sQuotaStatus;
	ULONG i, u;
	static constexpr const SizedSPropTagArray(5, sCols) =
		{5, {PR_EC_USERNAME_A, PR_MESSAGE_SIZE_EXTENDED,
		PR_QUOTA_WARNING_THRESHOLD, PR_QUOTA_SEND_THRESHOLD,
		PR_QUOTA_RECEIVE_THRESHOLD}};

	auto hr = lpAdminStore->OpenProperty(PR_EC_STATSTABLE_USERS, &IID_IMAPITable, 0, 0, &~lpTable);
	if (hr != hrSuccess)
		return kc_perror("Unable to open stats table for quota sizes", hr);
	hr = lpTable->SetColumns(sCols, MAPI_DEFERRED_ERRORS);
	if (hr != hrSuccess)
		return kc_perror("Unable to set columns on stats table for quota sizes", hr);

	if (lpecCompany->sCompanyId.cb != 0 && lpecCompany->sCompanyId.lpb != NULL) {
		sRestrictProp.ulPropTag = PR_EC_COMPANY_NAME_A;
		sRestrictProp.Value.lpszA = (char*)lpecCompany->lpszCompanyname;

		memory_ptr<SRestriction> lpsRestriction;
		hr = ECOrRestriction(
			ECNotRestriction(ECExistRestriction(PR_EC_COMPANY_NAME_A)) +
			ECPropertyRestriction(RELOP_EQ, PR_EC_COMPANY_NAME_A, &sRestrictProp, ECRestriction::Cheap)
		).CreateMAPIRestriction(&~lpsRestriction, ECRestriction::Cheap);
		if (hr != hrSuccess)
			return hr;
		hr = lpTable->Restrict(lpsRestriction, MAPI_DEFERRED_ERRORS);
		if (hr != hrSuccess)
			return kc_perror("Unable to restrict stats table", hr);
	}

	while (TRUE) {
		rowset_ptr lpRowSet;
		hr = lpTable->QueryRows(50, 0, &~lpRowSet);
		if (hr != hrSuccess)
			return kc_perror("Unable to receive stats table data", hr);
		if (lpRowSet->cRows == 0)
			break;

		for (i = 0; i < lpRowSet->cRows; ++i) {
			MsgStorePtr ptrStore;
			auto lpUsername  = lpRowSet[i].cfind(PR_EC_USERNAME_A);
			auto lpStoreSize = lpRowSet[i].cfind(PR_MESSAGE_SIZE_EXTENDED);
			auto lpQuotaWarn = lpRowSet[i].cfind(PR_QUOTA_WARNING_THRESHOLD);
			auto lpQuotaSoft = lpRowSet[i].cfind(PR_QUOTA_SEND_THRESHOLD);
			auto lpQuotaHard = lpRowSet[i].cfind(PR_QUOTA_RECEIVE_THRESHOLD);
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

			ec_log_err("Mailbox of user \"%s\" has exceeded its %s limit", lpUsername->Value.lpszA, sQuotaStatus.quotaStatus == QUOTA_WARN ? "warning" : sQuotaStatus.quotaStatus == QUOTA_SOFTLIMIT ? "soft" : "hard");
			// find the user in the full users list
			for (u = 0; u < cUsers; ++u) {
				if (strcmp((char*)lpsUserList[u].lpszUsername, lpUsername->Value.lpszA) == 0)
					break;
			}
			if (u == cUsers) {
				ec_log_err("Unable to find user \"%s\" in userlist", lpUsername->Value.lpszA);
				++m_ulFailed;
				continue;
			}
			hr = OpenUserStore(lpsUserList[u].lpszUsername, ACTIVE_USER, &~ptrStore);
			if (hr != hrSuccess)
				continue;
			hr = Notify(&lpsUserList[u], lpecCompany, &sQuotaStatus, ptrStore);
			if (hr != hrSuccess)
				++m_ulFailed;
		}
	}
	return hrSuccess;
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
	char cBuffer[TEMPLATE_LINE_LENGTH];
	std::string strSubject, strBody;
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

	auto lpszTemplate = m_lpThreadMonitor->lpConfig->GetSetting(strTemplateConfig.c_str());

	/* Start reading the template mail */
	auto fp = fopen(lpszTemplate, "rt");
	if (fp == nullptr) {
		ec_log_err("Failed to open template email file \"%s\": %s", lpszTemplate, strerror(errno));
		return MAPI_E_NOT_FOUND;
	}

	while(!feof(fp)) {
		memset(&cBuffer, 0, sizeof(cBuffer));

		if (!fgets(cBuffer, sizeof(cBuffer), fp))
			break;

		std::string strLine(cBuffer);
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

	*lpstrSubject = std::move(strSubject);
	*lpstrBody = std::move(strBody);
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
	memory_ptr<SPropValue> lpPropArray;
	ULONG cbFromEntryid = 0;
	memory_ptr<ENTRYID> lpFromEntryid, lpToEntryid;
    ULONG cbToEntryid = 0;
	ULONG cbFromSearchKey = 0;
	memory_ptr<unsigned char> lpFromSearchKey, lpToSearchKey;
	ULONG cbToSearchKey = 0;
	ULONG ulPropArrayMax = 50;
	ULONG ulPropArrayCur = 0;
	FILETIME ft;
	std::wstring name, email;
	convert_context converter;

	/* We are almost there, we have the mail and the recipients. Now we should create the Message */
	auto hr = MAPIAllocateBuffer(sizeof(SPropValue) * ulPropArrayMax, &~lpPropArray);
	if (hr != hrSuccess)
		return hr;
	if (TryConvert(converter, (char*)lpecToUser->lpszFullName, name) != hrSuccess) {
		ec_log_err("Unable to convert To name \"%s\" to widechar, using empty name in entryid", reinterpret_cast<const char *>(lpecToUser->lpszFullName));
		name.clear();
	}
	if (TryConvert(converter, (char*)lpecToUser->lpszMailAddress, email) != hrSuccess) {
		ec_log_err("Unable to convert To email \"%s\" to widechar, using empty name in entryid", reinterpret_cast<const char *>(lpecToUser->lpszMailAddress));
		email.clear();
	}
	hr = ECCreateOneOff((LPTSTR)name.c_str(), (LPTSTR)L"SMTP", (LPTSTR)email.c_str(), MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbToEntryid, &~lpToEntryid);
	if (hr != hrSuccess)
		return kc_perror("Failed creating one-off address", hr);
	hr = HrCreateEmailSearchKey("SMTP", (char*)lpecToUser->lpszMailAddress, &cbToSearchKey, &~lpToSearchKey);
	if (hr != hrSuccess)
		return kc_perror("Failed creating email searchkey", hr);
	if (TryConvert(converter, (char*)lpecFromUser->lpszFullName, name) != hrSuccess) {
		ec_log_err("Unable to convert From name \"%s\" to widechar, using empty name in entryid", reinterpret_cast<const char *>(lpecFromUser->lpszFullName));
		name.clear();
	}
	if (TryConvert(converter, (char*)lpecFromUser->lpszMailAddress, email) != hrSuccess) {
		ec_log_err("Unable to convert From email \"%s\" to widechar, using empty name in entryid", reinterpret_cast<const char *>(lpecFromUser->lpszMailAddress));
		email.clear();
	}
	hr = ECCreateOneOff((LPTSTR)name.c_str(), (LPTSTR)L"SMTP", (LPTSTR)email.c_str(), MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbFromEntryid, &~lpFromEntryid);
	if (hr != hrSuccess)
		return kc_perror("Failed creating one-off address", hr);
	hr = HrCreateEmailSearchKey("SMTP", (char*)lpecFromUser->lpszMailAddress, &cbFromSearchKey, &~lpFromSearchKey);
	if (hr != hrSuccess)
		return kc_perror("Failed creating email searchkey", hr);

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
	hr = KAllocCopy(lpToEntryid, cbToEntryid, reinterpret_cast<void **>(&lpPropArray[ulPropArrayCur].Value.bin.lpb), lpPropArray);
	if (hr != hrSuccess)
		return hr;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RCVD_REPRESENTING_ENTRYID;
	lpPropArray[ulPropArrayCur++].Value.bin.cb = cbToEntryid;
	lpPropArray[ulPropArrayCur].ulPropTag = PR_RCVD_REPRESENTING_NAME_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecToUser->lpszFullName ? (LPSTR)lpecToUser->lpszFullName : (LPSTR)"");
	hr = KAllocCopy(lpToSearchKey, cbToSearchKey, reinterpret_cast<void **>(&lpPropArray[ulPropArrayCur].Value.bin.lpb), lpPropArray);
	if (hr != hrSuccess)
		return hr;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RCVD_REPRESENTING_SEARCH_KEY;
	lpPropArray[ulPropArrayCur++].Value.bin.cb = cbToSearchKey;

	/* RECEIVED_BY_* properties */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_ADDRTYPE_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = const_cast<char *>("SMTP");

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_EMAIL_ADDRESS_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecToUser->lpszMailAddress ? (LPSTR)lpecToUser->lpszMailAddress : (LPSTR)"");
	hr = KAllocCopy(lpToEntryid, cbToEntryid, reinterpret_cast<void **>(&lpPropArray[ulPropArrayCur].Value.bin.lpb), lpPropArray);
	if (hr != hrSuccess)
		return hr;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_ENTRYID;
	lpPropArray[ulPropArrayCur++].Value.bin.cb = cbToEntryid;
	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_NAME_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecToUser->lpszFullName ? (LPSTR)lpecToUser->lpszFullName : (LPSTR)"");
	hr = KAllocCopy(lpToSearchKey, cbToSearchKey, reinterpret_cast<void **>(&lpPropArray[ulPropArrayCur].Value.bin.lpb), lpPropArray);
	if (hr != hrSuccess)
		return hr;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_RECEIVED_BY_SEARCH_KEY;
	lpPropArray[ulPropArrayCur++].Value.bin.cb = cbToSearchKey;

	/* System user, PR_SENDER* */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_ADDRTYPE_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = const_cast<char *>("SMTP");

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_EMAIL_ADDRESS_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecFromUser->lpszMailAddress ? (LPSTR)lpecFromUser->lpszMailAddress : (LPSTR)"");
	hr = KAllocCopy(lpFromEntryid, cbFromEntryid, reinterpret_cast<void **>(&lpPropArray[ulPropArrayCur].Value.bin.lpb), lpPropArray);
	if (hr != hrSuccess)
		return hr;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_ENTRYID;
	lpPropArray[ulPropArrayCur++].Value.bin.cb = cbFromEntryid;
	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_NAME_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecFromUser->lpszFullName ? (LPSTR)lpecFromUser->lpszFullName : (LPSTR)"kopano-system");
	hr = KAllocCopy(lpFromSearchKey, cbFromSearchKey, reinterpret_cast<void **>(&lpPropArray[ulPropArrayCur].Value.bin.lpb), lpPropArray);
	if (hr != hrSuccess)
		return hr;
	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENDER_SEARCH_KEY;
	lpPropArray[ulPropArrayCur++].Value.bin.cb = cbFromSearchKey;

	/* System user, PR_SENT_REPRESENTING* */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_ADDRTYPE_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = const_cast<char *>("SMTP");

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecFromUser->lpszMailAddress ? (LPSTR)lpecFromUser->lpszMailAddress : (LPSTR)"");
	hr = KAllocCopy(lpFromEntryid, cbFromEntryid, reinterpret_cast<void **>(&lpPropArray[ulPropArrayCur].Value.bin.lpb), lpPropArray);
	if (hr != hrSuccess)
		return hr;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_ENTRYID;
	lpPropArray[ulPropArrayCur++].Value.bin.cb = cbFromEntryid;
	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_NAME_A;
	lpPropArray[ulPropArrayCur++].Value.lpszA = (lpecFromUser->lpszFullName ? (LPSTR)lpecFromUser->lpszFullName : (LPSTR)"kopano-system");
	hr = KAllocCopy(lpFromSearchKey, cbFromSearchKey, reinterpret_cast<void **>(&lpPropArray[ulPropArrayCur].Value.bin.lpb), lpPropArray);
	if (hr != hrSuccess)
		return hr;

	lpPropArray[ulPropArrayCur].ulPropTag = PR_SENT_REPRESENTING_SEARCH_KEY;
	lpPropArray[ulPropArrayCur++].Value.bin.cb = cbFromSearchKey;

	/* Get the time to add to the message as PR_CLIENT_SUBMIT_TIME */
	GetSystemTimeAsFileTime(&ft);

	/* Submit time */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_CLIENT_SUBMIT_TIME;
	lpPropArray[ulPropArrayCur++].Value.ft = ft;

	/* Delivery time */
	lpPropArray[ulPropArrayCur].ulPropTag = PR_MESSAGE_DELIVERY_TIME;
	lpPropArray[ulPropArrayCur++].Value.ft = ft;
	assert(ulPropArrayCur <= ulPropArrayMax);
	*lppPropArray = lpPropArray.release();
	*lpcPropSize = ulPropArrayCur;
	return hrSuccess;
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
	adrlist_ptr lpAddrList;
	ULONG cbUserEntryid = 0;
	memory_ptr<ENTRYID> lpUserEntryid;
	ULONG cbUserSearchKey = 0;
	memory_ptr<unsigned char> lpUserSearchKey;

	auto hr = MAPIAllocateBuffer(CbNewADRLIST(cToUsers), &~lpAddrList);
	if(hr != hrSuccess)
		return hr;
	lpAddrList->cEntries = 0;
	for (ULONG i = 0; i < cToUsers; ++i) {
		auto &ent = lpAddrList->aEntries[i];
		ent.cValues = 7;
		hr = MAPIAllocateBuffer(sizeof(SPropValue) * ent.cValues, reinterpret_cast<void **>(&ent.rgPropVals));
		if (hr != hrSuccess)
			return hr;

		hr = ECCreateOneOff((LPTSTR)lpToUsers[i].lpszFullName, (LPTSTR)"SMTP", (LPTSTR)lpToUsers[i].lpszMailAddress,
			MAPI_SEND_NO_RICH_INFO, &cbUserEntryid, &~lpUserEntryid);
		if (hr != hrSuccess)
			return kc_perror("Failed creating one-off address", hr);
		hr = HrCreateEmailSearchKey("SMTP",
		     reinterpret_cast<const char *>(lpToUsers[i].lpszMailAddress),
		     &cbUserSearchKey, &~lpUserSearchKey);
		if (hr != hrSuccess)
			return kc_perror("Failed creating email searchkey", hr);

		auto &pv = ent.rgPropVals;
		pv[0].ulPropTag    = PR_ROWID;
		pv[0].Value.l      = 0;
		pv[1].ulPropTag    = PR_RECIPIENT_TYPE;
		pv[1].Value.l      = (i == 0) ? MAPI_TO : MAPI_CC;
		pv[2].ulPropTag    = PR_DISPLAY_NAME_A;
		pv[2].Value.lpszA  = reinterpret_cast<char *>(lpToUsers[i].lpszFullName != nullptr ? lpToUsers[i].lpszFullName : lpToUsers[i].lpszUsername);
		pv[3].ulPropTag    = PR_EMAIL_ADDRESS_A;
		pv[3].Value.lpszA  = (lpToUsers[i].lpszMailAddress != nullptr) ? reinterpret_cast<char *>(lpToUsers[i].lpszMailAddress) : const_cast<char *>("");
		pv[4].ulPropTag    = PR_ADDRTYPE_A;
		pv[4].Value.lpszA  = const_cast<char *>("SMTP");
		pv[5].ulPropTag    = PR_ENTRYID;
		pv[5].Value.bin.cb = cbUserEntryid;
		hr = KAllocCopy(lpUserEntryid, cbUserEntryid, reinterpret_cast<void **>(&pv[5].Value.bin.lpb), pv);
		if (hr != hrSuccess)
			return hr;
		pv[6].ulPropTag = PR_SEARCH_KEY;
		pv[6].Value.bin.cb = cbUserSearchKey;
		hr = KAllocCopy(lpUserSearchKey, cbUserSearchKey, reinterpret_cast<void **>(&pv[6].Value.bin.lpb), pv);
		if (hr != hrSuccess)
			return hr;
	}

	*lppAddrList = lpAddrList.release();
	return hrSuccess;
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
	object_ptr<IMessage> lpMessage;
	ULONG cbEntryID = 0;
	memory_ptr<ENTRYID> lpEntryID;
	object_ptr<IMAPIFolder> lpInbox;
	ULONG ulObjType;

	/* Get the entry id of the inbox */
	auto hr = lpMDB->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &cbEntryID, &~lpEntryID, nullptr);
	if (hr != hrSuccess)
		return kc_perror("Unable to resolve incoming folder", hr);
	/* Open the inbox */
	hr = lpMDB->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpInbox);
	if (hr != hrSuccess || ulObjType != MAPI_FOLDER) {
		kc_perror("Unable to open inbox folder", hr);
		if(ulObjType != MAPI_FOLDER)
			return MAPI_E_NOT_FOUND;
		return hr;
	}

	/* Create a new message in the correct folder */
	hr = lpInbox->CreateMessage(nullptr, 0, &~lpMessage);
	if (hr != hrSuccess)
		return kc_perror("Unable to create new message", hr);
	hr = lpMessage->SetProps(cPropSize, lpPropArray, NULL);
	if (hr != hrSuccess)
		return kc_perror("Unable to set properties", hr);
	hr = lpMessage->ModifyRecipients(MODRECIP_ADD, lpAddrList);
	if (hr != hrSuccess)
		return kc_pwarn("ModifyRecipients", hr);
	/* Save Message */
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		return hr;
	hr = HrNewMailNotification(lpMDB, lpMessage);
	if (hr != hrSuccess)
		return kc_pwarn("Unable to send \"New Mail\" notification", hr);
	return lpMDB->SaveChanges(KEEP_OPEN_READWRITE);
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
	ULONG cPropSize = 0;
	memory_ptr<SPropValue> lpPropArray;
	std::string strSubject, strBody;

	auto hr = CreateMailFromTemplate(lpVars, &strSubject, &strBody);
	if (hr != hrSuccess)
		return hr;
	hr = CreateMessageProperties(lpecToUser, lpecFromUser, strSubject, strBody, &cPropSize, &~lpPropArray);
	if (hr != hrSuccess)
		return hr;
	hr = SendQuotaWarningMail(lpMDB, cPropSize, lpPropArray, lpAddrList);
	if (hr != hrSuccess)
		return hr;
	ec_log_notice("Mail delivered to user \"%s\"", reinterpret_cast<const char *>(lpecToUser->lpszUsername));
	return hrSuccess;
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
	ExchangeManageStorePtr ptrEMS;
	ULONG cbUserStoreEntryID = 0;
	EntryIdPtr ptrUserStoreEntryID;
	MsgStorePtr ptrStore;

	auto hr = m_lpMDBAdmin->QueryInterface(IID_IExchangeManageStore, &~ptrEMS);
	if (hr != hrSuccess)
		return hr;
	hr = ptrEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(""), szStoreName,
	     OPENSTORE_HOME_LOGON, &cbUserStoreEntryID, &~ptrUserStoreEntryID);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_NOT_FOUND)
			ec_log_info("Store of %s \"%s\" not found", (objclass == CONTAINER_COMPANY) ? "company" : "user", reinterpret_cast<const char *>(szStoreName));
		else
			ec_log_err("Unable to get store entry id for \"%s\": %s (%x)",
				reinterpret_cast<const char *>(szStoreName),
				GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	hr = m_lpMAPIAdminSession->OpenMsgStore(0, cbUserStoreEntryID, ptrUserStoreEntryID, NULL, MDB_WRITE, lppStore);
	if (hr != hrSuccess)
		ec_log_err("Unable to open store for \"%s\": %s (%x)",
			reinterpret_cast<const char *>(szStoreName), GetMAPIErrorMessage(hr), hr);
	return hr;
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
	MessagePtr ptrMessage;
	SPropValuePtr ptrProp;
	FILETIME ft;
	FILETIME ftNextRun;

	auto hr = GetConfigMessage(lpStore, QUOTA_CONFIG_MSG, &~ptrMessage);
	if (hr != hrSuccess)
		return hr;
	hr = HrGetOneProp(ptrMessage, PR_EC_QUOTA_MAIL_TIME, &~ptrProp);
	if (hr == MAPI_E_NOT_FOUND) {
		*lppMessage = ptrMessage.release();
		*lpbTimeout = true;
		return hrSuccess;
	}
	if (hr != hrSuccess)
		return hr;

	/* Determine when the last warning mail was send, and if a new one should be sent. */
	auto lpResendInterval = m_lpThreadMonitor->lpConfig->GetSetting("mailquota_resend_interval");
	ULONG ulResendInterval = (lpResendInterval && atoui(lpResendInterval) > 0) ? atoui(lpResendInterval) : 1;
	GetSystemTimeAsFileTime(&ft);
	ftNextRun = UnixTimeToFileTime(FileTimeToUnixTime(ptrProp->Value.ft) + ulResendInterval * 60 * 60 * 24 - 2 * 60);
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
	SPropValue sPropTime;
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);

	sPropTime.ulPropTag = PR_EC_QUOTA_MAIL_TIME;
	sPropTime.Value.ft = ft;
	auto hr = HrSetOneProp(lpMessage, &sPropTime);
	if (hr != hrSuccess)
		return hr;
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		return kc_perror("Unable to save config message", hr);
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
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	MsgStorePtr ptrRecipStore;
	MAPIFolderPtr ptrRoot;
	MessagePtr ptrQuotaTSMessage;
	bool bTimeout;
	memory_ptr<SPropValue> lpsObject;
	adrlist_ptr lpAddrList;
	memory_ptr<ECUSER> lpecFromUser, lpToUsers;
	ULONG cToUsers = 0;
	memory_ptr<ECQUOTA> lpecQuota;
	ULONG cbUserId = 0;
	LPENTRYID lpUserId = NULL;
	struct TemplateVariables sVars;

	// check if we need to send the actual email
	auto hr = CheckQuotaInterval(lpStore, &~ptrQuotaTSMessage, &bTimeout);
	if (hr != hrSuccess)
		return kc_perror("Unable to query mail timeout value", hr);
	if (!bTimeout) {
		ec_log_info("Not sending message since the warning mail has already been sent in the past time interval");
		return hrSuccess;
	}
	hr = HrGetOneProp(m_lpMDBAdmin, PR_EC_OBJECT, &~lpsObject);
	if (hr != hrSuccess)
		return kc_perror("Unable to get internal object", hr);
	hr = reinterpret_cast<IUnknown *>(lpsObject->Value.lpszA)->QueryInterface(IID_IECServiceAdmin, &~lpServiceAdmin);
	if (hr != hrSuccess)
		return kc_perror("Unable to get service admin", hr);
	hr = lpServiceAdmin->GetUser(lpecCompany->sAdministrator.cb, (LPENTRYID)lpecCompany->sAdministrator.lpb, 0, &~lpecFromUser);
	if (hr != hrSuccess)
		return hr;

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

	hr = lpServiceAdmin->GetQuota(cbUserId, lpUserId, false, &~lpecQuota);
	if (hr != hrSuccess)
		return hr;

	sVars.ulStatus = lpecQuotaStatus->quotaStatus;
	sVars.strStoreSize = str_storage(lpecQuotaStatus->llStoreSize);
	sVars.strWarnSize = str_storage(lpecQuota->llWarnSize);
	sVars.strSoftSize = str_storage(lpecQuota->llSoftSize);
	sVars.strHardSize = str_storage(lpecQuota->llHardSize);

	hr = lpServiceAdmin->GetQuotaRecipients(cbUserId, lpUserId, 0, &cToUsers, &~lpToUsers);
	if (hr != hrSuccess)
		return hr;
	hr = CreateRecipientList(cToUsers, lpToUsers, &~lpAddrList);
	if (hr != hrSuccess)
		return hr;

	/* Go through all stores to deliver the mail to all recipients.
	 *
	 * Note that we will parse the template for each recipient separately,
	 * this is done to support better language support later on where each user
	 * will get a notification mail in his prefered language.
	 */
	for (ULONG i = 0; i < cToUsers; ++i) {
		/* Company quotas should not deliver to the first entry since that is the public store. */
		if (i == 0 && sVars.ulClass == CONTAINER_COMPANY) {
			if (cToUsers == 1)
				ec_log_err("No quota recipients for over quota company \"%s\"", reinterpret_cast<const char *>(lpecCompany->lpszCompanyname));
			continue;
		}
		if (OpenUserStore(lpToUsers[i].lpszUsername, sVars.ulClass, &~ptrRecipStore) != hrSuccess)
			continue;

		CreateQuotaWarningMail(&sVars, ptrRecipStore, &lpToUsers[i], lpecFromUser, lpAddrList);
	}

	if (UpdateQuotaTimestamp(ptrQuotaTSMessage) != hrSuccess)
		kc_perror("Unable to update last mail quota timestamp", hr);
	return hrSuccess;
}
