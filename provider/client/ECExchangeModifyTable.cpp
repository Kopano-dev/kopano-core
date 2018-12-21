/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <utility>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include "WSUtil.h"
#include "WSTransport.h"
#include "SOAPUtils.h"
#include <sstream>
#include <mapi.h>
#include <mapidefs.h>
#include <mapiutil.h>
#include <kopano/Util.h>
#include "ECExchangeModifyTable.h"
#include <mapicode.h>
#include <edkguid.h>
#include <kopano/ECGuid.h>
#include <mapiguid.h>
#include "pcutil.hpp"
#include <kopano/charset/convert.h>
#include "utf8/unchecked.h"

using namespace KC;

static LPWSTR WTF1252_to_WCHAR(LPCSTR szWTF1252, LPVOID lpBase, convert_context *lpConverter)
{
	LPWSTR lpszResult = NULL;

	if (!szWTF1252)
		return NULL;

	std::string str1252;
	str1252.reserve(strlen(szWTF1252));

	while (*szWTF1252) {
		utf8::uint32_t cp = utf8::unchecked::next(szWTF1252);

		// Since the string was originally windows-1252, all code points
		// should be in the range 0 <= cp < 256.
		str1252.append(1, cp < 256 ? cp : '?');
	}

	// Now convert the windows-1252 string to proper UTF8.
	std::wstring strConverted;
	if (lpConverter)
		strConverted = lpConverter->convert_to<std::wstring>(str1252, rawsize(str1252), "WINDOWS-1252");
	else
		strConverted = convert_to<std::wstring>(str1252, rawsize(str1252), "WINDOWS-1252");

	auto hr = MAPIAllocateMore((strConverted.size() + 1) * sizeof(*lpszResult), lpBase, reinterpret_cast<void **>(&lpszResult));
	if (hr == hrSuccess)
		wcscpy(lpszResult, strConverted.c_str());
	return lpszResult;
}

ECExchangeModifyTable::ECExchangeModifyTable(ULONG ulUniqueTag,
    ECMemTable *table, ECMAPIProp *parent, ULONG ulStartUniqueId,
    ULONG ulFlags) :
	m_ulUniqueId(ulStartUniqueId), m_ulUniqueTag(ulUniqueTag),
	m_ulFlags(ulFlags), m_lpParent(parent), m_ecTable(table)
{}

HRESULT ECExchangeModifyTable::CreateACLTable(ECMAPIProp *lpParent,
    ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj)
{
	object_ptr<ECMemTable> lpecTable;
	ULONG ulUniqueId = 1;
	static constexpr const SizedSPropTagArray(4, sPropACLs) =
		{4, { PR_MEMBER_ID, PR_MEMBER_ENTRYID, PR_MEMBER_RIGHTS,
		PR_MEMBER_NAME}};

	// Although PR_RULE_ID is PT_I8, it does not matter, since the low count comes first in memory
	// This will break on a big-endian system though
	auto hr = ECMemTable::Create(sPropACLs, PR_MEMBER_ID, &~lpecTable);
	if (hr!=hrSuccess)
		return hr;
	hr = OpenACLS(lpParent, ulFlags, lpecTable, &ulUniqueId);
	if(hr != hrSuccess)
		return hr;
	hr = lpecTable->HrSetClean();
	if(hr != hrSuccess)
		return hr;
	return alloc_wrap<ECExchangeModifyTable>(PR_MEMBER_ID, lpecTable,
	       lpParent, ulUniqueId, ulFlags)
	       .as(IID_IExchangeModifyTable, lppObj);
}

