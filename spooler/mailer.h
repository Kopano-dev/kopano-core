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

#ifndef MAILER_H
#define MAILER_H

#include <mapidefs.h>
#include <inetmapi/inetmapi.h>
#include <kopano/ECDefs.h>

extern HRESULT SendUndeliverable(KC::ECSender *, IMsgStore *, IMessage *);
HRESULT ProcessMessageForked(const wchar_t *szUsername, const char *szSMTP, int ulPort, const char *szPath, ULONG cbMsgEntryId, LPENTRYID lpMsgEntryId, bool bDoSentMail);

#endif
