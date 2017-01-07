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

#include <cstdio>
#include <cstdlib>

#include <sstream>
#include <iostream>
#include <algorithm>

#include <mapi.h>
#include <mapix.h>
#include <mapicode.h>
#include <mapidefs.h>
#include <mapiutil.h>
#include <mapiguid.h>
#include <kopano/ECDefs.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECTags.h>
#include <kopano/ECIConv.h>
#include <inetmapi/inetmapi.h>
#include <kopano/mapiext.h>
#include <vector>
#include <list>
#include <set>
#include <unordered_set>
#include <map>
#include <algorithm>
#include <kopano/base64.h>
#include <inetmapi/options.h>

#include <edkmdb.h>
#include <kopano/stringutil.h>
#include <kopano/codepage.h>
#include <kopano/charset/convert.h>
#include <kopano/restrictionutil.h>
#include <kopano/ecversion.h>
#include <kopano/ECGuid.h>
#include <kopano/namedprops.h>
#include "ECFeatures.h"

#include <boost/algorithm/string/join.hpp>
#include <kopano/mapi_ptr.h>

#include "IMAP.h"
using namespace std;

/** 
 * @ingroup gateway_imap
 * @{
 */
const string strMonth[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/**
 * Returns TRUE if the given string starts with strPrefix
 *
 * @param[in]	strInput	string to find prefix in
 * @param[in]	strPrefix	test if input starts with this string
 */
static bool Prefix(const std::string &strInput, const std::string &strPrefix)
{
    return (strInput.compare(0, strPrefix.size(), strPrefix) == 0);
}

IMAP::IMAP(const char *szServerPath, ECChannel *lpChannel, ECLogger *lpLogger,
    ECConfig *lpConfig) :
	ClientProto(szServerPath, lpChannel, lpLogger, lpConfig)
{
	lpSession = NULL;
	lpStore = NULL;
	lpAddrBook = NULL;
	lpPublicStore = NULL;

	imopt_default_delivery_options(&dopt);
	dopt.add_imap_data = parseBool(lpConfig->GetSetting("imap_store_rfc822"));

	bOnlyMailFolders = parseBool(lpConfig->GetSetting("imap_only_mailfolders"));
	bShowPublicFolder = parseBool(lpConfig->GetSetting("imap_public_folders"));

	m_ulLastUid = 0;

	m_bIdleMode = false;
	m_lpIdleAdviseSink = NULL;
	m_ulIdleAdviseConnection = 0;
	m_lpIdleTable = NULL;

	m_bContinue = false;
	
	m_ulCacheUID = 0;
	m_lpsIMAPTags = NULL;

	m_lpTable = NULL;
	m_ulErrors = 0;
	bCurrentFolderReadOnly = false;
	pthread_mutex_init(&m_mIdleLock, NULL);
}

IMAP::~IMAP() {
	if (m_lpTable)
		m_lpTable->Release();
	CleanupObject();
	pthread_mutex_destroy(&m_mIdleLock);
}

void IMAP::CleanupObject()
{
	// Free/release all new and allocated memory
	if (lpPublicStore)
		lpPublicStore->Release();
	lpPublicStore = NULL;

	if (lpStore)
		lpStore->Release();
	lpStore = NULL;

	if (lpAddrBook)
		lpAddrBook->Release();
	lpAddrBook = NULL;
	MAPIFreeBuffer(m_lpsIMAPTags);
	m_lpsIMAPTags = NULL;

	if (lpSession)
		lpSession->Release();
	lpSession = NULL;

	// idle cleanup
	if (m_lpIdleAdviseSink)
		m_lpIdleAdviseSink->Release();
	m_lpIdleAdviseSink = NULL;

	if (m_lpIdleTable)
		m_lpIdleTable->Release();
	m_lpIdleTable = NULL;
}

void IMAP::ReleaseContentsCache()
{
	if (m_lpTable)
		m_lpTable->Release();
	m_lpTable = NULL;
	m_vTableDataColumns.clear();
}

/** 
 * Returns number of minutes to keep connection alive
 * 
 * @return user logged in (true) or not (false)
 */
int IMAP::getTimeoutMinutes() {
	if (lpStore != NULL)
		return 30;				// 30 minutes when logged in
	else
		return 1;				// 1 minute when not logged in
}

/**
 * Uppercases a normal string
 */
void IMAP::ToUpper(string &strString) {
	transform(strString.begin(), strString.end(), strString.begin(), ::toupper);
}
/**
 * Uppercases a wide string
 */
void IMAP::ToUpper(wstring &strString) {
	transform(strString.begin(), strString.end(), strString.begin(), ::toupper);
}

/**
 * Case insensitive std::string compare
 *
 * @param[in]	strA	compare with strB
 * @param[in]	strB	compare with strA
 * @return	true if equal, not considering case differences
 */
bool IMAP::CaseCompare(const string& strA, const string& strB)
{
	return strcasecmp(strA.c_str(), strB.c_str()) == 0;
}

/**
 * Split a string with IMAP_HIERARCHY_DELIMITER into a vector
 *
 * @param[in]	strInput	Folder string with the IMAP_HIERARCHY_DELIMITER for a hierarchy
 * @param[out]	vPaths		Vector to all folders found in strInput
 * @retval		hrSuccess
 */
HRESULT IMAP::HrSplitPath(const wstring &strInputParam, vector<wstring> &vPaths) {
	std::wstring strInput = strInputParam;

    // Strip leading and trailing /
    if(strInput[0] == IMAP_HIERARCHY_DELIMITER)
		strInput.erase(0,1);
        
    if(strInput[strInput.size()-1] == IMAP_HIERARCHY_DELIMITER)
		strInput.erase(strInput.size()-1, 1);
        
    // split into parts
    vPaths = tokenize(strInput, IMAP_HIERARCHY_DELIMITER);
    
    return hrSuccess;    
}

/**
 * Concatenates a vector to a string with IMAP_HIERARCHY_DELIMITER
 *
 * @param[in]	vPaths		Vector to all folders names
 * @param[out]	strOutput	Folder string with the IMAP_HIERARCHY_DELIMITER for a hierarchy
 * @retval		hrSuccess
 */
HRESULT IMAP::HrUnsplitPath(const vector<wstring> &vPaths, wstring &strOutput) {
    strOutput.clear();
        
    for (size_t i = 0; i < vPaths.size(); ++i) {
        strOutput += vPaths[i];
        if(i != vPaths.size()-1)
            strOutput += IMAP_HIERARCHY_DELIMITER;
    }
    
    return hrSuccess;
}

/**
 * Splits IMAP list input into a vector and appends result in return variable.
 *
 * @param[in]	strInput	Single IMAP command argument that should be split in words
 * @param[out]	vWords		vector of words found in the strInput
 * @retval hrSuccess
 */
HRESULT IMAP::HrSplitInput(const string &strInput, vector<string> &vWords) {
	unsigned int uSpecialCount = 0;
	string::size_type beginPos = 0;
	string::size_type currentPos = 0;
	string::size_type findPos = strInput.find_first_of("\"()[] ", currentPos);
	string::size_type specialPos = string::npos;

	while (findPos != string::npos) {
		if (uSpecialCount == 0 && strInput[findPos] == '"') {
			// find corresponding " and add the string
			specialPos = findPos;
			do {
				specialPos = strInput.find_first_of("\"", specialPos + 1);
			} while (specialPos != string::npos && strInput[specialPos-1] == '\\');

			if (specialPos != string::npos) {
				vWords.push_back(strInput.substr(findPos + 1, specialPos - findPos - 1));
				findPos = specialPos;
				beginPos = findPos + 1;
			}
		} else if (strInput[findPos] == '(' || strInput[findPos] == '[') {
			++uSpecialCount;
		} else if (strInput[findPos] == ')' || strInput[findPos] == ']') {
			if (uSpecialCount > 0)
				--uSpecialCount;
		} else if (uSpecialCount == 0) {
			if (findPos > beginPos) {
				vWords.push_back(strInput.substr(beginPos, findPos - beginPos));
			}
			beginPos = findPos + 1;
		}

		currentPos = findPos + 1;
		findPos = strInput.find_first_of("\"()[] ", currentPos);
	}

	if (beginPos < strInput.size()) {
		vWords.push_back(strInput.substr(beginPos));
	}

	return hrSuccess;
}

/** 
 * Send login greeting to client
 * 
 * @param[in] strHostString optional hostname string
 * 
 * @return 
 */
HRESULT IMAP::HrSendGreeting(const std::string &strHostString)
{
	HRESULT hr = hrSuccess;

	if (parseBool(lpConfig->GetSetting("server_hostname_greeting")))
		hr = HrResponse(RESP_UNTAGGED, "OK [" + GetCapabilityString(false) + "] IMAP gateway ready" + strHostString);
	else
		hr = HrResponse(RESP_UNTAGGED, "OK [" + GetCapabilityString(false) + "] IMAP gateway ready");

	return hr;
}

/** 
 * Send client an error message that the socket will be closed by the server
 * 
 * @param[in] strQuitMsg quit message for client
 * @return MAPI error code
 */
HRESULT IMAP::HrCloseConnection(const std::string &strQuitMsg)
{
	return HrResponse(RESP_UNTAGGED, strQuitMsg);
}

/** 
 * Execute the IMAP command from the client
 * 
 * @param strInput received input from client
 * 
 * @return MAPI error code
 */
HRESULT IMAP::HrProcessCommand(const std::string &strInput)
{
	HRESULT hr = hrSuccess;
	vector<string> strvResult;
	std::string strTag;
	std::string strCommand;
	ULONG ulMaxMessageSize = atoui(lpConfig->GetSetting("imap_max_messagesize"));

	if (lpLogger->Log(EC_LOGLEVEL_DEBUG))
		lpLogger->Log(EC_LOGLEVEL_DEBUG, "< %s", strInput.c_str());

	HrSplitInput(strInput, strvResult);
	if (strvResult.empty()) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	if (strvResult.size() == 1) {
		// must be idle, and command must be done
		// DONE is not really a command, but the end of the IDLE command by the client marker
		ToUpper(strvResult[0]);
		if (strvResult[0].compare("DONE") == 0) {
			hr = HrDone(true);
		} else {
			hr = HrResponse(RESP_UNTAGGED, "BAD Command not recognized");
		}
		goto exit;
	}

	while (hr == hrSuccess && !strvResult.empty() && strvResult.back().size() > 2 && strvResult.back()[0] == '{')
	{
		bool bPlus = false;
		char *lpcres = NULL;
		string inBuffer;
		string strByteTag;
		ULONG ulByteCount;

		strByteTag = strvResult.back().substr(1, strvResult.back().length() -1);
		ulByteCount = strtoul(strByteTag.c_str(), &lpcres, 10);
		if (lpcres == strByteTag.c_str() || (*lpcres != '}' && *lpcres != '+')) {
			// invalid tag received
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Invalid size tag received: %s", strByteTag.c_str());
			goto exit;
		}
		bPlus = (*lpcres == '+');

		// no need to output the 
		if (!bPlus) {
			hr = HrResponse(RESP_CONTINUE, "Ready for literal data");
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Error sending during continuation");
				goto exit;
			}
		}

		if (ulByteCount > ulMaxMessageSize) {
			lpLogger->Log(EC_LOGLEVEL_WARNING, "Discarding %d bytes of data", ulByteCount);

			hr = lpChannel->HrReadAndDiscardBytes(ulByteCount);
			if (hr != hrSuccess)
				break;
		} else {
			// @todo select for timeout
			hr = lpChannel->HrReadBytes(&inBuffer, ulByteCount);
			if (hr != hrSuccess)
				break;
			// replace size request with literal 
			strvResult.back() = inBuffer;

			if (lpLogger->Log(EC_LOGLEVEL_DEBUG))
				lpLogger->Log(EC_LOGLEVEL_DEBUG, "< <%d bytes data> %s", ulByteCount, inBuffer.c_str());
		}

		hr = lpChannel->HrReadLine(&inBuffer);
		if (hr != hrSuccess) {
			if (errno)
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to read line: %s", strerror(errno));
			else
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Client disconnected");
			break;
		}

		HrSplitInput(inBuffer, strvResult);

		if (ulByteCount > ulMaxMessageSize) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Maximum message size reached (%u), message size is %u bytes", ulMaxMessageSize, ulByteCount);
			hr = HrResponse(RESP_TAGGED_NO, strTag, "[ALERT] Maximum message size reached");
			if (hr == hrSuccess)
				hr = MAPI_E_NOT_ENOUGH_MEMORY;
			break;
		}
	}
	if (hr != hrSuccess)
		goto exit;

	strTag = strvResult.front();
	strvResult.erase(strvResult.begin());

	strCommand = strvResult.front();
	strvResult.erase(strvResult.begin());

	ToUpper(strCommand);
	if (isIdle()) {
		hr = HrResponse(RESP_UNTAGGED, "BAD still in idle state");
		HrDone(false); // false for no output		
		goto exit;
	}

	// process {} and end of line

	if (strCommand.compare("CAPABILITY") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "CAPABILITY must have 0 arguments");
		} else {
			hr = HrCmdCapability(strTag);
		}
	} else if (strCommand.compare("NOOP") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "NOOP must have 0 arguments");
		} else {
			hr = HrCmdNoop(strTag);
		}
	} else if (strCommand.compare("LOGOUT") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "LOGOUT must have 0 arguments");
		} else {
			hr = HrCmdLogout(strTag);
			// let the gateway quit from the socket read loop
			hr = MAPI_E_END_OF_SESSION;
		}
	} else if (strCommand.compare("STARTTLS") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "STARTTLS must have 0 arguments");
		} else {
			hr = HrCmdStarttls(strTag);
			if (hr != hrSuccess) {
				// log ?
				// let the gateway quit from the socket read loop
				hr = MAPI_E_END_OF_SESSION;
			}
		}
	} else if (strCommand.compare("AUTHENTICATE") == 0) {
		if (strvResult.size() == 1) {
			hr = HrCmdAuthenticate(strTag, strvResult[0], string());
		} else if (strvResult.size() == 2) {
			hr = HrCmdAuthenticate(strTag, strvResult[0], strvResult[1]);
		} else {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "AUTHENTICATE must have 1 or 2 arguments");
		}
	} else if (strCommand.compare("LOGIN") == 0) {
		if (strvResult.size() != 2) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "LOGIN must have 2 arguments");
		} else {
			hr = HrCmdLogin(strTag, strvResult[0], strvResult[1]);
		}
	} else if (strCommand.compare("SELECT") == 0) {
		if (strvResult.size() != 1) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "SELECT must have 1 argument");
		} else {
			hr = HrCmdSelect(strTag, strvResult[0], false);
		}
	} else if (strCommand.compare("EXAMINE") == 0) {
		if (strvResult.size() != 1) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "EXAMINE must have 1 argument");
		} else {
			hr = HrCmdSelect(strTag, strvResult[0], true);
		}
	} else if (strCommand.compare("CREATE") == 0) {
		if (strvResult.size() != 1) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "CREATE must have 1 argument");
		} else {
			hr = HrCmdCreate(strTag, strvResult[0]);
		}
	} else if (strCommand.compare("DELETE") == 0) {
		if (strvResult.size() != 1) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "DELETE must have 1 argument");
		} else {
			hr = HrCmdDelete(strTag, strvResult[0]);
		}
	} else if (strCommand.compare("RENAME") == 0) {
		if (strvResult.size() != 2) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "RENAME must have 2 arguments");
		} else {
			hr = HrCmdRename(strTag, strvResult[0], strvResult[1]);
		}
	} else if (strCommand.compare("SUBSCRIBE") == 0) {
		if (strvResult.size() != 1) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "SUBSCRIBE must have 1 arguments");
		} else {
			hr = HrCmdSubscribe(strTag, strvResult[0], true);
		}
	} else if (strCommand.compare("UNSUBSCRIBE") == 0) {
		if (strvResult.size() != 1) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "UNSUBSCRIBE must have 1 arguments");
		} else {
			hr = HrCmdSubscribe(strTag, strvResult[0], false);
		}
	} else if (strCommand.compare("LIST") == 0) {
		if (strvResult.size() != 2) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "LIST must have 2 arguments");
		} else {
			hr = HrCmdList(strTag, strvResult[0], strvResult[1], false);
		}
	} else if (strCommand.compare("LSUB") == 0) {
		if (strvResult.size() != 2) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "LSUB must have 2 arguments");
		} else {
			hr = HrCmdList(strTag, strvResult[0], strvResult[1], true);
		}
	} else if (strCommand.compare("STATUS") == 0) {
		if (strvResult.size() != 2) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "STATUS must have 2 arguments");
		} else {
			hr = HrCmdStatus(strTag, strvResult[0], strvResult[1]);
		}
	} else if (strCommand.compare("APPEND") == 0) {
		if (strvResult.size() == 2) {
			hr = HrCmdAppend(strTag, strvResult[0], strvResult[1]);
		} else if (strvResult.size() == 3) {
			if (strvResult[1][0] == '(')
				hr = HrCmdAppend(strTag, strvResult[0], strvResult[2], strvResult[1]);
			else
				hr = HrCmdAppend(strTag, strvResult[0], strvResult[2], string(), strvResult[1]);
		} else if (strvResult.size() == 4) {
			// if both flags and time are given, it must be in that order
			hr = HrCmdAppend(strTag, strvResult[0], strvResult[3], strvResult[1], strvResult[2]);
		} else {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "APPEND must have 2, 3 or 4 arguments");
		}
	} else if (strCommand.compare("CHECK") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "CHECK must have 0 arguments");
		} else {
			hr = HrCmdCheck(strTag);
		}
	} else if (strCommand.compare("CLOSE") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "CLOSE must have 0 arguments");
		} else {
			hr = HrCmdClose(strTag);
		}
	} else if (strCommand.compare("EXPUNGE") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "EXPUNGE must have 0 arguments");
		} else {
			hr = HrCmdExpunge(strTag, string());
		}
	} else if (strCommand.compare("SEARCH") == 0) {
		if (strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "SEARCH must have 1 or more arguments");
		} else {
			hr = HrCmdSearch(strTag, strvResult, false);
		}
	} else if (strCommand.compare("FETCH") == 0) {
		if (strvResult.size() != 2) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "FETCH must have 2 arguments");
		} else {
			hr = HrCmdFetch(strTag, strvResult[0], strvResult[1], false);
		}
	} else if (strCommand.compare("STORE") == 0) {
		if (strvResult.size() != 3) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "STORE must have 3 arguments");
		} else {
			hr = HrCmdStore(strTag, strvResult[0], strvResult[1], strvResult[2], false);
		}
	} else if (strCommand.compare("COPY") == 0) {
		if (strvResult.size() != 2) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "COPY must have 2 arguments");
		} else {
			hr = HrCmdCopy(strTag, strvResult[0], strvResult[1], false);
		}
	} else if (strCommand.compare("IDLE") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "IDLE must have 0 arguments");
		} else {
			hr = HrCmdIdle(strTag);
		}
	} else if (strCommand.compare("NAMESPACE") == 0) {
		if (!strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "NAMESPACE must have 0 arguments");
		} else {
			hr = HrCmdNamespace(strTag);
		}
	} else if (strCommand.compare("GETQUOTAROOT") == 0) {
		if (strvResult.size() != 1) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "GETQUOTAROOT must have 1 arguments");
		} else {
			hr = HrCmdGetQuotaRoot(strTag, strvResult[0]);
		}
	} else if (strCommand.compare("GETQUOTA") == 0) {
		if (strvResult.size() != 1) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "GETQUOTA must have 1 arguments");
		} else {
			hr = HrCmdGetQuota(strTag, strvResult[0]);
		}
	} else if (strCommand.compare("SETQUOTA") == 0) {
		if (strvResult.size() != 2) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "SETQUOTA must have 2 arguments");
		} else {
			hr = HrCmdSetQuota(strTag, strvResult[0], strvResult[1]);
		}
	} else if (strCommand.compare("UID") == 0) {
		if (strvResult.empty()) {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "UID must have a command");
			goto exit;
		}

		strCommand = strvResult.front();
		strvResult.erase(strvResult.begin());
		ToUpper(strCommand);

		if (strCommand.compare("SEARCH") == 0) {
			if (strvResult.empty()) {
				hr = HrResponse(RESP_TAGGED_BAD, strTag, "UID SEARCH must have 1 or more arguments");
			} else {
				hr = HrCmdSearch(strTag, strvResult, true);
			}
		} else if (strCommand.compare("FETCH") == 0) {
			if (strvResult.size() != 2) {
				hr = HrResponse(RESP_TAGGED_BAD, strTag, "UID FETCH must have 2 arguments");
			} else {
				hr = HrCmdFetch(strTag, strvResult[0], strvResult[1], true);
			}
		} else if (strCommand.compare("STORE") == 0) {
			if (strvResult.size() != 3) {
				hr = HrResponse(RESP_TAGGED_BAD, strTag, "UID STORE must have 3 arguments");
			} else {
				hr = HrCmdStore(strTag, strvResult[0], strvResult[1], strvResult[2], true);
			}
		} else if (strCommand.compare("COPY") == 0) {
			if (strvResult.size() != 2) {
				hr = HrResponse(RESP_TAGGED_BAD, strTag, "UID COPY must have 2 arguments");
			} else {
				hr = HrCmdCopy(strTag, strvResult[0], strvResult[1], true);
			}
		} else if (strCommand.compare("XAOL-MOVE") == 0) {
			if (strvResult.size() != 2) {
				hr = HrResponse(RESP_TAGGED_BAD, strTag, "UID XAOL-MOVE must have 2 arguments");
			} else {
				hr = HrCmdUidXaolMove(strTag, strvResult[0], strvResult[1]);
			}
		} else if (strCommand.compare("EXPUNGE") == 0) {
			if (strvResult.size() != 1) {
				hr = HrResponse(RESP_TAGGED_BAD, strTag, "UID EXPUNGE must have 1 argument");
			} else {
				hr = HrCmdExpunge(strTag, strvResult[0]);
			}
		} else {
			hr = HrResponse(RESP_TAGGED_BAD, strTag, "UID Command not supported");
		}

	} else {
		hr = HrResponse(RESP_TAGGED_BAD, strTag, "Command not supported");
	}

exit:
	return hr;
}

/** 
 * Continue command, only supported for AUTHENTICATE command
 * 
 * @param[in] strInput more input from client
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrProcessContinue(const string &strInput)
{
	return HrCmdAuthenticate(m_strContinueTag, "PLAIN", strInput);
}

/** 
 * Returns the CAPABILITY string. In some stages, items can be listed
 * or not. This depends on the command received from the client, and
 * the logged on status of the user. Last state is autodetected in the
 * class.
 * 
 * @param[in]  bAllFlags Whether all capabilities should be placed in the string, and not only the pre-logon capabilities.
 * 
 * @return  The capabilities string
 */
std::string IMAP::GetCapabilityString(bool bAllFlags)
{
	string strCapabilities;
	const char *idle = lpConfig->GetSetting("imap_capability_idle");
	const char *plain = lpConfig->GetSetting("disable_plaintext_auth");

	// capabilities we always have
	strCapabilities = "CAPABILITY IMAP4rev1 LITERAL+";

	if (lpSession == NULL) {
		// authentication capabilities
		if (!lpChannel->UsingSsl() && lpChannel->sslctx())
			strCapabilities += " STARTTLS";

		if (!lpChannel->UsingSsl() && lpChannel->sslctx() && plain && strcmp(plain, "yes") == 0 && lpChannel->peer_is_local() <= 0)
			strCapabilities += " LOGINDISABLED";
		else
			strCapabilities += " AUTH=PLAIN";
	}

	if (lpSession || bAllFlags) {
		// capabilities after authentication
		strCapabilities += " CHILDREN XAOL-OPTION NAMESPACE QUOTA";

		if (idle && strcmp(idle, "yes") == 0)
			strCapabilities += " IDLE";
	}

	return strCapabilities;
}

/** 
 * @brief Handles the CAPABILITY command
 *
 * Sends all the gateway capabilities to the client, depending on the
 * state we're in. Authentication capabilities are skipped when a user
 * was already logged in.
 * 
 * @param[in] strTag the IMAP tag for this command
 * 
 * @return hrSuccess
 */
HRESULT IMAP::HrCmdCapability(const string &strTag) {
	HRESULT hr = hrSuccess;
	std::string strCapabilities = GetCapabilityString(true);

	hr = HrResponse(RESP_UNTAGGED, strCapabilities);
	if (hr != hrSuccess)
		goto exit;

	hr = HrResponse(RESP_TAGGED_OK, strTag, "CAPABILITY Completed");
	if (hr != hrSuccess)
		goto exit;

exit:
	return hr;
}

/** 
 * @brief Handles the NOOP command
 * 
 * Checks the current selected folder for changes, and possebly sends
 * the EXISTS and/or RECENT values to the client.
 *
 * @param[in] strTag the IMAP tag for this command
 * 
 * @return hrSuccess
 */
HRESULT IMAP::HrCmdNoop(const string &strTag) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;

	if (!strCurrentFolder.empty())
		hr = HrRefreshFolderMails(false, !bCurrentFolderReadOnly, false, NULL);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_BAD, strTag, "NOOP completed");
		goto exit;
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, "NOOP completed");
	if (hr != hrSuccess)
		goto exit;

exit:
	return (hr2 != hrSuccess) ? hr2 : hr;
}

/** 
 * @brief Handles the LOGOUT command
 * 
 * Sends the BYE response to the client. HandleIMAP() will close the
 * connection.
 *
 * @param[in] strTag the IMAP tag for this command
 * 
 * @return hrSuccess
 */
HRESULT IMAP::HrCmdLogout(const string &strTag) {
	HRESULT hr = hrSuccess;

	hr = HrResponse(RESP_UNTAGGED, "BYE server logging out");
	if (hr != hrSuccess)
		goto exit;
	
	hr = HrResponse(RESP_TAGGED_OK, strTag, "LOGOUT completed");
	if (hr != hrSuccess)
		goto exit;

exit:
	return hr;
}

/** 
 * @brief Handles the STARTTLS command
 * 
 * Tries to set the current connection to use SSL encryption.
 *
 * @param[in] strTag the IMAP tag for this command
 * 
 * @return hrSuccess
 */
HRESULT IMAP::HrCmdStarttls(const string &strTag) {
	HRESULT hr = hrSuccess;

	if (!lpChannel->sslctx()) {
		hr = HrResponse(RESP_TAGGED_NO, strTag, "STARTTLS error in ssl context");
		goto exit;
	}

	if (lpChannel->UsingSsl()) {
		hr = HrResponse(RESP_TAGGED_NO, strTag, "STARTTLS already using SSL/TLS");
		goto exit;
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, "Begin TLS negotiation now");
	if (hr != hrSuccess)
		goto exit;

	hr = lpChannel->HrEnableTLS(lpLogger);
	if (hr != hrSuccess) {
		HrResponse(RESP_TAGGED_BAD, strTag, "[ALERT] Error switching to secure SSL/TLS connection");
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Error switching to SSL in STARTTLS");
		goto exit;
	}

	if (lpChannel->UsingSsl())
		lpLogger->Log(EC_LOGLEVEL_INFO, "Using SSL now");

exit:
	return hr;
}

/** 
 * @brief Handles the AUTENTICATE command
 *
 * The authenticate command only implements the PLAIN authentication
 * method, since we require the actual password, and not just a hash
 * value.
 * 
 * According to the RFC, the data in strAuthData is UTF-8, but thunderbird still sends iso-8859-X.
 *
 * @param[in] strTag The IMAP tag for this command
 * @param[in] strAuthMethod Must be set to PLAIN
 * @param[in] strAuthData (optional) if empty we use a continuation request, otherwise we can authenticate, base64 encoded string
 * 
 * @return MAPI error code
 */
HRESULT IMAP::HrCmdAuthenticate(const string &strTag, string strAuthMethod, const string &strAuthData)
{
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	vector<string> vAuth;

	const char *plain = lpConfig->GetSetting("disable_plaintext_auth");

	// If plaintext authentication was disabled any authentication attempt must be refused very soon
	if (!lpChannel->UsingSsl() && lpChannel->sslctx() && plain && strcmp(plain, "yes") == 0 && lpChannel->peer_is_local() <= 0) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "[PRIVACYREQUIRED] Plaintext authentication disallowed on non-secure "
							 "(SSL/TLS) connections.");
		if (hr2 != hrSuccess)
			goto exit;

		lpLogger->Log(EC_LOGLEVEL_ERROR, "Aborted login from %s without username (tried to use disallowed plaintext auth)",
					  lpChannel->peer_addr());
		goto exit;
	}

	ToUpper(strAuthMethod);
	if (strAuthMethod.compare("PLAIN") != 0) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "AUTHENTICATE " + strAuthMethod + " method not supported");
		hr = MAPI_E_NO_SUPPORT;
		goto exit;
	}

	if (strAuthData.empty() && !m_bContinue) {
		// request the rest of the authentication data by sending one space in a continuation line
		hr2 = HrResponse(RESP_CONTINUE, string());
		m_strContinueTag = strTag;
		m_bContinue = true;
		hr = MAPI_W_PARTIAL_COMPLETION;
		goto exit;
	}
	m_bContinue = false;

	vAuth = tokenize(base64_decode(strAuthData), '\0');
	// vAuth[0] is the authorization name (ignored for now, but maybe we can use this for opening other stores?)
	// vAuth[1] is the authentication name
	// vAuth[2] is the password for vAuth[1]
		
	if (vAuth.size() != 3) {
		lpLogger->Log(EC_LOGLEVEL_INFO, "Invalid authentication data received, expected 3 items, have %lu items.", vAuth.size());
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "AUTHENTICATE " + strAuthMethod + " incomplete data received");
		hr = MAPI_E_LOGON_FAILED;
		goto exit;
	}

	hr = HrCmdLogin(strTag, vAuth[1], vAuth[2]);

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the LOGIN command
 *
 * Opens a MAPI session, addressbook and possebly open the public if
 * the username and password are correctly entered.
 *
 * @param[in] strTag IMAP tag
 * @param[in] strUser Username, currently tried as windows-1252 charsets
 * @param[in] strPass Password, currently tried as windows-1252 charsets
 * 
 * @return MAPI error code
 */
HRESULT IMAP::HrCmdLogin(const string &strTag, const string &strUser, const string &strPass) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	string strUsername;
	size_t i;
	wstring strwUsername;
	wstring strwPassword;
	const char *plain = lpConfig->GetSetting("disable_plaintext_auth");

	// strUser isn't sent in imap style utf-7, but \ is escaped, so strip those
	for (i = 0; i < strUser.length(); ++i) {
		if (strUser[i] == '\\' && i+1 < strUser.length() && (strUser[i+1] == '"' || strUser[i+1] == '\\'))
			++i;
		strUsername += strUser[i];
	}	

	// If plaintext authentication was disabled any login attempt must be refused very soon
	if (!lpChannel->UsingSsl() && lpChannel->sslctx() && plain && strcmp(plain, "yes") == 0 && lpChannel->peer_is_local() <= 0) {
		hr2 = HrResponse(RESP_UNTAGGED, "BAD [ALERT] Plaintext authentication not allowed without SSL/TLS, but your client "
						"did it anyway. If anyone was listening, the password was exposed.");
		if (hr2 != hrSuccess)
			goto exit;

		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "[PRIVACYREQUIRED] Plaintext authentication disallowed on non-secure "
							 "(SSL/TLS) connections.");
		if (hr2 != hrSuccess)
			goto exit;

		lpLogger->Log(EC_LOGLEVEL_ERROR, "Aborted login from %s with username \"%s\" (tried to use disallowed plaintext auth)",
					  lpChannel->peer_addr(), strUsername.c_str());
		goto exit;
	}

	if (lpSession != NULL) {
		lpLogger->Log(EC_LOGLEVEL_INFO, "Ignoring to login TWICE for username \"%s\"", strUsername.c_str());
		hr = HrResponse(RESP_TAGGED_NO, strTag, "LOGIN Can't login twice");
		// hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = TryConvert(strUsername, rawsize(strUsername), "windows-1252", strwUsername);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Illegal byte sequence in username");
		goto exit;
	}
	hr = TryConvert(strPass, rawsize(strPass), "windows-1252", strwPassword);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Illegal byte sequence in password");
		goto exit;
	}

	// do not disable notifications for imap connections, may be idle and sessions on the storage server will disappear.
	hr = HrOpenECSession(lpLogger, &lpSession, "gateway/imap", PROJECT_SVN_REV_STR, strwUsername.c_str(), strwPassword.c_str(), m_strPath.c_str(), EC_PROFILE_FLAGS_NO_COMPRESSION, NULL, NULL);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Failed to login from %s with invalid username \"%s\" or wrong password. Error: 0x%08X",
					  lpChannel->peer_addr(), strUsername.c_str(), hr);
		if (hr == MAPI_E_LOGON_FAILED)
			hr2 = HrResponse(RESP_TAGGED_NO, strTag, "LOGIN wrong username or password");
		else
			hr2 = HrResponse(RESP_TAGGED_BAD, strTag, "Internal error: OpenECSession failed");
		if (hr2 != hrSuccess)
			goto exit;
		++m_ulFailedLogins;
		if (m_ulFailedLogins >= LOGIN_RETRIES)
			// disconnect client
			hr = MAPI_E_END_OF_SESSION;
		goto exit;
	}

	hr = HrOpenDefaultStore(lpSession, &lpStore);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to open default store");
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "LOGIN can't open default store");
		goto exit;
	}

	hr = lpSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, &lpAddrBook);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Failed to open addressbook");
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "LOGIN can't open addressbook");
		goto exit;
	}

	// check if imap access is disabled
	if (isFeatureDisabled("imap", lpAddrBook, lpStore)) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "IMAP not enabled for user '%s'", strUsername.c_str());
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "LOGIN imap feature disabled");
		hr = MAPI_E_LOGON_FAILED;
		goto exit;
	}

	m_strwUsername = strwUsername;

	{
		PROPMAP_START;
		PROPMAP_NAMED_ID(ENVELOPE, PT_STRING8, PS_EC_IMAP, dispidIMAPEnvelope);
		PROPMAP_INIT(lpStore);

		hr = MAPIAllocateBuffer(CbNewSPropTagArray(4), (void**)&m_lpsIMAPTags);
		if (hr != hrSuccess)
			goto exit;

		m_lpsIMAPTags->aulPropTag[0] = PROP_ENVELOPE;
	}

	hr = HrGetSubscribedList();
	// ignore error, empty list of subscribed folder

	if(bShowPublicFolder){
		hr = HrOpenECPublicStore(lpSession, &lpPublicStore);
		if (hr != hrSuccess) {
			lpLogger->Log(EC_LOGLEVEL_WARNING, "Failed to open public store");
			lpPublicStore = NULL;
		}
	}

	hr = HrMakeSpecialsList();
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Failed to find special folder properties");
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "LOGIN can't find special folder properties");
		goto exit;
	}

	lpLogger->Log(EC_LOGLEVEL_NOTICE, "IMAP Login from %s for user %s", lpChannel->peer_addr(), strUsername.c_str());
	hr = HrResponse(RESP_TAGGED_OK, strTag, "[" + GetCapabilityString(false) + "] LOGIN completed");

