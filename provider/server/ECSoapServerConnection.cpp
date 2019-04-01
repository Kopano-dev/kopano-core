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
#include "SSLUtil.h"
#	include <dirent.h>
#	include <fcntl.h>
#	include <unistd.h>
#	include <kopano/UnixUtil.h>

using namespace KC;
using namespace std::string_literals;

int kc_ssl_options(struct soap *soap, const char *protos, const char *ciphers,
    const char *prefciphers, const char *curves)
{
#if !defined(OPENSSL_NO_ECDH) && defined(NID_X9_62_prime256v1)
	EC_KEY *ecdh;
#endif
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
	if (!ec_tls_minproto(soap->ctx, protos)) {
		ec_log_crit("Unknown protocol \"%s\" in protos setting", protos);
		return KCERR_CALL_FAILED;
	}
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
	m_lpDispatcher = std::make_unique<ECDispatcherEPoll>(m_lpConfig);
	ec_log_info("Using epoll events");
#else
	m_lpDispatcher = std::make_unique<ECDispatcherSelect>(m_lpConfig);
	ec_log_info("Using select events");
#endif
}

static int ignore_shutdown(struct soap *, SOAP_SOCKET, int shuttype)
{
	return 0;
}

static void custom_soap_bind(struct soap *soap, ec_socket &spec)
{
#if GSOAP_VERSION >= 20857
	/* The v6only field exists in 2.8.56, but has no effect there. */
	soap->bind_v6only = true;
#endif
	soap->sndbuf = soap->rcvbuf = 0;
	soap->bind_flags = SO_REUSEADDR;
	soap->master = soap->socket = spec.m_fd;
	spec.m_fd = -1;
	soap->fshutdownsocket = ignore_shutdown;
	soap->port = spec.m_port;
	soap->peerlen = std::min(sizeof(soap->peer.storage), static_cast<size_t>(spec.m_ai->ai_addrlen));
	memcpy(&soap->peer.storage, spec.m_ai->ai_addr, soap->peerlen);
	/* ec_listen_generic can return all kinds of AFs. */
	if (soap->peer.addr.sa_family == AF_LOCAL)
		SOAP_CONNECTION_TYPE(soap) = CONNECTION_TYPE_NAMED_PIPE;
}

ECRESULT ECSoapServerConnection::ListenTCP(struct ec_socket &spec)
{
	std::unique_ptr<struct soap, ec_soap_deleter> lpsSoap(soap_new2(SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING, SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING));
	if (lpsSoap == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	kopano_new_soap_listener(CONNECTION_TYPE_TCP, lpsSoap.get());
	custom_soap_bind(lpsSoap.get(), spec);
	/* Manually check for attachments, independent of streaming support. */
	soap_post_check_mime_attachments(lpsSoap.get());
	m_lpDispatcher->AddListenSocket(std::move(lpsSoap));
	return erSuccess;
}

ECRESULT ECSoapServerConnection::ListenSSL(struct ec_socket &spec,
    const char *lpszKeyFile, const char *lpszKeyPass,
    const char *lpszCAFile, const char *lpszCAPath)
{
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
	auto er = kc_ssl_options(lpsSoap.get(), m_lpConfig->GetSetting("server_tls_min_proto"),
	          server_ssl_ciphers, pref_ciphers, server_ssl_curves);
	if (er != erSuccess)
		return er;
	custom_soap_bind(lpsSoap.get(), spec);
	/* Manually check for attachments, independent of streaming support. */
	soap_post_check_mime_attachments(lpsSoap.get());
	m_lpDispatcher->AddListenSocket(std::move(lpsSoap));
	return erSuccess;
}

ECRESULT ECSoapServerConnection::ListenPipe(struct ec_socket &spec, bool bPriority)
{
	std::unique_ptr<struct soap, ec_soap_deleter> lpsSoap(soap_new2(SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING, SOAP_IO_KEEPALIVE | SOAP_XML_TREE | SOAP_C_UTFSTRING));
	if (lpsSoap == nullptr)
		return KCERR_NOT_ENOUGH_MEMORY;
	if (bPriority)
		kopano_new_soap_listener(CONNECTION_TYPE_NAMED_PIPE_PRIORITY, lpsSoap.get());
	else
		kopano_new_soap_listener(CONNECTION_TYPE_NAMED_PIPE, lpsSoap.get());
	custom_soap_bind(lpsSoap.get(), spec);
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
