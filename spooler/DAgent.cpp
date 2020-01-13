/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/*
 * E-mail is delivered to the client through this program; it is invoked
 * by the MTA with the username and rfc822 e-mail message, and the delivery
 * agent parses the rfc822 message, setting properties on the MAPI object
 * as it goes along.
 *
 * The delivery agent should be called with sufficient privileges to be
 * able to open other users' inboxes.
 *
 * The actual decoding is done by the inetmapi library.
 */
/*
 * An LMTP reply contains:
 * <SMTP code> <ESMTP code> <message>
 * See RFC 1123 and 2821 for normal codes, 1893 and 2034 for enhanced codes.
 *
 * Enhanced Delivery status codes: Class.Subject.Detail
 *
 * Classes:
 * 2 = Success, 4 = Persistent Transient Failure, 5 = Permanent Failure
 *
 * Subjects:
 * 0 = Other/Unknown, 1 = Addressing, 2 = Mailbox, 3 = Mail system
 * 4 = Network/Routing, 5 = Mail delivery, 6 = Message content, 7 = Security
 *
 * Detail:
 * see rfc.
 */
#include <kopano/platform.h>
#include <atomic>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <climits>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <map>
#include <poll.h>
#include <sys/resource.h>
#include <kopano/ECRestriction.h>
#include <kopano/MAPIErrors.h>
#include <kopano/automapi.hpp>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <kopano/tie.hpp>
#include <kopano/timeutil.hpp>
#include "charset/localeutil.h"
#include <kopano/fileutil.hpp>
#include "PyMapiPlugin.h"
#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pwd.h>

/*
  This is actually from sysexits.h
  but since those windows lamers don't have it ..
  let's copy some defines here..
*/
#define EX_OK		0	/* successful termination */
#define EX__BASE	64	/* base value for error messages */
#define EX_USAGE	64	/* command line usage error */
#define EX_SOFTWARE	70	/* internal software error */
#define EX_TEMPFAIL	75	/* temp failure; user is invited to retry */

#define USES_IID_IMAPIFolder
#define USES_IID_IExchangeManageStore
#define USES_IID_IMsgStore

#include <kopano/ECGuid.h>
#include <inetmapi/inetmapi.h>
#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>
#include <kopano/mapiext.h>
#include <mapiguid.h>
#include <edkguid.h>
#include <edkmdb.h>
#include <kopano/EMSAbTag.h>
#include <cctype>
#include <ctime>
#include <kopano/stringutil.h>
#include <kopano/CommonUtil.h>
#include <kopano/Util.h>
#include <kopano/ECLogger.h>
#include "rules.h"
#include "archive.h"
#include "helpers/MAPIPropHelper.h"
#include <getopt.h>
#include <inetmapi/options.h>
#include <kopano/charset/convert.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/ECTags.h>
#include <kopano/ECFeatures.hpp>
#include <kopano/ECChannel.h>
#include <kopano/UnixUtil.h>
#include "LMTP.h"
#include <kopano/ecversion.h>
#include <csignal>
#include "SSLUtil.h"
#include "StatsClient.h"

using namespace KC;
using namespace std::string_literals;
using std::cerr;
using std::cout;
using std::endl;
using std::min;
using std::string;
using std::wstring;

typedef enum {
	DM_STORE=0,
	DM_JUNK,
	DM_PUBLIC
} delivery_mode;

enum {
	GP_FORK,
	GP_THREAD,
	GP_SINGLE,
};

class DeliveryArgs final {
public:
	DeliveryArgs(void)
	{
		imopt_default_delivery_options(&sDeliveryOpts);
	}
	DeliveryArgs(const DeliveryArgs &o) :
		strPath(o.strPath), strAutorespond(o.strAutorespond),
		bCreateFolder(o.bCreateFolder), strDeliveryFolder(o.strDeliveryFolder),
		szPathSeparator(o.szPathSeparator), ulDeliveryMode(o.ulDeliveryMode),
		sDeliveryOpts(o.sDeliveryOpts), bNewmailNotify(o.bNewmailNotify),
		bResolveAddress(o.bResolveAddress)
	{}

	std::shared_ptr<StatsClient> sc;
	/* Channel for communication from MTA */
	std::unique_ptr<ECChannel> lpChannel;

	/* Connection path to storage server */
	std::string strPath;

	/* Path to autorespond handler */
	std::string strAutorespond;

	/* Options for delivery into special subfolder */
	bool bCreateFolder = false;
	std::wstring strDeliveryFolder;
	wchar_t szPathSeparator = '\\';

	/* Delivery options */
	delivery_mode ulDeliveryMode = DM_STORE;
	delivery_options sDeliveryOpts;

	/* Generate notifications regarding the new email */
	bool bNewmailNotify = true;

	/* Username is email address, resolve it to get username */
	bool bResolveAddress = false;

	/* Indication if we got an error calling external tools */
	bool got_error = false;
};

/**
 * ECRecipient contains an email address from LMTP, or from
 * commandline an email address or username.
 */
class ECRecipient {
public:
	ECRecipient(const std::string &wstrName) :
		wstrRCPT(wstrName), vwstrRecipients{wstrName}
	{
		/* strRCPT must match recipient string from LMTP caller */
	}

	~ECRecipient()
	{
		MAPIFreeBuffer(sEntryId.lpb);
		MAPIFreeBuffer(sSearchKey.lpb);
	}

	void combine(ECRecipient *lpRecip) {
		vwstrRecipients.emplace_back(lpRecip->wstrRCPT);
	}

	// sort recipients on imap data flag, then on username so find() for combine() works correctly.
	bool operator <(const ECRecipient &r) const {
		if (bHasIMAP == r.bHasIMAP)
			return wstrUsername < r.wstrUsername;
		else
			return bHasIMAP && !r.bHasIMAP;
	}

	ULONG ulResolveFlags = MAPI_UNRESOLVED;

	/* Information from LMTP caller */
	std::string wstrRCPT;
	std::vector<std::string> vwstrRecipients;

	/* User properties */
	std::wstring wstrUsername, wstrFullname, wstrCompany, wstrEmail;
	std::wstring wstrServerDisplayName;
	std::string wstrDeliveryStatus, strAddrType, strSMTP;
	unsigned int ulDisplayType = 0, ulAdminLevel = 0;
	SBinary sEntryId{}, sSearchKey{};
	bool bHasIMAP = false;
};

class dagent_stats final : public StatsClient {
	public:
	dagent_stats(std::shared_ptr<ECConfig>);
};

//Global variables
static unsigned int g_process_model = GP_FORK;
static bool g_bQuit = false, g_dump_config;
static bool g_bTempfail = true; // Most errors are tempfails
static pthread_t g_main_thread;
static std::atomic<bool> g_sighup_flag{false};
static std::atomic<unsigned int> g_nLMTPThreads{0};
static std::shared_ptr<ECLogger> g_lpLogger;
extern std::shared_ptr<ECConfig> g_lpConfig;
std::shared_ptr<ECConfig> g_lpConfig;

class sortRecipients {
public:
	bool operator()(const ECRecipient *left, const ECRecipient *right) const
	{
		return *left < *right;
	}
};

typedef std::set<ECRecipient *, sortRecipients> recipients_t;
// we group by server to correctly single-instance the email data on each server
typedef std::map<std::wstring, recipients_t, wcscasecmp_comparison> serverrecipients_t;
// then we group by company to minimize re-opening the addressbook
typedef std::map<std::wstring, serverrecipients_t, wcscasecmp_comparison> companyrecipients_t;

static void da_sigterm_async(int)
{
	g_bQuit = true;
}

static void da_sighup_async(int)
{
	g_sighup_flag = true;
}

static void da_sighup_sync()
{
	bool expect_one = true;
	if (!g_sighup_flag.compare_exchange_strong(expect_one, false))
		return;
	if (g_lpConfig != nullptr && !g_lpConfig->ReloadSettings() &&
	    g_lpLogger != nullptr)
		ec_log_warn("Unable to reload configuration file, continuing with current settings.");
	if (g_lpLogger == nullptr)
		return;
	if (g_lpConfig) {
		const char *ll = g_lpConfig->GetSetting("log_level");
		int new_ll = ll ? atoi(ll) : EC_LOGLEVEL_WARNING;
		g_lpLogger->SetLoglevel(new_ll);
	}
	g_lpLogger->Reset();
	ec_log_warn("Log connection was reset");
}

static void da_sigchld_async(int)
{
	int stat;
	while (waitpid (-1, &stat, WNOHANG) > 0)
		--g_nLMTPThreads;
}

/**
 * Check if the message should be processed with the autoaccepter
 *
 * This function returns TRUE if the message passed is a meeting request or meeting cancellation AND
 * the store being delivered in is marked for auto-accepting of meeting requests.
 *
 * @param lpStore Store that the message will be delivered in
 * @param lpMessage Message that will be delivered
 * @return TRUE if the message needs to be autoresponded
 */
static bool FNeedsAutoAccept(IMsgStore *lpStore, LPMESSAGE lpMessage)
{
	static constexpr const SizedSPropTagArray(2, sptaProps) =
		{2, {PR_RESPONSE_REQUESTED, PR_MESSAGE_CLASS}};
	memory_ptr<SPropValue> lpProps;
	ULONG cValues = 0;
	bool bAutoAccept = false, bDeclineConflict = false, bDeclineRecurring = false;

	auto hr = lpMessage->GetProps(sptaProps, 0, &cValues, &~lpProps);
	if (FAILED(hr)) {
		kc_perrorf("GetProps failed", hr);
		return false; /* hr */
	}
	if (PROP_TYPE(lpProps[1].ulPropTag) == PT_ERROR)
		return false; /* MAPI_E_NOT_FOUND */
	if (wcscasecmp(lpProps[1].Value.lpszW, L"IPM.Schedule.Meeting.Request") != 0 && wcscasecmp(lpProps[1].Value.lpszW, L"IPM.Schedule.Meeting.Canceled") != 0)
		return false; /* MAPI_E_NOT_FOUND */
	if ((PROP_TYPE(lpProps[0].ulPropTag) == PT_ERROR || !lpProps[0].Value.b) &&
	    wcscasecmp(lpProps[1].Value.lpszW, L"IPM.Schedule.Meeting.Request") == 0)
		// PR_RESPONSE_REQUESTED must be true for requests to start the auto accepter
		return false; /* MAPI_E_NOT_FOUND */
	hr = GetAutoAcceptSettings(lpStore, &bAutoAccept, &bDeclineConflict, &bDeclineRecurring);
	if (hr != hrSuccess) {
		kc_perrorf("GetAutoAcceptSettings failed", hr);
		return false; /* hr */
	}
	return bAutoAccept;
}

/**
 * Checks whether the message needs auto-processing
 */
static bool FNeedsAutoProcessing(IMsgStore *lpStore, IMessage *lpMessage)
{
	static constexpr const SizedSPropTagArray(1, sptaProps) = {1, {PR_MESSAGE_CLASS}};
	memory_ptr<SPropValue> lpProps;
	ULONG cValues = 0;

	auto hr = lpMessage->GetProps(sptaProps, 0, &cValues, &~lpProps);
	if (hr != hrSuccess) {
		kc_perrorf("GetProps failed", hr);
		return false; /* hr */
	}
	if(wcsncasecmp(lpProps[0].Value.lpszW, L"IPM.Schedule.Meeting.", wcslen(L"IPM.Schedule.Meeting.")) != 0)
		return false;

	bool autoprocess = true;
	hr = GetAutoAcceptSettings(lpStore, nullptr, nullptr, nullptr, &autoprocess);
	if (hr != hrSuccess) {
		kc_perrorf("GetAutoAcceptSettings failed", hr);
		return false; /* hr */
	}
	return autoprocess;
}

/**
 * Auto-respond to the passed message
 *
 * This function starts the external autoresponder. Since the external autoresponder needs to access a message,
 * we first copy (save) the message into the root folder of the store, then run the script on that message, and remove
 * the message afterwards (if the item was accepted, then it will have been moved to the calendar, which will cause
 * the delete to fail, but that's expected).
 *
 * @param lpRecip Recipient for whom lpMessage is being delivered
 * @param lpStore Store in which lpMessage is being delivered
 * @param lpMessage Message being delivered, should be a meeting request
 *
 * @return result
 */
static HRESULT HrAutoAccept(StatsClient *sc, ECRecipient *lpRecip,
    IMsgStore *lpStore, IMessage *lpMessage)
{
	object_ptr<IMAPIFolder> lpRootFolder;
	object_ptr<IMessage> lpMessageCopy;
	const char *autoresponder = g_lpConfig->GetSetting("mr_autoaccepter");
	std::string strEntryID;
	memory_ptr<SPropValue> lpEntryID;
	ULONG ulType = 0;
	ENTRYLIST sEntryList;

	sc->inc(SCN_DAGENT_AUTOACCEPT);
	// Our autoaccepter is an external script. This means that the message it is working on must be
	// saved so that it can find the message to open. Since we can't save the passed lpMessage (it
	// must be processed by the rules engine first), we make a copy, and let the autoaccept script
	// work on the copy.
	auto hr = lpStore->OpenEntry(0, nullptr, &iid_of(lpRootFolder), MAPI_MODIFY, &ulType, &~lpRootFolder);
	if (hr != hrSuccess)
		return kc_perrorf("OpenEntry failed", hr);
	hr = lpRootFolder->CreateMessage(nullptr, 0, &~lpMessageCopy);
	if (hr != hrSuccess)
		return kc_perrorf("CreateMessage failed", hr);
	hr = lpMessage->CopyTo(0, nullptr, nullptr, 0, nullptr, &IID_IMessage, lpMessageCopy, 0, nullptr);
	if (hr != hrSuccess)
		return kc_perrorf("CopyTo failed", hr);
	hr = lpMessageCopy->SaveChanges(0);
	if (hr != hrSuccess)
		return kc_perrorf("SaveChanges failed", hr);
	hr = HrGetOneProp(lpMessageCopy, PR_ENTRYID, &~lpEntryID);
	if (hr != hrSuccess)
		return kc_perrorf("HrGetOneProp failed", hr);
	strEntryID = bin2hex(lpEntryID->Value.bin);

	// We cannot rely on the 'current locale' to be able to represent the username in wstrUsername. We therefore
	// force UTF-8 output on the username. This means that the autoaccept script must also interpret the username
	// in UTF-8, *not* in the current locale.
	std::vector<std::string> cmdline = {
		autoresponder,
		convert_to<std::string>("UTF-8", lpRecip->wstrUsername, rawsize(lpRecip->wstrUsername), CHARSET_WCHAR),
		g_lpConfig->GetSettingsPath(), strEntryID
	};
	ec_log_debug("Starting autoaccept");
	if (!unix_system(autoresponder, cmdline, const_cast<const char **>(environ))) {
		hr = MAPI_E_CALL_FAILED;
		kc_perrorf("Invoking autoaccept script failed", hr);
	}

	// Delete the copy, irrespective of the outcome of the script.
	sEntryList.cValues = 1;
	sEntryList.lpbin = &lpEntryID->Value.bin;
	lpRootFolder->DeleteMessages(&sEntryList, 0, NULL, 0);
	// ignore error during delete; the autoaccept script may have already (re)moved the message
	return hr;
}

/**
 * Auto-process the passed message
 *
 * @param lpRecip Recipient for whom lpMessage is being delivered
 * @param lpStore Store in which lpMessage is being delivered
 * @param lpMessage Message being delivered, should be a meeting request
 *
 * @return result
 */
static HRESULT HrAutoProcess(StatsClient *sc, ECRecipient *lpRecip,
    IMsgStore *lpStore, IMessage *lpMessage)
{
	object_ptr<IMAPIFolder> lpRootFolder;
	object_ptr<IMessage> lpMessageCopy;
	const char *autoprocessor = g_lpConfig->GetSetting("mr_autoprocessor");
	memory_ptr<SPropValue> lpEntryID;
	ULONG ulType = 0;
	ENTRYLIST sEntryList;

	sc->inc(SCN_DAGENT_AUTOPROCESS);
	// Pass a copy to the external script
	auto hr = lpStore->OpenEntry(0, nullptr, &iid_of(lpRootFolder), MAPI_MODIFY, &ulType, &~lpRootFolder);
	if (hr != hrSuccess)
		return kc_perrorf("OpenEntry failed", hr);
	hr = lpRootFolder->CreateMessage(nullptr, 0, &~lpMessageCopy);
	if (hr != hrSuccess)
		return kc_perrorf("CreateMessage failed", hr);
	hr = lpMessage->CopyTo(0, nullptr, nullptr, 0, nullptr, &IID_IMessage, lpMessageCopy, 0, nullptr);
	if (hr != hrSuccess)
		return kc_perrorf("CopyTo failed", hr);
	hr = lpMessageCopy->SaveChanges(0);
	if (hr != hrSuccess)
		return kc_perrorf("SaveChanges failed", hr);
	hr = HrGetOneProp(lpMessageCopy, PR_ENTRYID, &~lpEntryID);
	if (hr != hrSuccess)
		return kc_perrorf("HrGetOneProp failed", hr);
	auto strEntryID = bin2hex(lpEntryID->Value.bin);

	// We cannot rely on the 'current locale' to be able to represent the username in wstrUsername. We therefore
	// force UTF-8 output on the username. This means that the autoaccept script must also interpret the username
	// in UTF-8, *not* in the current locale.
	std::vector<std::string> cmdline = {
		autoprocessor,
		convert_to<std::string>("UTF-8", lpRecip->wstrUsername, rawsize(lpRecip->wstrUsername), CHARSET_WCHAR),
		g_lpConfig->GetSettingsPath(), strEntryID
	};
	ec_log_debug("Starting autoprocessing");
	if (!unix_system(autoprocessor, cmdline, const_cast<const char **>(environ)))
		hr = MAPI_E_CALL_FAILED;

	// Delete the copy, irrespective of the outcome of the script.
	sEntryList.cValues = 1;
	sEntryList.lpbin = &lpEntryID->Value.bin;
	lpRootFolder->DeleteMessages(&sEntryList, 0, NULL, 0);
	// ignore error during delete; the autoaccept script may have already (re)moved the message
	return hr;
}

static bool kc_recip_in_list(const char *s, const char *recip)
{
	auto l = tokenize(s, ' ', true);
	return std::find(l.cbegin(), l.cend(), std::string(recip)) != l.cend();
}

/**
 * Save copy of the raw message
 *
 * @param[in] fp	File pointer to the email data
 * @param[in] lpRecipient	Pointer to a recipient name
 */
static void SaveRawMessage(FILE *fp, const char *lpRecipient, DeliveryArgs *lpArgs)
{
	if (!g_lpConfig || !g_lpLogger || !fp || !lpRecipient)
		return;

	std::string strFileName = g_lpConfig->GetSetting("log_raw_message_path");
	if (CreatePath(strFileName.c_str()) < 0)
		ec_log_err("Could not mkdir \"%s\": %s\n", strFileName.c_str(), strerror(errno));
	auto rawmsg = g_lpConfig->GetSetting("log_raw_message");
	bool y = parseBool(rawmsg);
	if (!y)
		return;
	y = strcasecmp(rawmsg, "all") == 0 || strcasecmp(rawmsg, "yes") == 0 ||
	    kc_recip_in_list(rawmsg, lpRecipient) ||
	    (strcasecmp(rawmsg, "error") == 0 && lpArgs->got_error);
	if (!y)
		return;

	char szBuff[64];
	tm tmResult;
	gmtime_safe(time(nullptr), &tmResult);
	if (strFileName.empty()) {
		 ec_log_crit("Unable to save raw message. Wrong configuration: field \"log_raw_message_path\" is empty.");
		 return;
	}
	if (strFileName[strFileName.size()-1] != '/')
		strFileName += '/';
	strFileName += lpRecipient;
	sprintf(szBuff, "_%04d%02d%02d%02d%02d%02d_%08x.eml", tmResult.tm_year+1900, tmResult.tm_mon+1, tmResult.tm_mday, tmResult.tm_hour, tmResult.tm_min, tmResult.tm_sec, rand_mt());
	strFileName += szBuff;
	if (DuplicateFile(fp, strFileName))
		ec_log_notice("Raw message saved to \"%s\"", strFileName.c_str());
}

/**
 * Opens the default addressbook container on the given addressbook.
 *
 * @param[in]	lpAdrBook	The IAddrBook interface
 * @param[out]	lppAddrDir	The default addressbook container.
 * @return		MAPI error code
 */
