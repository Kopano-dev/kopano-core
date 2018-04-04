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

#include <memory>
#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <mapicode.h>

template<typename Type> class ECInvariantChecker _kc_final {
	public:
	ECInvariantChecker(const Type *p) : m_p(p) { m_p->CheckInvariant(); }
	~ECInvariantChecker() { m_p->CheckInvariant(); }
	private:
	const Type *m_p;
};

#ifdef DEBUG
#	define DEBUG_CHECK_INVARIANT do { this->CheckInvariant(); } while (false)
#	define DEBUG_GUARD guard __g(this);
#else
#	define DEBUG_CHECK_INVARIANT do { } while (false)
#	define DEBUG_GUARD
#endif

#define DECL_INVARIANT_GUARD(__class) typedef ECInvariantChecker<__class> guard;
#define DECL_INVARIANT_CHECK void CheckInvariant() const;
#define DEF_INVARIANT_CHECK(__class) void __class::CheckInvariant() const

// C++ class to represent a property in the property list.
class ECProperty _kc_final {
public:
	ECProperty(const ECProperty &Property);
	ECProperty(const SPropValue *);
	~ECProperty();

	HRESULT CopyFrom(const SPropValue *);
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
	HRESULT CopyFromInternal(const SPropValue *);

	unsigned int ulSize, ulPropTag;
	union __UPV Value;
	DWORD dwLastError;
};

// A class representing a property we have in-memory, a list of which is held by ECMAPIProp
// Deleting a property just sets the property as deleted
class ECPropertyEntry _kc_final {
public:
	ECPropertyEntry(ULONG ulPropTag);
	ECPropertyEntry(std::unique_ptr<ECProperty> &&);
	ECPropertyEntry(ECPropertyEntry &&) = default;
	~ECPropertyEntry();

	HRESULT			HrSetProp(ECProperty *property);
	HRESULT HrSetProp(const SPropValue *);
	HRESULT			HrSetClean();
	ECProperty *GetProperty() const { return lpProperty.get(); }
	ULONG			GetPropTag() const { return ulPropTag; }
	BOOL			FIsDirty() const { return fDirty; }
	BOOL			FIsLoaded() const { return lpProperty != NULL; }

	DECL_INVARIANT_CHECK

private:
	DECL_INVARIANT_GUARD(ECPropertyEntry)

	ULONG			ulPropTag;
	std::unique_ptr<ECProperty> lpProperty;
	BOOL fDirty = true;
};

#endif
