/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/*
 * This is the Kopano spooler.
 *
 *
 * The actual encoding is done by the inetmapi library.
 *
 * The spooler starts up, runs the queue once, and then
 * waits for changes in the outgoing queue. If any changes
 * occur, the whole queue is run again. This is done by having
 * an advise sink which is called when a table change is detected.
 * This advise sink unblocks the main (waiting) thread.
 */
#include <kopano/platform.h>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include "mailer.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <getopt.h>
#include <time.h>
#include <sys/stat.h>

#define USES_IID_IMAPIFolder
#define USES_IID_IMessage
#define USES_IID_IMsgStore

#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>
#include <mapiguid.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/ECGuid.h>
#include <kopano/EMSAbTag.h>
#include <kopano/ECTags.h>
#include <kopano/ECABEntryID.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include <kopano/UnixUtil.h>
#include <kopano/automapi.hpp>
#include <kopano/memory.hpp>
#include <kopano/ecversion.h>
#include <kopano/Util.h>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>
#include <kopano/tie.hpp>
#include <kopano/timeutil.hpp>
#include <kopano/mapiext.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/mapiguidext.h>
#include "mapicontact.h"
#include <kopano/charset/convert.h>
#include <kopano/charset/convstring.h>
#include <kopano/charset/utf8string.h>
#include <kopano/ECGetText.h>
#include "charset/localeutil.h"
#include "StatsClient.h"
#include <kopano/fileutil.hpp>
#include <map>

using namespace KC;
using namespace std::chrono_literals;
using std::cout;
using std::endl;
using std::map;
using std::string;
using std::wstring;

enum {
	SPC_EXITED,
	SPC_SIGNALED,
	SPC_ABNORMAL,
};

enum {
	GP_FORK,
	GP_THREAD,
	GP_SINGLE,
};

class spooler_stats final : public StatsClient {
	public:
	spooler_stats(std::shared_ptr<ECConfig>);
};

static std::unique_ptr<StatsClient> sc;

// spooler exit codes
#define EXIT_WAIT 2
#define EXIT_REMOVE 3

static unsigned int g_process_model = GP_FORK;
static bool bQuit = false, sp_exp_config, g_dump_config, g_sighup_flag, g_sigusr2_flag;
static int nReload = 0, disconnects = 0;
static const char *szCommand = NULL;
static const char *szConfig = ECConfig::GetDefaultPath("spooler.cfg");
extern std::shared_ptr<ECConfig> g_lpConfig;
std::shared_ptr<ECConfig> g_lpConfig;
static std::shared_ptr<ECLogger> g_lpLogger;
static pthread_t g_main_thread;

// notification
static bool bMessagesWaiting = false;
static std::mutex hMutexMessagesWaiting;
static std::condition_variable hCondMessagesWaiting;

// messages being processed
struct SendData {
	std::string store_eid, msg_eid;
	std::wstring strUsername;
	ULONG ulFlags;
	int status;
};

struct sp_handlerargs {
	struct SendData sd;
	std::string smtp_host, path;
	unsigned int smtp_port;
	bool do_sentmail;
};

static std::map<pid_t, SendData> mapSendData; /* data for subprocesses */
static std::list<SendData> g_senddata_thr; /* data for subthreads */
static map<pid_t, int> mapFinished;	// exit status of finished processes
static std::mutex g_senddata_mtx; /* protect g_senddata_thr */
static std::mutex hMutexFinished; /* mutex for mapFinished */

static HRESULT running_server(const char *szSMTP, int port, const char *szPath);
static HRESULT handle_child_exit(IMAPISession *, IECSpooler *, StatsClient *, pid_t, unsigned int, unsigned int, const SendData &);

/**
 * Print command line options, only for daemon version, not for mailer fork process
 *
 * @param[in]	name	name of the command
 */
static void print_help(const char *name)
{
	cout << "Usage:\n" << endl;
	cout << name << " [-F] [-h|--host <serverpath>] [-c|--config <configfile>] [smtp server]" << endl;
	cout << "  -F\t\tDo not run in the background" << endl;
	cout << "  -h path\tUse alternate connect path (e.g. file:///var/run/socket).\n\t\tDefault: file:///var/run/kopano/server.sock" << endl;
	cout << "  -V Print version info." << endl;
	cout << "  -c filename\tUse alternate config file (e.g. /etc/kopano-spooler.cfg)\n\t\tDefault: /etc/kopano/spooler.cfg" << endl;
	cout << "  smtp server: The name or IP-address of the SMTP server, overriding the configuration" << endl;
	cout << endl;
}

static void sp_sigterm_async(int)
{
	bQuit = true;
	hCondMessagesWaiting.notify_one();
}

static void sp_sigchld_async(int)
{
	int stat;
	pid_t pid;

	std::unique_lock<std::mutex> finlock(hMutexFinished);
	while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
		mapFinished[pid] = stat;
	finlock.unlock();
}

static void sp_sighup_async(int)
{
	if (g_process_model == GP_THREAD && !pthread_equal(pthread_self(), g_main_thread))
		return;
	g_sighup_flag = true;
	hCondMessagesWaiting.notify_one();
}

