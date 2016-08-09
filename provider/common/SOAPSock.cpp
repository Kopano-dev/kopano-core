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

using namespace std;

/*
 * The structure of the data stored in soap->user on the _client_ side.
 * (cf. SOAPUtils.h for server side.)
 */
struct KCmdData {
	SOAP_SOCKET (*orig_fopen)(struct soap *, const char *, const char *, int);
};

static int ssl_zvcb_index = -1;	// the index to get our custom data

// we cannot patch http_post now (see external/gsoap/*.diff), so we redefine it
static int
http_post(struct soap *soap, const char *endpoint, const char *host, int port, const char *path, const char *action, size_t count)
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

   	return SOAP_OK;
}

static SOAP_SOCKET kc_client_connect(struct soap *soap, const char *ep,
    const char *host, int port)
{
	auto si = reinterpret_cast<struct KCmdData *>(soap->user);
	if (si == NULL)
		return -1;
	auto fopen = si->orig_fopen;
	delete si;
	if (fopen == NULL)
		return -1;
	soap->user = NULL;
	if (soap->ctx != NULL)
		SSL_CTX_set_ex_data(soap->ctx, ssl_zvcb_index,
			static_cast<void *>(const_cast<char *>(host)));
	return (*fopen)(soap, ep, host, port);
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
	struct KCmdData *si;

	if (strServerPath == NULL || *strServerPath == '\0' || lppCmd == NULL)
		return E_INVALIDARG;

	lpCmd = new KCmd();

	soap_set_imode(lpCmd->soap, iSoapiMode);
	soap_set_omode(lpCmd->soap, iSoapoMode);

	lpCmd->endpoint = strdup(strServerPath);
#if GSOAP_VERSION >= 20831
	lpCmd->soap->sndbuf = lpCmd->soap->rcvbuf = 0;
#endif

	// default allow SSLv3, TLSv1, TLSv1.1 and TLSv1.2
	lpCmd->soap->ctx = SSL_CTX_new(SSLv23_method());

#ifdef WITH_OPENSSL
	if (strncmp("https:", lpCmd->endpoint, 6) == 0) {
		// no need to add certificates to call, since soap also calls SSL_CTX_set_default_verify_paths()
		if(soap_ssl_client_context(lpCmd->soap,
								SOAP_SSL_DEFAULT | SOAP_SSL_SKIP_HOST_CHECK,
								strSSLKeyFile != NULL && *strSSLKeyFile != '\0' ? strSSLKeyFile : NULL,
								strSSLKeyPass != NULL && *strSSLKeyPass != '\0' ? strSSLKeyPass : NULL,
								NULL, NULL,
								NULL)) {
			free(const_cast<char *>(lpCmd->endpoint));
			delete lpCmd;
			return E_INVALIDARG;
		}

		// set connection string as callback information
		if (ssl_zvcb_index == -1)
			ssl_zvcb_index = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
		// callback data will be set right before tcp_connect()

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

	if (lpCmd->soap->user != NULL)
		ec_log_warn("Hmm. SOAP object custom data is already set.");
	si = new(std::nothrow) struct KCmdData;
	if (si == NULL) {
		ec_log_err("alloc: %s\n", strerror(errno));
		return E_OUTOFMEMORY;
	}
	si->orig_fopen = lpCmd->soap->fopen;
	lpCmd->soap->user = si;
	lpCmd->soap->fopen = kc_client_connect;
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

/**
 * @cn:		the common name (can be a pattern)
 * @host:	the host we are connecting to
 */
static bool kc_wildcard_cmp(const char *cn, const char *host)
{
	while (*cn != '\0' && *host != '\0') {
		if (*cn == '*') {
			++cn;
			for (; *host != '\0' && *host != '.'; ++host)
				if (kc_wildcard_cmp(cn, host))
					return true;
			continue;
		}
		if (tolower(*cn) != tolower(*host))
			return false;
		++cn;
		++host;
	}
	return *cn == '\0' && *host == '\0';
}

static int kc_ssl_check_name(X509_STORE_CTX *store)
{
	auto ssl = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx()));
	if (ssl == NULL) {
		ec_log_warn("Unable to get SSL object from store");
		return false;
	}
	auto ctx = SSL_get_SSL_CTX(ssl);
	if (ctx == NULL) {
		ec_log_warn("Unable to get SSL context from SSL object");
		return false;
	}
	auto cert = X509_STORE_CTX_get_current_cert(store);
	if (cert == NULL) {
		ec_log_warn("No certificate found in connection. What gives?");
		return false;
	}
	auto name = X509_get_subject_name(cert);
	const char *subject = NULL;
	if (name != NULL) {
		auto i = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
		if (i != -1)
			subject = reinterpret_cast<const char *>(ASN1_STRING_data(X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, i))));
	}
	if (subject == NULL) {
		ec_log_err("Server presented no X.509 Subject name. Aborting login.");
		return false;
	}
	auto hostname = static_cast<const char *>(SSL_CTX_get_ex_data(ctx, ssl_zvcb_index));
	if (hostname == NULL) {
		ec_log_err("Internal fluctuation - no hostname in our SSL context. Aborting login.");
		return false;
	}
	if (kc_wildcard_cmp(subject, hostname)) {
		ec_log_debug("Server %s presented matching certificate for %s.",
			hostname, subject);
		return true;
	}
	ec_log_err("Server %s presented non-matching certificate for %s. "
		"Aborting login.", hostname, subject);
	return false;
}

int ssl_verify_callback_kopano_silent(int ok, X509_STORE_CTX *store)
{
	if (!kc_ssl_check_name(store)) {
		X509_STORE_CTX_set_error(store, X509_V_ERR_CERT_REJECTED);
		ok = 0;
	}
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
