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
#include "mailer.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <csignal>
#include <sys/time.h> /* gettimeofday */

#define USES_IID_IMAPIFolder
#define USES_IID_IMessage
#define USES_IID_IMsgStore

#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>
#include <mapiguid.h>
#include <cctype>

#include <kopano/IECUnknown.h>
#include "IECSpooler.h"
#include <kopano/IECServiceAdmin.h>
#include <kopano/IECSecurity.h>
#include <kopano/MAPIErrors.h>
#include <kopano/ECGuid.h>
#include <kopano/EMSAbTag.h>
#include <kopano/ECTags.h>
#include <kopano/ECABEntryID.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#ifdef LINUX
#include <kopano/UnixUtil.h>
#endif
#include <kopano/MAPIErrors.h>
#include <kopano/my_getopt.h>
#include <kopano/ecversion.h>
#include <kopano/Util.h>
#include <kopano/stringutil.h>

#include <kopano/mapiext.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/mapiguidext.h>
#include "mapicontact.h"
#include <kopano/restrictionutil.h>
#include <kopano/charset/convert.h>
#include <kopano/charset/convstring.h>
#include <kopano/charset/utf8string.h>
#include <kopano/ECGetText.h>
#include "StatsClient.h"
#include "TmpPath.h"

#include <map>
#include "spmain.h"

using namespace std;

static StatsClient *sc = NULL;

// spooler exit codes
#define EXIT_OK 0
#define EXIT_FAILED 1
#define EXIT_WAIT 2
#define EXIT_REMOVE 3

static bool bQuit = false;
static int nReload = 0;
static int disconnects = 0;
static const char *szCommand = NULL;
#ifdef LINUX
static const char *szConfig = ECConfig::GetDefaultPath("spooler.cfg");
#else
static const char *szConfig = "spooler.cfg";
#endif
ECConfig *g_lpConfig = NULL;
ECLogger *g_lpLogger = NULL;

#ifdef LINUX
static pthread_t signal_thread;
static sigset_t signal_mask;
static bool bNPTL = true;
#endif

// notification
static bool bMessagesWaiting = false;
static pthread_mutex_t hMutexMessagesWaiting;
static pthread_cond_t hCondMessagesWaiting;

// messages being processed
typedef struct _SendData {
	ULONG cbStoreEntryId;
	BYTE* lpStoreEntryId;
	ULONG cbMessageEntryId;
	BYTE* lpMessageEntryId;
	ULONG ulFlags;
	wstring strUsername;
} SendData;
static map<pid_t, SendData> mapSendData;
static map<pid_t, int> mapFinished;	// exit status of finished processes
static pthread_mutex_t hMutexFinished;	// mutex for mapFinished

static HRESULT running_server(const char *szSMTP, int port, const char *szPath);

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

/**
 * Encode a wide string in UTF-8 and output it in hexadecimal.
 * @param[in]	lpszW	The wide string to encode
 * @return				The encoded string.
 */
static string encodestring(const wchar_t *lpszW) {
	const utf8string u8 = convstring(lpszW);
	return bin2hex(u8.size(), (const unsigned char*)u8.c_str());
}

/**
 * Decode a string previously encoded with encodestring.
 * @param[in]	lpszA	The string containing the hexadecimal
 * 						representation of the UTF-8 encoded string.
 * @return				The original wide string.
 */
static wstring decodestring(const char *lpszA) {
	const utf8string u8 = utf8string::from_string(hex2bin(lpszA));
	return convert_to<wstring>(u8);
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
static LONG __stdcall AdviseCallback(void *lpContext, ULONG cNotif,
    LPNOTIFICATION lpNotif)
{
	pthread_mutex_lock(&hMutexMessagesWaiting);
	for (ULONG i = 0; i < cNotif; ++i) {
		if (lpNotif[i].info.tab.ulTableEvent == TABLE_RELOAD) {
			// Table needs a reload - trigger a reconnect with the server
			nReload = true;
			bMessagesWaiting = true;
			pthread_cond_signal(&hCondMessagesWaiting);
		} 
		else if (lpNotif[i].info.tab.ulTableEvent != TABLE_ROW_DELETED) {
			bMessagesWaiting = true;
			pthread_cond_signal(&hCondMessagesWaiting);
			break;
		}
	}
	pthread_mutex_unlock(&hMutexMessagesWaiting);

	return 0;
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
#ifdef LINUX
/**
 * Starts a forked process which sends the actual mail, and removes it
 * from the queue, in normal situations.  On error, the
 * CleanFinishedMessages function will try to remove the failed
 * message if needed, else it will be tried again later (eg. timestamp
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
    int ulSMTPPort, const char *szPath, ULONG cbStoreEntryId,
    BYTE *lpStoreEntryId, ULONG cbMsgEntryId, BYTE *lpMsgEntryId,
    ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	SendData sSendData;
	pid_t pid;
	bool bDoSentMail = ulFlags & EC_SUBMIT_DOSENTMAIL;
	std::string strPort = stringify(ulSMTPPort);

	// place pid with entryid copy in map
	sSendData.cbStoreEntryId = cbStoreEntryId;
	hr = MAPIAllocateBuffer(cbStoreEntryId, (void**)&sSendData.lpStoreEntryId);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "StartSpoolerFork(): MAPIAllocateBuffer failed(1) %x", hr);
		goto exit;
	}

	memcpy(sSendData.lpStoreEntryId, lpStoreEntryId, cbStoreEntryId);
	sSendData.cbMessageEntryId = cbMsgEntryId;
	hr = MAPIAllocateBuffer(cbMsgEntryId, (void**)&sSendData.lpMessageEntryId);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "StartSpoolerFork(): MAPIAllocateBuffer failed(2) %x", hr);
		goto exit;
	}
	memcpy(sSendData.lpMessageEntryId, lpMsgEntryId, cbMsgEntryId);
	sSendData.ulFlags = ulFlags;
	sSendData.strUsername = szUsername;

	// execute the new spooler process to send the email
	pid = vfork();
	if (pid < 0) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, string("Unable to start new spooler process: ") + strerror(errno));
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	if (pid == 0) {
		char *bname = strdup(szCommand);
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s NULL",
			  szCommand, basename(bname) /* argv[0] */,
			  "--send-message-entryid", bin2hex(cbMsgEntryId, lpMsgEntryId).c_str(),
			  "--send-username-enc", encodestring(szUsername).c_str(),
			  "--log-fd", stringify(g_lpLogger->GetFileDescriptor()).c_str(),
			  "--config", szConfig,
			  "--host", szPath,
			  "--foreground", szSMTP,
			  "--port", strPort.c_str(),
			  bDoSentMail ? "--do-sentmail" : "");
#ifdef SPOOLER_FORK_DEBUG
		_exit(EXIT_WAIT);
#else
		// we execute because of all the mapi memory in use would be duplicated in the child,
		// and there won't be a nice way to clean it all up.
		execlp(szCommand, basename(bname) /* argv[0] */,
			  "--send-message-entryid", bin2hex(cbMsgEntryId, lpMsgEntryId).c_str(),
			  "--send-username-enc", encodestring(szUsername).c_str(),
			  "--log-fd", stringify(g_lpLogger->GetFileDescriptor()).c_str(),
			  "--config", szConfig,
			  "--host", szPath,
			  "--foreground", szSMTP, 
			  "--port", strPort.c_str(),
			  bDoSentMail ? "--do-sentmail" : NULL, NULL);
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, string("Cannot start spooler process `") + szCommand + "`: " + strerror(errno));
		_exit(EXIT_REMOVE);
