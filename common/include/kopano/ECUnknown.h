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

#ifndef ECUNKNOWN_H
#define ECUNKNOWN_H

#include <kopano/zcdefs.h>
#include <kopano/IECUnknown.h>

#include <list>
#include <mutex>
#include <mapi.h>

/**
 * Return interface pointer on a specific interface query.
 * @param[in]	_guid	The interface guid.
 * @param[in]	_interface	The class which implements the interface
 * @note guid variable must be named 'refiid', return variable must be named lppInterface.
 */
#define REGISTER_INTERFACE(_guid, _interface)	\
	do { \
		if (refiid == (_guid)) { \
			AddRef(); \
			*lppInterface = reinterpret_cast<void *>(_interface); \
			return hrSuccess; \
		} \
	} while (false)

#define REGISTER_INTERFACE2(cls, interface)	\
	do { \
		if (refiid == (IID_ ## cls)) { \
			AddRef(); \
			*lppInterface = static_cast<cls *>(interface); \
			return hrSuccess; \
		} \
	} while (false)
#define REGISTER_INTERFACE3(guid, cls, interface)	\
	do { \
		if (refiid == (IID_ ## guid)) { \
			AddRef(); \
			*lppInterface = static_cast<cls *>(interface); \
			return hrSuccess; \
		} \
	} while (false)

/**
 * Return interface pointer on a specific interface query without incrementing the refcount.
 * @param[in]	_guid	The interface guid.
 * @param[in]	_interface	The class which implements the interface
 * @note guid variable must be named 'refiid', return variable must be named lppInterface.
 */
#define REGISTER_INTERFACE_NOREF(_guid, _interface) \
	do { \
		if (refiid == (_guid)) { \
			AddRef(); \
			*lppInterface = reinterpret_cast<void *>(_interface); \
			return hrSuccess; \
		} \
	} while (false)

class ECUnknown : public IECUnknown {
public:
	ECUnknown(const char *szClassName = NULL);
	virtual ~ECUnknown();
	virtual ULONG AddRef(void) _kc_override;
	virtual ULONG Release(void) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
	virtual HRESULT AddChild(ECUnknown *lpChild);
	virtual HRESULT RemoveChild(ECUnknown *lpChild);

	class xUnknown _kc_final : public IUnknown {
		#include <kopano/xclsfrag/IUnknown.hpp>
	} m_xUnknown;

	// lpParent is public because it is always thread-safe and valid
	ECUnknown				*lpParent;
	virtual BOOL IsParentOf(const ECUnknown *lpObject);
	virtual BOOL IsChildOf(const ECUnknown *lpObject);

protected:
	// Called by AddChild
	virtual HRESULT SetParent(ECUnknown *lpParent);

	// Kills itself when lstChildren.empty() AND m_cREF == 0
	virtual HRESULT			Suicide();

	ULONG					m_cRef;
	const char *szClassName;
	std::list<ECUnknown *>	lstChildren; 
	std::mutex mutex;
};

#endif // ECUNKNOWN_H
