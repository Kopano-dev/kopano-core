/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
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

extern bool IsKopanoEntryId(unsigned int eid_size, const void *eid);
extern bool ValidateZEntryId(unsigned int eid_size, const void *eid, unsigned int check_type);
extern bool ValidateZEntryList(const ENTRYLIST *, unsigned int check_type);
extern ECRESULT ABEntryIDToID(unsigned int eid_size, const void *eid, unsigned int *id, objectid_t *extern_id, unsigned int *mapi_type);
extern ECRESULT SIEntryIDToID(unsigned int eid_size, const void *instance_eid, GUID *server_guid, unsigned int *instance_nid, unsigned int *prop_id = nullptr);
extern int SortCompareABEID(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b);
extern bool CompareABEID(ULONG asize, const ENTRYID *a, ULONG bsize, const ENTRYID *b);
extern ECRESULT ParseKopanoVersion(const std::string &commamagic, std::string *seg, unsigned int *integ);

//Clientside functions
extern HRESULT HrGetStoreGuidFromEntryId(unsigned int eid_size, const void *eid, GUID *store_guid);
extern HRESULT HrGetObjTypeFromEntryId(unsigned int eid_size, const void *eid, unsigned int *obj_type);
extern HRESULT HrSIEntryIDToID(unsigned int eid_size, const void *instance_eid, GUID *server_guid, unsigned int *id, unsigned int *prop_id = nullptr);

// Serverside functions
extern ECRESULT GetStoreGuidFromEntryId(unsigned int eid_size, const void *eid, GUID *store_guid);
extern ECRESULT GetObjTypeFromEntryId(unsigned int eid_size, const void *eid, unsigned int *obj_type);
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
