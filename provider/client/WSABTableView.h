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

/*
	class WSABTableView
*/

#ifndef WSABTABLEVIEW_H
#define WSABTABLEVIEW_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include "WSTableView.h"
#include "ECABLogon.h"

class WSABTableView _kc_final : public WSTableView {
protected:
	WSABTableView(ULONG ulType, ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECABLogon* lpABLogon, WSTransport *lpTransport);

public:
	static HRESULT Create(ULONG ulType, ULONG ulFlags, KCmd *lpCmd, pthread_mutex_t *lpDataLock, ECSESSIONID ecSessionId, ULONG cbEntryId, LPENTRYID lpEntryId, ECABLogon* lpABLogon, WSTransport *lpTransport, WSTableView **lppTableView);
	virtual	HRESULT	QueryInterface(REFIID refiid, void **lppInterface);

};

#endif
