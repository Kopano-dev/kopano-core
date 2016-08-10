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

#include <kopano/platform.h>

#include <kopano/ECLogger.h>
#include "ECSessionManager.h"
#include "ECStatsCollector.h"

#include "ECDatabaseFactory.h"
#include "ECDatabaseMySQL.h"
#include "ECServerEntrypoint.h"

#include "ECSessionManagerOffline.h"
#include "ECS3Attachment.h"

pthread_key_t database_key;
pthread_key_t plugin_key;

ECSessionManager*		g_lpSessionManager = NULL;
ECStatsCollector*		g_lpStatsCollector = NULL;
static std::set<ECDatabase *> g_lpDBObjectList;
static pthread_mutex_t g_hMutexDBObjectList;
static bool g_bInitLib = false;

void AddDatabaseObject(ECDatabase* lpDatabase)
{
	pthread_mutex_lock(&g_hMutexDBObjectList);

	g_lpDBObjectList.insert(std::set<ECDatabase*>::value_type(lpDatabase));

	pthread_mutex_unlock(&g_hMutexDBObjectList);
}

static void database_destroy(void *lpParam)
{
	ECDatabase *lpDatabase = (ECDatabase *)lpParam;

	pthread_mutex_lock(&g_hMutexDBObjectList);

	g_lpDBObjectList.erase(std::set<ECDatabase*>::key_type(lpDatabase));

	pthread_mutex_unlock(&g_hMutexDBObjectList);

	lpDatabase->ThreadEnd();
	delete lpDatabase;
}

static void plugin_destroy(void *lpParam)
{
	UserPlugin *lpPlugin = (UserPlugin *)lpParam;

	delete lpPlugin;
}

ECRESULT kopano_initlibrary(const char *lpDatabaseDir, const char *lpConfigFile)
{
	ECRESULT er;

	if (g_bInitLib == true)
		return KCERR_CALL_FAILED;

	// This is a global key that we can reference from each thread with a different value. The
	// database_destroy routine is called when the thread terminates.

	pthread_key_create(&database_key, database_destroy);
	pthread_key_create(&plugin_key, plugin_destroy); // same goes for the userDB-plugin

	// Init mutex for database object list
	pthread_mutex_init(&g_hMutexDBObjectList, NULL);
	er = ECDatabaseMySQL::InitLibrary(lpDatabaseDir, lpConfigFile);
	g_lpStatsCollector = new ECStatsCollector();
	
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
	pthread_mutex_lock(&g_hMutexDBObjectList);

	auto iterDBObject = g_lpDBObjectList.cbegin();
	while (iterDBObject != g_lpDBObjectList.cend())
	{
		auto iNext = iterDBObject;
		++iNext;
		delete (*iterDBObject);

		g_lpDBObjectList.erase(iterDBObject);

		iterDBObject = iNext;
	}

	pthread_mutex_unlock(&g_hMutexDBObjectList);

	// remove mutex for database object list
	pthread_mutex_destroy(&g_hMutexDBObjectList);
	ECDatabaseMySQL::UnloadLibrary();
	g_bInitLib = false;
	return erSuccess;
}

ECRESULT kopano_init(ECConfig *lpConfig, ECLogger *lpAudit, bool bHostedKopano, bool bDistributedKopano)
{
	ECRESULT er;

	if (!g_bInitLib)
		return KCERR_NOT_INITIALIZED;

	g_lpSessionManager = new ECSessionManager(lpConfig, lpAudit, bHostedKopano, bDistributedKopano);
	er = g_lpSessionManager->LoadSettings();
	if(er != erSuccess)
		return er;
	er = g_lpSessionManager->CheckUserLicense();
	if (er != erSuccess)
		return er;
#ifdef HAVE_LIBS3_H
        if (strcmp(lpConfig->GetSetting("attachment_storage"), "s3") == 0)
                ECS3Attachment::StaticInit(lpConfig);
#endif
	return erSuccess;
}

void kopano_removeallsessions()
{
	if(g_lpSessionManager) {
		g_lpSessionManager->RemoveAllSessions();
	}
}

ECRESULT kopano_exit()
{
	if (!g_bInitLib)
		return KCERR_NOT_INITIALIZED;

#ifdef HAVE_LIBS3_H
        if (g_lpSessionManager && strcmp(g_lpSessionManager->GetConfig()->GetSetting("attachment_storage"), "s3") == 0)
                ECS3Attachment::StaticDeinit();
#endif

	// delete our plugin of the mainthread: requires ECPluginFactory to be alive, because that holds the dlopen() result
	plugin_destroy(pthread_getspecific(plugin_key));

	delete g_lpSessionManager;
	g_lpSessionManager = NULL;

	delete g_lpStatsCollector;
	g_lpStatsCollector = NULL;

	// Close all database connections
	pthread_mutex_lock(&g_hMutexDBObjectList);

	for (auto dbobjp : g_lpDBObjectList)
		dbobjp->Close();
	pthread_mutex_unlock(&g_hMutexDBObjectList);
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
	if(strlen(szProxy) > 0 && strcasecmp(key, szProxy) == 0) {
		((SOAPINFO *)soap->user)->bProxy = true;
	}
	
	return ((SOAPINFO *)soap->user)->fparsehdr(soap, key, val);
}

// Called just after a new soap connection is established
void kopano_new_soap_connection(CONNECTION_TYPE ulType, struct soap *soap)
{
	const char *szProxy = g_lpSessionManager->GetConfig()->GetSetting("proxy_header");
	SOAPINFO *lpInfo = new SOAPINFO;
	lpInfo->ulConnectionType = ulType;
	lpInfo->bProxy = false;
	soap->user = (void *)lpInfo;
	
	if (szProxy[0]) {
		if(strcmp(szProxy, "*") == 0) {
			// Assume everything is proxied
			lpInfo->bProxy = true; 
		} else {
			// Parse headers to determine if the connection is proxied
			lpInfo->fparsehdr = soap->fparsehdr; // daisy-chain the existing code
			soap->fparsehdr = kopano_fparsehdr;
		}
	}
}

void kopano_end_soap_connection(struct soap *soap)
{
	delete (SOAPINFO *)soap->user;
}

void kopano_new_soap_listener(CONNECTION_TYPE ulType, struct soap *soap)
{
	SOAPINFO *lpInfo = new SOAPINFO;
	lpInfo->ulConnectionType = ulType;
	lpInfo->bProxy = false;
	soap->user = (void *)lpInfo;
}

void kopano_end_soap_listener(struct soap *soap)
{
	delete (SOAPINFO *)soap->user;
}

// Called just before the socket is reset, with the server-side socket still
// open
void kopano_disconnect_soap_connection(struct soap *soap)
{
	if(SOAP_CONNECTION_TYPE_NAMED_PIPE(soap)) {
		// Mark the persistent session as exited
		g_lpSessionManager->RemoveSessionPersistentConnection((unsigned int)soap->socket);
	}
}

// Export functions
ECRESULT GetDatabaseObject(ECDatabase **lppDatabase)
{
	if(g_lpSessionManager == NULL) {
		return KCERR_UNKNOWN;
	}

	if(lppDatabase == NULL) {
		return KCERR_INVALID_PARAMETER;
	}

	ECDatabaseFactory db(g_lpSessionManager->GetConfig());
	return GetThreadLocalDatabase(&db, lppDatabase);
}
