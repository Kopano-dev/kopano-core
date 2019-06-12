/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <utility>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <sys/socket.h>
#include <kopano/ECChannel.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
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
using namespace std::string_literals;

int kc_ssl_options(struct soap *soap, char *protos, const char *ciphers,
    const char *prefciphers, const char *curves)
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
		else if (strcasecmp(ssl_name, SSL_TXT_TLSV1) == 0 ||
		    strcasecmp(ssl_name, "TLSv1.0") == 0)
			ssl_proto = 0x04;
#ifdef SSL_TXT_TLSV1_1
		else if (strcasecmp(ssl_name, SSL_TXT_TLSV1_1) == 0)
			ssl_proto = 0x08;
#endif
#ifdef SSL_TXT_TLSV1_2
		else if (strcasecmp(ssl_name, SSL_TXT_TLSV1_2) == 0)
			ssl_proto = 0x10;
#endif
#ifdef SSL_OP_NO_TLSv1_3
		else if (strcasecmp(ssl_name, "TLSv1.3") == 0)
			ssl_proto = 0x20;
#endif
		else if (!ssl_neg) {
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
#ifdef SSL_OP_NO_TLSv1_3
	if ((ssl_exclude & 0x20) != 0)
		ssl_op |= SSL_OP_NO_TLSv1_3;
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
#if !defined(OPENSSL_NO_ECDH) && defined(SSL_CTX_set1_curves_list)
	if (curves && SSL_CTX_set1_curves_list(soap->ctx, curves) != 1) {
		ec_log_crit("Can not set SSL curve list to \"%s\": %s", curves,
			ERR_error_string(ERR_get_error(), 0));
		return KCERR_CALL_FAILED;
	}

	SSL_CTX_set_ecdh_auto(soap->ctx, 1);
#endif

	/* request certificate from client; it is OK if not present. */
	SSL_CTX_set_verify(soap->ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, nullptr);
	return erSuccess;
}

ECSoapServerConnection::ECSoapServerConnection(std::shared_ptr<ECConfig> lpConfig) :
	m_lpConfig(std::move(lpConfig))
{
#ifdef USE_EPOLL
	m_lpDispatcher = new ECDispatcherEPoll(m_lpConfig);
	ec_log_info("Using epoll events");
#else
	m_lpDispatcher = new ECDispatcherSelect(m_lpConfig);
	ec_log_info("Using select events");
#endif
}

ECSoapServerConnection::~ECSoapServerConnection(void)
{
	delete m_lpDispatcher;
}

static int ignore_shutdown(struct soap *, SOAP_SOCKET, int shuttype)
{
	return 0;
}

static void custom_soap_bind(struct soap *soap, const char *bindspec,
    bool v6only, int port = 0, int mode = -1, const char *user = nullptr,
    const char *group = nullptr)
{
#if GSOAP_VERSION >= 20857
	/* The v6only field exists in 2.8.56, but has no effect there. */
	soap->bind_v6only = v6only;
#endif
	soap->sndbuf = soap->rcvbuf = 0;
	soap->bind_flags = SO_REUSEADDR;
	auto ret = ec_listen_generic(bindspec, &soap->master, mode, user, group);
	if (ret < 0) {
		ec_log_crit("Unable to bind to %s: %s. Terminating.", bindspec,
			soap->fault != nullptr ? soap->fault->faultstring : strerror(errno));
		kill(0, SIGTERM);
		exit(1);
	} else if (ret == 0) {
		ec_log_notice("Listening for connections on %s (fd %d)", bindspec, soap->master);
	} else if (ret == 1) {
		soap->fshutdownsocket = ignore_shutdown;
		ec_log_info("Re-using fd %d to listen on %s", soap->socket, bindspec);
	}
	socklen_t sl = sizeof(soap->peer.storage);
	if (getsockname(soap->master, reinterpret_cast<struct sockaddr *>(&soap->peer.storage), &sl) == 0)
		soap->peerlen = std::min(static_cast<socklen_t>(sizeof(soap->peer.storage)), sl);
	soap->port = port;
	soap->socket = soap->master;
	/* ec_listen_generic can return all kinds of AFs. */
	if (soap->peer.addr.sa_family == AF_LOCAL)
		SOAP_CONNECTION_TYPE(soap) = CONNECTION_TYPE_NAMED_PIPE;
}

