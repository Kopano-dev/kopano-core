/*
 * Copyright 2017 Kopano and its licensors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <new>
#include <cstdio>
#include <cstring>
#include <mapix.h>
#include <kopano/memory.hpp>
#include <kopano/CommonUtil.h>
#include "dovestore.h"

using namespace KC;

void *kpxx_login()
{
	MAPIInitialize(nullptr);
	object_ptr<IMAPISession> ses;
	auto ret = HrOpenECSession(&~ses, "app_vers", "app_misc",
	           L"foo", L"xfoo", nullptr, 0, 0, 0);
	if (ret != hrSuccess)
		return nullptr;
	return ses.release();
}

void kpxx_logout(void *s)
{
	auto ses = static_cast<IMAPISession *>(s);
	if (ses != nullptr)
		ses->Release();
	MAPIUninitialize();
}
