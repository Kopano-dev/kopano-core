/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#include <kopano/platform.h>
#include <algorithm>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <kopano/ECChannel.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <kopano/UnixUtil.h>
#include <kopano/tie.hpp>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <openssl/conf.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/opensslconf.h>
#include <openssl/ssl.h>
#ifdef LINUX
#include <linux/rtnetlink.h>
#endif
#include <openssl/err.h>
#include <mapicode.h>
#include "SSLUtil.h"
#ifndef hrSuccess
#define hrSuccess 0
#endif

using namespace std::string_literals;

namespace KC {

class ai_deleter {
	public:
	void operator()(struct addrinfo *ai) { if (ai != nullptr) freeaddrinfo(ai); }
};

/*
To generate a RSA key:
openssl genrsa -out privkey.pem 2048

Creating a certificate request:
openssl req -new -key privkey.pem -out cert.csr

Creating a self-signed test certificate:
openssl req -new -x509 -key privkey.pem -out cacert.pem -days 1095
*/

shared_mutex ECChannel::ctx_lock;
SSL_CTX *ECChannel::lpCTX;

HRESULT ECChannel::HrSetCtx(ECConfig *lpConfig)
{
	HRESULT hr = MAPI_E_CALL_FAILED;
	if (lpConfig == NULL) {
		ec_log_err("ECChannel::HrSetCtx(): invalid parameters");
		return hr;
	}

	const char *szFile = nullptr, *szPath = nullptr;;
	auto cert_file = lpConfig->GetSetting("ssl_certificate_file");
	auto key_file = lpConfig->GetSetting("ssl_private_key_file");
	const char *ssl_ciphers = lpConfig->GetSetting("ssl_ciphers");
	const char *ssl_curves = lpConfig->GetSetting("ssl_curves");
#if !defined(OPENSSL_NO_ECDH) && defined(NID_X9_62_prime256v1)
	EC_KEY *ecdh;
#endif

	if (cert_file == nullptr || key_file == nullptr) {
		ec_log_err("ECChannel::HrSetCtx(): no cert or key file");
		return hr;
	}
	auto key_fh = fopen(key_file, "r");
	if (key_fh == nullptr) {
		ec_log_err("ECChannel::HrSetCtx(): cannot open key file %s: %s", key_file, strerror(errno));
		return hr;
	}
	fclose(key_fh);

	auto cert_fh = fopen(cert_file, "r");
	if (cert_fh == nullptr) {
		ec_log_err("ECChannel::HrSetCtx(): cannot open cert file %s: %s", cert_file, strerror(errno));
		return hr;
	}
	fclose(cert_fh);

	// Initialize SSL library under context lock; normally, this should only
	// happen on the first run of the SSL initializer and generally just once.
#if OPENSSL_VERSION_NUMBER >= 0x1010000fL
	{
		// New style init.
		std::unique_lock<shared_mutex> lck(ctx_lock);
		if (lpCTX == nullptr)
			OPENSSL_init_ssl(OPENSSL_INIT_ENGINE_ALL_BUILTIN, NULL);
	}

	// Default TLS context for OpenSSL >= 1.1, enables all methods.
	auto newctx = SSL_CTX_new(TLS_server_method());
#else // OPENSSL_VERSION_NUMBER < 0x1010000fL
	{
		// Old style init, modelled after Apache mod_ssl.
		std::unique_lock<shared_mutex> lck(ctx_lock);
		if (lpCTX == nullptr) {
			ERR_load_crypto_strings();
			SSL_load_error_strings();
			SSL_library_init();
#	ifndef OPENSSL_NO_ENGINE
			ENGINE_load_builtin_engines();
#	endif // !OPENSSL_NO_ENGINE
			OpenSSL_add_all_algorithms();
			OPENSSL_load_builtin_modules();
		}
	}

	// enable *all* server methods, not just ssl2 and ssl3, but also tls1 and tls1.1
	auto newctx = SSL_CTX_new(SSLv23_server_method());
#endif // OPENSSL_VERSION_NUMBER

	// Check context.
	if (newctx == nullptr) {
		ec_log_err("ECChannel::HrSetCtx(): failed to create new SSL context");
		return hr;
	}

#ifndef SSL_OP_NO_RENEGOTIATION
#	define SSL_OP_NO_RENEGOTIATION 0 /* unavailable in OpenSSL 1.0 */
#endif
#ifndef SSL_OP_NO_COMPRESSION
#	define SSL_OP_NO_COMPRESSION 0
#endif
	SSL_CTX_set_options(newctx, SSL_OP_ALL | SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);
	auto tlsprot = lpConfig->GetSetting("tls_min_proto");
	if (!ec_tls_minproto(newctx, tlsprot)) {
		ec_log_err("Unknown SSL/TLS protocol version \"%s\"", tlsprot);
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
#if !defined(OPENSSL_NO_ECDH) && defined(NID_X9_62_prime256v1)
	ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	if (ecdh != NULL) {
		/* SINGLE_ECDH_USE = renegotiate exponent for each handshake */
		SSL_CTX_set_options(newctx, SSL_OP_SINGLE_ECDH_USE);
		SSL_CTX_set_tmp_ecdh(newctx, ecdh);
		EC_KEY_free(ecdh);
	}
#endif
	if (ssl_ciphers && SSL_CTX_set_cipher_list(newctx, ssl_ciphers) != 1) {
		ec_log_err("Can not set SSL cipher list to \"%s\": %s", ssl_ciphers, ERR_error_string(ERR_get_error(), 0));
		goto exit;
	}
	if (parseBool(lpConfig->GetSetting("ssl_prefer_server_ciphers")))
		SSL_CTX_set_options(newctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
#if !defined(OPENSSL_NO_ECDH) && defined(SSL_CTX_set1_curves_list)
	if (ssl_curves && SSL_CTX_set1_curves_list(newctx, ssl_curves) != 1) {
		ec_log_err("Can not set SSL curve list to \"%s\": %s", ssl_curves, ERR_error_string(ERR_get_error(), 0));
		goto exit;
	}

	SSL_CTX_set_ecdh_auto(newctx, 1);
#endif

	SSL_CTX_set_default_verify_paths(newctx);
	if (SSL_CTX_use_certificate_chain_file(newctx, cert_file) != 1) {
		ec_log_err("SSL CTX certificate file error: %s", ERR_error_string(ERR_get_error(), 0));
		goto exit;
	}
	if (SSL_CTX_use_PrivateKey_file(newctx, key_file, SSL_FILETYPE_PEM) != 1) {
		ec_log_err("SSL CTX private key file error: %s", ERR_error_string(ERR_get_error(), 0));
		goto exit;
	}
	if (SSL_CTX_check_private_key(newctx) != 1) {
		ec_log_err("SSL CTX check private key error: %s", ERR_error_string(ERR_get_error(), 0));
		goto exit;
	}

	if (strcmp(lpConfig->GetSetting("ssl_verify_client"), "yes") == 0)
		SSL_CTX_set_verify(newctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, 0);
	else
		SSL_CTX_set_verify(newctx, SSL_VERIFY_NONE, 0);

	if (lpConfig->GetSetting("ssl_verify_file")[0])
		szFile = lpConfig->GetSetting("ssl_verify_file");
	if (lpConfig->GetSetting("ssl_verify_path")[0])
		szPath = lpConfig->GetSetting("ssl_verify_path");
	if ((szFile != nullptr || szPath != nullptr) &&
	    SSL_CTX_load_verify_locations(newctx, szFile, szPath) != 1) {
		ec_log_err("SSL CTX error loading verify locations: %s", ERR_error_string(ERR_get_error(), 0));
		goto exit;
	}

	// Swap in generated SSL context.
	{
		std::unique_lock<shared_mutex> lck(ctx_lock);
		std::swap(lpCTX, newctx);
		hr = hrSuccess;
	}
exit:
	if (newctx != nullptr)
		SSL_CTX_free(newctx);
	return hr;
}

HRESULT ECChannel::HrFreeCtx() {
	// Swap out current SSL context from global context pointer.
	SSL_CTX *ctx = nullptr;
	{
		std::unique_lock<shared_mutex> lck(ctx_lock);
		std::swap(lpCTX, ctx);
	}

	// Clean up retrieved context.
	if (ctx != nullptr)
		SSL_CTX_free(ctx);
	return hrSuccess;
}

ECChannel::ECChannel(int inputfd) :
	fd(inputfd), peer_atxt(), peer_sockaddr()
{
}

ECChannel::~ECChannel() {
	if (lpSSL) {
		SSL_shutdown(lpSSL);
		SSL_free(lpSSL);
	}
	close(fd);
}

HRESULT ECChannel::HrEnableTLS()
{
	int rc = -1;
	if (lpSSL != nullptr) {
		ec_log_err("ECChannel::HrEnableTLS(): trying to reenable TLS channel");
		return MAPI_E_CALL_FAILED;
	}

	/*
	 * Access context under shared lock to avoid races with HrSetCtx
	 * setting up a new context.
	 */
	SSL *ssl = nullptr;
	HRESULT hr = MAPI_E_CALL_FAILED;
	{
		std::shared_lock<KC::shared_mutex> lck(ctx_lock);
		if (lpCTX == NULL) {
			ec_log_err("ECChannel::HrEnableTLS(): trying to enable TLS channel when not set up");
			goto exit;
		}
		ssl = SSL_new(lpCTX);
	}

	if (ssl == nullptr) {
		ec_log_err("ECChannel::HrEnableTLS(): SSL_new failed");
		goto exit;
	}

#if OPENSSL_VERSION_NUMBER >= 0x1010100fL
	ec_log_info("ECChannel::HrEnableTLS(): min TLS version 0x%lx", SSL_get_min_proto_version(ssl));
	ec_log_info("ECChannel::HrEnableTLS(): max TLS version 0x%lx", SSL_get_max_proto_version(ssl));
#endif
	ec_log_info("ECChannel::HrEnableTLS(): TLS flags 0x%lx", SSL_get_options(ssl));

	if (SSL_set_fd(ssl, fd) != 1) {
		ec_log_err("ECChannel::HrEnableTLS(): SSL_set_fd failed");
		goto exit;
	}

	ERR_clear_error();
	rc = SSL_accept(ssl);
	if (rc != 1) {
		int err = SSL_get_error(ssl, rc);
		ec_log_err("ECChannel::HrEnableTLS(): SSL_accept failed: %d", err);
		if (err != SSL_ERROR_SYSCALL && err != SSL_ERROR_SSL)
			SSL_shutdown(ssl);
		goto exit;
	}

	std::swap(lpSSL, ssl);
	hr = hrSuccess;
exit:
	if (ssl != nullptr)
		SSL_free(ssl);
	return hr;
}

HRESULT ECChannel::HrGets(char *szBuffer, size_t ulBufSize, size_t *lpulRead)
{
	char *lpRet = NULL;
	int len = ulBufSize;

	if (!szBuffer || !lpulRead)
		return MAPI_E_INVALID_PARAMETER;
	if (lpSSL)
		lpRet = SSL_gets(szBuffer, &len);
	else
		lpRet = fd_gets(szBuffer, &len);
	if (lpRet) {
		*lpulRead = len;
		return hrSuccess;
	}
	return MAPI_E_CALL_FAILED;
}

/**
 * Read a line from a socket. Reads as much data until it encounters a
 * \n characters.
 *
 * @param[out] strBuffer network data will be placed in this buffer
 * @param[in] ulMaxBuffer optional, default 65k, breaks reading after this limit is reached
 *
 * @return MAPI_ERROR_CODE
 * @retval MAPI_E_TOO_BIG more data in the network buffer than requested to read
 */
HRESULT ECChannel::HrReadLine(std::string &strBuffer, size_t ulMaxBuffer)
{
	size_t ulRead = 0;
	static constexpr size_t BUFSIZE = 65536;
	auto buffer = std::make_unique<char[]>(BUFSIZE);

	// clear the buffer before appending
	strBuffer.clear();
	do {
		auto hr = HrGets(buffer.get(), BUFSIZE, &ulRead);
		if (hr != hrSuccess)
			return hr;
		strBuffer.append(buffer.get(), ulRead);
		if (strBuffer.size() > ulMaxBuffer)
			return MAPI_E_TOO_BIG;
	} while (ulRead == BUFSIZE - 1); // NUL terminator is not counted
	return hrSuccess;
}

HRESULT ECChannel::HrWriteString(const string_view &strBuffer)
{
	if (lpSSL) {
		if (SSL_write(lpSSL, strBuffer.data(), static_cast<int>(strBuffer.size())) < 1)
			return MAPI_E_NETWORK_ERROR;
	} else if (send(fd, strBuffer.data(), strBuffer.size(), 0) < 1) {
		return MAPI_E_NETWORK_ERROR;
	}
	return hrSuccess;
}

/**
 * Writes a line of data to socket
 *
 * Function takes specified length of data from the pointer,
 * if length is not specified all, the data of pointed by buffer is used.
 * It then adds CRLF to the end of the data and writes it to the socket
 *
 * @param[in]	szBuffer	pointer to the data to be written to socket
 * @param[in]	len			optional parameter to specify length of data in szBuffer, if empty then all data of szBuffer is written to socket.
 *
 * @retval		MAPI_E_CALL_FAILED	unable to write data to socket
 */
HRESULT ECChannel::HrWriteLine(const char *szBuffer)
{
	auto ret = HrWriteString(szBuffer);
	if (ret != hrSuccess)
		return ret;
	return HrWriteString("\r\n");
}

HRESULT ECChannel::HrWriteLine(const string_view &strBuffer)
{
	auto ret = HrWriteString(strBuffer);
	if (ret != hrSuccess)
		return ret;
	return HrWriteString("\r\n");
}

/**
 * Read and discard bytes
 *
 * Read from socket and discard the data
 *
 * @param[in] ulByteCount Amount of bytes to discard
 *
 * @retval MAPI_E_NETWORK_ERROR Unable to read bytes.
 * @retval MAPI_E_CALL_FAILED Reading wrong amount of data.
 */
HRESULT ECChannel::HrReadAndDiscardBytes(size_t ulByteCount)
{
	size_t ulTotRead = 0;
	static constexpr size_t BUFSIZE = 4096;
	auto szBuffer = std::make_unique<char[]>(BUFSIZE);

	while (ulTotRead < ulByteCount) {
		size_t ulBytesLeft = ulByteCount - ulTotRead;
		auto ulRead = std::min(ulBytesLeft, BUFSIZE);

		if (lpSSL)
			ulRead = SSL_read(lpSSL, szBuffer.get(), ulRead);
		else
			ulRead = recv(fd, szBuffer.get(), ulRead, 0);

		if (ulRead == static_cast<size_t>(-1)) {
			if (errno == EINTR)
				continue;
			return MAPI_E_NETWORK_ERROR;
		}
		if (ulRead == 0 || ulRead > ulByteCount)
			return MAPI_E_NETWORK_ERROR;
		ulTotRead += ulRead;
	}
	return (ulTotRead == ulByteCount) ? hrSuccess : MAPI_E_CALL_FAILED;
}

HRESULT ECChannel::HrReadBytes(char *szBuffer, size_t ulByteCount)
{
	size_t ulRead = 0, ulTotRead = 0;

	if(!szBuffer)
		return MAPI_E_INVALID_PARAMETER;

	while(ulTotRead < ulByteCount) {
		if (lpSSL)
			ulRead = SSL_read(lpSSL, szBuffer + ulTotRead, ulByteCount - ulTotRead);
		else
			ulRead = recv(fd, szBuffer + ulTotRead, ulByteCount - ulTotRead, 0);

		if (ulRead == static_cast<size_t>(-1)) {
			if (errno == EINTR)
				continue;
			return MAPI_E_NETWORK_ERROR;
		}
		if (ulRead == 0 || ulRead > ulByteCount)
			return MAPI_E_NETWORK_ERROR;
		ulTotRead += ulRead;
	}
	szBuffer[ulTotRead] = '\0';
	return (ulTotRead == ulByteCount) ? hrSuccess : MAPI_E_CALL_FAILED;
}

HRESULT ECChannel::HrReadBytes(std::string * strBuffer, size_t ulByteCount)
{
	std::unique_ptr<char[]> buffer;

	if (strBuffer == nullptr || ulByteCount == SIZE_MAX)
		return MAPI_E_INVALID_PARAMETER;
	buffer.reset(new(std::nothrow) char[ulByteCount+1]);
	if (buffer == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto hr = HrReadBytes(buffer.get(), ulByteCount);
	if (hr != hrSuccess)
		return hr;
	strBuffer->assign(buffer.get(), ulByteCount);
	return hrSuccess;
}

HRESULT ECChannel::HrSelect(int seconds) {
	struct pollfd pollfd = {fd, POLLIN, 0};

	if(lpSSL && SSL_pending(lpSSL))
		return hrSuccess;
	int res = poll(&pollfd, 1, seconds * 1000);
	if (res == -1) {
		if (errno == EINTR)
			/*
			 * We _must_ return to the caller so it gets a chance
			 * to e.g. shut down as a result of SIGTERM.
			 */
			return MAPI_E_CANCEL;
		return MAPI_E_NETWORK_ERROR;
	}
	if (res == 0)
		return MAPI_E_TIMEOUT;
	return hrSuccess;
}

/**
 * read from buffer until \n is found, or buffer length is reached
 * return buffer always contains \0 in the end, so max read from network is *lpulLen -1
 *
 * @param[out] buf buffer to read network data in
 * @param[in,out] lpulLen input is max size to read, output is read bytes from network
 *
 * @return NULL on error, or buf
 */
char * ECChannel::fd_gets(char *buf, int *lpulLen) {
	char *newline = NULL, *bp = buf;
	int len = *lpulLen;

	if (--len < 1)
		return NULL;
	do {
		/*
		 * Return NULL when we read nothing:
		 * other side has closed its writing socket.
		 */
		int n = recv(fd, bp, len, MSG_PEEK);
		if (n == 0)
			return NULL;
		if (n == -1) {
			if (errno == EINTR)
				continue;
			return NULL;
		}
		newline = static_cast<char *>(memchr(bp, '\n', n));
		if (newline != nullptr)
			n = newline - bp + 1;

	retry:
		int recv_n = recv(fd, bp, n, 0);
		if (recv_n == 0)
			return NULL;
		if (recv_n == -1) {
			if (errno == EINTR)
				goto retry;
			return NULL;
		}
		bp += recv_n;
		len -= recv_n;
	}
	while(!newline && len > 0);

	//remove the lf or crlf
	if(newline){
		--bp;
		--newline;
		if(newline >= buf && *newline == '\r')
			--bp;
	}
	*bp = '\0';
	*lpulLen = (int)(bp - buf);
	return buf;
}

char * ECChannel::SSL_gets(char *buf, int *lpulLen) {
	char *newline, *bp = buf;
	int len = *lpulLen;

	if (--len < 1)
		return NULL;
	do {
		/*
		 * Return NULL when we read nothing:
		 * other side has closed its writing socket.
		 */
		int n = SSL_peek(lpSSL, bp, len);
		if (n <= 0)
			return NULL;
		newline = static_cast<char *>(memchr(bp, '\n', n));
		if (newline != nullptr)
			n = newline - bp + 1;
		n = SSL_read(lpSSL, bp, n);
		if (n < 0)
			return NULL;
		bp += n;
		len -= n;
	} while (!newline && len > 0);

	//remove the lf or crlf
	if(newline){
		--bp;
		--newline;
		if(newline >= buf && *newline == '\r')
			--bp;
	}
	*bp = '\0';
	*lpulLen = (int)(bp - buf);
	return buf;
}

void ECChannel::SetIPAddress(const struct sockaddr *sa, size_t slen)
{
	char host[256], serv[16];
	if (getnameinfo(sa, slen, host, sizeof(host), serv, sizeof(serv),
	    NI_NUMERICHOST | NI_NUMERICSERV) != 0)
		snprintf(peer_atxt, sizeof(peer_atxt), "<indeterminate>");
	else if (sa->sa_family == AF_INET6)
		snprintf(peer_atxt, sizeof(peer_atxt), "[%s]:%s", host, serv);
	else if (sa->sa_family == AF_UNIX)
		snprintf(peer_atxt, sizeof(peer_atxt), "unix:%s:%s", host, serv);
	else
		snprintf(peer_atxt, sizeof(peer_atxt), "%s:%s", host, serv);
	memcpy(&peer_sockaddr, sa, slen);
	peer_salen = slen;
}

#ifdef LINUX
static int peer_is_local2(int rsk, const void *buf, size_t bufsize)
{
	if (send(rsk, buf, bufsize, 0) < 0)
		return -errno;
	char rspbuf[512];
	ssize_t ret = recv(rsk, rspbuf, sizeof(rspbuf), 0);
	if (ret < 0)
		return -errno;
	if (static_cast<size_t>(ret) < sizeof(struct nlmsghdr))
		return -ENODATA;
	auto nlh = reinterpret_cast<const struct nlmsghdr *>(rspbuf);
	if (!NLMSG_OK(nlh, nlh->nlmsg_len))
		return -EIO;
	auto rtm = reinterpret_cast<const struct rtmsg *>(NLMSG_DATA(nlh));
	return rtm->rtm_type == RTN_LOCAL;
}
#endif

/**
 * Determine if a file descriptor refers to some kind of local connection,
 * so as to decide on flags like compression.
 *
 * Returns negative errno code if indeterminate, otherwise false/true.
 */
static int zcp_peeraddr_is_local(const struct sockaddr *peer_sockaddr,
    socklen_t peer_socklen)
{
	if (peer_sockaddr->sa_family == AF_INET6) {
		if (peer_socklen < sizeof(struct sockaddr_in6))
			return -EIO;
	} else if (peer_sockaddr->sa_family == AF_INET) {
		if (peer_socklen < sizeof(struct sockaddr_in))
			return -EIO;
	} else {
		return -EPROTONOSUPPORT;
	}
#ifdef LINUX
	int rsk = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (rsk < 0) {
		fprintf(stderr, "socket AF_NETLINK: %s\n", strerror(errno));
		return -errno;
	}
	struct {
		struct nlmsghdr nh;
		struct rtmsg rth;
		char attrbuf[512];
	} req;
	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len     = NLMSG_LENGTH(sizeof(req.rth));
	req.nh.nlmsg_flags   = NLM_F_REQUEST;
	req.nh.nlmsg_type    = RTM_GETROUTE;
	req.rth.rtm_family   = peer_sockaddr->sa_family;
	req.rth.rtm_protocol = RTPROT_UNSPEC;
	req.rth.rtm_type     = RTN_UNSPEC;
	req.rth.rtm_scope    = RT_SCOPE_UNIVERSE;
	req.rth.rtm_table    = RT_TABLE_UNSPEC;
	auto rta = reinterpret_cast<struct rtattr *>(reinterpret_cast<char *>(&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
	rta->rta_type        = RTA_DST;

	int ret = -ENODATA;
	if (peer_sockaddr->sa_family == AF_INET6) {
		const struct in6_addr &ad = reinterpret_cast<const struct sockaddr_in6 *>(peer_sockaddr)->sin6_addr;
		static const uint8_t mappedv4[] =
			{0,0,0,0, 0,0,0,0, 0,0,0xff,0xff};
		req.rth.rtm_dst_len = sizeof(ad);
		if (memcmp(&ad, mappedv4, 12) == 0) {
			/* RTM_GETROUTE won't report RTN_LOCAL for ::ffff:127.0.0.1 */
			req.rth.rtm_family = AF_INET;
			rta->rta_len = RTA_LENGTH(sizeof(struct in_addr));
			req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + rta->rta_len;
			memcpy(RTA_DATA(rta), &ad.s6_addr[12], 4);
		} else {
			rta->rta_len = RTA_LENGTH(sizeof(ad));
			req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + rta->rta_len;
			memcpy(RTA_DATA(rta), &ad, sizeof(ad));
		}
	} else if (peer_sockaddr->sa_family == AF_INET) {
		const struct in_addr &ad = reinterpret_cast<const struct sockaddr_in *>(peer_sockaddr)->sin_addr;
		req.rth.rtm_dst_len = sizeof(ad);
		rta->rta_len = RTA_LENGTH(sizeof(ad));
		req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + rta->rta_len;
		memcpy(RTA_DATA(rta), &ad, sizeof(ad));
	}
	ret = peer_is_local2(rsk, &req, req.nh.nlmsg_len);
	close(rsk);
	return ret;
#endif
	return -EPROTONOSUPPORT;
}

int kc_peer_cred(int fd, uid_t *uid, pid_t *pid)
{
#if defined(SO_PEERCRED)
#ifdef HAVE_SOCKPEERCRED_UID
	struct sockpeercred cr;
#else
	struct ucred cr;
#endif
	unsigned int cr_len = sizeof(cr);
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &cr_len) != 0 || cr_len != sizeof(cr))
		return -1;
	*uid = cr.uid; /* uid is the uid of the user that is connecting */
	*pid = cr.pid;
#elif defined(HAVE_GETPEEREID)
	gid_t gid;
	if (getpeereid(fd, uid, &gid) != 0)
		return -1;
#else
#	error I have no way to find out the remote user and I want to cry
#endif
	return 0;
}

int zcp_peerfd_is_local(int fd)
{
	struct sockaddr_storage peer_sockaddr;
	auto sa = reinterpret_cast<struct sockaddr *>(&peer_sockaddr);
	int domain = AF_UNSPEC;
	socklen_t slen = sizeof(domain);
	auto ret = getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &slen);
	if (ret < 0)
		return -errno;
	if (domain == AF_LOCAL)
		return true;
	slen = sizeof(peer_sockaddr);
	ret = getsockname(fd, sa, &slen);
	if (ret < 0)
		return -errno;
	return zcp_peeraddr_is_local(sa, slen);
}

int ECChannel::peer_is_local() const
{
	return zcp_peeraddr_is_local(reinterpret_cast<const struct sockaddr *>(&peer_sockaddr), peer_salen);
}

static int ec_listen_generic(const struct ec_socket &sk, unsigned int mode,
    const char *user, const char *group)
{
	auto fd = socket(sk.m_ai->ai_family, sk.m_ai->ai_socktype, sk.m_ai->ai_protocol);
	if (fd < 0)
		return -errno;
	auto has_sun_path = false;
	auto u = reinterpret_cast<const struct sockaddr_un *>(sk.m_ai->ai_addr);
	if (sk.m_ai->ai_family == PF_LOCAL && sk.m_ai->ai_addrlen >= offsetof(struct sockaddr_un, sun_path)) {
		struct stat sb;
		if (u->sun_path[0] == '\0')
			/* abstract socket */;
		else if (strnlen(u->sun_path, sizeof(u->sun_path)) == sizeof(u->sun_path))
			ec_log_warn("K-1553: socket path is too long and not representable");
		else if (lstat(u->sun_path, &sb) != 0)
			/* does not exist, which is ok: bind will create it */
			has_sun_path = true;
		else if (!S_ISSOCK(sb.st_mode))
			ec_log_warn("K-1555: \"%s\" already exists, but it is not a socket", u->sun_path);
		else {
			unlink(u->sun_path);
			has_sun_path = true;
		}
	}
	int y = 1;
	if (sk.m_ai->ai_family == PF_INET6 &&
	    setsockopt(fd, SOL_IPV6, IPV6_V6ONLY, &y, sizeof(y)) < 0)
		ec_log_warn("K-1556: Unable to set IPV6_V6ONLY: %s", strerror(errno));
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&y), sizeof(y)) < 0)
		ec_log_warn("K-1557: Unable to set reuseaddr socket option: %s", strerror(errno));
#ifdef LINUX
	if (!sk.m_intf.empty() && setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, sk.m_intf.c_str(), sk.m_intf.size()) < 0)
		ec_log_warn("K-1558: Unable to limit socket %s to %s: %s", sk.m_spec.c_str(), sk.m_intf.c_str(), strerror(errno));
