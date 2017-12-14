/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include "ECTestProtocol.h"
#include "SOAPUtils.h"
#include "ECSessionManager.h"
#include "ECSession.h"
#include "ECTPropsPurge.h"
#include "ECSearchClient.h"

struct soap;

namespace KC {

ECRESULT TestPerform(ECSession *lpSession,
    const char *szCommand, unsigned int ulArgs, char **args)
{
    ECRESULT er = erSuccess;

	if (strcasecmp(szCommand, "indexer_syncrun") == 0) {
		if (parseBool(g_lpSessionManager->GetConfig()->GetSetting("search_enabled")))
			er = ECSearchClientNET(
				g_lpSessionManager->GetConfig()->GetSetting("search_socket"),
				60 * 10 /* 10 minutes should be enough for everyone */
			).SyncRun();
	} else if (strcasecmp(szCommand, "run_searchfolders") == 0) {
		lpSession->GetSessionManager()->GetSearchFolders()->FlushAndWait();
	} else if (strcasecmp(szCommand, "kill_sessions") == 0) {
	    ECSESSIONID id = lpSession->GetSessionId();

	    // Remove all sessions except our own
	    er = lpSession->GetSessionManager()->CancelAllSessions(id);
    } else if(strcasecmp(szCommand, "sleep") == 0) {
        if(ulArgs == 1 && args[0])
            Sleep(atoui(args[0]) * 1000);
    }
    return er;
}

ECRESULT TestSet(const char *szVarName, const char *szValue)
{
    if(strcasecmp(szVarName, "cell_cache_disabled") == 0) {
        if(atoi(szValue) > 0)
            g_lpSessionManager->GetCacheManager()->DisableCellCache();
        else
            g_lpSessionManager->GetCacheManager()->EnableCellCache();
    } else if (strcasecmp(szVarName, "search_enabled") == 0) {
		// Since there's no object that represents the indexer, it's probably cleanest to
		// update the configuration.
		if (atoi(szValue) > 0)
			g_lpSessionManager->GetConfig()->AddSetting("search_enabled", "yes", 0);
		else
			g_lpSessionManager->GetConfig()->AddSetting("search_enabled", "no", 0);
	}
	return erSuccess;
}

ECRESULT TestGet(struct soap *soap, const char *szVarName, char **szValue)
{
	if (strcasecmp(szVarName, "ping") == 0)
		*szValue = soap_strdup(soap, "pong");
	else
		return KCERR_NOT_FOUND;
	return erSuccess;
}

} /* namespace */
