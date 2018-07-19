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
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <sys/socket.h>
#include <kopano/ECChannel.h>
#include <kopano/ECLogger.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#include "ECSoapServerConnection.h"
#include "ECServerEntrypoint.h"
#	include <dirent.h>
#	include <fcntl.h>
#	include <unistd.h>
#	include <kopano/UnixUtil.h>

using namespace KC;

/** 
 * Creates a AF_UNIX socket in a given location and starts to listen
 * on that socket.
 * 
 * @param unix_socket the file location of that socket 
 * @param lpLogger a logger object
 * @param bInit unused
 * @param mode change the mode of the file to this value (octal!)
 * 
 * @return the socket we're listening on, or -1 for failure.
 */
static int create_pipe_socket(const char *unix_socket, ECConfig *lpConfig,
    bool bInit, int mode)
{
	int s;
	auto er = ec_listen_localsock(unix_socket, &s);
	if (er < 0) {
		ec_log_crit("Unable to bind to socket %s: %s. This program will terminate now.", unix_socket, strerror(-er));
                kill(0, SIGTERM);
                exit(1);
	}

	er = chmod(unix_socket,mode);
	if(er) {
		ec_log_crit("Unable to chmod socket %s. Error: %s", unix_socket, strerror(errno));
		close(s);
		return -1;
	}

	if(er) {
		ec_log_crit("Unable to chown socket %s, to %s:%s. Error: %s", unix_socket, lpConfig->GetSetting("run_as_user"), lpConfig->GetSetting("run_as_group"), strerror(errno));
		close(s);
		return -1;
	}
	if (listen(s, INT_MAX) < 0) {
		ec_log_crit("Can't listen on unix socket %s: %s", unix_socket, strerror(errno));
		close(s);
		return -1;
	}

	return s;
}