exit:
	if (hr != hrSuccess || hr2 != hrSuccess)
		CleanupObject();

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/**
 * @brief Handles the SELECT and EXAMINE commands
 *
 * Make the strFolder the current working folder.
 *
 * @param[in]	strTag	IMAP command tag
 * @param[in]	strFolder	IMAP folder name in UTF-7 something charset
 * @param[in]	bReadOnly	The EXAMINE command was given instead of the SELECT command
 */
HRESULT IMAP::HrCmdSelect(const string &strTag, const string &strFolder, bool bReadOnly) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	char szResponse[IMAP_RESP_MAX + 1];
	unsigned int ulUnseen = 0;
	string command = "SELECT";
	ULONG ulUIDValidity = 1;

	if (bReadOnly)
		command = "EXAMINE";
	
	if (!lpSession) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, command+" error no session");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// close old contents table if cached version was open
	ReleaseContentsCache();

	// Apple mail client does this request, so we need to block it.
	if (strFolder.empty()) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, command+" invalid folder name");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = IMAP2MAPICharset(strFolder, strCurrentFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, command+" invalid folder name");
		goto exit;
	}

	bCurrentFolderReadOnly = bReadOnly;
	hr = HrRefreshFolderMails(true, !bCurrentFolderReadOnly, false, &ulUnseen, &ulUIDValidity);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, command+" error getting mails in folder");
		goto exit;
	}

	// \Seen = PR_MESSAGE_FLAGS MSGFLAG_READ
	// \Answered = PR_MSG_STATUS MSGSTATUS_ANSWERED  //PR_LAST_VERB_EXECUTED EXCHIVERB_REPLYTOSENDER EXCHIVERB_REPLYTOALL
	// \Draft = PR_MSG_STATUS MSGSTATUS_DRAFT        //PR_MESSAGE_FLAGS MSGFLAG_UNSENT
	// \Flagged = PR_FLAG_STATUS
	// \Deleted = PR_MSG_STATUS MSGSTATUS_DELMARKED
	// \Recent = ??? (arrived after last command/login)
	// $Forwarded = PR_LAST_VERB_EXECUTED: NOTEIVERB_FORWARD
	hr = HrResponse(RESP_UNTAGGED, "FLAGS (\\Seen \\Draft \\Deleted \\Flagged \\Answered $Forwarded)");
	if (hr != hrSuccess)
		goto exit;
	hr = HrResponse(RESP_UNTAGGED, "OK [PERMANENTFLAGS (\\Seen \\Draft \\Deleted \\Flagged \\Answered $Forwarded)] Permanent flags");
	if (hr != hrSuccess)
		goto exit;
	snprintf(szResponse, IMAP_RESP_MAX, "OK [UIDNEXT %u] Predicted next UID", m_ulLastUid + 1);
	hr = HrResponse(RESP_UNTAGGED, szResponse);
	if (hr != hrSuccess)
		goto exit;
	if(ulUnseen) {
    	snprintf(szResponse, IMAP_RESP_MAX, "OK [UNSEEN %u] First unseen message", ulUnseen);
    	hr = HrResponse(RESP_UNTAGGED, szResponse);
		if (hr != hrSuccess)
			goto exit;
    }
	snprintf(szResponse, IMAP_RESP_MAX, "OK [UIDVALIDITY %u] UIDVALIDITY value", ulUIDValidity);
	hr = HrResponse(RESP_UNTAGGED, szResponse);
	if (hr != hrSuccess)
		goto exit;
	if (bReadOnly)
		hr = HrResponse(RESP_TAGGED_OK, strTag, "[READ-ONLY] EXAMINE completed");
	else
		hr = HrResponse(RESP_TAGGED_OK, strTag, "[READ-WRITE] SELECT completed");

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the CREATE command
 *
 * Recursively create a new folders, starting from the root folder.
 * 
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strFolderParam The foldername, encoded in IMAP UTF-7 charset
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdCreate(const string &strTag, const string &strFolderParam) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	IMAPIFolder *lpFolder = NULL;
	IMAPIFolder *lpSubFolder = NULL;
	vector<wstring> strPaths;
	wstring strFolder;
	wstring strPath;
	SPropValue sFolderClass;

	if (!lpSession) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "CREATE error no session");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	if (strFolderParam.empty()) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "CREATE error no folder");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = IMAP2MAPICharset(strFolderParam, strFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "CREATE invalid folder name");
		goto exit;
	}

	if (strFolder[0] == IMAP_HIERARCHY_DELIMITER) {
		// courier and dovecot also block this
        hr = HrResponse(RESP_TAGGED_NO, strTag, "CREATE invalid folder name");
        goto exit;
	}

    hr = HrFindFolderPartial(strFolder, &lpFolder, &strPath);
    if(hr != hrSuccess) {
        hr2 = HrResponse(RESP_TAGGED_NO, strTag, "CREATE error opening destination folder");
        goto exit;
    }

	if (strPath.empty()) {
		hr = HrResponse(RESP_TAGGED_NO, strTag, "CREATE folder already exists");
		goto exit;
	}

	strPaths = tokenize(strPath, IMAP_HIERARCHY_DELIMITER);

	for (const auto &path : strPaths) {
		hr = lpFolder->CreateFolder(FOLDER_GENERIC, const_cast<TCHAR *>(path.c_str()), NULL, NULL, MAPI_UNICODE, &lpSubFolder);
		if (hr != hrSuccess) {
			if (hr == MAPI_E_COLLISION)
				hr2 = HrResponse(RESP_TAGGED_NO, strTag, "CREATE folder already exists");
			else
				hr2 = HrResponse(RESP_TAGGED_NO, strTag, "CREATE can't create folder");
			goto exit;
		}

		sFolderClass.ulPropTag = PR_CONTAINER_CLASS_A;
		sFolderClass.Value.lpszA = const_cast<char *>("IPF.Note");
		hr = HrSetOneProp(lpSubFolder, &sFolderClass);
		if (hr != hrSuccess)
			goto exit;

		lpFolder->Release();
		lpFolder = lpSubFolder;
		lpSubFolder = NULL;
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, "CREATE completed");

exit:
	if (lpFolder)
		lpFolder->Release();

	if (lpSubFolder)
		lpSubFolder->Release();

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the DELETE command
 *
 * Deletes folders, starting from the root folder. Special MAPI
 * folders may not be deleted. The folder will be removed from the
 * subscribed list, if it was subscribed.
 * 
 * @test write test which checks if delete removes folder from subscribed list
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strFolderParam The foldername, encoded in IMAP UTF-7 charset
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdDelete(const string &strTag, const string &strFolderParam) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	list<SFolder>::const_iterator lpFolder;
	list<SFolder>::const_iterator lpDelFolder;
	IMAPIFolder *lpParentFolder = NULL;
	ULONG cbEntryID;
	LPENTRYID lpEntryID = NULL;
	wstring strFolder;

	if (!lpSession) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "DELETE error no session");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = IMAP2MAPICharset(strFolderParam, strFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "DELETE invalid folder name");
		goto exit;
	}
	ToUpper(strFolder);

	if (strFolder.compare(L"INBOX") == 0) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "DELETE error deleting INBOX is not allowed");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = HrFindFolderEntryID(strFolder, &cbEntryID, &lpEntryID);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "DELETE error folder not found");
		goto exit;
	}

	if (IsSpecialFolder(cbEntryID, lpEntryID)) {
		hr = HrResponse(RESP_TAGGED_NO, strTag, "DELETE special folder may not be deleted");
		goto exit;
	}

	hr = HrOpenParentFolder(cbEntryID, lpEntryID, &lpParentFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "DELETE error opening parent folder");
		goto exit;
	}

	hr = lpParentFolder->DeleteFolder(cbEntryID, lpEntryID, 0, NULL, DEL_FOLDERS | DEL_MESSAGES);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "DELETE error deleting folder");
		goto exit;
	}

	// remove from subscribed list
	hr = ChangeSubscribeList(false, cbEntryID, lpEntryID);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to update subscribed list for deleted folder '%ls'", strFolder.c_str());
		hr = hrSuccess;
	}

	// close folder if it was selected
	if (strCurrentFolder == strFolder) {
	    strCurrentFolder.clear();
		// close old contents table if cached version was open
		ReleaseContentsCache();
    }

	hr = HrResponse(RESP_TAGGED_OK, strTag, "DELETE completed");

exit:
	MAPIFreeBuffer(lpEntryID);
	if (lpParentFolder)
		lpParentFolder->Release();

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the RENAME command
 *
 * Renames or moves a folder. A folder cannot be moved under itself. A
 * folder cannot be renamed to another existing folder.
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strExistingFolderParam the folder to rename or move, in IMAP UTF-7 charset
 * @param[in] strNewFolderParam the new folder name, in IMAP UTF-7 charset
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdRename(const string &strTag, const string &strExistingFolderParam, const string &strNewFolderParam) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	LPSPropValue lppvFromEntryID = NULL;
	LPSPropValue lppvDestEntryID = NULL;
	IMAPIFolder *lpParentFolder = NULL;
	IMAPIFolder *lpMakeFolder = NULL;
	IMAPIFolder *lpSubFolder = NULL;
	ULONG ulObjType = 0;
	string::size_type deliPos;
	ULONG cbMovFolder = 0;
	LPENTRYID lpMovFolder = NULL;
	wstring strExistingFolder;
	wstring strNewFolder;
	wstring strPath;
	wstring strFolder;
	SPropValue sFolderClass;

	if (!lpSession) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME error no session");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = IMAP2MAPICharset(strExistingFolderParam, strExistingFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME invalid folder name");
		goto exit;
	}
	hr = IMAP2MAPICharset(strNewFolderParam, strNewFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME invalid folder name");
		goto exit;
	}

	ToUpper(strExistingFolder);
	if (strExistingFolder.compare(L"INBOX") == 0) {
		// FIXME, rfc text:
		// 
		//       Renaming INBOX is permitted, and has special behavior.  It moves
		//       all messages in INBOX to a new mailbox with the given name,
		//       leaving INBOX empty.  If the server implementation supports
		//       inferior hierarchical names of INBOX, these are unaffected by a
		//       rename of INBOX.

		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = HrFindFolderEntryID(strExistingFolder, &cbMovFolder, &lpMovFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME error source folder not found");
		goto exit;
	}

	if (IsSpecialFolder(cbMovFolder, lpMovFolder)) {
		hr = HrResponse(RESP_TAGGED_NO, strTag, "RENAME special folder may not be moved or renamed");
		goto exit;
	}

    hr = HrOpenParentFolder(cbMovFolder, lpMovFolder, &lpParentFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME error opening parent folder");
		goto exit;
	}

	// Find the folder as far as we can
	hr = HrFindFolderPartial(strNewFolder, &lpMakeFolder, &strPath);
	if(hr != hrSuccess) {
	    hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME error opening destination folder");
	    goto exit;
    }
    
    if(strPath.empty()) {
        hr = HrResponse(RESP_TAGGED_NO, strTag, "RENAME destination already exists");
        goto exit;
    }
    
    // strPath now contains subfolder we want to create (eg sub/new). So now we have to
    // mkdir -p all the folder leading up to the last (if any)
	do {
		deliPos = strPath.find(IMAP_HIERARCHY_DELIMITER);
		if (deliPos == string::npos) {
			strFolder = strPath;
		} else {
			strFolder = strPath.substr(0, deliPos);
			strPath = strPath.substr(deliPos + 1);

			if (!strFolder.empty())
				hr = lpMakeFolder->CreateFolder(FOLDER_GENERIC, (TCHAR *) strFolder.c_str(), NULL, NULL, MAPI_UNICODE | OPEN_IF_EXISTS, &lpSubFolder);

			if (hr != hrSuccess) {
				hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME error creating folder");
				goto exit;
			}

			sFolderClass.ulPropTag = PR_CONTAINER_CLASS_A;
			sFolderClass.Value.lpszA = const_cast<char *>("IPF.Note");
			hr = HrSetOneProp(lpSubFolder, &sFolderClass);
			if (hr != hrSuccess)
				goto exit;

			lpMakeFolder->Release();
			lpMakeFolder = lpSubFolder;
			lpSubFolder = NULL;
		}
	} while (deliPos != string::npos);

	if (HrGetOneProp(lpParentFolder, PR_ENTRYID, &lppvFromEntryID) != hrSuccess || HrGetOneProp(lpMakeFolder, PR_ENTRYID, &lppvDestEntryID) != hrSuccess) {
		hr = HrResponse(RESP_TAGGED_NO, strTag, "RENAME error opening source or destination");
		goto exit;
	}

	// When moving in the same folder, just rename
	if (lppvFromEntryID->Value.bin.cb != lppvDestEntryID->Value.bin.cb || memcmp(lppvFromEntryID->Value.bin.lpb, lppvDestEntryID->Value.bin.lpb, lppvDestEntryID->Value.bin.cb) != 0) {
	    // Do the real move
		hr = lpParentFolder->CopyFolder(cbMovFolder, lpMovFolder, &IID_IMAPIFolder, lpMakeFolder,
										(TCHAR *) strFolder.c_str(), 0, NULL, MAPI_UNICODE | FOLDER_MOVE);
	} else {
		// from is same as dest folder, use SetProps(PR_DISPLAY_NAME)
		SPropValue propName;
		propName.ulPropTag = PR_DISPLAY_NAME_W;
		propName.Value.lpszW = (WCHAR*)strFolder.c_str();

		hr = lpSession->OpenEntry(cbMovFolder, lpMovFolder, &IID_IMAPIFolder, MAPI_MODIFY,
								&ulObjType,	(LPUNKNOWN *) &lpSubFolder);
		if (hr != hrSuccess) {
			hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME error opening folder");
			goto exit;
		}

		hr = lpSubFolder->SetProps(1, &propName, NULL);
	}

	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "RENAME error moving folder");
		goto exit;
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, "RENAME completed");

exit:
	MAPIFreeBuffer(lppvFromEntryID);
	MAPIFreeBuffer(lppvDestEntryID);
	MAPIFreeBuffer(lpMovFolder);

	if (lpParentFolder)
		lpParentFolder->Release();

	if (lpMakeFolder)
		lpMakeFolder->Release();

	if (lpSubFolder)
		lpSubFolder->Release();

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the SUBSCRIBE and UNSUBSCRIBE commands
 * 
 * Subscribe or unsubscribe the given folder. Special IMAP folders
 * cannot be unsubscribed. Subscribed folders are listed with the LSUB
 * command.
 *
 * @note we differ here from other IMAP server, who do allow
 * unsubscribing the INBOX (which is normally the only special IMAP
 * folder)
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strFolderParam The folder to (un)subscribe, in IMAP UTF-7 charset
 * @param[in] bSubscribe subscribe (true) or unsubscribe (false)
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdSubscribe(const string &strTag, const string &strFolderParam, bool bSubscribe) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	string strAction;
	ULONG cbEntryID = 0;
	LPENTRYID lpEntryID = NULL;
	wstring strFolder;

	if (bSubscribe)
		strAction = "SUBSCRIBE";
	else
		strAction = "UNSUBSCRIBE";

	if (!lpSession) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strAction+" error no session");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = IMAP2MAPICharset(strFolderParam, strFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strAction+" invalid folder name");
		goto exit;
	}

	hr = HrFindFolderEntryID(strFolder, &cbEntryID, &lpEntryID);
	if (hr != hrSuccess) {
		// folder not found, but not error, so thunderbird updates view correctly.
		hr2 = HrResponse(RESP_TAGGED_OK, strTag, strAction+" folder not found");
		goto exit;
	}

	if (IsSpecialFolder(cbEntryID, lpEntryID)) {
		if (!bSubscribe)
			hr = HrResponse(RESP_TAGGED_NO, strTag, strAction+" cannot unsubscribe this special folder");
		else
			hr = HrResponse(RESP_TAGGED_OK, strTag, strAction+" completed");
		goto exit;
	}		

	hr = ChangeSubscribeList(bSubscribe, cbEntryID, lpEntryID);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strAction+" writing subscriptions to server failed");
		goto exit;
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, strAction+" completed");

exit:
	MAPIFreeBuffer(lpEntryID);
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the LIST and LSUB commands
 * 
 * Lists all or subscribed folders, with wildcard filtering. Supported
 * folder flags are \Noselect and \Has(No)Children.
 * @todo I don't think we check the \Noselect in the SELECT/EXAMINE command
 * @test with strFindFolder in UTF-7 with wildcards
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strReferenceFolder list folders below this folder, in IMAP UTF-7 charset
 * @param[in] strFindFolder list folders with names matching this name with/or wildcards (* and %) , in IMAP UTF-7 charset
 * @param[in] bSubscribedOnly Behave for the LIST command (false) or LSUB command (true)
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdList(const string &strTag, string strReferenceFolder, const string &strFindFolder, bool bSubscribedOnly) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	string strAction;
	string strResponse;
	wstring strPattern;
	string strListProps;
	string strCompare;
	wstring strFolderPath;
	list<SFolder> lstFolders;

	if (bSubscribedOnly)
		strAction = "LSUB";
	else
		strAction = "LIST";

	if (!lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strAction+" error no session");
		goto exit;
	}

	if (strFindFolder.empty()) {
		strResponse = strAction+" (\\Noselect) \"";
		strResponse += IMAP_HIERARCHY_DELIMITER;
		strResponse += "\" \"\"";
		hr = HrResponse(RESP_UNTAGGED, strResponse);
		if (hr == hrSuccess)
			HrResponse(RESP_TAGGED_OK, strTag, strAction+" completed");
		goto exit;
	}

	HrGetSubscribedList();
	// ignore error
	
	// add trailing delimiter to strReferenceFolder
	if (!strReferenceFolder.empty() && strReferenceFolder[strReferenceFolder.length()-1] != IMAP_HIERARCHY_DELIMITER) {
		strReferenceFolder += IMAP_HIERARCHY_DELIMITER;
	}

	strReferenceFolder += strFindFolder;
	hr = IMAP2MAPICharset(strReferenceFolder, strPattern);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strAction+" invalid folder name");
		goto exit;
	}
	ToUpper(strPattern);
	
	// Get all folders
	hr = HrGetFolderList(lstFolders);
	if(hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strAction+" unable to list folders");
	    goto exit;
	}

	// Loop through all folders to see if they match
	for (list<SFolder>::const_iterator iFld = lstFolders.begin(); iFld != lstFolders.end(); ++iFld) {
		if (bSubscribedOnly && !iFld->bActive && !iFld->bSpecialFolder)
		    // Folder is not subscribed to
		    continue;

		// Get full path name
		strFolderPath.clear();
		// if path is empty, we're probably dealing the IPM_SUBTREE entry
		if(HrGetFolderPath(iFld, lstFolders, strFolderPath) != hrSuccess || strFolderPath.empty())
		    continue;
		    
		if (!strFolderPath.empty())
			strFolderPath.erase(0,1); // strip / from start of string

        if (MatchFolderPath(strFolderPath, strPattern)) {
			hr = MAPI2IMAPCharset(strFolderPath, strResponse);
			if (hr != hrSuccess) {
				lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to represent foldername '%ls' in UTF-7", strFolderPath.c_str());
				continue;
			}

			strResponse = (string)"\"" + IMAP_HIERARCHY_DELIMITER + "\" \"" + strResponse + "\""; // prepend folder delimiter

			strListProps = strAction+" (";
			if (!iFld->bMailFolder) {
				strListProps += "\\Noselect ";
			}
			if (!bSubscribedOnly) {
				// don't list flag on LSUB command
				if (iFld->bHasSubfolders) {
					strListProps += "\\HasChildren";
				} else {
					strListProps += "\\HasNoChildren";
				}
			}
			strListProps += ") ";

			strResponse = strListProps + strResponse;

			hr = HrResponse(RESP_UNTAGGED, strResponse);
			if (hr != hrSuccess)
				break;
		}
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, strAction+" completed");

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles STATUS command
 * 
 * Returns values for status items. Status items are:
 * @li \b MESSAGES	The number of messages in the mailbox.
 * @li \b RECENT	The number of messages with the \Recent flag set.
 * @li \b UIDNEXT	The next unique identifier value of the mailbox.
 * @li \b UIDVALIDITY The unique identifier validity value of the mailbox.
 * @li \b UNSEEN	The number of messages which do not have the \Seen flag set.
 *
 * @note dovecot seems to be the only one enforcing strStatusData to be a list, and with actual items
 * @note courier doesn't return NIL for unknown status items
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strFolder The folder to query the status of, in IMAP UTF-7 charset
 * @param[in] strStatusData A list of status items to query
 * 
 * @return MAPI error code
 */
HRESULT IMAP::HrCmdStatus(const string &strTag, const string &strFolder, string strStatusData) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	list<SFolder>::const_iterator lpFolder;
	IMAPIFolder *lpStatusFolder = NULL;
	vector<string> lstStatusData;
	string strData;
	string strResponse;
	char szBuffer[11];
	ULONG ulCounter;
	ULONG ulMessages = 0;
	ULONG ulUnseen = 0;
	ULONG ulUIDValidity = 0;
	ULONG ulRecent = 0;
	ULONG cStatusData = 0;
	IMAPITable *lpTable = NULL;
	ULONG cValues;
	SizedSPropTagArray(3, sPropsFolderCounters) = { 3, { PR_CONTENT_COUNT, PR_CONTENT_UNREAD, PR_EC_HIERARCHYID } };
	LPSPropValue lpPropCounters = NULL;
	wstring strIMAPFolder;
	SPropValue sPropMaxID;
	LPSPropValue lpPropMaxID = NULL;
    SRestriction sRestrictRecent;
    
	if (!lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "STATUS error no session");
		goto exit;
	}

	hr = IMAP2MAPICharset(strFolder, strIMAPFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "STATUS invalid folder name");
		goto exit;
	}

	ToUpper(strStatusData);
	ToUpper(strIMAPFolder);

	hr = HrFindFolder(strIMAPFolder, false, &lpStatusFolder);
	if(hr != hrSuccess) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "STATUS error finding folder");
		goto exit;
	}

	if (!IsMailFolder(lpStatusFolder)) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "STATUS error no mail folder");
		goto exit;
	}

	hr = lpStatusFolder->GetProps((LPSPropTagArray)&sPropsFolderCounters, 0, &cValues, &lpPropCounters);
	if (FAILED(hr)) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "STATUS error fetching folder content counters");
		goto exit;
	}
	hr = hrSuccess;

	if (lpPropCounters[0].ulPropTag == PR_CONTENT_COUNT)
		ulMessages = lpPropCounters[0].Value.ul;

	if (lpPropCounters[1].ulPropTag == PR_CONTENT_UNREAD)
		ulUnseen = lpPropCounters[1].Value.ul;

	if (lpPropCounters[2].ulPropTag == PR_EC_HIERARCHYID)
		ulUIDValidity = lpPropCounters[2].Value.ul;

	// split statusdata
	if (strStatusData.size() > 1 && strStatusData[0] == '(') {
		strStatusData.erase(0,1);
		strStatusData.resize(strStatusData.size()-1);
	}

	HrSplitInput(strStatusData, lstStatusData);

	// loop statusdata
	cStatusData = lstStatusData.size();
	strResponse = "STATUS \"";
	strResponse += strFolder;	// strFolder is in upper case, works with all clients?
	strResponse += "\" (";
	for (ulCounter = 0; ulCounter < cStatusData; ++ulCounter) {
		strData = lstStatusData[ulCounter];
		strResponse += strData;
		strResponse += " ";

		if (strData.compare("MESSAGES") == 0) {
			snprintf(szBuffer, 10, "%u", ulMessages);
			strResponse += szBuffer;
		} else if (strData.compare("RECENT") == 0) {
            // Get 'recent' count from the table
            HrGetOneProp(lpStatusFolder, PR_EC_IMAP_MAX_ID, &lpPropMaxID);

            if(lpPropMaxID) {
                hr = lpStatusFolder->GetContentsTable(MAPI_DEFERRED_ERRORS, &lpTable);
                if(hr != hrSuccess) {
                    hr2 = HrResponse(RESP_TAGGED_NO, strTag, "STATUS error getting contents");
                    goto exit;
                }

                sPropMaxID.ulPropTag = PR_EC_IMAP_ID;
                sPropMaxID.Value.ul = lpPropMaxID->Value.ul;
                
                sRestrictRecent.rt = RES_PROPERTY;
                sRestrictRecent.res.resProperty.ulPropTag = PR_EC_IMAP_ID;
                sRestrictRecent.res.resProperty.relop = RELOP_GT;
                sRestrictRecent.res.resProperty.lpProp = &sPropMaxID;
                
                hr = lpTable->Restrict(&sRestrictRecent, TBL_BATCH);
                if(hr != hrSuccess) {
                    hr2 = HrResponse(RESP_TAGGED_NO, strTag, "STATUS error getting recent");
                    goto exit;
                }
                
                hr = lpTable->GetRowCount(0, &ulRecent);
                if(hr != hrSuccess) {
                    hr2 = HrResponse(RESP_TAGGED_NO, strTag, "STATUS error getting row count for recent");
                    goto exit;
                }
            } else {
                // No max id, all messages are Recent
                ulRecent = ulMessages;
            }
            
			snprintf(szBuffer, 10, "%u", ulRecent);
			strResponse += szBuffer;
		} else if (strData.compare("UIDNEXT") == 0) {
			// although m_ulLastUid is from the current selected folder, and not the requested status folder,
			// the hierarchy id should become the same, and it's not a value the imap client can use anyway.
			snprintf(szBuffer, 10, "%u", m_ulLastUid + 1);
			strResponse += szBuffer;
		} else if (strData.compare("UIDVALIDITY") == 0) {
			snprintf(szBuffer, 10, "%u", ulUIDValidity);
			strResponse += szBuffer;
		} else if (strData.compare("UNSEEN") == 0) {
			snprintf(szBuffer, 10, "%u", ulUnseen);
			strResponse += szBuffer;
		} else {
			strResponse += "NIL";
		}
		
		if (ulCounter+1 < cStatusData)
			strResponse += " ";
	}

	strResponse += ")";
	hr = HrResponse(RESP_UNTAGGED, strResponse);
	if (hr == hrSuccess)
		hr = HrResponse(RESP_TAGGED_OK, strTag, "STATUS completed");

exit:
	MAPIFreeBuffer(lpPropMaxID);
        
	if (lpTable)
		lpTable->Release();
        
	MAPIFreeBuffer(lpPropCounters);
	if (lpStatusFolder)
		lpStatusFolder->Release();

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the APPEND command
 * 
 * Create a new mail message in the given folder, and possebly set
 * flags and received time on the new message.
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strFolderParam the folder to create the message in, in IMAP UTF-7 charset
 * @param[in] strData the RFC-822 formatted email to save
 * @param[in] strFlags optional, contains a list of extra flags to save on the message (eg. \Seen)
 * @param[in] strTime optional, a timestamp for the message: internal date is received date
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdAppend(const string &strTag, const string &strFolderParam, const string &strData, string strFlags, const string &strTime) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	list<SFolder>::const_iterator lpFolder;
	IMAPIFolder *lpAppendFolder = NULL;
	LPMESSAGE lpMessage = NULL;
	vector<string> lstFlags;
	ULONG ulCounter;
	string strFlag;
	LPSPropValue lpPropVal = NULL;
	wstring strFolder;
	string strAppendUid;
	ULONG ulFolderUid = 0;
	ULONG ulMsgUid = 0;
	SizedSPropTagArray(10, delFrom) = { 10, {
			PR_SENT_REPRESENTING_ADDRTYPE_W, PR_SENT_REPRESENTING_NAME_W,
			PR_SENT_REPRESENTING_EMAIL_ADDRESS_W, PR_SENT_REPRESENTING_ENTRYID, PR_SENT_REPRESENTING_SEARCH_KEY,
			PR_SENDER_ADDRTYPE_W, PR_SENDER_NAME_W,
			PR_SENDER_EMAIL_ADDRESS_W, PR_SENDER_ENTRYID, PR_SENDER_SEARCH_KEY
		} };
	
	if (!lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "APPEND error no session");
		goto exit;
	}

	hr = IMAP2MAPICharset(strFolderParam, strFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "APPEND invalid folder name");
		goto exit;
	}

	hr = HrFindFolder(strFolder, false, &lpAppendFolder);
	if(hr != hrSuccess) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "[TRYCREATE] APPEND error finding folder");
		goto exit;
	}

	if (!IsMailFolder(lpAppendFolder)) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "APPEND error not a mail folder");
		goto exit;
	}

	hr = HrGetOneProp(lpAppendFolder, PR_EC_HIERARCHYID, &lpPropVal);
	if (hr == hrSuccess) {
		ulFolderUid = lpPropVal->Value.ul;
		MAPIFreeBuffer(lpPropVal);
		lpPropVal = NULL;
	}

	hr = lpAppendFolder->CreateMessage(NULL, 0, &lpMessage);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "APPEND error creating message");
		goto exit;
	}

	hr = IMToMAPI(lpSession, lpStore, lpAddrBook, lpMessage, strData, dopt, lpLogger);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "APPEND error converting message");
		goto exit;
	}

	if (IsSentItemFolder(lpAppendFolder) ) {
		// needed for blackberry
		if (HrGetOneProp(lpAppendFolder, PR_ENTRYID, &lpPropVal) == hrSuccess) {
			lpPropVal->ulPropTag = PR_SENTMAIL_ENTRYID;
			HrSetOneProp(lpMessage, lpPropVal);

			MAPIFreeBuffer(lpPropVal);
			lpPropVal = NULL;
		}
	}

	if (strFlags.size() > 2 && strFlags[0] == '(') {
		// remove () around flags
		strFlags.erase(0, 1);
		strFlags.erase(strFlags.size()-1, 1);
	}

	ToUpper(strFlags);
	HrSplitInput(strFlags, lstFlags);

	for (ulCounter = 0; ulCounter < lstFlags.size(); ++ulCounter) {
		strFlag = lstFlags[ulCounter];

		if (strFlag.compare("\\SEEN") == 0) {
			MAPIFreeBuffer(lpPropVal);
			lpPropVal = NULL;

			if (HrGetOneProp(lpMessage, PR_MESSAGE_FLAGS, &lpPropVal) != hrSuccess) {
				hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) & lpPropVal);
				if (hr != hrSuccess)
					goto exit;

				lpPropVal->ulPropTag = PR_MESSAGE_FLAGS;
				lpPropVal->Value.ul = 0;
			}

			lpPropVal->Value.ul |= MSGFLAG_READ;
			HrSetOneProp(lpMessage, lpPropVal);
		} else if (strFlag.compare("\\DRAFT") == 0) {
			MAPIFreeBuffer(lpPropVal);
			lpPropVal = NULL;

			if (HrGetOneProp(lpMessage, PR_MSG_STATUS, &lpPropVal) != hrSuccess) {
				hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) & lpPropVal);
				if (hr != hrSuccess)
					goto exit;

				lpPropVal->ulPropTag = PR_MSG_STATUS;
				lpPropVal->Value.ul = 0;
			}

			lpPropVal->Value.ul |= MSGSTATUS_DRAFT;
			HrSetOneProp(lpMessage, lpPropVal);

			// When saving a draft, also mark it as UNSENT, so webaccess opens the editor, not the viewer
			MAPIFreeBuffer(lpPropVal);
			lpPropVal = NULL;

			if (HrGetOneProp(lpMessage, PR_MESSAGE_FLAGS, &lpPropVal) != hrSuccess) {
				hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) & lpPropVal);
				if (hr != hrSuccess)
					goto exit;

				lpPropVal->ulPropTag = PR_MESSAGE_FLAGS;
				lpPropVal->Value.ul = 0;
			}

			lpPropVal->Value.ul |= MSGFLAG_UNSENT;
			HrSetOneProp(lpMessage, lpPropVal);

			// remove "from" properties, and ignore error
			lpMessage->DeleteProps((LPSPropTagArray)&delFrom, NULL);
		} else if (strFlag.compare("\\FLAGGED") == 0) {
			if (lpPropVal == NULL) {
				hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) & lpPropVal);
				if (hr != hrSuccess)
					goto exit;
			}

			lpPropVal->ulPropTag = PR_FLAG_STATUS;
			lpPropVal->Value.ul = 2;
			HrSetOneProp(lpMessage, lpPropVal);
		} else if (strFlag.compare("\\ANSWERED") == 0 || strFlag.compare("$FORWARDED") == 0) {
			MAPIFreeBuffer(lpPropVal);
			lpPropVal = NULL;

			if (HrGetOneProp(lpMessage, PR_MSG_STATUS, &lpPropVal) != hrSuccess) {
				hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) & lpPropVal);
				if (hr != hrSuccess)
					goto exit;

				lpPropVal->ulPropTag = PR_MSG_STATUS;
				lpPropVal->Value.ul = 0;
			}

			if (strFlag[0] == '\\') {
				lpPropVal->Value.ul |= MSGSTATUS_ANSWERED;
				HrSetOneProp(lpMessage, lpPropVal);
			}

			MAPIFreeBuffer(lpPropVal);
			lpPropVal = NULL;

			hr = MAPIAllocateBuffer(sizeof(SPropValue) * 3, (LPVOID *) & lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpPropVal[0].ulPropTag = PR_LAST_VERB_EXECUTED;
			if (strFlag[0] == '\\')
				lpPropVal[0].Value.ul = NOTEIVERB_REPLYTOSENDER;
			else
				lpPropVal[0].Value.ul = NOTEIVERB_FORWARD;
			lpPropVal[1].ulPropTag = PR_LAST_VERB_EXECUTION_TIME;
			GetSystemTimeAsFileTime(&lpPropVal[1].Value.ft);
			lpPropVal[2].ulPropTag = PR_ICON_INDEX;
			if (strFlag[0] == '\\')
				lpPropVal[2].Value.ul = ICON_MAIL_REPLIED;
			else
				lpPropVal[2].Value.ul = ICON_MAIL_FORWARDED;

			hr = lpMessage->SetProps(3, lpPropVal, NULL);
			if (hr != hrSuccess)
				goto exit;
		} else if (strFlag.compare("\\DELETED") == 0) {
			MAPIFreeBuffer(lpPropVal);
			lpPropVal = NULL;

			if (HrGetOneProp(lpMessage, PR_MSG_STATUS, &lpPropVal) != hrSuccess) {
				hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) & lpPropVal);
				if (hr != hrSuccess)
					goto exit;

				lpPropVal->ulPropTag = PR_MSG_STATUS;
				lpPropVal->Value.ul = 0;
			}

			lpPropVal->Value.ul |= MSGSTATUS_DELMARKED;
			// what, new deleted mail? moved deleted mail? uh?
			// @todo imap_expunge_on_delete
			HrSetOneProp(lpMessage, lpPropVal);
		}
	}

	// set time
	if (lpPropVal == NULL) {
		hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) & lpPropVal);
		if (hr != hrSuccess)
			goto exit;
	}

	if (!strTime.empty()) {
		lpPropVal->Value.ft = StringToFileTime(strTime);
		lpPropVal->ulPropTag = PR_MESSAGE_DELIVERY_TIME;
		HrSetOneProp(lpMessage, lpPropVal);
		
		lpPropVal->Value.ft = StringToFileTime(strTime, true);
		lpPropVal->ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
		HrSetOneProp(lpMessage, lpPropVal);
	}

	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE | FORCE_SAVE);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "APPEND error saving message");
		goto exit;
	}

	hr = HrGetOneProp(lpMessage, PR_EC_IMAP_ID, &lpPropVal);
	if (hr == hrSuccess) {
		ulMsgUid = lpPropVal->Value.ul;
		MAPIFreeBuffer(lpPropVal);
		lpPropVal = NULL;
	}

	if (ulMsgUid && ulFolderUid)
		strAppendUid = string("[APPENDUID ") + stringify(ulFolderUid) + " " + stringify(ulMsgUid) + "] ";

	if (strCurrentFolder == strFolder) {
	    // Fixme, add the appended message instead of HrRefreshFolderMails; the message is now seen as Recent
		HrRefreshFolderMails(false, !bCurrentFolderReadOnly, false, NULL);
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, strAppendUid+"APPEND completed");

