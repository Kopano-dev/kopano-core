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

	Version 7.0.1
	* update receive folder to unicode and increase the messageclass column size

	Version 7.1.0
	* update WLINK entries to new record key format

	Version independed
	* Add setting for IMAP
	* Change primary key in changetable and add an extra move key
	* Force addressbook resync
*/

ECRESULT InsertServerGUID(ECDatabase *lpDatabase)
{
	GUID guid;

	if (CoCreateGuid(&guid) != S_OK) {
		ec_log_err("InsertServerGUID(): CoCreateGuid failed");
		return KCERR_DATABASE_ERROR;
	}

	return lpDatabase->DoInsert("INSERT INTO `settings` VALUES ('server_guid', " + lpDatabase->EscapeBinary(reinterpret_cast<unsigned char *>(&guid), sizeof(GUID)) + ")");
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
