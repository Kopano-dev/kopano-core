/*
 * Copyright 2018, Kopano and its licensors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 or
 * (at your option) any later version.
 */
#include <memory>
#include <set>
#include <string>
#include <cassert>
#include <cstdlib>
#include <getopt.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/database.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>

using namespace KC;
using namespace KCHL;

class proptagindex final {
	public:
	proptagindex(std::shared_ptr<KDatabase>);
	~proptagindex();
	private:
	std::shared_ptr<KDatabase> m_db;
	std::set<std::string> m_indexed;
};

static const std::string our_proptables[] = {
	"properties", "tproperties", "mvproperties",
	"indexedproperties", "singleinstances", "lob",
};
static const std::string our_proptables_hier[] = {
	/* tables with a "hierarchyid" column */
	"properties", "tproperties", "mvproperties",
	"indexedproperties", "singleinstances",
};

static ECRESULT hidx_add(KDatabase &db, const std::string &tbl)
{
	ec_log_notice("dbadm: adding temporary helper index on %s", tbl.c_str());
	return db.DoUpdate("ALTER TABLE " + tbl + " ADD INDEX tmptag (tag)");
}

static void hidx_remove(KDatabase &db, const std::string &tbl)
{
	ec_log_notice("dbadm: discard helper index on %s", tbl.c_str());
	db.DoUpdate("ALTER TABLE " + tbl + " DROP INDEX tmptag");
}

proptagindex::proptagindex(std::shared_ptr<KDatabase> db) : m_db(db)
{
	for (const auto &tbl : our_proptables) {
		auto ret = hidx_add(*db.get(), tbl);
		if (ret == erSuccess)
			m_indexed.emplace(tbl);
	}
}

proptagindex::~proptagindex()
{
	for (const auto &tbl : m_indexed)
		hidx_remove(*m_db.get(), tbl.c_str());
}

static ECRESULT index_tags(std::shared_ptr<KDatabase> db)
{
	for (const auto &tbl : our_proptables)
		hidx_add(*db.get(), tbl);
	return erSuccess;
}

static ECRESULT remove_helper_index(std::shared_ptr<KDatabase> db)
{
	for (const auto &tbl : our_proptables)
		hidx_remove(*db.get(), tbl);
	return erSuccess;
}

static ECRESULT np_defrag(std::shared_ptr<KDatabase> db)
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

	ret = db->DoSelect("SELECT MAX(id) - COUNT(id) FROM names WHERE id <= 31485", &result);
	if (ret == erSuccess) {
		row = result.fetch_row();
		if (row != nullptr && row[0] != nullptr)
			ec_log_info("defrag: %zu entries to move", strtoul(row[0], nullptr, 0));
	}
	ret = db->DoSelect("SELECT id FROM names WHERE id <= 31485 ORDER BY id DESC", &result);
	if (ret != erSuccess)
		return ret;
	ec_log_notice("defrag: %zu entries present", result.get_num_rows());
	if (result.get_num_rows() == 0)
		return erSuccess;

	while ((row = result.fetch_row()) != nullptr) {
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
		assert(freemap.erase(newid) == 1);
		ec_log_notice("defrag: moving %u -> %u [names]", oldid, newid);
		fflush(stdout);
		ret = db->DoUpdate("UPDATE names SET id=" + stringify(newid) + " WHERE id=" + stringify(oldid));
		if (ret != erSuccess)
			return ret;
		for (const auto &tbl : our_proptables) {
			ec_log_notice("defrag: moving %u -> %u [%s]", oldid, newid, tbl.c_str());
			fflush(stdout);
			ret = db->DoUpdate("UPDATE " + tbl + " SET tag=" + stringify(newtag) + " WHERE tag=" + stringify(oldtag));
			if (ret != erSuccess)
				return ret;
		}
		auto x = freemap.emplace(oldid);
		assert(x.second);
		++newid;
	}
	/* autoupdates to highest id */
	ret = db->DoUpdate("ALTER TABLE names AUTO_INCREMENT=1");
	if (ret != erSuccess)
		return ret;
	ec_log_notice("defrag: done");
	return erSuccess;
}

