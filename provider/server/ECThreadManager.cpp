/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include "ECThreadManager.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <poll.h>
#include <unistd.h>
#include <kopano/stringutil.h>
#ifdef HAVE_EPOLL_CREATE
#include <sys/epoll.h>
#endif
#include <kopano/CommonUtil.h>
#include "ECSessionManager.h"
#include "StatsClient.h"
#include "ECServerEntrypoint.h"
#include "ECSoapServerConnection.h"
#include "soapKCmdService.h"

// errors from stdsoap2.h, differs per gSOAP release
#define RETURN_CASE(x) \
	case x: \
		return #x;

using namespace KC;
using std::string;

/*
 * A single work item - it doesn't contain much since we defer all processing, including XML
 * parsing until a worker thread starts processing
 */
class WORKITEM final : public ECTask {
	public:
	virtual ~WORKITEM();
	virtual void run();
	struct soap *xsoap = nullptr; /* socket and state associated with the connection */
	time_point dblReceiveStamp; /* time at which activity was detected on the socket */
	ECDispatcher *dispatcher = nullptr;
};

static string GetSoapError(int err)
{
	switch (err) {
		RETURN_CASE(SOAP_EOF)
		RETURN_CASE(SOAP_CLI_FAULT)
		RETURN_CASE(SOAP_SVR_FAULT)
		RETURN_CASE(SOAP_TAG_MISMATCH)
		RETURN_CASE(SOAP_TYPE)
		RETURN_CASE(SOAP_SYNTAX_ERROR)
		RETURN_CASE(SOAP_NO_TAG)
		RETURN_CASE(SOAP_IOB)
		RETURN_CASE(SOAP_MUSTUNDERSTAND)
		RETURN_CASE(SOAP_NAMESPACE)
		RETURN_CASE(SOAP_USER_ERROR)
		RETURN_CASE(SOAP_FATAL_ERROR)
		RETURN_CASE(SOAP_FAULT)
		RETURN_CASE(SOAP_NO_METHOD)
		RETURN_CASE(SOAP_NO_DATA)
		RETURN_CASE(SOAP_GET_METHOD)
		RETURN_CASE(SOAP_PUT_METHOD)
		RETURN_CASE(SOAP_DEL_METHOD)
		RETURN_CASE(SOAP_HTTP_METHOD)
		RETURN_CASE(SOAP_EOM)
		RETURN_CASE(SOAP_MOE)
		RETURN_CASE(SOAP_HDR)
		RETURN_CASE(SOAP_NULL)
		RETURN_CASE(SOAP_DUPLICATE_ID)
		RETURN_CASE(SOAP_MISSING_ID)
		RETURN_CASE(SOAP_HREF)
		RETURN_CASE(SOAP_UDP_ERROR)
		RETURN_CASE(SOAP_TCP_ERROR)
		RETURN_CASE(SOAP_HTTP_ERROR)
		RETURN_CASE(SOAP_SSL_ERROR)
		RETURN_CASE(SOAP_ZLIB_ERROR)
		RETURN_CASE(SOAP_DIME_ERROR)
		RETURN_CASE(SOAP_DIME_HREF)
		RETURN_CASE(SOAP_DIME_MISMATCH)
		RETURN_CASE(SOAP_DIME_END)
		RETURN_CASE(SOAP_MIME_ERROR)
		RETURN_CASE(SOAP_MIME_HREF)
		RETURN_CASE(SOAP_MIME_END)
		RETURN_CASE(SOAP_VERSIONMISMATCH)
		RETURN_CASE(SOAP_PLUGIN_ERROR)
		RETURN_CASE(SOAP_DATAENCODINGUNKNOWN)
		RETURN_CASE(SOAP_REQUIRED)
		RETURN_CASE(SOAP_PROHIBITED)
		RETURN_CASE(SOAP_OCCURS)
		RETURN_CASE(SOAP_LENGTH)
		RETURN_CASE(SOAP_FD_EXCEEDED)
	}
	return stringify(err);
}

