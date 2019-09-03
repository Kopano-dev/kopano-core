/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <errmsg.h>
#include "mysqld_error.h"
#include <kopano/stringutil.h>
#include <kopano/ECDefs.h>
#include "ECDBDef.h"
#include "ECUserManagement.h"
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/MAPIErrors.h>
#include <kopano/kcodes.h>
#include <kopano/ecversion.h>
#include <mapidefs.h>
#include "ECDatabase.h"
#include "SOAPUtils.h"
#include "ECSearchFolders.h"
#include "StatsClient.h"

namespace KC {

#ifdef KNOB144
#define DEBUG_SQL 0
#define DEBUG_TRANSACTION 0
#endif

struct sUpdateList_t {
	unsigned int ulVersion;
	const char *lpszLogComment;
	ECRESULT (*lpFunction)(ECDatabase* lpDatabase);
};

class zcp_versiontuple final {
	public:
	zcp_versiontuple(unsigned int maj = 0, unsigned int min = 0,
	    unsigned int mic = 0, unsigned int rev = 0, unsigned int dbs = 0) :
		v_major(maj), v_minor(min), v_micro(mic),
		v_rev(rev), v_schema(dbs)
	{
	}
	std::string stringify(char sep = '.') const;
	int compare(const zcp_versiontuple &) const;
	/* stupid major(3) function uses a #define in glibc */
	unsigned int v_major, v_minor, v_micro, v_rev, v_schema;
};

bool searchfolder_restart_required; //HACK for rebuild the searchfolders with an upgrade

static ECRESULT InsertServerGUID(ECDatabase *lpDatabase)
{
	GUID guid;
	if (CoCreateGuid(&guid) != S_OK) {
		ec_log_err("InsertServerGUID(): CoCreateGuid failed");
		return KCERR_DATABASE_ERROR;
	}
	return lpDatabase->DoInsert("INSERT INTO `settings` VALUES ('server_guid', " + lpDatabase->EscapeBinary(reinterpret_cast<unsigned char *>(&guid), sizeof(GUID)) + ")");
}

static ECRESULT dbup64(ECDatabase *db)
{
	return db->DoUpdate(
		"alter table `versions` "
		"add column `micro` int(11) unsigned not null default 0 after `minor`, "
		"drop primary key, "
		"add primary key (`major`, `minor`, `micro`, `revision`, `databaserevision`)");
}

static ECRESULT dbup65(ECDatabase *db)
{
	return db->DoUpdate(
		"alter table `changes` "
		"modify change_type int(11) unsigned not null default 0");
}

static ECRESULT dbup66(ECDatabase *db)
{
	return db->DoUpdate(
		"alter table `abchanges` "
		"modify change_type int(11) unsigned not null default 0");
}

static ECRESULT dbup68(ECDatabase *db)
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

static ECRESULT dbup69(ECDatabase *db)
{
	/*
	 * Add new indexes first to see if that runs afoul of the dataset. The
	 * operation is atomic; either both indexes will exist afterwards, or
	 * neither.
	 */
	auto ret = db->DoUpdate("ALTER TABLE `names` ADD UNIQUE INDEX `gni` (`guid`(16), `nameid`), ADD UNIQUE INDEX `gns` (`guid`(16), `namestring`), DROP INDEX `guidnameid`, DROP INDEX `guidnamestring`");
	if (ret == hrSuccess)
		return hrSuccess;

	ec_log_err("K-1216: Cannot update to schema v69, because the \"names\" table contains unexpected rows. Certain prior versions of the server erroneously allowed these duplicates to be added (KC-1108).");
	DB_RESULT res;
	unsigned long long ai = ~0ULL;
	ret = db->DoSelect("SELECT MAX(id)+1 FROM names", &res);
	if (ret == erSuccess) {
		auto row = res.fetch_row();
		if (row != nullptr && row[0] != nullptr)
			ai = strtoull(row[0], nullptr, 0);
	}
	if (ai >= 31485)
		ec_log_err("K-1219: It is possible that K-1216 has, in the past, led old clients to misplace data in the DB. This cannot be reliably detected and such data is effectively lost already.");
	ec_log_err("K-1220: To fix the excess rows, use `kopano-dbadm k-1216`. Consult the manpage and preferably make a backup first.");
	ec_log_err("K-1221: Alternatively, the server may be started with --ignore-da to forego the schema update.");
	return KCERR_INVALID_VERSION; /* allow use of ignore-da */
}

/*
 * ALTER statements are non-transacted. An update function using ALTER must not
 * issue other modification statements.
 */

static const sUpdateList_t sUpdateList[] = {
	// New in 7.2.2
	{64, "Add \"micro\" column to \"versions\" table", dbup64},
	// New in 8.1.0 / 7.2.4, MySQL 5.7 compatibility
	{65, "Updating abchanges table", dbup65},
	{66, "Updating changes table", dbup66},
	{67, "Drop clientupdatestatus table", [](ECDatabase *db) {
		return db->DoUpdate("DROP TABLE IF EXISTS `clientupdatestatus`"); }},
	{68, "Perform column type upgrade missed in SVN r23897", dbup68},
	{69, "Update \"names\" with uniqueness constraints", dbup69},
	{70, "names.guid change from blob to binary(16); drop old indexes", [](ECDatabase *db) {
		return db->DoUpdate("ALTER TABLE `names` CHANGE COLUMN `guid` `guid` binary(16) NOT NULL"); }},
	{71, "Add the \"filename\" column to \"singleinstances\"", [](ECDatabase *db) {
		return db->DoUpdate("ALTER TABLE `singleinstances` ADD COLUMN `filename` VARCHAR(255) DEFAULT NULL"); }},
	{118, "no-op marker", [](ECDatabase *) -> ECRESULT { return erSuccess; }},
};

static const char *const server_groups[] = {
  "kopano",
  NULL,
};

struct STOREDPROCS {
	const char *szName, *szSQL;
};

/**
 * Mode 0 = All bodies
 * Mode 1 = Best body only (RTF better than HTML) + plaintext
 * Mode 2 = Plaintext only
 */

static const char szGetProps[] =
"CREATE PROCEDURE GetProps(IN hid integer, IN mode integer)\n"
"BEGIN\n"
"  DECLARE bestbody INT;\n"

"  IF mode = 1 THEN\n"
"  	 call GetBestBody(hid, bestbody);\n"
"  END IF;\n"

"  SELECT 0, tag, properties.type, val_ulong, val_string, val_binary, val_double, val_longint, val_hi, val_lo, 0, names.nameid, names.namestring, names.guid\n"
"    FROM properties LEFT JOIN names ON properties.tag-34049=names.id WHERE hierarchyid=hid AND (tag <= 34048 OR names.id IS NOT NULL) AND (tag NOT IN (4105, 4115) OR mode = 0 OR (mode = 1 AND tag = bestbody))\n"
"  UNION\n"
"  SELECT count(*), tag, mvproperties.type, \n"
"          group_concat(length(mvproperties.val_ulong),':', mvproperties.val_ulong ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_string),':', mvproperties.val_string ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_binary),':', mvproperties.val_binary ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_double),':', mvproperties.val_double ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_longint),':', mvproperties.val_longint ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_hi),':', mvproperties.val_hi ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_lo),':', mvproperties.val_lo ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          0, names.nameid, names.namestring, names.guid \n"
"    FROM mvproperties LEFT JOIN names ON mvproperties.tag-34049=names.id WHERE hierarchyid=hid AND (tag <= 34048 OR names.id IS NOT NULL) GROUP BY tag, mvproperties.type; \n"
"END;\n";

static const char szPrepareGetProps[] =
"CREATE PROCEDURE PrepareGetProps(IN hid integer)\n"
"BEGIN\n"
"  SELECT 0, tag, properties.type, val_ulong, val_string, val_binary, val_double, val_longint, val_hi, val_lo, hierarchy.id, names.nameid, names.namestring, names.guid\n"
"    FROM properties JOIN hierarchy ON properties.hierarchyid=hierarchy.id LEFT JOIN names ON properties.tag-34049=names.id WHERE hierarchy.parent=hid AND (tag <= 34048 OR names.id IS NOT NULL);\n"
"  SELECT count(*), tag, mvproperties.type, \n"
"          group_concat(length(mvproperties.val_ulong),':', mvproperties.val_ulong ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_string),':', mvproperties.val_string ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_binary),':', mvproperties.val_binary ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_double),':', mvproperties.val_double ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_longint),':', mvproperties.val_longint ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_hi),':', mvproperties.val_hi ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          group_concat(length(mvproperties.val_lo),':', mvproperties.val_lo ORDER BY mvproperties.orderid SEPARATOR ''), \n"
"          hierarchy.id, names.nameid, names.namestring, names.guid \n"
"    FROM mvproperties JOIN hierarchy ON mvproperties.hierarchyid=hierarchy.id LEFT JOIN names ON mvproperties.tag-34049=names.id WHERE hierarchy.parent=hid AND (tag <= 34048 OR names.id IS NOT NULL) GROUP BY tag, mvproperties.type; \n"
"END;\n";

static const char szGetBestBody[] =
"CREATE PROCEDURE GetBestBody(hid integer, OUT bestbody integer)\n"
"DETERMINISTIC\n"
"BEGIN\n"
"  DECLARE best INT;\n"
"  DECLARE CONTINUE HANDLER FOR NOT FOUND\n"
"    SET bestbody = 0 ;\n"
"  \n"
"  # Get body with lowest id (RTF before HTML)\n"
"  SELECT tag INTO bestbody FROM properties WHERE hierarchyid=hid AND tag IN (4105, 4115) ORDER BY tag LIMIT 1;\n"
"END;\n";

static const char szStreamObj[] =
"# Read a type-5 (Message) item from the database, output properties and subobjects\n"
"CREATE PROCEDURE StreamObj(IN rootid integer, IN maxdepth integer, IN mode integer)\n"
"BEGIN\n"
"DECLARE no_more_rows BOOLEAN;\n"
"DECLARE subid INT;\n"
"DECLARE subsubid INT;\n"
"DECLARE subtype INT;\n"
"DECLARE cur_hierarchy CURSOR FOR\n"
"	SELECT id,hierarchy.type FROM hierarchy WHERE parent=rootid AND type=7; \n"
"DECLARE CONTINUE HANDLER FOR NOT FOUND\n"
"    SET no_more_rows = TRUE;\n"

"  call GetProps(rootid, mode);\n"

"  call PrepareGetProps(rootid);\n"

"  SELECT id,hierarchy.type FROM hierarchy WHERE parent=rootid;\n"

"  OPEN cur_hierarchy;\n"

"  the_loop: LOOP\n"
"    FETCH cur_hierarchy INTO subid, subtype;\n"

"    IF no_more_rows THEN\n"
"      CLOSE cur_hierarchy;\n"
"      LEAVE the_loop;\n"
"    END IF;\n"

"    IF subtype = 7 THEN\n"
"      BEGIN\n"
"        DECLARE CONTINUE HANDLER FOR NOT FOUND set subsubid = 0;\n"

"        IF maxdepth > 0 THEN\n"
"          SELECT id INTO subsubid FROM hierarchy WHERE parent=subid LIMIT 1;\n"
"          SELECT id, hierarchy.type FROM hierarchy WHERE parent = subid LIMIT 1;\n"

"          IF subsubid != 0 THEN\n"
"            # Recurse into submessage (must be type 5 since attachments can only contain nested messages)\n"
"            call StreamObj(subsubid, maxdepth-1, mode);\n"
"          END IF;\n"
"        ELSE\n"
"          # Maximum depth reached. Output a zero-length subset to indicate that we're\n"
"          # not recursing further.\n"
"          SELECT id, hierarchy.type FROM hierarchy WHERE parent=subid LIMIT 0;\n"
"        END IF;\n"
"      END;\n"

"    END IF;\n"
"  END LOOP the_loop;\n"

"END\n";

static const STOREDPROCS stored_procedures[] = {
	{ "GetProps", szGetProps },
	{ "PrepareGetProps", szPrepareGetProps },
	{ "GetBestBody", szGetBestBody },
	{ "StreamObj", szStreamObj }
};

std::string zcp_versiontuple::stringify(char sep) const
{
	return KC::stringify(v_major) + sep + KC::stringify(v_minor) + sep +
	       KC::stringify(v_micro) + sep + KC::stringify(v_rev) + sep +
	       KC::stringify(v_schema);
}

int zcp_versiontuple::compare(const zcp_versiontuple &rhs) const
{
	if (v_major < rhs.v_major)
		return -1;
	if (v_major > rhs.v_major)
		return 1;
	if (v_minor < rhs.v_minor)
		return -1;
	if (v_minor > rhs.v_minor)
		return 1;
	if (v_micro < rhs.v_micro)
		return -1;
	if (v_micro > rhs.v_micro)
		return 1;
	if (v_rev < rhs.v_rev)
		return -1;
	if (v_rev > rhs.v_rev)
		return 1;
	return 0;
}

ECDatabase::ECDatabase(std::shared_ptr<ECConfig> c, std::shared_ptr<ECStatsCollector> sc) :
	m_lpConfig(std::move(c)), m_stats(std::move(sc))
{
	auto s = m_lpConfig->GetSetting("mysql_database");
	if (s != nullptr)
		/* used by db_update_69 */
		m_dbname = s;
}

ECRESULT ECDatabase::InitLibrary(const char *lpDatabaseDir,
    const char *lpConfigFile)
{
	std::string strDatabaseDir, strConfigFile;
	int			ret = 0;

	if(lpDatabaseDir) {
    	strDatabaseDir = "--datadir=";
    	strDatabaseDir+= lpDatabaseDir;
    }
    if(lpConfigFile) {
    	strConfigFile = "--defaults-file=";
    	strConfigFile+= lpConfigFile;
    }
	const char *server_args[] = {
		"",		/* this string is not used */
		strConfigFile.c_str(),
		strDatabaseDir.c_str(),
	};
	/*
	 * mysql's function signature stinks, and even their samples
	 * do the cast :(
	 */
	ret = mysql_library_init(ARRAY_SIZE(server_args),
	      const_cast<char **>(server_args),
	      const_cast<char **>(server_groups));
	if (ret != 0) {
		ec_log_crit("Unable to initialize mysql: error 0x%08X", ret);
		return KCERR_DATABASE_ERROR;
	}
	return erSuccess;
}

/**
 * Initialize anything that has to be initialized at server startup.
 *
 * Currently this means we're updating all the stored procedure definitions
 */
ECRESULT ECDatabase::InitializeDBState(void)
{
	return InitializeDBStateInner();
}

ECRESULT ECDatabase::InitializeDBStateInner(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(stored_procedures); ++i) {
		auto er = DoUpdate(std::string("DROP PROCEDURE IF EXISTS ") + stored_procedures[i].szName);
		if(er != erSuccess)
			return er;
		er = DoUpdate(stored_procedures[i].szSQL);
		if (er == erSuccess)
			continue;
		int err = mysql_errno(&m_lpMySQL);
		if (err == ER_DBACCESS_DENIED_ERROR) {
			ec_log_err("The storage server is not allowed to create stored procedures");
			ec_log_err("Please grant CREATE ROUTINE permissions to the mysql user \"%s\" on the \"%s\" database",
				m_lpConfig->GetSetting("mysql_user"), m_lpConfig->GetSetting("mysql_database"));
		} else {
			ec_log_err("The storage server is unable to create stored procedures, error %d", err);
		}
		return er;
	}
	return erSuccess;
}

