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

#ifndef POP3_H
#define POP3_H

#include <memory>
#include <vector>
#include <kopano/zcdefs.h>
#include "ClientProto.h"

/**
 * @defgroup gateway_pop3 POP3 
 * @ingroup gateway
 * @{
 */

#define POP3_MAX_RESPONSE_LENGTH 512

#define POP3_RESP_OK "+OK "
#define POP3_RESP_TEMPFAIL "-ERR [SYS/TEMP] "
#define POP3_RESP_PERMFAIL "-ERR [SYS/PERM] "
#define POP3_RESP_AUTH_ERROR "-ERR [AUTH] "
#define POP3_RESP_ERR "-ERR "

/* enum POP3_Command { POP3_CMD_USER, POP3_CMD_PASS, POP3_CMD_STAT, POP3_CMD_LIST, POP3_CMD_RETR, POP3_CMD_DELE, POP3_CMD_NOOP, */
/* 		POP3_CMD_RSET, POP3_CMD_QUIT, POP3_CMD_TOP, POP3_CMD_UIDL }; */

class POP3 _kc_final : public ClientProto {
public:
	POP3(const char *path, KC::ECChannel *, std::shared_ptr<KC::ECConfig>);
	~POP3();

	// getTimeoutMinutes: 5 min when logged in otherwise 1 min
	int getTimeoutMinutes() const { return lpStore == nullptr ? 1 : 5; }

	HRESULT HrSendGreeting(const std::string &strHostString);
	HRESULT HrCloseConnection(const std::string &strQuitMsg);
	HRESULT HrProcessCommand(const std::string &strInput);
	HRESULT HrDone(bool bSendResponse);

private:
	std::string GetCapabilityString();

	HRESULT HrCmdCapability();
	HRESULT HrCmdStarttls();
	HRESULT HrCmdUser(const std::string &strUser);
	HRESULT HrCmdPass(const std::string &strPass);
	HRESULT HrCmdStat();
	HRESULT HrCmdList();
	HRESULT HrCmdList(unsigned int ulMailNr);
	HRESULT HrCmdRetr(unsigned int ulMailNr);
	HRESULT HrCmdDele(unsigned int ulMailNr);
	HRESULT HrCmdNoop();
	HRESULT HrCmdRset();
	HRESULT HrCmdQuit();
	HRESULT HrCmdUidl();
	HRESULT HrCmdUidl(unsigned int ulMailNr);
	HRESULT HrCmdTop(unsigned int ulMailNr, unsigned int ulLines);

	HRESULT HrResponse(const std::string &strResult, const std::string &strResponse);

	struct MailListItem {
		SBinary sbEntryID;
		ULONG ulSize;
		bool bDeleted;
	};

	HRESULT HrMakeMailList();
	HRESULT HrLogin(const std::string &strUsername, const std::string &strPassword);
	std::string DotFilter(const char *input);
	BOOL IsAuthorized() const { return !!lpStore; }

	KC::object_ptr<IMAPISession> lpSession;
	KC::object_ptr<IMsgStore> lpStore;
	KC::object_ptr<IMAPIFolder> lpInbox;
	KC::object_ptr<IAddrBook> lpAddrBook;
	KC::sending_options sopt;
	std::string szUser;

	std::vector<MailListItem> lstMails;
};

/** @} */
#endif
