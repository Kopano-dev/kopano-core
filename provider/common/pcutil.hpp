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

#ifndef KC_PCUTIL_HPP
#define KC_PCUTIL_HPP 1

// All functions which used in storage server and client
#include "kcore.hpp"
#include <kopano/kcodes.h>
#include "soapH.h"
#include <kopano/ECDefs.h>
#include "SOAPUtils.h"
#include <mapidefs.h>
#include <string>

namespace KC {

extern bool IsKopanoEntryId(ULONG eid_size, const BYTE *eid);
extern bool ValidateZEntryId(ULONG eid_size, const BYTE *eid, unsigned int check_type);
extern bool ValidateZEntryList(const ENTRYLIST *, unsigned int check_type);
extern ECRESULT ABEntryIDToID(ULONG eid_size, const BYTE *eid, unsigned int *id, objectid_t *extern_id, unsigned int *mapi_type);
extern ECRESULT SIEntryIDToID(ULONG eid_size, const BYTE *instance_eid, GUID *server_guid, unsigned int *instance_nid, unsigned int *prop_id = nullptr);
extern int SortCompareABEID(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b);
extern bool CompareABEID(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b);
extern ECRESULT ParseKopanoVersion(const std::string &commamagic, std::string *seg, unsigned int *integ);

//Clientside functions
extern HRESULT HrGetStoreGuidFromEntryId(ULONG eid_size, const BYTE *eid, GUID *store_guid);
extern HRESULT HrGetObjTypeFromEntryId(ULONG eid_size, const BYTE *eid, unsigned int *obj_type);
extern HRESULT HrSIEntryIDToID(ULONG eid_size, const BYTE *instance_eid, GUID *server_guid, unsigned int *id, unsigned int *prop_id = nullptr);

// Serverside functions
extern ECRESULT GetStoreGuidFromEntryId(ULONG eid_size, const BYTE *eid, GUID *store_guid);
extern ECRESULT GetObjTypeFromEntryId(ULONG eid_size, const BYTE *eid, unsigned int *obj_type);
extern ECRESULT GetStoreGuidFromEntryId(const entryId &, GUID *store_guid);
extern ECRESULT GetObjTypeFromEntryId(const entryId &, unsigned int *obj_type);
extern ECRESULT ABEntryIDToID(const entryId *eid, unsigned int *id, objectid_t *extern_id, unsigned int *mapi_type);
extern ECRESULT SIEntryIDToID(const entryId *instance_eid, GUID *server_guid, unsigned int *instance_nid, unsigned int *prop_id = nullptr);
ECRESULT ABIDToEntryID(struct soap *soap, unsigned int ulID, const objectid_t& strExternId, entryId *lpsEntryId);
extern ECRESULT SIIDToEntryID(struct soap *soap, const GUID *server_guid, unsigned int instance_id, unsigned int prop_id, entryId *instance_eid);
ECRESULT MAPITypeToType(ULONG ulMAPIType, objectclass_t *lpsUserObjClass);
ECRESULT TypeToMAPIType(objectclass_t sUserObjClass, ULONG *lpulMAPIType);

} /* namespace */

#endif /* KC_PCUTIL_HPP */
