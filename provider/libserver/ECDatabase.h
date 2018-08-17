/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECDATABASE_H
#define ECDATABASE_H

#include <kopano/zcdefs.h>
#include <kopano/database.hpp>
#include <memory>
#include <string>

namespace KC {

class ECConfig;
class ECStatsCollector;
class zcp_versiontuple;

class _kc_export ECDatabase _kc_final : public KDatabase {
public:
	ECDatabase(std::shared_ptr<ECConfig>, std::shared_ptr<ECStatsCollector>);
	virtual ~ECDatabase(void);
	static ECRESULT	InitLibrary(const char *dir, const char *config_file);
	static void UnloadLibrary(void);
	virtual kd_trans Begin(ECRESULT &) _kc_override;
	virtual ECRESULT Commit(void) _kc_override;
	ECRESULT Connect(void);
	ECRESULT CreateDatabase(void);
	virtual ECRESULT DoSelect(const std::string &query, DB_RESULT *result, bool stream_result = false) _kc_override;
	ECRESULT DoSelectMulti(const std::string &query);
	virtual ECRESULT DoDelete(const std::string &query, unsigned int *affected_rows = nullptr) _kc_override;
	virtual ECRESULT DoInsert(const std::string &query, unsigned int *insert_id = nullptr, unsigned int *affected_rows = nullptr) _kc_override;
	virtual ECRESULT DoSequence(const std::string &seqname, unsigned int ulCount, unsigned long long *first_id) _kc_override;
	virtual ECRESULT DoUpdate(const std::string &query, unsigned int *affected_rows = nullptr) _kc_override;
	ECRESULT FinalizeMulti(void);
	ECRESULT GetNextResult(DB_RESULT *);
	ECRESULT InitializeDBState(void);
	virtual ECRESULT Rollback(void) _kc_override;
	bool SuppressLockErrorLogging(bool suppress);
	void ThreadEnd(void);
	void ThreadInit(void);
	ECRESULT UpdateDatabase(bool force_update, std::string &report);
	const std::string &get_dbname() const { return m_dbname; }

	private:
	ECRESULT InitializeDBStateInner(void);
	virtual const struct sSQLDatabase_t *GetDatabaseDefs(void) _kc_override;
	ECRESULT GetDatabaseVersion(zcp_versiontuple *);
	ECRESULT GetFirstUpdate(unsigned int *lpulDatabaseRevision);
	ECRESULT UpdateDatabaseVersion(unsigned int ulDatabaseRevision);
	virtual ECRESULT Query(const std::string &q) _kc_override;

	std::string error, m_dbname;
	bool m_bForceUpdate = false, m_bFirstResult = false;
	std::shared_ptr<ECConfig> m_lpConfig;
	std::shared_ptr<ECStatsCollector> g_lpStatsCollector;
#ifdef KNOB144
	unsigned int m_ulTransactionState = 0;
#endif
};

extern _kc_export bool searchfolder_restart_required;

} /* namespace */

#endif // ECDATABASE_H
