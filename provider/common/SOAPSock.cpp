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

#if defined(WIN32) && !defined(DISABLE_SSL_UI)
#include "EntryPoint.h"
#include <kopano/ECGetText.h>
#include "CertificateDlg.h"
#endif

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

#ifdef WIN32
// This is a reference to gSoap's standard tcp_connect
SOAP_SOCKET (*tcp_connect)(struct soap *soap, const char *endpoint, const char *host, int port);

SOAP_SOCKET MyConnect(struct soap *soap, const char* endpoint, const char* host, int port)
{
	int sock = 0;
	int on = 1;

	// set callback information if we're connecting with SSL
	if (soap->ctx)
		SSL_CTX_set_ex_data(soap->ctx, ssl_zvcb_index, (void*)host);

	sock = tcp_connect(soap, endpoint, host, port);
	if(sock != SOAP_INVALID_SOCKET) {
		setsockopt(soap->socket, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on));
		// Set (in)sane keepalive values
		struct tcp_keepalive ka;
		DWORD ret;

		ka.onoff = 1;
		ka.keepaliveinterval = 1 * 1000; // Send keepalives 1s apart (max 10 or so before ending connection)
		ka.keepalivetime = 30 * 1000; // Wait 30 seconds before sending keepalives

		ret = WSAIoctl(soap->socket, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), NULL, 0, &ret, NULL, NULL);
		return sock;
	}

	return sock;
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
#ifdef WIN32
			LoadCertificatesFromRegistry();
#endif
		}
		// callback data will be set right before tcp_connect()

		// set our own certificate check function
#if defined(WIN32) && !defined(DISABLE_SSL_UI)
		if (ulUIFlags & 0x01 /*MDB_NO_DIALOG*/)
			lpCmd->soap->fsslverify = ssl_verify_callback_kopano_silent;
		else
			lpCmd->soap->fsslverify = ssl_verify_callback_kopano;
#else
		lpCmd->soap->fsslverify = ssl_verify_callback_kopano_silent;
#endif

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

#ifdef WIN32
	// UGLY HACK: we want to call tcp_connect in our our connect function. However, we can't link directly
	// since it has 'static' scope. However, we can read it here, store it in a global variable, and then
	// use it from there later.
	tcp_connect = lpCmd->soap->fopen;
	lpCmd->soap->fopen = MyConnect;
#endif

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

#ifdef WIN32
/*
 * This verification callback function will be called multiple times per connection,
 * because certificates will be checked one by one, starting at the root CA (depth > 0),
 * until the top (depth == 0) is reached.
 * We continue in the chain by returning 1, until we reach the top. If the certificate
 * verifies with windows, all is fine. Otherwise we leave the choice up to the user.
 *
 * Since Kopano client is multi threaded, we must collect info in a struct and map per pthread (connection).
 *
 * We get the certificate raw data, and give that to the win32 crypt api. This should ensure
 * that installed certificates in windows will accept the trust of the presented certificate.
 * The verify function I chose to use from the CryptoAPI needs the previous certificate to
 * verify with, so we keep that in the verifyinfo struct. This function validates the certificate
 * with the issuer, even if the issuer is not trusted.
 *
 * To test the trust of a certificate, we find it by issuer in the ROOT certificate store of windows.
 */
struct verifyinfo {
	verifyinfo() {
		pPrevCertificate = pLastCertificate = NULL;
		user_choice = errors = 0;
		max_depth = -1;
		bTrusted = bVerified = false;
	}
	PCCERT_CONTEXT pPrevCertificate;
	PCCERT_CONTEXT pLastCertificate;
	int user_choice;
	int max_depth;
	int errors;
	bool bTrusted;
	bool bVerified;
};

static CPthreadMutex	g_SCDMutex; // Mutex for map mapSCD
static map<std::string, struct verifyinfo> mapSCD;	// map server certificate data

