/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include <string>

namespace KC {
class Config;
class ECCacheManager;
struct ECSearchResultArray;
}

#include "ECGenericObjectTable.h"
#include "ECSearchClient.h"
#include "soapH.h"

namespace KC {
extern ECRESULT GetIndexerResults(ECDatabase *lpDatabase, Config *, ECCacheManager *lpCacheManager, GUID *guidServer, GUID *guidStore, ECListInt &lstFolders, struct restrictTable *lpRestrict, struct restrictTable **lppNewRestrict, std::list<unsigned int> &lstIndexerResults, std::string &suggestion);
}
