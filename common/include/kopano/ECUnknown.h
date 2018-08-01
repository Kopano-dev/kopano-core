/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECUNKNOWN_H
#define ECUNKNOWN_H

#include <kopano/zcdefs.h>
#include <atomic>
#include <list>
#include <mutex>
#include <mapi.h>
#include <mapidefs.h>

namespace KC {

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

class _kc_export ECUnknown : public virtual IUnknown {
public:
	ECUnknown(const char *szClassName = NULL);
	virtual ~ECUnknown(void);
	virtual ULONG AddRef(void) _kc_override;
	virtual ULONG Release(void) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **iface) _kc_override;
	virtual HRESULT AddChild(ECUnknown *lpChild);
	virtual HRESULT RemoveChild(ECUnknown *lpChild);

	// lpParent is public because it is always thread-safe and valid
	ECUnknown *lpParent = nullptr;
	virtual BOOL IsParentOf(const ECUnknown *) const;
	virtual BOOL IsChildOf(const ECUnknown *) const;

protected:
	// Called by AddChild
	virtual HRESULT SetParent(ECUnknown *lpParent);

	// Kills itself when lstChildren.empty() AND m_cREF == 0
	virtual HRESULT			Suicide();

	std::atomic<unsigned int> m_cRef{0};
	const char *szClassName;
	std::list<ECUnknown *>	lstChildren;
	std::mutex mutex;
};

} /* namespace */

#endif // ECUNKNOWN_H
