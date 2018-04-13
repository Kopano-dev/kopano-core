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

class proptagindex final {
	public:
	proptagindex(std::shared_ptr<KDatabase>);
	~proptagindex();
	private:
	std::shared_ptr<KDatabase> m_db;
	std::set<std::string> m_indexed;
};

static std::unique_ptr<proptagindex> our_proptagidx;
static const std::string our_proptables[] = {
	"properties", "tproperties", "mvproperties",
	"indexedproperties", "singleinstances", "lob",
};

proptagindex::proptagindex(std::shared_ptr<KDatabase> db) : m_db(db)
{
	for (const auto &tbl : our_proptables) {
		printf("dbadm: adding temporary helper index on %s\n", tbl.c_str());
		auto ret = db->DoUpdate("ALTER TABLE " + tbl + " ADD INDEX tmptag (tag)");
		if (ret == erSuccess)
			m_indexed.emplace(tbl);
	}
}

proptagindex::~proptagindex()
{
	for (const auto &tbl : m_indexed) {
		printf("dbadm: discard helper index on %s\n", tbl.c_str());
		m_db->DoUpdate("ALTER TABLE " + tbl + " DROP INDEX tmptag");
	}
}

static ECRESULT np_defrag(std::shared_ptr<KDatabase> db)
{
	DB_RESULT result;
	DB_ROW row;
	std::set<unsigned int> freemap;
	printf("dbadm: executing action \"np-defrag\"\n");

	for (unsigned int i = 1; i <= 31485; ++i)
		freemap.emplace(i);
	auto ret = db->DoSelect("SELECT id FROM names WHERE id <= 31485", &result);
	if (ret != erSuccess)
		return ret;
	while ((row = result.fetch_row()) != nullptr) {
		auto x = freemap.erase(strtoul(row[0], nullptr, 0));
		assert(x == 1);
	}

	ret = db->DoSelect("SELECT id FROM names WHERE id <= 31485 ORDER BY id", &result);
	if (ret != erSuccess)
		return ret;
	printf("defrag: %zu IDs\n", result.get_num_rows());
	if (result.get_num_rows() == 0)
		return erSuccess;
	if (our_proptagidx == nullptr)
		our_proptagidx.reset(new proptagindex(db));

	unsigned int newid = 1;
	while ((row = result.fetch_row()) != nullptr) {
		unsigned int oldid = strtoul(row[0], nullptr, 0);
		unsigned int oldtag = 0x8501 + oldid;
		unsigned int newtag = 0x8501 + newid;
		if (oldtag >= 0xFFFF || newtag >= 0xFFFF)
			continue;
		if (oldid == newid) {
			++newid;
			continue;
		}
		assert(oldid > newid);
		/* e.g. oldid=4 newid=3 */
		assert(freemap.erase(newid) == 1);
		printf("defrag: moving %u -> %u (names)", oldid, newid);
		fflush(stdout);
		ret = db->DoUpdate("UPDATE names SET id=" + stringify(newid) + " WHERE id=" + stringify(oldid));
		if (ret != erSuccess)
			return ret;
		for (const auto &tbl : our_proptables) {
			printf(" (%s)", tbl.c_str());
			fflush(stdout);
			ret = db->DoUpdate("UPDATE " + tbl + " SET tag=" + stringify(newtag) + " WHERE tag=" + stringify(oldtag));
			if (ret != erSuccess)
				return ret;
		}
		putchar('\n');
		auto x = freemap.emplace(oldid);
		assert(x.second);
		++newid;
	}
	/* autoupdates to highest id */
	ret = db->DoUpdate("ALTER TABLE names AUTO_INCREMENT=1");
	if (ret != erSuccess)
		return ret;
	printf("defrag: done\n");
	return erSuccess;
}

static ECRESULT np_remove_highid(KDatabase &db)
{
	/*
	 * This is a no-op for systems where only K-1220 and no K-1219 was
	 * diagnosed.
	 */
	printf("dbadm: executing action \"np-remove-highid\"\n");
	return db.DoUpdate("DELETE FROM names WHERE id > 31485");
}

static ECRESULT np_remove_xh(std::shared_ptr<KDatabase> db)
{
	printf("dbadm: executing action \"np-remove-xh\"\n");
	unsigned int aff = 0;
	if (our_proptagidx == nullptr)
		our_proptagidx.reset(new proptagindex(db));
	auto ret = db->DoUpdate("CREATE TEMPORARY TABLE n (SELECT id, 34049+id AS tag FROM names WHERE id <= 31485 AND guid=0x8603020000000000C000000000000046 AND (namestring LIKE \"X-%\" OR namestring LIKE \"x-%\"))");
	if (ret != erSuccess)
		return ret;
	for (const auto &tbl : our_proptables) {
		printf("remove-xh: purging \"%s\"...\n", tbl.c_str());
		ret = db->DoDelete("DELETE p FROM " + tbl + " AS p INNER JOIN n ON p.tag=n.tag", &aff);
		if (ret != erSuccess)
			return ret;
		printf("remove-xh: expunged %u rows.\n", aff);
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
	printf("dbadm: executing action \"np-repair-dups\"\n");

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
	printf("dup: %zu duplicates to repair\n", result.get_num_rows());
	if (result.get_num_rows() == 0)
		return erSuccess;
	if (our_proptagidx == nullptr)
		our_proptagidx.reset(new proptagindex(db));

	while ((row = result.fetch_row()) != nullptr) {
		unsigned int oldid = strtoul(row[0], nullptr, 0);
		unsigned int newid = strtoul(row[1], nullptr, 0);
		unsigned int oldtag = 0x8501 + oldid, newtag = 0x8501 + newid;
		if (newtag >= 0xFFFF || oldtag >= 0xFFFF)
			continue;
		for (const auto &tbl : our_proptables) {
			printf("dup: merging %u into %u (%s)...\n", oldid, newid, tbl.c_str());
			ret = db->DoUpdate("UPDATE properties SET tag=" + stringify(newtag) + " WHERE tag=" + stringify(oldtag));
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
		{nullptr, nullptr},
	};
	const char *cfg_file = ECConfig::GetDefaultPath("server.cfg");
	int c;
	while ((c = getopt_long(argc, argv, "c", nullptr, nullptr)) >= 0) {
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

	auto db = std::make_shared<KDatabase>();
	auto ret = db->Connect(cfg, true, 0, 0);
	if (ret != erSuccess) {
		ec_log_err("db connect failed: %s (%x)", GetMAPIErrorMessage(kcerr_to_mapierr(ret)), ret);
		return ret;
	}
	for (size_t i = 1; i < argc; ++i) {
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
