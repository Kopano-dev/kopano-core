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
#include <kopano/zcdefs.h>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <kopano/platform.h>

#include "ECDatabase.h"
#include "ECDatabaseUpdate.h"

#include <kopano/stringutil.h>

#include <kopano/ECDefs.h>
#include "ECDBDef.h"
#include "ECUserManagement.h"

#include <kopano/ecversion.h>

#include <mapidefs.h>
#include <mapitags.h>
#include "ECConversion.h"
#include "SOAPUtils.h"
#include "ECSearchFolders.h"

#include "ics.h"

#include <kopano/charset/convert.h>
#include "ECStringCompat.h"
#include "ECMAPI.h"

#include <zlib.h>
#include <kopano/mapiext.h>
#include <edkmdb.h>

namespace KC {

bool searchfolder_restart_required; //HACK for rebuild the searchfolders with an upgrade

/*
	database upgrade

	Version 6.30
	* Add externid column to object table (changed between betas)
	* Add reference table for Single Instance Attachments
	* Add distributed lock when upgrading to 6.30 (distributed only on clean 6.30)
	* Add abchanges table to hold ab sourcekeys (since they don't fit in the changes table anymore)
	* Set tag column in singleinstance to correct tag value (betas have wrong value)

	Version 6.40
	* Rename object_type columns to objectclass, and fix their contents with the new defined values (in 2 steps)
	* Add objectmvproperties table (for offline synced addressbook details)
	* Add syncedmessages table for keeping track of messages that were synchronized with a restriction
	* Update the primary key on the 'outgoingqueue' table
	* Update the primary key on the 'acl' table
	* Update externid in users and object table to be blob, so mysql 4.1 leaves trailing 0's
	* Update changes table primary key
	* Update mvproperties table primary key
	* Update objectclass for DB plugin groups to be security enabled
	* Update objectrelation table to switch send-as settings

	Version 7.00
	* Print error howto "convert", or if admin forced do the upgrade of tables with text fields to unicode.
	* Update stores table to store the username in a char field.
	* Update rules xml blobs to unicode.
	* Update searchfolder xml blobs to unicode.

	Version 7.0.1
	* update receive folder to unicode and increase the messageclass column size

	Version 7.1.0
	* update WLINK entries to new record key format

	Version independed
	* Add setting for IMAP
	* Change primary key in changetable and add an extra move key
	* Force addressbook resync
*/

struct SObject {
	SObject(unsigned int id, unsigned int type) {ulId = id; ulType = type;}
	bool operator<(const SObject &rhs) const noexcept
	{
		return (ulId < rhs.ulId || (ulId == rhs.ulId && ulType < rhs.ulType));
	}
	unsigned int ulId;
	unsigned int ulType;
};

struct SRelation {
	SRelation(unsigned int objectId, unsigned int parentObjectId, unsigned int relationType) {
		ulObjectId = objectId; ulParentObjectId = parentObjectId; ulRelationType = relationType;
	}
	unsigned int ulObjectId;
	unsigned int ulParentObjectId;
	unsigned int ulRelationType;
};

ECRESULT InsertServerGUID(ECDatabase *lpDatabase)
{
	GUID guid;

	if (CoCreateGuid(&guid) != S_OK) {
		ec_log_err("InsertServerGUID(): CoCreateGuid failed");
		return KCERR_DATABASE_ERROR;
	}

	return lpDatabase->DoInsert("INSERT INTO `settings` VALUES ('server_guid', " + lpDatabase->EscapeBinary(reinterpret_cast<unsigned char *>(&guid), sizeof(GUID)) + ")");
}

// 25
ECRESULT UpdateDatabaseAddExternIdToObject(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;
	DB_ROW			lpDBRow = NULL;
	DB_LENGTHS		lpDBLen = NULL;
	unsigned int	ulNewId = 0;
	unsigned int	ulNewParentId = 0;
	bool			bFirstResult;

	std::list<SObject> sObjectList;
	std::map<SObject,unsigned int> sObjectMap;
	std::map<SObject,unsigned int>::const_iterator sObjectMapIter;

	std::list<SRelation> sRelationList;
#define Z_TABLEDEF_OBJECT_R630	"CREATE TABLE object ( \
									`id` int(11) unsigned NOT NULL auto_increment, \
									`externid` varbinary(255), \
									`objecttype` int(11) unsigned NOT NULL default '0', \
									PRIMARY KEY (`id`, `objecttype`), \
									UNIQUE KEY id (`id`), \
									UNIQUE KEY externid (`externid`, `objecttype`) \
								) ENGINE=InnoDB;"

	// Create the new object table.
	std::string strQuery = Z_TABLEDEF_OBJECT_R630;
	strQuery.replace(strQuery.find("object"), 6, "object_temp");
	auto er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		goto exit;

	// Create the new objectproperty table.
	strQuery = Z_TABLEDEF_OBJECT_PROPERTY;
	strQuery.replace(strQuery.find("objectproperty"), 14, "objectproperty_temp");
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		goto exit;

	// Create the new objectrelation table.
	strQuery = Z_TABLEDEF_OBJECT_RELATION;
	strQuery.replace(strQuery.find("objectrelation"), 14, "objectrelation_temp");
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		goto exit;

