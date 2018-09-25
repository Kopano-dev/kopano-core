/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <kopano/ECChannel.h>
#include <kopano/ECDefs.h>
#include <kopano/stringutil.h>
#include "ECSearchClient.h"

namespace KC {

ECSearchClient::ECSearchClient(const char *szIndexerPath, unsigned int ulTimeOut)
	: ECChannelClient(szIndexerPath, ":;")
{
	m_ulTimeout = ulTimeOut;
}

ECRESULT ECSearchClient::GetProperties(setindexprops_t &setProps)
{
	std::vector<std::string> lstResponse;
	auto er = DoCmd("PROPS", lstResponse);
	if (er != erSuccess)
		return er;
	setProps.clear();
	if (lstResponse.empty())
		return erSuccess; // No properties
	auto lstProps = tokenize(lstResponse[0], " ");
	for (const auto &s : lstProps)
		setProps.emplace(atoui(s.c_str()));
	return erSuccess;
}

/**
 * Output SCOPE command
 *
 * Specifies the scope of the search.
 *
 * @param strServer[in] Server GUID to search in
 * @param strStore[in] Store GUID to search in
 * @param lstFolders[in] List of folders to search in. As a special case, no folders means 'all folders'
 * @return result
 */
ECRESULT ECSearchClient::Scope(const std::string &strServer,
    const std::string &strStore, const std::list<unsigned int> &lstFolders)
{
	std::vector<std::string> lstResponse;
	auto er = Connect();
	if (er != erSuccess)
		return er;
	auto strScope = "SCOPE " + strServer + " " + strStore + " " + kc_join(lstFolders, " ", stringify);
	er = DoCmd(strScope, lstResponse);
	if (er != erSuccess)
		return er;
	if (!lstResponse.empty())
		return KCERR_BAD_VALUE;
	return erSuccess;
}

/**
 * Output FIND command
 *
 * The FIND command specifies which term to look for in which fields. When multiple
 * FIND commands are issued, items must match ALL of the terms.
 *
 * @param setFields[in] Fields to match (may match any of these fields)
 * @param strTerm[in] Term to match (utf-8 encoded)
 * @return result
 */
ECRESULT ECSearchClient::Find(const std::set<unsigned int> &setFields,
    const std::string &strTerm)
{
	std::vector<std::string> lstResponse;
	auto strFind = "FIND " + kc_join(setFields, " ", stringify) + ":" + strTerm;
	auto er = DoCmd(strFind, lstResponse);
	if (er != erSuccess)
		return er;
	if (!lstResponse.empty())
		return KCERR_BAD_VALUE;
	return erSuccess;
}

/**
 * Run the search query
 *
 * @param lstMatches[out] List of matches as hierarchy IDs
 * @return result
 */
ECRESULT ECSearchClient::Query(std::list<unsigned int> &lstMatches)
{
	std::vector<std::string> lstResponse;
	lstMatches.clear();
	auto er = DoCmd("QUERY", lstResponse);
	if (er != erSuccess)
		return er;
	if (lstResponse.empty())
		return erSuccess; /* no matches */
	for (const auto &i : tokenize(lstResponse[0], " "))
		lstMatches.emplace_back(atoui(i.c_str()));
	return erSuccess;
}

/**
 * Do a full search query
 *
 * This function actually executes a number of commands:
 *
 * SCOPE <serverid> <storeid> <folder1> ... <folderN>
 * FIND <field1> ... <fieldN> : <term>
 * QUERY
 *
 * @param lpServerGuid[in] Server GUID to search in
 * @param lpStoreGuid[in] Store GUID to search in
 * @param lstFolders[in] List of folders to search in
 * @param lstSearches[in] List of searches that items should match (AND)
 * @param lstMatches[out] Output of matching items
 * @return result
 */
 
ECRESULT ECSearchClient::Query(GUID *lpServerGuid, GUID *lpStoreGuid, std::list<unsigned int>& lstFolders, std::list<SIndexedTerm> &lstSearches, std::list<unsigned int> &lstMatches, std::string &suggestion)
{
	auto strServer = bin2hex(sizeof(GUID), lpServerGuid);
	auto strStore = bin2hex(sizeof(GUID), lpStoreGuid);
	auto er = Scope(strServer, strStore, lstFolders);
	if (er != erSuccess)
		return er;
	for (const auto &i : lstSearches)
		Find(i.setFields, i.strTerm);
	er = Suggest(suggestion);
	if (er != erSuccess)
		return er;
	return Query(lstMatches);
}

ECRESULT ECSearchClient::Suggest(std::string &suggestion)
{
	std::vector<std::string> resp;
	ECRESULT er = DoCmd("SUGGEST", resp);
	if (er != erSuccess)
		return er;
	if (resp.size() < 1)
		return KCERR_CALL_FAILED;
	suggestion = std::move(resp[0]);
	if (suggestion[0] == ' ')
		suggestion.erase(0, 1);
	return erSuccess;
}

ECRESULT ECSearchClient::SyncRun()
{
	std::vector<std::string> lstVoid;
	return DoCmd("SYNCRUN", lstVoid);
}

} /* namespace */