BOOL ValidateCertificateChain(PCCERT_CONTEXT pCertificate)
{
	BOOL result = TRUE;
	PCCERT_CHAIN_CONTEXT	pTrusted = NULL;
	CERT_ENHKEY_USAGE		EnhkeyUsage;
	CERT_USAGE_MATCH		CertUsage;
	CERT_CHAIN_PARA			ChainPara;
	
	EnhkeyUsage.cUsageIdentifier = 0;
	EnhkeyUsage.rgpszUsageIdentifier = NULL;

	CertUsage.dwType = USAGE_MATCH_TYPE_AND;
	CertUsage.Usage = EnhkeyUsage;

	ChainPara.cbSize = sizeof(CERT_CHAIN_PARA);
	ChainPara.RequestedUsage = CertUsage;

	if(!CertGetCertificateChain(
				NULL,					// use the default chain engine
				pCertificate,			// pointer to the end certificate
				NULL,					// use the default time
				NULL,					// search no additional stores
				&ChainPara,				// use AND logic and enhanced key usage 
										//  as indicated in the ChainPara 
										//  data structure
				0/*CERT_CHAIN_REVOCATION_CHECK_CHAIN| */,
				NULL,					// currently reserved
				&pTrusted))				// return a pointer to the chain created
		return FALSE;

	if(pTrusted->TrustStatus.dwErrorStatus != 0)
		result = FALSE;

	CertFreeCertificateChain(pTrusted);
	return result;
}

int ssl_verify_callback_kopano_control(int ok, X509_STORE_CTX *store, BOOL bShowDlg)
{
	std::map<std::string, verifyinfo>::const_iterator iSCD; // current certificate data

	PCCERT_CONTEXT pCertificate = NULL;
	DWORD dwFlags = CERT_STORE_REVOCATION_FLAG | CERT_STORE_SIGNATURE_FLAG | CERT_STORE_TIME_VALIDITY_FLAG;
	int size = 0;
	unsigned char *data = NULL;
	unsigned char *end = NULL;

	int user_choice = 0;
	std::basic_string<TCHAR> strError;
	SSL *ssl = NULL;
	SSL_CTX *ctx = NULL;
	X509 *cert = NULL;
	BIO *bioBuffer = NULL;
	struct X509_name_st *name = NULL; // X509_NAME is also defined in wincrypt.h
	char *subject = NULL;
	char *hostname = NULL;
	std::string expiration;
	int i = 0;
	int depth = X509_STORE_CTX_get_error_depth(store); 
	bool bRootCA = false;

	g_SCDMutex.Lock();

	// get hostname from callback storage, to find the previous userchoice, if any
	ssl = (SSL*)X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx());
	if (!ssl) {
#ifndef DISABLE_SSL_UI
		if(bShowDlg)
			user_choice = MessageBox(NULL, _("Unable to get SSL object from store"), g_strProductName.c_str(), MB_OK);
#endif
		goto exit;
	}
	ctx = SSL_get_SSL_CTX(ssl);
	if (!ctx) {
#ifndef DISABLE_SSL_UI
		if(bShowDlg)
			user_choice = MessageBox(NULL, _("Unable to get SSL context from SSL object"), g_strProductName.c_str(), MB_OK);
#endif
		goto exit;
	}
	hostname = (char*)SSL_CTX_get_ex_data(ctx, ssl_zvcb_index);

	// find current working data, or create new
	iSCD = mapSCD.find(hostname);
	if (iSCD == mapSCD.end()) {
		// first time we're called for this certificate, so it must be the root.
		bRootCA = true;
		iSCD = mapSCD.insert(pair<string, verifyinfo>(hostname,verifyinfo())).first;
	} else if (iSCD->second.max_depth == -1 || iSCD->second.max_depth == depth) {
		// we know the certificate starts at this depth, so it must be the root.
		bRootCA = true;
		iSCD->second.max_depth = depth;
	}

	// find current working certificate
	cert = X509_STORE_CTX_get_current_cert(store);
	if (!cert) {
#ifndef DISABLE_SSL_UI
		if(bShowDlg)
			user_choice = MessageBox(NULL, _("No certificate found in connection"), g_strProductName.c_str(), MB_OK);
#endif
		goto exit;
	}

	// make blob from cert
	size = i2d_X509(cert, NULL);
	end = data = new unsigned char[size];
	size = i2d_X509(cert, &end);

	// make windows certificate
	pCertificate = CertCreateCertificateContext(X509_ASN_ENCODING, data, size);
	if (!pCertificate) {
#ifndef DISABLE_SSL_UI
		if(bShowDlg)
			user_choice = MessageBox(NULL, _("Unable to convert OpenSSL to WinCrypt"), g_strProductName.c_str(), MB_OK);
#endif
		goto exit;
	}

	if (depth == 0 && iSCD->second.user_choice != 0 && iSCD->second.pLastCertificate) {
		if (CertCompareCertificate(X509_ASN_ENCODING, pCertificate->pCertInfo, iSCD->second.pLastCertificate->pCertInfo) == TRUE) {	
			// we connected to the same server with the same certificate again, use cached userchoice
			// TODO: Is it enough to only compare the last certificate?
			user_choice = iSCD->second.user_choice;
			goto exit;
		}
		// server switched certificates
		CertFreeCertificateContext(iSCD->second.pLastCertificate);
		iSCD->second.pLastCertificate = NULL;
	}

	// see if windows trusts this certificate
	if (bRootCA) {
		// I'm not sure why the last step can't be checked for a trust anymore
		// since I try to find the issuer, aren't those all known and trusted?
		// or does it mean when the bRootCA one is trusted, everything below should automatically be trusted?
		if (!ValidateCertificateChain(pCertificate)) {
			++iSCD->second.errors;
			iSCD->second.bTrusted = false;
		} else {
			iSCD->second.bTrusted = true;
		}
	}

	// verify certificate. if we don't have a previous certificate yet, we can only verify the date
	if (!iSCD->second.pPrevCertificate)
		dwFlags &= ~(CERT_STORE_REVOCATION_FLAG | CERT_STORE_SIGNATURE_FLAG);
	if (CertVerifySubjectCertificateContext(pCertificate, iSCD->second.pPrevCertificate, &dwFlags) != TRUE) {
		// flags say what's wrong
		++iSCD->second.errors;
		iSCD->second.bVerified = false;
	} else {
		iSCD->second.bVerified = true;
	}
	if (iSCD->second.pPrevCertificate)
		CertFreeCertificateContext(iSCD->second.pPrevCertificate);
	iSCD->second.pPrevCertificate = pCertificate;
	pCertificate = NULL;

	// we're not done with all the certificates. force openssl to continue anyway
	if (depth > 0) {
		user_choice = IDYES;
		goto exit;
	}
	// find subject
	name = X509_get_subject_name(cert);
	if (name) {
		i = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
		if (i != -1)
			subject = (char*)ASN1_STRING_data(X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, i)));
	}
	if (subject == NULL)
		subject = "<unknown>";
	else {
		if (subject[0] == '*') {
			//wildcard certificate, domain must match
			int hlen = strlen(hostname);
			int slen = strlen(subject+1);
			if (hlen < slen-1 || strcmp(subject+1, hostname+hlen-slen) != 0) {
				++iSCD->second.errors;
				iSCD->second.bVerified = false;
			}
		} else {
			// name must match
			if (strcmp(subject, hostname) != 0) {
				++iSCD->second.errors;
				iSCD->second.bVerified = false;
			}
		}
	}

	// store copy of last certificate so we can cache this valid connection
	iSCD->second.pLastCertificate = CertCreateCertificateContext(X509_ASN_ENCODING, data, size);

	// we're at the last certificate, see if this connection is valid
	if (iSCD->second.errors == 0) {
		user_choice = IDYES;
		goto exit;
	}