	// Create a list of all current objects from the object table.
	strQuery = "SELECT id, objecttype FROM object ORDER BY objecttype";
	er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		goto exit;

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL) {
			er = KCERR_DATABASE_ERROR;
			ec_log_err("  object table contains invalid NULL records");
			goto exit;
		}
		sObjectList.emplace_back(atoi(lpDBRow[0]), atoi(lpDBRow[1]));
	}

	// Recreate the objects in the object_temp table and on the fly create the queries to regenerate
	// their properties in the objectpropert_temp table.
	for (const auto &obj : sObjectList) {
		strQuery = "INSERT INTO object_temp (objecttype, externid) VALUES (" +
		           stringify(obj.ulType) + ", '" + stringify(obj.ulId) + "')";
		er = lpDatabase->DoInsert(strQuery, &ulNewId);
		if (er != erSuccess)
			goto exit;

		// Add to the map for later use
		sObjectMap[obj] = ulNewId;

		// Find the properties for this object
		strQuery = "SELECT propname, value FROM objectproperty WHERE objectid=" +
		           stringify(obj.ulId);
		er = lpDatabase->DoSelect(strQuery, &lpResult);
		if (er != erSuccess)
			goto exit;

		strQuery.clear();
		bFirstResult = true;
		while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
			lpDBLen = lpResult.fetch_row_lengths();
			if (lpDBLen == NULL) {
				er = KCERR_DATABASE_ERROR;
				ec_log_err("UpdateDatabaseAddExternIdToObject(): FetchRowLengths failed");
				goto exit;
			}

			if (lpDBRow[0] == NULL) {
				er = KCERR_DATABASE_ERROR;
				ec_log_err("UpdateDatabaseAddExternIdToObject(): column NULL");
				goto exit;
			}

			if (strQuery.empty())
				strQuery = "INSERT INTO objectproperty_temp (objectid, propname, value) VALUES ";

			if (!bFirstResult)
				strQuery += ",";
			else
				bFirstResult = false;

			strQuery += 
				"(" + stringify(ulNewId) + "," + 
				lpDatabase->EscapeBinary((unsigned char*)lpDBRow[0], lpDBLen[0]) + ",";

			if (lpDBRow[1] == NULL)
				strQuery += "NULL)";
			else
				strQuery += lpDatabase->EscapeBinary((unsigned char*)lpDBRow[1], lpDBLen[1]) + ")";
		}

		if (!strQuery.empty()) {
			er = lpDatabase->DoInsert(strQuery);
			if (er != erSuccess)
				goto exit;
		}

		er = lpDatabase->DoDelete("DELETE FROM objectproperty WHERE objectid=" + stringify(obj.ulId));
		if (er != erSuccess)
			goto exit;
	}

	// Now repopulate the objectrelation table.
	strQuery = "SELECT objectid, parentobjectid, relationtype FROM objectrelation";
	er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		goto exit;

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
			er = KCERR_DATABASE_ERROR;
			ec_log_crit("  objectrelation table contains invalid NULL records");
			goto exit;
		}
		sRelationList.emplace_back(atoi(lpDBRow[0]), atoi(lpDBRow[1]), atoi(lpDBRow[2]));
	}

	strQuery.clear();
	bFirstResult = true;
	for (const auto &rel : sRelationList) {
		// Find the new parentId, if not found: ignore so they disappear .. would have been invalid relations anyway.
		switch (rel.ulRelationType) {
		case OBJECTRELATION_QUOTA_USERRECIPIENT:
		case OBJECTRELATION_USER_SENDAS:
			sObjectMapIter = sObjectMap.find(SObject(rel.ulParentObjectId, 1 /* USEROBJECT_TYPE_USER */));
			if (sObjectMapIter == sObjectMap.cend())
				sObjectMapIter = sObjectMap.find(SObject(rel.ulParentObjectId, 5 /* USEROBJECT_TYPE_NONACTIVE */));
			if (sObjectMapIter == sObjectMap.cend())
				continue;
			ulNewParentId = sObjectMapIter->second;
			break;

		case OBJECTRELATION_GROUP_MEMBER:
			sObjectMapIter = sObjectMap.find(SObject(rel.ulParentObjectId, 2 /* USEROBJECT_TYPE_GROUP */));
			if (sObjectMapIter == sObjectMap.cend())
				continue;
			ulNewParentId = sObjectMapIter->second;
			break;

		case OBJECTRELATION_COMPANY_VIEW:
		case OBJECTRELATION_COMPANY_ADMIN:
		case OBJECTRELATION_QUOTA_COMPANYRECIPIENT:
			sObjectMapIter = sObjectMap.find(SObject(rel.ulParentObjectId, 4 /* USEROBJECT_TYPE_COMPANY */));
			if (sObjectMapIter == sObjectMap.cend())
				continue;
			ulNewParentId = sObjectMapIter->second;
			break;

		case OBJECTRELATION_ADDRESSLIST_MEMBER:
			sObjectMapIter = sObjectMap.find(SObject(rel.ulParentObjectId, 6 /* USEROBJECT_TYPE_ADDRESSLIST */));
			if (sObjectMapIter == sObjectMap.cend())
				continue;
			ulNewParentId = sObjectMapIter->second;
			break;
		}

		// Find the new object id
		sObjectMapIter = sObjectMap.find(SObject(rel.ulObjectId, 1 /* USEROBJECT_TYPE_USER */));
		if (sObjectMapIter == sObjectMap.cend())
			sObjectMapIter = sObjectMap.find(SObject(rel.ulObjectId, 5)); // USEROBJECT_TYPE_NONACTIVE
		if (sObjectMapIter == sObjectMap.cend())
			continue;
		ulNewId = sObjectMapIter->second;

		// Update strQuery for this relation
		if (strQuery.empty())
			strQuery = "INSERT INTO objectrelation_temp (objectid,parentobjectid,relationtype) VALUES ";

		if (!bFirstResult)
			strQuery += ",";
		else
			bFirstResult = false;

		strQuery += "(" + stringify(ulNewId) + "," +
		            stringify(ulNewParentId) + "," +
		            stringify(rel.ulRelationType) + ")";
	}

	if (!strQuery.empty()) {
		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			goto exit;
	}

	// Now delete the old tables and rename the new ones
	er = lpDatabase->DoDelete("DROP TABLE object, objectproperty, objectrelation");
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->DoUpdate("RENAME TABLE object_temp TO object, "
										   "objectproperty_temp TO objectproperty, "
										   "objectrelation_temp TO objectrelation");

exit:
	// Delete the temporary tables if they exist at this point
	lpDatabase->DoDelete("DROP TABLE IF EXISTS object_temp, objectproperty_temp, objectrelation_temp");
	return er;
}

// 26
ECRESULT UpdateDatabaseCreateReferences(ECDatabase *lpDatabase)
{
	auto er = lpDatabase->DoInsert(Z_TABLEDEF_REFERENCES);
	if (er != erSuccess)
		return er;

	/* 
	 * Create all attachment references from hierarchy table, let
	 * instanceid be equal to hierarchyid to minimize the impact
	 * on the upgrade.
	 */
	std::string strQuery =
		"INSERT INTO `singleinstances` (`instanceid`, `hierarchyid`, `tag`) "
			"SELECT id, id, " + stringify(PROP_ID(PR_ATTACH_DATA_BIN)) + " "
			"FROM `hierarchy` "
			"WHERE type = " + stringify(MAPI_ATTACH);

	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		return er;

	/* We need to rename the column in `lob` */
	strQuery =
		"ALTER TABLE `lob` "
		"CHANGE COLUMN `hierarchyid` `instanceid` int(11) unsigned NOT NULL";
	return lpDatabase->DoUpdate(strQuery);
}

