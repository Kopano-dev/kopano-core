/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once

#include "PyMapiPlugin.h"
#include "StatsClient.h"

#include <mapidefs.h>
#include <mapix.h>
#include <string>
#include <vector>

class PyMapiPlugin;

extern HRESULT HrProcessRules(const std::string &recip, pym_plugin_intf *, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *inbox, IMessage *, KC::StatsClient *);
extern bool dagent_header_present(const std::vector<std::string> &headers, const std::vector<std::string> &patterns);
