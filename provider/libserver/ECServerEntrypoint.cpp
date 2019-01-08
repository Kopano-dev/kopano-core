/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <memory>
#include <mutex>
#include <utility>
#include <pthread.h>
#include <kopano/ECLogger.h>
#include <kopano/hl.hpp>
#include "ECDatabase.h"
#include "ECSessionManager.h"
#include "StatsClient.h"
#include "ECDatabaseFactory.h"
#include "ECServerEntrypoint.h"
#include "ECS3Attachment.h"

namespace KC {

pthread_key_t plugin_key;

_kc_export std::unique_ptr<ECSessionManager> g_lpSessionManager;
static bool g_bInitLib = false;

static void plugin_destroy(void *lpParam)
{
	delete static_cast<UserPlugin *>(lpParam);
}

ECRESULT kopano_initlibrary(const char *lpDatabaseDir, const char *lpConfigFile)
{
	if (g_bInitLib)
		return KCERR_CALL_FAILED;
	pthread_key_create(&plugin_key, plugin_destroy); // same goes for the userDB-plugin

	// Init mutex for database object list
	auto er = ECDatabase::InitLibrary(lpDatabaseDir, lpConfigFile);

	//TODO: with an error remove all variables and g_bInitLib = false
	g_bInitLib = true;
	return er;
}

ECRESULT kopano_unloadlibrary(void)
{
	if (!g_bInitLib)
		return KCERR_NOT_INITIALIZED;

	// Delete the global key,
	//  on this position, there are zero or more threads exist.
	//  As you delete the keys, the function plugin_destroy will never called
	//
	pthread_key_delete(plugin_key);
	// remove mutex for database object list
	ECDatabase::UnloadLibrary();
	g_bInitLib = false;
	return erSuccess;
}

ECRESULT kopano_init(std::shared_ptr<ECConfig> cfg, std::shared_ptr<ECLogger> ad,
    std::shared_ptr<server_stats> sc, bool bHostedKopano, bool bDistributedKopano)
{
	if (!g_bInitLib)
		return KCERR_NOT_INITIALIZED;
	try {
		g_lpSessionManager = std::make_unique<ECSessionManager>(std::move(cfg), std::move(ad), std::move(sc), bHostedKopano, bDistributedKopano);
	} catch (const KMAPIError &e) {
		return e.code();
	}
	return g_lpSessionManager->LoadSettings();
}

ECRESULT kopano_exit()
{
	if (!g_bInitLib)
		return KCERR_NOT_INITIALIZED;
	// delete our plugin of the mainthread: requires ECPluginFactory to be alive, because that holds the dlopen() result
	plugin_destroy(pthread_getspecific(plugin_key));
	g_lpSessionManager.reset();
	return erSuccess;
}

/**
 * Called for each HTTP header in a request, handles the proxy header
 * and marks the connection as using the proxy if it is found. The value
 * of the header is ignored. The special value '*' for proxy_header is
 * not searched for here, but it is used in GetBestServerPath()
 *
 * We use the soap->user->fparsehdr to daisy chain the request to, which
 * is the original gSoap header parsing code. This is needed to decode
 * normal headers like content-type, etc.
 *
 * @param[in] soap Soap structure of the incoming call
 * @param[in] key Key part of the header (left of the :)
 * @param[in] vak Value part of the header (right of the :)
 * @return SOAP_OK or soap error
 */
static int kopano_fparsehdr(struct soap *soap, const char *key,
    const char *val)
{
	const char *szProxy = g_lpSessionManager->GetConfig()->GetSetting("proxy_header");
	if (strlen(szProxy) > 0 && strcasecmp(key, szProxy) == 0)
		soap_info(soap)->bProxy = true;
	return soap_info(soap)->fparsehdr(soap, key, val);
}

// Called just after a new soap connection is established
void kopano_new_soap_connection(CONNECTION_TYPE ulType, struct soap *soap)
{
	const char *szProxy = g_lpSessionManager->GetConfig()->GetSetting("proxy_header");
	auto lpInfo = new SOAPINFO;
	lpInfo->ulConnectionType = ulType;
	lpInfo->bProxy = false;
	soap->user = lpInfo;
	if (szProxy[0] == '\0')
		return;
	if (strcmp(szProxy, "*") == 0) {
		// Assume everything is proxied
		lpInfo->bProxy = true;
		return;
	}
	// Parse headers to determine if the connection is proxied
	lpInfo->fparsehdr = soap->fparsehdr; // daisy-chain the existing code
	soap->fparsehdr = kopano_fparsehdr;
}

void kopano_end_soap_connection(struct soap *soap)
{
	delete soap_info(soap);
}

void kopano_new_soap_listener(CONNECTION_TYPE ulType, struct soap *soap)
{
	auto lpInfo = new SOAPINFO;
	lpInfo->ulConnectionType = ulType;
	lpInfo->bProxy = false;
	soap->user = lpInfo;
}

void kopano_end_soap_listener(struct soap *soap)
{
	delete soap_info(soap);
}

// Export functions
ECRESULT GetDatabaseObject(std::shared_ptr<ECStatsCollector> sc, ECDatabase **lppDatabase)
{
	if (g_lpSessionManager == NULL)
		return KCERR_UNKNOWN;
	if (lppDatabase == NULL)
		return KCERR_INVALID_PARAMETER;
	return g_lpSessionManager->get_db_factory()->get_tls_db(lppDatabase);
}

} /* namespace */
