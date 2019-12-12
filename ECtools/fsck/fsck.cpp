/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
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

using namespace KC;
using std::cin;
using std::cout;
using std::endl;
using std::string;

string auto_fix;
string auto_del;
typedef std::map<std::string, std::unique_ptr<Fsck>> CHECKMAP;

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
	memory_ptr<MAPINAMEID *> lppArray;
	LPMAPINAMEID lpBuffer = NULL;

	auto hr = MAPIAllocateBuffer(ulSize * sizeof(LPMAPINAMEID), &~lppArray);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateMore(ulSize * sizeof(MAPINAMEID), lppArray, reinterpret_cast<void **>(&lpBuffer));
	if (hr != hrSuccess)
		return hr;
	for (ULONG i = 0; i < ulSize; ++i)
		lppArray[i] = &lpBuffer[i];
	*lpppNameArray = lppArray.release();
	return hrSuccess;
}

HRESULT ReadProperties(LPMESSAGE lpMessage, ULONG ulCount, const ULONG *lpTag,
    LPSPropValue *lppPropertyArray)
{
	memory_ptr<SPropTagArray> lpPropertyTagArray;
	ULONG ulPropertyCount = 0;

	auto hr = MAPIAllocateBuffer(sizeof(SPropTagArray) + (sizeof(ULONG) * ulCount), &~lpPropertyTagArray);
	if (hr != hrSuccess)
		return hr;
	lpPropertyTagArray->cValues = ulCount;
	for (ULONG i = 0; i < ulCount; ++i)
		lpPropertyTagArray->aulPropTag[i] = lpTag[i];
	hr = lpMessage->GetProps(lpPropertyTagArray, 0, &ulPropertyCount, lppPropertyArray);
	if (FAILED(hr))
		kc_perror("Failed to obtain all properties", hr);
	return hr;
}

HRESULT ReadNamedProperties(LPMESSAGE lpMessage, ULONG ulCount, LPMAPINAMEID *lppTag,
			    LPSPropTagArray *lppPropertyTagArray, LPSPropValue *lppPropertyArray)
{
	ULONG ulReadCount = 0;
	auto hr = lpMessage->GetIDsFromNames(ulCount, lppTag, 0, lppPropertyTagArray);
	if(hr != hrSuccess) {
		kc_perror("Failed to obtain IDs from names", hr);
		/*
		 * Override status to make sure FAILED() will catch this,
		 * this is required to make sure the called won't attempt
		 * to access lppPropertyArray.
		 */
		return MAPI_E_CALL_FAILED;
	}
	hr = lpMessage->GetProps(*lppPropertyTagArray, 0, &ulReadCount, lppPropertyArray);
	if (FAILED(hr))
		kc_perror("Failed to obtain all properties", hr);
	return hr;
}

static HRESULT DetectFolderDetails(LPMAPIFOLDER lpFolder, string *lpName,
    string *lpClass, ULONG *lpFolderType)
{
	memory_ptr<SPropValue> lpPropertyArray;
	ULONG ulPropertyCount = 0;
	static constexpr const SizedSPropTagArray(3, PropertyTagArray) =
		{3, {PR_DISPLAY_NAME_A, PR_CONTAINER_CLASS_A, PR_FOLDER_TYPE}};

	auto hr = lpFolder->GetProps(PropertyTagArray, 0, &ulPropertyCount,
	          &~lpPropertyArray);
	if (FAILED(hr))
		return kc_perror("Failed to obtain all properties", hr);
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
    IMAPIFolder *lpRootFolder, const SRow *lpRow, const CHECKMAP &checkmap)
{
	object_ptr<IMAPIFolder> lpFolder;
	unsigned int ulObjectType = 0, ulFolderType = 0;
	std::string strName, strClass;

	auto lpItemProperty = lpRow->cfind(PR_ENTRYID);
	if (!lpItemProperty) {
		cout << "Row does not contain an EntryID." << endl;
		return hrSuccess;
	}
	auto hr = lpRootFolder->OpenEntry(lpItemProperty->Value.bin.cb,
	          reinterpret_cast<ENTRYID *>(lpItemProperty->Value.bin.lpb),
	          &IID_IMAPIFolder, 0, &ulObjectType, &~lpFolder);
	if (hr != hrSuccess)
		return kc_perror("Failed to open EntryID", hr);
	/*
	 * Detect folder name and class.
	 */
	hr = DetectFolderDetails(lpFolder, &strName, &strClass, &ulFolderType);
	if (hr != hrSuccess) {
		if (!strName.empty()) {
			cout << "Unknown class, skipping entry";
			cout << " \"" << strName << "\"" << endl;
		} else
			kc_perror("Failed to detect folder details", hr);
		return hr;
	}

	if (setFolderIgnore.find(string((const char*)lpItemProperty->Value.bin.lpb, lpItemProperty->Value.bin.cb)) != setFolderIgnore.end()) {
		cout << "Ignoring folder: ";
		cout << "\"" << strName << "\" (" << strClass << ")" << endl;
		return hrSuccess;
	}
	if (ulFolderType != FOLDER_GENERIC) {
		cout << "Ignoring search folder: ";
		cout << "\"" << strName << "\" (" << strClass << ")" << endl;
		return hrSuccess;
	}

	for (const auto &i : checkmap)
		if (i.first == strClass) {
			i.second->ValidateFolder(lpFolder, strName);
			return hrSuccess;
		}

	cout << "Ignoring folder: ";
	cout << "\"" << strName << "\" (" << strClass << ")" << endl;
	return hrSuccess;
}