#endif
	}

	g_lpLogger->Log(EC_LOGLEVEL_INFO, "Spooler process started on pid %d", pid);

	// process is started, place in map
	mapSendData[pid] = sSendData;

exit:
	return hr;
}
#endif

/**
 * Opens all required objects of the administrator to move an error
 * mail out of the queue.
 *
 * @param[in]	sSendData		Struct with information about the mail in the queue which caused a fatal error
 * @param[in]	lpAdminSession	MAPI session of Kopano SYSTEM user
 * @param[out]	lppAddrBook		MAPI Addressbook object
 * @param[out]	lppMailer		inetmapi ECSender object, which can generate an error text for the body for the mail
 * @param[out]	lppUserAdmin	The administrator user in an ECUSER struct
 * @param[out]	lppUserStore	The store of the user with the error mail, open with admin rights
 * @param[out]	lppMessage		The message of the user which caused the error, open with admin rights
 * @return		HRESULT
 */
static HRESULT GetErrorObjects(const SendData &sSendData,
    IMAPISession *lpAdminSession, IAddrBook **lppAddrBook,
    ECSender **lppMailer, ECUSER **lppUserAdmin, IMsgStore **lppUserStore,
    IMessage **lppMessage)
{
	HRESULT hr = hrSuccess;
	ULONG ulObjType = 0;
	IECServiceAdmin	*lpServiceAdmin = NULL;
	LPSPropValue lpsProp = NULL;

	if (*lppAddrBook == NULL) {
		hr = lpAdminSession->OpenAddressBook(0, NULL, AB_NO_DIALOG, lppAddrBook);
		if (hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open addressbook for error mail, skipping. Error 0x%08X", hr);
			goto exit;
		}
	}

	if (*lppMailer == NULL) {
		*lppMailer = CreateSender(g_lpLogger, "localhost", 25); // SMTP server does not matter here, we just use the object for the error body
		if (! (*lppMailer)) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to create error object for error mail, skipping.");
			goto exit;
		}
	}

	if (*lppUserStore == NULL) {
		hr = lpAdminSession->OpenMsgStore(0, sSendData.cbStoreEntryId, (LPENTRYID)sSendData.lpStoreEntryId, NULL, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, lppUserStore);
		if (hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open store of user for error mail, skipping. Error 0x%08X", hr);
			goto exit;
		}
	}

	if (*lppMessage == NULL) {
		hr = (*lppUserStore)->OpenEntry(sSendData.cbMessageEntryId, (LPENTRYID)sSendData.lpMessageEntryId, &IID_IMessage, MAPI_BEST_ACCESS, &ulObjType, (IUnknown**)lppMessage);
		if (hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open message of user for error mail, skipping. Error 0x%08X", hr);
			goto exit;
		}
	}

	if (*lppUserAdmin == NULL) {
		hr = HrGetOneProp(*lppUserStore, PR_EC_OBJECT, &lpsProp);
		if (hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open ECObject of user for error mail, skipping. Error 0x%08X", hr);
			goto exit;
		}

		hr = ((IECUnknown*)lpsProp->Value.lpszA)->QueryInterface(IID_IECServiceAdmin, (void **)&lpServiceAdmin);
		if (hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "ServiceAdmin interface not supported: %s (%x)",
				GetMAPIErrorMessage(hr), hr);
			goto exit;
		}

		hr = lpServiceAdmin->GetUser(g_cbDefaultEid, (LPENTRYID)g_lpDefaultEid, MAPI_UNICODE, lppUserAdmin);
		if (hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get user admin information from store for user error mail, skipping. Error 0x%08X", hr);
			goto exit;
		}
	}

