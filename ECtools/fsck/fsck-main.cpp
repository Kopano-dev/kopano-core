/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <iostream>
#include <set>
#include <string>
#include <list>
#include <utility>
#include <kopano/CommonUtil.h>
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <kopano/memory.hpp>
#include <mapiutil.h>
#include <mapix.h>
#include <kopano/stringutil.h>
#include "fsck.h"

using namespace KC;
using std::endl;
using std::cin;
using std::cout;
using std::string;

static bool ReadYesNoMessage(const std::string &strMessage,
    const std::string &strAuto)
{
	string strReply;

	cout << strMessage << " [yes/no]: ";
	if (strAuto.empty())
		getline(cin, strReply);
	else {
		cout << strAuto << endl;
		strReply = strAuto;
	}
	return (strReply[0] == 'y' || strReply[0] == 'Y');
}

static HRESULT DeleteEntry(LPMAPIFOLDER lpFolder,
    const SPropValue *lpItemProperty)
{
	memory_ptr<ENTRYLIST> lpEntryList;
	HRESULT hr = MAPIAllocateBuffer(sizeof(ENTRYLIST), &~lpEntryList);
	if (hr != hrSuccess)
		goto exit;
	hr = MAPIAllocateMore(sizeof(SBinary), lpEntryList, reinterpret_cast<void **>(&lpEntryList->lpbin));
	if (hr != hrSuccess)
		goto exit;
	lpEntryList->cValues = 1;
	lpEntryList->lpbin[0] = lpItemProperty->Value.bin;
	hr = lpFolder->DeleteMessages(lpEntryList, 0, NULL, 0);

exit:
	if (hr == hrSuccess)
		cout << "Item deleted." << endl;
	else
		cout << "Failed to delete entry." << endl;
	return hr;
}

static HRESULT FixProperty(LPMESSAGE lpMessage, const std::string &strName,
    ULONG ulTag, __UPV Value)
{
	SPropValue ErrorProp;

	ErrorProp.ulPropTag = ulTag;
	ErrorProp.Value = Value;
	/* NOTE: Named properties don't have the PT value set by default,
	   The caller of this function should have taken care of this. */
	if (PROP_ID(ulTag) == 0 || PROP_TYPE(ulTag) == 0) {
		cout << "Invalid property tag: " << stringify_hex(ulTag) << endl;
		return MAPI_E_INVALID_PARAMETER;
	}
	auto hr = lpMessage->SetProps(1, &ErrorProp, nullptr);
	if (hr != hrSuccess) {
		cout << "Failed to fix broken property." << endl;
		return hr;
	}
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		cout << "Failed to save changes to fix broken property";
	return hr;
}

static HRESULT DetectFolderEntryDetails(LPMESSAGE lpMessage, string *lpName,
    string *lpClass)
{
	memory_ptr<SPropValue> lpPropertyArray;
	ULONG ulPropertyCount;
	static constexpr const SizedSPropTagArray(3, PropertyTagArray) =
		{3, {PR_SUBJECT_A, PR_NORMALIZED_SUBJECT_A, PR_MESSAGE_CLASS_A}};

	auto hr = lpMessage->GetProps(PropertyTagArray, 0, &ulPropertyCount,
	          &~lpPropertyArray);
	if (FAILED(hr)) {
		cout << "Failed to obtain all properties." << endl;
		return hr;
	}

	for (ULONG i = 0; i < ulPropertyCount; ++i) {
		if (PROP_TYPE(lpPropertyArray[i].ulPropTag) == PT_ERROR)
			continue;
		else if (lpPropertyArray[i].ulPropTag == PR_SUBJECT_A)
			*lpName = lpPropertyArray[i].Value.lpszA;
		else if (lpPropertyArray[i].ulPropTag == PR_NORMALIZED_SUBJECT_A)
			*lpName = lpPropertyArray[i].Value.lpszA;
		else if (lpPropertyArray[i].ulPropTag == PR_MESSAGE_CLASS_A)
			*lpClass = lpPropertyArray[i].Value.lpszA;
	}

	/*
	 * The name is allowed to be empty, the class however not.
	 */
	if (lpClass->empty())
		cout << "Message class is empty.";
	else
		hr = hrSuccess;
	return hr;
}