static HRESULT RunStoreValidation(const char *strHost, const char *strUser,
    const char *strPass, const char *strAltUser, bool bPublic,
    const CHECKMAP &checkmap)
{
	AutoMAPI mapiinit;
	object_ptr<IMAPISession> lpSession;
	object_ptr<IMsgStore> lpStore, lpAltStore;
	LPMDB lpReadStore = NULL;
	object_ptr<IMAPIFolder> lpRootFolder;
	object_ptr<IMAPITable> lpHierarchyTable;
	unsigned int ulObjectType, ulCount;
	object_ptr<IExchangeManageStore> lpIEMS;
    // user
	memory_ptr<ENTRYID> lpUserStoreEntryID, lpEntryIDSrc;
	std::set<std::string> setFolderIgnore;
	memory_ptr<SPropValue> lpAddRenProp;
	unsigned int cbUserStoreEntryID = 0, cbEntryIDSrc = 0;

	auto hr = mapiinit.Initialize();
	if (hr != hrSuccess)
		return kc_perror("Unable to initialize session", hr);
	// input from commandline is current locale
	hr = HrOpenECSession(&~lpSession, PROJECT_VERSION, "fsck",
	     strUser, strPass, strHost, 0, nullptr, nullptr);
	if(hr != hrSuccess) {
		cout << "Wrong username or password." << endl;
		return hr;
	}

	if (bPublic) {
		hr = HrOpenECPublicStore(lpSession, &~lpStore);
		if (hr != hrSuccess)
			return kc_perror("Failed to open public store", hr);
	} else {
		hr = HrOpenDefaultStore(lpSession, &~lpStore);
		if (hr != hrSuccess)
			return kc_perror("Failed to open default store", hr);
	}

	if (strAltUser != nullptr && *strAltUser != '\0') {
		hr = lpStore->QueryInterface(IID_IExchangeManageStore, &~lpIEMS);
		if (hr != hrSuccess)
			return kc_perror("Cannot open ExchangeManageStore object", hr);

		hr = lpIEMS->CreateStoreEntryID(reinterpret_cast<const TCHAR *>(L""),
		     reinterpret_cast<const TCHAR *>(convert_to<std::wstring>(strAltUser).c_str()),
		     MAPI_UNICODE | OPENSTORE_HOME_LOGON, &cbUserStoreEntryID,
		     &~lpUserStoreEntryID);
        if (hr != hrSuccess) {
            cout << "Cannot get user store id for user" << endl;
		return hr;
        }
        hr = lpSession->OpenMsgStore(0, cbUserStoreEntryID, lpUserStoreEntryID, nullptr, MDB_WRITE | MDB_NO_DIALOG | MDB_NO_MAIL | MDB_TEMPORARY, &~lpAltStore);
        if (hr != hrSuccess) {
            cout << "Cannot open user store of user" << endl;
		return hr;
        }
        lpReadStore = lpAltStore;
	} else {
	    lpReadStore = lpStore;
    }

	hr = lpReadStore->OpenEntry(0, nullptr, &IID_IMAPIFolder, 0, &ulObjectType, &~lpRootFolder);
	if (hr != hrSuccess)
		return kc_perror("Failed to open root folder", hr);
	if (HrGetOneProp(lpRootFolder, PR_IPM_OL2007_ENTRYIDS /*PR_ADDITIONAL_REN_ENTRYIDS_EX*/, &~lpAddRenProp) == hrSuccess &&
	    Util::ExtractSuggestedContactsEntryID(lpAddRenProp, &cbEntryIDSrc, &~lpEntryIDSrc) == hrSuccess)
		setFolderIgnore.emplace(reinterpret_cast<const char *>(lpEntryIDSrc.get()), cbEntryIDSrc);
	hr = lpRootFolder->GetHierarchyTable(CONVENIENT_DEPTH, &~lpHierarchyTable);
	if (hr != hrSuccess)
		return kc_perror("Failed to open hierarchy", hr);
	/*
	 * Check if we have found at least *something*.
	 */
	hr = lpHierarchyTable->GetRowCount(0, &ulCount);
	if (hr != hrSuccess) {
		return kc_perror("Failed to count number of rows", hr);
	} else if (!ulCount) {
		cout << "No entries inside Calendar." << endl;
		return hr;
	}

	/*
	 * Loop through each row/entry and validate.
	 */
	while (true) {
		rowset_ptr lpRows;
		hr = lpHierarchyTable->QueryRows(20, 0, &~lpRows);
		if (hr != hrSuccess)
			return hr;
		if (lpRows->cRows == 0)
			break;
		for (ULONG i = 0; i < lpRows->cRows; ++i)
			RunFolderValidation(setFolderIgnore, lpRootFolder, &lpRows[i], checkmap);
	}
	return hrSuccess;
}

