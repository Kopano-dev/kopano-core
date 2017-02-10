#ifndef KOPANO_DATABASE_HPP
#define KOPANO_DATABASE_HPP 1

#include <mutex>
#include <string>
#include <mysql.h>
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>

namespace KC {

typedef void *DB_RESULT;
typedef char **DB_ROW;
typedef unsigned long *DB_LENGTHS;

class _kc_export KDatabase {
	public:
	KDatabase(void);
	virtual ~KDatabase(void) _kc_impdtor;
	virtual ECRESULT Close(void) = 0;
	virtual ECRESULT DoDelete(const std::string &query, unsigned int *affect = nullptr) = 0;
	virtual ECRESULT DoInsert(const std::string &query, unsigned int *insert_id = nullptr, unsigned int *affect = nullptr) = 0;
	virtual ECRESULT DoSelect(const std::string &query, DB_RESULT *, bool stream = false) = 0;
	/* Sequence generator - Do not call this from within a transaction. */
	virtual ECRESULT DoSequence(const std::string &seq, unsigned int count, unsigned long long *first_id) = 0;
	virtual ECRESULT DoUpdate(const std::string &query, unsigned int *affect = nullptr) = 0;
	std::string Escape(const std::string &);
	std::string EscapeBinary(const unsigned char *, size_t);
	std::string EscapeBinary(const std::string &);
	DB_ROW FetchRow(DB_RESULT);
	DB_LENGTHS FetchRowLengths(DB_RESULT);
	void FreeResult(DB_RESULT);
	const char *GetError(void);
	virtual unsigned int GetMaxAllowedPacket(void) = 0;
	unsigned int GetNumRows(DB_RESULT);
	/*
	 * Transactions.
	 * These functions should be used to wrap blocks of queries into
	 * transactions. This will speed up writes a lot, so try to use them as
	 * much as possible. If you don't start a transaction then each INSERT
	 * or UPDATE will automatically be a single transaction, causing an
	 * fsync after each write-query, which is not fast to say the least.
	 */
	virtual ECRESULT Begin(void) = 0;
	virtual ECRESULT Commit(void) = 0;
	virtual ECRESULT Rollback(void) = 0;

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
	unsigned int GetInsertId(void);
	virtual bool isConnected(void) = 0;
	virtual ECRESULT _Update(const std::string &q, unsigned int *affected) = 0;

	MYSQL m_lpMySQL;
	bool m_bMysqlInitialize = false, m_bConnected = false;

	private:
	std::recursive_mutex m_hMutexMySql;
	bool m_bAutoLock = true;
};

} /* namespace */

#endif /* KOPANO_DATABASE_HPP */
