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

#include <kopano/platform.h>
#include <utility>
#include <mapitags.h>
#include <mapidefs.h>
#include <mapiutil.h>

#include <libintl.h>

#include "ECMAPI.h"
#include <kopano/stringutil.h>
#include "SOAPUtils.h"
#include "soapH.h"
#include "ECStoreObjectTable.h"
#include "ECGenProps.h"
#include "kcore.hpp"
#include <kopano/ECDefs.h>
#include "ECUserManagement.h"
#include "ECSecurity.h"
#include "ECSessionManager.h"
#include "ECLockManager.h"

#include <edkmdb.h>
#include <kopano/mapiext.h>

#define _(string) dcgettext("kopano", string, LC_MESSAGES)

namespace KC {

extern ECSessionManager*	g_lpSessionManager;

ECRESULT ECGenProps::GetMVPropSubquery(unsigned int ulPropTagRequested, std::string &subquery) 
{
	ECRESULT er = erSuccess;
	unsigned int ulType = PROP_TYPE(ulPropTagRequested);

	//skip MV_INSTANCE
	switch(ulType) {
	case PT_MV_I2:
	case PT_MV_LONG:
		subquery = "SELECT concat(count(*), ':', group_concat(length(val_ulong),':',val_ulong ORDER BY subquery.orderid SEPARATOR '')) FROM mvproperties AS subquery WHERE subquery.hierarchyid=hierarchy.id AND subquery.type="+stringify(PROP_TYPE(ulPropTagRequested))+" AND subquery.tag="+stringify(PROP_ID(ulPropTagRequested))+" GROUP BY subquery.hierarchyid";
		break;
	case PT_MV_R4:
	case PT_MV_DOUBLE:
	case PT_MV_APPTIME:
		subquery = "SELECT concat(count(*), ':', group_concat(length(val_double),':',val_double ORDER BY subquery.orderid SEPARATOR '')) FROM mvproperties AS subquery WHERE subquery.hierarchyid=hierarchy.id AND subquery.type="+stringify(PROP_TYPE(ulPropTagRequested))+" AND subquery.tag="+stringify(PROP_ID(ulPropTagRequested))+" GROUP BY subquery.hierarchyid";
		break;
	case PT_MV_CURRENCY:
	case PT_MV_SYSTIME:
		subquery = "SELECT group_concat(count(*),':',length(val_hi),':',val_hi ORDER BY subquery.orderid SEPARATOR '')) FROM mvproperties AS subquery WHERE subquery.hierarchyid=hierarchy.id AND subquery.type="+stringify(PROP_TYPE(ulPropTagRequested))+" AND subquery.tag="+stringify(PROP_ID(ulPropTagRequested))+" GROUP BY subquery.hierarchyid";
		subquery += "),(SELECT group_concat(count(*),':',length(val_lo),':',val_lo ORDER BY subquery.orderid SEPARATOR '')) FROM mvproperties AS subquery WHERE subquery.hierarchyid=hierarchy.id AND subquery.type="+stringify(PROP_TYPE(ulPropTagRequested))+" AND subquery.tag="+stringify(PROP_ID(ulPropTagRequested))+" GROUP BY subquery.hierarchyid";
		break;
	case PT_MV_BINARY:
	case PT_MV_CLSID:
		subquery = "SELECT concat(count(*), ':', group_concat(length(val_binary),':',val_binary ORDER BY subquery.orderid SEPARATOR '')) FROM mvproperties AS subquery WHERE subquery.hierarchyid=hierarchy.id AND subquery.type="+stringify(PROP_TYPE(ulPropTagRequested))+" AND subquery.tag="+stringify(PROP_ID(ulPropTagRequested))+" GROUP BY subquery.hierarchyid";
		break;
	case PT_MV_STRING8:
	case PT_MV_UNICODE:
		subquery = "SELECT concat(count(*), ':', group_concat(char_length(val_string),':',val_string ORDER BY subquery.orderid SEPARATOR '')) FROM mvproperties AS subquery WHERE subquery.hierarchyid=hierarchy.id AND subquery.type="+stringify(PROP_TYPE(ulPropTagRequested))+" AND subquery.tag="+stringify(PROP_ID(ulPropTagRequested))+" GROUP BY subquery.hierarchyid";
		break;
	case PT_MV_I8:
		subquery = "SELECT concat(count(*), ':', group_concat(length(val_longint),':',val_longint ORDER BY subquery.orderid SEPARATOR '')) FROM mvproperties AS subquery WHERE subquery.hierarchyid=hierarchy.id AND subquery.type="+stringify(PROP_TYPE(ulPropTagRequested))+" AND subquery.tag="+stringify(PROP_ID(ulPropTagRequested))+" GROUP BY subquery.hierarchyid";
		break;
	default:
		er = KCERR_NOT_FOUND;
		break;
	}
	

	return er;
}

ECRESULT ECGenProps::GetPropSubquery(unsigned int ulPropTagRequested, std::string &subquery) 
{
	ECRESULT er = erSuccess;

	switch(ulPropTagRequested) {
	case PR_PARENT_DISPLAY_W:
	case PR_PARENT_DISPLAY_A:
		subquery = "SELECT properties.val_string FROM properties JOIN hierarchy as subquery ON properties.hierarchyid=subquery.parent WHERE subquery.id=hierarchy.id AND properties.tag=0x3001"; // PR_DISPLAY_NAME of parent
		break;

    case PR_EC_OUTGOING_FLAGS:
        subquery = "SELECT outgoingqueue.flags FROM outgoingqueue where outgoingqueue.hierarchy_id = hierarchy.id and outgoingqueue.flags & 1 = 1";
        break;

    case PR_EC_PARENT_HIERARCHYID:
        // This isn't really a subquery, because all we want is the hierarchy.parent field. Since the subquery engine is already joining with 'hierarchy' we can
        // read directly from that table by just returning hierarchy.parent
        subquery = "hierarchy.parent";
        break;
        
    case PR_ASSOCIATED:
        subquery = "hierarchy.flags & " + stringify(MAPI_ASSOCIATED);
        break;
        
	default:
		er = KCERR_NOT_FOUND;
		break;
	}

	return er;
}

/**
 * Get a property substitution
 *
 * This is used in tables; A substitition works as follows:
 *
 * - In the table engine, any column with the requested property tag is replaced with the required property
 * - The requested property is retrieved from cache or database as if the column had been retrieved as such in the first place
 * - You must create a GetPropComputed entry to convert back to the originally requested property
 *
 * @param ulObjType MAPI_MESSAGE, or MAPI_FOLDER
 * @param ulPropTagRequested The property tag set by SetColumns
 * @param ulPropTagRequired[out] Output property to be retrieved from the database
 * @return Result
 */
ECRESULT ECGenProps::GetPropSubstitute(unsigned int ulObjType, unsigned int ulPropTagRequested, unsigned int *lpulPropTagRequired)
{
	unsigned int ulPropTagRequired = 0;

	switch(PROP_ID(ulPropTagRequested)) {
	case PROP_ID(PR_NORMALIZED_SUBJECT):
		ulPropTagRequired = PR_SUBJECT;
		break;
	case PROP_ID(PR_CONTENT_UNREAD):
		if (ulObjType == MAPI_MESSAGE)
			ulPropTagRequired = PR_MESSAGE_FLAGS;
		else
			return KCERR_NOT_FOUND;
		break;
	default:
		return KCERR_NOT_FOUND;
	}

	*lpulPropTagRequired = ulPropTagRequired;
	return erSuccess;
}

// This should be synchronized with GetPropComputed
ECRESULT ECGenProps::IsPropComputed(unsigned int ulPropTag, unsigned int ulObjType)
{
	switch(ulPropTag) {
	case PR_MSG_STATUS:
	case PR_EC_IMAP_ID:
	case PR_NORMALIZED_SUBJECT_A:
	case PR_NORMALIZED_SUBJECT_W:
	case PR_SUBMIT_FLAGS:
		return erSuccess;
	case PR_CONTENT_UNREAD:
		return ulObjType == MAPI_MESSAGE ? erSuccess : KCERR_NOT_FOUND;
	case PR_RECORD_KEY:
		return ulObjType == MAPI_ATTACH ? KCERR_NOT_FOUND : erSuccess;
	default:
		return KCERR_NOT_FOUND;
	}
}

// This should be synchronized with GetPropComputedUncached
ECRESULT ECGenProps::IsPropComputedUncached(unsigned int ulPropTag, unsigned int ulObjType)
{
    switch(PROP_ID(ulPropTag)) {
        case PROP_ID(PR_LONGTERM_ENTRYID_FROM_TABLE):
	case PROP_ID(PR_ENTRYID):
	case PROP_ID(PR_PARENT_ENTRYID):
	case PROP_ID(PR_STORE_ENTRYID):
	case PROP_ID(PR_STORE_RECORD_KEY):
	case PROP_ID(PR_USER_NAME):
	case PROP_ID(PR_MAILBOX_OWNER_NAME):
	case PROP_ID(PR_USER_ENTRYID):
	case PROP_ID(PR_MAILBOX_OWNER_ENTRYID):
	case PROP_ID(PR_EC_MAILBOX_OWNER_ACCOUNT):
	case PROP_ID(PR_EC_HIERARCHYID):
	case PROP_ID(PR_EC_STORETYPE):
	case PROP_ID(PR_INSTANCE_KEY):
	case PROP_ID(PR_OBJECT_TYPE):
	case PROP_ID(PR_SOURCE_KEY):
	case PROP_ID(PR_PARENT_SOURCE_KEY):
	case PROP_ID(PR_RIGHTS):
	case PROP_ID(PR_ACCESS_LEVEL):
	case PROP_ID(PR_ACCESS):
	case PROP_ID(PR_ROW_TYPE):
	case PROP_ID(PR_MAPPING_SIGNATURE):
		return erSuccess;
	case PROP_ID(PR_RECORD_KEY):
		return ulObjType == MAPI_ATTACH ? KCERR_NOT_FOUND : erSuccess;
	case PROP_ID(PR_DISPLAY_NAME): // only the store property is generated
	case PROP_ID(PR_EC_DELETED_STORE):
		return ulObjType == MAPI_STORE ? erSuccess : KCERR_NOT_FOUND;
	case PROP_ID(PR_CONTENT_COUNT):
		return ulObjType == MAPI_MESSAGE ? erSuccess : KCERR_NOT_FOUND;
        default:
		return KCERR_NOT_FOUND;
    }
}

// These are properties that are never written to the 'properties' table; ie they are never directly queried. This
// is not the same as the generated properties, as they may access data in the database to *create* a generated
// property. 
ECRESULT ECGenProps::IsPropRedundant(unsigned int ulPropTag, unsigned int ulObjType)
{
    switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_ACCESS):					// generated from ACLs
	case PROP_ID(PR_USER_NAME):				// generated from owner (hierarchy)
	case PROP_ID(PR_MAILBOX_OWNER_NAME):		// generated from owner (hierarchy)
	case PROP_ID(PR_USER_ENTRYID):			// generated from owner (hierarchy)
	case PROP_ID(PR_MAILBOX_OWNER_ENTRYID):	// generated from owner (hierarchy)
	case PROP_ID(PR_EC_MAILBOX_OWNER_ACCOUNT): // generated from owner (hierarchy)
	case PROP_ID(PR_EC_HIERARCHYID):			// generated from hierarchy
	case PROP_ID(PR_SUBFOLDERS):				// generated from hierarchy
	case PROP_ID(PR_HASATTACH):				// generated from hierarchy
	case PROP_ID(PR_LONGTERM_ENTRYID_FROM_TABLE): // generated from hierarchy
	case PROP_ID(PR_ENTRYID):				// generated from hierarchy
	case PROP_ID(PR_PARENT_ENTRYID): 		// generated from hierarchy
	case PROP_ID(PR_STORE_ENTRYID):			// generated from store id
	case PROP_ID(PR_STORE_RECORD_KEY):		// generated from store id
	case PROP_ID(PR_INSTANCE_KEY):			// table data only
	case PROP_ID(PR_OBJECT_TYPE):			// generated from hierarchy
	case PROP_ID(PR_CONTENT_COUNT):			// generated from hierarchy
	case PROP_ID(PR_CONTENT_UNREAD):			// generated from hierarchy
	case PROP_ID(PR_RIGHTS):					// generated from security system
	case PROP_ID(PR_ACCESS_LEVEL):			// generated from security system
	case PROP_ID(PR_PARENT_SOURCE_KEY):		// generated from ics system
	case PROP_ID(PR_FOLDER_TYPE):			// generated from hierarchy (CreateFolder)
	case PROP_ID(PR_EC_IMAP_ID):				// generated for each new mail and updated on move by the server
		return erSuccess;
	case PROP_ID(PR_RECORD_KEY):				// generated from hierarchy except for attachments
		return ulObjType == MAPI_ATTACH ? KCERR_NOT_FOUND : erSuccess;
	default:
		return KCERR_NOT_FOUND;
    }
}