void ECDatabase::UnloadLibrary(void)
{
	/*
	 * MySQL will timeout waiting for its own threads if the mysql
	 * initialization was done in another thread than the one where
	 * mysql_*_end() is called. [Global problem - it also affects
	 * projects other than Kopano's.] :(
	 */
	ec_log_notice("Waiting for mysql_server_end");
	mysql_server_end();// mysql > 4.1.10 = mysql_library_end();
	ec_log_notice("Waiting for mysql_library_end");
	mysql_library_end();
}

ECRESULT ECDatabase::Connect(void)
{
	auto gcm = atoui(m_lpConfig->GetSetting("mysql_group_concat_max_len"));
	if (gcm < 1024)
		gcm = 1024;
	/*
	 * Set auto reconnect OFF. mysql < 5.0.4 default on, mysql 5.0.4 >
	 * reconnection default off. We always want reconnect OFF, because we
	 * want to know when the connection is broken since this creates a new
	 * MySQL session, and we want to set some session variables.
	 */
	auto er = KDatabase::Connect(m_lpConfig.get(), false,
	          CLIENT_MULTI_STATEMENTS, gcm);
	if (er != erSuccess)
		return er;
	if (Query("set max_sp_recursion_depth = 255") != 0) {
		ec_log_err("Unable to set recursion depth");
		er = KCERR_DATABASE_ERROR;
		goto exit;
	}
	if (m_lpMySQL.server_version) {
		// m_lpMySQL.server_version is a C type string (char*) containing something like "5.5.37-0+wheezy1" (MySQL),
		// "5.5.37-MariaDB-1~wheezy-log" or "10.0.11-MariaDB=1~wheezy-log" (MariaDB)
		// The following code may look funny, but it is correct, see http://www.cplusplus.com/reference/cstdlib/strtol/
		long int majorversion = strtol(m_lpMySQL.server_version, NULL, 10);
		// Check for over/underflow and version.
		if (errno != ERANGE && majorversion >= 5)
			/*
			 * This option was introduced in mysql 5.0, so let's
			 * not even try on 4.1 servers. Ignore error, if any.
			 */
			Query("SET SESSION sql_mode = 'STRICT_ALL_TABLES,NO_UNSIGNED_SUBTRACTION'");
	}
exit:
	if (er == erSuccess)
		m_stats->inc(SCN_DATABASE_CONNECTS);
	else
		m_stats->inc(SCN_DATABASE_FAILED_CONNECTS);
	return er;
}

