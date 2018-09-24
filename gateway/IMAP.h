/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef IMAP_H
#define IMAP_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <list>
#include <set>
#include <cstring>
#include <kopano/zcdefs.h>
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

#define ROWS_PER_REQUEST_SMALL 200
#define ROWS_PER_REQUEST_BIG 4000
#define IMAP_HIERARCHY_DELIMITER '/'
#define PUBLIC_FOLDERS_NAME L"Public folders"
#define IMAP_RESP_MAX	65536
#define RESP_UNTAGGED "* "
#define RESP_CONTINUE "+ "
#define RESP_TAGGED_OK " OK "
#define RESP_TAGGED_NO " NO "
#define RESP_TAGGED_BAD " BAD "

/**
 * An ownership-indicating wrapper atop SBinary.
 */
class BinaryArray _kc_final : public SBinary {
public:
	BinaryArray() : SBinary() {}
	BinaryArray(const void *lpData, ULONG cbData, bool b = false) :
		SBinary{cbData, nullptr}, bcheap(b)
	{
		if (cbData == 0)
			return;
		if (!bcheap) {
			lpb = new BYTE[cbData];
			memcpy(lpb, lpData, cbData);
		} else {
			lpb = static_cast<BYTE *>(const_cast<void *>(lpData));
		}
	}
	BinaryArray(const BinaryArray &old) :
		SBinary{old.cb, nullptr}, bcheap(false)
	{
		if (cb == 0)
			return;
		lpb = new BYTE[cb];
		memcpy(lpb, old.lpb, cb);
	}
	BinaryArray(BinaryArray &&o) :
		SBinary{o.cb, o.lpb}, bcheap(o.bcheap)
	{
		o.lpb = nullptr;
		o.cb = 0;
		o.bcheap = false;
	}
	BinaryArray(const SBinary &bin) :
		SBinary{bin.cb, nullptr}, bcheap(false)
	{
		if (cb == 0)
			return;
		lpb = new BYTE[bin.cb];
		memcpy(lpb, bin.lpb, bin.cb);
	}
	~BinaryArray(void)
	{
		if (!bcheap)
			delete[] lpb;
	}

	BinaryArray &operator=(const SBinary &b)
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

	BinaryArray &operator=(BinaryArray &&b)
	{
		cb = b.cb;
		bcheap = false;
		if (b.cb == 0) {
			lpb = nullptr;
		} else if (!b.bcheap) {
			lpb = b.lpb;
			b.lpb = nullptr;
			b.cb = 0;
			b.bcheap = false;
		} else {
			lpb = new BYTE[cb];
			memcpy(lpb, b.lpb, cb);
		}
		return *this;
	}

	bool bcheap = false;
};

// FLAGS: \Seen \Answered \Flagged \Deleted \Draft \Recent
class IMAP _kc_final : public ClientProto {
public:
	IMAP(const char *path, std::shared_ptr<KC::ECChannel>, std::shared_ptr<KC::ECConfig>);
	~IMAP();

