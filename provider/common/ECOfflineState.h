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

#ifndef ECOFFLINE_STATE
#define ECOFFLINE_STATE

#ifdef HAVE_OFFLINE_SUPPORT
#include <string>

#include <kopano/kcodes.h>
#include "IMAPIOffline.h"

class ECOfflineState {
public:
	enum OFFLINESTATE {
		OFFLINESTATE_ONLINE,
		OFFLINESTATE_OFFLINE
	};

	static HRESULT SetOfflineState(const std::string &strProfname, OFFLINESTATE state);
	static HRESULT GetOfflineState(const std::string &strProfname, OFFLINESTATE *state);

private:
	static HRESULT GetIMAPIOffline(const std::string &strProfname, IMAPIOffline **lppOffline);

	HRESULT Advise();

};
#endif

#endif
