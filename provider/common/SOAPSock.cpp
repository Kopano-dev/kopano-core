/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include "SOAPSock.h"
#include <sys/un.h>
#include "SOAPUtils.h"
#include <kopano/ECLogger.h>
#include <kopano/stringutil.h>
#include <kopano/CommonUtil.h>
#include <string>
#include <map>
#include <kopano/charset/convert.h>
#include "soapKCmdProxy.h"

using namespace KC;

// we cannot patch http_post now (see external/gsoap/*.diff), so we redefine it
static int
http_post(struct soap *soap, const char *endpoint, const char *host, int port, const char *path, const char *action, ULONG64 count)
{ int err;
  if (strlen(endpoint) + strlen(soap->http_version) > sizeof(soap->tmpbuf) - 80
   || strlen(host) + strlen(soap->http_version) > sizeof(soap->tmpbuf) - 80)
    return soap->error = SOAP_EOM;
  sprintf(soap->tmpbuf, "POST /%s HTTP/%s", (*path == '/' ? path + 1 : path), soap->http_version);

	err = soap->fposthdr(soap, soap->tmpbuf, nullptr);
	if (err != 0)
		return err;
	err = soap->fposthdr(soap, "Host", host);
	if (err != 0)
		return err;
	err = soap->fposthdr(soap, "User-Agent", "gSOAP/2.8");
	if (err != 0)
		return err;
	err = soap_puthttphdr(soap, SOAP_OK, count);
	if (err != 0)
		return err;
#ifdef WITH_ZLIB
#ifdef WITH_GZIP
	err = soap->fposthdr(soap, "Accept-Encoding", "gzip, deflate");
#else
	err = soap->fposthdr(soap, "Accept-Encoding", "deflate");
#endif
	if (err != 0)
		return err;
#endif
  return soap->fposthdr(soap, NULL, NULL);
}

// This function wraps the GSOAP fopen call to support "file:///var/run/socket" Unix socket URIs
static int gsoap_connect_pipe(struct soap *soap, const char *endpoint,
    const char *host, int port)
{
	// See stdsoap2.cpp:tcp_connect() function
	if (soap_valid_socket(soap->socket))
	    return SOAP_OK;

	struct sockaddr_un saddr;
	memset(&saddr, 0, sizeof(struct sockaddr_un));
	soap->socket = SOAP_INVALID_SOCKET;

	if (strncmp(endpoint, "file://", 7) != 0)
		return SOAP_EOF;
	const char *socket_name = strchr(endpoint + 7, '/');
	// >= because there also needs to be room for the 0x00
	if (socket_name == NULL ||
	    strlen(socket_name) >= sizeof(saddr.sun_path))
		return SOAP_EOF;
	auto fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return SOAP_EOF;

	saddr.sun_family = AF_UNIX;
	kc_strlcpy(saddr.sun_path, socket_name, sizeof(saddr.sun_path));
	if (connect(fd, (struct sockaddr *)&saddr, sizeof(struct sockaddr_un)) < 0) {
		close(fd);
		return SOAP_EOF;
	}

 	soap->sendfd = soap->recvfd = SOAP_INVALID_SOCKET;
	soap->socket = fd;
	// Because 'file:///var/run/file' will be parsed into host='', path='/var/run/file',
	// the gSOAP code doesn't set the soap->status variable. (see soap_connect_command in
	// stdsoap2.cpp:12278) This could possibly lead to
	// soap->status accidentally being set to SOAP_GET, which would break things. The
	// chances of this happening are, of course, small, but also very real.
	soap->status = SOAP_POST;
	/* Do like gsoap's tcp_connect would */
	soap->keep_alive = -((soap->omode & SOAP_IO_KEEPALIVE) != 0);
   	return SOAP_OK;
}

