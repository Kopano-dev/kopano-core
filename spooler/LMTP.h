/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef LMTP_H
#define LMTP_H

#include <string>
#include <vector>
#include <kopano/zcdefs.h>
#include <kopano/ECChannel.h>
#include <kopano/ECConfig.h>

enum LMTP_Command {LMTP_Command_LHLO, LMTP_Command_MAIL_FROM, LMTP_Command_RCPT_TO, LMTP_Command_DATA, LMTP_Command_RSET, LMTP_Command_QUIT };

class LMTP _kc_final {
public:
	LMTP(KC::ECChannel *, const char *path, KC::ECConfig *);
	HRESULT HrGetCommand(const std::string &strCommand, LMTP_Command &eCommand);
	HRESULT HrResponse(const std::string &strResponse);

	HRESULT HrCommandLHLO(const std::string &strInput, std::string & nameOut);
	HRESULT HrCommandMAILFROM(const std::string &buffer, std::string &addr);
	HRESULT HrCommandRCPTTO(const std::string &buffer, std::string &addr_unsolved);
	HRESULT HrCommandDATA(FILE *tmp);

private:
	HRESULT HrParseAddress(const std::string &buffer, std::string &email);

	KC::ECChannel *m_lpChannel;
	KC::ECConfig *m_lpConfig;
	std::string		m_strPath;
};

#endif

