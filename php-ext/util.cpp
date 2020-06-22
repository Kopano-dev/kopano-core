/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <cmath>
#include <kopano/MAPIErrors.h>
#include <kopano/memory.hpp>
#include <kopano/stringutil.h>
#include <mapi.h>
#include <mapix.h>
#include <mapidefs.h>
#include <mapiutil.h>
#include <string>

using namespace KC;

#include "util.h"

static std::string last_error;

static HRESULT seterror(HRESULT hr, const char *text)
{
	last_error = format("%s: %s (%x)", text, GetMAPIErrorMessage(hr), hr);
	return hr;
}

HRESULT mapi_util_createprof(const char *szProfName, const char *szServiceName,
    ULONG cValues, LPSPropValue lpPropVals)
{
	object_ptr<IProfAdmin> lpProfAdmin;
	object_ptr<IMsgServiceAdmin> lpServiceAdmin1;
	object_ptr<IMsgServiceAdmin2> lpServiceAdmin;
	object_ptr<IMAPITable> lpTable;
	rowset_ptr lpRows;
	MAPIUID service_uid;

	// Get the MAPI Profile administration object
	auto hr = MAPIAdminProfiles(0, &~lpProfAdmin);
	if (hr != hrSuccess)
		return seterror(hr, "Unable to get IProfAdmin object");

	lpProfAdmin->DeleteProfile(reinterpret_cast<const TCHAR *>(szProfName), 0);
	// Create a profile that we can use (empty now)
	hr = lpProfAdmin->CreateProfile(reinterpret_cast<const TCHAR *>(szProfName), reinterpret_cast<const TCHAR *>(""), 0, 0);
	if (hr != hrSuccess)
		return seterror(hr, "Unable to create new profile");

	// Get the services admin object
	hr = lpProfAdmin->AdminServices(reinterpret_cast<const TCHAR *>(szProfName), reinterpret_cast<const TCHAR *>(""), 0, 0, &~lpServiceAdmin1);
	if (hr != hrSuccess)
		return seterror(hr, "Unable to administer new profile");
	hr = lpServiceAdmin1->QueryInterface(IID_IMsgServiceAdmin2, &~lpServiceAdmin);
	if (hr != hrSuccess)
		return seterror(hr, "Unable to QueryInterface IMsgServiceAdmin2");

	// Create a message service (provider) for the szServiceName (see mapisvc.inf) service
	// (not coupled to any file or server yet)
	hr = lpServiceAdmin->CreateMsgServiceEx(szServiceName, "", 0, 0, &service_uid);
	if (hr != hrSuccess)
		return seterror(hr, "Service unavailable");
	// optional, ignore error
	if (strcmp(szServiceName, "ZARAFA6") == 0)
		lpServiceAdmin->CreateMsgServiceEx("ZCONTACTS", "", 0, 0, nullptr);

	// Configure the message service
	hr = lpServiceAdmin->ConfigureMsgService(&service_uid, 0, 0, cValues, lpPropVals);
	if (hr != hrSuccess)
		return seterror(hr, "Unable to setup service for provider");
	return hrSuccess;
}

HRESULT mapi_util_deleteprof(const char *szProfName)
{
	object_ptr<IProfAdmin> lpProfAdmin;
	// Get the MAPI Profile administration object
	auto hr = MAPIAdminProfiles(0, &~lpProfAdmin);
	if (hr != hrSuccess)
		return seterror(hr, "Unable to get IProfAdmin object");
	lpProfAdmin->DeleteProfile(reinterpret_cast<const TCHAR *>(szProfName), 0);
	return hrSuccess;
}

std::string mapi_util_getlasterror()
{
	return last_error;
}
