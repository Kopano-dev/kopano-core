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

ECRESULT db_update_68(ECDatabase *db)
{
	auto ret = db->DoUpdate("ALTER TABLE `hierarchy` MODIFY COLUMN `owner` int(11) unsigned NOT NULL DEFAULT 0");
	if (ret != erSuccess)
		return KCERR_DATABASE_ERROR;
	ret = db->DoUpdate("ALTER TABLE `stores` MODIFY COLUMN `id` int(11) unsigned NOT NULL auto_increment");
	if (ret != erSuccess)
		return KCERR_DATABASE_ERROR;
	ret = db->DoUpdate("ALTER TABLE `stores` MODIFY COLUMN `user_id` int(11) unsigned NOT NULL DEFAULT 0");
	if (ret != erSuccess)
		return KCERR_DATABASE_ERROR;
	ret = db->DoUpdate("ALTER TABLE `stores` MODIFY COLUMN `company` int(11) unsigned NOT NULL DEFAULT 0");
	if (ret != erSuccess)
		return KCERR_DATABASE_ERROR;
	ret = db->DoUpdate("ALTER TABLE `users` MODIFY COLUMN `id` int(11) NOT NULL AUTO_INCREMENT");
	if (ret != erSuccess)
		return KCERR_DATABASE_ERROR;
	return db->DoUpdate("ALTER TABLE `users` MODIFY COLUMN `company` int(11) NOT NULL DEFAULT 0");
}

ECRESULT db_update_69(ECDatabase *db)
{
	/*
	 * Add new indexes first to see if that runs afoul of the dataset. The
	 * operation is atomic; either both indexes will exist afterwards, or
	 * neither.
	 */
	auto ret = db->DoUpdate("ALTER TABLE `names` ADD UNIQUE INDEX `gni` (`guid`(16), `nameid`), ADD UNIQUE INDEX `gns` (`guid`(16), `namestring`), DROP INDEX `guidnameid`, DROP INDEX `guidnamestring`");
	if (ret == hrSuccess)
		return hrSuccess;

	ec_log_err("K-1216: Cannot update to schema v69 because the \"names\" table contains unexpected rows.");
	DB_RESULT res;
	unsigned long long ai = ~0ULL;
	ret = db->DoSelect("SELECT `AUTO_INCREMENT` FROM information_schema WHERE table_schema=\"" + db->Escape(db->get_dbname()) + "\" AND table_name=\"names\"", &res);
	if (ret == erSuccess) {
		auto row = res.fetch_row();
		if (row != nullptr && row[0] != nullptr)
			ai = strtoull(row[0], nullptr, 0);
	}
	if (ai == ~0ULL)
		ec_log_err("K-1217: Table fill level is indeterminate.");
	else
		ec_log_err("K-1218: Table fill level is " + stringify(ai) + " of 31485.");
	if (ai >= 31485)
		ec_log_err("K-1219: K-1216 may have already caused a loss of data in other tables.");
	else
		ec_log_err("K-1220: Looks like K-1216 has not yet caused loss of data. The dataset should be repaired as soon as possible.");
	ec_log_err("K-1221: Proceeding with --ignore-da and ignoring the schema update is technically possible, but data corruption may happen sooner or later.");
	return KCERR_DATABASE_ERROR;
}

ECRESULT db_update_70(ECDatabase *db)
{
	return db->DoUpdate("ALTER TABLE `names` CHANGE COLUMN `guid` `guid` binary(16) NOT NULL");
}

} /* namespace */
