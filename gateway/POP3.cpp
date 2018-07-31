/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/tie.hpp>
#include <mapi.h>
#include <mapix.h>
#include <mapicode.h>
#include <mapidefs.h>
#include <mapiutil.h>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include <kopano/ECTags.h>
#include <inetmapi/inetmapi.h>
#include <kopano/mapiext.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <kopano/ecversion.h>
#include <kopano/charset/utf8string.h>
#include <kopano/ECFeatures.hpp>
#include "POP3.h"

using namespace KC;
using std::string;

/**
 * @ingroup gateway_pop3
 * @{
 */
POP3::POP3(const char *szServerPath, std::shared_ptr<ECChannel> lpChannel,
    std::shared_ptr<ECConfig> lpConfig) :
	ClientProto(szServerPath, std::move(lpChannel), std::move(lpConfig))
{
	imopt_default_sending_options(&sopt);
	sopt.no_recipients_workaround = true;	// do not stop processing mail on empty recipient table
	sopt.add_received_date = true;			// add Received header (outlook uses this)
}

POP3::~POP3() {
	for (auto &m : lstMails)
		delete[] m.sbEntryID.lpb;
}

HRESULT POP3::HrSendGreeting(const std::string &strHostString) {
	HRESULT hr = hrSuccess;

	if (parseBool(lpConfig->GetSetting("server_hostname_greeting")))
		hr = HrResponse(POP3_RESP_OK, "POP3 gateway ready" + strHostString);
	else
		hr = HrResponse(POP3_RESP_OK, "POP3 gateway ready");
	return hr;
}

/**
 * Send client an error message that the socket will be closed by the server
 *
 * @param[in] strQuitMsg quit message for client
 * @return MAPI error code
 */
HRESULT POP3::HrCloseConnection(const std::string &strQuitMsg)
{
	return HrResponse(POP3_RESP_ERR, strQuitMsg);
}

/**
 * Process the requested command from the POP3 client
 *
 * @param[in] strIput received input from client
 *
 * @return MAPI error code
 */
