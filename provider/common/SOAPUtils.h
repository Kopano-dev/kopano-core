/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include "soapH.h"
#include "SOAPAlloc.h"
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include <kopano/pcuser.hpp>
#include <kopano/rqstat.hpp>
#include <kopano/timeutil.hpp>
#include <kopano/ustringutil.h>
#include <memory>
#include <string>
#include <vector>

namespace KC {

extern std::string FilterBMP(const std::string &strToFilter);
// SortOrderSets
extern int CompareSortOrderArray(const struct sortOrderArray *lpsSortOrder1, const struct sortOrderArray *lpsSortOrder2);

// PropTagArrays
extern ECRESULT CopyPropTagArray(struct soap *soap, const struct propTagArray* lpPTsSrc, struct propTagArray** lppsPTsDst);

// Restrictions
extern ECRESULT CopyRestrictTable(struct soap *soap, const struct restrictTable *lpSrc, struct restrictTable **lppDst);

// SearchCriteria
extern ECRESULT CopySearchCriteria(struct soap *soap, const struct searchCriteria *lpSrc, struct searchCriteria **lppDst);

// PropValArrays
extern struct propVal *FindProp(const struct propValArray *lpPropValArray, unsigned int ulPropTag);
extern ECRESULT CopyPropValArray(const struct propValArray *lpSrc, struct propValArray *lpDst, struct soap *soap);
extern ECRESULT CopyPropValArray(const struct propValArray *lpSrc, struct propValArray **lppDst, struct soap *soap);
extern ECRESULT MergePropValArray(struct soap *soap, const struct propValArray *lpsPropValArray1, const struct propValArray *lpsPropValArray2, struct propValArray *lpPropValArrayNew);

// PropVals
extern ECRESULT CompareProp(const struct propVal *lpProp1, const struct propVal *lpProp2, const ECLocale &locale, int *lpCompareResult);
extern ECRESULT CompareMVPropWithProp(struct propVal *lpMVProp1, const struct propVal *lpProp2, unsigned int ulType, const ECLocale &locale, bool* lpfMatch);

size_t PropSize(const struct propVal *);
extern ECRESULT CopyPropVal(const struct propVal *src, struct propVal *dst, struct soap * = nullptr, bool truncate = false);
extern ECRESULT CopyPropVal(const struct propVal *src, struct propVal **dst, struct soap * = nullptr, bool truncate = false); /* allocates new lpDst and calls other version */

// EntryList
ECRESULT			CopyEntryList(struct soap *soap, struct entryList *lpSrc, struct entryList **lppDst);

// EntryId
ECRESULT			CopyEntryId(struct soap *soap, entryId* lpSrc, entryId** lppDst);

// Notification
ECRESULT			CopyNotificationStruct(struct soap *, const notification *from, notification &to);
ECRESULT			CopyNotificationArrayStruct(notificationArray *lpNotifyArrayFrom, notificationArray *lpNotifyArrayTo);

// Rights
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

class DynamicPropValArray final {
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

class DynamicPropTagArray final {
public:
    DynamicPropTagArray(struct soap *soap);
    ECRESULT AddPropTag(unsigned int ulPropTag);
    BOOL HasPropTag(unsigned int ulPropTag) const;
    ECRESULT GetPropTagArray(struct propTagArray *lpPropTagArray);
    
private:
	std::vector<unsigned int> m_lstPropTags;
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
	struct request_stat st;
};

class ec_soap_deleter {
	public:
	void operator()(struct soap *);
};

static inline struct SOAPINFO *soap_info(struct soap *s)
{
	return static_cast<struct SOAPINFO *>(s->user);
}

static inline struct SOAPINFO *soap_info(const std::unique_ptr<struct soap, ec_soap_deleter> &s)
{
	return static_cast<struct SOAPINFO *>(s->user);
}

} /* namespace */
