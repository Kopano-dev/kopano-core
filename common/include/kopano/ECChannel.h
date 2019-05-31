/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECCHANNEL_H
#define ECCHANNEL_H

#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <kopano/zcdefs.h>

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

class _kc_export ec_bindaddr_less {
	public:
	bool operator()(const std::string &, const std::string &) const;
};

/* helpers to open socket */
extern _kc_export int ec_listen_generic(const char *bind, int *fd, int mode = -1, const char *user = nullptr, const char *group = nullptr);
/* accept data on connection */
extern _kc_export HRESULT HrAccept(int fd, ECChannel **ch);
extern _kc_export int zcp_bindtodevice(int fd, const char *iface);
extern int zcp_peeraddr_is_local(const struct sockaddr *, socklen_t);
extern _kc_export int zcp_peerfd_is_local(int);
extern _kc_export std::pair<std::string, uint16_t> ec_parse_bindaddr(const char *);
extern _kc_export void ec_reexec_prepare_sockets();
extern _kc_export int ec_fdtable_socket(const char *);

} /* namespace KC */

#endif
