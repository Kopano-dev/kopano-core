/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <stdexcept>
#include <vector>
#include <sys/types.h>
#include <pwd.h>
#include <sstream>
#include <cassert>
#include <sys/stat.h>
#include <grp.h>
#include <crypt.h>
#include <set>
#include <iterator>
#ifdef HAVE_SHADOW_H
#include <shadow.h>
#include <cerrno>
#endif
#include <kopano/EMSAbTag.h>
#include <kopano/ECConfig.h>
#include <kopano/ECDefs.h>
#include <kopano/ECLogger.h>
#include <kopano/ECPluginSharedData.h>
#include <kopano/stringutil.h>
#include "UnixUserPlugin.h"
#include <kopano/ecversion.h>

/**
 * static buffer size for getpwnam_r() calls etc.
 * needs to be big enough for all strings in the passwd/group/spwd struct:
 */
#define PWBUFSIZE 16384

using namespace KC;

extern "C" {

UserPlugin *getUserPluginInstance(std::mutex &pluginlock,
    ECPluginSharedData *shareddata)
{
	return new UnixUserPlugin(pluginlock, shareddata);
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

UnixUserPlugin::UnixUserPlugin(std::mutex &pluginlock,
    ECPluginSharedData *shareddata) :
	DBPlugin(pluginlock, shareddata)
{
	static constexpr const configsetting_t lpDefaults[] = {
		{ "fullname_charset", "iso-8859-15" }, // US-ASCII compatible with support for high characters
		{ "default_domain", "localhost" },			// no sane default
		{"non_login_shell", "/sbin/nologin /bin/false", CONFIGSETTING_RELOADABLE}, // create a non-login box when a user has this shell
		{ "min_user_uid", "1000", CONFIGSETTING_RELOADABLE },
		{ "max_user_uid", "10000", CONFIGSETTING_RELOADABLE },
		{ "except_user_uids", "", CONFIGSETTING_RELOADABLE },
		{ "min_group_gid", "1000", CONFIGSETTING_RELOADABLE },
		{ "max_group_gid", "10000", CONFIGSETTING_RELOADABLE },
		{ "except_group_gids", "", CONFIGSETTING_RELOADABLE },
		{ NULL, NULL }
	};

	m_config = shareddata->CreateConfig(lpDefaults);
	if (!m_config)
		throw runtime_error(string("Not a valid configuration file."));

	if (m_bHosted)
		throw notsupported("Hosted Kopano not supported when using the Unix Plugin");
	if (m_bDistributed)
		throw notsupported("Distributed Kopano not supported when using the Unix Plugin");
}

void UnixUserPlugin::InitPlugin(std::shared_ptr<ECStatsCollector> sc)
{
	DBPlugin::InitPlugin(std::move(sc));

	// we only need unix_charset -> kopano charset
	try {
		m_iconv.reset(new decltype(m_iconv)::element_type("utf-8", m_config->GetSetting("fullname_charset")));
	} catch (const convert_exception &) {
		throw runtime_error(string("Cannot setup charset converter, check \"fullname_charset\" in cfg"));
	}
}

void UnixUserPlugin::findUserID(const string &id, struct passwd *pwd, char *buffer)
{
	struct passwd *pw = NULL;
	uid_t minuid = fromstring<const char *, uid_t>(m_config->GetSetting("min_user_uid"));
	uid_t maxuid = fromstring<const char *, uid_t>(m_config->GetSetting("max_user_uid"));
	auto exceptuids = tokenize(m_config->GetSetting("except_user_uids"), " \t");
	int ret = getpwuid_r(atoi(id.c_str()), pwd, buffer, PWBUFSIZE, &pw);
	if (ret != 0)
		errnoCheck(id, ret);
	if (pw == NULL)
		throw objectnotfound(id);

	if (pw->pw_uid < minuid || pw->pw_uid >= maxuid)
		throw objectnotfound(id);
			
	for (unsigned int i = 0; i < exceptuids.size(); ++i)
		if (pw->pw_uid == fromstring<const std::string, uid_t>(exceptuids[i]))
			throw objectnotfound(id);
}

void UnixUserPlugin::findUser(const string &name, struct passwd *pwd, char *buffer)
{
	struct passwd *pw = NULL;
	uid_t minuid = fromstring<const char *, uid_t>(m_config->GetSetting("min_user_uid"));
	uid_t maxuid = fromstring<const char *, uid_t>(m_config->GetSetting("max_user_uid"));
	auto exceptuids = tokenize(m_config->GetSetting("except_user_uids"), " \t");
	int ret = getpwnam_r(name.c_str(), pwd, buffer, PWBUFSIZE, &pw);
	if (ret != 0)
		errnoCheck(name, ret);
	if (pw == NULL)
		throw objectnotfound(name);

	if (pw->pw_uid < minuid || pw->pw_uid >= maxuid)
		throw objectnotfound(name);
			
	for (unsigned int i = 0; i < exceptuids.size(); ++i)
		if (pw->pw_uid == fromstring<const std::string, uid_t>(exceptuids[i]))
			throw objectnotfound(name);
}

void UnixUserPlugin::findGroupID(const string &id, struct group *grp, char *buffer)
{
	struct group *gr = NULL;
	gid_t mingid = fromstring<const char *, gid_t>(m_config->GetSetting("min_group_gid"));
	gid_t maxgid = fromstring<const char *, gid_t>(m_config->GetSetting("max_group_gid"));
	auto exceptgids = tokenize(m_config->GetSetting("except_group_gids"), " \t");
	int ret = getgrgid_r(atoi(id.c_str()), grp, buffer, PWBUFSIZE, &gr);
	if (ret != 0)
		errnoCheck(id, ret);
	if (gr == NULL)
		throw objectnotfound(id);

	if (gr->gr_gid < mingid || gr->gr_gid >= maxgid)
		throw objectnotfound(id);

	for (unsigned int i = 0; i < exceptgids.size(); ++i)
		if (gr->gr_gid == fromstring<const std::string, gid_t>(exceptgids[i]))
			throw objectnotfound(id);
}

void UnixUserPlugin::findGroup(const string &name, struct group *grp, char *buffer)
{
	struct group *gr = NULL;
	gid_t mingid = fromstring<const char *, gid_t>(m_config->GetSetting("min_group_gid"));
	gid_t maxgid = fromstring<const char *, gid_t>(m_config->GetSetting("max_group_gid"));
	auto exceptgids = tokenize(m_config->GetSetting("except_group_gids"), " \t");
	int ret = getgrnam_r(name.c_str(), grp, buffer, PWBUFSIZE, &gr);
	if (ret != 0)
		errnoCheck(name, ret);
	if (gr == NULL)
		throw objectnotfound(name);

	if (gr->gr_gid < mingid || gr->gr_gid >= maxgid)
		throw objectnotfound(name);

	for (unsigned int i = 0; i < exceptgids.size(); ++i)
		if (gr->gr_gid == fromstring<const std::string, gid_t>(exceptgids[i]))
			throw objectnotfound(name);
}

static objectclass_t shell_to_class(const std::vector<std::string> &nls, const char *shell)
{
	return std::find(nls.cbegin(), nls.cend(), shell) == nls.cend() ?
	       ACTIVE_USER : NONACTIVE_USER;
}

static objectclass_t shell_to_class(ECConfig *cfg, const char *shell)
{
	return shell_to_class(tokenize(cfg->GetSetting("non_login_shell"), ' ', true), shell);
}

objectsignature_t UnixUserPlugin::resolveUserName(const string &name)
{
	char buffer[PWBUFSIZE];
	struct passwd pws;
	findUser(name, &pws, buffer);
	objectid_t objectid{tostring(pws.pw_uid), shell_to_class(m_config, pws.pw_shell)};
	return objectsignature_t(objectid, getDBSignature(objectid) + pws.pw_gecos + pws.pw_name);
}

objectsignature_t UnixUserPlugin::resolveGroupName(const string &name)
{
	char buffer[PWBUFSIZE];
	struct group grp;
  
	findGroup(name, &grp, buffer);
	return objectsignature_t(objectid_t(tostring(grp.gr_gid), DISTLIST_SECURITY), grp.gr_name);
}

objectsignature_t UnixUserPlugin::resolveName(objectclass_t objclass, const string &name, const objectid_t &company)
{
	objectsignature_t user;
	objectsignature_t group;

	if (company.id.empty())
		LOG_PLUGIN_DEBUG("%s Class %x, Name %s", __FUNCTION__, objclass, name.c_str());
	else
		LOG_PLUGIN_DEBUG("%s Class %x, Name %s, Company %s", __FUNCTION__, objclass, name.c_str(), company.id.c_str());

	switch (OBJECTCLASS_TYPE(objclass)) {
	case OBJECTTYPE_UNKNOWN:
		// Caller doesn't know what he is looking for, try searching through
		// users and groups. Note that 1 function _must_ fail because otherwise
		// we have a duplicate entry.
		try {
			user = resolveUserName(name);
		} catch (const std::exception &e) {
			// object is not a user
		}

		try {
			group = resolveGroupName(name);
		} catch (const std::exception &e) {
			// object is not a group
		}

		if (!user.id.id.empty()) {
			// Object is both user and group
			if (!group.id.id.empty())
				throw toomanyobjects(name);
			return user;
		} else {
			// Object is neither user not group
			if (group.id.id.empty())
				throw objectnotfound(name);
			return group;
		}
	case OBJECTTYPE_MAILUSER:
		return resolveUserName(name);
	case OBJECTTYPE_DISTLIST:
		return resolveGroupName(name);
	default:
		throw runtime_error("Unknown object type " + stringify(objclass));
	}
}

objectsignature_t UnixUserPlugin::authenticateUser(const string &username, const string &password, const objectid_t &companyname) {
	struct passwd pws, *pw = NULL;
	char buffer[PWBUFSIZE];
	uid_t minuid = fromstring<const char *, uid_t>(m_config->GetSetting("min_user_uid"));
	uid_t maxuid = fromstring<const char *, uid_t>(m_config->GetSetting("max_user_uid"));
	auto exceptuids = tokenize(m_config->GetSetting("except_user_uids"), " \t");
	std::unique_ptr<struct crypt_data> cryptdata;

	cryptdata.reset(new struct crypt_data); // malloc because it is > 128K !
	memset(cryptdata.get(), 0, sizeof(struct crypt_data));

	int ret = getpwnam_r(username.c_str(), &pws, buffer, PWBUFSIZE, &pw);
	if (ret != 0)
		errnoCheck(username, ret);
	if (pw == NULL)
		throw objectnotfound(username);

	if (pw->pw_uid < minuid || pw->pw_uid >= maxuid)
		throw objectnotfound(username);
		
	for (unsigned i = 0; i < exceptuids.size(); ++i)
		if (pw->pw_uid == fromstring<const std::string, uid_t>(exceptuids[i]))
			throw objectnotfound(username);
	if (shell_to_class(m_config, pw->pw_shell) != ACTIVE_USER)
		throw login_error("Non-active user disallowed to login");

	auto ud = objectdetailsFromPwent(pw);
	auto crpw = crypt_r(password.c_str(), ud.GetPropString(OB_PROP_S_PASSWORD).c_str(), cryptdata.get());
	if (crpw == nullptr || strcmp(crpw, ud.GetPropString(OB_PROP_S_PASSWORD).c_str()) != 0)
		throw login_error("Trying to authenticate failed: wrong username or password");

	objectid_t objectid{tostring(pw->pw_uid), ACTIVE_USER};
	return objectsignature_t(objectid, getDBSignature(objectid) + pw->pw_gecos + pw->pw_name);
}

bool UnixUserPlugin::matchUserObject(struct passwd *pw, const string &match, unsigned int ulFlags)
{
	bool matched = false;

	// username or fullname
	if (ulFlags & EMS_AB_ADDRESS_LOOKUP)
		matched =
			(strcasecmp(pw->pw_name, (char*)match.c_str()) == 0) ||
			(strcasecmp((char*)m_iconv->convert(pw->pw_gecos).c_str(), (char*)match.c_str()) == 0);
	else
		matched =
			(strncasecmp(pw->pw_name, (char*)match.c_str(), match.size()) == 0) ||
			(strncasecmp((char*)m_iconv->convert(pw->pw_gecos).c_str(), (char*)match.c_str(), match.size()) == 0);

	if (matched)
		return matched;

	auto email = std::string(pw->pw_name) + "@" + m_config->GetSetting("default_domain");
	if(ulFlags & EMS_AB_ADDRESS_LOOKUP)
		return email == match;
	return strncasecmp(email.c_str(), match.c_str(), match.size()) == 0;
}

bool UnixUserPlugin::matchGroupObject(struct group *gr, const string &match, unsigned int ulFlags)
{
	if(ulFlags & EMS_AB_ADDRESS_LOOKUP)
		return strcasecmp(gr->gr_name, match.c_str()) == 0;
	return strncasecmp(gr->gr_name, match.c_str(), match.size()) == 0;
}

signatures_t UnixUserPlugin::getAllUserObjects(const std::string &match,
    unsigned int ulFlags)
{
	signatures_t objectlist;
	char buffer[PWBUFSIZE];
	struct passwd pws, *pw = NULL;
	uid_t minuid = fromstring<const char *, uid_t>(m_config->GetSetting("min_user_uid"));
	uid_t maxuid = fromstring<const char *, uid_t>(m_config->GetSetting("max_user_uid"));
	auto forbid_sh = tokenize(m_config->GetSetting("non_login_shell"), ' ', true);
	auto exceptuids = tokenize(m_config->GetSetting("except_user_uids"), " \t");
	std::set<uid_t> exceptuidset;

	transform(exceptuids.begin(), exceptuids.end(), inserter(exceptuidset, exceptuidset.begin()), fromstring<const std::string,uid_t>);

	setpwent();
	while (true) {
		if (getpwent_r(&pws, buffer, PWBUFSIZE, &pw) != 0)
			break;
		if (pw == NULL)
			break;

		// system users don't have kopano accounts
		if (pw->pw_uid < minuid || pw->pw_uid >= maxuid)
			continue;

		if (exceptuidset.find(pw->pw_uid) != exceptuidset.end())
			continue;

		if (!match.empty() && !matchUserObject(pw, match, ulFlags))
			continue;
		objectid_t objectid{tostring(pw->pw_uid), shell_to_class(forbid_sh, pw->pw_shell)};
		objectlist.emplace_back(objectid, getDBSignature(objectid) + pw->pw_gecos + pw->pw_name);
	}
	endpwent();

	return objectlist;
}

signatures_t UnixUserPlugin::getAllGroupObjects(const std::string &match,
    unsigned int ulFlags)
{
	signatures_t objectlist;
	char buffer[PWBUFSIZE];
	struct group grs, *gr = NULL;
	gid_t mingid = fromstring<const char *, gid_t>(m_config->GetSetting("min_group_gid"));
	gid_t maxgid = fromstring<const char *, gid_t>(m_config->GetSetting("max_group_gid"));
	auto exceptgids = tokenize(m_config->GetSetting("except_group_gids"), " \t");
	std::set<gid_t> exceptgidset;

	transform(exceptgids.begin(), exceptgids.end(), inserter(exceptgidset, exceptgidset.begin()), fromstring<const std::string,uid_t>);

	setgrent();
	while (true) {
		if (getgrent_r(&grs, buffer, PWBUFSIZE, &gr) != 0)
			break;
		if (gr == NULL)
			break;

		// system groups don't have kopano accounts
		if (gr->gr_gid < mingid || gr->gr_gid >= maxgid)
			continue;

		if (exceptgidset.find(gr->gr_gid) != exceptgidset.end())
			continue;

		if (!match.empty() && !matchGroupObject(gr, match, ulFlags))
			continue;
		objectlist.emplace_back(objectid_t(tostring(gr->gr_gid), DISTLIST_SECURITY), gr->gr_name);
	}
	endgrent();

	return objectlist;
}

signatures_t
UnixUserPlugin::getAllObjects(const objectid_t &companyid,
    objectclass_t objclass)
{
	signatures_t objectlist;
	std::map<objectclass_t, std::string> objectstrings;
	DB_RESULT lpResult;
	DB_ROW lpDBRow = NULL;
	unsigned int ulRows = 0;

	if (companyid.id.empty())
		LOG_PLUGIN_DEBUG("%s Class %x", __FUNCTION__, objclass);
	else
		LOG_PLUGIN_DEBUG("%s Company %s, Class %x", __FUNCTION__, companyid.id.c_str(), objclass);

	// use mutex to protect thread-unsafe setpwent()/setgrent() calls
	ulock_normal biglock(m_plugin_lock);
	switch (OBJECTCLASS_TYPE(objclass)) {
	case OBJECTTYPE_UNKNOWN:
		objectlist.merge(getAllUserObjects());
		objectlist.merge(getAllGroupObjects());
		break;
	case OBJECTTYPE_MAILUSER:
		objectlist.merge(getAllUserObjects());
		break;
	case OBJECTTYPE_DISTLIST:
		objectlist.merge(getAllGroupObjects());
		break;
	case OBJECTTYPE_CONTAINER:
		throw notsupported("objecttype not supported " + stringify(objclass));
	default:
		throw runtime_error("Unknown object type " + stringify(objclass));
	}
	biglock.unlock();

	// Cleanup old entries from deleted users/groups
	if (objectlist.empty())
		return objectlist;

	// Distribute all objects over the various types
	for (const auto &obj : objectlist) {
		if (!objectstrings[obj.id.objclass].empty())
			objectstrings[obj.id.objclass] += ", ";
		objectstrings[obj.id.objclass] += m_lpDatabase->Escape(obj.id.id);
	}

	// make list of obsolete objects
	auto strQuery = "SELECT id, objectclass FROM " + std::string(DB_OBJECT_TABLE) + " WHERE ";
	for (auto iterStrings = objectstrings.cbegin();
	     iterStrings != objectstrings.cend(); ++iterStrings) {
		if (iterStrings != objectstrings.cbegin())
			strQuery += "AND ";
		strQuery +=
			"(externid NOT IN (" + iterStrings->second + ") "
			 "AND " + OBJECTCLASS_COMPARE_SQL("objectclass", iterStrings->first) + ")";
	}

	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess) {
		ec_log_err("Unix plugin: Unable to cleanup old entries");
		return objectlist;
	}

	/* check if we have obsolete objects */
	ulRows = lpResult.get_num_rows();
	if (!ulRows)
		return objectlist;

	// clear our stringlist containing the valid entries and fill it with the deleted item ids
	objectstrings.clear();
	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (!objectstrings[(objectclass_t)atoi(lpDBRow[1])].empty())
			objectstrings[(objectclass_t)atoi(lpDBRow[1])] += ", ";
		objectstrings[(objectclass_t)atoi(lpDBRow[1])] += lpDBRow[0];
	}

	// remove obsolete objects
	strQuery = "DELETE FROM " + (string)DB_OBJECT_TABLE + " WHERE ";
	for (auto iterStrings = objectstrings.cbegin();
	     iterStrings != objectstrings.cend(); ++iterStrings) {
		if (iterStrings != objectstrings.cbegin())
			strQuery += "OR ";
		strQuery += "(externid IN (" + iterStrings->second + ") AND objectclass = " + stringify(iterStrings->first) + ")";
	}

	er = m_lpDatabase->DoDelete(strQuery, &ulRows);
	if (er != erSuccess) {
		ec_log_err("Unix plugin: Unable to cleanup old entries in object table");
		return objectlist;
	} else if (ulRows > 0) {
		ec_log_info("Unix plugin: Cleaned up %d old entries from object table", ulRows);
	}

	// Create subquery to select all ids which will be deleted
	auto strSubQuery =
		"SELECT o.id "
		"FROM " + (string)DB_OBJECT_TABLE + " AS o "
		"WHERE ";
	for (auto iterStrings = objectstrings.cbegin();
	     iterStrings != objectstrings.cend(); ++iterStrings) {
		if (iterStrings != objectstrings.cbegin())
			strSubQuery += "OR ";
		strSubQuery += "(o.externid IN (" + iterStrings->second + ") AND o.objectclass = " + stringify(iterStrings->first) + ")";
	}

	/* remove obsolete object properties */
	strQuery =
		"DELETE FROM " + (string)DB_OBJECTPROPERTY_TABLE + " "
		"WHERE objectid IN (" + strSubQuery + ")";
	er = m_lpDatabase->DoDelete(strQuery, &ulRows);
	if (er != erSuccess) {
		ec_log_err("Unix plugin: Unable to cleanup old entries in objectproperty table");
		return objectlist;
	} else if (ulRows > 0) {
		ec_log_info("Unix plugin: Cleaned up %d old entries from objectproperty table", ulRows);
	}

	strQuery =
		"DELETE FROM " + (string)DB_OBJECT_RELATION_TABLE + " "
		"WHERE objectid IN (" + strSubQuery + ") "
		"OR parentobjectid IN (" + strSubQuery + ")";
	er = m_lpDatabase->DoDelete(strQuery, &ulRows);
	if (er != erSuccess) {
		ec_log_err("Unix plugin: Unable to cleanup old entries in objectrelation table");
		return objectlist;
	} else if (ulRows > 0) {
		ec_log_info("Unix plugin: Cleaned-up %d old entries from objectrelation table", ulRows);
	}
	return objectlist;
}

