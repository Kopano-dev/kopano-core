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
#include <map>
#include <climits>
#include <getopt.h>
#include <kopano/automapi.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <kopano/memory.hpp>
#include <mapiutil.h>
#include <mapix.h>
#include <kopano/namedprops.h>
#include <kopano/stringutil.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/charset/convert.h>
#include <kopano/ecversion.h>
#include "charset/localeutil.h"
#include <kopano/Util.h>
#include <kopano/ECLogger.h>
#include "fsck.h"

using namespace std;
using namespace KCHL;

string auto_fix;
string auto_del;

/*
 * Some typedefs to make typing easier. ;)
 */
typedef pair<string, Fsck* > CHECKMAP_P;
typedef map<string, Fsck* > CHECKMAP;

enum {
	OPT_HELP = UCHAR_MAX + 1,
	OPT_HOST,
	OPT_PASS,
	OPT_USER,
	OPT_PUBLIC,
	OPT_CALENDAR,
	//OPT_STICKY,
	//OPT_EMAIL,
	OPT_CONTACT,
	OPT_TASK,
	//OPT_JOURNAL,
	OPT_AUTO_FIX,
	OPT_AUTO_DEL,
	OPT_CHECK_ONLY,
	OPT_PROMPT,
	OPT_ACCEPT_DISCLAIMER,
	OPT_ALL
};

static const struct option long_options[] = {
	{ "help",	0, NULL, OPT_HELP },
	{ "host",	1, NULL, OPT_HOST },
	{ "pass",	1, NULL, OPT_PASS },
	{ "user",	1, NULL, OPT_USER },
	{ "public",	0, NULL, OPT_PUBLIC },
	{ "calendar",	0, NULL, OPT_CALENDAR },
	//{ "sticky",	0, NULL, OPT_STICKY }
	//{ "email",	0, NULL, OPT_EMAIL },
	{ "contact",	0, NULL, OPT_CONTACT },
	{ "task",	0, NULL, OPT_TASK },
	//{ "journal",	0, NULL, OPT_JOURNAL },
	{ "all",	0, NULL, OPT_ALL },
	{ "autofix",	1, NULL, OPT_AUTO_FIX },
	{ "autodel",	1, NULL, OPT_AUTO_DEL },
	{ "checkonly",	0, NULL, OPT_CHECK_ONLY },
	{ "prompt",		0, NULL, OPT_PROMPT },
	{ "acceptdisclaimer", 0, NULL, OPT_ACCEPT_DISCLAIMER },
	{}
};

static void print_help(const char *strName)
{
	cout << "Calendar item validator tool" << endl;
	cout << endl;
	cout << "Usage:" << endl;
	cout << strName << " [options] [filters]" << endl;
	cout << endl;
	cout << "Options:" << endl;
	cout << "[-h|--host] <hostname>\tKopano server" << endl;
	cout << "[-u|--user] <username>\tUsername used to login" << endl;
	cout << "[-p|--pass] <password>\tPassword used to login" << endl;
	cout << "[-P|--prompt]\t\tPrompt for password to login" << endl;
	cout << "[--acceptdisclaimer]\tAuto accept disclaimer" << endl;
	cout << "[--public]\t\tCheck the public store for the user" << endl;
        cout << "[--autofix] <yes/no>\tAutoreply to all \"fix property\" messages" << endl;
        cout << "[--autodel] <yes/no>\tAutoreply to all \"delete item\" messages" << endl;
	cout << "[--checkonly]\t\tImplies \"--autofix no\" and \"--autodel no\"" << endl;
	cout << "[--help]\t\tPrint this help screen" << endl;
	cout << endl;
	cout << "Filters:" << endl;
	cout << "[--calendar]\tCheck all Calendar folders" << endl;
	//cout << "[--sticky]\tCheck all Sticky folders" << endl;
	//cout << "[--email]\tCheck all Email folders" << endl;
	cout << "[--contact]\tCheck all Contact folders" << endl;
	cout << "[--task]\tCheck all Task folders" << endl;
	//cout << "[--journal]\tCheck all Journal folders" << endl;
	cout << "[--all]\tCheck all folders" << endl;
}

