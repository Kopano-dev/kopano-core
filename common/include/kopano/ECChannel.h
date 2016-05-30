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

#include <kopano/zcdefs.h>
#include <cstdio>
#include <iostream>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <string>

#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>

struct sockaddr;

// ECChannel is the communication channel with the other side. Initially, it is
// a simple way to read/write full lines of data. The reason why we specify
// a special 'HrWriteLine' instead of 'HrWrite' is that SSL encryption prefers
// writing all the data at once, instead of via multiple write() calls. Also, 
// this ensures that the ECChannel class is responsible for reading, writing
// and culling newline characters.

class ECChannel _zcp_final {
public:
	ECChannel(int socket);
	~ECChannel();

	HRESULT HrEnableTLS(ECLogger *const lpLogger);

	HRESULT HrGets(char *szBuffer, ULONG ulBufSize, ULONG *lpulRead);
	HRESULT HrReadLine(std::string * strBuffer, ULONG ulMaxBuffer = 65536);
	HRESULT HrWriteString(const char *szBuffer);
	HRESULT HrWriteString(const std::string & strBuffer);
	HRESULT HrWriteLine(const char *szBuffer, int len = 0);
	HRESULT HrWriteLine(const std::string & strBuffer);
	HRESULT HrReadBytes(char *szBuffer, ULONG ulByteCount);
	HRESULT HrReadBytes(std::string * strBuffer, ULONG ulByteCount);
	HRESULT HrReadAndDiscardBytes(ULONG ulByteCount);

	HRESULT HrSelect(int seconds);

	void SetIPAddress(const struct sockaddr *, size_t);
	const char *peer_addr(void) const { return peer_atxt; }
	int peer_is_local(void) const;
		
	bool UsingSsl(void) const { return lpSSL != NULL; }
	bool sslctx(void) const { return lpCTX != NULL; }

	static HRESULT HrSetCtx(ECConfig * lpConfig, ECLogger * lpLogger);
	static HRESULT HrFreeCtx();

private:
	int fd;
	SSL *lpSSL;
	static SSL_CTX *lpCTX;
	char peer_atxt[256+16];
	struct sockaddr_storage peer_sockaddr;
	socklen_t peer_salen;

	char *fd_gets(char *buf, int *lpulLen);
	char *SSL_gets(char *buf, int *lpulLen);
};

/* helpers to open socket */
HRESULT HrListen(ECLogger *lpLogger, const char *szPath, int *lpulListenSocket);
HRESULT HrListen(ECLogger *lpLogger, const char *szBind, uint16_t ulPort, int *lpulListenSocket);
/* accept data on connection */
HRESULT HrAccept(ECLogger *lpLogger, int ulListenFD, ECChannel **lppChannel);

extern "C" {

extern int zcp_bindtodevice(ECLogger *log, int fd, const char *iface);
extern int zcp_peeraddr_is_local(const struct sockaddr *, socklen_t);
extern int zcp_peerfd_is_local(int);

}

#endif