static ECRESULT np_remove_highid(KDatabase &db)
{
	/*
	 * This is a no-op for systems where only K-1220 and no K-1219 was
	 * diagnosed.
	 */
	ec_log_notice("dbadm: executing action \"np-remove-highid\"");
	return db.DoUpdate("DELETE FROM names WHERE id > 31485");
}

static ECRESULT np_remove_xh(std::shared_ptr<KDatabase> db)
{
	ec_log_notice("dbadm: executing action \"np-remove-xh\"");
	unsigned int aff = 0;
	auto ret = db->DoUpdate("CREATE TEMPORARY TABLE n (SELECT id, 34049+id AS tag FROM names WHERE id <= 31485 AND guid=0x8603020000000000C000000000000046 AND (namestring LIKE \"X-%\" OR namestring LIKE \"x-%\"))");
	if (ret != erSuccess)
		return ret;
	for (const auto &tbl : our_proptables) {
		ec_log_notice("remove-xh: purging \"%s\"...", tbl.c_str());
		ret = db->DoDelete("DELETE p FROM " + tbl + " AS p INNER JOIN n ON p.tag=n.tag", &aff);
		if (ret != erSuccess)
			return ret;
		ec_log_notice("remove-xh: expunged %u rows.", aff);
	}
	ret = db->DoDelete("DELETE names FROM names INNER JOIN n ON names.id=n.id");
	if (ret != erSuccess)
		return ret;
	db->DoDelete("DROP TABLE n");
	return erSuccess;
}

static ECRESULT np_repair_dups(std::shared_ptr<KDatabase> db)
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
	ec_log_notice("dup: %zu duplicates to repair", result.get_num_rows());
	if (result.get_num_rows() == 0)
		return erSuccess;

	while ((row = result.fetch_row()) != nullptr) {
		unsigned int oldid = strtoul(row[0], nullptr, 0);
		unsigned int newid = strtoul(row[1], nullptr, 0);
		unsigned int oldtag = 0x8501 + oldid, newtag = 0x8501 + newid;
		if (newtag >= 0xFFFF || oldtag >= 0xFFFF)
			continue;

		auto soldtag = stringify(oldtag), snewtag = stringify(newtag);
		unsigned int aff;
		for (const auto &tbl : our_proptables_hier) {
			ec_log_notice("dup: merging #%u into #%u in \"%s\"...", oldid, newid, tbl.c_str());

			/* Remove ambiguous props */
			ret = db->DoUpdate("CREATE TEMPORARY TABLE vt (SELECT hierarchyid FROM " + tbl + " WHERE tag IN (" + soldtag + "," + snewtag + ") GROUP BY hierarchyid HAVING COUNT(*) >= 2)");
			if (ret != erSuccess)
				return ret;
			ret = db->DoDelete("DELETE p FROM " + tbl + " AS p INNER JOIN vt ON p.hierarchyid=vt.hierarchyid AND p.tag IN (" + soldtag + "," + snewtag + ")", &aff);
			if (ret != erSuccess)
				return ret;
			if (aff > 0)
				ec_log_notice("dup: deleted %u ambiguous rows in \"%s\"", aff, tbl.c_str());
			ret = db->DoUpdate("DROP TEMPORARY TABLE vt");
			if (ret != erSuccess)
				return ret;

			/* Merge unambiguous ones */
			ret = db->DoUpdate("UPDATE " + tbl + " SET tag=" + stringify(newtag) + " WHERE tag=" + stringify(oldtag), &aff);
			if (ret != erSuccess)
				return ret;
			if (aff > 0)
				ec_log_notice("dup: updated %u rows in \"%s\"", aff, tbl.c_str());
			ret = db->DoDelete("DELETE FROM " + tbl + " WHERE tag=" + stringify(oldtag));
			if (ret != erSuccess)
				return ret;
		}

		/* Lonely table with "instanceid" instead of "hierarchyid"... */
		for (const std::string &tbl : {"lob"}) {
			ec_log_notice("dup: merging #%u into #%u in \"%s\"...", oldid, newid, tbl.c_str());

			ret = db->DoUpdate("CREATE TEMPORARY TABLE vt (SELECT instanceid FROM " + tbl + " WHERE tag IN (" + soldtag + "," + snewtag + ") GROUP BY instanceid HAVING COUNT(*) >= 2)");
			if (ret != erSuccess)
				return ret;
			ret = db->DoDelete("DELETE p FROM " + tbl + " AS p INNER JOIN vt ON p.instanceid=vt.instanceid AND p.tag IN (" + soldtag + "," + snewtag + ")", &aff);
			if (ret != erSuccess)
				return ret;
			if (aff > 0)
				ec_log_notice("dup: deleted %u ambiguous rows in \"%s\"", aff, tbl.c_str());
			ret = db->DoUpdate("DROP TEMPORARY TABLE vt");
			if (ret != erSuccess)
				return ret;

			ret = db->DoUpdate("UPDATE " + tbl + " SET tag=" + stringify(newtag) + " WHERE tag=" + stringify(oldtag), &aff);
			if (ret != erSuccess)
				return ret;
			if (aff > 0)
				ec_log_notice("dup: updated %u rows in \"%s\"", aff, tbl.c_str());
			ret = db->DoDelete("DELETE FROM " + tbl + " WHERE tag=" + stringify(oldtag));
			if (ret != erSuccess)
				return ret;
		}

		ret = db->DoUpdate("DELETE FROM names WHERE id=" + stringify(oldid));
		if (ret != erSuccess)
			return ret;
	}
	/* Now the names table is clean, but fragmented ... */
	return erSuccess;
}