// 28
ECRESULT UpdateDatabaseCreateABChangesTable(ECDatabase *lpDatabase)
{
	std::string strQuery, strSyncId;
	DB_RESULT lpResult;
	DB_ROW			lpDBRow = NULL;
	DB_LENGTHS		lpDBLen = NULL;
	int				ulId = 0;
	std::list<int> syncIds;
	std::list<std::string> queries;
	bool			fFirst = true;

	auto er = lpDatabase->DoInsert(Z_TABLEDEF_ABCHANGES);
	if (er != erSuccess)
		goto exit;
		
	strQuery = "SELECT id, sourcekey, parentsourcekey, change_type FROM changes WHERE change_type & " + stringify(ICS_AB) + " AND parentsourcekey=0x00000000AC21A95040D3EE48B319FBA75330442500000000040000000100000000000000";
	er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		goto exit;

	// Extract the AB changes from the changes table.
	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		lpDBLen = lpResult.fetch_row_lengths();
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBLen[1] == 0 || lpDBRow[2] == NULL || lpDBLen[2] == 0) {
			er = KCERR_DATABASE_ERROR;
			ec_log_crit("  changes table contains invalid NULL records");
			goto exit;
		}

		ulId = atoi(lpDBRow[0]);
		syncIds.emplace_back(ulId);
		strQuery = "INSERT INTO abchanges (id, sourcekey, parentsourcekey, change_type";
		strQuery += std::string(") VALUES (") + lpDBRow[0] + ", " +
								   lpDatabase->EscapeBinary((unsigned char*)lpDBRow[1], lpDBLen[1]) + ", " +
								   lpDatabase->EscapeBinary((unsigned char*)lpDBRow[2], lpDBLen[2]) + ", " +
								   lpDBRow[3];
		strQuery += ")";
		queries.emplace_back(std::move(strQuery));
	}
	
	// Populate the abchanges table with the extracted data
	for (const auto &query : queries) {
		er = lpDatabase->DoInsert(query);
		if (er != erSuccess)
			goto exit;
	}

	// Remove the extracted changes from the changes table
	strQuery = "DELETE FROM changes WHERE id IN (";
	for (auto id : syncIds) {
		strSyncId = stringify(id, false);
		
		if (strQuery.length() + strSyncId.length() + 2 >= lpDatabase->GetMaxAllowedPacket()) {	// we need to be able to add a ',' and a ')';
			strQuery += ")";
			er = lpDatabase->DoInsert(strQuery);
			if (er != erSuccess)
				goto exit;
				
			strQuery = "DELETE FROM changes WHERE id IN (";
			fFirst = true;
		}
			
		if (!fFirst)
			strQuery += ",";
		fFirst = false;

		strQuery += strSyncId;
	}
	if (!fFirst) {
		strQuery += ")";
		er = lpDatabase->DoInsert(strQuery);
	}
	
exit:
	if (er != erSuccess)
		lpDatabase->DoDelete("DROP TABLE IF EXISTS abchanges");

	return er;
}

// 29
ECRESULT UpdateDatabaseSetSingleinstanceTag(ECDatabase *lpDatabase)
{
	// Force all tag values to PR_ATTACH_DATA_BIN. Up to now, no other values can be present in the table.
	return lpDatabase->DoUpdate("UPDATE `singleinstances` SET `tag` = " + stringify(PROP_ID(PR_ATTACH_DATA_BIN)));
}

// 30
ECRESULT UpdateDatabaseCreateSyncedMessagesTable(ECDatabase *lpDatabase)
{
	return lpDatabase->DoInsert(Z_TABLEDEFS_SYNCEDMESSAGES);
}

// 31
ECRESULT UpdateDatabaseForceAbResync(ECDatabase *lpDatabase)
{
	return KCERR_IGNORE_ME;
}

// 32
ECRESULT UpdateDatabaseRenameObjectTypeToObjectClass(ECDatabase *lpDatabase)
{
	// rename columns in users and object tables
	std::string strQuery =
		"ALTER TABLE `users` "
		"CHANGE COLUMN `object_type` `objectclass` int(11) unsigned NOT NULL";
	auto er = lpDatabase->DoUpdate(strQuery);
	if (er != erSuccess)
		return er;

	// Note: type also changes from int to tinyint here
	strQuery =
		"ALTER TABLE `object` "
		"CHANGE COLUMN `objecttype` `objectclass` int(11) unsigned NOT NULL";
	return lpDatabase->DoUpdate(strQuery);
}

