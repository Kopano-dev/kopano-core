/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef EC_USERSTORE_TABLE_H
#define EC_USERSTORE_TABLE_H

#include <kopano/zcdefs.h>
#include <kopano/Util.h>
/* #include "ECStoreObjectTable.h" */
#include "ECGenericObjectTable.h"
#include <kopano/pcuser.hpp>
#include <map>

struct soap;

namespace KC {

class ECSession;

struct ECUserStore {
	long long ulUserId = -1; /* id of user (-1 if there is no user) */
	objectid_t		sExternId;		// externid of user
	std::string		strUsername;	// actual username from ECUserManagement
	std::string		strGuessname;	// "guess" from user_name column in stores table
	std::string sGuid; /* The GUID of the store */
	std::string strCompanyName; /* Company name of the store. (can be empty) */
	unsigned int ulCompanyId = 0; /* Company id of store (or user if store is not found) */
	unsigned int ulStoreType = 0; /* Type of the store (private, public, archive) */
	unsigned int ulObjId = 0; /* Hierarchyid of the store */
	time_t tModTime = 0; /* Modification time of the store */
	unsigned long long ullStoreSize = 0; /* Size of the store */
};

class KC_EXPORT_DYCAST ECUserStoreTable KC_FINAL_OPG : public ECGenericObjectTable {
	protected:
	KC_HIDDEN ECUserStoreTable(ECSession *, unsigned int flags, const ECLocale &);

public:
	KC_HIDDEN static ECRESULT Create(ECSession *, unsigned int flags, const ECLocale &, ECUserStoreTable **ret);
	KC_HIDDEN static ECRESULT QueryRowData(ECGenericObjectTable *, struct soap *, ECSession *, const ECObjectTableList *, const struct propTagArray *, const void *priv, struct rowSet **rowset, bool cache_table_data, bool table_limit);
	KC_HIDDEN virtual ECRESULT Load();

private:
	std::map<unsigned int, ECUserStore> m_mapUserStoreData;
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif
