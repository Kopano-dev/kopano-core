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
#include "ECTestProtocol.h"
#include "SOAPUtils.h"
#include "ECSessionManager.h"
#include "ECSession.h"
#include "ECTPropsPurge.h"
#include "ECSearchClient.h"

struct soap;

namespace KC {

extern ECSessionManager*    g_lpSessionManager;

ECRESULT TestPerform(struct soap *soap, ECSession *lpSession,
    const char *szCommand, unsigned int ulArgs, char **args)
{
    ECRESULT er = erSuccess;

    if(strcasecmp(szCommand, "purge_deferred") == 0) {
        while (1) {
            unsigned int ulFolderId = 0;
			ECDatabase *lpDatabase = NULL;

			er = lpSession->GetDatabase(&lpDatabase);
            if(er != erSuccess)
				return er;
			
            er = ECTPropsPurge::GetLargestFolderId(lpDatabase, &ulFolderId);
            if(er != erSuccess) {
                er = erSuccess;
                break;
            }
            
            er = ECTPropsPurge::PurgeDeferredTableUpdates(lpDatabase, ulFolderId);
            if(er != erSuccess)
                return er;
        }
    } else if (strcasecmp(szCommand, "indexer_syncrun") == 0) {
		if (parseBool(g_lpSessionManager->GetConfig()->GetSetting("search_enabled")))
			er = ECSearchClient(
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

ECRESULT TestSet(struct soap *soap, ECSession *lpSession,
    const char *szVarName, const char *szValue)
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

ECRESULT TestGet(struct soap *soap, ECSession *lpSession,
    const char *szVarName, char **szValue)
{
	if (strcasecmp(szVarName, "ping") == 0)
		*szValue = s_strcpy(soap, "pong");
	else
		return KCERR_NOT_FOUND;
	return erSuccess;
}

} /* namespace */
