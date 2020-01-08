/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright 2018, Kopano and its licensors
 */
#include <atomic>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <getopt.h>
#include <mapitags.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ECThreadPool.h>
#include <kopano/database.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>
#include <kopano/timeutil.hpp>
#include <kopano/tie.hpp>
#include "ECDatabase.h"
#include "ECDatabaseFactory.h"
#include "ECDatabaseUtils.h"
#include "StatsClient.h"
#include "ECSessionManager.h"
#include "ECStatsTables.h"

using namespace std::string_literals;
using namespace KC;

class da_exec;

class da_task final : public ECTask {
	public:
	enum action { NONE, SELECT, UPDATE, DELETE, };
	da_task(da_exec *, std::shared_ptr<ECConfig>, action, const std::string &note, const std::string &query, DB_RESULT *, unsigned int *aff);
	virtual void run();
	ECRESULT m_retcode = erSuccess;

	private:
	std::shared_ptr<ECConfig> m_config;
	std::string m_note, m_query;
	da_exec *m_pool = nullptr;
	DB_RESULT *m_result = nullptr;
	unsigned int *m_aff = nullptr;
	action m_action = NONE;
};

/* A joint dispatcher/threadpool in one */
class da_exec final : private ECThreadPool {
	public:
	da_exec(std::shared_ptr<ECConfig> c, size_t z);
	ECRESULT quiesce();
	void notify(ECRESULT);

	void DoUpdate_async(const std::string &note, const std::string &query)
	{
		enqueue(new da_task(this, m_config, da_task::UPDATE, note, query, nullptr, nullptr), true);
	}
	ECRESULT DoSelect(const std::string &q, DB_RESULT *r) { return m_db.DoSelect(q, r); }
	ECRESULT DoUpdate(const std::string &q, unsigned int *a = nullptr) { return m_db.DoUpdate(q, a); }
	ECRESULT DoDelete(const std::string &q, unsigned int *a = nullptr) { return m_db.DoDelete(q, a); }

	private:
	std::atomic<ECRESULT> m_retcode{erSuccess};
	std::shared_ptr<ECConfig> m_config;
	KDatabase m_db;
};

using fancydb = std::shared_ptr<da_exec>;
static int adm_sigterm_count = 3;
static bool adm_quit;

static const std::string our_proptables[] = {
	"properties", "tproperties", "mvproperties",
	"indexedproperties", "singleinstances", "lob",
};
static const std::string our_proptables_hier[] = {
	/* tables with a "hierarchyid" column */
	"properties", "tproperties", "mvproperties",
	"indexedproperties", "singleinstances",
};

da_task::da_task(da_exec *p, std::shared_ptr<ECConfig> c, da_task::action a,
    const std::string &n, const std::string &q, DB_RESULT *r,
    unsigned int *z) :
	m_config(std::move(c)), m_note(n), m_query(q), m_pool(p),
	m_result(r), m_aff(z), m_action(a)
{}

void da_task::run()
{
	KDatabase db;
	auto ret = db.Connect(m_config.get(), true, 0, 0);
	if (ret != hrSuccess) {
		ec_perror("Could not connect to db", ret);
		m_pool->notify(ret);
		return;
	}
	if (!m_note.empty())
		ec_log_notice("Starting " + m_note);
	if (m_action == SELECT)
		m_pool->notify(db.DoSelect(m_query, m_result));
	else if (m_action == UPDATE)
		m_pool->notify(db.DoDelete(m_query, m_aff));
	else if (m_action == DELETE)
		m_pool->notify(db.DoDelete(m_query, m_aff));
	if (!m_note.empty())
		ec_log_notice("Ending " + m_note);
}

da_exec::da_exec(std::shared_ptr<ECConfig> c, size_t z) :
	ECThreadPool("exec", z), m_config(std::move(c))
{
	auto ret = m_db.Connect(m_config.get(), true, 0, 0);
	if (ret != hrSuccess) {
		ec_perror("Could not connect to db", ret);
		return;
	}
	m_db.DoUpdate("SET SESSION sql_mode = 'STRICT_ALL_TABLES,NO_UNSIGNED_SUBTRACTION'");
}

void da_exec::notify(ECRESULT ret)
{
	ECRESULT exp = erSuccess;
	m_retcode.compare_exchange_strong(exp, ret);
}

