/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <iostream>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cerrno>
#include <cassert>
#include <mutex>
#include <kopano/EMSAbTag.h>
#include <kopano/ECConfig.h>
#include <kopano/ECDefs.h>
#include <kopano/ECLogger.h>
#include <kopano/ECPluginSharedData.h>
#include <kopano/stringutil.h>
#include "ECDatabaseFactory.h"
#include "DBUserPlugin.h"
#include <kopano/ecversion.h>

using namespace KC;

extern "C" {

UserPlugin *getUserPluginInstance(std::mutex &pluginlock,
    ECPluginSharedData *shareddata)
{
	return new DBUserPlugin(pluginlock, shareddata);
}

	void deleteUserPluginInstance(UserPlugin *up) {
		delete up;
	}

unsigned long getUserPluginVersion()
{
	return 1;
}

const char kcsrv_plugin_version[] = PROJECT_VERSION;

} /* extern "C" */

using std::runtime_error;
using std::string;

DBUserPlugin::DBUserPlugin(std::mutex &pluginlock,
    ECPluginSharedData *shareddata) :
	DBPlugin(pluginlock, shareddata)
{
	if (m_bDistributed)
		throw notsupported("Distributed Kopano not supported when using the Database Plugin");
}

void DBUserPlugin::InitPlugin()
{
	DBPlugin::InitPlugin();
}

objectsignature_t DBUserPlugin::resolveName(objectclass_t objclass, const string &name, const objectid_t &company)
{
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;
	string signature;
	const char *lpszSearchProperty;

	if (company.id.empty())
		LOG_PLUGIN_DEBUG("%s Class %x, Name %s", __FUNCTION__, objclass, name.c_str());
	else
		LOG_PLUGIN_DEBUG("%s Class %x, Name %s, Company %s", __FUNCTION__, objclass, name.c_str(), company.id.c_str());

	switch (objclass) {
	case OBJECTCLASS_USER:
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		lpszSearchProperty = OP_LOGINNAME;
		break;
	case OBJECTCLASS_DISTLIST:
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
	case DISTLIST_DYNAMIC:
		lpszSearchProperty = OP_GROUPNAME;
		break;
	case CONTAINER_COMPANY:
		lpszSearchProperty = OP_COMPANYNAME;
		break;
	case OBJECTCLASS_CONTAINER:
	case CONTAINER_ADDRESSLIST:
	case OBJECTCLASS_UNKNOWN:
		// We don't have a search property, search through all fields
		lpszSearchProperty = NULL;
		break;
	default:
		throw runtime_error("Object is wrong type");
	}

	/*
	 * Join the DB_OBJECT_TABLE together twice. This is done because
	 * we need to link the entry for the user and the entry for the
	 * company into a single line. Once those are all linked we can
	 * set the WHERE restrictions to get the correct line.
	 */
	auto strQuery =
		"SELECT DISTINCT o.externid, o.objectclass, modtime.value, user.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS user "
			"ON user.objectid = o.id "
			"AND upper(user.value) = upper('" + m_lpDatabase->Escape(name) + "') ";
	if (lpszSearchProperty)
		strQuery += "AND user.propname = '" + (string)lpszSearchProperty + "' ";

	if (m_bHosted && !company.id.empty())
		// join company, company itself inclusive
		strQuery +=
			"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS usercompany "
				"ON usercompany.objectid = o.id "
				"AND (usercompany.propname = '" + OP_COMPANYID + "' AND usercompany.value = hex('" + m_lpDatabase->Escape(company.id) + "') OR "
					"usercompany.propname = '" + OP_COMPANYNAME + "' AND usercompany.objectid = '" + m_lpDatabase->Escape(company.id) + "')";

	strQuery +=
		"LEFT JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS modtime "
			"ON modtime.propname = '" + OP_MODTIME + "' "
			"AND modtime.objectid = o.id ";
	if (objclass != OBJECTCLASS_UNKNOWN)
		strQuery += "WHERE " + OBJECTCLASS_COMPARE_SQL("o.objectclass", objclass);
	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[3] == NULL)
			throw runtime_error(string("db_row_failed: object null"));

		if (strcasecmp(lpDBRow[3], name.c_str()) != 0)
			continue;
		auto lpDBLen = lpResult.fetch_row_lengths();
		if (lpDBLen == NULL || lpDBLen[0] == 0)
			throw runtime_error(string("db_row_failed: object empty"));

		if(lpDBRow[2] != NULL)
			signature = lpDBRow[2];
		return objectsignature_t(objectid_t(std::string(lpDBRow[0], lpDBLen[0]), static_cast<objectclass_t>(atoi(lpDBRow[1]))), signature);
	}

	throw objectnotfound(name);
}