ECRESULT ECGenProps::GetPropComputed(struct soap *soap, unsigned int ulObjType, unsigned int ulPropTagRequested, unsigned int ulObjId, struct propVal *lpPropVal)
{
	ECRESULT		er = erSuccess;

	switch(PROP_ID(ulPropTagRequested)) {
	case PROP_ID(PR_MSG_STATUS):
		if (lpPropVal->ulPropTag == ulPropTagRequested)
			return KCERR_NOT_FOUND;
		lpPropVal->ulPropTag = PR_MSG_STATUS;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		lpPropVal->Value.ul = 0;
		break;
    case PROP_ID(PR_EC_IMAP_ID):
		if (lpPropVal->ulPropTag == ulPropTagRequested)
			return KCERR_NOT_FOUND;
		lpPropVal->ulPropTag = PR_EC_IMAP_ID;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		lpPropVal->Value.ul = ulObjId;
		break;
	case PROP_ID(PR_CONTENT_UNREAD):
		// Convert from PR_MESSAGE_FLAGS to PR_CONTENT_UNREAD
		if (ulObjType != MAPI_MESSAGE || lpPropVal->ulPropTag == PR_CONTENT_UNREAD)
			return KCERR_NOT_FOUND;
		lpPropVal->ulPropTag = PR_CONTENT_UNREAD;
		lpPropVal->__union = SOAP_UNION_propValData_ul;
		lpPropVal->Value.ul = lpPropVal->Value.ul & MSGFLAG_READ ? 0 : 1;
		break;
    case PROP_ID(PR_NORMALIZED_SUBJECT):
    	if(lpPropVal->ulPropTag != PR_SUBJECT) {
    		lpPropVal->ulPropTag = PROP_TAG(PT_ERROR, PROP_ID(PR_NORMALIZED_SUBJECT));
    		lpPropVal->Value.ul = KCERR_NOT_FOUND;
    		lpPropVal->__union = SOAP_UNION_propValData_ul;
    	} else {
		lpPropVal->ulPropTag = ulPropTagRequested;
		// Check for RE, FWD and similar muck at the start of the subject line   		
		const char *lpszColon = strchr(lpPropVal->Value.lpszA, ':');
		if (lpszColon != NULL && (lpszColon - lpPropVal->Value.lpszA) > 1 && (lpszColon - lpPropVal->Value.lpszA) < 4)
		{
			char* c = lpPropVal->Value.lpszA;
			while (c < lpszColon && isdigit(*c))
				++c; // test for all digits prefix
			if (c != lpszColon)
			{
				++lpszColon; // skip ':'
				size_t newlength = strlen(lpszColon);
				if (newlength > 0 && lpszColon[0] == ' ')
				{
					++lpszColon; // skip space
					--newlength; // adjust length
				}
				lpPropVal->Value.lpszA = s_alloc<char>(soap, newlength + 1);
				memcpy(lpPropVal->Value.lpszA, lpszColon, newlength);
				lpPropVal->Value.lpszA[newlength] = '\0';	// add C-type string terminator
			}
		}
	}
        break;
	case PROP_ID(PR_SUBMIT_FLAGS):
		if (ulObjType == MAPI_MESSAGE) {
			if (g_lpSessionManager->GetLockManager()->IsLocked(ulObjId, NULL))
				lpPropVal->Value.ul |= SUBMITFLAG_LOCKED;
			else
				lpPropVal->Value.ul &= ~SUBMITFLAG_LOCKED;
		}
		break;
	case PROP_ID(PR_RECORD_KEY):
		if (ulObjType != MAPI_ATTACH || lpPropVal->ulPropTag == ulPropTagRequested)
			return KCERR_NOT_FOUND;
		lpPropVal->ulPropTag = PR_RECORD_KEY;
		lpPropVal->__union = SOAP_UNION_propValData_bin;
		lpPropVal->Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		lpPropVal->Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ULONG));
		lpPropVal->Value.bin->__size = sizeof(ULONG);
		memcpy(lpPropVal->Value.bin->__ptr, &ulObjId, sizeof(ULONG));
		break;
	default:
		return KCERR_NOT_FOUND;
	}
	return er;
}