static HRESULT ProcessFolderEntry(Fsck *lpFsck, LPMAPIFOLDER lpFolder,
    const SRow *lpRow)
{
	HRESULT hr = hrSuccess;
	object_ptr<IMessage> lpMessage;
	ULONG ulObjectType = 0;
	std::string strName, strClass;

	auto lpItemProperty = lpRow->cfind(PR_ENTRYID);
	if (!lpItemProperty) {
		cout << "Row does not contain an EntryID." << endl;
		goto exit;
	}
	hr = lpFolder->OpenEntry(lpItemProperty->Value.bin.cb,
	          reinterpret_cast<ENTRYID *>(lpItemProperty->Value.bin.lpb),
	          &IID_IMessage, MAPI_MODIFY, &ulObjectType, &~lpMessage);
	if (hr != hrSuccess) {
		cout << "Failed to open EntryID." << endl;
		goto exit;
	}
	hr = DetectFolderEntryDetails(lpMessage, &strName, &strClass);
	if (hr != hrSuccess)
		goto exit;
	hr = lpFsck->ValidateMessage(lpMessage, strName, strClass);
	if (hr != hrSuccess)
		goto exit;
exit:
	if (hr != hrSuccess)
		hr = lpFsck->DeleteMessage(lpFolder, lpItemProperty);
	return hr;
}

static HRESULT ProcessFolder(Fsck *lpFsck, LPMAPIFOLDER lpFolder,
    const std::string &strName)
{
	object_ptr<IMAPITable> lpTable;
	ULONG ulCount;
	HRESULT hr = lpFolder->GetContentsTable(0, &~lpTable);
 	if(hr != hrSuccess) {
		cout << "Failed to open Folder table." << endl;
		return hr;
	}
	/*
	 * Check if we have found at least *something*.
	 */
	hr = lpTable->GetRowCount(0, &ulCount);
	if(hr != hrSuccess) {
		cout << "Failed to count number of rows." << endl;
		return hr;
	} else if (!ulCount) {
		cout << "No entries inside folder." << endl;
		return hr;
	}

	/*
	 * Loop through each row/entry and validate.
	 */
	while (true) {
		rowset_ptr lpRows;
		hr = lpTable->QueryRows(20, 0, &~lpRows);
		if (hr != hrSuccess)
			return hr;
		if (lpRows->cRows == 0)
			break;
		for (ULONG i = 0; i < lpRows->cRows; ++i) {
			hr = ProcessFolderEntry(lpFsck, lpFolder, &lpRows[i]);
			if (hr != hrSuccess)
				// Move along, nothing to see.
				cout << "Failed to validate entry." << endl;
		}
	}
	return hrSuccess;
}

/*
 * Fsck implementation.
 */
HRESULT Fsck::ValidateMessage(LPMESSAGE lpMessage,
    const std::string &strName, const std::string &strClass)
{
	cout << "Validating entry: \"" << strName << "\"" << endl;
	++ulEntries;
	auto hr = ValidateItem(lpMessage, strClass);
	cout << "Validation of entry \"" << strName << "\" complete" << endl;
	return hr;
}

HRESULT Fsck::ValidateFolder(LPMAPIFOLDER lpFolder,
    const std::string &strName)
{
	cout << "Validating folder \"" << strName << "\"" << endl;
	++ulFolders;
	HRESULT hr = ProcessFolder(this, lpFolder, strName);
	cout << "Validation of folder \"" << strName << "\" complete" << endl;
	return hr;
}

HRESULT Fsck::AddMissingProperty(LPMESSAGE lpMessage,
    const std::string &strName, ULONG ulTag, __UPV Value)
{
	cout << "Missing property " << strName << endl;
	++ulProblems;
	if (!ReadYesNoMessage("Add missing property?", auto_fix))
		return hrSuccess;
	auto hr = FixProperty(lpMessage, strName, ulTag, Value);
	if (hr == hrSuccess)
		++ulFixed;
	return hr;
}