#endif

	auto ret = bind(fd, sk.m_ai->ai_addr, sk.m_ai->ai_addrlen);
	if (ret < 0) {
		ret = -errno;
		close(fd);
		ec_log_err("K-1559: bind %s: %s", sk.m_spec.c_str(), strerror(-ret));
		return ret;
	}
	if (has_sun_path) {
		ret = unix_chown(u->sun_path, user, group);
		if (ret < 0) {
			ret = -errno;
			close(fd);
			ec_log_err("K-1560: chown \"%s\": %s", u->sun_path, strerror(-ret));
			return ret;
		}
	}
	if (has_sun_path && mode != static_cast<unsigned int>(-1)) {
		ret = chmod(u->sun_path, mode);
		if (ret < 0) {
			ret = -errno;
			close(fd);
			ec_log_err("K-1561: chmod \"%s\": %s", u->sun_path, strerror(-ret));
			return ret;
		}
	}
	if (has_sun_path)
		ec_log_debug("K-1562: changed owner \"%s\" to %s:%s mode %0o",
			u->sun_path, user != nullptr ? user : "(unchanged)",
			group != nullptr ? group : "(unchanged)", mode);
	ret = listen(fd, INT_MAX);
	if (ret < 0) {
		ret = -errno;
		close(fd);
		ec_log_err("K-1563: listen %s: %s", sk.m_spec.c_str(), strerror(-ret));
		return ret;
	}
	ec_log_info("Listening on %s (fd %d)", sk.m_spec.c_str(), fd);
	return fd;
}

