/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <algorithm>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <climits>
#include <cstring>
#include "ZCABContainer.h"
#include "ZCMAPIProp.h"
#include <mapiutil.h>
#include <kopano/ECMemTable.h>
#include <kopano/ECGuid.h>
#include <kopano/CommonUtil.h>
#include <kopano/hl.hpp>
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <kopano/memory.hpp>
#include <kopano/namedprops.h>
#include <kopano/charset/convert.h>
#include <kopano/ECGetText.h>
#include <kopano/EMSAbTag.h>
#include <kopano/ECRestriction.h>
#include <kopano/Util.h>
#include <kopano/stringutil.h>

using namespace KC;

ZCABContainer::ZCABContainer(std::shared_ptr<std::vector<zcabFolderEntry>> lpFolders,
    IMAPIFolder *lpContacts, IMAPISupport *lpMAPISup, void *lpProvider) :
	m_lpFolders(std::move(lpFolders)),
	m_lpContactFolder(lpContacts), m_lpMAPISup(lpMAPISup),
	m_lpProvider(lpProvider)
{
	assert(lpFolders == nullptr || lpContacts == nullptr);
}

HRESULT	ZCABContainer::QueryInterface(REFIID refiid, void **lppInterface)
{
	if (m_lpDistList == NULL)
		REGISTER_INTERFACE2(ZCABContainer, this);
	else
		REGISTER_INTERFACE3(ZCDistList, ZCABContainer, this);
	REGISTER_INTERFACE2(ECUnknown, this);

	if (m_lpDistList == NULL)
		REGISTER_INTERFACE2(IABContainer, this);
	else
		REGISTER_INTERFACE2(IDistList, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

/**
 * Create a ZCABContainer as either the top level (lpFolders is set) or
 * as a subfolder (lpContacts is set).
 *
 * @param[in] lpFolders Only the top container has the list to the wanted Kopano Contacts Folders, NULL otherwise.
 * @param[in] lpContacts Create this object as wrapper for the lpContacts folder, NULL if
 * @param[in] lpMAPISup
 * @param[in] lpProvider
 * @param[out] lppABContainer The newly created ZCABContainer class
 *
 * @return
 */
HRESULT ZCABContainer::Create(std::shared_ptr<std::vector<zcabFolderEntry>> lpFolders,
    IMAPIFolder *lpContacts, IMAPISupport *lpMAPISup, void *lpProvider,
    ZCABContainer **lppABContainer)
{
	return alloc_wrap<ZCABContainer>(std::move(lpFolders), lpContacts, lpMAPISup, lpProvider).put(lppABContainer);
}

HRESULT ZCABContainer::Create(IMessage *lpContact, ULONG cbEntryID,
    const ENTRYID *lpEntryID, IMAPISupport *lpMAPISup,
    ZCABContainer **lppABContainer)
{
	object_ptr<ZCMAPIProp> lpDistList;
	object_ptr<ZCABContainer> lpABContainer(new(std::nothrow) ZCABContainer(nullptr, nullptr, lpMAPISup, nullptr));
	if (lpABContainer == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	auto hr = ZCMAPIProp::Create(lpContact, cbEntryID, lpEntryID, &~lpDistList);
	if (hr != hrSuccess)
		return hr;
	hr = lpDistList->QueryInterface(IID_IMAPIProp, &~lpABContainer->m_lpDistList);
	if (hr != hrSuccess)
		return hr;
	return lpABContainer->QueryInterface(IID_ZCDistList, reinterpret_cast<void **>(lppABContainer));
}

// IMAPIContainer
HRESULT ZCABContainer::MakeWrappedEntryID(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulObjType, ULONG ulOffset, ULONG *lpcbEntryID, LPENTRYID *lppEntryID)
{
	cabEntryID *lpWrapped = NULL;
	ULONG cbWrapped = CbNewCABENTRYID(cbEntryID);
	auto hr = MAPIAllocateBuffer(cbWrapped, reinterpret_cast<void **>(&lpWrapped));
	if (hr != hrSuccess)
		return hr;

	memset(lpWrapped, 0, cbWrapped);
	memcpy(&lpWrapped->muid, &MUIDZCSAB, sizeof(MAPIUID));
	lpWrapped->ulObjType = ulObjType;
	lpWrapped->ulOffset = ulOffset;
	memcpy(lpWrapped->origEntryID, lpEntryID, cbEntryID);


	*lpcbEntryID = cbWrapped;
	*lppEntryID = (LPENTRYID)lpWrapped;
	return hrSuccess;
}

static constexpr const MAPINAMEID default_namedprops[(6*5)+2] = {
#define PS const_cast<GUID *>(&PSETID_Address)
	/* index with MVI_FLAG */
	{PS, MNID_ID, {dispidABPEmailList}},

	/* MVI offset 0: email1 set */
	{PS, MNID_ID, {dispidEmail1DisplayName}},
	{PS, MNID_ID, {dispidEmail1AddressType}},
	{PS, MNID_ID, {dispidEmail1Address}},
	{PS, MNID_ID, {dispidEmail1OriginalDisplayName}},
	{PS, MNID_ID, {dispidEmail1OriginalEntryID}},

	/* MVI offset 1: email2 set */
	{PS, MNID_ID, {dispidEmail2DisplayName}},
	{PS, MNID_ID, {dispidEmail2AddressType}},
	{PS, MNID_ID, {dispidEmail2Address}},
	{PS, MNID_ID, {dispidEmail2OriginalDisplayName}},
	{PS, MNID_ID, {dispidEmail2OriginalEntryID}},

	/* MVI offset 2: email3 set */
	{PS, MNID_ID, {dispidEmail3DisplayName}},
	{PS, MNID_ID, {dispidEmail3AddressType}},
	{PS, MNID_ID, {dispidEmail3Address}},
	{PS, MNID_ID, {dispidEmail3OriginalDisplayName}},
	{PS, MNID_ID, {dispidEmail3OriginalEntryID}},

	/* MVI offset 3: business fax (fax2) set */
	{PS, MNID_ID, {dispidFax2DisplayName}},
	{PS, MNID_ID, {dispidFax2AddressType}},
	{PS, MNID_ID, {dispidFax2Address}},
	{PS, MNID_ID, {dispidFax2OriginalDisplayName}},
	{PS, MNID_ID, {dispidFax2OriginalEntryID}},

	/* MVI offset 4: home fax (fax3) set */
	{PS, MNID_ID, {dispidFax3DisplayName}},
	{PS, MNID_ID, {dispidFax3AddressType}},
	{PS, MNID_ID, {dispidFax3Address}},
	{PS, MNID_ID, {dispidFax3OriginalDisplayName}},
	{PS, MNID_ID, {dispidFax3OriginalEntryID}},

	/* MVI offset 5: primary fax (fax1) set */
	{PS, MNID_ID, {dispidFax1DisplayName}},
	{PS, MNID_ID, {dispidFax1AddressType}},
	{PS, MNID_ID, {dispidFax1Address}},
	{PS, MNID_ID, {dispidFax1OriginalDisplayName}},
	{PS, MNID_ID, {dispidFax1OriginalEntryID}},

	/* restriction */
	{PS, MNID_ID, {dispidABPArrayType}},
#undef PS
};

HRESULT ZCABContainer::GetFolderContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;
	ULONG i, j = 0;
	ECAndRestriction resAnd;
	SPropValue sRestrictProp;

	// I_MV_INDEX is dispidABPEmailList from mnNamedProps
	enum {
		I_DISPLAY_NAME = 0, I_ADDRTYPE, I_EMAIL_ADDRESS,
		I_NORMALIZED_SUBJECT, I_ENTRYID, I_MESSAGE_CLASS,
		I_ORIGINAL_DISPLAY_NAME, I_PARENT_ENTRYID, I_SOURCE_KEY,
		I_PARENT_SOURCE_KEY, I_CHANGE_KEY, I_COMPANY_NAME,
		I_PHONE_BUS, I_PHONE_MOB, I_NCOLS,
		I_MV_INDEX = I_NCOLS, I_NAMEDSTART
	};
	// data from the contact
	SizedSPropTagArray(I_NCOLS, inputCols) =
		{I_NCOLS, {PR_DISPLAY_NAME, PR_ADDRTYPE, PR_EMAIL_ADDRESS,
		PR_NORMALIZED_SUBJECT, PR_ENTRYID, PR_MESSAGE_CLASS,
		PR_ORIGINAL_DISPLAY_NAME, PR_PARENT_ENTRYID, PR_SOURCE_KEY,
		PR_PARENT_SOURCE_KEY, PR_CHANGE_KEY, PR_COMPANY_NAME,
		PR_BUSINESS_TELEPHONE_NUMBER, PR_MOBILE_TELEPHONE_NUMBER}};

	// data for the table
	enum {
		O_DISPLAY_NAME = 0, O_ADDRTYPE, O_EMAIL_ADDRESS,
		O_NORMALIZED_SUBJECT, O_ENTRYID, O_DISPLAY_TYPE, O_OBJECT_TYPE,
		O_ORIGINAL_DISPLAY_NAME, O_ZC_ORIGINAL_ENTRYID,
		O_ZC_ORIGINAL_PARENT_ENTRYID, O_ZC_ORIGINAL_SOURCE_KEY,
		O_ZC_ORIGINAL_PARENT_SOURCE_KEY, O_ZC_ORIGINAL_CHANGE_KEY,
		O_SEARCH_KEY, O_INSTANCE_KEY, O_COMPANY_NAME,
		O_PHONE_BUS, O_PHONE_MOB, O_ROWID, O_NCOLS
	};
	SizedSPropTagArray(O_NCOLS, outputCols) =
		{O_NCOLS, {PR_DISPLAY_NAME, PR_ADDRTYPE, PR_EMAIL_ADDRESS,
		PR_NORMALIZED_SUBJECT, PR_ENTRYID, PR_DISPLAY_TYPE,
		PR_OBJECT_TYPE, PR_ORIGINAL_DISPLAY_NAME,
		PR_ZC_ORIGINAL_ENTRYID, PR_ZC_ORIGINAL_PARENT_ENTRYID,
		PR_ZC_ORIGINAL_SOURCE_KEY, PR_ZC_ORIGINAL_PARENT_SOURCE_KEY,
		PR_ZC_ORIGINAL_CHANGE_KEY, PR_SEARCH_KEY, PR_INSTANCE_KEY,
		PR_COMPANY_NAME, PR_BUSINESS_TELEPHONE_NUMBER,
		PR_MOBILE_TELEPHONE_NUMBER, PR_ROWID}};

	// named properties
	memory_ptr<SPropTagArray> ptrNameTags, ptrContactCols;
	memory_ptr<MAPINAMEID *> lppNames;
	ULONG ulNames = (6 * 5) + 2;
	ULONG ulType = (ulFlags & MAPI_UNICODE) ? PT_UNICODE : PT_STRING8;

	Util::proptag_change_unicode(ulFlags, inputCols);
	Util::proptag_change_unicode(ulFlags, outputCols);
	auto hr = ECMemTable::Create(outputCols, PR_ROWID, &~lpTable);
	if(hr != hrSuccess)
		return hr;

	// root container has no contents, on hierarchy entries
	object_ptr<IMAPITable> ptrContents;
	if (m_lpContactFolder == NULL)
		goto done;
	hr = m_lpContactFolder->GetContentsTable(ulFlags | MAPI_DEFERRED_ERRORS, &~ptrContents);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(MAPINAMEID *) * ulNames, &~lppNames);
	if (hr != hrSuccess)
		return hr;

	std::remove_cv_t<decltype(default_namedprops)> mnNamedProps;
	memcpy(mnNamedProps, default_namedprops, sizeof(default_namedprops));
	for (i = 0; i < ulNames; ++i)
		lppNames[i] = &mnNamedProps[i];
	hr = m_lpContactFolder->GetIDsFromNames(ulNames, lppNames, MAPI_CREATE, &~ptrNameTags);
	if (FAILED(hr))
		return hr;

	// fix types
	ptrNameTags->aulPropTag[0] = CHANGE_PROP_TYPE(ptrNameTags->aulPropTag[0], PT_MV_LONG | MV_INSTANCE);
	for (i = 0; i < (ulNames - 2) / 5; ++i) {
		ptrNameTags->aulPropTag[1+ (i*5) + 0] = CHANGE_PROP_TYPE(ptrNameTags->aulPropTag[1+ (i*5) + 0], ulType);
		ptrNameTags->aulPropTag[1+ (i*5) + 1] = CHANGE_PROP_TYPE(ptrNameTags->aulPropTag[1+ (i*5) + 1], ulType);
		ptrNameTags->aulPropTag[1+ (i*5) + 2] = CHANGE_PROP_TYPE(ptrNameTags->aulPropTag[1+ (i*5) + 2], ulType);
		ptrNameTags->aulPropTag[1+ (i*5) + 3] = CHANGE_PROP_TYPE(ptrNameTags->aulPropTag[1+ (i*5) + 3], ulType);
		ptrNameTags->aulPropTag[1+ (i*5) + 4] = CHANGE_PROP_TYPE(ptrNameTags->aulPropTag[1+ (i*5) + 4], PT_BINARY);
	}
	ptrNameTags->aulPropTag[ulNames-1] = CHANGE_PROP_TYPE(ptrNameTags->aulPropTag[ulNames-1], PT_LONG);

	// add func HrCombinePropTagArrays(part1, part2, dest);
	hr = MAPIAllocateBuffer(CbNewSPropTagArray(inputCols.cValues + ptrNameTags->cValues), &~ptrContactCols);
	if (hr != hrSuccess)
		return hr;
	j = 0;
	for (i = 0; i < inputCols.cValues; ++i)
		ptrContactCols->aulPropTag[j++] = inputCols.aulPropTag[i];
	for (i = 0; i < ptrNameTags->cValues; ++i)
		ptrContactCols->aulPropTag[j++] = ptrNameTags->aulPropTag[i];
	ptrContactCols->cValues = j;

	// the exists is extra compared to the outlook restriction
	// restrict: ( distlist || ( contact && exist(abparraytype) && abparraytype != 0 ) )

	sRestrictProp.ulPropTag = PR_MESSAGE_CLASS_A;
	sRestrictProp.Value.lpszA = const_cast<char *>("IPM.Contact");
	resAnd += ECContentRestriction(FL_PREFIX|FL_IGNORECASE, PR_MESSAGE_CLASS_A, &sRestrictProp, ECRestriction::Shallow);
	sRestrictProp.ulPropTag = ptrNameTags->aulPropTag[ulNames-1];
	sRestrictProp.Value.ul = 0;
	resAnd += ECExistRestriction(sRestrictProp.ulPropTag);
	resAnd += ECPropertyRestriction(RELOP_NE, sRestrictProp.ulPropTag, &sRestrictProp, ECRestriction::Shallow);

	sRestrictProp.ulPropTag = PR_MESSAGE_CLASS_A;
	sRestrictProp.Value.lpszA = const_cast<char *>("IPM.DistList");
	hr = ECOrRestriction(
		ECContentRestriction(FL_PREFIX | FL_IGNORECASE, PR_MESSAGE_CLASS_A, &sRestrictProp, ECRestriction::Cheap) +
		resAnd
	).RestrictTable(ptrContents, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;
	// set columns
	hr = ptrContents->SetColumns(ptrContactCols, TBL_BATCH);
	if (hr != hrSuccess)
		return hr;

	j = 0;
	while (true) {
		rowset_ptr ptrRows;
		hr = ptrContents->QueryRows(256, 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;
		if (ptrRows.empty())
			break;

		for (i = 0; i < ptrRows.size(); ++i) {
			ULONG ulOffset = 0;
			std::string strSearchKey;
			SPropValue lpColData[O_NCOLS];

			memset(lpColData, 0, sizeof(lpColData));

			auto props = ptrRows[i].lpProps;
			if (props[I_MV_INDEX].ulPropTag == (ptrNameTags->aulPropTag[0] & ~MVI_FLAG)) {
				// do not index outside named properties
				if (props[I_MV_INDEX].Value.ul > 5)
					continue;
				ulOffset = props[I_MV_INDEX].Value.ul * 5;
			}

			if (PROP_TYPE(props[I_MESSAGE_CLASS].ulPropTag) == PT_ERROR)
				// no PR_MESSAGE_CLASS, unusable
				continue;

			if (((ulFlags & MAPI_UNICODE) && wcscasecmp(props[I_MESSAGE_CLASS].Value.lpszW, L"IPM.Contact") == 0) ||
			    ((ulFlags & MAPI_UNICODE) == 0 && strcasecmp(props[I_MESSAGE_CLASS].Value.lpszA, "IPM.Contact") == 0)) {
				lpColData[O_DISPLAY_TYPE].ulPropTag = PR_DISPLAY_TYPE;
				lpColData[O_DISPLAY_TYPE].Value.ul = DT_MAILUSER;

				lpColData[O_OBJECT_TYPE].ulPropTag = PR_OBJECT_TYPE;
				lpColData[O_OBJECT_TYPE].Value.ul = MAPI_MAILUSER;

				lpColData[O_ADDRTYPE].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_ADDRTYPE], PROP_TYPE(props[I_NAMEDSTART+ulOffset+1].ulPropTag));
				lpColData[O_ADDRTYPE].Value = props[I_NAMEDSTART+ulOffset+1].Value;
			} else if (((ulFlags & MAPI_UNICODE) && wcscasecmp(props[I_MESSAGE_CLASS].Value.lpszW, L"IPM.DistList") == 0) ||
			    ((ulFlags & MAPI_UNICODE) == 0 && strcasecmp(props[I_MESSAGE_CLASS].Value.lpszA, "IPM.DistList") == 0)) {
				lpColData[O_DISPLAY_TYPE].ulPropTag = PR_DISPLAY_TYPE;
				lpColData[O_DISPLAY_TYPE].Value.ul = DT_PRIVATE_DISTLIST;

				lpColData[O_OBJECT_TYPE].ulPropTag = PR_OBJECT_TYPE;
				lpColData[O_OBJECT_TYPE].Value.ul = MAPI_DISTLIST;

				lpColData[O_ADDRTYPE].ulPropTag = PR_ADDRTYPE_W;
				lpColData[O_ADDRTYPE].Value.lpszW = const_cast<wchar_t *>(L"MAPIPDL");
			} else {
				continue;
			}

			// divide by 5 since a block of properties on a contact is a set of 5 (see mnNamedProps above)
			memory_ptr<ENTRYID> wrapped_eid;
			hr = MakeWrappedEntryID(props[I_ENTRYID].Value.bin.cb, reinterpret_cast<ENTRYID *>(props[I_ENTRYID].Value.bin.lpb),
									lpColData[O_OBJECT_TYPE].Value.ul, ulOffset/5,
						&lpColData[O_ENTRYID].Value.bin.cb, &~wrapped_eid);
			if (hr != hrSuccess)
				return hr;
			lpColData[O_ENTRYID].Value.bin.lpb = reinterpret_cast<BYTE *>(wrapped_eid.get());
			lpColData[O_ENTRYID].ulPropTag = PR_ENTRYID;

			ulOffset += I_NAMEDSTART;

			lpColData[O_DISPLAY_NAME].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_DISPLAY_NAME], PROP_TYPE(props[ulOffset+0].ulPropTag));
			if (PROP_TYPE(lpColData[O_DISPLAY_NAME].ulPropTag) == PT_ERROR)
				// Email#Display not available, fallback to normal PR_DISPLAY_NAME
				lpColData[O_DISPLAY_NAME] = props[I_DISPLAY_NAME];
			else
				lpColData[O_DISPLAY_NAME].Value = props[ulOffset + 0].Value;

			lpColData[O_EMAIL_ADDRESS].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_EMAIL_ADDRESS], PROP_TYPE(props[ulOffset+2].ulPropTag));
			if (PROP_TYPE(lpColData[O_EMAIL_ADDRESS].ulPropTag) == PT_ERROR)
				// Email#Address not available, fallback to normal PR_EMAIL_ADDRESS
				lpColData[O_EMAIL_ADDRESS] = props[I_EMAIL_ADDRESS];
			else
				lpColData[O_EMAIL_ADDRESS].Value = props[ulOffset+2].Value;

			lpColData[O_NORMALIZED_SUBJECT].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_NORMALIZED_SUBJECT], PROP_TYPE(props[ulOffset+3].ulPropTag));
			if (PROP_TYPE(lpColData[O_NORMALIZED_SUBJECT].ulPropTag) == PT_ERROR)
				// Email#OriginalDisplayName not available, fallback to normal PR_NORMALIZED_SUBJECT
				lpColData[O_NORMALIZED_SUBJECT] = props[I_NORMALIZED_SUBJECT];
			else
				lpColData[O_NORMALIZED_SUBJECT].Value = props[ulOffset+3].Value;

			lpColData[O_ORIGINAL_DISPLAY_NAME].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_ORIGINAL_DISPLAY_NAME], PROP_TYPE(props[I_DISPLAY_NAME].ulPropTag));
			lpColData[O_ORIGINAL_DISPLAY_NAME].Value = props[I_DISPLAY_NAME].Value;
			lpColData[O_ZC_ORIGINAL_ENTRYID].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_ZC_ORIGINAL_ENTRYID], PROP_TYPE(props[I_ENTRYID].ulPropTag));
			lpColData[O_ZC_ORIGINAL_ENTRYID].Value = props[I_ENTRYID].Value;
			lpColData[O_ZC_ORIGINAL_PARENT_ENTRYID].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_ZC_ORIGINAL_PARENT_ENTRYID], PROP_TYPE(props[I_PARENT_ENTRYID].ulPropTag));
			lpColData[O_ZC_ORIGINAL_PARENT_ENTRYID].Value = props[I_PARENT_ENTRYID].Value;
			lpColData[O_ZC_ORIGINAL_SOURCE_KEY].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_ZC_ORIGINAL_SOURCE_KEY], PROP_TYPE(props[I_SOURCE_KEY].ulPropTag));
			lpColData[O_ZC_ORIGINAL_SOURCE_KEY].Value = props[I_SOURCE_KEY].Value;
			lpColData[O_ZC_ORIGINAL_PARENT_SOURCE_KEY].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_ZC_ORIGINAL_PARENT_SOURCE_KEY], PROP_TYPE(props[I_PARENT_SOURCE_KEY].ulPropTag));
			lpColData[O_ZC_ORIGINAL_PARENT_SOURCE_KEY].Value = props[I_PARENT_SOURCE_KEY].Value;
			lpColData[O_ZC_ORIGINAL_CHANGE_KEY].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_ZC_ORIGINAL_CHANGE_KEY], PROP_TYPE(props[I_CHANGE_KEY].ulPropTag));
			lpColData[O_ZC_ORIGINAL_CHANGE_KEY].Value = props[I_CHANGE_KEY].Value;
			lpColData[O_COMPANY_NAME].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_COMPANY_NAME], PROP_TYPE(props[I_COMPANY_NAME].ulPropTag));
			lpColData[O_COMPANY_NAME].Value = props[I_COMPANY_NAME].Value;
			lpColData[O_PHONE_BUS].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_PHONE_BUS], PROP_TYPE(props[I_PHONE_BUS].ulPropTag));
			lpColData[O_PHONE_BUS].Value = props[I_PHONE_BUS].Value;
			lpColData[O_PHONE_MOB].ulPropTag = CHANGE_PROP_TYPE(outputCols.aulPropTag[O_PHONE_MOB], PROP_TYPE(props[I_PHONE_MOB].ulPropTag));
			lpColData[O_PHONE_MOB].Value = props[I_PHONE_MOB].Value;

			// @note, outlook seems to set the gab original search key (if possible, otherwise SMTP). The IMessage contact in the folder contains some unusable binary blob.
			if (PROP_TYPE(lpColData[O_ADDRTYPE].ulPropTag) == PT_STRING8 &&
			    PROP_TYPE(lpColData[O_EMAIL_ADDRESS].ulPropTag) == PT_STRING8)
				strSearchKey = strToUpper(std::string(lpColData[O_ADDRTYPE].Value.lpszA) + ":" + lpColData[O_EMAIL_ADDRESS].Value.lpszA);
			else if (PROP_TYPE(lpColData[O_ADDRTYPE].ulPropTag) == PT_UNICODE &&
			    PROP_TYPE(lpColData[O_EMAIL_ADDRESS].ulPropTag) == PT_UNICODE)
				strSearchKey = strToUpper(convert_to<std::string>(std::wstring(lpColData[O_ADDRTYPE].Value.lpszW) + L":" + lpColData[O_EMAIL_ADDRESS].Value.lpszW));
			else
				// e.g. distlists
				hr = MAPI_E_NOT_FOUND;
			if (hr == hrSuccess) {
				lpColData[O_SEARCH_KEY].ulPropTag = PR_SEARCH_KEY;
				lpColData[O_SEARCH_KEY].Value.bin.cb = strSearchKey.length()+1;
				lpColData[O_SEARCH_KEY].Value.bin.lpb = (BYTE*)strSearchKey.data();
			} else {
				lpColData[O_SEARCH_KEY].ulPropTag = CHANGE_PROP_TYPE(PR_SEARCH_KEY, PT_ERROR);
				lpColData[O_SEARCH_KEY].Value.ul = MAPI_E_NOT_FOUND;
			}

			lpColData[O_INSTANCE_KEY].ulPropTag = PR_INSTANCE_KEY;
			lpColData[O_INSTANCE_KEY].Value.bin.cb = sizeof(ULONG);
			lpColData[O_INSTANCE_KEY].Value.bin.lpb = (LPBYTE)&j;

			lpColData[O_ROWID].ulPropTag = PR_ROWID;
			lpColData[O_ROWID].Value.ul = j++;

			hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, lpColData, O_NCOLS);
			if (hr != hrSuccess)
				return hr;
		}
	}