exit:
	MAPIFreeBuffer(lpsProp);
	if (lpServiceAdmin)
		lpServiceAdmin->Release();

	return hr;
}

/**
 * Cleans finished messages. Normally only prints a logmessage. If the
 * mailer completely failed (eg. segfault), this function will try to
 * remove the faulty mail from the queue.
 *
 * @param[in]	lpAdminSession	MAPI session of the Kopano SYSTEM user
 * @param[in]	lpSpooler		IECSpooler object
 * @return		HRESULT
 */
static HRESULT CleanFinishedMessages(IMAPISession *lpAdminSession,
    IECSpooler *lpSpooler)
{
	HRESULT hr = hrSuccess;
	std::map<pid_t, int>::const_iterator i, iDel;
	SendData sSendData;
	bool bErrorMail;
	map<pid_t, int> finished; // exit status of finished processes
	int status;
	// error message creation
	IAddrBook *lpAddrBook = NULL;
	ECSender *lpMailer = NULL;
	ECUSER *lpUserAdmin = NULL;
	// user error message, release after using
	IMsgStore *lpUserStore = NULL;
	IMessage *lpMessage = NULL;

	pthread_mutex_lock(&hMutexFinished);

	if (mapFinished.empty()) {
		pthread_mutex_unlock(&hMutexFinished);
		return hr;
	}

	// copy map contents and clear it, so hMutexFinished can be unlocked again asap
	finished = mapFinished;
	mapFinished.clear();

	pthread_mutex_unlock(&hMutexFinished);

	g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Cleaning %d messages from queue", (int)finished.size());

	// process finished entries
	for (i = finished.begin(); i != finished.end(); ++i) {
		sSendData = mapSendData[i->first];

		/* Find exit status, and decide to remove mail from queue or not */
		status = i->second;

		bErrorMail = false;

		bool wasSent = false;

#ifdef WEXITSTATUS
		if(WIFEXITED(status)) {					/* Child exited by itself */
			if (WEXITSTATUS(status) == EXIT_WAIT) {
				// timed message, try again later
				g_lpLogger->Log(EC_LOGLEVEL_INFO, "Message for user %ls will be tried again later", sSendData.strUsername.c_str());
				sc -> countInc("Spooler", "exit_wait");
			}
			else if (WEXITSTATUS(status) == EXIT_OK || WEXITSTATUS(status) == EXIT_FAILED) {
				// message was sent, or the user already received an error mail.
				g_lpLogger->Log(EC_LOGLEVEL_INFO, "Processed message for user %ls", sSendData.strUsername.c_str());
				wasSent = true;
			}
			else {
				// message was not sent, and could not be removed from queue. Notify user also.
				bErrorMail = true;
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Failed message for user %ls will be removed from queue, error 0x%x", sSendData.strUsername.c_str(), status);
			}
		}
		else if(WIFSIGNALED(status)) {        /* Child was killed by a signal */
			bErrorMail = true;
			g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "Spooler process %d was killed by signal %d", i->first, WTERMSIG(status));
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Message for user %ls will be removed from queue", sSendData.strUsername.c_str());
			sc -> countInc("Spooler", "sig_killed");
		}
		else {								/* Something strange happened */
			bErrorMail = true;
			g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "Spooler process %d terminated abnormally", i->first);
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Message for user %ls will be removed from queue", sSendData.strUsername.c_str());
			sc -> countInc("Spooler", "abnormal_terminate");
		}
#else
		if (status) {
			bErrorMail = true;
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Spooler process %d exited with status %d", i->first, status);
		}