static HRESULT OpenResolveAddrFolder(LPADRBOOK lpAdrBook,
    IABContainer **lppAddrDir)
{
	memory_ptr<ENTRYID> lpEntryId;
	unsigned int cbEntryId = 0, ulObj = 0;

	if (lpAdrBook == nullptr || lppAddrDir == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = lpAdrBook->GetDefaultDir(&cbEntryId, &~lpEntryId);
	if (hr != hrSuccess)
		return kc_perrorf("Unable to find default resolve directory", hr);
	hr = lpAdrBook->OpenEntry(cbEntryId, lpEntryId, &iid_of(*lppAddrDir), 0, &ulObj, reinterpret_cast<IUnknown **>(lppAddrDir));
	if (hr != hrSuccess)
		return kc_perror("Unable to open default resolve directory", hr);
	return hrSuccess;
}

/**
 * Opens the addressbook on the given session, and optionally opens
 * the default addressbook container of the addressbook.
 *
 * @param[in]	lpSession	The IMAPISession interface of the logged in user.
 * @param[out]	lpAdrBook	The Global Addressbook.
 * @param[out]	lppAddrDir	The default addressbook container, may be NULL if not wanted.
 * @return		MAPI error code.
 */
static HRESULT OpenResolveAddrFolder(IMAPISession *lpSession,
    LPADRBOOK *lppAdrBook, IABContainer **lppAddrDir)
{
	if (lpSession == NULL || lppAdrBook == NULL)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = lpSession->OpenAddressBook(0, NULL, 0, lppAdrBook);
	if (hr != hrSuccess)
		return kc_perror("Unable to open addressbook", hr);
	if (lppAddrDir == nullptr)
		return hrSuccess;
	hr = OpenResolveAddrFolder(*lppAdrBook, lppAddrDir);
	if(hr != hrSuccess)
		return kc_perrorf("OpenResolveAddrFolder failed", hr);
	return hrSuccess;
}

/**
 * Resolve usernames/email addresses to Kopano users.
 *
 * @param[in] lpAddrFolder resolve users from this addressbook container
 * @param[in,out] lRCPT the list of recipients to resolve in Kopano
 *
 * @return MAPI Error code
 */
static HRESULT ResolveUsers(IABContainer *lpAddrFolder, recipients_t *lRCPT)
{
	adrlist_ptr lpAdrList;
	memory_ptr<FlagList> lpFlagList;
	static constexpr const SizedSPropTagArray(13, sptaAddress) = {13,
	{ PR_ENTRYID, PR_DISPLAY_NAME_W, PR_ACCOUNT_W, PR_SMTP_ADDRESS_A,
	  PR_ADDRTYPE_A, PR_EMAIL_ADDRESS_W, PR_DISPLAY_TYPE, PR_SEARCH_KEY,
	  PR_EC_COMPANY_NAME_W,	PR_EC_HOMESERVER_NAME_W, PR_EC_ADMINISTRATOR,
	  PR_EC_ENABLED_FEATURES_A, PR_OBJECT_TYPE }
	};
	ULONG ulRCPT = lRCPT->size();

	auto hr = MAPIAllocateBuffer(CbNewADRLIST(ulRCPT), &~lpAdrList);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed(1)", hr);
	lpAdrList->cEntries = 0;
	hr = MAPIAllocateBuffer(CbNewFlagList(ulRCPT), &~lpFlagList);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed(2)", hr);
	lpFlagList->cFlags = ulRCPT;

	ulRCPT = 0;
	for (const auto &recip : *lRCPT) {
		lpAdrList->aEntries[ulRCPT].cValues = 1;

		hr = MAPIAllocateBuffer(sizeof(SPropValue), reinterpret_cast<void **>(&lpAdrList->aEntries[ulRCPT].rgPropVals));
		if (hr != hrSuccess)
			return kc_perrorf("MAPIAllocateBuffer failed(3)", hr);
		++lpAdrList->cEntries;
		/* szName can either be the email address or username, it doesn't really matter */
		lpAdrList->aEntries[ulRCPT].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME_A;
		lpAdrList->aEntries[ulRCPT].rgPropVals[0].Value.lpszA = const_cast<char *>(recip->wstrRCPT.c_str());
		lpFlagList->ulFlag[ulRCPT] = MAPI_UNRESOLVED;
		++ulRCPT;
	}

	// MAPI_UNICODE flag here doesn't have any effect, since we give all proptags ourself
	hr = lpAddrFolder->ResolveNames(sptaAddress,
	     MAPI_UNICODE | EMS_AB_ADDRESS_LOOKUP, lpAdrList, lpFlagList);
	if (hr != hrSuccess)
		return hr;

	ulRCPT = 0;
	for (const auto &recip : *lRCPT) {
		recip->ulResolveFlags = lpFlagList->ulFlag[ulRCPT];

		ULONG temp = lpFlagList->ulFlag[ulRCPT];
		if (temp != MAPI_RESOLVED) {
			ec_log_err("Failed to resolve recipient %s (%x)", recip->wstrRCPT.c_str(), temp);
			continue;
		}
		/* Yay, resolved the address, get it */
		auto lpEntryIdProp  = lpAdrList->aEntries[ulRCPT].cfind(PR_ENTRYID);
		auto lpFullNameProp = lpAdrList->aEntries[ulRCPT].cfind(PR_DISPLAY_NAME_W);
		auto lpAccountProp  = lpAdrList->aEntries[ulRCPT].cfind(PR_ACCOUNT_W);
		auto lpSMTPProp     = lpAdrList->aEntries[ulRCPT].cfind(PR_SMTP_ADDRESS_A);
		auto lpObjectProp   = lpAdrList->aEntries[ulRCPT].cfind(PR_OBJECT_TYPE);
		// the only property that is allowed NULL in this list
		auto lpDisplayProp  = lpAdrList->aEntries[ulRCPT].cfind(PR_DISPLAY_TYPE);
		if(!lpEntryIdProp || !lpFullNameProp || !lpAccountProp || !lpSMTPProp || !lpObjectProp) {
			ec_log_err("Not all properties found for %s", recip->wstrRCPT.c_str());
			continue;
		}

		if (lpObjectProp->Value.ul != MAPI_MAILUSER) {
			ec_log_warn("Resolved recipient %s is not a user", recip->wstrRCPT.c_str());
			continue;
		} else if (lpDisplayProp && lpDisplayProp->Value.ul == DT_REMOTE_MAILUSER) {
			// allowed are DT_MAILUSER, DT_ROOM and DT_EQUIPMENT. all other DT_* defines are no MAPI_MAILUSER
			ec_log_warn("Resolved recipient %s is a contact address, unable to deliver", recip->wstrRCPT.c_str());
			continue;
		}

		ec_log_notice("Resolved recipient %s as user %ls", recip->wstrRCPT.c_str(), lpAccountProp->Value.lpszW);
		/* The following are allowed to be NULL */
		auto lpCompanyProp   = lpAdrList->aEntries[ulRCPT].cfind(PR_EC_COMPANY_NAME_W);
		auto lpServerProp    = lpAdrList->aEntries[ulRCPT].cfind(PR_EC_HOMESERVER_NAME_W);
		auto lpAdminProp     = lpAdrList->aEntries[ulRCPT].cfind(PR_EC_ADMINISTRATOR);
		auto lpAddrTypeProp  = lpAdrList->aEntries[ulRCPT].cfind(PR_ADDRTYPE_A);
		auto lpEmailProp     = lpAdrList->aEntries[ulRCPT].cfind(PR_EMAIL_ADDRESS_W);
		auto lpSearchKeyProp = lpAdrList->aEntries[ulRCPT].cfind(PR_SEARCH_KEY);
		recip->wstrUsername.assign(lpAccountProp->Value.lpszW);
		recip->wstrFullname.assign(lpFullNameProp->Value.lpszW);
		recip->strSMTP.assign(lpSMTPProp->Value.lpszA);
		if (Util::HrCopyBinary(lpEntryIdProp->Value.bin.cb, lpEntryIdProp->Value.bin.lpb, &recip->sEntryId.cb, &recip->sEntryId.lpb) != hrSuccess)
			continue;

		/* Only when multi-company has been enabled will we have the companyname. */
		if (lpCompanyProp)
			recip->wstrCompany.assign(lpCompanyProp->Value.lpszW);
		/* Only when distributed has been enabled will we have the servername. */
		if (lpServerProp)
			recip->wstrServerDisplayName.assign(lpServerProp->Value.lpszW);
		if (lpDisplayProp)
			recip->ulDisplayType = lpDisplayProp->Value.ul;
		if (lpAdminProp)
			recip->ulAdminLevel = lpAdminProp->Value.ul;
		if (lpAddrTypeProp)
			recip->strAddrType.assign(lpAddrTypeProp->Value.lpszA);
		else
			recip->strAddrType.assign("SMTP");
		if (lpEmailProp)
			recip->wstrEmail.assign(lpEmailProp->Value.lpszW);

		if (lpSearchKeyProp) {
			if (Util::HrCopyBinary(lpSearchKeyProp->Value.bin.cb, lpSearchKeyProp->Value.bin.lpb, &recip->sSearchKey.cb, &recip->sSearchKey.lpb) != hrSuccess)
				continue;
		} else {
			std::string key = "SMTP:" + recip->strSMTP;
			key = strToUpper(key);

			recip->sSearchKey.cb = key.size() + 1; // + terminating 0
			if (KAllocCopy(key.c_str(), recip->sSearchKey.cb,
			    reinterpret_cast<void **>(&recip->sSearchKey.lpb)) != hrSuccess)
				++ulRCPT;
		}

		auto lpFeatureList = lpAdrList->aEntries[ulRCPT].cfind(PR_EC_ENABLED_FEATURES_A);
		recip->bHasIMAP = lpFeatureList && hasFeature("imap", lpFeatureList) == hrSuccess;
		++ulRCPT;
	}
	return hrSuccess;
}

/**
 * Resolve a single recipient as Kopano user
 *
 * @param[in] lpAddrFolder resolve users from this addressbook container
 * @param[in,out] lpRecip recipient to resolve in Kopano
 *
 * @return MAPI Error code
 */
static HRESULT ResolveUser(IABContainer *lpAddrFolder, ECRecipient *lpRecip)
{
	recipients_t list = {lpRecip};
	auto hr = ResolveUsers(lpAddrFolder, &list);
	if (hr != hrSuccess)
		return kc_perrorf("ResolveUsers failed", hr);
	else if (lpRecip->ulResolveFlags != MAPI_RESOLVED)
		return MAPI_E_NOT_FOUND;
	return hrSuccess;
}

/**
 * Free a list of recipients
 *
 * @param[in] lpCompanyRecips list to free memory of, and clear.
 *
 * @return MAPI Error code
 */
static HRESULT FreeServerRecipients(companyrecipients_t *lpCompanyRecips)
{
	if (lpCompanyRecips == NULL)
		return MAPI_E_INVALID_PARAMETER;
	for (const auto &cmp : *lpCompanyRecips)
		for (const auto &srv : cmp.second)
			for (const auto &rcpt : srv.second)
				delete rcpt;
	lpCompanyRecips->clear();
	return hrSuccess;
}

/**
 * Add a recipient to a delivery list, grouped by companies and
 * servers. If recipient is added to the container, it will be set to
 * NULL so you can't free it anymore. It will be freed when the
 * container is freed.
 *
 * @param[in,out] lpCompanyRecips container to add recipient in
 * @param[in,out] lppRecipient Recipient to add to the container
 *
 * @return MAPI Error code
 */
static HRESULT AddServerRecipient(companyrecipients_t *lpCompanyRecips,
    ECRecipient **lppRecipient)
{
	ECRecipient *lpRecipient = *lppRecipient;

	if (lpCompanyRecips == NULL)
		return MAPI_E_INVALID_PARAMETER;
	/*
	 * emplace yields pair<iterator,bool>, take first;
	 * iterator deref yields a pair<wstring,mapped_type>, take second.
	 */
	auto &srvmap = lpCompanyRecips->emplace(lpRecipient->wstrCompany, serverrecipients_t()).first->second;
	auto &rcpset = srvmap.emplace(lpRecipient->wstrServerDisplayName, recipients_t()).first->second;
	/* find yields iterator, deref that yields ECRecipient * */
	auto iterRecip = rcpset.find(lpRecipient);
	if (iterRecip == rcpset.cend()) {
		rcpset.emplace(lpRecipient);
		// The recipient is in the list, and no longer belongs to the caller
		*lppRecipient = NULL;
	} else {
		ec_log_info("Combining recipient %s and %ls, delivering only once",
			lpRecipient->wstrRCPT.c_str(), (*iterRecip)->wstrUsername.c_str());
		(*iterRecip)->combine(lpRecipient);
	}
	return hrSuccess;
}

/**
 * Make a map of recipients grouped by url instead of server name
 *
 * @param[in] lpSession MAPI admin session
 * @param[in] lpServerNameRecips recipients grouped by server name
 * @param[in] strDefaultPath default connection url to kopano
 * @param[out] lpServerPathRecips recipients grouped by server url
 *
 * @return MAPI Error code
 */
static HRESULT ResolveServerToPath(IMAPISession *lpSession,
    const serverrecipients_t *lpServerNameRecips,
    const std::string &strDefaultPath, serverrecipients_t *lpServerPathRecips)
{
	object_ptr<IMsgStore> lpAdminStore;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<SPropValue> lpsObject;
	memory_ptr<ECSVRNAMELIST> lpSrvNameList;
	memory_ptr<ECSERVERLIST> lpSrvList;

	if (lpServerNameRecips == nullptr || lpServerPathRecips == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	/* Single server environment, use default path */
	if (lpServerNameRecips->size() == 1 && lpServerNameRecips->begin()->first.empty()) {
		lpServerPathRecips->emplace(convert_to<std::wstring>(strDefaultPath), lpServerNameRecips->begin()->second);
		return hrSuccess;
	}
	auto hr = HrOpenDefaultStore(lpSession, &~lpAdminStore);
	if (hr != hrSuccess)
		// HrLogon() failed .. try again later
		return kc_perror("Unable to open default store for system account", hr);
	hr = GetECObject(lpAdminStore, iid_of(lpServiceAdmin), &~lpServiceAdmin);
	if (hr != hrSuccess)
		return kc_perror("Unable to get internal object", hr);
	hr = MAPIAllocateBuffer(sizeof(ECSVRNAMELIST), &~lpSrvNameList);
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateBuffer failed", hr);
	hr = MAPIAllocateMore(sizeof(wchar_t *) * lpServerNameRecips->size(), lpSrvNameList, reinterpret_cast<void **>(&lpSrvNameList->lpszaServer));
	if (hr != hrSuccess)
		return kc_perrorf("MAPIAllocateMore failed(1)", hr);

	lpSrvNameList->cServers = 0;
	for (const auto &iter : *lpServerNameRecips) {
		if (iter.first.empty())
			// recipient doesn't have a home server.
			// don't try to resolve since that will break the GetServerDetails call
			// and thus fail all recipients, not just this one
			continue;

		hr = MAPIAllocateMore((iter.first.size() + 1) * sizeof(wchar_t),
		     lpSrvNameList, reinterpret_cast<LPVOID *>(&lpSrvNameList->lpszaServer[lpSrvNameList->cServers]));
		if (hr != hrSuccess)
			return kc_perrorf("MAPIAllocateMore failed(2)", hr);
		wcscpy(reinterpret_cast<LPWSTR>(lpSrvNameList->lpszaServer[lpSrvNameList->cServers]), iter.first.c_str());
		++lpSrvNameList->cServers;
	}

	hr = lpServiceAdmin->GetServerDetails(lpSrvNameList, EC_SERVERDETAIL_PREFEREDPATH | MAPI_UNICODE, &~lpSrvList);
	if (hr != hrSuccess)
		return kc_perrorf("GetServerDetails failed", hr);

	for (ULONG i = 0; i < lpSrvList->cServers; ++i) {
		auto iter = lpServerNameRecips->find((LPWSTR)lpSrvList->lpsaServer[i].lpszName);
		if (iter == lpServerNameRecips->cend()) {
			ec_log_err("Server '%s' not found", (char*)lpSrvList->lpsaServer[i].lpszName);
			return MAPI_E_NOT_FOUND;
		}
		ec_log_debug("%zu recipient(s) on server \"%ls\" (URL %ls)",
			iter->second.size(), lpSrvList->lpsaServer[i].lpszName,
			lpSrvList->lpsaServer[i].lpszPreferedPath);
		lpServerPathRecips->emplace(reinterpret_cast<wchar_t *>(lpSrvList->lpsaServer[i].lpszPreferedPath), iter->second);
	}
	return hrSuccess;
}

/**
 * For a given recipient, open its store, inbox and delivery folder.
 *
 * @param[in] lpSession MAPI Admin session
 * @param[in] lpAdminStore Store of the admin
 * @param[in] lpRecip Resolved Kopano recipient to open folders for
 * @param[in] lpArgs Use these delivery options to open correct folders etc.
 * @param[out] lppStore Store of the recipient
 * @param[out] lppInbox Inbox of the recipient
 * @param[out] lppFolder Delivery folder of the recipient
 *
 * @return MAPI Error code
 */
static HRESULT HrGetDeliveryStoreAndFolder(IMAPISession *lpSession,
    IMsgStore *lpAdminStore, ECRecipient *lpRecip, DeliveryArgs *lpArgs,
    LPMDB *lppStore, IMAPIFolder **lppInbox, IMAPIFolder **lppFolder)
{
	object_ptr<IMsgStore> lpUserStore, lpPublicStore, lpDeliveryStore;
	object_ptr<IMAPIFolder> lpInbox, lpSubFolder, lpJunkFolder, lpDeliveryFolder;
	object_ptr<IExchangeManageStore> lpIEMS;
	memory_ptr<SPropValue> lpJunkProp, lpWritePerms;
	unsigned int cbUserStoreEntryId = 0, cbEntryId = 0, ulObjType = 0;
	memory_ptr<ENTRYID> lpUserStoreEntryId, lpEntryId;
	std::wstring strDeliveryFolder = lpArgs->strDeliveryFolder;
	bool bPublicStore = false;

	auto hr = lpAdminStore->QueryInterface(IID_IExchangeManageStore, &~lpIEMS);
	if (hr != hrSuccess)
		return kc_perrorf("QueryInterface failed", hr);
	hr = lpIEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(L""), reinterpret_cast<const TCHAR *>(lpRecip->wstrUsername.c_str()), MAPI_UNICODE | OPENSTORE_HOME_LOGON, &cbUserStoreEntryId, &~lpUserStoreEntryId);
	if (hr != hrSuccess)
		return kc_perrorf("CreateStoreEntry failed", hr);
	hr = lpSession->OpenMsgStore(0, cbUserStoreEntryId, lpUserStoreEntryId, nullptr, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, &~lpUserStore);
	if (hr != hrSuccess)
		return kc_perrorf("OpenMsgStore failed", hr);
	hr = lpUserStore->GetReceiveFolder(reinterpret_cast<const TCHAR *>("IPM"), 0, &cbEntryId, &~lpEntryId, nullptr);
	if (hr != hrSuccess)
		return kc_perror("Unable to resolve incoming folder", hr);

	// Open the inbox
	hr = lpUserStore->OpenEntry(cbEntryId, lpEntryId, &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpInbox);
	if (hr != hrSuccess || ulObjType != MAPI_FOLDER) {
		kc_perror("Unable to open Inbox folder", hr);
		return MAPI_E_NOT_FOUND;
	}

	// set default delivery to inbox, and default entryid for notify
	lpDeliveryFolder.reset(lpInbox);
	lpDeliveryStore.reset(lpUserStore);
	switch (lpArgs->ulDeliveryMode) {
	case DM_STORE:
		ec_log_info("Mail will be delivered in Inbox");
		lpArgs->sc->inc(SCN_DAGENT_DELIVER_INBOX);
		break;
	case DM_JUNK:
		lpArgs->sc->inc(SCN_DAGENT_DELIVER_JUNK);
		hr = HrGetOneProp(lpInbox, PR_ADDITIONAL_REN_ENTRYIDS, &~lpJunkProp);
		if (hr != hrSuccess || lpJunkProp->Value.MVbin.lpbin[4].cb == 0) {
			ec_log_warn("Unable to resolve junk folder, using normal Inbox: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
			break;
		}

		ec_log_info("Mail will be delivered in junkmail folder");
		// Open the Junk folder
		hr = lpUserStore->OpenEntry(lpJunkProp->Value.MVbin.lpbin[4].cb, reinterpret_cast<ENTRYID *>(lpJunkProp->Value.MVbin.lpbin[4].lpb),
		     &IID_IMAPIFolder, MAPI_MODIFY, &ulObjType, &~lpJunkFolder);
		if (hr != hrSuccess || ulObjType != MAPI_FOLDER) {
			ec_log_warn("Unable to open junkmail folder, using normal Inbox: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
			break;
		}
		// set new delivery folder
		lpDeliveryFolder = lpJunkFolder;
		break;
	case DM_PUBLIC:
		lpArgs->sc->inc(SCN_DAGENT_DELIVER_PUBLIC);
		hr = HrOpenECPublicStore(lpSession, &~lpPublicStore);
		if (hr != hrSuccess) {
			kc_perror("Unable to open public store", hr);
			// revert to normal inbox delivery
			strDeliveryFolder.clear();
			ec_log_warn("Mail will be delivered in Inbox");
		} else {
			ec_log_info("Mail will be delivered in Public store subfolder");
			lpDeliveryStore = lpPublicStore;
			bPublicStore = true;
		}
		break;
	};

	if (!strDeliveryFolder.empty() && lpArgs->ulDeliveryMode != DM_JUNK) {
		hr = OpenSubFolder(lpDeliveryStore, strDeliveryFolder.c_str(),
		     lpArgs->szPathSeparator, bPublicStore,
		     lpArgs->bCreateFolder, &~lpSubFolder);
		if (hr != hrSuccess) {
			kc_pwarn("Subfolder not found, using normal Inbox", hr);
			// folder not found, use inbox
			lpDeliveryFolder = lpInbox;
			lpDeliveryStore = lpUserStore;
		} else {
			lpDeliveryFolder = lpSubFolder;
		}
	}

	// check if we may write in the selected folder
	hr = HrGetOneProp(lpDeliveryFolder, PR_ACCESS_LEVEL, &~lpWritePerms);
	if (FAILED(hr))
		return kc_perror("Unable to read folder properties", hr);
	if ((lpWritePerms->Value.ul & MAPI_MODIFY) == 0) {
		ec_log_warn("No write access in folder, using regular inbox");
		lpDeliveryStore = lpUserStore;
		lpDeliveryFolder = lpInbox;
	}
	*lppStore  = lpDeliveryStore.release();
	*lppInbox  = lpInbox.release();
	*lppFolder = lpDeliveryFolder.release();
	return hrSuccess;
}

/**
 * Make the message a fallback message.
 *
 * @param[in,out] lpMessage Message to place fallback data in
 * @param[in] msg original rfc2822 received message
 *
 * @return MAPI Error code
 */
static HRESULT FallbackDelivery(StatsClient *sc, IMessage *lpMessage,
    const std::string &msg)
{
	std::string newbody;
	SPropValue pm[8], pa[4];
	FILETIME		ft;
	object_ptr<IAttach> lpAttach;
	unsigned int ulAttachNum, m = 0, n = 0;
	object_ptr<IStream> lpStream;

	sc->inc(SCN_DAGENT_FALLBACKDELIVERY);
	pm[m].ulPropTag     = PR_SUBJECT_W;
	pm[m++].Value.lpszW = const_cast<wchar_t *>(L"Fallback delivery");
	pm[m].ulPropTag     = PR_MESSAGE_FLAGS;
	pm[m++].Value.ul    = 0;
	pm[m].ulPropTag     = PR_MESSAGE_CLASS_W;
	pm[m++].Value.lpszW = const_cast<wchar_t *>(L"IPM.Note");
	GetSystemTimeAsFileTime(&ft);
	pm[m].ulPropTag  = PR_CLIENT_SUBMIT_TIME;
	pm[m++].Value.ft = ft;
	pm[m].ulPropTag  = PR_MESSAGE_DELIVERY_TIME;
	pm[m++].Value.ft = ft;

	newbody = "An e-mail sent to you could not be delivered correctly.\n\n"
	          "The original message is attached to this e-mail (the one you are reading right now).\n";
	pm[m].ulPropTag     = PR_BODY_A;
	pm[m++].Value.lpszA = const_cast<char *>(newbody.c_str());

	// Add the original message into the errorMessage
	auto hr = lpMessage->CreateAttach(nullptr, 0, &ulAttachNum, &~lpAttach);
	if (hr != hrSuccess)
		return kc_pwarn("Unable to create attachment", hr);
	hr = lpAttach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~lpStream);
	if (hr != hrSuccess)
		return kc_perrorf("lpAttach->OpenProperty failed", hr);
	hr = lpStream->Write(msg.c_str(), msg.size(), NULL);
	if (hr != hrSuccess)
		return kc_perrorf("lpStream->Write failed", hr);
	hr = lpStream->Commit(0);
	if (hr != hrSuccess)
		return kc_perrorf("lpStream->Commit failed", hr);

	// Add attachment properties
	pa[n].ulPropTag     = PR_ATTACH_METHOD;
	pa[n++].Value.ul    = ATTACH_BY_VALUE;
	pa[n].ulPropTag     = PR_ATTACH_LONG_FILENAME_W;
	pa[n++].Value.lpszW = const_cast<wchar_t *>(L"original.eml");
	pa[n].ulPropTag     = PR_ATTACH_FILENAME_W;
	pa[n++].Value.lpszW = const_cast<wchar_t *>(L"original.eml");
	pa[n].ulPropTag     = PR_ATTACH_CONTENT_ID_W;
	pa[n++].Value.lpszW = const_cast<wchar_t *>(L"dagent-001@localhost");

	// Add attachment properties
	hr = lpAttach->SetProps(n, pa, nullptr);
	if (hr != hrSuccess)
		return kc_perrorf("SetProps failed(1)", hr);
	hr = lpAttach->SaveChanges(0);
	if (hr != hrSuccess)
		return kc_perrorf("SaveChanges failed", hr);

	// Add message properties
	hr = lpMessage->SetProps(m, pm, nullptr);
	if (hr != hrSuccess)
		return kc_perrorf("SetProps failed(2)", hr);
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		return kc_perrorf("lpMessage->SaveChanges failed", hr);
	return hrSuccess;
}

/**
 * Write into the given fd, and if that fails log an error.
 *
 * @param[in] fd file descriptor to write to
 * @param[in] buffer buffer to write
 * @param[in] len length of buffer to write
 * @param[in] wrap optional wrapping, inserts a \r\n at the point of the wrapping point
 *
 * @return MAPI Error code
 */
static HRESULT WriteOrLogError(int fd, const char *buffer, size_t len,
    size_t wrap = 0)
{
	if (!wrap)
		wrap = len;

	while (len > 0) {
		ssize_t n = min(len, wrap);

		if (write_retry(fd, buffer, n) != n) {
			ec_log_err("Write error to temp file for out of office mail: %s", strerror(errno));
			return MAPI_E_CALL_FAILED;
		}
		buffer += n;
		len -= n;
		if (len > 0 && write_retry(fd, "\r\n", 2) != 2) {
			ec_log_err("Write error to temp file for out of office mail: %s", strerror(errno));
			return MAPI_E_CALL_FAILED;
		}
	}
	return hrSuccess;
}

static bool dagent_oof_active(const SPropValue *prop)
{
	bool a = prop[0].ulPropTag == PR_EC_OUTOFOFFICE && prop[0].Value.b;
	if (!a)
		return false;
	time_t now = time(nullptr);
	if (prop[3].ulPropTag == PR_EC_OUTOFOFFICE_FROM)
		a &= FileTimeToUnixTime(prop[3].Value.ft) <= now;
	if (prop[4].ulPropTag == PR_EC_OUTOFOFFICE_UNTIL)
		a &= now <= FileTimeToUnixTime(prop[4].Value.ft);
	return a;
}

/**
 * Create an out-of-office mail, and start the script to trigger its
 * optional sending.
 *
 * @param[in] lpAdrBook Addressbook for email address rewrites
 * @param[in] lpMDB Store of the user that triggered the oof email
 * @param[in] lpMessage delivery message that triggered the oof email
 * @param[in] lpRecip delivery recipient sending the oof email from
 * @param[in] strBaseCommand Command to use to start the oof mailer (kopano-autorespond)
 *
 * @return MAPI Error code
 */
static HRESULT SendOutOfOffice(StatsClient *sc, IAddrBook *lpAdrBook,
    IMsgStore *lpMDB, IMessage *lpMessage, ECRecipient *lpRecip,
    const std::string &strBaseCommand)
{
	static constexpr const SizedSPropTagArray(5, sptaStoreProps) = {5, {
		PR_EC_OUTOFOFFICE, PR_EC_OUTOFOFFICE_MSG_W,
		PR_EC_OUTOFOFFICE_SUBJECT_W,
		PR_EC_OUTOFOFFICE_FROM, PR_EC_OUTOFOFFICE_UNTIL,
	}};
	static constexpr const SizedSPropTagArray(5, sptaMessageProps) = {5, {
		PR_TRANSPORT_MESSAGE_HEADERS_A, PR_MESSAGE_TO_ME,
		PR_MESSAGE_CC_ME, PR_SUBJECT_W, PR_EC_MESSAGE_BCC_ME,
	}};
	memory_ptr<SPropValue> lpStoreProps, lpMessageProps;
	ULONG cValues;

	const wchar_t *szSubject = L"Out of office";
	char szHeader[PATH_MAX]{}, szTemp[PATH_MAX]{};
	wchar_t szwHeader[PATH_MAX]{};
	int fd = -1;
	wstring	strFromName, strFromType, strFromEmail, strBody;
	std::vector<std::string> cmdline = {strBaseCommand};
	// Environment
	size_t s = 0;
	std::string strTmpFile, strTmpFileEnv;

	sc->inc(SCN_DAGENT_OUTOFOFFICE);
	// @fixme need to stream PR_TRANSPORT_MESSAGE_HEADERS_A and PR_EC_OUTOFOFFICE_MSG_W if they're > 8Kb
	auto hr = lpMDB->GetProps(sptaStoreProps, 0, &cValues, &~lpStoreProps);
	if (FAILED(hr))
		return kc_perrorf("GetProps failed(1)", hr);
	hr = hrSuccess;

	// Check for autoresponder
	if (!dagent_oof_active(lpStoreProps)) {
		ec_log_debug("Target user has OOF inactive");
		return hrSuccess;
	}

	ec_log_debug("Target user has OOF active");
	// Check for presence of PR_EC_OUTOFOFFICE_MSG_W
	if (lpStoreProps[1].ulPropTag == PR_EC_OUTOFOFFICE_MSG_W) {
		strBody = lpStoreProps[1].Value.lpszW;
	} else {
		StreamPtr ptrStream;
		hr = lpMDB->OpenProperty(PR_EC_OUTOFOFFICE_MSG_W, &IID_IStream, 0, 0, &~ptrStream);
		if (hr == MAPI_E_NOT_FOUND) {
			/* no message is ok */
		} else if (hr != hrSuccess || (hr = Util::HrStreamToString(ptrStream, strBody)) != hrSuccess) {
			kc_perror("Unable to download out of office message", hr);
			return MAPI_E_FAILURE;
		}
	}

	// Possibly override default subject
	if (lpStoreProps[2].ulPropTag == PR_EC_OUTOFOFFICE_SUBJECT_W)
		szSubject = lpStoreProps[2].Value.lpszW;
	hr = lpMessage->GetProps(sptaMessageProps, 0, &cValues, &~lpMessageProps);
	if (FAILED(hr))
		return kc_perror("GetProps failed(2)", hr);
	hr = spv_postload_large_props(lpMessage, sptaMessageProps, cValues, lpMessageProps);
	if (FAILED(hr))
		kc_pwarn("SendOutOfOffice: spv_postload", hr);
	hr = hrSuccess;

	// See if we're looping
	if (lpMessageProps[0].ulPropTag == PR_TRANSPORT_MESSAGE_HEADERS_A) {
		if (dagent_avoid_autoreply(tokenize(lpMessageProps[0].Value.lpszA, "\n"))) {
			ec_log_debug("Avoiding OOF reply to an automated message.");
			return erSuccess;
		}
		// save headers to a file so they can also be tested from the script we're running
		snprintf(szTemp, PATH_MAX, "%s/autorespond-headers.XXXXXX", TmpPath::instance.getTempPath().c_str());
		fd = mkstemp(szTemp);
		if (fd >= 0) {
			hr = WriteOrLogError(fd, lpMessageProps[0].Value.lpszA, strlen(lpMessageProps[0].Value.lpszA));
			if (hr == hrSuccess)
				strTmpFile = szTemp; // pass to script
			else
				unlink(szTemp);	// ignore headers, but still try oof script
			close(fd);
			fd = -1;
		}
	}

	auto laters = make_scope_success([&]() {
		if (fd != -1)
			close(fd);
		if (szTemp[0] != 0)
			unlink(szTemp);
		if (!strTmpFile.empty())
			unlink(strTmpFile.c_str());
	});

	hr = HrGetAddress(lpAdrBook, lpMessage, PR_SENDER_ENTRYID, PR_SENDER_NAME, PR_SENDER_ADDRTYPE, PR_SENDER_EMAIL_ADDRESS, strFromName, strFromType, strFromEmail);
	if (hr != hrSuccess)
		return kc_perror("Unable to get sender e-mail address for autoresponder", hr);
	snprintf(szTemp, PATH_MAX, "%s/autorespond.XXXXXX", TmpPath::instance.getTempPath().c_str());
	fd = mkstemp(szTemp);
	if (fd < 0) {
		ec_log_warn("Unable to create temp file for out of office mail: %s", strerror(errno));
		return MAPI_E_FAILURE;
	}

	// \n is on the beginning of the next header line because of snprintf and the requirement of the \n
	// PATH_MAX should never be reached though.
	auto quoted = ToQuotedBase64Header(lpRecip->wstrFullname);
	snprintf(szHeader, PATH_MAX, "From: %s <%s>", quoted.c_str(), lpRecip->strSMTP.c_str());
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(1)", hr);
	snprintf(szHeader, PATH_MAX, "\nTo: %ls", strFromEmail.c_str());
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(2)", hr);

	// add anti-loop header for Kopano
	snprintf(szHeader, PATH_MAX, "\nX-Kopano-Vacation: autorespond");
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(3)", hr);
	/*
	 * Add anti-loop header for Exchange, see
	 * http://msdn.microsoft.com/en-us/library/ee219609(v=exchg.80).aspx
	 */
	snprintf(szHeader, PATH_MAX, "\nX-Auto-Response-Suppress: All");
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(4)", hr);
	/*
	 * Add anti-loop header for vacation(1) compatible implementations,
	 * see book "Sendmail" (ISBN 0596555342), section 10.9.
	 * RFC 3834 §3.1.8.
	 */
	snprintf(szHeader, PATH_MAX, "\nPrecedence: bulk");
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(5)", hr);

	if (lpMessageProps[3].ulPropTag == PR_SUBJECT_W)
		// convert as one string because of [] characters
		swprintf(szwHeader, PATH_MAX, L"%ls [%ls]", szSubject, lpMessageProps[3].Value.lpszW);
	else
		swprintf(szwHeader, PATH_MAX, L"%ls", szSubject);
	quoted = ToQuotedBase64Header(szwHeader);
	snprintf(szHeader, PATH_MAX, "\nSubject: %s", quoted.c_str());
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(4)", hr);

	auto timelocale = newlocale(LC_TIME_MASK, "C", nullptr);
	time_t now = time(NULL);
	tm local;
	localtime_r(&now, &local);
	strftime_l(szHeader, PATH_MAX, "\nDate: %a, %d %b %Y %T %z", &local, timelocale);
	freelocale(timelocale);
	if (WriteOrLogError(fd, szHeader, strlen(szHeader)) != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(5)", hr);

	snprintf(szHeader, PATH_MAX, "\nContent-Type: text/plain; charset=utf-8; format=flowed");
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(6)", hr);

	snprintf(szHeader, PATH_MAX, "\nContent-Transfer-Encoding: base64");
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(7)", hr);

	snprintf(szHeader, PATH_MAX, "\nMIME-Version: 1.0"); // add mime-version header, so some clients show high-characters correctly
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(8)", hr);

	snprintf(szHeader, PATH_MAX, "\n\n"); // last header line has double \n
	hr = WriteOrLogError(fd, szHeader, strlen(szHeader));
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(9)", hr);

	// write body
	auto unquoted = convert_to<std::string>("UTF-8", strBody, rawsize(strBody), CHARSET_WCHAR);
	quoted = base64_encode(unquoted.c_str(), unquoted.length());
	hr = WriteOrLogError(fd, quoted.c_str(), quoted.length(), 76);
	if (hr != hrSuccess)
		return kc_perrorf("WriteOrLogError failed(10)", hr);
	close(fd);
	fd = -1;

	// Args: From, To, Subject, Username, Msg_Filename
	// Should run in UTF-8 to get correct strings in UTF-8 from shell_escape(wstring)
	cmdline.emplace_back(lpRecip->strSMTP);
	cmdline.emplace_back(convert_to<std::string>(strFromEmail));
	cmdline.emplace_back(convert_to<std::string>(szSubject));
	cmdline.emplace_back(convert_to<std::string>(lpRecip->wstrUsername));
	cmdline.emplace_back(szTemp);

	// Set MESSAGE_TO_ME and MESSAGE_CC_ME in environment
	auto strToMe = "MESSAGE_TO_ME="s + (lpMessageProps[1].ulPropTag == PR_MESSAGE_TO_ME && lpMessageProps[1].Value.b ? "1" : "0");
	auto strCcMe = "MESSAGE_CC_ME="s + (lpMessageProps[2].ulPropTag == PR_MESSAGE_CC_ME && lpMessageProps[2].Value.b ? "1" : "0");
	auto strBccMe = "MESSAGE_BCC_ME="s + (lpMessageProps[4].ulPropTag == PR_EC_MESSAGE_BCC_ME && lpMessageProps[4].Value.b ? "1" : "0");
	while (environ[s] != nullptr)
		s++;

	auto env = make_unique_nt<const char *[]>(s + 5);
	if (env == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	for (size_t i = 0; i < s && environ[i] != nullptr; ++i)
		env[i] = environ[i];
	env[s] = strToMe.c_str();
	env[s+1] = strCcMe.c_str();
	strTmpFileEnv = "MAILHEADERS=" + strTmpFile;
	env[s+2] = strTmpFileEnv.c_str();
	env[s+3] = strBccMe.c_str();
	env[s+4] = NULL;

	ec_log_info("Starting autoresponder for out-of-office message");
	if (!unix_system(strBaseCommand.c_str(), cmdline, env.get()))
		ec_log_err("Autoresponder failed");
	return hr;
}

/**
 * Create an empty message for delivery
 *
 * @param[in] lpFolder Create the message in this folder
 * @param[in] lpFallbackFolder If write access forbids the creation, fallback to this folder
 * @param[out] lppDeliveryFolder The folder where the message was created
 * @param[out] lppMessage The newly created message
 *
 * @return MAPI Error code
 */
static HRESULT HrCreateMessage(IMAPIFolder *lpFolder,
    IMAPIFolder *lpFallbackFolder, IMAPIFolder **lppDeliveryFolder,
    IMessage **lppMessage)
{
	object_ptr<IMessage> lpMessage;

	auto hr = lpFolder->CreateMessage(nullptr, 0, &~lpMessage);
	if (hr != hrSuccess && lpFallbackFolder) {
		kc_pwarn("Unable to create new message in subfolder, using regular inbox", hr);
		lpFolder = lpFallbackFolder;
		hr = lpFolder->CreateMessage(nullptr, 0, &~lpMessage);
	}
	if (hr != hrSuccess)
		return kc_perror("Unable to create new message", hr);
	hr = lpMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppMessage));
	if (hr != hrSuccess)
		return kc_perrorf("QueryInterface:message failed", hr);
	hr = lpFolder->QueryInterface(IID_IMAPIFolder, reinterpret_cast<void **>(lppDeliveryFolder));
	if (hr != hrSuccess)
		return kc_perrorf("QueryInterface:folder failed", hr);
	return hrSuccess;
}

/**
 * Convert the received rfc2822 email into a MAPI message
 *
 * @param[in] strMail the received email
 * @param[in] lpSession a MAPI Session
 * @param[in] lpMsgStore The store of the delivery
 * @param[in] lpAdrBook The Global Addressbook
 * @param[in] lpDeliveryFolder Folder to create a new message in when the conversion fails
 * @param[in] lpMessage The message to write the conversion data in
 * @param[in] lpArgs delivery options
 * @param[out] lppMessage The delivered message
 * @param[out] lpbFallbackDelivery indicating if the message is a fallback message or not
 *
 * @return MAPI Error code
 */
static HRESULT HrStringToMAPIMessage(const string &strMail,
    IMAPISession *lpSession, IMsgStore *lpMsgStore, LPADRBOOK lpAdrBook,
    IMAPIFolder *lpDeliveryFolder, IMessage *lpMessage, ECRecipient *lpRecip,
    DeliveryArgs *lpArgs, IMessage **lppMessage, bool *lpbFallbackDelivery)
{
	object_ptr<IMessage> lpFallbackMessage;
	bool bFallback = false;

	lpArgs->sDeliveryOpts.add_imap_data = lpRecip->bHasIMAP;

	// Set the properties on the object
	auto hr = IMToMAPI(lpSession, lpMsgStore, lpAdrBook, lpMessage, strMail, lpArgs->sDeliveryOpts);
	if (hr != hrSuccess) {
		kc_pwarn("E-mail parsing failed; starting fallback delivery.", hr);

		// create new message
		hr = lpDeliveryFolder->CreateMessage(nullptr, 0, &~lpFallbackMessage);
		if (hr != hrSuccess) {
			kc_perror("Unable to create fallback message", hr);
			goto exit;
		}
		hr = FallbackDelivery(lpArgs->sc.get(), lpFallbackMessage, strMail);
		if (hr != hrSuccess) {
			kc_perror("Unable to deliver fallback message", hr);
			goto exit;
		}
		// override original message with fallback version to return
		lpMessage = lpFallbackMessage;
		bFallback = true;
	}

	// return the filled (real or fallback) message
	hr = lpMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppMessage));
	if (hr != hrSuccess) {
		kc_perrorf("QueryInterface failed", hr);
		goto exit;
	}
	*lpbFallbackDelivery = bFallback;
exit:
	lpArgs->sc->inc(SCN_DAGENT_STRINGTOMAPI);
	// count attachments
	object_ptr<IMAPITable> lppAttTable;
	if (lpMessage->GetAttachmentTable(0, &~lppAttTable) == hrSuccess &&
	    lppAttTable != nullptr) {
		ULONG countAtt = 0;
		if (lppAttTable->GetRowCount(0, &countAtt) == hrSuccess &&
		    countAtt > 0) {
			lpArgs->sc->inc(SCN_DAGENT_NWITHATTACHMENT);
			lpArgs->sc->inc(SCN_DAGENT_ATTACHMENT_COUNT, static_cast<int64_t>(countAtt));
		}
	}

	// count recipients
	object_ptr<IMAPITable> lppRecipTable;
	if (lpMessage->GetRecipientTable(0, &~lppRecipTable) == hrSuccess &&
	    lppRecipTable != nullptr) {
		ULONG countRecip = 0;
		if (lppRecipTable->GetRowCount(0, &countRecip) == hrSuccess)
			lpArgs->sc->inc(SCN_DAGENT_RECIPS, static_cast<int64_t>(countRecip));
	}
	return hr;
}

