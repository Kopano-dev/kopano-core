/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/ECUnknown.h>
#include <kopano/platform.h>
#include "m4l.common.h"
#include <mapicode.h>
#include <mapidefs.h>
#include <mapiguid.h>

ULONG M4LUnknown::AddRef() {
	return ++ref;
}

ULONG M4LUnknown::Release() {
	ULONG nRef = --ref;
	if (nRef == 0)
		delete this;
	return nRef;
}

HRESULT M4LUnknown::QueryInterface(const IID &refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}