#ifndef DISABLE_SSL_UI
	if (!bShowDlg)
		goto exit;

	char *issuer = NULL;
	BUF_MEM *bioMem = NULL;
	ASN1_TIME *asnNotAfter = NULL;

	/*
	 * The certificate could not be validated (dwFlags contain _last_ error)
	 * show this to the user, and present them the choice to continue anyway
	 */
	// find issuer
	name = X509_get_issuer_name(cert);
	if (name) {
		i = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
		if (i != -1)
			issuer = (char*)ASN1_STRING_data(X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, i)));
	}
	if (issuer == NULL)
		issuer = "<unknown>";

	// find expiration date
	asnNotAfter = X509_get_notAfter(cert);
	if (asnNotAfter) {
		bioBuffer = BIO_new(BIO_s_mem());
		ASN1_TIME_print(bioBuffer, asnNotAfter);
		BIO_get_mem_ptr(bioBuffer, &bioMem);
		expiration.assign(bioMem->data, bioMem->length);
	} else {
		expiration = "<unknown>";
	}

	// subject already retrieved to compare to hostname

	if (!iSCD->second.bTrusted && !iSCD->second.bVerified) {
		strError += _("The certificate is not trusted and could not be verified.");
	} else if (!iSCD->second.bTrusted) {
		strError += _("The certificate is not trusted.");
	} else if (!iSCD->second.bVerified) {
		// probably date error
		strError += _("The certificate is trusted but does not verify correctly.");
	}
	strError += _T("\n\n");
	strError += _("Do you wish to continue?");

	// show certificate to user
	{
		convert_context converter;

		// use resource
		AFX_MANAGE_STATE(AfxGetStaticModuleState());
		
		CWnd* pWnd = AfxGetMainWnd();

		CCertificateDlg cCertDlg(data, size, pWnd);

		cCertDlg.SetDialogText(converter.convert_to<LPTSTR>(hostname), 
							   converter.convert_to<LPTSTR>(subject), 
							   converter.convert_to<LPTSTR>(issuer), 
							   converter.convert_to<LPTSTR>(expiration), 
							   converter.convert_to<LPTSTR>(strError));
		if (cCertDlg.DoModal() == IDOK)
			user_choice = IDYES;
		else
			user_choice = IDNO;
	}

	// save user choice for next time
	iSCD->second.user_choice = user_choice;