objectdetails_t UnixUserPlugin::getObjectDetails(const objectid_t &externid)
{
	char buffer[PWBUFSIZE];
	objectdetails_t ud;
	struct passwd pws;
	struct group grp;
	DB_RESULT lpResult;

	LOG_PLUGIN_DEBUG("%s for externid %s, class %d", __FUNCTION__, bin2hex(externid.id).c_str(), externid.objclass);

	switch (externid.objclass) {
	case ACTIVE_USER:
	case NONACTIVE_USER:
	case NONACTIVE_ROOM:
	case NONACTIVE_EQUIPMENT:
	case NONACTIVE_CONTACT:
		findUserID(externid.id, &pws, buffer);
		ud = objectdetailsFromPwent(&pws);
		break;
	case DISTLIST_GROUP:
	case DISTLIST_SECURITY:
		findGroupID(externid.id, &grp, buffer);
		ud = objectdetailsFromGrent(&grp);
		break;
	default:
		throw runtime_error("Object is wrong type");
		break;
	}

	auto id = m_lpDatabase->Escape(externid.id);
	auto objclass = stringify(externid.objclass);
	auto strQuery = "SELECT id FROM " + std::string(DB_OBJECT_TABLE) + " WHERE externid = '" + id + "' AND objectclass = " + objclass;
	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		throw runtime_error(externid.id);
	auto lpRow = lpResult.fetch_row();
	if (lpRow && lpRow[0]) {
		strQuery = "UPDATE " + (string)DB_OBJECT_TABLE + " SET externid='" + id + "',objectclass=" + objclass + " WHERE id=" + lpRow[0];
		er = m_lpDatabase->DoUpdate(strQuery);
	} else {
		strQuery = "INSERT INTO " + (string)DB_OBJECT_TABLE + " (externid, objectclass) VALUES ('" + id + "', " + objclass + ")";
		er = m_lpDatabase->DoInsert(strQuery);
	}
	if (er != erSuccess)
		throw runtime_error(externid.id);

	try {
		ud.MergeFrom(DBPlugin::getObjectDetails(externid));
	} catch (...) { } // ignore errors; we'll try with just the information we have from Pwent

	return ud;
}

