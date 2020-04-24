/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/kcodes.h>
#include <string>
#include "ECSession.h"

struct soap;

namespace KC {

struct ECODStore;

namespace ECGenProps {

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

// Returns whether a different property should be retrieved instead of the
// requested property.
extern ECRESULT GetPropSubstitute(unsigned int objtype, unsigned int proptag_requested, unsigned int *proptag_required);
// Return erSuccess if a property can be generated in GetPropComputed()
extern ECRESULT IsPropComputed(unsigned int proptag, unsigned int objtype);
// Return erSuccess if a property can be generated in GetPropComputedUncached()
extern ECRESULT IsPropComputedUncached(unsigned int proptag, unsigned int objtype);
// Return erSuccess if a property needn't be saved in the properties table
extern ECRESULT IsPropRedundant(unsigned int proptag, unsigned int objtype);
// Returns a subquery to run for the specified column
extern ECRESULT GetPropSubquery(unsigned int proptag_requested, std::string &subquery);
// Does post-processing after retrieving data from either cache or DB
extern ECRESULT GetPropComputed(struct soap *soap, unsigned int objtype, unsigned int proptag_requested, unsigned int objid, struct propVal *);
// returns the computed value for a property which doesn't has database actions
extern ECRESULT GetPropComputedUncached(struct soap *, const ECODStore *, ECSession *, unsigned int proptag, unsigned int obj_id, unsigned int order_id, unsigned int store_id, unsigned int parent_id, unsigned int obj_type, struct propVal *);
extern ECRESULT GetStoreName(struct soap *soap, ECSession *, unsigned int store_id, unsigned int store_type, char **store_name);
extern ECRESULT IsOrphanStore(ECSession *, unsigned int obj_id, bool *is_orphan);

} /* namespace */
} /* namespace */