HRESULT Fsck::ReplaceProperty(LPMESSAGE lpMessage,
    const std::string &strName, ULONG ulTag, const std::string &strError,
    __UPV Value)
{
	cout << "Invalid property " << strName << " - " << strError << endl;
	++ulProblems;
	if (!ReadYesNoMessage("Fix broken property?", auto_fix))
		return hrSuccess;
	auto hr = FixProperty(lpMessage, strName, ulTag, Value);
	if (hr == hrSuccess)
		++ulFixed;
	return hr;
}

HRESULT Fsck::DeleteRecipientList(LPMESSAGE lpMessage, std::list<unsigned int> &mapiReciptDel, bool &bChanged)
{
	++ulProblems;
	cout << mapiReciptDel.size() << " duplicate or invalid recipients found. " << endl;
	if (!ReadYesNoMessage("Remove duplicate or invalid recipients?", auto_fix))
		return hrSuccess;

	memory_ptr<ADRLIST> lpMods;
	HRESULT hr = MAPIAllocateBuffer(CbNewADRLIST(mapiReciptDel.size()), &~lpMods);
	if (hr != hrSuccess)
		return hr;

	lpMods->cEntries = 0;
	for (const auto &recip : mapiReciptDel) {
		auto &ent = lpMods->aEntries[lpMods->cEntries++];
		ent.cValues = 1;
		hr = MAPIAllocateMore(sizeof(SPropValue), lpMods, reinterpret_cast<void **>(&ent.rgPropVals));
		if (hr != hrSuccess)
			return hr;
		ent.rgPropVals->ulPropTag = PR_ROWID;
		ent.rgPropVals->Value.ul = recip;
	}

	hr = lpMessage->ModifyRecipients(MODRECIP_REMOVE, lpMods.get());
	if (hr != hrSuccess)
		return hr;
	hr = lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		return hr;
	bChanged = true;
	++ulFixed;
	return hrSuccess;
}

HRESULT Fsck::DeleteMessage(LPMAPIFOLDER lpFolder,
    const SPropValue *lpItemProperty)
{
	if (!ReadYesNoMessage("Delete message?", auto_del))
		return hrSuccess;
	auto hr = DeleteEntry(lpFolder, lpItemProperty);
	if (hr == hrSuccess)
		++ulDeleted;
	return hr;
}

HRESULT Fsck::ValidateRecursiveDuplicateRecipients(LPMESSAGE lpMessage, bool &bChanged)
{
	bool bSubChanged = false;
	object_ptr<IMAPITable> lpTable;
    ULONG cRows = 0;
	static constexpr const SizedSPropTagArray(2, sptaProps) =
		{2, {PR_ATTACH_NUM, PR_ATTACH_METHOD}};

	auto hr = lpMessage->GetAttachmentTable(0, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->GetRowCount(0, &cRows);
	if (hr != hrSuccess)
		return hr;
	if (cRows == 0)
		goto message;
	hr = lpTable->SetColumns(sptaProps, 0);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		rowset_ptr pRows;
		hr = lpTable->QueryRows(50, 0, &~pRows);
		if (hr != hrSuccess)
			return hr;
		if (pRows->cRows == 0)
			break;

		for (unsigned int i = 0; i < pRows->cRows; ++i) {
			if (pRows[i].lpProps[1].ulPropTag != PR_ATTACH_METHOD ||
			    pRows[i].lpProps[1].Value.ul != ATTACH_EMBEDDED_MSG)
				continue;

			object_ptr<IAttach> lpAttach;
			object_ptr<IMessage> lpSubMessage;
			bSubChanged = false;
			hr = lpMessage->OpenAttach(pRows[i].lpProps[0].Value.ul, nullptr, MAPI_BEST_ACCESS, &~lpAttach);
			if (hr != hrSuccess)
				return hr;
			hr = lpAttach->OpenProperty(PR_ATTACH_DATA_OBJ, &IID_IMessage, 0, MAPI_MODIFY, &~lpSubMessage);
			if (hr != hrSuccess)
				return hr;
			hr = ValidateRecursiveDuplicateRecipients(lpSubMessage, bSubChanged);
			if (hr != hrSuccess)
				return hr;
			if (!bSubChanged)
				continue;
			hr = lpAttach->SaveChanges(KEEP_OPEN_READWRITE);
			if (hr != hrSuccess)
				return hr;
			bChanged = bSubChanged;
		}
	}

message:
	hr = ValidateDuplicateRecipients(lpMessage, bChanged);
	if (hr != hrSuccess)
		return hr;
	if (bChanged)
		lpMessage->SaveChanges(KEEP_OPEN_READWRITE);
	return hrSuccess;
}

