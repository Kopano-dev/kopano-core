/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/database.hpp>
#include <memory>
#include <string>

namespace KC {

class ECConfig;
class ECStatsCollector;
class zcp_versiontuple;

class KC_EXPORT ECDatabase final : public KDatabase {
public:
	ECDatabase(std::shared_ptr<ECConfig>, std::shared_ptr<ECStatsCollector>);
	static ECRESULT	InitLibrary(const char *dir, const char *config_file);
	static void UnloadLibrary(void);
	ECRESULT Connect(void);
	ECRESULT CreateDatabase(void);
	virtual ECRESULT DoSelect(const std::string &query, DB_RESULT *result, bool stream_result = false) override;
	ECRESULT DoSelectMulti(const std::string &query);
	virtual ECRESULT DoDelete(const std::string &query, unsigned int *affected_rows = nullptr) override;
	virtual ECRESULT DoInsert(const std::string &query, unsigned int *insert_id = nullptr, unsigned int *affected_rows = nullptr) override;
	virtual ECRESULT DoSequence(const std::string &seqname, unsigned int ulCount, unsigned long long *first_id) override;
	virtual ECRESULT DoUpdate(const std::string &query, unsigned int *affected_rows = nullptr) override;
	ECRESULT FinalizeMulti(void);
	ECRESULT GetNextResult(DB_RESULT *);
	ECRESULT InitializeDBState(void);
	bool SuppressLockErrorLogging(bool suppress);
	void ThreadEnd(void);
	void ThreadInit(void);
	ECRESULT UpdateDatabase(bool force_update, std::string &report);
	const std::string &get_dbname() const { return m_dbname; }

	private:
	ECRESULT InitializeDBStateInner(void);
	virtual const struct sSQLDatabase_t *GetDatabaseDefs() override;
	ECRESULT GetDatabaseVersion(zcp_versiontuple *);
	ECRESULT GetFirstUpdate(unsigned int *lpulDatabaseRevision);
	ECRESULT UpdateDatabaseVersion(unsigned int ulDatabaseRevision);
	virtual ECRESULT Query(const std::string &q) override;

	std::string error, m_dbname;
	bool m_bForceUpdate = false, m_bFirstResult = false;
	std::shared_ptr<ECConfig> m_lpConfig;
	std::shared_ptr<ECStatsCollector> m_stats;
};

extern KC_EXPORT bool searchfolder_restart_required;

} /* namespace */