/**
 * Perform an SQL query on MySQL
 *
 * Sends a query to the MySQL server, and does a reconnect if the server connection is lost before or during
 * the SQL query. The reconnect is done only once. If the query fails after the reconnect, the entire call
 * fails.
 *
 * It is up to the caller to get any result information from the query.
 *
 * @param[in] strQuery SQL query to perform
 * @return result (erSuccess or KCERR_DATABASE_ERROR)
 */
ECRESULT ECDatabase::Query(const std::string &strQuery)
{
	ECRESULT er = erSuccess;
	int err = KDatabase::Query(strQuery);

	if(err && (mysql_errno(&m_lpMySQL) == CR_SERVER_LOST || mysql_errno(&m_lpMySQL) == CR_SERVER_GONE_ERROR)) {
		ec_log_warn("SQL [%08lu] info: Try to reconnect", m_lpMySQL.thread_id);
		er = Close();
		if(er != erSuccess)
			return er;
		er = Connect();
		if(er != erSuccess)
			return er;
		// Try again
		err = mysql_real_query( &m_lpMySQL, strQuery.c_str(), strQuery.length() );
	}
	if(err) {
		if (!m_bSuppressLockErrorLogging || GetLastError() == DB_E_UNKNOWN)
			ec_log_err("SQL [%08lu] Failed: %s, Query Size: %zu, Query: \"%s\"", m_lpMySQL.thread_id, mysql_error(&m_lpMySQL), strQuery.size(), strQuery.c_str());
		er = KCERR_DATABASE_ERROR;
	}
	return er;
}