#endif

		if (wasSent)
			sc -> countInc("Spooler", "sent");
		else if (bErrorMail)
			sc -> countInc("Spooler", "send_failed");

		if (bErrorMail) {
			hr = GetErrorObjects(sSendData, lpAdminSession, &lpAddrBook, &lpMailer, &lpUserAdmin, &lpUserStore, &lpMessage);
			if (hr == hrSuccess) {
				lpMailer->setError(_("A fatal error occurred while processing your message, and Kopano is unable to send your email."));
				hr = SendUndeliverable(lpAddrBook, lpMailer, lpUserStore, lpUserAdmin, lpMessage);
				// TODO: if failed, and we have the lpUserStore, create message?
			}
			if (hr != hrSuccess)
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Failed to create error message for user %ls: %s (%x)",
					sSendData.strUsername.c_str(), GetMAPIErrorMessage(hr), hr);

			// remove mail from queue
			hr = lpSpooler->DeleteFromMasterOutgoingTable(sSendData.cbMessageEntryId, (LPENTRYID)sSendData.lpMessageEntryId, sSendData.ulFlags);
			if (hr != hrSuccess)
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Could not remove invalid message from queue, error code: 0x%08X", hr);

			// move mail to sent items folder
			if (sSendData.ulFlags & EC_SUBMIT_DOSENTMAIL && lpMessage) {
				hr = DoSentMail(lpAdminSession, lpUserStore, 0, lpMessage);
				lpMessage = NULL;
				if (hr != hrSuccess)
					g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to move sent mail to sent-items folder: %s (%x)",
						GetMAPIErrorMessage(hr), hr);
			}

			if (lpUserStore) {
				lpUserStore->Release();
				lpUserStore = NULL;
			}
			if (lpMessage) {
				lpMessage->Release();
				lpMessage = NULL;
			}
		}

		MAPIFreeBuffer(sSendData.lpStoreEntryId);
		MAPIFreeBuffer(sSendData.lpMessageEntryId);
		mapSendData.erase(i->first);
	}

	if (lpAddrBook)
		lpAddrBook->Release();

	delete lpMailer;
	MAPIFreeBuffer(lpUserAdmin);
	if (lpUserStore)
		lpUserStore->Release();

	if (lpMessage)
		lpMessage->Release();

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
static HRESULT ProcessAllEntries(IMAPISession *lpAdminSession,
    IECSpooler *lpSpooler, IMAPITable *lpTable, const char *szSMTP, int ulPort,
    const char *szPath)
{
	HRESULT 	hr				= hrSuccess;
	unsigned int ulMaxThreads	= 0;
	unsigned int ulFreeThreads	= 0;
	ULONG		ulRowCount		= 0;
	LPSRowSet	lpsRowSet		= NULL;
	std::wstring strUsername;
	bool bForceReconnect = false;

	hr = lpTable->GetRowCount(0, &ulRowCount);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get outgoing queue count: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	if (ulRowCount) {
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Number of messages in the queue: %d", ulRowCount);

		sc -> countInc("Spooler", "batch_invokes");
		sc -> countAdd("Spooler", "batch_count", int64_t(ulRowCount));
	}

	ulMaxThreads = atoi(g_lpConfig->GetSetting("max_threads"));
	if (ulMaxThreads == 0)
		ulMaxThreads = 1;

	while(!bQuit) {
		if (lpsRowSet) {
			FreeProws(lpsRowSet);
			lpsRowSet = NULL;
		}

		ulFreeThreads = ulMaxThreads - mapSendData.size();

		if (ulFreeThreads == 0) {
			Sleep(100);
			// remove enties from mapSendData which are finished
			CleanFinishedMessages(lpAdminSession, lpSpooler);
			continue;	/* Continue looping until threads become available */
		}
		
		hr = lpTable->QueryRows(1, 0, &lpsRowSet);
		if (hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to fetch data from table, error code: 0x%08X", hr);
			goto exit;
		}

		if (lpsRowSet->cRows == 0)		// All rows done
			goto exit;

		if (lpsRowSet->aRow[0].lpProps[4].ulPropTag == PR_DEFERRED_SEND_TIME) {
			// check time
			time_t now = time(NULL);
			time_t sendat;
			
			FileTimeToUnixTime(lpsRowSet->aRow[0].lpProps[4].Value.ft, &sendat);
			if (now < sendat) {
				// if we ever add logging here, it should trigger just once for this mail
				continue;
			}
		}

		// Check whether the row contains the entryid and store id
		if (lpsRowSet->aRow[0].lpProps[0].ulPropTag != PR_EC_MAILBOX_OWNER_ACCOUNT_W ||
			lpsRowSet->aRow[0].lpProps[1].ulPropTag != PR_STORE_ENTRYID ||
		    lpsRowSet->aRow[0].lpProps[2].ulPropTag != PR_ENTRYID ||
		    lpsRowSet->aRow[0].lpProps[3].ulPropTag != PR_EC_OUTGOING_FLAGS)
		{
			// Client was quick enough to remove message from queue before we could read it
			g_lpLogger->Log(EC_LOGLEVEL_NOTICE, "Empty row in OutgoingQueue");

			if (lpsRowSet->aRow[0].lpProps[2].ulPropTag == PR_ENTRYID && lpsRowSet->aRow[0].lpProps[3].ulPropTag == PR_EC_OUTGOING_FLAGS) {
				// we can remove this message
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Removing invalid entry from OutgoingQueue");

				hr = lpSpooler->DeleteFromMasterOutgoingTable(lpsRowSet->aRow[0].lpProps[2].Value.bin.cb, (LPENTRYID)lpsRowSet->aRow[0].lpProps[2].Value.bin.lpb, lpsRowSet->aRow[0].lpProps[3].Value.ul);

				if (hr != hrSuccess) {
					g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Could not remove invalid message from queue, error code: 0x%08X", hr);
					// since we have an error, we will reconnect to the server to fully reload the table
					goto exit;
				}
			}
			else {
				// this error makes the spooler disconnect from the server, and reconnect again (bQuit still false)
				bForceReconnect = true;
			}

			continue;
		}

		strUsername = lpsRowSet->aRow[0].lpProps[0].Value.lpszW;
		// Check if there is already an active process for this message
		bool bMatch = false;
		for (std::map<pid_t, SendData>::const_iterator i = mapSendData.begin();
		     i != mapSendData.end(); ++i) {
			if (i->second.cbMessageEntryId == lpsRowSet->aRow[0].lpProps[2].Value.bin.cb &&
				memcmp(i->second.lpMessageEntryId, lpsRowSet->aRow[0].lpProps[2].Value.bin.lpb, i->second.cbMessageEntryId) == 0) {
				bMatch = true;
				break;
			}
		}
		if (bMatch)
			continue;

		// Start new process to send the mail
		hr = StartSpoolerFork(strUsername.c_str(), szSMTP, ulPort, szPath, lpsRowSet->aRow[0].lpProps[1].Value.bin.cb, lpsRowSet->aRow[0].lpProps[1].Value.bin.lpb, lpsRowSet->aRow[0].lpProps[2].Value.bin.cb, lpsRowSet->aRow[0].lpProps[2].Value.bin.lpb, lpsRowSet->aRow[0].lpProps[3].Value.ul);
		if (hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "ProcessAllEntries(): Failed starting spooler: %x", hr);
			goto exit;
		}
	}