static void sp_sighup_sync()
{
	g_sighup_flag = false;
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

static void sp_sigusr2_async(int)
{
	g_sigusr2_flag = true;
	hCondMessagesWaiting.notify_one();
}

static void sp_sigusr2_sync()
{
	g_sigusr2_flag = false;
	ec_log_debug("Spooler stats:");
	ec_log_debug("Running threads: %zu", mapSendData.size());
	std::lock_guard<std::mutex> l(hMutexFinished);
	ec_log_debug("Finished threads: %zu", mapFinished.size());
	ec_log_debug("Disconnects: %d", disconnects);
}

/**
 * Notification callback will be called about new messages in the
 * queue. Since this will happen from a different thread, we'll need
 * to use a mutex.
 *
 * @param[in]	lpContext	context of the callback (?)
 * @param[in]	cNotif		number of notifications in lpNotif
 * @param[in]	lpNotif		notification data
 */
static LONG AdviseCallback(void *lpContext, ULONG cNotif,
    LPNOTIFICATION lpNotif)
{
	std::unique_lock<std::mutex> lk(hMutexMessagesWaiting);
	for (ULONG i = 0; i < cNotif; ++i) {
		if (lpNotif[i].info.tab.ulTableEvent == TABLE_RELOAD) {
			// Table needs a reload - trigger a reconnect with the server
			nReload = true;
			bMessagesWaiting = true;
			hCondMessagesWaiting.notify_one();
		}
		else if (lpNotif[i].info.tab.ulTableEvent != TABLE_ROW_DELETED) {
			bMessagesWaiting = true;
			hCondMessagesWaiting.notify_one();
			break;
		}
	}
	return 0;
}

static void *HandlerSP_Thread(void *varg)
{
	std::unique_ptr<sp_handlerargs> a(static_cast<sp_handlerargs *>(varg));
	auto ret = ProcessMessageForked(a->sd.strUsername.c_str(),
		a->smtp_host.c_str(), a->smtp_port, a->path.c_str(),
		a->sd.msg_eid.length(), reinterpret_cast<const ENTRYID *>(a->sd.msg_eid.data()),
		g_lpLogger, a->do_sentmail);
	a->sd.status = ret;
	std::unique_lock<std::mutex> lk(g_senddata_mtx);
	g_senddata_thr.emplace_back(std::move(a->sd));
	lk.unlock();
	hCondMessagesWaiting.notify_one();
	return nullptr;
}

static HRESULT StartSpoolerThread(SendData &&sd, const char *smtp_host,
    uint16_t smtp_port, const char *path, unsigned int flags)
{
	auto th_arg = make_unique_nt<sp_handlerargs>();
	if (th_arg == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	th_arg->sd = std::move(sd);
	th_arg->smtp_host = smtp_host;
	th_arg->smtp_port = smtp_port;
	th_arg->path = path;
	th_arg->do_sentmail = flags & EC_SUBMIT_DOSENTMAIL;

	if (g_process_model == GP_SINGLE) {
		HandlerSP_Thread(th_arg.release());
		return hrSuccess;
	}

	pthread_attr_t attr;
	pthread_t tid;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 1 << 20);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	auto err = pthread_create(&tid, &attr, HandlerSP_Thread, th_arg.get());
	pthread_attr_destroy(&attr);
	if (err != 0) {
		ec_log_err("Could not create worker thread: %s", strerror(err));
		return MAPI_E_CALL_FAILED;
	}
	th_arg.release();
	ec_log_info("Worker thread 0x%lx started", reinterpret_cast<unsigned long>(tid));
	return hrSuccess;
}

/*
 * starting fork, passes:
 * -c config    for all log settings and smtp server and such
 * --log-fd x   execpt you should log here through a pipe
 * --entryids   the data to send
 * --username   the user to send the message as
 * if (szPath)  kopano host
 * if (szSMTP)  smtp host
 * if (szSMTPPport) smtp port
 */
/**
 * Starts a forked process which sends the actual mail, and removes it
 * from the queue, in normal situations.  On error, the
 * CleanFinishedMessages function will try to remove the failed
 * message if needed, else it will be tried again later (e.g. timestamp
 * on sending, or SMTP not responding).
 *
 * @param[in]	szUsername	The username. This name is in unicode.
 * @param[in]	szSMTP		SMTP server to use
 * @param[in]	ulPort		SMTP port to use
 * @param[in]	szPath		URL to storage server
 * @param[in]	cbStoreEntryId	Length of lpStoreEntryId
 * @param[in]	lpStoreEntryId	Entry ID of store of user containing the message to be sent
 * @param[in]	cbStoreEntryId	Length of lpMsgEntryId
 * @param[in]	lpMsgEntryId	Entry ID of message to be sent
 * @param[in]	ulFlags		PR_EC_OUTGOING_FLAGS of message (EC_SUBMIT_DOSENTMAIL flag)
 * @return		HRESULT
 */
static HRESULT StartSpoolerFork(const wchar_t *szUsername, const char *szSMTP,
    int ulSMTPPort, const char *szPath, const SBinary &store_eid,
    const SBinary &msg_eid, unsigned int ulFlags)
{
	SendData sSendData;
	bool bDoSentMail = ulFlags & EC_SUBMIT_DOSENTMAIL;

	// place pid with entryid copy in map
	sSendData.store_eid.assign(reinterpret_cast<const char *>(store_eid.lpb), store_eid.cb);
	sSendData.msg_eid.assign(reinterpret_cast<const char *>(msg_eid.lpb), msg_eid.cb);
	sSendData.ulFlags = ulFlags;
	sSendData.strUsername = szUsername;

	if (g_process_model != GP_FORK)
		return StartSpoolerThread(std::move(sSendData), szSMTP, ulSMTPPort, szPath, ulFlags);

	std::string strPort = stringify(ulSMTPPort);
	// execute the new spooler process to send the email
	const char *argv[18];
	int argc = 0;
	argv[argc++] = szCommand;
	std::unique_ptr<char[], cstdlib_deleter> bname(strdup(szCommand));
	argv[argc++] = basename(bname.get());
	auto eidhex = bin2hex(msg_eid.cb, msg_eid.lpb);
	argv[argc++] = "--send-message-entryid";
	argv[argc++] = eidhex.c_str();
	auto encuser = convert_to<std::string>("UTF-8", szUsername, rawsize(szUsername), CHARSET_WCHAR);
	argv[argc++] = "--send-username-enc";
	argv[argc++] = encuser.c_str();
	auto logfd = stringify(g_lpLogger->GetFileDescriptor());
	argv[argc++] = "--log-fd";
	argv[argc++] = logfd.c_str();
	if (szConfig != nullptr && sp_exp_config) {
		argv[argc++] = "--config";
		argv[argc++] = szConfig;
	}
	argv[argc++] = "--host";
	argv[argc++] = szPath;
	argv[argc++] = "--foreground",
	argv[argc++] = szSMTP;
	argv[argc++] = "--port";
	argv[argc++] = strPort.c_str();
	if (bDoSentMail)
		argv[argc++] = "--do-sentmail";
	argv[argc] = nullptr;
	std::vector<std::string> cmd{argv, argv + argc};
	ec_log_debug("Executing \"%s\"", kc_join(cmd, "\" \"").c_str());

	auto pid = vfork();
	if (pid < 0) {
		ec_log_crit(string("Unable to start new spooler process: ") + strerror(errno));
		return MAPI_E_CALL_FAILED;
	}
	/*
	 * We must exec. spooler still has the IMAPISession object alive in the
	 * caller, and that destructor must not be called now, since the actual
	 * socket connection/fd remains shared between children.
	 */
	if (pid == 0) {
		execvp(argv[0], const_cast<char *const *>(argv));
#ifdef SPOOLER_FORK_DEBUG
		_exit(EXIT_WAIT);
#else
		_exit(EXIT_REMOVE);
#endif
	}

	ec_log_info("Spooler process started on PID %d", pid);
	// process is started, place in map
	mapSendData[pid] = std::move(sSendData);
	return hrSuccess;
}

/**
 * Opens all required objects of the administrator to move an error
 * mail out of the queue.
 *
 * @param[in]	sSendData		Struct with information about the mail in the queue which caused a fatal error
 * @param[in]	lpAdminSession	MAPI session of Kopano SYSTEM user
 * @param[out]	lppAddrBook		MAPI Addressbook object
 * @param[out]	lppMailer		inetmapi ECSender object, which can generate an error text for the body for the mail
 * @param[out]	lppUserStore	The store of the user with the error mail, open with admin rights
 * @param[out]	lppMessage		The message of the user which caused the error, open with admin rights
 * @return		HRESULT
 */
static HRESULT GetErrorObjects(const SendData &sSendData,
    IMAPISession *lpAdminSession, IAddrBook **lppAddrBook,
    ECSender **lppMailer, IMsgStore **lppUserStore, IMessage **lppMessage)
{
	ULONG ulObjType = 0;

	if (*lppAddrBook == NULL) {
		auto hr = lpAdminSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, lppAddrBook);
		if (hr != hrSuccess)
			return kc_perror("Unable to open addressbook for error mail (skipping)", hr);
	}

	if (*lppMailer == NULL) {
		/*
		 * SMTP server does not matter here, we just use the
		 * object for the error body.
		 */
		*lppMailer = CreateSender("localhost", 25);
		if (! (*lppMailer)) {
			ec_log_err("Unable to create error object for error mail, skipping.");
			return hrSuccess;
		}
	}

	if (*lppUserStore == NULL) {
		auto hr = lpAdminSession->OpenMsgStore(0, sSendData.store_eid.size(),
		          reinterpret_cast<const ENTRYID *>(sSendData.store_eid.data()),
		          nullptr, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, lppUserStore);
		if (hr != hrSuccess)
			return kc_perror("Unable to open store of user for error mail (skipping)", hr);
	}

	if (*lppMessage == NULL) {
		auto hr = (*lppUserStore)->OpenEntry(sSendData.msg_eid.size(),
		          reinterpret_cast<const ENTRYID *>(sSendData.msg_eid.data()),
		          &IID_IMessage, MAPI_BEST_ACCESS, &ulObjType,
		          reinterpret_cast<IUnknown **>(lppMessage));
		if (hr != hrSuccess)
			return kc_perror("Unable to open message of user for error mail (skipping)", hr);
	}
	return hrSuccess;
}

