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

#ifndef ECDATABASEUPDATE_H
#define ECDATABASEUPDATE_H

#include <kopano/zcdefs.h>
#include <kopano/ECLogger.h>

namespace KC {

ECRESULT InsertServerGUID(ECDatabase *lpDatabase);

ECRESULT UpdateVersionsTbl(ECDatabase *db);
ECRESULT UpdateChangesTbl(ECDatabase *db);
ECRESULT UpdateABChangesTbl(ECDatabase *db);
ECRESULT DropClientUpdateStatusTbl(ECDatabase *db);
ECRESULT db_update_68(ECDatabase *);
ECRESULT db_update_69(ECDatabase *);

extern _kc_export bool searchfolder_restart_required;

} /* namespace */

#endif // #ifndef ECDATABASEUPDATE_H
