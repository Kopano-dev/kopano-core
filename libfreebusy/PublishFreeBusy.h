/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <mapidefs.h>
#include <ctime>

struct IMAPISession;
struct IMsgStore;

namespace KC {

extern KC_EXPORT HRESULT HrPublishDefaultCalendar(IMAPISession *, IMsgStore *, time_t start, unsigned int months);

} /* namespace */
