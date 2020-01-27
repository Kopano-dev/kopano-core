/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright 2018 Kopano and its licensors
 */
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <mapidefs.h>
#include <edkmdb.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ECRestriction.h>
#include <kopano/ECThreadPool.h>
#include <kopano/fileutil.hpp>
#include <kopano/MAPIErrors.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <db_cxx.h>
#include "indexer.hpp"
#include "idx_plugin.hpp"
#include "idx_util.hpp"

using namespace std::string_literals;

namespace KC {

enum {
	ST_REINDEX = 1 << 0,
	ST_IS_ROOT = 1 << 1,
	ST_RECURSE_BFS = 1 << 2,
	ST_RECURSE_CVD = 1 << 3,
};

/**
 * An extended worker that has state and holds a server connection open.
 * Without it, ECIndexService would have to keep and manage the connections for
 * use by the tasks that are going to get enqueued on workers.
 */
class ECIndexWorker final : public ECThreadWorker {
	public:
	ECIndexWorker(ECThreadPool *p, ECIndexService *i) : ECThreadWorker(p), m_indexer(i) {}
	virtual bool init();

	private:
	ECIndexService *m_indexer = nullptr;
	KServerContext m_srvctx;
	std::string m_state_db;

	friend class StoreOpener;
};

/**
 * An extended pool that produces ECIndexWorkers instead of stateless workers.
 */
class ECIndexerPool final : public ECThreadPool {
	public:
	ECIndexerPool(ECIndexService *i) : ECThreadPool("worker", 0), m_indexer(i) {}
	virtual std::unique_ptr<ECThreadWorker> make_worker() { return make_unique_nt<ECIndexWorker>(this, m_indexer); }

	private:
	ECIndexService *m_indexer = nullptr;
};

class ECIndexService final : public IIndexer {
	public:
	static HRESULT create(const char *file, IIndexer **);
	ECIndexService();
	~ECIndexService();
	HRESULT service_start() override;
	void service_stop() override;
	std::shared_ptr<ECConfig> get_config() const override { return m_config; }
	std::string exec1(client_state &, const char *cmd) override;
	std::string exec(client_state &, const std::vector<std::string> &cmd) override;

	private:
	HRESULT init(const char *);
	HRESULT initial_sync(bool reindex = false);
	void incr_sync();
	std::string cmd_props();
	std::string cmd_syncrun();
	std::string cmd_scope(client_state &, const std::vector<std::string> &);
	std::string cmd_find(client_state &, const std::vector<std::string> &);
	std::string cmd_suggest(client_state &);
	std::string cmd_query(client_state &);
	void cmd_reindex(const std::string &);

	std::shared_ptr<ECConfig> m_config;
	std::unique_ptr<IIndexerPlugin> m_plugin;
	ECIndexerPool m_pool;
	KServerContext m_srvctx;
	GUID m_server_guid;
	std::vector<std::string> m_reindex_queue;
	std::mutex m_reindex_qlock;
};

/* used for initial sync */
struct SyncState final {
	ECThreadPool *pool = nullptr;
	std::shared_ptr<ECConfig> config;
	std::atomic<size_t> in_flight{0}, processed{0};
};

/* used for initial sync */
struct StoreInfo final {
	std::string outbox, wastebasket, drafts;
};

/**
 * Extension of ECTask that keeps a job count. The initial sync procedure uses
 * this as synchronization to wait for the (initial) job queue to empty.
 */
class ScanTask : public ECTask {
	public:
	ScanTask(std::shared_ptr<SyncState> st, unsigned int flags) :
		m_state(std::move(st))
	{
		++m_state->in_flight;
	}
	virtual ~ScanTask()
	{
		--m_state->in_flight;
		++m_state->processed;
	}

	protected:
	std::shared_ptr<SyncState> m_state;
	unsigned int m_flags = 0;
};

/**
 * A task that will open a store once it runs.
 */
class StoreOpener final : public ScanTask {
	public:
	StoreOpener(std::shared_ptr<SyncState> st, std::string &&seid, unsigned int flags) :
		ScanTask(std::move(st), flags), m_info(std::make_shared<StoreInfo>()), m_seid(std::move(seid))
	{}
	virtual void run();

