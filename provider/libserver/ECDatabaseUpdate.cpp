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

/* Edit no. 67 */
ECRESULT DropClientUpdateStatusTbl(ECDatabase *db)
{
	return db->DoUpdate("drop table if exists `clientupdatestatus`");
}

} /* namespace */