exit:
	MAPIFreeBuffer(lpPropVal);
	if (lpMessage)
		lpMessage->Release();

	if (lpAppendFolder)
		lpAppendFolder->Release();

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the CHECK command
 * 
 * For us, the same as NOOP. @todo merge with noop command
 *
 * @param[in] strTag the IMAP tag for this command
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdCheck(const string &strTag) {
    HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
    
	hr = HrRefreshFolderMails(false, !bCurrentFolderReadOnly, false, NULL);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "CHECK error reading folder messages");
		goto exit;
	}
    
	hr = HrResponse(RESP_TAGGED_OK, strTag, "CHECK completed");

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the CLOSE command
 *
 * Closes the current selected mailbox, expunging any \Delete marked
 * messages in this mailbox.
 * 
 * @param[in] strTag the IMAP tag for this command
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdClose(const string &strTag) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "CLOSE error no folder");
		goto exit;
	}

	// close old contents table if cached version was open
	ReleaseContentsCache();

	if (bCurrentFolderReadOnly) {
		// cannot expunge messages on a readonly folder
		hr = HrResponse(RESP_TAGGED_OK, strTag, "CLOSE completed");
		goto exit;
	}

	hr = HrExpungeDeleted(strTag, "CLOSE", NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = HrResponse(RESP_TAGGED_OK, strTag, "CLOSE completed");

exit:
	strCurrentFolder.clear();	// always "close" the SELECT command

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the EXPUNGE command
 * 
 * All \Deleted marked emails will actually be removed (softdeleted in
 * Kopano). Optional is the sequence set (UIDPLUS extension), which
 * messages only must be expunged if \Deleted flag was marked AND are
 * present in this sequence.
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strStatusData optional sequence set
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdExpunge(const string &strTag, const string &strSeqSet) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	list<ULONG> lstMails;
	LPSRestriction lpRestriction = NULL;
	string strCommand;

	if (strSeqSet.empty())
		strCommand = "EXPUNGE";
	else
		strCommand = "UID EXPUNGE";

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strCommand+" error no folder");
		goto exit;
	}

	if (bCurrentFolderReadOnly) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strCommand+" error folder read only");
		goto exit;
	}

	// UID EXPUNGE is always passed UIDs
	hr = HrSeqUidSetToRestriction(strSeqSet, &lpRestriction);
	if (hr != hrSuccess)
		goto exit;

	hr = HrExpungeDeleted(strTag, strCommand, lpRestriction);
	if (hr != hrSuccess)
		goto exit;
    
	// Let HrRefreshFolderMails output the actual EXPUNGEs
	HrRefreshFolderMails(false, !bCurrentFolderReadOnly, false, NULL);

	hr = HrResponse(RESP_TAGGED_OK, strTag, strCommand+" completed");

exit:
	if (lpRestriction)
		FREE_RESTRICTION(lpRestriction);

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the SEARCH command
 *
 * Searches in the current selected folder using the given search
 * criteria.
 * 
 * @param[in] strTag the IMAP tag for this command
 * @param[in] lstSearchCriteria The clients search options.
 * @param[in] bUidMode UID SEARCH (true) or flat ID SEARCH (false)
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdSearch(const string &strTag, vector<string> &lstSearchCriteria, bool bUidMode) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	list<ULONG> lstMailnr;
	ULONG ulCriterianr = 0;
	string strResponse;
	char szBuffer[33];
	ECIConv *iconv = NULL;
	string strMode;

	if (bUidMode)
		strMode = "UID ";

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+"SEARCH error no folder");
		goto exit;
	}

	// don't support other charsets
	// @todo unicode searches
	if (lstSearchCriteria[0].compare("CHARSET") == 0) {
		if (lstSearchCriteria[1] != "WINDOWS-1252") {
			iconv = new ECIConv("windows-1252", lstSearchCriteria[1]);
			if (!iconv->canConvert()) {
				hr2 = HrResponse(RESP_TAGGED_NO, strTag, "[BADCHARSET (WINDOWS-1252)] " + strMode + "SEARCH charset not supported");
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}
		}
		ulCriterianr += 2;
	}

	hr = HrSearch(lstSearchCriteria, ulCriterianr, lstMailnr, iconv);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+"SEARCH error");
		goto exit;
	}

	strResponse = "SEARCH";

	for (auto nr : lstMailnr) {
		snprintf(szBuffer, 32, " %u", bUidMode ? lstFolderMailEIDs[nr].ulUid : nr + 1);
		strResponse += szBuffer;
	}

	hr = HrResponse(RESP_UNTAGGED, strResponse);
	if (hr == hrSuccess)
		hr = HrResponse(RESP_TAGGED_OK, strTag, strMode+"SEARCH completed");

exit:
	delete iconv;
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the FETCH command
 * 
 * Fetch specified parts of a list of emails.
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strSeqSet an IMAP sequence of IDs or UIDs depending on bUidMode
 * @param[in] strMsgDataItemNames The parts of the emails to fetch
 * @param[in] bUidMode use UID (true) or ID (false) numbers
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdFetch(const string &strTag, const string &strSeqSet, const string &strMsgDataItemNames, bool bUidMode) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	vector<string> lstDataItems;
	list<ULONG> lstMails;
	ULONG ulCurrent = 0;
	bool bFound = false;
	string strMode;

	if (bUidMode)
		strMode = "UID ";

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_BAD, strTag, strMode+"FETCH error no folder");
		goto exit;
	}

	HrGetDataItems(strMsgDataItemNames, lstDataItems);
	if (bUidMode) {
		for (ulCurrent = 0; !bFound && ulCurrent < lstDataItems.size(); ++ulCurrent) {
			if (lstDataItems[ulCurrent].compare("UID") == 0)
				bFound = true;
		}
		if (!bFound)
			lstDataItems.push_back("UID");
	}

	if (bUidMode)
		hr = HrParseSeqUidSet(strSeqSet, lstMails);
	else
		hr = HrParseSeqSet(strSeqSet, lstMails);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+"FETCH sequence parse error in: " + strSeqSet);
		goto exit;
	}

	hr = HrPropertyFetch(lstMails, lstDataItems);
	if (hr != hrSuccess)
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+"FETCH failed");
	else
		hr2 = HrResponse(RESP_TAGGED_OK, strTag, strMode+"FETCH completed");

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the STORE command
 * 
 * Sets, appends or removes flags from a list of emails.
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strSeqSet an IMAP sequence of IDs or UIDs depending on bUidMode
 * @param[in] strMsgDataItemName contains the FLAGS modifier command
 * @param[in] strMsgDataItemValue contains a list of flags to modify the emails with
 * @param[in] bUidMode use UID (true) or ID (false) numbers
 * 
 * @return MAPI error code
 */
HRESULT IMAP::HrCmdStore(const string &strTag, const string &strSeqSet, const string &strMsgDataItemName, const string &strMsgDataItemValue, bool bUidMode) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	list<ULONG> lstMails;
	vector<string> lstDataItems;
	string strMode;
	bool bDelete = false;

	if (bUidMode)
		strMode = "UID";
	strMode += " STORE";

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+" error no folder");
		goto exit;
	}

	if (bCurrentFolderReadOnly) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+" error folder read only");
		goto exit;
	}

	lstDataItems.push_back("FLAGS");
	if (bUidMode)
		lstDataItems.push_back("UID");

	if (bUidMode)
		hr = HrParseSeqUidSet(strSeqSet, lstMails);
	else
		hr = HrParseSeqSet(strSeqSet, lstMails);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+" sequence parse error in: " + strSeqSet);
		goto exit;
	}

	hr = HrStore(lstMails, strMsgDataItemName, strMsgDataItemValue, &bDelete);
	if (hr != MAPI_E_NOT_ME) {
		HrPropertyFetch(lstMails, lstDataItems);
		hr = hrSuccess;
	}

	if (bDelete && parseBool(lpConfig->GetSetting("imap_expunge_on_delete"))) {
		SRestrictionPtr lpRestriction;

		if (bUidMode) {
			hr = HrSeqUidSetToRestriction(strSeqSet, &lpRestriction);
			if (hr != hrSuccess)
				goto exit;
		}

		if (HrExpungeDeleted(strTag, strMode, lpRestriction) != hrSuccess)
			// HrExpungeDeleted sent client NO result.
			goto exit;

		// Let HrRefreshFolderMails output the actual EXPUNGEs
		HrRefreshFolderMails(false, !bCurrentFolderReadOnly, false, NULL);
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, strMode+" completed");

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the COPY command
 * 
 * Copy a list of emails from the current selected folder to the given folder.
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strSeqSet an IMAP sequence of IDs or UIDs depending on bUidMode
 * @param[in] strFolder the folder to copy the message to, in IMAP UTF-7 charset
 * @param[in] bUidMode use UID (true) or ID (false) numbers
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdCopy(const string &strTag, const string &strSeqSet, const string &strFolder, bool bUidMode) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	list<ULONG> lstMails;
	string strMode;

	if (bUidMode)
		strMode = "UID ";

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+"COPY error no folder");
		goto exit;
	}

	if (bUidMode)
		hr = HrParseSeqUidSet(strSeqSet, lstMails);
	else
		hr = HrParseSeqSet(strSeqSet, lstMails);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+"COPY sequence parse error in: " + strSeqSet);
		goto exit;
	}

	hr = HrCopy(lstMails, strFolder, false);
	if (hr == MAPI_E_NOT_FOUND) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "[TRYCREATE] "+strMode+"COPY folder not found");
		goto exit;
	} else if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strMode+"COPY error");
		goto exit;
	}

	hr = HrResponse(RESP_TAGGED_OK, strTag, strMode+"COPY completed");

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the UID XAOL-MOVE command (non-RFC command)
 * 
 * This extension, currently only used by thunderbird, moves a list of
 * emails into the given folder. Normally IMAP can only move mails
 * with COPY/STORE(\Deleted) commands.
 *
 * @param[in] strTag the IMAP tag for this command
 * @param[in] strSeqSet an IMAP sequence of IDs or UIDs depending on bUidMode
 * @param[in] strFolder the folder to copy the message to, in IMAP UTF-7 charset
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdUidXaolMove(const string &strTag, const string &strSeqSet, const string &strFolder) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	list<ULONG> lstMails;

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "UID XAOL-MOVE error no folder");
		goto exit;
	}

	hr = HrParseSeqUidSet(strSeqSet, lstMails);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "UID XAOL-MOVE sequence parse error in: " + strSeqSet);
		goto exit;
	}

	hr = HrCopy(lstMails, strFolder, true);
	if (hr == MAPI_E_NOT_FOUND) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "[TRYCREATE] UID XAOL-MOVE folder not found");
		goto exit;
	} else if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "UID XAOL-MOVE error");
		goto exit;
	}

	// Let HrRefreshFolderMails output the actual EXPUNGEs
	HrRefreshFolderMails(false, !bCurrentFolderReadOnly, false, NULL);

	hr = HrResponse(RESP_TAGGED_OK, strTag, "UID XAOL-MOVE completed");

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * Convert an MAPI array of properties (from a table) to an IMAP FLAGS list.
 * 
 * @todo, flags always are in a list, so this function should add the ()
 *
 * @param[in] lpProps Array of MAPI properties
 * @param[in] cValues Number of properties in lpProps
 * @param[in] bRecent Add the recent flag to the list
 * 
 * @return string with IMAP Flags
 */
std::string IMAP::PropsToFlags(LPSPropValue lpProps, unsigned int cValues, bool bRecent, bool bRead) {
	string strFlags;
	const SPropValue *lpMessageFlags = PpropFindProp(lpProps, cValues, PR_MESSAGE_FLAGS);
	const SPropValue *lpFlagStatus = PpropFindProp(lpProps, cValues, PR_FLAG_STATUS);
	const SPropValue *lpMsgStatus = PpropFindProp(lpProps, cValues, PR_MSG_STATUS);
	const SPropValue *lpLastVerb = PpropFindProp(lpProps, cValues, PR_LAST_VERB_EXECUTED);

	if ((lpMessageFlags && lpMessageFlags->Value.ul & MSGFLAG_READ) || bRead) {
		strFlags += "\\Seen ";
	}

	if (lpFlagStatus && lpFlagStatus->Value.ul != 0) {
		strFlags += "\\Flagged ";
	}

	if (lpLastVerb) {
		if ((lpLastVerb->Value.ul == NOTEIVERB_REPLYTOSENDER || lpLastVerb->Value.ul == NOTEIVERB_REPLYTOALL)) {
			strFlags += "\\Answered ";
		}

		// there is no flag in imap for forwards. thunderbird uses the custom flag $Forwarded,
		// and this is the only custom flag we support.
		if (lpLastVerb->Value.ul == NOTEIVERB_FORWARD) {
			strFlags += "$Forwarded ";
		}
	}

	if (lpMsgStatus) {
		if (lpMsgStatus->Value.ul & MSGSTATUS_DRAFT) {
			strFlags += "\\Draft ";
		}

		if (!lpLastVerb && lpMsgStatus->Value.ul & MSGSTATUS_ANSWERED) {
			strFlags += "\\Answered ";
		}

		if (lpMsgStatus->Value.ul & MSGSTATUS_DELMARKED) {
			strFlags += "\\Deleted ";
		}
	}
	
	if (bRecent)
	    strFlags += "\\Recent ";
	
	// strip final space
	if (!strFlags.empty()) {
		strFlags.resize(strFlags.size() - 1);
	}

	return strFlags;
}

/** 
 * The notify callback function. Sends changes to the client on the
 * current selected folder during the IDLE command.
 * 
 * @param[in] lpContext callback data, containing "this" IMAP class
 * @param[in] cNotif Number of notification messages in lpNotif
 * @param[in] lpNotif Array of changes on the folder
 * 
 * @return MAPI Error code
 */
LONG __stdcall IMAPIdleAdviseCallback(void *lpContext, ULONG cNotif, LPNOTIFICATION lpNotif) {
	IMAP *lpIMAP = (IMAP*)lpContext;
	string strFlags;
	ULONG ulMailNr = 0;
	ULONG ulRecent = 0;
	bool bReload = false;
	vector<IMAP::SMail> oldMails;
	vector<IMAP::SMail>::iterator iterMail;
	vector<IMAP::SMail>::reverse_iterator riterOld, riterNew;
	enum { EID, IKEY, IMAPID, MESSAGE_FLAGS, FLAG_STATUS, MSG_STATUS, LAST_VERB, NUM_COLS };
	IMAP::SMail sMail;

	{
		// modify:
		// * [mailnr] FETCH (FLAGS (\Seen \Recent \Flagged \Deleted))
		// remove:
		// * [mailnr] EXPUNGE
		// new mail/add:
		// * [total] exists (optional?)
		// * 1 recent
	}

	if (!lpIMAP)
		return MAPI_E_CALL_FAILED;

	pthread_mutex_lock(&lpIMAP->m_mIdleLock);

	if (!lpIMAP->m_bIdleMode) {
		pthread_mutex_unlock(&lpIMAP->m_mIdleLock);
		return MAPI_E_CALL_FAILED;
	}

	for (ULONG i = 0; i < cNotif && !bReload; ++i) {
		if (lpNotif[i].ulEventType != fnevTableModified)
			continue;

		switch (lpNotif[i].info.tab.ulTableEvent) {
		case TABLE_ROW_ADDED:
			sMail.sEntryID = BinaryArray(lpNotif[i].info.tab.row.lpProps[EID].Value.bin);
			sMail.sInstanceKey = BinaryArray(lpNotif[i].info.tab.propIndex.Value.bin);
			sMail.bRecent = true;

			if (lpNotif[i].info.tab.row.lpProps[IMAPID].ulPropTag == PR_EC_IMAP_ID) {
				sMail.ulUid = lpNotif[i].info.tab.row.lpProps[IMAPID].Value.ul;
			}

			sMail.strFlags = lpIMAP->PropsToFlags(lpNotif[i].info.tab.row.lpProps, lpNotif[i].info.tab.row.cValues, true, false);
			lpIMAP->lstFolderMailEIDs.push_back(sMail);
			lpIMAP->m_ulLastUid = max(lpIMAP->m_ulLastUid, sMail.ulUid);
			++ulRecent;
			break;

		case TABLE_ROW_DELETED:
			// find number and print N EXPUNGE
			if (lpNotif[i].info.tab.propIndex.ulPropTag == PR_INSTANCE_KEY) {
			    for (iterMail = lpIMAP->lstFolderMailEIDs.begin(); iterMail != lpIMAP->lstFolderMailEIDs.end(); ++iterMail) {
			        if(iterMail->sInstanceKey == BinaryArray(lpNotif[i].info.tab.propIndex.Value.bin))
			            break;
				}
			    
				if (iterMail != lpIMAP->lstFolderMailEIDs.end()) {
					ulMailNr = iterMail - lpIMAP->lstFolderMailEIDs.begin();

					// remove mail from list
					lpIMAP->HrResponse(RESP_UNTAGGED, stringify(ulMailNr+1) + " EXPUNGE");

					lpIMAP->lstFolderMailEIDs.erase(iterMail);
				}
			}
			break;

		case TABLE_ROW_MODIFIED:
			// find number and print N FETCH (FLAGS (flags...))
			strFlags.clear();

			if (lpNotif[i].info.tab.row.lpProps[IMAPID].ulPropTag == PR_EC_IMAP_ID) {
				iterMail = find(lpIMAP->lstFolderMailEIDs.begin(), lpIMAP->lstFolderMailEIDs.end(), lpNotif[i].info.tab.row.lpProps[IMAPID].Value.ul);
				// not found probably means the client needs to sync
				if (iterMail != lpIMAP->lstFolderMailEIDs.end()) {
					ulMailNr = iterMail - lpIMAP->lstFolderMailEIDs.begin();

					strFlags = lpIMAP->PropsToFlags(lpNotif[i].info.tab.row.lpProps, lpNotif[i].info.tab.row.cValues, iterMail->bRecent, false);

					lpIMAP->HrResponse(RESP_UNTAGGED, stringify(ulMailNr+1) + " FETCH (FLAGS ("+strFlags+"))");
				}
			}
			break;

		case TABLE_RELOAD:
			// TABLE_RELOAD is unused in Kopano
		case TABLE_CHANGED:
            lpIMAP->HrRefreshFolderMails(false, !lpIMAP->bCurrentFolderReadOnly, false, NULL);
		    break;
		};
	}

	if (ulRecent) {
		lpIMAP->HrResponse(RESP_UNTAGGED, stringify(ulRecent) + " RECENT");
		lpIMAP->HrResponse(RESP_UNTAGGED, stringify(lpIMAP->lstFolderMailEIDs.size()) + " EXISTS");
	}

	pthread_mutex_unlock(&lpIMAP->m_mIdleLock);

	return S_OK;
}

/** 
 * @brief Handles the IDLE command
 * 
 * If we have a current selected folder, a notification advise is
 * created in another thread, which will print updates to the
 * client. The IMAP client can at anytime close the thread by exiting,
 * or sending the DONE command.
 *
 * @param[in] strTag the IMAP tag for this command
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdIdle(const string &strTag) {
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	LPMAPIFOLDER lpFolder = NULL;
	enum { EID, IKEY, IMAPID, MESSAGE_FLAGS, FLAG_STATUS, MSG_STATUS, LAST_VERB, NUM_COLS };
	SizedSPropTagArray(NUM_COLS, spt) = { NUM_COLS, {PR_ENTRYID, PR_INSTANCE_KEY, PR_EC_IMAP_ID, PR_MESSAGE_FLAGS, PR_FLAG_STATUS, PR_MSG_STATUS, PR_LAST_VERB_EXECUTED} };

	// Outlook (express) IDLEs without selecting a folder.
	// When sending an error from this command, Outlook loops on the IDLE command forever :(
	// Therefore, we can never return an HrResultBad() or ...No() here, so we always "succeed"

	m_strIdleTag = strTag;
	m_bIdleMode = true;

	if (strCurrentFolder.empty() || !lpSession) {
		hr = HrResponse(RESP_CONTINUE, "empty idle, nothing is going to happen");
		goto exit;
	}

	hr = HrFindFolder(strCurrentFolder, bCurrentFolderReadOnly, &lpFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_CONTINUE, "Can't open selected folder to idle in");
		goto exit;
	}

	hr = lpFolder->GetContentsTable(0, &m_lpIdleTable);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_CONTINUE, "Can't open selected contents table to idle in");
		goto exit;
	}

	hr = m_lpIdleTable->SetColumns((LPSPropTagArray) &spt, 0);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_CONTINUE, "Can't select colums on selected contents table for idle information");
		goto exit;
	}

	hr = HrAllocAdviseSink(IMAPIdleAdviseCallback, (void*)this, &m_lpIdleAdviseSink);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_CONTINUE, "Can't allocate memory to idle");
		goto exit;
	}

	pthread_mutex_lock(&m_mIdleLock);

	hr = m_lpIdleTable->Advise(fnevTableModified, m_lpIdleAdviseSink, &m_ulIdleAdviseConnection);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_CONTINUE, "Can't advise on current selected folder");
		pthread_mutex_unlock(&m_mIdleLock);
		goto exit;
	}

	// \o/ we really succeeded this time
	hr = HrResponse(RESP_CONTINUE, "waiting for notifications");

	pthread_mutex_unlock(&m_mIdleLock);

exit:
	if (hr != hrSuccess || hr2 != hrSuccess) {
		if (m_ulIdleAdviseConnection && m_lpIdleTable) {
			m_lpIdleTable->Unadvise(m_ulIdleAdviseConnection);
			m_ulIdleAdviseConnection = 0;
		}
		if (m_lpIdleAdviseSink) {
			m_lpIdleAdviseSink->Release();
			m_lpIdleAdviseSink = NULL;
		}
		if (m_lpIdleTable) {
			m_lpIdleTable->Release();
			m_lpIdleTable = NULL;
		}
	}

	if (lpFolder)
		lpFolder->Release();

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * @brief Handles the DONE command
 * 
 * The DONE command closes the previous IDLE command, and has no IMAP
 * tag. This function can be also called implicitly, so a response to
 * the client is not always required.
 *
 * @param[in] bSendResponse Send a response to the client or not
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrDone(bool bSendResponse) {
	HRESULT hr = hrSuccess;

	// TODO: maybe add sleep here, so thunderbird gets all notifications?

	pthread_mutex_lock(&m_mIdleLock);

	if (bSendResponse) {
		if (m_bIdleMode)
			hr = HrResponse(RESP_TAGGED_OK, m_strIdleTag, "IDLE complete");
		else
			hr = HrResponse(RESP_TAGGED_BAD, m_strIdleTag, "was not idling");
	}

	if (m_ulIdleAdviseConnection && m_lpIdleTable) {
		m_lpIdleTable->Unadvise(m_ulIdleAdviseConnection);
		m_ulIdleAdviseConnection = 0;
	}

	if (m_lpIdleAdviseSink)
		m_lpIdleAdviseSink->Release();
	m_lpIdleAdviseSink = NULL;

	m_ulIdleAdviseConnection = 0;
	m_bIdleMode = false;
	m_strIdleTag.clear();

	if (m_lpIdleTable)
		m_lpIdleTable->Release();
	m_lpIdleTable = NULL;

	pthread_mutex_unlock(&m_mIdleLock);

	return hr;
}

/** 
 * @brief Handles the NAMESPACE command
 *
 * We currently only support the user namespace.
 * 
 * @param[in] strTag the IMAP tag for this command
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCmdNamespace(const string &strTag) {
	HRESULT hr = hrSuccess;

	hr = HrResponse(RESP_UNTAGGED, string("NAMESPACE ((\"\" \"") + IMAP_HIERARCHY_DELIMITER + "\")) NIL NIL");
	if (hr != hrSuccess)
		goto exit;

	hr = HrResponse(RESP_TAGGED_OK, strTag, "NAMESPACE Completed");
	if (hr != hrSuccess)
		goto exit;

exit:
	return hr;
}

/** 
 * Sends a response to the client with specific quota information. We
 * only export the hard quota level to the IMAP client.
 * 
 * @param[in] strTag Tag of the IMAP command
 * @param[in] strQuotaRoot Which quota root is requested
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrPrintQuotaRoot(const string& strTag)
{
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	SizedSPropTagArray(2, sStoreProps) = { 2, { PR_MESSAGE_SIZE_EXTENDED, PR_QUOTA_RECEIVE_THRESHOLD } };
	LPSPropValue lpProps = NULL;
	ULONG cValues = 0;

	hr = lpStore->GetProps((LPSPropTagArray)&sStoreProps, 0, &cValues, &lpProps);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, "GetQuota MAPI Error");
		goto exit;
	}

	// only print quota if we have a level
	if (lpProps[1].Value.ul)
		hr = HrResponse(RESP_UNTAGGED, "QUOTA \"\" (STORAGE "+stringify(lpProps[0].Value.li.QuadPart / 1024)+" "+stringify(lpProps[1].Value.ul)+")");

exit:
	MAPIFreeBuffer(lpProps);
	if (hr2 != hrSuccess)
		return hr2;
	return hr;	
}

/** 
 * @brief Handles the GETQUOTAROOT command
 * 
 * Sends which quota roots are available (only hard quota).
 * 
 * @param[in] strTag Tag of the IMAP command
 * @param[in] strFolder The folder to request quota information on
 * 
 * @todo check if folder is in public and show no quota?
 *
 * @return 
 */
HRESULT IMAP::HrCmdGetQuotaRoot(const string &strTag, const string &strFolder)
{
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;

	if (!lpStore) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_BAD, strTag, "Login first");
		goto exit;
	}

	// @todo check if folder exists
	hr = HrResponse(RESP_UNTAGGED, "QUOTAROOT \""+strFolder+"\" \"\"");
	if (hr != hrSuccess)
		goto exit;

	hr = HrPrintQuotaRoot(strTag);
	if (hr != hrSuccess)
		goto exit;

	hr = HrResponse(RESP_TAGGED_OK, strTag, "GetQuotaRoot complete");

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * Get the quota value for a given quota root. We only have the "" quota root.
 * 
 * @param[in] strTag Tag of the IMAP command
 * @param[in] strQuotaRoot given quota root
 * 
 * @return 
 */