	private:
	std::shared_ptr<StoreInfo> m_info;
	std::string m_seid;
};

/**
 * A task that scans a chosen folder object.
 */
class FolderScanner final : public ScanTask {
	public:
	FolderScanner(std::shared_ptr<SyncState> &&s, std::shared_ptr<StoreInfo> &&i, object_ptr<IMAPIContainer> &&c, std::string &&feid, unsigned int flags) :
		ScanTask(std::move(s), flags), m_info(std::move(i)), m_cont(std::move(c)), m_feid(std::move(feid))
	{}
	virtual void run();

	private:
	void enlist_children();
	void index_me();

	std::shared_ptr<StoreInfo> m_info;
	object_ptr<IMAPIContainer> m_cont;
	std::string m_feid;
};

/**
 * A task that will opens a folder once it runs.
 */
class FolderOpener final : public ScanTask {
	public:
	FolderOpener(std::shared_ptr<SyncState> s, std::shared_ptr<StoreInfo> i, object_ptr<IMAPIContainer> c, std::string &&feid, unsigned int flags) :
		ScanTask(std::move(s), flags), m_info(std::move(i)), m_cont(std::move(c)), m_feid(std::move(feid))
	{}
	virtual void run();

	private:
	std::shared_ptr<StoreInfo> m_info;
	object_ptr<IMAPIContainer> m_cont;
	std::string m_feid;
};

static constexpr const configsetting_t idx_defaults[] = {
	{"index_attachments", "yes", CONFIGSETTING_RELOADABLE},
	{"index_attachment_extension_filter", "", CONFIGSETTING_UNUSED},
	{"index_attachment_mime_filter", "", CONFIGSETTING_UNUSED},
	{"index_attachment_max_size", "16777216", CONFIGSETTING_RELOADABLE},
	{"index_attachment_parser", "", CONFIGSETTING_UNUSED},
	{"index_attachment_parser_max_memory", "", CONFIGSETTING_UNUSED},
	{"index_attachment_parser_max_cputime", "", CONFIGSETTING_UNUSED},
	{"index_exclude_properties", "0x007d 0x0064 0x0c1e 0x0075 0x678e 0x678f 0x001a", CONFIGSETTING_RELOADABLE},
	{"index_path", "/var/lib/kopano/search"},
	{"index_processes", "0"},
	{"limit_results", "1000"},
	{"optimize_age", "", CONFIGSETTING_UNUSED},
	{"optimize_start", "", CONFIGSETTING_UNUSED},
	{"optimize_stop", "", CONFIGSETTING_UNUSED},
	{"search_engine", "xapian"},
	{"suggestions", "yes"},
	{"index_junk", "yes"},
	{"index_drafts", "yes"},
	{"term_cache_size", "64000000"},

	{"discovery_mode", "bfs"},
	{"server_socket", "default:"},

	/* Network frontend */
	{"server_bind_name", "", CONFIGSETTING_OBSOLETE},
	{"indexer_listen", "unix:/var/run/kopano/search.sock"},
	{"log_method", "file"},
	{"log_level", "3"},
	{"log_file", "/var/log/kopano/search.log"},
	{"log_timestamp", "yes"},
	{"run_as_user", "kopano"},
	{"run_as_group", "kopano"},
	{nullptr, nullptr},
};

static constexpr const SizedSPropTagArray(5, spta_mbox) =
	{5, {PR_ENTRYID, PR_MAILBOX_OWNER_ENTRYID, PR_EC_STORETYPE,
	PR_OBJECT_TYPE, PR_DISPLAY_NAME_W}};

static std::string db_get(const char *file, const char *key)
{
	Db db(nullptr, 0);
	db.open(nullptr, file, nullptr, DB_HASH, DB_CREATE, 0);
	Dbt xk(const_cast<char *>(key), strlen(key)), xv{};
	db.get(nullptr, &xk, &xv, 0);
	return std::string(reinterpret_cast<char *>(xv.get_data()), xv.get_size());
}

static HRESULT db_put(const char *file, const char *key, const char *value)
{
	Db db(nullptr, 0);
	db.open(nullptr, file, nullptr, DB_HASH, DB_CREATE, 0);
	Dbt xk(const_cast<char *>(key), strlen(key)), xv(const_cast<char *>(value), strlen(value));
	return db.put(nullptr, &xk, &xv, 0) == 0 ? hrSuccess : MAPI_E_CALL_FAILED;
}

#if 0
ServerImporter::ServerImporter(const std::string &serverid, std::shared_ptr<ECConfig> config)
{
	m_mapping_db = config->GetSetting("index_path") + serverid + "_mapping");
}

