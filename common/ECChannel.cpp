/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <list>
#include <memory>
#include <new>
#include <vector>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <kopano/ECChannel.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <kopano/UnixUtil.h>
#include <kopano/tie.hpp>
#include <csignal>
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
#ifdef LINUX
#include <linux/rtnetlink.h>
#endif
#include <openssl/err.h>
#include <cerrno>
#include <mapicode.h>
#include "SSLUtil.h"
#ifndef hrSuccess
#define hrSuccess 0
#endif

using namespace std::string_literals;

namespace KC {

class ai_deleter {
	public:
	void operator()(struct addrinfo *ai) { freeaddrinfo(ai); }
};

/*
To generate a RSA key:
openssl genrsa -out privkey.pem 2048

Creating a certificate request:
openssl req -new -key privkey.pem -out cert.csr

Creating a self-signed test certificate:
openssl req -new -x509 -key privkey.pem -out cacert.pem -days 1095
*/

// because of statics
SSL_CTX* ECChannel::lpCTX = NULL;

HRESULT ECChannel::HrSetCtx(ECConfig *lpConfig)
{
	if (lpConfig == NULL) {
		ec_log_err("ECChannel::HrSetCtx(): invalid parameters");
		return MAPI_E_CALL_FAILED;
	}

	HRESULT hr = hrSuccess;
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
		return MAPI_E_CALL_FAILED;
	}
	auto key_fh = fopen(key_file, "r");
	if (key_fh == nullptr) {
		ec_log_err("ECChannel::HrSetCtx(): cannot open key file");
		return MAPI_E_CALL_FAILED;
	}
	fclose(key_fh);

	auto cert_fh = fopen(cert_file, "r");
	if (cert_fh == nullptr) {
		ec_log_err("ECChannel::HrSetCtx(): cannot open cert file");
		return MAPI_E_CALL_FAILED;
	}
	fclose(cert_fh);

	if (lpCTX) {
		SSL_CTX_free(lpCTX);
		lpCTX = NULL;
	}

	SSL_library_init();
	SSL_load_error_strings();

