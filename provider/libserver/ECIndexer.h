/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECINDEXER_H
#define ECINDEXER_H

namespace KC {
class ECConfig;
class ECCacheManager;
struct ECSearchResultArray;
}

#include "ECGenericObjectTable.h"
#include "ECSearchClient.h"
#include "soapH.h"

namespace KC {
extern ECRESULT GetIndexerResults(ECDatabase *lpDatabase, ECConfig *lpConfig, ECCacheManager *lpCacheManager, GUID *guidServer, GUID *guidStore, ECListInt &lstFolders, struct restrictTable *lpRestrict, struct restrictTable **lppNewRestrict, std::list<unsigned int> &lstIndexerResults, std::string &suggestion);
}

#endif