/**
 * Check if the message was expired (delivery limit, header: Expiry-Time)
 *
 * @param[in] lpMessage message for delivery
 * @param[out] bExpired message is expired or not
 *
 * @return always hrSuccess
 */
static HRESULT HrMessageExpired(StatsClient *sc, IMessage *lpMessage, bool *bExpired)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpsExpiryTime;
	auto laters = make_scope_success([&]() { sc->inc(*bExpired ? SCN_DAGENT_MSG_EXPIRED : SCN_DAGENT_MSG_NOT_EXPIRED); });
	/*
	 * If the message has an expiry date, and it is past that time,
	 * skip delivering the email.
	 */
	if (HrGetOneProp(lpMessage, PR_EXPIRY_TIME, &~lpsExpiryTime) == hrSuccess &&
	    time(nullptr) > FileTimeToUnixTime(lpsExpiryTime->Value.ft)) {
		// exit with no errors
		hr = hrSuccess;
		*bExpired = true;
		ec_log_warn("Message was expired, not delivering");
		// TODO: if a read-receipt was requested, we need to send a non-read read-receipt
		return hr;
	}
	*bExpired = false;
	return hr;
}

static HRESULT recip_in_distlist(IAddrBook *ab, const SBinary &eid,
    ECRecipient *rcpt, bool &pres)
{
	static constexpr const SizedSPropTagArray(2, cols) = {2, {PR_OBJECT_TYPE, PR_ENTRYID}};
	unsigned int objtype = 0;
	object_ptr<IDistList> dl;
	auto ret = ab->OpenEntry(eid.cb, reinterpret_cast<ENTRYID *>(eid.lpb), &iid_of(dl), 0, &objtype, &~dl);
	if (ret != hrSuccess)
		return ret;
	object_ptr<IMAPITable> tbl;
	ret = dl->GetContentsTable(0, &~tbl);
	if (ret != hrSuccess)
		return ret;
	ret = tbl->SetColumns(cols, 0);
	if (ret != hrSuccess)
		return kc_perrorf("SetColumns", ret);
	SPropValue scmp[4];
	scmp[0].ulPropTag   = PR_OBJECT_TYPE;
	scmp[0].Value.ul    = MAPI_DISTLIST;
	scmp[1].ulPropTag   = PR_OBJECT_TYPE;
	scmp[1].Value.ul    = MAPI_MAILUSER;
	scmp[2].ulPropTag   = PR_ADDRTYPE_A;
	scmp[2].Value.lpszA = const_cast<char *>("ZARAFA");
	scmp[3].ulPropTag   = PR_SMTP_ADDRESS_A;
	scmp[3].Value.lpszA = const_cast<char *>(rcpt->strSMTP.c_str());
	ret = ECOrRestriction(
		ECPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, &scmp[0], ECRestriction::Cheap) +
		ECAndRestriction(
			ECPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, &scmp[1], ECRestriction::Cheap) +
			ECPropertyRestriction(RELOP_EQ, PR_ADDRTYPE_A, &scmp[2], ECRestriction::Cheap) +
			ECPropertyRestriction(RELOP_EQ, PR_SMTP_ADDRESS_A, &scmp[3], ECRestriction::Cheap)
		)
	).RestrictTable(tbl, ECRestriction::Cheap);
	if (ret != hrSuccess)
		return kc_perrorf("RestrictTable", ret);
	rowset_ptr rset;
	ret = tbl->QueryRows(INT_MAX, 0, &~rset);
	if (ret != hrSuccess)
		return kc_perrorf("QueryRows", ret);

	for (unsigned int i = 0; i < rset->cRows; ++i) {
		const auto &row = rset->aRow[i];
		if (row.lpProps[0].Value.ul == MAPI_MAILUSER) {
			pres = true;
			return hrSuccess;
		}
		ret = recip_in_distlist(ab, row.lpProps[1].Value.bin, rcpt, pres);
		if (ret != hrSuccess)
			return ret;
	}
	return hrSuccess;
}

