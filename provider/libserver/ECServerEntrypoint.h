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

#include <kopano/zcdefs.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include "ECDatabase.h"
#include "pcutil.hpp"

#define KOPANO_SERVER_INIT_SERVER		0
#define KOPANO_SERVER_INIT_OFFLINE		1

#define SOAP_CONNECTION_TYPE(s) (soap_info(s)->ulConnectionType)

struct soap;

namespace KC {

static inline bool SOAP_CONNECTION_TYPE_NAMED_PIPE(struct soap *soap)
{
	if (soap == nullptr || soap->user == nullptr)
		return false;
	auto si = soap_info(soap);
	return si->ulConnectionType == CONNECTION_TYPE_NAMED_PIPE ||
	       si->ulConnectionType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY;
}

extern _kc_export ECRESULT kopano_init(ECConfig *, ECLogger *audit, bool hosted_kopano, bool distr_kopano);
extern _kc_export ECRESULT kopano_exit(void);
extern _kc_export void kopano_removeallsessions(void);

//Internal used functions
void AddDatabaseObject(ECDatabase* lpDatabase);

// server init function
extern _kc_export ECRESULT kopano_initlibrary(const char *dbdir, const char *config_file); // Init mysql library
extern _kc_export ECRESULT kopano_unloadlibrary(void); // Unload mysql library

// Exported functions
extern _kc_export ECRESULT GetDatabaseObject(ECDatabase **);

// SOAP connection management
extern _kc_export void kopano_new_soap_connection(CONNECTION_TYPE, struct soap *);
extern _kc_export void kopano_end_soap_connection(struct soap *);
extern _kc_export void kopano_new_soap_listener(CONNECTION_TYPE, struct soap *);
extern _kc_export void kopano_end_soap_listener(struct soap *);
void kopano_disconnect_soap_connection(struct soap *soap);

} /* namespace */

#endif //ECECSERVERENTRYPOINT_H