WORKITEM::~WORKITEM()
{
	if (xsoap == nullptr)
		return;
	/*
	 * This part only runs when ECDispatcher ends operation and unprocessed
	 * items remain in the queue. Then, ~ECThreadPool will clear all the
	 * tasks, and the soap must be released.
	 */
	kopano_end_soap_connection(xsoap);
	soap_free(xsoap);
}

void WORKITEM::run()
{
	kcsrv_blocksigs();
	auto lpWorkItem = this;
	int err = 0;
	pthread_t thrself = pthread_self();
	struct soap *soap = lpWorkItem->xsoap;
	lpWorkItem->xsoap = nullptr; /* Snatch soap away from ~WORKITEM */
	set_thread_name(thrself, (m_worker->m_pool->m_poolname + "/" + soap->host).c_str());

	// For SSL connections, we first must do the handshake and pass it back to the queue
	if (soap->ctx && soap->ssl == nullptr) {
		err = soap_ssl_accept(soap);
		if (err) {
			auto d1 = soap_faultstring(soap);
			auto d = soap_faultdetail(soap);
			ec_log_warn("K-2171: soap_ssl_accept: %s (%s)",
				d1 != nullptr && *d1 != nullptr ? *d1 : "(no error set)",
				d != nullptr && *d != nullptr ? *d : "");
		}
	} else {
		// Record start of handling of this request
		auto dblStart = std::chrono::steady_clock::now();
		// Reset last session ID so we can use it reliably after the call is done
		auto info = soap_info(soap);
		info->ulLastSessionId = 0;
		// Pass information on start time of the request into soap->user, so that it can be applied to the correct
		// session after XML parsing
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &info->threadstart);
		info->start = decltype(dblStart)::clock::now();
		info->szFname = nullptr;
		info->fdone = NULL;

		// Do processing of work item
		soap_begin(soap);
		if (soap_begin_recv(soap)) {
			if (soap->error < SOAP_STOP) {
				// Client Updater returns 404 to the client to say it doesn't need to update, so skip this HTTP error
				if (soap->error != SOAP_EOF && soap->error != 404)
					ec_log_debug("gSOAP error on receiving request: %s", GetSoapError(soap->error).c_str());
				soap_send_fault(soap);
				goto done;
			}
			soap_closesock(soap);
			goto done;
		}

		// WARNING
		//
		// From the moment we call soap_serve_request, the soap object MAY be handled
		// by another thread. In this case, soap_serve_request() returns SOAP_NULL. We
		// can NOT rely on soap->error being this value since the other thread may already
		// have overwritten the error value.
		if (soap_envelope_begin_in(soap) || soap_recv_header(soap) ||
		    soap_body_begin_in(soap)) {
			err = soap->error;
		} else {
			try {
				err = KCmdService(soap).dispatch();
			} catch (const int &) {
				/* matching part is in cmd.cpp: "throw SOAP_NULL;" (23) */
				// Reply processing is handled by the callee, totally ignore the rest of processing for this item
				return;
			}
		}

		if (err) {
			ec_log_debug("gSOAP error on processing request: %s", GetSoapError(err).c_str());
			soap_send_fault(soap);
			goto done;
		}

done:
		if (info->fdone != nullptr)
			info->fdone(soap, info->fdoneparam);

		auto dblEnd = decltype(dblStart)::clock::now();
		// Tell the session we're done processing the request for this session. This will also tell the session that this
		// thread is done processing the item, so any time spent in this thread until now can be accounted in that session.
		g_lpSessionManager->RemoveBusyState(info->ulLastSessionId, thrself);
		// Track cpu usage server-wide
		g_lpSessionManager->m_stats->inc(SCN_SOAP_REQUESTS);
		g_lpSessionManager->m_stats->inc(SCN_PROCESSING_TIME, std::chrono::duration_cast<std::chrono::milliseconds>(dblEnd - dblStart).count());
		g_lpSessionManager->m_stats->inc(SCN_RESPONSE_TIME, std::chrono::duration_cast<std::chrono::milliseconds>(dblEnd - lpWorkItem->dblReceiveStamp).count());
	}

	// Clear memory used by soap calls. Note that this does not actually
	// undo our soap_new2() call so the soap object is still valid after these calls
	soap_destroy(soap);
	soap_end(soap);
	// We're done processing the item, the workitem's socket is returned to the queue
	dispatcher->NotifyDone(soap);
	set_thread_name(thrself, (m_worker->m_pool->m_poolname + "/idle").c_str());
}