HRESULT HrAccept(int ulListenFD, ECChannel **lppChannel)
{
	struct sockaddr_storage client;
	std::unique_ptr<ECChannel> lpChannel;
	socklen_t len = sizeof(client);

	if (ulListenFD < 0 || lppChannel == NULL) {
		ec_log_err("HrAccept: invalid parameters");
		return MAPI_E_INVALID_PARAMETER;
	}
#ifdef TCP_FASTOPEN
	static const int qlen = SOMAXCONN;
	if (setsockopt(ulListenFD, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen)) < 0)
		/* ignore - no harm in not having fastopen */;
#endif
	memset(&client, 0, sizeof(client));
	auto socket = accept(ulListenFD, (struct sockaddr *)&client, &len);
	if (socket == -1) {
		ec_log_err("Unable to accept(): %s", strerror(errno));
		return MAPI_E_NETWORK_ERROR;
	}
	auto chn = new(std::nothrow) ECChannel(socket);
	if (chn == nullptr) {
		close(socket);
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}
	lpChannel.reset(chn);
	lpChannel->SetIPAddress(reinterpret_cast<const struct sockaddr *>(&client), len);
	ec_log_info("Accepted connection from %s", lpChannel->peer_addr());
	*lppChannel = lpChannel.release();
	return hrSuccess;
}

