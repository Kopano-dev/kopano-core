/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef __M4L_COMMON_IMPL_H
#define __M4L_COMMON_IMPL_H

#include <kopano/zcdefs.h>
#include <atomic>
#include <mapidefs.h>

class M4LUnknown : public virtual IUnknown {
private:
	std::atomic<unsigned int> ref{0};
    
public:
	virtual ~M4LUnknown(void) = default;
	virtual ULONG AddRef(void) _kc_override;
	virtual ULONG Release(void) _kc_override;
	virtual HRESULT QueryInterface(REFIID refiid, void **lpvoid) _kc_override;
};

#endif