ECDispatcher::ECDispatcher(std::shared_ptr<ECConfig> lpConfig) :
	m_lpConfig(std::move(lpConfig))
{
	// Default socket settings
	m_nRecvTimeout = atoi(m_lpConfig->GetSetting("server_recv_timeout"));
	m_nReadTimeout = atoi(m_lpConfig->GetSetting("server_read_timeout"));
	m_nSendTimeout = atoi(m_lpConfig->GetSetting("server_send_timeout"));
}

ECDispatcher::~ECDispatcher()
{
	for (auto &s : m_setListenSockets)
		kopano_end_soap_listener(s.second.get());
}

void ECDispatcher::GetThreadCount(unsigned int *lpulThreads, unsigned int *lpulIdleThreads)
{
	size_t a, i;
	m_pool.thread_counts(&a, &i);
	*lpulThreads = a;
	*lpulIdleThreads = i;
}

// Get the age (in seconds) of the next-in-line item in the queue, or 0 if the queue is empty
time_duration ECDispatcher::front_item_age()
{
	return m_pool.front_item_age();
}

size_t ECDispatcher::queue_length()
{
	return m_pool.queue_length() + m_prio.queue_length();
}

void ECDispatcher::AddListenSocket(std::unique_ptr<struct soap, ec_soap_deleter> &&soap)
{
	soap->recv_timeout = m_nReadTimeout; // Use m_nReadTimeout, the value for timeouts during XML reads
	soap->send_timeout = m_nSendTimeout;
	m_setListenSockets.emplace(soap->socket, std::move(soap));
}

void ECDispatcher::QueueItem(struct soap *soap)
{
	auto item = new WORKITEM;
	CONNECTION_TYPE ulType;

	item->xsoap = soap;
	item->dblReceiveStamp = std::chrono::steady_clock::now();
	item->dispatcher = this;
	ulType = SOAP_CONNECTION_TYPE(soap);
	if (ulType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY)
		m_prio.enqueue(item, true);
	else
		m_pool.enqueue(item, true);
}

// Called by a worker thread when it's done with an item
void ECDispatcher::NotifyDone(struct soap *soap)
{
    // During exit, don't requeue active sockets, but close them
    if(m_bExit) {
		kopano_end_soap_connection(soap);
        soap_free(soap);
		return;
	}
	if (soap->socket == SOAP_INVALID_SOCKET) {
		// SOAP has closed the socket, no need to requeue
		kopano_end_soap_connection(soap);
		soap_free(soap);
		return;
	}

	SOAP_SOCKET socket = soap->socket;
	ACTIVESOCKET sActive;
	sActive.soap = soap;
	time(&sActive.ulLastActivity);
	ulock_normal l_sock(m_mutexSockets);
	m_setSockets.emplace(soap->socket, sActive);
	l_sock.unlock();
	// Notify select restart, send socket number which is done
	NotifyRestart(socket);
}

ECRESULT ECDispatcher::DoHUP()
{
	m_nRecvTimeout = atoi(m_lpConfig->GetSetting("server_recv_timeout"));
	m_nReadTimeout = atoi(m_lpConfig->GetSetting("server_read_timeout"));
	m_nSendTimeout = atoi(m_lpConfig->GetSetting("server_send_timeout"));
	m_pool.set_thread_count(atoui(m_lpConfig->GetSetting("threads")),
		atoui(m_lpConfig->GetSetting("thread_limit")));

	for (auto const &p : m_setListenSockets) {
		auto ulType = SOAP_CONNECTION_TYPE(p.second);
		if (ulType != CONNECTION_TYPE_SSL)
			continue;
		if (soap_ssl_server_context(p.second.get(), SOAP_SSL_DEFAULT,
		    m_lpConfig->GetSetting("server_ssl_key_file"),
		    m_lpConfig->GetSetting("server_ssl_key_pass", "", NULL),
		    m_lpConfig->GetSetting("server_ssl_ca_file", "", NULL),
		    m_lpConfig->GetSetting("server_ssl_ca_path", "", NULL),
		    NULL, NULL, "EC")) {
			auto d1 = soap_faultstring(p.second.get());
			auto d = soap_faultdetail(p.second.get());
			ec_log_crit("K-3904: Unable to setup ssl context: %s (%s)",
				d1 != nullptr && *d1 != nullptr ? *d1 : "(no error set)",
				d != nullptr && *d != nullptr ? *d : "");
			return KCERR_CALL_FAILED;
		}
		auto er = kc_ssl_options(p.second.get(), m_lpConfig->GetSetting("server_tls_min_proto"),
			m_lpConfig->GetSetting("server_ssl_ciphers"),
			m_lpConfig->GetSetting("server_ssl_prefer_server_ciphers"),
			m_lpConfig->GetSetting("server_ssl_curves"));
		if (er != erSuccess)
			ec_log_err("SSL reload failed");
	}
	return erSuccess;
}