HRESULT POP3::HrProcessCommand(const std::string &strInput)
{
	auto vWords = tokenize(strInput, ' ');
	if (vWords.empty()) {
		ec_log_warn("Empty line received");
		return MAPI_E_CALL_FAILED;
	}

	ec_log_debug("Command received: %s", vWords[0].c_str());
	auto strCommand = strToUpper(vWords[0]);
	if (strCommand.compare("CAPA") == 0) {
		if (vWords.size() != 1)
			return HrResponse(POP3_RESP_ERR,
			       "CAPA command must have 0 arguments");
		return HrCmdCapability();
	} else if (strCommand.compare("STLS") == 0) {
		if (vWords.size() != 1)
			return HrResponse(POP3_RESP_ERR,
			       "STLS command must have 0 arguments");
		if (HrCmdStarttls() != hrSuccess)
			// log ?
			// let the gateway quit from the socket read loop
			return MAPI_E_END_OF_SESSION;
		return hrSuccess;
	} else if (strCommand.compare("USER") == 0) {
		if (vWords.size() != 2)
			return HrResponse(POP3_RESP_ERR,
			       "User command must have 1 argument");
		return HrCmdUser(vWords[1]);
	} else if (strCommand.compare("PASS") == 0) {
		if (vWords.size() < 2)
			return HrResponse(POP3_RESP_ERR,
			       "Pass command must have 1 argument");
		string strPass = strInput;
		strPass.erase(0, strCommand.length()+1);
		return HrCmdPass(strPass);
	} else if (strCommand.compare("QUIT") == 0) {
		HrCmdQuit();
		// let the gateway quit from the socket read loop
		return MAPI_E_END_OF_SESSION;
    } else if (!IsAuthorized()) {
		HrResponse(POP3_RESP_ERR, "Invalid command");
		ec_log_err("Not authorized for command \"%s\"", vWords[0].c_str());
		return MAPI_E_CALL_FAILED;
	} else if (strCommand.compare("STAT") == 0) {
		if (vWords.size() != 1)
			return HrResponse(POP3_RESP_ERR,
			       "Stat command has no arguments");
		return HrCmdStat();
	} else if (strCommand.compare("LIST") == 0) {
		if (vWords.size() > 2)
			return HrResponse(POP3_RESP_ERR,
			       "List must have 0 or 1 arguments");
		if (vWords.size() == 2)
			return HrCmdList(strtoul(vWords[1].c_str(), NULL, 0));
		return HrCmdList();
	} else if (strCommand.compare("RETR") == 0) {
		if (vWords.size() != 2)
			return HrResponse(POP3_RESP_ERR,
			       "RETR must have 1 argument");
		return HrCmdRetr(strtoul(vWords[1].c_str(), NULL, 0));
	} else if (strCommand.compare("DELE") == 0) {
		if (vWords.size() != 2)
			return HrResponse(POP3_RESP_ERR,
			       "DELE must have 1 argument");
		return HrCmdDele(strtoul(vWords[1].c_str(), NULL, 0));
	} else if (strCommand.compare("NOOP") == 0) {
		if (vWords.size() > 1)
			return HrResponse(POP3_RESP_ERR,
			       "NOOP must have 0 arguments");
		return HrCmdNoop();
	} else if (strCommand.compare("RSET") == 0) {
		if (vWords.size() > 1)
			return HrResponse(POP3_RESP_ERR,
			       "RSET must have 0 arguments");
		return HrCmdRset();
	} else if (strCommand.compare("TOP") == 0) {
		if (vWords.size() != 3)
			return HrResponse(POP3_RESP_ERR,
			       "TOP must have 2 arguments");
		return HrCmdTop(strtoul(vWords[1].c_str(), NULL, 0), strtoul(vWords[2].c_str(), NULL, 0));
	} else if (strCommand.compare("UIDL") == 0) {
		if (vWords.size() > 2)
			return HrResponse(POP3_RESP_ERR,
			       "UIDL must have 0 or 1 arguments");
		if (vWords.size() == 2)
			return HrCmdUidl(strtoul(vWords[1].c_str(), NULL, 0));
		return HrCmdUidl();
	}
	HrResponse(POP3_RESP_ERR, "Function not (yet) implemented");
	ec_log_err("non-existing function \"%s\" called", vWords[0].c_str());
	return MAPI_E_CALL_FAILED;
}

/**
 * Cleanup connection
 *
 * @return hrSuccess
 */
HRESULT POP3::HrDone(bool bSendResponse)
{
	// no cleanup for POP3 required
	return hrSuccess;
}

/**
 * Send a response to the client, either +OK or -ERR
 *
 * @param[in] strResult +OK or -ERR result (use defines)
 * @param[in] strResponse string to send to client with given result
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrResponse(const string &strResult, const string &strResponse) {
	ec_log_debug("%s%s", strResult.c_str(), strResponse.c_str());
	return lpChannel->HrWriteLine(strResult + strResponse);
}

/**
 * Returns the CAPA string. In some stages, items can be listed or
 * not. This depends on the command received from the client, and
 * the logged on status of the user. Last state is autodetected in
 * the class.
 *
 * @return  The capabilities string
 */
std::string POP3::GetCapabilityString()
{
	const char *plain = lpConfig->GetSetting("disable_plaintext_auth");

	// capabilities we always have
	std::string strCapabilities = "\r\nCAPA\r\nTOP\r\nUIDL\r\nRESP-CODES\r\nAUTH-RESP-CODE\r\n";
	if (lpSession == NULL) {
		// authentication capabilities
		if (!lpChannel->UsingSsl() && lpChannel->sslctx())
			strCapabilities += "STLS\r\n";

		if (!(!lpChannel->UsingSsl() && lpChannel->sslctx() && plain && strcmp(plain, "yes") == 0 && lpChannel->peer_is_local() <= 0))
			strCapabilities += "USER\r\n";
	}
	strCapabilities += ".";
	return strCapabilities;
}

/**
 * @brief Handle the CAPA command
 *
 * Sends all the gateway capabilities to the client, depending on the
 * state we're in. Authentication capabilities are skipped when a user
 * was already logged in.
 *
 * @return hrSuccess
 */
HRESULT POP3::HrCmdCapability() {
	return HrResponse(POP3_RESP_OK, GetCapabilityString());
}