ECRESULT da_exec::quiesce()
{
	ulock_normal locker(m_hMutex);
	while (!adm_quit && (m_listTasks.size() > 0 || m_active > 0))
		m_hCondTaskDone.wait(locker);
	/* Reset code for use with subsequent quiesce call. */
	auto ret = m_retcode.exchange(erSuccess);
	locker.unlock();

	if (adm_quit)
		set_thread_count(0, 0, true);
	return ret;
}

static ECRESULT hidx_remove(fancydb db, const std::string &tbl)
{
	ec_log_notice("dbadm: discard helper index on %s", tbl.c_str());
	return db->DoUpdate("ALTER TABLE " + tbl + " DROP INDEX tmptag");
}

static std::set<std::string> hidx_add_all(fancydb db)
{
	std::set<std::string> status;
	for (const auto &tbl : our_proptables)
		db->DoUpdate_async(format("index-tags: adding temporary helper index on %s", tbl.c_str()),
			"ALTER TABLE " + tbl + " ADD INDEX tmptag (tag)");
	auto ret = db->quiesce();
	if (ret != erSuccess)
		ec_log_info("Index creation failures are not fatal; it affects at most the processing speed.");
	return status;
}

static ECRESULT index_tags(fancydb db)
{
	hidx_add_all(db);
	return erSuccess;
}

static ECRESULT remove_helper_index(fancydb db)
{
	ECRESULT coll = erSuccess;
	for (const auto &tbl : our_proptables) {
		auto ret = hidx_remove(db, tbl);
		if (coll == erSuccess)
			coll = ret;
	}
	if (coll != erSuccess)
		ec_log_info("This failure is not fatal. Extraneous indices only take up disk space.");
	return erSuccess;
}

static ECRESULT np_defrag(fancydb db)
{
	DB_RESULT result;
	DB_ROW row;
	std::set<unsigned int> freemap;
	ec_log_notice("dbadm: executing action \"np-defrag\"");

	for (unsigned int i = 1; i <= 31485; ++i)
		freemap.emplace(i);
	auto ret = db->DoSelect("SELECT id FROM names WHERE id <= 31485", &result);
	if (ret != erSuccess)
		return ret;
	while ((row = result.fetch_row()) != nullptr) {
		auto x = freemap.erase(strtoul(row[0], nullptr, 0));
		assert(x == 1);
	}

	/*
	 * 1. Let T be the set of input tagids. $T is its cardinality.
	 * 2. Defrag only “renames” elements within the set T.
	 *    $T will therefore not change.
	 * 3. Let B be a bitvector of size $T (B[1]..B[T]).
	 *    B[i] indicates whether i is, or is not, in T.
	 * 4. Defrag produces a contiguous sequence starting at 1.
	 *    Therefore, B must be an all-ones bitvector after defrag.
	 * 5. Therefore, the number of zero bits in B prior to defrag
	 *    gives the amount of work to be done.
	 *
	 * $T = select count(*) from names where id<=31485;
	 * ones_count(B) = select count(*) from names where id<=$T;
	 * zeros_count(B) = $T - ones_count(B)
	 */
	unsigned int tags_to_move = 0, tags_moved = 0;
	ret = db->DoSelect("SELECT (SELECT COUNT(*) FROM names WHERE id<=31485)-COUNT(*) FROM names WHERE id <= (SELECT COUNT(*) FROM names WHERE id<=31485)", &result);
	if (ret == erSuccess) {
		row = result.fetch_row();
		if (row != nullptr && row[0] != nullptr) {
			tags_to_move = strtoul(row[0], nullptr, 0);
			ec_log_info("defrag: %u entries to move", tags_to_move);
		}
	}
	ret = db->DoSelect("SELECT id FROM names WHERE id <= 31485 ORDER BY id DESC", &result);
	if (ret != erSuccess)
		return ret;
	ec_log_notice("defrag: %zu entries present", result.get_num_rows());
	if (result.get_num_rows() == 0)
		return erSuccess;

	time_point start_ts = decltype(start_ts)::clock::now();
	while (!adm_quit && (row = result.fetch_row()) != nullptr) {
		unsigned int oldid = strtoul(row[0], nullptr, 0);
		unsigned int oldtag = 0x8501 + oldid;
		if (freemap.size() == 0)
			break;
		unsigned int newid = *freemap.begin();
		if (newid >= oldid)
			break;
		unsigned int newtag = 0x8501 + newid;
		if (oldtag >= 0xFFFF || newtag >= 0xFFFF)
			continue;
		auto x0 = freemap.erase(newid);
		assert(x0 == 1);
		for (const auto &tbl : our_proptables) {
			ec_log_notice("defrag: moving %u -> %u [%s]", oldid, newid, tbl.c_str());
			ret = db->DoUpdate("UPDATE " + tbl + " SET tag=" + stringify(newtag) + " WHERE tag=" + stringify(oldtag));
			if (ret != erSuccess)
				return ret;
		}
		ec_log_notice("defrag: moving %u -> %u [names]", oldid, newid);
		ret = db->DoUpdate("UPDATE names SET id=" + stringify(newid) + " WHERE id=" + stringify(oldid));
		if (ret != erSuccess)
			return ret;
		auto x = freemap.emplace(oldid);
		assert(x.second);
		++tags_moved;
		auto diff_ts = dur2dbl(decltype(start_ts)::clock::now() - start_ts);
		ec_log_notice("defrag: %u left, est. %.0f minutes", tags_to_move - tags_moved,
			(tags_to_move - tags_moved) * diff_ts / tags_moved / 60);
	}
	if (adm_quit) {
		ec_log_notice("defrag: operation interrupted safely.");
		return erSuccess;
	}
	/* autoupdates to highest id */
	ret = db->DoUpdate("ALTER TABLE names AUTO_INCREMENT=1");
	if (ret != erSuccess)
		return ret;
	ec_log_notice("defrag: done");
	return erSuccess;
}