static void disclaimer(bool acceptDisclaimer)
{
	std::string dummy;

	cout << "*********" << endl;
	cout << " WARNING" << endl;
	cout << "*********" << endl;
	cout << "This tool will repair items and remove invalid items from a mailbox." << endl;
	cout << "It is recommended to use this tool outside of office hours, as it may affect server performance." << endl;
	cout << "Before running this program, ensure a working backup is available." << endl;
	cout << endl;
	cout << "To accept these terms, press <ENTER> to continue or <CTRL-C> to quit." << endl;

	if (!acceptDisclaimer)
		getline(cin,dummy);
}

HRESULT allocNamedIdList(ULONG ulSize, LPMAPINAMEID **lpppNameArray)
{
	HRESULT hr;
	memory_ptr<MAPINAMEID *> lppArray;
	LPMAPINAMEID lpBuffer = NULL;

	hr = MAPIAllocateBuffer(ulSize * sizeof(LPMAPINAMEID), &~lppArray);
	if (hr != hrSuccess)
		return hr;

	hr = MAPIAllocateMore(ulSize * sizeof(MAPINAMEID), lppArray, (void**)&lpBuffer);
	if (hr != hrSuccess) {
		return hr;
	}

	for (ULONG i = 0; i < ulSize; ++i)
		lppArray[i] = &lpBuffer[i];

	*lpppNameArray = lppArray.release();
	return hrSuccess;
}

HRESULT ReadProperties(LPMESSAGE lpMessage, ULONG ulCount, const ULONG *lpTag,
    LPSPropValue *lppPropertyArray)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropTagArray> lpPropertyTagArray;
	ULONG ulPropertyCount = 0;

	hr = MAPIAllocateBuffer(sizeof(SPropTagArray) + (sizeof(ULONG) * ulCount), &~lpPropertyTagArray);
	if (hr != hrSuccess)
		return hr;
	lpPropertyTagArray->cValues = ulCount;
	for (ULONG i = 0; i < ulCount; ++i)
		lpPropertyTagArray->aulPropTag[i] = lpTag[i];

	hr = lpMessage->GetProps(lpPropertyTagArray, 0, &ulPropertyCount, lppPropertyArray);
	if (FAILED(hr))
		cout << "Failed to obtain all properties." << endl;
	return hr;
}

HRESULT ReadNamedProperties(LPMESSAGE lpMessage, ULONG ulCount, LPMAPINAMEID *lppTag,
			    LPSPropTagArray *lppPropertyTagArray, LPSPropValue *lppPropertyArray)
{
	HRESULT hr;
	ULONG ulReadCount = 0;

	hr = lpMessage->GetIDsFromNames(ulCount, lppTag, 0, lppPropertyTagArray);
	if(hr != hrSuccess) {
		cout << "Failed to obtain IDs from names." << endl;
		/*
		 * Override status to make sure FAILED() will catch this,
		 * this is required to make sure the called won't attempt
		 * to access lppPropertyArray.
		 */
		return MAPI_E_CALL_FAILED;
	}

	hr = lpMessage->GetProps(*lppPropertyTagArray, 0, &ulReadCount, lppPropertyArray);
	if (FAILED(hr)) {
		cout << "Failed to obtain all properties." << endl;
		return hr;
	}
	return hrSuccess;
}

static HRESULT DetectFolderDetails(LPMAPIFOLDER lpFolder, string *lpName,
    string *lpClass, ULONG *lpFolderType)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpPropertyArray;
	ULONG ulPropertyCount = 0;

	SizedSPropTagArray(3, PropertyTagArray) = {
		3,
		{
			PR_DISPLAY_NAME_A,
			PR_CONTAINER_CLASS_A,
			PR_FOLDER_TYPE,
		}
	};

	hr = lpFolder->GetProps(PropertyTagArray, 0, &ulPropertyCount,
	     &~lpPropertyArray);
	if (FAILED(hr)) {
		cout << "Failed to obtain all properties." << endl;
		return hr;
	}

	*lpFolderType = 0;

	for (ULONG i = 0; i < ulPropertyCount; ++i) {
		if (PROP_TYPE(lpPropertyArray[i].ulPropTag) == PT_ERROR)
			hr = MAPI_E_INVALID_OBJECT;
		else if (lpPropertyArray[i].ulPropTag == PR_DISPLAY_NAME_A)
			*lpName = lpPropertyArray[i].Value.lpszA;
		else if (lpPropertyArray[i].ulPropTag == PR_CONTAINER_CLASS_A)
			*lpClass = lpPropertyArray[i].Value.lpszA;
		else if (lpPropertyArray[i].ulPropTag == PR_FOLDER_TYPE)
			*lpFolderType = lpPropertyArray[i].Value.ul;
	}

	/*
	 * As long as we found what we were looking for, we should be satisfied.
	 */
	if (!lpName->empty() && !lpClass->empty())
		hr = hrSuccess;
	return hr;
}