static HRESULT recip_me_check(IAddrBook *ab, IMAPITable *tbl,
    ECRecipient *rcpt, bool &xto, bool &xcc, bool &xdl)
{
	static constexpr const SizedSPropTagArray(3, cols) =
		// MAPI_TO, MAPI_MAILUSER, ...
		// MAPI_CC, MAPI_DISTLIST, ...
		{3, {PR_RECIPIENT_TYPE, PR_OBJECT_TYPE, PR_ENTRYID}};
	auto ret = tbl->SetColumns(cols, 0);
	if (ret != hrSuccess)
		return kc_perrorf("SetColumns failed", ret);

	SPropValue scmp[4];
	scmp[0].ulPropTag   = PR_OBJECT_TYPE;
	scmp[0].Value.ul    = MAPI_DISTLIST;
	scmp[1].ulPropTag   = PR_OBJECT_TYPE;
	scmp[1].Value.ul    = MAPI_MAILUSER;
	scmp[2].ulPropTag   = PR_ADDRTYPE_A;
	scmp[2].Value.lpszA = const_cast<char *>("ZARAFA");
	scmp[3].ulPropTag   = PR_SMTP_ADDRESS_A;
	scmp[3].Value.lpszA = const_cast<char *>(rcpt->strSMTP.c_str());
	ret = ECOrRestriction(
		ECPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, &scmp[0], ECRestriction::Cheap) +
		ECAndRestriction(
			ECPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, &scmp[1], ECRestriction::Cheap) +
			ECPropertyRestriction(RELOP_EQ, PR_ADDRTYPE_A, &scmp[2], ECRestriction::Cheap) +
			ECPropertyRestriction(RELOP_EQ, PR_SMTP_ADDRESS_A, &scmp[3], ECRestriction::Cheap)
		)
	).RestrictTable(tbl, ECRestriction::Cheap);
	if (ret != hrSuccess)
		return kc_perrorf("RestrictTable", ret);
	rowset_ptr rset;
	ret = tbl->QueryRows(INT_MAX, 0, &~rset);
	if (ret != hrSuccess)
		return kc_perrorf("QueryRows", ret);

	for (unsigned int i = 0; i < rset->cRows; ++i) {
		const auto &row = rset->aRow[i];
		if (row.lpProps[1].Value.ul == MAPI_DISTLIST) {
			ret = recip_in_distlist(ab, row.lpProps[2].Value.bin, rcpt, xdl);
			if (ret != hrSuccess)
				return ret;
			continue;
		}
		if (row.lpProps[0].Value.ul == MAPI_TO)
			xto = true;
		if (row.lpProps[0].Value.ul == MAPI_CC)
			xcc = true;
	}
	return hrSuccess;
}

/**
 * Replace To recipient data in message with new recipient
 *
 * @param[in] lpMessage delivery message to set new recipient data in
 * @param[in] lpRecip new recipient to deliver same message for
 *
 * @return MAPI Error code
 */
static HRESULT HrOverrideRecipProps(IAddrBook *ab,
    IMessage *lpMessage, ECRecipient *lpRecip)
{
	object_ptr<IMAPITable> lpRecipTable;
	SPropValue sPropRecip[4];
	bool bToMe = false, bCcMe = false, distgrp = false;

	auto hr = lpMessage->GetRecipientTable (0, &~lpRecipTable);
	if (hr != hrSuccess)
		return kc_perrorf("GetRecipientTable failed", hr);
	hr = recip_me_check(ab, lpRecipTable, lpRecip, bToMe, bCcMe, distgrp);
	if (hr != hrSuccess)
		/* ignore */;
	sPropRecip[1].ulPropTag = PR_MESSAGE_TO_ME;
	sPropRecip[1].Value.b = bToMe;
	sPropRecip[2].ulPropTag = PR_MESSAGE_CC_ME;
	sPropRecip[2].Value.b = bCcMe;
	sPropRecip[3].ulPropTag = PR_EC_MESSAGE_BCC_ME;
	sPropRecip[3].Value.b = !bToMe && !bCcMe && !distgrp;
	sPropRecip[0].ulPropTag = PR_MESSAGE_RECIP_ME;
	sPropRecip[0].Value.b = bToMe || bCcMe || sPropRecip[3].Value.b;
	hr = lpMessage->SetProps(4, sPropRecip, NULL);
	if (hr != hrSuccess)
		return kc_perror("SetProps failed", hr);
	return hrSuccess;
}

/**
 * Replace To and From recipient data in fallback message with new recipient
 *
 * @param[in] lpMessage fallback message to set new recipient data in
 * @param[in] lpRecip new recipient to deliver same message for
 *
 * @return MAPI Error code
 */
static HRESULT HrOverrideFallbackProps(IMessage *lpMessage, ECRecipient *r)
{
	memory_ptr<ENTRYID> lpEntryIdSender;
	ULONG cbEntryIdSender, n = 0;
	SPropValue p[17];

	// Set From: and To: to the receiving party, reply will be to yourself...
	// Too much information?
	p[n].ulPropTag     = PR_SENDER_NAME_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"System Administrator");
	p[n].ulPropTag     = PR_SENT_REPRESENTING_NAME_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"System Administrator");
	p[n].ulPropTag     = PR_RECEIVED_BY_NAME_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(r->wstrEmail.c_str());
	p[n].ulPropTag     = PR_SENDER_EMAIL_ADDRESS_A;
	p[n++].Value.lpszA = const_cast<char *>(r->strSMTP.c_str());
	p[n].ulPropTag     = PR_SENT_REPRESENTING_EMAIL_ADDRESS_A;
	p[n++].Value.lpszA = const_cast<char *>(r->strSMTP.c_str());
	p[n].ulPropTag     = PR_RECEIVED_BY_EMAIL_ADDRESS_A;
	p[n++].Value.lpszA = const_cast<char *>(r->strSMTP.c_str());
	p[n].ulPropTag     = PR_RCVD_REPRESENTING_EMAIL_ADDRESS_A;
	p[n++].Value.lpszA = const_cast<char *>(r->strSMTP.c_str());
	p[n].ulPropTag     = PR_SENDER_ADDRTYPE_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
	p[n].ulPropTag     = PR_SENT_REPRESENTING_ADDRTYPE_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
	p[n].ulPropTag     = PR_RECEIVED_BY_ADDRTYPE_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
	p[n].ulPropTag     = PR_RCVD_REPRESENTING_ADDRTYPE_W;
	p[n++].Value.lpszW = const_cast<wchar_t *>(L"SMTP");
	p[n].ulPropTag     = PR_SENDER_SEARCH_KEY;
	p[n++].Value.bin   = r->sSearchKey;
	p[n].ulPropTag     = PR_RECEIVED_BY_SEARCH_KEY;
	p[n++].Value.bin   = r->sSearchKey;
	p[n].ulPropTag     = PR_SENT_REPRESENTING_SEARCH_KEY;
	p[n++].Value.bin   = r->sSearchKey;

	auto hr = ECCreateOneOff(reinterpret_cast<const TCHAR *>(r->wstrFullname.c_str()),
	          reinterpret_cast<const TCHAR *>(L"SMTP"),
	          reinterpret_cast<const TCHAR *>(convert_to<std::wstring>(r->strSMTP).c_str()),
	          MAPI_UNICODE | MAPI_SEND_NO_RICH_INFO, &cbEntryIdSender, &~lpEntryIdSender);
	if (hr == hrSuccess) {
		p[n].ulPropTag       = PR_SENDER_ENTRYID;
		p[n].Value.bin.cb    = cbEntryIdSender;
		p[n++].Value.bin.lpb = reinterpret_cast<BYTE *>(lpEntryIdSender.get());
		p[n].ulPropTag       = PR_RECEIVED_BY_ENTRYID;
		p[n].Value.bin.cb    = cbEntryIdSender;
		p[n++].Value.bin.lpb = reinterpret_cast<BYTE *>(lpEntryIdSender.get());
		p[n].ulPropTag       = PR_SENT_REPRESENTING_ENTRYID;
		p[n].Value.bin.cb    = cbEntryIdSender;
		p[n++].Value.bin.lpb = reinterpret_cast<BYTE *>(lpEntryIdSender.get());
	} else {
		hr = hrSuccess;
	}

	hr = lpMessage->SetProps(n, p, nullptr);
	if (hr != hrSuccess)
		return kc_perror("Unable to set fallback delivery properties", hr);
	return hrSuccess;
}

/**
 * Set new To recipient data in message
 *
 * @param[in] lpMessage message to update recipient data in
 * @param[in] lpRecip recipient data to use
 *
 * @return MAPI error code
 */
static HRESULT HrOverrideReceivedByProps(IMessage *lpMessage,
    ECRecipient *lpRecip)
{
	SPropValue p[5];

	p[0].ulPropTag   = PR_RECEIVED_BY_ADDRTYPE_A;
	p[0].Value.lpszA = const_cast<char *>(lpRecip->strAddrType.c_str());
	p[1].ulPropTag   = PR_RECEIVED_BY_EMAIL_ADDRESS_W;
	p[1].Value.lpszW = const_cast<wchar_t *>(lpRecip->wstrUsername.c_str());
	p[2].ulPropTag   = PR_RECEIVED_BY_ENTRYID;
	p[2].Value.bin   = lpRecip->sEntryId;
	p[3].ulPropTag   = PR_RECEIVED_BY_NAME_W;
	p[3].Value.lpszW = const_cast<wchar_t *>(lpRecip->wstrFullname.c_str());
	p[4].ulPropTag   = PR_RECEIVED_BY_SEARCH_KEY;
	p[4].Value.bin   = lpRecip->sSearchKey;
	HRESULT hr = lpMessage->SetProps(ARRAY_SIZE(p), p, nullptr);
	if (hr != hrSuccess)
		return kc_perror("Unable to set RECEIVED_BY properties", hr);
	return hrSuccess;
}

/**
 * Copy a delivered message to another recipient
 *
 * @param[in] lpOrigMessage The original delivered message
 * @param[in] lpDeliverFolder The delivery folder of the new message
 * @param[in] lpRecip recipient data to use
 * @param[in] lpFallbackFolder Fallback folder in case lpDeliverFolder cannot be delivered to
 * @param[in] bFallbackDelivery lpOrigMessage is a fallback delivery message
 * @param[out] lppFolder folder the new message was created in
 * @param[out] lppMessage the newly copied message
 *
 * @return MAPI Error code
 */
static HRESULT HrCopyMessageForDelivery(IMessage *lpOrigMessage,
    IMAPIFolder *lpDeliverFolder, ECRecipient *lpRecip,
    IMAPIFolder *lpFallbackFolder, bool bFallbackDelivery,
    IMAPIFolder **lppFolder = NULL, IMessage **lppMessage = NULL)
{
	object_ptr<IMessage> lpMessage;
	object_ptr<IMAPIFolder> lpFolder;
	helpers::MAPIPropHelperPtr ptrArchiveHelper;
	static constexpr const SizedSPropTagArray(13, sptaReceivedBy) = {
		13, {
			/* Overridden by HrOverrideRecipProps() */
			PR_MESSAGE_RECIP_ME,
			PR_MESSAGE_TO_ME,
			PR_MESSAGE_CC_ME,
			/* HrOverrideReceivedByProps() */
			PR_RECEIVED_BY_ADDRTYPE,
			PR_RECEIVED_BY_EMAIL_ADDRESS,
			PR_RECEIVED_BY_ENTRYID,
			PR_RECEIVED_BY_NAME,
			PR_RECEIVED_BY_SEARCH_KEY,
			/* Written by rules */
			PR_LAST_VERB_EXECUTED,
			PR_LAST_VERB_EXECUTION_TIME,
			PR_ICON_INDEX,
		}
	};
	static constexpr const SizedSPropTagArray(12, sptaFallback) = {
		12, {
			/* Overridden by HrOverrideFallbackProps() */
			PR_SENDER_ADDRTYPE,
			PR_SENDER_EMAIL_ADDRESS,
			PR_SENDER_ENTRYID,
			PR_SENDER_NAME,
			PR_SENDER_SEARCH_KEY,
			PR_SENT_REPRESENTING_ADDRTYPE,
			PR_SENT_REPRESENTING_EMAIL_ADDRESS,
			PR_SENT_REPRESENTING_ENTRYID,
			PR_SENT_REPRESENTING_NAME,
			PR_SENT_REPRESENTING_SEARCH_KEY,
			PR_RCVD_REPRESENTING_ADDRTYPE,
			PR_RCVD_REPRESENTING_EMAIL_ADDRESS,
		}
	};

	auto hr = HrCreateMessage(lpDeliverFolder, lpFallbackFolder, &~lpFolder, &~lpMessage);
	if (hr != hrSuccess)
		return kc_perrorf("HrCreateMessage failed", hr);

	/* Copy message, exclude all previously set properties (Those are recipient dependent) */
	hr = lpOrigMessage->CopyTo(0, NULL, sptaReceivedBy, 0, NULL,
	     &IID_IMessage, lpMessage, 0, NULL);
	if (hr != hrSuccess)
		return kc_perrorf("CopyTo failed", hr);
	// For a fallback, remove some more properties
	if (bFallbackDelivery)
		lpMessage->DeleteProps(sptaFallback, 0);

	// Make sure the message is not attached to an archive
	hr = helpers::MAPIPropHelper::Create(object_ptr<IMAPIProp>(lpMessage), &ptrArchiveHelper);
	if (hr != hrSuccess)
		return kc_perrorf("helpers::MAPIPropHelper::Create failed", hr);
	hr = ptrArchiveHelper->DetachFromArchives();
	if (hr != hrSuccess)
		return kc_perrorf("DetachFromArchives failed", hr);
	if (lpRecip->bHasIMAP)
		hr = Util::HrCopyIMAPData(lpOrigMessage, lpMessage);
	else
		hr = Util::HrDeleteIMAPData(lpMessage); // make sure the imap data is not set for this user.
	if (hr != hrSuccess)
		return kc_perrorf("IMAP handling failed", hr);
	if (lppFolder)
		lpFolder->QueryInterface(IID_IMAPIFolder, reinterpret_cast<void **>(lppFolder));
	if (lppMessage)
		lpMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppMessage));
	return hrSuccess;
}

/**
 * Make a new MAPI session under a specific username
 *
 * @param[in] lpArgs delivery options
 * @param[in] szUsername username to create mapi session for
 * @param[out] lppSession new MAPI session for user
 * @param[in] bSuppress suppress logging (default: false)
 *
 * @return MAPI Error code
 */
