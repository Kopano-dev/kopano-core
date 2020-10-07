/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include <mapidefs.h>
#include <inetmapi/inetmapi.h>
#include <kopano/ECDefs.h>

namespace KC { class Logger; }
extern HRESULT SendUndeliverable(KC::ECSender *, IMsgStore *, IMessage *);
extern HRESULT ProcessMessageForked(const wchar_t *user, const char *smtp_host, int smtp_port, const char *path, unsigned int eid_size, const ENTRYID *msg_eid, std::shared_ptr<KC::Logger>, bool do_sentmail);