HRESULT Fsck::ValidateDuplicateRecipients(LPMESSAGE lpMessage, bool &bChanged)
{
	object_ptr<IMAPITable> lpTable;
	ULONG cRows = 0;
	std::set<std::string> mapRecip;
	std::list<unsigned int> mapiReciptDel;
	static constexpr const SizedSPropTagArray(5, sptaProps) =
		{5, {PR_ROWID, PR_DISPLAY_NAME_A, PR_EMAIL_ADDRESS_A,
		PR_RECIPIENT_TYPE, PR_ENTRYID}};

	HRESULT hr = lpMessage->GetRecipientTable(0, &~lpTable);
	if (hr != hrSuccess)
		return hr;
	hr = lpTable->GetRowCount(0, &cRows);
	if (hr != hrSuccess)
		return hr;
	if (cRows < 1)
		// 0 or 1 row not needed to check
		return hr;
	hr = lpTable->SetColumns(sptaProps, 0);
	if (hr != hrSuccess)
		return hr;

	while (true) {
		rowset_ptr pRows;
		hr = lpTable->QueryRows(50, 0, &~pRows);
		if (hr != hrSuccess)
			return hr;
		if (pRows->cRows == 0)
			break;

		for (unsigned int i = 0; i < pRows->cRows; ++i) {
			if (pRows[i].lpProps[1].ulPropTag != PR_DISPLAY_NAME_A &&
			    pRows[i].lpProps[2].ulPropTag != PR_EMAIL_ADDRESS_A) {
				mapiReciptDel.emplace_back(pRows[i].lpProps[0].Value.ul);
				continue;
			}
			// Invalid or missing entryid
			if (pRows[i].lpProps[4].ulPropTag != PR_ENTRYID ||
			    pRows[i].lpProps[4].Value.bin.cb == 0) {
				mapiReciptDel.emplace_back(pRows[i].lpProps[0].Value.ul);
				continue;
			}

			std::string strData;
			if (pRows[i].lpProps[1].ulPropTag == PR_DISPLAY_NAME_A)
				strData += pRows[i].lpProps[1].Value.lpszA;
			if (pRows[i].lpProps[2].ulPropTag == PR_EMAIL_ADDRESS_A)
				strData += pRows[i].lpProps[2].Value.lpszA;
			if (pRows[i].lpProps[3].ulPropTag == PR_RECIPIENT_TYPE)
				strData += stringify(pRows[i].lpProps[3].Value.ul);
			auto res = mapRecip.emplace(std::move(strData));
			if (!res.second)
				mapiReciptDel.emplace_back(pRows[i].lpProps[0].Value.ul);
		}
	}
	// modify
	if (!mapiReciptDel.empty())
		hr = DeleteRecipientList(lpMessage, mapiReciptDel, bChanged);
	return hr;
}

void Fsck::PrintStatistics(const std::string &title)
{
	cout << title << endl;
	cout << "\tFolders:\t" << ulFolders << endl;
	cout << "\tEntries:\t" << ulEntries << endl;
	cout << "\tProblems:\t" << ulProblems << endl;
	cout << "\tFixed:\t\t" << ulFixed << endl;
	cout << "\tDeleted:\t" << ulDeleted << endl;
}