void UnixUserPlugin::changeObject(const objectid_t &id, const objectdetails_t &details, const std::list<std::string> *lpRemove)
{
	objectdetails_t tmp(details);

	// explicitly deny pam updates
	if (!details.GetPropString(OB_PROP_S_PASSWORD).empty())
		throw runtime_error("Updating the password is not allowed with the Unix plugin.");
	if (!details.GetPropString(OB_PROP_S_FULLNAME).empty())
		throw runtime_error("Updating the fullname is not allowed with the Unix plugin.");

	// Although updating username is invalid in Unix plugin, we still receive the OB_PROP_S_LOGIN field.
	// This is because kopano-admin -u <username> sends it, and that requirement is because the
	// UpdateUserDetailsFromClient call needs to convert the username/company to details.
	// Remove the username detail to allow updating user information.
	tmp.SetPropString(OB_PROP_S_LOGIN, string());

	DBPlugin::changeObject(id, tmp, lpRemove);
}

objectsignature_t UnixUserPlugin::createObject(const objectdetails_t &details) {
	throw notimplemented("Creating objects is not supported when using the Unix user plugin.");
}

void UnixUserPlugin::deleteObject(const objectid_t &id) {
	throw notimplemented("Deleting objects is not supported when using the Unix user plugin.");
}