static HRESULT HrGetSession(const DeliveryArgs *lpArgs,
    const wchar_t *szUsername, IMAPISession **lppSession,
    bool bSuppress = false)
{
	auto hr = HrOpenECSession(lppSession, PROJECT_VERSION, "dagent",
	          szUsername, L"", lpArgs->strPath.c_str(), 0,
	          g_lpConfig->GetSetting("sslkey_file", "", nullptr),
	          g_lpConfig->GetSetting("sslkey_pass", "", nullptr));
	if (hr == hrSuccess)
		return hrSuccess;
	// if connecting fails, the mailer should try to deliver again.
	switch (hr) {
	case MAPI_E_NETWORK_ERROR:
		if (!bSuppress)
			ec_log_err("Unable to connect to storage server for user %ls, using socket: \"%s\"", szUsername, lpArgs->strPath.c_str());
		break;

	// MAPI_E_NO_ACCESS or MAPI_E_LOGON_FAILED are fatal (user does not exist)
	case MAPI_E_LOGON_FAILED: {
		// running dagent as Unix user != lpRecip->strUsername and ! listed in local_admin_user, which gives this error too
		if (!bSuppress)
			ec_log_err("Access denied or connection failed for user \"%ls\", using socket: \"%s\": %s (%x)",
				szUsername, lpArgs->strPath.c_str(), GetMAPIErrorMessage(hr), hr);
		// so also log userid we're running as
		auto pwd = getpwuid(getuid());
		std::string strUnixUser = (pwd != nullptr && pwd->pw_name != nullptr) ? pwd->pw_name : stringify(getuid());
		if (!bSuppress)
			ec_log_debug("Current uid:%d username:%s", getuid(), strUnixUser.c_str());
		break;
	}
	default:
		if (!bSuppress)
			ec_log_err("Unable to login for user \"%ls\": %s (%x)",
				szUsername, GetMAPIErrorMessage(hr), hr);
		break;
	}
	return hr;
}

/**
 * Run rules on a message and/or send an oof email before writing it
 * to the server.
 *
 * @param[in] lpAdrBook Addressbook to use during rules
 * @param[in] lpStore Store the message will be written too
 * @param[in] lpInbox Inbox of the user message is being delivered to
 * @param[in] lpFolder Actual delivery folder of message
 * @param[in,out] lppMessage message being delivered, can return another message due to rules
 * @param[in] lpRecip recipient that is delivered to
 * @param[in] lpArgs delivery options
 *
 * @return MAPI Error code
 */
static HRESULT HrPostDeliveryProcessing(pym_plugin_intf *lppyMapiPlugin,
    LPADRBOOK lpAdrBook, LPMDB lpStore, IMAPIFolder *lpInbox,
    IMAPIFolder *lpFolder, IMessage **lppMessage, ECRecipient *lpRecip,
    DeliveryArgs *lpArgs)
{
	object_ptr<IMAPISession> lpUserSession;
	SPropValuePtr ptrProp;

	auto hr = HrOpenECSession(&~lpUserSession, PROJECT_VERSION,
	          "dagent:delivery", lpRecip->wstrUsername.c_str(), L"",
	          lpArgs->strPath.c_str(), EC_PROFILE_FLAGS_NO_NOTIFICATIONS,
	          g_lpConfig->GetSetting("sslkey_file", "", nullptr),
	          g_lpConfig->GetSetting("sslkey_pass", "", nullptr));
	if (hr != hrSuccess)
		return hr;

	if(FNeedsAutoAccept(lpStore, *lppMessage)) {
		ec_log_info("Starting MR autoaccepter");
		hr = HrAutoAccept(lpArgs->sc.get(), lpRecip, lpStore, *lppMessage);
		if(hr == hrSuccess) {
			ec_log_info("Autoaccept processing completed successfully. Skipping further processing.");
			// The MR autoaccepter has processed the message. Skip any further work on this message: don't
			// run rules and don't send new mail notifications (The message should be deleted now)
			return MAPI_E_CANCEL;
		}
		ec_log_info("Autoaccept processing failed, proceeding with rules processing: %s (%x).",
			GetMAPIErrorMessage(hr), hr);
		lpArgs->got_error = true;
		// The MR autoaccepter did not run properly. This could be correct behaviour; for example the
		// autoaccepter may want to defer accepting to a human controller. This means we have to continue
		// processing as if the autoaccepter was not used
		hr = hrSuccess;
	}
	else if (FNeedsAutoProcessing(lpStore, *lppMessage)) {
		ec_log_info("Starting MR auto processing");
		hr = HrAutoProcess(lpArgs->sc.get(), lpRecip, lpStore, *lppMessage);
		if (hr == hrSuccess) {
			ec_log_info("Automatic MR processing successful.");
		} else {
			ec_log_info("Automatic MR processing failed: %s (%x).",
				GetMAPIErrorMessage(hr), hr);
			lpArgs->got_error = true;
		}
	}

	if (lpFolder == lpInbox) {
		// process rules for the inbox
		hr = HrProcessRules(convert_to<std::string>(lpRecip->wstrUsername), lppyMapiPlugin, lpUserSession, lpAdrBook, lpStore, lpInbox, lppMessage, lpArgs->sc.get());
		if (hr == MAPI_E_CANCEL)
			ec_log_notice("Message canceled by rule");
		else if (hr != hrSuccess)
			kc_pwarn("Unable to process rules", hr);
		// continue, still send possible out-of-office message
	}

	// do not send vacation message for junk messages
	if (lpArgs->ulDeliveryMode != DM_JUNK &&
	// do not send vacation message on delegated messages
	    (HrGetOneProp(*lppMessage, PR_DELEGATED_BY_RULE, &~ptrProp) != hrSuccess || !ptrProp->Value.b)) {
		auto autoresponder = lpArgs->strAutorespond.size() > 0 ? lpArgs->strAutorespond : g_lpConfig->GetSetting("autoresponder");
		SendOutOfOffice(lpArgs->sc.get(), lpAdrBook, lpStore,
			*lppMessage, lpRecip, autoresponder);
	}
	return hr;
}

/**
 * Find spam header if needed, and mark delivery as spam delivery if
 * header found.
 *
 * @param[in] strMail rfc2822 email being delivered
 * @param[in,out] lpArgs delivery options
 *
 * @return MAPI Error code
 */
static HRESULT FindSpamMarker(const std::string &strMail,
    DeliveryArgs *lpArgs)
{
	HRESULT hr = hrSuccess;
	const char *szHeader = g_lpConfig->GetSetting("spam_header_name", "", NULL);
	const char *szValue = g_lpConfig->GetSetting("spam_header_value", "", NULL);
	std::string strHeaders;
	auto laters = make_scope_success([&]() { lpArgs->sc->inc(lpArgs->ulDeliveryMode == DM_JUNK ? SCN_DAGENT_IS_SPAM : SCN_DAGENT_IS_HAM); });

	if (!szHeader || !szValue)
		return hr;
	// find end of headers
	auto end = strMail.find("\r\n\r\n");
	if (end == string::npos)
		return hr;
	end += 2;

	// copy headers in upper case, need to resize destination first
	strHeaders.resize(end);
	transform(strMail.begin(), strMail.begin() +end, strHeaders.begin(), ::toupper);
	auto match = strToUpper("\r\n"s + szHeader + ":");

	// find header
	auto pos = strHeaders.find(match.c_str());
	if (pos == string::npos)
		return hr;

	// skip header and find end of line
	pos += match.length();
	end = strHeaders.find("\r\n", pos);
	match = strToUpper(szValue);
	// find value in header line (no header continuations supported here)
	pos = strHeaders.find(match.c_str(), pos);
	if (pos == string::npos || pos > end)
		return hr;
	// found, override delivery to junkmail folder
	lpArgs->ulDeliveryMode = DM_JUNK;
	ec_log_info("Spam marker found in e-mail, delivering to junk-mail folder");
	return hr;
}

/**
 * Deliver an email (source is either rfc2822 or previous delivered
 * mapi message) to a specific recipient.
 *
 * @param[in] lpSession MAPI session (user session when not in LMTP mode, else admin session)
 * @param[in] lpStore default store for lpSession (user store when not in LMTP mode, else admin store)
 * @param[in] bIsAdmin indicates that lpSession and lpStore are an admin session and store (true in LMTP mode)
 * @param[in] lpAdrBook Global Addressbook
 * @param[in] lpOrigMessage a previously delivered message, if any
 * @param[in] bFallbackDelivery previously delivered message was a fallback message
 * @param[in] strMail original received rfc2822 email
 * @param[in] lpRecip recipient to deliver message to
 * @param[in] lpArgs delivery options
 * @param[out] lppMessage the newly delivered message
 * @param[out] lpbFallbackDelivery newly delivered message is a fallback message
 *
 * @return MAPI Error code
 */
static HRESULT ProcessDeliveryToRecipient(pym_plugin_intf *lppyMapiPlugin,
    IMAPISession *lpSession, IMsgStore *lpStore, bool bIsAdmin,
    LPADRBOOK lpAdrBook, IMessage *lpOrigMessage, bool bFallbackDelivery,
    const std::string &strMail, ECRecipient *lpRecip, DeliveryArgs *lpArgs,
    IMessage **lppMessage, bool *lpbFallbackDelivery)
{
	object_ptr<IMsgStore> lpTargetStore;
	object_ptr<IMAPIFolder> lpTargetFolder, lpFolder, lpInbox;
	object_ptr<IMessage> lpDeliveryMessage, lpMessageTmp;
	object_ptr<IABContainer> lpAddrDir;
	ULONG ulResult = 0;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	memory_ptr<ECQUOTASTATUS> lpsQuotaStatus;
	bool over_quota = false;

	// single user deliver did not lookup the user
	if (lpRecip->strSMTP.empty()) {
		auto hr = OpenResolveAddrFolder(lpAdrBook, &~lpAddrDir);
		if (hr != hrSuccess)
			return kc_perrorf("OpenResolveAddrFolder failed", hr);
		hr = ResolveUser(lpAddrDir, lpRecip);
		if (hr != hrSuccess)
			return kc_perrorf("ResolveUser failed", hr);
	}

	auto hr = HrGetDeliveryStoreAndFolder(lpSession, lpStore, lpRecip,
	          lpArgs, &~lpTargetStore, &~lpInbox, &~lpTargetFolder);
	if (hr != hrSuccess)
		return kc_perrorf("HrGetDeliveryStoreAndFolder failed", hr);

	if (!lpOrigMessage) {
		/* No message was provided, we have to construct it personally */
		bool bExpired = false;

		hr = HrCreateMessage(lpTargetFolder, lpInbox, &~lpFolder, &~lpMessageTmp);
		if (hr != hrSuccess)
			return kc_perrorf("HrCreateMessage failed", hr);
		hr = HrStringToMAPIMessage(strMail, lpSession, lpTargetStore, lpAdrBook, lpFolder, lpMessageTmp, lpRecip, lpArgs, &~lpDeliveryMessage, &bFallbackDelivery);
		if (hr != hrSuccess)
			return kc_perrorf("HrStringToMAPIMessage failed", hr);

		/*
		 * Check if the message has expired.
		 */
		hr = HrMessageExpired(lpArgs->sc.get(), lpDeliveryMessage, &bExpired);
		if (hr != hrSuccess)
			return kc_perrorf("HrMessageExpired failed", hr);
		if (bExpired)
			/* Set special error code for callers */
			return MAPI_W_CANCEL_MESSAGE;

		hr = lppyMapiPlugin->MessageProcessing("PostConverting", lpSession, lpAdrBook, NULL, NULL, lpDeliveryMessage, &ulResult);
		if (hr != hrSuccess)
			return kc_perrorf("MessageProcessing failed", hr);
		// TODO do something with ulResult
	} else {
		/* Copy message to prepare for new delivery */
		hr = HrCopyMessageForDelivery(lpOrigMessage, lpTargetFolder, lpRecip, lpInbox, bFallbackDelivery, &~lpFolder, &~lpDeliveryMessage);
		if (hr != hrSuccess)
			return kc_perrorf("HrCopyMessageForDelivery failed", hr);
	}

	hr = HrOverrideRecipProps(lpAdrBook, lpDeliveryMessage, lpRecip);
	if (hr != hrSuccess)
		return kc_perrorf("HrOverrideRecipProps failed", hr);

	if (bFallbackDelivery) {
		hr = HrOverrideFallbackProps(lpDeliveryMessage, lpRecip);
		if (hr != hrSuccess)
			return kc_perrorf("HrOverrideFallbackProps failed", hr);
	} else {
		hr = HrOverrideReceivedByProps(lpDeliveryMessage, lpRecip);
		if (hr != hrSuccess)
			return kc_perrorf("HrOverrideReceivedByProps failed", hr);
	}

	hr = lppyMapiPlugin->MessageProcessing("PreDelivery", lpSession, lpAdrBook, lpTargetStore, lpTargetFolder, lpDeliveryMessage, &ulResult);
	if (hr != hrSuccess)
		return kc_perrorf("MessageProcessing(2) failed", hr);

	// TODO do something with ulResult
	if (ulResult == MP_STOP_SUCCESS) {
		if (lppMessage)
			lpDeliveryMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppMessage));
		if (lpbFallbackDelivery)
			*lpbFallbackDelivery = bFallbackDelivery;
		return hr;
	}

	// Do rules & out-of-office
	hr = HrPostDeliveryProcessing(lppyMapiPlugin, lpAdrBook, lpTargetStore, lpInbox, lpTargetFolder, &+lpDeliveryMessage, lpRecip, lpArgs);
	if (hr != MAPI_E_CANCEL) {
		if(bIsAdmin) {
			hr = lpStore->QueryInterface(IID_IECServiceAdmin, &~lpServiceAdmin);
			if(hr != hrSuccess)
				kc_perror("Unable to access ServiceAdmin interface", hr);
			else {
				hr = lpServiceAdmin->GetQuotaStatus(lpRecip->sEntryId.cb, (LPENTRYID)lpRecip->sEntryId.lpb, &~lpsQuotaStatus);
				if(hr != hrSuccess)
					kc_perrorf("Unable to determine quota status", hr);
				else
					over_quota = lpsQuotaStatus->quotaStatus == QUOTA_HARDLIMIT;
			}
		}

		if(over_quota)
			hr = MAPI_E_STORE_FULL;
		else
			// ignore other errors for rules, still want to save the delivered message
			// Save message changes, message becomes visible for the user
			hr = lpDeliveryMessage->SaveChanges(KEEP_OPEN_READWRITE);

		if (hr != hrSuccess) {
			if (hr == MAPI_E_STORE_FULL)
				// make sure the error is printed on stderr, so this will be bounced as error by the MTA.
				// use cerr to avoid quiet mode.
				fprintf(stderr, "Store of user %ls is over quota limit.\n", lpRecip->wstrUsername.c_str());
			else
				kc_perror("Unable to commit message", hr);
			return hr;
		}

		hr = lppyMapiPlugin->MessageProcessing("PostDelivery", lpSession, lpAdrBook, lpTargetStore, lpTargetFolder, lpDeliveryMessage, &ulResult);
		if (hr != hrSuccess)
			return kc_perrorf("MessageProcessing(3) failed", hr);

		// TODO do something with ulResult
		if (parseBool(g_lpConfig->GetSetting("archive_on_delivery"))) {
			MAPISessionPtr ptrAdminSession;
			ArchivePtr ptrArchive;

			if (bIsAdmin)
				hr = lpSession->QueryInterface(iid_of(ptrAdminSession), &~ptrAdminSession);
			else {
				const char *server = g_lpConfig->GetSetting("server_socket");
				server = GetServerUnixSocket(server); // let environment override if present
				hr = HrOpenECAdminSession(&~ptrAdminSession, PROJECT_VERSION,
				     "dagent:system", server, EC_PROFILE_FLAGS_NO_NOTIFICATIONS,
				     g_lpConfig->GetSetting("sslkey_file", "", nullptr),
				     g_lpConfig->GetSetting("sslkey_pass", "", nullptr));
			}
			if (hr != hrSuccess)
				return kc_perror("Unable to open admin session for archive access", hr);
			hr = Archive::Create(ptrAdminSession, &ptrArchive);
			if (hr != hrSuccess)
				return kc_perror("Unable to instantiate archive object", hr);
			hr = ptrArchive->HrArchiveMessageForDelivery(lpDeliveryMessage, g_lpLogger);
			if (hr != hrSuccess) {
				Util::HrDeleteMessage(lpSession, lpDeliveryMessage);
				return kc_perror("Unable to archive message", hr);
			}
		}

		if (lpArgs->bNewmailNotify) {
			ULONG ulNewMailNotify = TRUE;

			hr = lppyMapiPlugin->RequestCallExecution("SendNewMailNotify",  lpSession, lpAdrBook, lpTargetStore, lpTargetFolder, lpDeliveryMessage, &ulNewMailNotify, &ulResult);
			if (hr != hrSuccess) {
				// Plugin failed so fallback on the original state
				ulNewMailNotify = lpArgs->bNewmailNotify;
				hr = hrSuccess;
			}
			if (ulNewMailNotify) {
				hr = HrNewMailNotification(lpTargetStore, lpDeliveryMessage);
				if (hr != hrSuccess)
					kc_pwarn("Unable to send \"New Mail\" notification", hr);
				else
					ec_log_debug("Send 'New Mail' notification");
				hr = hrSuccess;
			}
		}
	}

	if (lppMessage)
		lpDeliveryMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppMessage));
	if (lpbFallbackDelivery)
		*lpbFallbackDelivery = bFallbackDelivery;
	return hr;
}

/**
 * Log that the message was expired, and send that response for every given LMTP received recipient
 *
 * @param[in] start Start of recipient list
 * @param[in] end End of recipient list
 */
static void RespondMessageExpired(recipients_t::const_iterator iter,
    recipients_t::const_iterator end)
{
	convert_context converter;
	ec_log_warn("Message was expired, not delivering");
	for (; iter != end; ++iter)
		(*iter)->wstrDeliveryStatus = "250 2.4.7 %s Delivery time expired";
}

/**
 * For a specific storage server, deliver the same message to a list of
 * recipients. This makes sure this message is correctly single
 * instanced on this server.
 *
 * In this function, it is mandatory to have processed all recipients
 * in the list.
 *
 * @param[in] lpUserSession optional session of one user the message is being delivered to (cmdline dagent, NULL on LMTP mode)
 * @param[in] lpMessage an already delivered message
 * @param[in] bFallbackDelivery already delivered message is a fallback message
 * @param[in] strMail the rfc2822 received email
 * @param[in] strServer uri of the storage server to connect to
 * @param[in] listRecipients list of recipients present on the server connecting to
 * @param[in] lpAdrBook Global addressbook
 * @param[in] lpArgs delivery options
 * @param[out] lppMessage The newly delivered message
 * @param[out] lpbFallbackDelivery newly delivered message is a fallback message
 *
 * @return MAPI Error code
 */
static HRESULT ProcessDeliveryToServer(pym_plugin_intf *lppyMapiPlugin,
    IMAPISession *lpUserSession, IMessage *lpMessage, bool bFallbackDelivery,
    const std::string &strMail, const std::string &strServer,
    const recipients_t &listRecipients, LPADRBOOK lpAdrBook,
    DeliveryArgs *lpArgs, IMessage **lppMessage, bool *lpbFallbackDelivery)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPISession> lpSession;
	object_ptr<IMsgStore> lpStore;
	object_ptr<IMessage> lpOrigMessage;
	bool bFallbackDeliveryTmp = false;
	convert_context converter;

	lpArgs->sc->inc(SCN_DAGENT_TO_SERVER);
	// if we already had a message, we can create a copy.
	if (lpMessage)
		lpMessage->QueryInterface(IID_IMessage, &~lpOrigMessage);

	if (lpUserSession)
		hr = lpUserSession->QueryInterface(IID_IMAPISession, &~lpSession);
	else
		hr = HrOpenECAdminSession(&~lpSession, PROJECT_VERSION,
		     "dagent/delivery:system", strServer.c_str(),
		     EC_PROFILE_FLAGS_NO_NOTIFICATIONS,
		     g_lpConfig->GetSetting("sslkey_file", "", NULL),
		     g_lpConfig->GetSetting("sslkey_pass", "", NULL));
	if (hr != hrSuccess || (hr = HrOpenDefaultStore(lpSession, &~lpStore)) != hrSuccess) {
		// notify LMTP client soft error to try again later
		for (const auto &recip : listRecipients)
			// error will be shown in postqueue status in postfix, probably too in other serves and mail syslog service
			recip->wstrDeliveryStatus = "450 4.5.0 %s network or permissions error to storage server: " + stringify_hex(hr);
		return kc_perror("Unable to open default store for system account", hr);
	}

	for (auto iter = listRecipients.cbegin(); iter != listRecipients.end(); ++iter) {
		const auto &recip = *iter;
		object_ptr<IMessage> lpMessageTmp;
		/*
		 * Normal error codes must be ignored, since we want to attempt to deliver the email to all users,
		 * however when the error code MAPI_W_CANCEL_MESSAGE was provided, the message has expired and it is
		 * pointles to continue delivering the mail. However we must continue looping through all recipients
		 * to inform the MTA we did handle the email properly.
		 */
		hr = ProcessDeliveryToRecipient(lppyMapiPlugin, lpSession,
		     lpStore, lpUserSession == NULL, lpAdrBook, lpOrigMessage,
		     bFallbackDelivery, strMail, recip, lpArgs, &~lpMessageTmp,
		     &bFallbackDeliveryTmp);
		if (hr == hrSuccess || hr == MAPI_E_CANCEL) {
			if (hr == hrSuccess) {
				memory_ptr<SPropValue> lpMessageId, lpSubject;
				wstring wMessageId;

				if (HrGetOneProp(lpMessageTmp, PR_INTERNET_MESSAGE_ID_W, &~lpMessageId) == hrSuccess)
					wMessageId = lpMessageId->Value.lpszW;
				HrGetFullProp(lpMessageTmp, PR_SUBJECT_W, &~lpSubject);
				ec_log_info("Delivered message to \"%ls\", Subject: \"%ls\", Message-Id: %ls, size %zu",
					recip->wstrUsername.c_str(),
					(lpSubject != NULL) ? lpSubject->Value.lpszW : L"<none>",
					wMessageId.c_str(), strMail.size());
			}
			// cancel already logged.
			hr = hrSuccess;
			recip->wstrDeliveryStatus = "250 2.1.5 %s Ok";
		} else if (hr == MAPI_W_CANCEL_MESSAGE) {
			/* Loop through all remaining recipients and start responding the status to LMTP */
			RespondMessageExpired(iter, listRecipients.cend());
			return MAPI_W_CANCEL_MESSAGE;
		} else {
			ec_log_err("Unable to deliver message to \"%ls\": %s (%x)",
				recip->wstrUsername.c_str(), GetMAPIErrorMessage(hr), hr);
			/* LMTP requires different notification when Quota for user was exceeded */
			if (hr == MAPI_E_STORE_FULL)
				recip->wstrDeliveryStatus = "552 5.2.2 %s Quota exceeded";
			else
				recip->wstrDeliveryStatus = "450 4.2.0 %s Mailbox temporarily unavailable";
		}

		if (lpMessageTmp) {
			if (lpOrigMessage == NULL)
				// If we delivered the message for the first time,
				// we keep the intermediate message to make copies of.
				lpMessageTmp->QueryInterface(IID_IMessage, &~lpOrigMessage);
			bFallbackDelivery = bFallbackDeliveryTmp;
		}
	}
	if (lppMessage != nullptr && lpOrigMessage)
		lpOrigMessage->QueryInterface(IID_IMessage, reinterpret_cast<void **>(lppMessage));
	if (lpbFallbackDelivery)
		*lpbFallbackDelivery = bFallbackDelivery;
	return hr;
}

