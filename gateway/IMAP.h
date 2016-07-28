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

#ifndef IMAP_H
#define IMAP_H

#include "ClientProto.h"

#include <string>
#include <vector>
#include <list>
#include <set>

#include <kopano/ECIConv.h>
#include <kopano/ECChannel.h>

using namespace std;

/**
 * @defgroup gateway_imap IMAP 
 * @ingroup gateway
 * @{
 */

#define ROWS_PER_REQUEST 200
#define IMAP_HIERARCHY_DELIMITER '/'
#define PUBLIC_FOLDERS_NAME L"Public folders"

#define IMAP_RESP_MAX	65536

#define RESP_UNTAGGED "* "
#define RESP_CONTINUE "+ "

#define RESP_TAGGED_OK " OK "
#define RESP_TAGGED_NO " NO "
#define RESP_TAGGED_BAD " BAD "


LONG __stdcall IMAPIdleAdviseCallback(void *lpContext, ULONG cNotif, LPNOTIFICATION lpNotif);

class BinaryArray {
public:
	BinaryArray(void) : lpb(NULL), cb(0), bcheap(false) {}
	BinaryArray(BYTE *lpData, ULONG cbData, bool bcheap = false)
	{
		this->bcheap = bcheap;
		if (cbData == 0) {
			cb = 0;
			lpb = NULL;
		} else {
			if (!bcheap) {
				lpb = new BYTE[cbData];
				memcpy(lpb, lpData, cbData);
			} else {
				lpb = lpData;
			}
			cb = cbData;
		}
	}
	BinaryArray(const BinaryArray &old) {
		bcheap = false;
		if (old.cb == 0) {
			cb = 0;
			lpb = NULL;
		} else {
			cb = old.cb;
			lpb = new BYTE[cb];
			memcpy(lpb, old.lpb, cb);
		}
	}
	BinaryArray(const SBinary &bin) {
		bcheap = false;
		if (bin.cb == 0) {
			cb = 0;
			lpb = NULL;
		} else {
			lpb = new BYTE[bin.cb];
			memcpy(lpb, bin.lpb, bin.cb);
			cb = bin.cb;
		}
	}
	~BinaryArray(void)
	{
		if (!bcheap)
			delete[] lpb;
	}

	bool operator==(const BinaryArray &b) const
	{
		if (b.cb == 0 && this->cb == 0)
			return true;
		if (b.cb != this->cb)
			return false;
		else
			return memcmp(lpb, b.lpb, cb) == 0;
	}

	BinaryArray &operator=(const BinaryArray &b)
	{
		BYTE *lpbPrev = lpb;
		if (b.cb == 0) {
			cb = 0;
			lpb = NULL;
		} else {
			cb = b.cb;
			lpb = new BYTE[cb];
			memcpy(lpb, b.lpb, cb);
			if (!bcheap)
				delete[] lpbPrev;
		}
		bcheap = false;
		return *this;
	}
    
	BYTE *lpb;
	ULONG cb;
	bool bcheap;
};

struct lessBinaryArray {
	bool operator()(const BinaryArray& a, const BinaryArray& b) const
	{
		if (a.cb < b.cb || (a.cb == b.cb && memcmp(a.lpb, b.lpb, a.cb) < 0) )
			return true;
		return false;
	}
};

// FLAGS: \Seen \Answered \Flagged \Deleted \Draft \Recent
class IMAP : public ClientProto {
public:
	IMAP(const char *szServerPath, ECChannel *lpChannel, ECLogger *lpLogger, ECConfig *lpConfig);
	~IMAP();

	int getTimeoutMinutes();
	bool isIdle();
	bool isContinue();

	HRESULT HrSendGreeting(const std::string &strHostString);
	HRESULT HrCloseConnection(const std::string &strQuitMsg);
	HRESULT HrProcessCommand(const std::string &strInput);
	HRESULT HrProcessContinue(const std::string &strInput);
	HRESULT HrDone(bool bSendResponse);

private:
	void CleanupObject();
	void ReleaseContentsCache();

	std::string GetCapabilityString(bool bAllFlags);