static ECRESULT np_remove_highid(fancydb db)
{
	/*
	 * This is a no-op for systems where only K-1220 and no K-1219 was
	 * diagnosed.
	 */
	ec_log_notice("dbadm: executing action \"np-remove-highid\"");
	return db->DoUpdate("DELETE FROM names WHERE id > 31485");
}

static ECRESULT np_remove_unused(fancydb db)
{
	ec_log_notice("dbadm: executing action \"np-remove-unused\"");
	auto ret = db->DoUpdate("CREATE TEMPORARY TABLE ut (PRIMARY KEY (`tag`)) SELECT * FROM ("
		"SELECT DISTINCT tag FROM properties UNION "
		"SELECT DISTINCT tag FROM tproperties UNION "
		"SELECT DISTINCT tag FROM mvproperties UNION "
		"SELECT DISTINCT tag FROM indexedproperties UNION "
		"SELECT DISTINCT tag FROM singleinstances UNION "
		"SELECT DISTINCT tag FROM lob) AS t WHERE tag>=34049");
	if (ret != erSuccess)
		return ret;
	unsigned int aff = 0;
	ret = db->DoDelete("DELETE names FROM names LEFT JOIN ut ON names.id+34049=ut.tag WHERE ut.tag IS NULL", &aff);
	if (ret != erSuccess)
		return ret;
	ec_log_notice("remove-unused: expunged %u rows.", aff);
	return erSuccess;
}

static ECRESULT np_remove_xh(fancydb db)
{
	ec_log_notice("dbadm: executing action \"np-remove-xh\"");
	unsigned int aff = 0;
	auto ret = db->DoUpdate("CREATE TEMPORARY TABLE n (SELECT id, 34049+id AS tag FROM names WHERE id <= 31485 AND guid=0x8603020000000000C000000000000046 AND (namestring LIKE \"X-%\" OR namestring LIKE \"x-%\"))");
	if (ret != erSuccess)
		return ret;
	for (const auto &tbl : our_proptables) {
		if (adm_quit)
			break;
		ec_log_notice("remove-xh: purging \"%s\"...", tbl.c_str());
		ret = db->DoDelete("DELETE p FROM " + tbl + " AS p INNER JOIN n ON p.tag=n.tag", &aff);
		if (ret != erSuccess)
			return ret;
		ec_log_notice("remove-xh: expunged %u rows.", aff);
	}
	if (adm_quit) {
		ec_log_notice("remove-xh: operation interrupted safely.");
		return erSuccess;
	}
	ret = db->DoDelete("DELETE names FROM names INNER JOIN n ON names.id=n.id");
	if (ret != erSuccess)
		return ret;
	db->DoDelete("DROP TABLE n");
	return erSuccess;
}

