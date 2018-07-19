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
#include <kopano/platform.h>
#include <cmath>
#include <kopano/memory.hpp>
#include <mapi.h>
#include <mapix.h>
#include <mapidefs.h>
#include <mapiutil.h>
#include <string>

using namespace KC;

#include "util.h"

static std::string last_error;

HRESULT mapi_util_createprof(const char *szProfName, const char *szServiceName,
    ULONG cValues, LPSPropValue lpPropVals)
{
	HRESULT			hr = hrSuccess;
	object_ptr<IProfAdmin> lpProfAdmin;
	object_ptr<IMsgServiceAdmin> lpServiceAdmin1;
	object_ptr<IMsgServiceAdmin2> lpServiceAdmin;
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRows;
	MAPIUID service_uid;

	// Get the MAPI Profile administration object
	hr = MAPIAdminProfiles(0, &~lpProfAdmin);
	if(hr != hrSuccess) {
		last_error = "Unable to get IProfAdmin object";
		return hr;
	}

	lpProfAdmin->DeleteProfile(reinterpret_cast<const TCHAR *>(szProfName), 0);
	// Create a profile that we can use (empty now)
	hr = lpProfAdmin->CreateProfile(reinterpret_cast<const TCHAR *>(szProfName), reinterpret_cast<const TCHAR *>(""), 0, 0);

	if(hr != hrSuccess) {
		last_error = "Unable to create new profile";
		return hr;
	}

	// Get the services admin object
	hr = lpProfAdmin->AdminServices(reinterpret_cast<const TCHAR *>(szProfName), reinterpret_cast<const TCHAR *>(""), 0, 0, &~lpServiceAdmin1);
	if(hr != hrSuccess) {
		last_error = "Unable to administer new profile";
		return hr;
	}
	hr = lpServiceAdmin1->QueryInterface(IID_IMsgServiceAdmin2, &~lpServiceAdmin);
	if (hr != hrSuccess) {
		last_error = "Unable to QueryInterface IMsgServiceAdmin2";
		return hr;
	}

	// Create a message service (provider) for the szServiceName (see mapisvc.inf) service
	// (not coupled to any file or server yet)
	hr = lpServiceAdmin->CreateMsgServiceEx(szServiceName, "", 0, 0, &service_uid);
	if(hr != hrSuccess) {
		last_error = "Service unavailable";
		return hr;
	}

	// optional, ignore error
	if (strcmp(szServiceName, "ZARAFA6") == 0)
		lpServiceAdmin->CreateMsgServiceEx("ZCONTACTS", "", 0, 0, nullptr);

	// Configure the message service
	hr = lpServiceAdmin->ConfigureMsgService(&service_uid, 0, 0, cValues, lpPropVals);
	if (hr != hrSuccess)
		last_error = "Unable to setup service for provider";
	return hr;
}

HRESULT mapi_util_deleteprof(const char *szProfName)
{
	object_ptr<IProfAdmin> lpProfAdmin;
	HRESULT hr = hrSuccess;

	// Get the MAPI Profile administration object
	hr = MAPIAdminProfiles(0, &~lpProfAdmin);
	if(hr != hrSuccess) {
		last_error = "Unable to get IProfAdmin object";
		return hr;
	}

	lpProfAdmin->DeleteProfile(reinterpret_cast<const TCHAR *>(szProfName), 0);
	return hrSuccess;
}

std::string mapi_util_getlasterror()
{
	return last_error;
}
