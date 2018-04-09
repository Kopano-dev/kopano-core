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

// Interface for writing and reading properties to disk (which does the actual transfer and save)
// 
// Strategy is to load most of the small (ie not much data) properties at load-time. This saves
// a lot of overhead if the properties were to be acquired one-by-one over the network. However, a
// complete list of properties is also read through HrReadProps(), so the local system also knows
// about larger properties. These properties are read through HrLoadProp().
//
// When a save is done, only the *changes* to the properties are propagated, by using HrDeleteProps
// and HrWriteProps. HrWriteProps changes and/or adds any new or modified properties, while HrDeleteProps
// removes any properties that have been completely removed.
//
// This keeps the overall performance high, by having relatively low latency problems as most message 
// accesses will be kept down to about 1 to 5 network accesses, and have low bandwidth requirements as
// large data is only loaded on demand.
//

#ifndef IECPROPSTORAGE_H
#define IECPROPSTORAGE_H

#include <mapi.h>
#include <mapispi.h>
#include <list>
#include <set>
#include "ECPropertyEntry.h"
#include "kcore.hpp"
#include <kopano/Util.h>

struct MAPIOBJECT {
	MAPIOBJECT() = default;
	MAPIOBJECT(unsigned int ulType, unsigned int ulId) : ulUniqueId(ulId), ulObjType(ulType) {
	}
	MAPIOBJECT(unsigned int uqid, unsigned int id, unsigned int type) :
		ulUniqueId(uqid), ulObjId(id), ulObjType(type)
	{}
	~MAPIOBJECT();

	bool operator < (const MAPIOBJECT &other) const {
		std::pair<unsigned int, unsigned int> me(ulObjType, ulUniqueId), him(other.ulObjType, other.ulUniqueId);
		
		return me < him;
	};

	struct CompareMAPIOBJECT {
		bool operator()(const MAPIOBJECT *a, const MAPIOBJECT *b) const
		{
			return *a < *b;
		}
	};

	MAPIOBJECT(const MAPIOBJECT &s) :
		lstDeleted(s.lstDeleted), lstAvailable(s.lstAvailable),
		lstModified(s.lstModified), lstProperties(s.lstProperties),
		bChangedInstance(s.bChangedInstance), bChanged(s.bChanged),
		bDelete(s.bDelete), ulUniqueId(s.ulUniqueId),
		ulObjId(s.ulObjId), ulObjType(s.ulObjType)
	{
		KC::Util::HrCopyEntryId(s.cbInstanceID, reinterpret_cast<const ENTRYID *>(s.lpInstanceID),
			&cbInstanceID, reinterpret_cast<ENTRYID **>(&lpInstanceID));
		for (const auto &i : s.lstChildren)
			lstChildren.emplace(new MAPIOBJECT(*i));
	};

	void operator=(const MAPIOBJECT &) = delete;

	/* data */
	std::set<MAPIOBJECT *, CompareMAPIOBJECT> lstChildren; /* ECSavedObjects */
	std::list<ULONG> lstDeleted; /* proptags client->server only */
	std::list<ULONG> lstAvailable; /* proptags server->client only */
	std::list<ECProperty> lstModified; /* propval client->server only */
	std::list<ECProperty> lstProperties; /* propval client->server but not serialized and server->client  */
	LPSIEID lpInstanceID = nullptr; /* Single Instance ID */
	ULONG cbInstanceID = 0; /* Single Instance ID length */
	BOOL bChangedInstance = false; /* Single Instance ID changed */
	BOOL bChanged = false; /* this is a saved child, otherwise only loaded */
	BOOL bDelete = false; /* delete this object completely */
	ULONG ulUniqueId = 0; /* PR_ROWID (recipients) or PR_ATTACH_NUM (attachments) only */
	ULONG ulObjId = 0; /* hierarchy id of recipients and attachments */
	ULONG ulObjType = 0;
};

typedef std::set<MAPIOBJECT*, MAPIOBJECT::CompareMAPIOBJECT>	ECMapiObjects;

class IECPropStorage : public virtual IUnknown {
public:

	// Get a single (large) property from an object
	virtual HRESULT HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue) = 0;

	// Save a complete object to the server
	virtual HRESULT HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpSavedObjects) = 0;

	// Load a complete object from the server
	virtual HRESULT HrLoadObject(MAPIOBJECT **lppSavedObjects) = 0;

	// Returns the correct storage which can connect to the server
	virtual IECPropStorage* GetServerStorage() = 0;
};

#endif