/**
 * @brief Handle the STLS command
 *
 * Tries to set the current connection to use SSL encryption.
 *
 * @return hrSuccess
 */
HRESULT POP3::HrCmdStarttls() {
	if (!lpChannel->sslctx())
		return HrResponse(POP3_RESP_PERMFAIL, "STLS error in ssl context");
	if (lpChannel->UsingSsl())
		return HrResponse(POP3_RESP_ERR, "STLS already using SSL/TLS");
	HRESULT hr = HrResponse(POP3_RESP_OK, "Begin TLS negotiation now");
	if (hr != hrSuccess)
		return hr;
	hr = lpChannel->HrEnableTLS();
	if (hr != hrSuccess) {
		HrResponse(POP3_RESP_ERR, "Error switching to secure SSL/TLS connection");
		ec_log_err("Error switching to SSL in STLS");
		return hr;
	}
	if (lpChannel->UsingSsl())
		ec_log_info("Using SSL now");
	return hrSuccess;
}

/**
 * @brief Handle the USER command
 *
 * Stores the username in the class, since the password is in a second call
 *
 * @param[in] strUser loginname of the user who wants to login
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdUser(const string &strUser) {
	const char *plain = lpConfig->GetSetting("disable_plaintext_auth");

	if (!lpChannel->UsingSsl() && lpChannel->sslctx() && plain && strcmp(plain, "yes") == 0 && lpChannel->peer_is_local() <= 0) {
		auto hr = HrResponse(POP3_RESP_AUTH_ERROR, "Plaintext authentication disallowed on non-secure (SSL/TLS) connections");
		ec_log_err("Aborted login from [%s] with username \"%s\" (tried to use disallowed plaintext auth)",
					  lpChannel->peer_addr(), strUser.c_str());
		return hr;
	} else if (lpStore != NULL) {
		return HrResponse(POP3_RESP_AUTH_ERROR, "Can't login twice");
	} else if (strUser.length() > POP3_MAX_RESPONSE_LENGTH) {
		ec_log_err("Username too long: %d > %d", (int)strUser.length(), POP3_MAX_RESPONSE_LENGTH);
		return HrResponse(POP3_RESP_PERMFAIL, "Username too long");
	}
	szUser = strUser;
	return HrResponse(POP3_RESP_OK, "Waiting for password");
}

/**
 * @brief Handle the PASS command
 *
 * Now that we have the password, we can login.
 *
 * @param[in] strPass password of the user to login with
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdPass(const string &strPass) {
	const char *plain = lpConfig->GetSetting("disable_plaintext_auth");

	if (!lpChannel->UsingSsl() && lpChannel->sslctx() && plain && strcmp(plain, "yes") == 0 && lpChannel->peer_is_local() <= 0) {
		auto hr = HrResponse(POP3_RESP_AUTH_ERROR, "Plaintext authentication disallowed on non-secure (SSL/TLS) connections");
		if(szUser.empty())
			ec_log_err("Aborted login from [%s] without username (tried to use disallowed "
							 "plaintext auth)", lpChannel->peer_addr());
		else
			ec_log_err("Aborted login from [%s] with username \"%s\" (tried to use disallowed "
							 "plaintext auth)", lpChannel->peer_addr(), szUser.c_str());
		return hr;
	} else if (lpStore != NULL) {
		return HrResponse(POP3_RESP_AUTH_ERROR, "Can't login twice");
	} else if (strPass.length() > POP3_MAX_RESPONSE_LENGTH) {
		ec_log_err("Password too long: %d > %d", (int)strPass.length(), POP3_MAX_RESPONSE_LENGTH);
		return HrResponse(POP3_RESP_PERMFAIL, "Password too long");
	} else if (szUser.empty()) {
		return HrResponse(POP3_RESP_ERR, "Give username first");
	}

	auto hr = this->HrLogin(szUser, strPass);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_LOGON_FAILED)
			HrResponse(POP3_RESP_AUTH_ERROR, "Wrong username or password");
		else
			HrResponse(POP3_RESP_TEMPFAIL, "Internal error: HrLogin failed");
		return hr;
	}
	hr = this->HrMakeMailList();
	if (hr != hrSuccess) {
		HrResponse(POP3_RESP_ERR, "Can't get mail list");
		return hr;
	}
	return HrResponse(POP3_RESP_OK, "Username and password accepted");
}

/**
 * @brief Handle the STAT command
 *
 * STAT displays the number of messages and the total size of the Inbox
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdStat() {
	ULONG ulSize = 0;
	char szResponse[POP3_MAX_RESPONSE_LENGTH];

	for (size_t i = 0; i < lstMails.size(); ++i)
		ulSize += lstMails[i].ulSize;
	snprintf(szResponse, POP3_MAX_RESPONSE_LENGTH, "%u %u", (ULONG)lstMails.size(), ulSize);
	return HrResponse(POP3_RESP_OK, szResponse);
}

/**
 * @brief Handle the LIST command
 *
 * List shows for every message the number to retrieve the message
 * with and the size of the message. Since we don't know a client
 * which uses this size exactly, we can use the table version.
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdList() {
	char szResponse[POP3_MAX_RESPONSE_LENGTH];

	snprintf(szResponse, POP3_MAX_RESPONSE_LENGTH, "%u messages", (ULONG)lstMails.size());
	HRESULT hr = HrResponse(POP3_RESP_OK, szResponse);
	if (hr != hrSuccess)
		return hr;

	for (size_t i = 0; i < lstMails.size(); ++i) {
		snprintf(szResponse, POP3_MAX_RESPONSE_LENGTH, "%u %u", (ULONG)i + 1, lstMails[i].ulSize);
		hr = lpChannel->HrWriteLine(szResponse);
		if (hr != hrSuccess)
			return hr;
	}
	return lpChannel->HrWriteLine(".");
}

/**
 * @brief Handle the LIST <number> command
 *
 * Shows the size of the given mail number.
 *
 * @param[in] ulMailNr number of the email, starting at 1
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdList(unsigned int ulMailNr) {
	char szResponse[POP3_MAX_RESPONSE_LENGTH];

	if (ulMailNr > lstMails.size() || ulMailNr < 1) {
		HrResponse(POP3_RESP_ERR, "Wrong mail number");
		return MAPI_E_NOT_FOUND;
	}
	snprintf(szResponse, POP3_MAX_RESPONSE_LENGTH, "%u %u", ulMailNr, lstMails[ulMailNr - 1].ulSize);
	return HrResponse(POP3_RESP_OK, szResponse);
}

/**
 * @brief Handle the RETR command
 *
 * Retrieve the complete mail for a given number
 *
 * @param[in] ulMailNr number of the email, starting at 1
 *
 * @return
 */
