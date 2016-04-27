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

#ifndef ECECSERVERENTRYPOINT_H
#define ECECSERVERENTRYPOINT_H

#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include "ECDatabase.h"
#include "pcutil.hpp"


#define KOPANO_SERVER_INIT_SERVER		0
#define KOPANO_SERVER_INIT_OFFLINE		1

#define SOAP_CONNECTION_TYPE_NAMED_PIPE(soap)	\
	((soap) && ((soap)->user) && ((((SOAPINFO*)(soap)->user)->ulConnectionType == CONNECTION_TYPE_NAMED_PIPE) || (((SOAPINFO*)(soap)->user)->ulConnectionType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY)))

#define SOAP_CONNECTION_TYPE(soap)	\
	(((SOAPINFO*)(soap)->user)->ulConnectionType)


extern ECRESULT kopano_init(ECConfig *lpConfig, ECLogger *lpAudit, bool bHostedKopano, bool bDistributedKopano);
ECRESULT kopano_exit();
void kopano_removeallsessions();

//Internal used functions
void AddDatabaseObject(ECDatabase* lpDatabase);

// server init function
extern ECRESULT kopano_initlibrary(const char *lpDatabaseDir, const char *lpConfigFile); // Init mysql library
extern ECRESULT kopano_unloadlibrary(void); // Unload mysql library

// Exported functions
KDLLAPI ECRESULT GetDatabaseObject(ECDatabase **lppDatabase);

// SOAP connection management
void kopano_new_soap_connection(CONNECTION_TYPE ulType, struct soap *soap);
void kopano_end_soap_connection(struct soap *soap);

void kopano_new_soap_listener(CONNECTION_TYPE ulType, struct soap *soap);
void kopano_end_soap_listener(struct soap *soap);
    
void kopano_disconnect_soap_connection(struct soap *soap);


#endif //ECECSERVERENTRYPOINT_H
