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

#ifndef SOAPUTILS_H
#define SOAPUTILS_H

#include "soapH.h"
#include "SOAPAlloc.h"
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include <kopano/pcuser.hpp>
#include <kopano/ustringutil.h>

#include <list>
#include <string>

namespace KC {

// SortOrderSets
extern void FreeSortOrderArray(struct sortOrderArray *lpsSortOrder);
extern int CompareSortOrderArray(const struct sortOrderArray *lpsSortOrder1, const struct sortOrderArray *lpsSortOrder2);

// PropTagArrays
extern ECRESULT CopyPropTagArray(struct soap *soap, const struct propTagArray* lpPTsSrc, struct propTagArray** lppsPTsDst);
extern void FreePropTagArray(struct propTagArray *lpsPropTags, bool bFreeBase = true);

// RowSets
void				FreeRowSet(struct rowSet *lpRowSet, bool bBasePointerDel);

// Restrictions
extern ECRESULT FreeRestrictTable(struct restrictTable *lpRestrict, bool base = true);
extern ECRESULT CopyRestrictTable(struct soap *soap, const struct restrictTable *lpSrc, struct restrictTable **lppDst);

// SearchCriteria
extern ECRESULT CopySearchCriteria(struct soap *soap, const struct searchCriteria *lpSrc, struct searchCriteria **lppDst);
extern ECRESULT FreeSearchCriteria(struct searchCriteria *lpSearchCriteria);

// PropValArrays
extern ECRESULT FreePropValArray(struct propValArray *lpPropValArray, bool bFreeBase = false);
extern struct propVal *FindProp(const struct propValArray *lpPropValArray, unsigned int ulPropTag);
extern ECRESULT CopyPropValArray(const struct propValArray *lpSrc, struct propValArray *lpDst, struct soap *soap);
extern ECRESULT CopyPropValArray(const struct propValArray *lpSrc, struct propValArray **lppDst, struct soap *soap);
extern ECRESULT MergePropValArray(struct soap *soap, const struct propValArray *lpsPropValArray1, const struct propValArray *lpsPropValArray2, struct propValArray *lpPropValArrayNew);

// PropVals
extern ECRESULT CompareProp(const struct propVal *lpProp1, const struct propVal *lpProp2, const ECLocale &locale, int *lpCompareResult);
extern ECRESULT CompareMVPropWithProp(struct propVal *lpMVProp1, const struct propVal *lpProp2, unsigned int ulType, const ECLocale &locale, bool* lpfMatch);

size_t PropSize(const struct propVal *);
ECRESULT			FreePropVal(struct propVal *lpProp, bool bBasePointerDel);
ECRESULT CopyPropVal(const struct propVal *lpSrc, struct propVal *lpDst, struct soap *soap = NULL, bool bTruncate = false);
ECRESULT CopyPropVal(const struct propVal *lpSrc, struct propVal **lppDst, struct soap *soap = NULL, bool bTruncate = false); /* allocates new lpDst and calls other version */

// EntryList
ECRESULT			CopyEntryList(struct soap *soap, struct entryList *lpSrc, struct entryList **lppDst);
ECRESULT			FreeEntryList(struct entryList *lpEntryList, bool bFreeBase = true);

// EntryId
ECRESULT			CopyEntryId(struct soap *soap, entryId* lpSrc, entryId** lppDst);
ECRESULT			FreeEntryId(entryId* lpEntryId, bool bFreeBase);

// Notification
ECRESULT			FreeNotificationStruct(notification *lpNotification, bool bFreeBase=true);
ECRESULT			CopyNotificationStruct(struct soap *, const notification *from, notification &to);

ECRESULT			FreeNotificationArrayStruct(notificationArray *lpNotifyArray, bool bFreeBase);
ECRESULT			CopyNotificationArrayStruct(notificationArray *lpNotifyArrayFrom, notificationArray *lpNotifyArrayTo);

// Rights
ECRESULT			FreeRightsArray(struct rightsArray *lpRights);
ECRESULT			CopyRightsArrayToSoap(struct soap *soap, struct rightsArray *lpRightsArraySrc, struct rightsArray **lppRightsArrayDst);

// userobjects
ECRESULT			CopyUserDetailsToSoap(unsigned int ulId, entryId *lpUserEid, const objectdetails_t &details, bool bCopyBinary,
										  struct soap *soap, struct user *lpUser);
ECRESULT			CopyUserDetailsFromSoap(struct user *lpUser, std::string *lpstrExternId, objectdetails_t *details, struct soap *soap);
ECRESULT			CopyGroupDetailsToSoap(unsigned int ulId, entryId *lpGroupEid, const objectdetails_t &details, bool bCopyBinary,
										   struct soap *soap, struct group *lpGroup);
ECRESULT			CopyGroupDetailsFromSoap(struct group *lpGroup, std::string *lpstrExternId, objectdetails_t *details, struct soap *soap);
ECRESULT			CopyCompanyDetailsToSoap(unsigned int ulId, entryId *lpCompanyEid, unsigned int ulAdmin, entryId *lpAdminEid, 
											 const objectdetails_t &details, bool bCopyBinary, struct soap *soap, struct company *lpCompany);
ECRESULT			CopyCompanyDetailsFromSoap(struct company *lpCompany, std::string *lpstrExternId, unsigned int ulAdmin,
											   objectdetails_t *details, struct soap *soap);

ULONG 				NormalizePropTag(ULONG ulPropTag);

const char *GetSourceAddr(struct soap *soap);

size_t SearchCriteriaSize(const struct searchCriteria *);
size_t RestrictTableSize(const struct restrictTable *);
size_t PropValArraySize(const struct propValArray *);
size_t EntryListSize(const struct entryList *);
size_t EntryIdSize(const entryId *);
size_t NotificationStructSize(const notification *);
size_t PropTagArraySize(const struct propTagArray *);
size_t SortOrderArraySize(const struct sortOrderArray *);

class DynamicPropValArray _kc_final {
public:
    DynamicPropValArray(struct soap *soap, unsigned int ulHint = 10);
    ~DynamicPropValArray();
    