done:
	AddChild(lpTable);
	hr = lpTable->HrGetView(createLocaleFromName(nullptr), ulFlags, &~lpTableView);
	if(hr != hrSuccess)
		return hr;
	return lpTableView->QueryInterface(IID_IMAPITable,
	       reinterpret_cast<void **>(lppTable));
#undef TCOLS
}

HRESULT ZCABContainer::GetDistListContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	SizedSPropTagArray(13, sptaCols) =
		{13, {PR_NULL /* reserve for PR_ROWID */, PR_ADDRTYPE,
		PR_DISPLAY_NAME, PR_DISPLAY_TYPE, PR_EMAIL_ADDRESS, PR_ENTRYID,
		PR_INSTANCE_KEY, PR_OBJECT_TYPE, PR_RECORD_KEY, PR_SEARCH_KEY,
		PR_SEND_INTERNET_ENCODING, PR_SEND_RICH_INFO,
		PR_TRANSMITABLE_DISPLAY_NAME}};
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;
	unsigned int ulObjType, cValues;
	SPropValue sKey;
	object_ptr<ZCMAPIProp> ptrZCMAPIProp;

	Util::proptag_change_unicode(ulFlags, sptaCols);
	auto hr = ECMemTable::Create(sptaCols, PR_ROWID, &~lpTable);
	if(hr != hrSuccess)
		return hr;

	// getprops, open real contacts, make table
	// Members "entryids" named property, see data layout below
	memory_ptr<SPropValue> ptrEntries;
	hr = HrGetOneProp(m_lpDistList, PROP_TAG(PT_MV_BINARY, PS_Address_to_static(dispidMembers)), &~ptrEntries);
	if (hr != hrSuccess)
		return hr;

	memset(&sKey, 0, sizeof(sKey));
	sKey.ulPropTag = PR_ROWID;
	for (ULONG i = 0; i < ptrEntries->Value.MVbin.cValues; ++i) {
		ULONG ulOffset = 0;
		BYTE cType = 0;

		// Wrapped entryids:
		// Flags: (ULONG) 0
		// Provider: (GUID) 0xC091ADD3519DCF11A4A900AA0047FAA4
		// Type: (BYTE) <value>, describes wrapped entryid
		//  lower 4 bits:
		//   0x00 = OneOff (use addressbook)
		//   0x03 = Contact (use folder / session?)
		//   0x04 = PDL  (use folder / session?)
		//   0x05 = GAB IMailUser (use addressbook)
		//   0x06 = GAB IDistList (use addressbook)
		//  next 3 bits: if lower is 0x03
		//   0x00 = business fax, or oneoff entryid
		//   0x10 = home fax
		//   0x20 = primary fax
		//   0x30 = no contact data
		//   0x40 = email 1
		//   0x50 = email 2
		//   0x60 = email 3
		//  top bit:
		//   0x80 default on, except for oneoff entryids

		// either WAB_GUID or ONE_OFF_MUID
		auto &tv = ptrEntries->Value.MVbin.lpbin[i];
		if (memcmp(tv.lpb + sizeof(ULONG), &WAB_GUID, sizeof(GUID)) == 0) {
			// handle wrapped entryids
			ulOffset = sizeof(ULONG) + sizeof(GUID) + sizeof(BYTE);
			cType = tv.lpb[sizeof(ULONG) + sizeof(GUID)];
		}
		object_ptr<IMAPIProp> ptrUser;
		hr = m_lpMAPISup->OpenEntry(tv.cb - ulOffset,
		     reinterpret_cast<ENTRYID *>(tv.lpb + ulOffset),
		     &iid_of(ptrUser), 0, &ulObjType, &~ptrUser);
		if (hr != hrSuccess)
			continue;

		memory_ptr<SPropValue> ptrProps;
		if ((cType & 0x80) && (cType & 0x0F) < 5 && (cType & 0x0F) > 0) {
			unsigned int cbEntryID, ulObjOffset = 0;
			memory_ptr<SPropValue> ptrPropEntryID;

			hr = HrGetOneProp(ptrUser, PR_ENTRYID, &~ptrPropEntryID);
			if (hr != hrSuccess)
				return hr;

			if ((cType & 0x0F) == 3) {
				ulObjType = MAPI_MAILUSER;
				ulObjOffset = cType >> 4;
			} else
				ulObjType = MAPI_DISTLIST;

			memory_ptr<ENTRYID> ptrEntryID;
			hr = MakeWrappedEntryID(ptrPropEntryID->Value.bin.cb, (LPENTRYID)ptrPropEntryID->Value.bin.lpb, ulObjType, ulObjOffset, &cbEntryID, &~ptrEntryID);
			if (hr != hrSuccess)
				return hr;
			hr = ZCMAPIProp::Create(ptrUser, cbEntryID, ptrEntryID, &~ptrZCMAPIProp);
			if (hr != hrSuccess)
				return hr;
			hr = ptrZCMAPIProp->GetProps(sptaCols, 0, &cValues, &~ptrProps);
			if (FAILED(hr))
				continue;
		} else {
			hr = ptrUser->GetProps(sptaCols, 0, &cValues, &~ptrProps);
			if (FAILED(hr))
				continue;
		}

		ptrProps[0] = sKey;

		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, ptrProps.get(), cValues);
		if (hr != hrSuccess)
			return hr;
		++sKey.Value.ul;
	}

	AddChild(lpTable);
	hr = lpTable->HrGetView(createLocaleFromName(nullptr), ulFlags, &~lpTableView);
	if(hr != hrSuccess)
		return hr;
	return lpTableView->QueryInterface(IID_IMAPITable,
	       reinterpret_cast<void **>(lppTable));
}