ECRESULT ECDatabase::DoSelect(const std::string &strQuery,
    DB_RESULT *lppResult, bool fStreamResult)
{
	ECRESULT er = KDatabase::DoSelect(strQuery, lppResult, fStreamResult);
	m_stats->inc(SCN_DATABASE_SELECTS);
	if (er != erSuccess) {
		m_stats->inc(SCN_DATABASE_FAILED_SELECTS);
		m_stats->SetTime(SCN_DATABASE_LAST_FAILED, time(nullptr));
	}
	return er;
}

ECRESULT ECDatabase::DoSelectMulti(const std::string &strQuery)
{
	ECRESULT er = erSuccess;
	assert(strQuery.length() != 0);
	autolock alk(*this);

	if( Query(strQuery) != erSuccess ) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("ECDatabase::DoSelectMulti(): select failed");
		goto exit;
	}
	m_bFirstResult = true;
	m_stats->inc(SCN_DATABASE_SELECTS);
exit:
	if (er != erSuccess) {
		m_stats->inc(SCN_DATABASE_FAILED_SELECTS);
		m_stats->SetTime(SCN_DATABASE_LAST_FAILED, time(nullptr));
	}
	return er;
}

/**
 * Get next resultset from a multi-resultset query
 *
 * @param[out] lppResult Resultset
 * @return result
 */
