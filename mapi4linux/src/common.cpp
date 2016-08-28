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
#include "m4l.common.h"
#include <mapicode.h>
#include <mapidefs.h>
#include <mapiguid.h>

M4LUnknown::M4LUnknown() {
    ref = 0;
    pthread_mutex_init(&mutex, NULL);
}

M4LUnknown::~M4LUnknown() {
    pthread_mutex_destroy(&mutex);
}

ULONG M4LUnknown::AddRef() {
    ULONG ret;
    
    pthread_mutex_lock(&mutex);
    ret = ++ref;
    pthread_mutex_unlock(&mutex);
    
    return ret;
}

ULONG M4LUnknown::Release() {
    ULONG nRef;
    
    pthread_mutex_lock(&mutex);
	--this->ref;
    nRef = ref;

    pthread_mutex_unlock(&mutex);

    if(ref == 0)
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