HRESULT CreateSoapTransport(ULONG ulUIFlags,
	const char *strServerPath,
	const char *strSSLKeyFile,
	const char *strSSLKeyPass,
	ULONG ulConnectionTimeOut,
	const char *strProxyHost,
	WORD wProxyPort,
	const char *strProxyUserName,
	const char *strProxyPassword,
	ULONG ulProxyFlags,
	int				iSoapiMode,
	int				iSoapoMode,
	KCmdProxy **lppCmd)
{
	if (strServerPath == NULL || *strServerPath == '\0' || lppCmd == NULL)
		return E_INVALIDARG;
	auto lpCmd = new KCmdProxy();
	soap_set_imode(lpCmd->soap, iSoapiMode);
	soap_set_omode(lpCmd->soap, iSoapoMode);
	lpCmd->soap_endpoint = strdup(strServerPath);
	lpCmd->soap->sndbuf = lpCmd->soap->rcvbuf = 0;
	lpCmd->soap->maxoccurs = SIZE_MAX; // override default limit of 100000, as this breaks ICS for large folders at least
	// default allow SSLv3, TLSv1, TLSv1.1 and TLSv1.2
	lpCmd->soap->ctx = SSL_CTX_new(SSLv23_method());

#ifdef WITH_OPENSSL
	if (strncmp("https:", lpCmd->soap_endpoint, 6) == 0) {
		// no need to add certificates to call, since soap also calls SSL_CTX_set_default_verify_paths()
		if (soap_ssl_client_context(lpCmd->soap, SOAP_SSL_DEFAULT,
								strSSLKeyFile != NULL && *strSSLKeyFile != '\0' ? strSSLKeyFile : NULL,
								strSSLKeyPass != NULL && *strSSLKeyPass != '\0' ? strSSLKeyPass : NULL,
								NULL, NULL,
								NULL)) {
			free(const_cast<char *>(lpCmd->soap_endpoint));
			lpCmd->destroy();
			delete lpCmd;
			return E_INVALIDARG;
		}
		// set our own certificate check function
		lpCmd->soap->fsslverify = ssl_verify_callback_kopano_silent;
		SSL_CTX_set_verify(lpCmd->soap->ctx, SSL_VERIFY_PEER, lpCmd->soap->fsslverify);
	}
#endif
	if(strncmp("file:", lpCmd->soap_endpoint, 5) == 0) {
		lpCmd->soap->fconnect = gsoap_connect_pipe;
		lpCmd->soap->fpost = http_post;
	} else {
		if ((ulProxyFlags&0x0000001/*EC_PROFILE_PROXY_FLAGS_USE_PROXY*/) && strProxyHost != NULL && *strProxyHost != '\0') {
			lpCmd->soap->proxy_host = strdup(strProxyHost);
			lpCmd->soap->proxy_port = wProxyPort;
			if (strProxyUserName != NULL && *strProxyUserName != '\0')
				lpCmd->soap->proxy_userid = strdup(strProxyUserName);
			if (strProxyPassword != NULL && *strProxyPassword != '\0')
				lpCmd->soap->proxy_passwd = strdup(strProxyPassword);
		}
		lpCmd->soap->connect_timeout = ulConnectionTimeOut;
	}
	*lppCmd = lpCmd;
	return hrSuccess;
}

void DestroySoapTransport(KCmdProxy *lpCmd)
{
	if (!lpCmd)
		return;
	/* strdup'd all of them earlier */
	free(const_cast<char *>(lpCmd->soap_endpoint));
	free(const_cast<char *>(lpCmd->soap->proxy_host));
	free(const_cast<char *>(lpCmd->soap->proxy_userid));
	free(const_cast<char *>(lpCmd->soap->proxy_passwd));
	lpCmd->destroy();
	delete lpCmd;
}

int ssl_verify_callback_kopano_silent(int ok, X509_STORE_CTX *store)
{
	if (ok != 0)
		return ok;
	auto sslerr = X509_STORE_CTX_get_error(store);
	switch (sslerr) {
	case X509_V_ERR_CERT_HAS_EXPIRED:
	case X509_V_ERR_CERT_NOT_YET_VALID:
	case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
	case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
		// always ignore these errors
		X509_STORE_CTX_set_error(store, X509_V_OK);
		ok = 1;
		break;
	default:
		break;
	}
	return ok;
}
