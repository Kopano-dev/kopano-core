/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <kopano/ECChannel.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <kopano/UnixUtil.h>
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
#ifndef hrSuccess
#define hrSuccess 0
#endif

namespace KC {

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
	std::unique_ptr<char[], cstdlib_deleter> ssl_protocols(strdup(lpConfig->GetSetting("ssl_protocols")));
	const char *ssl_ciphers = lpConfig->GetSetting("ssl_ciphers");
	const char *ssl_curves = lpConfig->GetSetting("ssl_curves");
 	char *ssl_name = NULL;
 	int ssl_op = 0, ssl_include = 0, ssl_exclude = 0;
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
	ssl_name = strtok(ssl_protocols.get(), " ");
	while(ssl_name != NULL) {
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
#ifdef SSL_OP_NO_TLSv1_3
		else if (strcasecmp(ssl_name, "TLSv1.3") == 0)
			ssl_proto = 0x20;
#endif
		else if (!ssl_neg) {
			ec_log_err("Unknown protocol \"%s\" in ssl_protocols setting", ssl_name);
			hr = MAPI_E_CALL_FAILED;
			goto exit;
		}

		if (ssl_neg)
			ssl_exclude |= ssl_proto;
		else
			ssl_include |= ssl_proto;

		ssl_name = strtok(NULL, " ");
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
	if (ssl_protocols)
		SSL_CTX_set_options(lpCTX, ssl_op);

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
static int peer_is_local2(int rsk, const struct nlmsghdr *nlh)
{
	if (send(rsk, nlh, nlh->nlmsg_len, 0) < 0)
		return -errno;
	char rspbuf[512];
	ssize_t ret = recv(rsk, rspbuf, sizeof(rspbuf), 0);
	if (ret < 0)
		return -errno;
	if (static_cast<size_t>(ret) < sizeof(struct nlmsghdr))
		return -ENODATA;
	nlh = reinterpret_cast<const struct nlmsghdr *>(rspbuf);
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
int zcp_peeraddr_is_local(const struct sockaddr *peer_sockaddr,
    socklen_t peer_socklen)
{
	if (peer_sockaddr->sa_family == AF_UNIX) {
		return true;
	} else if (peer_sockaddr->sa_family == AF_INET6) {
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
	ret = peer_is_local2(rsk, &req.nh);
	close(rsk);
	return ret;
#endif
	return -EPROTONOSUPPORT;
}

int zcp_peerfd_is_local(int fd)
{
	struct sockaddr_storage peer_sockaddr;
	socklen_t peer_socklen = sizeof(peer_sockaddr);
	auto sa = reinterpret_cast<struct sockaddr *>(&peer_sockaddr);
	int ret = getsockname(fd, sa, &peer_socklen);
	if (ret < 0)
		return -errno;
	return zcp_peeraddr_is_local(sa, peer_socklen);
}

int ECChannel::peer_is_local(void) const
{
	return zcp_peeraddr_is_local(reinterpret_cast<const struct sockaddr *>(&peer_sockaddr), peer_salen);
}

/**
 * getaddrinfo() adheres to the preference weights given in /etc/gai.conf,
 * but only for connect sockets. For AI_PASSIVE, sockets may be returned in
 * any order. This function will reorder an addrinfo linked list and place
 * IPv6 in the front.
 */
static struct addrinfo *reorder_addrinfo_ipv6(struct addrinfo *node)
{
	struct addrinfo v6head, othead;
	v6head.ai_next = NULL;
	othead.ai_next = node;
	struct addrinfo *v6tail = &v6head, *prev = &othead;

	while (node != NULL) {
		if (node->ai_family != AF_INET6) {
			prev = node;
			node = node->ai_next;
			continue;
		}
		/* disconnect current node (INET6) */
		prev->ai_next = node->ai_next;
		node->ai_next = NULL;
		/* - reattach to v6 list */
		v6tail->ai_next = node;
		v6tail = node;
		/* continue in ot list */
		node = prev->ai_next;
	}
	/* join list */
	v6tail->ai_next = othead.ai_next;
	return v6head.ai_next;
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

/**
 * Create a PF_LOCAL socket for listening and return the fd.
 */
static int ec_listen_localsock(const char *path, int *pfd, int mode,
    const char *user, const char *group)
{
	struct sockaddr_un sk;
	if (strlen(path) + 1 >= sizeof(sk.sun_path)) {
		ec_log_err("%s: \"%s\" is too long", __func__, path);
		return -EINVAL;
	}
	struct stat sb;
	auto ret = lstat(path, &sb);
	if (ret == 0 && !S_ISSOCK(sb.st_mode)) {
		ec_log_err("%s: file already exists, but it is not a socket", path);
		return -EADDRINUSE;
	}
	unlink(path);
	int fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd < 0) {
		ec_log_err("%s: socket: %s", __func__, strerror(errno));
		return -errno;
	}
	sk.sun_family = AF_LOCAL;
	kc_strlcpy(sk.sun_path, path, sizeof(sk.sun_path));
	ret = bind(fd, reinterpret_cast<const sockaddr *>(&sk), sizeof(sk));
	if (ret < 0) {
		int saved_errno = errno;
		ec_log_err("%s: bind %s: %s", __func__, path, strerror(saved_errno));
		close(fd);
		return -(errno = saved_errno);
	}
	ret = unix_chown(path, user, group);
	if (ret < 0) {
		ec_log_err("%s: chown %s: %s", __func__, path, strerror(-ret));
		return ret;
	}
	if (mode != static_cast<mode_t>(-1)) {
		ret = chmod(path, mode);
		if (ret < 0) {
			ret = -errno;
			ec_log_err("%s: chown %s: %s", __func__, path, strerror(-ret));
			return ret;
		}
	}
	ret = listen(fd, INT_MAX);
	if (ret < 0) {
		int saved_errno = errno;
		ec_log_err("%s: listen: %s", __func__, strerror(saved_errno));
		close(fd);
		return -(errno = saved_errno);
	}
	*pfd = fd;
	return 0;
}

/**
 * Create PF_INET/PF_INET6 socket for listening and return the fd.
 */
static int ec_listen_inet(const char *szBind, uint16_t ulPort, int *lpulListenSocket)
{
	int fd = -1, opt = 1, ret;
	struct addrinfo *sock_res = NULL, sock_hints;
	const struct addrinfo *sock_addr, *sock_last = NULL;
	char port_string[sizeof("65535")];

	if (lpulListenSocket == nullptr || ulPort == 0 || szBind == nullptr)
		return -EINVAL;

	snprintf(port_string, sizeof(port_string), "%u", ulPort);
	memset(&sock_hints, 0, sizeof(sock_hints));
	/*
	 * AI_NUMERICHOST is reflected in the kopano documentation:
	 * an address is required for the "server_bind" parameter.
	 */
	sock_hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;
	sock_hints.ai_socktype = SOCK_STREAM;
	ret = getaddrinfo(*szBind == '\0' ? NULL : szBind,
	      port_string, &sock_hints, &sock_res);
	if (ret == EAI_SYSTEM) {
		ec_log_err("getaddrinfo [%s]:%u: %s", szBind, ulPort, strerror(errno));
		return -errno;
	} else if (ret != 0) {
		ec_log_err("getaddrinfo [%s]:%u: %s", szBind, ulPort, gai_strerror(ret));
		return -EINVAL;
	}
	sock_res = reorder_addrinfo_ipv6(sock_res);

	errno = 0;
	for (sock_addr = sock_res; sock_addr != NULL;
	     sock_addr = sock_addr->ai_next)
	{
		sock_last = sock_addr;
		fd = socket(sock_addr->ai_family, sock_addr->ai_socktype,
		     sock_addr->ai_protocol);
		if (fd < 0) {
			if (errno != EAFNOSUPPORT)
				break;
			continue;
		}

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		    reinterpret_cast<const char *>(&opt), sizeof(opt)) < 0)
			ec_log_warn("Unable to set reuseaddr socket option: %s",
				strerror(errno));

		ret = bind(fd, sock_addr->ai_addr, sock_addr->ai_addrlen);
		if (ret < 0 && errno == EADDRINUSE) {
			/*
			 * If the port is used, drop out early. Do not let it
			 * happen that we move to an AF where it happens to be
			 * unused.
			 */
			int saved_errno = errno;
			close(fd);
			fd = -1;
			errno = saved_errno;
			break;
		}
		if (ret < 0) {
			int saved_errno = errno;
			close(fd);
			fd = -1;
			errno = saved_errno;
			continue;
		}

		if (listen(fd, INT_MAX) < 0) {
			auto saved_errno = errno;
			ec_log_err("Unable to start listening on socket [%s]:%d: %s",
				szBind, ulPort, strerror(errno));
			errno = saved_errno;
			goto exit;
		}
		/*
		 * Function signature currently only permits a single fd, so if
		 * we have a good socket, try no more. The IPv6 socket is
		 * generally returned first, and is also IPv4-capable
		 * (through mapped addresses).
		 */
		break;
	}
	if (fd < 0 && sock_last != NULL) {
		ec_log_crit("Unable to create socket for [%s]:%u (family=%u protocol=%u): %s",
			szBind, ulPort, sock_last->ai_family,
			sock_last->ai_protocol, strerror(errno));
		goto exit;
	} else if (fd < 0) {
		ec_log_err("OS proposed no sockets for \"[%s]:%u\"", szBind, ulPort);
		errno = ENOENT;
		goto exit;
	}
	*lpulListenSocket = fd;
	errno = 0;
exit:
	int saved_errno = errno;
	if (sock_res != NULL)
		freeaddrinfo(sock_res);
	errno = saved_errno;
	return -errno;
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
	/* IPv6 */
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

/**
 * Create a listening socket.
 * @spec:	a string in the form of { INETSPEC | UNIXSPEC }
 *
 * INETSPEC := { hostname | ipv4-addr | "[" ipv6-addr "]" } [ ":" portnumber ]
 * UNIXSPEC := "unix:" path
 *
 * NB: hostname and ipv4-addr are not specified to be enclosed in square
 * brackets, but ec_parse_bindaddr2 supports it by chance.
 */
int ec_listen_generic(const char *spec, int *pfd, int mode,
    const char *user, const char *group)
{
	if (strncmp(spec, "unix:", 5) == 0)
		return ec_listen_localsock(spec + 5, pfd, mode, user, group);
	auto parts = ec_parse_bindaddr(spec);
	if (parts.first == "!" || parts.second == 0)
		return -EINVAL;
	return ec_listen_inet(parts.first.c_str(), parts.second, pfd);
}

bool ec_bindaddr_less::operator()(const std::string &a, const std::string &b) const
{
	return ec_parse_bindaddr(a.c_str()) < ec_parse_bindaddr(b.c_str());
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

static int ec_fdtable_socket_ai(const struct addrinfo *ai,
    struct sockaddr_storage *oaddr, socklen_t *olen)
{
	auto maxfd = ec_fdtable_size();
	for (int fd = 3; fd < maxfd; ++fd) {
		int set = 0;
		socklen_t arglen = sizeof(set);
		auto ret = getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &set, &arglen);
		if (ret < 0 && errno == EBADF)
			break;
		else if (ret < 0 || set == 0)
			continue;
		struct sockaddr_storage addr{};
		arglen = sizeof(addr);
		ret = getsockname(fd, reinterpret_cast<struct sockaddr *>(&addr), &arglen);
		if (ret < 0)
			continue;
		arglen = std::min(static_cast<socklen_t>(sizeof(addr)), arglen);
		for (auto sk = ai; sk != nullptr; sk = sk->ai_next) {
			if (arglen != sk->ai_addrlen ||
			    memcmp(&addr, sk->ai_addr, arglen) != 0)
				continue;
			if (oaddr != nullptr) {
				memset(oaddr, 0, sizeof(*oaddr));
				memcpy(oaddr, sk->ai_addr, arglen);
			}
			if (olen != nullptr)
				*olen = arglen;
			fcntl(fd, F_SETFD, FD_CLOEXEC);
			return fd;
		}
	}
	return -1;
}

/**
 * Search the file descriptor table for a listening socket matching
 * the host:port specification, and return the fd and exact sockaddr.
 *
 * This somewhat convoluted search is necessary because some daemons
 * have both plain and SSL sockets and must match each fd with the
 * config directives.
 */
int ec_fdtable_socket(const char *spec, struct sockaddr_storage *oaddr,
    socklen_t *olen)
{
	struct addrinfo hints{}, *res = nullptr;
	hints.ai_flags    = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	if (strncmp(spec, "unix:", 5) == 0) {
		struct sockaddr_un u{};
		kc_strlcpy(u.sun_path, spec + 5, sizeof(u.sun_path));
		hints.ai_family  = u.sun_family = AF_LOCAL;
		hints.ai_addr    = reinterpret_cast<struct sockaddr *>(&u);
		hints.ai_addrlen = sizeof(u) - sizeof(u.sun_path) + strlen(u.sun_path) + 1;
		return ec_fdtable_socket_ai(&hints, oaddr, olen);
	}
	hints.ai_family = AF_UNSPEC;
	auto parts = ec_parse_bindaddr(spec);
	if (parts.first == "!" || parts.second == 0)
		return -EINVAL;
	auto ret = getaddrinfo(parts.first[0] == '\0' ? nullptr : parts.first.c_str(),
	           std::to_string(parts.second).c_str(), &hints, &res);
	if (ret != 0 || res == nullptr)
		return -1;
	ret = ec_fdtable_socket_ai(res, oaddr, olen);
	freeaddrinfo(res);
	return ret;
}

} /* namespace */