ECRESULT ECDatabase::GetNextResult(DB_RESULT *lppResult)
{
	ECRESULT er = erSuccess;
	DB_RESULT lpResult;
	int ret = 0;
	autolock alk(*this);

	if(!m_bFirstResult)
		ret = mysql_next_result( &m_lpMySQL );
	m_bFirstResult = false;
	if(ret < 0) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("SQL [%08lu] next_result failed: expected more results", m_lpMySQL.thread_id);
		goto exit;
	}
	if(ret > 0) {
		er = KCERR_DATABASE_ERROR;
		ec_log_err("SQL [%08lu] next_result of multi-resultset failed: %s", m_lpMySQL.thread_id, mysql_error(&m_lpMySQL));
		goto exit;
	}
	lpResult = DB_RESULT(this, mysql_store_result(&m_lpMySQL));
	if (lpResult == nullptr) {
   		// I think this can only happen on the first result set of a query since otherwise mysql_next_result() would already fail
		er = KCERR_DATABASE_ERROR;
		ec_log_err("SQL [%08lu] result failed: %s", m_lpMySQL.thread_id, mysql_error(&m_lpMySQL));
		goto exit;
   	}
	if (lppResult)
		*lppResult = std::move(lpResult);
exit:
	if (er != erSuccess) {
		m_stats->inc(SCN_DATABASE_FAILED_SELECTS);
		m_stats->SetTime(SCN_DATABASE_LAST_FAILED, time(nullptr));
	}
	return er;
}

/**
 * Finalize a multi-SQL call
 *
 * A stored procedure will output a NULL result set at the end of the stored procedure to indicate
 * that the results have ended. This must be consumed here, otherwise the database will be left in a bad
 * state.
 */
ECRESULT ECDatabase::FinalizeMulti(void)
{
	autolock alk(*this);

	mysql_next_result(&m_lpMySQL);
	auto lpResult = DB_RESULT(this, mysql_store_result(&m_lpMySQL));
	if (lpResult != nullptr) {
		ec_log_err("SQL [%08lu] result failed: unexpected results received at end of batch", m_lpMySQL.thread_id);
		return KCERR_DATABASE_ERROR;
	}
	return erSuccess;
}

ECRESULT ECDatabase::DoUpdate(const std::string &strQuery,
    unsigned int *lpulAffectedRows)
{
	auto er = KDatabase::DoUpdate(strQuery, lpulAffectedRows);
	m_stats->inc(SCN_DATABASE_UPDATES);
	if (er != erSuccess) {
		m_stats->inc(SCN_DATABASE_FAILED_UPDATES);
		m_stats->SetTime(SCN_DATABASE_LAST_FAILED, time(nullptr));
	}
	return er;
}

ECRESULT ECDatabase::DoInsert(const std::string &strQuery,
    unsigned int *lpulInsertId, unsigned int *lpulAffectedRows)
{
	auto er = KDatabase::DoInsert(strQuery, lpulInsertId, lpulAffectedRows);
	m_stats->inc(SCN_DATABASE_INSERTS);
	if (er != erSuccess) {
		m_stats->inc(SCN_DATABASE_FAILED_INSERTS);
		m_stats->SetTime(SCN_DATABASE_LAST_FAILED, time(nullptr));
	}
	return er;
}