// 33
ECRESULT UpdateDatabaseConvertObjectTypeToObjectClass(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;
	DB_LENGTHS	lpDBLen = NULL;
	std::string strQuery, strUpdate;
	bool bFirst = true;
	std::map<unsigned int, unsigned int> mapTypes;
	std::list<std::string> lstUpdates;

	// make internal SYSTEM a objectclass_t user
	auto er = lpDatabase->DoUpdate("UPDATE `users` SET `objectclass` = " + stringify(ACTIVE_USER) + " WHERE `externid` is NULL AND `objectclass` = 1");
	if (er != erSuccess)
		return er;

	// make internal EVERYONE a objectclass_t security group
	er = lpDatabase->DoUpdate("UPDATE `users` SET `objectclass` = "+stringify(DISTLIST_SECURITY)+" WHERE `externid` is NULL AND `objectclass` = 2");
	if (er != erSuccess)
		return er;

	// database stored typed, convert to the new objectclass_t values
	mapTypes.emplace(1, ACTIVE_USER); // USEROBJECT_TYPE_USER
	mapTypes.emplace(2, DISTLIST_GROUP); // USEROBJECT_TYPE_GROUP
	mapTypes.emplace(3, NONACTIVE_CONTACT); // USEROBJECT_TYPE_CONTACT (unused, but who knows..)
	mapTypes.emplace(4, CONTAINER_COMPANY); // USEROBJECT_TYPE_COMPANY
	mapTypes.emplace(5, NONACTIVE_USER); // USEROBJECT_TYPE_NONACTIVE
	mapTypes.emplace(6, CONTAINER_ADDRESSLIST); // USEROBJECT_TYPE_ADDRESSLIST

	for (const auto &p : mapTypes) {
		// extern id, because it links to object table for DB plugin
		// on LDAP plugin, object table is empty.
		er = lpDatabase->DoSelect("SELECT `externid`, `objectclass` FROM `users` WHERE `externid` is not NULL AND `objectclass` = " + stringify(p.first), &lpResult);
		if (er != erSuccess)
			return er;

		strUpdate = "(";
		bFirst = true;
		while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
			lpDBLen = lpResult.fetch_row_lengths();
			if (lpDBRow[0] == NULL || lpDBLen == NULL || lpDBLen[0] == 0) {
				ec_log_crit("  users table contains invalid NULL records for type %d", p.first);
				return KCERR_DATABASE_ERROR;
			}

			if (!bFirst)
				strUpdate += ",";

			strUpdate += lpDatabase->EscapeBinary((unsigned char*)lpDBRow[0], lpDBLen[0]);
			bFirst = false;
		}
		strUpdate += ")";

		if (bFirst)
			continue;			// we had no results for this type, continue with the next

		// save all queries in a list, so we don't cross-update types

		strQuery =
			"UPDATE `users` SET `objectclass`=" + stringify(p.second) + " "
			"WHERE `externid` IN " + strUpdate + " "
			"AND `objectclass` = " + stringify(p.first);
		lstUpdates.emplace_back(std::move(strQuery));
		strQuery =
			"UPDATE `object` SET `objectclass`=" + stringify(p.second) + " "
			"WHERE `externid` IN " + strUpdate + " "
			"AND `objectclass` = " + stringify(p.first);
		lstUpdates.emplace_back(std::move(strQuery));
	}

	// process all type updates
	for (const auto &q : lstUpdates) {
		er = lpDatabase->DoUpdate(q);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

// 34
ECRESULT UpdateDatabaseAddMVPropertyTable(ECDatabase *lpDatabase)
{
	return lpDatabase->DoInsert(Z_TABLEDEF_OBJECT_MVPROPERTY);
}

// 35
ECRESULT UpdateDatabaseCompanyNameToCompanyId(ECDatabase *lpDatabase)
{
	std::map<std::string, std::string> mapIdToName;
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;
	DB_LENGTHS	lpDBLen = NULL;

	// find all companies
	std::string strQuery = "SELECT object.externid, objectproperty.value FROM objectproperty JOIN object ON objectproperty.objectid=object.id WHERE objectproperty.propname = 'companyname'";
	auto er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return er;

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL)
			continue;
		lpDBLen = lpResult.fetch_row_lengths();
		mapIdToName.emplace(std::string(lpDBRow[0], lpDBLen[0]), std::string(lpDBRow[1], lpDBLen[1]));
	}

	// update objects to link via externid in companyid, not companyname anymore
	for (const auto &p : mapIdToName) {
		strQuery = "UPDATE objectproperty SET value = 0x" + bin2hex(p.first) +
			" WHERE propname='companyid' AND value = '" + p.second + "'";
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

// 36
ECRESULT UpdateDatabaseOutgoingQueuePrimarykey(ECDatabase *lpDatabase)
{
	return lpDatabase->DoUpdate("ALTER TABLE outgoingqueue DROP PRIMARY KEY, ADD PRIMARY KEY (`hierarchy_id`,`flags`,`store_id`)");
}

// 37
ECRESULT UpdateDatabaseACLPrimarykey(ECDatabase *lpDatabase)
{
	return lpDatabase->DoUpdate("ALTER TABLE acl DROP PRIMARY KEY, ADD PRIMARY KEY (`hierarchy_id`,`id`,`type`)");
}

// 38
ECRESULT UpdateDatabaseBlobExternId(ECDatabase *lpDatabase)
{
	std::string strQuery = "ALTER TABLE `object` "
				"DROP KEY `externid`, "
				"MODIFY `externid` blob, "
				"ADD UNIQUE KEY `externid` (`externid`(255), `objectclass`)";
	auto er = lpDatabase->DoUpdate(strQuery);
	if (er != erSuccess)
		return er;

	strQuery = "ALTER TABLE `users` "
				"DROP KEY `externid`, "
				"MODIFY `externid` blob, "
				"ADD UNIQUE KEY `externid` (`externid`(255), `objectclass`)";
	return lpDatabase->DoUpdate(strQuery);
}

// 39
ECRESULT UpdateDatabaseKeysChanges2(ECDatabase *lpDatabase)
{
	// Change index
	return lpDatabase->DoUpdate("ALTER TABLE changes DROP PRIMARY KEY, ADD PRIMARY KEY(`parentsourcekey`,`sourcekey`,`change_type`)");
}

/**
 * Update the primary key in mvproperties
 *
 * Change the primary key to get more performance in mysql because the mysql can not 
 * choose the right key we change the index into one primary key.
 *
 * @remarks We are checking extra for the 'hi' key because some upgrade issues
 *
 * @param[in]	lpDatabase	ECDatabase object pointer to update.
 * @retval		erSuccess 
 *					Update is done.
 * @retval		KCERR_DATABASE_ERROR
 *					Update failed
 */
// 40
ECRESULT UpdateDatabaseMVPropertiesPrimarykey(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;
	bool		bUpdate = false;

	auto er = lpDatabase->DoSelect("SHOW KEYS FROM mvproperties", &lpResult);
	if (er != erSuccess)
		return er;

	// Result: | Table | Non_unique | Key_name | Seq_in_index | Column_name | Collation | Cardinality | Sub_part | Packed | Null | Index_type | Comment |
	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL)
			continue;

		if(strcasecmp(lpDBRow[2], "hi") == 0) {
			bUpdate = true;
			break;
		}
	}

	if (bUpdate) {
		er = lpDatabase->DoUpdate("ALTER TABLE mvproperties DROP PRIMARY KEY, ADD PRIMARY KEY (`hierarchyid`,`tag`,`type`,`orderid`), DROP KEY `hi`");
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

// 41
ECRESULT UpdateDatabaseFixDBPluginGroups(ECDatabase *lpDatabase)
{
	return lpDatabase->DoUpdate("UPDATE object SET objectclass="+stringify(DISTLIST_SECURITY)+" WHERE objectclass="+stringify(DISTLIST_GROUP));
}

// 42
ECRESULT UpdateDatabaseFixDBPluginSendAs(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;
	std::list<std::pair<std::string, std::string>> lstRelations;

	// relation 6 == OBJECTRELATION_USER_SENDAS
	auto er = lpDatabase->DoSelect("SELECT objectid, parentobjectid FROM objectrelation WHERE relationtype=6", &lpResult);
	if (er != erSuccess)
		return er;

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL)
			continue;
		auto lpDBLen = lpResult.fetch_row_lengths();
		lstRelations.emplace_back(std::string(lpDBRow[0], lpDBLen[0]), std::string(lpDBRow[1], lpDBLen[1]));
	}

	er = lpDatabase->DoDelete("DELETE FROM objectrelation WHERE relationtype=6");
	if (er != erSuccess)
		return er;

	for (const auto &p : lstRelations) {
		er = lpDatabase->DoUpdate("INSERT INTO objectrelation (objectid, parentobjectid, relationtype) VALUES (" +
		     p.second + ", " + p.first + ", 6)");
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

/**
 * IMAP used to store subscriptions on the store. This gave problems
 * when multi-threaded IMAP clients (Thunderbird) subscribed on
 * folders in one thread, and requested the list on another
 * thread. This would have returned the old subscribed list, since the
 * store doesn't have an update notification system like normal
 * folders do.  Moved to the Inbox, since this folder is always
 * present and easy to find on the server and client.
 *
 * @param[in]	lpDatabase	ECDatabase object pointer to update.
 * @return		ECRESULT	erSuccess or KCERR_DATABASE_ERROR
 */
// 43
ECRESULT UpdateDatabaseMoveSubscribedList(ECDatabase *lpDatabase)
{
	std::map<std::string, std::string> mapStoreInbox;
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;

	auto er = lpDatabase->DoSelect("SELECT storeid, objid FROM receivefolder WHERE messageclass='IPM'", &lpResult);
	if (er != erSuccess)
		return er;

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL)
			continue;
		auto lpDBLen = lpResult.fetch_row_lengths();
		mapStoreInbox.emplace(std::string(lpDBRow[0], lpDBLen[0]), std::string(lpDBRow[1], lpDBLen[1]));
	}

	for (const auto &p : mapStoreInbox) {
		// Remove property if it's already there (possible if you run new gateway against old server before upgrade)
		er = lpDatabase->DoDelete("DELETE FROM properties WHERE storeid=" +
		     p.first + " AND hierarchyid=" + p.first +
		     " AND tag=0x6784 AND type=0x0102");
		if (er != erSuccess)
			return er;

		// does not return an error if property was not in the database
		er = lpDatabase->DoUpdate("UPDATE properties SET hierarchyid=" +
		     p.second + " WHERE storeid=" + p.first +
		     " AND hierarchyid=" + p.first +
		     " AND tag=0x6784 AND type=0x0102");
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

// 44
ECRESULT UpdateDatabaseSyncTimeIndex(ECDatabase *lpDatabase)
{
	bool bHaveIndex;

	// There are upgrade paths where the sync_time key already exists.
	auto er = lpDatabase->CheckExistIndex("syncs", "sync_time", &bHaveIndex);
	if (er == erSuccess && !bHaveIndex)
		er = lpDatabase->DoUpdate("ALTER TABLE syncs ADD INDEX sync_time (`sync_time`)");

	return er;
}

// 45
ECRESULT UpdateDatabaseAddStateKey(ECDatabase *lpDatabase)
{
	bool bHaveIndex;

	// There are upgrade paths where the state key already exists.
	auto er = lpDatabase->CheckExistIndex("changes", "state", &bHaveIndex);
	if (er == erSuccess && !bHaveIndex)
		er = lpDatabase->DoUpdate("ALTER TABLE changes ADD UNIQUE KEY `state` (`parentsourcekey`,`id`)");
	return er;
}

// 46
ECRESULT UpdateDatabaseConvertToUnicode(ECDatabase *lpDatabase)
{
	if (lpDatabase->m_bForceUpdate) {
		// Admin requested a forced upgrade, converting known tables
		
		/*
		 * Since we inserted the company guid into the objectproperty
		 * table, this convert may break, since the binary data won't
		 * be valid utf-8 data in mysql. So we convert the 'companyid'
		 * properties from this table into a hexed version, which is
		 * plain text and will not break.
		 *
		 * We need to do this first, since the begin/commit will work
		 * on this statement, and won't on the following alter table
		 * commands.
		 */
		std::string strQuery = "UPDATE objectproperty SET value = hex(value) WHERE propname = 'companyid'";
		auto er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			return er;

		// Convert tables to unicode

		strQuery = "ALTER TABLE mvproperties MODIFY val_string longtext CHARSET utf8 COLLATE utf8_general_ci";
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			return er;

		// No need to convert the properties table as that will be done on the fly in update 50 (Z_UPDATE_CONVERT_PROPERTIES)

		// db-plugin
		strQuery = "ALTER TABLE objectproperty MODIFY propname VARCHAR(255) CHARSET utf8 COLLATE utf8_general_ci, MODIFY value TEXT CHARSET utf8 COLLATE utf8_general_ci";
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			return er;
		/*
		 * Another similar change is to the SYSADMIN property; it used
		 * to be 12345:XXXXXXXX with XXXXX being a binary externid. That
		 * has changed to be a hexed version, so, 12345:HHHHHHHHHH, with
		 * HHHH being the hexed version of XXXXX
		 */
		strQuery = "UPDATE objectproperty SET value = concat(substr(value,1,instr(value,';')-1),';',hex(substr(value,instr(value,';')+1))) WHERE propname = 'companyadmin'";
		er = lpDatabase->DoUpdate(strQuery);
		if (er != erSuccess)
			return er;
		strQuery = "ALTER TABLE objectmvproperty MODIFY propname VARCHAR(255) CHARSET utf8 COLLATE utf8_general_ci, MODIFY value TEXT CHARSET utf8 COLLATE utf8_general_ci";
		return lpDatabase->DoUpdate(strQuery);
		/*
		 * Other tables containing varchar's are not converted, all data in those fields are us-ascii anyway:
		 * - receivefolder
		 * - stores (specially handled in next update
		 * - settings
		 */
	} else {
		ec_log_crit("Will not upgrade your database from Zarafa 6.40.x.");
		ec_log_crit("The recommended upgrade procedure is to first upgrade by first upgrading to ZCP 7.2 and using the zarafa7-upgrade commandline tool.");
		ec_log_crit("Please consult the Zarafa and Kopano administrator manual on how to correctly upgrade your database.");
		ec_log_crit("Alternatively you may try to upgrade using --force-database-upgrade,");
		ec_log_crit("but no progress and estimates within the updates will be available.");
		return KCERR_USER_CANCEL;
	}
}

// 47
ECRESULT UpdateDatabaseConvertStoreUsername(ECDatabase *lpDatabase)
{
	ECRESULT er = erSuccess;
	er = lpDatabase->DoUpdate("UPDATE stores SET user_name = CAST(CONVERT(user_name USING latin1) AS CHAR(255) CHARACTER SET utf8)");
	if (er == erSuccess)
		er = lpDatabase->DoUpdate("ALTER TABLE stores MODIFY user_name VARCHAR(255) CHARACTER SET utf8 NOT NULL DEFAULT ''");
	return er;
}

// 48
ECRESULT UpdateDatabaseConvertRules(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;

	convert_context converter;

	auto er = lpDatabase->DoSelect("SELECT p.hierarchyid, p.storeid, p.val_binary FROM properties AS p JOIN receivefolder AS r ON p.hierarchyid=r.objid AND p.storeid=r.storeid JOIN stores AS s ON r.storeid=s.hierarchy_id WHERE p.tag=0x3fe1 AND p.type=0x102 AND r.messageclass='IPM'", &lpResult);
	if (er != erSuccess)
		return er;

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
			ec_log_err("UpdateDatabaseConvertRules(): column NULL");
			return KCERR_DATABASE_ERROR;
		}

		// Use WTF-1252 here since the pre-unicode rule serializer didn't pass the SOAP_C_UTFSTRING flag, causing
		// gsoap to encode the data as UTF8, eventhough it was already encoded as WINDOWS-1252.
		std::unique_ptr<char[]> lpszConverted(ECStringCompat::WTF1252_to_UTF8(nullptr, lpDBRow[2], &converter));
		er = lpDatabase->DoUpdate("UPDATE properties SET val_binary='" + lpDatabase->Escape(lpszConverted.get()) + "' WHERE hierarchyid=" + lpDBRow[0] + " AND storeid=" + lpDBRow[1] + " AND tag=0x3fe1 AND type=0x102");
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

// 49
ECRESULT UpdateDatabaseConvertSearchFolders(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;

	convert_context converter;
	std::string strQuery = "SELECT h.id, p.storeid, p.val_string FROM hierarchy AS h JOIN properties AS p ON p.hierarchyid=h.id AND p.tag=" + stringify(PROP_ID(PR_EC_SEARCHCRIT)) +" AND p.type=" + stringify(PROP_TYPE(PR_EC_SEARCHCRIT)) + " WHERE h.type=3 AND h.flags=2";
	auto er = lpDatabase->DoSelect(strQuery, &lpResult);
	if (er != erSuccess)
		return er;

	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
			ec_log_err("UpdateDatabaseConvertSearchFolders(): column NULL");
			return KCERR_DATABASE_ERROR;
		}

		// Use WTF-1252 here since the pre-unicode rule serializer didn't pass the SOAP_C_UTFSTRING flag, causing
		// gsoap to encode the data as UTF8, eventhough it was already encoded as WINDOWS-1252.
		std::unique_ptr<char[]> lpszConverted(ECStringCompat::WTF1252_to_WINDOWS1252(nullptr, lpDBRow[2], &converter));
		er = lpDatabase->DoUpdate("UPDATE properties SET val_string='" + lpDatabase->Escape(lpszConverted.get()) + "' WHERE hierarchyid=" + lpDBRow[0] + " AND storeid=" + lpDBRow[1] + " AND tag=" + stringify(PROP_ID(PR_EC_SEARCHCRIT)) +" AND type=" + stringify(PROP_TYPE(PR_EC_SEARCHCRIT)));
		if (er != erSuccess)
			return er;
	}
	return erSuccess;
}