static void CleanFinishedMessagesThreaded(IMAPISession *ses, IECSpooler *spooler)
{
	std::unique_lock<std::mutex> lk(g_senddata_mtx);
	if (g_senddata_thr.empty())
		return;
	auto finished = std::move(g_senddata_thr);
	g_senddata_thr.clear();
	lk.unlock();
	ec_log_debug("Cleaning %zu subthreads from queue", finished.size());
	for (const auto &sd : finished)
		handle_child_exit(ses, spooler, sc.get(), 0, SPC_EXITED, sd.status, sd);
}

/**
 * Cleans finished messages. Normally only prints a logmessage. If the
 * mailer completely failed (e.g. segfault), this function will try to
 * remove the faulty mail from the queue.
 *
 * @param[in]	lpAdminSession	MAPI session of the Kopano SYSTEM user
 * @param[in]	lpSpooler		IECSpooler object
 * @return		HRESULT
 */
static void CleanFinishedMessages(IMAPISession *lpAdminSession,
    IECSpooler *lpSpooler)
{
	if (g_process_model != GP_FORK) {
		CleanFinishedMessagesThreaded(lpAdminSession, lpSpooler);
		return;
	}

	std::unique_lock<std::mutex> lock(hMutexFinished);
	if (mapFinished.empty())
		return;
	// copy map contents and clear it, so hMutexFinished can be unlocked again asap
	auto finished = std::move(mapFinished);
	mapFinished.clear();
	lock.unlock();
	ec_log_debug("Cleaning %zu subprocesses from queue", finished.size());

	// process finished entries
	for (const auto &i : finished) {
		auto sdi = mapSendData.find(i.first);
		if (sdi == mapSendData.cend())
			/* not a mail worker subprocess */
			continue;
		/* Find exit status, and decide to remove mail from queue or not */
		auto mode = WIFEXITED(i.second) ? SPC_EXITED : WIFSIGNALED(i.second) ? SPC_SIGNALED : SPC_ABNORMAL;
		auto value = WIFEXITED(i.second) ? WEXITSTATUS(i.second) : WIFSIGNALED(i.second) ? WTERMSIG(i.second) : 0;
		handle_child_exit(lpAdminSession, lpSpooler, sc.get(), i.first, mode, value, sdi->second);
		mapSendData.erase(i.first);
	}
}

