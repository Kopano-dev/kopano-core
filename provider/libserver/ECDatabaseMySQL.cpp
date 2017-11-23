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
#include <kopano/platform.h>

#include <iostream>
#include <list>
#include <string>
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

#include "ECDatabaseUpdate.h"
#include "ECStatsCollector.h"

namespace KC {

#ifdef DEBUG
#define DEBUG_SQL 0
#define DEBUG_TRANSACTION 0
#endif

struct sUpdateList_t {
	unsigned int ulVersion;
	unsigned int ulVersionMin; // Version to start the update
	const char *lpszLogComment;
	ECRESULT (*lpFunction)(ECDatabase* lpDatabase);
};

class zcp_versiontuple _kc_final {
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

static const sUpdateList_t sUpdateList[] = {
	// New in 7.2.2
	{ Z_UPDATE_VERSIONTBL_MICRO, 0, "Add \"micro\" column to \"versions\" table", UpdateVersionsTbl },

	// New in 8.1.0 / 7.2.4, MySQL 5.7 compatibility
	{ Z_UPDATE_ABCHANGES_PKEY, 0, "Updating abchanges table", UpdateABChangesTbl },
	{ Z_UPDATE_CHANGES_PKEY, 0, "Updating changes table", UpdateChangesTbl },
};

static const char *const server_groups[] = {
  "kopano",
  NULL,
};

struct STOREDPROCS {
	const char *szName;
	const char *szSQL;
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
	return ::stringify(v_major) + sep + ::stringify(v_minor) + sep +
	       ::stringify(v_micro) + sep + ::stringify(v_rev) + sep +
	       ::stringify(v_schema);
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

ECDatabase::ECDatabase(ECConfig *cfg) :
    m_lpConfig(cfg)
{
}

ECDatabase::~ECDatabase(void)
{
	Close();
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
	if ((ret = mysql_library_init(ARRAY_SIZE(server_args),
	     const_cast<char **>(server_args),
	     const_cast<char **>(server_groups))) != 0) {
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

ECRESULT ECDatabase::CheckExistColumn(const std::string &strTable,
    const std::string &strColumn, bool *lpbExist)
{
	DB_RESULT lpDBResult;

	std::string strQuery = "SELECT 1 FROM information_schema.COLUMNS "
				"WHERE TABLE_SCHEMA = '" + std::string(m_lpConfig->GetSetting("mysql_database")) + "' "
				"AND TABLE_NAME = '" + strTable + "' "
				"AND COLUMN_NAME = '" + strColumn + "' LIMIT 1";
	auto er = DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;
	*lpbExist = lpDBResult.fetch_row() != nullptr;
	return er;
}

ECRESULT ECDatabase::CheckExistIndex(const std::string &strTable,
    const std::string &strKey, bool *lpbExist)
{
	DB_RESULT lpDBResult;
	DB_ROW			lpRow = NULL;

	// WHERE not supported in MySQL < 5.0.3 
	std::string strQuery = "SHOW INDEXES FROM " + strTable;
	auto er = DoSelect(strQuery, &lpDBResult);
	if (er != erSuccess)
		return er;

	*lpbExist = false;
	while ((lpRow = lpDBResult.fetch_row()) != nullptr) {
		// 2 is Key_name
		if (lpRow[2] && strcmp(lpRow[2], strKey.c_str()) == 0) {
			*lpbExist = true;
			break;
		}
	}
	return er;
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
	auto er = KDatabase::Connect(m_lpConfig, false,
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
		g_lpStatsCollector->Increment(SCN_DATABASE_CONNECTS);
	else
		g_lpStatsCollector->Increment(SCN_DATABASE_FAILED_CONNECTS);
		
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
		// Don't assert on ER_NO_SUCH_TABLE because it's an anticipated error in the db upgrade code.
		if (mysql_errno(&m_lpMySQL) != ER_NO_SUCH_TABLE)
			assert(false);
	}
	return er;
}

ECRESULT ECDatabase::DoSelect(const std::string &strQuery,
    DB_RESULT *lppResult, bool fStreamResult)
{
	ECRESULT er = KDatabase::DoSelect(strQuery, lppResult, fStreamResult);
	g_lpStatsCollector->Increment(SCN_DATABASE_SELECTS);
	if (er != erSuccess) {
		g_lpStatsCollector->Increment(SCN_DATABASE_FAILED_SELECTS);
		g_lpStatsCollector->SetTime(SCN_DATABASE_LAST_FAILED, time(NULL));
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

	g_lpStatsCollector->Increment(SCN_DATABASE_SELECTS);
	
exit:
	if (er != erSuccess) {
		g_lpStatsCollector->Increment(SCN_DATABASE_FAILED_SELECTS);
		g_lpStatsCollector->SetTime(SCN_DATABASE_LAST_FAILED, time(NULL));
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
		g_lpStatsCollector->Increment(SCN_DATABASE_FAILED_SELECTS);
		g_lpStatsCollector->SetTime(SCN_DATABASE_LAST_FAILED, time(NULL));
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
	g_lpStatsCollector->Increment(SCN_DATABASE_UPDATES);
	if (er != erSuccess) {
		g_lpStatsCollector->Increment(SCN_DATABASE_FAILED_UPDATES);
		g_lpStatsCollector->SetTime(SCN_DATABASE_LAST_FAILED, time(NULL));
	}
	return er;
}

ECRESULT ECDatabase::DoInsert(const std::string &strQuery,
    unsigned int *lpulInsertId, unsigned int *lpulAffectedRows)
{
	auto er = KDatabase::DoInsert(strQuery, lpulInsertId, lpulAffectedRows);
	g_lpStatsCollector->Increment(SCN_DATABASE_INSERTS);
	if (er != erSuccess) {
		g_lpStatsCollector->Increment(SCN_DATABASE_FAILED_INSERTS);
		g_lpStatsCollector->SetTime(SCN_DATABASE_LAST_FAILED, time(NULL));
	}
	return er;
}

ECRESULT ECDatabase::DoDelete(const std::string &strQuery,
    unsigned int *lpulAffectedRows)
{
	auto er = KDatabase::DoDelete(strQuery, lpulAffectedRows);
	g_lpStatsCollector->Increment(SCN_DATABASE_DELETES);
	if (er != erSuccess) {
		g_lpStatsCollector->Increment(SCN_DATABASE_FAILED_DELETES);
		g_lpStatsCollector->SetTime(SCN_DATABASE_LAST_FAILED, time(NULL));
	}
	return er;
}

/*
 */
ECRESULT ECDatabase::DoSequence(const std::string &strSeqName,
    unsigned int ulCount, unsigned long long *lpllFirstId)
{
#ifdef DEBUG
#if DEBUG_TRANSACTION
	if (m_ulTransactionState != 0)
		assert(false);
#endif
#endif
	return KDatabase::DoSequence(strSeqName, ulCount, lpllFirstId);
}

/** 
 * For some reason, MySQL only supports up to 3 bytes of UTF-8 data. This means
 * that data outside the BMP is not supported. This function filters the passed UTF-8 string
 * and removes the non-BMP characters. Since it should be extremely uncommon to have useful
 * data outside the BMP, this should be acceptable.
 *
 * Note: BMP stands for Basic Multilingual Plane (first 0x10000 code points in unicode)
 *
 * If somebody points out a useful use case for non-BMP characters in the future, then we'll
 * have to rethink this.
 *
 */
std::string ECDatabase::FilterBMP(const std::string &strToFilter)
{
	const char *c = strToFilter.c_str();
	std::string strFiltered;

	for (size_t pos = 0; pos < strToFilter.size(); ) {
		// Copy 1, 2, and 3-byte UTF-8 sequences
		int len;
		
		if((c[pos] & 0x80) == 0)
			len = 1;
		else if((c[pos] & 0xE0) == 0xC0)
			len = 2;
		else if((c[pos] & 0xF0) == 0xE0)
			len = 3;
		else if((c[pos] & 0xF8) == 0xF0)
			len = 4;
		else if((c[pos] & 0xFC) == 0xF8)
			len = 5;
		else if((c[pos] & 0xFE) == 0xFC)
			len = 6;
		else
			// Invalid UTF-8 ?
			len = 1;
		if (len < 4)
			strFiltered.append(&c[pos], len);
		pos += len;
	}
	
	return strFiltered;
}

bool ECDatabase::SuppressLockErrorLogging(bool bSuppress)
{
	std::swap(bSuppress, m_bSuppressLockErrorLogging);
	return bSuppress;
}

ECRESULT ECDatabase::Begin(void)
{
	auto er = KDatabase::Begin();
#ifdef DEBUG
#if DEBUG_TRANSACTION
	ec_log_debug("%08X: BEGIN", &m_lpMySQL);
	if(m_ulTransactionState != 0) {
		ec_log_debug("BEGIN ALREADY ISSUED");
		assert(("BEGIN ALREADY ISSUED", false));
	}
	m_ulTransactionState = 1;
#endif
#endif
	
	return er;
}

ECRESULT ECDatabase::Commit(void)
{
	auto er = KDatabase::Commit();
#ifdef DEBUG
#if DEBUG_TRANSACTION
	ec_log_debug("%08X: COMMIT", &m_lpMySQL);
	if(m_ulTransactionState != 1) {
		ec_log_debug("NO BEGIN ISSUED");
		assert(("NO BEGIN ISSUED", false));
	}
	m_ulTransactionState = 0;
#endif
#endif

	return er;
}

ECRESULT ECDatabase::Rollback(void)
{
	auto er = KDatabase::Rollback();
#ifdef DEBUG
#if DEBUG_TRANSACTION
	ec_log_debug("%08X: ROLLBACK", &m_lpMySQL);
	if(m_ulTransactionState != 1) {
		ec_log_debug("NO BEGIN ISSUED");
		assert(("NO BEGIN ISSUED", false));
	}
	m_ulTransactionState = 0;
#endif
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
	auto er = KDatabase::CreateDatabase(m_lpConfig, false);
	if (er != erSuccess)
		return er;

	er = KDatabase::CreateTables();
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

	er = InsertServerGUID(this);
	if(er != erSuccess)
		return er;

	// Add the release id in the database
	er = UpdateDatabaseVersion(Z_UPDATE_RELEASE_ID);
	if(er != erSuccess)
		return er;

	// Loop throught the update list
	for (size_t i = Z_UPDATE_RELEASE_ID;
	     i < ARRAY_SIZE(sUpdateList); ++i)
	{
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

		for (lpDBRow = lpResult.fetch_row(); lpDBRow != nullptr; lpDBRow = lpResult.fetch_row()) {
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

ECRESULT ECDatabase::IsUpdateDone(unsigned int ulDatabaseRevision,
    unsigned int ulRevision)
{
	DB_RESULT lpResult;

	std::string strQuery = "SELECT major,minor,revision,databaserevision FROM versions WHERE databaserevision = " + stringify(ulDatabaseRevision);
	if (ulRevision > 0)
		strQuery += " AND revision = " + stringify(ulRevision);

	strQuery += " ORDER BY major DESC, minor DESC, revision DESC, databaserevision DESC LIMIT 1";
	auto er = DoSelect(strQuery, &lpResult);
	if(er != erSuccess)
		return er;
	if (lpResult.get_num_rows() != 1)
		return KCERR_NOT_FOUND;
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
	bool			bUpdated = false;
	bool			bSkipped = false;
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
	} else if (cmp > 0) {
		// Start a old server with a new database
		strReport = "Database version (" + stored_ver.stringify(',') +
		            ") is newer than the server version (" + program_ver.stringify(',') + ")";
		return KCERR_INVALID_VERSION;
	}

	this->m_bForceUpdate = bForceUpdate;

	if (bForceUpdate)
		ec_log_warn("Manually forced the database upgrade because the option \"--force-database-upgrade\" was given.");

	// Loop throught the update list
	for (size_t i = 0; i < ARRAY_SIZE(sUpdateList); ++i) {
		if (stored_ver.v_schema >= sUpdateList[i].ulVersion)
			// Update already done, next
			continue;

		ec_log_info("Start: %s", sUpdateList[i].lpszLogComment);

		er = Begin();
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
			Rollback();
			ec_log_err("Failed: Rollback database");
			return er;
		}

		er = UpdateDatabaseVersion(sUpdateList[i].ulVersion);
		if(er != erSuccess)
			return er;
		er = Commit();
		if(er != erSuccess)
			return er;
		ec_log_notice("%s: %s", bSkipped ? "Skipped" : "Done", sUpdateList[i].lpszLogComment);
		bUpdated = true;
	}

	// Ok, no changes for the database, but for update history we add a version record
	if(bUpdated == false) {
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
/**
 * Validate all database tables
*/
ECRESULT ECDatabase::ValidateTables(void)
{
	std::list<std::string> listTables, listErrorTables;
	DB_RESULT lpResult;
	DB_ROW		lpDBRow = NULL;

	auto er = DoSelect("SHOW TABLES", &lpResult);
	if(er != erSuccess) {
		ec_log_err("Unable to get all tables from the mysql database. %s", GetError());
		return er;
	}

	// Get all tables of the database
	while ((lpDBRow = lpResult.fetch_row()) != nullptr) {
		if (lpDBRow == NULL || lpDBRow[0] == NULL) {
			ec_log_err("Wrong table information.");
			return KCERR_DATABASE_ERROR;
		}
		listTables.emplace(listTables.end(), lpDBRow[0]);
	}

	for (const auto &table : listTables) {
		er = DoSelect("CHECK TABLE " + table, &lpResult);
		if(er != erSuccess) {
			ec_log_err("Unable to check table \"%s\"", table.c_str());
			return er;
		}
		lpDBRow = lpResult.fetch_row();
		if (lpDBRow == NULL || lpDBRow[0] == NULL || lpDBRow[1] == NULL || lpDBRow[2] == NULL) {
			ec_log_err("Wrong check table information.");
			return KCERR_DATABASE_ERROR;
		}

		ec_log_info("%30s | %15s | %s", lpDBRow[0], lpDBRow[2], lpDBRow[3]);
		if (strcmp(lpDBRow[2], "error") == 0)
			listErrorTables.emplace(listErrorTables.end(), lpDBRow[0]);
	}

	if (!listErrorTables.empty())
	{
		ec_log_notice("Rebuilding tables.");
		for (const auto &table : listErrorTables) {
			er = DoUpdate("ALTER TABLE " + table + " FORCE");
			if(er != erSuccess) {
				ec_log_crit("Unable to fix table \"%s\"", table.c_str());
				break;
			}
		}
		if (er != erSuccess)
			ec_log_crit("Rebuild tables failed. Error code 0x%08x", er);
		else
			ec_log_notice("Rebuilding tables done.");
	}//	if (!listErrorTables.empty())
	return er;
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
	{"clientupdatestatus", Z_TABLEDEF_CLIENTUPDATESTATUS},
	{nullptr, nullptr},
};

const struct sSQLDatabase_t *ECDatabase::GetDatabaseDefs(void)
{
	return kcsrv_tables;
}

} /* namespace */