ECRESULT ECDatabase::DoDelete(const std::string &strQuery,
    unsigned int *lpulAffectedRows)
{
	auto er = KDatabase::DoDelete(strQuery, lpulAffectedRows);
	m_stats->inc(SCN_DATABASE_DELETES);
	if (er != erSuccess) {
		m_stats->inc(SCN_DATABASE_FAILED_DELETES);
		m_stats->SetTime(SCN_DATABASE_LAST_FAILED, time(nullptr));
	}
	return er;
}

/*
 */
ECRESULT ECDatabase::DoSequence(const std::string &strSeqName,
    unsigned int ulCount, unsigned long long *lpllFirstId)
{
#if defined(KNOB144) && DEBUG_TRANSACTION
	if (m_ulTransactionState != 0)
		assert(false);
#endif
	return KDatabase::DoSequence(strSeqName, ulCount, lpllFirstId);
}

bool ECDatabase::SuppressLockErrorLogging(bool bSuppress)
{
	std::swap(bSuppress, m_bSuppressLockErrorLogging);
	return bSuppress;
}

kd_trans ECDatabase::Begin(ECRESULT &res)
{
	auto dtx = KDatabase::Begin(res);
#if defined(KNOB144) && DEBUG_TRANSACTION
	ec_log_debug("%08X: BEGIN", &m_lpMySQL);
	if(m_ulTransactionState != 0) {
		ec_log_debug("BEGIN ALREADY ISSUED");
		assert(("BEGIN ALREADY ISSUED", false));
	}
	m_ulTransactionState = 1;
#endif
	return dtx;
}

ECRESULT ECDatabase::Commit(void)
{
	auto er = KDatabase::Commit();
#if defined(KNOB144) && DEBUG_TRANSACTION
	ec_log_debug("%08X: COMMIT", &m_lpMySQL);
	if(m_ulTransactionState != 1) {
		ec_log_debug("NO BEGIN ISSUED");
		assert(("NO BEGIN ISSUED", false));
	}
	m_ulTransactionState = 0;
#endif
	return er;
}

ECRESULT ECDatabase::Rollback(void)
{
	auto er = KDatabase::Rollback();
#if defined(KNOB144) && DEBUG_TRANSACTION
	ec_log_debug("%08X: ROLLBACK", &m_lpMySQL);
	if(m_ulTransactionState != 1) {
		ec_log_debug("NO BEGIN ISSUED");
		assert(("NO BEGIN ISSUED", false));
	}
	m_ulTransactionState = 0;
#endif
	return er;
}

void ECDatabase::ThreadInit(void)
{
	mysql_thread_init();
}

void ECDatabase::ThreadEnd(void)
{
	mysql_thread_end();
}

ECRESULT ECDatabase::CreateDatabase(void)
{
	auto er = KDatabase::CreateDatabase(m_lpConfig.get(), false);
	if (er != erSuccess)
		return er;
	const char *charset = nullptr;
	er = KDatabase::CreateTables(m_lpConfig.get(), &charset);
	if (er != erSuccess)
		return er;

	// database default data
	static constexpr const sSQLDatabase_t sDatabaseData[] = {
		{"users", Z_TABLEDATA_USERS},
		{"stores", Z_TABLEDATA_STORES},
		{"hierarchy", Z_TABLEDATA_HIERARCHY},
		{"properties", Z_TABLEDATA_PROPERTIES},
		{"acl", Z_TABLEDATA_ACL},
		{"settings", Z_TABLEDATA_SETTINGS},
		{"indexedproperties", Z_TABLEDATA_INDEXED_PROPERTIES},
	};

	// Add the default table data
	for (size_t i = 0; i < ARRAY_SIZE(sDatabaseData); ++i) {
		ec_log_info("Add table data for \"%s\"", sDatabaseData[i].lpComment);
		er = DoInsert(sDatabaseData[i].lpSQL);
		if(er != erSuccess)
			return er;
	}
	if (charset != nullptr)
		DoUpdate("UPDATE `settings` SET `value`='" + Escape(charset) + "' WHERE `name`='charset'");

	er = InsertServerGUID(this);
	if(er != erSuccess)
		return er;
	// Add the release id in the database
	er = UpdateDatabaseVersion(Z_UPDATE_RELEASE_ID);
	if(er != erSuccess)
		return er;

	// Loop throught the update list
	for (size_t i = 0; i < ARRAY_SIZE(sUpdateList); ++i) {
		if (sUpdateList[i].ulVersion <= Z_UPDATE_RELEASE_ID)
			continue;
		er = UpdateDatabaseVersion(sUpdateList[i].ulVersion);
		if(er != erSuccess)
			return er;
	}

	ec_log_notice("Database has been created/updated and populated");
	return erSuccess;
}

