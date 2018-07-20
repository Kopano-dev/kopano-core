/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef MAILER_H
#define MAILER_H

#include <mapidefs.h>
#include <inetmapi/inetmapi.h>
#include <kopano/ECDefs.h>

extern HRESULT SendUndeliverable(KC::ECSender *, IMsgStore *, IMessage *);
HRESULT ProcessMessageForked(const wchar_t *szUsername, const char *szSMTP, int ulPort, const char *szPath, ULONG cbMsgEntryId, LPENTRYID lpMsgEntryId, bool bDoSentMail);

#endif