#endif

exit:
	if (bioBuffer)
		BIO_free(bioBuffer);
	delete[] data;
	if (pCertificate)
		CertFreeCertificateContext(pCertificate);
	if (depth == 0 && iSCD->second.pPrevCertificate) {
		CertFreeCertificateContext(iSCD->second.pPrevCertificate);
		iSCD->second.pPrevCertificate = NULL;
	}

	if (user_choice == IDYES) {
		// user chose to ignore error, or we're validating and want to continue
		X509_STORE_CTX_set_error(store, X509_V_OK);
		ok = 1;
	} else {
		// user chose to close the connection to this unsafe server
		X509_STORE_CTX_set_error(store, X509_V_ERR_CERT_REJECTED);
		ok = 0;
	}

	g_SCDMutex.Unlock();

	/* Note: return 1 to continue, but unsafe progress will be terminated by SSL */
	return ok;
}

int ssl_verify_callback_kopano(int ok, X509_STORE_CTX *store)
{
	return ssl_verify_callback_kopano_control(ok, store, TRUE);
}

#endif

int ssl_verify_callback_kopano_silent(int ok, X509_STORE_CTX *store)
{
	int sslerr;

	if (ok == 0)
	{
		// Get the last ssl error
		sslerr = X509_STORE_CTX_get_error(store);
#ifdef WIN32
		if(ssl_verify_callback_kopano_control(ok, store, FALSE) == 1) {
				// ssl is verified return ok
				ok = 1;
				goto exit;
		}
#endif
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

#ifdef WIN32
exit:
#endif
#if defined(WIN32) && !defined(DISABLE_SSL_UI)
	if(!ok)
		TRACE_RELEASE("Server certificate rejected. Connect once with Outlook to verify the authenticity and select the option to remember the choice. Please make sure you do this for each server in your cluster.");
#endif
	return ok;
}

#ifdef WIN32
HRESULT LoadCertificatesFromRegistry()
{
	HRESULT hr = NOERROR;
	HKEY keySSL;
	unsigned char *lpRawCert;
	DWORD cbRawCert;
	DWORD index = 0;
	char lpValueName[1024];
	DWORD cbValueName;
	DWORD dwType;
	std::map<std::string, verifyinfo>::const_iterator iSCD;

	g_SCDMutex.Lock();

	if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Kopano\\Client\\SSL", 0, KEY_READ, &keySSL) != ERROR_SUCCESS)
		goto exit;	// skip certificates, folder probably not there

	do {
		cbValueName = arraySize(lpValueName);
		if (RegEnumValueA(keySSL, index, lpValueName, &cbValueName, NULL, &dwType, NULL, &cbRawCert) != ERROR_SUCCESS)
			break;
		if (dwType != REG_BINARY) {
			++index;
			continue;
		}

		lpRawCert = new unsigned char[cbRawCert];
		cbValueName = arraySize(lpValueName);
		RegEnumValueA(keySSL, index, lpValueName, &cbValueName, NULL, &dwType, lpRawCert, &cbRawCert);

		iSCD = mapSCD.insert(make_pair(string(lpValueName, cbValueName), verifyinfo())).first;
		iSCD->second.pLastCertificate = CertCreateCertificateContext(X509_ASN_ENCODING, lpRawCert, cbRawCert);
		iSCD->second.user_choice = IDYES;
		
		delete [] lpRawCert;
		++index;
	} while (TRUE);
	
	RegCloseKey(keySSL);

exit:
	g_SCDMutex.Unlock();

	return hr;
}
#endif