	HRESULT HrSplitInput(const string &strInput, vector<string> &lstWords);
	HRESULT HrSplitPath(const wstring &strInput, vector<wstring> &lstFolders);
	HRESULT HrUnsplitPath(const vector<wstring> &lstFolders, wstring &strPath);

	// All IMAP4rev1 commands
	HRESULT HrCmdCapability(const string &strTag);
	HRESULT HrCmdNoop(const string &strTag);
	HRESULT HrCmdLogout(const string &strTag);
	HRESULT HrCmdStarttls(const string &strTag);
	HRESULT HrCmdAuthenticate(const string &strTag, string strAuthMethod, const string &strAuthData);
	HRESULT HrCmdLogin(const string &strTag, const string &strUser, const string &strPass);
	HRESULT HrCmdSelect(const string &strTag, const string &strFolder, bool bReadOnly);
	HRESULT HrCmdCreate(const string &strTag, const string &strFolder);
	HRESULT HrCmdDelete(const string &strTag, const string &strFolder);
	HRESULT HrCmdRename(const string &strTag, const string &strExistingFolder, const string &strNewFolder);
	HRESULT HrCmdSubscribe(const string &strTag, const string &strFolder, bool bSubscribe);
	HRESULT HrCmdList(const string &strTag, string strReferenceFolder, const string &strFolder, bool bSubscribedOnly);
	HRESULT HrCmdStatus(const string &strTag, const string &strFolder, string strStatusData);
	HRESULT HrCmdAppend(const string &strTag, const string &strFolder, const string &strData, string strFlags=string(), const string &strTime=string());
	HRESULT HrCmdCheck(const string &strTag);
	HRESULT HrCmdClose(const string &strTag);
	HRESULT HrCmdExpunge(const string &strTag, const string &strSeqSet);
	HRESULT HrCmdSearch(const string &strTag, vector<string> &lstSearchCriteria, bool bUidMode);
	HRESULT HrCmdFetch(const string &strTag, const string &strSeqSet, const string &strMsgDataItemNames, bool bUidMode);
	HRESULT HrCmdStore(const string &strTag, const string &strSeqSet, const string &strMsgDataItemName, const string &strMsgDataItemValue, bool bUidMode);
	HRESULT HrCmdCopy(const string &strTag, const string &strSeqSet, const string &strFolder, bool bUidMode);
	HRESULT HrCmdUidXaolMove(const string &strTag, const string &strSeqSet, const string &strFolder);
	HRESULT HrCmdIdle(const string &strTag);
	HRESULT HrCmdNamespace(const string &strTag);
	HRESULT HrCmdGetQuotaRoot(const string &strTag, const string &strFolder);
	HRESULT HrCmdGetQuota(const string &strTag, const string &strQuotaRoot);
	HRESULT HrCmdSetQuota(const string &strTag, const string &strQuotaRoot, const string &strQuotaList);

	/* Untagged response, * or + */
	HRESULT HrResponse(const string &strUntag, const string& strResponse);
	/* Tagged response with result OK, NO or BAD */
	HRESULT HrResponse(const string &strResult, const string &strTag, const string& strResponse);

private:
	bool bOnlyMailFolders;
	bool bShowPublicFolder;

	// All data per folder for the folderlist
	struct SFolder {
		BinaryArray sEntryID;	// EntryID of folder
		wstring strFolderName;	// Folder name
		bool bActive;			// Subscribed folder
		bool bMailFolder;		// E-mail type folder
		bool bSpecialFolder;	// 'special' folder (eg inbox)
		bool bHasSubfolders;	// Has child folders
		list<SFolder>::const_iterator lpParentFolder;
	};

	// All data to be mapped per mail in the current folder
	// Used class to be able to use sort
	class SMail {
    public:
        BinaryArray sEntryID;		// EntryID of message
        BinaryArray sInstanceKey;	// Instance key of message
		ULONG ulUid;				// PR_EC_IMAP_UID of message
		bool bRecent;				// \Recent flag
		std::string strFlags;		// String of all flags, including \Recent