exit:
	if (lpsRowSet)
		FreeProws(lpsRowSet);

	return bForceReconnect ? MAPI_E_NETWORK_ERROR : hr;
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
	HRESULT		hr = hrSuccess;
	IECSpooler	*lpSpooler = NULL;
	IMsgStore	*lpMDB = NULL;
	SPropValue	*lpsProp = NULL;

	hr = HrOpenDefaultStore(lpAdminSession, &lpMDB);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open default store for system account. Error 0x%08X", hr);
		goto exit;
	}

	hr = HrGetOneProp(lpMDB, PR_EC_OBJECT, &lpsProp);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to get Kopano internal object: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = ((IECUnknown *)lpsProp->Value.lpszA)->QueryInterface(IID_IECSpooler, (void **)&lpSpooler);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Spooler interface not supported: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	*lppSpooler = lpSpooler;

exit:
	if (lpMDB)
		lpMDB->Release();
	MAPIFreeBuffer(lpsProp);
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
 * @param[in]	szPath	URI of storage server to connect to, must be file:// or https:// with valid ssl certificates.
 * @return		HRESULT
 */
static HRESULT ProcessQueue(const char *szSMTP, int ulPort, const char *szPath)
{
	HRESULT				hr				= hrSuccess;
	IMAPISession		*lpAdminSession = NULL;
	IECSpooler			*lpSpooler		= NULL;
	IMAPITable			*lpTable		= NULL;
	IMAPIAdviseSink		*lpAdviseSink	= NULL;
	ULONG				ulConnection	= 0;

	SizedSPropTagArray(5, sOutgoingCols) = {
		5, {
			PR_EC_MAILBOX_OWNER_ACCOUNT_W,
			PR_STORE_ENTRYID,
			PR_ENTRYID,
			PR_EC_OUTGOING_FLAGS,
			PR_DEFERRED_SEND_TIME,
		}
	};
	
	SSortOrderSet sSort = { 1, 0, 0, { { PR_EC_HIERARCHYID, TABLE_SORT_ASCEND } } };

	hr = HrOpenECAdminSession(g_lpLogger, &lpAdminSession, "kopano-spooler:system", PROJECT_SVN_REV_STR, szPath, EC_PROFILE_FLAGS_NO_PUBLIC_STORE,
							  g_lpConfig->GetSetting("sslkey_file", "", NULL),
							  g_lpConfig->GetSetting("sslkey_pass", "", NULL));
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to open admin session. Error 0x%08X", hr);
		goto exit;
	}

	if (disconnects == 0)
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Connection to storage server succeeded");
	else
		g_lpLogger->Log(EC_LOGLEVEL_INFO, "Connection to storage server succeeded after %d retries", disconnects);

	disconnects = 0;			// first call succeeded, assume all is well.

	hr = GetAdminSpooler(lpAdminSession, &lpSpooler);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "ProcessQueue: GetAdminSpooler failed %x", hr);
		goto exit;
	}

	// Mark reload as done since we reloaded the outgoing table
	nReload = false;
	
	// Request the master outgoing table
	hr = lpSpooler->GetMasterOutgoingTable(0, &lpTable);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Master outgoing queue not available: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = lpTable->SetColumns((LPSPropTagArray)&sOutgoingCols, 0);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to setColumns() on OutgoingQueue: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}
	
	// Sort by ascending hierarchyid: first in, first out queue
	hr = lpTable->SortTable(&sSort, 0);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to SortTable() on OutgoingQueue: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	hr = HrAllocAdviseSink(AdviseCallback, NULL, &lpAdviseSink);	
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Unable to allocate memory for advise sink: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

	// notify on new mail in the outgoing table
	hr = lpTable->Advise(fnevTableModified, lpAdviseSink, &ulConnection);

	while(!bQuit && !nReload) {
		bMessagesWaiting = false;

		lpTable->SeekRow(BOOKMARK_BEGINNING, 0, NULL);

		// also checks not to send a message again which is already sending
		hr = ProcessAllEntries(lpAdminSession, lpSpooler, lpTable, szSMTP, ulPort, szPath);
		if(hr != hrSuccess) {
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "ProcessQueue: ProcessAllEntries failed %x", hr);
			goto exit;
		}

		// Exit signal, break the operation
		if(bQuit)
			break;
			
		if(nReload)
			break;

		pthread_mutex_lock(&hMutexMessagesWaiting);
		if(!bMessagesWaiting) {
			struct timespec timeout;
			struct timeval now;

			// Wait for max 60 sec, then run queue anyway
			gettimeofday(&now,NULL);
			timeout.tv_sec = now.tv_sec + 60;
			timeout.tv_nsec = now.tv_usec * 1000;

			while (!bMessagesWaiting) {
				if (pthread_cond_timedwait(&hCondMessagesWaiting, &hMutexMessagesWaiting, &timeout) == ETIMEDOUT || bMessagesWaiting || bQuit || nReload)
					break;

				// not timed out, no messages waiting, not quit requested, no table reload required:
				// we were triggered for a cleanup call.
				CleanFinishedMessages(lpAdminSession, lpSpooler);
			}
		}

		pthread_mutex_unlock(&hMutexMessagesWaiting);

		// remove any entries that were done during the wait
		CleanFinishedMessages(lpAdminSession, lpSpooler);
	}