HRESULT POP3::HrCmdRetr(unsigned int ulMailNr) {
	object_ptr<IMessage> lpMessage;
	object_ptr<IStream> lpStream;
	ULONG ulObjType;
	string strMessage;
	char szResponse[POP3_MAX_RESPONSE_LENGTH];

	if (ulMailNr < 1 || ulMailNr > lstMails.size()) {
		HrResponse(POP3_RESP_ERR, "mail nr not found");
		return MAPI_E_NOT_FOUND;
	}

	auto hr = lpStore->OpenEntry(lstMails[ulMailNr-1].sbEntryID.cb, reinterpret_cast<ENTRYID *>(lstMails[ulMailNr-1].sbEntryID.lpb), &IID_IMessage, MAPI_DEFERRED_ERRORS,
	     &ulObjType, &~lpMessage);
	if (hr != hrSuccess) {
		HrResponse(POP3_RESP_ERR, "Failing to open entry");
		return hr;
	}
	hr = lpMessage->OpenProperty(PR_EC_IMAP_EMAIL, &IID_IStream, 0, 0, &~lpStream);
	if (hr == hrSuccess) {
		hr = Util::HrStreamToString(lpStream, strMessage);
		if (hr == hrSuccess)
			strMessage = DotFilter(strMessage.c_str());
	}
	if (hr != hrSuccess) {
		// unable to load streamed version, so try full conversion.
		std::unique_ptr<char[]> szMessage;
		hr = IMToINet(lpSession, lpAddrBook, lpMessage, &unique_tie(szMessage), sopt);
		if (hr != hrSuccess) {
			HrResponse(POP3_RESP_PERMFAIL, "Converting MAPI to MIME error");
			return kc_perror("Error converting MAPI to MIME", hr);
		}
		strMessage = DotFilter(szMessage.get());
	}

	snprintf(szResponse, POP3_MAX_RESPONSE_LENGTH, "%u octets", (ULONG)strMessage.length());
	HrResponse(POP3_RESP_OK, szResponse);
	lpChannel->HrWriteLine(strMessage);
	lpChannel->HrWriteLine(".");
	return hrSuccess;
}