/**
 * Returns an addressbook contents table of the IPM.Contacts folder in m_lpContactFolder.
 *
 * @param[in] ulFlags MAPI_UNICODE for default unicode columns
 * @param[out] lppTable contents table of all items found in folder
 *
 * @return
 */
HRESULT ZCABContainer::GetContentsTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	if (m_lpDistList)
		return GetDistListContentsTable(ulFlags, lppTable);
	return GetFolderContentsTable(ulFlags, lppTable);
}

/**
 * Can return 3 kinds of tables:
 * 1. Root Container, contains one entry: the provider container
 * 2. Provider Container, contains user folders
 * 3. CONVENIENT_DEPTH: 1 + 2
 *
 * @param[in] ulFlags MAPI_UNICODE | CONVENIENT_DEPTH
 * @param[out] lppTable root container table
 *
 * @return MAPI Error code
 */
HRESULT ZCABContainer::GetHierarchyTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMemTable> lpTable;
	object_ptr<ECMemTableView> lpTableView;
	SizedSPropTagArray(9, sptaCols) = {9, {PR_ENTRYID, PR_STORE_ENTRYID, PR_DISPLAY_NAME_W, PR_OBJECT_TYPE, PR_CONTAINER_FLAGS, PR_DISPLAY_TYPE, PR_AB_PROVIDER_ID, PR_DEPTH, PR_INSTANCE_KEY}};
	enum { XENTRYID = 0, STORE_ENTRYID, DISPLAY_NAME, OBJECT_TYPE, CONTAINER_FLAGS, DISPLAY_TYPE, AB_PROVIDER_ID, DEPTH, INSTANCE_KEY, ROWID, XTCOLS };
	ULONG ulInstance = 0;

	if ((ulFlags & MAPI_UNICODE) == 0)
		sptaCols.aulPropTag[DISPLAY_NAME] = PR_DISPLAY_NAME_A;
	auto hr = ECMemTable::Create(sptaCols, PR_ROWID, &~lpTable);
	if(hr != hrSuccess)
		return hr;
	/*
	  1. root container		: m_lpFolders = NULL, m_lpContactFolder = NULL, m_lpDistList = NULL, m_lpProvider = ZCABLogon (one entry: provider)
	  2. provider container	: m_lpFolders = data, m_lpContactFolder = NULL, m_lpDistList = NULL, m_lpProvider = ZCABLogon (N entries: folders)
	  3. contact folder		: m_lpFolders = NULL, m_lpContactFolder = data, m_lpDistList = NULL, m_lpProvider = ZCABContainer from (2), (no hierarchy entries)
	  4. distlist			: m_lpFolders = NULL, m_lpContactFolder = NULL, m_lpDistList = data, m_lpProvider = ZCABContainer from (3), (no hierarchy entries)

	  when ulFlags contains CONVENIENT_DEPTH, (1) also contains (2)
	  so we open (2) through the provider, which gives the m_lpFolders
	*/

	if (m_lpFolders) {
		// create hierarchy with folders from user stores
		for (const auto &folder : *m_lpFolders) {
			memory_ptr<cabEntryID> lpEntryID;
			ULONG cbEntryID = CbNewCABENTRYID(folder.cbFolder);
			KPropbuffer<XTCOLS> sProps;

			hr = MAPIAllocateBuffer(cbEntryID, &~lpEntryID);
			if (hr != hrSuccess)
				return hr;

			memset(lpEntryID, 0, cbEntryID);
			memcpy(&lpEntryID->muid, &MUIDZCSAB, sizeof(MAPIUID));
			lpEntryID->ulObjType = MAPI_ABCONT;
			lpEntryID->ulOffset = 0;
			memcpy(lpEntryID->origEntryID, folder.lpFolder, folder.cbFolder);

			sProps[XENTRYID].ulPropTag = sptaCols.aulPropTag[XENTRYID];
			sProps[XENTRYID].Value.bin.cb = cbEntryID;
			sProps[XENTRYID].Value.bin.lpb = reinterpret_cast<BYTE *>(lpEntryID.get());
			sProps[STORE_ENTRYID].ulPropTag = CHANGE_PROP_TYPE(sptaCols.aulPropTag[STORE_ENTRYID], PT_ERROR);
			sProps[STORE_ENTRYID].Value.err = MAPI_E_NOT_FOUND;

			if ((ulFlags & MAPI_UNICODE) == 0)
				sProps.set(
					DISPLAY_NAME, PR_DISPLAY_NAME,
					convert_to<std::string>(folder.strwDisplayName));
			else
				sProps.set(DISPLAY_NAME, sptaCols.aulPropTag[DISPLAY_NAME], folder.strwDisplayName);

			sProps[OBJECT_TYPE].ulPropTag = sptaCols.aulPropTag[OBJECT_TYPE];
			sProps[OBJECT_TYPE].Value.ul = MAPI_ABCONT;

			sProps[CONTAINER_FLAGS].ulPropTag = sptaCols.aulPropTag[CONTAINER_FLAGS];
			sProps[CONTAINER_FLAGS].Value.ul = AB_RECIPIENTS | AB_UNMODIFIABLE | AB_UNICODE_OK;

			sProps[DISPLAY_TYPE].ulPropTag = sptaCols.aulPropTag[DISPLAY_TYPE];
			sProps[DISPLAY_TYPE].Value.ul = DT_NOT_SPECIFIC;

			sProps[AB_PROVIDER_ID].ulPropTag = sptaCols.aulPropTag[AB_PROVIDER_ID];
			sProps[AB_PROVIDER_ID].Value.bin.cb = sizeof(GUID);
			sProps[AB_PROVIDER_ID].Value.bin.lpb = (BYTE*)&MUIDZCSAB;

			sProps[DEPTH].ulPropTag = PR_DEPTH;
			sProps[DEPTH].Value.ul = (ulFlags & CONVENIENT_DEPTH) ? 1 : 0;

			sProps[INSTANCE_KEY].ulPropTag = PR_INSTANCE_KEY;
			sProps[INSTANCE_KEY].Value.bin.cb = sizeof(ULONG);
			sProps[INSTANCE_KEY].Value.bin.lpb = (LPBYTE)&ulInstance;

			sProps[ROWID].ulPropTag = PR_ROWID;
			sProps[ROWID].Value.ul = ulInstance;
			hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, nullptr, sProps.get(), XTCOLS);
			if (hr != hrSuccess)
				return hr;
			++ulInstance;
		}
	} else if (!m_lpContactFolder) {
		// only if not using a contacts folder, which should make the contents table. so this would return an empty hierarchy table, which is true.
		// create toplevel hierarchy. one entry: "Kopano Contacts Folders"
		BYTE sEntryID[4 + sizeof(GUID)]; // minimum sized entryid. no extra info needed
		SPropValue sProps[XTCOLS];

		memset(sEntryID, 0, sizeof(sEntryID));
		memcpy(sEntryID + 4, &MUIDZCSAB, sizeof(GUID));

		sProps[XENTRYID].ulPropTag = sptaCols.aulPropTag[XENTRYID];
		sProps[XENTRYID].Value.bin.cb = sizeof(sEntryID);
		sProps[XENTRYID].Value.bin.lpb = sEntryID;
		sProps[STORE_ENTRYID].ulPropTag = CHANGE_PROP_TYPE(sptaCols.aulPropTag[STORE_ENTRYID], PT_ERROR);
		sProps[STORE_ENTRYID].Value.err = MAPI_E_NOT_FOUND;

		sProps[DISPLAY_NAME].ulPropTag = sptaCols.aulPropTag[DISPLAY_NAME];
		if ((ulFlags & MAPI_UNICODE) == 0) {
			sProps[DISPLAY_NAME].ulPropTag = PR_DISPLAY_NAME_A;
			sProps[DISPLAY_NAME].Value.lpszA = KC_A("Kopano Contacts Folders");
		} else {
			sProps[DISPLAY_NAME].Value.lpszW = KC_W("Kopano Contacts Folders");
		}

		sProps[OBJECT_TYPE].ulPropTag = sptaCols.aulPropTag[OBJECT_TYPE];
		sProps[OBJECT_TYPE].Value.ul = MAPI_ABCONT;

		// AB_SUBCONTAINERS flag causes this folder to be skipped in the IAddrBook::GetSearchPath()
		sProps[CONTAINER_FLAGS].ulPropTag = sptaCols.aulPropTag[CONTAINER_FLAGS];
		sProps[CONTAINER_FLAGS].Value.ul = AB_SUBCONTAINERS | AB_UNMODIFIABLE | AB_UNICODE_OK;

		sProps[DISPLAY_TYPE].ulPropTag = sptaCols.aulPropTag[DISPLAY_TYPE];
		sProps[DISPLAY_TYPE].Value.ul = DT_NOT_SPECIFIC;

		sProps[AB_PROVIDER_ID].ulPropTag = sptaCols.aulPropTag[AB_PROVIDER_ID];
		sProps[AB_PROVIDER_ID].Value.bin.cb = sizeof(GUID);
		sProps[AB_PROVIDER_ID].Value.bin.lpb = (BYTE*)&MUIDZCSAB;

		sProps[DEPTH].ulPropTag = PR_DEPTH;
		sProps[DEPTH].Value.ul = 0;

		sProps[INSTANCE_KEY].ulPropTag = PR_INSTANCE_KEY;
		sProps[INSTANCE_KEY].Value.bin.cb = sizeof(ULONG);
		sProps[INSTANCE_KEY].Value.bin.lpb = (LPBYTE)&ulInstance;

		sProps[ROWID].ulPropTag = PR_ROWID;
		sProps[ROWID].Value.ul = ulInstance++;
		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, nullptr, sProps, XTCOLS);
		if (hr != hrSuccess)
			return hr;

		if (ulFlags & CONVENIENT_DEPTH) {
			object_ptr<IABContainer> ptrContainer;
			ULONG ulObjType;

			hr = ((ZCABLogon*)m_lpProvider)->OpenEntry(sizeof(sEntryID), reinterpret_cast<ENTRYID *>(sEntryID),
			     &iid_of(ptrContainer), 0, &ulObjType, &~ptrContainer);
			if (hr != hrSuccess)
				return hr;
			object_ptr<IMAPITable> ptrTable;
			hr = ptrContainer->GetHierarchyTable(ulFlags, &~ptrTable);
			if (hr != hrSuccess)
				return hr;
			rowset_ptr ptrRows;
			hr = ptrTable->QueryRows(INT_MAX, 0, &~ptrRows);
			if (hr != hrSuccess)
				return hr;

			for (rowset_ptr::size_type i = 0; i < ptrRows.size(); ++i) {
				// use PR_STORE_ENTRYID field to set instance key, since that is always MAPI_E_NOT_FOUND (see above)
				auto lpProp = ptrRows[i].find(CHANGE_PROP_TYPE(PR_STORE_ENTRYID, PT_ERROR));
				if (lpProp == nullptr)
					continue;
				lpProp->ulPropTag = PR_ROWID;
				lpProp->Value.ul = ulInstance++;

				hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, ptrRows[i].lpProps, ptrRows[i].cValues);
				if (hr != hrSuccess)
					return hr;
			}
		}
	}

	AddChild(lpTable);
	hr = lpTable->HrGetView(createLocaleFromName(nullptr), ulFlags, &~lpTableView);
	if(hr != hrSuccess)
		return hr;
	return lpTableView->QueryInterface(IID_IMAPITable,
	       reinterpret_cast<void **>(lppTable));
}