exit:
	// when we exit, we must make sure all forks started are cleaned
	if (bQuit) {
		ULONG ulCount = 0;
		ULONG ulThreads = 0;

		while (ulCount < 60) {
			if ((ulCount % 5) == 0) {
				ulThreads = mapSendData.size();
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Still waiting for %d thread%c to exit.", ulThreads, ulThreads!=1?'s':' ');
			}

			CleanFinishedMessages(lpAdminSession, lpSpooler);

			if (mapSendData.size() == 0)
				break;

			Sleep(1000);
			++ulCount;
		}
		if (ulCount == 60)
			g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "%d threads did not yet exit, closing anyway.", (int)mapSendData.size());
	}
	else if (nReload) {
		g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Table reload requested, breaking server connection");
	}

	if (lpTable && ulConnection)
		lpTable->Unadvise(ulConnection);

	if (lpAdviseSink)
		lpAdviseSink->Release();

	if (lpTable)
		lpTable->Release();

	if (lpSpooler)
		lpSpooler->Release();

	if (lpAdminSession)
		lpAdminSession->Release();

	return hr;
}

#ifdef LINUX
/**
 * Segfault signal handler. Prints the backtrace of the crash in the log.
 *
 * @param[in]	signr	Any signal that can dump core. Mostly SIGSEGV.
 */
static void sigsegv(int signr, siginfo_t *si, void *uc)
{
	generic_sigsegv_handler(g_lpLogger, "Spooler",
		PROJECT_VERSION_SPOOLER_STR, signr, si, uc);
}

/** 
 * actual signal handler, direct entry point if only linuxthreads is available.
 * 
 * @param[in] sig signal received
 */
static void process_signal(int sig)
{
	int stat;
	pid_t pid;

	switch (sig) {
	case SIGTERM:
	case SIGINT:
		bQuit = true;
		// Trigger condition so we force wakeup the queue thread
		pthread_mutex_lock(&hMutexMessagesWaiting);
		pthread_cond_signal(&hCondMessagesWaiting);
		pthread_mutex_unlock(&hMutexMessagesWaiting);
		break;

	case SIGCHLD:
		pthread_mutex_lock(&hMutexFinished);
		while ((pid = waitpid (-1, &stat, WNOHANG)) > 0)
			mapFinished[pid] = stat;
		pthread_mutex_unlock(&hMutexFinished);
		// Trigger condition so the messages get cleaned from the queue
		pthread_mutex_lock(&hMutexMessagesWaiting);
		pthread_cond_signal(&hCondMessagesWaiting);
		pthread_mutex_unlock(&hMutexMessagesWaiting);
		break;

	case SIGHUP:
		if (g_lpConfig) {
			if (!g_lpConfig->ReloadSettings() && g_lpLogger)
				g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Unable to reload configuration file, continuing with current settings.");
		}

		if (g_lpLogger) {
			if (g_lpConfig) {
				const char *ll = g_lpConfig->GetSetting("log_level");
				int new_ll = ll ? atoi(ll) : EC_LOGLEVEL_WARNING;
				g_lpLogger->SetLoglevel(new_ll);
			}

			g_lpLogger->Reset();
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Log connection was reset");
		}
		break;

	case SIGUSR2:
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Spooler stats:");
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Running threads: %lu", mapSendData.size());
		pthread_mutex_lock(&hMutexFinished);
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Finished threads: %lu", mapFinished.size());
		pthread_mutex_unlock(&hMutexFinished);
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Disconnects: %d", disconnects);
		break;

	default:
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Unknown signal %d received", sig);
		break;
	}
}

/** 
 * Signal handler thread. Currently handles SIGTERM, SIGINT, SIGCHLD, SIGHUP and SIGUSR2.
 *
 * SIGCHLD waits for the mailer child, and stores the exit status for cleanup from the queue.
 * SIGHUP reloads the config file
 * SIGUSR2 prints some simple stats in the log 
 * 
 * @return 
 */
static void *signal_handler(void *)
{
	int sig;

	g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Signal thread started");

	// already blocking signals

	while (!bQuit && sigwait(&signal_mask, &sig) == 0)
	{
		g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Received signal %d", sig);

		process_signal(sig);
	}
	
	g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Signal thread done");

	return NULL;
}
#endif

/**
 * Main program loop. Calls ProcessQueue, which logs in to MAPI. This
 * way, disconnects are solved. After a disconnect from the server,
 * the loop will try again after 3 seconds.
 *
 * @param[in]	szSMTP	The SMTP server to send to.
 * @param[in]	ulPort	The SMTP port to send to.
 * @param[in]	szPath	URI of storage server to connect to, must be file:// or https:// with valid ssl certificates.
 * @return		HRESULT
 */
static HRESULT running_server(const char *szSMTP, int ulPort,
    const char *szPath)
{
	HRESULT hr = hrSuccess;

	g_lpLogger->Log(EC_LOGLEVEL_ALWAYS, "Starting kopano-spooler version " PROJECT_VERSION_SPOOLER_STR " (" PROJECT_SVN_REV_STR "), pid %d", getpid());
	g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Using SMTP server: %s, port %d", szSMTP, ulPort);

	disconnects = 0;

	while (1) {
		hr = ProcessQueue(szSMTP, ulPort, szPath);

		if (bQuit)
			break;

		if (disconnects == 0)
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "Server connection lost. Reconnecting in 3 seconds...");
		++disconnects;
		Sleep(3000);			// wait 3s until retry to connect
	}

	bQuit = true;				// make sure the sigchld does not use the lock anymore
	pthread_mutex_destroy(&hMutexMessagesWaiting);
	pthread_cond_destroy(&hCondMessagesWaiting);

	return hr;
}

