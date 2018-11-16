/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECMONITORDEFS_H
#define ECMONITORDEFS_H

#include <memory>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include <kopano/memory.hpp>

struct ECTHREADMONITOR {
	std::unique_ptr<KC::ECConfig> lpConfig;
	bool bShutdown = false;
};

#endif
