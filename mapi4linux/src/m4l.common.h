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