void ECDispatcher::ShutDown()
{
    m_bExit = true;
}

ECDispatcherSelect::ECDispatcherSelect(std::shared_ptr<ECConfig> lpConfig) :
	ECDispatcher(std::move(lpConfig))
{
    int pipes[2];
    pipe(pipes);
	/* No fd relocation, as this is using select. */
	// Create a pipe that we can use to trigger select() to return
    m_fdRescanRead = pipes[0];
    m_fdRescanWrite = pipes[1];
}

ECRESULT ECDispatcherSelect::MainLoop()
{
	ECRESULT er = erSuccess;
	int maxfds = getdtablesize();
	if (maxfds < 0)
		throw std::runtime_error("getrlimit failed");
    time_t now;
	CONNECTION_TYPE ulType;
	auto pollfd = std::make_unique<struct pollfd[]>(maxfds);

	for (size_t n = 0; n < maxfds; ++n)
		pollfd[n].events = POLLIN;

    // This will start the threads
	m_pool.set_thread_count(atoui(m_lpConfig->GetSetting("threads")), atoui(m_lpConfig->GetSetting("thread_limit")));
	m_pool.enable_watchdog(true, m_lpConfig);
	m_prio.set_thread_count(1);

    // Main loop
    while(!m_bExit) {
		if (sv_sighup_flag)
			sv_sighup_sync();
		int nfds = 0, pfd_begin_sock, pfd_begin_listen;
        time(&now);

        // Listen on rescan trigger
		pollfd[nfds++].fd = m_fdRescanRead;

        // Listen on active sockets
		ulock_normal l_sock(m_mutexSockets);
		pfd_begin_sock = nfds;
		for (const auto &p : m_setSockets) {
			ulType = SOAP_CONNECTION_TYPE(p.second.soap);
			if (ulType != CONNECTION_TYPE_NAMED_PIPE &&
			    ulType != CONNECTION_TYPE_NAMED_PIPE_PRIORITY &&
				now - static_cast<time_t>(p.second.ulLastActivity) > m_nRecvTimeout)
				// Socket has been inactive for more than server_recv_timeout seconds, close the socket
				shutdown(p.second.soap->socket, SHUT_RDWR);
			pollfd[nfds++].fd = p.second.soap->socket;
        }
        // Listen on listener sockets
		pfd_begin_listen = nfds;
		for (const auto &p : m_setListenSockets)
			pollfd[nfds++].fd = p.second->socket;
		l_sock.unlock();

        // Wait for at most 1 second, so that we can close inactive sockets
        // Wait for activity
		auto n = poll(pollfd.get(), nfds, 1 * 1000);
		if (n < 0)
            continue; // signal caught, restart
		if (pollfd[0].revents & POLLIN) {
            char s[128];
            // A socket rescan has been triggered, we don't need to do anything, just read the data, discard it
            // and restart the select call
            read(m_fdRescanRead, s, sizeof(s));
        }

        // Search for activity on active sockets
		l_sock.lock();
	auto iterSockets = m_setSockets.cbegin();
		for (size_t i = pfd_begin_sock; i < pfd_begin_listen; ++i) {
			if (!(pollfd[i].revents & POLLIN))
				continue;
			/*
			 * Forward to the data structure belonging to the pollfd.
			 * (The order of pollfd and m_setSockets is the same,
			 * so the element has to be there.)
			 */
			while (iterSockets != m_setSockets.cend() && iterSockets->second.soap->socket != pollfd[i].fd)
				++iterSockets;
			if (iterSockets == m_setSockets.cend()) {
				ec_log_err("K-1577: socket lost");
				/* something is very off - try again at next iteration */
				break;
			}

		// Activity on a TCP/pipe socket
		// First, check for EOF
			char s = 0;
			if (recv(pollfd[i].fd, &s, 1, MSG_PEEK) == 0) {
			// EOF occurred, just close the socket and remove it from the socket list
			kopano_end_soap_connection(iterSockets->second.soap);
			soap_free(iterSockets->second.soap);
				iterSockets = m_setSockets.erase(iterSockets);
				continue;
			}
			// Actual data waiting, push it on the processing queue
			QueueItem(iterSockets->second.soap);
			// Remove socket from listen list for now, since we're already handling data there and don't
			// want to interfere with the thread that is now handling that socket. It will be passed back
			// to us when the request is done.
			iterSockets = m_setSockets.erase(iterSockets);
		}
		l_sock.unlock();

        // Search for activity on listen sockets
		auto sockiter = m_setListenSockets.cbegin();
		for (size_t i = pfd_begin_listen; i < nfds; ++i) {
			if (!(pollfd[i].revents & POLLIN))
				continue;
			while (sockiter != m_setListenSockets.cend() && sockiter->second->socket != pollfd[i].fd)
				++sockiter;
			if (sockiter == m_setListenSockets.cend()) {
				ec_log_err("K-1578: socket lost");
				break;
			}
			const auto &p = *sockiter;
			ACTIVESOCKET sActive;
			auto newsoap = soap_copy(p.second.get());
			if (newsoap == NULL) {
				ec_log_crit("Unable to accept new connection: out of memory");
				continue;
			}
			kopano_new_soap_connection(SOAP_CONNECTION_TYPE(p.second), newsoap);
			// Record last activity (now)
			time(&sActive.ulLastActivity);
			ulType = SOAP_CONNECTION_TYPE(p.second);
			if (ulType == CONNECTION_TYPE_NAMED_PIPE ||
			    ulType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY) {
				socklen_t socklen = sizeof(newsoap->peer.storage);
				newsoap->socket = accept(newsoap->master, &newsoap->peer.addr, &socklen);
				newsoap->peerlen = socklen;
				if (newsoap->socket == SOAP_INVALID_SOCKET ||
				    socklen > sizeof(newsoap->peer.storage)) {
					newsoap->peerlen = 0;
					memset(&newsoap->peer, 0, sizeof(newsoap->peer));
				}
				/* Do like gsoap's soap_accept would */
				newsoap->keep_alive = -(((newsoap->imode | newsoap->omode) & SOAP_IO_KEEPALIVE) != 0);
			} else {
				soap_accept(newsoap);
			}
			if (newsoap->socket == SOAP_INVALID_SOCKET) {
				if (ulType == CONNECTION_TYPE_NAMED_PIPE)
					ec_log_debug("Error accepting incoming connection from file://%s: %s", m_lpConfig->GetSetting("server_pipe_name"), strerror(newsoap->errnum));
				else if (ulType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY)
					ec_log_debug("Error accepting incoming connection from file://%s: %s", m_lpConfig->GetSetting("server_pipe_priority"), strerror(newsoap->errnum));
				else
					ec_log_debug("Error accepting incoming connection from network: %s", *soap_faultstring(newsoap));
				kopano_end_soap_connection(newsoap);
				soap_free(newsoap);
				continue;
			}
			newsoap->socket = ec_relocate_fd(newsoap->socket);
			g_lpSessionManager->m_stats->Max(SCN_MAX_SOCKET_NUMBER, static_cast<LONGLONG>(newsoap->socket));
			g_lpSessionManager->m_stats->inc(SCN_SERVER_CONNECTIONS);
			sActive.soap = newsoap;
			l_sock.lock();
			m_setSockets.emplace(sActive.soap->socket, sActive);
			l_sock.unlock();
		}
	}

    // Set the thread count to zero so that threads will exit
	m_pool.set_thread_count(0, 0, true);
	m_prio.set_thread_count(0, 0, true);

	// Close all sockets. This will cause all that we were listening on clients to get an EOF
	ulock_normal l_sock(m_mutexSockets);
	for (const auto &p : m_setSockets) {
		kopano_end_soap_connection(p.second.soap);
		soap_free(p.second.soap);
	}
	m_setSockets.clear();
	l_sock.unlock();
    return er;
}

