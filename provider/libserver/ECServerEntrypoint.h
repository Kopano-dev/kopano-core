/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECECSERVERENTRYPOINT_H
#define ECECSERVERENTRYPOINT_H

#include <memory>
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

class server_stats;

static inline bool SOAP_CONNECTION_TYPE_NAMED_PIPE(struct soap *soap)
{
	if (soap == nullptr || soap->user == nullptr)
		return false;
	auto si = soap_info(soap);
	return si->ulConnectionType == CONNECTION_TYPE_NAMED_PIPE ||
	       si->ulConnectionType == CONNECTION_TYPE_NAMED_PIPE_PRIORITY;
}

extern _kc_export ECRESULT kopano_init(std::shared_ptr<ECConfig>, std::shared_ptr<ECLogger> audit, std::shared_ptr<server_stats>, bool hosted_kopano, bool distr_kopano);
extern _kc_export ECRESULT kopano_exit(void);

//Internal used functions
void AddDatabaseObject(ECDatabase* lpDatabase);

// server init function
extern _kc_export ECRESULT kopano_initlibrary(const char *dbdir, const char *config_file); // Init mysql library
extern _kc_export ECRESULT kopano_unloadlibrary(void); // Unload mysql library

// Exported functions
extern _kc_export ECRESULT GetDatabaseObject(std::shared_ptr<ECStatsCollector>, ECDatabase **);

// SOAP connection management
extern _kc_export void kopano_new_soap_connection(CONNECTION_TYPE, struct soap *);
extern _kc_export void kopano_end_soap_connection(struct soap *);
extern _kc_export void kopano_new_soap_listener(CONNECTION_TYPE, struct soap *);
extern _kc_export void kopano_end_soap_listener(struct soap *);
void kopano_disconnect_soap_connection(struct soap *soap);

} /* namespace */

#endif //ECECSERVERENTRYPOINT_H