static ECRESULT np_repair_dups(fancydb db)
{
	DB_RESULT result;
	DB_ROW row;
	ec_log_notice("dbadm: executing action \"np-repair-dups\"");

	auto ret = db->DoSelect(
		"SELECT n1.id, n2.min_id FROM names AS n1, "
		"(SELECT MIN(id) AS min_id, guid, nameid, namestring "
		"FROM names GROUP BY guid, nameid, namestring) AS n2 WHERE "
		"n1.id!=n2.min_id AND " /* don't need to update these if the id does not change */
		"n1.guid=n2.guid AND "
		"(n1.nameid=n2.nameid OR (n1.nameid IS NULL AND n2.nameid IS NULL)) AND "
		"(n1.namestring=n2.namestring OR (n1.namestring IS NULL AND n2.namestring IS NULL)) AND "
		"n1.id <= 31485 " /* don't bother with higher: that data is gone */
		"ORDER BY n1.id", &result);
	if (ret != erSuccess)
		return ret;
	unsigned int tags_to_move = result.get_num_rows(), tags_moved = 0;
	ec_log_notice("dup: %u duplicates to repair", tags_to_move);
	if (tags_to_move == 0)
		return erSuccess;

	time_point start_ts = decltype(start_ts)::clock::now();
	while (!adm_quit && (row = result.fetch_row()) != nullptr) {
		unsigned int oldid = strtoul(row[0], nullptr, 0);
		unsigned int newid = strtoul(row[1], nullptr, 0);
		unsigned int oldtag = 0x8501 + oldid, newtag = 0x8501 + newid;
		if (newtag >= 0xFFFF || oldtag >= 0xFFFF)
			continue;

		auto soldtag = stringify(oldtag), snewtag = stringify(newtag);
		unsigned int aff;
		for (const auto &tbl : our_proptables_hier) {
			if (adm_quit)
				break;
			ec_log_notice("dup: merging #%u into #%u in \"%s\"...", oldid, newid, tbl.c_str());

			/* Remove ambiguous props */
			ret = db->DoUpdate("CREATE TEMPORARY TABLE vt (SELECT hierarchyid FROM " + tbl + " WHERE tag IN (" + soldtag + "," + snewtag + ") GROUP BY hierarchyid HAVING COUNT(*) >= 2)");
			if (ret != erSuccess)
				return ret;
			if (adm_quit)
				break;
			ret = db->DoDelete("DELETE p FROM " + tbl + " AS p INNER JOIN vt ON p.hierarchyid=vt.hierarchyid AND p.tag IN (" + soldtag + "," + snewtag + ")", &aff);
			if (ret != erSuccess)
				return ret;
			if (aff > 0)
				ec_log_notice("dup: deleted %u ambiguous rows in \"%s\"", aff, tbl.c_str());
			if (adm_quit)
				break;
			ret = db->DoUpdate("DROP TEMPORARY TABLE vt");
			if (ret != erSuccess)
				return ret;
			if (adm_quit)
				break;

			/* Merge unambiguous ones */
			ret = db->DoUpdate("UPDATE " + tbl + " SET tag=" + stringify(newtag) + " WHERE tag=" + stringify(oldtag), &aff);
			if (ret != erSuccess)
				return ret;
			if (aff > 0)
				ec_log_notice("dup: updated %u rows in \"%s\"", aff, tbl.c_str());
			if (adm_quit)
				break;
			ret = db->DoDelete("DELETE FROM " + tbl + " WHERE tag=" + stringify(oldtag));
			if (ret != erSuccess)
				return ret;
		}

		/* Lonely table with "instanceid" instead of "hierarchyid"... */
		for (const std::string &tbl : {"lob"}) {
			if (adm_quit)
				break;
			ec_log_notice("dup: merging #%u into #%u in \"%s\"...", oldid, newid, tbl.c_str());

			ret = db->DoUpdate("CREATE TEMPORARY TABLE vt (SELECT instanceid FROM " + tbl + " WHERE tag IN (" + soldtag + "," + snewtag + ") GROUP BY instanceid HAVING COUNT(*) >= 2)");
			if (ret != erSuccess)
				return ret;
			if (adm_quit)
				break;
			ret = db->DoDelete("DELETE p FROM " + tbl + " AS p INNER JOIN vt ON p.instanceid=vt.instanceid AND p.tag IN (" + soldtag + "," + snewtag + ")", &aff);
			if (ret != erSuccess)
				return ret;
			if (aff > 0)
				ec_log_notice("dup: deleted %u ambiguous rows in \"%s\"", aff, tbl.c_str());
			if (adm_quit)
				break;
			ret = db->DoUpdate("DROP TEMPORARY TABLE vt");
			if (ret != erSuccess)
				return ret;
			if (adm_quit)
				break;

			ret = db->DoUpdate("UPDATE " + tbl + " SET tag=" + stringify(newtag) + " WHERE tag=" + stringify(oldtag), &aff);
			if (ret != erSuccess)
				return ret;
			if (aff > 0)
				ec_log_notice("dup: updated %u rows in \"%s\"", aff, tbl.c_str());
			if (adm_quit)
				break;
			ret = db->DoDelete("DELETE FROM " + tbl + " WHERE tag=" + stringify(oldtag));
			if (ret != erSuccess)
				return ret;
		}

		if (adm_quit)
			break;
		ret = db->DoUpdate("DELETE FROM names WHERE id=" + stringify(oldid));
		if (ret != erSuccess)
			return ret;
		++tags_moved;
		auto diff_ts = dur2dbl(decltype(start_ts)::clock::now() - start_ts);
		ec_log_notice("dup: %u left, est. %.0f minutes", tags_to_move - tags_moved,
			(tags_to_move - tags_moved) * diff_ts / tags_moved / 60);
	}
	if (adm_quit) {
		ec_log_notice("dup: stopped (safely) after user request to exit.");
		return erSuccess;
	}
	/* Now the names table is clean, but fragmented ... */
	return erSuccess;
}