HRESULT ECExchangeModifyTable::CreateRulesTable(ECMAPIProp *lpParent,
    ULONG ulFlags, LPEXCHANGEMODIFYTABLE *lppObj)
{
	object_ptr<IStream> lpRulesData;
	STATSTG statRulesData;
	unsigned int ulRead, ulRuleId = 1;
	object_ptr<ECMemTable> ecTable;
	static constexpr const SizedSPropTagArray(7, sPropRules) =
		{7, {PR_RULE_ID, PR_RULE_SEQUENCE, PR_RULE_STATE,
		PR_RULE_CONDITION, PR_RULE_ACTIONS, PR_RULE_USER_FLAGS,
		PR_RULE_PROVIDER}};

	// Although PR_RULE_ID is PT_I8, it does not matter, since the low count comes first in memory
	// This will break on a big-endian system though
	auto hr = ECMemTable::Create(sPropRules, PR_RULE_ID, &~ecTable);
	if (hr!=hrSuccess)
		return hr;

	// PR_RULES_DATA can grow quite large. GetProps() only supports until size 8192, larger is not returned
	if (lpParent != nullptr &&
	    lpParent->OpenProperty(PR_RULES_DATA, &IID_IStream, 0, 0, &~lpRulesData) == hrSuccess) {
		lpRulesData->Stat(&statRulesData, 0);
		auto szXML = make_unique_nt<char[]>(statRulesData.cbSize.LowPart + 1);
		if (szXML == nullptr)
			return MAPI_E_NOT_ENOUGH_MEMORY;
		// TODO: Loop to read all data?
		hr = lpRulesData->Read(szXML.get(), statRulesData.cbSize.LowPart, &ulRead);
		if (hr != hrSuccess || ulRead == 0)
			goto empty;
		szXML[ulRead] = 0;
		if (ulRead < statRulesData.cbSize.LowPart)
			ec_log_notice("Bug: PR_RULES_DATA: read only %u/%u bytes",
				ulRead, statRulesData.cbSize.LowPart);
		hr = HrDeserializeTable(szXML.get(), ecTable, &ulRuleId);
		if (hr == MAPI_E_CORRUPT_DATA)
			ec_log_debug("PR_RULES_DATA [%u/%u bytes]: rejected due to garbage or truncation",
				ulRead, statRulesData.cbSize.LowPart);
		/*
		 * If the data was corrupted, or imported from
		 * Exchange, it is incompatible, so return an
		 * empty table.
		 */
		if (hr != hrSuccess) {
			ecTable->HrClear(); // just to be sure
			goto empty;
		}
	}

empty:
	hr = ecTable->HrSetClean();
	if(hr != hrSuccess)
		return hr;
	return alloc_wrap<ECExchangeModifyTable>(PR_RULE_ID, ecTable, lpParent,
	       ulRuleId, ulFlags).as(IID_IExchangeModifyTable, lppObj);
}

