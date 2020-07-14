/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
// Interface for writing and reading properties to disk (which does the actual transfer and save)
//
// Strategy is to load most of the small (i.e. not much data) properties at load-time. This saves
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
#pragma once
#include <mapi.h>
#include <mapispi.h>
#include <list>
#include <set>
#include <tuple>
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
	MAPIOBJECT(const MAPIOBJECT &);
	~MAPIOBJECT();
	void operator=(const MAPIOBJECT &) = delete;

	bool operator<(const MAPIOBJECT &other) const noexcept {
		return std::tie(ulObjType, ulUniqueId) <
		       std::tie(other.ulObjType, other.ulUniqueId);
	};

	struct CompareMAPIOBJECT {
		bool operator()(const MAPIOBJECT *a, const MAPIOBJECT *b) const noexcept
		{
			return *a < *b;
		}
	};

	/* data */
	std::set<MAPIOBJECT *, CompareMAPIOBJECT> lstChildren; /* ECSavedObjects */
	std::list<ULONG> lstDeleted; /* proptags client->server only */
	std::list<ULONG> lstAvailable; /* proptags server->client only */
	std::list<ECProperty> lstModified; /* propval client->server only */
	std::list<ECProperty> lstProperties; /* propval client->server but not serialized and server->client  */
	SIEID *lpInstanceID = nullptr; /* Single Instance ID */
	ULONG cbInstanceID = 0; /* Single Instance ID length */
	BOOL bChangedInstance = false; /* Single Instance ID changed */
	BOOL bChanged = false; /* this is a saved child, otherwise only loaded */
	BOOL bDelete = false; /* delete this object completely */
	ULONG ulUniqueId = 0; /* PR_ROWID (recipients) or PR_ATTACH_NUM (attachments) only */
	ULONG ulObjId = 0; /* hierarchy id of recipients and attachments */
	ULONG ulObjType = 0;
};

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