// 50
ECRESULT UpdateDatabaseConvertProperties(ECDatabase *lpDatabase)
{
	DB_RESULT lpResult;

	// Create the temporary properties table
	std::string strQuery = Z_TABLEDEF_PROPERTIES;
	strQuery.replace(strQuery.find("CREATE TABLE"), strlen("CREATE TABLE"), "CREATE TABLE IF NOT EXISTS");
	strQuery.replace(strQuery.find("properties"), strlen("properties"), "properties_temp");
	auto er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		return er;

	while (true) {
		strQuery = "INSERT IGNORE INTO properties_temp (hierarchyid,tag,type,val_ulong,val_string,val_binary,val_double,val_longint,val_hi,val_lo) SELECT hierarchyid,tag,type,val_ulong,val_string,val_binary,val_double,val_longint,val_hi,val_lo FROM properties ORDER BY hierarchyid ASC LIMIT 10000";
		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			return er;
		strQuery = "DELETE FROM properties ORDER BY hierarchyid ASC LIMIT 10000";
		er = lpDatabase->DoDelete(strQuery);
		if (er != erSuccess)
			return er;
		er = lpDatabase->Commit();
		if (er != erSuccess)
			return er;
		er = lpDatabase->Begin();
		if (er != erSuccess)
			return er;
		strQuery = "SELECT MIN(hierarchyid) FROM properties";
		er = lpDatabase->DoSelect(strQuery, &lpResult);
		if (er != erSuccess)
			return er;
		auto lpDBRow = lpResult.fetch_row();
		if (lpDBRow == NULL || lpDBRow[0] == NULL)
			break;
	}

	// update webaccess settings which were already utf8 in our latin1 table
	strQuery = "UPDATE properties_temp JOIN hierarchy ON properties_temp.hierarchyid=hierarchy.id AND hierarchy.parent IS NULL SET val_string = CAST(CAST(CONVERT(val_string USING latin1) AS binary) AS CHAR CHARACTER SET utf8) WHERE properties_temp.type=0x1e AND properties_temp.tag=26480";
	er = lpDatabase->DoUpdate(strQuery);
	if (er != erSuccess)
		return er;
	er = lpDatabase->DoUpdate("RENAME TABLE properties TO properties_old, properties_temp TO properties");
	if (er != erSuccess)
		return er;
	return lpDatabase->DoDelete("DROP TABLE properties_old");
}

