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

#ifndef ECPROPERTYENTRY_H
#define ECPROPERTYENTRY_H

#include <mapidefs.h>
#include <mapicode.h>

#include "ECInvariantChecker.h"

// C++ class to represent a property in the property list.
class ECProperty {
public:
	ECProperty(const ECProperty &Property);
	ECProperty(LPSPropValue lpsProp);
	~ECProperty();

	HRESULT CopyFrom(LPSPropValue lpsProp);
	HRESULT CopyTo(LPSPropValue lpsProp, void *lpBase, ULONG ulPropTag);
	HRESULT CopyToByRef(LPSPropValue lpsProp) const;
	
	bool operator==(const ECProperty &property) const;
	SPropValue GetMAPIPropValRef(void) const;

	ULONG GetSize() const { return ulSize; }
	ULONG GetPropTag() const { return ulPropTag; }
	DWORD GetLastError() const { return dwLastError; }

	DECL_INVARIANT_CHECK

private:
	DECL_INVARIANT_GUARD(ECProperty)
	HRESULT CopyFromInternal(LPSPropValue lpsProp);

private:
	ULONG ulSize;
	ULONG ulPropTag;
	union _PV Value;

	DWORD dwLastError;
};

// A class representing a property we have in-memory, a list of which is held by ECMAPIProp
// Deleting a property just sets the property as deleted
class ECPropertyEntry {
public:
	ECPropertyEntry(ULONG ulPropTag);
	ECPropertyEntry(ECProperty *property);
	~ECPropertyEntry();

	HRESULT			HrSetProp(ECProperty *property);
	HRESULT			HrSetProp(LPSPropValue lpsPropValue);
	HRESULT			HrSetClean();

	ECProperty *	GetProperty() { return lpProperty; }
	ULONG			GetPropTag() const { return ulPropTag; }
	void			DeleteProperty();
	BOOL			FIsDirty() const { return fDirty; }
	BOOL			FIsLoaded() const { return lpProperty != NULL; }

	DECL_INVARIANT_CHECK

private:
	DECL_INVARIANT_GUARD(ECPropertyEntry)

	ECProperty		*lpProperty;
	ULONG			ulPropTag;
	BOOL			fDirty;
};

#endif