// All in memory properties
ECRESULT ECGenProps::GetPropComputedUncached(struct soap *soap, ECODStore *lpODStore, ECSession* lpSession, unsigned int ulPropTag, unsigned int ulObjId, unsigned int ulOrderId, unsigned int ulStoreId, unsigned int ulParentId, unsigned int ulObjType, struct propVal *lpPropVal)
{
	ECRESULT		er = erSuccess;
	unsigned int	ulRights = 0;
	bool			bOwner = false;
	bool			bAdmin = false;
	unsigned int	ulFlags = 0;
	unsigned int	ulUserId = 0;
	char*			lpStoreName = NULL;
	struct propVal sPropVal{__gszeroinit};

	struct propValArray sPropValArray{__gszeroinit};
	struct propTagArray sPropTagArray{__gszeroinit};

	switch(PROP_ID(ulPropTag)) {
	case PROP_ID(PR_LONGTERM_ENTRYID_FROM_TABLE):
	case PROP_ID(PR_ENTRYID):
	case PROP_ID(PR_PARENT_ENTRYID):
	case PROP_ID(PR_STORE_ENTRYID):
	case PROP_ID(PR_RECORD_KEY):
	{
		entryId sEntryId;
		unsigned int ulEidFlags = 0;

		if (ulPropTag == PR_PARENT_ENTRYID) {
			if(ulParentId == 0) {
				er = lpSession->GetSessionManager()->GetCacheManager()->GetParent(ulObjId, &ulParentId);
				if(er != erSuccess)
					goto exit;
			}
			er = lpSession->GetSessionManager()->GetCacheManager()->GetObject(ulParentId, NULL, NULL, &ulFlags, &ulObjType);
			if (er != erSuccess)
				goto exit;
			if (ulObjType == MAPI_FOLDER)
				ulObjId = ulParentId;
		} else if (ulPropTag == PR_STORE_ENTRYID || ulObjId == ulStoreId) {
			ulObjId = ulStoreId;
			if (lpODStore && lpODStore->ulTableFlags & TABLE_FLAG_OVERRIDE_HOME_MDB)
				ulEidFlags = OPENSTORE_OVERRIDE_HOME_MDB;
		}

		er = lpSession->GetSessionManager()->GetCacheManager()->GetEntryIdFromObject(ulObjId, soap, ulEidFlags, &sEntryId);
		if (er != erSuccess) {
			// happens on recipients, attachments and msg-in-msg .. TODO: add strict type checking?
			//assert(false);
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		sPropVal.ulPropTag = ulPropTag;
		sPropVal.__union = SOAP_UNION_propValData_bin;
		sPropVal.Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		sPropVal.Value.bin->__ptr = sEntryId.__ptr;
		sPropVal.Value.bin->__size = sEntryId.__size;
		break;
	}
	case PROP_ID(PR_STORE_RECORD_KEY):
		sPropVal.__union = SOAP_UNION_propValData_bin;
		sPropVal.ulPropTag = ulPropTag;
		sPropVal.Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		sPropVal.Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(GUID));
		sPropVal.Value.bin->__size = sizeof(GUID);
		er = lpSession->GetSessionManager()->GetCacheManager()->GetStore(ulStoreId, 0, (GUID *)sPropVal.Value.bin->__ptr);
		if (er != erSuccess) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_USER_ENTRYID):
		sPropTagArray.__ptr = new unsigned int[1];
		sPropTagArray.__ptr[0] = PR_ENTRYID;
		sPropTagArray.__size = 1;
		ulUserId = lpSession->GetSecurity()->GetUserId();
		if (lpSession->GetUserManagement()->GetProps(soap, ulUserId, &sPropTagArray, &sPropValArray) == erSuccess &&
		    sPropValArray.__ptr && sPropValArray.__ptr[0].ulPropTag == PR_ENTRYID)
		{
			sPropVal.__union = sPropValArray.__ptr[0].__union;
			sPropVal.ulPropTag = PR_USER_ENTRYID;
			sPropVal.Value.bin = sPropValArray.__ptr[0].Value.bin; // memory is allocated in GetUserData(..)
		} else {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_USER_NAME):
		sPropTagArray.__ptr = new unsigned int[1];
		sPropTagArray.__ptr[0] = PR_ACCOUNT;
		sPropTagArray.__size = 1;
		ulUserId = lpSession->GetSecurity()->GetUserId();
		if (lpSession->GetUserManagement()->GetProps(soap, ulUserId, &sPropTagArray, &sPropValArray) == erSuccess &&
		    sPropValArray.__ptr && sPropValArray.__ptr[0].ulPropTag == PR_ACCOUNT)
		{
			sPropVal.__union = sPropValArray.__ptr[0].__union;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(PR_USER_NAME, (PROP_TYPE(ulPropTag)));
			sPropVal.Value.lpszA = sPropValArray.__ptr[0].Value.lpszA;// memory is allocated in GetUserData(..)
		} else {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_DISPLAY_NAME):
	{
		unsigned int ulStoreType = 0;

		if (ulObjType != MAPI_STORE) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		er = lpSession->GetSessionManager()->GetCacheManager()->GetStoreAndType(ulObjId, NULL, NULL, &ulStoreType);
		if (er != erSuccess)
			goto exit;
		er = GetStoreName(soap, lpSession, ulObjId, ulStoreType, &lpStoreName);
		if (er != erSuccess)
			goto exit;
		sPropVal.__union = SOAP_UNION_propValData_lpszA;
		sPropVal.ulPropTag = CHANGE_PROP_TYPE(PR_DISPLAY_NAME, (PROP_TYPE(ulPropTag)));
		sPropVal.Value.lpszA = lpStoreName;
		break;
	}
	case PROP_ID(PR_MAILBOX_OWNER_NAME):
		sPropTagArray.__ptr = new unsigned int[1];
		sPropTagArray.__ptr[0] = PR_DISPLAY_NAME;
		sPropTagArray.__size = 1;

		if (lpSession->GetSecurity()->GetStoreOwner(ulStoreId, &ulUserId) == erSuccess &&
		    lpSession->GetUserManagement()->GetProps(soap, ulUserId, &sPropTagArray, &sPropValArray) == erSuccess &&
		    sPropValArray.__ptr && sPropValArray.__ptr[0].ulPropTag == PR_DISPLAY_NAME)
		{
			sPropVal.__union = sPropValArray.__ptr[0].__union;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(PR_MAILBOX_OWNER_NAME, (PROP_TYPE(ulPropTag)));
			sPropVal.Value.lpszA = sPropValArray.__ptr[0].Value.lpszA; // memory is allocated in GetUserData(..)
		} else {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_MAILBOX_OWNER_ENTRYID):
		sPropTagArray.__ptr = new unsigned int[1];
		sPropTagArray.__ptr[0] = PR_ENTRYID;
		sPropTagArray.__size = 1;

		if (lpSession->GetSecurity()->GetStoreOwner(ulStoreId, &ulUserId) == erSuccess &&
		    lpSession->GetUserManagement()->GetProps(soap, ulUserId, &sPropTagArray, &sPropValArray) == erSuccess &&
		    sPropValArray.__ptr && sPropValArray.__ptr[0].ulPropTag == PR_ENTRYID)
		{
			sPropVal.__union = sPropValArray.__ptr[0].__union;
			sPropVal.ulPropTag = PR_MAILBOX_OWNER_ENTRYID;
			sPropVal.Value.bin = sPropValArray.__ptr[0].Value.bin;// memory is allocated in GetUserData(..)
		} else {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_EC_MAILBOX_OWNER_ACCOUNT):
		sPropTagArray.__ptr = new unsigned int[1];
		sPropTagArray.__ptr[0] = PR_ACCOUNT;
		sPropTagArray.__size = 1;

		if (lpSession->GetSecurity()->GetStoreOwner(ulStoreId, &ulUserId) == erSuccess &&
		    lpSession->GetUserManagement()->GetProps(soap, ulUserId, &sPropTagArray, &sPropValArray) == erSuccess &&
		    sPropValArray.__ptr && sPropValArray.__ptr[0].ulPropTag == PR_ACCOUNT) {
			sPropVal.__union = sPropValArray.__ptr[0].__union;
			sPropVal.ulPropTag = CHANGE_PROP_TYPE(PR_EC_MAILBOX_OWNER_ACCOUNT, (PROP_TYPE(ulPropTag)));
			sPropVal.Value.lpszA = sPropValArray.__ptr[0].Value.lpszA; // memory is allocated in GetUserData(..)
		} else {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_EC_HIERARCHYID):
		sPropVal.ulPropTag = ulPropTag;
		sPropVal.__union = SOAP_UNION_propValData_ul;
		sPropVal.Value.ul = ulObjId;
		break;
	case PROP_ID(PR_EC_STORETYPE): {
		unsigned int ulStoreType = 0;

		if (ulObjType != MAPI_STORE) {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		er = lpSession->GetSessionManager()->GetCacheManager()->GetStoreAndType(ulObjId, NULL, NULL, &ulStoreType);
		if (er != erSuccess)
			goto exit;
		sPropVal.ulPropTag = ulPropTag;
		sPropVal.__union = SOAP_UNION_propValData_ul;
		sPropVal.Value.ul = ulStoreType;
		break;
	}
	case PROP_ID(PR_INSTANCE_KEY):
		sPropVal.ulPropTag = ulPropTag;
		sPropVal.__union = SOAP_UNION_propValData_bin;
		sPropVal.Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		sPropVal.Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(ULONG) * 2);
		sPropVal.Value.bin->__size = sizeof(ULONG) * 2;
		memcpy(sPropVal.Value.bin->__ptr, &ulObjId, sizeof(ULONG));
		memcpy(sPropVal.Value.bin->__ptr+sizeof(ULONG), &ulOrderId, sizeof(ULONG));
		break;
	case PROP_ID(PR_OBJECT_TYPE):
		sPropVal.ulPropTag = PR_OBJECT_TYPE;
		sPropVal.__union = SOAP_UNION_propValData_ul;
		sPropVal.Value.ul = ulObjType;
		break;
	case PROP_ID(PR_SOURCE_KEY):
		sPropVal.ulPropTag = PR_SOURCE_KEY;
		sPropVal.__union = SOAP_UNION_propValData_bin;
		sPropVal.Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		sPropVal.Value.bin->__size = 0;
		sPropVal.Value.bin->__ptr = NULL;
		er = lpSession->GetSessionManager()->GetCacheManager()->GetPropFromObject(PROP_ID(PR_SOURCE_KEY), ulObjId, soap, (unsigned int *)&sPropVal.Value.bin->__size, &sPropVal.Value.bin->__ptr);
		break;
	case PROP_ID(PR_PARENT_SOURCE_KEY):
		sPropVal.ulPropTag = PR_PARENT_SOURCE_KEY;
		sPropVal.__union = SOAP_UNION_propValData_bin;
		sPropVal.Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		sPropVal.Value.bin->__size = 0;
		sPropVal.Value.bin->__ptr = NULL;
		if (ulParentId == 0) {
			er = lpSession->GetSessionManager()->GetCacheManager()->GetObject(ulObjId, &ulParentId, NULL, NULL, NULL);
			if (er != erSuccess)
				goto exit;
		}
		er = lpSession->GetSessionManager()->GetCacheManager()->GetPropFromObject(PROP_ID(PR_SOURCE_KEY), ulParentId, soap, (unsigned int *)&sPropVal.Value.bin->__size, &sPropVal.Value.bin->__ptr);
		if (er != erSuccess)
			goto exit;
		break;
	case PROP_ID(PR_CONTENT_COUNT):
		if (ulObjType == MAPI_MESSAGE) {
			sPropVal.ulPropTag = ulPropTag;
			sPropVal.__union = SOAP_UNION_propValData_ul;
			sPropVal.Value.ul = 1;
		} else {
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_RIGHTS):
		if (ulObjType != MAPI_FOLDER)
		{
			er = KCERR_NOT_FOUND;
			goto exit;
		}

		sPropVal.ulPropTag = PR_RIGHTS;
		sPropVal.__union = SOAP_UNION_propValData_ul;

		if (lpSession->GetSecurity()->IsStoreOwner(ulObjId) == erSuccess || lpSession->GetSecurity()->IsAdminOverOwnerOfObject(ulObjId) == erSuccess)
			sPropVal.Value.ul = ecRightsAll;
		else if (lpSession->GetSecurity()->GetObjectPermission(ulObjId, &ulRights) == hrSuccess)
			sPropVal.Value.ul = ulRights;
		else
			sPropVal.Value.ul = 0;
		break;
	case PROP_ID(PR_ACCESS):
		if (ulObjType == MAPI_STORE || ulObjType == MAPI_ATTACH)
		{
			er = KCERR_NOT_FOUND;
			goto exit;
		}

		sPropVal.ulPropTag = PR_ACCESS;
		sPropVal.__union = SOAP_UNION_propValData_ul;
		sPropVal.Value.ul = 0;

		// Optimize: for a message, the rights are equal to that of the parent. It is more efficient for
		// the cache to check the folder permissions than the message permissions
		if (ulObjType == MAPI_MESSAGE && ulParentId)
			ulObjId = ulParentId;

		// if the requested object is from the owners store, return all permissions
		if (lpSession->GetSecurity()->IsStoreOwner(ulObjId) == erSuccess ||
			lpSession->GetSecurity()->IsAdminOverOwnerOfObject(ulObjId) == erSuccess) {
			switch (ulObjType) {
			case MAPI_FOLDER:
				sPropVal.Value.ul = MAPI_ACCESS_READ | MAPI_ACCESS_MODIFY | MAPI_ACCESS_DELETE;
				if (ulFlags != FOLDER_SEARCH) //FOLDER_GENERIC, FOLDER_ROOT
					sPropVal.Value.ul |= MAPI_ACCESS_CREATE_HIERARCHY | MAPI_ACCESS_CREATE_CONTENTS | MAPI_ACCESS_CREATE_ASSOCIATED;
				break;
			case MAPI_MESSAGE:
				sPropVal.Value.ul = MAPI_ACCESS_READ | MAPI_ACCESS_MODIFY | MAPI_ACCESS_DELETE;
				break;
			case MAPI_ATTACH:
			case MAPI_STORE:
			default:
				er = KCERR_NOT_FOUND;
				goto exit;
			}
			break;
		}

		// someone else is accessing your store, so check their rights
		ulRights = 0;
		lpSession->GetSecurity()->GetObjectPermission(ulObjId, &ulRights); // skip error checking, ulRights = 0

		// will be false when someone else created this object in this store (or true if you're that someone)
		bOwner = (lpSession->GetSecurity()->IsOwner(ulObjId) == erSuccess);

		switch (ulObjType) {
		case MAPI_FOLDER:
			if ((ulRights & ecRightsReadAny) == ecRightsReadAny)
				sPropVal.Value.ul |= MAPI_ACCESS_READ;
			if (bOwner == true || (ulRights & ecRightsFolderAccess) == ecRightsFolderAccess)
				sPropVal.Value.ul |= MAPI_ACCESS_DELETE | MAPI_ACCESS_MODIFY;

			if (ulFlags != FOLDER_SEARCH) //FOLDER_GENERIC, FOLDER_ROOT
			{
				if ((ulRights & ecRightsCreateSubfolder) == ecRightsCreateSubfolder)
					sPropVal.Value.ul |= MAPI_ACCESS_CREATE_HIERARCHY;
				if ((ulRights & ecRightsCreate) == ecRightsCreate)
					sPropVal.Value.ul |= MAPI_ACCESS_CREATE_CONTENTS;

				// olk2k7 fix: if we have delete access, we must set create contents access (eventhough an actual saveObject will still be denied) for deletes to work.
				if ((ulRights & ecRightsDeleteAny) == ecRightsDeleteAny || (bOwner == true && (ulRights & ecRightsDeleteOwned) == ecRightsDeleteOwned))
					sPropVal.Value.ul |= MAPI_ACCESS_CREATE_CONTENTS;
				if ((ulRights & ecRightsFolderAccess) == ecRightsFolderAccess)
					sPropVal.Value.ul |= MAPI_ACCESS_CREATE_ASSOCIATED;
			}
			break;
		case MAPI_MESSAGE:
			if ((ulRights & ecRightsReadAny) == ecRightsReadAny)
				sPropVal.Value.ul |= MAPI_ACCESS_READ;
			if ((ulRights & ecRightsEditAny) == ecRightsEditAny || (bOwner == true && (ulRights & ecRightsEditOwned) == ecRightsEditOwned))
				sPropVal.Value.ul |= MAPI_ACCESS_MODIFY;
			if ((ulRights & ecRightsDeleteAny) == ecRightsDeleteAny || (bOwner == true && (ulRights & ecRightsDeleteOwned) == ecRightsDeleteOwned))
				sPropVal.Value.ul |= MAPI_ACCESS_DELETE;
			break;
		case MAPI_ATTACH:
		case MAPI_STORE:
		default:
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_ACCESS_LEVEL):
	{
		sPropVal.ulPropTag = PR_ACCESS_LEVEL;
		sPropVal.__union = SOAP_UNION_propValData_ul;
		sPropVal.Value.ul = 0;
		ulRights = 0;

		// @todo if store only open with read rights, access level = 0
		if (lpSession->GetSecurity()->IsAdminOverOwnerOfObject(ulObjId) == erSuccess)
			bAdmin = true; // Admin of all stores
		else if (lpSession->GetSecurity()->IsStoreOwner(ulObjId) == erSuccess)
			bAdmin = true; // Admin of your one store
		else {
			lpSession->GetSecurity()->GetObjectPermission(ulObjId, &ulRights); // skip error checking, ulRights = 0
			bOwner = lpSession->GetSecurity()->IsOwner(ulObjId) == erSuccess; // owner of this particular object in someone else's store
		}
		if (bAdmin || ulRights & ecRightsCreate || ulRights & ecRightsEditAny || ulRights & ecRightsDeleteAny || ulRights & ecRightsCreateSubfolder ||
		   (bOwner && (ulRights & ecRightsEditOwned || ulRights & ecRightsDeleteOwned)))
			sPropVal.Value.ul = MAPI_MODIFY;
		break;
	}
	case PROP_ID(PR_ROW_TYPE):
		sPropVal.ulPropTag = ulPropTag;
		sPropVal.__union = SOAP_UNION_propValData_ul;
		sPropVal.Value.ul = TBL_LEAF_ROW;
		break;
	case PROP_ID(PR_MAPPING_SIGNATURE):
		sPropVal.ulPropTag = ulPropTag;
		sPropVal.Value.bin = s_alloc<struct xsd__base64Binary>(soap);
		sPropVal.Value.bin->__ptr = s_alloc<unsigned char>(soap, sizeof(GUID));
		sPropVal.__union = SOAP_UNION_propValData_bin;
		sPropVal.Value.bin->__size = sizeof(GUID);
		er = lpSession->GetServerGUID((GUID *)sPropVal.Value.bin->__ptr);
		if (er != erSuccess){
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	case PROP_ID(PR_EC_DELETED_STORE):
		sPropVal.ulPropTag = PR_EC_DELETED_STORE;
		sPropVal.__union = SOAP_UNION_propValData_b;
		er = IsOrphanStore(lpSession, ulObjId, &sPropVal.Value.b);
		if (er != erSuccess){
			er = KCERR_NOT_FOUND;
			goto exit;
		}
		break;
	default:
		er = KCERR_NOT_FOUND;
		goto exit;
	}

	*lpPropVal = std::move(sPropVal);
exit:
	delete[] sPropTagArray.__ptr;

	if(soap == NULL) { // soap != NULL gsoap will cleanup the memory
		delete[] sPropValArray.__ptr;

		if (er != erSuccess)
			FreePropVal(&sPropVal, false);
	}

	return er;
}