static HRESULT handle_child_exit(IMAPISession *lpAdminSession,
    IECSpooler *lpSpooler, StatsClient *sc, pid_t pid, unsigned int mode,
    unsigned int status, const SendData &sSendData)
{
	bool wasSent = false, bErrorMail = false;

	if (mode == SPC_EXITED) {
		if (status == EXIT_WAIT) {
			// timed message, try again later
			ec_log_info("Message for user %ls will be tried again later", sSendData.strUsername.c_str());
			sc->inc(SCN_SPOOLER_EXIT_WAIT);
		} else if (status == EXIT_SUCCESS || status == EXIT_FAILURE) {
			// message was sent, or the user already received an error mail.
			ec_log_info("Processed message for user %ls", sSendData.strUsername.c_str());
			wasSent = true;
		} else {
			// message was not sent, and could not be removed from queue. Notify user also.
			bErrorMail = true;
			ec_log_warn("Failed message for user %ls will be removed from queue, error 0x%x", sSendData.strUsername.c_str(), status);
		}
	} else if (mode == SPC_SIGNALED) {
		bErrorMail = true;
		ec_log_notice("Spooler process %d was killed by signal %d", pid, status);
		ec_log_warn("Message for user %ls will be removed from queue", sSendData.strUsername.c_str());
		sc->inc(SCN_SPOOLER_SIGKILLED);
	} else { /* Something strange happened */
		bErrorMail = true;
		ec_log_notice("Spooler process %d terminated abnormally", pid);
		ec_log_warn("Message for user %ls will be removed from queue", sSendData.strUsername.c_str());
		sc->inc(SCN_SPOOLER_ABNORM_TERM);
	}
	if (wasSent)
		sc->inc(SCN_SPOOLER_SENT);
	else if (bErrorMail)
		sc->inc(SCN_SPOOLER_SEND_FAILED);

	if (!bErrorMail)
		return hrSuccess;

	object_ptr<IMsgStore> lpUserStore;
	object_ptr<IMessage> lpMessage;
	object_ptr<IAddrBook> lpAddrBook;
	std::unique_ptr<ECSender> lpMailer;
	auto hr = GetErrorObjects(sSendData, lpAdminSession, &~lpAddrBook, &unique_tie(lpMailer), &~lpUserStore, &~lpMessage);
	if (hr == hrSuccess) {
		lpMailer->setError(KC_TX("A fatal error occurred while processing your message, and Kopano is unable to send your email."));
		hr = SendUndeliverable(lpMailer.get(), lpUserStore, lpMessage);
		// TODO: if failed, and we have the lpUserStore, create message?
	}
	if (hr != hrSuccess)
		ec_log_warn("Failed to create error message for user %ls: %s (%x)",
			sSendData.strUsername.c_str(), GetMAPIErrorMessage(hr), hr);
	// remove mail from queue
	hr = lpSpooler->DeleteFromMasterOutgoingTable(sSendData.msg_eid.size(),
	     reinterpret_cast<const ENTRYID *>(sSendData.msg_eid.data()), sSendData.ulFlags);
	if (hr != hrSuccess)
		kc_pwarn("Could not remove invalid message from queue", hr);
	// move mail to sent items folder
	if (sSendData.ulFlags & EC_SUBMIT_DOSENTMAIL && lpMessage) {
		hr = DoSentMail(lpAdminSession, lpUserStore, 0, std::move(lpMessage));
		if (hr != hrSuccess)
			kc_perror("Unable to move sent mail to Sent Items folder", hr);
	}
	return hr;
}

/**
 * Sends all messages found in lpTable (outgoing queue).
 *
 * Starts forks or threads until maximum number of simultatious
 * messages is reached. Loops until a slot comes free to start a new
 * fork/thread. When a fatal MAPI error has occurred, or the table is
 * empty, this function will return.
 *
 * @param[in]	lpAdminSession	MAPI Session of Kopano SYSTEM user
 * @param[in]	lpSpooler		IECSpooler object
 * @param[in]	lpTable			Outgoing queue table view
 * @param[in]	szSMTP			SMTP server to use
 * @param[in]	ulPort			SMTP port
 * @param[in]	szPath			URI to storage server
 * @return		HRESULT
 */
static HRESULT ProcessAllEntries2(IMAPISession *lpAdminSession,
    IECSpooler *lpSpooler, IMAPITable *lpTable, const char *szSMTP, int ulPort,
    const char *szPath, bool &bForceReconnect)
{
	ULONG ulRowCount = 0, later_mails = 0;
	auto report = make_scope_success([&]() {
		if (ulRowCount != 0)
			ec_log_debug("Messages with delayed delivery: %d", later_mails);
	});
	auto hr = lpTable->GetRowCount(0, &ulRowCount);
	if (hr != hrSuccess)
		return kc_perror("Unable to get outgoing queue count", hr);
	if (ulRowCount) {
		ec_log_debug("Number of messages in the queue: %d", ulRowCount);
		sc->inc(SCN_SPOOLER_BATCH_INVOKES);
		sc->inc(SCN_SPOOLER_BATCH_COUNT, static_cast<int64_t>(ulRowCount));
	}

	auto ulMaxThreads = atoi(g_lpConfig->GetSetting("max_threads"));
	if (ulMaxThreads == 0)
		ulMaxThreads = 1;

	while(!bQuit) {
		auto ulFreeThreads = ulMaxThreads - mapSendData.size();
		if (ulFreeThreads == 0) {
			Sleep(100);
			// remove enties from mapSendData which are finished
			CleanFinishedMessages(lpAdminSession, lpSpooler);
			continue;	/* Continue looping until threads become available */
		}

		rowset_ptr lpsRowSet;
		hr = lpTable->QueryRows(1, 0, &~lpsRowSet);
		if (hr != hrSuccess)
			return kc_perror("Unable to fetch data from table", hr);
		if (lpsRowSet->cRows == 0)		// All rows done
			break;
		if (lpsRowSet[0].lpProps[4].ulPropTag == PR_DEFERRED_SEND_TIME &&
		    time(nullptr) < FileTimeToUnixTime(lpsRowSet[0].lpProps[4].Value.ft)) {
			// if we ever add logging here, it should trigger just once for this mail
			++later_mails;
			continue;
		}

		// Check whether the row contains the entryid and store id
		if (lpsRowSet[0].lpProps[0].ulPropTag != PR_EC_MAILBOX_OWNER_ACCOUNT_W ||
		    lpsRowSet[0].lpProps[1].ulPropTag != PR_STORE_ENTRYID ||
		    lpsRowSet[0].lpProps[2].ulPropTag != PR_ENTRYID ||
		    lpsRowSet[0].lpProps[3].ulPropTag != PR_EC_OUTGOING_FLAGS)
		{
			// Client was quick enough to remove message from queue before we could read it
			ec_log_notice("Empty row in OutgoingQueue");

			if (lpsRowSet[0].lpProps[2].ulPropTag == PR_ENTRYID &&
			    lpsRowSet[0].lpProps[3].ulPropTag == PR_EC_OUTGOING_FLAGS) {
				// we can remove this message
				ec_log_warn("Removing invalid entry from OutgoingQueue");
				hr = lpSpooler->DeleteFromMasterOutgoingTable(lpsRowSet[0].lpProps[2].Value.bin.cb, reinterpret_cast<ENTRYID *>(lpsRowSet[0].lpProps[2].Value.bin.lpb), lpsRowSet[0].lpProps[3].Value.ul);
				if (hr != hrSuccess)
					// since we have an error, we will reconnect to the server to fully reload the table
					return kc_pwarn("Could not remove invalid message from queue", hr);
			}
			else {
				// this error makes the spooler disconnect from the server, and reconnect again (bQuit still false)
				bForceReconnect = true;
			}

			continue;
		}

		std::wstring strUsername = lpsRowSet[0].lpProps[0].Value.lpszW;
		// Check if there is already an active process for this message
		bool bMatch = false;
		for (const auto &i : mapSendData)
			if (i.second.msg_eid.size() == lpsRowSet[0].lpProps[2].Value.bin.cb &&
			    memcmp(i.second.msg_eid.data(), lpsRowSet[0].lpProps[2].Value.bin.lpb, i.second.msg_eid.size()) == 0) {
				bMatch = true;
				break;
			}
		if (bMatch)
			continue;

		// Start new process to send the mail
		hr = StartSpoolerFork(strUsername.c_str(), szSMTP, ulPort, szPath,
		     lpsRowSet[0].lpProps[1].Value.bin, lpsRowSet[0].lpProps[2].Value.bin,
		     lpsRowSet[0].lpProps[3].Value.ul);
		if (hr != hrSuccess)
			return kc_pwarn("ProcessAllEntries(): Failed starting spooler", hr);
	}
	return hrSuccess;
}

