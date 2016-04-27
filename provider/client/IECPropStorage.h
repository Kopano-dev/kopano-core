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

#include <kopano/IECUnknown.h>
#include <mapi.h>
#include <mapispi.h>
#include <list>
#include <set>
#include "ECPropertyEntry.h"
#include "kcore.hpp"
#include <kopano/Util.h>

typedef struct MAPIOBJECT {
	MAPIOBJECT() {
		/* see AllocNewMapiObject :: Mem.cpp */
	};
	
	MAPIOBJECT(unsigned int ulType, unsigned int ulId) : ulUniqueId(ulId), ulObjType(ulType) {
	}

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

	/* copy constructor */
	MAPIOBJECT(const MAPIOBJECT *lpSource)
	{
		this->bChanged = lpSource->bChanged;
		this->bChangedInstance = lpSource->bChangedInstance;
		this->bDelete = lpSource->bDelete;
		this->ulUniqueId = lpSource->ulUniqueId;
		this->ulObjId = lpSource->ulObjId;
		this->ulObjType = lpSource->ulObjType;

		Util::HrCopyEntryId(lpSource->cbInstanceID, (LPENTRYID)lpSource->lpInstanceID,
							&this->cbInstanceID, (LPENTRYID *)&this->lpInstanceID);

		this->lstChildren = new std::set<MAPIOBJECT*, CompareMAPIOBJECT>;
		this->lstDeleted = new std::list<ULONG>;
		this->lstAvailable = new std::list<ULONG>;
		this->lstModified = new std::list<ECProperty>;
		this->lstProperties = new std::list<ECProperty>;

		*this->lstDeleted = *lpSource->lstDeleted;
		*this->lstModified = *lpSource->lstModified;
		*this->lstProperties = *lpSource->lstProperties;
		*this->lstAvailable = *lpSource->lstAvailable;

		for (std::set<MAPIOBJECT *, CompareMAPIOBJECT>::const_iterator i = lpSource->lstChildren->begin();
		     i != lpSource->lstChildren->end(); ++i)
			this->lstChildren->insert(new MAPIOBJECT(*i));
	};

	/* data */
	std::set<MAPIOBJECT*, CompareMAPIOBJECT>	*lstChildren;	/* ECSavedObjects */
	std::list<ULONG>		*lstDeleted;	/* proptags client->server only */
	std::list<ULONG>		*lstAvailable;	/* proptags server->client only */
	std::list<ECProperty>	*lstModified;	/* propval client->server only */
	std::list<ECProperty>	*lstProperties;	/* propval client->server but not serialized and server->client  */
	LPSIEID					lpInstanceID;	/* Single Instance ID */
	ULONG					cbInstanceID;	/* Single Instance ID length */
	BOOL					bChangedInstance; /* Single Instance ID changed */
	BOOL					bChanged;		/* this is a saved child, otherwise only loaded */
	BOOL					bDelete;		/* delete this object completely */
	ULONG					ulUniqueId;		/* PR_ROWID (recipients) or PR_ATTACH_NUM (attachments) only */
	ULONG					ulObjId;		/* hierarchy id of recipients and attachments */
	ULONG					ulObjType;
} MAPIOBJECT;

typedef std::set<MAPIOBJECT*, MAPIOBJECT::CompareMAPIOBJECT>	ECMapiObjects;


class IECPropStorage : public IECUnknown
{
public:
	// Get a list of the properties
	virtual HRESULT HrReadProps(LPSPropTagArray *lppPropTags,ULONG *cValues, LPSPropValue *ppValues) = 0;

	// Get a single (large) property from an object
	virtual HRESULT HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue) = 0;

	// Write all properties to disk (overwrites a property if it already exists)
	virtual	HRESULT	HrWriteProps(ULONG cValues, LPSPropValue pValues, ULONG ulFlags = 0) = 0;

	// Delete properties from file
	virtual HRESULT HrDeleteProps(LPSPropTagArray lpsPropTagArray) = 0;

	// Save a complete object to the server
	virtual HRESULT HrSaveObject(ULONG ulFlags, MAPIOBJECT *lpSavedObjects) = 0;

	// Load a complete object from the server
	virtual HRESULT HrLoadObject(MAPIOBJECT **lppSavedObjects) = 0;

	// Returns the correct storage which can connect to the server
	virtual IECPropStorage* GetServerStorage() = 0;
};

#endif
