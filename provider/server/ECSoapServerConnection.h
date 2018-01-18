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

#ifndef ECSOAPSERVERCONNECTION_H
#define ECSOAPSERVERCONNECTION_H

#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include "ECThreadManager.h"
#include "soapH.h"
#include <kopano/ECConfig.h>

using KC::ECRESULT;
extern int kc_ssl_options(struct soap *, char *protos, const char *ciphers, const char *prefciphers);

class ECSoapServerConnection _kc_final {
public:
	ECSoapServerConnection(KC::ECConfig *);
	~ECSoapServerConnection();
	ECRESULT ListenTCP(const char *host, int port);
	ECRESULT ListenSSL(const char *host, int port, const char *keyfile, const char *keypass, const char *cafile, const char *capath);
	ECRESULT ListenPipe(const char* lpPipeName, bool bPriority = false);

	ECRESULT MainLoop();
	
	// These can be called asynchronously from MainLoop();
	ECRESULT NotifyDone(struct soap *soap);
	ECRESULT ShutDown();
	ECRESULT DoHUP();
	ECRESULT GetStats(unsigned int *lpulQueueLength, double *lpdblAge, unsigned int *lpulThreadCount, unsigned int *lpulIdleThreads);

	static SOAP_SOCKET CreatePipeSocketCallback(void *lpParam);

private:
    // Main thread handler
    ECDispatcher *m_lpDispatcher;
	KC::ECConfig *m_lpConfig;
	std::string	m_strPipeName;
};

#endif // #ifndef ECSOAPSERVERCONNECTION_H