static inline bool row_has_null(DB_ROW row, size_t z)
{
	if (row == NULL)
		return true;
	while (z-- > 0)
		if (row[z] == NULL)
			return true;
	return false;
}

ECRESULT ECDatabase::GetDatabaseVersion(zcp_versiontuple *dbv)
{
	DB_RESULT lpResult;
	DB_ROW			lpDBRow = NULL;

	/* Check if the "micro" column already exists (it does since v64) */
	auto er = DoSelect("SELECT databaserevision FROM versions WHERE databaserevision>=64 LIMIT 1", &lpResult);
	if (er != erSuccess)
		return er;
	bool have_micro = lpResult.get_num_rows() > 0;
	std::string strQuery = "SELECT major, minor";
	strQuery += have_micro ? ", micro" : ", 0";
	strQuery += ", revision, databaserevision FROM versions ORDER BY major DESC, minor DESC";
	if (have_micro)
		strQuery += ", micro DESC";
	strQuery += ", revision DESC, databaserevision DESC LIMIT 1";

	er = DoSelect(strQuery, &lpResult);
	if(er != erSuccess && mysql_errno(&m_lpMySQL) != ER_NO_SUCH_TABLE)
		return er;

	if (er != erSuccess || lpResult.get_num_rows() == 0) {
		// Ok, maybe < than version 5.10
		// check version
		strQuery = "SHOW COLUMNS FROM properties";
		er = DoSelect(strQuery, &lpResult);
		if(er != erSuccess)
			return er;

		while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
			if (lpDBRow[0] != NULL && strcasecmp(lpDBRow[0], "storeid") == 0) {
				dbv->v_major  = 5;
				dbv->v_minor  = 0;
				dbv->v_rev    = 0;
				dbv->v_schema = 0;
				er = erSuccess;
				break;
			}
		}
		return KCERR_UNKNOWN_DATABASE;
	}

	lpDBRow = lpResult.fetch_row();
	if (row_has_null(lpDBRow, 5)) {
		ec_log_err("ECDatabase::GetDatabaseVersion(): NULL row or columns");
		return KCERR_DATABASE_ERROR;
	}
	dbv->v_major  = strtoul(lpDBRow[0], NULL, 0);
	dbv->v_minor  = strtoul(lpDBRow[1], NULL, 0);
	dbv->v_micro  = strtoul(lpDBRow[2], NULL, 0);
	dbv->v_rev    = strtoul(lpDBRow[3], NULL, 0);
	dbv->v_schema = strtoul(lpDBRow[4], NULL, 0);
	return erSuccess;
}

ECRESULT ECDatabase::GetFirstUpdate(unsigned int *lpulDatabaseRevision)
{
	DB_RESULT lpResult;
	DB_ROW			lpDBRow = NULL;

	auto er = DoSelect("SELECT MIN(databaserevision) FROM versions", &lpResult);
	if(er != erSuccess && mysql_errno(&m_lpMySQL) != ER_NO_SUCH_TABLE)
		return er;
	else if(er == erSuccess)
		lpDBRow = lpResult.fetch_row();
	if (lpDBRow == nullptr || lpDBRow[0] == nullptr)
		*lpulDatabaseRevision = 0;
	else
		*lpulDatabaseRevision = atoui(lpDBRow[0]);
	return erSuccess;
}

/**
 * Update the database to the current version.
 *
 * @param[in]  bForceUpdate possebly force upgrade
 * @param[out] strReport error message
 *
 * @return Kopano error code
 */