// 51
ECRESULT UpdateDatabaseCreateCounters(ECDatabase *lpDatabase)
{
	static const struct {
		ULONG ulPropTag;
		ULONG ulChildType;
		ULONG ulChildFlagMask;
		ULONG ulChildFlags;
		const char* lpszValue;
	} counter_info[] = {
		{ PR_CONTENT_COUNT,				MAPI_MESSAGE,	MAPI_ASSOCIATED|MSGFLAG_DELETED,				0,									"COUNT(*)" },
		{ PR_CONTENT_UNREAD,			MAPI_MESSAGE,	MAPI_ASSOCIATED|MSGFLAG_DELETED|MSGFLAG_READ,	0,									"SUM(IF(flags&1,0,1))" },
		{ PR_ASSOC_CONTENT_COUNT,		MAPI_MESSAGE,	MAPI_ASSOCIATED|MSGFLAG_DELETED,				MAPI_ASSOCIATED,					"0" },
		{ PR_DELETED_MSG_COUNT,			MAPI_MESSAGE,	MAPI_ASSOCIATED|MSGFLAG_DELETED,				MSGFLAG_DELETED,					"0" },
		{ PR_DELETED_ASSOC_MSG_COUNT,	MAPI_MESSAGE,	MAPI_ASSOCIATED|MSGFLAG_DELETED,				MAPI_ASSOCIATED|MSGFLAG_DELETED,	"0" },
		{ PR_SUBFOLDERS,				MAPI_FOLDER,	MSGFLAG_DELETED,								0,									"0" },
		{ PR_FOLDER_CHILD_COUNT,		MAPI_FOLDER,	MSGFLAG_DELETED,								0,									"0" },
		{ PR_DELETED_FOLDER_COUNT,		MAPI_FOLDER,	MSGFLAG_DELETED,								MSGFLAG_DELETED,					"0" }
	};

	for (unsigned i = 0; i < 8; ++i) {
		std::string strQuery = "REPLACE INTO properties(hierarchyid,tag,type,val_ulong) "
						"SELECT parent.id,"+stringify(PROP_ID(counter_info[i].ulPropTag))+","+stringify(PROP_TYPE(counter_info[i].ulPropTag))+",count(child.id) "
						"FROM hierarchy AS parent "
							"LEFT JOIN hierarchy AS child ON parent.id=child.parent AND "
														"parent.type=3 and child.type="+stringify(counter_info[i].ulChildType)+" AND "
														"child.flags & "+stringify(counter_info[i].ulChildFlagMask)+"="+stringify(counter_info[i].ulChildFlags)+" "
						"GROUP BY parent.id";
		auto er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			return er;
		strQuery =	"REPLACE INTO properties(hierarchyid,tag,type,val_ulong) "
						"SELECT folderid,"+stringify(PROP_ID(counter_info[i].ulPropTag))+","+stringify(PROP_TYPE(counter_info[i].ulPropTag))+","+counter_info[i].lpszValue+" FROM searchresults GROUP BY folderid";
		er = lpDatabase->DoInsert(strQuery);
		if (er != erSuccess)
			return er;
	}
	return hrSuccess;
}