void UnixUserPlugin::modifyObjectId(const objectid_t &oldId, const objectid_t &newId)
{
	throw notimplemented("Modifying objectid is not supported when using the Unix user plugin.");
}

signatures_t
UnixUserPlugin::getParentObjectsForObject(userobject_relation_t relation,
    const objectid_t &childid)
{
	signatures_t objectlist;
	char buffer[PWBUFSIZE];
	struct passwd pws;
	struct group grs, *gr = NULL;
	gid_t mingid = fromstring<const char *, gid_t>(m_config->GetSetting("min_group_gid"));
	gid_t maxgid = fromstring<const char *, gid_t>(m_config->GetSetting("max_group_gid"));
	auto exceptgids = tokenize(m_config->GetSetting("except_group_gids"), " \t");
	std::set<gid_t> exceptgidset;

	if (relation != OBJECTRELATION_GROUP_MEMBER)
		return DBPlugin::getParentObjectsForObject(relation, childid);

	LOG_PLUGIN_DEBUG("%s Relation: Group member", __FUNCTION__);

	findUserID(childid.id, &pws, buffer);
	std::string username = pws.pw_name; // make sure we have a _copy_ of the string, not just another pointer

	try {
		findGroupID(tostring(pws.pw_gid), &grs, buffer);
		objectlist.emplace_back(objectid_t(tostring(grs.gr_gid), DISTLIST_SECURITY), grs.gr_name);
	} catch (const std::exception &e) {
		// Ignore error
	}	

	transform(exceptgids.begin(), exceptgids.end(), inserter(exceptgidset, exceptgidset.begin()), fromstring<const std::string,gid_t>);

	// This is a rather expensive operation: loop through all the
	// groups, and check each member for each group.
	ulock_normal biglock(m_plugin_lock);
	setgrent();
	while (true) {
		if (getgrent_r(&grs, buffer, PWBUFSIZE, &gr) != 0)
			break;
		if (gr == NULL)
			break;

		// system users don't have kopano accounts
		if (gr->gr_gid < mingid || gr->gr_gid >= maxgid)
			continue;

		if (exceptgidset.find(gr->gr_gid) != exceptgidset.end())
			continue;

		for (int i = 0; gr->gr_mem[i] != NULL; ++i)
			if (strcmp(username.c_str(), gr->gr_mem[i]) == 0) {
				objectlist.emplace_back(objectid_t(tostring(gr->gr_gid), DISTLIST_SECURITY), gr->gr_name);
				break;
			}
	}
	endgrent();
	biglock.unlock();

	// because users can be explicitly listed in their default group
	objectlist.sort();
	objectlist.unique();
	return objectlist;
}

