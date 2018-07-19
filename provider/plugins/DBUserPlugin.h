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

// -*- Mode: c++ -*-
#ifndef DBUSERPLUGIN_H
#define DBUSERPLUGIN_H

#include <memory>
#include <mutex>
#include <string>
#include <kopano/zcdefs.h>
#include "plugin.h"
#include "DBBase.h"

/**
 * @defgroup userplugin_bi_db Build-in database user plugin
 * @ingroup userplugin
 * @{
 */
using KC::objectclass_t;
using KC::objectdetails_t;
using KC::objectid_t;
using KC::objectsignature_t;
using KC::quotadetails_t;
using KC::serverdetails_t;
using KC::serverlist_t;
using KC::signatures_t;
using KC::userobject_relation_t;

/**
 * Build-in database user plugin.
 *
 * User management based on Mysql. This is the build-in user management system
 */
class DBUserPlugin _kc_final : public KC::DBPlugin {
public:
    /**
	 * @param[in]	pluginlock
	 *					The plugin mutex
	 * @param[in]   shareddata
	 *					The singleton shared plugin data.
	 * @throw notsupported When multi-server support is enabled
	 */
	DBUserPlugin(std::mutex &, KC::ECPluginSharedData *shareddata);
    /**
	 * Initialize plugin
	 *
	 * Calls DBPlugin::InitPlugin()
	 */
	virtual void InitPlugin();

	/**
	 * Resolve name and company to objectsignature
	 *
	 * @param[in]	objclass
	 *					The objectclass of the name which should be resolved.
	 *					The objectclass can be partially unknown (OBJECTCLASS_UNKNOWN, MAILUSER_UNKNOWN, ...)
	 * @param[in]	name
	 *					The name which should be resolved
	 * @param[in]	company
	 *					The company beneath which the name should be searched
	 *					This objectid can be empty.
	 * @return The object signature of the resolved object
	 * @throw runtime_error When a Database error occurred.
	 * @throw objectnotfound When no object was found.
	 * @throw toomanyobjects When more then one object was found.
	 */
	virtual objectsignature_t resolveName(objectclass_t objclass, const std::string &name, const objectid_t &company);

    /**
	 * Authenticate user with username and password
	 *
	 * @param[in]	username
	 *					The username of the user to be authenticated
	 * @param[in]	password
	 *					The password of the user to be authenticated
	 * @param[in]	company
	 *					The objectid of the company to which the user belongs.
	 *					This objectid can be empty.
	 * @return The objectsignature of the authenticated user
	 * @throw runtime_error When a Database error occurred.
	 * @throw login_error When no user was found or the password was incorrect.
	 */
	virtual objectsignature_t authenticateUser(const std::string &username, const std::string &password, const objectid_t &company);

    /**
	 * Search for all objects which match the given string,
	 * the name and email address should be compared for this search.
	 *
	 * This will call DBBase::searchObjects()
	 *
	 * @param[in]   match
	 *					The string which should be found
	 * @param[in]	ulFlags
	 *					If EMS_AB_ADDRESS_LOOKUP the string must exactly match the name or email address
	 *					otherwise a partial match is allowed.
	 * @return List of object signatures which match the given string
	 * @throw std::exception
	 */
	virtual signatures_t searchObject(const std::string &match, unsigned int flags) override;

	/**
	 * Modify id of object in plugin
	 *
	 * @note This function is not supported by this plugin and will always throw an exception
	 *
	 * @param[in]	oldId
	 *					The old objectid
	 * @param[in]	newId
	 *					The new objectid
	 * @throw notsupported Always when this function is called
	 */
	virtual void modifyObjectId(const objectid_t &oldId, const objectid_t &newId);

    /**
	 * Set quota information on object
	 *
	 * This will call DBBase::setQuota()
	 *
	 * @param[in]   id
	 *                  The objectid which should be updated
	 * @param[in]   quotadetails
	 *                  The quota information which should be written to the object
	 * @throw runtime_error When a Database error occurred.
	 * @throw objectnotfound When the object was not found.
	 */
	virtual void setQuota(const objectid_t &id, const quotadetails_t &quotadetails);

    /**
	 * Obtain details for the public store
	 *
	 * @note This function is not supported by this plugin and will always throw an exception
	 *
	 * @return The public store details
	 * @throw notsupported Always when this function is called
	 */
	virtual objectdetails_t getPublicStoreDetails() override;

    /**
	 * Obtain the objectdetails for a server
	 *
	 * @note This function is not supported by this plugin and will always throw an exception
	 *
	 * @param[in]	server
	 *					The externid of the server
	 * @return The server details
	 * @throw notsupported Always when this function is called
	 */
	virtual serverdetails_t getServerDetails(const std::string &server) override;

	/**
	 * Obtain server list
	 *
	 * @return list of servers
	 * @throw runtime_error LDAP query failure
	 */
	virtual serverlist_t getServers() override;

    /**
	 * Add relation between child and parent. This can be used
	 * for example to add a user to a group or add
	 * permission relations on companies.
	 *
	 * This will call DBPlugin::addSubObjectRelation().
	 *
	 * @param[in]	relation
	 *					The relation type which should connect the
	 *					child and parent.
	 * @param[in]	parentobject
	 *					The parent object.
	 * @param[in]	childobject
	 *					The child object.
	 * @throw runtime_error When a Database error occurred
	 * @throw objectnotfound When the parent does not exist.
	 */
	virtual void addSubObjectRelation(userobject_relation_t relation,
									  const objectid_t &parentobject, const objectid_t &childobject);
};

extern "C" {
	extern _kc_export KC::UserPlugin *getUserPluginInstance(std::mutex &, KC::ECPluginSharedData *);
	extern _kc_export void deleteUserPluginInstance(KC::UserPlugin *);
	extern _kc_export unsigned long getUserPluginVersion(void);
	extern _kc_export const char kcsrv_plugin_version[];
}
/** @} */

#endif
