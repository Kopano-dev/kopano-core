/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <kopano/ECChannel.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>

#define LOGIN_RETRIES 5

class ClientProto {
public:
	ClientProto(const char *szServerPath, std::shared_ptr<KC::ECChannel> &&ch, std::shared_ptr<KC::ECConfig> cfg) :
		m_strPath(szServerPath), lpChannel(std::move(ch)),
		lpConfig(std::move(cfg)), m_ulFailedLogins(0)
	{};
	virtual ~ClientProto() = default;
	virtual int getTimeoutMinutes() const = 0;
	virtual bool isAuthenticated() const = 0;
	virtual bool isContinue() const { return false; }; // imap only
	virtual HRESULT HrSendGreeting(const KC::string_view &host) = 0;
	virtual HRESULT HrCloseConnection(const std::string &strQuitMsg) = 0;
	virtual HRESULT HrProcessCommand(const std::string &strInput) = 0;
	virtual HRESULT HrProcessContinue(const std::string &strInput) { return MAPI_E_NO_SUPPORT; }; // imap only
	virtual HRESULT HrDone(bool bSendResponse) = 0;

protected:
	std::string	m_strPath;
	std::shared_ptr<KC::ECChannel> lpChannel;
	std::shared_ptr<KC::ECConfig> lpConfig;
	ULONG		m_ulFailedLogins;
};