    // Copies the passed propVal
    ECRESULT AddPropVal(struct propVal &propVal);
    
    // Return a propvalarray of all properties passed
    ECRESULT GetPropValArray(struct propValArray *lpPropValArray, bool release = true);

private:
    ECRESULT Resize(unsigned int ulSize);
    
    struct soap *m_soap;
    struct propVal *m_lpPropVals;
    unsigned int m_ulCapacity;
	unsigned int m_ulPropCount = 0;
};

class DynamicPropTagArray _kc_final {
public:
    DynamicPropTagArray(struct soap *soap);
    ECRESULT AddPropTag(unsigned int ulPropTag);
    BOOL HasPropTag(unsigned int ulPropTag) const;
    
    ECRESULT GetPropTagArray(struct propTagArray *lpPropTagArray);
    
private:
    std::list<unsigned int> m_lstPropTags;
    struct soap *m_soap;
};

// The structure of the data stored in soap->user on the server side
struct SOAPINFO {
	CONNECTION_TYPE ulConnectionType;
	int (*fparsehdr)(struct soap *soap, const char *key, const char *val);
	bool bProxy;
	void (*fdone)(struct soap *soap, void *param);
	void *fdoneparam;
	ECSESSIONID ulLastSessionId; // Session ID of the last processed request
	struct timespec threadstart; 	// Start count of when the thread started processing the request
	KC::time_point start; /* Start timestamp of when we started processing the request */
	const char *szFname;
};

static inline struct SOAPINFO *soap_info(struct soap *s)
{
	return static_cast<struct SOAPINFO *>(s->user);
}

} /* namespace */

#endif