void ECDispatcherSelect::ShutDown()
{
	ECDispatcher::ShutDown();
    char s = 0;
    // Notify select wakeup
    write(m_fdRescanWrite, &s, 1);
}

void ECDispatcherSelect::NotifyRestart(SOAP_SOCKET s)
{
	write(m_fdRescanWrite, &s, sizeof(SOAP_SOCKET));
}

#ifdef HAVE_EPOLL_CREATE
ECDispatcherEPoll::ECDispatcherEPoll(std::shared_ptr<ECConfig> lpConfig) :
	ECDispatcher(std::move(lpConfig))
{
	m_fdMax = getdtablesize();
	if (m_fdMax < 0)
		throw std::runtime_error("getrlimit failed");
	m_epFD = epoll_create(m_fdMax);
	if (m_epFD < 0)
		throw std::runtime_error("epoll_create failed");
}

ECDispatcherEPoll::~ECDispatcherEPoll()
{
	if (m_epFD >= 0)
		close(m_epFD);
}

ECRESULT ECDispatcherEPoll::MainLoop()
{
	ECRESULT er = erSuccess;
	time_t now = 0, last = 0;
	CONNECTION_TYPE ulType;
	epoll_event epevent;
	auto epevents = make_unique_nt<epoll_event[]>(m_fdMax);
	int n;

	if (epevents == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	// setup epoll for listen sockets
	memset(&epevent, 0, sizeof(epoll_event));
	epevent.events = EPOLLIN | EPOLLPRI; // wait for input and priority (?) events
	for (const auto &pair : m_setListenSockets) {
		epevent.data.fd = pair.second->socket;
		epoll_ctl(m_epFD, EPOLL_CTL_ADD, pair.second->socket, &epevent);
	}

	// This will start the threads
	m_pool.set_thread_count(atoui(m_lpConfig->GetSetting("threads")), atoui(m_lpConfig->GetSetting("thread_limit")));
	m_pool.enable_watchdog(true, m_lpConfig);
	m_prio.set_thread_count(1);

	while (!m_bExit) {
		if (sv_sighup_flag)
			sv_sighup_sync();
		time(&now);

		// find timedout sockets once per second
		ulock_normal l_sock(m_mutexSockets);
		if(now > last) {
			for (const auto &pair : m_setSockets) {
				ulType = SOAP_CONNECTION_TYPE(pair.second.soap);
				if (ulType != CONNECTION_TYPE_NAMED_PIPE &&
				    ulType != CONNECTION_TYPE_NAMED_PIPE_PRIORITY &&
				    now - static_cast<time_t>(pair.second.ulLastActivity) > m_nRecvTimeout)
					// Socket has been inactive for more than server_recv_timeout seconds, close the socket
					shutdown(pair.second.soap->socket, SHUT_RDWR);
            }
            last = now;
        }
		l_sock.unlock();

		n = epoll_wait(m_epFD, epevents.get(), m_fdMax, 1000); // timeout -1 is wait indefinitely
		l_sock.lock();
		for (int i = 0; i < n; ++i) {
			auto iterListenSockets = m_setListenSockets.find(epevents[i].data.fd);

			if (iterListenSockets != m_setListenSockets.end()) {
				// this was a listen socket .. accept and continue
				ACTIVESOCKET sActive;
				auto newsoap = soap_copy(iterListenSockets->second.get());
                kopano_new_soap_connection(SOAP_CONNECTION_TYPE(iterListenSockets->second), newsoap);
				// Record last activity (now)
				time(&sActive.ulLastActivity);
				ulType = SOAP_CONNECTION_TYPE(iterListenSockets->second);
				if (ulType == CONNECTION_TYPE_NAMED_PIPE || ulType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY) {
					newsoap->socket = accept(newsoap->master, NULL, 0);
					/* Do like gsoap's soap_accept would */
					newsoap->keep_alive = -(((newsoap->imode | newsoap->omode) & SOAP_IO_KEEPALIVE) != 0);
				} else {
					soap_accept(newsoap);
				}

				if(newsoap->socket == SOAP_INVALID_SOCKET) {
					if (ulType == CONNECTION_TYPE_NAMED_PIPE)
						ec_log_debug("epaccept(%d) on file://%s: %s", newsoap->master, m_lpConfig->GetSetting("server_pipe_name"), *soap_faultstring(newsoap));
					else if (ulType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY)
						ec_log_debug("epaccept(%d) on file://%s: %s", newsoap->master, m_lpConfig->GetSetting("server_pipe_priority"), *soap_faultstring(newsoap));
					else
						ec_log_debug("epaccept(%d): %s", newsoap->master, *soap_faultstring(newsoap));
					kopano_end_soap_connection(newsoap);
					soap_free(newsoap);
				} else {
					if (ulType == CONNECTION_TYPE_NAMED_PIPE)
						ec_log_debug("Accepted incoming connection from file://%s", m_lpConfig->GetSetting("server_pipe_name"));
					else if (ulType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY)
						ec_log_debug("Accepted incoming connection from file://%s", m_lpConfig->GetSetting("server_pipe_priority"));
					else
						ec_log_debug("Accepted incoming%sconnection from %s",
							ulType == CONNECTION_TYPE_SSL ? " SSL ":" ",
							newsoap->host);
					newsoap->socket = ec_relocate_fd(newsoap->socket);
					g_lpSessionManager->m_stats->Max(SCN_MAX_SOCKET_NUMBER, static_cast<LONGLONG>(newsoap->socket));
					g_lpSessionManager->m_stats->inc(SCN_SERVER_CONNECTIONS);
					// directly make worker thread active
                    sActive.soap = newsoap;
					m_setSockets.emplace(sActive.soap->socket, sActive);
					NotifyRestart(newsoap->socket);
				}
			} else {
				// this is a new request from an existing client
				auto iterSockets = m_setSockets.find(epevents[i].data.fd);
				if (iterSockets != m_setSockets.cend()) {
					// remove from epfd, either close socket, or it will be reactivated later in the epfd
					epevent.data.fd = iterSockets->second.soap->socket;
					epoll_ctl(m_epFD, EPOLL_CTL_DEL, iterSockets->second.soap->socket, &epevent);

					if (epevents[i].events & EPOLLHUP) {
						kopano_end_soap_connection(iterSockets->second.soap);
						soap_free(iterSockets->second.soap);
						m_setSockets.erase(iterSockets);
					} else {
						QueueItem(iterSockets->second.soap);
						// Remove socket from listen list for now, since we're already handling data there and don't
						// want to interfere with the thread that is now handling that socket. It will be passed back
						// to us when the request is done.
						m_setSockets.erase(iterSockets);
					}
				}
			}
		}
		l_sock.unlock();
	}

	m_pool.set_thread_count(0, 0, true);
	m_prio.set_thread_count(0, 0, true);

    // Close all sockets. This will cause all that we were listening on clients to get an EOF
	ulock_normal l_sock(m_mutexSockets);
	for (auto &pair : m_setSockets) {
		kopano_end_soap_connection(pair.second.soap);
		soap_free(pair.second.soap);
    }
	m_setSockets.clear();
	l_sock.unlock();
	return er;
}

void ECDispatcherEPoll::NotifyRestart(SOAP_SOCKET s)
{
	// add soap socket in epoll fd
	epoll_event epevent;
	memset(&epevent, 0, sizeof(epoll_event));
	epevent.events = EPOLLIN | EPOLLPRI; // wait for input and priority (?) events
	epevent.data.fd = s;
	epoll_ctl(m_epFD, EPOLL_CTL_ADD, epevent.data.fd, &epevent);
}
#endif
