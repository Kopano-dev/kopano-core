/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECCHANNEL_H
#define ECCHANNEL_H

#include <list>
#include <string>
#include <utility>
#include <vector>
#include <kopano/zcdefs.h>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <kopano/zcdefs.h>
#include <kopano/platform.h>

struct addrinfo;
struct sockaddr;

namespace KC {

class ECConfig;

// ECChannel is the communication channel with the other side. Initially, it is
// a simple way to read/write full lines of data. The reason why we specify
// a special 'HrWriteLine' instead of 'HrWrite' is that SSL encryption prefers
// writing all the data at once, instead of via multiple write() calls. Also,
// this ensures that the ECChannel class is responsible for reading, writing
// and culling newline characters.

class _kc_export ECChannel KC_FINAL {
public:
	_kc_hidden ECChannel(int sockfd);
	~ECChannel();
	HRESULT HrEnableTLS(void);
	_kc_hidden HRESULT HrGets(char *buf, size_t bufsize, size_t *have_read);
	HRESULT HrReadLine(std::string &buf, size_t maxbuf = 65536);
	HRESULT HrWriteString(const std::string & strBuffer);
	HRESULT HrWriteLine(const char *buf, size_t len = 0);
	HRESULT HrWriteLine(const std::string & strBuffer);
	_kc_hidden HRESULT HrReadBytes(char *buf, size_t len);
	HRESULT HrReadBytes(std::string *buf, size_t len);
	HRESULT HrReadAndDiscardBytes(size_t);
	HRESULT HrSelect(int seconds);
	_kc_hidden void SetIPAddress(const struct sockaddr *, size_t);
	_kc_hidden const char *peer_addr(void) const { return peer_atxt; }
	int peer_is_local(void) const;
	_kc_hidden bool UsingSsl(void) const { return lpSSL != NULL; }
	_kc_hidden bool sslctx(void) const { return lpCTX != NULL; }
	static HRESULT HrSetCtx(ECConfig *);
	static HRESULT HrFreeCtx();

private:
	int fd;
	SSL *lpSSL = nullptr;
	static SSL_CTX *lpCTX;
	char peer_atxt[280];
	struct sockaddr_storage peer_sockaddr;
	socklen_t peer_salen = 0;

	_kc_hidden char *fd_gets(char *buf, int *len);
	_kc_hidden char *SSL_gets(char *buf, int *len);
};

/**
 * @spec:	textual representation of the socket for reporting
 * @port:	m_port-parsed port number
 * @ai:		addrinfo (exactly one) for m_spec
 * @fd:		socket fd for this addrinfo
 * @err:	errno obtained during socket creation
 */
struct _kc_export ec_socket {
	public:
	ec_socket() = default;
	ec_socket(ec_socket &&);
	~ec_socket();
	bool operator==(const struct ec_socket &) const;
	bool operator<(const struct ec_socket &) const;

	std::string m_spec, m_intf;
	struct addrinfo *m_ai = nullptr;
	int m_fd = -1, m_err = 0, m_port = 0;
};

/* accept data on connection */
extern _kc_export HRESULT HrAccept(int fd, ECChannel **ch);
extern _kc_export int zcp_bindtodevice(int fd, const char *iface);
extern _kc_export int zcp_peerfd_is_local(int);
extern _kc_export ec_socket ec_parse_bindaddr(const char *);
extern _kc_export void ec_reexec_prepare_sockets();
extern _kc_export std::pair<int, std::list<ec_socket>> ec_bindspec_to_sockets(std::vector<std::string> &&, unsigned int mode, const char *user, const char *group);

} /* namespace KC */

#endif