/**
 * Commandline dagent delivery entrypoint.
 * Deliver an email to one recipient.
 *
 * Although this function is passed a recipient list, it's only
 * because the rest of the functions it calls requires this and the
 * caller of this function already has a list.
 *
 * @param[in] lpSession User MAPI session
 * @param[in] lpAdrBook Global addressbook
 * @param[in] fp input file which contains the email to deliver
 * @param[in] lstSingleRecip list of recipients to deliver email to (one user)
 * @param[in] lpArgs delivery options
 *
 * @return MAPI Error code
 */
static HRESULT ProcessDeliveryToSingleRecipient(pym_plugin_intf *lppyMapiPlugin,
    IMAPISession *lpSession, LPADRBOOK lpAdrBook, FILE *fp,
    recipients_t &lstSingleRecip, DeliveryArgs *lpArgs)
{
	std::string strMail;
	lpArgs->sc->inc(SCN_DAGENT_TO_SINGLE_RECIP);

	/* Always start at the beginning of the file */
	rewind(fp);
	/* Read file into string */
	HRESULT hr = HrMapFileToString(fp, &strMail);
	if (hr != hrSuccess)
		return kc_perror("Unable to map input to memory", hr);

	FindSpamMarker(strMail, lpArgs);
	hr = ProcessDeliveryToServer(lppyMapiPlugin, lpSession, NULL, false, strMail, lpArgs->strPath, lstSingleRecip, lpAdrBook, lpArgs, NULL, NULL);
	if (hr != hrSuccess)
		return kc_perrorf("ProcessDeliveryToServer failed", hr);
	return hrSuccess;
}

/**
 * Deliver email from file to a list of recipients which are grouped
 * by server.
 *
 * @param[in] lpSession Admin MAPI Session
 * @param[in] lpAdrBook Addressbook
 * @param[in] fp file containing the received email
 * @param[in] lpServerNameRecips recipients grouped by server
 * @param[in] lpArgs delivery options
 *
 * @return MAPI Error code
 */
static HRESULT ProcessDeliveryToCompany(pym_plugin_intf *lppyMapiPlugin,
    IMAPISession *lpSession, LPADRBOOK lpAdrBook, FILE *fp,
    const serverrecipients_t *lpServerNameRecips, DeliveryArgs *lpArgs)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMessage> lpMasterMessage;
	std::string strMail;
	serverrecipients_t listServerPathRecips;
	bool bFallbackDelivery = false, bExpired = false;

	lpArgs->sc->inc(SCN_DAGENT_TO_COMPANY);
	if (lpServerNameRecips == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	/* Always start at the beginning of the file */
	rewind(fp);
	/* Read file into string */
	hr = HrMapFileToString(fp, &strMail);
	if (hr != hrSuccess)
		return kc_perror("Unable to map input to memory", hr);

	FindSpamMarker(strMail, lpArgs);
	hr = ResolveServerToPath(lpSession, lpServerNameRecips, lpArgs->strPath, &listServerPathRecips);
	if (hr != hrSuccess)
		return kc_perrorf("ResolveServerToPath failed", hr);

	for (const auto &iter : listServerPathRecips) {
		object_ptr<IMessage> lpMessageTmp;
		bool bFallbackDeliveryTmp = false;

		if (bExpired) {
			/* Simply loop through all recipients to respond to LMTP */
			RespondMessageExpired(iter.second.cbegin(), iter.second.cend());
			continue;
		}
		hr = ProcessDeliveryToServer(lppyMapiPlugin, NULL,
		     lpMasterMessage, bFallbackDelivery, strMail,
		     convert_to<std::string>(iter.first), iter.second,
		     lpAdrBook, lpArgs, &~lpMessageTmp, &bFallbackDeliveryTmp);
		if (hr == MAPI_W_CANCEL_MESSAGE)
			bExpired =  true;
			/* Don't report the error further (ignore it) */
		else if (hr != hrSuccess)
			ec_log_err("Unable to deliver all messages for server \"%ls\"",
				iter.first.c_str());

		/* lpMessage is our base message which we will copy to each server/recipient */
		if (lpMessageTmp == nullptr)
			continue;
		if (lpMasterMessage == NULL)
			// keep message to make copies of on the same server
			lpMessageTmp->QueryInterface(IID_IMessage, &~lpMasterMessage);
		bFallbackDelivery = bFallbackDeliveryTmp;
	}

	ec_log_info("Finished processing message");
	return hrSuccess;
}

/**
 * Within a company space, find the recipient with the lowest
 * administrator rights. This user can be used to open the Global
 * Addressbook.
 *
 * @param[in] lpServerRecips all recipients to deliver for within a company
 * @param[out] lppRecipient a recipient with the rights lower than server admin
 *
 * @return MAPI Error code
 */
static HRESULT
FindLowestAdminLevelSession(const serverrecipients_t *lpServerRecips,
    DeliveryArgs *lpArgs, IMAPISession **lppUserSession)
{
	HRESULT hr = hrSuccess;
	ECRecipient *lpRecip = NULL;
	bool bFound = false;

	for (const auto &server : *lpServerRecips) {
		for (const auto &recip : server.second) {
			if (recip->ulDisplayType == DT_REMOTE_MAILUSER)
				continue;
			else if (!lpRecip)
				lpRecip = recip;
			else if (recip->ulAdminLevel <= lpRecip->ulAdminLevel)
				lpRecip = recip;

			if (lpRecip->ulAdminLevel < 2) {
				// if this recipient cannot make the session for the addressbook, it will also not be able to open the store for delivery later on
				hr = HrGetSession(lpArgs, lpRecip->wstrUsername.c_str(), lppUserSession);
				if (hr == hrSuccess) {
					bFound = true;
					goto found;
				}
				// remove found entry, so higher admin levels can be found too if a lower cannot login
				ec_log_debug("Login on user \"%ls\" for addressbook resolves failed: %s (%x)",
					lpRecip->wstrUsername.c_str(), GetMAPIErrorMessage(hr), hr);
				lpRecip = NULL;
			}
		}
	}
	if (lpRecip && !bFound) {
		// we picked only an admin from the list, try this logon
		hr = HrGetSession(lpArgs, lpRecip->wstrUsername.c_str(), lppUserSession);
		bFound = (hr == hrSuccess);
	}

found:
	if (!bFound)
		hr = MAPI_E_NOT_FOUND; /* This only happens if there are no recipients or everybody is a contact */

	return hr;
}

/**
 * LMTP delivery entry point
 * Deliver email to a list of recipients, grouped by company, grouped by server.
 *
 * @param[in] lpSession Admin MAPI Session
 * @param[in] fp file containing email to deliver
 * @param[in] lpCompanyRecips list of all recipients to deliver to
 * @param[in] lpArgs delivery options
 *
 * @return MAPI Error code
 */
static HRESULT ProcessDeliveryToList(pym_plugin_intf *lppyMapiPlugin,
    IMAPISession *lpSession, FILE *fp, companyrecipients_t *lpCompanyRecips,
    DeliveryArgs *lpArgs)
{
	HRESULT hr = hrSuccess;
	lpArgs->sc->inc(SCN_DAGENT_TO_LIST);
	/*
	 * Find user with lowest adminlevel, we will use the addressbook for this
	 * user to make sure the recipient resolving for all recipients for the company
	 * resolving will occur with the minimum set of view-levels to other
	 * companies.
	 */
	for (const auto &comp : *lpCompanyRecips) {
		object_ptr<IMAPISession> lpUserSession;
		object_ptr<IAddrBook> lpAdrBook;

		hr = FindLowestAdminLevelSession(&comp.second, lpArgs, &~lpUserSession);
		if (hr != hrSuccess)
			return kc_perrorf("FindLowestAdminLevelSession failed", hr);
		hr = OpenResolveAddrFolder(lpUserSession, &~lpAdrBook, nullptr);
		if (hr != hrSuccess)
			return kc_perrorf("OpenResolveAddrFolder failed", hr);
		hr = ProcessDeliveryToCompany(lppyMapiPlugin, lpSession, lpAdrBook, fp, &comp.second, lpArgs);
		if (hr != hrSuccess)
			return kc_perrorf("ProcessDeliveryToCompany failed", hr);
	}
	return hrSuccess;
}

static void add_misc_headers(FILE *tmp, const std::string &helo,
    const std::string &from, const DeliveryArgs *args)
{
	/*
	 * 1. Return-Path
	 * Add return-path header string, as specified by RFC 5321 (ZCP-12424)
	 * https://tools.ietf.org/html/rfc5322
	 * it should look like:
	 * 	Return-Path: <noreply+dev=kopano.io@other.com>
	 */
	fprintf(tmp, "Return-Path: <%s>\r\n", from.c_str());

	/*
	 * 2. Received
	 *
	 * Received: from lists.digium.com (digium-69-16-138-164.phx1.puregig.net [69.16.138.164])
	 *  by blah.com (Postfix) with ESMTP id 78BEB1CA369
	 *  for <target@blah.com>; Mon, 12 Dec 2005 11:35:12 +0100 (CET)
	 */
	std::string server_name;
	const char *dummy = g_lpConfig->GetSetting("server_name");
	if (dummy != nullptr) {
		server_name = dummy;
	} else {
		char buffer[4096]{};
		if (gethostname(buffer, sizeof buffer) == -1)
			strcpy(buffer, "???");
		server_name = buffer;
	}

	time_t t = time(nullptr);
	struct tm *tm = localtime(&t);
	char time_str[4096];
	strftime(time_str, sizeof(time_str), "%a, %d %b %Y %T %z (%Z)", tm);
	fprintf(tmp, "Received: from %s (%s)\r\n", helo.c_str(), args->lpChannel->peer_addr());
	fprintf(tmp, "\tby %s (kopano-dagent) with LMTP;\r\n", server_name.c_str());
	fprintf(tmp, "\t%s\r\n", time_str);
}

/**
 * Handle an incoming LMTP connection
 *
 * @param[in] lpArg delivery options
 *
 * @return NULL
 */
static void *HandlerLMTP(void *lpArg)
{
	std::unique_ptr<DeliveryArgs> lpArgs(static_cast<DeliveryArgs *>(lpArg));
	std::string strMailAddress, inBuffer, curFrom = "???", heloName = "???";
	companyrecipients_t mapRCPT;
	std::list<std::string> lOrderedRecipients;
	std::map<std::string, std::string> mapRecipientResults;
	HRESULT hr = hrSuccess;
	bool bLMTPQuit = false;
	int timeouts = 0;
	PyMapiPluginFactory pyMapiPluginFactory;
	convert_context converter;
	LMTP lmtp(lpArgs->lpChannel.get(), lpArgs->strPath.c_str(), g_lpConfig.get());

	/* For resolving addresses from Address Book */
	object_ptr<IMAPISession> lpSession;
	object_ptr<IAddrBook> lpAdrBook;
	object_ptr<IABContainer> lpAddrDir;

	auto laters = make_scope_success([&]() {
		FreeServerRecipients(&mapRCPT);
		ec_log_info("LMTP thread exiting");
	});

	lpArgs->sc->inc(SCN_LMTP_SESSIONS);
	ec_log_info("Starting worker for LMTP request pid %d", getpid());
	const char *lpEnvGDB  = getenv("GDB");
	if (lpEnvGDB && parseBool(lpEnvGDB)) {
		lmtp.HrResponse("220-DEBUG MODE, please wait");
		Sleep(10000); //wait 10 seconds so you can attach gdb
		ec_log_info("Starting worker for LMTP request");
	}
	hr = HrGetSession(lpArgs.get(), KOPANO_SYSTEM_USER_W, &~lpSession);
	if (hr != hrSuccess) {
		kc_perrorf("HrGetSession failed", hr);
		lmtp.HrResponse("421 internal error: GetSession failed");
		return nullptr;
	}
	hr = OpenResolveAddrFolder(lpSession, &~lpAdrBook, &~lpAddrDir);
	if (hr != hrSuccess) {
		kc_perrorf("OpenResolveAddrFolder failed", hr);
		lmtp.HrResponse("421 internal error: OpenResolveAddrFolder failed");
		return nullptr;
	}

	// Send hello message
	lmtp.HrResponse("220 2.1.5 LMTP server is ready");
	while (!bLMTPQuit && !g_bQuit) {
		LMTP_Command eCommand;

		hr = lpArgs->lpChannel->HrSelect(60);
		if (hr == MAPI_E_CANCEL)
			/* signalled - reevaluate quit status */
			continue;

		if(hr == MAPI_E_TIMEOUT) {
			if(timeouts < 10) {
				++timeouts;
				continue;
			}
			lmtp.HrResponse("221 5.0.0 Connection closed due to timeout");
			ec_log_err("Connection closed due to timeout");
			bLMTPQuit = true;
			break;
		} else if (hr == MAPI_E_NETWORK_ERROR) {
			ec_log_err("Socket error: %s", strerror(errno));
			bLMTPQuit = true;
			break;
		}

		timeouts = 0;
		inBuffer.clear();
		errno = 0;				// clear errno, might be from double logoff to server
		hr = lpArgs->lpChannel->HrReadLine(inBuffer);
		if (hr != hrSuccess){
			if (errno)
				ec_log_err("Failed to read line: %s", strerror(errno));
			else
				ec_log_err("Client disconnected");
			bLMTPQuit = true;
			break;
		}

		if (g_bQuit) {
			lmtp.HrResponse("221 2.0.0 Server is shutting down");
			bLMTPQuit = true;
			hr = MAPI_E_CALL_FAILED;
			break;
		}

		ec_log_debug("> " + inBuffer);
		hr = lmtp.HrGetCommand(inBuffer, eCommand);
		if (hr != hrSuccess) {
			lmtp.HrResponse("555 5.5.4 Command not recognized");
			lpArgs->sc->inc(SCN_LMTP_UNKNOWN_COMMAND);
			continue;
		}

		switch (eCommand) {
		case LMTP_Command_LHLO:
			if (lmtp.HrCommandLHLO(inBuffer, heloName) == hrSuccess) {
				lmtp.HrResponse("250-SERVER ready");
				lmtp.HrResponse("250-PIPELINING");
				lmtp.HrResponse("250-8BITMIME");
				lmtp.HrResponse("250-ENHANCEDSTATUSCODE");
				lmtp.HrResponse("250-RSET");
				lmtp.HrResponse("250 SMTPUTF8");
			} else {
				lmtp.HrResponse("501 5.5.4 Syntax: LHLO hostname");
				lpArgs->sc->inc(SCN_LMTP_LHLO_FAIL);
			}
			break;

		case LMTP_Command_MAIL_FROM:
			// @todo, if this command is received a second time, repond: 503 5.5.1 Error: nested MAIL command
			if (lmtp.HrCommandMAILFROM(inBuffer, curFrom) != hrSuccess) {
				lmtp.HrResponse("503 5.1.7 Bad sender's mailbox address syntax");
				lpArgs->sc->inc(SCN_LMTP_BAD_SENDER_ADDRESS);
			}
			else {
				lmtp.HrResponse("250 2.1.0 Ok");
			}
			break;

		case LMTP_Command_RCPT_TO: {
			if (lmtp.HrCommandRCPTTO(inBuffer, strMailAddress) != hrSuccess) {
				lmtp.HrResponse("503 5.1.3 Bad destination mailbox address syntax");
				lpArgs->sc->inc(SCN_LMTP_BAD_RECIP_ADDR);
				break;
			}
			auto lpRecipient = new ECRecipient(strMailAddress);
			// Resolve the mail address, so to have a user name instead of a mail address
			hr = ResolveUser(lpAddrDir, lpRecipient);
			if (hr == hrSuccess) {
				// This is the status until it is delivered or some other error occurs
				lpRecipient->wstrDeliveryStatus = "450 4.2.0 %s Mailbox temporarily unavailable";
				hr = AddServerRecipient(&mapRCPT, &lpRecipient);
				if (hr != hrSuccess)
					lmtp.HrResponse("503 5.1.1 Failed to add user to recipients");
				else {
					// Save original order for final response when mail is delivered in DATA command
					lOrderedRecipients.emplace_back(strMailAddress);
					lmtp.HrResponse("250 2.1.5 Ok");
				}
			} else if (hr == MAPI_E_NOT_FOUND) {
				if (lpRecipient->ulResolveFlags == MAPI_AMBIGUOUS) {
					ec_log_err("Requested e-mail address \"%s\" resolves to multiple users.", strMailAddress.c_str());
					lmtp.HrResponse("503 5.1.4 Destination mailbox address ambiguous");
				} else {
					ec_log_err("Requested e-mail address \"%s\" does not resolve to a user.", strMailAddress.c_str());
					lmtp.HrResponse("503 5.1.1 User does not exist");
				}
			} else {
				kc_perror("Failed to lookup email address", hr);
				lmtp.HrResponse("503 5.1.1 Connection error: " + stringify_hex(hr));
			}

			/*
			 * If recipient resolving failed, we need to free the recipient structure,
			 * only when the structure was added to the mapRCPT will it be freed automatically
			 * later during email delivery.
			 */
			delete lpRecipient;
			break;
		}

		case LMTP_Command_DATA: {
			if (mapRCPT.empty()) {
				lmtp.HrResponse("503 5.1.1 No recipients");
				lpArgs->sc->inc(SCN_LMTP_NO_RECIP);
				break;
			}

			FILE *tmp = tmpfile();
			if (!tmp) {
				lmtp.HrResponse("503 5.1.1 Internal error during delivery");
				ec_log_err("Unable to create temp file for email delivery. Please check write-access in /tmp directory. Error: %s", strerror(errno));
				lpArgs->sc->inc(SCN_LMTP_TMPFILEFAIL);
				break;
			}

			add_misc_headers(tmp, heloName, curFrom, lpArgs.get());
			hr = lmtp.HrCommandDATA(tmp);
			if (hr == hrSuccess) {
				std::unique_ptr<pym_plugin_intf> ptrPyMapiPlugin;
				hr = pyMapiPluginFactory.create_plugin(g_lpConfig.get(), "DAgentPluginManager", &unique_tie(ptrPyMapiPlugin));
				if (hr != hrSuccess) {
					ec_log_crit("K-1731: Unable to initialize the dagent plugin manager: %s (%x).",
						GetMAPIErrorMessage(hr), hr);
					lmtp.HrResponse("503 5.1.1 Internal error during delivery");
					lpArgs->sc->inc(SCN_LMTP_INTERNAL_ERROR);
					fclose(tmp);
					hr = hrSuccess;
					break;
				}

				// During delivery lpArgs->ulDeliveryMode can be set to DM_JUNK. However it won't reset it
				// if required. So make sure to reset it here so we can safely reuse the LMTP connection
				delivery_mode ulDeliveryMode = lpArgs->ulDeliveryMode;
				ProcessDeliveryToList(ptrPyMapiPlugin.get(), lpSession, tmp, &mapRCPT, lpArgs.get());
				lpArgs->ulDeliveryMode = ulDeliveryMode;
			}

			// We are not that interested in the error value here; if an error occurs, then this will be reflected in the
			// wstrDeliveryStatus of each recipient.
			hr = hrSuccess;

			/* Responses need to be sent in the same sequence that we received the recipients in.
			 * Build all responses and find the sequence through the ordered list
			 */
			auto rawmsg = g_lpConfig->GetSetting("log_raw_message");
			auto save_all = parseBool(rawmsg) && (strcasecmp(rawmsg, "all") == 0 || strcasecmp(rawmsg, "yes") == 0);
			auto save_error = strcasecmp(rawmsg, "error") == 0 && lpArgs->got_error;
			if (save_all || save_error)
				SaveRawMessage(tmp, "LMTP", lpArgs.get());

			for (const auto &company : mapRCPT)
				for (const auto &server : company.second)
					for (const auto &recip : server.second) {
						char wbuffer[4096];
						for (const auto &i : recip->vwstrRecipients) {
							static_assert(std::is_same<decltype(recip->wstrDeliveryStatus.c_str()), decltype(i.c_str())>::value, "need compatible types");
							snprintf(wbuffer, ARRAY_SIZE(wbuffer), recip->wstrDeliveryStatus.c_str(), i.c_str());
							mapRecipientResults.emplace(i, wbuffer);
							if (save_all || save_error)
								continue;
							auto save_username = converter.convert_to<std::string>(recip->wstrUsername);
							SaveRawMessage(tmp, save_username.c_str(), lpArgs.get());
						}
					}

			fclose(tmp);

			// Reply each recipient in the received order
			for (const auto &i : lOrderedRecipients) {
				std::map<std::string, std::string>::const_iterator r = mapRecipientResults.find(i);
				if (r == mapRecipientResults.cend()) {
					// FIXME if a following item from lORderedRecipients does succeed, then this error status
					// is forgotten. is that ok? (FvH)
					hr = lmtp.HrResponse("503 5.1.1 Internal error while searching recipient delivery status");
					lpArgs->sc->inc(SCN_LMTP_INTERNAL_ERROR);
				}
				else {
					hr = lmtp.HrResponse(r->second);
				}
				if (hr != hrSuccess)
					break;
			}

			lpArgs->sc->inc(SCN_LMTP_RECEIVED);
			// Reset RCPT TO list now
			FreeServerRecipients(&mapRCPT);
			lOrderedRecipients.clear();
			mapRecipientResults.clear();
			break;
		}

		case LMTP_Command_RSET:
			// Reset RCPT TO list
			FreeServerRecipients(&mapRCPT);
			lOrderedRecipients.clear();
			mapRecipientResults.clear();
			curFrom.clear();
			lmtp.HrResponse("250 2.1.0 Ok");
			break;

		case LMTP_Command_QUIT:
			lmtp.HrResponse("221 2.0.0 Bye");
			bLMTPQuit = true;
			break;
		}
	}

	if (g_process_model == GP_THREAD)
		--g_nLMTPThreads;
	return NULL;
}

