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

#ifndef ECCHANNELCLIENT_H
#define ECCHANNELCLIENT_H

#include <string>
#include <vector>

#include <kopano/platform.h>
#include <kopano/kcodes.h>

class ECChannel;

class ECChannelClient {
public:
	ECChannelClient(const char *szPath, const char *szTokenizer);
	~ECChannelClient();

	ECRESULT DoCmd(const std::string &strCommand, std::vector<std::string> &lstResponse);

protected:
	ECRESULT Connect();
	ECRESULT ConnectSocket();
	ECRESULT ConnectHttp();

	unsigned int m_ulTimeout; ///< Response timeout in second

private:
	std::string m_strTokenizer;
	std::string m_strPath;
	bool m_bSocket;
	uint16_t m_ulPort;
	ECChannel *m_lpChannel;
};

#endif /* ECCHANNELCLIENT_H */
