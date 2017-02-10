#ifndef KOPANO_DATABASE_HPP
#define KOPANO_DATABASE_HPP 1

#include <string>
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>

namespace KC {

typedef void *DB_RESULT;
typedef char **DB_ROW;
typedef unsigned long *DB_LENGTHS;

class _kc_export KDatabase {
	public:
	virtual ~KDatabase(void) _kc_impdtor;
	virtual ECRESULT Close(void) = 0;
	virtual ECRESULT DoDelete(const std::string &query, unsigned int *affect = nullptr) = 0;
	virtual ECRESULT DoInsert(const std::string &query, unsigned int *insert_id = nullptr, unsigned int *affect = nullptr) = 0;
	virtual ECRESULT DoSelect(const std::string &query, DB_RESULT *, bool stream = false) = 0;
	/* Sequence generator - Do not call this from within a transaction. */
	virtual ECRESULT DoSequence(const std::string &seq, unsigned int count, unsigned long long *first_id) = 0;
	virtual ECRESULT DoUpdate(const std::string &query, unsigned int *affect = nullptr) = 0;
	virtual std::string Escape(const std::string &) = 0;
	virtual std::string EscapeBinary(const unsigned char *, size_t) = 0;
	virtual std::string EscapeBinary(const std::string &) = 0;
	virtual DB_ROW FetchRow(DB_RESULT) = 0;
	virtual DB_LENGTHS FetchRowLengths(DB_RESULT) = 0;
	virtual void FreeResult(DB_RESULT) = 0;
	virtual const char *GetError(void) = 0;
	virtual unsigned int GetMaxAllowedPacket(void) = 0;
	virtual unsigned int GetNumRows(DB_RESULT) = 0;
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
	virtual ECRESULT _Update(const std::string &q, unsigned int *affected) = 0;
	virtual unsigned int GetAffectedRows(void) = 0;
	virtual unsigned int GetInsertId(void) = 0;
	virtual bool isConnected(void) = 0;
};

} /* namespace */

#endif /* KOPANO_DATABASE_HPP */
