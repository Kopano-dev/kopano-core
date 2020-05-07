/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <mapidefs.h>
#include <mapix.h>
#include <string>
#include <vector>
#include "PyMapiPlugin.h"
#include "StatsClient.h"

class PyMapiPlugin;

extern HRESULT HrProcessRules(const std::string &recip, pym_plugin_intf *, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *inbox, IMessage *, KC::StatsClient *);
extern bool dagent_avoid_autoreply(const std::vector<std::string> &headers);
