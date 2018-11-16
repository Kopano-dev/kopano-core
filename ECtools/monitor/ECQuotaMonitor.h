/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECQUOTAMONITOR
#define ECQUOTAMONITOR

#include <kopano/ECDefs.h>

#define TEMPLATE_LINE_LENGTH		1024

struct TemplateVariables {
	KC::objectclass_t ulClass;
	KC::eQuotaStatus ulStatus;
	std::string strUserName;
	std::string strFullName;
	std::string strCompany;
	std::string strStoreSize;
	std::string strWarnSize;
	std::string strSoftSize;
	std::string strHardSize;
};

class ECQuotaMonitor final {
private:
	ECQuotaMonitor(ECTHREADMONITOR *lpThreadMonitor, LPMAPISESSION lpMAPIAdminSession, LPMDB lpMDBAdmin);

public:
	static void* Create(void* lpVoid);
	HRESULT	CheckQuota();
	HRESULT CheckCompanyQuota(KC::ECCOMPANY *);
	HRESULT CheckServerQuota(ULONG cUsers, KC::ECUSER *userlist, KC::ECCOMPANY *, LPMDB lpAdminStore);

private:
	HRESULT CreateMailFromTemplate(TemplateVariables *lpVars, std::string *lpstrSubject, std::string *lpstrBody);
	HRESULT CreateMessageProperties(KC::ECUSER *to, KC::ECUSER *from, const std::string &subj, const std::string &body, ULONG *nprops, SPropValue **props);
	HRESULT CreateRecipientList(ULONG nusers, KC::ECUSER *to, ADRLIST **);
	HRESULT SendQuotaWarningMail(IMsgStore* lpMDB, ULONG cPropSize, LPSPropValue lpPropArray, LPADRLIST lpAddrList);
	HRESULT CreateQuotaWarningMail(TemplateVariables *, IMsgStore *, KC::ECUSER *to, KC::ECUSER *from, ADRLIST *);
	HRESULT OpenUserStore(TCHAR *name, KC::objectclass_t, IMsgStore **);
	HRESULT CheckQuotaInterval(LPMDB lpStore, LPMESSAGE *lppMessage, bool *lpbTimeout);
	HRESULT UpdateQuotaTimestamp(LPMESSAGE lpMessage);
	HRESULT Notify(KC::ECUSER *, KC::ECCOMPANY *, KC::ECQUOTASTATUS *, IMsgStore *);

	ECTHREADMONITOR *m_lpThreadMonitor;
	KC::object_ptr<IMAPISession> m_lpMAPIAdminSession;
	KC::object_ptr<IMsgStore> m_lpMDBAdmin;
	ULONG m_ulProcessed = 0, m_ulFailed = 0;
};

#endif