HRESULT IMAP::HrCmdGetQuota(const string &strTag, const string &strQuotaRoot)
{
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;

	if (!lpStore) {
		hr = MAPI_E_CALL_FAILED;
		hr2 = HrResponse(RESP_TAGGED_BAD, strTag, "Login first");
		goto exit;
	}

	if (strQuotaRoot.empty()) {
		hr = HrPrintQuotaRoot(strTag);
		if (hr != hrSuccess)
			goto exit;

		hr = HrResponse(RESP_TAGGED_OK, strTag, "GetQuota complete");
		if (hr != hrSuccess)
			goto exit;
	} else {
		hr = HrResponse(RESP_TAGGED_NO, strTag, "Quota root does not exist");
		goto exit;
	}

exit:
	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

HRESULT IMAP::HrCmdSetQuota(const string &strTag, const string &strQuotaRoot, const string &strQuotaList)
{
	return HrResponse(RESP_TAGGED_NO, strTag, "SetQuota Permission denied");
}

/** 
 * Returns the idle state.
 * 
 * @return If the last client command was IDLE
 */
bool IMAP::isIdle() {
	return m_bIdleMode;
}

/** 
 * A command has sent a continuation response, and requires more data
 * from the client. This is currently only used in the AUTHENTICATE
 * command, other continuations are already handled in the main loop.
 * 
 * @return Last response to the client was a continuation request.
 */
bool IMAP::isContinue() {
	return m_bContinue;
}

/** 
 * Send an untagged response.
 * 
 * @param[in] strUntag Either RESP_UNTAGGED or RESP_CONTINUE
 * @param[in] strResponse Status information to send to the client
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrResponse(const string &strUntag, const string &strResponse)
{
    // Early cutoff of debug messages. This means the current process's config
    // determines if we log debug info (so HUP will only affect new processes if
    // you want debug output)
	if (lpLogger->Log(EC_LOGLEVEL_DEBUG))
		lpLogger->Log(EC_LOGLEVEL_DEBUG, "> %s%s", strUntag.c_str(), strResponse.c_str());
	return lpChannel->HrWriteLine(strUntag + strResponse);
}

/** 
 * Tagged response to the client. You may only send one tagged
 * response to the client per received command.
 * 
 * @note be careful when sending NO or BAD results: some clients may try the same command over and over.
 *
 * @param[in] strTag The tag received from the client for a command
 * @param[in] strResult The result of the command, either RESP_TAGGED_OK, RESP_TAGGED_NO or RESP_TAGGED_BAD
 * @param[in] strResponse The result of the command to send to the client
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrResponse(const string &strResult, const string &strTag, const string &strResponse)
{
	unsigned int max_err;

	max_err = strtoul(lpConfig->GetSetting("imap_max_fail_commands"), NULL, 0);

	// Some clients keep looping, so if we keep sending errors, just disconnect the client.
	if (strResult.compare(RESP_TAGGED_OK) == 0)
		m_ulErrors = 0;
	else
		++m_ulErrors;
	if (m_ulErrors >= max_err) {
		lpLogger->Log(EC_LOGLEVEL_ERROR, "Disconnecting client of user %ls because too many (%u) erroneous commands received, last reply:", m_strwUsername.c_str(), max_err);
		lpLogger->Log(EC_LOGLEVEL_ERROR, "%s%s%s", strTag.c_str(), strResult.c_str(), strResponse.c_str());
		return MAPI_E_END_OF_SESSION;
	}
		
	if (lpLogger->Log(EC_LOGLEVEL_DEBUG))
		lpLogger->Log(EC_LOGLEVEL_DEBUG, "> %s%s%s", strTag.c_str(), strResult.c_str(), strResponse.c_str());
	return lpChannel->HrWriteLine(strTag + strResult + strResponse);
}

/** 
 * Remove \Deleted marked message from current selected folder. Only
 * response in case of an error.
 * 
 * @param[in] strTag IMAP tag given for command
 * @param[in] strCommand IMAP command which triggered the expunge
 * @param[in] lpUIDRestriction optional restriction to limit messages actually deleted (see HrCmdExpunge)
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrExpungeDeleted(const std::string &strTag,
    const std::string &strCommand, const SRestriction *lpUIDRestriction)
{
	HRESULT hr = hrSuccess;
	HRESULT hr2 = hrSuccess;
	IMAPIFolder *lpFolder = NULL;
	ENTRYLIST sEntryList;
	LPSRestriction lpRootRestrict = NULL;
	LPMAPITABLE lpTable = NULL;
	LPSRowSet lpRows = NULL;
	enum { EID, NUM_COLS };
	SizedSPropTagArray(NUM_COLS, spt) = { NUM_COLS, {PR_ENTRYID} };

	sEntryList.lpbin = NULL;

	hr = HrFindFolder(strCurrentFolder, bCurrentFolderReadOnly, &lpFolder);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strCommand + " error opening folder");
		goto exit;
	}

	hr = lpFolder->GetContentsTable(MAPI_DEFERRED_ERRORS , &lpTable);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strCommand + " error opening folder contents");
		goto exit;
	}

	CREATE_RESTRICTION(lpRootRestrict);
	if (lpUIDRestriction) {
		CREATE_RES_AND(lpRootRestrict, lpRootRestrict, 3);
		// lazy copy
		lpRootRestrict->res.resAnd.lpRes[2] = *lpUIDRestriction;
	} else {
		CREATE_RES_AND(lpRootRestrict, lpRootRestrict, 2);
	}

	DATA_RES_EXIST(lpRootRestrict, lpRootRestrict->res.resAnd.lpRes[0], PR_MSG_STATUS);
	DATA_RES_BITMASK(lpRootRestrict, lpRootRestrict->res.resAnd.lpRes[1], BMR_NEZ, PR_MSG_STATUS, MSGSTATUS_DELMARKED); 

	hr = HrQueryAllRows(lpTable, (LPSPropTagArray) &spt, lpRootRestrict, NULL, 0, &lpRows);
	if (hr != hrSuccess) {
		hr2 = HrResponse(RESP_TAGGED_NO, strTag, strCommand + " error queryring rows");
		goto exit;
	}

	if(lpRows->cRows) {
        sEntryList.cValues = 0;
        if ((hr = MAPIAllocateBuffer(sizeof(SBinary) * lpRows->cRows, (LPVOID *) &sEntryList.lpbin)) != hrSuccess)
		goto exit;

        for (ULONG ulMailnr = 0; ulMailnr < lpRows->cRows; ++ulMailnr) {
			hr = lpFolder->SetMessageStatus(lpRows->aRow[ulMailnr].lpProps[EID].Value.bin.cb, (LPENTRYID)lpRows->aRow[ulMailnr].lpProps[EID].Value.bin.lpb,
											0, ~MSGSTATUS_DELMARKED, NULL);
			if (hr != hrSuccess)
				lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to update message status flag during " + strCommand);

            sEntryList.lpbin[sEntryList.cValues++] = lpRows->aRow[ulMailnr].lpProps[EID].Value.bin;
        }

        hr = lpFolder->DeleteMessages(&sEntryList, 0, NULL, 0);
        if (hr != hrSuccess) {
            hr2 = HrResponse(RESP_TAGGED_NO, strTag, strCommand + " error deleting messages");
            goto exit;
        }
    }

exit:
	MAPIFreeBuffer(sEntryList.lpbin);
	if (lpRootRestrict)
		FREE_RESTRICTION(lpRootRestrict);

	if (lpRows)
		FreeProws(lpRows);

	if (lpTable)
		lpTable->Release();

	if (lpFolder)
		lpFolder->Release();

	if (hr2 != hrSuccess)
		return hr2;
	return hr;
}

/** 
 * Create a flat list of all folders in the users tree, and possibly
 * add the public folders to this list too.
 * 
 * @param[out] lstFolders Sets the folder list
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrGetFolderList(list<SFolder> &lstFolders) {
	HRESULT hr = hrSuccess;
	LPSPropValue lpPropVal = NULL;
	ULONG cbEntryID;
	LPENTRYID lpEntryID = NULL;

	lstFolders.clear();

	hr = HrGetOneProp(lpStore, PR_IPM_SUBTREE_ENTRYID, &lpPropVal);
	if (hr != hrSuccess) {
		goto exit;
	}

	// make folders list from IPM_SUBTREE
	hr = HrGetSubTree(lstFolders, lpPropVal->Value.bin, wstring(), lstFolders.end());
	if (hr != hrSuccess) {
		goto exit;
	}

	hr = lpStore->GetReceiveFolder((LPTSTR)"IPM", 0, &cbEntryID, &lpEntryID, NULL);
	if (hr != hrSuccess) {
		goto exit;
	}

	// find the inbox, and name it INBOX
	for (auto &folder : lstFolders)
		if (cbEntryID == folder.sEntryID.cb && memcmp(folder.sEntryID.lpb, lpEntryID, cbEntryID) == 0) {
			folder.strFolderName = L"INBOX";
			break;
		}
	
	MAPIFreeBuffer(lpPropVal);
	lpPropVal = NULL;
	if(!lpPublicStore)
		goto exit;

	hr = HrGetOneProp(lpPublicStore, PR_IPM_PUBLIC_FOLDERS_ENTRYID, &lpPropVal);
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Public store is enabled in configuration, but Public Folders inside public store could not be found.");
		hr = hrSuccess;
		goto exit;
	}

	// make public folder folders list
	hr = HrGetSubTree(lstFolders, lpPropVal->Value.bin, PUBLIC_FOLDERS_NAME, --lstFolders.end());
	if (hr != hrSuccess) {
		lpLogger->Log(EC_LOGLEVEL_WARNING, "Public store is enabled in configuration, but Public Folders inside public store could not be found.");
		hr = hrSuccess;
	}

exit:
	MAPIFreeBuffer(lpPropVal);
	MAPIFreeBuffer(lpEntryID);
	return hr;
}

/**
 * Loads a binary blob from PR_EC_IMAP_SUBSCRIBED property on the
 * Inbox where entryid's of folders are store in, which describes the
 * folders the user subscribed to with an IMAP client.
 *
 * @return MAPI Error code
 * @retval hrSuccess Subscribed folder list loaded in m_vSubscriptions
 */
HRESULT IMAP::HrGetSubscribedList() {
	HRESULT hr = hrSuccess;
	LPSTREAM lpStream = NULL;
	LPMAPIFOLDER lpInbox = NULL;
	ULONG cbEntryID = 0;
	LPENTRYID lpEntryID = NULL;
	ULONG ulObjType = 0;
	ULONG size, i;
	ULONG read;
	ULONG cb = 0;
	BYTE *lpb = NULL;
	
	m_vSubscriptions.clear();

	hr = lpStore->GetReceiveFolder((LPTSTR)"IPM", 0, &cbEntryID, &lpEntryID, NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = lpSession->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, 0, &ulObjType, (LPUNKNOWN*)&lpInbox);
	if (hr != hrSuccess)
		goto exit;

	hr = lpInbox->OpenProperty(PR_EC_IMAP_SUBSCRIBED, &IID_IStream, 0, 0, (LPUNKNOWN*)&lpStream);
	if (hr != hrSuccess)
		goto exit;

	hr = lpStream->Read(&size, sizeof(ULONG), &read);
	if (hr != hrSuccess || read != sizeof(ULONG))
		goto exit;

	if (size == 0)
		goto exit;

	for (i = 0; i < size; ++i) {
		hr = lpStream->Read(&cb, sizeof(ULONG), &read);
		if (hr != hrSuccess || read != sizeof(ULONG))
			goto exit;

		lpb = new BYTE[cb];

		hr = lpStream->Read(lpb, cb, &read);
		if (hr != hrSuccess || read != cb) {
		    hr = MAPI_E_NOT_FOUND;
			goto exit;
        }

		m_vSubscriptions.push_back(BinaryArray(lpb, cb));
		
		delete [] lpb;
		lpb = NULL;
	}

exit:
	delete[] lpb;
	if (lpStream)
		lpStream->Release();

	if (lpInbox)
		lpInbox->Release();

	MAPIFreeBuffer(lpEntryID);
	return hr;
}

/**
 * Saves the m_vSubscriptions list of subscribed folders to
 * PR_EC_IMAP_SUBSCRIBED property in the inbox of the user.
 *
 * @return MAPI Error code
 * @retval hrSuccess Subscribed folder list saved in Inbox property
 */
HRESULT IMAP::HrSetSubscribedList() {
	HRESULT hr = hrSuccess;
	LPSTREAM lpStream = NULL;
	LPMAPIFOLDER lpInbox = NULL;
	ULONG cbEntryID = 0;
	LPENTRYID lpEntryID = NULL;
	ULONG ulObjType = 0;
	ULONG written;
	ULONG size;
	ULARGE_INTEGER liZero = {{0, 0}};

	hr = lpStore->GetReceiveFolder((LPTSTR)"IPM", 0, &cbEntryID, &lpEntryID, NULL);
	if (hr != hrSuccess)
		goto exit;

	hr = lpSession->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, MAPI_BEST_ACCESS, &ulObjType, (LPUNKNOWN*)&lpInbox);
	if (hr != hrSuccess)
		goto exit;

	hr = lpInbox->OpenProperty(PR_EC_IMAP_SUBSCRIBED, &IID_IStream, STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, (LPUNKNOWN*)&lpStream);
	if (hr != hrSuccess)
		goto exit;
		
    lpStream->SetSize(liZero);

	size = m_vSubscriptions.size();
	hr = lpStream->Write(&size, sizeof(ULONG), &written);
	if (hr != hrSuccess)
		goto exit;

	for (const auto &folder : m_vSubscriptions) {
		hr = lpStream->Write(&folder.cb, sizeof(ULONG), &written);
		if (hr != hrSuccess)
			goto exit;
		hr = lpStream->Write(folder.lpb, folder.cb, &written);
		if (hr != hrSuccess)
			goto exit;
	}

	hr = lpStream->Commit(0);

exit:
	if (lpStream)
		lpStream->Release();

	if (lpInbox)
		lpInbox->Release();

	MAPIFreeBuffer(lpEntryID);
	return hr;
}

/** 
 * Add or remove a folder EntryID from the subscribed list, and save
 * it to the server when changed.
 * 
 * @param[in] bSubscribe Add (true) to the list, or remove (false)
 * @param[in] cbEntryID number of bytes in lpEntryID
 * @param[in] lpEntryID EntryID to find in the subscribed list
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::ChangeSubscribeList(bool bSubscribe, ULONG cbEntryID, LPENTRYID lpEntryID)
{
	HRESULT hr = hrSuccess;
	vector<BinaryArray>::iterator iFolder;
	bool bChanged = false;

	iFolder = find(m_vSubscriptions.begin(), m_vSubscriptions.end(), BinaryArray((BYTE *)lpEntryID, cbEntryID, true));
	if (iFolder == m_vSubscriptions.end()) {
		if (bSubscribe) {
			m_vSubscriptions.push_back(BinaryArray((BYTE*)lpEntryID, cbEntryID));
			bChanged = true;
		}
	} else {
		if (!bSubscribe) {
			m_vSubscriptions.erase(iFolder);
			bChanged = true;
		}
	}

	if (bChanged) {
		hr = HrSetSubscribedList();
		if (hr != hrSuccess)
			goto exit;
	}

exit:
	return hr;
}

/** 
 * Create a list of special IMAP folders, which may not be deleted,
 * renamed or unsubscribed from.
 * 
 * @return MAPI Error code
 * @retval hrSuccess the special folderlist is saved in lstSpecialEntryIDs
 */
HRESULT IMAP::HrMakeSpecialsList() {
	HRESULT			hr = hrSuccess;
	ULONG			cbEntryID = 0;
	LPENTRYID		lpEntryID = NULL;
	LPMAPIFOLDER	lpInbox = NULL;
	ULONG			ulObjType = 0;
	ULONG			cValues = 0;
	LPSPropValue	lpPropArrayStore = NULL;
	LPSPropValue	lpPropArrayInbox = NULL;
	LPSPropValue	lpPropVal = NULL;
	SizedSPropTagArray(3, sPropsStore) = { 3,
		{ PR_IPM_OUTBOX_ENTRYID, PR_IPM_SENTMAIL_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID } };
	SizedSPropTagArray(6, sPropsInbox) = { 6,
		{ PR_IPM_APPOINTMENT_ENTRYID, PR_IPM_CONTACT_ENTRYID, PR_IPM_DRAFTS_ENTRYID,
		  PR_IPM_JOURNAL_ENTRYID, PR_IPM_NOTE_ENTRYID, PR_IPM_TASK_ENTRYID } };

	hr = lpStore->GetProps((LPSPropTagArray)&sPropsStore, 0, &cValues, &lpPropArrayStore);
	if (hr != hrSuccess)
		goto exit;

	for (ULONG i = 0; i < cValues; ++i)
		if (PROP_TYPE(lpPropArrayStore[i].ulPropTag) == PT_BINARY)
			lstSpecialEntryIDs.insert(BinaryArray(lpPropArrayStore[i].Value.bin.lpb, lpPropArrayStore[i].Value.bin.cb));

	hr = lpStore->GetReceiveFolder((LPTSTR)"IPM", 0, &cbEntryID, &lpEntryID, NULL);
	if (hr != hrSuccess)
		goto exit;

	// inbox is special too
	lstSpecialEntryIDs.insert(BinaryArray((LPBYTE)lpEntryID, cbEntryID));

	hr = lpSession->OpenEntry(cbEntryID, lpEntryID, &IID_IMAPIFolder, 0, &ulObjType, (LPUNKNOWN*)&lpInbox);
	if (hr != hrSuccess)
		goto exit;

	hr = lpInbox->GetProps((LPSPropTagArray)&sPropsInbox, 0, &cValues, &lpPropArrayInbox);
	if (hr != hrSuccess)
		goto exit;

	for (ULONG i = 0; i < cValues; ++i)
		if (PROP_TYPE(lpPropArrayInbox[i].ulPropTag) == PT_BINARY)
			lstSpecialEntryIDs.insert(BinaryArray(lpPropArrayInbox[i].Value.bin.lpb, lpPropArrayInbox[i].Value.bin.cb));

	if (HrGetOneProp(lpInbox, PR_ADDITIONAL_REN_ENTRYIDS, &lpPropVal) == hrSuccess &&
		lpPropVal->Value.MVbin.cValues >= 5 && lpPropVal->Value.MVbin.lpbin[4].cb != 0)
	{
		lstSpecialEntryIDs.insert(BinaryArray(lpPropVal->Value.MVbin.lpbin[4].lpb, lpPropVal->Value.MVbin.lpbin[4].cb));
	}
	
	MAPIFreeBuffer(lpPropVal);
	lpPropVal = NULL;
	if(!lpPublicStore)
		goto exit;

	if (HrGetOneProp(lpPublicStore, PR_IPM_PUBLIC_FOLDERS_ENTRYID, &lpPropVal) == hrSuccess)
		lstSpecialEntryIDs.insert(BinaryArray(lpPropVal->Value.bin.lpb, lpPropVal->Value.bin.cb));

exit:
	MAPIFreeBuffer(lpPropArrayStore);
	MAPIFreeBuffer(lpPropArrayInbox);
	MAPIFreeBuffer(lpPropVal);
	MAPIFreeBuffer(lpEntryID);
	if (lpInbox)
		lpInbox->Release();

	return hr;
}

/** 
 * Check if the folder with the given EntryID is a special folder.
 * 
 * @param[in] cbEntryID number of bytes in lpEntryID
 * @param[in] lpEntryID bytes of the entryid
 * 
 * @return is a special folder (true) or a custom user folder (false)
 */
bool IMAP::IsSpecialFolder(ULONG cbEntryID, LPENTRYID lpEntryID) {
	if(lstSpecialEntryIDs.find(BinaryArray((BYTE *)lpEntryID, cbEntryID, true)) == lstSpecialEntryIDs.end())
        return false;
        
    return true;
}

/** 
 * Make a list of all mails in the current selected folder.
 * 
 * @param[in] bInitialLoad Create a new clean list of mails (false to append only)
 * @param[in] bResetRecent Update the value of PR_EC_IMAP_MAX_ID for this folder
 * @param[in] bShowUID Send UID numbers to the client (true) or normal IMAP IDs (false)
 * @param[out] lpulUnseen The number of unread emails in this folder
 * @param[out] lpulUIDValidity The UIDVALIDITY value for this folder (optional)
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrRefreshFolderMails(bool bInitialLoad, bool bResetRecent, bool bShowUID, unsigned int *lpulUnseen, ULONG *lpulUIDValidity) {
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpFolder = NULL;
	LPMAPITABLE lpTable = NULL;
	LPSRowSet lpRows = NULL;
	ULONG ulMailnr = 0;
	ULONG ulMaxUID = 0;
	ULONG ulRecent = 0;
	int n = 0;
	SMail sMail;
	bool bNewMail = false;
	enum { EID, IKEY, IMAPID, FLAGS, FLAGSTATUS, MSGSTATUS, LAST_VERB, NUM_COLS };
	SizedSPropTagArray(NUM_COLS, spt) = { NUM_COLS, {PR_ENTRYID, PR_INSTANCE_KEY, PR_EC_IMAP_ID, PR_MESSAGE_FLAGS, PR_FLAG_STATUS, PR_MSG_STATUS, PR_LAST_VERB_EXECUTED} };
	SizedSSortOrderSet(1, sSortUID) = { 1, 0, 0, { { PR_EC_IMAP_ID, TABLE_SORT_ASCEND } } };
	vector<SMail>::const_iterator iterMail;
	map<unsigned int, unsigned int> mapUIDs; // Map UID -> ID
	map<unsigned int, unsigned int>::iterator iterUID;
	SPropValue sPropMax;
	unsigned int ulUnseen = 0;
	SizedSPropTagArray(2, sPropsFolderIDs) = { 2, { PR_EC_IMAP_MAX_ID, PR_EC_HIERARCHYID } };
	LPSPropValue lpFolderIDs = NULL;
	ULONG cValues;

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = HrFindFolder(strCurrentFolder, bCurrentFolderReadOnly, &lpFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = lpFolder->GetProps((LPSPropTagArray)&sPropsFolderIDs, 0, &cValues, &lpFolderIDs);
	if (FAILED(hr))
		goto exit;

	if (lpFolderIDs[0].ulPropTag == PR_EC_IMAP_MAX_ID)
        ulMaxUID = lpFolderIDs[0].Value.ul;
	else
        ulMaxUID = 0;

	if (lpulUIDValidity && lpFolderIDs[1].ulPropTag == PR_EC_HIERARCHYID)
		*lpulUIDValidity = lpFolderIDs[1].Value.ul;

	hr = lpFolder->GetContentsTable(MAPI_DEFERRED_ERRORS, &lpTable);
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->SetColumns((LPSPropTagArray) &spt, TBL_BATCH);
	if (hr != hrSuccess)
		goto exit;

    hr = lpTable->SortTable((LPSSortOrderSet)&sSortUID, TBL_BATCH);
    if (hr != hrSuccess)
        goto exit;

    // Remember UIDs if needed
    if(!bInitialLoad)
		for (const auto &mail : lstFolderMailEIDs)
			mapUIDs[mail.ulUid] = n++;
    
    if(bInitialLoad)
        lstFolderMailEIDs.clear();

    iterMail = lstFolderMailEIDs.begin();

    // Scan MAPI for new and existing messages
	while(1) {
		hr = lpTable->QueryRows(ROWS_PER_REQUEST, 0, &lpRows);
		if (hr != hrSuccess)
            goto exit;
            
        if(lpRows->cRows == 0)
            break;
            
		for (ulMailnr = 0; ulMailnr < lpRows->cRows; ++ulMailnr) {
            if (lpRows->aRow[ulMailnr].lpProps[EID].ulPropTag != spt.aulPropTag[EID] ||
                lpRows->aRow[ulMailnr].lpProps[IKEY].ulPropTag != spt.aulPropTag[IKEY] ||
                lpRows->aRow[ulMailnr].lpProps[IMAPID].ulPropTag != spt.aulPropTag[IMAPID])
                continue;

            iterUID = mapUIDs.find(lpRows->aRow[ulMailnr].lpProps[IMAPID].Value.ul);
		    if(iterUID == mapUIDs.end()) {
		        // There is a new message
                sMail.sEntryID = BinaryArray(lpRows->aRow[ulMailnr].lpProps[EID].Value.bin);
                sMail.sInstanceKey = BinaryArray(lpRows->aRow[ulMailnr].lpProps[IKEY].Value.bin);
                sMail.ulUid = lpRows->aRow[ulMailnr].lpProps[IMAPID].Value.ul;

                // Mark as recent if the message has a UID higher than the last highest read UID
                // in this folder. This means that this session is the only one to see the message
                // as recent.
                sMail.bRecent = sMail.ulUid > ulMaxUID;

                // Remember flags
                sMail.strFlags = PropsToFlags(lpRows->aRow[ulMailnr].lpProps, lpRows->aRow[ulMailnr].cValues, sMail.bRecent, false);

                // Put message on the end of our message
    			lstFolderMailEIDs.push_back(sMail);

                m_ulLastUid = max(sMail.ulUid, m_ulLastUid);
                bNewMail = true;
                
                // Remember the first unseen message
                if(ulUnseen == 0) {
                    if(lpRows->aRow[ulMailnr].lpProps[FLAGS].ulPropTag == PR_MESSAGE_FLAGS && (lpRows->aRow[ulMailnr].lpProps[FLAGS].Value.ul & MSGFLAG_READ) == 0) {
                        ulUnseen = lstFolderMailEIDs.size()-1+1; // size()-1 = last offset, mail ID = position + 1
                    }
                }
            } else {
                // Check flags
                std::string strFlags = PropsToFlags(lpRows->aRow[ulMailnr].lpProps, lpRows->aRow[ulMailnr].cValues, lstFolderMailEIDs[iterUID->second].bRecent, false);
                
                if(lstFolderMailEIDs[iterUID->second].strFlags != strFlags) {
                    // Flags have changed, notify it
                    if(bShowUID)
                        hr = HrResponse(RESP_UNTAGGED, stringify(iterUID->second+1) + " FETCH (UID " + stringify(lpRows->aRow[ulMailnr].lpProps[IMAPID].Value.ul) + " FLAGS (" + strFlags + "))");
                    else
                        hr = HrResponse(RESP_UNTAGGED, stringify(iterUID->second+1) + " FETCH (FLAGS (" + strFlags + "))");
					if (hr != hrSuccess)
						goto exit;
                    lstFolderMailEIDs[iterUID->second].strFlags = strFlags;
                }
                    
                // We already had this message, remove it from setUIDs
                mapUIDs.erase(iterUID);
            }
        
		}

		if (lpRows)
			FreeProws(lpRows);
		lpRows = NULL;
    }

    // All messages left in mapUIDs have been deleted, so loop through the current list so we can
    // send the correct EXPUNGE calls; At the same time, count RECENT messages.
    ulMailnr = 0;
    while(ulMailnr < lstFolderMailEIDs.size()) {
        if(mapUIDs.find(lstFolderMailEIDs[ulMailnr].ulUid) != mapUIDs.end()) {
            hr = HrResponse(RESP_UNTAGGED, stringify(ulMailnr+1) + " EXPUNGE");
			if (hr != hrSuccess)
				goto exit;
            lstFolderMailEIDs.erase(lstFolderMailEIDs.begin() + ulMailnr);
        } else {
            if(lstFolderMailEIDs[ulMailnr].bRecent)
                ++ulRecent;
                
            ++iterMail;
            ++ulMailnr;
        }
    }
    
    if (bNewMail || bInitialLoad) {
        hr = HrResponse(RESP_UNTAGGED, stringify(lstFolderMailEIDs.size()) + " EXISTS");
		if (hr != hrSuccess)
			goto exit;
		hr = HrResponse(RESP_UNTAGGED, stringify(ulRecent) + " RECENT");
		if (hr != hrSuccess)
			goto exit;
    }

	sort(lstFolderMailEIDs.begin(), lstFolderMailEIDs.end());
	
    // Save the max UID so that other session will not see the items as \Recent
    if(bResetRecent && ulRecent) {
    	sPropMax.ulPropTag = PR_EC_IMAP_MAX_ID;
    	sPropMax.Value.ul = m_ulLastUid;
    	HrSetOneProp(lpFolder, &sPropMax);
    }
    
    if(lpulUnseen)
        *lpulUnseen = ulUnseen;

exit:
	MAPIFreeBuffer(lpFolderIDs);
	if (lpRows)
		FreeProws(lpRows);

	if (lpTable)
		lpTable->Release();

	if (lpFolder)
		lpFolder->Release();

	return hr;
}

/** 
 * Return the IMAP Path for a given folder. Recursively recreates the
 * path using the parent iterator in the SFolder struct.
 * 
 * @param[in] lpFolder Return the path name for this folder
 * @param[in] lstFolders The list of folders for this user, where lpFolder is a valid iterator in
 * @param[out] strPath The full imap path with separators in wide characters
 * 
 * @return MAPI Error code
 * @retval MAPI_E_NOT_FOUND lpFolder is not a valid iterator in lstFolders
 */
HRESULT IMAP::HrGetFolderPath(list<SFolder>::const_iterator lpFolder, list<SFolder> &lstFolders, wstring &strPath) {
	HRESULT hr = hrSuccess;

	if (lpFolder == lstFolders.end()) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	if (lpFolder->lpParentFolder != lstFolders.end()) {
		hr = HrGetFolderPath(lpFolder->lpParentFolder, lstFolders, strPath);
		if (hr != hrSuccess)
			goto exit;
		strPath += IMAP_HIERARCHY_DELIMITER;
		strPath += lpFolder->strFolderName;
	}

exit:
	return hr;
}

/** 
 * Appends the given folder list with the given folder from the
 * EntryID with name. If bSubfolders is true, the given folder should
 * have subfolders, and these will recursively be processed.
 * 
 * @param[out] lstFolders Add the given folder data to this list
 * @param[in] sEntryID Folder EntryID to add to list, and to process for subfolders
 * @param[in] strFolderName The name of the folder to add to the list
 * @param[in] lpParentFolder iterator in lstFolders to set as the parent folder
 * @param[in] bSubfolders marker if this folder has subfolders
 * @param[in] bMailFolder marker if this folder contains emails
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrGetSubTree(list<SFolder> &lstFolders, SBinary &sEntryID, wstring strFolderName, list<SFolder>::const_iterator lpParentFolder,
						   bool bSubfolders, bool bMailFolder) {
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpFolder = NULL;
	LPMAPITABLE lpTable = NULL;
	LPSRowSet lpRows = NULL;
	ULONG ulObjType;
	SFolder sFolder;
	SBinary sChildEntryID;
	list<SFolder>::const_iterator lpNewParent;
	string strClass;
	vector<BinaryArray>::const_iterator iFolder;

	enum { EID, NAME, IMAPID, SUBFOLDERS, CONTAINERCLASS, NUM_COLS };
	SizedSPropTagArray(NUM_COLS, spt) = { NUM_COLS, {PR_ENTRYID, PR_DISPLAY_NAME_W, PR_EC_IMAP_ID,
													 PR_SUBFOLDERS, PR_CONTAINER_CLASS_A } };

	if (!lpSession) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	while (strFolderName.find(IMAP_HIERARCHY_DELIMITER) != string::npos) {
		strFolderName.erase(strFolderName.find(IMAP_HIERARCHY_DELIMITER), 1);
	}

	iFolder = find(m_vSubscriptions.begin(), m_vSubscriptions.end(), BinaryArray(sEntryID));
	sFolder.bActive = !(iFolder == m_vSubscriptions.end());
	sFolder.bSpecialFolder = IsSpecialFolder(sEntryID.cb, (LPENTRYID)sEntryID.lpb);
	sFolder.bMailFolder = true;
	sFolder.lpParentFolder = lpParentFolder;
	sFolder.strFolderName = strFolderName;
	sFolder.sEntryID = sEntryID;
	sFolder.bMailFolder = bMailFolder;
	sFolder.bHasSubfolders = bSubfolders;

	lstFolders.push_front(sFolder);
	lpNewParent = lstFolders.begin();
	
	if(!bSubfolders)
		goto exit;

	hr = lpSession->OpenEntry(sEntryID.cb, (LPENTRYID) sEntryID.lpb, &IID_IMAPIFolder, 0, &ulObjType, (LPUNKNOWN *) &lpFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = lpFolder->GetHierarchyTable(0, &lpTable);
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->SetColumns((LPSPropTagArray) &spt, 0);
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->QueryRows(-1, 0, &lpRows);
	if (hr != hrSuccess)
		goto exit;

	for (ULONG i = 0; i < lpRows->cRows; ++i) {
		if (PROP_TYPE(lpRows->aRow[i].lpProps[EID].ulPropTag) != PT_BINARY)
			continue;			// no entryid, no folder

		if (PROP_TYPE(lpRows->aRow[i].lpProps[IMAPID].ulPropTag) != PT_LONG) {
		    lpLogger->Log(EC_LOGLEVEL_FATAL, "Server does not support PR_EC_IMAP_ID. Please update the storage server.");
		    break;
        }

		const wchar_t *foldername = L"";
		bSubfolders = true;
		bMailFolder = true;

		if (PROP_TYPE(lpRows->aRow[i].lpProps[NAME].ulPropTag) == PT_UNICODE)
			foldername = lpRows->aRow[i].lpProps[NAME].Value.lpszW;

		if (PROP_TYPE(lpRows->aRow[i].lpProps[SUBFOLDERS].ulPropTag) == PT_BOOLEAN)
			bSubfolders = lpRows->aRow[i].lpProps[SUBFOLDERS].Value.b;

		if (PROP_TYPE(lpRows->aRow[i].lpProps[CONTAINERCLASS].ulPropTag) == PT_STRING8){
			strClass = lpRows->aRow[i].lpProps[CONTAINERCLASS].Value.lpszA;

			ToUpper(strClass);
			
			if (!strClass.empty() && strClass.compare(0, 3, "IPM") != 0 && strClass.compare("IPF.NOTE") != 0){
				if (bOnlyMailFolders)
					continue;
				bMailFolder = false;
			}
		}

		sChildEntryID = lpRows->aRow[i].lpProps[EID].Value.bin;
		HrGetSubTree(lstFolders, sChildEntryID, foldername, lpNewParent, bSubfolders, bMailFolder);
	}

exit:
	if (lpRows)
		FreeProws(lpRows);

	if (lpTable)
		lpTable->Release();

	if (lpFolder)
		lpFolder->Release();

	return hr;
}

/** 
 * Extends IMAP shortcuts into real full IMAP proptags, and returns an
 * vector of all separate and capitalized items.
 * 
 * @param[in] strMsgDataItemNames String of wanted data items from a FETCH command.
 * @param[out] lstDataItems Vector of all separate items in the string in uppercase.
 * 
 * @return hrSuccess
 */
HRESULT IMAP::HrGetDataItems(string strMsgDataItemNames, vector<string> &lstDataItems) {
	// translate macro's
	ToUpper(strMsgDataItemNames);
	if (strMsgDataItemNames.compare("ALL") == 0) {
		strMsgDataItemNames = "FLAGS INTERNALDATE RFC822.SIZE ENVELOPE";
	} else if (strMsgDataItemNames.compare("FAST") == 0) {
		strMsgDataItemNames = "FLAGS INTERNALDATE RFC822.SIZE";
	} else if (strMsgDataItemNames.compare("FULL") == 0) {
		strMsgDataItemNames = "FLAGS INTERNALDATE RFC822.SIZE ENVELOPE BODY";
	}

	// split data items
	if (strMsgDataItemNames.size() > 1 && strMsgDataItemNames[0] == '(') {
		strMsgDataItemNames.erase(0,1);
		strMsgDataItemNames.erase(strMsgDataItemNames.size()-1, 1);
	}
	return HrSplitInput(strMsgDataItemNames, lstDataItems);
}

/** 
 * Replaces all ; characters in the input to , characters.
 * 
 * @param[in] strData The string to modify
 * 
 * @return hrSuccess
 */
HRESULT IMAP::HrSemicolonToComma(string &strData) {
	string::size_type ulPos = strData.find(";");

	while (ulPos != string::npos) {
		strData.replace(ulPos, 1, ",");
		ulPos = strData.find(";", ulPos + 1);
	}

	return hrSuccess;
}

