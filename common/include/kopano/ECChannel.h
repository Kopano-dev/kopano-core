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

#ifndef ECCHANNEL_H
#define ECCHANNEL_H

#include <set>
#include <string>
#include <utility>
#include <kopano/zcdefs.h>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>

struct sockaddr;

namespace KC {

// ECChannel is the communication channel with the other side. Initially, it is
// a simple way to read/write full lines of data. The reason why we specify
// a special 'HrWriteLine' instead of 'HrWrite' is that SSL encryption prefers
// writing all the data at once, instead of via multiple write() calls. Also, 
// this ensures that the ECChannel class is responsible for reading, writing
// and culling newline characters.

class _kc_export ECChannel _kc_final {
public:
	_kc_hidden ECChannel(int sockfd);
	~ECChannel();
	HRESULT HrEnableTLS(void);
	_kc_hidden HRESULT HrGets(char *buf, ULONG bufsize, ULONG *have_read);
	HRESULT HrReadLine(std::string * strBuffer, ULONG ulMaxBuffer = 65536);
	HRESULT HrWriteString(const std::string & strBuffer);
	HRESULT HrWriteLine(const char *szBuffer, int len = 0);
	HRESULT HrWriteLine(const std::string & strBuffer);
	_kc_hidden HRESULT HrReadBytes(char *buf, ULONG count);
	HRESULT HrReadBytes(std::string * strBuffer, ULONG ulByteCount);
	HRESULT HrReadAndDiscardBytes(ULONG ulByteCount);

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
	char peer_atxt[256+16];
	struct sockaddr_storage peer_sockaddr;
	socklen_t peer_salen = 0;

	_kc_hidden char *fd_gets(char *buf, int *len);
	_kc_hidden char *SSL_gets(char *buf, int *len);
};

/* helpers to open socket */
extern _kc_export int ec_listen_localsock(const char *path, int *fd);
extern _kc_export HRESULT HrListen(const char *bind, uint16_t port, int *fd);
/* accept data on connection */
extern _kc_export HRESULT HrAccept(int fd, ECChannel **ch);

extern _kc_export int zcp_bindtodevice(int fd, const char *iface);
extern int zcp_peeraddr_is_local(const struct sockaddr *, socklen_t);
extern _kc_export int zcp_peerfd_is_local(int);
extern _kc_export std::set<std::pair<std::string, uint16_t>> kc_parse_bindaddrs(const char *, uint16_t);

} /* namespace KC */

#endif