static ECRESULT np_stat(fancydb db)
{
	DB_RESULT result;
	auto ret = db->DoSelect("SELECT MAX(id) FROM `names`", &result);
	if (ret != erSuccess)
		return ret;
	auto row = result.fetch_row();
	assert(row != nullptr && row[0] != nullptr);
	auto top_id = strtoul(row[0], nullptr, 0);

	ret = db->DoSelect("SELECT COUNT(*) FROM `names`", &result);
	if (ret != erSuccess)
		return ret;
	row = result.fetch_row();
	assert(row != nullptr && row[0] != nullptr);
	auto uniq_ids = strtoul(row[0], nullptr, 0);

	ret = db->DoSelect("SELECT COUNT(*) FROM (SELECT 1 FROM `names` "
		"GROUP BY `guid`, `nameid`, `namestring`) AS `t1`", &result);
	if (ret != erSuccess)
		return ret;
	row = result.fetch_row();
	assert(row != nullptr && row[0] != nullptr);
	auto uniq_maps = strtoul(row[0], nullptr, 0);

	printf("Fill level now: %lu%% (top ID %lu)\n", top_id * 100 / 31485, top_id);
	printf("Fill level after np_defrag: %lu%% (%lu IDs)\n", uniq_ids * 100 / 31485, uniq_ids);
	printf("Fill level after np_repair_dups+np_defrag: %lu%% (%lu unique entries)\n", uniq_maps * 100 / 31485, uniq_maps);
	return erSuccess;
}

static ECRESULT k1216(fancydb db)
{
	auto idx = hidx_add_all(db);
	/* If indices failed, so be it. Proceed at slow speed, then. */
	auto terminate_handler = make_scope_success([&]() {
		if (adm_quit)
			/* Quick stop, waste no time with more ALTER TABLE. */
			idx.clear();
	});
	auto clean_indices = make_scope_success([&]() {
		for (const auto &tbl : idx)
			hidx_remove(db, tbl.c_str());
	});
	auto ret = np_remove_highid(db);
	if (ret != erSuccess)
		return ret;
	if (adm_quit)
		return erSuccess;
	ret = np_remove_unused(db);
	if (ret != erSuccess)
		return ret;
	if (adm_quit)
		return erSuccess;
	ret = np_repair_dups(db);
	if (ret != erSuccess)
		return ret;
	if (adm_quit)
		return erSuccess;
	return np_defrag(db);
}

