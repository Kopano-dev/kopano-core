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
#include <new>

#include <mapidefs.h>
#include <mapiutil.h>
#include <mapix.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <kopano/ECChannel.h>
#include <kopano/ECDefs.h>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>

#include "ECChannelClient.h"

namespace KC {

ECChannelClient::ECChannelClient(const char *szPath, const char *tk) :
	m_strTokenizer(tk), m_strPath(GetServerNameFromPath(szPath))
{
	if (strncmp(szPath, "file", 4) == 0 || szPath[0] == PATH_SEPARATOR) {
		m_bSocket = true;
		m_ulPort = 0;
		return;
	}
	m_bSocket = false;
	m_ulPort = atoi(GetServerPortFromPath(szPath).c_str());
}

ECRESULT ECChannelClient::DoCmd(const std::string &strCommand, std::vector<std::string> &lstResponse)
{
	std::string strResponse;
	auto er = Connect();
	if (er != erSuccess)
		return er;

	er = m_lpChannel->HrWriteLine(strCommand);
	if (er != erSuccess)
		return er;

	er = m_lpChannel->HrSelect(m_ulTimeout);
	if (er != erSuccess)
		return er;

	// @todo, should be able to read more than 4MB of results
	er = m_lpChannel->HrReadLine(strResponse, 4*1024*1024);
	if (er != erSuccess)
		return er;

	lstResponse = tokenize(strResponse, m_strTokenizer);

	if (!lstResponse.empty() && lstResponse.front() == "OK")
		lstResponse.erase(lstResponse.begin());
	else
		return KCERR_CALL_FAILED;
	return erSuccess;
}

ECRESULT ECChannelClient::Connect()
{
	if (m_lpChannel)
		return erSuccess;

	return m_bSocket ? ConnectSocket() : ConnectHttp();
}

ECRESULT ECChannelClient::ConnectSocket()
{
	struct sockaddr_un saddr;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sun_family = AF_UNIX;
	if (m_strPath.size() >= sizeof(saddr.sun_path)) {
		ec_log_warn("%s: path %s too long", __PRETTY_FUNCTION__,
			m_strPath.c_str());
		return KCERR_INVALID_PARAMETER;
	}
	kc_strlcpy(saddr.sun_path, m_strPath.c_str(), sizeof(saddr.sun_path));

	auto fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return KCERR_INVALID_PARAMETER;

	if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		if (fd != -1)
			close(fd);
		return KCERR_NETWORK_ERROR;
	}

	m_lpChannel.reset(new(std::nothrow) ECChannel(fd));
	if (!m_lpChannel) {
		if (fd != -1)
			close(fd);
		return KCERR_NOT_ENOUGH_MEMORY;
	}

	return erSuccess;
}

ECRESULT ECChannelClient::ConnectHttp()
{
	int fd = -1;
	struct addrinfo *sock_res, sock_hints;
	const struct addrinfo *sock_addr;
	char port_string[sizeof("65536")];

	snprintf(port_string, sizeof(port_string), "%u", m_ulPort);
	memset(&sock_hints, 0, sizeof(sock_hints));
	sock_hints.ai_socktype = SOCK_STREAM;
	auto ret = getaddrinfo(m_strPath.c_str(), port_string, &sock_hints, &sock_res);
	if (ret != 0)
		return KCERR_NETWORK_ERROR;

	auto free_sock_res = make_scope_success([&]() {
		if (sock_res != nullptr)
			freeaddrinfo(sock_res);
	});

	for (sock_addr = sock_res; sock_addr != NULL;
	     sock_addr = sock_addr->ai_next)
	{
		fd = socket(sock_addr->ai_family, sock_addr->ai_socktype,
		     sock_addr->ai_protocol);
		if (fd < 0)
			/* Socket type could not be created */
			continue;

		if (connect(fd, sock_addr->ai_addr,
		    sock_addr->ai_addrlen) < 0) {
			/* No route */
			int saved_errno = errno;
			close(fd);
			fd = -1;
			errno = saved_errno;
			continue;
		}
		/* Good connected socket, use it */
		break;
	}
	if (fd < 0)
		return KCERR_NETWORK_ERROR;

	m_lpChannel.reset(new(std::nothrow) ECChannel(fd));
	if (!m_lpChannel) {
		if (fd != -1)
			close(fd);
		return KCERR_NOT_ENOUGH_MEMORY;
	}

	return erSuccess;
}

} /* namespace */