	// getTimeoutMinutes: 30 min when logged in otherwise 1 min
	int getTimeoutMinutes() const { return lpStore == nullptr ? 1 : 30; }
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
	template<bool check> HRESULT HrCmdNoop(const std::string &tag) { return HrCmdNoop(tag, check); }
	HRESULT HrCmdNoop(const std::string &tag, bool check);
	HRESULT HrCmdLogout(const std::string &tag);
	HRESULT HrCmdStarttls(const std::string &tag);
	HRESULT HrCmdAuthenticate(const std::string &tag, std::string auth_method, const std::string &auth_data);
	HRESULT HrCmdLogin(const std::string &tag, const std::vector<std::string> &args);
	template<bool read_only> HRESULT HrCmdSelect(const std::string &tag, const std::vector<std::string> &args) { return HrCmdSelect(tag, args, read_only); }
	HRESULT HrCmdSelect(const std::string &tag, const std::vector<std::string> &args, bool read_only);
	HRESULT HrCmdCreate(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdDelete(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdRename(const std::string &tag, const std::vector<std::string> &args);
	template<bool subscribe> HRESULT HrCmdSubscribe(const std::string &tag, const std::vector<std::string> &args) { return HrCmdSubscribe(tag, args, subscribe); }
	HRESULT HrCmdSubscribe(const std::string &tag, const std::vector<std::string> &args, bool subscribe);
	template<bool sub_only> HRESULT HrCmdList(const std::string &tag, const std::vector<std::string> &args) { return HrCmdList(tag, args, sub_only); }
	HRESULT HrCmdList(const std::string &tag, const std::vector<std::string> &args, bool sub_only);
	HRESULT get_recent_uidnext(IMAPIFolder *folder, const std::string &tag, ULONG &recent, ULONG &uidnext, const ULONG &messages);
	HRESULT HrCmdStatus(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdAppend(const std::string &tag, const std::string &folder, const std::string &data, std::string flags = {}, const std::string &time = {});
	HRESULT HrCmdClose(const std::string &tag);
	HRESULT HrCmdExpunge(const std::string &tag, const std::vector<std::string> &args);
	HRESULT HrCmdSearch(const std::string &tag, std::vector<std::string> &search_crit, bool uid_mode);
	HRESULT HrCmdFetch(const std::string &tag, const std::vector<std::string> &args, bool uid_mode);
	template <bool uid> HRESULT HrCmdFetch(const std::string &strTag, const std::vector<std::string> &args) { return HrCmdFetch(strTag, args, uid); }
	HRESULT HrCmdStore(const std::string &tag, const std::vector<std::string> &args, bool uid_mode);
	template <bool uid> HRESULT HrCmdStore(const std::string &strTag, const std::vector<std::string> &args) { return HrCmdStore(strTag, args, uid); }
	HRESULT HrCmdCopy(const std::string &tag, const std::vector<std::string> &args, bool uid_mode);
	template <bool uid> HRESULT HrCmdCopy(const std::string &strTag, const std::vector<std::string> &args) { return HrCmdCopy(strTag, args, uid); }
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
		ULONG ulSpecialFolderType;
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

		bool operator<(const SMail &sMail) const noexcept { return ulUid < sMail.ulUid; }
		bool operator<(ULONG uid) const noexcept { return ulUid < uid; }
		operator ULONG() const noexcept { return ulUid; }
		bool operator==(ULONG uid) const noexcept { return ulUid == uid; }
	};

	KC::object_ptr<IMAPISession> lpSession;
	KC::object_ptr<IAddrBook> lpAddrBook;
	KC::memory_ptr<SPropTagArray> m_lpsIMAPTags;

	// current folder name
	KC::object_ptr<IMAPIFolder> current_folder;
	std::pair<std::wstring, bool> current_folder_state;
	std::wstring strCurrentFolder;
	KC::object_ptr<IMAPITable> m_lpTable; /* current contents table */
	std::vector<std::string> m_vTableDataColumns; /* current dataitems that caused the setcolumns on the table */

	// true if folder is opened with examine
	bool bCurrentFolderReadOnly = false;

	// vector of mails in the current folder. The index is used for mail number.
	std::vector<SMail> lstFolderMailEIDs;
	KC::object_ptr<IMsgStore> lpStore, lpPublicStore;

	enum { PR_IPM_FAKEJUNK_ENTRYID = PR_ADDITIONAL_REN_ENTRYIDS };
	// special folder entryids (not able to move/delete inbox and such ...)
	std::map<BinaryArray, ULONG> lstSpecialEntryIDs;

	// Message cache
	std::string m_strCache;
	ULONG m_ulCacheUID = 0;

	/* A command has sent a continuation response, and requires more
	 * data from the client. This is currently only used in the
	 * AUTHENTICATE command, other continuations are already handled
	 * in the main loop. m_bContinue marks this. */
	bool m_bContinue = false;
	std::string m_strContinueTag;

	// Idle mode variables
	bool m_bIdleMode = false;
	KC::object_ptr<IMAPIAdviseSink> m_lpIdleAdviseSink;
	ULONG m_ulIdleAdviseConnection = 0;
	std::string m_strIdleTag;
	KC::object_ptr<IMAPITable> m_lpIdleTable;
	std::mutex m_mIdleLock;
	ULONG m_ulLastUid = 0, m_ulErrors = 0;
	std::wstring m_strwUsername;
	KC::delivery_options dopt;

	HRESULT HrPrintQuotaRoot(const std::string &tag);
	HRESULT HrFindFolder(const std::wstring &folder, bool readonly, IMAPIFolder **, ULONG * = nullptr, ENTRYID ** = nullptr);
	HRESULT HrFindFolderPartial(const std::wstring &folder, IMAPIFolder **, std::wstring *notfound);
	HRESULT HrFindSubFolder(IMAPIFolder *lpFolder, const std::wstring &folder, ULONG *eid_size, LPENTRYID *eid);
	bool IsSpecialFolder(ULONG eid_size, ENTRYID *, ULONG * = nullptr) const;
	bool IsMailFolder(IMAPIFolder *) const;
	HRESULT HrOpenParentFolder(IMAPIFolder *lpFolder, IMAPIFolder **lppFolder);
	HRESULT HrGetFolderList(std::list<SFolder> &);
	int check_mboxname_with_resp(const std::wstring &name, const std::string &tag, const std::string &cmd);

	/* subscribed folders */
	std::vector<BinaryArray> m_vSubscriptions;

	HRESULT HrGetSubscribedList();
	HRESULT HrSetSubscribedList();
	HRESULT ChangeSubscribeList(bool bSubscribe, ULONG eid_size, const ENTRYID *);
	HRESULT HrMakeSpecialsList();
	HRESULT HrRefreshFolderMails(bool bInitialLoad, bool bResetRecent, unsigned int *lpulUnseen, ULONG *lpulUIDValidity = NULL);
	HRESULT HrGetSubTree(std::list<SFolder> &folders, bool public_folders, std::list<SFolder>::const_iterator parent_folder);
	HRESULT HrGetFolderPath(std::list<SFolder>::const_iterator lpFolder, const std::list<SFolder> &lstFolder, std::wstring &path);
	HRESULT HrGetDataItems(std::string msgdata_itemnames, std::vector<std::string> &data_items);

	// fetch calls another fetch depending on the data items requested
	HRESULT HrPropertyFetch(std::list<ULONG> &mails, std::vector<std::string> &data_items);
	HRESULT save_generated_properties(const std::string &text, IMessage *message);
	HRESULT HrPropertyFetchRow(LPSPropValue props, ULONG nprops, std::string &response, ULONG mail_nr, bool bounce_flags, const std::vector<std::string> &data_items);
	HRESULT HrGetMessageFlags(std::string &response, LPMESSAGE msg, bool recent);
	HRESULT HrGetMessagePart(std::string &message_part, std::string &msg, std::string part_name);
	ULONG LastOrNumber(const char *szNr, bool bUID);
	HRESULT HrParseSeqSet(const std::string &seq, std::list<ULONG> &mails);
	HRESULT HrParseSeqUidSet(const std::string &seq, std::list<ULONG> &mails);
	HRESULT HrSeqUidSetToRestriction(const std::string &seq, std::unique_ptr<KC::ECRestriction> &);
	HRESULT HrStore(const std::list<ULONG> &mails, std::string msgdata_itemname, std::string msgdata_itemvalue, bool *do_del);
	HRESULT HrCopy(const std::list<ULONG> &mails, const std::wstring &folder, bool move);
	HRESULT HrSearchNU(const std::vector<std::string> &cond, ULONG startcond, std::list<ULONG> &mailnr);
	HRESULT HrSearch(std::vector<std::string> &&cond, ULONG startcond, std::list<ULONG> &mailnr);
	HRESULT HrGetBodyStructure(bool ext, std::string &body_structure, const std::string &msg);
	HRESULT HrGetEmailAddress(LPSPropValue props, ULONG addr_type, ULONG eid, ULONG name, ULONG email, std::string header_name, std::string *hdrs);

	// IMAP4rev1 date format: 01-Jan-2000 00:00 +0000
	std::string FileTimeToString(const FILETIME &sFiletime);
	bool StringToFileTime(std::string t, FILETIME &sFileTime, bool date_only = false);
	// add 24 hour to the time to be able to check if a time is on a date
	FILETIME AddDay(const FILETIME &sFileTime);
	// Folder names are in a *modified* utf-7 form. See RFC2060, chapter 5.1.3
	HRESULT MAPI2IMAPCharset(const std::wstring &input, std::string &output);
	HRESULT IMAP2MAPICharset(const std::string &input, std::wstring &output);
	// Match a folder path
	bool MatchFolderPath(const std::wstring &folder, const std::wstring &pattern);
	// Various conversion functions
	std::string PropsToFlags(LPSPropValue props, unsigned int nprops, bool recent, bool read);
	void HrParseHeaders(const std::string &, std::list<std::pair<std::string, std::string> > &);
	void HrGetSubString(std::string &output, const std::string &input, const std::string &begin, const std::string &end);
	HRESULT HrExpungeDeleted(const std::string &tag, const std::string &cmd, std::unique_ptr<KC::ECRestriction> &&);
	HRESULT HrGetCurrentFolder(KC::object_ptr<IMAPIFolder> &);
};

/** @} */
#endif