/**
 * Runs the LMTP service daemon. Listens on the LMTP port for incoming
 * connections and starts a new thread or child process to handle the
 * connection.  Only accepts the incoming connection when the maximum
 * number of processes hasn't been reached.
 *
 * @param[in]	lpArgs		Struct containing delivery parameters
 * @retval MAPI error code
 */
static int dagent_listen(ECConfig *cfg, std::vector<struct pollfd> &pollers,
    std::vector<int> &closefd)
{
	auto lmtp_info = ec_bindspec_to_sockets(tokenize(cfg->GetSetting("lmtp_listen"), ' ', true),
	                 S_IRWUGO, cfg->GetSetting("run_as_user"), cfg->GetSetting("run_as_group"));
	if (lmtp_info.first < 0) {
		ec_log_err("Socket binding failed");
		return lmtp_info.first;
	}
	auto &lmtp_sock = lmtp_info.second;
	struct pollfd x;
	memset(&x, 0, sizeof(x));
	x.events = POLLIN;
	pollers.reserve(lmtp_sock.size());
	closefd.reserve(lmtp_sock.size());
	for (auto &spec : lmtp_sock) {
		x.fd = spec.m_fd;
		spec.m_fd = -1;
		pollers.push_back(x);
		closefd.push_back(x.fd);
	}
	return 0;
}