static ec_socket ec_parse_bindaddr2(const char *spec)
{
	ec_socket ret;
	char *e = nullptr;
	auto y = spec;

	if (*spec == '[') {
		++spec;
		while (*y != '\0' && *y != ']')
			++y;
		ret.m_spec.assign(spec, y - spec);
		if (*y == '\0') {
			ret.m_spec = "!";
			return ret;
		}
		++y;
	} else {
		while (*y != '\0' && *y != '%' && *y != ':')
			++y;
		ret.m_spec.assign(spec, y - spec);
	}

	if (*y == '%') {
		spec = ++y;
		while (*y != '\0' && *y != ':')
			++y;
		ret.m_intf.assign(spec, y - spec);
	}
	if (*y == ':') {
		ret.m_port = strtoul(++y, &e, 10);
		y = e;
	}
	if (*y == '\0')
		return ret;
	ret.m_spec = "!";
	ret.m_port = 0;
	return ret;
}

/**
 * Tokenize bind specifier.
 * @spec:	a string in the form of INETSPEC
 *
 * If @spec is not in the desired format, the parsed host will be "!". Absence
 * of a port part will result in port being emitted as 0 - the caller needs to
 * check for this, because unfiltered, this means "random port" to the OS.
 */
struct ec_socket ec_parse_bindaddr(const char *spec)
{
	auto parts = ec_parse_bindaddr2(spec);
	if (parts.m_spec == "*")
		/* getaddrinfo/soap_bind want the empty string for wildcard binding */
		parts.m_spec.clear();
	return parts;
}

