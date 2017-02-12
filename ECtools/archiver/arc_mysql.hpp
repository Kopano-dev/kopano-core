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

// ECDatabaseMySQL.h: interface for the ECDatabaseMySQL class.
//
//////////////////////////////////////////////////////////////////////

#ifndef ARC_MYSQL_HPP
#define ARC_MYSQL_HPP 1

#include <kopano/platform.h>
#include <kopano/ECConfig.h>
#include <kopano/database.hpp>
#include <kopano/kcodes.h>
#include <string>

using namespace std;

namespace KC {

class KCMDatabaseMySQL _kc_final : public KDatabase {
public:
	virtual ~KCMDatabaseMySQL(void);
	ECRESULT		Connect(ECConfig *lpConfig);
	virtual const struct sSQLDatabase_t *GetDatabaseDefs(void) _kc_override;
};

} /* namespace */

#endif
