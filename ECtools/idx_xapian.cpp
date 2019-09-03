/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2018, Kopano and its licensors
 */
#include <chrono>
#include <new>
#include <utility>
#include <cctype>
#include <libHX/io.h>
#include <kopano/ECLogger.h>
#include <kopano/stringutil.h>
#include <kopano/timeutil.hpp>
#include <kopano/UnixUtil.h>
#include <xapian.h>
#include "indexer.hpp"
#include "idx_plugin.hpp"

namespace KC {

class ECXapianIndexer final : public IIndexerPlugin {
	public:
	ECXapianIndexer(const char *index_path);
	virtual std::vector<std::string> extract_terms(const char *) override;
	virtual std::vector<std::string> search(const GUID &server, const GUID &store, const std::vector<unsigned int> &folders, const FIELDTERMS &, const std::string &query, size_t limit) override;
	virtual std::string suggest(const GUID &server, const GUID &store, const std::vector<std::string> &terms, const std::string &orig) override;
	virtual void update_doc(const index_doc &) override;
	virtual void delete_doc(const index_doc &) override;
	virtual void commit(const std::string &sugg) override;
	virtual void reindex(const GUID &server, const GUID &store) override;

	private:
	std::string mkpath(const GUID &server, const GUID &store) const;

	std::string index_path;
	std::vector<index_doc> m_updates, m_deletes;
};

IIndexerPlugin *make_xapian_plugin(const char *path)
{
	return new(std::nothrow) ECXapianIndexer(path);
}

ECXapianIndexer::ECXapianIndexer(const char *p) :
	index_path(p)
{}

std::string ECXapianIndexer::mkpath(const GUID &server, const GUID &store) const
{
	return index_path + "/" + bin2hex(sizeof(server), &server) + "-" +
	       bin2hex(sizeof(store), &store);
}

std::vector<std::string> ECXapianIndexer::extract_terms(const char *text)
{
	Xapian::Document doc;
	Xapian::TermGenerator gen;
	gen.set_document(doc);
	std::string tx = text;
	/* xapian sees '_' as a word-character (to search for identifiers in source code) */
	std::replace(tx.begin(), tx.end(), '_', ' ');
	gen.index_text(tx);
	return {doc.termlist_begin(), doc.termlist_end()};
}

std::vector<std::string> ECXapianIndexer::search(const GUID &server, const GUID &store,
    const std::vector<unsigned int> &folders, const FIELDTERMS &field_terms,
    const std::string &query, size_t limit)
{
	Xapian::Database db;
	try {
		db = Xapian::Database(mkpath(server, store));
	} catch (Xapian::DatabaseError &) {
		ec_log_err("Xapian cannot open the database");
		return {};
	}

	Xapian::QueryParser qp;
	qp.add_prefix("sourcekey", "XK:");
	qp.add_prefix("folderid", "XF:");
	for (const auto &p : field_terms)
		for (auto f : p.first)
			qp.add_prefix(format("mapi%d", f), format("XM%d:", f));
	qp.set_database(db);
	auto enq = Xapian::Enquire(db);
	enq.set_query(qp.parse_query(query, qp.FLAG_BOOLEAN | qp.FLAG_PHRASE | qp.FLAG_WILDCARD));
	std::vector<std::string> matches;
	for (auto docid : enq.get_mset(0, limit ? limit : db.get_doccount()))
		matches.push_back(db.get_document(docid).get_value(0));
	return matches;
}

std::string ECXapianIndexer::suggest(const GUID &server, const GUID &store,
    const std::vector<std::string> &terms, const std::string &orig)
{
	Xapian::Database db;
	try {
		db = Xapian::Database(mkpath(server, store));
	} catch (Xapian::DatabaseError &e) {
		ec_log_err("Xapian cannot open the database");
		return {};
	}

	auto newterms = terms;
	auto newrig = orig;
	std::sort(newterms.begin(), newterms.end(), [](const std::string &a, const std::string &b) { return a.size() < b.size(); });
	for (const auto &term : newterms) {
		auto sug = db.get_spelling_suggestion(term);
		if (sug.empty())
			continue;
		auto pos = newrig.find(term);
		if (pos != -1)
			newrig.replace(pos, term.size(), sug);
	}
	return newrig;
}

void ECXapianIndexer::update_doc(const index_doc &doc)
{
	m_updates.push_back(doc);
}

void ECXapianIndexer::delete_doc(const index_doc &doc)
{
	m_deletes.push_back(doc);
}

void ECXapianIndexer::commit(const std::string &sugg)
{
	if (m_updates.empty() && m_deletes.empty())
		return;
	auto t0 = std::chrono::steady_clock::now();
	auto updates = std::move(m_updates);
	auto deletes = std::move(m_deletes);
	Xapian::WritableDatabase db;
	try {
		/* we assume here that all data is from the same store */
		const index_doc &doc = updates.size() > 0 ? *updates.begin() : *deletes.begin();
		db = Xapian::WritableDatabase(mkpath(doc.serverid, doc.storeid), Xapian::DB_CREATE_OR_OPEN);
	} catch (Xapian::DatabaseError &) {
		ec_log_err("Xapian could not open database");
		return;
	}
	Xapian::TermGenerator tg;
	tg.set_database(db);
	if (!sugg.empty())
		tg.set_flags(tg.FLAG_SPELLING);
	for (const auto &doc : updates) {
		Xapian::Document xdoc;
		tg.set_document(xdoc);
		for (const auto &p : doc.items) {
			if (p.first.compare(0, 4, "mapi") != 0)
				continue;
			auto v = p.second;
			std::replace(v.begin(), v.end(), '_', ' ');
			/* Add to full-text. Needed for spelling dictionary? */
			tg.index_text_without_positions(v);
			tg.index_text_without_positions(v, 1, "XM" + p.first.substr(0, 4) + ":");
		}
		xdoc.add_value(0, stringify(doc.docid));
		auto xk = "XK:" + bin2hex(sizeof(doc.sourcekey), &doc.sourcekey);
		std::transform(&xk[3], &xk[xk.size()], &xk[3], [](char c) { return tolower(c); });
		xdoc.add_term(xk);
		xdoc.add_term("XF:" + stringify(doc.folderid));
		xdoc.set_data(doc.data);
		db.replace_document(std::move(xk), std::move(xdoc));
	}
	for (const auto &doc : deletes) {
		auto xk = "XK:" + bin2hex(sizeof(doc.sourcekey), &doc.sourcekey);
		std::transform(&xk[3], &xk[xk.size()], &xk[3], [](char c) { return tolower(c); });
		db.delete_document(xk);
	}
	ec_log_debug("Commit took %.2f seconds (%zu items)",
		dur2dbl(decltype(t0)::clock::now() - t0), updates.size());
}

void ECXapianIndexer::reindex(const GUID &server, const GUID &store)
{
	auto path = mkpath(server, store);
	ec_log_info("Removing \"%s\"", path.c_str());
	auto ret = HX_rrmdir(path.c_str());
	if (ret < 0)
		ec_log_err("Removal of \"%s\" failed: %s", path.c_str(), strerror(-ret));
}

} /* namespace */