/** 
 * Do a FETCH based on table data for a specific list of
 * messages. Replies directly to the IMAP client with the result for
 * each mail given.
 * 
 * @param[in] lstMails a full sorted list of emails to process
 * @param[in] lstDataItems vector of IMAP data items to send to the client
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrPropertyFetch(list<ULONG> &lstMails, vector<string> &lstDataItems) {
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpFolder = NULL;
	LPSRowSet lpRows = NULL;
	LPSRow lpRow = NULL;
	LONG nRow = -1;
	ULONG ulDataItemNr;
	string strDataItem;
	SRestriction sRestriction;
	SPropValue sPropVal;
	string strResponse;
	LPSPropTagArray lpPropTags = NULL;
    set<ULONG> setProps;
    int n;
    LPSPropValue lpProps;
    ULONG cValues;
    unsigned int ulReadAhead = 0;
    SizedSSortOrderSet (1, sSortUID) = { 1, 0, 0, { { PR_EC_IMAP_ID, TABLE_SORT_ASCEND } } };
	bool bMarkAsRead = false;
	LPENTRYLIST lpEntryList = NULL;
	LPSPropValue lpProp = NULL; // non-free

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// Setup the readahead length
	ulReadAhead = lstMails.size() > ROWS_PER_REQUEST ? ROWS_PER_REQUEST : lstMails.size();

	// Find out which properties we will be needing from the table. This should be kept in-sync
	// with the properties that are used in HrPropertyFetchRow()
	// Also check if we need to mark the message as read.
	for (ulDataItemNr = 0; ulDataItemNr < lstDataItems.size(); ++ulDataItemNr) {
		strDataItem = lstDataItems[ulDataItemNr];

		if (strDataItem.compare("FLAGS") == 0) {
			setProps.insert(PR_MESSAGE_FLAGS);
			setProps.insert(PR_FLAG_STATUS);
			setProps.insert(PR_MSG_STATUS);
			setProps.insert(PR_LAST_VERB_EXECUTED);
		} else if (strDataItem.compare("XAOL.SIZE") == 0) {
			// estimated size
			setProps.insert(PR_MESSAGE_SIZE);
		} else if (strDataItem.compare("INTERNALDATE") == 0) {
			setProps.insert(PR_MESSAGE_DELIVERY_TIME);
			setProps.insert(PR_CLIENT_SUBMIT_TIME);
		} else if (strDataItem.compare("BODY") == 0) {
			setProps.insert(PR_EC_IMAP_BODY);
		} else if (strDataItem.compare("BODYSTRUCTURE") == 0) {
			setProps.insert(PR_EC_IMAP_BODYSTRUCTURE);
		} else if (strDataItem.compare("ENVELOPE") == 0) {
			setProps.insert(m_lpsIMAPTags->aulPropTag[0]);
		} else if (strDataItem.compare("RFC822.SIZE") == 0) {
			// real size
			setProps.insert(PR_EC_IMAP_EMAIL_SIZE);
		} else if (strstr(strDataItem.c_str(), "HEADER") != NULL) {
			// RFC822.HEADER, BODY[HEADER or BODY.PEEK[HEADER
			setProps.insert(PR_TRANSPORT_MESSAGE_HEADERS_A);
			// if we have the full body, we can skip some hacks to make headers match with the otherwise regenerated version.
			setProps.insert(PR_EC_IMAP_EMAIL_SIZE);

			// this is where RFC822.HEADER seems to differ from BODY[HEADER] requests
			// (according to dovecot and courier)
			if (Prefix(strDataItem, "BODY["))
				bMarkAsRead = true;
		} else if (Prefix(strDataItem, "BODY") || Prefix(strDataItem, "RFC822")) {
			// we don't want PR_EC_IMAP_EMAIL in the table (size problem),
			// and it must be in sync with PR_EC_IMAP_EMAIL_SIZE anyway, so detect presence from size
			setProps.insert(PR_EC_IMAP_EMAIL_SIZE);

			if (strstr(strDataItem.c_str(), "PEEK") == NULL)
				bMarkAsRead = true;
		}
	}

	if (bMarkAsRead) {
		setProps.insert(PR_MESSAGE_FLAGS);
		setProps.insert(PR_FLAG_STATUS);
		setProps.insert(PR_MSG_STATUS);
		setProps.insert(PR_LAST_VERB_EXECUTED);

		hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), (void**)&lpEntryList);
		if (hr != hrSuccess)
			goto exit;

		hr = MAPIAllocateMore(lstMails.size()*sizeof(SBinary), lpEntryList, (void**)&lpEntryList->lpbin);
		if (hr != hrSuccess)
			goto exit;

		lpEntryList->cValues = 0;
	}

	if(!setProps.empty() && m_vTableDataColumns != lstDataItems) {
		ReleaseContentsCache();

        // Build an LPSPropTagArray
        hr = MAPIAllocateBuffer(CbNewSPropTagArray(setProps.size()+1), (void **)&lpPropTags);
        if(hr != hrSuccess)
            goto exit;

        // Always get UID
        lpPropTags->aulPropTag[0] = PR_INSTANCE_KEY;
        n = 1;
		for (auto prop : setProps)
			lpPropTags->aulPropTag[n++] = prop;
        lpPropTags->cValues = setProps.size()+1;

        // Open the folder in question
        hr = HrFindFolder(strCurrentFolder, bCurrentFolderReadOnly, &lpFolder);
        if (hr != hrSuccess)
            goto exit;

        // Don't let the server cap the contents to 255 bytes, so our PR_TRANSPORT_MESSAGE_HEADERS is complete in the table
        hr = lpFolder->GetContentsTable(EC_TABLE_NOCAP | MAPI_DEFERRED_ERRORS, &m_lpTable);
        if (hr != hrSuccess)
            goto exit;

        // Request our columns
        hr = m_lpTable->SetColumns(lpPropTags, TBL_BATCH);
        if (hr != hrSuccess)
            goto exit;
            
        // Messages are usually requested in UID order, so sort the table in UID order too. This improves
        // the row prefetch hit ratio.
        hr = m_lpTable->SortTable((LPSSortOrderSet)&sSortUID, TBL_BATCH);
        if(hr != hrSuccess)
            goto exit;

		m_vTableDataColumns = lstDataItems;
    } else if (bMarkAsRead) {
        // we need the folder to mark mails as read
        hr = HrFindFolder(strCurrentFolder, bCurrentFolderReadOnly, &lpFolder);
        if (hr != hrSuccess)
            goto exit;
    }

	if (m_lpTable) {
		hr = m_lpTable->SeekRow(BOOKMARK_BEGINNING, 0, NULL);
		if(hr != hrSuccess)
			goto exit;
	}

	// Setup a find restriction that we modify for each row
	sRestriction.rt = RES_PROPERTY;
	sRestriction.res.resProperty.relop = RELOP_EQ;
	sRestriction.res.resProperty.ulPropTag = PR_INSTANCE_KEY;
	sRestriction.res.resProperty.lpProp = &sPropVal;
	sPropVal.ulPropTag = PR_INSTANCE_KEY;
    
	// Loop through all requested rows, and get the data for each (FIXME: slow for large requests)
	for (auto mail_idx : lstMails) {
		lpProp = NULL;			// by default: no need to mark-as-read

		sPropVal.Value.bin.cb = lstFolderMailEIDs[mail_idx].sInstanceKey.cb;
		sPropVal.Value.bin.lpb = lstFolderMailEIDs[mail_idx].sInstanceKey.lpb;

        // We use a read-ahead mechanism here, reading 50 rows at a time.		
		if (m_lpTable) {
            // First, see if the next row is somewhere in our already-read data
            lpRow = NULL;
            if(lpRows) {
				// use nRow to start checking where we left off
                for (unsigned int i = nRow + 1; i < lpRows->cRows; ++i) {
                    if(lpRows->aRow[i].lpProps[0].ulPropTag == PR_INSTANCE_KEY && BinaryArray(lpRows->aRow[i].lpProps[0].Value.bin) == BinaryArray(sPropVal.Value.bin)) {
                        lpRow = &lpRows->aRow[i];
						nRow = i;
						break;
                    }
                }
            }

            if(lpRow == NULL) {
                if(lpRows)
                    FreeProws(lpRows);
                lpRows = NULL;
                
                // Row was not found in our current data, request new data
                if(m_lpTable->FindRow(&sRestriction, BOOKMARK_CURRENT, 0) == hrSuccess && m_lpTable->QueryRows(ulReadAhead, 0, &lpRows) == hrSuccess) {
					if (lpRows->cRows != 0) {
						// The row we want is the first returned row
						lpRow = &lpRows->aRow[0];
						nRow = 0;
					}
                }
            }
            
		    // Pass the row data for conversion
		    if(lpRow) {
				// possebly add message to mark-as-read
				if (bMarkAsRead) {
					lpProp = PpropFindProp(lpRow->lpProps, lpRow->cValues, PR_MESSAGE_FLAGS);
					if (!lpProp || (lpProp->Value.ul & MSGFLAG_READ) == 0) {
						lpEntryList->lpbin[lpEntryList->cValues].cb = lstFolderMailEIDs[mail_idx].sEntryID.cb;
						lpEntryList->lpbin[lpEntryList->cValues].lpb = lstFolderMailEIDs[mail_idx].sEntryID.lpb;
						++lpEntryList->cValues;
					}
				}
    		    cValues = lpRow->cValues;
	    	    lpProps = lpRow->lpProps;
            } else {
                cValues = 0;
                lpProps = NULL;
            }
        } else {
            // If the row is unavailable, or the table is not needed, do not pass any properties
            cValues = 0;
            lpProps = NULL;
        }
        
        // Fetch the row data
        if (HrPropertyFetchRow(lpProps, cValues, strResponse, mail_idx, (lpProp != NULL), lstDataItems) != hrSuccess) {
            lpLogger->Log(EC_LOGLEVEL_WARNING, "{?} Error fetching mail");
        } else {
            hr = HrResponse(RESP_UNTAGGED, strResponse);
			if (hr != hrSuccess)
				goto exit;
        }
	}

	if (lpEntryList && lpEntryList->cValues) {
		// mark unread messages as read
		hr = lpFolder->SetReadFlags(lpEntryList, 0, NULL, SUPPRESS_RECEIPT);
		if (FAILED(hr))
			goto exit;
	}

exit:
	MAPIFreeBuffer(lpEntryList);
	MAPIFreeBuffer(lpPropTags);
	if (lpRows)
		FreeProws(lpRows);

	if (lpFolder)
		lpFolder->Release();

	return hr;
}

/** 
 * Does a FETCH based on row-data from a MAPI table. If the table data
 * is not sufficient, the PR_EC_IMAP_EMAIL property may be fetched
 * when present, or a full re-generation of the email will be
 * triggered to get the data.
 * 
 * @param[in] lpProps Array of MAPI properties of a message 
 * @param[in] cValues Number of properties in lpProps
 * @param[out] strResponse The string to send to the client
 * @param[in] ulMailnr Number of current email which we're creating a response for
 * @param[in] lstDataItems IMAP data items to add to the result string
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrPropertyFetchRow(LPSPropValue lpProps, ULONG cValues, string &strResponse, ULONG ulMailnr, bool bForceFlags, const vector<string> &lstDataItems)
{
	HRESULT hr = hrSuccess;
	vector<string>::const_iterator iFetch;
	string strItem;
	string strParts;
	string::size_type ulPos;
	LPSPropValue lpProp = NULL; // non-free
	char szBuffer[IMAP_RESP_MAX + 1];
	IMessage *lpMessage = NULL;
	ULONG ulObjType = 0;
	sending_options sopt;
	imopt_default_sending_options(&sopt);
	sopt.no_recipients_workaround = true;	// do not stop processing mail on empty recipient table
	sopt.alternate_boundary = const_cast<char *>("=_ZG_static");
	sopt.force_utf8 = parseBool(lpConfig->GetSetting("imap_generate_utf8"));
	string strMessage;
	string strMessagePart;
	unsigned int ulCount = 0;
	ostringstream oss;
	LPSTREAM lpStream = NULL;
	string strFlags;
	bool bSkipOpen = true;
	vector<string> vProps;
	
	// Response always starts with "<id> FETCH ("
	snprintf(szBuffer, IMAP_RESP_MAX, "%u FETCH (", ulMailnr + 1);
	strResponse = szBuffer;

	// rules to open the message:
	// 1. BODY requested and not present in table (generate)
	// 2. BODYSTRUCTURE requested and not present in table (generate)
	// 3. ENVELOPE requested and not present in table
	// 4. BODY* or body part requested
	// 5. RFC822* requested
	// and ! cached
	bSkipOpen = true;
	for (iFetch = lstDataItems.begin();
	     bSkipOpen && iFetch != lstDataItems.end(); ++iFetch)
	{
		if (iFetch->compare("BODY") == 0) {
			bSkipOpen = PpropFindProp(lpProps, cValues, PR_EC_IMAP_BODY) != NULL;
		} else if (iFetch->compare("BODYSTRUCTURE") == 0) {
			bSkipOpen = PpropFindProp(lpProps, cValues, PR_EC_IMAP_BODYSTRUCTURE) != NULL;
		} else if (iFetch->compare("ENVELOPE") == 0) {
			bSkipOpen = PpropFindProp(lpProps, cValues, m_lpsIMAPTags->aulPropTag[0]) != NULL;
		} else if (iFetch->compare("RFC822.SIZE") == 0) {
			bSkipOpen = PpropFindProp(lpProps, cValues, PR_EC_IMAP_EMAIL_SIZE) != NULL;
		} else if (strstr(iFetch->c_str(), "HEADER") != NULL) {
			// we can only use PR_TRANSPORT_MESSAGE_HEADERS when we have the full email.
			bSkipOpen = (PpropFindProp(lpProps, cValues, PR_TRANSPORT_MESSAGE_HEADERS_A) != NULL &&
						 PpropFindProp(lpProps, cValues, PR_EC_IMAP_EMAIL_SIZE) != NULL);
		}
		// full/partial body fetches, or size
		else if (Prefix(*iFetch, "BODY") || Prefix(*iFetch, "RFC822")) {
			bSkipOpen = false;
		}
	}
	if (!bSkipOpen && m_ulCacheUID != lstFolderMailEIDs[ulMailnr].ulUid) {
		// ignore error, we can't print an error halfway to the imap client
		lpSession->OpenEntry(lstFolderMailEIDs[ulMailnr].sEntryID.cb, (LPENTRYID) lstFolderMailEIDs[ulMailnr].sEntryID.lpb,
							 &IID_IMessage, MAPI_DEFERRED_ERRORS, &ulObjType, (LPUNKNOWN *) &lpMessage);
	}

	// Handle requested properties
	for (iFetch = lstDataItems.begin(); iFetch != lstDataItems.end(); ++iFetch) {
		if (iFetch->compare("FLAGS") == 0) {
			// if flags were already set from message, skip this version.
			if (strFlags.empty()) {
				strFlags = "FLAGS (";
				strFlags += PropsToFlags(lpProps, cValues, lstFolderMailEIDs[ulMailnr].bRecent, bForceFlags);
				strFlags += ")";
			}
		} else if (iFetch->compare("XAOL.SIZE") == 0) {
			lpProp = PpropFindProp(lpProps, cValues, PR_MESSAGE_SIZE);
			vProps.push_back(*iFetch);
			vProps.push_back(lpProp ? stringify(lpProp->Value.ul) : "NIL");
		} else if (iFetch->compare("INTERNALDATE") == 0) {
			vProps.push_back(*iFetch);

			lpProp = PpropFindProp(lpProps, cValues, PR_MESSAGE_DELIVERY_TIME);
			if (!lpProp)
				lpProp = PpropFindProp(lpProps, cValues, PR_CLIENT_SUBMIT_TIME);
			if (!lpProp)
				lpProp = PpropFindProp(lpProps, cValues, PR_CREATION_TIME);

			if (lpProp) {
				vProps.push_back("\"" + FileTimeToString(lpProp->Value.ft) + "\"");
			} else {
				vProps.push_back("NIL");
			}
		} else if (iFetch->compare("UID") == 0) {
			vProps.push_back(*iFetch);
			vProps.push_back(stringify(lstFolderMailEIDs[ulMailnr].ulUid));
		} else if (iFetch->compare("ENVELOPE") == 0) {
			lpProp = PpropFindProp(lpProps, cValues, m_lpsIMAPTags->aulPropTag[0]);
			if (lpProp) {
				vProps.push_back(*iFetch);
				vProps.push_back(string("(") + lpProp->Value.lpszA + ")");
			} else if (lpMessage) {
				string strEnvelope;
				HrGetMessageEnvelope(strEnvelope, lpMessage);
				vProps.push_back(strEnvelope); // @note contains ENVELOPE (...)
			} else {
				vProps.push_back(*iFetch);
				vProps.push_back("NIL");
			}
		} else if (bSkipOpen && iFetch->compare("BODY") == 0) {
			// table version
			lpProp = PpropFindProp(lpProps, cValues, PR_EC_IMAP_BODY);
			vProps.push_back(*iFetch);
			vProps.push_back(lpProp ? lpProp->Value.lpszA : "NIL");
		} else if (bSkipOpen && iFetch->compare("BODYSTRUCTURE") == 0) {
			// table version
			lpProp = PpropFindProp(lpProps, cValues, PR_EC_IMAP_BODYSTRUCTURE);
			vProps.push_back(*iFetch);
			vProps.push_back(lpProp ? lpProp->Value.lpszA : "NIL");
		} else if (Prefix(*iFetch, "BODY") || Prefix(*iFetch, "RFC822")) {
			// the only exceptions when we don't need to generate anything yet.
			if (iFetch->compare("RFC822.SIZE") == 0) {
				lpProp = PpropFindProp(lpProps, cValues, PR_EC_IMAP_EMAIL_SIZE);
				if (lpProp) {
					vProps.push_back(*iFetch);
					vProps.push_back(stringify(lpProp->Value.ul));
					continue;
				}
			}

			// mapping with RFC822 to BODY[* requests
			strItem = *iFetch;
			if (strItem.compare("RFC822") == 0) {
				strItem = "BODY[]";
			} else if (strItem.compare("RFC822.TEXT") == 0) {
				strItem = "BODY[TEXT]";
			} else if (strItem.compare("RFC822.HEADER") == 0) {
				strItem = "BODY[HEADER]";
			}

			// structure only, take shortcut if we have it
			if (strItem.find('[') == string::npos) {
				/* RFC 3501 6.4.5:
				 * BODY
				 *        Non-extensible form of BODYSTRUCTURE.
				 * BODYSTRUCTURE
				 *        The [MIME-IMB] body structure of the message.
				 */
				if (iFetch->length() > 4)
					lpProp = PpropFindProp(lpProps, cValues, PR_EC_IMAP_BODYSTRUCTURE);
				else
					lpProp = PpropFindProp(lpProps, cValues, PR_EC_IMAP_BODY);

				if (lpProp) {
					vProps.push_back(*iFetch);
					vProps.push_back(lpProp->Value.lpszA);
					continue;
				}
				// data not available in table, need to regenerate.
			}

			strMessage.clear();

			sopt.headers_only = strstr(strItem.c_str(), "HEADER") != NULL;

			if (m_ulCacheUID == lstFolderMailEIDs[ulMailnr].ulUid) {
				// Get message from cache
				strMessage = m_strCache;
			} else {
				// We need to send headers or a body(part) to the client.
				// For some clients, we need to make sure that headers match the bodies,
				// So if we don't have the full email in the database, we must fix the headers to match
				// vmime regenerated messages.

				if (sopt.headers_only && bSkipOpen) {
					lpProp = PpropFindProp(lpProps, cValues, PR_TRANSPORT_MESSAGE_HEADERS_A);
					if(lpProp) {
						strMessage = lpProp->Value.lpszA;
					} else {
						// still need to convert message 
						hr = MAPI_E_NOT_FOUND;
					}
				} else {
					// If we have the full body, download that property
					lpProp = PpropFindProp(lpProps, cValues, PR_EC_IMAP_EMAIL_SIZE);
					if (lpProp) {
						// we have PR_EC_IMAP_EMAIL_SIZE, so we also have PR_EC_IMAP_EMAIL
						hr = lpMessage->OpenProperty(PR_EC_IMAP_EMAIL, &IID_IStream, 0, 0, (LPUNKNOWN*)&lpStream);
						if (hr == hrSuccess) {
							hr = Util::HrStreamToString(lpStream, strMessage);

							lpStream->Release();
							lpStream = NULL;
						}
					} else {
						hr = MAPI_E_NOT_FOUND;
					}
				}
				// no full imap email in database available, so regenerate all
				if (hr != hrSuccess) {
					ASSERT(lpMessage);
					if (oss.tellp() == ostringstream::pos_type(0)) { // already converted in previous loop?
						if (!lpMessage || IMToINet(lpSession, lpAddrBook, lpMessage, oss, sopt, lpLogger) != hrSuccess) {
							vProps.push_back(*iFetch);
							vProps.push_back("NIL");
							lpLogger->Log(EC_LOGLEVEL_WARNING, "Error in generating message %d for user %ls in folder %ls", ulMailnr+1, m_strwUsername.c_str(), strCurrentFolder.c_str());
							continue;
						}
					}
					strMessage = oss.str();
					hr = hrSuccess;
					// @todo save message and all generated crap and when not headers only
				}

				// Cache the generated message
				if(!sopt.headers_only) {
					m_ulCacheUID = lstFolderMailEIDs[ulMailnr].ulUid;
					m_strCache = strMessage;
				}
			}

			if (iFetch->compare("RFC822.SIZE") == 0) {
				// We must return the real size, since clients use this when using chunked mode to download the full message
				vProps.push_back(*iFetch);
				vProps.push_back(stringify(strMessage.size()));
				continue;
			} 			

			if (iFetch->compare("BODY") == 0 || iFetch->compare("BODYSTRUCTURE") == 0) {
				string strData;

				HrGetBodyStructure(iFetch->length() > 4, strData, strMessage);
				vProps.push_back(*iFetch);
				vProps.push_back(strData);
				continue;
			}

			/* RFC 3501 6.4.5:
			 * BODY[<section>]<<partial>>
			 *        The text of a particular body section, without boundaries.
			 * BODY.PEEK[<section>]<<partial>>
			 *        An alternate form of BODY[<section>] that does not implicitly
			 *        set the \Seen flag.
			 */
			if (strstr(strItem.c_str(), "[]") != NULL) {
				// Nasty: eventhough the client requests .PEEK, it may not be present in the reply.
				string strReply = *iFetch;

				ulPos = strReply.find(".PEEK");
				if (ulPos != string::npos)
					strReply.erase(ulPos, strlen(".PEEK"));

				// Nasty: eventhough the client requests <12345.12345>, it may not be present in the reply.
				ulPos = strReply.rfind('<');
				if (ulPos != string::npos)
					strReply.erase(ulPos, string::npos);

				vProps.push_back(strReply);

				// Handle BODY[] and RFC822 (entire message)
				strMessagePart = strMessage;
			} else {
				// Handle BODY[subparts]

				// BODY[subpart], strParts = <subpart> (so "1.2.3" or "3.HEADER" or "TEXT" etc)
				ulPos = strItem.find("[");
				if (ulPos != string::npos)
					strParts = strItem.substr(ulPos + 1);

				ulPos = strParts.find("]");
				if (ulPos != string::npos)
					strParts.erase(ulPos);

				if(Prefix(*iFetch, "BODY")) {
					vProps.push_back("BODY[" + strParts + "]");
				} else {
					vProps.push_back("RFC822." + strParts);
				}

				// Get the correct message part (1.2.3, TEXT, HEADER, 1.2.3.TEXT, 1.2.3.HEADER)
				HrGetMessagePart(strMessagePart, strMessage, strParts);
			}

			// Process byte-part request ( <12345.12345> ) for BODY
			ulPos = strItem.rfind('<');
			if (ulPos != string::npos) {
				strParts = strItem.substr(ulPos + 1, strItem.size() - ulPos - 2);

				ulPos = strParts.find('.');
				if (ulPos != string::npos) {
					ulCount = strtoul(strParts.substr(0, ulPos).c_str(), NULL, 0);
					ulPos = strtoul(strParts.substr(ulPos + 1).c_str(), NULL, 0);
				} else {
					ulCount = strtoul(strParts.c_str(), NULL, 0);
					ulPos = strMessagePart.size();
				}

				if (ulCount > strMessagePart.size()) {
					strMessagePart.clear();
				} else if (ulCount + ulPos > strMessagePart.size()) {
					strMessagePart.erase(0, ulCount);
				} else {
					strMessagePart.erase(0, ulCount);
					strMessagePart.erase(ulPos);
				}

				snprintf(szBuffer, IMAP_RESP_MAX, "<%u>", ulCount);
				vProps.back() += szBuffer;
			}

			if (strMessagePart.empty()) {
				vProps.push_back("NIL");
			} else {
				// Output actual data
				snprintf(szBuffer, IMAP_RESP_MAX, "{%u}\r\n", (ULONG)strMessagePart.size());
				vProps.push_back(szBuffer);
				vProps.back() += strMessagePart;
			}

		} else {
			// unknown item
			vProps.push_back(*iFetch);
			vProps.push_back("NIL");
		}
	}

	if (bForceFlags && strFlags.empty()) {
		strFlags = "FLAGS (";
		strFlags += PropsToFlags(lpProps, cValues, lstFolderMailEIDs[ulMailnr].bRecent, bForceFlags);
		strFlags += ")";
	}

	// Output flags if modified
	if (!strFlags.empty())
		vProps.push_back(strFlags);

	strResponse += boost::algorithm::join(vProps, " ");
	strResponse += ")";

	if(lpMessage)
		lpMessage->Release();

	return hr;
}

/**
 * Returns a recipient block for the envelope request. Format:
 * (("fullname" NIL "email name" "email domain")(...))
 *
 * @param[in]	lpRows	recipient table rows
 * @param[in]	ulType	recipient type to print
 * @param[in]	strCharset	charset for the fullname
 * @return string containing the To/Cc/Bcc recipient data
 */
std::string IMAP::HrEnvelopeRecipients(LPSRowSet lpRows, ULONG ulType, std::string& strCharset, bool bIgnore)
{
	ULONG ulCount;
	std::string strResponse;
	std::string strPart;
	std::string::size_type ulPos;
	enum { EMAIL_ADDRESS, DISPLAY_NAME, RECIPIENT_TYPE, ADDRTYPE, ENTRYID, NUM_COLS };

	strResponse = "(";
	for (ulCount = 0; ulCount < lpRows->cRows; ++ulCount) {
		SPropValue *pr = lpRows->aRow[ulCount].lpProps;

		if (pr[RECIPIENT_TYPE].Value.ul != ulType)
			continue;
		/*
		 * """The fields of an address structure are in the following
		 * order: personal name, SMTP at-domain-list (source route),
		 * mailbox name, and host name.""" RFC 3501 §2.3.5 p.76.
		 */
		strResponse += "(";
		if (pr[DISPLAY_NAME].ulPropTag == PR_DISPLAY_NAME_W) {
			strResponse += EscapeString(pr[DISPLAY_NAME].Value.lpszW, strCharset, bIgnore);
		} else {
			strResponse += "NIL";
		}

		strResponse += " NIL ";
		bool has_email = pr[EMAIL_ADDRESS].ulPropTag == PR_EMAIL_ADDRESS_A;
		bool za_addr = pr[ADDRTYPE].ulPropTag == PR_ADDRTYPE_W &&
		               wcscmp(pr[ADDRTYPE].Value.lpszW, L"ZARAFA") == 0;
		std::string strPart;

		if (has_email && za_addr) {
			std::wstring name, type, email;
			HRESULT ret;
			ret = HrGetAddress(lpAddrBook, pr, NUM_COLS,
			      PR_ENTRYID, PR_DISPLAY_NAME_W, PR_ADDRTYPE_W,
			      PR_EMAIL_ADDRESS_A, name, type, email);
			if (ret == hrSuccess)
				strPart = convert_to<std::string>(email);
		} else if (has_email) {
			/* treat all non-ZARAFA cases as "SMTP" */
			strPart = pr[EMAIL_ADDRESS].Value.lpszA;
		}
		if (strPart.length() > 0) {
			ulPos = strPart.find("@");
			if (ulPos != string::npos) {
				strResponse += EscapeStringQT(strPart.substr(0, ulPos));
				strResponse += " ";
				strResponse += EscapeStringQT(strPart.substr(ulPos + 1));
			} else {
				strResponse += EscapeStringQT(strPart);
				strResponse += " NIL";
			}
		} else {
			strResponse += "NIL NIL";
		}

		strResponse += ") ";
	}

    if (strResponse.compare(strResponse.size() - 1, 1, " ") == 0) {
        strResponse.resize(strResponse.size() - 1);
        strResponse += ") ";
    } else {
		// no recipients at all
        strResponse.resize(strResponse.size() - 1);
        strResponse += "NIL ";
    }

	return strResponse;
}

/**
 * Returns a sender block for the envelope request. Format:
 * (("fullname" NIL "email name" "email domain"))
 *
 * @param[in]	lpMessage	GetProps object
 * @param[in]	ulTagName	Proptag to get for the fullname
 * @param[in]	ulTagEmail	Proptag to get for the email address
 * @param[in]	strCharset	Charset for the fullname
 * @return string containing the From/Sender/Reply-To envelope data
 */
std::string IMAP::HrEnvelopeSender(LPMESSAGE lpMessage, ULONG ulTagName, ULONG ulTagEmail, std::string& strCharset, bool bIgnore)
{
	HRESULT hr = hrSuccess;
	std::string strResponse;
	std::string strPart;
	std::string::size_type ulPos;
	LPSPropValue lpPropValues = NULL;
	ULONG ulProps;
	SizedSPropTagArray(2, sPropTags) = { 2, {ulTagName, ulTagEmail} };

	hr = lpMessage->GetProps((LPSPropTagArray)&sPropTags, 0, &ulProps, &lpPropValues);

	strResponse = "((";
	if (!FAILED(hr) && PROP_TYPE(lpPropValues[0].ulPropTag) != PT_ERROR) {
		strResponse += EscapeString(lpPropValues[0].Value.lpszW, strCharset, bIgnore);
	} else {
		strResponse += "NIL";
	}

	strResponse += " NIL ";

	if (!FAILED(hr) && PROP_TYPE(lpPropValues[1].ulPropTag) != PT_ERROR) {
		strPart = lpPropValues[1].Value.lpszA;

		ulPos = strPart.find("@", 0);
		if (ulPos != string::npos) {
			strResponse += EscapeStringQT(strPart.substr(0, ulPos));
			strResponse += " ";
			strResponse += EscapeStringQT(strPart.substr(ulPos + 1));
		} else {
			strResponse += EscapeStringQT(strPart);
			strResponse += " NIL";
		}
	} else {
		strResponse += "NIL";
	}

	strResponse += ")) ";
	MAPIFreeBuffer(lpPropValues);
	return strResponse;
}

/**
 * Returns the IMAP ENVELOPE string of a specific email. Since this
 * doesn't come from vmime, the values in this response may differ
 * from other requests.
 *
 * @param[out]	strResponse	The ENVELOPE answer will be concatenated to this string
 * @param[in]	lpMessage	The MAPI message object
 * @return	MAPI Error code
 */
HRESULT IMAP::HrGetMessageEnvelope(string &strResponse, LPMESSAGE lpMessage) {
	HRESULT hr = hrSuccess;
	LPSPropValue lpPropVal = NULL;
	LPSPropValue lpInternetCPID = NULL;
	const char *lpszCharset = NULL;
	string strCharset;
	bool bIgnoreCharsetErrors = false;
	LPMAPITABLE lpTable = NULL;
	LPSRowSet lpRows = NULL;
	SizedSPropTagArray(5, spt) = {5, {PR_EMAIL_ADDRESS_A, PR_DISPLAY_NAME_W, PR_RECIPIENT_TYPE, PR_ADDRTYPE_W, PR_ENTRYID}};

	if (!lpMessage) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// Get the outgoing charset we want to be using
	// @todo, add gateway force_utf8 option
	if (!parseBool(lpConfig->GetSetting("imap_generate_utf8")) &&
		HrGetOneProp(lpMessage, PR_INTERNET_CPID, &lpInternetCPID) == hrSuccess &&
		HrGetCharsetByCP(lpInternetCPID->Value.ul, &lpszCharset) == hrSuccess)
	{
		strCharset = lpszCharset;
		bIgnoreCharsetErrors = true;
	} else {
		// default to utf-8 if not set
		strCharset = "UTF-8";
	}

	strResponse += "ENVELOPE (";
	// date string
	if (HrGetOneProp(lpMessage, PR_CLIENT_SUBMIT_TIME, &lpPropVal) == hrSuccess
		|| HrGetOneProp(lpMessage, PR_MESSAGE_DELIVERY_TIME, &lpPropVal) == hrSuccess) {
		strResponse += "\"";
		strResponse += FileTimeToString(lpPropVal->Value.ft);
		strResponse += "\" ";
	} else {
		strResponse += "NIL ";
	}

	MAPIFreeBuffer(lpPropVal);
	lpPropVal = NULL;

	// subject
	if (HrGetOneProp(lpMessage, PR_SUBJECT_W, &lpPropVal) == hrSuccess) {
		strResponse += EscapeString(lpPropVal->Value.lpszW, strCharset, bIgnoreCharsetErrors);
	}
	strResponse += " ";

	// from
	strResponse += HrEnvelopeSender(lpMessage, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_A, strCharset, bIgnoreCharsetErrors);
	// sender
	strResponse += HrEnvelopeSender(lpMessage, PR_SENDER_NAME_W, PR_SENDER_EMAIL_ADDRESS_A, strCharset, bIgnoreCharsetErrors);
	// reply-to, @fixme use real reply-to info from PR_REPLY_RECIPIENT_ENTRIES
	strResponse += HrEnvelopeSender(lpMessage, PR_SENT_REPRESENTING_NAME_W, PR_SENT_REPRESENTING_EMAIL_ADDRESS_A, strCharset, bIgnoreCharsetErrors);

	// recipients
	hr = lpMessage->GetRecipientTable(0, &lpTable);
	if (hr != hrSuccess)
		goto recipientsdone;

	hr = lpTable->SetColumns((LPSPropTagArray) &spt, 0);
	if (hr != hrSuccess)
		goto recipientsdone;

	hr = lpTable->QueryRows(-1, 0, &lpRows);
	if (hr != hrSuccess)
		goto recipientsdone;

	strResponse += HrEnvelopeRecipients(lpRows, MAPI_TO, strCharset, bIgnoreCharsetErrors);
	strResponse += HrEnvelopeRecipients(lpRows, MAPI_CC, strCharset, bIgnoreCharsetErrors);
	strResponse += HrEnvelopeRecipients(lpRows, MAPI_BCC, strCharset, bIgnoreCharsetErrors);

recipientsdone:
	if (hr != hrSuccess) {
		strResponse += "NIL NIL NIL ";
		hr = hrSuccess;
	}

	MAPIFreeBuffer(lpPropVal);
	lpPropVal = NULL;

	// in reply to
	if (HrGetOneProp(lpMessage, PR_IN_REPLY_TO_ID_A, &lpPropVal) == hrSuccess) {
		strResponse += EscapeStringQT(lpPropVal->Value.lpszA);
	} else {
		strResponse += "NIL";
	}

	strResponse += " ";

	MAPIFreeBuffer(lpPropVal);
	lpPropVal = NULL;

	// internet message id
	if (HrGetOneProp(lpMessage, PR_INTERNET_MESSAGE_ID_A, &lpPropVal) == hrSuccess) {
		strResponse += EscapeStringQT(lpPropVal->Value.lpszA);
	} else {
		strResponse += "NIL";
	}

	strResponse += ")";

exit:
	if (lpRows)
		FreeProws(lpRows);

	if (lpTable)
		lpTable->Release();

	MAPIFreeBuffer(lpPropVal);
	MAPIFreeBuffer(lpInternetCPID);
	return hr;
}