signatures_t
UnixUserPlugin::getSubObjectsForObject(userobject_relation_t relation,
    const objectid_t &parentid)
{
	signatures_t objectlist;
	char buffer[PWBUFSIZE];
	struct passwd pws, *pw = NULL;
	struct group grp;
	uid_t minuid = fromstring<const char *, uid_t>(m_config->GetSetting("min_user_uid"));
	uid_t maxuid = fromstring<const char *, uid_t>(m_config->GetSetting("max_user_uid"));
	auto forbid_sh = tokenize(m_config->GetSetting("non_login_shell"), ' ', true);
	gid_t mingid = fromstring<const char *, gid_t>(m_config->GetSetting("min_group_gid"));
	gid_t maxgid = fromstring<const char *, gid_t>(m_config->GetSetting("max_group_gid"));
	auto exceptuids = tokenize(m_config->GetSetting("except_user_uids"), " \t");
	std::set<uid_t> exceptuidset;

	if (relation != OBJECTRELATION_GROUP_MEMBER)
		return DBPlugin::getSubObjectsForObject(relation, parentid);

	LOG_PLUGIN_DEBUG("%s Relation: Group member", __FUNCTION__);

	findGroupID(parentid.id, &grp, buffer);
	for (unsigned int i = 0; grp.gr_mem[i] != NULL; ++i)
		try {
			objectlist.emplace_back(resolveUserName(grp.gr_mem[i]));
		} catch (const std::exception &e) {
			// Ignore error
		}

	transform(exceptuids.begin(), exceptuids.end(), inserter(exceptuidset, exceptuidset.begin()), fromstring<const std::string,uid_t>);

	// iterate through /etc/passwd users to find default group (e.g. users) and add it to the list
	ulock_normal biglock(m_plugin_lock);
	setpwent();
	while (true) {
		if (getpwent_r(&pws, buffer, PWBUFSIZE, &pw) != 0)
			break;
		if (pw == NULL)
			break;

		// system users don't have kopano accounts
		if (pw->pw_uid < minuid || pw->pw_uid >= maxuid)
			continue;

		if (exceptuidset.find(pw->pw_uid) != exceptuidset.end())
			continue;

		// is it a member, and fits the default group in the range?
		if (pw->pw_gid != grp.gr_gid || pw->pw_gid < mingid || pw->pw_gid >= maxgid)
			continue;
		objectid_t objectid{tostring(pw->pw_uid), shell_to_class(forbid_sh, pw->pw_shell)};
		objectlist.emplace_back(objectid, getDBSignature(objectid) + pw->pw_gecos + pw->pw_name);
	}
	endpwent();
	biglock.unlock();

	// because users can be explicitly listed in their default group
	objectlist.sort();
	objectlist.unique();
	return objectlist;
}

