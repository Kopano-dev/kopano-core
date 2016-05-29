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

#ifdef LINUX
#include <sys/un.h>
#endif

#include "SOAPUtils.h"
#include <kopano/threadutil.h>
#include <kopano/CommonUtil.h>
#include <string>
#include <map>

#include <kopano/charset/convert.h>
#include <kopano/charset/utf8string.h>

using namespace std;

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

#ifdef LINUX

// This function wraps the GSOAP fopen call to support "file:///var/run/socket" unix-socket URI's
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
	if (socket_name == NULL ||
	    strlen(socket_name) >= sizeof(saddr.sun_path))
		return SOAP_EOF;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);

	saddr.sun_family = AF_UNIX;

	// >= because there also needs to be room for the 0x00
	if (strlen(socket_name) >= sizeof(saddr.sun_path))
		return SOAP_EOF;

	strncpy(saddr.sun_path, socket_name, sizeof(saddr.sun_path));

	connect(fd, (struct sockaddr *)&saddr, sizeof(struct sockaddr_un));

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
#else
/* In windows, we support file: URI's with named pipes (ie file://\\server\pipe\kopano)
 * Unfortunately, we can't use the standard fsend/frecv call with this, because gsoap
 * uses winsock-style SOCKET handles for send and recv, so we also have to supply
 * fsend,frecv, etc to use HANDLE-type connections
 */

int gsoap_win_fsend(struct soap *soap, const char *s, size_t n)
{
	int rc = SOAP_ERR;
	DWORD ulWritten;
	OVERLAPPED op = {0};

	op.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);	// Will this ever fail?

	if (WriteFile((HANDLE)soap->socket, s, n, &ulWritten, &op) == TRUE)
		rc = SOAP_OK;

	else if (GetLastError() == ERROR_IO_PENDING) {
		if (WaitForSingleObject(op.hEvent, 70*1000) == WAIT_OBJECT_0) {
			// We would normaly reset the event here, but we're not reusing it anyway.
			if (GetOverlappedResult((HANDLE)soap->socket, &op, &ulWritten, TRUE) == TRUE)
				rc = SOAP_OK;
		} else
			CancelIo((HANDLE)soap->socket);
	}

	CloseHandle(op.hEvent);
	return rc;
}

size_t gsoap_win_frecv(struct soap *soap, char *s, size_t n)
{
	size_t rc = 0;
	DWORD ulRead;
	OVERLAPPED op = {0};

	op.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);	// Will this ever fail?

	if (ReadFile((HANDLE)soap->socket, s, n, &ulRead, &op) == TRUE)
		rc = ulRead;

	else if (GetLastError() == ERROR_IO_PENDING) {
		if (WaitForSingleObject(op.hEvent, 70*1000) == WAIT_OBJECT_0) {
			// We would normaly reset the event here, but we're not reusing it anyway.
			if (GetOverlappedResult((HANDLE)soap->socket, &op, &ulRead, TRUE) == TRUE)
				rc = ulRead;
		} else
			CancelIo((HANDLE)soap->socket);
	} 

	CloseHandle(op.hEvent);
	return rc;
}

int gsoap_win_fclose(struct soap *soap)
{
	if((HANDLE)soap->socket == INVALID_HANDLE_VALUE)
		return SOAP_OK;

	CloseHandle((HANDLE)soap->socket); // Ignore error
	soap->socket = (int)INVALID_HANDLE_VALUE;

	return SOAP_OK;
}

int gsoap_win_fpoll(struct soap *soap)
{
	// Always a connection
	return SOAP_OK;
}

// Override the soap function shutdown
//  disable read/write actions
static int gsoap_win_shutdownsocket(struct soap *soap, SOAP_SOCKET fd, int how)
{
	// Normaly gsoap called shutdown, but the function isn't
	// exist for named pipes, so return;
	return SOAP_OK;
}

int gsoap_connect_pipe(struct soap *soap, const char *endpoint, const char *host, int port)
{
	HANDLE hSocket = INVALID_HANDLE_VALUE;

#ifdef UNDER_CE
	WCHAR x[1024];
#endif
	// Check whether it is already a validated socket.
	if ((HANDLE)soap->socket != INVALID_HANDLE_VALUE) // if (soap_valid_socket(soap->socket))
		return SOAP_OK;

	if (strncmp(endpoint, "file:", 5) || strlen(endpoint) < 7)
		return SOAP_EOF;

#ifdef UNDER_CE
		MultiByteToWideChar(CP_ACP,MB_PRECOMPOSED,endpoint+5+2,strlen(endpoint+5+2),x,1024);
#endif

	for (int nRetry = 0; nRetry < 10; ++nRetry) {
#ifdef UNDER_CE
		hSocket = CreateFileW(x, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
#else
		hSocket = CreateFileA(endpoint+5+2, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
#endif
		if (hSocket == INVALID_HANDLE_VALUE) {
			if (GetLastError() != ERROR_PIPE_BUSY) {
				return SOAP_EOF; // Could not open pipe
			}

			// All pipe instances are busy, so wait for 1 second.
			if (!WaitNamedPipeA(endpoint+5+2, 1000)) {
				if (GetLastError() != ERROR_PIPE_BUSY) {
					return SOAP_EOF; // Could not open pipe
				}
			}
		} else {
			break;
		}
	}// for (...)

	if (hSocket == INVALID_HANDLE_VALUE)
		return SOAP_EOF;

#ifdef UNDER_CE
	soap->sendfd = soap->recvfd = NULL;
#else
	soap->sendfd = soap->recvfd = SOAP_INVALID_SOCKET;
#endif

	soap->max_keep_alive = 0;
	soap->socket = (int)hSocket;
	soap->status = SOAP_POST;

	//Override soap functions
	soap->fpoll = gsoap_win_fpoll;
	soap->fsend = gsoap_win_fsend;
	soap->frecv = gsoap_win_frecv;
	soap->fclose = gsoap_win_fclose;
	soap->fshutdownsocket = gsoap_win_shutdownsocket;

	// Unused
	soap->faccept = NULL;
	soap->fopen = NULL;

	return SOAP_OK;
}
#endif

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
	HRESULT		hr = hrSuccess;
	KCmd*	lpCmd = NULL;

	if (strServerPath == NULL || *strServerPath == '\0' || lppCmd == NULL) {
		hr = E_INVALIDARG;
		goto exit;
	}

	lpCmd = new KCmd();

	soap_set_imode(lpCmd->soap, iSoapiMode);
	soap_set_omode(lpCmd->soap, iSoapoMode);

	lpCmd->endpoint = strdup(strServerPath);

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
			hr = E_INVALIDARG;
			goto exit;
		}

		// set connection string as callback information
		if (ssl_zvcb_index == -1) {
			ssl_zvcb_index = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
		}
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

	*lppCmd = lpCmd;
exit:
	if (hr != hrSuccess && lpCmd) {
		/* strdup'd them earlier */
		free(const_cast<char *>(lpCmd->endpoint));
		delete lpCmd;
	}

	return hr;
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
		// Get the last ssl error
		sslerr = X509_STORE_CTX_get_error(store);
		switch (sslerr)
		{
			case X509_V_ERR_CERT_HAS_EXPIRED:
			case X509_V_ERR_CERT_NOT_YET_VALID:
			case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
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
