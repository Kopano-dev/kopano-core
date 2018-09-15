/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSOAPSERVERCONNECTION_H
#define ECSOAPSERVERCONNECTION_H

#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include "ECThreadManager.h"
#include "soapH.h"
#include <kopano/ECConfig.h>

using KC::ECRESULT;
extern int kc_ssl_options(struct soap *, char *protos, const char *ciphers, const char *prefciphers, const char *curves);

class ECSoapServerConnection _kc_final {
public:
	ECSoapServerConnection(std::shared_ptr<KC::ECConfig>);
	~ECSoapServerConnection();
	ECRESULT ListenTCP(const char *host, int port);
	ECRESULT ListenSSL(const char *host, int port, const char *keyfile, const char *keypass, const char *cafile, const char *capath);
	ECRESULT ListenPipe(const char* lpPipeName, bool bPriority = false);
	ECRESULT MainLoop();
	// These can be called asynchronously from MainLoop();
	void NotifyDone(struct soap *);
	void ShutDown();
	ECRESULT DoHUP();
	void GetStats(unsigned int *qlen, double *age, unsigned int *thrtotal, unsigned int *thridle);

private:
    // Main thread handler
    ECDispatcher *m_lpDispatcher;
	std::shared_ptr<KC::ECConfig> m_lpConfig;
};

#endif // #ifndef ECSOAPSERVERCONNECTION_H
