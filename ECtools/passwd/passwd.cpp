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

#include <iostream>
#include <memory>
#include <kopano/charset/convert.h>
#include <climits>
#include <cmath>
#include <getopt.h>
#include <kopano/memory.hpp>
#include <mapidefs.h>
#include <mapispi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/ECTags.h>
#include <kopano/ECGuid.h>
#include <kopano/CommonUtil.h>
#include <kopano/ecversion.h>
#include <kopano/stringutil.h>
#include <kopano/MAPIErrors.h>
#include <kopano/ECLogger.h>

using namespace std;
using namespace KCHL;

static bool verbose = false;

enum modes {
	MODE_INVALID = 0, MODE_CHANGE_PASSWD, MODE_HELP
};

enum {
	OPT_HELP = UCHAR_MAX + 1, // high to avoid clashes with modes
	OPT_HOST
};

static const struct option long_options[] = {
		{ "help", 0, NULL, OPT_HELP },
		{ "host", 1, NULL, OPT_HOST }
};

static void print_help(const char *name)
{
	cout << "Usage:" << endl;
	cout << name << " [action] [options]" << endl << endl;
	cout << "Actions: [-u] " << endl;
	cout << "\t" << " -u user" << "\t" << "update user password, -p or -P" << endl;
	cout << endl;
	cout << "Options: [-u username] [-p password] [-o oldpassword] [-h path]" << endl;
	cout << "\t" << " -o oldpass" << "\t\t" << "old password to login" << endl;
	cout << "\t" << " -p pass" << "\t\t" << "set password to pass" << endl;
	cout << endl;
	cout << "Global options: [-h|--host path]" << endl;
	cout << "\t" << " -h path" << "\t\t" << "connect through <path>, e.g. file:///var/run/socket" << endl;
	cout << "\t" << " -v\t\tenable verbosity" << endl;
	cout << "\t" << " -V Print version info." << endl;
	cout << "\t" << " --help" << "\t\t" << "show this help text." << endl;
	cout << endl;
}

static HRESULT UpdatePassword(const char *lpPath, const char *lpUsername,
    const char *lpPassword, const char *lpNewPassword)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMAPISession> lpSession;
	object_ptr<IUnknown> lpECMsgStore;
	object_ptr<IMsgStore> lpMsgStore;
	object_ptr<IECServiceAdmin> lpServiceAdmin;
	ULONG cbUserId = 0;
	memory_ptr<ENTRYID> lpUserId;
	memory_ptr<SPropValue> lpPropValue;
	memory_ptr<ECUSER> lpECUser;
	convert_context converter;

	std::wstring strwUsername, strwPassword;

	strwUsername = converter.convert_to<wstring>(lpUsername);
	strwPassword = converter.convert_to<wstring>(lpPassword);

	ECLogger *lpLogger = NULL;
	if (verbose)
		lpLogger = new ECLogger_File(EC_LOGLEVEL_FATAL, 0, "-", false);
	else
		lpLogger = new ECLogger_Null();
	ec_log_set(lpLogger);
	hr = HrOpenECSession(&~lpSession, "passwd", PROJECT_VERSION,
	     strwUsername.c_str(), strwPassword.c_str(), lpPath,
	     EC_PROFILE_FLAGS_NO_NOTIFICATIONS | EC_PROFILE_FLAGS_NO_PUBLIC_STORE,
	     NULL, NULL);
	lpLogger->Release();
	if(hr != hrSuccess) {
		cerr << "Wrong username or password." << endl;
		return hr;
	}
	hr = HrOpenDefaultStore(lpSession, &~lpMsgStore);
	if(hr != hrSuccess) {
		cerr << "Unable to open store." << endl;
		return hr;
	}
	hr = HrGetOneProp(lpMsgStore, PR_EC_OBJECT, &~lpPropValue);
	if(hr != hrSuccess || !lpPropValue)
		return hr;
	lpECMsgStore.reset(reinterpret_cast<IUnknown *>(lpPropValue->Value.lpszA), false);
	if(!lpECMsgStore)
		return hr;
	lpECMsgStore->AddRef();
	hr = lpECMsgStore->QueryInterface(IID_IECServiceAdmin, &~lpServiceAdmin);
	if(hr != hrSuccess)
		return hr;
	hr = lpServiceAdmin->ResolveUserName((LPTSTR)lpUsername, 0, &cbUserId, &~lpUserId);
	if (hr != hrSuccess) {
		cerr << "Unable to update password, user not found." << endl;
		return hr;
	}

	// get old features. we need these, because not setting them would mean: remove them
	hr = lpServiceAdmin->GetUser(cbUserId, lpUserId, 0, &~lpECUser);
	if (hr != hrSuccess) {
		cerr << "Unable to get user details, " << getMapiCodeString(hr, lpUsername) << endl;
		return hr;
	}

	lpECUser->lpszPassword = (LPTSTR)lpNewPassword;

	hr = lpServiceAdmin->SetUser(lpECUser, 0);
	if(hr != hrSuccess) {
		cerr << "Unable to update user password." << endl;
		return hr;
	}
	return hrSuccess;
}