static HRESULT
RunFolderValidation(const std::set<std::string> &setFolderIgnore,
    LPMAPIFOLDER lpRootFolder, LPSRow lpRow, CHECKMAP checkmap)
{
	HRESULT hr = hrSuccess;
	LPSPropValue lpItemProperty = NULL;
	object_ptr<IMAPIFolder> lpFolder;
	Fsck *lpFsck = NULL;
	ULONG ulObjectType = 0;
	string strName;
	string strClass;
	ULONG ulFolderType = 0;

	lpItemProperty = PpropFindProp(lpRow->lpProps, lpRow->cValues, PR_ENTRYID);
	if (!lpItemProperty) {
		cout << "Row does not contain an EntryID." << endl;
		goto exit;
	}
	hr = lpRootFolder->OpenEntry(lpItemProperty->Value.bin.cb,
	     reinterpret_cast<ENTRYID *>(lpItemProperty->Value.bin.lpb),
	     &IID_IMAPIFolder, 0, &ulObjectType, &~lpFolder);
	if (hr != hrSuccess) {
		cout << "Failed to open EntryID." << endl;
		goto exit;
	}

	/*
	 * Detect folder name and class.
	 */
	hr = DetectFolderDetails(lpFolder, &strName, &strClass, &ulFolderType);
	if (hr != hrSuccess) {
		if (!strName.empty()) {
			cout << "Unknown class, skipping entry";
			cout << " \"" << strName << "\"" << endl;
		} else
			cout << "Failed to detect folder details." << endl;
		goto exit;
	}

	if (setFolderIgnore.find(string((const char*)lpItemProperty->Value.bin.lpb, lpItemProperty->Value.bin.cb)) != setFolderIgnore.end()) {
		cout << "Ignoring folder: ";
		cout << "\"" << strName << "\" (" << strClass << ")" << endl;
		goto exit;
	}

	if (ulFolderType != FOLDER_GENERIC) {
		cout << "Ignoring search folder: ";
		cout << "\"" << strName << "\" (" << strClass << ")" << endl;
		goto exit;
	}

	for (const auto &i : checkmap)
		if (i.first == strClass) {
			lpFsck = i.second;
			break;
		}

	if (lpFsck)
		lpFsck->ValidateFolder(lpFolder, strName);
	else {
		cout << "Ignoring folder: ";
		cout << "\"" << strName << "\" (" << strClass << ")" << endl;
	}

exit:
	return hr;
}

