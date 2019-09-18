/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSOAPSERVERCONNECTION_H
#define ECSOAPSERVERCONNECTION_H

#include <memory>
#include <kopano/kcodes.h>
#include "ECThreadManager.h"
#include "soapH.h"
#include <kopano/ECConfig.h>

namespace KC {
struct ec_socket;
}
using KC::ECRESULT;
extern int kc_ssl_options(struct soap *, const char *proto, const char *ciphers, const char *prefciphers, const char *curves);

class ECSoapServerConnection final {
public:
	ECSoapServerConnection(std::shared_ptr<KC::ECConfig>);
	ECRESULT ListenTCP(struct KC::ec_socket &);
	ECRESULT ListenSSL(struct KC::ec_socket &, const char *keyfile, const char *keypass, const char *cafile, const char *capath);
	ECRESULT ListenPipe(struct KC::ec_socket &, bool priority = false);
	ECRESULT MainLoop();
	// These can be called asynchronously from MainLoop();
	void NotifyDone(struct soap *);
	void ShutDown();
	ECRESULT DoHUP();
	void GetStats(unsigned int *qlen, KC::time_duration *age, unsigned int *thrtotal, unsigned int *thridle);

private:
    // Main thread handler
	std::unique_ptr<ECDispatcher> m_lpDispatcher;
	std::shared_ptr<KC::ECConfig> m_lpConfig;
};

#endif // #ifndef ECSOAPSERVERCONNECTION_H