void UnixUserPlugin::addSubObjectRelation(userobject_relation_t relation, const objectid_t &id, const objectid_t &member)
{
	if (relation != OBJECTRELATION_QUOTA_USERRECIPIENT && relation != OBJECTRELATION_USER_SENDAS)
		throw notimplemented("Adding object relations is not supported when using the Unix user plugin.");
	DBPlugin::addSubObjectRelation(relation, id, member);
}

void UnixUserPlugin::deleteSubObjectRelation(userobject_relation_t relation, const objectid_t &id, const objectid_t &member)
{
	if (relation != OBJECTRELATION_QUOTA_USERRECIPIENT && relation != OBJECTRELATION_USER_SENDAS)
		throw notimplemented("Deleting object relations is not supported when using the Unix user plugin.");
	DBPlugin::deleteSubObjectRelation(relation, id, member);
}

signatures_t
UnixUserPlugin::searchObject(const std::string &match, unsigned int ulFlags)
{
	struct passwd pws, *pw = NULL;
	signatures_t objectlist;

	LOG_PLUGIN_DEBUG("%s %s flags:%x", __FUNCTION__, match.c_str(), ulFlags);

	ulock_normal biglock(m_plugin_lock);
	objectlist.merge(getAllUserObjects(match, ulFlags));
	objectlist.merge(getAllGroupObjects(match, ulFlags));
	biglock.unlock();

	// See if we get matches based on database details as well
	try {
		char buffer[PWBUFSIZE];
		static constexpr const char *const search_props[] = {OP_EMAILADDRESS, nullptr};
		for (const auto &sig : DBPlugin::searchObjects(match, search_props, nullptr, ulFlags)) {
			// the DBPlugin returned the DB signature, so we need to prepend this with the gecos signature
			int ret = getpwuid_r(atoi(sig.id.id.c_str()), &pws, buffer, PWBUFSIZE, &pw);
			if (ret != 0)
				errnoCheck(sig.id.id, ret);
			if (pw == NULL)	// object not found anymore
				continue;
			objectlist.emplace_back(sig.id, sig.signature + pw->pw_gecos + pw->pw_name);
		}
	} catch (const objectnotfound &e) {
			// Ignore exception, we will check lObjects.empty() later.
	} // All other exceptions should be thrown further up the chain.

	objectlist.sort();
	objectlist.unique();
	if (objectlist.empty())
		throw objectnotfound(string("unix_plugin: no match: ") + match);

	return objectlist;
}

