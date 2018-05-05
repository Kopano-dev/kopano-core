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

#ifndef GATEWAY_COMMON_H
#define GATEWAY_COMMON_H

#include <kopano/zcdefs.h>
#include <memory>
#include <string>
#include <utility>
#include <kopano/ECChannel.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>

#define LOGIN_RETRIES 5

class ClientProto {
public:
	ClientProto(const char *szServerPath, KC::ECChannel *lpChannel, std::shared_ptr<KC::ECConfig> cfg) :
		m_strPath(szServerPath), lpChannel(lpChannel), lpConfig(std::move(cfg)), m_ulFailedLogins(0)
	{};
	virtual ~ClientProto(void) = default;
	virtual int getTimeoutMinutes() const = 0;
	virtual bool isContinue() const { return false; }; // imap only

	virtual HRESULT HrSendGreeting(const std::string &strHostString) = 0;
	virtual HRESULT HrCloseConnection(const std::string &strQuitMsg) = 0;
	virtual HRESULT HrProcessCommand(const std::string &strInput) = 0;
	virtual HRESULT HrProcessContinue(const std::string &strInput) { return MAPI_E_NO_SUPPORT; }; // imap only
	virtual HRESULT HrDone(bool bSendResponse) = 0;

protected:
	std::string	m_strPath;
	KC::ECChannel *lpChannel;
	std::shared_ptr<KC::ECConfig> lpConfig;
	ULONG		m_ulFailedLogins;
};

#endif