static HRESULT RunStoreValidation(const char *strHost, const char *strUser,
    const char *strPass, const char *strAltUser, bool bPublic,
    CHECKMAP checkmap)
{
	HRESULT hr = hrSuccess;
	AutoMAPI mapiinit;
	object_ptr<IMAPISession> lpSession;
	object_ptr<IMsgStore> lpStore, lpAltStore;
	LPMDB lpReadStore = NULL;
	object_ptr<IMAPIFolder> lpRootFolder;
	object_ptr<IMAPITable> lpHierarchyTable;
	LPSRowSet lpRows = NULL;
	ULONG ulObjectType;
	ULONG ulCount;
	object_ptr<IExchangeManageStore> lpIEMS;
    // user
    ULONG			cbUserStoreEntryID = 0;
	memory_ptr<ENTRYID> lpUserStoreEntryID, lpEntryIDSrc;
	wstring strwUsername;
	wstring strwAltUsername;
	wstring strwPassword;
	std::set<std::string> setFolderIgnore;
	memory_ptr<SPropValue> lpAddRenProp;
	ULONG cbEntryIDSrc = 0;

	hr = mapiinit.Initialize();
	if (hr != hrSuccess) {
		cout << "Unable to initialize session" << endl;
		goto exit;
	}

	// input from commandline is current locale
	if (strUser)
		strwUsername = convert_to<wstring>(strUser);
	if (strPass)
		strwPassword = convert_to<wstring>(strPass);
	if (strAltUser)
		strwAltUsername = convert_to<wstring>(strAltUser);

	hr = HrOpenECSession(&~lpSession, "kopano-fsck", PROJECT_SVN_REV_STR,
	     strwUsername.c_str(), strwPassword.c_str(), strHost, 0, NULL, NULL);
	if(hr != hrSuccess) {
		cout << "Wrong username or password." << endl;
		goto exit;
	}
	
	if (bPublic) {
		hr = HrOpenECPublicStore(lpSession, &~lpStore);
		if (hr != hrSuccess) {
			cout << "Failed to open public store." << endl;
			goto exit;
		}
	} else {
		hr = HrOpenDefaultStore(lpSession, &~lpStore);
		if (hr != hrSuccess) {
			cout << "Failed to open default store." << endl;
			goto exit;
		}
	}

	if (!strwAltUsername.empty()) {
		hr = lpStore->QueryInterface(IID_IExchangeManageStore, &~lpIEMS);
        if (hr != hrSuccess) {
            cout << "Cannot open ExchangeManageStore object" << endl;
            goto exit;
        }

        hr = lpIEMS->CreateStoreEntryID(const_cast<wchar_t *>(L""),
             (LPTSTR)strwAltUsername.c_str(),
             MAPI_UNICODE | OPENSTORE_HOME_LOGON, &cbUserStoreEntryID,
	     &~lpUserStoreEntryID);
        if (hr != hrSuccess) {
            cout << "Cannot get user store id for user" << endl;
            goto exit;
        }
        hr = lpSession->OpenMsgStore(0, cbUserStoreEntryID, lpUserStoreEntryID, nullptr, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, &~lpAltStore);
        if (hr != hrSuccess) {
            cout << "Cannot open user store of user" << endl;
            goto exit;
        }
        
        lpReadStore = lpAltStore;
	} else {
	    lpReadStore = lpStore;
    }

	hr = lpReadStore->OpenEntry(0, nullptr, &IID_IMAPIFolder, 0, &ulObjectType, &~lpRootFolder);
	if(hr != hrSuccess) {
		cout << "Failed to open root folder." << endl;
		goto exit;
	}

	if (HrGetOneProp(lpRootFolder, PR_IPM_OL2007_ENTRYIDS /*PR_ADDITIONAL_REN_ENTRYIDS_EX*/, &~lpAddRenProp) == hrSuccess &&
	    Util::ExtractSuggestedContactsEntryID(lpAddRenProp, &cbEntryIDSrc, &~lpEntryIDSrc) == hrSuccess)
		setFolderIgnore.insert(string(reinterpret_cast<const char *>(lpEntryIDSrc.get()), cbEntryIDSrc));

	hr = lpRootFolder->GetHierarchyTable(CONVENIENT_DEPTH, &~lpHierarchyTable);
	if (hr != hrSuccess) {
		cout << "Failed to open hierarchy." << endl;
		goto exit;
	}

	/*
	 * Check if we have found at least *something*.
	 */
	hr = lpHierarchyTable->GetRowCount(0, &ulCount);
	if(hr != hrSuccess) {
		cout << "Failed to count number of rows." << endl;
		goto exit;
	} else if (!ulCount) {
		cout << "No entries inside Calendar." << endl;
		goto exit;
	}

	/*
	 * Loop through each row/entry and validate.
	 */
	while (true) {
		hr = lpHierarchyTable->QueryRows(20, 0, &lpRows);
		if (hr != hrSuccess)
			break;

		if (lpRows->cRows == 0)
			break;

		for (ULONG i = 0; i < lpRows->cRows; ++i)
			RunFolderValidation(setFolderIgnore, lpRootFolder, &lpRows->aRow[i], checkmap);
		FreeProws(lpRows);
		lpRows = NULL;
	}

exit:
	if (lpRows) {
		FreeProws(lpRows);
		lpRows = NULL;
	}
	return hr;
}

