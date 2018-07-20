/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECSEARCHCLIENT_H
#define ECSEARCHCLIENT_H

#include <kopano/zcdefs.h>
#include <map>
#include <set>
#include <string>
#include <soapH.h>
#include <kopano/kcodes.h>
#include "ECChannelClient.h"

namespace KC {

struct SIndexedTerm {
    std::string strTerm;
    std::set<unsigned int> setFields;
};

typedef std::set<unsigned int> setindexprops_t;

class ECSearchClient _kc_final : public ECChannelClient {
public:
	ECSearchClient(const char *szIndexerPath, unsigned int ulTimeOut);
	ECRESULT GetProperties(setindexprops_t &mapProps);
	ECRESULT Query(GUID *lpServerGuid, GUID *lpStoreGUID, std::list<unsigned int> &lstFolders, std::list<SIndexedTerm> &lstSearches, std::list<unsigned int> &lstMatches, std::string &suggestion);
	ECRESULT SyncRun();
	
private:
	ECRESULT Scope(const std::string &strServer, const std::string &strStore, const std::list<unsigned int> &ulFolders);
	ECRESULT Find(const std::set<unsigned int> &setFields, const std::string &strTerm);
	ECRESULT Query(std::list<unsigned int> &lstMatches);
	ECRESULT Suggest(std::string &suggestion);
};

} /* namespace */

#endif /* ECSEARCHCLIENT_H */