/**
 * Is the given store a orphan store.
 *
 * @param[in] lpSession	Session to use for this context
 * @param[in] ulObjId	Hierarchy id of a store
 * @param[out] lpbIsOrphan	True is the store is a orphan store, false if not.
 */
ECRESULT ECGenProps::IsOrphanStore(ECSession* lpSession, unsigned int ulObjId, bool *lpbIsOrphan)
{
	ECRESULT	er = erSuccess;
	ECDatabase *lpDatabase = NULL;
	DB_RESULT lpDBResult;
	std::string strQuery;
	bool		bIsOrphan = false;

	if (!lpSession || !lpbIsOrphan) {
		er = KCERR_INVALID_PARAMETER;
		goto exit;
	}

	er = lpSession->GetDatabase(&lpDatabase);
	if (er != erSuccess)
		goto exit;

	strQuery = "SELECT 0 FROM stores as s LEFT JOIN users as u ON s.user_id=u.id WHERE s.user_id != 0 and s.hierarchy_id="+stringify(ulObjId) + " AND u.id IS NOT NULL";
	er = lpDatabase->DoSelect(strQuery, &lpDBResult);
	if(er != erSuccess)
		goto exit;

	if (lpDatabase->GetNumRows(lpDBResult) == 0)
		bIsOrphan = true;

	*lpbIsOrphan = bIsOrphan;

exit:
	lpDatabase->FreeResult(lpDBResult);
	return er;
}