static int ec_fdtable_size()
{
	struct rlimit r;
	if (getrlimit(RLIMIT_NOFILE, &r) == 0)
		return std::min(static_cast<rlim_t>(INT_MAX), r.rlim_max);
	auto v = sysconf(_SC_OPEN_MAX);
	if (v >= 0)
		return v;
	return INT_MAX;
}

/**
 * Unset FD_CLOEXEC on listening sockets so that they survive an execve().
 */
void ec_reexec_prepare_sockets(int maxfd)
{
	size_t fdcount = 0;
	if (maxfd == -1)
		maxfd = ec_fdtable_size();
	for (int fd = 3; fd < maxfd; ++fd) {
		int set = 0;
		socklen_t setlen = sizeof(set);
		auto ret = getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &set, &setlen);
		if (ret < 0 || set == 0)
			continue;
		/*
		 * Linux kernel oddity: F_GETFD can fail if the socket owner is
		 * someone else, but F_SETFD succeeds nevertheless.
		 */
		unsigned int flags = 0;
		if (fcntl(fd, F_GETFD, &flags) != 0)
			/* ignore */;
		flags &= ~FD_CLOEXEC;
		if (fcntl(fd, F_SETFD, flags) != 0)
			ec_log_warn("fcntl F_SETFD %d: %s", fd, strerror(errno));
		++fdcount;
	}
	setenv("LISTEN_FDS", std::to_string(fdcount).c_str(), true);
	setenv("KC_LISTEN_FDS_END", std::to_string(maxfd).c_str(), true);
}

