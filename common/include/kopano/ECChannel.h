/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECCHANNEL_H
#define ECCHANNEL_H

#include <list>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <kopano/zcdefs.h>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <openssl/ossl_typ.h>
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

class KC_EXPORT ECChannel KC_FINAL {
public:
	KC_HIDDEN ECChannel(int sockfd);
	~ECChannel();
	HRESULT HrEnableTLS(void);
	KC_HIDDEN HRESULT HrGets(char *buf, size_t bufsize, size_t *have_read);
	HRESULT HrReadLine(std::string &buf, size_t maxbuf = 65536);
	HRESULT HrWriteString(const std::string & strBuffer);
	HRESULT HrWriteLine(const char *buf, size_t len = 0);
	HRESULT HrWriteLine(const std::string & strBuffer);
	KC_HIDDEN HRESULT HrReadBytes(char *buf, size_t len);
	HRESULT HrReadBytes(std::string *buf, size_t len);
	HRESULT HrReadAndDiscardBytes(size_t);
	HRESULT HrSelect(int seconds);
	KC_HIDDEN void SetIPAddress(const struct sockaddr *, size_t);
	KC_HIDDEN const char *peer_addr() const { return peer_atxt; }
	int peer_is_local(void) const;
	KC_HIDDEN bool UsingSsl() const { return lpSSL != nullptr; }
	KC_HIDDEN bool sslctx() const { return lpCTX != nullptr; }
	static HRESULT HrSetCtx(ECConfig *);
	static HRESULT HrFreeCtx();

private:
	int fd;
	SSL *lpSSL = nullptr;
	static shared_mutex ctx_lock;
	static SSL_CTX *lpCTX;
	char peer_atxt[280];
	struct sockaddr_storage peer_sockaddr;
	socklen_t peer_salen = 0;

	KC_HIDDEN char *fd_gets(char *buf, int *len);
	KC_HIDDEN char *SSL_gets(char *buf, int *len);
};

/**
 * @spec:	textual representation of the socket for reporting
 * @port:	m_port-parsed port number
 * @ai:		addrinfo (exactly one) for m_spec
 * @fd:		socket fd for this addrinfo
 * @err:	errno obtained during socket creation
 */
struct KC_EXPORT ec_socket {
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
extern KC_EXPORT HRESULT HrAccept(int fd, ECChannel **ch);
extern KC_EXPORT int kc_peer_cred(int fd, uid_t *, pid_t *);
extern KC_EXPORT int zcp_peerfd_is_local(int);
extern KC_EXPORT ec_socket ec_parse_bindaddr(const char *);
extern KC_EXPORT void ec_reexec_prepare_sockets();
extern KC_EXPORT std::pair<int, std::list<ec_socket>> ec_bindspec_to_sockets(std::vector<std::string> &&, unsigned int mode, const char *user, const char *group);

} /* namespace KC */

#endif