/* Get store name
 *
 * Gets the PR_DISPLAY_NAME for the given store ID
 *
 * @param soap gSOAP struct for memory allocation
 * @param lpSession Session to use for this context
 * @param ulStoreId Store ID to get display name for
 * @param lppStoreName Output pointer
 * @return result
 */
ECRESULT ECGenProps::GetStoreName(struct soap *soap, ECSession* lpSession, unsigned int ulStoreId, unsigned int ulStoreType, char** lppStoreName)
{
	ECRESULT			er = erSuccess;
	unsigned int		ulUserId = 0;
	unsigned int	    ulCompanyId = 0;
	struct propValArray sPropValArray{__gszeroinit};
	struct propTagArray sPropTagArray{__gszeroinit};

	string				strFormat;
	char*				lpStoreName = NULL;

	er = lpSession->GetSecurity()->GetStoreOwner(ulStoreId, &ulUserId);
	if (er != erSuccess)
		goto exit;

	// get the companyid to which the logged in user belongs to.
	er = lpSession->GetSecurity()->GetUserCompany(&ulCompanyId);
	if (er != erSuccess)
		goto exit;

	// When the userid belongs to a company or group everybody, the store is considered a public store.
	if(ulUserId == KOPANO_UID_EVERYONE || ulUserId == ulCompanyId) {
	    strFormat = _("Public Folders");
	} else {
        sPropTagArray.__ptr = new unsigned int[3];
        sPropTagArray.__ptr[0] = PR_DISPLAY_NAME;
        sPropTagArray.__ptr[1] = PR_ACCOUNT;
        sPropTagArray.__ptr[2] = PR_EC_COMPANY_NAME;
        sPropTagArray.__size = 3;

        er = lpSession->GetUserManagement()->GetProps(soap, ulUserId, &sPropTagArray, &sPropValArray);
        if (er != erSuccess || !sPropValArray.__ptr) {
            er = KCERR_NOT_FOUND;
            goto exit;
        }

        strFormat = string(lpSession->GetSessionManager()->GetConfig()->GetSetting("storename_format"));

        for (gsoap_size_t i = 0; i < sPropValArray.__size; ++i) {
            string sub;
            size_t pos = 0;

            switch (sPropValArray.__ptr[i].ulPropTag) {
            case PR_DISPLAY_NAME:
                sub = "%f";
                break;
            case PR_ACCOUNT:
                sub = "%u";
                break;
            case PR_EC_COMPANY_NAME:
                sub = "%c";
                break;
            default:
                break;
            }

            if (sub.empty())
                continue;

            while ((pos = strFormat.find(sub, pos)) != string::npos)
                strFormat.replace(pos, sub.size(), sPropValArray.__ptr[i].Value.lpszA);
        }

		if (ulStoreType == ECSTORE_TYPE_PRIVATE)
			strFormat = string(_("Inbox")) + " - " + strFormat;
		else if (ulStoreType == ECSTORE_TYPE_ARCHIVE)
			strFormat = string(_("Archive")) + " - " + strFormat;
		else
			assert(false);
    }
    
	lpStoreName = s_alloc<char>(soap, strFormat.size() + 1);
	strcpy(lpStoreName, strFormat.c_str());

	*lppStoreName = lpStoreName;

exit:
	delete[] sPropTagArray.__ptr;
	return er;
}

} /* namespace */