		bool operator < (SMail sMail) const {
			return this->ulUid < sMail.ulUid;
		}
		bool operator < (ULONG ulUid) const {
			return this->ulUid < ulUid;
		}
		operator ULONG() const {
			return this->ulUid;
		}
		bool operator == (ULONG ulUid) const {
		    return this->ulUid == ulUid;
		}
	};

	IMAPISession *lpSession;
	IAddrBook *lpAddrBook;
	LPSPropTagArray m_lpsIMAPTags;

	// current folder name
	wstring strCurrentFolder;
	IMAPITable* m_lpTable;		/* current contents table */
	vector<string> m_vTableDataColumns; /* current dataitems that caused the setcolumns on the table */

	// true if folder is opened with examine
	bool bCurrentFolderReadOnly;

	// vector of mails in the current folder. The index is used for mail number.
	vector<SMail> lstFolderMailEIDs;
	IMsgStore *lpStore;
	IMsgStore *lpPublicStore;

	// special folder entryids (not able to move/delete inbox and such ...)
	set<BinaryArray, lessBinaryArray> lstSpecialEntryIDs;

	// Message cache
	string m_strCache;
	ULONG m_ulCacheUID;

	// HrResponseContinuation state, used for HrCmdAuthenticate
	bool m_bContinue;
	string m_strContinueTag;
	
	// Idle mode variables
	bool m_bIdleMode;
	IMAPIAdviseSink *m_lpIdleAdviseSink;
	ULONG m_ulIdleAdviseConnection;
	string m_strIdleTag;
	IMAPITable *m_lpIdleTable;
	pthread_mutex_t m_mIdleLock;
	ULONG m_ulLastUid;
	ULONG m_ulErrors;

	wstring m_strwUsername;

	delivery_options dopt;

	HRESULT HrPrintQuotaRoot(const string& strTag);