static ECRESULT kc1375(fancydb db)
{
	ec_log_notice("kc1375: purging problematic-looking IMAP envelopes...");
	unsigned int aff = 0;
	auto ret = db->DoDelete("DELETE p "
		"FROM properties AS p INNER JOIN names AS n ON p.tag-34049=n.id "
		"WHERE n.guid=0x08F1F5003F8EC746AF725E201C2349E7 AND n.nameid=1 "
		"AND p.type=30 AND p.val_string LIKE '% (((%))) %'", &aff);
	if (ret != erSuccess)
		return ret;
	ec_log_notice("kc1375: deleted %u rows.", aff);
	return erSuccess;
}

static ECRESULT kc_1444(fancydb db)
{
	ec_log_notice("kc-1444: updating tproperties...");
	unsigned int aff = 0;
	auto ret = db->DoUpdate("UPDATE tproperties AS t JOIN properties AS p"
		" ON p.hierarchyid=t.hierarchyid AND p.tag=t.tag AND p.type=t.type"
		" SET t.val_ulong=p.val_ulong"
		" WHERE p.tag=" + stringify(PROP_ID(PR_MESSAGE_FLAGS)) +
		" AND p.type=" + stringify(PROP_TYPE(PR_MESSAGE_FLAGS)), &aff);
	if (ret != erSuccess)
		return ret;
	ec_log_notice("kc-1444: updated %u rows.", aff);
	return erSuccess;
}

static ECRESULT kc_2888(fancydb db)
{
	ec_log_notice("kf-2888: updating store table...");
	unsigned int aff = 0;
	auto ret = db->DoUpdate("UPDATE stores AS s INNER JOIN indexedproperties AS ip"
		" ON s.hierarchy_id=ip.hierarchyid AND ip.tag=" + stringify(PROP_ID(PR_ENTRYID)) +
		" AND s.guid != SUBSTRING(ip.val_binary, 5, 16)"
		" SET s.guid=SUBSTRING(ip.val_binary, 5, 16)", &aff);
	if (ret != erSuccess)
		return ret;
	ec_log_notice("kf-2888: updated %u rows.", aff);
	return erSuccess;
}

static ECRESULT usmp_shrink_columns(fancydb db)
{
	unsigned int aff = 0;
	ec_log_notice("dbadm: executing action \"usmp-column-shrink\"");
	ec_log_notice("usmp: discovering overly long named properties...");
	auto ret = db->DoUpdate("CREATE TEMPORARY TABLE n (SELECT id, 34049+id AS tag FROM names WHERE LENGTH(namestring) > 185)");
	if (ret != erSuccess)
		return ret;
	for (const auto &tbl : our_proptables) {
		if (adm_quit)
			break;
		ec_log_notice("usmp: purging long namedprops from \"%s\"...", tbl.c_str());
		ret = db->DoDelete("DELETE p FROM " + tbl + " AS p INNER JOIN n ON p.tag=n.tag", &aff);
		if (ret != erSuccess)
			return ret;
		ec_log_notice("usmp: expunged %u rows", aff);
	}
	/* For now, the hope for these tables is that no user has strings longer than 185 */
	if (adm_quit)
		return erSuccess;
	db->DoUpdate_async("usmp: resizing names.namestring...",
		"ALTER TABLE `names` MODIFY COLUMN `namestring` varchar(185) BINARY DEFAULT NULL");
	db->DoUpdate_async("usmp: resizing receivefolder.messageclass...",
		"ALTER TABLE `receivefolder` MODIFY COLUMN `messageclass` varchar(185) NOT NULL DEFAULT ''");
	db->DoUpdate_async("usmp: resizing objectproperty.propname...",
		"ALTER TABLE `objectproperty` MODIFY COLUMN `propname` varchar(185) BINARY NOT NULL");
	db->DoUpdate_async("usmp: resizing objectmvproperty.propname...",
		"ALTER TABLE `objectmvproperty` MODIFY COLUMN `propname` varchar(185) BINARY NOT NULL");
	db->DoUpdate_async("usmp: resizing settings.name...",
		"ALTER TABLE `settings` MODIFY COLUMN `name` varchar(185) BINARY NOT NULL");
	return db->quiesce();
}

