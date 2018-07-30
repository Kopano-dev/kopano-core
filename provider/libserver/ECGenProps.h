/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECGENPROPS_H
#define ECGENPROPS_H

#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include <string>
#include "ECSession.h"

struct soap;

namespace KC {

/*
 * This class is a general serverside class for generated properties. A Generated
 * property is any property that cannot be directly stored or read from the database.
 * This means properties like PR_ENTRYID because they are not stored as a property, 
 * properties like PR_NORMALIZED_SUBJECT because they are computed from the PR_SUBJECT,
 * and PR_MAPPING_SIGNATURE because they are stored elsewhere within the database.
 *
 * The client can also generate some properties, but only if these are fixed properties
 * that don't have any storage on the server, and they are also properties that are
 * never sorted on in tables. (due to the server actually doing the sorting)
 */

struct ECODStore;

class ECGenProps _kc_final {
public:
	// Returns whether a different property should be retrieved instead of the
	// requested property.
	static ECRESULT	GetPropSubstitute(unsigned int ulObjType, unsigned int ulPropTagRequested, unsigned int *lpulPropTagRequired);
	// Return erSuccess if a property can be generated in GetPropComputed()
	static ECRESULT IsPropComputed(unsigned int ulPropTag, unsigned int ulObjType);
	// Return erSuccess if a property can be generated in GetPropComputedUncached()
	static ECRESULT IsPropComputedUncached(unsigned int ulPropTag, unsigned int ulObjType);
	// Return erSuccess if a property needn't be saved in the properties table
	static ECRESULT IsPropRedundant(unsigned int ulPropTag, unsigned int ulObjType);
	// Returns a subquery to run for the specified column
	static ECRESULT GetPropSubquery(unsigned int ulPropTagRequested, std::string &subquery);
	// Does post-processing after retrieving data from either cache or DB
	static ECRESULT	GetPropComputed(struct soap *soap, unsigned int ulObjType, unsigned int ulPropTagRequested, unsigned int ulObjId, struct propVal *lpsPropVal);
	// returns the computed value for a property which doesn't has database actions
	static ECRESULT GetPropComputedUncached(struct soap *, const ECODStore *, ECSession *, unsigned int proptag, unsigned int obj_id, unsigned int order_id, unsigned int store_id, unsigned int parent_id, unsigned int obj_type, struct propVal *);
	static ECRESULT GetMVPropSubquery(unsigned int ulPropTagRequested, std::string &subquery);
	static ECRESULT GetStoreName(struct soap *soap, ECSession* lpSession, unsigned int ulStoreId, unsigned int ulStoreType, char** lppStoreName);
	static ECRESULT IsOrphanStore(ECSession* lpSession, unsigned int ulObjId, bool *lpbIsOrphan);
};

} /* namespace */

#endif // ECGENPROPS_H