/**
 * @ai:		a single addrinfo (no list)
 *
 * Search the file descriptor table for a listening socket matching
 * the host:port specification, and return the fd and exact sockaddr.
 */
static int ec_fdtable_socket_ai(const ec_socket &sk)
{
#ifdef __sunos__
#define SO_PROTOCOL SO_PROTOTYPE
#endif
	auto ep = getenv("LISTEN_FDS");
	int maxfd = ep != nullptr ? 3 + strtoul(ep, nullptr, 0) : 0;
	ep = getenv("KC_LISTEN_FDS_END");
	if (ep != nullptr)
		/* SD protocol is a linear streak of fds, but ec_reexec does not bother with rearranging fds */
		maxfd = strtoul(ep, nullptr, 0);

	for (int fd = 3; fd < maxfd; ++fd) {
		int set = 0;
		socklen_t arglen = sizeof(set);
		auto ret = getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &set, &arglen);
		if (ret < 0 || set == 0)
			continue;
		/*
		 * The sockname is specific to the particular (domain, type,
		 * protocol) socket setup tuple, and can be equal between
		 * different protocols (think tcp:*:236 and udp:*:236), so the
		 * tuple absolutely must be checked.
		 */
		int domain = 0, type = 0, proto = 0;
		arglen = sizeof(domain);
		ret = getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &arglen);
		if (ret < 0)
			continue;
		arglen = sizeof(type);
		ret = getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &arglen);
		if (ret < 0)
			continue;
		arglen = sizeof(proto);
		ret = getsockopt(fd, SOL_SOCKET, SO_PROTOCOL, &proto, &arglen);
		if (ret < 0)
			continue;
		char ifnam[24];
		arglen = sizeof(ifnam);
		ret = getsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, ifnam, &arglen);
		if (ret != 0)
			ifnam[0] = '\0';
		else if (arglen < sizeof(ifnam))
			ifnam[arglen] = '\0';
		else
			ifnam[sizeof(ifnam)-1] = '\0';
		struct sockaddr_storage addr{};
		arglen = sizeof(addr);
		ret = getsockname(fd, reinterpret_cast<struct sockaddr *>(&addr), &arglen);
		if (ret < 0)
			continue;
		arglen = std::min(static_cast<socklen_t>(sizeof(addr)), arglen);
		auto ai = sk.m_ai;
		if (ai->ai_family != domain || ai->ai_socktype != type ||
		    ai->ai_protocol != proto || strcmp(sk.m_intf.c_str(), ifnam) != 0)
			continue;
		if (arglen == ai->ai_addrlen && memcmp(&addr, ai->ai_addr, arglen) == 0) {
			unsigned int flags = 0;
			if (fcntl(fd, F_GETFD, &flags) != 0)
				/* ignore */;
			flags |= FD_CLOEXEC;
			fcntl(fd, F_SETFD, flags);
			return fd;
		}
	}
	return -1;
}

