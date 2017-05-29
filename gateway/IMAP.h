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

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <list>
#include <set>
#include <kopano/zcdefs.h>
#include <kopano/ECIConv.h>
#include <kopano/ECChannel.h>
#include <kopano/memory.hpp>
#include <kopano/hl.hpp>
#include "ClientProto.h"

using namespace std;
using namespace KCHL;
namespace KC {
class ECRestriction;
}

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

class BinaryArray _kc_final {
public:
	BinaryArray(void) : lpb(NULL), cb(0), bcheap(false) {}
	BinaryArray(KEntryId &entry_id) : lpb(reinterpret_cast<BYTE *>(entry_id.lpb())), cb(entry_id.cb()), bcheap(true) {}
	BinaryArray(BYTE *lpData, ULONG cbData, bool bcheap = false)
	{
		this->bcheap = bcheap;
		if (cbData == 0) {
			cb = 0;
			lpb = NULL;
			return;
		}
		if (!bcheap) {
			lpb = new BYTE[cbData];
			memcpy(lpb, lpData, cbData);
		} else {
			lpb = lpData;
		}
		cb = cbData;
	}
	BinaryArray(const BinaryArray &old) {
		bcheap = false;
		if (old.cb == 0) {
			cb = 0;
			lpb = NULL;
			return;
		}
		cb = old.cb;
		lpb = new BYTE[cb];
		memcpy(lpb, old.lpb, cb);
	}
	BinaryArray(BinaryArray &&o) :
	    lpb(o.lpb), cb(o.cb), bcheap(o.bcheap)
	{
		o.lpb = nullptr;
		o.cb = 0;
		o.bcheap = false;
	}
	BinaryArray(const SBinary &bin) {
		bcheap = false;
		if (bin.cb == 0) {
			cb = 0;
			lpb = NULL;
			return;
		}
		lpb = new BYTE[bin.cb];
		memcpy(lpb, bin.lpb, bin.cb);
		cb = bin.cb;
	}
	BinaryArray(SBinary &&o) :
	    lpb(o.lpb), cb(o.cb), bcheap(false)
	{
		o.lpb = nullptr;
		o.cb = 0;
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
class IMAP _kc_final : public ClientProto {
public:
	IMAP(const char *szServerPath, ECChannel *lpChannel, ECLogger *lpLogger, ECConfig *lpConfig);
	~IMAP();

	int getTimeoutMinutes();
	bool isIdle() const { return m_bIdleMode; }
	bool isContinue() const { return m_bContinue; }

	HRESULT HrSendGreeting(const std::string &strHostString);
	HRESULT HrCloseConnection(const std::string &strQuitMsg);
	HRESULT HrProcessCommand(const std::string &strInput);
	HRESULT HrProcessContinue(const std::string &strInput);
	HRESULT HrDone(bool bSendResponse);

private:
	using string = std::string;
	using wstring = std::wstring;
	template<typename T> using list = std::list<T>;
	template<typename T> using vector = std::vector<T>;
	template<typename... T> using set = std::set<T...>;

	void CleanupObject();
	void ReleaseContentsCache();

	std::string GetCapabilityString(bool bAllFlags);

	HRESULT HrSplitInput(const string &strInput, vector<string> &lstWords);
	HRESULT HrSplitPath(const wstring &strInput, vector<wstring> &lstFolders);
	HRESULT HrUnsplitPath(const vector<wstring> &lstFolders, wstring &strPath);

	// All IMAP4rev1 commands
	HRESULT HrCmdCapability(const string &strTag);
	template<bool> HRESULT HrCmdNoop(const std::string &tag);
	HRESULT HrCmdNoop(const std::string &tag, bool check);
	HRESULT HrCmdLogout(const string &strTag);
	HRESULT HrCmdStarttls(const string &strTag);
	HRESULT HrCmdAuthenticate(const string &strTag, string strAuthMethod, const string &strAuthData);
	HRESULT HrCmdLogin(const std::string &tag, const std::vector<std::string> &args);
	template<bool> HRESULT HrCmdSelect(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdSelect(const std::string &tag, const std::vector<std::string> &args, bool read_only);
	HRESULT HrCmdCreate(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdDelete(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdRename(const std::string &tag, const std::vector<std::string> &args);
	template<bool> HRESULT HrCmdSubscribe(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdSubscribe(const std::string &tag, const std::vector<std::string> &args, bool subscribe);
	template<bool> HRESULT HrCmdList(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdList(const std::string &tag, const std::vector<std::string> &args, bool sub_only);
	HRESULT get_uid_next(KFolder &&status_folder, const std::string &tag, ULONG &uid_next);
	HRESULT get_recent(KFolder &&folder, const std::string &tag, ULONG &recent, const ULONG &messages);
	HRESULT HrCmdStatus(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdAppend(const string &strTag, const string &strFolder, const string &strData, string strFlags=string(), const string &strTime=string());
	HRESULT HrCmdClose(const string &strTag);
	HRESULT HrCmdExpunge(const string &strTag, const std::vector<std::string> &args);
	HRESULT HrCmdSearch(const string &strTag, vector<string> &lstSearchCriteria, bool bUidMode);
	HRESULT HrCmdFetch(const string &strTag, const std::vector<std::string> &args, bool bUidMode);
	template <bool uid> HRESULT HrCmdFetch(const std::string &strTag, const std::vector<std::string> &args);
	HRESULT HrCmdStore(const string &strTag, const std::vector<std::string> &args, bool bUidMode);
	template <bool uid> HRESULT HrCmdStore(const std::string &strTag, const std::vector<std::string> &args);
	HRESULT HrCmdCopy(const string &strTag, const std::vector<std::string> &args, bool bUidMode);
	template <bool uid> HRESULT HrCmdCopy(const std::string &strTag, const std::vector<std::string> &args);
	HRESULT HrCmdUidXaolMove(const string &strTag, const std::vector<std::string> &args);
	HRESULT HrCmdIdle(const string &strTag);
	HRESULT HrCmdNamespace(const string &strTag);
	HRESULT HrCmdGetQuotaRoot(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdGetQuota(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdSetQuota(const std::string &tag, const std::vector<std::string> &args);

	/* Untagged response, * or + */
	void HrResponse(const std::string &untag, const std::string &resp);
	/* Tagged response with result OK, NO or BAD */
	void HrResponse(const std::string &result, const std::string &tag, const std::string &resp);
	static LONG __stdcall IdleAdviseCallback(void *ctx, ULONG numnotif, LPNOTIFICATION);

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

		bool operator <(const SMail &sMail) const
		{
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

	object_ptr<IMAPISession> lpSession;
	object_ptr<IAddrBook> lpAddrBook;
	memory_ptr<SPropTagArray> m_lpsIMAPTags;

	// current folder name
	wstring strCurrentFolder;
	object_ptr<IMAPITable> m_lpTable; /* current contents table */
	vector<string> m_vTableDataColumns; /* current dataitems that caused the setcolumns on the table */

	// true if folder is opened with examine
	bool bCurrentFolderReadOnly = false;

	// vector of mails in the current folder. The index is used for mail number.
	vector<SMail> lstFolderMailEIDs;
	object_ptr<IMsgStore> lpStore, lpPublicStore;

	// special folder entryids (not able to move/delete inbox and such ...)
	set<BinaryArray, lessBinaryArray> lstSpecialEntryIDs;

	// Message cache
	string m_strCache;
	ULONG m_ulCacheUID = 0;

	// Folder cache
	unsigned int cache_folders_time_limit = 0;
	time_t cache_folders_last_used = 0;

	std::list<SFolder> cached_folders;

	/* A command has sent a continuation response, and requires more
	 * data from the client. This is currently only used in the
	 * AUTHENTICATE command, other continuations are already handled
	 * in the main loop. m_bContinue marks this. */
	bool m_bContinue = false;
	string m_strContinueTag;

	// Idle mode variables
	bool m_bIdleMode = false;
	object_ptr<IMAPIAdviseSink> m_lpIdleAdviseSink;
	ULONG m_ulIdleAdviseConnection = 0;
	string m_strIdleTag;
	object_ptr<IMAPITable> m_lpIdleTable;
	std::mutex m_mIdleLock;
	ULONG m_ulLastUid = 0, m_ulErrors = 0;
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

	HRESULT HrRefreshFolderMails(bool bInitialLoad, bool bResetRecent, unsigned int *lpulUnseen, ULONG *lpulUIDValidity = NULL);

	HRESULT HrGetSubTree(list<SFolder> &folders, bool public_folders, list<SFolder>::const_iterator parent_folder);
	HRESULT HrGetFolderPath(list<SFolder>::const_iterator lpFolder, const list<SFolder> &lstFolder, wstring &strPath);
	HRESULT HrGetDataItems(string strMsgDataItemNames, vector<string> &lstDataItems);
	HRESULT HrSemicolonToComma(string &strData);

	// fetch calls an other fetch depending on the data items requested
	HRESULT HrPropertyFetch(list<ULONG> &lstMails, vector<string> &lstDataItems);
	HRESULT save_generated_properties(const std::string &text, IMessage *message);
	HRESULT HrPropertyFetchRow(LPSPropValue lpProps, ULONG cValues, string &strResponse, ULONG ulMailnr, bool bForceFlags, const vector<string> &lstDataItems);

	std::string HrEnvelopeRecipients(LPSRowSet lpRows, ULONG ulType, std::string& strCharset, bool bIgnore);
	std::string HrEnvelopeSender(LPMESSAGE lpMessage, ULONG ulTagName, ULONG ulTagEmail, std::string& strCharset, bool bIgnore);
	HRESULT HrGetMessageEnvelope(string &strResponse, LPMESSAGE lpMessage);
	HRESULT HrGetMessageFlags(string &strResponse, LPMESSAGE lpMessage, bool bRecent);

	HRESULT HrGetMessagePart(string &strMessagePart, string &strMessage, string strPartName);

	ULONG LastOrNumber(const char *szNr, bool bUID);
	HRESULT HrParseSeqSet(const string &strSeqSet, list<ULONG> &lstMails);
	HRESULT HrParseSeqUidSet(const string &strSeqSet, list<ULONG> &lstMails);
	HRESULT HrSeqUidSetToRestriction(const string &strSeqSet, std::unique_ptr<ECRestriction> &);

	HRESULT HrStore(const list<ULONG> &lstMails, string strMsgDataItemName, string strMsgDataItemValue, bool *lpbDoDelete);
	HRESULT HrCopy(const list<ULONG> &lstMails, const string &strFolder, bool bMove);
	HRESULT HrSearchNU(const std::vector<std::string> &cond, ULONG startcond, std::list<ULONG> &mailnr);
	HRESULT HrSearch(std::vector<std::string> &&cond, ULONG startcond, std::list<ULONG> &mailnr);
	string GetHeaderValue(const string &strMessage, const string &strHeader, const string &strDefault);
	HRESULT HrGetBodyStructure(bool bExtended, string &strBodyStructure, const string& strMessage);

	HRESULT HrGetEmailAddress(LPSPropValue lpPropValues, ULONG ulAddrType, ULONG ulEntryID, ULONG ulName, ULONG ulEmail, string strHeaderName, string *strHeaders);

	// Make the string uppercase
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

	HRESULT HrExpungeDeleted(const string &strTag, const string &strCommand, std::unique_ptr<ECRestriction> &&);
};

/** @} */
#endif