/**
 * @brief Handle the DELE command
 *
 * Mark an email for deletion after the QUIT command
 *
 * @param[in] ulMailNr number of the email, starting at 1
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdDele(unsigned int ulMailNr) {
	if (ulMailNr < 1 || ulMailNr > lstMails.size()) {
		HRESULT hr = HrResponse(POP3_RESP_ERR, "mail nr not found");
		if (hr == hrSuccess)
			hr = MAPI_E_NOT_FOUND;
		return hr;
	}

	lstMails[ulMailNr - 1].bDeleted = true;
	return HrResponse(POP3_RESP_OK, "mail deleted");
}

/**
 * @brief Handle the NOOP command
 *
 * Sends empty OK string to client
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdNoop() {
	return HrResponse(POP3_RESP_OK, string());
}

/**
 * @brief Handle the RSET command
 *
 * Resets the connections, sets every email to not deleted.
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdRset() {
	for (auto &m : lstMails)
		m.bDeleted = false;
	return HrResponse(POP3_RESP_OK, "Undeleted mails");
}

/**
 * @brief Handle the QUIT command
 *
 * Delete all delete marked emails and close the connection.
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdQuit() {
	unsigned int DeleteCount = 0;
	SBinaryArray ba = {0, NULL};

	for (const auto &m : lstMails)
		if (m.bDeleted)
			++DeleteCount;

	if (DeleteCount) {
		ba.cValues = DeleteCount;
		ba.lpbin = new SBinary[DeleteCount];
		DeleteCount = 0;

		for (const auto &m : lstMails)
			if (m.bDeleted) {
				ba.lpbin[DeleteCount] = m.sbEntryID;
				++DeleteCount;
			}

		lpInbox->DeleteMessages(&ba, 0, NULL, 0);
		// ignore error, we always send the Bye to the client
	}

	auto hr = HrResponse(POP3_RESP_OK, "Bye");
	delete[] ba.lpbin;
	return hr;
}

/**
 * @brief Handle the UIDL command
 *
 * List all messages by number and Unique ID (EntryID). This ID must
 * be valid over different sessions.
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdUidl() {
	char szResponse[POP3_MAX_RESPONSE_LENGTH];
	HRESULT hr = lpChannel->HrWriteLine("+OK");
	if (hr != hrSuccess)
		return hr;

	for (size_t i = 0; i < lstMails.size(); ++i) {
		snprintf(szResponse, POP3_MAX_RESPONSE_LENGTH, "%u ", (ULONG)i + 1);
		std::string strResponse = szResponse;
		strResponse += bin2hex(lstMails[i].sbEntryID);
		hr = lpChannel->HrWriteLine(strResponse);
		if (hr != hrSuccess)
			return hr;
	}
	return lpChannel->HrWriteLine(".");
}

/**
 * @brief Handle the UIDL <number> command
 *
 * List the given message number by number and Unique ID
 * (EntryID). This ID must be valid over different sessions.
 *
 * @param ulMailNr number of the email to get the Unique ID for
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdUidl(unsigned int ulMailNr) {
	char szResponse[POP3_MAX_RESPONSE_LENGTH];

	if (ulMailNr < 1 || ulMailNr > lstMails.size()) {
		auto hr = HrResponse(POP3_RESP_ERR, "mail nr not found");
		if (hr == hrSuccess)
			hr = MAPI_E_NOT_FOUND;
		return hr;
	}

	snprintf(szResponse, POP3_MAX_RESPONSE_LENGTH, "%u ", ulMailNr);
	std::string strResponse = szResponse;
	strResponse += bin2hex(lstMails[ulMailNr-1].sbEntryID);
	return HrResponse(POP3_RESP_OK, strResponse);
}

/**
 * @brief Handle the TOP command
 *
 * List the first N body lines of an email. The headers are always sent.
 *
 * @param[in] ulMailNr The email to list
 * @param[in] ulLines The number of lines of the email to send
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrCmdTop(unsigned int ulMailNr, unsigned int ulLines) {
	object_ptr<IMessage> lpMessage;
	object_ptr<IStream> lpStream;
	ULONG ulObjType;
	string strMessage;

	if (ulMailNr < 1 || ulMailNr > lstMails.size()) {
		auto hr = HrResponse(POP3_RESP_ERR, "mail nr not found");
		if (hr == hrSuccess)
			return MAPI_E_NOT_FOUND;
		return hr;
	}

	auto hr = lpStore->OpenEntry(lstMails[ulMailNr-1].sbEntryID.cb, reinterpret_cast<ENTRYID *>(lstMails[ulMailNr-1].sbEntryID.lpb), &IID_IMessage, MAPI_DEFERRED_ERRORS,
	     &ulObjType, &~lpMessage);
	if (hr != hrSuccess) {
		HrResponse(POP3_RESP_ERR, "Failing to open entry");
		return hr;
	}
	hr = lpMessage->OpenProperty(PR_EC_IMAP_EMAIL, &IID_IStream, 0, 0, &~lpStream);
	if (hr == hrSuccess)
		hr = Util::HrStreamToString(lpStream, strMessage);
	if (hr != hrSuccess) {
		// unable to load streamed version, so try full conversion.
		std::unique_ptr<char[]> szMessage;
		hr = IMToINet(lpSession, lpAddrBook, lpMessage, &unique_tie(szMessage), sopt);
		if (hr != hrSuccess) {
			HrResponse(POP3_RESP_PERMFAIL, "Converting MAPI to MIME error");
			return kc_perror("Error converting MAPI to MIME", hr);
		}
		strMessage = szMessage.get();
	}

	auto ulPos = strMessage.find("\r\n\r\n", 0);
	++ulLines;
	while (ulPos != string::npos && ulLines--)
		ulPos = strMessage.find("\r\n", ulPos + 1);
	if (ulPos != string::npos)
		strMessage = strMessage.substr(0, ulPos);
	strMessage = DotFilter(strMessage.c_str());
	if (HrResponse(POP3_RESP_OK, string()) != hrSuccess ||
		lpChannel->HrWriteLine(strMessage) != hrSuccess ||
		lpChannel->HrWriteLine(".") != hrSuccess)
		return MAPI_E_CALL_FAILED;
	return hrSuccess;
}

/**
 * Open the Inbox with the given login credentials
 *
 * @param[in] strUsername Username to login with
 * @param[in] strPassword Corresponding password of username
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrLogin(const std::string &strUsername, const std::string &strPassword) {
	unsigned int cbEntryID = 0, ulObjType = 0;
	memory_ptr<ENTRYID> lpEntryID;
	std::wstring strwUsername, strwPassword;
	unsigned int flags;

	auto hr = TryConvert(strUsername, rawsize(strUsername), "windows-1252", strwUsername);
	if (hr != hrSuccess) {
		ec_log_err("Illegal byte sequence in username");
		goto exit;
	}
	hr = TryConvert(strPassword, rawsize(strPassword), "windows-1252", strwPassword);
	if (hr != hrSuccess) {
		ec_log_err("Illegal byte sequence in password");
		goto exit;
	}

	flags = EC_PROFILE_FLAGS_NO_NOTIFICATIONS | EC_PROFILE_FLAGS_NO_COMPRESSION;
	if (!parseBool(lpConfig->GetSetting("bypass_auth")))
		flags |= EC_PROFILE_FLAGS_NO_UID_AUTH;
	hr = HrOpenECSession(&~lpSession, PROJECT_VERSION, "gateway/pop3",
	     strwUsername.c_str(), strwPassword.c_str(), m_strPath.c_str(),
	     flags, NULL, NULL);
	if (hr != hrSuccess) {
		ec_log_err("Failed to login from [%s] with invalid username \"%s\" or wrong password: %s (%x)",
			lpChannel->peer_addr(), strUsername.c_str(), GetMAPIErrorMessage(hr), hr);
		++m_ulFailedLogins;
		if (m_ulFailedLogins >= LOGIN_RETRIES)
			// disconnect client
			hr = MAPI_E_END_OF_SESSION;
		goto exit;
	}

	hr = HrOpenDefaultStore(lpSession, &~lpStore);
	if (hr != hrSuccess) {
		ec_log_err("Failed to open default store");
		goto exit;
	}
	hr = lpSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, &~lpAddrBook);
	if (hr != hrSuccess) {
		ec_log_err("Failed to open addressbook");
		goto exit;
	}
	// check if pop3 access is disabled
	if (checkFeature("pop3", lpAddrBook, lpStore, PR_EC_DISABLED_FEATURES_A)) {
		ec_log_err("POP3 not enabled for user \"%s\"", strUsername.c_str());
		hr = MAPI_E_LOGON_FAILED;
		goto exit;
	}
	hr = lpStore->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &cbEntryID, &~lpEntryID, nullptr);
	if (hr != hrSuccess) {
		ec_log_err("Failed to find receive folder of store");
		goto exit;
	}
	hr = lpStore->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpInbox);
	if (ulObjType != MAPI_FOLDER)
		hr = MAPI_E_NOT_FOUND;

	if (hr != hrSuccess) {
		ec_log_err("Failed to open receive folder");
		goto exit;
	}
	ec_log_notice("POP3 login from [%s] for user \"%s\"", lpChannel->peer_addr(), strUsername.c_str());
exit:
	if (hr != hrSuccess) {
		lpInbox.reset();
		lpStore.reset();
	}
	return hr;
}

/**
 * Make a list of all emails in the Opened inbox
 *
 * @return MAPI Error code
 */
