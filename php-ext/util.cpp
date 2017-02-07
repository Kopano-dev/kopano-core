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

using namespace std;
using namespace KCHL;

#include "util.h"

std::string	last_error = "";

HRESULT mapi_util_createprof(const char *szProfName, const char *szServiceName,
    ULONG cValues, LPSPropValue lpPropVals)
{
	HRESULT			hr = hrSuccess;
	object_ptr<IProfAdmin> lpProfAdmin;
	object_ptr<IMsgServiceAdmin> lpServiceAdmin;
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRows;
	const SPropValue *lpServiceUID = nullptr;
	static constexpr const SizedSPropTagArray(2, sptaMsgServiceCols) =
		{2, {PR_SERVICE_NAME_A, PR_SERVICE_UID}};

	// Get the MAPI Profile administration object
	hr = MAPIAdminProfiles(0, &~lpProfAdmin);
	if(hr != hrSuccess) {
		last_error = "Unable to get IProfAdmin object";
		goto exit;
	}

	lpProfAdmin->DeleteProfile((LPTSTR)szProfName, 0);

	// Create a profile that we can use (empty now)
	hr = lpProfAdmin->CreateProfile((LPTSTR)szProfName, (LPTSTR)"", 0, 0);

	if(hr != hrSuccess) {
		last_error = "Unable to create new profile";
		goto exit;
	}

	// Get the services admin object
	hr = lpProfAdmin->AdminServices((LPTSTR)szProfName, (LPTSTR)"", 0, 0, &~lpServiceAdmin);
	if(hr != hrSuccess) {
		last_error = "Unable to administer new profile";
		goto exit;
	}

	// Create a message service (provider) for the szServiceName (see mapisvc.inf) service
	// (not coupled to any file or server yet)
	hr = lpServiceAdmin->CreateMsgService((LPTSTR)szServiceName, (LPTSTR)"", 0, 0);
	
	if(hr != hrSuccess) {
		last_error = "Service unavailable";
		goto exit;
	}

	// optional, ignore error
	if (strcmp(szServiceName, "ZARAFA6") == 0)
		lpServiceAdmin->CreateMsgService((LPTSTR)"ZCONTACTS", (LPTSTR)"", 0, 0);

	// Strangely we now have to get the SERVICE_UID for the service we just added from
	// the table. (see MSDN help page of CreateMsgService at the bottom of the page)
	hr = lpServiceAdmin->GetMsgServiceTable(0, &~lpTable);
	if(hr != hrSuccess) {
		last_error = "Service table unavailable";
		goto exit;
	}
	hr = lpTable->SetColumns(sptaMsgServiceCols, 0);
	if(hr != hrSuccess) {
		last_error = "Unable to set columns on service table";
		goto exit;
	}

	// Find the correct row
	while(TRUE) {
		hr = lpTable->QueryRows(1, 0, &~lpRows);
		if(hr != hrSuccess || lpRows->cRows != 1) {
			last_error = "Unable to read service table";
			goto exit;
		}

		auto lpServiceName = PCpropFindProp(lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues, PR_SERVICE_NAME_A);
		if(lpServiceName && strcmp(lpServiceName->Value.lpszA, szServiceName) == 0)
			break;
	}

	// Get the PR_SERVICE_UID from the row
	lpServiceUID = PCpropFindProp(lpRows->aRow[0].lpProps, lpRows->aRow[0].cValues, PR_SERVICE_UID);
	if(!lpServiceUID) {
		hr = MAPI_E_NOT_FOUND;
		last_error = "Unable to find service UID";
		goto exit;
	}

	// Configure the message service
	hr = lpServiceAdmin->ConfigureMsgService((MAPIUID *)lpServiceUID->Value.bin.lpb, 0, 0, cValues, lpPropVals);

	if(hr != hrSuccess) {
		last_error = "Unable to setup service for provider";
		goto exit;
	}

exit:
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

	lpProfAdmin->DeleteProfile((LPTSTR)szProfName, 0);
	return hrSuccess;
}

std::string mapi_util_getlasterror()
{
	return last_error;
}