static ECRESULT usmp_charset(fancydb db)
{
	ec_log_notice("dbadm: executing action \"usmp-charset\"");
	/*
	 * "CONVERT TO CHARACTER SET" resets the collation, which we do not want.
	 * It is split into individual operations here and the collation is reassured.
	 */
	for (const auto &tbl : {"names", "objectmvproperty", "objectproperty", "settings"}) {
		if (adm_quit)
			break;
		auto ret = db->DoUpdate("ALTER TABLE `"s + tbl + "` DEFAULT CHARSET utf8mb4");
		if (ret != erSuccess)
			return ret;
	}
	if (adm_quit)
		return erSuccess;

	/* Heavy operations follow */
	for (const auto &tbl : {"abchanges", "acl", "changes", "deferredupdate",
	    "hierarchy", "indexedproperties", "lob", "mvproperties",
	    "object", "objectrelation",
	    "outgoingqueue", "properties", "receivefolder", "searchresults",
	    "settings", "singleinstances", "stores", "syncedmessages", "syncs",
	    "tproperties", "users", "versions"})
		db->DoUpdate_async(format("usmp: converting \"%s\" to utf8mb4...", tbl),
			"ALTER TABLE `"s + tbl + "` CONVERT TO CHARSET utf8mb4");

	db->DoUpdate_async("usmp: converting \"names.namestring\" to utf8mb4...",
		"ALTER TABLE `names` MODIFY COLUMN `namestring` varchar(185) CHARACTER SET utf8mb4 BINARY DEFAULT NULL");
	for (const auto &tbl : {"objectproperty", "objectmvproperty"})
		db->DoUpdate_async(format("usmp: converting \"%s.propname\" to utf8mb4...", tbl),
			"ALTER TABLE `"s + tbl + "` MODIFY COLUMN `propname` varchar(185) CHARACTER SET utf8mb4 BINARY NOT NULL");
	db->DoUpdate_async("usmp: converting \"settings.name\" to utf8mb4...",
		"ALTER TABLE `settings` MODIFY COLUMN `name` varchar(185) CHARACTER SET utf8mb4 BINARY NOT NULL");

	auto ret = db->quiesce();
	if (ret != erSuccess)
		return ret;
	if (adm_quit)
		return erSuccess;
	return db->DoUpdate("REPLACE INTO `settings` (`name`, `value`) VALUES ('charset', 'utf8mb4')");
}

static ECRESULT usmp(fancydb db)
{
	auto ret = usmp_shrink_columns(db);
	if (ret != erSuccess)
		return ret;
	return usmp_charset(db);
}

static ECRESULT db_populate(std::shared_ptr<ECConfig> cfg)
{
	std::unique_ptr<ECDatabase> db;
	std::string err;
	auto stats = std::make_shared<ECStatsCollector>(cfg);
	auto fac = std::make_unique<ECDatabaseFactory>(cfg, stats);
	auto ret = fac->CreateDatabaseObject(&unique_tie(db), err);
	if (ret == erSuccess) {
		ec_log_info("Database exists and is not empty. If corrupt, drop it manually.");
		return erSuccess;
	} else if (ret != KCERR_DATABASE_NOT_FOUND) {
		ec_log_err("DB: %s", err.c_str());
		return ec_perror("Failed to connect to database", ret);
	}
	ret = fac->CreateDatabase();
	if (ret != erSuccess)
		return ec_perror("Failed to create database", ret);
	ec_log_notice("Database created and populated.");
	return erSuccess;
}

static void adm_sigterm(int sig)
{
	if (--adm_sigterm_count <= 0) {
		ec_log_crit("Forced termination. The database may be left in an inconsistent state.");
		sigaction(sig, nullptr, nullptr);
		exit(1);
		return;
	}
	ec_log_notice("Received request to terminate. "
		"Please wait until the program has reached a consistent database state. "
		"(This may take a while!) To force stop, reissue the request %u more time(s).",
		adm_sigterm_count);
	adm_quit = true;
}

static bool adm_setup_signals()
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = adm_sigterm;
	sa.sa_flags   = SA_RESTART;
	auto ret = sigaction(SIGINT, &sa, nullptr);
	if (ret < 0) {
		perror("sigaction");
		return false;
	}
	ret = sigaction(SIGTERM, &sa, nullptr);
	if (ret < 0) {
		perror("sigaction");
		return false;
	}
	return true;
}

