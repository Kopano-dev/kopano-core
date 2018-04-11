#ifndef KOPANO_DATABASE_HPP
#define KOPANO_DATABASE_HPP 1

#include <mutex>
#include <string>
#include <utility>
#include <mapidefs.h>
#include <mysql.h>
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>

namespace KC {

typedef char **DB_ROW;
typedef unsigned long *DB_LENGTHS;
typedef unsigned int DB_ERROR;

enum {
	DB_E_UNKNOWN = -1,
	DB_E_LOCK_WAIT_TIMEOUT = 1,
	DB_E_LOCK_DEADLOCK = 2,
};

enum {
	/*
	 * The maximum packet size. This is automatically also the maximum size
	 * of a single entry in the database. This means that PR_BODY,
	 * PR_COMPRESSED_RTF etc. cannot grow larger than 16M. This shouldn't
	 * be such a problem in practice.
	 *
	 * In Debian Lenny, setting your max_allowed_packet to 16M actually
	 * gives this value.... Unknown why.
	 */
	KC_DFL_MAX_PACKET_SIZE = 16776192,
};

struct sSQLDatabase_t {
	const char *lpComment;
	const char *lpSQL;
};

class ECConfig;
class KDatabase;

class _kc_export DB_RESULT _kc_final {
	public:
	DB_RESULT(void) = default;
	DB_RESULT(KDatabase *d, void *r) : m_res(r), m_db(d) {}
	DB_RESULT(DB_RESULT &&o) = default;
	~DB_RESULT(void);
	DB_RESULT &operator=(DB_RESULT &&o);
	operator bool(void) const { return m_res != nullptr; }
	bool operator==(std::nullptr_t) const noexcept { return m_res == nullptr; }
	bool operator!=(std::nullptr_t) const noexcept { return m_res != nullptr; }
	void *get(void) const noexcept { return m_res; }
	void *release(void) noexcept
	{
		void *p = m_res;
		m_res = nullptr;
		return p;
	}

	size_t get_num_rows(void) const;
	DB_ROW fetch_row(void);
	DB_LENGTHS fetch_row_lengths(void);

	private:
	void *m_res = nullptr;
	KDatabase *m_db = nullptr;
};

class kt_completion {
	public:
	virtual ECRESULT Commit() = 0;
	virtual ECRESULT Rollback() = 0;
};

/**
 * kd_trans is an explicit variable wrapper around a database transaction.
 * The transaction's scope is bounded by the scope of a certain kd_trans.
 * Essentially, this gives the programmer an implicit rollback whenever
 * a kd_trans did not have commit() called.
 */
class _kc_export kd_trans final {
	public:
	kd_trans();
	kd_trans(kt_completion &d, ECRESULT &r) : m_db(&d), m_result(&r) {}
	kd_trans(kd_trans &&);
	~kd_trans();
	kd_trans &operator=(kd_trans &&);
	ECRESULT commit();
	ECRESULT rollback();

	private:
	kt_completion *m_db;
	ECRESULT *m_result;
	bool m_done = false;
};

class _kc_export KDatabase {
	public:
	KDatabase(void);
	virtual ~KDatabase(void) = default;
	ECRESULT Close(void);
	virtual ECRESULT Connect(ECConfig *, bool, unsigned int, unsigned int);
	virtual ECRESULT CreateDatabase(ECConfig *, bool);
	virtual ECRESULT CreateTables(void);
	virtual ECRESULT DoDelete(const std::string &query, unsigned int *affect = nullptr);
	virtual ECRESULT DoInsert(const std::string &query, unsigned int *insert_id = nullptr, unsigned int *affect = nullptr);
	virtual ECRESULT DoSelect(const std::string &query, DB_RESULT *, bool stream = false);
	/* Sequence generator - Do not call this from within a transaction. */
	virtual ECRESULT DoSequence(const std::string &seq, unsigned int count, unsigned long long *first_id);
	virtual ECRESULT DoUpdate(const std::string &query, unsigned int *affect = nullptr);
	std::string Escape(const std::string &);
	std::string EscapeBinary(const void *, size_t);
	std::string EscapeBinary(const std::string &s) { return EscapeBinary(s.c_str(), s.size()); }
	std::string EscapeBinary(const SBinary &s) { return EscapeBinary(s.lpb, s.cb); }
	const char *GetError(void);
	DB_ERROR GetLastError(void);
	unsigned int GetMaxAllowedPacket(void) const { return m_ulMaxAllowedPacket; }
	/*
	 * Transactions.
	 * These functions should be used to wrap blocks of queries into
	 * transactions. This will speed up writes a lot, so try to use them as
	 * much as possible. If you don't start a transaction then each INSERT
	 * or UPDATE will automatically be a single transaction, causing an
	 * fsync after each write-query, which is not fast to say the least.
	 */
	virtual ECRESULT Begin(void) { return Query("BEGIN") == 0 ? erSuccess : KCERR_DATABASE_ERROR; }
	virtual ECRESULT Commit(void) { return Query("COMMIT") == 0 ? erSuccess : KCERR_DATABASE_ERROR; }
	virtual ECRESULT Rollback(void) { return Query("ROLLBACK") == 0 ? erSuccess : KCERR_DATABASE_ERROR; }

	protected:
	class autolock : private std::unique_lock<std::recursive_mutex> {
		public:
		autolock(KDatabase &p) :
			std::unique_lock<std::recursive_mutex>(p.m_hMutexMySql, std::defer_lock_t())
		{
			if (p.m_bAutoLock)
				lock();
		}
	};

	unsigned int GetAffectedRows(void);
	virtual const struct sSQLDatabase_t *GetDatabaseDefs() { return nullptr; }
	unsigned int GetInsertId(void);
	ECRESULT InitEngine(bool reconnect);
	bool isConnected(void) const { return m_bConnected; }
	ECRESULT IsInnoDBSupported(void);
	virtual ECRESULT Query(const std::string &q);
	ECRESULT I_Update(const std::string &q, unsigned int *affected);

	MYSQL m_lpMySQL;
	unsigned int m_ulMaxAllowedPacket = KC_DFL_MAX_PACKET_SIZE;
	bool m_bMysqlInitialize = false, m_bConnected = false;
	bool m_bSuppressLockErrorLogging = false;

	private:
	void FreeResult_internal(void *);

	std::recursive_mutex m_hMutexMySql;
	bool m_bAutoLock = true;

	friend class DB_RESULT;
};

} /* namespace */

#endif /* KOPANO_DATABASE_HPP */
