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

#ifndef ECSUBRESTRICTION_H
#define ECSUBRESTRICTION_H

#include <set>
#include <vector>
#include <kopano/ECKeyTable.h>
#include "ECDatabase.h"
#include "soapH.h"
#include <kopano/ustringutil.h>

namespace KC {

class ECSession;

// These are some helper function to help running subqueries.

/* 
 * How we run subqueries assumes the following:
 * - SubSubRestrictions are invalid
 * - The most efficient way to run a search with a subquery is to first evaluate the subquery and THEN the
 *   main query
 *
 * We number subqueries in a query in a depth-first order. The numbering can be used because we don't have
 * nested subqueries (see above). Each subquery can therefore be pre-calculated for each item in the main
 * query target. The results of the subqueries is then passed to the main query solver, which only needs
 * to check the outcome of a subquery.
 */
 
// A set containing all the objects that match a subquery. The row id here is for the parent object, not for
// the actual object that matched the restriction (ie the message id is in here, not the recipient id or
// attachment id)
typedef std::set<unsigned int> SUBRESTRICTIONRESULT;
// A list of sets of subquery matches
typedef std::vector<SUBRESTRICTIONRESULT> SUBRESTRICTIONRESULTS;

ECRESULT GetSubRestrictionCount(struct restrictTable *lpRestrict, unsigned int *lpulCount);
ECRESULT GetSubRestriction(struct restrictTable *lpBase, unsigned int ulCount, struct restrictSub **lppSubRestrict);

// Get results for all subqueries for a set of objects
extern ECRESULT RunSubRestrictions(ECSession *, const void *ecod_store, struct restrictTable *, ECObjectTableList *, const ECLocale &, SUBRESTRICTIONRESULTS &);

#define SUBRESTRICTION_MAXDEPTH	64

} /* namespace */

#endif
