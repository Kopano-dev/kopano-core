/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <mapispi.h>
#include <mapicode.h>
#include "Mem.h"
#include "IECPropStorage.h"

MAPIOBJECT::~MAPIOBJECT()
{
	for (auto &obj : lstChildren)
		delete obj;
	if (lpInstanceID != nullptr)
		ECFreeBuffer(lpInstanceID);
}