objectdetails_t UnixUserPlugin::getPublicStoreDetails()
{
	throw notsupported("public store details");
}

serverdetails_t UnixUserPlugin::getServerDetails(const std::string &server)
{
	throw notsupported("server details");
}

serverlist_t UnixUserPlugin::getServers()
{
	throw notsupported("server list");
}

std::map<objectid_t, objectdetails_t>
UnixUserPlugin::getObjectDetails(const std::list<objectid_t> &objectids)
{
	std::map<objectid_t, objectdetails_t> mapdetails;

	if (objectids.empty())
		return mapdetails;

	for (const auto &id : objectids) {
		try {
			mapdetails[id] = getObjectDetails(id);
		} catch (const objectnotfound &e) {
			// ignore not found error
		}
	}
    
    return mapdetails;
}

// -------------
// private
// -------------

objectdetails_t UnixUserPlugin::objectdetailsFromPwent(const struct passwd *pw)
{
	objectdetails_t ud;

	ud.SetPropString(OB_PROP_S_LOGIN, pw->pw_name);
	ud.SetClass(shell_to_class(m_config, pw->pw_shell));
	auto gecos = m_iconv->convert(pw->pw_gecos);

	// gecos may contain room/phone number etc. too
	auto comma = gecos.find(",");
	if (comma != string::npos)
		ud.SetPropString(OB_PROP_S_FULLNAME, gecos.substr(0, comma));
	else
		ud.SetPropString(OB_PROP_S_FULLNAME, gecos);

	if (!strcmp(pw->pw_passwd, "x")) {
		// shadow password entry
		struct spwd spws, *spw = NULL;
		char sbuffer[PWBUFSIZE];

		if (getspnam_r(pw->pw_name, &spws, sbuffer, PWBUFSIZE, &spw) != 0) {
			ec_log_warn("getspnam_r: %s", strerror(errno));
			/* set invalid password entry, cannot login without a password */
			ud.SetPropString(OB_PROP_S_PASSWORD, "x");
		} else if (spw == NULL) {
			// invalid entry, must have a shadow password set in this case
			// throw objectnotfound(ud->id);
			// too bad that the password couldn't be found, but it's not that critical
			ec_log_warn("Warning: unable to find password for user \"%s\": %s", pw->pw_name, strerror(errno));
			ud.SetPropString(OB_PROP_S_PASSWORD, "x"); // set invalid password entry, cannot login without a password
		} else {
			ud.SetPropString(OB_PROP_S_PASSWORD, spw->sp_pwdp);
		}
	} else if (!strcmp(pw->pw_passwd, "*") || !strcmp(pw->pw_passwd, "!")){
		throw objectnotfound(string());
	} else {
		ud.SetPropString(OB_PROP_S_PASSWORD, pw->pw_passwd);
	}
	
	// This may be overridden by settings in the database
	ud.SetPropString(OB_PROP_S_EMAIL, std::string(pw->pw_name) + "@" + m_config->GetSetting("default_domain"));
	return ud;
}