	// enable *all* server methods, not just ssl2 and ssl3, but also tls1 and tls1.1
	lpCTX = SSL_CTX_new(SSLv23_server_method());
#ifndef SSL_OP_NO_RENEGOTIATION
#	define SSL_OP_NO_RENEGOTIATION 0 /* unavailable in openSSL 1.0 */
#endif
	SSL_CTX_set_options(lpCTX, SSL_OP_ALL | SSL_OP_NO_RENEGOTIATION);
	auto tlsprot = lpConfig->GetSetting("tls_min_proto");
	if (!ec_tls_minproto(lpCTX, tlsprot)) {
		ec_log_err("Unknown SSL/TLS protocol version \"%s\"", tlsprot);
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
#if !defined(OPENSSL_NO_ECDH) && defined(NID_X9_62_prime256v1)
	ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	if (ecdh != NULL) {
		/* SINGLE_ECDH_USE = renegotiate exponent for each handshake */
		SSL_CTX_set_options(lpCTX, SSL_OP_SINGLE_ECDH_USE);
		SSL_CTX_set_tmp_ecdh(lpCTX, ecdh);
		EC_KEY_free(ecdh);
	}
#endif

	if (ssl_ciphers && SSL_CTX_set_cipher_list(lpCTX, ssl_ciphers) != 1) {
		ec_log_err("Can not set SSL cipher list to \"%s\": %s", ssl_ciphers, ERR_error_string(ERR_get_error(), 0));
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	if (parseBool(lpConfig->GetSetting("ssl_prefer_server_ciphers")))
		SSL_CTX_set_options(lpCTX, SSL_OP_CIPHER_SERVER_PREFERENCE);
#if !defined(OPENSSL_NO_ECDH) && defined(SSL_CTX_set1_curves_list)
	if (ssl_curves && SSL_CTX_set1_curves_list(lpCTX, ssl_curves) != 1) {
		ec_log_err("Can not set SSL curve list to \"%s\": %s", ssl_curves, ERR_error_string(ERR_get_error(), 0));
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	SSL_CTX_set_ecdh_auto(lpCTX, 1);
#endif

	SSL_CTX_set_default_verify_paths(lpCTX);
	if (SSL_CTX_use_certificate_chain_file(lpCTX, cert_file) != 1) {
		ec_log_err("SSL CTX certificate file error: %s", ERR_error_string(ERR_get_error(), 0));
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	if (SSL_CTX_use_PrivateKey_file(lpCTX, key_file, SSL_FILETYPE_PEM) != 1) {
		ec_log_err("SSL CTX private key file error: %s", ERR_error_string(ERR_get_error(), 0));
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	if (SSL_CTX_check_private_key(lpCTX) != 1) {
		ec_log_err("SSL CTX check private key error: %s", ERR_error_string(ERR_get_error(), 0));
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	if (strcmp(lpConfig->GetSetting("ssl_verify_client"), "yes") == 0)
		SSL_CTX_set_verify(lpCTX, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, 0);
	else
		SSL_CTX_set_verify(lpCTX, SSL_VERIFY_NONE, 0);

	if (lpConfig->GetSetting("ssl_verify_file")[0])
		szFile = lpConfig->GetSetting("ssl_verify_file");
	if (lpConfig->GetSetting("ssl_verify_path")[0])
		szPath = lpConfig->GetSetting("ssl_verify_path");
	if ((szFile || szPath) && SSL_CTX_load_verify_locations(lpCTX, szFile, szPath) != 1)
		ec_log_err("SSL CTX error loading verify locations: %s", ERR_error_string(ERR_get_error(), 0));
exit:
	if (hr != hrSuccess)
		HrFreeCtx();
	return hr;
}

HRESULT ECChannel::HrFreeCtx() {
	if (lpCTX) {
		SSL_CTX_free(lpCTX);
		lpCTX = NULL;
	}
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

HRESULT ECChannel::HrEnableTLS(void)
{
	int rc = -1;
	HRESULT hr = hrSuccess;

	if (lpSSL || lpCTX == NULL) {
		hr = MAPI_E_CALL_FAILED;
		ec_log_err("ECChannel::HrEnableTLS(): invalid parameters");
		goto exit;
	}

	lpSSL = SSL_new(lpCTX);
	if (!lpSSL) {
		ec_log_err("ECChannel::HrEnableTLS(): SSL_new failed");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
	SSL_clear(lpSSL);
	if (SSL_set_fd(lpSSL, fd) != 1) {
		ec_log_err("ECChannel::HrEnableTLS(): SSL_set_fd failed");
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}

	SSL_set_accept_state(lpSSL);
	rc = SSL_accept(lpSSL);
	if (rc != 1) {
		ec_log_err("ECChannel::HrEnableTLS(): SSL_accept failed: %d", SSL_get_error(lpSSL, rc));
		hr = MAPI_E_CALL_FAILED;
		goto exit;
	}
exit:
	if (hr != hrSuccess && lpSSL) {
		SSL_shutdown(lpSSL);
		SSL_free(lpSSL);
		lpSSL = NULL;
	}
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
	char buffer[65536];

	// clear the buffer before appending
	strBuffer.clear();
	do {
		auto hr = HrGets(buffer, 65536, &ulRead);
		if (hr != hrSuccess)
			return hr;
		strBuffer.append(buffer, ulRead);
		if (strBuffer.size() > ulMaxBuffer)
			return MAPI_E_TOO_BIG;
	} while (ulRead == 65535);	// zero-terminator is not counted
	return hrSuccess;
}

HRESULT ECChannel::HrWriteString(const std::string & strBuffer) {
	if (lpSSL) {
		if (SSL_write(lpSSL, strBuffer.c_str(), (int)strBuffer.size()) < 1)
			return MAPI_E_NETWORK_ERROR;
	} else if (send(fd, strBuffer.c_str(), (int)strBuffer.size(), 0) < 1) {
		return MAPI_E_NETWORK_ERROR;
	}
	return hrSuccess;
}

/**
 * Writes a line of data to socket
 *
 * Function takes specified length of data from the pointer,
 * if length is not specified all the data of pointed by buffer is used.
 * It then adds CRLF to the end of the data and writes it to the socket
 *
 * @param[in]	szBuffer	pointer to the data to be written to socket
 * @param[in]	len			optional parameter to specify length of data in szBuffer, if empty then all data of szBuffer is written to socket.
 *
 * @retval		MAPI_E_CALL_FAILED	unable to write data to socket
 */
HRESULT ECChannel::HrWriteLine(const char *szBuffer, size_t len)
{
	std::string strLine;

	if (len == 0)
		strLine.assign(szBuffer);
	else
		strLine.assign(szBuffer, len);

	strLine += "\r\n";
	return HrWriteString(std::move(strLine));
}

HRESULT ECChannel::HrWriteLine(const std::string & strBuffer) {
	return HrWriteString(strBuffer + "\r\n");
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
	char szBuffer[4096];

	while (ulTotRead < ulByteCount) {
		size_t ulBytesLeft = ulByteCount - ulTotRead;
		auto ulRead = std::min(ulBytesLeft, sizeof(szBuffer));

		if (lpSSL)
			ulRead = SSL_read(lpSSL, szBuffer, ulRead);
		else
			ulRead = recv(fd, szBuffer, ulRead, 0);

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

int ECChannel::peer_is_local(void) const
{
	return zcp_peeraddr_is_local(reinterpret_cast<const struct sockaddr *>(&peer_sockaddr), peer_salen);
}

int zcp_bindtodevice(int fd, const char *i)
{
	if (i == NULL || strcmp(i, "any") == 0 || strcmp(i, "all") == 0 ||
	    strcmp(i, "") == 0)
		return 0;
#ifdef LINUX
	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, i, strlen(i)) >= 0)
		return 0;
	ec_log_err("Unable to bind to interface %s: %s", i, strerror(errno));
	return -errno;
#else
	ec_log_err("Bind-to-interface not supported.");
	return -ENOSYS;
#endif
}

static int ec_listen_generic(const struct ec_socket &sk, unsigned int mode,
    const char *user, const char *group)
{
	auto fd = socket(sk.m_ai->ai_family, sk.m_ai->ai_socktype, sk.m_ai->ai_protocol);
	if (fd < 0)
		return -errno;
	if (sk.m_ai->ai_family == PF_LOCAL && sk.m_ai->ai_addrlen >= offsetof(struct sockaddr_un, sun_path)) {
		auto u = reinterpret_cast<const struct sockaddr_un *>(sk.m_ai->ai_addr);
		struct stat sb;
		if (u->sun_path[0] == '\0')
			/* abstract socket */;
		else if (strnlen(u->sun_path, sizeof(u->sun_path)) == sizeof(u->sun_path))
			/* cannot test */;
		else if (lstat(u->sun_path, &sb) != 0)
			/* cannot test */;
		else if (!S_ISSOCK(sb.st_mode))
			ec_log_warn("\"%s\" already exists, but it is not a socket", u->sun_path);
		else
			unlink(u->sun_path);
	}
	int y = 1;
	if (sk.m_ai->ai_family == PF_INET6 &&
	    setsockopt(fd, SOL_IPV6, IPV6_V6ONLY, &y, sizeof(y)) < 0)
		ec_log_warn("Unable to set IPV6_V6ONLY: %s", strerror(errno));
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&y), sizeof(y)) < 0)
		ec_log_warn("Unable to set reuseaddr socket option: %s", strerror(errno));
	auto ret = bind(fd, sk.m_ai->ai_addr, sk.m_ai->ai_addrlen);
	if (ret < 0) {
		ret = -errno;
		close(fd);
		ec_log_err("bind %s: %s", sk.m_spec.c_str(), strerror(-ret));
		return ret;
	}
	ret = unix_chown(fd, user, group);
	if (ret < 0) {
		ret = -errno;
		close(fd);
		ec_log_err("%s: chown %s: %s", __func__, sk.m_spec.c_str(), strerror(-ret));
		return ret;
	}
	if (mode != static_cast<unsigned int>(-1)) {
		ret = fchmod(fd, mode);
		if (ret < 0) {
			ret = -errno;
			close(fd);
			ec_log_err("%s: chmod %s: %s", __func__, sk.m_spec.c_str(), strerror(-ret));
			return ret;
		}
	}
	ret = listen(fd, INT_MAX);
	if (ret < 0) {
		ret = -errno;
		close(fd);
		ec_log_err("%s: listen %s: %s", __func__, sk.m_spec.c_str(), strerror(-ret));
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

static std::pair<std::string, uint16_t>
ec_parse_bindaddr2(const char *spec)
{
	char *e = nullptr;
	if (*spec != '[') { /* ] */
		/* IPv4 or hostname */
		auto y = strchr(spec, ':');
		if (y == nullptr)
			return {spec, 0};
		uint16_t port = strtoul(y + 1, &e, 10);
		if (e == nullptr || *e != '\0')
			return {"!", 0};
		return {std::string(spec, y - spec), port};
	}
	/* address (v4/v6) */
	auto y = strchr(spec + 1, ']');
	if (y == nullptr)
		return {"!", 0};
	if (*++y == '\0')
		return {std::string(spec + 1, y - spec - 2), 0};
	if (*y != ':')
		return {"!", 0};
	uint16_t port = strtoul(y + 1, &e, 10);
	if (e == nullptr || *e != '\0')
		return {"!", 0};
	return {std::string(spec + 1, y - spec - 2), port};
}

/**
 * Tokenize bind specifier.
 * @spec:	a string in the form of INETSPEC
 *
 * If @spec is not in the desired format, the parsed host will be "!". Absence
 * of a port part will result in port being emitted as 0 - the caller needs to
 * check for this, because unfiltered, this means "random port" to the OS.
 */
std::pair<std::string, uint16_t> ec_parse_bindaddr(const char *spec)
{
	auto parts = ec_parse_bindaddr2(spec);
	if (parts.first == "*")
		/* getaddrinfo/soap_bind want the empty string for wildcard binding */
		parts.first.clear();
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
void ec_reexec_prepare_sockets()
{
	auto maxfd = ec_fdtable_size();
	for (int fd = 3; fd < maxfd; ++fd) {
		int set = 0;
		socklen_t setlen = sizeof(set);
		auto ret = getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &set, &setlen);
		if (ret < 0 && errno == EBADF)
			break;
		else if (ret < 0 || set == 0)
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
	}
}

/**
 * @ai:		a single addrinfo (no list)
 *
 * Search the file descriptor table for a listening socket matching
 * the host:port specification, and return the fd and exact sockaddr.
 */
static int ec_fdtable_socket_ai(const struct addrinfo *ai)
{
#ifdef __sunos__
#define SO_PROTOCOL SO_PROTOTYPE
#endif
	auto maxfd = ec_fdtable_size();
	for (int fd = 3; fd < maxfd; ++fd) {
		int set = 0;
		socklen_t arglen = sizeof(set);
		auto ret = getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &set, &arglen);
		if (ret < 0 && errno == EBADF)
			break;
		else if (ret < 0 || set == 0)
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
		struct sockaddr_storage addr{};
		arglen = sizeof(addr);
		ret = getsockname(fd, reinterpret_cast<struct sockaddr *>(&addr), &arglen);
		if (ret < 0)
			continue;
		arglen = std::min(static_cast<socklen_t>(sizeof(addr)), arglen);
		if (ai->ai_family != domain || ai->ai_socktype != type ||
		    ai->ai_protocol != proto)
			continue;
		if (arglen == ai->ai_addrlen && memcmp(&addr, ai->ai_addr, arglen) == 0) {
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			return fd;
		}
	}
	return -1;
}

ec_socket::~ec_socket()
{
	freeaddrinfo(m_ai);
	if (m_fd >= 0)
		close(m_fd);
}

ec_socket::ec_socket(ec_socket &&o) :
	m_spec(std::move(o.m_spec)), m_ai(o.m_ai), m_fd(o.m_fd), m_port(o.m_port)
{
	o.m_ai = nullptr;
	o.m_fd = -1;
}

bool ec_socket::operator==(const ec_socket &other) const
{
	if (m_ai == nullptr || other.m_ai == nullptr ||
	    m_ai->ai_family != other.m_ai->ai_family)
		return false;
	if (m_ai->ai_family != AF_LOCAL &&
	    m_ai->ai_protocol != other.m_ai->ai_protocol)
		return false;
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

static ec_socket ec_bindspec_to_unixinfo(std::string &&spec)
{
	ec_socket sk;
	sk.m_spec = std::move(spec);
	if (sk.m_spec.size() - 5 >= sizeof(sockaddr_un::sun_path)) {
		sk.m_err = -ENAMETOOLONG;
		return sk;
	}

	auto ai = sk.m_ai = static_cast<struct addrinfo *>(calloc(1, sizeof(struct addrinfo) + sizeof(struct sockaddr_storage)));
	ai->ai_family   = AF_LOCAL;
	ai->ai_socktype = SOCK_STREAM;
	ai->ai_addrlen  = sizeof(struct sockaddr_un);
	auto u = reinterpret_cast<struct sockaddr_un *>(ai + 1);
	ai->ai_addr     = reinterpret_cast<struct sockaddr *>(u);
	u->sun_family   = AF_LOCAL;
	strncpy(u->sun_path, sk.m_spec.c_str() + 5, sizeof(u->sun_path));
	u->sun_path[sizeof(u->sun_path)-1] = '\0';
	return sk;
}

static std::pair<int, std::list<ec_socket>> ec_bindspec_to_inetinfo(const char *spec)
{
	struct addrinfo hints{};
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	std::list<ec_socket> vec;
	auto parts = ec_parse_bindaddr(spec);
	if (parts.first == "!" || parts.second == 0)
		return {-EINVAL, std::move(vec)};
	std::unique_ptr<struct addrinfo, ai_deleter> res;
	auto ret = getaddrinfo(parts.first.size() != 0 ? parts.first.c_str() : nullptr,
	           std::to_string(parts.second).c_str(), &hints, &unique_tie(res));
	if (ret != 0) {
		ec_log_warn("getaddrinfo: %s", gai_strerror(ret));
		return {-EINVAL, std::move(vec)};
	}

	while (res != nullptr) {
		ec_socket sk;
		auto curr = res.release();
		res.reset(curr->ai_next);
		curr->ai_next = nullptr;
		sk.m_ai = curr;
		sk.m_port = parts.second;
		char tmp[256];
		if (getnameinfo(curr->ai_addr, curr->ai_addrlen, tmp,
		    sizeof(tmp), nullptr, 0, NI_NUMERICHOST) != 0)
			sk.m_spec = tmp + " <type-"s + std::to_string(curr->ai_family) +
			            "-" + std::to_string(curr->ai_protocol) + ">";
		else if (curr->ai_family == AF_INET6)
			sk.m_spec = "["s + tmp + "]:" + std::to_string(sk.m_port);
		else
			sk.m_spec = tmp + ":"s + std::to_string(sk.m_port);
		vec.emplace_back(std::move(sk));
	}
	return {0, std::move(vec)};
}

static std::pair<int, std::list<ec_socket>> ec_bindspec_to_sockinfo(std::string &&spec)
{
	if (kc_starts_with(spec, "unix:")) {
		std::list<ec_socket> skl;
		auto sk = ec_bindspec_to_unixinfo(std::move(spec));
		if (sk.m_err >= 0)
			skl.emplace_back(std::move(sk));
		return {sk.m_err, std::move(skl)};
	}
	return ec_bindspec_to_inetinfo(spec.c_str());
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
    unsigned int mode, const char *user, const char *group)
{
	std::list<ec_socket> out;
	int xerr = 0;

	for (auto &&spec : in) {
		auto p = ec_bindspec_to_sockinfo(std::move(spec));
		if (p.first != 0)
			return {p.first, std::move(out)};
		out.splice(out.end(), std::move(p.second));
	}
	out.sort();
	out.unique();

	for (auto &sk : out) {
		auto fd = ec_fdtable_socket_ai(sk.m_ai);
		if (fd >= 0) {
			ec_log_info("Re-using fd %d for %s", fd, sk.m_spec.c_str());
			sk.m_fd = fd;
			continue;
		}
		fd = ec_listen_generic(sk, mode, user, group);
		if (fd < 0)
			sk.m_err = fd;
		else
			sk.m_fd = fd;
		if (xerr == 0 && sk.m_err != 0)
			xerr = sk.m_err;
	}
	return {xerr, std::move(out)};
}

} /* namespace */