// 52
ECRESULT UpdateDatabaseCreateCommonProps(ECDatabase *lpDatabase)
{
	std::string strQuery = "REPLACE INTO properties(hierarchyid,tag,type,val_hi,val_lo,val_ulong) "
					"SELECT h.id,"+stringify(PROP_ID(PR_CREATION_TIME))+","+stringify(PROP_TYPE(PR_CREATION_TIME))+",(UNIX_TIMESTAMP(h.createtime) * 10000000 + 116444736000000000) >> 32,(UNIX_TIMESTAMP(h.createtime) * 10000000 + 116444736000000000) & 0xffffffff, NULL "
					"FROM hierarchy AS h "
						"WHERE h.type IN (3,5,7)";
	auto er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		return er;
	strQuery =	"REPLACE INTO properties(hierarchyid,tag,type,val_hi,val_lo,val_ulong) "
					"SELECT h.id,"+stringify(PROP_ID(PR_LAST_MODIFICATION_TIME))+","+stringify(PROP_TYPE(PR_LAST_MODIFICATION_TIME))+",(UNIX_TIMESTAMP(h.modtime) * 10000000 + 116444736000000000) >> 32,(UNIX_TIMESTAMP(h.modtime) * 10000000 + 116444736000000000) & 0xffffffff, NULL "
					"FROM hierarchy AS h "
						"WHERE h.type IN (3,5,7)";
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		return er;
	strQuery =	"REPLACE INTO properties(hierarchyid,tag,type,val_hi,val_lo,val_ulong) "
					"SELECT h.id,"+stringify(PROP_ID(PR_MESSAGE_FLAGS))+","+stringify(PROP_TYPE(PR_MESSAGE_FLAGS))+",NULL, NULL, h.flags "
					"FROM hierarchy AS h "
						"WHERE h.type IN (3,5,7)";
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		return er;
	strQuery =	"REPLACE INTO properties(hierarchyid,tag,type,val_hi,val_lo,val_ulong) "
					"SELECT h.id,"+stringify(PROP_ID(PR_FOLDER_TYPE))+","+stringify(PROP_TYPE(PR_FOLDER_TYPE))+",NULL, NULL, h.flags & 0x3 "
					"FROM hierarchy AS h "
						"WHERE h.type=3";
	return lpDatabase->DoInsert(strQuery);
}

// 53
ECRESULT UpdateDatabaseCheckAttachments(ECDatabase *lpDatabase)
{
	std::string strQuery = "REPLACE INTO properties(hierarchyid,tag,type,val_ulong) "
					"SELECT h.id,"+stringify(PROP_ID(PR_HASATTACH))+","+stringify(PROP_TYPE(PR_HASATTACH))+",IF(att.id,1,0) "
						"FROM hierarchy AS h "
							"LEFT JOIN hierarchy AS att ON h.id=att.parent AND att.type=7 AND h.type=5 "
					"GROUP BY h.id";
	auto er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		return er;
	strQuery =	"UPDATE properties AS p "
					"JOIN hierarchy AS h ON p.hierarchyid=h.id AND h.type=5 "
					"LEFT JOIN hierarchy AS c ON c.type=7 AND c.parent=p.hierarchyid "
				"SET p.val_ulong = IF(c.id,p.val_ulong|"+stringify(MSGFLAG_DELETED)+", p.val_ulong & ~"+stringify(MSGFLAG_DELETED)+") "
				"WHERE p.tag="+stringify(PROP_ID(PR_MESSAGE_FLAGS))+" AND p.type="+stringify(PROP_TYPE(PR_MESSAGE_FLAGS));
	return lpDatabase->DoInsert(strQuery);
}

// 54
ECRESULT UpdateDatabaseCreateTProperties(ECDatabase *lpDatabase)
{
	// Create the tproperties table
	auto er = lpDatabase->DoInsert(Z_TABLEDEF_TPROPERTIES);
	if (er != erSuccess)
		return er;

	std::string strQuery = "INSERT IGNORE INTO tproperties (folderid,hierarchyid,tag,type,val_ulong,val_string,val_binary,val_double,val_longint,val_hi,val_lo) "
					"SELECT h.id, p.hierarchyid, p.tag, p.type, p.val_ulong, LEFT(p.val_string,255), LEFT(p.val_binary,255), p.val_double, p.val_longint, p.val_hi, p.val_lo "
					"FROM properties AS p "
						"JOIN hierarchy AS tmp ON p.hierarchyid = tmp.id AND p.tag NOT IN (" + stringify(PROP_ID(PR_BODY_HTML)) + "," + stringify(PROP_ID(PR_RTF_COMPRESSED)) + ")"
						"LEFT JOIN hierarchy AS h ON tmp.parent = h.id AND h.type = 3";
	return lpDatabase->DoInsert(strQuery);
}