int main(int argc, char *argv[])
{
	HRESULT hr = hrSuccess;
	CHECKMAP checkmap;
	const char *strHost = NULL;
	char* strUser = NULL;
	const char *strPass = "";
	char* strAltUser = NULL;
	int c;
	bool bAll = false;
	bool bPrompt = false;
	bool bPublic = false;
	bool acceptDisclaimer = false;

	setlocale(LC_MESSAGES, "");
	if (!forceUTF8Locale(true))
		return -1;

	strHost = GetServerUnixSocket();

	/*
	 * Read arguments.
	 */
	while (true) {
		c = getopt_long(argc, argv, "u:p:h:a:P", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'a':
		    strAltUser = optarg;
		    break;
		case OPT_USER:
		case 'u':
			strUser = optarg;
			break;
		case OPT_PASS:
		case 'p':
			strPass = optarg;
			break;
        case OPT_PROMPT:
        case 'P':
            bPrompt = true;
            break;
		case OPT_PUBLIC:
			bPublic = true;
			break;
		case OPT_HOST:
		case 'h':
			strHost = optarg;
			break;
		case OPT_HELP:
			print_help(argv[0]);
			return 0;
		case OPT_CALENDAR:
			checkmap.insert(CHECKMAP_P("IPF.Appointment", new FsckCalendar()));
			break;
		//case OPT_STICKY:
		//	checkmap.insert(CHECKMAP_P("IPF.StickyNote", new FsckStickyNote()));
		//	break;
		//case OPT_EMAIL:
		//	checkmap.insert(CHECKMAP_P("IPF.Note", new FsckNote()));
		//	break;
		case OPT_CONTACT:
			checkmap.insert(CHECKMAP_P("IPF.Contact", new FsckContact()));
			break;
		case OPT_TASK:
			checkmap.insert(CHECKMAP_P("IPF.Task", new FsckTask()));
			break;
		//case OPT_JOURNAL:
		//	checkmap.insert(CHECKMAP_P("IPF.Journal", new FsckJournal()));
		//	break;
		case OPT_ALL:
			bAll = true;
			break;
		case OPT_AUTO_FIX:
			auto_fix = optarg;
			break;
		case OPT_AUTO_DEL:
			auto_del = optarg;
			break;
		case OPT_CHECK_ONLY:
			auto_fix = "no";
			auto_del = "no";
			break;
		case OPT_ACCEPT_DISCLAIMER:
			acceptDisclaimer = true;
			break;
		default:
			cout << "Invalid argument" << endl;
			print_help(argv[0]);
			return 1;
		};
	}

	disclaimer(acceptDisclaimer);

	/*
	 * Validate arguments.
	 */
	if (!strHost || !strUser) {
		cout << "Arguments missing: " << (!strHost ? "Host" : "User") << endl;
		print_help(argv[0]);
		return 1;
	}
	
	if (bPrompt) {
		strPass = get_password("Enter password:");
		if(!strPass) {
			cout << "Invalid password." << endl;
			return 1;
		}
	}

	if (checkmap.empty()) {
		if (!bAll)
			cout << "Filter arguments missing, defaulting to --all" << endl;
		checkmap.insert(CHECKMAP_P("IPF.Appointment", new FsckCalendar()));
		//checkmap.insert(CHECKMAP_P("IPF.StickyNote", new FsckStickyNote()));
		//checkmap.insert(CHECKMAP_P("IPF.Note", new FsckNote()));
		checkmap.insert(CHECKMAP_P("IPF.Contact", new FsckContact()));
		checkmap.insert(CHECKMAP_P("IPF.Task", new FsckTask()));
		//checkmap.insert(CHECKMAP_P("IPF.Journal", new FsckJournal()));
	}

	hr = RunStoreValidation(strHost, strUser, strPass, strAltUser, bPublic, checkmap);

	/*
	 * Cleanup
	 */
	if (hr == hrSuccess)
		cout << endl << "Statistics:" << endl;

	for (auto i = checkmap.begin(); i != checkmap.end();
	     i = checkmap.begin()) {
		Fsck *lpFsck = i->second;
		
		if (hr == hrSuccess)
			lpFsck->PrintStatistics(i->first);
		
		checkmap.erase(i);
		delete lpFsck;
	}

	return (hr == hrSuccess);
}