ServerImporter::update(
	queue({0, item.storeid, item.folder.entryid});
}

ServerImporter::del()
{
}
#endif

HRESULT IIndexer::create(const char *file, IIndexer **out)
{
	return ECIndexService::create(file, out);
}

ECIndexService::ECIndexService() : m_pool(this)
{}

ECIndexService::~ECIndexService()
{
	service_stop();
}

HRESULT ECIndexService::create(const char *cfg, IIndexer **out)
{
	std::unique_ptr<ECIndexService> ix(new(std::nothrow) ECIndexService);
	if (ix == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto ret = ix->init(cfg);
	if (ret == hrSuccess)
		*out = ix.release();
	return ret;
}

HRESULT ECIndexService::init(const char *cfg_file)
{
	m_config.reset(ECConfig::Create(idx_defaults));
	if (m_config == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto ret = m_config->LoadSettings(cfg_file, cfg_file != nullptr) ? hrSuccess : MAPI_E_CALL_FAILED;
	if (ret != hrSuccess)
		return ret;
	auto index_path = m_config->GetSetting("index_path");
	if (CreatePath(index_path) < 0) {
		ec_log_err("Could not create \"%s\": %s\n", index_path, strerror(errno));
		return MAPI_E_CALL_FAILED;
	}
	m_plugin.reset(make_xapian_plugin(index_path));
	return hrSuccess;
}

/**
 * Starts the search component. Worker threads will be spawned. Initial search
 * database population is performed if the search database is empty.
 * Incremental synchronization will start in the background as another thread.
 */
HRESULT ECIndexService::service_start()
{
	if (m_plugin == nullptr)
		return MAPI_E_CALL_FAILED;
	auto ncpus = atoui(m_config->GetSetting("index_processes"));
	if (ncpus == 0)
		ncpus = 1;
	m_pool.set_thread_count(ncpus, 0, false);

	m_srvctx.m_app_misc = "kindexd";
	m_srvctx.m_host = m_config->GetSetting("server_socket");
	auto ret = m_srvctx.logon();
	if (ret != hrSuccess) {
		ec_log_err("logon");
		return false;
	}
	ret = server_guid(m_srvctx.m_admstore, m_server_guid);
	if (ret != hrSuccess) {
		ec_log_err("server_guid");
		return false;
	}
	auto state_db = m_config->GetSetting("index_path") + "/"s + bin2hex(sizeof(m_server_guid), &m_server_guid) + "_state";
	std::string state;
	try {
		state = db_get(state_db.c_str(), "SERVER");
	} catch (const DbException &ex) {
		ec_log_err("Cannot access \"%s\": %s (errno %d: %s)", state_db.c_str(),
			ex.what(), ex.get_errno(), strerror(ex.get_errno()));
		return MAPI_E_CALL_FAILED;
	}
	if (!state.empty()) {
		ec_log_info("No previous state (%s)", state_db.c_str());
	} else {
		std::string new_state;
		ret = ics_state(m_srvctx.m_admstore, false, new_state);
		if (ret != hrSuccess)
			return kc_perror("ics_state", ret);
		initial_sync();
		db_put(state_db.c_str(), "SERVER", new_state.c_str());
		ec_log_info("Saved server sync state %s", bin2hex(new_state).c_str());
	}

	pthread_t tid;
	auto err = pthread_create(&tid, nullptr, [](void *a) -> void * { static_cast<ECIndexService *>(a)->incr_sync(); return nullptr; }, this);
	if (err != 0) {
		ec_log_err("pthread create: %s", strerror(err));
		return MAPI_E_CALL_FAILED;
	}
	return hrSuccess;
}

void ECIndexService::service_stop()
{
	
}

HRESULT ECIndexService::initial_sync(bool reindex)
{
	object_ptr<IExchangeManageStore> ems;
	auto ret = m_srvctx.m_ecobject->QueryInterface(IID_IExchangeManageStore, &~ems);
	if (ret != hrSuccess)
		return kc_perror("QueryInterface EMS", ret);
	object_ptr<IMAPITable> storetbl;
	ret = ems->GetMailboxTable(nullptr, &~storetbl, MAPI_DEFERRED_ERRORS);
	if (ret != hrSuccess)
		return kc_perror("GetMailboxTable", ret);
	ret = storetbl->SetColumns(spta_mbox, TBL_BATCH);
	if (ret != hrSuccess)
		return kc_perror("SetColumns", ret);

	auto disc_mode = m_config->GetSetting("discovery_mode");
	auto do_cvd = strcmp(disc_mode, "cvd") == 0;
	ec_log_info("Starting initial sync");
	auto st = std::make_shared<SyncState>();
	st->pool = &m_pool;
	st->config = m_config;

	for (const auto &row : mapitable_range(storetbl.get())) {
		const auto &eid = row.lpProps[0].Value.bin;
		unsigned int flags = reindex ? ST_REINDEX : 0;
		flags |= do_cvd ? ST_RECURSE_CVD : ST_RECURSE_BFS;
		auto task = new StoreOpener(st, std::string(reinterpret_cast<const char *>(eid.lpb), eid.cb), flags);
		m_pool.enqueue(task, true);
	}
	while (st->in_flight > 0) {
		ec_log_debug("initial_sync: %zu objects done so far, %zu more are scheduled", st->processed.load(), st->in_flight.load());
		Sleep(200);
	}
	ec_log_debug("initial_sync: completed after %zu objects", st->processed.load());
	return hrSuccess;
}

void ECIndexService::incr_sync()
{
	
}

std::string ECIndexService::cmd_props()
{
	return "OK:"s + m_config->GetSetting("index_exclude_properties");
}

std::string ECIndexService::cmd_scope(client_state &cs, const std::vector<std::string> &arg)
{
	if (arg.size() < 3)
		return "ERROR: Not enough arguments";
	auto svg = hex2bin(arg[1]);
	if (svg.size() != sizeof(cs.m_server_guid))
		return "ERROR: Invalid server GUID";
	auto stg = hex2bin(arg[2]);
	if (stg.size() != sizeof(cs.m_store_guid))
		return "ERROR: Invalid store GUID";
	memcpy(&cs.m_server_guid, svg.data(), sizeof(GUID));
	memcpy(&cs.m_store_guid, stg.data(), sizeof(GUID));
	cs.m_folder_ids.clear();
	std::transform(arg.cbegin() + 3, arg.cend(), std::back_inserter(cs.m_folder_ids),
		[](const auto &s) { return atoui(s.c_str()); });
	return "OK:";
}

std::string ECIndexService::cmd_find(client_state &cs, const std::vector<std::string> &arg)
{
	std::vector<unsigned int> fields;
	auto it = arg.cbegin() + 1;
	cs.m_orig.clear();

	for (; it != arg.cend(); ++it) {
		char *end = nullptr;
		auto id = strtoul(it->c_str(), &end, 0);
		if (end == nullptr || *end == '\0') {
			fields.push_back(id);
			continue;
		} else if (*end == ':') {
			fields.push_back(id);
			if (end[1] != '\0')
				cs.m_orig = strToLower(end + 1);
			++it;
			break;
		} else {
			return "ERROR: wrong syntax";
		}
	}

	for (; it != arg.cend(); ++it)
		cs.m_orig.append(" " + strToLower(*it));
	auto terms = m_plugin->extract_terms(cs.m_orig.c_str());
	if (terms.size() > 32)
		terms.resize(32);
	if (fields.size() != 0 && terms.size() != 0)
		cs.m_fields_terms.push_back({std::move(fields), std::move(terms)});
	return "OK:";
}

std::string ECIndexService::cmd_suggest(client_state &cs)
{
	if (!parseBool(get_config()->GetSetting("suggestions")) ||
	    cs.m_fields_terms.size() != 1)
		return "OK:";
	return "OK:" + m_plugin->suggest(cs.m_server_guid, cs.m_store_guid, cs.m_fields_terms[0].second, cs.m_orig);
}

std::string ECIndexService::cmd_query(client_state &cs)
{
	std::vector<std::string> rst;
	if (cs.m_folder_ids.size() > 0) {
		auto s = "(folderid:"s + stringify(cs.m_folder_ids[0]);
		for (size_t i = 1; i < cs.m_folder_ids.size(); ++i)
			s += " OR folderid:" + stringify(cs.m_folder_ids[i]);
		s += ")";
		rst.push_back(std::move(s));
	}
	for (const auto &pair : cs.m_fields_terms) {
		std::vector<std::string> fz;
		for (const auto &f : pair.first) {
			std::vector<std::string> fr;
			for (const auto &term : pair.second)
				fr.push_back("mapi" + stringify(f) + ":" + term + "*");
			fz.push_back("(" + kc_join(fr, " AND ") + ")");
		}
		rst.push_back("(" + kc_join(fz, " OR ") + ")");
	}
	auto query = kc_join(rst, " AND ");
	std::string ret = "OK:";
	for (auto &&id : m_plugin->search(cs.m_server_guid, cs.m_store_guid, cs.m_folder_ids, cs.m_fields_terms, query))
		ret += " " + std::move(id);
	return ret;
}

void ECIndexService::cmd_reindex(const std::string &guid)
{
	auto disc_mode = m_config->GetSetting("discovery_mode");
	auto st = std::make_shared<SyncState>();
	st->pool = &m_pool;
	st->config = m_config;
	unsigned int flags = strcmp(disc_mode, "cvd") == 0 ? ST_RECURSE_CVD : ST_RECURSE_BFS;
	flags |= ST_REINDEX;
	auto task = new StoreOpener(st, hex2bin(guid), flags);
	m_pool.enqueue(task, true);
	ec_log_info("Store \"%s\" queued for reindexing", guid.c_str());
}

std::string ECIndexService::exec1(client_state &cs, const char *s)
{
	return exec(cs, tokenize(s, " "));
}

std::string ECIndexService::exec(client_state &cs, const std::vector<std::string> &arg)
{
	if (arg.size() == 0)
		return "ERROR: no command";
	if (arg[0] == "PROPS")
		return "OK:"s + m_config->GetSetting("index_exclude_properties");
	if (arg[0] == "SYNCRUN")
		return "OK:";
	if (arg[0] == "SCOPE")
		return cmd_scope(cs, arg);
	if (arg[0] == "FIND")
		return cmd_find(cs, arg);
	if (arg[0] == "SUGGEST")
		return cmd_suggest(cs);
	if (arg[0] == "QUERY")
		return cmd_query(cs);
	if (arg[0] == "REINDEX") {
		if (arg.size() > 0)
			cmd_reindex(arg[1]);
		return "OK:";
	}
	ec_log_err("Unknown indexer command \"%s\"", kc_join(arg, "\" \"").c_str());
	return "ERROR: unknown command";
}

bool ECIndexWorker::init()
{
	set_thread_name(pthread_self(), "IndexWorker");
	auto cfg = m_indexer->get_config();
	m_srvctx.m_app_misc = "kindexd";
	m_srvctx.m_host = cfg->GetSetting("server_socket");
	m_srvctx.m_ssl_keyfile = cfg->GetSetting("ssl_key_file", "", nullptr);
	m_srvctx.m_ssl_keypass = cfg->GetSetting("ssl_key_pass", "", nullptr);
	auto ret = m_srvctx.logon();
	if (ret != hrSuccess) {
		ec_log_err("logon");
		return false;
	}
	GUID guid;
	ret = server_guid(m_srvctx.m_admstore, guid);
	if (ret != hrSuccess) {
		ec_log_err("Could not determine server guid");
		return false;
	}
	m_state_db = cfg->GetSetting("index_path") + "/"s + bin2hex(sizeof(guid), &guid) + "_state";
	return true;
}

void StoreOpener::run()
{
	auto worker = static_cast<ECIndexWorker *>(m_worker);
	object_ptr<IMsgStore> store;
	auto ret = worker->m_srvctx.m_session->OpenMsgStore(0, m_seid.size(), reinterpret_cast<const ENTRYID *>(m_seid.data()), &iid_of(store), 0, &~store);
	if (ret != hrSuccess)
		return;
	object_ptr<IMAPIContainer> root;
	unsigned int objtype = 0;
	ret = store->OpenEntry(0, nullptr, &iid_of(root), 0, &objtype, &~root);
	if (ret != hrSuccess || objtype != MAPI_FOLDER)
		return;
	static const SizedSPropTagArray(4, tags) = {4, {PR_ENTRYID, PR_IPM_OUTBOX_ENTRYID, PR_IPM_WASTEBASKET_ENTRYID, PR_IPM_DRAFTS_ENTRYID}};
	unsigned int nvals = 0;
	memory_ptr<SPropValue> prop;
	ret = store->GetProps(tags, 0, &nvals, &~prop);
	if (FAILED(ret) || prop == nullptr || prop[0].ulPropTag != PR_ENTRYID)
		return;
	if (prop[1].ulPropTag == tags.aulPropTag[1])
		m_info->outbox.assign(reinterpret_cast<char *>(prop[1].Value.bin.lpb), prop[1].Value.bin.cb);
	if (prop[2].ulPropTag == tags.aulPropTag[2])
		m_info->wastebasket.assign(reinterpret_cast<char *>(prop[2].Value.bin.lpb), prop[2].Value.bin.cb);
	if (prop[3].ulPropTag == tags.aulPropTag[3])
		m_info->drafts.assign(reinterpret_cast<char *>(prop[3].Value.bin.lpb), prop[3].Value.bin.cb);
	auto reid = std::string(reinterpret_cast<char *>(prop[0].Value.bin.lpb), prop[0].Value.bin.cb);
	FolderScanner(std::move(m_state), std::move(m_info), std::move(root), std::move(reid), m_flags | ST_IS_ROOT).run();
}

void FolderScanner::run()
{
	/* Scan for children */
	if (m_flags & (ST_IS_ROOT | ST_RECURSE_BFS))
		enlist_children();
	index_me();
}

void FolderScanner::enlist_children()
{
	object_ptr<IMAPITable> tbl;
	auto ret = m_cont->GetHierarchyTable((m_flags & ST_RECURSE_CVD) ? CONVENIENT_DEPTH : 0, &~tbl);
	if (ret != hrSuccess)
		return;
	ret = tbl->SetColumns(spta_mbox, TBL_BATCH);
	if (ret != hrSuccess)
		return;
	SPropValue objtype;
	objtype.ulPropTag = PR_OBJECT_TYPE;
	objtype.Value.ul = MAPI_FOLDER;
	ret = ECPropertyRestriction(RELOP_EQ, PR_OBJECT_TYPE, &objtype, ECRestriction::Cheap).RestrictTable(tbl);
	if (ret != hrSuccess)
		return;

	for (const auto &row : mapitable_range(tbl.get())) {
		const auto &eid = row.lpProps[0].Value.bin;
		auto task = new FolderOpener(m_state, m_info, m_cont, std::string(reinterpret_cast<const char *>(eid.lpb), eid.cb), m_flags & ~ST_IS_ROOT);
		m_state->pool->enqueue(task, true);
	}
}

void FolderScanner::index_me()
{
	if (m_feid == m_info->outbox)
		return;
	if (m_feid == m_info->wastebasket /* && !config[index_junk] */)
		return;
	if (m_feid == m_info->drafts /* && !config[index_drafts] */)
		return;
	//ec_log_info("Syncing folder %s", "%SSSS");
	auto sugg = m_state->config->GetSetting("suggestions")[0] != '\0' && m_feid != m_info->wastebasket;
//	auto importer = FolderImporter(
	
}

void FolderOpener::run()
{
	unsigned int objtype = 0;
	object_ptr<IMAPIContainer> child;
	auto ret = m_cont->OpenEntry(m_feid.size(), reinterpret_cast<const ENTRYID *>(m_feid.data()), &iid_of(child), 0, &objtype, &~child);
	if (ret != hrSuccess || objtype != MAPI_FOLDER) {
		ec_log_notice("No open folder %s type %u: %s", bin2hex(m_feid.size(), m_feid.data()).c_str(), objtype, GetMAPIErrorMessage(ret));
		return;
	}
	FolderScanner(std::move(m_state), std::move(m_info), std::move(child), std::move(m_feid), m_flags).run();
}

} /* namespace */