HRESULT POP3::HrMakeMailList() {
	object_ptr<IMAPITable> lpTable;
	enum { EID, SIZE, NUM_COLS };
	static constexpr const SizedSPropTagArray(NUM_COLS, spt) =
		{NUM_COLS, {PR_ENTRYID, PR_MESSAGE_SIZE}};
	static constexpr const SizedSSortOrderSet(1, tableSort) =
		{1, 0, 0, {{PR_CREATION_TIME, TABLE_SORT_ASCEND}}};

	auto hr = lpInbox->GetContentsTable(0, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SetColumns(spt, 0);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->SortTable(tableSort, 0);
	if (hr != hrSuccess)
		return hr;

	rowset_ptr lpRows;
	hr = lpTable->QueryRows(-1, 0, &~lpRows);
	if (hr != hrSuccess)
		return hr;

	lstMails.clear();
	for (ULONG i = 0; i < lpRows->cRows; ++i) {
		if (PROP_TYPE(lpRows[i].lpProps[EID].ulPropTag) == PT_ERROR) {
			ec_log_err("Missing EntryID in message table for message %d", i);
			continue;
		}
		if (PROP_TYPE(lpRows[i].lpProps[SIZE].ulPropTag) == PT_ERROR) {
			ec_log_err("Missing size in message table for message %d", i);
			continue;
		}

		MailListItem sMailListItem;
		sMailListItem.sbEntryID.cb = lpRows[i].lpProps[EID].Value.bin.cb;
		sMailListItem.sbEntryID.lpb = new BYTE[lpRows[i].lpProps[EID].Value.bin.cb];
		memcpy(sMailListItem.sbEntryID.lpb, lpRows[i].lpProps[EID].Value.bin.lpb, lpRows[i].lpProps[EID].Value.bin.cb);
		sMailListItem.bDeleted = false;
		sMailListItem.ulSize = lpRows[i].lpProps[SIZE].Value.l;
		lstMails.emplace_back(std::move(sMailListItem));
	}
	return hrSuccess;
}

/**
 * Since a POP3 email stops with one '.' line, we need to escape these lines in the actual email.
 *
 * @param[in] input input email to escape
 *
 * @return POP3 escaped email
 */
string POP3::DotFilter(const char *input) {
	string output;
	ULONG i = 0;

	while (input[i] != '\0') {
		if (input[i] == '.' && input[i-1] == '\n')
			output += '.';
		output += input[i++];
	}
	return output;
}

/** @} */