/**
 * Opens the contact from any given contact folder, and makes a ZCMAPIProp object for that contact.
 *
 * @param[in] cbEntryID wrapped contact entryid bytes
 * @param[in] lpEntryID
 * @param[in] lpInterface requested interface
 * @param[in] ulFlags unicode flags
 * @param[out] lpulObjType returned object type
 * @param[out] lppUnk returned object
 *
 * @return MAPI Error code
 */
HRESULT ZCABContainer::OpenEntry(ULONG cbEntryID, const ENTRYID *lpEntryID,
    const IID *lpInterface, ULONG ulFlags, ULONG *lpulObjType,
    IUnknown **lppUnk)
{
	HRESULT hr = hrSuccess;
	auto lpCABEntryID = reinterpret_cast<const cabEntryID *>(lpEntryID);
	unsigned int cbNewCABEntryID = CbNewCABENTRYID(0), cbFolder = 0, ulObjType = 0;
	LPENTRYID lpFolder = NULL;
	object_ptr<ZCABContainer> lpZCABContacts;
	object_ptr<ZCMAPIProp> lpZCMAPIProp;

	if (cbEntryID < cbNewCABEntryID || lpEntryID == nullptr ||
	    memcmp(&lpCABEntryID->muid, &MUIDZCSAB, sizeof(MAPIUID)) != 0)
		return MAPI_E_UNKNOWN_ENTRYID;

	if (m_lpDistList)
		// there is nothing to open from the distlist point of view
		// @todo, passthrough to IMAPISupport object?
		return MAPI_E_NO_SUPPORT;

	cbFolder = cbEntryID - cbNewCABEntryID;
	lpFolder = (LPENTRYID)((LPBYTE)lpEntryID + cbNewCABEntryID);

	if (lpCABEntryID->ulObjType == MAPI_ABCONT) {
		object_ptr<IMAPIFolder> ptrContactFolder;
		hr = m_lpMAPISup->OpenEntry(cbFolder, lpFolder, &iid_of(ptrContactFolder), 0, &ulObjType, &~ptrContactFolder);
		if (hr == MAPI_E_NOT_FOUND) {
			// the folder is most likely in a store that is not yet available through this MAPI session
			// try opening the store through the support object, and see if we can get it anyway
			object_ptr<IMAPIGetSession> ptrGetSession;

			hr = m_lpMAPISup->QueryInterface(IID_IMAPIGetSession, &~ptrGetSession);
			if (hr != hrSuccess)
				return hr;
			object_ptr<IMAPISession> ptrSession;
			hr = ptrGetSession->GetMAPISession(&~ptrSession);
			if (hr != hrSuccess)
				return hr;

			std::vector<zcabFolderEntry>::iterator i;
			// find the store of this folder
			for (i = m_lpFolders->begin();
			     i != m_lpFolders->cend(); ++i) {
				ULONG res;
				if (m_lpMAPISup->CompareEntryIDs(i->cbFolder, reinterpret_cast<ENTRYID *>(i->lpFolder),
				    cbFolder, lpFolder, 0, &res) == hrSuccess && res == true)
					break;
			}
			if (i == m_lpFolders->cend())
				return MAPI_E_NOT_FOUND;
			object_ptr<IMsgStore> ptrStore;
			ulock_normal lk(m_storemap_lock);
			if (i->store == nullptr) {
				hr = ptrSession->OpenMsgStore(0, i->cbStore, reinterpret_cast<ENTRYID *>(i->lpStore), nullptr, 0, &~i->store);
				if (hr != hrSuccess)
					return hr;
			}
			hr = i->store->OpenEntry(cbFolder, lpFolder, &iid_of(ptrContactFolder), 0, &ulObjType, &~ptrContactFolder);
		}
		if (hr != hrSuccess)
			return hr;
		hr = ZCABContainer::Create(nullptr, ptrContactFolder, m_lpMAPISup, m_lpProvider, &~lpZCABContacts);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpZCABContacts);
		hr = lpZCABContacts->QueryInterface(lpInterface != nullptr ? *lpInterface : IID_IABContainer, reinterpret_cast<void **>(lppUnk));
	} else if (lpCABEntryID->ulObjType == MAPI_DISTLIST) {
		// open the Original Message
		object_ptr<IMessage> ptrContact;
		hr = m_lpMAPISup->OpenEntry(cbFolder, lpFolder, &iid_of(ptrContact), 0, &ulObjType, &~ptrContact);
		if (hr != hrSuccess)
			return hr;
		hr = ZCABContainer::Create(ptrContact, cbEntryID, lpEntryID, m_lpMAPISup, &~lpZCABContacts);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpZCABContacts);
		hr = lpZCABContacts->QueryInterface(lpInterface != nullptr ? *lpInterface : IID_IDistList, reinterpret_cast<void **>(lppUnk));
	} else if (lpCABEntryID->ulObjType == MAPI_MAILUSER) {
		// open the Original Message
		object_ptr<IMessage> ptrContact;
		hr = m_lpMAPISup->OpenEntry(cbFolder, lpFolder, &iid_of(ptrContact), 0, &ulObjType, &~ptrContact);
		if (hr != hrSuccess)
			return hr;
		hr = ZCMAPIProp::Create(ptrContact, cbEntryID, lpEntryID, &~lpZCMAPIProp);
		if (hr != hrSuccess)
			return hr;
		AddChild(lpZCMAPIProp);
		hr = lpZCMAPIProp->QueryInterface(lpInterface != nullptr ? *lpInterface : IID_IMailUser, reinterpret_cast<void **>(lppUnk));
	} else {
		return MAPI_E_UNKNOWN_ENTRYID;
	}

	if (lpulObjType != nullptr)
		*lpulObjType = lpCABEntryID->ulObjType;
	return hr;
}

