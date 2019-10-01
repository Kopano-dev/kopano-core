/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECDATABASEUTILS_H
#define ECDATABASEUTILS_H

#include <kopano/zcdefs.h>
#include "ECMAPI.h"
#include "kcore.hpp"
#include <kopano/kcodes.h>
#include "ECDatabase.h"
#include "ECDatabaseFactory.h"
#include <kopano/ECLogger.h>
#include <string>

struct propVal;
struct soap;

namespace KC {

#define MAX_PROP_SIZE 32768
#define MAX_QUERY 4096

#define PROPCOL_ULONG	"val_ulong"
#define PROPCOL_STRING	"val_string"
#define PROPCOL_BINARY	"val_binary"
#define PROPCOL_DOUBLE	"val_double"
#define PROPCOL_LONGINT	"val_longint"
#define PROPCOL_HI		"val_hi"
#define PROPCOL_LO		"val_lo"

#define I_PROPCOL_ULONG(tab)    #tab "." PROPCOL_ULONG
#define I_PROPCOL_STRING(tab)   #tab "." PROPCOL_STRING
#define I_PROPCOL_BINARY(tab)   #tab "." PROPCOL_BINARY
#define I_PROPCOL_DOUBLE(tab)   #tab "." PROPCOL_DOUBLE
#define I_PROPCOL_LONGINT(tab)  #tab "." PROPCOL_LONGINT
#define I_PROPCOL_HI(tab)       #tab "." PROPCOL_HI
#define I_PROPCOL_LO(tab)       #tab "." PROPCOL_LO

#define PROPCOL_HILO		PROPCOL_HI "," PROPCOL_LO
#define I_PROPCOL_HILO(tab)	PROPCOL_HI(tab) "," PROPCOL_LO(tab)

/* make string of define value */
#define STRINGIFY(x) #x
#define STR(macro) STRINGIFY(macro)

// Warning! Code references the ordering of these values! Do not change unless you know what you're doing!
#define PROPCOLVALUEORDER(tab) I_PROPCOL_ULONG(tab) "," I_PROPCOL_STRING(tab) "," I_PROPCOL_BINARY(tab) "," I_PROPCOL_DOUBLE(tab) "," I_PROPCOL_LONGINT(tab) "," I_PROPCOL_HI(tab) "," I_PROPCOL_LO(tab)
#define PROPCOLVALUEORDER_TRUNCATED(tab) I_PROPCOL_ULONG(tab) ", LEFT(" I_PROPCOL_STRING(tab) "," STR(TABLE_CAP_STRING) "),LEFT(" I_PROPCOL_BINARY(tab) "," STR(TABLE_CAP_BINARY) ")," I_PROPCOL_DOUBLE(tab) "," I_PROPCOL_LONGINT(tab) "," I_PROPCOL_HI(tab) "," I_PROPCOL_LO(tab)
enum { VALUE_NR_ULONG=0, VALUE_NR_STRING, VALUE_NR_BINARY, VALUE_NR_DOUBLE, VALUE_NR_LONGINT, VALUE_NR_HILO, VALUE_NR_MAX };

#define PROPCOLORDER "0,properties.tag,properties.type," PROPCOLVALUEORDER(properties)
#define PROPCOLORDER_TRUNCATED "0,properties.tag,properties.type," PROPCOLVALUEORDER_TRUNCATED(properties)
#define MVPROPCOLORDER "count(*),mvproperties.tag,mvproperties.type,group_concat(length(mvproperties.val_ulong),':', mvproperties.val_ulong ORDER BY mvproperties.orderid SEPARATOR ''), group_concat(length(mvproperties.val_string),':', mvproperties.val_string ORDER BY mvproperties.orderid SEPARATOR ''), group_concat(length(mvproperties.val_binary),':', mvproperties.val_binary ORDER BY mvproperties.orderid SEPARATOR ''), group_concat(length(mvproperties.val_double),':', mvproperties.val_double ORDER BY mvproperties.orderid SEPARATOR ''), group_concat(length(mvproperties.val_longint),':', mvproperties.val_longint ORDER BY mvproperties.orderid SEPARATOR ''), group_concat(length(mvproperties.val_hi),':', mvproperties.val_hi ORDER BY mvproperties.orderid SEPARATOR ''), group_concat(length(mvproperties.val_lo),':', mvproperties.val_lo ORDER BY mvproperties.orderid SEPARATOR '')"
#define MVIPROPCOLORDER "0,mvproperties.tag,mvproperties.type | 8192," PROPCOLVALUEORDER(mvproperties)
#define MVIPROPCOLORDER_TRUNCATED "0,mvproperties.tag,mvproperties.type | 8192," PROPCOLVALUEORDER_TRUNCATED(mvproperties)

enum { FIELD_NR_ID=0, FIELD_NR_TAG, FIELD_NR_TYPE, FIELD_NR_ULONG, FIELD_NR_STRING, FIELD_NR_BINARY, FIELD_NR_DOUBLE, FIELD_NR_LONGINT, FIELD_NR_HI, FIELD_NR_LO, FIELD_NR_MAX };

ULONG GetColOffset(ULONG ulPropTag);
std::string GetPropColOrder(unsigned int ulPropTag, const std::string &strSubQuery);
unsigned int GetColWidth(unsigned int ulPropType);
ECRESULT	GetPropSize(DB_ROW lpRow, DB_LENGTHS lpLen, unsigned int *lpulSize);
extern ECRESULT CopySOAPPropValToDatabasePropVal(const struct propVal *, unsigned int *col_id, std::string &col_data, ECDatabase *, bool truncate);
ECRESULT	CopyDatabasePropValToSOAPPropVal(struct soap *soap, DB_ROW lpRow, DB_LENGTHS lpLen, propVal *lpPropVal);
gsoap_size_t GetMVItemCount(struct propVal *lpPropVal);
ECRESULT CopySOAPPropValToDatabaseMVPropVal(struct propVal *lpPropVal, int nItem, std::string &strColName, std::string &strColData, ECDatabase *lpDatabase);
ECRESULT ParseMVProp(const char *lpRowData, ULONG ulSize, unsigned int *lpulLastPos, std::string *lpstrData);
unsigned int NormalizeDBPropTag(unsigned int ulPropTag);
bool CompareDBPropTag(unsigned int ulPropTag1, unsigned int ulPropTag2);

/**
 * This class is used to suppress the lock-error logging for the database passed to its
 * constructor during the lifetime of the instance.
 * This means the lock-error logging is restored when the scope in which an instance of
 * this class exists is exited.
 */
class SuppressLockErrorLogging final {
public:
	SuppressLockErrorLogging(ECDatabase *lpDatabase);
	~SuppressLockErrorLogging();

private:
	ECDatabase *m_lpDatabase;
	bool m_bResetValue;

	SuppressLockErrorLogging(const SuppressLockErrorLogging &) = delete;
	SuppressLockErrorLogging &operator=(const SuppressLockErrorLogging &) = delete;
};

/**
 * This macro allows anyone to create a temporary scope in which the lock-errors
 * for a database connection are suppressed.
 *
 * Simple usage:
 * WITH_SUPPRESSED_LOGGING(lpDatabase)
 *   do_something_with(lpDatabase);
 *
 * or when more stuff needs to be done with the suppression enabled:
 * WITH_SUPPRESSED_LOGGING(lpDatabase) {
 *   do_something_with(lpDatabase);
 *   do_something_else_with(lpDatabase);
 * }
 */
#define WITH_SUPPRESSED_LOGGING(db) \
	for (SuppressLockErrorLogging suppressor(db), *ptr = nullptr; ptr == nullptr; ptr = &suppressor)

} /* namespace */

#endif // ECDATABASEUTILS_H