/** 
 * Returns IMAP flags for a given message
 * 
 * @param[out] strResponse the FLAGS reply for the given message
 * @param[in] lpMessage the MAPI message to get the IMAP flags for
 * @param[in] bRecent mark this message as recent
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrGetMessageFlags(string &strResponse, LPMESSAGE lpMessage, bool bRecent) {
	HRESULT hr = hrSuccess;
	LPSPropValue lpProps = NULL;
	ULONG cValues;
	SizedSPropTagArray(4, sptaFlagProps) = { 4, { PR_MESSAGE_FLAGS, PR_FLAG_STATUS, PR_MSG_STATUS, PR_LAST_VERB_EXECUTED } };
	
	if (!lpMessage) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = lpMessage->GetProps((LPSPropTagArray)&sptaFlagProps, 0, &cValues, &lpProps);
	if (FAILED(hr))
		goto exit;
	hr = hrSuccess;

	strResponse += "FLAGS (";
	strResponse += PropsToFlags(lpProps, cValues, bRecent, false);
	strResponse += ")";

exit:
	MAPIFreeBuffer(lpProps);
	return hr;
}

/*
 * RFC 3501, section 6.4.5:
 *
 *       BODY[<section>]<<partial>>
 *
 *       The text of a particular body section.  The section
 *       specification is a set of zero or more part specifiers
 *       delimited by periods.  A part specifier is either a part number
 *       or one of the following: HEADER, HEADER.FIELDS, 
 *       HEADER.FIELDS.NOT, MIME, and TEXT.  An empty section
 *       specification refers to the entire message, including the
 *       header.
 *
 *       --- end of RFC text
 *
 *		 Please note that there is a difference between getting MIME and HEADER:
 *		 - HEADER and TEXT are only used in the top-level object OR an embedded message/rfc822 message
 *		 - MIME are the Content-* headers for the MIME part, NEVER the header for a message/rfc822 message
 *		 - The 'whole' part is HEADER + TEXT for toplevel and message/rfc822 parts
 *		 - The 'whole' part does NOT include the MIME-IMB headers part
 *
 */
/** 
 * Returns a body part of an RFC-822 message
 * 
 * @param[out] strMessagePart The requested message part
 * @param[in] strMessage The full mail to scan for the part
 * @param[in] strPartName IMAP request identifying a part in strMessage
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrGetMessagePart(string &strMessagePart, string &strMessage, string strPartName) {
	HRESULT hr = hrSuccess;
	string::size_type ulPos;
	unsigned long int ulPartnr;
	string strHeaders;
	string strBoundary;
	size_t ulHeaderBegin;
	size_t ulHeaderEnd;
	size_t ulCounter;
	const char *ptr, *end;

	if (strPartName.find_first_of("123456789") == 0) {
		// @todo rewrite without copying strings
	    std::string strNextPart;
	    // BODY[1] or BODY[1.2] etc
	    
	    // Find subsection
		ulPos = strPartName.find(".");
		if (ulPos == string::npos) { // first section
			ulPartnr = strtoul(strPartName.c_str(), NULL, 0);
		} else { // sub section
			ulPartnr = strtoul(strPartName.substr(0, ulPos).c_str(), NULL, 0);
		}

		// Find the correct part
		end = str_ifind((char*)strMessage.c_str(), "\r\n\r\n");
		ptr = str_ifind((char*)strMessage.c_str(), "boundary=");
		if (ptr && end && ptr < end) {
			ulHeaderBegin = std::distance(strMessage.c_str(), ptr) + strlen("boundary=");
			if (strMessage[ulHeaderBegin] == '"') {
				++ulHeaderBegin;
				// space in boundary is a possebility.
				ulHeaderEnd = strMessage.find_first_of("\"", ulHeaderBegin);
			} else {
				ulHeaderEnd = strMessage.find_first_of(" ;\t\r\n", ulHeaderBegin);
			}

			if (ulHeaderEnd != string::npos) {
			    // strBoundary is the boundary we are looking for
				strBoundary = strMessage.substr(ulHeaderBegin, ulHeaderEnd - ulHeaderBegin);
				// strHeaders is what we are looking for
				strHeaders = (string) "\r\n--" + strBoundary + "\r\n"; //Skip always the end header
				ulHeaderBegin = strMessage.find(strHeaders, ulHeaderBegin);

				// Find the section/part by looking for the Nth boundary string
				for (ulCounter = 0; ulCounter < ulPartnr && ulHeaderBegin != string::npos; ++ulCounter) {
					ulHeaderBegin += strHeaders.size();
					ulHeaderEnd = ulHeaderBegin;
					ulHeaderBegin = strMessage.find(strHeaders, ulHeaderBegin);
				}

				if (ulHeaderBegin != string::npos) {
				    // Found it, discard data after and before the part we want
				    strMessage.erase(ulHeaderBegin);
				    strMessage.erase(0, ulHeaderEnd);
				} else {
				    // Didn't find it, see if we can find the trailing boundary
                    ulHeaderBegin = strMessage.find((string) "\r\n--" + strBoundary + "--\r\n", ulHeaderEnd);
                    if(ulHeaderBegin != string::npos) {
                        // If found, output everything up to the trailing boundary
                        strMessage.erase(ulHeaderBegin);
                        strMessage.erase(0, ulHeaderEnd);
                    } else {
                        // Otherwise, treat the rest of the message as data
                        strMessage.erase(0, ulHeaderEnd);
                    }
				}
			}
		}

		// We now have the entire MIME part in strMessage, decide what to do with it
		if (ulPos != string::npos) {
			// There are sub sections, see what we want to do
			strNextPart = strPartName.substr(ulPos+1);
			if(strNextPart.compare("MIME") == 0) {
			    // Handle MIME request
                ulPos = strMessage.find("\r\n\r\n");
                if (ulPos != string::npos) {
                    strMessagePart = strMessage.substr(0, ulPos+4); // include trailing \r\n\r\n (+4)
                } else {
                    // Only headers in the message
                    strMessagePart = strMessage + "\r\n\r\n";
                }

                // All done
                goto exit;
			} else if(strNextPart.find_first_of("123456789") == 0) {
			    // Handle Subpart
    			HrGetMessagePart(strMessagePart, strMessage, strNextPart);

    			// All done
    			goto exit;
            }
        }
        
        // Handle any other request (HEADER, TEXT or 'empty'). This means we first skip the MIME-IMB headers
        // and process the rest from there.
        ulPos = strMessage.find("\r\n\r\n");
        if (ulPos != string::npos) {
            strMessage.erase(0, ulPos + 4);
        } else {
            // The message only has headers ?
            strMessage.clear();
        }
        // Handle HEADER and TEXT if requested        
        if (!strNextPart.empty())
            HrGetMessagePart(strMessagePart, strMessage, strNextPart);
        else
            // Swap to conserve memory: Original: strMessagePart = strMessage
            swap(strMessagePart, strMessage);

	} else if (strPartName.compare("TEXT") == 0) {
	    // Everything except for the headers
		ulPos = strMessage.find("\r\n\r\n");
		if (ulPos != string::npos) {
		    // Swap for less memory usage. Original: strMessagePart = strMessage.substr(ulPos+4)
		    strMessage.erase(0,ulPos+4);
		    swap(strMessage, strMessagePart);
		} else {
		    // The message only has headers ?
			strMessagePart.clear();
		}
	} else if (strPartName.compare("HEADER") == 0) {
	    // Only the headers
		ulPos = strMessage.find("\r\n\r\n");
		if (ulPos != string::npos) {
		    // Swap for less memory usage. Original: strMessagePart = strMessage.substr(0, ulPos+4);
		    strMessage.erase(ulPos+4, strMessage.size() - (ulPos+4));
		    swap(strMessagePart, strMessage);
		} else {
		    // Only headers in the message
			strMessagePart = strMessage + "\r\n\r\n";
		}
	} else if (Prefix(strPartName, "HEADER.FIELDS")) {
	    /* RFC 3501, section 6.4.5
	     *
	     * HEADER.FIELDS and HEADER.FIELDS.NOT are followed by a list of
         * field-name (as defined in [RFC-2822]) names, and return a
         * subset of the header.
         *
         * eg. HEADER.FIELDS (SUBJECT TO)
         * eg. HEADER.FIELDS.NOT (SUBJECT)
         */
        bool bNot = Prefix(strPartName, "HEADER.FIELDS.NOT");
        list<pair<string, string> > lstFields;
        string strFields;
        
        // Parse headers in message
        HrParseHeaders(strMessage, lstFields);
        
        // Get (<fields>)
        HrGetSubString(strFields, strPartName, "(", ")");
        
        strMessagePart.clear();
        
        if(bNot) {
            set<string> setFields;
            set<string>::const_iterator iterSet;

            // Get fields as set
            HrTokenize(setFields, strFields);
            
            // Output all headers except those specified
            for (const auto &field : lstFields) {
                std::string strFieldUpper = field.first;
                ToUpper(strFieldUpper);
                if(setFields.find(strFieldUpper) == setFields.end()) {
                    strMessagePart += field.first;
                    strMessagePart += ": ";
                    strMessagePart += field.second;
                    strMessagePart += "\r\n";
                }
            }
        } else {
            vector<string> lstReqFields;
            std::unordered_set<std::string> seen;

            // Get fields as vector
			lstReqFields = tokenize(strFields, " ");
            
            // Output headers specified, in order of field set
            for (const auto &reqfield : lstReqFields) {
                if (!seen.insert(reqfield).second)
                    continue;
                for (const auto &field : lstFields) {
                    if(CaseCompare(reqfield, field.first)) {
                        strMessagePart += field.first;
                        strMessagePart += ": ";
                        strMessagePart += field.second;
                        strMessagePart += "\r\n";
						break;
                    }
                }
            }
        }
		// mark end-of-headers
		strMessagePart += "\r\n";
	} else {
		strMessagePart = "NIL";
	}
	
exit:
	return hr;
}

/** 
 * Convert a sequence number to its actual number. It will either
 * return a number or a UID, depending on the input.
 * A special treatment for 
 * 
 * @param[in] szNr a number of the sequence input
 * @param[in] bUID sequence input are UID numbers or not
 * 
 * @return the number corresponding to the input.
 */
ULONG IMAP::LastOrNumber(const char *szNr, bool bUID)
{
	if (*szNr != '*')
		return atoui(szNr);

	if (!bUID)
		return lstFolderMailEIDs.size();

	if (lstFolderMailEIDs.empty())
		return 0;				// special case: return an "invalid" number
	else
		return lstFolderMailEIDs.back().ulUid;
}

/** 
 * Convert a UID sequence set into a MAPI restriction.
 * 
 * @param[in] strSeqSet The sequence set with uids given by the client
 * @param[out] lppRestriction The MAPI restriction to get all messages matching the sequence
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrSeqUidSetToRestriction(const string &strSeqSet, LPSRestriction *lppRestriction)
{
	HRESULT hr = hrSuccess;
	LPSRestriction lpRestriction = NULL;
	vector<string> vSequences;
	string::size_type ulPos = 0;
	SPropValue sProp;
	SPropValue sPropEnd;

	if (strSeqSet.empty()) {
		// no restriction
		*lppRestriction = NULL;
		goto exit;
	}

	sProp.ulPropTag = PR_EC_IMAP_ID;
	sPropEnd.ulPropTag = PR_EC_IMAP_ID;

	vSequences = tokenize(strSeqSet, ',');

	CREATE_RESTRICTION(lpRestriction);
	CREATE_RES_OR(lpRestriction, lpRestriction, vSequences.size());

	for (ULONG i = 0; i < vSequences.size(); ++i) {
		ulPos = vSequences[i].find(':');
		if (ulPos == string::npos) {
			// single number
			sProp.Value.ul = LastOrNumber(vSequences[i].c_str(), true);
			DATA_RES_PROPERTY(lpRestriction, lpRestriction->res.resOr.lpRes[i], RELOP_EQ, PR_EC_IMAP_ID, &sProp);
		} else {
			sProp.Value.ul = LastOrNumber(vSequences[i].c_str(), true);
			sPropEnd.Value.ul = LastOrNumber(vSequences[i].c_str() + ulPos + 1, true);

			if (sProp.Value.ul > sPropEnd.Value.ul)
				swap(sProp.Value.ul, sPropEnd.Value.ul);

			vector<SMail>::const_iterator b = std::lower_bound(lstFolderMailEIDs.begin(), lstFolderMailEIDs.end(), sProp.Value.ul);
			vector<SMail>::const_iterator e = std::upper_bound(lstFolderMailEIDs.begin(), lstFolderMailEIDs.end(), sPropEnd.Value.ul);
			

			CREATE_RES_AND(lpRestriction, &lpRestriction->res.resOr.lpRes[i], 2);

			DATA_RES_PROPERTY(lpRestriction, lpRestriction->res.resOr.lpRes[i].res.resAnd.lpRes[0], RELOP_GE, PR_EC_IMAP_ID, &sProp);
			DATA_RES_PROPERTY(lpRestriction, lpRestriction->res.resOr.lpRes[i].res.resAnd.lpRes[1], RELOP_LE, PR_EC_IMAP_ID, &sPropEnd);
		}
	}

	*lppRestriction = lpRestriction;
	lpRestriction = NULL;

exit:
	MAPIFreeBuffer(lpRestriction);
	return hr;
}

/** 
 * Convert an IMAP sequence set to a flat list of email numbers. See
 * RFC-3501 paragraph 9 for the syntax.  This function will return the
 * closest range of requested items, and thus may return an empty
 * list.
 * 
 * @param[in] strSeqSet IMAP sequence set of UID numbers
 * @param[out] lstMails flat list of email numbers
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrParseSeqUidSet(const string &strSeqSet, list<ULONG> &lstMails) {
	HRESULT hr = hrSuccess;
	vector<string> vSequences;
	string::size_type ulPos = 0;
	ULONG ulMailnr;
	ULONG ulBeginMailnr;

	// split different sequence parts into a vector
	vSequences = tokenize(strSeqSet, ',');

	for (ULONG i = 0; i < vSequences.size(); ++i) {
		ulPos = vSequences[i].find(':');
		if (ulPos == string::npos) {
			// single number
			ulMailnr = LastOrNumber(vSequences[i].c_str(), true);

			vector<SMail>::iterator i = find(lstFolderMailEIDs.begin(), lstFolderMailEIDs.end(), ulMailnr);
			if (i != lstFolderMailEIDs.end())
				lstMails.push_back(std::distance(lstFolderMailEIDs.begin(), i));
		} else {
			// range
			ulBeginMailnr = LastOrNumber(vSequences[i].c_str(), true);
			ulMailnr = LastOrNumber(vSequences[i].c_str() + ulPos + 1, true);

			if (ulBeginMailnr > ulMailnr)
				swap(ulBeginMailnr, ulMailnr);

			vector<SMail>::iterator b = std::lower_bound(lstFolderMailEIDs.begin(), lstFolderMailEIDs.end(), ulBeginMailnr);
			vector<SMail>::const_iterator e = std::upper_bound(lstFolderMailEIDs.begin(), lstFolderMailEIDs.end(), ulMailnr);

			for (std::vector<SMail>::iterator i = b; i != e; ++i)
				lstMails.push_back(std::distance(lstFolderMailEIDs.begin(), i));
		}
	}

	lstMails.sort();
	lstMails.unique();

	return hr;
}

/** 
 * Convert an IMAP sequence set to a flat list of email numbers. See
 * RFC-3501 paragraph 9 for the syntax.  These exact numbers requested
 * must be present in the folder, otherwise an error will be returned.
 *
 * @param[in] strSeqSet IMAP sequence set of direct numbers
 * @param[out] lstMails flat list of email numbers
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrParseSeqSet(const string &strSeqSet, list<ULONG> &lstMails) {
	HRESULT hr = hrSuccess;
	vector<string> vSequences;
	string::size_type ulPos = 0;
	ULONG ulMailnr;
	ULONG ulBeginMailnr;

	if(lstFolderMailEIDs.empty()) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	// split different sequence parts into a vector
	vSequences = tokenize(strSeqSet, ',');

	for (ULONG i = 0; i < vSequences.size(); ++i) {
		ulPos = vSequences[i].find(':');
		if (ulPos == string::npos) {
			// single number
			ulMailnr = LastOrNumber(vSequences[i].c_str(), false) - 1;
			if (ulMailnr >= lstFolderMailEIDs.size()) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}
			lstMails.push_back(ulMailnr);
		} else {
			// range
			ulBeginMailnr = LastOrNumber(vSequences[i].c_str(), false) - 1;
			ulMailnr = LastOrNumber(vSequences[i].c_str() + ulPos + 1, false) - 1;

			if (ulBeginMailnr > ulMailnr)
				swap(ulBeginMailnr, ulMailnr);

			if (ulBeginMailnr >= lstFolderMailEIDs.size() || ulMailnr >= lstFolderMailEIDs.size()) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			for (ULONG j = ulBeginMailnr; j <= ulMailnr; ++j)
				lstMails.push_back(j);
		}
	}

	lstMails.sort();
	lstMails.unique();

exit:
	return hr;
}

/** 
 * Implementation of the STORE command
 * 
 * @param[in] lstMails list of emails to process
 * @param[in] strMsgDataItemName how to modify the message (set, append, remove)
 * @param[in] strMsgDataItemValue new flag values from IMAP client
 * 
 * @return MAPI Error code
 */
// @todo c store 2 (+FLAGS) (\Deleted) shouldn't but does work
// @todo c store 2 +FLAGS (\Deleted) should and does work
HRESULT IMAP::HrStore(const list<ULONG> &lstMails, string strMsgDataItemName, string strMsgDataItemValue, bool *lpbDoDelete)
{
	HRESULT hr = hrSuccess;
	vector<string> lstFlags;
	ULONG ulCurrent;
	LPMESSAGE lpMessage = NULL;
	LPSPropValue lpPropVal = NULL;
	LPSPropTagArray lpPropTagArray = NULL;
	ULONG cValues;
	ULONG ulObjType;
	string strNewFlags;
	bool bDelete = false;

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	ToUpper(strMsgDataItemName);
	ToUpper(strMsgDataItemValue);
	if (strMsgDataItemValue.size() > 1 && strMsgDataItemValue[0] == '(') {
		strMsgDataItemValue.erase(0, 1);
		strMsgDataItemValue.erase(strMsgDataItemValue.size() - 1, 1);
	}
	HrSplitInput(strMsgDataItemValue, lstFlags);

	for (auto mail_idx : lstMails) {
		hr = lpSession->OpenEntry(lstFolderMailEIDs[mail_idx].sEntryID.cb, (LPENTRYID) lstFolderMailEIDs[mail_idx].sEntryID.lpb,
								&IID_IMessage, MAPI_MODIFY, &ulObjType, (LPUNKNOWN *) &lpMessage);
		if (hr != hrSuccess) {
			goto exit;
		}

		// FLAGS, FLAGS.SILENT, +FLAGS, +FLAGS.SILENT, -FLAGS, -FLAGS.SILENT
		if (strMsgDataItemName.compare(0, 5, "FLAGS") == 0) {

			if (strMsgDataItemValue.find("\\SEEN") == string::npos) {
				hr = lpMessage->SetReadFlag(CLEAR_READ_FLAG);
				if (hr != hrSuccess)
					goto exit;
			} else {
				hr = lpMessage->SetReadFlag(SUPPRESS_RECEIPT);
				if (hr != hrSuccess)
					goto exit;
			}

			MAPIFreeBuffer(lpPropTagArray);
			lpPropTagArray = NULL;

			hr = MAPIAllocateBuffer(CbNewSPropTagArray(5), (void**)&lpPropTagArray);
			if (hr != hrSuccess)
				goto exit;

			lpPropTagArray->cValues = 5;
			lpPropTagArray->aulPropTag[0] = PR_MSG_STATUS;
			lpPropTagArray->aulPropTag[1] = PR_FLAG_STATUS;
			lpPropTagArray->aulPropTag[2] = PR_ICON_INDEX;
			lpPropTagArray->aulPropTag[3] = PR_LAST_VERB_EXECUTED;
			lpPropTagArray->aulPropTag[4] = PR_LAST_VERB_EXECUTION_TIME;

			hr = lpMessage->GetProps(lpPropTagArray, 0, &cValues, &lpPropVal);
			if (FAILED(hr))
				goto exit;
			cValues = 5;

			lpPropVal[1].ulPropTag = PR_FLAG_STATUS;
			if (strMsgDataItemValue.find("\\FLAGGED") == string::npos) {
				lpPropVal[1].Value.ul = 0;
			} else {
				lpPropVal[1].Value.ul = 2;
			}

			if (lpPropVal[2].ulPropTag != PR_ICON_INDEX) {
				lpPropVal[2].ulPropTag = PR_ICON_INDEX;
				lpPropVal[2].Value.l = ICON_FOLDER_DEFAULT;
			}

			if (lpPropVal[0].ulPropTag != PR_MSG_STATUS) {
				lpPropVal[0].ulPropTag = PR_MSG_STATUS;
				lpPropVal[0].Value.ul = 0;
			}

			if (strMsgDataItemValue.find("\\ANSWERED") == string::npos) {
				lpPropVal[0].Value.ul &= ~MSGSTATUS_ANSWERED;
				cValues -= 2;	// leave PR_LAST_VERB_EXECUTED properties
			} else {
				lpPropVal[0].Value.ul |= MSGSTATUS_ANSWERED;
				lpPropVal[2].Value.ul = ICON_MAIL_REPLIED;

				lpPropVal[3].ulPropTag = PR_LAST_VERB_EXECUTED;
				lpPropVal[3].Value.ul = NOTEIVERB_REPLYTOSENDER;

				lpPropVal[4].ulPropTag = PR_LAST_VERB_EXECUTION_TIME;
				GetSystemTimeAsFileTime(&lpPropVal[4].Value.ft);
			}

			if (strMsgDataItemValue.find("$FORWARDED") == string::npos) {
				if (cValues == 5)
					cValues -= 2;	// leave PR_LAST_VERB_EXECUTED properties if still present
			} else {
				lpPropVal[2].Value.ul = ICON_MAIL_FORWARDED;

				lpPropVal[3].ulPropTag = PR_LAST_VERB_EXECUTED;
				lpPropVal[3].Value.ul = NOTEIVERB_FORWARD;

				lpPropVal[4].ulPropTag = PR_LAST_VERB_EXECUTION_TIME;
				GetSystemTimeAsFileTime(&lpPropVal[4].Value.ft);
			}

			if (strMsgDataItemValue.find("\\DELETED") == string::npos) {
				lpPropVal[0].Value.ul &= ~MSGSTATUS_DELMARKED;
			} else {
				lpPropVal[0].Value.ul |= MSGSTATUS_DELMARKED;
				bDelete = true;
			}

			// remove all "flag" properties
			hr = lpMessage->DeleteProps(lpPropTagArray, NULL);
			if (hr != hrSuccess)
				goto exit;

			// set new values (can be partial, see answered and forwarded)
			hr = lpMessage->SetProps(cValues, lpPropVal, NULL);
			if (hr != hrSuccess)
				goto exit;

			hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE | FORCE_SAVE);
			if (hr != hrSuccess)
				goto exit;
		} else if (strMsgDataItemName.compare(0, 6, "+FLAGS") == 0) {
			for (ulCurrent = 0; ulCurrent < lstFlags.size(); ++ulCurrent) {
				if (lstFlags[ulCurrent].compare("\\SEEN") == 0) {
					hr = lpMessage->SetReadFlag(SUPPRESS_RECEIPT);
					if (hr != hrSuccess)
						goto exit;
				} else if (lstFlags[ulCurrent].compare("\\DRAFT") == 0) {
					// not allowed
				} else if (lstFlags[ulCurrent].compare("\\FLAGGED") == 0) {
					MAPIFreeBuffer(lpPropVal);
					hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) &lpPropVal);
					if (hr != hrSuccess)
						goto exit;

					lpPropVal->ulPropTag = PR_FLAG_STATUS;
					lpPropVal->Value.ul = 2; // 0: none, 1: green ok mark, 2: red flag
					HrSetOneProp(lpMessage, lpPropVal);
					// TODO: set PR_FLAG_ICON here too?
				} else if (lstFlags[ulCurrent].compare("\\ANSWERED") == 0 || lstFlags[ulCurrent].compare("$FORWARDED") == 0) {
					MAPIFreeBuffer(lpPropVal);
					lpPropVal = NULL;
					MAPIFreeBuffer(lpPropTagArray);
					lpPropTagArray = NULL;

					hr = MAPIAllocateBuffer(CbNewSPropTagArray(4), (void**)&lpPropTagArray);
					if (hr != hrSuccess)
						goto exit;

					lpPropTagArray->cValues = 4;
					lpPropTagArray->aulPropTag[0] = PR_MSG_STATUS;
					lpPropTagArray->aulPropTag[1] = PR_ICON_INDEX;
					lpPropTagArray->aulPropTag[2] = PR_LAST_VERB_EXECUTED;
					lpPropTagArray->aulPropTag[3] = PR_LAST_VERB_EXECUTION_TIME;

					hr = lpMessage->GetProps(lpPropTagArray, 0, &cValues, &lpPropVal);
					if (FAILED(hr))
						goto exit;
					cValues = 4;

					if (lpPropVal[0].ulPropTag != PR_MSG_STATUS) {
						lpPropVal[0].ulPropTag = PR_MSG_STATUS;
						lpPropVal[0].Value.ul = 0;
					}
					// answered
					if (lstFlags[ulCurrent][0] == '\\')
						lpPropVal->Value.ul |= MSGSTATUS_ANSWERED;

					lpPropVal[1].ulPropTag = PR_ICON_INDEX;
					if (lstFlags[ulCurrent][0] == '\\')
						lpPropVal[1].Value.l = ICON_MAIL_REPLIED;
					else
						lpPropVal[1].Value.l = ICON_MAIL_FORWARDED;
					lpPropVal[2].ulPropTag = PR_LAST_VERB_EXECUTED;
					if (lstFlags[ulCurrent][0] == '\\')
						lpPropVal[2].Value.ul = NOTEIVERB_REPLYTOSENDER;
					else
						lpPropVal[2].Value.l = NOTEIVERB_FORWARD;
					lpPropVal[3].ulPropTag = PR_LAST_VERB_EXECUTION_TIME;
					GetSystemTimeAsFileTime(&lpPropVal[3].Value.ft);

					hr = lpMessage->SetProps(cValues, lpPropVal, NULL);
					if (hr != hrSuccess)
						goto exit;
				} else if (lstFlags[ulCurrent].compare("\\DELETED") == 0) {
					MAPIFreeBuffer(lpPropVal);
					lpPropVal = NULL;

					if (HrGetOneProp(lpMessage, PR_MSG_STATUS, &lpPropVal) != hrSuccess) {
						hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) &lpPropVal);
						if (hr != hrSuccess)
							goto exit;
						lpPropVal->ulPropTag = PR_MSG_STATUS;
						lpPropVal->Value.ul = 0;
					}

					lpPropVal->Value.ul |= MSGSTATUS_DELMARKED;
					HrSetOneProp(lpMessage, lpPropVal);
					bDelete = true;
				}
				lpMessage->SaveChanges(KEEP_OPEN_READWRITE | FORCE_SAVE);
			}
		} else if (strMsgDataItemName.compare(0, 6, "-FLAGS") == 0) {
			for (ulCurrent = 0; ulCurrent < lstFlags.size(); ++ulCurrent) {
				if (lstFlags[ulCurrent].compare("\\SEEN") == 0) {
					hr = lpMessage->SetReadFlag(CLEAR_READ_FLAG);
					if (hr != hrSuccess)
						goto exit;
				} else if (lstFlags[ulCurrent].compare("\\DRAFT") == 0) {
					// not allowed
				} else if (lstFlags[ulCurrent].compare("\\FLAGGED") == 0) {
					if (lpPropVal == NULL) {
						hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) &lpPropVal);
						if (hr != hrSuccess)
							goto exit;
					}

					lpPropVal->ulPropTag = PR_FLAG_STATUS;
					lpPropVal->Value.ul = 0;
					HrSetOneProp(lpMessage, lpPropVal);
				} else if (lstFlags[ulCurrent].compare("\\ANSWERED") == 0 || lstFlags[ulCurrent].compare("$FORWARDED") == 0) {
					MAPIFreeBuffer(lpPropVal);
					lpPropVal = NULL;
					MAPIFreeBuffer(lpPropTagArray);
					lpPropTagArray = NULL;

					hr = MAPIAllocateBuffer(CbNewSPropTagArray(4), (void**)&lpPropTagArray);
					if (hr != hrSuccess)
						goto exit;

					lpPropTagArray->cValues = 4;
					lpPropTagArray->aulPropTag[0] = PR_MSG_STATUS;
					lpPropTagArray->aulPropTag[1] = PR_ICON_INDEX;
					lpPropTagArray->aulPropTag[2] = PR_LAST_VERB_EXECUTED;
					lpPropTagArray->aulPropTag[3] = PR_LAST_VERB_EXECUTION_TIME;

					hr = lpMessage->GetProps(lpPropTagArray, 0, &cValues, &lpPropVal);
					if (FAILED(hr))
						goto exit;
					cValues = 4;

					if (lpPropVal[0].ulPropTag != PR_MSG_STATUS) {
						lpPropVal[0].ulPropTag = PR_MSG_STATUS;
						lpPropVal[0].Value.ul = 0;
					}
					lpPropVal->Value.ul &= ~MSGSTATUS_ANSWERED;

					lpPropVal[1].ulPropTag = PR_ICON_INDEX;
					lpPropVal[1].Value.l = ICON_FOLDER_DEFAULT;
					lpPropVal[2].ulPropTag = PR_LAST_VERB_EXECUTED;
					lpPropVal[2].Value.ul = NOTEIVERB_OPEN;
					lpPropVal[3].ulPropTag = PR_LAST_VERB_EXECUTION_TIME;
					GetSystemTimeAsFileTime(&lpPropVal[3].Value.ft);

					hr = lpMessage->SetProps(cValues, lpPropVal, NULL);
					if (hr != hrSuccess)
						goto exit;
				} else if (lstFlags[ulCurrent].compare("\\DELETED") == 0) {
					MAPIFreeBuffer(lpPropVal);
					lpPropVal = NULL;

					if (HrGetOneProp(lpMessage, PR_MSG_STATUS, &lpPropVal) != hrSuccess) {
						hr = MAPIAllocateBuffer(sizeof(SPropValue), (LPVOID *) &lpPropVal);
						if (hr != hrSuccess)
							goto exit;
						lpPropVal->ulPropTag = PR_MSG_STATUS;
						lpPropVal->Value.ul = 0;
					}

					lpPropVal->Value.ul &= ~MSGSTATUS_DELMARKED;
					HrSetOneProp(lpMessage, lpPropVal);
				}
				lpMessage->SaveChanges(KEEP_OPEN_READWRITE | FORCE_SAVE);
			}
		}

		/* Get the newly updated flags */
		hr = HrGetMessageFlags(strNewFlags, lpMessage, lstFolderMailEIDs[mail_idx].bRecent);
		if (hr != hrSuccess)
			goto exit;

		/* Update our internal flag status */
		lstFolderMailEIDs[mail_idx].strFlags = strNewFlags;

		if (lpMessage)
			lpMessage->Release();
		lpMessage = NULL;
		MAPIFreeBuffer(lpPropVal);
		lpPropVal = NULL;

	} // loop on mails

	if (strMsgDataItemName.size() > 7 && strMsgDataItemName.compare(strMsgDataItemName.size() - 7, 7, ".SILENT") == 0) {
		hr = MAPI_E_NOT_ME;		// abuse error code that will not be used elsewhere from this function
	}
	if (lpbDoDelete)
		*lpbDoDelete = bDelete;

exit:
	MAPIFreeBuffer(lpPropTagArray);
	MAPIFreeBuffer(lpPropVal);
	if (lpMessage)
		lpMessage->Release();

	return hr;
}