static HRESULT ProcessAllEntries(IMAPISession *ses, IECSpooler *spooler,
    IMAPITable *table, const char *smtp, int port, const char *path)
{
	bool reconn = false;
	auto ret = ProcessAllEntries2(ses, spooler, table, smtp, port, path, reconn);
	return reconn ? MAPI_E_NETWORK_ERROR : ret;
}

/**
 * Opens the IECSpooler object on an admin session.
 *
 * @param[in]	lpAdminSession	MAPI Session of the Kopano SYSTEM user
 * @param[out]	lppSpooler		IECSpooler is a Kopano interface to the outgoing queue functions.
 * @return		HRESULT
 */
static HRESULT GetAdminSpooler(IMAPISession *lpAdminSession,
    IECSpooler **lppSpooler)
{
	object_ptr<IMsgStore> lpMDB;
	auto hr = HrOpenDefaultStore(lpAdminSession, &~lpMDB);
	if (hr != hrSuccess)
		return kc_perror("Unable to open default store for system account", hr);
	hr = GetECObject(lpMDB, iid_of(*lppSpooler), reinterpret_cast<void **>(lppSpooler));
	if (hr != hrSuccess)
		return kc_perror("Unable to get Kopano internal object", hr);
	return hr;
}


/**
 * Opens an admin session and the outgoing queue. If either one
 * produces an error this function will return. If the queue is empty,
 * it will wait for a notification when new data is present in the
 * outgoing queue table.
 *
 * @param[in]	szSMTP	The SMTP server to send to.
 * @param[in]	ulPort	The SMTP port to sent to.
 * @param[in]	szPath	URI of storage server to connect to, must be file:// or https:// with valid SSL certificates.
 * @return		HRESULT
 */
static HRESULT ProcessQueue2(IMAPISession *lpAdminSession,
    IECSpooler *lpSpooler, const char *szSMTP, int ulPort, const char *szPath)
{
	object_ptr<IMAPITable> lpTable;
	object_ptr<IMAPIAdviseSink> lpAdviseSink;
	ULONG				ulConnection	= 0;
	static constexpr const SizedSPropTagArray(5, sOutgoingCols) =
		{5, {PR_EC_MAILBOX_OWNER_ACCOUNT_W, PR_STORE_ENTRYID,
		PR_ENTRYID, PR_EC_OUTGOING_FLAGS, PR_DEFERRED_SEND_TIME}};
	static constexpr const SizedSSortOrderSet(1, sSort) =
		{1, 0, 0, {{PR_EC_HIERARCHYID, TABLE_SORT_ASCEND}}};

	auto adv_clean = make_scope_success([&]() {
		if (lpTable != nullptr && ulConnection != 0)
			lpTable->Unadvise(ulConnection);
	});
	// Mark reload as done since we reloaded the outgoing table
	nReload = false;

	// Request the master outgoing table
	auto hr = lpSpooler->GetMasterOutgoingTable(0, &~lpTable);
	if (hr != hrSuccess)
		return kc_perror("Master outgoing queue not available", hr);
	hr = lpTable->SetColumns(sOutgoingCols, 0);
	if (hr != hrSuccess)
		return kc_perror("Unable to setColumns() on OutgoingQueue", hr);
	// Sort by ascending hierarchyid: first in, first out queue
	hr = lpTable->SortTable(sSort, 0);
	if (hr != hrSuccess)
		return kc_perror("Unable to SortTable() on OutgoingQueue", hr);
	hr = HrAllocAdviseSink(AdviseCallback, nullptr, &~lpAdviseSink);
	if (hr != hrSuccess)
		return kc_perror("Unable to allocate memory for advise sink", hr);

	// notify on new mail in the outgoing table
	hr = lpTable->Advise(fnevTableModified, lpAdviseSink, &ulConnection);

	while(!bQuit && !nReload) {
		bMessagesWaiting = false;
		lpTable->SeekRow(BOOKMARK_BEGINNING, 0, NULL);
		// also checks not to send a message again which is already sending
		hr = ProcessAllEntries(lpAdminSession, lpSpooler, lpTable, szSMTP, ulPort, szPath);
		if (hr != hrSuccess)
			return kc_pwarn("ProcessQueue: ProcessAllEntries failed", hr);
		// Exit signal, break the operation
		if(bQuit)
			break;
		if(nReload)
			break;

		std::unique_lock<std::mutex> lk(hMutexMessagesWaiting);
		if(!bMessagesWaiting) {
			auto target = std::chrono::steady_clock::now() + 60s;
			while (!bMessagesWaiting) {
				auto s = hCondMessagesWaiting.wait_until(lk, target);
				if (g_sigusr2_flag)
					sp_sigusr2_sync();
				if (g_sighup_flag)
					sp_sighup_sync();
				if (bQuit) {
					ec_log_info("User requested graceful shutdown. To force quit, reissue the request.");
					break;
				}
				if (s == std::cv_status::timeout || bMessagesWaiting || nReload)
					break;
				// not timed out, no messages waiting, not quit requested, no table reload required:
				// we were triggered for a cleanup call.
				lk.unlock();
				CleanFinishedMessages(lpAdminSession, lpSpooler);
				lk.lock();
			}
		}
		lk.unlock();
		// remove any entries that were done during the wait
		CleanFinishedMessages(lpAdminSession, lpSpooler);
	}
	return hrSuccess;
}