int main(int argc, char *argv[]) {

	HRESULT hr = hrSuccess;
	const char *szPath = NULL;
	const char *szSMTP = NULL;
	int ulPort = 0;
	int c;
	int daemonize = 1;
	int logfd = -1;
	bool bForked = false;
	std::string strMsgEntryId;
	std::wstring strUsername;
	bool bDoSentMail = false;
	bool bIgnoreUnknownConfigOptions = false;

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
		OPT_PORT
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
		{ NULL, 0, NULL, 0 }
	};

	// Default settings
	static const configsetting_t lpDefaults[] = {
		{ "smtp_server","localhost", CONFIGSETTING_RELOADABLE },
		{ "smtp_port","25", CONFIGSETTING_RELOADABLE },
		{ "server_socket", "default:" },
#ifdef LINUX
		{ "run_as_user", "kopano" },
		{ "run_as_group", "kopano" },
		{ "pid_file", "/var/run/kopano/spooler.pid" },
		{ "running_path", "/var/lib/kopano" },
		{ "coredump_enabled", "no" },
#endif
		{ "log_method","file" },
		{ "log_file","-" },
		{ "log_level", "3", CONFIGSETTING_RELOADABLE },
		{ "log_timestamp","1" },
		{ "log_buffer_size", "0" },
		{ "sslkey_file", "" },
		{ "sslkey_pass", "", CONFIGSETTING_EXACT },
		{ "max_threads", "5", CONFIGSETTING_RELOADABLE },
		{ "fax_domain", "", CONFIGSETTING_RELOADABLE },
		{ "fax_international", "+", CONFIGSETTING_RELOADABLE },
		{ "always_send_delegates", "no", CONFIGSETTING_RELOADABLE },
		{ "always_send_tnef", "no", CONFIGSETTING_RELOADABLE },
		{ "always_send_utf8", "no", CONFIGSETTING_RELOADABLE },
		{ "charset_upgrade", "windows-1252", CONFIGSETTING_RELOADABLE },
		{ "allow_redirect_spoofing", "yes", CONFIGSETTING_RELOADABLE },
		{ "allow_delegate_meeting_request", "yes", CONFIGSETTING_RELOADABLE },
		{ "allow_send_to_everyone", "yes", CONFIGSETTING_RELOADABLE },
		{ "copy_delegate_mails", "yes", CONFIGSETTING_RELOADABLE },
		{ "expand_groups", "no", CONFIGSETTING_RELOADABLE },
		{ "archive_on_send", "no", CONFIGSETTING_RELOADABLE },
		{ "enable_dsn", "yes", CONFIGSETTING_RELOADABLE },
        { "plugin_enabled", "yes" },
        { "plugin_path", "/var/lib/kopano/spooler/plugins" },
        { "plugin_manager_path", "/usr/share/kopano-spooler/python" },
		{ "z_statsd_stats", "/var/run/kopano/statsd.sock" },
		{ "tmp_path", "/tmp" },
		{ "tmp_path", "/tmp" },
		{ "tmp_path", "/tmp" },
		{ NULL, NULL },
	};
#ifdef LINUX
    // SIGSEGV backtrace support
    stack_t st;
    struct sigaction act;

    memset(&st, 0, sizeof(st));
    memset(&act, 0, sizeof(act));
