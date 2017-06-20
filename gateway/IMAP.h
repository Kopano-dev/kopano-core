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
	BinaryArray(KCHL::KEntryId &entry_id) : lpb(reinterpret_cast<BYTE *>(entry_id.lpb())), cb(entry_id.cb()), bcheap(true) {}
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

	bool operator==(const BinaryArray &b) const noexcept
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
	bool operator()(const BinaryArray &a, const BinaryArray &b) const noexcept
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
	void CleanupObject();
	void ReleaseContentsCache();

	std::string GetCapabilityString(bool bAllFlags);
	HRESULT HrSplitInput(const std::string &input, std::vector<std::string> &words);
	HRESULT HrSplitPath(const std::wstring &input, std::vector<std::wstring> &folders);
	HRESULT HrUnsplitPath(const std::vector<std::wstring> &folders, std::wstring &path);

	// All IMAP4rev1 commands
	HRESULT HrCmdCapability(const std::string &tag);
	template<bool> HRESULT HrCmdNoop(const std::string &tag);
	HRESULT HrCmdNoop(const std::string &tag, bool check);
	HRESULT HrCmdLogout(const std::string &tag);
	HRESULT HrCmdStarttls(const std::string &tag);
	HRESULT HrCmdAuthenticate(const std::string &tag, std::string auth_method, const std::string &auth_data);
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
	HRESULT get_uid_next(KCHL::KFolder &status_folder, const std::string &tag, ULONG &uid_next);
	HRESULT get_recent(KCHL::KFolder &folder, const std::string &tag, ULONG &recent, const ULONG &messages);
	HRESULT HrCmdStatus(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdAppend(const std::string &tag, const std::string &folder, const std::string &data, std::string flags = {}, const std::string &time = {});
	HRESULT HrCmdClose(const std::string &tag);
	HRESULT HrCmdExpunge(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdSearch(const std::string &tag, std::vector<std::string> &search_crit, bool uid_mode);
	HRESULT HrCmdFetch(const std::string &tag, const std::vector<std::string> &args, bool uid_mode);
	template <bool uid> HRESULT HrCmdFetch(const std::string &strTag, const std::vector<std::string> &args);
	HRESULT HrCmdStore(const std::string &tag, const std::vector<std::string> &args, bool uid_mode);
	template <bool uid> HRESULT HrCmdStore(const std::string &strTag, const std::vector<std::string> &args);
	HRESULT HrCmdCopy(const std::string &tag, const std::vector<std::string> &args, bool uid_mode);
	template <bool uid> HRESULT HrCmdCopy(const std::string &strTag, const std::vector<std::string> &args);
	HRESULT HrCmdUidXaolMove(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdIdle(const std::string &tag);
	HRESULT HrCmdNamespace(const std::string &tag);
	HRESULT HrCmdGetQuotaRoot(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdGetQuota(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdSetQuota(const std::string &tag, const std::vector<std::string> &args);

	/* Untagged response, * or + */
	void HrResponse(const std::string &untag, const std::string &resp);
	/* Tagged response with result OK, NO or BAD */
	void HrResponse(const std::string &result, const std::string &tag, const std::string &resp);
	static LONG IdleAdviseCallback(void *ctx, ULONG numnotif, LPNOTIFICATION);

	bool bOnlyMailFolders;
	bool bShowPublicFolder;

	// All data per folder for the folderlist
	struct SFolder {
		BinaryArray sEntryID;	// EntryID of folder
		std::wstring strFolderName;
		bool bActive;			// Subscribed folder
		bool bMailFolder;		// E-mail type folder
		bool bSpecialFolder;	// 'special' folder (eg inbox)
		bool bHasSubfolders;	// Has child folders
		std::list<SFolder>::const_iterator lpParentFolder;
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

		bool operator<(const SMail &sMail) const noexcept { return this->ulUid < sMail.ulUid; }
		bool operator<(ULONG ulUid) const noexcept { return this->ulUid < ulUid; }
		operator ULONG() const noexcept { return this->ulUid; }
		bool operator==(ULONG ulUid) const noexcept { return this->ulUid == ulUid; }
	};

	KCHL::object_ptr<IMAPISession> lpSession;
	KCHL::object_ptr<IAddrBook> lpAddrBook;
	KCHL::memory_ptr<SPropTagArray> m_lpsIMAPTags;

	// current folder name
	std::wstring strCurrentFolder;
	KCHL::object_ptr<IMAPITable> m_lpTable; /* current contents table */
	std::vector<std::string> m_vTableDataColumns; /* current dataitems that caused the setcolumns on the table */

	// true if folder is opened with examine
	bool bCurrentFolderReadOnly = false;

	// vector of mails in the current folder. The index is used for mail number.
	std::vector<SMail> lstFolderMailEIDs;
	KCHL::object_ptr<IMsgStore> lpStore, lpPublicStore;

	// special folder entryids (not able to move/delete inbox and such ...)
	std::set<BinaryArray, lessBinaryArray> lstSpecialEntryIDs;

	// Message cache
	std::string m_strCache;
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
	std::string m_strContinueTag;

	// Idle mode variables
	bool m_bIdleMode = false;
	KCHL::object_ptr<IMAPIAdviseSink> m_lpIdleAdviseSink;
	ULONG m_ulIdleAdviseConnection = 0;
	std::string m_strIdleTag;
	KCHL::object_ptr<IMAPITable> m_lpIdleTable;
	std::mutex m_mIdleLock;
	ULONG m_ulLastUid = 0, m_ulErrors = 0;
	std::wstring m_strwUsername;
	delivery_options dopt;

	HRESULT HrPrintQuotaRoot(const std::string &tag);
	HRESULT HrFindFolder(const std::wstring &folder, bool readonly, IMAPIFolder **);
	HRESULT HrFindFolderEntryID(const std::wstring &folder, ULONG *eid_size, LPENTRYID *eid);
	HRESULT HrFindFolderPartial(const std::wstring &folder, IMAPIFolder **, std::wstring *notfound);
	HRESULT HrFindSubFolder(IMAPIFolder *lpFolder, const std::wstring &folder, ULONG *eid_size, LPENTRYID *eid);
	bool IsSpecialFolder(IMAPIFolder *lpFolder);
	bool IsSpecialFolder(ULONG cbEntryID, LPENTRYID lpEntryID);
	bool IsMailFolder(IMAPIFolder *lpFolder);
	bool IsSentItemFolder(IMAPIFolder *lpFolder);
	HRESULT HrOpenParentFolder(ULONG cbEntryID, LPENTRYID lpEntryID, IMAPIFolder **lppFolder);
	HRESULT HrOpenParentFolder(IMAPIFolder *lpFolder, IMAPIFolder **lppFolder);
	HRESULT HrGetFolderList(std::list<SFolder> &);

	/* subscribed folders */
	std::vector<BinaryArray> m_vSubscriptions;
	HRESULT HrGetSubscribedList();
	HRESULT HrSetSubscribedList();
	HRESULT ChangeSubscribeList(bool bSubscribe, ULONG cbEntryID, LPENTRYID lpEntryID);

	HRESULT HrMakeSpecialsList();

	HRESULT HrRefreshFolderMails(bool bInitialLoad, bool bResetRecent, unsigned int *lpulUnseen, ULONG *lpulUIDValidity = NULL);
	HRESULT HrGetSubTree(std::list<SFolder> &folders, bool public_folders, std::list<SFolder>::const_iterator parent_folder);
	HRESULT HrGetFolderPath(std::list<SFolder>::const_iterator lpFolder, const std::list<SFolder> &lstFolder, std::wstring &path);
	HRESULT HrGetDataItems(std::string msgdata_itemnames, std::vector<std::string> &data_items);
	HRESULT HrSemicolonToComma(std::string &data);

	// fetch calls an other fetch depending on the data items requested
	HRESULT HrPropertyFetch(std::list<ULONG> &mails, std::vector<std::string> &data_items);
	HRESULT save_generated_properties(const std::string &text, IMessage *message);
	HRESULT HrPropertyFetchRow(LPSPropValue props, ULONG nprops, std::string &response, ULONG mail_nr, bool bounce_flags, const std::vector<std::string> &data_items);
	std::string HrEnvelopeRecipients(LPSRowSet lpRows, ULONG ulType, std::string& strCharset, bool bIgnore);
	std::string HrEnvelopeSender(LPMESSAGE lpMessage, ULONG ulTagName, ULONG ulTagEmail, std::string& strCharset, bool bIgnore);
	HRESULT HrGetMessageEnvelope(std::string &response, LPMESSAGE msg);
	HRESULT HrGetMessageFlags(std::string &response, LPMESSAGE msg, bool recent);
	HRESULT HrGetMessagePart(std::string &message_part, std::string &msg, std::string part_name);
	ULONG LastOrNumber(const char *szNr, bool bUID);
	HRESULT HrParseSeqSet(const std::string &seq, std::list<ULONG> &mails);
	HRESULT HrParseSeqUidSet(const std::string &seq, std::list<ULONG> &mails);
	HRESULT HrSeqUidSetToRestriction(const std::string &seq, std::unique_ptr<ECRestriction> &);
	HRESULT HrStore(const std::list<ULONG> &mails, std::string msgdata_itemname, std::string msgdata_itemvalue, bool *do_del);
	HRESULT HrCopy(const std::list<ULONG> &mails, const std::string &folder, bool move);
	HRESULT HrSearchNU(const std::vector<std::string> &cond, ULONG startcond, std::list<ULONG> &mailnr);
	HRESULT HrSearch(std::vector<std::string> &&cond, ULONG startcond, std::list<ULONG> &mailnr);
	std::string GetHeaderValue(const std::string &msg, const std::string &hdr, const std::string &dfl);
	HRESULT HrGetBodyStructure(bool ext, std::string &body_structure, const std::string &msg);
	HRESULT HrGetEmailAddress(LPSPropValue props, ULONG addr_type, ULONG eid, ULONG name, ULONG email, std::string header_name, std::string *hdrs);

	// Make the string uppercase
	bool CaseCompare(const std::string &, const std::string &);

	// IMAP4rev1 date format: 01-Jan-2000 00:00 +0000
	std::string FileTimeToString(FILETIME sFiletime);
	FILETIME StringToFileTime(std::string t, bool date_only = false);
	// add 24 hour to the time to be able to check if a time is on a date
	FILETIME AddDay(FILETIME sFileTime);

	// escape (quote) a unicode string to a specific charset in quoted-printable header format
	std::string EscapeString(wchar_t *input, std::string &charset, bool ignore = false);

	// escape (quote) a string for a quoted-text (between "")
	std::string EscapeStringQT(const std::string &);

	// Folder names are in a *modified* utf-7 form. See RFC2060, chapter 5.1.3
	HRESULT MAPI2IMAPCharset(const std::wstring &input, std::string &output);
	HRESULT IMAP2MAPICharset(const std::string &input, std::wstring &output);
	
	// Match a folder path
	bool MatchFolderPath(std::wstring folder, const std::wstring &pattern);

	// Various conversion functions
	std::string PropsToFlags(LPSPropValue props, unsigned int nprops, bool recent, bool read);
	void HrParseHeaders(const std::string &, std::list<std::pair<std::string, std::string> > &);
	void HrGetSubString(std::string &output, const std::string &input, const std::string &begin, const std::string &end);
	void HrTokenize(std::set<std::string> &setTokens, const std::string &strInput);
	HRESULT HrExpungeDeleted(const std::string &tag, const std::string &cmd, std::unique_ptr<ECRestriction> &&);
};

/** @} */
#endif
