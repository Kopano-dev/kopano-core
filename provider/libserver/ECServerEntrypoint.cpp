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

pthread_key_t database_key;
pthread_key_t plugin_key;

_kc_export ECSessionManager *g_lpSessionManager;
static std::set<ECDatabase *> g_lpDBObjectList;
static std::mutex g_hMutexDBObjectList;
static bool g_bInitLib = false;

void AddDatabaseObject(ECDatabase* lpDatabase)
{
	scoped_lock lk(g_hMutexDBObjectList);
	g_lpDBObjectList.emplace(lpDatabase);
}

static void database_destroy(void *lpParam)
{
	auto lpDatabase = static_cast<ECDatabase *>(lpParam);
	ulock_normal l_obj(g_hMutexDBObjectList);

	g_lpDBObjectList.erase(std::set<ECDatabase*>::key_type(lpDatabase));
	l_obj.unlock();
	lpDatabase->ThreadEnd();
	delete lpDatabase;
}

static void plugin_destroy(void *lpParam)
{
	delete static_cast<UserPlugin *>(lpParam);
}

ECRESULT kopano_initlibrary(const char *lpDatabaseDir, const char *lpConfigFile)
{
	if (g_bInitLib)
		return KCERR_CALL_FAILED;

	// This is a global key that we can reference from each thread with a different value. The
	// database_destroy routine is called when the thread terminates.
	pthread_key_create(&database_key, database_destroy);
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
	//  As you delete the keys, the function database_destroy and plugin_destroy will never called
	//
	pthread_key_delete(database_key);
	pthread_key_delete(plugin_key);

	// Remove all exist database objects
	ulock_normal l_obj(g_hMutexDBObjectList);
	for (auto o = g_lpDBObjectList.cbegin(); o != g_lpDBObjectList.cend();
	     o = g_lpDBObjectList.erase(o))
		delete *o;
	l_obj.unlock();

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
		g_lpSessionManager = new ECSessionManager(std::move(cfg), std::move(ad), std::move(sc), bHostedKopano, bDistributedKopano);
	} catch (const KMAPIError &e) {
		return e.code();
	}
	return g_lpSessionManager->LoadSettings();
}

void kopano_removeallsessions()
{
	if (g_lpSessionManager != nullptr)
		g_lpSessionManager->RemoveAllSessions();
}

ECRESULT kopano_exit()
{
	if (!g_bInitLib)
		return KCERR_NOT_INITIALIZED;
	// delete our plugin of the mainthread: requires ECPluginFactory to be alive, because that holds the dlopen() result
	plugin_destroy(pthread_getspecific(plugin_key));

	delete g_lpSessionManager;
	g_lpSessionManager = NULL;

	// Close all database connections
	scoped_lock l_obj(g_hMutexDBObjectList);
	for (auto dbobjp : g_lpDBObjectList)
		dbobjp->Close();
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

// Called just before the socket is reset, with the server-side socket still
// open
void kopano_disconnect_soap_connection(struct soap *soap)
{
	if (SOAP_CONNECTION_TYPE_NAMED_PIPE(soap))
		// Mark the persistent session as exited
		g_lpSessionManager->RemoveSessionPersistentConnection((unsigned int)soap->socket);
}

// Export functions
ECRESULT GetDatabaseObject(std::shared_ptr<ECStatsCollector> sc, ECDatabase **lppDatabase)
{
	if (g_lpSessionManager == NULL)
		return KCERR_UNKNOWN;
	if (lppDatabase == NULL)
		return KCERR_INVALID_PARAMETER;
	ECDatabaseFactory db(g_lpSessionManager->GetConfig(), std::move(sc));
	return GetThreadLocalDatabase(&db, lppDatabase);
}

} /* namespace */