#endif

	setlocale(LC_CTYPE, "");
	setlocale(LC_MESSAGES, "");

	while(1) {
		c = my_getopt_long_permissive(argc, argv, "c:h:iuVF", long_options, NULL);

		if(c == -1)
			break;

		switch(c) {
		case OPT_CONFIG:
		case 'c':
			szConfig = optarg;
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
			strUsername = decodestring(optarg);
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
		case 'V':
			cout << "Product version:\t" <<  PROJECT_VERSION_SPOOLER_STR << endl
				 << "File version:\t\t" << PROJECT_SVN_REV_STR << endl;
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

	g_lpConfig = ECConfig::Create(lpDefaults);
	if (szConfig) {
		int argidx = 0;

		if (!g_lpConfig->LoadSettings(szConfig) ||
		    !g_lpConfig->ParseParams(argc - optind, &argv[optind], &argidx) ||
		    (!bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors())) {
			g_lpLogger = new ECLogger_File(EC_LOGLEVEL_INFO, 0, "-", false); // create info logger without a timestamp to stderr
			ec_log_set(g_lpLogger);
			LogConfigErrors(g_lpConfig);
			hr = E_FAIL;
			goto exit;
		}
		
		// ECConfig::ParseParams returns the index in the passed array,
		// after some shuffling, where it stopped parsing. optind is
		// the index where my_getopt_long_permissive stopped parsing. So
		// adding argidx to optind will result in the index after all
		// options are parsed.
		optind += argidx;
	}

	// commandline overwrites spooler.cfg
	if (optind < argc)
		szSMTP = argv[optind];
	else
		szSMTP = g_lpConfig->GetSetting("smtp_server");
	
	if (!ulPort)
		ulPort = atoui(g_lpConfig->GetSetting("smtp_port"));

	szCommand = argv[0];

	// setup logging, use pipe to log if started in forked mode and using pipe (file) logger, create normal logger for syslog
#ifdef LINUX
	if (bForked && logfd != -1)
		g_lpLogger = new ECLogger_Pipe(logfd, 0, atoi(g_lpConfig->GetSetting("log_level")));
	else
#endif
		g_lpLogger = CreateLogger(g_lpConfig, argv[0], "KopanoSpooler");

	ec_log_set(g_lpLogger);
	if ((bIgnoreUnknownConfigOptions && g_lpConfig->HasErrors()) || g_lpConfig->HasWarnings())
		LogConfigErrors(g_lpConfig);

	if (!TmpPath::getInstance() -> OverridePath(g_lpConfig))
		g_lpLogger->Log(EC_LOGLEVEL_ERROR, "Ignoring invalid path-setting!");

#ifdef LINUX
	// detect linuxthreads, which is too broken to correctly run the spooler
	if (!bForked) {
		char buffer[256] = { 0 };
		confstr(_CS_GNU_LIBPTHREAD_VERSION, buffer, sizeof(buffer));

		if (strncmp(buffer, "linuxthreads", strlen("linuxthreads")) == 0) {
			bNPTL = false;
			g_lpConfig->AddSetting("max_threads","1");
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: your system is running with outdated linuxthreads.");
			g_lpLogger->Log(EC_LOGLEVEL_WARNING, "WARNING: the kopano-spooler will only be able to send one message at a time.");
		}
	}
#endif

	// set socket filename
	if (!szPath)
		szPath = g_lpConfig->GetSetting("server_socket");

	if (bForked) {
		// keep sending mail when we're killed in forked mode
		signal(SIGTERM, SIG_IGN);
		signal(SIGINT, SIG_IGN);
#ifdef LINUX
		signal(SIGHUP, SIG_IGN);
		signal(SIGUSR1, SIG_IGN);
		signal(SIGUSR2, SIG_IGN);
#endif
	}
	else {
		pthread_mutex_init(&hMutexFinished, NULL);
		// notification condition
		pthread_mutex_init(&hMutexMessagesWaiting, NULL);
		pthread_cond_init(&hCondMessagesWaiting, NULL);
		sigemptyset(&signal_mask);
		sigaddset(&signal_mask, SIGTERM);
		sigaddset(&signal_mask, SIGINT);
		sigaddset(&signal_mask, SIGCHLD);
		sigaddset(&signal_mask, SIGHUP);
		sigaddset(&signal_mask, SIGUSR2);
	}

#ifdef LINUX
    st.ss_sp = malloc(65536);
    st.ss_flags = 0;
    st.ss_size = 65536;

	act.sa_sigaction = sigsegv;
	act.sa_flags = SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;

    sigaltstack(&st, NULL);
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    sigaction(SIGABRT, &act, NULL);
#endif

	bQuit = bMessagesWaiting = false;

#ifdef LINUX
	if (parseBool(g_lpConfig->GetSetting("coredump_enabled")))
		unix_coredump_enable(g_lpLogger);

	// fork if needed and drop privileges as requested.
	// this must be done before we do anything with pthreads
	if (unix_runas(g_lpConfig, g_lpLogger)) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "main(): run-as failed");
		goto exit;
	}
	if (daemonize && unix_daemonize(g_lpConfig, g_lpLogger)) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "main(): failed daemonizing");
		goto exit;
	}

	if (!daemonize)
		setsid();

	if (bForked == false && unix_create_pidfile(argv[0], g_lpConfig, g_lpLogger, false) < 0) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "main(): Failed creating PID file");
		goto exit;
	}
	g_lpLogger = StartLoggerProcess(g_lpConfig, g_lpLogger);
	ec_log_set(g_lpLogger);
#endif
	g_lpLogger->SetLogprefix(LP_PID);

	hr = MAPIInitialize(NULL);
	if (hr != hrSuccess) {
		g_lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to initialize MAPI: %s (%x)",
			GetMAPIErrorMessage(hr), hr);
		goto exit;
	}

#ifdef LINUX
	if (!bForked) {
		if (bNPTL) {
			// valid for all threads afterwards
			pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
			// create signal handler thread, will handle all blocked signals
			// must be done after the daemonize
			pthread_create(&signal_thread, NULL, signal_handler, NULL);
			set_thread_name(signal_thread, "SignalHandler");
		} else {
			// signal thread not possible, so register all signals separately
			signal(SIGTERM, process_signal);
			signal(SIGINT, process_signal);
			signal(SIGCHLD, process_signal);
			signal(SIGHUP, process_signal);
			signal(SIGUSR2, process_signal);
		}
	}
#endif

	sc = new StatsClient(g_lpLogger);
	sc->startup(g_lpConfig->GetSetting("z_statsd_stats"));
	if (bForked)
		hr = ProcessMessageForked(strUsername.c_str(), szSMTP, ulPort, szPath, strMsgEntryId.length(), (LPENTRYID)strMsgEntryId.data(), bDoSentMail);
	else
			hr = running_server(szSMTP, ulPort, szPath);

	delete sc;

#ifdef LINUX
	if (!bForked) {
		if (bNPTL) {
			g_lpLogger->Log(EC_LOGLEVEL_DEBUG, "Joining signal thread");
			pthread_join(signal_thread, NULL);
			g_lpLogger->Log(EC_LOGLEVEL_INFO, "Spooler shutdown complete");
		}
		else {
			// ignore the death of the pipe logger
			signal(SIGCHLD, SIG_IGN);
		}
	}
#endif

	MAPIUninitialize();

exit:
	delete g_lpConfig;
	DeleteLogger(g_lpLogger);

#ifdef LINUX
	free(st.ss_sp);
#endif

	switch(hr) {
	case hrSuccess:
		return EXIT_OK;

	case MAPI_E_WAIT:			// Timed message
		case MAPI_W_NO_SERVICE:	// SMTP server did not react in forked mode, mail should be retried later
		return EXIT_WAIT;
	}

		// forked: failed sending message, but is already removed from the queue
		return EXIT_FAILED;
}
