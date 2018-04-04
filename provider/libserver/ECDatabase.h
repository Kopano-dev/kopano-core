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

#ifndef ECDATABASE_H
#define ECDATABASE_H

#include <kopano/zcdefs.h>
#include <kopano/ECConfig.h>
#include <kopano/database.hpp>
#include <string>

namespace KC {

class ECConfig;
class zcp_versiontuple;

class _kc_export ECDatabase _kc_final : public KDatabase {
public:
	ECDatabase(ECConfig *);
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
	std::string FilterBMP(const std::string &to_filter);
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
	ECConfig *m_lpConfig = nullptr;
#ifdef DEBUG
	unsigned int m_ulTransactionState = 0;
#endif
};

} /* namespace */

#endif // ECDATABASE_H