static HRESULT ProcessQueue(const char *smtp, int port, const char *path)
{
	object_ptr<IMAPISession> lpAdminSession;
	object_ptr<IECSpooler> lpSpooler;

	auto hr = HrOpenECAdminSession(&~lpAdminSession, PROJECT_VERSION,
	          "spooler:system", path, EC_PROFILE_FLAGS_NO_PUBLIC_STORE,
	          g_lpConfig->GetSetting("sslkey_file", "", nullptr),
	          g_lpConfig->GetSetting("sslkey_pass", "", nullptr));
	if (hr != hrSuccess)
		return kc_perror("Unable to open admin session", hr);
	if (disconnects == 0)
		ec_log_debug("Connection to storage server succeeded");
	else
		ec_log_info("Connection to storage server succeeded after %d retries", disconnects);
	disconnects = 0; /* first call succeeded, assume all is well. */
	hr = GetAdminSpooler(lpAdminSession, &~lpSpooler);
	if (hr != hrSuccess)
		return kc_perrorf("GetAdminSpooler failed", hr);
	hr = ProcessQueue2(lpAdminSession, lpSpooler, smtp, port, path);

	/* When we exit, we must make sure all forks started are cleaned. */
	if (bQuit) {
		size_t ulCount = 0;
		while (ulCount < 60) {
			size_t ulThreads = mapSendData.size();
			if (ulThreads == 0)
				break;
			if ((ulCount % 5) == 0)
				ec_log_warn("Still waiting for %zu thread(s) to exit.", ulThreads);
			if (lpSpooler != nullptr)
				CleanFinishedMessages(lpAdminSession, lpSpooler);
			Sleep(1000);
			++ulCount;
		}
		if (ulCount == 60)
			ec_log_debug("%zu threads did not yet exit, closing anyway.", mapSendData.size());
	}
	else if (nReload) {
		ec_log_warn("Table reload requested, breaking server connection");
	}
	return hr;
}

/**
 * Main program loop. Calls ProcessQueue, which logs in to MAPI. This
 * way, disconnects are solved. After a disconnect from the server,
 * the loop will try again after 3 seconds.
 *
 * @param[in]	szSMTP	The SMTP server to send to.
 * @param[in]	ulPort	The SMTP port to send to.
 * @param[in]	szPath	URI of storage server to connect to, must be file:// or https:// with valid SSL certificates.
 * @return		HRESULT
 */
static HRESULT running_server(const char *szSMTP, int ulPort,
    const char *szPath)
{
	HRESULT hr = hrSuccess;
	ec_log_debug("Using SMTP server: %s, port %d", szSMTP, ulPort);
	disconnects = 0;

	while (1) {
		hr = ProcessQueue(szSMTP, ulPort, szPath);
		if (bQuit)
			break;
		if (disconnects == 0)
			ec_log_warn("Server connection lost. Reconnecting in 3 seconds...");
		++disconnects;
		Sleep(3000);			// wait 3s until retry to connect
	}
	bQuit = true;				// make sure the sigchld does not use the lock anymore
	return hr;
}

