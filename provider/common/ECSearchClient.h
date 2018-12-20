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
#include "ECtools/indexer.hpp"
#include "ECChannelClient.h"

namespace KC {

struct SIndexedTerm {
    std::string strTerm;
    std::set<unsigned int> setFields;
};

typedef std::set<unsigned int> setindexprops_t;

class ECSearchClient {
public:
	virtual ~ECSearchClient() {}
	ECRESULT GetProperties(setindexprops_t &mapProps);
	ECRESULT Query(const GUID *server_guid, const GUID *store_guid, const std::list<unsigned int> &folders, const std::list<SIndexedTerm> &searches, std::list<unsigned int> &matches, std::string &suggestion);
	ECRESULT SyncRun();
	
private:
	virtual ECRESULT DoCmd(const std::string &command, std::vector<std::string> &response) = 0;
	virtual ECRESULT Connect() { return erSuccess; }
	ECRESULT Scope(const std::string &strServer, const std::string &strStore, const std::list<unsigned int> &ulFolders);
	ECRESULT Find(const std::set<unsigned int> &setFields, const std::string &strTerm);
	ECRESULT Query(std::list<unsigned int> &lstMatches);
	ECRESULT Suggest(std::string &suggestion);
};

class ECSearchClientMM final : public ECSearchClient {
	public:
	ECSearchClientMM();

	private:
	virtual ECRESULT DoCmd(const std::string &c, std::vector<std::string> &r);
	virtual ECRESULT Connect();

	std::unique_ptr<IIndexer> m_indexer;
	IIndexer::client_state m_state;
};

class ECSearchClientNET final :
    public ECSearchClient, private ECChannelClient {
	public:
	ECSearchClientNET(const char *szIndexerPath, unsigned int ulTimeOut);
	private:
	virtual ECRESULT DoCmd(const std::string &c, std::vector<std::string> &r) { return ECChannelClient::DoCmd(c, r); }
	virtual ECRESULT Connect() { return ECChannelClient::Connect(); }
};

} /* namespace */

#endif /* ECSEARCHCLIENT_H */
