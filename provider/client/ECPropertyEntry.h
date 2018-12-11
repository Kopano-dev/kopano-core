/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECPROPERTYENTRY_H
#define ECPROPERTYENTRY_H

#include <memory>
#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <mapicode.h>

template<typename Type> class ECInvariantChecker final {
	public:
	ECInvariantChecker(const Type *p) : m_p(p) { m_p->CheckInvariant(); }
	~ECInvariantChecker() { m_p->CheckInvariant(); }
	private:
	const Type *m_p;
};

#ifdef KNOB144
#	define DEBUG_CHECK_INVARIANT do { CheckInvariant(); } while (false)
#	define DEBUG_GUARD guard debug_guard(this);
#else
#	define DEBUG_CHECK_INVARIANT do { } while (false)
#	define DEBUG_GUARD
#endif

#define DECL_INVARIANT_GUARD(cls) typedef ECInvariantChecker<cls> guard;
#define DECL_INVARIANT_CHECK void CheckInvariant() const;
#define DEF_INVARIANT_CHECK(cls) void cls::CheckInvariant() const

// C++ class to represent a property in the property list.
class ECProperty final {
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
class ECPropertyEntry final {
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
