/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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
ECRESULT db_update_70(ECDatabase *);

extern _kc_export bool searchfolder_restart_required;

} /* namespace */

#endif // #ifndef ECDATABASEUPDATE_H