ECRESULT ECSoapServerConnection::ListenTCP(const char *bindspec,
    bool v6only, int nServerPort)
{
	if (bindspec == nullptr)
		return KCERR_INVALID_PARAMETER;

	//init soap
	std::unique_ptr<struct soap, ec_soap_deleter> lpsSoap(soap_new2(SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING, SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING));
	if (lpsSoap == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	kopano_new_soap_listener(CONNECTION_TYPE_TCP, lpsSoap.get());
	custom_soap_bind(lpsSoap.get(), bindspec, v6only, nServerPort);
	/* Manually check for attachments, independent of streaming support. */
	soap_post_check_mime_attachments(lpsSoap.get());
	m_lpDispatcher->AddListenSocket(std::move(lpsSoap));
	return erSuccess;
}

ECRESULT ECSoapServerConnection::ListenSSL(const char *bindspec, bool v6only,
    int nServerPort, const char *lpszKeyFile, const char *lpszKeyPass,
    const char *lpszCAFile, const char *lpszCAPath)
{
	if (bindspec == nullptr)
		return KCERR_INVALID_PARAMETER;

	std::unique_ptr<char[], cstdlib_deleter> server_ssl_protocols(strdup(m_lpConfig->GetSetting("server_ssl_protocols")));
	const char *server_ssl_ciphers = m_lpConfig->GetSetting("server_ssl_ciphers");
	const char *server_ssl_curves = m_lpConfig->GetSetting("server_ssl_curves");
	auto pref_ciphers = m_lpConfig->GetSetting("server_ssl_prefer_server_ciphers");
	std::unique_ptr<struct soap, ec_soap_deleter> lpsSoap(soap_new2(SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING, SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING));
	if (lpsSoap == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	kopano_new_soap_listener(CONNECTION_TYPE_SSL, lpsSoap.get());
	if (soap_ssl_server_context(lpsSoap.get(),
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
		soap_set_fault(lpsSoap.get());
		auto se = lpsSoap->ssl != nullptr ? soap_ssl_error(lpsSoap.get(), 0, SSL_ERROR_NONE) : 0;
		ec_log_crit("K-2170: Unable to setup SSL context: soap_ssl_server_context: %s: %s", *soap_faultdetail(lpsSoap.get()), se);
		return KCERR_CALL_FAILED;
	}
	auto er = kc_ssl_options(lpsSoap.get(), server_ssl_protocols.get(), server_ssl_ciphers, pref_ciphers, server_ssl_curves);
	if (er != erSuccess)
		return er;
	custom_soap_bind(lpsSoap.get(), bindspec, v6only, nServerPort);
	/* Manually check for attachments, independent of streaming support. */
	soap_post_check_mime_attachments(lpsSoap.get());
	m_lpDispatcher->AddListenSocket(std::move(lpsSoap));
	return erSuccess;
}

ECRESULT ECSoapServerConnection::ListenPipe(const char* lpPipeName, bool bPriority)
{
	if (lpPipeName == nullptr)
		return KCERR_INVALID_PARAMETER;

	//init soap
	std::unique_ptr<struct soap, ec_soap_deleter> lpsSoap(soap_new2(SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING, SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING));
	if (lpsSoap == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	unsigned int mode = S_IRWUG;
	if (bPriority) {
		kopano_new_soap_listener(CONNECTION_TYPE_NAMED_PIPE_PRIORITY, lpsSoap.get());
	} else {
		kopano_new_soap_listener(CONNECTION_TYPE_NAMED_PIPE, lpsSoap.get());
		mode |= S_IROTH | S_IWOTH;
	}
	custom_soap_bind(lpsSoap.get(), lpPipeName, false, 0, mode,
		m_lpConfig->GetSetting("run_as_user"), m_lpConfig->GetSetting("run_as_group"));
	/* Manually check for attachments, independent of streaming support. */
	soap_post_check_mime_attachments(lpsSoap.get());
	m_lpDispatcher->AddListenSocket(std::move(lpsSoap));
	return erSuccess;
}

void ECSoapServerConnection::ShutDown()
{
	m_lpDispatcher->ShutDown();
}

ECRESULT ECSoapServerConnection::DoHUP()
{
	return m_lpDispatcher->DoHUP();
}

ECRESULT ECSoapServerConnection::MainLoop()
{
	return m_lpDispatcher->MainLoop();
}

void ECSoapServerConnection::NotifyDone(struct soap *soap)
{
	m_lpDispatcher->NotifyDone(soap);
}

void ECSoapServerConnection::GetStats(unsigned int *lpulQueueLength,
    time_duration *age, unsigned int *lpulThreadCount, unsigned int *lpulIdleThreads)
{
	*lpulQueueLength = m_lpDispatcher->queue_length();
	*age = m_lpDispatcher->front_item_age();
	m_lpDispatcher->GetThreadCount(lpulThreadCount, lpulIdleThreads);
}
