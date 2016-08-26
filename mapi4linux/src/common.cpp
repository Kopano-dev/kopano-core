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

#include <kopano/platform.h>
#include <kopano/lockhelper.hpp>
#include "m4l.common.h"
#include <mapicode.h>
#include <mapidefs.h>
#include <mapiguid.h>

M4LUnknown::M4LUnknown() {
    ref = 0;
}

ULONG M4LUnknown::AddRef() {
	scoped_lock lock(mutex);
	return ++ref;
}

ULONG M4LUnknown::Release() {
	ulock_normal lock(mutex);
	ULONG nRef = --this->ref;
	lock.unlock();
	if (ref == 0)
		delete this;
	return nRef;
}

HRESULT M4LUnknown::QueryInterface(REFIID refiid, void **lpvoid) {
    if(refiid == IID_IUnknown) {
		AddRef();
		*lpvoid = (void *)this;
		return hrSuccess;
    }

    return MAPI_E_INTERFACE_NOT_SUPPORTED;
}
