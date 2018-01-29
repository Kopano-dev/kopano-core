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
#include <kopano/charset/utf8string.h>
#include "soapKCmdProxy.h"

// we cannot patch http_post now (see external/gsoap/*.diff), so we redefine it
static int
#if GSOAP_VERSION >= 20850
http_post(struct soap *soap, const char *endpoint, const char *host, int port, const char *path, const char *action, ULONG64 count)
#else
http_post(struct soap *soap, const char *endpoint, const char *host, int port, const char *path, const char *action, size_t count)
#endif
{ int err;
  if (strlen(endpoint) + strlen(soap->http_version) > sizeof(soap->tmpbuf) - 80
   || strlen(host) + strlen(soap->http_version) > sizeof(soap->tmpbuf) - 80)
    return soap->error = SOAP_EOM;
  sprintf(soap->tmpbuf, "POST /%s HTTP/%s", (*path == '/' ? path + 1 : path), soap->http_version);
  if ((err = soap->fposthdr(soap, soap->tmpbuf, NULL)) ||
      (err = soap->fposthdr(soap, "Host", host)) ||
      (err = soap->fposthdr(soap, "User-Agent", "gSOAP/2.8")) ||
      (err = soap_puthttphdr(soap, SOAP_OK, count)))
    return err;
#ifdef WITH_ZLIB
#ifdef WITH_GZIP
  if ((err = soap->fposthdr(soap, "Accept-Encoding", "gzip, deflate")))
#else
  if ((err = soap->fposthdr(soap, "Accept-Encoding", "deflate")))
#endif
    return err;
#endif
  return soap->fposthdr(soap, NULL, NULL);
}

// This function wraps the GSOAP fopen call to support "file:///var/run/socket" Unix socket URIs
static int gsoap_connect_pipe(struct soap *soap, const char *endpoint,
    const char *host, int port)
{
	int fd;
	struct sockaddr_un saddr;
	memset(&saddr, 0, sizeof(struct sockaddr_un));

	// See stdsoap2.cpp:tcp_connect() function
	if (soap_valid_socket(soap->socket))
	    return SOAP_OK;

	soap->socket = SOAP_INVALID_SOCKET;

	if (strncmp(endpoint, "file://", 7) != 0)
		return SOAP_EOF;
	const char *socket_name = strchr(endpoint + 7, '/');
	// >= because there also needs to be room for the 0x00
	if (socket_name == NULL ||
	    strlen(socket_name) >= sizeof(saddr.sun_path))
		return SOAP_EOF;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
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
	KCmd **lppCmd)
{
	KCmd*	lpCmd = NULL;

	if (strServerPath == NULL || *strServerPath == '\0' || lppCmd == NULL)
		return E_INVALIDARG;

	lpCmd = new KCmd();

	soap_set_imode(lpCmd->soap, iSoapiMode);
	soap_set_omode(lpCmd->soap, iSoapoMode);

	lpCmd->endpoint = strdup(strServerPath);
	lpCmd->soap->sndbuf = lpCmd->soap->rcvbuf = 0;
	lpCmd->soap->maxoccurs = SIZE_MAX; // override default limit of 100000, as this breaks ICS for large folders at least

	// default allow SSLv3, TLSv1, TLSv1.1 and TLSv1.2
	lpCmd->soap->ctx = SSL_CTX_new(SSLv23_method());

#ifdef WITH_OPENSSL
	if (strncmp("https:", lpCmd->endpoint, 6) == 0) {
		// no need to add certificates to call, since soap also calls SSL_CTX_set_default_verify_paths()
		if (soap_ssl_client_context(lpCmd->soap, SOAP_SSL_DEFAULT,
								strSSLKeyFile != NULL && *strSSLKeyFile != '\0' ? strSSLKeyFile : NULL,
								strSSLKeyPass != NULL && *strSSLKeyPass != '\0' ? strSSLKeyPass : NULL,
								NULL, NULL,
								NULL)) {
			free(const_cast<char *>(lpCmd->endpoint));
			delete lpCmd;
			return E_INVALIDARG;
		}
		// set our own certificate check function
		lpCmd->soap->fsslverify = ssl_verify_callback_kopano_silent;
		SSL_CTX_set_verify(lpCmd->soap->ctx, SSL_VERIFY_PEER, lpCmd->soap->fsslverify);
	}
#endif

	if(strncmp("file:", lpCmd->endpoint, 5) == 0) {
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

VOID DestroySoapTransport(KCmd *lpCmd)
{
	if (!lpCmd)
		return;

	/* strdup'd all of them earlier */
	free(const_cast<char *>(lpCmd->endpoint));
	free(const_cast<char *>(lpCmd->soap->proxy_host));
	free(const_cast<char *>(lpCmd->soap->proxy_userid));
	free(const_cast<char *>(lpCmd->soap->proxy_passwd));
	delete lpCmd;
}

int ssl_verify_callback_kopano_silent(int ok, X509_STORE_CTX *store)
{
	int sslerr;

	if (ok == 0)
	{
		// Get the last SSL error
		sslerr = X509_STORE_CTX_get_error(store);
		switch (sslerr)
		{
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
	}
	return ok;
}