ec_socket::~ec_socket()
{
	if (m_ai != nullptr) {
		if (m_custom_alloc)
			free(m_ai);
		else
			freeaddrinfo(m_ai);
	}
	if (m_fd >= 0)
		close(m_fd);
}

ec_socket::ec_socket(ec_socket &&o) :
	m_spec(std::move(o.m_spec)), m_intf(std::move(o.m_intf)),
	m_ai(o.m_ai), m_fd(o.m_fd), m_port(o.m_port),
	m_custom_alloc(o.m_custom_alloc)
{
	o.m_ai = nullptr;
	o.m_fd = -1;
}

bool ec_socket::operator==(const ec_socket &other) const
{
	if (m_ai == nullptr || other.m_ai == nullptr ||
	    m_ai->ai_family != other.m_ai->ai_family)
		return false;
	if (m_ai->ai_family != AF_LOCAL) {
		if (m_ai->ai_protocol != other.m_ai->ai_protocol)
			return false;
		if (m_intf != other.m_intf)
			return false;
	}
	if (m_ai->ai_addrlen != other.m_ai->ai_addrlen)
		return false;
	return memcmp(m_ai->ai_addr, other.m_ai->ai_addr, m_ai->ai_addrlen) == 0;
}

bool ec_socket::operator<(const ec_socket &other) const
{
	if (m_ai == nullptr || other.m_ai == nullptr)
		return other.m_ai != nullptr;
	if (m_ai->ai_family < other.m_ai->ai_family)
		return true;
	else if (m_ai->ai_family > other.m_ai->ai_family)
		return false;
	if (m_ai->ai_family != AF_LOCAL) {
		auto r = m_intf.compare(other.m_intf);
		if (r < 0)
			return true;
		else if (r > 0)
			return false;
		if (m_ai->ai_protocol < other.m_ai->ai_protocol)
			return true;
		else if (m_ai->ai_protocol > other.m_ai->ai_protocol)
			return false;
	}
	if (m_ai->ai_addrlen < other.m_ai->ai_addrlen)
		return true;
	else if (m_ai->ai_addrlen > other.m_ai->ai_addrlen)
		return false;
	return memcmp(m_ai->ai_addr, other.m_ai->ai_addr, m_ai->ai_addrlen) < 0;
}