int main(int argc, char **argv) try
{
	const configsetting_t defaults[] = {
		{"database_engine", "mysql"},
		{"mysql_host", "localhost"},
		{"mysql_port", "3306"},
		{"mysql_user", "root"},
		{"mysql_password", "", CONFIGSETTING_EXACT},
		{"mysql_database", "kopano"},
		{"mysql_socket", ""},
		{"log_file", ""},
		{"log_level", "3", CONFIGSETTING_NONEMPTY | CONFIGSETTING_RELOADABLE},
		{"log_method", ""},
		{"log_timestamp", "1", CONFIGSETTING_RELOADABLE},
		{"mysql_group_concat_max_len", "21844", CONFIGSETTING_RELOADABLE},
		{nullptr, nullptr},
	};
	const char *cfg_file = ECConfig::GetDefaultPath("server.cfg");
	unsigned int opt_parallel = 0;
	int c;
	while ((c = getopt_long(argc, argv, "c:j:", nullptr, nullptr)) >= 0) {
		switch (c) {
		case 'c':
			cfg_file = optarg;
			break;
		case 'j':
			opt_parallel = atoui(optarg);
			break;
		}
	}
	if (opt_parallel == 0)
		opt_parallel = 1;
	auto cfg = std::shared_ptr<ECConfig>(ECConfig::Create(defaults));
	if (!cfg->LoadSettings(cfg_file)) {
		ec_log_err("Errors in config; run kopano-server to see details.");
		return EXIT_FAILURE;
	}
	if (argc < 2) {
		ec_log_notice("No action selected, nothing done.");
		return EXIT_SUCCESS;
	}

	cfg->AddSetting("log_method", "file");
	cfg->AddSetting("log_file", "-");
	auto g_logger = CreateLogger(cfg.get(), argv[0], "kopano-dbadm", false);
	ec_log_set(g_logger);
	if (!ec_log_get()->Log(EC_LOGLEVEL_INFO))
		ec_log_get()->SetLoglevel(EC_LOGLEVEL_INFO);

	auto db = std::make_shared<da_exec>(cfg, opt_parallel);
	if (!adm_setup_signals())
		return EXIT_FAILURE;
	for (size_t i = optind; i < argc; ++i) {
		ECRESULT ret = KCERR_NOT_FOUND;
		if (strcmp(argv[i], "k-1216") == 0)
			ret = k1216(db);
		else if (strcmp(argv[i], "np-defrag") == 0)
			ret = np_defrag(db);
		else if (strcmp(argv[i], "np-remove-highid") == 0)
			ret = np_remove_highid(db);
		else if (strcmp(argv[i], "np-remove-unused") == 0)
			ret = np_remove_unused(db);
		else if (strcmp(argv[i], "np-remove-xh") == 0)
			ret = np_remove_xh(db);
		else if (strcmp(argv[i], "np-repair-dups") == 0)
			ret = np_repair_dups(db);
		else if (strcmp(argv[i], "np-stat") == 0)
			ret = np_stat(db);
		else if (strcmp(argv[i], "index-tags") == 0)
			ret = index_tags(db);
		else if (strcmp(argv[i], "rm-helper-index") == 0)
			ret = remove_helper_index(db);
		else if (strcmp(argv[i], "kc-1375") == 0)
			ret = kc1375(db);
		else if (strcmp(argv[i], "kc-1444") == 0)
			ret = kc_1444(db);
		else if (strcmp(argv[i], "kf-2888") == 0)
			ret = kc_2888(db);
		else if (strcmp(argv[i], "usmp-shrink-columns") == 0)
			ret = usmp_shrink_columns(db);
		else if (strcmp(argv[i], "usmp-charset") == 0)
			ret = usmp_charset(db);
		else if (strcmp(argv[i], "usmp") == 0)
			ret = usmp(db);
		else if (strcmp(argv[i], "populate") == 0)
			ret = db_populate(cfg);
		if (ret == KCERR_NOT_FOUND) {
			ec_log_err("dbadm: unknown action \"%s\"", argv[i]);
			return EXIT_FAILURE;
		}
		if (ret != erSuccess) {
			ec_log_err("dbadm: action failed: %s (%x)", GetMAPIErrorMessage(kcerr_to_mapierr(ret)), ret);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
} catch (...) {
	std::terminate();
}
