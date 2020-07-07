/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <kopano/memory.hpp>
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

class POP3 final : public ClientProto {
public:
	POP3(const char *path, std::shared_ptr<KC::ECChannel>, std::shared_ptr<KC::ECConfig>);
	~POP3();

	// getTimeoutMinutes: 5 min when logged in otherwise 1 min
	int getTimeoutMinutes() const { return lpStore == nullptr ? 1 : 5; }
	virtual HRESULT HrSendGreeting(const KC::string_view &host) override;
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
	bool IsAuthorized() const { return lpStore != nullptr; }

	KC::object_ptr<IMAPISession> lpSession;
	KC::object_ptr<IMsgStore> lpStore;
	KC::object_ptr<IMAPIFolder> lpInbox;
	KC::object_ptr<IAddrBook> lpAddrBook;
	KC::sending_options sopt;
	std::string szUser;
	std::vector<MailListItem> lstMails;
};

/** @} */