objectsignature_t DBUserPlugin::authenticateUser(const string &username, const string &password, const objectid_t &company)
{
	objectid_t	objectid;
	std::string signature;
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;

	/*
	 * Join the DB_OBJECT_TABLE together twice. This is done because
	 * we need to link the entry for the user and the entry for the
	 * company into a single line. Once those are all linked we can
	 * set the WHERE restrictions to get the correct line.
	 */
	auto strQuery =
		"SELECT pass.propname, pass.value, o.externid, modtime.value, op.value "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS op "
		"ON o.id = op.objectid "
		"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS pass "
		"ON pass.objectid = o.id ";
	if (m_bHosted && !company.id.empty())
		// join company, company itself inclusive
		strQuery +=
			"JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS usercompany "
				"ON usercompany.objectid = o.id "
				"AND (usercompany.propname = '" + OP_COMPANYID + "' AND usercompany.value = hex('" + m_lpDatabase->Escape(company.id) + "') OR "
					"usercompany.propname = '" + OP_COMPANYNAME + "' AND usercompany.objectid = '" + m_lpDatabase->Escape(company.id) + "')";

	strQuery +=
		"LEFT JOIN " + (string)DB_OBJECTPROPERTY_TABLE + " AS modtime "
		"ON modtime.objectid = o.id "
		"AND modtime.propname = '" + OP_MODTIME + "' "
		"WHERE " + OBJECTCLASS_COMPARE_SQL("o.objectclass", ACTIVE_USER) + " "
		"AND op.propname = '" + (string)OP_LOGINNAME + "' "
		"AND op.value = '" + m_lpDatabase->Escape(username) + "' "
		"AND pass.propname = '" + (string)OP_PASSWORD "'";
	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL || lpDBRow[4] == NULL)
			throw runtime_error("Trying to authenticate failed: database error");

		if (strcasecmp(lpDBRow[4], username.c_str()) != 0)
			continue;
		auto lpDBLen = lpResult.fetch_row_lengths();
		if (lpDBLen == NULL || lpDBLen[2] == 0)
			throw runtime_error("Trying to authenticate failed: database error");

		if (strcmp(lpDBRow[0], OP_PASSWORD) != 0)
			throw login_error("Trying to authenticate failed: wrong username or password");

		// Check Password
		MD5_CTX crypt;
		std::string salt = lpDBRow[1];
		salt.resize(8);
		MD5_Init(&crypt);
		MD5_Update(&crypt, salt.c_str(), salt.length());
		MD5_Update(&crypt, password.c_str(), password.size());
		auto strMD5 = salt + zcp_md5_final_hex(&crypt);
		if (strMD5.compare(lpDBRow[1]) == 0)
			objectid = objectid_t(string(lpDBRow[2], lpDBLen[2]), ACTIVE_USER);	// Password is oke
		else
			throw login_error("Trying to authenticate failed: wrong username or password");
		if(lpDBRow[3] != NULL)
			signature = lpDBRow[3];

		return objectsignature_t(objectid, signature);
	}

	throw login_error("Trying to authenticate failed: wrong username or password");
}

signatures_t
DBUserPlugin::searchObject(const std::string &match, unsigned int ulFlags)
{
	static constexpr const char *const search_props[] =
	{
		OP_LOGINNAME, OP_FULLNAME, OP_EMAILADDRESS,	/* This will match all users */
		OP_GROUPNAME,								/* This will match all groups */
		OP_COMPANYNAME,								/* This will match all companies */
		NULL,
	};

	LOG_PLUGIN_DEBUG("%s %s flags:%x", __FUNCTION__, match.c_str(), ulFlags);
	return searchObjects(match.c_str(), search_props, nullptr, ulFlags);
}

void DBUserPlugin::modifyObjectId(const objectid_t &oldId, const objectid_t &newId)
{
	throw notimplemented("Modifying objects is not supported when using the DB user plugin.");
}

void DBUserPlugin::setQuota(const objectid_t &objectid, const quotadetails_t &quotadetails)
{
	DB_RESULT lpResult;

	// check if user exist
	auto strQuery =
		"SELECT o.externid "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"WHERE o.externid='" + m_lpDatabase->Escape(objectid.id) + "' "
		"AND " + OBJECTCLASS_COMPARE_SQL("o.objectclass", objectid.objclass) + " LIMIT 2";
	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if(er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
	if (lpResult.get_num_rows() != 1)
		throw objectnotfound(objectid.id);
	auto lpDBRow = lpResult.fetch_row();
	if(lpDBRow == NULL || lpDBRow[0] == NULL)
		throw runtime_error(string("db_row_failed: object null"));

	DBPlugin::setQuota(objectid, quotadetails);
}

objectdetails_t DBUserPlugin::getPublicStoreDetails()
{
	throw notsupported("public store details");
}

serverdetails_t DBUserPlugin::getServerDetails(const std::string &server)
{
	throw notsupported("server details");
}

serverlist_t DBUserPlugin::getServers()
{
	throw notsupported("server list");
}

void DBUserPlugin::addSubObjectRelation(userobject_relation_t relation, const objectid_t &parentobject, const objectid_t &childobject)
{
	DB_RESULT lpResult;

	// Check if parent exist
	auto strQuery =
		"SELECT o.externid "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"WHERE o.externid='" + m_lpDatabase->Escape(parentobject.id) + "' "
			"AND " + OBJECTCLASS_COMPARE_SQL("o.objectclass", parentobject.objclass);
	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(string("db_query: ") + strerror(er));
	if (lpResult.get_num_rows() != 1)
		throw objectnotfound("db_user: Relation does not exist, id:" + parentobject.id);

	DBPlugin::addSubObjectRelation(relation, parentobject, childobject);
}