static int main2(int argc, char **argv)
{
	HRESULT hr = hrSuccess;
	const char *szPath = nullptr, *szSMTP = nullptr;
	int ulPort = 0, daemonize = 1, logfd = -1;
	bool bForked = false;
	std::string strMsgEntryId;
	std::wstring strUsername;
	bool bDoSentMail = false, bIgnoreUnknownConfigOptions = false;

	// options
	enum {
		OPT_HELP = UCHAR_MAX + 1,
		OPT_CONFIG,
		OPT_HOST,
		OPT_FOREGROUND,
		OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS,
		// only called by spooler itself
		OPT_SEND_MESSAGE_ENTRYID,
		OPT_SEND_USERNAME,
		OPT_LOGFD,
		OPT_DO_SENTMAIL,
		OPT_PORT,
		OPT_DUMP_CONFIG,
	};
	static const struct option long_options[] = {
		{ "help", 0, NULL, OPT_HELP },		// help text
		{ "config", 1, NULL, OPT_CONFIG },	// config file
		{ "host", 1, NULL, OPT_HOST },		// kopano host location
		{ "foreground", 0, NULL, OPT_FOREGROUND },		// do not daemonize
		// only called by spooler itself
		{ "send-message-entryid", 1, NULL, OPT_SEND_MESSAGE_ENTRYID },	// entryid of message to send
		{ "send-username-enc", 1, NULL, OPT_SEND_USERNAME },			// owner's username of message to send in hex-utf8
		{ "log-fd", 1, NULL, OPT_LOGFD },								// fd where to send log messages to
		{ "do-sentmail", 0, NULL, OPT_DO_SENTMAIL },
		{ "port", 1, NULL, OPT_PORT },
		{ "ignore-unknown-config-options", 0, NULL, OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS },
		{"dump-config", 0, nullptr, OPT_DUMP_CONFIG},
		{ NULL, 0, NULL, 0 }
	};
	// Default settings
	static const configsetting_t lpDefaults[] = {
		{ "smtp_server","localhost", CONFIGSETTING_RELOADABLE },
		{ "smtp_port","25", CONFIGSETTING_RELOADABLE },
		{ "server_socket", "default:" },
		{ "run_as_user", "kopano" },
		{ "run_as_group", "kopano" },
		{ "pid_file", "/var/run/kopano/spooler.pid" },
		{"running_path", "/var/lib/kopano/empty", CONFIGSETTING_OBSOLETE},
		{"coredump_enabled", "systemdefault"},
		{"log_method", "auto", CONFIGSETTING_NONEMPTY},
		{"log_file", ""},
		{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{ "log_timestamp","1" },
		{ "log_buffer_size", "0" },
		{ "sslkey_file", "" },
		{ "sslkey_pass", "", CONFIGSETTING_EXACT },
		{ "max_threads", "5", CONFIGSETTING_RELOADABLE },
		{ "fax_domain", "", CONFIGSETTING_RELOADABLE },
		{ "fax_international", "+", CONFIGSETTING_RELOADABLE },
		{ "always_send_delegates", "no", CONFIGSETTING_RELOADABLE },
		{ "always_send_tnef", "no", CONFIGSETTING_RELOADABLE },
		{ "always_send_utf8", "no", CONFIGSETTING_UNUSED },
		{ "charset_upgrade", "windows-1252", CONFIGSETTING_RELOADABLE },
		{ "allow_redirect_spoofing", "yes", CONFIGSETTING_RELOADABLE },
		{ "allow_delegate_meeting_request", "yes", CONFIGSETTING_RELOADABLE },
		{ "allow_send_to_everyone", "yes", CONFIGSETTING_RELOADABLE },
		{"copy_delegate_mails", "yes", CONFIGSETTING_RELOADABLE},
		{ "expand_groups", "no", CONFIGSETTING_RELOADABLE },
		{ "archive_on_send", "no", CONFIGSETTING_RELOADABLE },
		{ "enable_dsn", "yes", CONFIGSETTING_RELOADABLE },
        { "plugin_enabled", "yes" },
        { "plugin_path", "/var/lib/kopano/spooler/plugins" },
        { "plugin_manager_path", "/usr/share/kopano-spooler/python" },
		{"statsclient_url", "unix:/var/run/kopano/statsd.sock", CONFIGSETTING_RELOADABLE},
		{"statsclient_interval", "0", CONFIGSETTING_RELOADABLE},
		{"statsclient_ssl_verify", "yes", CONFIGSETTING_RELOADABLE},
		{ "tmp_path", "/tmp" },
		{"log_raw_message_path", "/var/lib/kopano", CONFIGSETTING_RELOADABLE},
		{"log_raw_message_stage1", "no", CONFIGSETTING_RELOADABLE},
		{"process_model", "fork", CONFIGSETTING_NONEMPTY},
		{ NULL, NULL },
	};
    // SIGSEGV backtrace support
    struct sigaction act;
    memset(&act, 0, sizeof(act));

	setlocale(LC_CTYPE, "");
	forceUTF8Locale(true);
	setlocale(LC_MESSAGES, "");

	while(1) {
		auto c = my_getopt_long_permissive(argc, argv, "c:h:iuVF", long_options, NULL);
		if(c == -1)
			break;

		switch(c) {
		case OPT_CONFIG:
		case 'c':
			szConfig = optarg;
			sp_exp_config = true;
			break;
		case OPT_HOST:
		case 'h':
			szPath = optarg;
			break;
		case 'i': // Install service
		case 'u': // Uninstall service
			break;
		case 'F':
		case OPT_FOREGROUND:
			daemonize = 0;
			break;
		case OPT_SEND_MESSAGE_ENTRYID:
			bForked = true;
			strMsgEntryId = hex2bin(optarg);
			break;
		case OPT_SEND_USERNAME:
			bForked = true;
			strUsername = convert_to<std::wstring>(optarg, rawsize(optarg), "UTF-8");
			break;
		case OPT_LOGFD:
			logfd = atoi(optarg);
			break;
		case OPT_DO_SENTMAIL:
			bDoSentMail = true;
			break;
		case OPT_PORT:
			ulPort = atoi(optarg);
			break;
		case OPT_IGNORE_UNKNOWN_CONFIG_OPTIONS:
			bIgnoreUnknownConfigOptions = true;
			break;
		case OPT_DUMP_CONFIG:
			g_dump_config = true;
			break;
		case 'V':
			cout << "kopano-spooler " PROJECT_VERSION << endl;
			return 1;
		case OPT_HELP:
		default:
			cout << "Unknown option: " << c << endl;
			print_help(argv[0]);
			return 1;
		}
	}

	if (bForked)
		bIgnoreUnknownConfigOptions = true;
	g_lpConfig.reset(ECConfig::Create(lpDefaults));
	int argidx = 0;
	if (!g_lpConfig->LoadSettings(szConfig, !sp_exp_config) ||
	    (argidx = g_lpConfig->ParseParams(argc - optind, &argv[optind])) < 0 ||
	    (!bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors())) {
		/* Create info logger without a timestamp to stderr. */
		g_lpLogger.reset(new(std::nothrow) ECLogger_File(EC_LOGLEVEL_INFO, 0, "-", false));
		if (g_lpLogger == nullptr)
			return EXIT_FAILURE; /* MAPI_E_NOT_ENOUGH_MEMORY */
		ec_log_set(g_lpLogger);
		LogConfigErrors(g_lpConfig.get());
		return EXIT_FAILURE; /* E_FAIL */
	}
	// ECConfig::ParseParams returns the index in the passed array,
	// after some shuffling, where it stopped parsing. optind is
	// the index where my_getopt_long_permissive stopped parsing. So
	// adding argidx to optind will result in the index after all
	// options are parsed.
	optind += argidx;
	// commandline overwrites spooler.cfg
	if (optind < argc)
		szSMTP = argv[optind];
	else
		szSMTP = g_lpConfig->GetSetting("smtp_server");
	if (!ulPort)
		ulPort = atoui(g_lpConfig->GetSetting("smtp_port"));

	szCommand = argv[0];
	// setup logging, use pipe to log if started in forked mode and using pipe (file) logger, create normal logger for syslog
	if (bForked && logfd != -1)
		g_lpLogger.reset(new(std::nothrow) ECLogger_Pipe(logfd, 0, atoi(g_lpConfig->GetSetting("log_level"))));
	else
		g_lpLogger.reset(CreateLogger(g_lpConfig.get(), argv[0], "KopanoSpooler"));
	ec_log_set(g_lpLogger);
	if ((bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors()) || g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig.get());
	if (g_dump_config)
		return g_lpConfig->dump_config(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

	if (!bDoSentMail)
		ec_log_always("Starting kopano-spooler version " PROJECT_VERSION " (pid %d uid %u)", getpid(), getuid());
	else
		ec_log_debug("Starting kopano-spooler worker");
	if (!TmpPath::instance.OverridePath(g_lpConfig.get()))
		ec_log_err("Ignoring invalid path setting!");
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
	if (parseBool(g_lpConfig->GetSetting("plugin_enabled"))) {
		std::string path = g_lpConfig->GetSetting("plugin_manager_path");
		auto prev_path = getenv("PYTHONPATH");
		if (prev_path != nullptr)
			path += std::string(":") + prev_path;
		setenv("PYTHONPATH", path.c_str(), 1);
		ec_log_debug("PYTHONPATH = %s", path.c_str());
	}
	if (g_process_model == GP_THREAD)
		g_lpLogger->SetLogprefix(LP_TID);
	// set socket filename
	if (!szPath)
		szPath = g_lpConfig->GetSetting("server_socket");

	if (bForked) {
		// keep sending mail when we're killed in forked mode
		signal(SIGTERM, SIG_IGN);
		signal(SIGINT, SIG_IGN);
		signal(SIGHUP, SIG_IGN);
		signal(SIGUSR1, SIG_IGN);
		signal(SIGUSR2, SIG_IGN);
	}
	else {
		// notification condition
		sigemptyset(&act.sa_mask);
		act.sa_handler = sp_sigchld_async;
		act.sa_flags = SA_ONSTACK;
		sigaction(SIGCHLD, &act, nullptr);
		act.sa_handler = sp_sighup_async;
		sigaction(SIGHUP, &act, nullptr);
		act.sa_handler = sp_sigusr2_async;
		sigaction(SIGUSR2, &act, nullptr);
		act.sa_handler = sp_sigterm_async;
		act.sa_flags = SA_ONSTACK | SA_RESETHAND;
		sigaction(SIGINT, &act, nullptr);
		sigaction(SIGTERM, &act, nullptr);
	}

	ec_setup_segv_handler("kopano-spooler", PROJECT_VERSION);
	bQuit = bMessagesWaiting = false;
	unix_coredump_enable(g_lpConfig->GetSetting("coredump_enabled"));
	umask(S_IRWXG | S_IRWXO);

	AutoMAPI mapiinit;
	// fork if needed and drop privileges as requested.
	// this must be done before we do anything with pthreads
	auto ret = unix_runas(g_lpConfig.get());
	if (ret < 0) {
		ec_log_crit("main(): run_as failed");
		return MAPI_E_CALL_FAILED;
	} else if (ret == 0) {
		ec_reexec_finalize();
	} else if (ret > 0) {
		ret = ec_reexec(argv);
		if (ret < 0)
			ec_log_notice("K-1240: Failed to re-exec self: %s. "
				"Continuing with restricted coredumps.", strerror(-ret));
	}
	if (daemonize && unix_daemonize(g_lpConfig.get())) {
		ec_log_crit("main(): failed daemonizing");
		return MAPI_E_CALL_FAILED;
	}
	if (!daemonize)
		setsid();
	if (!bForked && unix_create_pidfile(argv[0], g_lpConfig.get(), false) < 0) {
		ec_log_crit("main(): Failed creating PID file");
		return MAPI_E_CALL_FAILED;
	}
	if (g_process_model == GP_FORK)
		g_lpLogger = StartLoggerProcess(g_lpConfig.get(), std::move(g_lpLogger));
	ec_log_set(g_lpLogger);
	g_lpLogger->SetLogprefix(LP_PID);

	hr = mapiinit.Initialize();
	if (hr != hrSuccess) {
		ec_log_crit("Unable to initialize MAPI: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		return hr;
	}

	sc.reset(new spooler_stats(g_lpConfig));
	if (bForked)
		hr = ProcessMessageForked(strUsername.c_str(), szSMTP, ulPort,
		     szPath, strMsgEntryId.length(), reinterpret_cast<const ENTRYID *>(strMsgEntryId.data()),
		     g_lpLogger, bDoSentMail);
	else
			hr = running_server(szSMTP, ulPort, szPath);
	if (!bForked)
		ec_log_info("Spooler shutdown complete");
	/* ~ECLogger_Pipe could call hCondMessagesWaiting.notify_one and thus needs to be gone before the cv */
	g_lpLogger.reset();
	return hr;
}

int main(int argc, char **argv) try {
	auto hr = main2(argc, argv);
	switch(hr) {
	case hrSuccess:
		return EXIT_SUCCESS;

	case MAPI_E_WAIT:			// Timed message
		case MAPI_W_NO_SERVICE:	// SMTP server did not react in forked mode, mail should be retried later
		return EXIT_WAIT;
	}

	// forked: failed sending message, but is already removed from the queue
	return EXIT_FAILURE;
} catch (...) {
	std::terminate();
}

spooler_stats::spooler_stats(std::shared_ptr<ECConfig> cfg) :
	StatsClient(std::move(cfg))
{
	set(SCN_PROGRAM_NAME, "kopano-spooler");
	AddStat(SCN_SPOOLER_EXIT_WAIT, SCT_INTEGER, "spooler_exit_wait");
	AddStat(SCN_SPOOLER_SIGKILLED, SCT_INTEGER, "spooler_sigkilled");
	AddStat(SCN_SPOOLER_ABNORM_TERM, SCT_INTEGER, "spooler_abnormal_termination");
	AddStat(SCN_SPOOLER_SENT, SCT_INTEGER, "spooler_sent");
	AddStat(SCN_SPOOLER_SEND_FAILED, SCT_INTEGER, "spooler_send_failed");
	AddStat(SCN_SPOOLER_BATCH_INVOKES, SCT_INTEGER, "spooler_batch_invokes");
	AddStat(SCN_SPOOLER_BATCH_COUNT, SCT_INTEGER, "spooler_batch_count");
}