/** 
 * Implementation of the COPY and XAOL-MOVE commands
 * 
 * @param[in] lstMails list of email to copy or move
 * @param[in] strFolderParam folder to copy/move mails to, in IMAP UTF-7 charset
 * @param[in] bMove copy (false) or move (true)
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrCopy(const list<ULONG> &lstMails, const string &strFolderParam, bool bMove) {
	HRESULT hr = hrSuccess;
	IMAPIFolder *lpFromFolder = NULL;
	IMAPIFolder *lpDestFolder = NULL;
	ULONG ulCount;
	ENTRYLIST sEntryList;
	wstring strFolder;

	sEntryList.lpbin = NULL;

	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	hr = HrFindFolder(strCurrentFolder, bCurrentFolderReadOnly, &lpFromFolder);
	if (hr != hrSuccess)
		goto exit;

	// get dest folder
	hr = IMAP2MAPICharset(strFolderParam, strFolder);
	if (hr != hrSuccess)
		goto exit;

	hr = HrFindFolder(strFolder, false, &lpDestFolder);
	if (hr != hrSuccess)
		goto exit;

	sEntryList.cValues = lstMails.size();
	if ((hr = MAPIAllocateBuffer(sizeof(SBinary) * lstMails.size(), (LPVOID *) &sEntryList.lpbin)) != hrSuccess)
		goto exit;
	ulCount = 0;

	for (auto mail_idx : lstMails) {
		sEntryList.lpbin[ulCount].cb = lstFolderMailEIDs[mail_idx].sEntryID.cb;
		sEntryList.lpbin[ulCount].lpb = lstFolderMailEIDs[mail_idx].sEntryID.lpb;
		++ulCount;
	}

	hr = lpFromFolder->CopyMessages(&sEntryList, NULL, lpDestFolder, 0, NULL, bMove ? MESSAGE_MOVE : 0);

exit:
	MAPIFreeBuffer(sEntryList.lpbin);
	if (lpFromFolder)
		lpFromFolder->Release();

	if (lpDestFolder)
		lpDestFolder->Release();

	return hr;
}

/** 
 * Implements the SEARCH command
 * 
 * @param[in] lstSearchCriteria search options from the (this value is modified, cannot be used again)
 * @param[in] ulStartCriteria offset in the lstSearchCriteria to start parsing
 * @param[out] lstMailnr number of email messages that match the search criteria
 * @param[in] iconv make sure we search using the correct charset @todo, in the unicode tree this has a whole other meaning.
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrSearch(vector<string> &lstSearchCriteria, ULONG &ulStartCriteria, list<ULONG> &lstMailnr, ECIConv *iconv) {
	HRESULT hr = hrSuccess;
	string strSearchCriterium;
	vector<string> vSubSearch;
	vector<LPSRestriction> lstRestrictions;
	list<ULONG> lstMails;
	LPSRestriction lpRootRestrict = NULL;
	LPSRestriction lpRestriction, lpExtraRestriction;
	IMAPIFolder *lpFolder = NULL;
	LPMAPITABLE lpTable = NULL;
	LPSRowSet lpRows = NULL;
	LPSPropValue lpPropVal;
	ULONG ulMailnr, ulRownr, ulCount;
	FILETIME ft;
	char *szBuffer;
	enum { EID, NUM_COLS };
	SizedSPropTagArray(NUM_COLS, spt) = { NUM_COLS, {PR_EC_IMAP_ID} };
	map<unsigned int, unsigned int> mapUIDs;
	map<unsigned int, unsigned int>::const_iterator iterUID;
	int n = 0;
	SRestriction sAndRestriction;
	SRestriction sPropertyRestriction;
	
	if (strCurrentFolder.empty() || !lpSession) {
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	// no need to search in empty folders, won't find anything
	if (lstFolderMailEIDs.empty())
		goto exit;

	// don't search if only search for uid, sequence set, all, recent, new or old
	strSearchCriterium = lstSearchCriteria[ulStartCriteria];
	ToUpper(strSearchCriterium);
	if (lstSearchCriteria.size() - ulStartCriteria == 2 && strSearchCriterium.compare("UID") == 0) {
		hr = HrParseSeqUidSet(lstSearchCriteria[ulStartCriteria + 1], lstMailnr);
		goto exit;
	}

	if (lstSearchCriteria.size() - ulStartCriteria == 1) {
		if (strSearchCriterium.find_first_of("123456789*") == 0) {
			hr = HrParseSeqSet(lstSearchCriteria[ulStartCriteria], lstMailnr);
			goto exit;
		} else if (strSearchCriterium.compare("ALL") == 0) {
			for (ulMailnr = 0; ulMailnr < lstFolderMailEIDs.size(); ++ulMailnr)
				lstMailnr.push_back(ulMailnr);
			goto exit;
		} else if (strSearchCriterium.compare("RECENT") == 0) {
			for (ulMailnr = 0; ulMailnr < lstFolderMailEIDs.size(); ++ulMailnr)
			    if(lstFolderMailEIDs[ulMailnr].bRecent)
    				lstMailnr.push_back(ulMailnr);
			goto exit;
		} else if (strSearchCriterium.compare("NEW") == 0) {
			for (ulMailnr = 0; ulMailnr < lstFolderMailEIDs.size(); ++ulMailnr)
			    if(lstFolderMailEIDs[ulMailnr].bRecent && lstFolderMailEIDs[ulMailnr].strFlags.find("Seen") == std::string::npos)
    				lstMailnr.push_back(ulMailnr);
			goto exit;
		} else if (strSearchCriterium.compare("OLD") == 0) {
			for (ulMailnr = 0; ulMailnr < lstFolderMailEIDs.size(); ++ulMailnr)
			    if(!lstFolderMailEIDs[ulMailnr].bRecent)
    				lstMailnr.push_back(ulMailnr);
			goto exit;
		}
	}
	
	// Make a map of UID->ID
	for (const auto &e : lstFolderMailEIDs)
		mapUIDs[e.ulUid] = n++;

	hr = HrFindFolder(strCurrentFolder, bCurrentFolderReadOnly, &lpFolder);
	if (hr != hrSuccess) {
		goto exit;
	}

	hr = lpFolder->GetContentsTable(MAPI_DEFERRED_ERRORS, &lpTable);
	if (hr != hrSuccess) {
		goto exit;
	}

	hr = MAPIAllocateBuffer(sizeof(SRestriction), (LPVOID *) &lpRootRestrict);
	if (hr != hrSuccess)
		goto exit;

	lstRestrictions.push_back(lpRootRestrict);

	// Thunderbird searches:
	// or:
	// 12 uid SEARCH UNDELETED (OR SUBJECT "Undelivered" TO "henk")
	// 15 uid SEARCH UNDELETED (OR (OR SUBJECT "sender" SUBJECT "mail") SUBJECT "returned")
	// 17 uid SEARCH UNDELETED (OR SUBJECT "Undelivered" NOT TO "henk")
	// and:
	// 14 uid SEARCH UNDELETED SUBJECT "Undelivered" TO "henk"
	// 16 uid SEARCH UNDELETED SUBJECT "Undelivered" NOT TO "henk"
	// both cases, ulStartCriteria == 3: UNDELETED

	// this breaks to following search:
	//   (or subject henk subject kees) (or to henk from kees)
	// since this will translate in all or's, not: and(or(subj:henk,subj:kees),or(to:henk,from:kees))
	// however, thunderbird cannot build such a query, so we don't care currently.

	while (ulStartCriteria < lstSearchCriteria.size())
	{
		if (lstSearchCriteria[ulStartCriteria][0] == '(') {
			// remove all () and [], and resplit.
			strSearchCriterium.clear();
			for (auto c : lstSearchCriteria[ulStartCriteria]) {
				if (c == '(' || c == ')' || c == '[' || c == ']')
					continue;
				strSearchCriterium += c;
			}
			vSubSearch.clear();
			HrSplitInput(strSearchCriterium, vSubSearch);

			// replace in list.
			lstSearchCriteria.erase(lstSearchCriteria.begin() + ulStartCriteria);
			lstSearchCriteria.insert(lstSearchCriteria.begin() + ulStartCriteria, vSubSearch.begin(), vSubSearch.end());
		}

		strSearchCriterium = lstSearchCriteria[ulStartCriteria];
		ToUpper(strSearchCriterium);

		if (lstRestrictions.size() == 1) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpRestriction);
			if (hr != hrSuccess)
				goto exit;

			lstRestrictions[0]->rt = RES_AND; // RES_AND / RES_OR
			lstRestrictions[0]->res.resAnd.cRes = 2;
			lstRestrictions[0]->res.resAnd.lpRes = lpRestriction;
			lstRestrictions[0] = &lpRestriction[1];
		} else {
			lpRestriction = lstRestrictions[lstRestrictions.size() - 1];
			lstRestrictions.pop_back();
		}

		if (strSearchCriterium.find_first_of("123456789*") == 0) {	// sequence set
			lstMails.clear();

			hr = HrParseSeqSet(strSearchCriterium, lstMails);
			if (hr != hrSuccess)
				goto exit;

			hr = MAPIAllocateMore(sizeof(SRestriction) * lstMails.size(), lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = lstMails.size();
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;
			ulCount = 0;

			for (auto mail_idx : lstMails) {
				hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
				if (hr != hrSuccess)
					goto exit;

				lpPropVal->ulPropTag = PR_EC_IMAP_ID;
				lpPropVal->Value.ul = lstFolderMailEIDs[mail_idx].ulUid;
				lpExtraRestriction[ulCount].rt = RES_PROPERTY;
				lpExtraRestriction[ulCount].res.resProperty.relop = RELOP_EQ;
				lpExtraRestriction[ulCount].res.resProperty.ulPropTag = PR_EC_IMAP_ID;
				lpExtraRestriction[ulCount].res.resProperty.lpProp = lpPropVal;
				++ulCount;
			}

			++ulStartCriteria;
		} else if (strSearchCriterium.compare("ALL") == 0 || strSearchCriterium.compare("NEW") == 0 || strSearchCriterium.compare("RECENT") == 0) {	// do nothing
			lpRestriction->rt = RES_EXIST;
			lpRestriction->res.resExist.ulReserved1 = 0;
			lpRestriction->res.resExist.ulReserved2 = 0;
			lpRestriction->res.resExist.ulPropTag = PR_ENTRYID;
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("ANSWERED") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_NEZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].res.resBitMask.ulMask = MSGSTATUS_ANSWERED;
			++ulStartCriteria;
			// TODO: find also in PR_LAST_VERB_EXECUTED
		} else if (strSearchCriterium.compare("BEFORE") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			lpPropVal->ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			ft = StringToFileTime(lstSearchCriteria[ulStartCriteria + 1].c_str());
			lpPropVal->Value.ft = ft;
			lpExtraRestriction[1].rt = RES_PROPERTY;
			lpExtraRestriction[1].res.resProperty.relop = RELOP_LT;
			lpExtraRestriction[1].res.resProperty.ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			lpExtraRestriction[1].res.resProperty.lpProp = lpPropVal;
			ulStartCriteria += 2;
		} else if (strSearchCriterium == "BODY") {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_BODY;
			lpExtraRestriction[1].rt = RES_CONTENT;
			lpExtraRestriction[1].res.resContent.ulFuzzyLevel = lstSearchCriteria[ulStartCriteria+1].size() > 0 ? (FL_SUBSTRING | FL_IGNORECASE) : FL_FULLSTRING;
			lpExtraRestriction[1].res.resContent.ulPropTag = PR_BODY;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			if (iconv)
				iconv->convert(lstSearchCriteria[ulStartCriteria+1]);

			hr = MAPIAllocateMore(lstSearchCriteria[ulStartCriteria + 1].size() + 1, lpRootRestrict,
								  (LPVOID *) &szBuffer);
			if (hr != hrSuccess)
				goto exit;

			memcpy(szBuffer, lstSearchCriteria[ulStartCriteria + 1].c_str(), lstSearchCriteria[ulStartCriteria + 1].size() + 1);
			// @todo, unicode
			lpPropVal->ulPropTag = PR_BODY_A;
			lpPropVal->Value.lpszA = szBuffer;
			lpExtraRestriction[1].res.resContent.lpProp = lpPropVal;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("DELETED") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_NEZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].res.resBitMask.ulMask = MSGSTATUS_DELMARKED;
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("DRAFT") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_NEZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].res.resBitMask.ulMask = MSGSTATUS_DRAFT;
			++ulStartCriteria;
			// FIXME: add restriction to find PR_MESSAGE_FLAGS with MSGFLAG_UNSENT on
		} else if (strSearchCriterium.compare("FLAGGED") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_FLAG_STATUS;
			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_NEZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_FLAG_STATUS;
			lpExtraRestriction[1].res.resBitMask.ulMask = 65535;
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("FROM") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_CONTENT;
			lpExtraRestriction[0].res.resContent.ulFuzzyLevel = lstSearchCriteria[ulStartCriteria+1].size() > 0 ? (FL_SUBSTRING | FL_IGNORECASE) : FL_FULLSTRING;
			lpExtraRestriction[0].res.resContent.ulPropTag = PR_SENT_REPRESENTING_NAME;
			lpExtraRestriction[1].rt = RES_CONTENT;
			lpExtraRestriction[1].res.resContent.ulFuzzyLevel = lstSearchCriteria[ulStartCriteria+1].size() > 0 ? (FL_SUBSTRING | FL_IGNORECASE) : FL_FULLSTRING;
			lpExtraRestriction[1].res.resContent.ulPropTag = PR_SENT_REPRESENTING_EMAIL_ADDRESS;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			if (iconv)
				iconv->convert(lstSearchCriteria[ulStartCriteria+1]);

			hr = MAPIAllocateMore(lstSearchCriteria[ulStartCriteria + 1].size() + 1, lpRootRestrict,
								  (LPVOID *) &szBuffer);
			if (hr != hrSuccess)
				goto exit;

			memcpy(szBuffer, lstSearchCriteria[ulStartCriteria + 1].c_str(), lstSearchCriteria[ulStartCriteria + 1].size() + 1);
			// @todo, unicode
			lpPropVal->ulPropTag = PR_SENT_REPRESENTING_NAME_A;
			lpPropVal->Value.lpszA = szBuffer;
			lpExtraRestriction[0].res.resContent.lpProp = lpPropVal;
			lpExtraRestriction[1].res.resContent.lpProp = lpPropVal;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("KEYWORD") == 0) {
			lpRestriction->rt = RES_BITMASK;
			lpRestriction->res.resBitMask.relBMR = BMR_NEZ;
			lpRestriction->res.resBitMask.ulPropTag = PR_ENTRYID;
			lpRestriction->res.resBitMask.ulMask = 0;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("LARGER") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			// OR (AND (EXIST (PR_EC_IMAP_SIZE), (RES_PROP PR_EC_IMAP_SIZE RELOP_GT size)), (AND (NOT (EXIST (PR_EC_IMAP_SIZE), RES_PROP PR_MESSAGE_SIZE RELOP_GT size))))

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction[0].res.resAnd.lpRes);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[0].rt = RES_AND;
			lpExtraRestriction[0].res.resAnd.cRes = 2;

			lpExtraRestriction[0].res.resAnd.lpRes[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resAnd.lpRes[0].res.resExist.ulPropTag = PR_EC_IMAP_EMAIL_SIZE;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpPropVal->ulPropTag = PR_EC_IMAP_EMAIL_SIZE;
			lpPropVal->Value.ul = strtoul(lstSearchCriteria[ulStartCriteria + 1].c_str(), NULL, 0);

			lpExtraRestriction[0].res.resAnd.lpRes[1].rt = RES_PROPERTY;
			lpExtraRestriction[0].res.resAnd.lpRes[1].res.resProperty.relop = RELOP_GT;
			lpExtraRestriction[0].res.resAnd.lpRes[1].res.resProperty.ulPropTag = PR_EC_IMAP_EMAIL_SIZE;
			lpExtraRestriction[0].res.resAnd.lpRes[1].res.resProperty.lpProp = lpPropVal;

			//--

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction[1].res.resAnd.lpRes);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[1].rt = RES_AND;
			lpExtraRestriction[1].res.resAnd.cRes = 2;

			//-- not()

			lpExtraRestriction[1].res.resAnd.lpRes[0].rt = RES_NOT;
			hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestrict, (LPVOID *) &lpExtraRestriction[1].res.resAnd.lpRes[0].res.resNot.lpRes);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[1].res.resAnd.lpRes[0].res.resNot.lpRes[0].rt = RES_EXIST;
			lpExtraRestriction[1].res.resAnd.lpRes[0].res.resNot.lpRes[0].res.resExist.ulPropTag = PR_EC_IMAP_EMAIL_SIZE;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpPropVal->ulPropTag = PR_MESSAGE_SIZE;
			lpPropVal->Value.ul = strtoul(lstSearchCriteria[ulStartCriteria + 1].c_str(), NULL, 0);

			lpExtraRestriction[1].res.resAnd.lpRes[1].rt = RES_PROPERTY;
			lpExtraRestriction[1].res.resAnd.lpRes[1].res.resProperty.relop = RELOP_GT;
			lpExtraRestriction[1].res.resAnd.lpRes[1].res.resProperty.ulPropTag = PR_MESSAGE_SIZE;
			lpExtraRestriction[1].res.resAnd.lpRes[1].res.resProperty.lpProp = lpPropVal;

			ulStartCriteria += 2;
			// NEW done with ALL
		} else if (strSearchCriterium.compare("NOT") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_NOT;
			lpRestriction->res.resNot.ulReserved = 0;
			lpRestriction->res.resNot.lpRes = lpExtraRestriction;
			lstRestrictions.push_back(lpExtraRestriction);
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("OLD") == 0) {	// none?
			lpRestriction->rt = RES_BITMASK;
			lpRestriction->res.resBitMask.relBMR = BMR_NEZ;
			lpRestriction->res.resBitMask.ulPropTag = PR_ENTRYID;
			lpRestriction->res.resBitMask.ulMask = 0;
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("ON") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 3, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 3;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			lpPropVal->ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			ft = StringToFileTime(lstSearchCriteria[ulStartCriteria + 1].c_str());
			lpPropVal->Value.ft = ft;
			lpExtraRestriction[1].rt = RES_PROPERTY;
			lpExtraRestriction[1].res.resProperty.relop = RELOP_GE;
			lpExtraRestriction[1].res.resProperty.lpProp = lpPropVal;
			lpExtraRestriction[1].res.resProperty.ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpPropVal->ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			ft = StringToFileTime(lstSearchCriteria[ulStartCriteria + 1].c_str());
			lpPropVal->Value.ft = AddDay(ft);
			lpExtraRestriction[2].rt = RES_PROPERTY;
			lpExtraRestriction[2].res.resProperty.relop = RELOP_LT;
			lpExtraRestriction[2].res.resProperty.ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			lpExtraRestriction[2].res.resProperty.lpProp = lpPropVal;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("OR") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;
			lstRestrictions.push_back(&lpExtraRestriction[1]);
			lstRestrictions.push_back(&lpExtraRestriction[0]);
			++ulStartCriteria;
			// RECENT done with ALL
		} else if (strSearchCriterium.compare("SEEN") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_MESSAGE_FLAGS;
			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_NEZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_MESSAGE_FLAGS;
			lpExtraRestriction[1].res.resBitMask.ulMask = MSGFLAG_READ;
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("SENTBEFORE") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			lpPropVal->ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			ft = StringToFileTime(lstSearchCriteria[ulStartCriteria + 1].c_str());
			lpPropVal->Value.ft = ft;
			lpExtraRestriction[1].rt = RES_PROPERTY;
			lpExtraRestriction[1].res.resProperty.relop = RELOP_LT;
			lpExtraRestriction[1].res.resProperty.ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			lpExtraRestriction[1].res.resProperty.lpProp = lpPropVal;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("SENTON") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 3, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 3;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			lpPropVal->ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			ft = StringToFileTime(lstSearchCriteria[ulStartCriteria + 1].c_str());
			lpPropVal->Value.ft = ft;
			lpExtraRestriction[1].rt = RES_PROPERTY;
			lpExtraRestriction[1].res.resProperty.relop = RELOP_GE;
			lpExtraRestriction[1].res.resProperty.lpProp = lpPropVal;
			lpExtraRestriction[1].res.resExist.ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpPropVal->ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			ft = StringToFileTime(lstSearchCriteria[ulStartCriteria + 1].c_str());
			lpPropVal->Value.ft = AddDay(ft);
			lpExtraRestriction[2].rt = RES_PROPERTY;
			lpExtraRestriction[2].res.resProperty.relop = RELOP_LT;
			lpExtraRestriction[2].res.resProperty.ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			lpExtraRestriction[2].res.resProperty.lpProp = lpPropVal;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("SENTSINCE") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			lpPropVal->ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			ft = StringToFileTime(lstSearchCriteria[ulStartCriteria + 1].c_str());
			lpPropVal->Value.ft = ft;
			lpExtraRestriction[1].rt = RES_PROPERTY;
			lpExtraRestriction[1].res.resProperty.relop = RELOP_GE;
			lpExtraRestriction[1].res.resProperty.ulPropTag = PR_EC_CLIENT_SUBMIT_DATE;
			lpExtraRestriction[1].res.resProperty.lpProp = lpPropVal;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("SINCE") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			lpPropVal->ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			ft = StringToFileTime(lstSearchCriteria[ulStartCriteria + 1].c_str());
			lpPropVal->Value.ft = ft;
			lpExtraRestriction[1].rt = RES_PROPERTY;
			lpExtraRestriction[1].res.resProperty.relop = RELOP_GE;
			lpExtraRestriction[1].res.resProperty.ulPropTag = PR_EC_MESSAGE_DELIVERY_DATE;
			lpExtraRestriction[1].res.resProperty.lpProp = lpPropVal;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("SMALLER") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			// OR (AND (EXIST (PR_EC_IMAP_EMAIL_SIZE), (RES_PROP PR_EC_IMAP_EMAIL_SIZE RELOP_LT size)), (RES_PROP PR_MESSAGE_SIZE RELOP_LT size))

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction[0].res.resAnd.lpRes);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[0].rt = RES_AND;
			lpExtraRestriction[0].res.resAnd.cRes = 2;

			lpExtraRestriction[0].res.resAnd.lpRes[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resAnd.lpRes[0].res.resExist.ulPropTag = PR_EC_IMAP_EMAIL_SIZE;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpPropVal->ulPropTag = PR_EC_IMAP_EMAIL_SIZE;
			lpPropVal->Value.ul = strtoul(lstSearchCriteria[ulStartCriteria + 1].c_str(), NULL, 0);

			lpExtraRestriction[0].res.resAnd.lpRes[1].rt = RES_PROPERTY;
			lpExtraRestriction[0].res.resAnd.lpRes[1].res.resProperty.relop = RELOP_LT;
			lpExtraRestriction[0].res.resAnd.lpRes[1].res.resProperty.ulPropTag = PR_EC_IMAP_EMAIL_SIZE;
			lpExtraRestriction[0].res.resAnd.lpRes[1].res.resProperty.lpProp = lpPropVal;

			//--

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction[1].res.resAnd.lpRes);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[1].rt = RES_AND;
			lpExtraRestriction[1].res.resAnd.cRes = 2;

			//-- not()

			lpExtraRestriction[1].res.resAnd.lpRes[0].rt = RES_NOT;
			hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestrict, (LPVOID *) &lpExtraRestriction[1].res.resAnd.lpRes[0].res.resNot.lpRes);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[1].res.resAnd.lpRes[0].res.resNot.lpRes[0].rt = RES_EXIST;
			lpExtraRestriction[1].res.resAnd.lpRes[0].res.resNot.lpRes[0].res.resExist.ulPropTag = PR_EC_IMAP_EMAIL_SIZE;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			lpPropVal->ulPropTag = PR_MESSAGE_SIZE;
			lpPropVal->Value.ul = strtoul(lstSearchCriteria[ulStartCriteria + 1].c_str(), NULL, 0);

			lpExtraRestriction[1].res.resAnd.lpRes[1].rt = RES_PROPERTY;
			lpExtraRestriction[1].res.resAnd.lpRes[1].res.resProperty.relop = RELOP_LT;
			lpExtraRestriction[1].res.resAnd.lpRes[1].res.resProperty.ulPropTag = PR_MESSAGE_SIZE;
			lpExtraRestriction[1].res.resAnd.lpRes[1].res.resProperty.lpProp = lpPropVal;

			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("SUBJECT") == 0) {
		    // Handle SUBJECT <s>
		    char *szSearch;
		    
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			szSearch  = (char *)lstSearchCriteria[ulStartCriteria+1].c_str();

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_AND;
			lpRestriction->res.resAnd.cRes = 2;
			lpRestriction->res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_SUBJECT;
			lpExtraRestriction[1].rt = RES_CONTENT;

            lpExtraRestriction[1].res.resContent.ulFuzzyLevel = szSearch[0] ? (FL_SUBSTRING | FL_IGNORECASE) : FL_FULLSTRING;
			lpExtraRestriction[1].res.resContent.ulPropTag = PR_SUBJECT;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			// @todo, unicode
			lpPropVal->ulPropTag = PR_SUBJECT_A;
			lpPropVal->Value.lpszA = szSearch;
			lpExtraRestriction[1].res.resContent.lpProp = lpPropVal;

            ulStartCriteria += 2;
                
		} else if (strSearchCriterium.compare("TEXT") == 0) {
			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;
			lpRestriction = lpExtraRestriction;

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction[0].rt = RES_AND;
			lpRestriction[0].res.resAnd.cRes = 2;
			lpRestriction[0].res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_BODY;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			if (iconv)
				iconv->convert(lstSearchCriteria[ulStartCriteria+1]);

			hr = MAPIAllocateMore(lstSearchCriteria[ulStartCriteria + 1].size() + 1, lpRootRestrict,
								  (LPVOID *) &szBuffer);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[1].rt = RES_CONTENT;
			lpExtraRestriction[1].res.resContent.ulFuzzyLevel = lstSearchCriteria[ulStartCriteria+1].size() > 0 ? (FL_SUBSTRING | FL_IGNORECASE) : FL_FULLSTRING;
			lpExtraRestriction[1].res.resContent.ulPropTag = PR_BODY;
			memcpy(szBuffer, lstSearchCriteria[ulStartCriteria + 1].c_str(), lstSearchCriteria[ulStartCriteria + 1].size() + 1);
			// @todo, unicode
			lpPropVal->ulPropTag = PR_BODY_A;
			lpPropVal->Value.lpszA = szBuffer;
			lpExtraRestriction[1].res.resContent.lpProp = lpPropVal;

			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction[1].rt = RES_AND;
			lpRestriction[1].res.resAnd.cRes = 2;
			lpRestriction[1].res.resAnd.lpRes = lpExtraRestriction;
			lpExtraRestriction[0].rt = RES_EXIST;
			lpExtraRestriction[0].res.resExist.ulReserved1 = 0;
			lpExtraRestriction[0].res.resExist.ulReserved2 = 0;
			lpExtraRestriction[0].res.resExist.ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;

			hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
			if (hr != hrSuccess)
				goto exit;

			if (iconv)
				iconv->convert(lstSearchCriteria[ulStartCriteria+1]);

			hr = MAPIAllocateMore(lstSearchCriteria[ulStartCriteria + 1].size() + 1, lpRootRestrict,
								  (LPVOID *) &szBuffer);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[1].rt = RES_CONTENT;
			lpExtraRestriction[1].res.resContent.ulFuzzyLevel = lstSearchCriteria[ulStartCriteria+1].size() > 0 ? (FL_SUBSTRING | FL_IGNORECASE) : FL_FULLSTRING;
			lpExtraRestriction[1].res.resContent.ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;
			memcpy(szBuffer, lstSearchCriteria[ulStartCriteria + 1].c_str(), lstSearchCriteria[ulStartCriteria + 1].size() + 1);
			lpPropVal->ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;
			lpPropVal->Value.lpszA = szBuffer;
			lpExtraRestriction[1].res.resContent.lpProp = lpPropVal;
			ulStartCriteria += 2;
			}
		else if (strSearchCriterium.compare("TO") == 0 || strSearchCriterium.compare("CC") == 0 || strSearchCriterium.compare("BCC") == 0) {
		    char *szField = NULL;
		    char *szSearch = NULL;

			if (lstSearchCriteria.size() - ulStartCriteria <= 1) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}
			
			szField = (char *)strSearchCriterium.c_str();
			szSearch = (char *)lstSearchCriteria[ulStartCriteria+1].c_str();
			
            // Search for "^HEADER:.*DATA" in PR_TRANSPORT_HEADERS
            string strSearch = (string)"^" + szField + ":.*" + szSearch;
            
            lpRestriction->rt = RES_PROPERTY;
            lpRestriction->res.resProperty.ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;
            lpRestriction->res.resProperty.relop = RELOP_RE;

            hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (void **)&lpPropVal);
            if(hr != hrSuccess)
                goto exit;
                
            lpPropVal->ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;
            
            hr = MAPIAllocateMore(strSearch.size()+1, lpRootRestrict, (void **)&lpPropVal->Value.lpszA);
            strcpy(lpPropVal->Value.lpszA, strSearch.c_str());
            
            lpRestriction->res.resProperty.lpProp = lpPropVal;

			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("UID") == 0) {
			lstMails.clear();

			hr = HrParseSeqUidSet(lstSearchCriteria[ulStartCriteria + 1], lstMails);
			if (hr != hrSuccess)
				goto exit;

			hr = MAPIAllocateMore(sizeof(SRestriction) * lstMails.size(), lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = lstMails.size();
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;
			ulCount = 0;

			for (auto mail_idx : lstMails) {
				hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (LPVOID *) &lpPropVal);
				if (hr != hrSuccess)
					goto exit;

				lpPropVal->ulPropTag = PR_EC_IMAP_ID;
				lpPropVal->Value.ul = lstFolderMailEIDs[mail_idx].ulUid;
				lpExtraRestriction[ulCount].rt = RES_PROPERTY;
				lpExtraRestriction[ulCount].res.resProperty.relop = RELOP_EQ;
				lpExtraRestriction[ulCount].res.resProperty.ulPropTag = PR_EC_IMAP_ID;
				lpExtraRestriction[ulCount].res.resProperty.lpProp = lpPropVal;
				++ulCount;
			}

			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("UNANSWERED") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;

			hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestrict, (LPVOID *) &lpRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[0].rt = RES_NOT;
			lpExtraRestriction[0].res.resNot.ulReserved = 0;
			lpExtraRestriction[0].res.resNot.lpRes = lpRestriction;

			lpRestriction[0].rt = RES_EXIST;
			lpRestriction[0].res.resExist.ulReserved1 = 0;
			lpRestriction[0].res.resExist.ulReserved2 = 0;
			lpRestriction[0].res.resExist.ulPropTag = PR_MSG_STATUS;

			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_EQZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].res.resBitMask.ulMask = MSGSTATUS_ANSWERED;
			++ulStartCriteria;
			// TODO: also find in PR_LAST_VERB_EXECUTED
		} else if (strSearchCriterium.compare("UNDELETED") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;

			hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestrict, (LPVOID *) &lpRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[0].rt = RES_NOT;
			lpExtraRestriction[0].res.resNot.ulReserved = 0;
			lpExtraRestriction[0].res.resNot.lpRes = lpRestriction;

			lpRestriction[0].rt = RES_EXIST;
			lpRestriction[0].res.resExist.ulReserved1 = 0;
			lpRestriction[0].res.resExist.ulReserved2 = 0;
			lpRestriction[0].res.resExist.ulPropTag = PR_MSG_STATUS;

			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_EQZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].res.resBitMask.ulMask = MSGSTATUS_DELMARKED;
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("UNDRAFT") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;

			hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestrict, (LPVOID *) &lpRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[0].rt = RES_NOT;
			lpExtraRestriction[0].res.resNot.ulReserved = 0;
			lpExtraRestriction[0].res.resNot.lpRes = lpRestriction;

			lpRestriction[0].rt = RES_EXIST;
			lpRestriction[0].res.resExist.ulReserved1 = 0;
			lpRestriction[0].res.resExist.ulReserved2 = 0;
			lpRestriction[0].res.resExist.ulPropTag = PR_MSG_STATUS;

			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_EQZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_MSG_STATUS;
			lpExtraRestriction[1].res.resBitMask.ulMask = MSGSTATUS_DRAFT;
			++ulStartCriteria;
			// FIXME: add restrictin to find PR_MESSAGE_FLAGS with MSGFLAG_UNSENT off
		} else if (strSearchCriterium.compare("UNFLAGGED") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;

			hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestrict, (LPVOID *) &lpRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[0].rt = RES_NOT;
			lpExtraRestriction[0].res.resNot.ulReserved = 0;
			lpExtraRestriction[0].res.resNot.lpRes = lpRestriction;

			lpRestriction[0].rt = RES_EXIST;
			lpRestriction[0].res.resExist.ulReserved1 = 0;
			lpRestriction[0].res.resExist.ulReserved2 = 0;
			lpRestriction[0].res.resExist.ulPropTag = PR_FLAG_STATUS;

			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_EQZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_FLAG_STATUS;
			lpExtraRestriction[1].res.resBitMask.ulMask = 65535;
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("UNKEYWORD") == 0) {
			lpRestriction->rt = RES_EXIST;
			lpRestriction->res.resExist.ulReserved1 = 0;
			lpRestriction->res.resExist.ulReserved2 = 0;
			lpRestriction->res.resExist.ulPropTag = PR_ENTRYID;
			ulStartCriteria += 2;
		} else if (strSearchCriterium.compare("UNSEEN") == 0) {
			hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (LPVOID *) &lpExtraRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpRestriction->rt = RES_OR;
			lpRestriction->res.resOr.cRes = 2;
			lpRestriction->res.resOr.lpRes = lpExtraRestriction;

			hr = MAPIAllocateMore(sizeof(SRestriction), lpRootRestrict, (LPVOID *) &lpRestriction);
			if (hr != hrSuccess)
				goto exit;

			lpExtraRestriction[0].rt = RES_NOT;
			lpExtraRestriction[0].res.resNot.ulReserved = 0;
			lpExtraRestriction[0].res.resNot.lpRes = lpRestriction;

			lpRestriction[0].rt = RES_EXIST;
			lpRestriction[0].res.resExist.ulReserved1 = 0;
			lpRestriction[0].res.resExist.ulReserved2 = 0;
			lpRestriction[0].res.resExist.ulPropTag = PR_MESSAGE_FLAGS;

			lpExtraRestriction[1].rt = RES_BITMASK;
			lpExtraRestriction[1].res.resBitMask.relBMR = BMR_EQZ;
			lpExtraRestriction[1].res.resBitMask.ulPropTag = PR_MESSAGE_FLAGS;
			lpExtraRestriction[1].res.resBitMask.ulMask = MSGFLAG_READ;
			++ulStartCriteria;
		} else if (strSearchCriterium.compare("HEADER") == 0) {
		    char *szField;
		    char *szSearch;
		    
			if (lstSearchCriteria.size() - ulStartCriteria <= 2) {
				hr = MAPI_E_CALL_FAILED;
				goto exit;
			}
			
			szField = (char *)lstSearchCriteria[ulStartCriteria+1].c_str();
			szSearch = (char *)lstSearchCriteria[ulStartCriteria+2].c_str();
			
            // Search for "^HEADER:.*DATA" in PR_TRANSPORT_HEADERS
            string strSearch = (string)"^" + szField + ":.*" + szSearch;
            
            lpRestriction->rt = RES_PROPERTY;
            lpRestriction->res.resProperty.ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;
            lpRestriction->res.resProperty.relop = RELOP_RE;

            hr = MAPIAllocateMore(sizeof(SPropValue), lpRootRestrict, (void **)&lpPropVal);
            if(hr != hrSuccess)
                goto exit;
                
            lpPropVal->ulPropTag = PR_TRANSPORT_MESSAGE_HEADERS_A;
            
            hr = MAPIAllocateMore(strSearch.size()+1, lpRootRestrict, (void **)&lpPropVal->Value.lpszA);
            strcpy(lpPropVal->Value.lpszA, strSearch.c_str());
            
            lpRestriction->res.resProperty.lpProp = lpPropVal;

			ulStartCriteria += 3;
		} else {
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}
	}

	while (lstRestrictions.size() > 0) {
		lpRestriction = lstRestrictions[lstRestrictions.size() - 1];
		lpRestriction->rt = RES_EXIST;
		lpRestriction->res.resExist.ulReserved1 = 0;
		lpRestriction->res.resExist.ulReserved2 = 0;
		lpRestriction->res.resExist.ulPropTag = PR_ENTRYID;
		lstRestrictions.pop_back();
	}
		
    // Wrap the resulting restriction (lpRootRestrict) in an AND:
    // AND (lpRootRestrict, EXIST(PR_INSTANCE_KEY)) to make sure that the query
    // will not be passed to the indexer
    
	sPropertyRestriction.rt = RES_EXIST;
	sPropertyRestriction.res.resExist.ulPropTag = PR_INSTANCE_KEY;
	
	sAndRestriction.rt = RES_AND;
	sAndRestriction.res.resAnd.cRes = 2;
	if ((hr = MAPIAllocateMore(sizeof(SRestriction) * 2, lpRootRestrict, (void **)&sAndRestriction.res.resAnd.lpRes)) != hrSuccess)
		goto exit;
	sAndRestriction.res.resAnd.lpRes[0] = sPropertyRestriction;
	sAndRestriction.res.resAnd.lpRes[1] = *lpRootRestrict;
		
	hr = HrQueryAllRows(lpTable, (LPSPropTagArray) &spt, &sAndRestriction, NULL, 0, &lpRows);
	if (hr != hrSuccess)
		goto exit;

	if (lpRows->cRows == 0)
		goto exit;

    for (ulRownr = 0; ulRownr < lpRows->cRows; ++ulRownr) {
        iterUID = mapUIDs.find(lpRows->aRow[ulRownr].lpProps[0].Value.ul);
        if(iterUID == mapUIDs.end()) {
            // Found a match for a message that is not in our message list .. skip it
            continue;
        }
        
        lstMailnr.push_back(iterUID->second);
    }

	lstMailnr.sort();

exit:
	MAPIFreeBuffer(lpRootRestrict);
	if (lpRows)
		FreeProws(lpRows);

	if (lpTable)
		lpTable->Release();

	if (lpFolder)
		lpFolder->Release();

	return hr;
}

/** 
 * Lookup a header value in a full message
 * 
 * @param[in] strMessage The message to find the header in
 * @param[in] strHeader The header to find in the header part of the message
 * @param[in] strDefault The default value of the header if it wasn't found
 * 
 * @return The header value from the message enclosed in quotes, or the given default
 */