/**
 * Resolve MAPI_UNRESOLVED items in lpAdrList and possibly add resolved
 *
 * @param[in] lpPropTagArray properties to be included in lpAdrList
 * @param[in] ulFlags EMS_AB_ADDRESS_LOOKUP | MAPI_UNICODE
 * @param[in,out] lpAdrList
 * @param[in,out] lpFlagList MAPI_AMBIGUOUS | MAPI_RESOLVED | MAPI_UNRESOLVED
 *
 * @return
 */
HRESULT ZCABContainer::ResolveNames(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags, LPADRLIST lpAdrList, LPFlagList lpFlagList)
{
	// only columns we can set from our contents table
	static constexpr SizedSPropTagArray(7, sptaDefault) =
		{7, {PR_ADDRTYPE_A, PR_DISPLAY_NAME_A, PR_DISPLAY_TYPE,
		PR_EMAIL_ADDRESS_A, PR_ENTRYID, PR_INSTANCE_KEY,
		PR_OBJECT_TYPE}};
	static constexpr SizedSPropTagArray(7, sptaUnicode) =
		{7, {PR_ADDRTYPE_W, PR_DISPLAY_NAME_W, PR_DISPLAY_TYPE,
		PR_EMAIL_ADDRESS_W, PR_ENTRYID, PR_INSTANCE_KEY,
		PR_OBJECT_TYPE}};
	ULONG i;
	rowset_ptr ptrRows;

	if (lpPropTagArray == NULL)
		lpPropTagArray = (ulFlags & MAPI_UNICODE) ? sptaUnicode : sptaDefault;

	// in this container table, find given PR_DISPLAY_NAME
	if (m_lpFolders) {
		// return MAPI_E_NO_SUPPORT ? since you should not query on this level
		// @todo search in all folders, loop over self, since we want this providers entry ids
		if (m_lpFolders->empty())
			return hrSuccess;
		object_ptr<IMAPITable> ptrHierarchy;
		auto hr = GetHierarchyTable(0, &~ptrHierarchy);
		if (hr != hrSuccess)
			return hr;
		hr = ptrHierarchy->QueryRows(m_lpFolders->size(), 0, &~ptrRows);
		if (hr != hrSuccess)
			return hr;

		for (i = 0; i < ptrRows.size(); ++i) {
			auto lpEntryID = ptrRows[i].cfind(PR_ENTRYID);
			ULONG ulObjType;

			if (!lpEntryID)
				continue;

			// this? provider?
			object_ptr<IABContainer> ptrContainer;
			hr = OpenEntry(lpEntryID->Value.bin.cb, reinterpret_cast<ENTRYID *>(lpEntryID->Value.bin.lpb),
			     &iid_of(ptrContainer), 0, &ulObjType, &~ptrContainer);
			if (hr != hrSuccess)
				return hr;
			hr = ptrContainer->ResolveNames(lpPropTagArray, ulFlags, lpAdrList, lpFlagList);
			if (hr != hrSuccess)
				return hr;
		}
	} else if (m_lpContactFolder) {
		// only search within this folder and set entries in lpAdrList / lpFlagList
		std::set<ULONG> stProps;

		// make joint proptags
		std::copy(lpPropTagArray->aulPropTag, lpPropTagArray->aulPropTag + lpPropTagArray->cValues, std::inserter(stProps, stProps.begin()));
		for (i = 0; i < lpAdrList->aEntries[0].cValues; ++i)
			stProps.emplace(lpAdrList->aEntries[0].rgPropVals[i].ulPropTag);
		memory_ptr<SPropTagArray> ptrColumns;
		auto hr = MAPIAllocateBuffer(CbNewSPropTagArray(stProps.size()), &~ptrColumns);
		if (hr != hrSuccess)
			return hr;
		ptrColumns->cValues = stProps.size();
		std::copy(stProps.begin(), stProps.end(), ptrColumns->aulPropTag);

		object_ptr<IMAPITable> ptrContents;
		hr = GetContentsTable(ulFlags & MAPI_UNICODE, &~ptrContents);
		if (hr != hrSuccess)
			return hr;
		hr = ptrContents->SetColumns(ptrColumns, 0);
		if (hr != hrSuccess)
			return hr;

		for (i = 0; i < lpAdrList->cEntries; ++i) {
			auto lpDisplayNameA = lpAdrList->aEntries[i].cfind(PR_DISPLAY_NAME_A);
			auto lpDisplayNameW = lpAdrList->aEntries[i].cfind(PR_DISPLAY_NAME_W);
			if (!lpDisplayNameA && !lpDisplayNameW)
				continue;

			ULONG ulResFlag = (ulFlags & EMS_AB_ADDRESS_LOOKUP) ? FL_FULLSTRING : FL_PREFIX | FL_IGNORECASE;
			ULONG ulStringType = lpDisplayNameW ? PT_UNICODE : PT_STRING8;
			SPropValue sProp = lpDisplayNameW ? *lpDisplayNameW : *lpDisplayNameA;

			ECOrRestriction resFind;
			static const ULONG ulSearchTags[] = {PR_DISPLAY_NAME, PR_EMAIL_ADDRESS, PR_ORIGINAL_DISPLAY_NAME};

			for (size_t j = 0; j < ARRAY_SIZE(ulSearchTags); ++j) {
				sProp.ulPropTag = CHANGE_PROP_TYPE(ulSearchTags[j], ulStringType);
				resFind += ECContentRestriction(ulResFlag, CHANGE_PROP_TYPE(ulSearchTags[j], ulStringType), &sProp, ECRestriction::Cheap);
			}

			hr = resFind.RestrictTable(ptrContents, 0);
			if (hr != hrSuccess)
				return hr;
			hr = ptrContents->QueryRows(INT_MAX, MAPI_UNICODE, &~ptrRows);
			if (hr != hrSuccess)
				return hr;

			if (ptrRows.size() == 1) {
				lpFlagList->ulFlag[i] = MAPI_RESOLVED;

				// release old row
				MAPIFreeBuffer(lpAdrList->aEntries[i].rgPropVals);
				lpAdrList->aEntries[i].rgPropVals = NULL;

				hr = Util::HrCopySRow(reinterpret_cast<SRow *>(&lpAdrList->aEntries[i]), &ptrRows[0], nullptr);
				if (hr != hrSuccess)
					return hr;
			} else if (ptrRows.size() > 1) {
				lpFlagList->ulFlag[i] = MAPI_AMBIGUOUS;
			}
		}
	} else {
		// not supported within MAPI_DISTLIST container
		return MAPI_E_NO_SUPPORT;
	}
	return hrSuccess;
}

// IMAPIProp for m_lpDistList
HRESULT ZCABContainer::GetProps(const SPropTagArray *lpPropTagArray,
    ULONG ulFlags, ULONG *lpcValues, SPropValue **lppPropArray)
{
	if (m_lpDistList != NULL)
		return m_lpDistList->GetProps(lpPropTagArray, ulFlags, lpcValues, lppPropArray);
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCABContainer::GetPropList(ULONG ulFlags, LPSPropTagArray *lppPropTagArray)
{
	if (m_lpDistList != NULL)
		return m_lpDistList->GetPropList(ulFlags, lppPropTagArray);
	return MAPI_E_NO_SUPPORT;
}