	HRESULT HrFindFolder(const wstring& strFolder, bool bReadOnly, IMAPIFolder **lppFolder);
	HRESULT HrFindFolderEntryID(const wstring& strFolder, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	HRESULT HrFindFolderPartial(const wstring& strFolder, IMAPIFolder **lppFolder, wstring *strNotFound);
	HRESULT HrFindSubFolder(IMAPIFolder *lpFolder, const wstring& strFolder, ULONG *lpcbEntryID, LPENTRYID *lppEntryID);
	
	bool IsSpecialFolder(IMAPIFolder *lpFolder);
	bool IsSpecialFolder(ULONG cbEntryID, LPENTRYID lpEntryID);
	bool IsMailFolder(IMAPIFolder *lpFolder);
	bool IsSentItemFolder(IMAPIFolder *lpFolder);
	HRESULT HrOpenParentFolder(ULONG cbEntryID, LPENTRYID lpEntryID, IMAPIFolder **lppFolder);
	HRESULT HrOpenParentFolder(IMAPIFolder *lpFolder, IMAPIFolder **lppFolder);
	HRESULT HrGetFolderList(list<SFolder> &lstFolders);

	/* subscribed folders */
	vector<BinaryArray> m_vSubscriptions;
	HRESULT HrGetSubscribedList();
	HRESULT HrSetSubscribedList();
	HRESULT ChangeSubscribeList(bool bSubscribe, ULONG cbEntryID, LPENTRYID lpEntryID);

	HRESULT HrMakeSpecialsList();

	HRESULT HrRefreshFolderMails(bool bInitialLoad, bool bResetRecent, bool bShowUID, unsigned int *lpulUnseen, ULONG *lpulUIDValidity = NULL);

	HRESULT HrGetSubTree(list<SFolder> &lstFolders, SBinary &sEntryID, wstring strFolderName, list<SFolder>::const_iterator lpParentFolder, bool bSubfolders = true, bool bIsEmailFolder = true);
	HRESULT HrGetFolderPath(list<SFolder>::const_iterator lpFolder, list<SFolder> &lstFolder, wstring &strPath);
	HRESULT HrGetDataItems(string strMsgDataItemNames, vector<string> &lstDataItems);
	HRESULT HrSemicolonToComma(string &strData);

	// fetch calls an other fetch depending on the data items requested
	HRESULT HrPropertyFetch(list<ULONG> &lstMails, vector<string> &lstDataItems);
	HRESULT HrPropertyFetchRow(LPSPropValue lpProps, ULONG cValues, string &strResponse, ULONG ulMailnr, bool bForceFlags, const vector<string> &lstDataItems);

	std::string HrEnvelopeRecipients(LPSRowSet lpRows, ULONG ulType, std::string& strCharset, bool bIgnore);
	std::string HrEnvelopeSender(LPMESSAGE lpMessage, ULONG ulTagName, ULONG ulTagEmail, std::string& strCharset, bool bIgnore);
	HRESULT HrGetMessageEnvelope(string &strResponse, LPMESSAGE lpMessage);
	HRESULT HrGetMessageFlags(string &strResponse, LPMESSAGE lpMessage, bool bRecent);

	HRESULT HrGetMessagePart(string &strMessagePart, string &strMessage, string strPartName);

	ULONG LastOrNumber(const char *szNr, bool bUID);
	HRESULT HrParseSeqSet(const string &strSeqSet, list<ULONG> &lstMails);
	HRESULT HrParseSeqUidSet(const string &strSeqSet, list<ULONG> &lstMails);
	HRESULT HrSeqUidSetToRestriction(const string &strSeqSet, LPSRestriction *lppRestriction);

	HRESULT HrStore(const list<ULONG> &lstMails, string strMsgDataItemName, string strMsgDataItemValue, bool *lpbDoDelete);
	HRESULT HrCopy(const list<ULONG> &lstMails, const string &strFolder, bool bMove);
	HRESULT HrSearch(vector<string> &lstSearchCriteria, ULONG &ulStartCriteria, list<ULONG> &lstMailnr, ECIConv *iconv);

	string GetHeaderValue(const string &strMessage, const string &strHeader, const string &strDefault);
	HRESULT HrGetBodyStructure(bool bExtended, string &strBodyStructure, const string& strMessage);

	HRESULT HrGetEmailAddress(LPSPropValue lpPropValues, ULONG ulAddrType, ULONG ulEntryID, ULONG ulName, ULONG ulEmail, string strHeaderName, string *strHeaders);

	// Make the string uppercase
	void ToUpper(string &strString);
	void ToUpper(wstring &strString);
	bool CaseCompare(const string& strA, const string& strB);

	// IMAP4rev1 date format: 01-Jan-2000 00:00 +0000
	string FileTimeToString(FILETIME sFiletime);
	FILETIME StringToFileTime(string strTime, bool bDateOnly = false);
	// add 24 hour to the time to be able to check if a time is on a date
	FILETIME AddDay(FILETIME sFileTime);

	// escape (quote) a unicode string to a specific charset in quoted-printable header format
	string EscapeString(WCHAR *input, std::string& charset, bool bIgnore = false);

	// escape (quote) a string for a quoted-text (between "")
	string EscapeStringQT(const string &str);

	// Folder names are in a *modified* utf-7 form. See RFC2060, chapter 5.1.3
	HRESULT MAPI2IMAPCharset(const wstring& input, string& output);
	HRESULT IMAP2MAPICharset(const string& input, wstring& output);
	
	// Match a folder path
	bool MatchFolderPath(wstring strFolder, const wstring& strPattern);

	// Various conversion functions
	string PropsToFlags(LPSPropValue lpProps, unsigned int cValues, bool bRecent, bool bRead);

	void HrParseHeaders(const std::string &, std::list<std::pair<std::string, std::string> > &);
	void HrGetSubString(string &strOutput, const std::string &strInput, const std::string &strBegin, const std::string &strEnd);
	void HrTokenize(std::set<std::string> &setTokens, const std::string &strInput);

	HRESULT HrExpungeDeleted(const string &strTag, const string &strCommand, const SRestriction *lpUIDRestriction);

	friend LONG __stdcall IMAPIdleAdviseCallback(void *lpContext, ULONG cNotif, LPNOTIFICATION lpNotif);
};

/** @} */
#endif