// 55
ECRESULT UpdateDatabaseConvertHierarchy(ECDatabase *lpDatabase)
{
	// Create the temporary properties table
	std::string strQuery = Z_TABLEDEF_HIERARCHY;
	strQuery.replace(strQuery.find("hierarchy"), strlen("hierarchy"), "hierarchy_temp");
	auto er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		goto exit;

	// Folders can be 0, 1, 2 and 0x400 (Deleted)
	// Messages can be 0x40 (associated) and Deleted
	// Other can be Deleted
	strQuery = "INSERT INTO hierarchy_temp (id, parent, type, flags, owner) SELECT id, parent, type, CASE type WHEN 3 THEN flags & 0x403 WHEN 5 THEN flags & 0x440 ELSE flags & 0x400 END, owner FROM hierarchy";
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->DoUpdate("RENAME TABLE hierarchy TO hierarchy_old, hierarchy_temp TO hierarchy");
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->DoDelete("DROP TABLE hierarchy_old");
exit:
	lpDatabase->DoDelete("DROP TABLE IF EXISTS hierarchy_temp");
	
	return er;
}

// 56
ECRESULT UpdateDatabaseCreateDeferred(ECDatabase *lpDatabase)
{
	// Create the deferred table
	return lpDatabase->DoInsert(Z_TABLEDEF_DELAYEDUPDATE);
}

// 57
ECRESULT UpdateDatabaseConvertChanges(ECDatabase *lpDatabase)
{
	bool bDropColumn;
	
	// In some upgrade paths the moved_from column doesn't exist. We'll
	// check so no error (which we could ignore) will be logged.
	auto er = lpDatabase->CheckExistColumn("changes", "moved_from", &bDropColumn);
	if (er == erSuccess && bDropColumn) {
		std::string strQuery = "ALTER TABLE changes DROP COLUMN moved_from, DROP key moved";
		er = lpDatabase->DoDelete(strQuery);
	}
	return er;
}

// 58
ECRESULT UpdateDatabaseConvertNames(ECDatabase *lpDatabase)
{
	// CharsetDetect(names)

	// Create the temporary names table
	std::string strQuery = Z_TABLEDEF_NAMES;
	strQuery.replace(strQuery.find("names"), strlen("names"), "names_temp");
	auto er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		goto exit;

	strQuery = "INSERT INTO names_temp (id,nameid,namestring,guid) SELECT id,nameid,CAST(CAST(CONVERT(namestring USING latin1) AS binary) AS CHAR CHARACTER SET utf8),guid FROM names";
	er = lpDatabase->DoInsert(strQuery);
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->DoUpdate("RENAME TABLE names TO names_old, names_temp TO names");
	if (er != erSuccess)
		goto exit;

	er = lpDatabase->DoDelete("DROP TABLE names_old");
exit:
	lpDatabase->DoDelete("DROP TABLE IF EXISTS names_temp");
	
	return er;
}

// 59
ECRESULT UpdateDatabaseReceiveFolderToUnicode(ECDatabase *lpDatabase)
{
	std::string strQuery = "ALTER TABLE receivefolder MODIFY messageclass varchar(255) CHARSET utf8 COLLATE utf8_general_ci NOT NULL DEFAULT''";
	return lpDatabase->DoUpdate(strQuery);
}

// 60
ECRESULT UpdateDatabaseClientUpdateStatus(ECDatabase *lpDatabase)
{
	return lpDatabase->DoInsert(Z_TABLEDEF_CLIENTUPDATESTATUS);
}

// 61
ECRESULT UpdateDatabaseConvertStores(ECDatabase *lpDatabase)
{
	// user_hierarchy_id does not exist on all servers, depends on upgrade path
	std::string strQuery = "ALTER TABLE stores "
					"DROP KEY `user_hierarchy_id` ";
	auto er = lpDatabase->DoUpdate(strQuery);
	if (er != erSuccess) {
		ec_log_err("Ignoring optional index error, and continuing database upgrade");
		er = erSuccess;
	}

	strQuery = "ALTER TABLE stores "
					"DROP PRIMARY KEY, "
					"ADD COLUMN `type` smallint(6) unsigned NOT NULL default '0', "
					"ADD PRIMARY KEY (`user_id`, `hierarchy_id`, `type`), "
					"ADD UNIQUE KEY `id` (`id`)";
	return lpDatabase->DoUpdate(strQuery);
}

// 62
ECRESULT UpdateDatabaseUpdateStores(ECDatabase *lpDatabase)
{
	std::string strQuery = "UPDATE stores SET type=" + stringify(ECSTORE_TYPE_PUBLIC) + " WHERE user_id=1 OR user_id IN (SELECT id FROM users where objectclass=" + stringify(CONTAINER_COMPANY) + ")";
	return lpDatabase->DoUpdate(strQuery);
}

// 63
ECRESULT UpdateWLinkRecordKeys(ECDatabase *lpDatabase)
{
	std::string strQuery = "update stores "	// For each store
				"join properties as p1 on p1.tag = 0x35E6 and p1.hierarchyid=stores.hierarchy_id " // Get PR_COMMON_VIEWS_ENTRYID
				"join indexedproperties as i1 on i1.val_binary = p1.val_binary and i1.tag=0xfff " // Get hierarchy for common views
				"join hierarchy as h2 on h2.parent=i1.hierarchyid " // Get children of common views
				"join properties as p2 on p2.hierarchyid=h2.id and p2.tag=0x684d " // Get PR_WLINK_RECKEY for each child
				"join properties as p3 on p3.hierarchyid=h2.id and p3.tag=0x684c " // Get PR_WLINK_ENTRYID for each child
				"set p2.val_binary = p3.val_binary "								// Set PR_WLINK_RECKEY = PR_WLINK_ENTRYID
				"where length(p3.val_binary) = 48";									// Where entryid length is 48 (kopano)
	return lpDatabase->DoUpdate(strQuery);
}

/* Edit no. 64 */
ECRESULT UpdateVersionsTbl(ECDatabase *db)
{
	return db->DoUpdate(
		"alter table `versions` "
		"add column `micro` int(11) unsigned not null default 0 after `minor`, "
		"drop primary key, "
		"add primary key (`major`, `minor`, `micro`, `revision`, `databaserevision`)");
}

/* Edit no. 65 */
ECRESULT UpdateChangesTbl(ECDatabase *db)
{
	return db->DoUpdate(
		"alter table `changes` "
		"modify change_type int(11) unsigned not null default 0");
}

/* Edit no. 66 */
ECRESULT UpdateABChangesTbl(ECDatabase *db)
{
	return db->DoUpdate(
		"alter table `abchanges` "
		"modify change_type int(11) unsigned not null default 0");
}

} /* namespace */
