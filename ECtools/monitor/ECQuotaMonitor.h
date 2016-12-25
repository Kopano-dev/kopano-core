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

#ifndef ECQUOTAMONITOR
#define ECQUOTAMONITOR

#include <kopano/zcdefs.h>
#include <kopano/ECDefs.h>

#define TEMPLATE_LINE_LENGTH		1024

struct TemplateVariables {
	objectclass_t ulClass;
	eQuotaStatus ulStatus;
	std::string strUserName;
	std::string strFullName;
	std::string strCompany;
	std::string strStoreSize;
	std::string strWarnSize;
	std::string strSoftSize;
	std::string strHardSize;
};

class ECQuotaMonitor _kc_final {
private:
	ECQuotaMonitor(ECTHREADMONITOR *lpThreadMonitor, LPMAPISESSION lpMAPIAdminSession, LPMDB lpMDBAdmin);

public:
	virtual ~ECQuotaMonitor(void);
	static void* Create(void* lpVoid);

	HRESULT	CheckQuota();
	HRESULT CheckCompanyQuota(ECCOMPANY *lpecCompany);
	HRESULT CheckServerQuota(ULONG cUsers, ECUSER *lpsUserList, ECCOMPANY *lpecCompany, LPMDB lpAdminStore);

private:
	HRESULT CreateMailFromTemplate(TemplateVariables *lpVars, std::string *lpstrSubject, std::string *lpstrBody);
	HRESULT CreateMessageProperties(ECUSER *touesr, ECUSER *fromuser, const std::string &subj, const std::string &body, ULONG *lpcPropSize, LPSPropValue *lppPropArray);
	HRESULT CreateRecipientList(ULONG cToUsers, ECUSER *lpToUsers, LPADRLIST *lppAddrList);

	HRESULT SendQuotaWarningMail(IMsgStore* lpMDB, ULONG cPropSize, LPSPropValue lpPropArray, LPADRLIST lpAddrList);

	HRESULT CreateQuotaWarningMail(TemplateVariables *lpVars, IMsgStore* lpMDB, ECUSER *lpecToUser, ECUSER *lpecFromUser, LPADRLIST lpAddrList);

	HRESULT OpenUserStore(LPTSTR szStoreName, objectclass_t objclass, LPMDB *lppStore);
	HRESULT CheckQuotaInterval(LPMDB lpStore, LPMESSAGE *lppMessage, bool *lpbTimeout);
	HRESULT UpdateQuotaTimestamp(LPMESSAGE lpMessage);

	HRESULT Notify(ECUSER *lpecUser, ECCOMPANY *lpecCompany, ECQUOTASTATUS *lpecQuotaStatus, LPMDB lpStore);

private:
	ECTHREADMONITOR *m_lpThreadMonitor;
	LPMAPISESSION		m_lpMAPIAdminSession;
	LPMDB				m_lpMDBAdmin;
	ULONG				m_ulProcessed;
	ULONG				m_ulFailed;
};


#endif