int main(int argc, char* argv[])
{
	HRESULT hr = hrSuccess;
	const char *username = NULL;
	const char *newpassword = NULL;
	std::string szOldPassword, szNewPassword;
	const char *oldpassword = NULL;
	const char *path = NULL;
	modes	mode = MODE_INVALID;
	int		passprompt = 1;

	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");

	if(argc < 2) {
		print_help(argv[0]);
		return 1;
	}

	int c;
	while (1) {
		c = getopt_long(argc, argv, "u:Pp:h:o:Vv", long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case 'u':
			mode = MODE_CHANGE_PASSWD;
			username = optarg;
			break;
		case 'p':
			newpassword = optarg;
			passprompt = 0;
			break;
		case 'o':
			oldpassword = optarg;
			passprompt = 0;
			break;
			// error handling?
		case '?':
			break;
		case OPT_HOST:
		case 'h':
			path = optarg;
			break;
		case 'V':
			cout << "kopano-passwd " PROJECT_VERSION << endl;
			return 1;			
		case 'v':
			verbose = true;
			break;
		case OPT_HELP:
			mode = MODE_HELP;
			break;
		default:
			break;
		};
	}

	// check parameters
	if (optind < argc) {
		cerr << "Too many options given." << endl;
		return 1;
	}

	if (mode == MODE_INVALID) {
		cerr << "No correct command given." << endl;
		return 1;
	}

	if (mode == MODE_HELP) {
		print_help(argv[0]);
		return 0;
	}

	if (mode == MODE_CHANGE_PASSWD && ((newpassword == NULL && passprompt == 0) ||
		username == NULL || (oldpassword == NULL && passprompt == 0)) ) {
		cerr << "Missing information to update user password." << endl;
		return 1;
	}

	//Init mapi
	hr = MAPIInitialize(NULL);
	if (hr != hrSuccess) {
		cerr << "Unable to initialize" << endl;
		goto exit;
	}

	
	// fully logged on, action!

	switch(mode) {
	case MODE_CHANGE_PASSWD:
		
		if(passprompt)
		{
			std::unique_ptr<char[], cstdlib_deleter> tmp(get_password("Enter old password:"));
			if (tmp == nullptr) {
				cerr << "Wrong old password" << endl;
				goto exit;
			}
			
			cout << endl;
			szOldPassword = tmp.get();
			oldpassword = szOldPassword.c_str();
			tmp.reset(get_password("Enter new password:"));
			if (tmp == nullptr) {
				cerr << "Wrong new password" << endl;
				goto exit;
			}

			cout << endl;
			szNewPassword = tmp.get();
			newpassword = szNewPassword.c_str();
			tmp.reset(get_password("Re-Enter password:"));
			if (tmp == nullptr) {
				cerr << "Wrong new password" << endl;
				goto exit;
			}
			if (szNewPassword != tmp.get())
				cerr << "Passwords don't match" << endl;
			cout << endl;
		}

		hr = UpdatePassword(path, username, oldpassword, newpassword);
		if (hr != hrSuccess)
			goto exit;		

	case MODE_INVALID:
	case MODE_HELP:
		// happy compiler
		break;
	};

exit:
	MAPIUninitialize();
	if (hr == hrSuccess)
		return 0;
	else
		return 1;
}