int main(int argc, char **argv) try
{
	CHECKMAP checkmap;
	char *strUser = nullptr, *strAltUser = nullptr;
	const char *strPass = "";
	int c;
	bool bAll = false, bPrompt = false, bPublic = false, acceptDisclaimer = false;

	setlocale(LC_MESSAGES, "");
	if (!forceUTF8Locale(true))
		return -1;

	auto strHost = GetServerUnixSocket();

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
			/*
			 * g++ 4.9 is not smart enough to derive that "new
			 * FsckCalendar" should be converted to
			 * std::unique_ptr, so it needs to be spelled out.
			 * (Fixed in modern g++s.)
			 */
			checkmap.emplace("IPF.Appointment", std::unique_ptr<Fsck>(new FsckCalendar));
			break;
		//case OPT_STICKY:
		//	checkmap.emplace("IPF.StickyNote", std::unique_ptr<Fsck>(new FsckStickyNote));
		//	break;
		//case OPT_EMAIL:
		//	checkmap.emplace("IPF.Note", std::unique_ptr<Fsck>(new FsckNote));
		//	break;
		case OPT_CONTACT:
			checkmap.emplace("IPF.Contact", std::unique_ptr<Fsck>(new FsckContact));
			break;
		case OPT_TASK:
			checkmap.emplace("IPF.Task", std::unique_ptr<Fsck>(new FsckTask));
			break;
		//case OPT_JOURNAL:
		//	checkmap.emplace("IPF.Journal", std::unique_ptr<Fsck>(new FsckJournal));
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
		checkmap.emplace("IPF.Appointment", std::unique_ptr<Fsck>(new FsckCalendar));
		//checkmap.emplace("IPF.StickyNote", std::unique_ptr<Fsck>(new FsckStickyNote));
		//checkmap.emplace("IPF.Note", std::unique_ptr<Fsck>(new FsckNote));
		checkmap.emplace("IPF.Contact", std::unique_ptr<Fsck>(new FsckContact));
		checkmap.emplace("IPF.Task", std::unique_ptr<Fsck>(new FsckTask));
		//checkmap.emplace("IPF.Journal", std::unique_ptr<Fsck>(new FsckJournal));
	}

	auto hr = RunStoreValidation(strHost, strUser, strPass, strAltUser, bPublic, checkmap);
	/*
	 * Cleanup
	 */
	if (hr != hrSuccess)
		return 1;
	cout << endl << "Statistics:" << endl;
	for (const auto &i : checkmap)
		i.second->PrintStatistics(i.first);
	return 0;
} catch (...) {
	std::terminate();
}