static HRESULT running_service(char **argv, DeliveryArgs *lpArgs)
{
	HRESULT hr = hrSuccess;
	unsigned int nMaxThreads;

	ec_log_always("Starting kopano-dagent version " PROJECT_VERSION " (pid %d uid %u) (LMTP mode)", getpid(), getuid());
	auto laters = make_scope_success([&]() { ECChannel::HrFreeCtx(); });

	// Setup sockets
	std::vector<struct pollfd> lmtp_poll;
	std::vector<int> closefd;
	auto err = dagent_listen(g_lpConfig.get(), lmtp_poll, closefd);
	if (err < 0)
		return MAPI_E_NETWORK_ERROR;
	err = unix_runas(g_lpConfig.get());
	if (err < 0) {
		return MAPI_E_CALL_FAILED;
	} else if (err == 0) {
		ec_reexec_finalize();
	} else if (err > 0) {
		ec_reexec_prepare_sockets();
		err = ec_reexec(argv);
		if (err < 0)
			ec_log_notice("K-1240: Failed to re-exec self: %s. "
				"Continuing with restricted coredumps.", strerror(-err));
	}

	// Setup signals
	struct sigaction act{};
	sigemptyset(&act.sa_mask);
	act.sa_flags   = SA_ONSTACK | SA_RESETHAND;
	act.sa_handler = da_sigterm_async;
	sigaction(SIGTERM, &act, nullptr);
	sigaction(SIGINT, &act, nullptr);
	act.sa_flags   = SA_ONSTACK;
	act.sa_handler = da_sigchld_async;
	sigaction(SIGCHLD, &act, nullptr);

	unix_create_pidfile(argv[0], g_lpConfig.get());
	if (g_process_model == GP_FORK)
		g_lpLogger = StartLoggerProcess(g_lpConfig.get(), std::move(g_lpLogger)); // maybe replace logger
	ec_log_set(g_lpLogger);

	nMaxThreads = atoui(g_lpConfig->GetSetting("lmtp_max_threads"));
	if (nMaxThreads == 0 || nMaxThreads == INT_MAX)
		nMaxThreads = 20;
	ec_log_info("Maximum LMTP threads set to %d", nMaxThreads);

	AutoMAPI mapiinit;
	hr = mapiinit.Initialize();
	if (hr != hrSuccess) {
		ec_log_crit("Unable to initialize MAPI: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	auto sc = std::make_shared<dagent_stats>(g_lpConfig);
	pthread_attr_t thr_attr;
	pthread_attr_init(&thr_attr);
	pthread_attr_setdetachstate(&thr_attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&thr_attr, 1 << 20 /* 1 MB */);

	// Mainloop
	while (!g_bQuit) {
		if (g_sighup_flag)
			da_sighup_sync();
		for (size_t i = 0; i < lmtp_poll.size(); ++i)
			lmtp_poll[i].revents = 0;
		err = poll(&lmtp_poll[0], lmtp_poll.size(), 10 * 1000);
		if (err < 0) {
			if (errno != EINTR) {
				ec_log_err("Socket error: %s", strerror(errno));
				g_bQuit = true;
				hr = MAPI_E_NETWORK_ERROR;
			}

			continue;
		} else if (err == 0) {
			continue;
		}

		for (size_t i = 0; i < lmtp_poll.size(); ++i) {
			if (!(lmtp_poll[i].revents & POLLIN))
				/* OS might set more bits than requested */
				continue;

			// don't start more "threads" that lmtp_max_threads config option
			if (g_nLMTPThreads == nMaxThreads) {
				sc->inc(SCN_DAGENT_MAX_THREAD_COUNT);
				Sleep(100);
				break;
			}

			// One socket has signalled a new incoming connection
			auto da = std::make_unique<DeliveryArgs>(*lpArgs);
			hr = HrAccept(lmtp_poll[i].fd, &unique_tie(da->lpChannel));
			if (hr != hrSuccess) {
				kc_perrorf("HrAccept failed", hr);
				hr = hrSuccess;
				continue;
			}
			sc->inc(SCN_DAGENT_INCOMING_SESSION);
			da->sc = sc;
			if (g_process_model == GP_FORK) {
				++g_nLMTPThreads;
				if (unix_fork_function(HandlerLMTP, da.get(), closefd.size(), &closefd[0]) < 0) {
					ec_log_err("Can't create LMTP process.");
					--g_nLMTPThreads;
				}
				continue;
			} else if (g_process_model == GP_SINGLE) {
				HandlerLMTP(da.release());
				continue;
			}
			pthread_t tid;
			++g_nLMTPThreads;
			err = pthread_create(&tid, &thr_attr, HandlerLMTP, da.get());
			if (err != 0) {
				--g_nLMTPThreads;
				ec_log_err("Could not create LMTP thread: %s", strerror(err));
				continue;
			}
			da.release();
			continue;
		}
	}

	ec_log_always("LMTP service will now exit");
	if (g_process_model == GP_FORK) {
		signal(SIGTERM, SIG_IGN);
		kill(0, SIGTERM);
	}

	// wait max 30 seconds
	for (int i = 30; g_nLMTPThreads && i; --i) {
		if (i % 5 == 0)
			ec_log_debug("Waiting for %d processes/threads to terminate", g_nLMTPThreads.load());
		sleep(1);
	}
	if (g_nLMTPThreads)
		ec_log_notice("Forced shutdown with %d processes/threads left", g_nLMTPThreads.load());
	else
		ec_log_info("LMTP service shutdown complete");
	return hr;
}

/**
 * Commandline delivery for one given user.
 *
 * Deliver the main received from <stdin> to the user named in the
 * recipient parameter. Valid recipient input is a username in current
 * locale, or the emailadress of the user.
 *
 * @param[in]	recipient	Username or emailaddress of a user, in current locale.
 * @param[in]	bStringEmail	true if we should strip everything from the @ to get the name to deliver to.
 * @param[in]	file		Filepointer to start of email.
 * @param[in]	lpArgs		Delivery arguments, according to given options on the commandline.
 * @return		MAPI Error code.
 */
static HRESULT deliver_recipient(pym_plugin_intf *lppyMapiPlugin,
    const char *recipient, bool bStringEmail, FILE *fpMail,
    DeliveryArgs *lpArgs)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPISession> lpSession;
	object_ptr<IAddrBook> lpAdrBook;
	object_ptr<IABContainer> lpAddrDir;
	recipients_t lRCPT;
	std::string strUsername = recipient;

	if (bStringEmail)
		// we have to strip off the @domainname.tld to get the username
		strUsername = strUsername.substr(0, strUsername.find_first_of("@"));

	ECRecipient single_recip(strUsername);

	// Always try to resolve the user unless we just stripped an email address.
	if (!bStringEmail) {
		// only suppress error when it has no meaning (e.g. delivery of Unix user to itself)
		hr = HrGetSession(lpArgs, KOPANO_SYSTEM_USER_W, &~lpSession, !lpArgs->bResolveAddress);
		if (hr == hrSuccess) {
			hr = OpenResolveAddrFolder(lpSession, &~lpAdrBook, &~lpAddrDir);
			if (hr != hrSuccess)
				return kc_perrorf("OpenResolveAddrFolder failed", hr);
			hr = ResolveUser(lpAddrDir, &single_recip);
			if (hr != hrSuccess) {
				if (hr == MAPI_E_NOT_FOUND)
					g_bTempfail = false;
				return kc_perrorf("ResolveUser failed", hr);
			}
		}
		else if (lpArgs->bResolveAddress) {
			// Failure to open the admin session will only result in error if resolve was requested.
			// Non fatal, so when config is changes the message can be delivered.
			return hr;
		}
		else {
			// set commandline user in resolved name to deliver without resolve function
			single_recip.wstrUsername = convert_to<std::wstring>(single_recip.wstrRCPT);
		}
	}
	else {
		// set commandline user in resolved name to deliver without resolve function
		single_recip.wstrUsername = convert_to<std::wstring>(single_recip.wstrRCPT);
	}

	hr = HrGetSession(lpArgs, single_recip.wstrUsername.c_str(), &~lpSession);
	if (hr != hrSuccess) {
		if (hr == MAPI_E_LOGON_FAILED)
			// This is a hard failure, two things could have happened
			// * strUsername does not exist
			// * user does exist, but dagent is not running with the correct SYSTEM privileges, or user doesn't have a store
			// Since we cannot detect the difference, we're handling both of these situations
			// as hard errors
			g_bTempfail = false;
		return kc_perrorf("HrGetSession failed", hr);
	}

	hr = OpenResolveAddrFolder(lpSession, &~lpAdrBook, &~lpAddrDir);
	if (hr != hrSuccess)
		return kc_perrorf("OpenResolveAddrFolder failed", hr);
	lRCPT.emplace(&single_recip);
	hr = ProcessDeliveryToSingleRecipient(lppyMapiPlugin, lpSession, lpAdrBook, fpMail, lRCPT, lpArgs);
	// Over quota is a hard error
	if (hr == MAPI_E_STORE_FULL)
	    g_bTempfail = false;
	// Save copy of the raw message
	SaveRawMessage(fpMail, recipient, lpArgs);
	return hr;
}

static HRESULT deliver_recipients(pym_plugin_intf *py_plugin,
    unsigned int nrecip, char **recip, bool strip_em, FILE *file,
    DeliveryArgs *args)
{
	HRESULT func_ret = hrSuccess;
	args->sc->inc(SCN_DAGENT_STDIN_RECEIVED);
	FILE *fpmail = nullptr;
	auto ret = HrFileLFtoCRLF(file, &fpmail);
	if (ret != hrSuccess) {
		ec_log_warn("Unable to convert input to CRLF format: %s (%x)", GetMAPIErrorMessage(ret), ret);
		fpmail = file;
	}
	for (unsigned int ridx = 0; ridx < nrecip; ++ridx) {
		ret = deliver_recipient(py_plugin, recip[ridx], strip_em, fpmail, args);
		if (ret != hrSuccess && func_ret == hrSuccess)
			func_ret = ret;
	}
	if (fpmail != file)
		fclose(fpmail);
	return func_ret;
}

static HRESULT direct_delivery(int argc, char **argv,
    DeliveryArgs &&sDeliveryArgs, FILE *fp, bool strip_email)
{
	PyMapiPluginFactory pyMapiPluginFactory;
	std::unique_ptr<pym_plugin_intf> ptrPyMapiPlugin;
	AutoMAPI mapiinit;
	auto hr = mapiinit.Initialize();
	if (hr != hrSuccess) {
		ec_log_crit("Unable to initialize MAPI: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	auto sc = std::make_shared<dagent_stats>(g_lpConfig);
	sDeliveryArgs.sc = std::move(sc);
	hr = pyMapiPluginFactory.create_plugin(g_lpConfig.get(), "DAgentPluginManager", &unique_tie(ptrPyMapiPlugin));
	if (hr != hrSuccess) {
		ec_log_crit("K-1732: Unable to initialize the dagent plugin manager: %s (%x).",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}
	hr = deliver_recipients(ptrPyMapiPlugin.get(), argc - optind, argv + optind, strip_email, fp, &sDeliveryArgs);
	if (hr != hrSuccess)
		kc_perrorf("deliver_recipient failed", hr);
	fclose(fp);
	return hr;
}

static void print_help(const char *name)
{
	cout << "Usage:\n" << endl;
	cout << name << " <recipient>" << endl;
	cout << " [-h|--host <serverpath>] [-c|--config <configfile>] [-f|--file <email-file>]" << endl;
	cout << " [-j|--junk] [-F|--folder <foldername>] [-P|--public <foldername>] [-p <separator>] [-C|--create]" << endl;
	cout << " [-l|--listen] [-r|--read] [-s] [-v] [-q] [-e] [-n] [-R]" << endl;
	cout << endl;
	cout << "  <recipient> Username or e-mail address of recipient" << endl;
	cout << "  -f file\t read e-mail from file" << endl;
	cout << "  -h path\t path to connect to (e.g. file:///var/run/socket)" << endl;
	cout << "  -c filename\t Use configuration file (e.g. /etc/kopano/dagent.cfg)\n\t\t Default: no config file used." << endl;
	cout << "  -j\t\t deliver in Junkmail" << endl;
	cout << "  -F foldername\t deliver in a subfolder of the store. Eg. 'Inbox\\sales'" << endl;
	cout << "  -P foldername\t deliver in a subfolder of the public store. Eg. 'sales\\incoming'" << endl;
	cout << "  -p separator\t Override default path separator (\\). Eg. '-p % -F 'Inbox%dealers\\resellers'" << endl;
	cout << "  -C\t\t Create the subfolder if it does not exist. Default behaviour is to revert to the normal Inbox folder" << endl;
	cout << endl;
	cout << "  -s\t\t Make DAgent silent. No errors will be printed, except when the calling parameters are wrong." << endl;
	cout << "  -v\t\t Make DAgent verbose. More information on processing email rules can be printed." << endl;
	cout << "  -q\t\t Return qmail style errors." << endl;
	cout << "  -e\t\t Strip email domain from storename, eg username@domain.com will deliver to 'username'." << endl;
	cout << "  -R\t\t Attempt to resolve the passed name. Issue an error if the resolve fails. Only one of -e and -R may be specified." << endl;
	cout << "  -n\t\t Use 'now' as delivery time. Otherwise, time from delivery at the mailserver will be used." << endl;
	cout << "  -N\t\t Do not send a new mail notification to clients looking at this inbox. (Fixes Outlook 2000 running rules too)." << endl;
	cout << "  -r\t\t Mark mail as read on delivery. Default: mark mail as new unread mail." << endl;
	cout << "  -l\t\t Run DAgent as LMTP listener" << endl;
	cout << endl;
	cout << "  -a responder\t path to autoresponder (e.g. /usr/local/bin/autoresponder)" << endl;
	cout << "\t\t The autoresponder is called with </path/to/autoresponder> <from> <to> <subject> <kopano-username> <messagefile>" << endl;
	cout << "\t\t when the autoresponder is enabled for this user, and -j is not specified" << endl;
	cout << endl;
	cout << "<storename> is the name of the user where to deliver this mail." << endl;
	cout << "If no file is specified with -f, it will be read from standard in." << endl;
	cout << endl;
}

static int get_return_value(HRESULT hr, bool listen_lmtp, bool qmail)
{
	if (hr == hrSuccess || listen_lmtp)
		return EX_OK;
	if (g_bTempfail)
		// please retry again later.
		return qmail ? 111 : EX_TEMPFAIL;
	// fatal error, mail was undelivered (or Fallback delivery, but still return an error)
	return qmail ? 100 : EX_SOFTWARE;
}

int main(int argc, char **argv) try {
	FILE *fp = stdin;
	HRESULT hr = hrSuccess;
	bool bDefaultConfigWarning = false; // Provide warning when default configuration is used
	bool bExplicitConfig = false; // User added config option to commandline
	bool bListenLMTP = false; // Do not listen for LMTP by default
	bool qmail = false, strip_email = false;
	int loglevel = EC_LOGLEVEL_WARNING;	// normally, log warnings and up
	bool bIgnoreUnknownConfigOptions = false;
	struct sigaction act;
	struct rlimit file_limit;
	memset(&act, 0, sizeof(act));

	DeliveryArgs sDeliveryArgs;
		const char *szConfig = ECConfig::GetDefaultPath("dagent.cfg");

	enum {
		OPT_HELP = UCHAR_MAX + 1,
		OPT_CONFIG,
		OPT_JUNK,
		OPT_FILE,
		OPT_HOST,
		OPT_DAEMONIZE,
		OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS,
		OPT_LISTEN,
		OPT_FOLDER,
		OPT_PUBLIC,
		OPT_CREATE,
		OPT_MARKREAD,
		OPT_NEWMAIL,
		OPT_DUMP_CONFIG,
	};
	static const struct option long_options[] = {
		{ "help", 0, NULL, OPT_HELP },	// help text
		{ "config", 1, NULL, OPT_CONFIG },	// config file
		{ "junk", 0, NULL, OPT_JUNK },	// junk folder
		{ "file", 1, NULL, OPT_FILE },	// file as input
		{ "host", 1, NULL, OPT_HOST },	// kopano host parameter
		{ "daemonize",0 ,NULL,OPT_DAEMONIZE}, // daemonize and listen for LMTP
		{ "listen", 0, NULL, OPT_LISTEN},   // listen for LMTP
		{ "folder", 1, NULL, OPT_FOLDER },	// subfolder of store to deliver in
		{ "public", 1, NULL, OPT_PUBLIC },	// subfolder of public to deliver in
		{ "create", 0, NULL, OPT_CREATE },	// create subfolder if not exist
		{ "read", 0, NULL, OPT_MARKREAD },	// mark mail as read on delivery
		{ "do-not-notify", 0, NULL, OPT_NEWMAIL },	// do not send new mail notification
		{ "ignore-unknown-config-options", 0, NULL, OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS }, // ignore unknown settings
		{"dump-config", 0, nullptr, OPT_DUMP_CONFIG},
		{ NULL, 0, NULL, 0 }
	};
	// Default settings
	static const configsetting_t lpDefaults[] = {
		{"server_bind_intf", "", CONFIGSETTING_OBSOLETE},
		{ "run_as_user", "kopano" },
		{ "run_as_group", "kopano" },
		{ "pid_file", "/var/run/kopano/dagent.pid" },
		{"coredump_enabled", "systemdefault"},
		{"lmtp_listen", "*%lo:2003"},
		{ "lmtp_max_threads", "20" },
		{"process_model", "thread", CONFIGSETTING_NONEMPTY},
		{"log_method", "auto", CONFIGSETTING_NONEMPTY},
		{"log_file", ""},
		{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{"log_timestamp", "yes"},
		{ "log_buffer_size", "0" },
		{ "server_socket", "default:" },
		{ "sslkey_file", "" },
		{ "sslkey_pass", "", CONFIGSETTING_EXACT },
		{ "spam_header_name", "X-Spam-Status" },
		{ "spam_header_value", "Yes," },
		{ "log_raw_message", "error", CONFIGSETTING_RELOADABLE },
		{"log_raw_message_path", "/var/lib/kopano", CONFIGSETTING_RELOADABLE},
		{ "archive_on_delivery", "no", CONFIGSETTING_RELOADABLE },
		{ "mr_autoaccepter", "/usr/sbin/kopano-mr-accept", CONFIGSETTING_RELOADABLE },
		{ "mr_autoprocessor", "/usr/sbin/kopano-mr-process", CONFIGSETTING_RELOADABLE },
		{ "autoresponder", "/usr/sbin/kopano-autorespond", CONFIGSETTING_RELOADABLE },
		{"plugin_enabled", "no"},
		{ "plugin_path", "/var/lib/kopano/dagent/plugins" },
		{ "plugin_manager_path", "/usr/share/kopano-dagent/python" },
		{ "default_charset", "us-ascii"},
		{"insecure_html_join", "no", CONFIGSETTING_RELOADABLE},
		{ "set_rule_headers", "yes", CONFIGSETTING_RELOADABLE },
		{ "no_double_forward", "yes", CONFIGSETTING_RELOADABLE },
		{"statsclient_url", "unix:/var/run/kopano/statsd.sock", CONFIGSETTING_RELOADABLE},
		{"statsclient_interval", "0", CONFIGSETTING_RELOADABLE},
		{"statsclient_ssl_verify", "yes", CONFIGSETTING_RELOADABLE},
		{ "tmp_path", "/tmp" },
		{"forward_whitelist_domains", "*", CONFIGSETTING_RELOADABLE},
		{"forward_whitelist_domains_file", "", CONFIGSETTING_RELOADABLE},
		{"forward_whitelist_domain_message", "The Kopano mail system has rejected your "
		 "request to forward your e-mail with subject \"%subject\" (via mail filters) "
		 "to %sender: the operation is not permitted.\n\nRemove the rule or contact "
		 "your administrator about the forward_whitelist_domains setting.\n", CONFIGSETTING_RELOADABLE},
		{"forward_whitelist_domain_message_file", "", CONFIGSETTING_RELOADABLE},
		{"forward_whitelist_domain_subject", "REJECT: %subject not forwarded (administratively blocked)", CONFIGSETTING_RELOADABLE},
		{"html_safety_filter", "no"},
		{"unknown_charset_substitutions", ""},
		{"indexed_headers", ""},
		{"conversion_detail", "no"},
		{ NULL, NULL },
	};

	forceUTF8Locale(true);
	if (argc < 2) {
		print_help(argv[0]);
		return EX_USAGE;
	}

	while (1) {
		auto c = my_getopt_long_permissive(argc, argv, "c:jf:h:a:F:P:p:qsvenCVrRlN", long_options, nullptr);
		if (c == -1)
			break;
		switch (c) {
		case OPT_CONFIG:
		case 'c':
			szConfig = optarg;
			bExplicitConfig = true;
			break;
		case OPT_JUNK:
		case 'j':				// junkmail
			sDeliveryArgs.ulDeliveryMode = DM_JUNK;
			break;
		case OPT_FILE:
		case 'f':				// use file as input
			bListenLMTP = false;
			fp = fopen(optarg, "rb");
			if(!fp) {
				cerr << "Unable to open file '" << optarg << "' for reading" << endl;
				return EX_USAGE;
			}
			break;
		case OPT_LISTEN:
		case 'l':
			bListenLMTP = true;
			break;
		case OPT_HOST:
		case 'h':				// 'host' (file:///var/run/kopano/server.sock)
			sDeliveryArgs.strPath = optarg;
			break;
		case 'a':				// external autoresponder program
			sDeliveryArgs.strAutorespond = optarg;
			break;
		case 'q':				// use qmail errors
			qmail = true;
			break;
		case 's':				// silent, no logging
			loglevel = EC_LOGLEVEL_NONE;
			break;
		case 'v':				// verbose logging
			if (loglevel == EC_LOGLEVEL_INFO)
				loglevel = EC_LOGLEVEL_DEBUG;
			else
				loglevel = EC_LOGLEVEL_INFO;
			break;
		case 'e':				// strip @bla.com from username
			strip_email = true;
			break;
		case 'n':
			sDeliveryArgs.sDeliveryOpts.use_received_date = false;	// conversion will use now()
			break;
		case OPT_FOLDER:
		case 'F':
			sDeliveryArgs.strDeliveryFolder = convert_to<wstring>(optarg);
			break;
		case OPT_PUBLIC:
		case 'P':
			sDeliveryArgs.ulDeliveryMode = DM_PUBLIC;
			sDeliveryArgs.strDeliveryFolder = convert_to<wstring>(optarg);
			break;
		case 'p':
			sDeliveryArgs.szPathSeparator = optarg[0];
			break;
		case OPT_CREATE:
		case 'C':
			sDeliveryArgs.bCreateFolder = true;
			break;
		case OPT_MARKREAD:
		case 'r':
			sDeliveryArgs.sDeliveryOpts.mark_as_read = true;
			break;
		case OPT_NEWMAIL:
		case 'N':
			sDeliveryArgs.bNewmailNotify = false;
			break;
		case OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS:
			bIgnoreUnknownConfigOptions = true;
			break;
		case OPT_DUMP_CONFIG:
			g_dump_config = true;
			break;
		case 'V':
			cout << "kopano-dagent " PROJECT_VERSION << endl;
			return EX_USAGE;
		case 'R':
			sDeliveryArgs.bResolveAddress = true;
			break;
		case OPT_HELP:
		default:
			print_help(argv[0]);
			return EX_USAGE;
		};
	}

	g_lpConfig.reset(ECConfig::Create(lpDefaults));
	/* When LoadSettings fails, provide warning to user (but wait until we actually have the Logger) */
	if (!g_lpConfig->LoadSettings(szConfig))
		bDefaultConfigWarning = true;
	else {
		auto argidx = g_lpConfig->ParseParams(argc - optind, &argv[optind]);
		if (argidx < 0)
			return get_return_value(hr, bListenLMTP, qmail);
		if (argidx > 0)
			// If one overrides the config, it is assumed that the
			// config is explicit. This causes errors from
			// ECConfig::ParseParams to be logged. Besides that
			// it doesn't make sense to override your config if
			// you don't know what's in it.
			bExplicitConfig = true;

		// ECConfig::ParseParams returns the index in the passed array,
		// after some shuffling, where it stopped parsing. optind is
		// the index where my_getopt_long_permissive stopped parsing. So
		// adding argidx to optind will result in the index after all
		// options are parsed.
		optind += argidx;
	}
	if (!bListenLMTP && optind == argc) {
		cerr << "Not enough options given, need at least the username" << endl;
		return EX_USAGE;
	}
	if (strip_email && sDeliveryArgs.bResolveAddress) {
		cerr << "You must specify either -e or -R, not both" << endl;
		return EX_USAGE;
	}

	if (!loglevel)
		g_lpLogger.reset(new(std::nothrow) ECLogger_Null);
	else
		g_lpLogger = CreateLogger(g_lpConfig.get(), argv[0], "KopanoDAgent");
	ec_log_set(g_lpLogger);
	if (!g_lpLogger->Log(loglevel))
		/* raise loglevel if there are more -v on the command line than in dagent.cfg */
		g_lpLogger->SetLoglevel(loglevel);

	/* Warn users that we are using the default configuration */
	if (bDefaultConfigWarning && bExplicitConfig) {
		ec_log_err("Unable to open configuration file %s", szConfig);
		ec_log_err("Continuing with defaults");
	}

	if ((bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors()) || g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig.get());
	if (!TmpPath::instance.OverridePath(g_lpConfig.get()))
		ec_log_err("Ignoring invalid path-setting!");
	/* If something went wrong, create special Logger, log message and bail out */
	if (g_lpConfig->HasErrors() && bExplicitConfig) {
		LogConfigErrors(g_lpConfig.get());
		return get_return_value(E_FAIL, bListenLMTP, qmail);
	}
	if (g_dump_config)
		return g_lpConfig->dump_config(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

	g_main_thread = pthread_self();
	auto model = g_lpConfig->GetSetting("process_model");
	if (strcmp(model, "thread") == 0)
		g_process_model = GP_THREAD;
	else if (strcmp(model, "fork") == 0)
		g_process_model = GP_FORK;
	else if (strcmp(model, "single") == 0)
		g_process_model = GP_SINGLE;
	if (g_process_model == GP_THREAD &&
	    parseBool(g_lpConfig->GetSetting("plugin_enabled"))) {
		/*
		 * Though you can create multiple interpreters, they
		 * cannot run simultaneously, defeating the purpose.
		 */
		ec_log_err("Use of Python (plugin_enabled=yes) forces process_model=fork");
		g_process_model = GP_FORK;
	}
	if (g_process_model == GP_THREAD)
		g_lpLogger->SetLogprefix(LP_TID);
	else if (!bListenLMTP)
		// log process id prefix to distinguinsh events, file logger only affected
		g_lpLogger->SetLogprefix(LP_PID);

	/* When path wasn't provided through commandline, resolve it from config file */
	if (sDeliveryArgs.strPath.empty())
		sDeliveryArgs.strPath = g_lpConfig->GetSetting("server_socket");
	sDeliveryArgs.strPath = GetServerUnixSocket((char*)sDeliveryArgs.strPath.c_str()); // let environment override if present
	sDeliveryArgs.sDeliveryOpts.ascii_upgrade = g_lpConfig->GetSetting("default_charset");
	sDeliveryArgs.sDeliveryOpts.insecure_html_join = parseBool(g_lpConfig->GetSetting("insecure_html_join"));
	sDeliveryArgs.sDeliveryOpts.conversion_notices = parseBool(g_lpConfig->GetSetting("conversion_detail"));
#ifdef HAVE_TIDYBUFFIO_H
	sDeliveryArgs.sDeliveryOpts.html_safety_filter = strcasecmp(g_lpConfig->GetSetting("html_safety_filter"), "yes") == 0;
#else
	if (strcasecmp(g_lpConfig->GetSetting("html_safety_filter"), "yes") == 0)
		ec_log_warn("HTML safety filter is enabled in configuration, but KC is not compiled with libtidy");
#endif
	{
		auto s = g_lpConfig->GetSetting("unknown_charset_substitutions");
		if (s != nullptr) {
			auto t = tokenize(s, ' ', true);
			for (size_t i = 0; i + 1 < t.size(); i += 2)
				sDeliveryArgs.sDeliveryOpts.cset_subst[t[i]] = std::move(t[i+1]);
		}
	}
	{
		auto s = g_lpConfig->GetSetting("indexed_headers");
		if (s != nullptr) {
			auto l = tokenize(s, ' ', true);
			auto &headers = sDeliveryArgs.sDeliveryOpts.indexed_headers;

			if (l.size() > 0) {
				headers.clear();
				for (const auto &elem : l)
					headers.push_back(elem);
			}
		}
		if (parseBool(g_lpConfig->GetSetting("no_double_forward")))
			sDeliveryArgs.sDeliveryOpts.indexed_headers.push_back("x-kopano-rule-action");
	}

	signal(SIGPIPE, SIG_IGN);
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_ONSTACK;
	act.sa_handler = da_sighup_async;
	sigaction(SIGHUP, &act, nullptr);
	ec_setup_segv_handler("kopano-dagent", PROJECT_VERSION);
	file_limit.rlim_cur = KC_DESIRED_FILEDES;
	file_limit.rlim_max = KC_DESIRED_FILEDES;

	if (setrlimit(RLIMIT_NOFILE, &file_limit) < 0)
		ec_log_err("WARNING: setrlimit(RLIMIT_NOFILE, %d) failed: %s. "
			"You will only be able to connect up to %d sockets. "
			"Either start the process as root, "
			"or increase user limits for open file descriptors.",
			KC_DESIRED_FILEDES, strerror(errno), getdtablesize());
	unix_coredump_enable(g_lpConfig->GetSetting("coredump_enabled"));
	umask(S_IRWXG | S_IRWXO);

	if (parseBool(g_lpConfig->GetSetting("plugin_enabled"))) {
		std::string strEnvPython = g_lpConfig->GetSetting("plugin_manager_path");
		auto lpEnvPython = getenv("PYTHONPATH");
		if (lpEnvPython)
			strEnvPython += std::string(":") + lpEnvPython;
		setenv("PYTHONPATH", strEnvPython.c_str(), 1);
		ec_log_debug("PYTHONPATH = %s", strEnvPython.c_str());
	}

	if (bListenLMTP) {
		/* MAPIInitialize done inside running_service */
		hr = running_service(argv, &sDeliveryArgs);
		if (hr != hrSuccess)
			return get_return_value(hr, true, qmail);
	}
	else {
		hr = direct_delivery(argc, argv, std::move(sDeliveryArgs), fp, strip_email);
	}

	return get_return_value(hr, bListenLMTP, qmail);
} catch (...) {
	std::terminate();
}

dagent_stats::dagent_stats(std::shared_ptr<ECConfig> cfg) :
	StatsClient(std::move(cfg))
{
	set(SCN_PROGRAM_NAME, "kopano-dagent");
	AddStat(SCN_DAGENT_ATTACHMENT_COUNT, SCT_INTEGER, "dagent_attachment_count", "Number of attachments processed");
	AddStat(SCN_DAGENT_AUTOACCEPT, SCT_INTEGER, "dagent_autoaccept", "Number of meeting requests that underwent autoacceptance");
	AddStat(SCN_DAGENT_AUTOPROCESS, SCT_INTEGER, "dagent_autoprocess", "Number of meeting requests that underwent autoprocessing");
	AddStat(SCN_DAGENT_DELIVER_INBOX, SCT_INTEGER, "dagent_deliver_inbox", "Number of messages delivered to inboxes");
	AddStat(SCN_DAGENT_DELIVER_JUNK, SCT_INTEGER, "dagent_deliver_junk", "Number of messages delivered to junk folders");
	AddStat(SCN_DAGENT_DELIVER_PUBLIC, SCT_INTEGER, "dagent_deliver_public", "Number of messages delivered to public folders");
	AddStat(SCN_DAGENT_FALLBACKDELIVERY, SCT_INTEGER, "dagent_fallbackdelivery", "Number of messages that could not be parsed");
	AddStat(SCN_DAGENT_INCOMING_SESSION, SCT_INTEGER, "dagent_incoming_session", "Socket connections accepted");
	AddStat(SCN_DAGENT_IS_HAM, SCT_INTEGER, "dagent_is_ham", "Messages classified as ham");
	AddStat(SCN_DAGENT_IS_SPAM, SCT_INTEGER, "dagent_is_spam", "Messages classified as spam");
	AddStat(SCN_DAGENT_MAX_THREAD_COUNT, SCT_INTEGER, "dagent_thread_maxed");
	AddStat(SCN_DAGENT_MSG_EXPIRED, SCT_INTEGER, "dagent_msg_expired", "Messages that have expired at the time of processing");
	AddStat(SCN_DAGENT_MSG_NOT_EXPIRED, SCT_INTEGER, "dagent_msg_not_expired", "Messages that have not expired at the time of processing");
	AddStat(SCN_DAGENT_NWITHATTACHMENT, SCT_INTEGER, "dagent_nwithattachment", "Messages with attachments");
	AddStat(SCN_DAGENT_OUTOFOFFICE, SCT_INTEGER, "dagent_outofoffice", "Messages that triggered OOF");
	AddStat(SCN_DAGENT_RECIPS, SCT_INTEGER, "dagent_recips", "Number of recipients processed");
	AddStat(SCN_DAGENT_STDIN_RECEIVED, SCT_INTEGER, "dagent_stdin_received", "Number of mails processed from stdin");
	AddStat(SCN_DAGENT_STRINGTOMAPI, SCT_INTEGER, "dagent_stringtomapi");
	AddStat(SCN_DAGENT_TO_COMPANY, SCT_INTEGER, "dagent_to_company");
	AddStat(SCN_DAGENT_TO_LIST, SCT_INTEGER, "dagent_to_list");
	AddStat(SCN_DAGENT_TO_SERVER, SCT_INTEGER, "dagent_to_server");
	AddStat(SCN_DAGENT_TO_SINGLE_RECIP, SCT_INTEGER, "dagent_to_single_recip");
	AddStat(SCN_LMTP_BAD_RECIP_ADDR, SCT_INTEGER, "lmtp_bad_recip_addr", "Bad RCPT commands");
	AddStat(SCN_LMTP_BAD_SENDER_ADDRESS, SCT_INTEGER, "lmtp_bad_sender_addr", "Bad FROM commands");
	AddStat(SCN_LMTP_INTERNAL_ERROR, SCT_INTEGER, "lmtp_internal_error");
	AddStat(SCN_LMTP_LHLO_FAIL, SCT_INTEGER, "lmtp_lhlo_fail", "Bad LHLO commands");
	AddStat(SCN_LMTP_NO_RECIP, SCT_INTEGER, "lmtp_no_recip", "DATA without RCPT");
	AddStat(SCN_LMTP_RECEIVED, SCT_INTEGER, "lmtp_received");
	AddStat(SCN_LMTP_SESSIONS, SCT_INTEGER, "lmtp_session");
	AddStat(SCN_LMTP_TMPFILEFAIL, SCT_INTEGER, "lmtp_tmpfilefail");
	AddStat(SCN_LMTP_UNKNOWN_COMMAND, SCT_INTEGER, "lmtp_unknown_command", "Unknown commands issued");
	AddStat(SCN_RULES_BOUNCE, SCT_INTEGER, "rules_bounce", "OP_BOUNCE actions processed");
	AddStat(SCN_RULES_COPYMOVE, SCT_INTEGER, "rules_copymove", "OP_COPY/OP_MOVE actions processed");
	AddStat(SCN_RULES_DEFER, SCT_INTEGER, "rules_defer", "OP_DEFERs processed");
	AddStat(SCN_RULES_DELEGATE, SCT_INTEGER, "rules_delegate", "OP_DELEGATE actions processed");
	AddStat(SCN_RULES_DELETE, SCT_INTEGER, "rules_delete", "OP_DELETE actions processed");
	AddStat(SCN_RULES_FORWARD, SCT_INTEGER, "rules_forward", "OP_FORWARD actions processed");
	AddStat(SCN_RULES_INVOKES, SCT_INTEGER, "rules_invokes", "Messages subject to inbox rule processing");
	AddStat(SCN_RULES_INVOKES_FAIL, SCT_INTEGER, "rules_invokes_fail", "Messages with inbox rule processing errors");
	AddStat(SCN_RULES_MARKREAD, SCT_INTEGER, "rules_markread", "OP_MARK_READ actions processed");
	AddStat(SCN_RULES_NACTIONS, SCT_INTEGER, "rules_nactions", "Actions evaluated");
	AddStat(SCN_RULES_NRULES, SCT_INTEGER, "rules_nrules", "Rules evaluated");
	AddStat(SCN_RULES_REPLY_AND_OOF, SCT_INTEGER, "rules_reply_oof", "OP_REPLY/OP_REPLY_OOF actions processed");
	AddStat(SCN_RULES_TAG, SCT_INTEGER, "rules_tag", "OP_TAG actions processed");
}