ECRESULT ECDatabase::UpdateDatabase(bool bForceUpdate, std::string &strReport)
{
	bool bUpdated = false, bSkipped = false;
	unsigned int	ulDatabaseRevisionMin = 0;
	zcp_versiontuple stored_ver;
	zcp_versiontuple program_ver(PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_MICRO, PROJECT_VERSION_REVISION, Z_UPDATE_LAST);

	auto er = GetDatabaseVersion(&stored_ver);
	if(er != erSuccess)
		return er;
	er = GetFirstUpdate(&ulDatabaseRevisionMin);
	if(er != erSuccess)
		return er;
	if (stored_ver.v_schema > 0 && stored_ver.v_schema < 63) {
		strReport = format("DB schema is %u and older than v63 (ZCP 7.2). "
		            "KC 8.4 was the last version able to upgrade such.",
		            stored_ver.v_schema);
		return KCERR_INVALID_VERSION;
	}

	//default error
	strReport = "Unable to upgrade database from version " +
	            stored_ver.stringify() + " to " + program_ver.stringify();

	// Check version
	int cmp = stored_ver.compare(program_ver);
	if (cmp == 0 && stored_ver.v_schema == Z_UPDATE_LAST) {
		// up to date
		return erSuccess;
	} else if (cmp > 0 || (cmp == 0 && stored_ver.v_schema > Z_UPDATE_LAST)) {
		// Start a old server with a new database
		strReport = "Database version (" + stored_ver.stringify(',') +
		            ") is newer than the server version (" + program_ver.stringify(',') + ")";
		return KCERR_INVALID_VERSION;
	}

	m_bForceUpdate = bForceUpdate;
	if (bForceUpdate)
		ec_log_warn("Manually forced the database upgrade because the option \"--force-database-upgrade\" was given.");

	// Loop throught the update list
	for (size_t i = 0; i < ARRAY_SIZE(sUpdateList); ++i) {
		if (stored_ver.v_schema >= sUpdateList[i].ulVersion)
			// Update already done, next
			continue;
		ec_log_info("Start: %s", sUpdateList[i].lpszLogComment);
		auto dtx = Begin(er);
		if(er != erSuccess)
			return er;
		bSkipped = false;
		er = sUpdateList[i].lpFunction(this);
		if (er == KCERR_IGNORE_ME) {
			bSkipped = true;
			er = erSuccess;
		} else if (er == KCERR_USER_CANCEL) {
			return er; // Reason should be logged in the update itself.
		} else if (er != hrSuccess) {
			return er;
		}
		er = UpdateDatabaseVersion(sUpdateList[i].ulVersion);
		if(er != erSuccess)
			return er;
		er = dtx.commit();
		if(er != erSuccess)
			return er;
		ec_log_notice("%s: %s", bSkipped ? "Skipped" : "Done", sUpdateList[i].lpszLogComment);
		bUpdated = true;
	}

	// Ok, no changes for the database, but for update history we add a version record
	if (!bUpdated) {
		// Update version table
		er = UpdateDatabaseVersion(Z_UPDATE_LAST);
		if(er != erSuccess)
			return er;
	}
	return erSuccess;
}

ECRESULT ECDatabase::UpdateDatabaseVersion(unsigned int ulDatabaseRevision)
{
	DB_RESULT result;

	/* Check for "micro" column (present in v64+) */
	auto er = DoSelect("SELECT databaserevision FROM versions WHERE databaserevision>=64 LIMIT 1", &result);
	if (er != erSuccess)
		return er;
	bool have_micro = result.get_num_rows() > 0;

	// Insert version number
	std::string strQuery = "INSERT INTO versions (major, minor, ";
	if (have_micro)
		strQuery += "micro, ";
	strQuery += "revision, databaserevision, updatetime) VALUES(";
	strQuery += stringify(PROJECT_VERSION_MAJOR) + std::string(", ") + stringify(PROJECT_VERSION_MINOR) + std::string(", ");
	if (have_micro)
		strQuery += stringify(PROJECT_VERSION_MICRO) + std::string(", ");
	strQuery += "'" + stringify(PROJECT_VERSION_REVISION) + "', " + stringify(ulDatabaseRevision) + ", FROM_UNIXTIME(" + stringify(time(nullptr)) + "))";
	return DoInsert(strQuery);
}

static constexpr const sSQLDatabase_t kcsrv_tables[] = {
	{"acl", Z_TABLEDEF_ACL},
	{"hierarchy", Z_TABLEDEF_HIERARCHY},
	{"names", Z_TABLEDEF_NAMES},
	{"mvproperties", Z_TABLEDEF_MVPROPERTIES},
	{"tproperties", Z_TABLEDEF_TPROPERTIES},
	{"properties", Z_TABLEDEF_PROPERTIES},
	{"delayedupdate", Z_TABLEDEF_DELAYEDUPDATE},
	{"receivefolder", Z_TABLEDEF_RECEIVEFOLDER},
	{"stores", Z_TABLEDEF_STORES},
	{"users", Z_TABLEDEF_USERS},
	{"outgoingqueue", Z_TABLEDEF_OUTGOINGQUEUE},
	{"lob", Z_TABLEDEF_LOB},
	{"searchresults", Z_TABLEDEF_SEARCHRESULTS},
	{"changes", Z_TABLEDEF_CHANGES},
	{"syncs", Z_TABLEDEF_SYNCS},
	{"versions", Z_TABLEDEF_VERSIONS},
	{"indexedproperties", Z_TABLEDEF_INDEXED_PROPERTIES},
	{"settings", Z_TABLEDEF_SETTINGS},
	{"object", Z_TABLEDEF_OBJECT},
	{"objectproperty", Z_TABLEDEF_OBJECT_PROPERTY},
	{"objectmvproperty", Z_TABLEDEF_OBJECT_MVPROPERTY},
	{"objectrelation", Z_TABLEDEF_OBJECT_RELATION},
	{"singleinstances", Z_TABLEDEF_REFERENCES},
	{"abchanges", Z_TABLEDEF_ABCHANGES},
	{"syncedmessages", Z_TABLEDEFS_SYNCEDMESSAGES},
	{nullptr, nullptr},
};

const struct sSQLDatabase_t *ECDatabase::GetDatabaseDefs(void)
{
	return kcsrv_tables;
}

} /* namespace */