static ec_socket ec_bindspec_to_unixinfo(const std::string &spec)
{
	ec_socket sk;
	sk.m_spec = spec;
	if (sk.m_spec.size() - 5 >= sizeof(sockaddr_un::sun_path)) {
		sk.m_err = -ENAMETOOLONG;
		return sk;
	}

	sk.m_custom_alloc = true;
	auto ai = sk.m_ai = static_cast<struct addrinfo *>(calloc(1, sizeof(struct addrinfo) + sizeof(struct sockaddr_un)));
	ai->ai_family   = AF_LOCAL;
	ai->ai_socktype = SOCK_STREAM;
	auto u = reinterpret_cast<struct sockaddr_un *>(ai + 1);
	ai->ai_addr     = reinterpret_cast<struct sockaddr *>(u);
	u->sun_family   = AF_LOCAL;
	strncpy(u->sun_path, sk.m_spec.c_str() + 5, sizeof(u->sun_path));
	u->sun_path[sizeof(u->sun_path)-1] = '\0';
	ai->ai_addrlen  = sizeof(struct sockaddr_un) - sizeof(u->sun_path) + strlen(u->sun_path) + 1;
	return sk;
}

static std::pair<int, std::list<ec_socket>> ec_bindspec_to_inetinfo(const char *spec)
{
	struct addrinfo hints{};
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
#ifdef IPPROTO_SCTP
	if (strncmp(spec, "sctp:", 5) == 0) {
		/*
		 * In glibc and FreeBSD libc, SCTP will only be returned if
		 * explicitly asked for. OpenBSD does not have SCTP.
		 */
		spec += 5;
		hints.ai_protocol = IPPROTO_SCTP;
	}
#endif

	std::list<ec_socket> vec;
	auto parts = ec_parse_bindaddr(spec);
	if (parts.m_spec == "!" || parts.m_port == 0)
		return {-EINVAL, std::move(vec)};
	std::unique_ptr<struct addrinfo, ai_deleter> res;
	auto ret = getaddrinfo(parts.m_spec.size() != 0 ? parts.m_spec.c_str() : nullptr,
	           std::to_string(parts.m_port).c_str(), &hints, &unique_tie(res));
	if (ret != 0) {
		ec_log_warn("getaddrinfo: %s", gai_strerror(ret));
		return {-EINVAL, std::move(vec)};
	}

	while (res != nullptr) {
		ec_socket sk;
		auto curr = res.release();
		res.reset(curr->ai_next);
		curr->ai_next = nullptr;
		sk.m_ai   = curr;
		sk.m_intf = parts.m_intf;
		sk.m_port = parts.m_port;

		/* Resolve "*" in spec into AF-specific host number */
		char tmp[256];
		if (getnameinfo(curr->ai_addr, curr->ai_addrlen, tmp,
		    sizeof(tmp), nullptr, 0, NI_NUMERICHOST) != 0)
			sk.m_spec = "<unresolvable-type-"s + std::to_string(curr->ai_family) +
			            "-" + std::to_string(curr->ai_protocol) + ">";
		else if (curr->ai_family == AF_INET6)
			sk.m_spec = "["s + tmp + "]";
		else
			sk.m_spec = tmp;
		if (!sk.m_intf.empty())
			sk.m_spec += "%" + sk.m_intf;
		if (hints.ai_family != AF_LOCAL)
			sk.m_spec += ":" + std::to_string(sk.m_port);
#ifdef IPPROTO_SCTP
		if (hints.ai_protocol == IPPROTO_SCTP)
			sk.m_spec.insert(0, "sctp:");
#endif
		vec.emplace_back(std::move(sk));
	}
	return {0, std::move(vec)};
}

static std::pair<int, std::list<ec_socket>> ec_bindspec_to_sockinfo(const std::string &spec)
{
	if (!kc_starts_with(spec, "unix:"))
		return ec_bindspec_to_inetinfo(spec.c_str());
	std::list<ec_socket> skl;
	auto sk = ec_bindspec_to_unixinfo(spec);
	if (sk.m_err >= 0)
		skl.emplace_back(std::move(sk));
	return {sk.m_err, std::move(skl)};
}

/**
 * Create listening sockets (or snatch them from environment).
 * @spec:      vector of strings in the form of { INETSPEC | UNIXSPEC }
 *
 * INETSPEC := { hostname | ipv4-addr | "[" ipv6-addr "]" } [ ":" portnumber ]
 * UNIXSPEC := "unix:" path
 *
 * NB: hostname and ipv4-addr are not specified to be enclosed in square
 * brackets, but ec_parse_bindaddr2 supports it by chance.
 */
std::pair<int, std::list<ec_socket>> ec_bindspec_to_sockets(std::vector<std::string> &&in,
    unsigned int mode, const char *user, const char *group, std::vector<int> &used_fds)
{
	std::list<ec_socket> out;
	int xerr = 0;

	for (const auto &spec : in) {
		auto p = ec_bindspec_to_sockinfo(spec);
		if (p.first != 0) {
			ec_log_err("Unrecognized format in bindspec: \"%s\"", spec.c_str());
			return {p.first, std::move(out)};
		}
		out.splice(out.end(), std::move(p.second));
	}

	for (auto &sk : out) {
		auto fd = ec_fdtable_socket_ai(sk);
		if (std::find(used_fds.cbegin(), used_fds.cend(), fd) == used_fds.cend() &&
		    fd >= 0) {
			ec_log_info("Re-using fd %d for %s", fd, sk.m_spec.c_str());
			sk.m_fd = fd;
			used_fds.push_back(fd);
			continue;
		}
		fd = ec_listen_generic(sk, mode, user, group);
		if (fd < 0) {
			sk.m_err = fd;
		} else {
			sk.m_fd = fd;
			used_fds.push_back(fd);
		}
		if (xerr == 0 && sk.m_err != 0)
			xerr = sk.m_err;
	}
	return {xerr, std::move(out)};
}

} /* namespace */
