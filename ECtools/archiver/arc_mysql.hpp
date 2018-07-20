/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ARC_MYSQL_HPP
#define ARC_MYSQL_HPP 1

#include <kopano/zcdefs.h>
#include <kopano/database.hpp>
#include <string>

namespace KC {

class ECConfig;

class KCMDatabaseMySQL _kc_final : public KDatabase {
public:
	virtual ~KCMDatabaseMySQL(void);
	ECRESULT		Connect(ECConfig *lpConfig);
	virtual const struct sSQLDatabase_t *GetDatabaseDefs(void) _kc_override;
};

} /* namespace */

#endif