int kc_ssl_options(struct soap *soap, char *protos, const char *ciphers,
    const char *prefciphers)
{
#if !defined(OPENSSL_NO_ECDH) && defined(NID_X9_62_prime256v1)
	EC_KEY *ecdh;
#endif
	char *ssl_name = nullptr;
	int ssl_op = 0, ssl_include = 0, ssl_exclude = 0;

#ifndef SSL_OP_NO_RENEGOTIATION
#	define SSL_OP_NO_RENEGOTIATION 0 /* unavailable in openSSL 1.0 */
#endif
	SSL_CTX_set_options(soap->ctx, SSL_OP_ALL | SSL_OP_NO_RENEGOTIATION);
#if !defined(OPENSSL_NO_ECDH) && defined(NID_X9_62_prime256v1)
	ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	if (ecdh != nullptr) {
		SSL_CTX_set_options(soap->ctx, SSL_OP_SINGLE_ECDH_USE);
		SSL_CTX_set_tmp_ecdh(soap->ctx, ecdh);
		EC_KEY_free(ecdh);
	}
#endif
	ssl_name = strtok(protos, " ");
	while (ssl_name != nullptr) {
		int ssl_proto = 0;
		bool ssl_neg = false;

		if (*ssl_name == '!') {
			++ssl_name;
			ssl_neg = true;
		}

		if (strcasecmp(ssl_name, SSL_TXT_SSLV3) == 0)
			ssl_proto = 0x02;
#ifdef SSL_TXT_SSLV2
		else if (strcasecmp(ssl_name, SSL_TXT_SSLV2) == 0)
			ssl_proto = 0x01;
#endif
		else if (strcasecmp(ssl_name, SSL_TXT_TLSV1) == 0)
			ssl_proto = 0x04;
#ifdef SSL_TXT_TLSV1_1
		else if (strcasecmp(ssl_name, SSL_TXT_TLSV1_1) == 0)
			ssl_proto = 0x08;
#endif
#ifdef SSL_TXT_TLSV1_2
		else if (strcasecmp(ssl_name, SSL_TXT_TLSV1_2) == 0)
			ssl_proto = 0x10;
#endif
		else {
			ec_log_crit("Unknown protocol \"%s\" in protos setting", ssl_name);
			return KCERR_CALL_FAILED;
		}

		if (ssl_neg)
			ssl_exclude |= ssl_proto;
		else
			ssl_include |= ssl_proto;

		ssl_name = strtok(nullptr, " ");
	}

	if (ssl_include != 0)
		// Exclude everything, except those that are included (and let excludes still override those)
		ssl_exclude |= 0x1f & ~ssl_include;
	if ((ssl_exclude & 0x01) != 0)
		ssl_op |= SSL_OP_NO_SSLv2;
	if ((ssl_exclude & 0x02) != 0)
		ssl_op |= SSL_OP_NO_SSLv3;
	if ((ssl_exclude & 0x04) != 0)
		ssl_op |= SSL_OP_NO_TLSv1;
#ifdef SSL_OP_NO_TLSv1_1
	if ((ssl_exclude & 0x08) != 0)
		ssl_op |= SSL_OP_NO_TLSv1_1;
#endif
#ifdef SSL_OP_NO_TLSv1_2
	if ((ssl_exclude & 0x10) != 0)
		ssl_op |= SSL_OP_NO_TLSv1_2;
#endif
	if (protos != nullptr)
		SSL_CTX_set_options(soap->ctx, ssl_op);
	if (ciphers && SSL_CTX_set_cipher_list(soap->ctx, ciphers) != 1) {
		ec_log_crit("Can not set SSL cipher list to \"%s\": %s",
			ciphers, ERR_error_string(ERR_get_error(), 0));
		return KCERR_CALL_FAILED;
	}
	if (parseBool(prefciphers))
		SSL_CTX_set_options(soap->ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

	/* request certificate from client; it is OK if not present. */
	SSL_CTX_set_verify(soap->ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, nullptr);
	return erSuccess;
}

ECSoapServerConnection::ECSoapServerConnection(ECConfig *lpConfig) :
	m_lpConfig(lpConfig)
{
#ifdef USE_EPOLL
	m_lpDispatcher = new ECDispatcherEPoll(lpConfig);
	ec_log_info("Using epoll events");
#else
	m_lpDispatcher = new ECDispatcherSelect(lpConfig);
	ec_log_info("Using select events");
#endif
}

ECSoapServerConnection::~ECSoapServerConnection(void)
{
	delete m_lpDispatcher;
}

ECRESULT ECSoapServerConnection::ListenTCP(const char *lpServerName, int nServerPort)
{
	int			socket = SOAP_INVALID_SOCKET;
	struct soap	*lpsSoap = NULL;

	if (lpServerName == nullptr)
		return KCERR_INVALID_PARAMETER;

	//init soap
	lpsSoap = soap_new2(SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING, SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING);
	kopano_new_soap_listener(CONNECTION_TYPE_TCP, lpsSoap);
	lpsSoap->sndbuf = lpsSoap->rcvbuf = 0;
	lpsSoap->bind_flags = SO_REUSEADDR;
#if GSOAP_VERSION >= 20857
	/* The v6only field exists in 2.8.56, but has no effect. */
	lpsSoap->bind_v6only = strcmp(lpServerName, "*") != 0;
#endif
	lpsSoap->socket = socket = soap_bind(lpsSoap, *lpServerName == '\0' ? NULL : lpServerName, nServerPort, 100);
        if (socket == -1) {
                ec_log_crit("Unable to bind to port %d: %s. This is usually caused by another process (most likely another server) already using this port. This program will terminate now.", nServerPort, lpsSoap->fault->faultstring);
                kill(0, SIGTERM);
                exit(1);
        }

	m_lpDispatcher->AddListenSocket(lpsSoap);
    
	/* Manually check for attachments, independent of streaming support. */
	soap_post_check_mime_attachments(lpsSoap);
	ec_log_notice("Listening for TCP connections on port %d", nServerPort);
	return erSuccess;
}

ECRESULT ECSoapServerConnection::ListenSSL(const char *lpServerName,
    int nServerPort, const char *lpszKeyFile, const char *lpszKeyPass,
    const char *lpszCAFile, const char *lpszCAPath)
{
	ECRESULT	er = erSuccess;
	int			socket = SOAP_INVALID_SOCKET;
	struct soap	*lpsSoap = NULL;

	if (lpServerName == nullptr)
		return KCERR_INVALID_PARAMETER;

	char *server_ssl_protocols = strdup(m_lpConfig->GetSetting("server_ssl_protocols"));
	const char *server_ssl_ciphers = m_lpConfig->GetSetting("server_ssl_ciphers");
	auto pref_ciphers = m_lpConfig->GetSetting("server_ssl_prefer_server_ciphers");

	lpsSoap = soap_new2(SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING, SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING);
	kopano_new_soap_listener(CONNECTION_TYPE_SSL, lpsSoap);
	lpsSoap->sndbuf = lpsSoap->rcvbuf = 0;

	if (soap_ssl_server_context(
			lpsSoap,
			SOAP_SSL_DEFAULT,	// we set SSL_VERIFY_PEER and more soon ourselves
			lpszKeyFile,		// key file
			lpszKeyPass,		// key password
			lpszCAFile,			// CA certificate file which signed clients
			lpszCAPath,			// CA certificate path of thrusted sources
			NULL,				// dh file, null == rsa
			NULL,				// create random data on the fly (/dev/urandom is slow .. create file?)
			"EC") // unique name for SSL session cache
		)
	{
		soap_set_fault(lpsSoap);
		ec_log_crit("K-2170: Unable to setup ssl context: %s", *soap_faultdetail(lpsSoap));
		er = KCERR_CALL_FAILED;
		goto exit;
	}
	er = kc_ssl_options(lpsSoap, server_ssl_protocols, server_ssl_ciphers, pref_ciphers);
	if (er != erSuccess)
		goto exit;
	lpsSoap->bind_flags = SO_REUSEADDR;
#if GSOAP_VERSION >= 20857
	lpsSoap->bind_v6only = strcmp(lpServerName, "*") != 0;
#endif
	lpsSoap->socket = socket = soap_bind(lpsSoap, *lpServerName == '\0' ? NULL : lpServerName, nServerPort, 100);
        if (socket == -1) {
                ec_log_crit("Unable to bind to port %d: %s (SSL). This is usually caused by another process (most likely another server) already using this port. This program will terminate now.", nServerPort, lpsSoap->fault->faultstring);
                kill(0, SIGTERM);
                exit(1);
        }

	m_lpDispatcher->AddListenSocket(lpsSoap);

	/* Manually check for attachments, independent of streaming support. */
	soap_post_check_mime_attachments(lpsSoap);
	ec_log_notice("Listening for SSL connections on port %d", nServerPort);
exit:
	free(server_ssl_protocols);
	if (er != erSuccess)
		soap_free(lpsSoap);
	return er;
}

ECRESULT ECSoapServerConnection::ListenPipe(const char* lpPipeName, bool bPriority)
{
	ECRESULT	er = erSuccess;
	int			sPipe = -1;
	struct soap	*lpsSoap = NULL;
	socklen_t socklen;

	if (lpPipeName == nullptr)
		return KCERR_INVALID_PARAMETER;

	//init soap
	lpsSoap = soap_new2(SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING, SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING);
	if (bPriority)
		kopano_new_soap_listener(CONNECTION_TYPE_NAMED_PIPE_PRIORITY, lpsSoap);
	else
		kopano_new_soap_listener(CONNECTION_TYPE_NAMED_PIPE, lpsSoap);
	
	// Create a Unix or Windows pipe
	lpsSoap->sndbuf = lpsSoap->rcvbuf = 0;
	// set the mode stricter for the priority socket: let only the same Unix user or root connect on the priority socket, users should not be able to abuse the socket
	lpsSoap->socket = sPipe = create_pipe_socket(lpPipeName, m_lpConfig, true, bPriority ? 0660 : 0666);
	// This just marks the socket as being a pipe, which triggers some slightly different behaviour
	strcpy(lpsSoap->path,"pipe");

	if (sPipe == -1) {
		er = KCERR_CALL_FAILED;
		goto exit;
	}

	lpsSoap->master = sPipe;
	socklen = sizeof(lpsSoap->peer.storage);
	if (getsockname(lpsSoap->socket, &lpsSoap->peer.addr, &socklen) != 0) {
		ec_log_warn("getsockname %s: %s", lpPipeName, strerror(errno));
		socklen = 0;
	} else if (socklen > sizeof(lpsSoap->peer.storage)) {
		socklen = 0;
	}
	lpsSoap->peerlen = socklen;
	if (socklen == 0)
		memset(&lpsSoap->peer, 0, sizeof(lpsSoap->peer));
	m_lpDispatcher->AddListenSocket(lpsSoap);

	/* Manually check for attachments, independent of streaming support. */
	soap_post_check_mime_attachments(lpsSoap);
	ec_log_notice("Listening for %spipe connections on %s", bPriority ? "priority " : "", lpPipeName);
exit:
	if (er != erSuccess)
		soap_free(lpsSoap);
	return er;
}

ECRESULT ECSoapServerConnection::ShutDown()
{
    return m_lpDispatcher->ShutDown();
}

ECRESULT ECSoapServerConnection::DoHUP()
{
	return m_lpDispatcher->DoHUP();
}

ECRESULT ECSoapServerConnection::MainLoop()
{	
	return m_lpDispatcher->MainLoop();
}

ECRESULT ECSoapServerConnection::NotifyDone(struct soap *soap)
{
	return m_lpDispatcher->NotifyDone(soap);
}

ECRESULT ECSoapServerConnection::GetStats(unsigned int *lpulQueueLength, double *lpdblAge,unsigned int *lpulThreadCount, unsigned int *lpulIdleThreads)
{
	ECRESULT er = m_lpDispatcher->GetQueueLength(lpulQueueLength);
    if(er != erSuccess)
		return er;
        
    er = m_lpDispatcher->GetFrontItemAge(lpdblAge);
    if(er != erSuccess)
		return er;
	return m_lpDispatcher->GetThreadCount(lpulThreadCount, lpulIdleThreads);
}
