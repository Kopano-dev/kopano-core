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

ECRESULT UpdateDatabaseConvertToUnicode(ECDatabase *);

ECRESULT InsertServerGUID(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseAddUserCompany(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseAddObjectRelationType(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseDelUserCompany(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseAddCompanyToStore(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseAddIMAPSequenceNumber(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseKeysChanges(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseMoveFoldersInPublicFolder(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseAddExternIdToObject(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseCreateReferences(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseCreateABChangesTable(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseSetSingleinstanceTag(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseCreateSyncedMessagesTable(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseForceAbResync(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseRenameObjectTypeToObjectClass(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseConvertObjectTypeToObjectClass(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseAddMVPropertyTable(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseCompanyNameToCompanyId(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseOutgoingQueuePrimarykey(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseACLPrimarykey(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseBlobExternId(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseKeysChanges2(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseMVPropertiesPrimarykey(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseFixDBPluginGroups(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseFixDBPluginSendAs(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseMoveSubscribedList(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseSyncTimeIndex(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseAddStateKey(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseConvertStoreUsername(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseConvertRules(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseConvertSearchFolders(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseConvertProperties(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseCreateCounters(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseCreateCommonProps(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseCheckAttachments(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseCreateTProperties(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseConvertHierarchy(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseCreateDeferred(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseConvertChanges(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseConvertNames(ECDatabase *lpDatabase);

ECRESULT UpdateDatabaseReceiveFolderToUnicode(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseClientUpdateStatus(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseConvertStores(ECDatabase *lpDatabase);
ECRESULT UpdateDatabaseUpdateStores(ECDatabase *lpDatabase);

ECRESULT UpdateWLinkRecordKeys(ECDatabase *lpDatabase);
ECRESULT UpdateVersionsTbl(ECDatabase *db);
ECRESULT UpdateChangesTbl(ECDatabase *db);
ECRESULT UpdateABChangesTbl(ECDatabase *db);

extern _kc_export bool searchfolder_restart_required;

} /* namespace */

#endif // #ifndef ECDATABASEUPDATE_H
