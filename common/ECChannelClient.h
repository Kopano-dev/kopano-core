/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECCHANNELCLIENT_H
#define ECCHANNELCLIENT_H

#include <memory>
#include <string>
#include <vector>
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <kopano/kcodes.h>
#include <kopano/ECChannel.h>

namespace KC {

class _kc_export ECChannelClient {
public:
	ECChannelClient(const char *szPath, const char *szTokenizer);
	ECRESULT DoCmd(const std::string &strCommand, std::vector<std::string> &lstResponse);

protected:
	ECRESULT Connect();
	_kc_hidden ECRESULT ConnectSocket(void);
	_kc_hidden ECRESULT ConnectHttp(void);

	unsigned int m_ulTimeout = 5; ///< Response timeout in second

private:
	std::string m_strTokenizer, m_strPath;
	bool m_bSocket;
	uint16_t m_ulPort;
	std::unique_ptr<ECChannel> m_lpChannel;
};

} /* namespace */

#endif /* ECCHANNELCLIENT_H */