objectdetails_t UnixUserPlugin::objectdetailsFromGrent(const struct group *gr)
{
	objectdetails_t gd(DISTLIST_SECURITY);
	gd.SetPropString(OB_PROP_S_LOGIN, gr->gr_name);
	gd.SetPropString(OB_PROP_S_FULLNAME, gr->gr_name);
	return gd;
}

std::string UnixUserPlugin::getDBSignature(const objectid_t &id)
{
	DB_RESULT lpResult;
	auto strQuery =
		"SELECT op.value "
		"FROM " + (string)DB_OBJECTPROPERTY_TABLE + " AS op "
		"JOIN " + (string)DB_OBJECT_TABLE + " AS o "
			"ON op.objectid = o.id "
		"WHERE o.externid = '" + m_lpDatabase->Escape(id.id) + "' "
			"AND o.objectclass = " + stringify(id.objclass) + " "
			"AND op.propname = '" + OP_MODTIME + "' LIMIT 1";

	auto er = m_lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return string();
	auto lpDBRow = lpResult.fetch_row();
	if (lpDBRow == NULL || lpDBRow[0] == NULL)
		return string();

	return lpDBRow[0];
}

void UnixUserPlugin::errnoCheck(const std::string &user, int e) const
{
	if (e == 0)
		return;
	char buffer[256];
	auto retbuf = strerror_r(e, buffer, sizeof(buffer));

	// from the getpwnam() man page: (notice the last or...)
	//  ERRORS
	//    0 or ENOENT or ESRCH or EBADF or EPERM or ...
	//    The given name or uid was not found.

	switch (e) {
		// 0 is handled in top if()
	case ENOENT:
	case ESRCH:
	case EBADF:
	case EPERM:
		// calling function must check pw == NULL to throw objectnotfound()
		break;
	default:
		// broken system .. do not delete user from database
		throw runtime_error(string("unable to query for user ") + user + string(". Error: ") + retbuf);
	};
}