string IMAP::GetHeaderValue(const string &strMessage, const string &strHeader, const string &strDefault) {
	string::size_type posStart, posEnd;

	posStart = strMessage.find(strHeader);
	if (posStart == string::npos)
		return strDefault;

	posStart += strHeader.length();
	posEnd = strMessage.find("\r\n", posStart);
	if (posEnd == string::npos)
		return strDefault;

	// don't care about uppercase, it's all good.
	return "\"" + strMessage.substr(posStart, posEnd - posStart) + "\"";
}

/** 
 * Create a bodystructure (RFC 3501). Since this is parsed from a
 * VMIME generated message, this function has alot of assumptions. It
 * will still fail to correctly generate a body structure in the case
 * when an email contains another (quoted) RFC-822 email.
 * 
 * @param[in] bExtended generate BODYSTRUCTURE (true) or BODY (false) version 
 * @param[out] strBodyStructure The generated body structure
 * @param[in] strMessage use this message to generate the output
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrGetBodyStructure(bool bExtended, string &strBodyStructure, const string& strMessage) {
	HRESULT hr = hrSuccess;

	if (bExtended)
		hr = createIMAPProperties(strMessage, NULL, NULL, &strBodyStructure);
	else
		hr = createIMAPProperties(strMessage, NULL, &strBodyStructure, NULL);

	return hr;
}

/** 
 * Convert a MAPI FILETIME structure to a IMAP string. This string is
 * slightly differently formatted than an RFC-822 string.
 *
 * Format: 01-Jan-2006 00:00:00 +0000
 * 
 * @param[in] sFileTime time structure to convert
 * 
 * @return date/time in string format
 */
string IMAP::FileTimeToString(FILETIME sFileTime) {
	string strTime;
	char szBuffer[31];
	time_t sTime;
	struct tm ptr;

	sTime = FileTimeToUnixTime(sFileTime.dwHighDateTime, sFileTime.dwLowDateTime);
	gmtime_safe(&sTime, &ptr);
	strftime(szBuffer, 30, "%d-", &ptr);
	strTime += szBuffer;
	strTime += strMonth[ptr.tm_mon];
	strftime(szBuffer, 30, "-%Y %H:%M:%S +0000", &ptr);
	strTime += szBuffer;

	return strTime;
}

/** 
 * Parses an IMAP date/time string into a MAPI FILETIME struct.
 * 
 * @todo, rewrite this function, but it's not widely used (append + search, both slow anyway)
 *
 * @param[in] strTime parse this string into a FILETIME structure
 * @param[in] bDateOnly parse date part only (true) or add time (false)
 * 
 * @return MAPI FILETIME structure
 */
FILETIME IMAP::StringToFileTime(string strTime, bool bDateOnly) {
	FILETIME sFileTime;
	struct tm sTm;
	ULONG ulMonth;
	time_t sTime;
	
	sTm.tm_mday = 1;
	sTm.tm_mon = 0;
	sTm.tm_year = 100;			// years since 1900
	sTm.tm_hour = 0;
	sTm.tm_min = 0;
	sTm.tm_sec = 0;
	sTm.tm_isdst = -1;			// daylight saving time off

	// 01-Jan-2006 00:00:00 +0000
	if (strTime.size() < 2)
		goto done;
	
	if(strTime.at(1) == '-'){
		strTime = " " + strTime;
	}

	// day of month
	if(strTime.at(0) == ' '){
		sTm.tm_mday = atoi(strTime.substr(1, 1).c_str());
	}else{
		sTm.tm_mday = atoi(strTime.substr(0, 2).c_str());
	}
	// month name 3 chars
	if (strTime.size() < 6)
		goto done;

	sTm.tm_mon = 0;
	for (ulMonth = 0; ulMonth < 12; ++ulMonth) {
		if (CaseCompare(strMonth[ulMonth],strTime.substr(3, 3))) {
			sTm.tm_mon = ulMonth;
		}
	}
	
	    
	if (strTime.size() < 11)
		goto done;

	sTm.tm_year = atoi(strTime.substr(7, 4).c_str()) - 1900;	// year 4 chars
	if (strTime.size() < 14)
		goto done;

	if (bDateOnly)
	    goto done;

	sTm.tm_hour = atoi(strTime.substr(12, 2).c_str());	// hours
	if (strTime.size() < 17)
		goto done;

	sTm.tm_min = atoi(strTime.substr(15, 2).c_str());	// minutes
	if (strTime.size() < 20)
		goto done;

	sTm.tm_sec = atoi(strTime.substr(18, 2).c_str());	// seconds
	if (strTime.size() < 26)
		goto done;

	if (strTime.substr(21, 1) == "+") {
		sTm.tm_hour -= atoi(strTime.substr(22, 2).c_str());
		sTm.tm_min -= atoi(strTime.substr(24, 2).c_str());
	} else if (strTime.substr(21, 1) == "-") {
		sTm.tm_hour += atoi(strTime.substr(22, 2).c_str());
		sTm.tm_min += atoi(strTime.substr(24, 2).c_str());
	}

done:
	sTime = timegm(&sTm);
	UnixTimeToFileTime(sTime, &sFileTime);
	return sFileTime;
}

/** 
 * Add 24 hours to the given time struct
 * 
 * @param[in] sFileTime Original time
 * 
 * @return Input + 24 hours
 */
FILETIME IMAP::AddDay(FILETIME sFileTime) {
	FILETIME sFT;

	// add 24 hour in seconds = 24*60*60 seconds
	UnixTimeToFileTime(FileTimeToUnixTime(sFileTime.dwHighDateTime, sFileTime.dwLowDateTime) + 24 * 60 * 60, &sFT);
	return sFT;
}

/** 
 * Converts a unicode string to an encoded representation in a
 * specified charset. This function can return either quoted-printable
 * or base64 encoded data.
 * 
 * @param[in] input string to escape in quoted-printable or base64
 * @param[in] charset charset for output string
 * @param[in] bIgnore add the //TRANSLIT or //IGNORE flag to iconv
 * 
 * @return 
 */
string IMAP::EscapeString(WCHAR *input, std::string& charset, bool bIgnore)
{
	std::string tmp;
	std::string iconvCharset = charset;
	if (bIgnore)
		setCharsetBestAttempt(iconvCharset);
	try {
		tmp = convert_to<std::string>(iconvCharset.c_str(), input, rawsize(input), CHARSET_WCHAR);
	} catch (const convert_exception &ce) {
		return "NIL";
	}
	// known charsets that are better represented in base64 than quoted-printable
	if (CaseCompare(charset, "UTF-8") || CaseCompare(charset, "ISO-2022-JP"))
		return "\"" + ToQuotedBase64Header(tmp, charset) + "\"";
	else
		return "\"" + ToQuotedPrintable(tmp, charset, true, true) + "\"";
}

/** 
 * Escapes input string with \ character for specified characters.
 * 
 * @param[in] input string to escape
 * 
 * @return escaped string
 */
string IMAP::EscapeStringQT(const string &input) {
	string s;
	unsigned int i;

	/*
	 * qtext           =       NO-WS-CTL /     ; Non white space controls
	 * %d33 /          ; The rest of the US-ASCII
	 * %d35-91 /       ;  characters not including "\"
	 * %d93-126        ;  or the quote character
	 */

	s.reserve(input.length() * 2); // worst-case, only short strings are passing in this function
	s.append(1, '"');
	// We quote NO-WS-CTL anyway, just to be sure
	for (i = 0; i < input.length(); ++i) {
		if (input[i] == 33 || (input[i] >= 35 && input[i] <= 91) || (input[i] >= 93 && input[i] <= 126))
			s += input[i];
		else if (input[i] == 34) {
			// " found, should send literal and data
			s = "{" + stringify(input.length()) + "}\n" + input;
			goto exit;
		} else {
			s.append(1, '\\');
			s += input[i];
		}
	}
	s.append(1, '"');

exit:
	return s;
}

/**
 * @brief Converts an unicode string to modified UTF-7
 *
 * IMAP folder encoding is a modified form of utf-7 (+ becomes &, so & is "escaped",
 * utf-7 is a modifed form of base64, based from the utf16 character
 * I'll use the iconv convertor for this, per character .. sigh
 *
 * @param[in]	input	unicode string to convert
 * @param[out]	output	valid IMAP folder name to send to the client
 * @return MAPI Error code
 */
HRESULT IMAP::MAPI2IMAPCharset(const wstring &input, string &output) {
	size_t i;
	convert_context converter;

	output.clear();
	output.reserve(input.size() * 2);
	for (i = 0; i < input.length(); ++i) {
		if ( (input[i] >= 0x20 && input[i] <= 0x25) || (input[i] >= 0x27 && input[i] <= 0x7e) ) {
			if (input[i] == '"' || input[i] == '\\')
				output += '\\';
			output += input[i];
		} else if (input[i] == 0x26) {
			output += "&-";		// & is encoded as &-
		} else {
			wstring conv;
			string utf7;
			conv = input[i];
			while (i+1 < input.length() && (input[i+1] < 0x20 || input[i+1] >= 0x7f))
				conv += input[++i];

			try {
				utf7 = converter.convert_to<string>("UTF-7", conv, rawsize(conv), CHARSET_WCHAR);
			} catch(...) {
				return MAPI_E_BAD_CHARWIDTH;
			}

			utf7[0] = '&';		// convert + to &
			for (size_t j = 0; j < utf7.size(); ++j)
				if (utf7[j] == '/')
					utf7[j] = ','; // convert / from base64 to ,
			output += utf7;	// also contains the terminating -
		}
	}

	return hrSuccess;
}

/**
 * Converts an IMAP encoded folder string to an unicode string.
 *
 * @param[in]	input	IMAP folder name, in modified UTF-7
 * @param[out]	output	 widestring version of input
 * @return MAPI Error code
 */
HRESULT IMAP::IMAP2MAPICharset(const string& input, wstring& output) {
	size_t i;
	convert_context converter;

	output.clear();
	output.reserve(input.size());
	for (i = 0; i < input.length(); ++i) {
		if (input[i] < 0 || input[i] > 127)
			return MAPI_E_BAD_CHARWIDTH;
		if (input[i] == '&') {
			if (i+1 >= input.length()) {
				// premature end of string
				output += input[i];
				break;
			}
			if (input[i+1] == '-') {
				output += '&';
				++i; // skip '-'
			} else {
				string conv = "+";
				++i; // skip imap '&', is a '+' in utf-7
				while (i < input.length() && input[i] != '-') {
					if (input[i] == ',')
						conv += '/'; // , -> / for utf-7
					else
						conv += input[i];
					++i;
				}
				try {
					output += converter.convert_to<wstring>(CHARSET_WCHAR, conv, rawsize(conv), "UTF-7");
				} catch(...) {
					return MAPI_E_BAD_CHARWIDTH;
				}
			}
		} else {
			if (input[i] == '\\' && i+1 < input.length() && (input[i+1] == '"' || input[i+1] == '\\'))
				++i;
			output += input[i];
		}
	}
	return hrSuccess;
}

/** 
 * Check for a MAPI LIST/LSUB pattern in a given foldername.
 * 
 * @param[in] strFolder folderpath to match
 * @param[in] strPattern Pattern to match with, in uppercase.
 * 
 * @return whether folder matches
 */
bool IMAP::MatchFolderPath(wstring strFolder, const wstring& strPattern)
{
    bool bMatch = false;
    int f = 0;
    int p = 0;
    
    ToUpper(strFolder);
    
    while(1) {
        if(f == (int)strFolder.size() && p == (int)strPattern.size()) {
            // Reached the end of the folder and the pattern strings, so match
            bMatch = true;
            break;
        }
        
        if(strPattern[p] == '*') {
            // Match 0-n chars, try longest match first
            for (int i = strFolder.size(); i >= f; --i) {
                // Try matching the rest of the string from position i in the string
                if(MatchFolderPath(strFolder.substr(i), strPattern.substr(p+1))) {
                    // Match OK, apply the 'skip i' chars
                    bMatch = true;
                    goto exit;
                }
            }
            
            // No match found, failed
            bMatch = false;
            break;
        } else if(strPattern[p] == '%') {
            // Match 0-n chars excluding '/', try longest match first
            size_t slash = strFolder.find('/', f);
            if(slash == std::string::npos)
                slash = strFolder.size();

            for (int i = slash; i >= f; --i) {
                // Try matching the rest of the string from position i in the string
                if(MatchFolderPath(strFolder.substr(i), strPattern.substr(p+1))) {
                    // Match OK, apply the 'skip i' chars
                    bMatch = true;
                    goto exit;
                }
            }

            // No match found, failed
            bMatch = false;
            break;
        } else {
            // Match normal string
            if(strFolder[f] != strPattern[p])
                break;
            ++f;
            ++p;
        }
    }
    
exit:
    return bMatch;
}

/** 
 * Parse RFC-822 email headers in to a list of string <name, value> pairs.
 * 
 * @param[in] strHeaders Email headers to parse (this data will be modified and should not be used after this function)
 * @param[out] lstHeaders list of headers, in header / value pairs
 */
void IMAP::HrParseHeaders(const string &strHeaders, list<pair<string, string> > &lstHeaders)
{
    size_t pos = 0;
    size_t end = 0;
    string strLine;
    string strField;
    string strData;
    list<pair<string, string> >::iterator iterLast;
    
    lstHeaders.clear();
    iterLast = lstHeaders.end();
    
    while(1) {
        end = strHeaders.find("\r\n", pos);
        
        if(end == string::npos) {
            strLine = strHeaders.substr(pos);
        } else {
            strLine = strHeaders.substr(pos, end-pos);
        }
		if (strLine.empty())
			break;				// parsed all headers

        if((strLine[0] == ' ' || strLine[0] == '\t') && iterLast != lstHeaders.end()) {
            // Continuation of previous header
            iterLast->second += "\r\n";
            iterLast->second += strLine;
        } else {
            size_t colon = strLine.find(":");
            
            if(colon != string::npos) {
                // Get field name
                strField = strLine.substr(0, colon);
            
                strData = strLine.substr(colon+1);
                
                // Remove leading spaces
                while(strData[0] == ' ') {
                    strData.erase(0,1);
                }
                
                lstHeaders.push_back(pair<string, string>(strField, strData));
                iterLast = --lstHeaders.end();
            } else {
                // Broken header ? (no :)
            }
        }    
        
        if(end == string::npos)
            break;
        
        pos = end + 2; // Go to next line (+2 = \r\n)
    }
}

/**
 * Find a substring in strInput that starts with strBegin, and
 * possebly ends with strEnd. Only the data between the strBegin and
 * strEnd is returned.
 *
 * @param[out]	strOutput	The found substring in strInput
 * @param[in]	strInput	Input string
 * @param[in]	strBegin	Substring should start with
 * @param[in]	strEnd	Substring should end with
 */
void IMAP::HrGetSubString(std::string &strOutput, const std::string &strInput,
    const std::string &strBegin, const std::string &strEnd)
{
    size_t begin;
    size_t end;

    strOutput.clear();

    begin = strInput.find(strBegin);
    if(begin == string::npos)
        return;
        
    end = strInput.find(strEnd, begin+1);
    if(end == string::npos)
        strOutput = strInput.substr(begin+1);
    else
        strOutput = strInput.substr(begin+1, end-begin-1);
}

/** 
 * Make a set of tokens from a string, separated by spaces.
 * 
 * @param[out] setTokens A set of strings from input
 * @param[in] strInput split by spaces into the set
 */
void IMAP::HrTokenize(std::set<std::string> &setTokens,
    const std::string &strInput)
{
    vector<string> lstTokens = tokenize(strInput, " ");

    setTokens.clear();
	std::copy(lstTokens.begin(), lstTokens.end(), std::inserter(setTokens, setTokens.begin()));    
}

/** 
 * Find the MAPI folder from a full folder path
 * 
 * @param[in] strFolder The full folder path
 * @param[in] bReadOnly open read-only or with write access (if possible)
 * @param[out] lppFolder The MAPI folder corresponding to the given name
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrFindFolder(const wstring& strFolder, bool bReadOnly, IMAPIFolder **lppFolder)
{
    HRESULT hr = hrSuccess;
    ULONG cbEntryID = 0;
    LPENTRYID lpEntryID = NULL;
    ULONG ulObjType = 0;
    IMAPIFolder *lpFolder = NULL;
	ULONG ulFlags = 0;

	if (!bReadOnly)
		ulFlags |= MAPI_MODIFY;

    hr = HrFindFolderEntryID(strFolder, &cbEntryID, &lpEntryID);
    if(hr != hrSuccess)
        goto exit;
        
    hr = lpSession->OpenEntry(cbEntryID, lpEntryID, NULL, ulFlags, &ulObjType, (IUnknown **)&lpFolder);
    if(hr != hrSuccess)
        goto exit;
        
    if(ulObjType != MAPI_FOLDER) {
        hr = MAPI_E_INVALID_PARAMETER;
        goto exit;
    }
    
    *lppFolder = lpFolder;
    
exit:
	MAPIFreeBuffer(lpEntryID);
	return hr;
}

/** 
 * Find an EntryID of a folder from a full folder path
 * 
 * @param[in] strFolder Full path of a folder to find the MAPI EntryID for
 * @param[out] lpcbEntryID number of bytes in lppEntryID
 * @param[in] lppEntryID The EntryID of the given folder
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrFindFolderEntryID(const wstring& strFolder, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
    HRESULT hr = hrSuccess;
    vector<wstring> vFolders;
    ULONG cbEntryID = 0;
    LPENTRYID lpEntryID = NULL;
    IMAPIFolder *lpFolder = NULL;
    ULONG ulObjType = 0;
    
    hr = HrSplitPath(strFolder, vFolders);
    if(hr != hrSuccess)
        goto exit;
        
    for (unsigned int i = 0; i < vFolders.size(); ++i) {
        hr = HrFindSubFolder(lpFolder, vFolders[i], &cbEntryID, &lpEntryID);
        if(hr != hrSuccess)
            goto exit;
            
        if(i == vFolders.size() - 1)
            break;
            
        if(lpFolder)
            lpFolder->Release();
        lpFolder = NULL;
            
        hr = lpSession->OpenEntry(cbEntryID, lpEntryID, NULL, MAPI_MODIFY, &ulObjType, (IUnknown **)&lpFolder);
        if(hr != hrSuccess)
            goto exit;
            
        if(ulObjType != MAPI_FOLDER) {
            hr = MAPI_E_INVALID_PARAMETER;
            goto exit;
        }
            
        MAPIFreeBuffer(lpEntryID);
        lpEntryID = NULL;
    }
    
    *lpcbEntryID = cbEntryID;
    *lppEntryID = lpEntryID;
    lpEntryID = NULL;
            
exit:
    MAPIFreeBuffer(lpEntryID);
        
    if(lpFolder)
        lpFolder->Release();
        
    return hr;
}

/** 
 * Find the EntryID for a named subfolder in a given MAPI Folder
 *
 * @param[in] lpFolder The parent folder to find strFolder in, or NULL when no parent is present yet.
 * When no parent is present, one will be found as either it's: INBOX, "public", or the user store.
 * @param[in] strFolder The name of the subfolder to find
 * @param[out] lpcbEntryID number of bytes in lppEntryID
 * @param[out] lppEntryID The EntryID of the folder
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrFindSubFolder(IMAPIFolder *lpFolder, const wstring& strFolder, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
    HRESULT hr = hrSuccess;
    IMAPITable *lpTable = NULL;
    SRestriction sRestrict;
    SPropValue sProp;
    SizedSPropTagArray(2, sptaCols) = {2, { PR_ENTRYID, PR_DISPLAY_NAME_W }};
    LPENTRYID lpEntryID = NULL;
    ULONG cbEntryID = 0;
    LPSRowSet lpRowSet = NULL;
    LPSPropValue lpProp = NULL;
    IMAPIFolder *lpSubTree = NULL;
    ULONG ulObjType = 0;
    
    sRestrict.rt = RES_PROPERTY;
    sRestrict.res.resProperty.ulPropTag = PR_DISPLAY_NAME_W;
    sRestrict.res.resProperty.relop = RELOP_EQ;
    sRestrict.res.resProperty.lpProp = &sProp;
    
    sProp.ulPropTag = PR_DISPLAY_NAME_W;
    sProp.Value.lpszW = (WCHAR *)strFolder.c_str();
    
    // lpFolder is NULL when we're referring to the IMAP root. The IMAP root contains
    // INBOX, the public folder container, and all folders under the users IPM_SUBTREE.
    
    if(lpFolder == NULL) {
        if(wcscasecmp(strFolder.c_str(), L"INBOX") == 0) {
            // Inbox request, we know where that is.
            hr = lpStore->GetReceiveFolder((LPTSTR)"IPM", 0, lpcbEntryID, lppEntryID, NULL);
            goto exit;            
        } else if(wcscasecmp(strFolder.c_str(), PUBLIC_FOLDERS_NAME) == 0) {
            // Public folders requested, we know where that is too
			if(!lpPublicStore) {
				hr = MAPI_E_NOT_FOUND;
				goto exit;
			}

            hr = HrGetOneProp(lpPublicStore, PR_IPM_PUBLIC_FOLDERS_ENTRYID, &lpProp);
            if(hr != hrSuccess)
                goto exit;
                
            cbEntryID = lpProp->Value.bin.cb;
            hr = MAPIAllocateBuffer(cbEntryID, (void **)&lpEntryID);
            if(hr != hrSuccess)
                goto exit;
                
            memcpy(lpEntryID, lpProp->Value.bin.lpb, cbEntryID);
            
            *lppEntryID = lpEntryID;
            *lpcbEntryID = cbEntryID;
            
            goto exit;
        } else {
            // Other folder in the root requested, use normal search algorithm to find it
            // under IPM_SUBTREE
            
            hr = HrGetOneProp(lpStore, PR_IPM_SUBTREE_ENTRYID, &lpProp);
            if(hr != hrSuccess)
                goto exit;
                
            hr = lpStore->OpenEntry(lpProp->Value.bin.cb, (LPENTRYID)lpProp->Value.bin.lpb, NULL, 0, &ulObjType, (IUnknown **)&lpSubTree);
            if(hr != hrSuccess)
                goto exit;
                
            lpFolder = lpSubTree;
            // Fall through to normal folder lookup code
        }
    }
    
    // Use a restriction to find the folder in the hierarchy table
    hr = lpFolder->GetHierarchyTable(MAPI_DEFERRED_ERRORS, &lpTable);
    if(hr != hrSuccess)
        goto exit;
     
    hr = lpTable->SetColumns((LPSPropTagArray)&sptaCols, 0);
    if(hr != hrSuccess)
        goto exit;
        
    hr = lpTable->Restrict(&sRestrict, TBL_BATCH);
    if(hr != hrSuccess)
        goto exit;
        
    hr = lpTable->QueryRows(1, 0, &lpRowSet);
    if(hr != hrSuccess)
        goto exit;
        
    if(lpRowSet->cRows == 0) {
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    if(lpRowSet->aRow[0].lpProps[0].ulPropTag != PR_ENTRYID) {
        hr = MAPI_E_INVALID_PARAMETER;
        goto exit;
    }
    
    cbEntryID = lpRowSet->aRow[0].lpProps[0].Value.bin.cb;

    hr = MAPIAllocateBuffer(cbEntryID, (void **)&lpEntryID);
    if(hr != hrSuccess)
        goto exit;
        
    memcpy(lpEntryID, lpRowSet->aRow[0].lpProps[0].Value.bin.lpb, cbEntryID);
    
    *lppEntryID = lpEntryID;
    *lpcbEntryID = cbEntryID;
    
exit:
    if(lpSubTree)
        lpSubTree->Release();
    MAPIFreeBuffer(lpProp);
    if(lpRowSet)
        FreeProws(lpRowSet);
    if(lpTable)
        lpTable->Release();
         
    return hr;
}

/**
 * Find the deepest folder in strFolder and return that IMAPIFolder
 * and the remaining folders which were not found.
 * If no folders are found from strFolder at all, return the IPM subtree.
 *
 * @param[in]	strFolder	Folder string complete path to find deepest IMAPIFolder for
 * @param[out]	lppFolder	Last IMAPIFolder found in strFolder
 * @param[out]	strNotFound	Folders not found in strFolder
 */
HRESULT IMAP::HrFindFolderPartial(const wstring& strFolder, IMAPIFolder **lppFolder, wstring *strNotFound)
{
    HRESULT hr = hrSuccess;
    vector<wstring> vFolders;
    ULONG cbEntryID = 0;
    LPENTRYID lpEntryID = NULL;
    IMAPIFolder *lpFolder = NULL;
    ULONG ulObjType = 0;
    LPSPropValue lpTree = NULL;
    unsigned int i = 0;
    
    hr = HrSplitPath(strFolder, vFolders);
    if(hr != hrSuccess)
        goto exit;
        
    // Loop through all the path parts until we find a part that we can't find
    for (i = 0; i < vFolders.size(); ++i) {
        hr = HrFindSubFolder(lpFolder, vFolders[i], &cbEntryID, &lpEntryID);
        if(hr != hrSuccess) {
            hr = hrSuccess; // Not an error
            break;
        }
        
        if(lpFolder)
            lpFolder->Release();
        lpFolder = NULL;
            
        hr = lpSession->OpenEntry(cbEntryID, lpEntryID, NULL, MAPI_MODIFY, &ulObjType, (IUnknown **)&lpFolder);
        if(hr != hrSuccess)
            goto exit;

        MAPIFreeBuffer(lpEntryID);
        lpEntryID = NULL;
        
    }
    
    // Remove parts that we already have processed
    vFolders.erase(vFolders.begin(),vFolders.begin()+i);

    // The remaining path parts are the relative path that we could not find    
    hr = HrUnsplitPath(vFolders, *strNotFound);
    if(hr != hrSuccess)
        goto exit;

    if(lpFolder == NULL) {
        hr = HrGetOneProp(lpStore, PR_IPM_SUBTREE_ENTRYID, &lpTree);
        if(hr != hrSuccess)
            goto exit;
            
        hr = lpSession->OpenEntry(lpTree->Value.bin.cb, (LPENTRYID)lpTree->Value.bin.lpb, NULL, MAPI_MODIFY, &ulObjType, (IUnknown **)&lpFolder);
        if(hr != hrSuccess)
            goto exit;
    }
            
    *lppFolder = lpFolder;
    lpFolder = NULL;
    
exit:
	MAPIFreeBuffer(lpTree);
	if (lpFolder)
		lpFolder->Release();
	MAPIFreeBuffer(lpEntryID);
	return hr;
}

/** 
 * Special MAPI Folders are blocked to delete, rename or unsubscribe from.
 * 
 * @param[in] lpFolder MAPI Folder to check
 * 
 * @return Special (true) or not (false)
 */
bool IMAP::IsSpecialFolder(IMAPIFolder *lpFolder)
{
    bool result = false;
    LPSPropValue lpProp = NULL;
    HRESULT hr = hrSuccess;
    
    hr = HrGetOneProp(lpFolder, PR_ENTRYID, &lpProp);
    if(hr != hrSuccess)
        goto exit;
        
    result = IsSpecialFolder(lpProp->Value.bin.cb, (LPENTRYID)lpProp->Value.bin.lpb);
    
exit:
	MAPIFreeBuffer(lpProp);
	return result;
}

/** 
 * Check if this folder contains e-mail
 * 
 * @param[in] lpFolder MAPI Folder to check
 * 
 * @return may contain e-mail (true) or not (false)
 */
bool IMAP::IsMailFolder(IMAPIFolder *lpFolder)
{
    bool result = false;
    LPSPropValue lpProp = NULL;
    HRESULT hr = hrSuccess;
    
    hr = HrGetOneProp(lpFolder, PR_CONTAINER_CLASS_A, &lpProp);
    if(hr != hrSuccess) {
		// if the property is missing, treat it as an email folder
		result = true;
        goto exit;
	}
        
	result = strcasecmp(lpProp->Value.lpszA, "IPM") == 0 || strcasecmp(lpProp->Value.lpszA, "IPF.NOTE") == 0;
    
exit:
	MAPIFreeBuffer(lpProp);
	return result;
}

bool IMAP::IsSentItemFolder(IMAPIFolder *lpFolder)
{
    ULONG ulResult = FALSE;
    LPSPropValue lpProp = NULL;
	LPSPropValue lpPropStore = NULL;
    HRESULT hr = hrSuccess;
    
	hr = HrGetOneProp(lpFolder, PR_ENTRYID, &lpProp);
	if (hr != hrSuccess)
		goto exit;

	hr = HrGetOneProp(lpStore, PR_IPM_SENTMAIL_ENTRYID, &lpPropStore);
	if (hr != hrSuccess)
		goto exit;

	hr = lpStore->CompareEntryIDs(lpProp->Value.bin.cb, (LPENTRYID)lpProp->Value.bin.lpb, lpPropStore->Value.bin.cb, (LPENTRYID)lpPropStore->Value.bin.lpb , 0, &ulResult);
	if (hr != hrSuccess)
		goto exit;
exit:
	MAPIFreeBuffer(lpProp);
	MAPIFreeBuffer(lpPropStore);
	return ulResult;
}

/** 
 * Return the parent folder for an EntryID
 * 
 * @param[in] cbEntryID number of bytes in lpEntryID
 * @param[in] lpEntryID EntryID of a folder
 * @param[out] lppFolder Parent MAPI Folder of given EntryID
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrOpenParentFolder(ULONG cbEntryID, LPENTRYID lpEntryID, IMAPIFolder **lppFolder)
{
    HRESULT hr = hrSuccess;
    IMAPIFolder *lpFolder = NULL;
    ULONG ulObjType = 0;

	hr = lpSession->OpenEntry(cbEntryID, lpEntryID, NULL, MAPI_MODIFY, &ulObjType, reinterpret_cast<IUnknown **>(&lpFolder));
	if (hr != hrSuccess)
		goto exit;
	if (ulObjType != MAPI_FOLDER) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}
        
    hr = HrOpenParentFolder(lpFolder, lppFolder);

exit:
    if(lpFolder)
        lpFolder->Release();
        
    return hr;  
}

/** 
 * Open parent MAPI folder for given MAPI folder
 * 
 * @param[in] lpFolder MAPI Folder to open the parent folder for
 * @param[out] lppFolder Parent folder
 * 
 * @return MAPI Error code
 */
HRESULT IMAP::HrOpenParentFolder(IMAPIFolder *lpFolder, IMAPIFolder **lppFolder)
{
    HRESULT hr = hrSuccess;
    LPSPropValue lpParent = NULL;
    ULONG ulObjType = 0;
    
    hr = HrGetOneProp(lpFolder, PR_PARENT_ENTRYID, &lpParent);
    if(hr != hrSuccess)
        goto exit;
        
    hr = lpSession->OpenEntry(lpParent->Value.bin.cb, (LPENTRYID)lpParent->Value.bin.lpb, NULL, MAPI_MODIFY, &ulObjType, (IUnknown **)lppFolder);
    if(hr != hrSuccess)
        goto exit;
    
exit:
	MAPIFreeBuffer(lpParent);
	return hr;
}

/** @} */