static ECRESULT np_stat(KDatabase &db)
{
	DB_RESULT result;
	auto ret = db.DoSelect("SELECT MAX(id) FROM `names`", &result);
	if (ret != erSuccess)
		return ret;
	auto row = result.fetch_row();
	assert(row != nullptr && row[0] != nullptr);
	auto top_id = strtoul(row[0], nullptr, 0);

	ret = db.DoSelect("SELECT COUNT(*) FROM `names`", &result);
	if (ret != erSuccess)
		return ret;
	row = result.fetch_row();
	assert(row != nullptr && row[0] != nullptr);
	auto uniq_ids = strtoul(row[0], nullptr, 0);

	ret = db.DoSelect("SELECT COUNT(*) FROM (SELECT 1 FROM `names` "
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

static ECRESULT k1216(std::shared_ptr<KDatabase> db)
{
	proptagindex helper(db);
	auto ret = np_remove_highid(*db.get());
	if (ret != erSuccess)
		return ret;
	ret = np_repair_dups(db);
	if (ret != erSuccess)
		return ret;
	return np_defrag(db);
}

int main(int argc, char **argv)
{
	const configsetting_t defaults[] = {
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
		{nullptr, nullptr},
	};
	const char *cfg_file = ECConfig::GetDefaultPath("server.cfg");
	int c;
	while ((c = getopt_long(argc, argv, "c:", nullptr, nullptr)) >= 0) {
		switch (c) {
		case 'c':
			cfg_file = optarg;
			break;
		}
	}
	auto cfg = ECConfig::Create(defaults);
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
	ec_log_set(CreateLogger(cfg, argv[0], "kopano-dbadm", false));
	auto db = std::make_shared<KDatabase>();
	auto ret = db->Connect(cfg, true, 0, 0);
	if (ret != erSuccess) {
		ec_log_err("db connect failed: %s (%x)", GetMAPIErrorMessage(kcerr_to_mapierr(ret)), ret);
		return ret;
	}
	for (size_t i = optind; i < argc; ++i) {
		ret = KCERR_NOT_FOUND;
		if (strcmp(argv[i], "k-1216") == 0)
			ret = k1216(db);
		else if (strcmp(argv[i], "np-defrag") == 0)
			ret = np_defrag(db);
		else if (strcmp(argv[i], "np-remove-highid") == 0)
			ret = np_remove_highid(*db.get());
		else if (strcmp(argv[i], "np-remove-xh") == 0)
			ret = np_remove_xh(db);
		else if (strcmp(argv[i], "np-repair-dups") == 0)
			ret = np_repair_dups(db);
		else if (strcmp(argv[i], "np-stat") == 0)
			ret = np_stat(*db.get());
		else if (strcmp(argv[i], "index-tags") == 0)
			ret = index_tags(db);
		else if (strcmp(argv[i], "rm-helper-index") == 0)
			ret = remove_helper_index(db);
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
}