HRESULT ECExchangeModifyTable::QueryInterface(REFIID refiid, void **lppInterface) {
	REGISTER_INTERFACE2(ECExchangeModifyTable, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IECExchangeModifyTable, this);
	REGISTER_INTERFACE2(IExchangeModifyTable, this);
	REGISTER_INTERFACE2(IUnknown, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ECExchangeModifyTable::GetLastError(HRESULT hResult, ULONG ulFlags,
    LPMAPIERROR *lppMAPIError)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ECExchangeModifyTable::GetTable(ULONG ulFlags, LPMAPITABLE *lppTable)
{
	object_ptr<ECMemTableView> lpView;
	HRESULT hr = m_ecTable->HrGetView(createLocaleFromName(""), m_ulFlags, &~lpView);
	if(hr != hrSuccess)
		return hr;
	return lpView->QueryInterface(IID_IMAPITable,
	       reinterpret_cast<void **>(lppTable));
}

HRESULT ECExchangeModifyTable::ModifyTable(ULONG ulFlags, LPROWLIST lpMods)
{
	SPropValue		sRowId;
	LPSPropValue	lpProps = NULL;
	memory_ptr<SPropValue> lpPropRemove;
	unsigned int cValues = 0, ulFlagsRow = 0;
	SPropValue		sPropXML;

	if(ulFlags == ROWLIST_REPLACE) {
		auto hr = m_ecTable->HrDeleteAll();
		if(hr != hrSuccess)
			return hr;
	}

	for (unsigned int i = 0; i < lpMods->cEntries; ++i) {
		switch(lpMods->aEntries[i].ulRowFlags) {
		case ROW_ADD:
		case ROW_MODIFY: {
			// Note: the ECKeyTable only uses an ULONG as the key.
			//       Information placed in the HighPart of this PT_I8 is lost!
			auto lpFind = PCpropFindProp(lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues, m_ulUniqueTag);
			if (lpFind == NULL) {
				sRowId.ulPropTag = m_ulUniqueTag;
				sRowId.Value.li.QuadPart = m_ulUniqueId++;
				auto hr = Util::HrAddToPropertyArray(lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues, &sRowId, &~lpPropRemove, &cValues);
				if(hr != hrSuccess)
					return hr;
				lpProps = lpPropRemove;
			} else {
				lpProps = lpMods->aEntries[i].rgPropVals;
				cValues = lpMods->aEntries[i].cValues;
			}
			if (lpMods->aEntries[i].ulRowFlags == ROW_ADD)
				ulFlagsRow = ECKeyTable::TABLE_ROW_ADD;
			else
				ulFlagsRow = ECKeyTable::TABLE_ROW_MODIFY;

			auto hr = m_ecTable->HrModifyRow(ulFlagsRow, lpFind, lpProps, cValues);
			if(hr != hrSuccess)
				return hr;
			break;
		}
		case ROW_REMOVE: {
			auto hr = m_ecTable->HrModifyRow(ECKeyTable::TABLE_ROW_DELETE, NULL, lpMods->aEntries[i].rgPropVals, lpMods->aEntries[i].cValues);
			if(hr != hrSuccess)
				return hr;
			break;
		}
		case ROW_EMPTY:
			break;
		}
	}

	// Do not push the data to the server
	if (!m_bPushToServer)
		return m_ecTable->HrSetClean();

	// The data has changed now, so save the data in the parent folder
	if(m_ulUniqueTag == PR_RULE_ID)
	{
		char *xml = nullptr;
		auto hr = HrSerializeTable(m_ecTable, &xml);
		std::unique_ptr<char[]> szXML(xml);
		if(hr != hrSuccess)
			return hr;
		sPropXML.ulPropTag = PR_RULES_DATA;
		sPropXML.Value.bin.lpb = reinterpret_cast<BYTE *>(szXML.get());
		sPropXML.Value.bin.cb = strlen(szXML.get());

		hr = m_lpParent->SetProps(1, &sPropXML, NULL);
		if(hr != hrSuccess)
			return hr;
	} else if (m_ulUniqueTag == PR_MEMBER_ID) {
		auto hr = SaveACLS(m_lpParent, m_ecTable);
		if(hr != hrSuccess)
			return hr;
		// FIXME: if username not exist, just resolve
	} else {
		assert(false);
		return MAPI_E_CALL_FAILED;
	}
	// Mark all as saved
	return m_ecTable->HrSetClean();
}

HRESULT ECExchangeModifyTable::OpenACLS(ECMAPIProp *lpecMapiProp, ULONG ulFlags, ECMemTable *lpTable, ULONG *lpulUniqueID)
{
	if (lpecMapiProp == nullptr || lpTable == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	object_ptr<IECSecurity> lpSecurity;
	memory_ptr<ECPERMISSION> lpECPerms;
	SPropValue	lpsPropMember[4];
	WCHAR* lpMemberName = NULL;
	unsigned int ulUserid = 0, cPerms = 0;
	auto hr = lpecMapiProp->QueryInterface(IID_IECSecurity, &~lpSecurity);
	if (hr != hrSuccess)
		return hr;
	hr = lpSecurity->GetPermissionRules(ACCESS_TYPE_GRANT, &cPerms, &~lpECPerms);
	if (hr != hrSuccess)
		return hr;

	// Default exchange PR_MEMBER_ID ids
	//  0 = default acl
	// -1 = Anonymous acl
	for (ULONG i = 0; i < cPerms; ++i) {
		if (lpECPerms[i].ulType != ACCESS_TYPE_GRANT)
			continue;

		memory_ptr<ECUSER> lpECUser;
		memory_ptr<ECGROUP> lpECGroup;

		if (lpecMapiProp->GetMsgStore()->lpTransport->HrGetUser(lpECPerms[i].sUserId.cb, (LPENTRYID)lpECPerms[i].sUserId.lpb, MAPI_UNICODE, &~lpECUser) != hrSuccess &&
		    lpecMapiProp->GetMsgStore()->lpTransport->HrGetGroup(lpECPerms[i].sUserId.cb, (LPENTRYID)lpECPerms[i].sUserId.lpb, MAPI_UNICODE, &~lpECGroup) != hrSuccess)
			continue;

		if (lpECGroup != nullptr)
			lpMemberName = (LPTSTR)((lpECGroup->lpszFullname)?lpECGroup->lpszFullname:lpECGroup->lpszGroupname);
		else
			lpMemberName = (LPTSTR)((lpECUser->lpszFullName)?lpECUser->lpszFullName:lpECUser->lpszUsername);

		lpsPropMember[0].ulPropTag = PR_MEMBER_ID;
		if (ABEntryIDToID(lpECPerms[i].sUserId.cb, lpECPerms[i].sUserId.lpb, &ulUserid, nullptr, nullptr) == erSuccess && ulUserid == 1)
			lpsPropMember[0].Value.li.QuadPart= 0; //everyone / exchange default
		else
			lpsPropMember[0].Value.li.QuadPart= (*lpulUniqueID)++;

		lpsPropMember[1].ulPropTag = PR_MEMBER_RIGHTS;
		lpsPropMember[1].Value.ul = lpECPerms[i].ulRights;
		lpsPropMember[2].ulPropTag = PR_MEMBER_NAME;
		lpsPropMember[2].Value.lpszW = (WCHAR*)lpMemberName;
		lpsPropMember[3].ulPropTag = PR_MEMBER_ENTRYID;
		lpsPropMember[3].Value.bin = lpECPerms[i].sUserId;
		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &lpsPropMember[0], lpsPropMember, 4);
		if(hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT ECExchangeModifyTable::DisablePushToServer()
{
	m_bPushToServer = false;
	return hrSuccess;
}

HRESULT ECExchangeModifyTable::SaveACLS(ECMAPIProp *lpecMapiProp, ECMemTable *lpTable)
{
	rowset_ptr lpRowSet;
	memory_ptr<SPropValue> lpIDs;
	memory_ptr<ULONG> lpulStatus;
	memory_ptr<ECPERMISSION> lpECPermissions;
	ULONG			cECPerm = 0;
	entryId sEntryId;
	object_ptr<IECSecurity> lpSecurity;

	// Get the ACLS
	auto hr = lpecMapiProp->QueryInterface(IID_IECSecurity, &~lpSecurity);
	if (hr != hrSuccess)
		return hr;
	// Get a data
	hr = lpTable->HrGetAllWithStatus(&~lpRowSet, &~lpIDs, &~lpulStatus);
	if (hr != hrSuccess)
		return hr;
	hr = MAPIAllocateBuffer(sizeof(ECPERMISSION)*lpRowSet->cRows, &~lpECPermissions);
	if (hr != hrSuccess)
		return hr;

	for (ULONG i = 0; i < lpRowSet->cRows; ++i) {
		if (lpulStatus[i]  == ECROW_NORMAL)
			continue;

		lpECPermissions[cECPerm].ulState = RIGHT_AUTOUPDATE_DENIED;
		lpECPermissions[cECPerm].ulType = ACCESS_TYPE_GRANT;

		if (lpulStatus[i] == ECROW_DELETED)
			lpECPermissions[cECPerm].ulState |= RIGHT_DELETED;
		else if (lpulStatus[i] == ECROW_ADDED)
			lpECPermissions[cECPerm].ulState |= RIGHT_NEW;
		else if (lpulStatus[i] == ECROW_MODIFIED)
			lpECPermissions[cECPerm].ulState |= RIGHT_MODIFY;

		auto lpMemberID = lpRowSet[i].cfind(PR_MEMBER_ID);
		auto lpMemberEntryID = lpRowSet[i].cfind(PR_MEMBER_ENTRYID);
		auto lpMemberRights = lpRowSet[i].cfind(PR_MEMBER_RIGHTS);
		if (lpMemberID == NULL || lpMemberRights == NULL || (lpMemberID->Value.ul != 0 && lpMemberEntryID == NULL))
			continue;

		if (lpMemberID->Value.ul != 0) {
			lpECPermissions[cECPerm].sUserId = lpMemberEntryID->Value.bin;
		} else {
			// Create everyone entryid
			// NOTE: still makes a V0 entry id, because externid id part is empty
			if (ABIDToEntryID(nullptr, 1, objectid_t(DISTLIST_GROUP), &sEntryId) != erSuccess)
				return MAPI_E_CALL_FAILED;

			lpECPermissions[cECPerm].sUserId.cb = sEntryId.__size;
			hr = KAllocCopy(sEntryId.__ptr, sEntryId.__size, reinterpret_cast<void **>(&lpECPermissions[cECPerm].sUserId.lpb), lpECPermissions);
			if (hr != hrSuccess)
				return hr;
			FreeEntryId(&sEntryId, false);
		}

		lpECPermissions[cECPerm].ulRights = lpMemberRights->Value.ul&ecRightsAll;
		++cECPerm;
	}

	if (cECPerm > 0)
		hr = lpSecurity->SetPermissionRules(cECPerm, lpECPermissions);
	return hr;
}

// Serializes the rules ECMemTable data into an XML stream.
HRESULT	ECExchangeModifyTable::HrSerializeTable(ECMemTable *lpTable, char **lppSerialized)
{
	object_ptr<ECMemTableView> lpView;
	memory_ptr<SPropTagArray> lpCols;
	rowset_ptr lpRowSet;
	std::ostringstream os;
	struct rowSet *	lpSOAPRowSet = NULL;
	char *szXML = NULL;
	struct soap soap;

	auto laters = make_scope_success([&]() {
		if(lpSOAPRowSet)
			FreeRowSet(lpSOAPRowSet, true);
		soap_destroy(&soap);
		soap_end(&soap); // clean up allocated temporaries
	});

	// Get a view
	auto hr = lpTable->HrGetView(createLocaleFromName(""), MAPI_UNICODE, &~lpView);
	if(hr != hrSuccess)
		return hr;
	// Get all Columns
	hr = lpView->QueryColumns(TBL_ALL_COLUMNS, &~lpCols);
	if(hr != hrSuccess)
		return hr;
	hr = lpView->SetColumns(lpCols, 0);
	if(hr != hrSuccess)
		return hr;
	// Get all rows
	hr = lpView->QueryRows(0x7fffffff, 0, &~lpRowSet);
	if(hr != hrSuccess)
		return hr;
	// we need to convert data from clients which save PT_STRING8 inside PT_SRESTRICTION and PT_ACTIONS structures,
	// because unicode clients won't be able to understand those anymore.
	hr = ConvertString8ToUnicode(lpRowSet.get());
	if(hr != hrSuccess)
		return hr;
	// Convert to SOAP rows
	hr = CopyMAPIRowSetToSOAPRowSet(lpRowSet.get(), &lpSOAPRowSet);
	if(hr != hrSuccess)
		return hr;

	// Convert to XML
	soap_set_omode(&soap, SOAP_C_UTFSTRING);
	soap_begin(&soap);
	soap.os = &os;
	soap_serialize_rowSet(&soap, lpSOAPRowSet);
	if (soap_begin_send(&soap) != 0 ||
	    soap_put_rowSet(&soap, lpSOAPRowSet, "tableData", "rowSet") != 0 ||
	    soap_end_send(&soap) != 0)
		return MAPI_E_NETWORK_ERROR;

	// os now contains XML for row data
	szXML = new char [ os.str().size()+1 ];
	strcpy(szXML, os.str().c_str());
	szXML[os.str().size()] = 0;
	*lppSerialized = std::move(szXML);
	return hr;
}

// Deserialize the rules xml data to ECMemtable
HRESULT ECExchangeModifyTable::HrDeserializeTable(char *lpSerialized, ECMemTable *lpTable, ULONG *ulRuleId)
{
	std::istringstream is(lpSerialized);
	struct rowSet sSOAPRowSet;
	rowset_ptr lpsRowSet;
	unsigned int cValues, ulHighestRuleID = 1;
	SPropValue		sRowId;
	struct soap soap;
	convert_context converter;

	auto laters = make_scope_success([&]() {
		soap_destroy(&soap);
		soap_end(&soap); // clean up allocated temporaries
	});

	soap.is = &is;
	soap_set_imode(&soap, SOAP_C_UTFSTRING);
	soap_begin(&soap);
	if (soap_begin_recv(&soap) != 0)
		return MAPI_E_NETWORK_FAILURE;
	if (!soap_get_rowSet(&soap, &sSOAPRowSet, "tableData", "rowSet"))
		return MAPI_E_CORRUPT_DATA;
	if (soap_end_recv(&soap) != 0)
		return MAPI_E_NETWORK_FAILURE;
	auto hr = CopySOAPRowSetToMAPIRowSet(NULL, &sSOAPRowSet, &~lpsRowSet, 0);
	if(hr != hrSuccess)
		return hr;

	for (unsigned int i = 0; i < lpsRowSet->cRows; ++i) {
		memory_ptr<SPropValue> lpProps;

		// Note: the ECKeyTable only uses an ULONG as the key.
		//       Information placed in the HighPart of this PT_I8 is lost!
		sRowId.ulPropTag = PR_RULE_ID;
		sRowId.Value.li.QuadPart = ulHighestRuleID++;
		hr = Util::HrAddToPropertyArray(lpsRowSet[i].lpProps, lpsRowSet[i].cValues, &sRowId, &~lpProps, &cValues);
		if(hr != hrSuccess)
			return hr;
		for (unsigned int n = 0; n < cValues; ++n) {
			/*
			 * If a string type is PT_STRING8, it is old and
			 * assumed to be in WTF-1252 (CP-1252 values directly
			 * transcoded into UTF-8).
			 */
			if (PROP_TYPE(lpProps[n].ulPropTag) == PT_STRING8) {
				lpProps[n].ulPropTag = CHANGE_PROP_TYPE(lpProps[n].ulPropTag, PT_UNICODE);
				lpProps[n].Value.lpszW = WTF1252_to_WCHAR(lpProps[n].Value.lpszA, lpProps, &converter);
			}
		}

		hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, &sRowId, lpProps, cValues);
		if(hr != hrSuccess)
			return hr;
	}
	*ulRuleId = ulHighestRuleID;
	return hr;
}
