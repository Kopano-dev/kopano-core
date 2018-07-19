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
#include <new>
#include "ZCMAPIProp.h"
#include "ZCABData.h"
#include <mapidefs.h>
#include <mapiguid.h>
#include <mapicode.h>
#include <mapiutil.h>
#include <kopano/Util.h>
#include <kopano/ECGuid.h>
#include <kopano/mapi_ptr.h>
#include <kopano/memory.hpp>
#include <kopano/namedprops.h>
#include <kopano/mapiguidext.h>

using namespace KC;

ZCMAPIProp::ZCMAPIProp(ULONG ulObjType, const char *szClassName) :
    ECUnknown(szClassName), m_ulObject(ulObjType)
{
}

ZCMAPIProp::~ZCMAPIProp()
{
	MAPIFreeBuffer(m_base);
}

#define ADD_PROP_OR_EXIT(dest, src, base, propid) {						\
	if (src) {															\
		hr = Util::HrCopyProperty(&dest, src, base);					\
		if (hr != hrSuccess)											\
			goto exitm; \
		dest.ulPropTag = propid;										\
		m_mapProperties.emplace(PROP_ID(propid), dest); \
		src = NULL;														\
	} \
}

/** 
 * Add properties required to make an IMailUser. Properties are the
 * same list as Outlook Contacts Provider does.
 * 
 * @param[in] cValues number of props in lpProps
 * @param[in] lpProps properties of the original contact
 * 
 * @return 
 */
HRESULT ZCMAPIProp::ConvertMailUser(LPSPropTagArray lpNames, ULONG cValues, LPSPropValue lpProps, ULONG ulIndex)
{
	HRESULT hr = hrSuccess;
	SPropValue sValue, sSource;
	std::string strSearchKey;
	convert_context converter;

	auto lpProp = PCpropFindProp(lpProps, cValues, PR_BODY);
	if (lpProp) {
		ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_BODY);
	} else {
		sValue.ulPropTag = PR_BODY;
		sValue.Value.lpszW = empty;
		m_mapProperties.emplace(PROP_ID(PR_BODY), sValue);
	}

	lpProp = PCpropFindProp(lpProps, cValues, PR_BUSINESS_ADDRESS_CITY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_BUSINESS_ADDRESS_CITY);
	lpProp = PCpropFindProp(lpProps, cValues, PR_BUSINESS_ADDRESS_STATE_OR_PROVINCE);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_BUSINESS_ADDRESS_STATE_OR_PROVINCE);
	lpProp = PCpropFindProp(lpProps, cValues, PR_BUSINESS_FAX_NUMBER);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_BUSINESS_FAX_NUMBER);
	lpProp = PCpropFindProp(lpProps, cValues, PR_COMPANY_NAME);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_COMPANY_NAME);

	if (lpNames)
		lpProp = PCpropFindProp(lpProps, cValues, CHANGE_PROP_TYPE(lpNames->aulPropTag[0], PT_UNICODE));
	if (!lpProp)
		lpProp = PCpropFindProp(lpProps, cValues, PR_DISPLAY_NAME);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_DISPLAY_NAME);

	sValue.ulPropTag = PR_DISPLAY_TYPE;
	sValue.Value.ul = DT_MAILUSER;
	m_mapProperties.emplace(PROP_ID(PR_DISPLAY_TYPE), sValue);

	if (lpNames)
		lpProp = PCpropFindProp(lpProps, cValues, CHANGE_PROP_TYPE(lpNames->aulPropTag[1], PT_UNICODE));
	if (!lpProp)
		lpProp = PCpropFindProp(lpProps, cValues, PR_ADDRTYPE);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ADDRTYPE);

	if (lpNames)
		lpProp = PCpropFindProp(lpProps, cValues, CHANGE_PROP_TYPE(lpNames->aulPropTag[2], PT_UNICODE));
	if (!lpProp)
		lpProp = PCpropFindProp(lpProps, cValues, PR_EMAIL_ADDRESS);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_EMAIL_ADDRESS);

	lpProp = PCpropFindProp(lpProps, cValues, PR_GIVEN_NAME);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_GIVEN_NAME);
	lpProp = PCpropFindProp(lpProps, cValues, PR_MIDDLE_NAME);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_MIDDLE_NAME);
	lpProp = PCpropFindProp(lpProps, cValues, PR_NORMALIZED_SUBJECT);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_NORMALIZED_SUBJECT);

	sValue.ulPropTag = PR_OBJECT_TYPE;
	sValue.Value.ul = MAPI_MAILUSER;
	m_mapProperties.emplace(PROP_ID(PR_OBJECT_TYPE), sValue);

	if (lpNames)
		lpProp = PCpropFindProp(lpProps, cValues, CHANGE_PROP_TYPE(lpNames->aulPropTag[3], PT_UNICODE));
	if (!lpProp)
		lpProp = PCpropFindProp(lpProps, cValues, PR_DISPLAY_NAME);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ORIGINAL_DISPLAY_NAME);

	if (lpNames)
		lpProp = PCpropFindProp(lpProps, cValues, CHANGE_PROP_TYPE(lpNames->aulPropTag[4], PT_BINARY));
	if (!lpProp)
		lpProp = PCpropFindProp(lpProps, cValues, PR_ENTRYID);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ORIGINAL_ENTRYID);

	lpProp = PCpropFindProp(lpProps, cValues, PR_RECORD_KEY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_RECORD_KEY);

	if (lpNames) {
		lpProp = PCpropFindProp(lpProps, cValues, CHANGE_PROP_TYPE(lpNames->aulPropTag[1], PT_UNICODE));
		if (lpProp) {
			strSearchKey += converter.convert_to<std::string>(lpProp->Value.lpszW) + ":";
		} else {
			strSearchKey += "SMTP:";
		}
		lpProp = PCpropFindProp(lpProps, cValues, CHANGE_PROP_TYPE(lpNames->aulPropTag[2], PT_UNICODE));
		if (lpProp)
			strSearchKey += converter.convert_to<std::string>(lpProp->Value.lpszW);

		sSource.ulPropTag = PR_SEARCH_KEY;
		sSource.Value.bin.cb = strSearchKey.size();
		sSource.Value.bin.lpb = (LPBYTE)strSearchKey.data();
		lpProp = &sSource;
		ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_SEARCH_KEY);
	}

	lpProp = PCpropFindProp(lpProps, cValues, PR_TITLE);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_TITLE);
	lpProp = PCpropFindProp(lpProps, cValues, PR_TRANSMITABLE_DISPLAY_NAME);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_TRANSMITABLE_DISPLAY_NAME);
	lpProp = PCpropFindProp(lpProps, cValues, PR_ENTRYID);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_ENTRYID);
	lpProp = PCpropFindProp(lpProps, cValues, PR_PARENT_ENTRYID);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_PARENT_ENTRYID);
	lpProp = PCpropFindProp(lpProps, cValues, PR_SOURCE_KEY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_SOURCE_KEY);
	lpProp = PCpropFindProp(lpProps, cValues, PR_PARENT_SOURCE_KEY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_PARENT_SOURCE_KEY);
	lpProp = PCpropFindProp(lpProps, cValues, PR_CHANGE_KEY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_CHANGE_KEY);
 exitm:
	return hr;
}

HRESULT ZCMAPIProp::ConvertDistList(ULONG cValues, LPSPropValue lpProps)
{
	HRESULT hr = hrSuccess;
	SPropValue sValue, sSource;

	sSource.ulPropTag = PR_ADDRTYPE;
	sSource.Value.lpszW = const_cast<wchar_t *>(L"MAPIPDL");
	const SPropValue *lpProp = &sSource;
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ADDRTYPE);

	lpProp = PCpropFindProp(lpProps, cValues, PR_DISPLAY_NAME);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_DISPLAY_NAME);

	sValue.ulPropTag = PR_DISPLAY_TYPE;
	sValue.Value.ul = DT_PRIVATE_DISTLIST;
	m_mapProperties.emplace(PROP_ID(PR_DISPLAY_TYPE), sValue);

	sValue.ulPropTag = PR_OBJECT_TYPE;
	sValue.Value.ul = MAPI_DISTLIST;
	m_mapProperties.emplace(PROP_ID(PR_OBJECT_TYPE), sValue);

	lpProp = PCpropFindProp(lpProps, cValues, PR_RECORD_KEY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_RECORD_KEY);

	// above are all the props present in the Outlook Contact Provider ..
	// but I need the props below too

	// one off members in 0x81041102
	lpProp = PCpropFindProp(lpProps, cValues, 0x81041102);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, 0x81041102);

	// real eid members in 0x81051102 (gab, maybe I only need this one)
	lpProp = PCpropFindProp(lpProps, cValues, 0x81051102);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, 0x81051102);
	lpProp = PCpropFindProp(lpProps, cValues, PR_ENTRYID);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_ENTRYID);
	lpProp = PCpropFindProp(lpProps, cValues, PR_PARENT_ENTRYID);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_PARENT_ENTRYID);
	lpProp = PCpropFindProp(lpProps, cValues, PR_SOURCE_KEY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_SOURCE_KEY);
	lpProp = PCpropFindProp(lpProps, cValues, PR_PARENT_SOURCE_KEY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_PARENT_SOURCE_KEY);
	lpProp = PCpropFindProp(lpProps, cValues, PR_CHANGE_KEY);
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ZC_ORIGINAL_CHANGE_KEY);
 exitm:
	return hr;
}

/** 
 * Get Props from the contact and remembers them is this object.
 * 
 * @param[in] lpContact 
 * @param[in] ulIndex index in named properties
 * 
 * @return 
 */
HRESULT ZCMAPIProp::ConvertProps(IMAPIProp *lpContact, ULONG cbEntryID,
    const ENTRYID *lpEntryID, ULONG ulIndex)
{
	HRESULT hr = hrSuccess;
	ULONG cValues = 0;
	SPropValuePtr ptrContactProps;
	SPropValue sValue, sSource;
	LPSPropValue lpProp = NULL;

	// named properties
	SPropTagArrayPtr ptrNameTags;
	memory_ptr<MAPINAMEID *> lppNames;
	ULONG ulNames = 5;
	MAPINAMEID mnNamedProps[5] = {
		{(LPGUID)&PSETID_Address, MNID_ID, {dispidEmail1DisplayName}},
		{(LPGUID)&PSETID_Address, MNID_ID, {dispidEmail1AddressType}},
		{(LPGUID)&PSETID_Address, MNID_ID, {dispidEmail1Address}},
		{(LPGUID)&PSETID_Address, MNID_ID, {dispidEmail1OriginalDisplayName}},
		{(LPGUID)&PSETID_Address, MNID_ID, {dispidEmail1OriginalEntryID}}
	};

	hr = MAPIAllocateBuffer(sizeof(LPMAPINAMEID) * ulNames, &~lppNames);
	if (hr != hrSuccess)
		goto exitm;

	if (ulIndex < 3) {
		for (ULONG i = 0; i < ulNames; ++i) {
			mnNamedProps[i].Kind.lID += (ulIndex * 0x10);
			lppNames[i] = &mnNamedProps[i];
		}
		hr = lpContact->GetIDsFromNames(ulNames, lppNames, MAPI_CREATE, &~ptrNameTags);
		if (FAILED(hr))
			goto exitm;
	}
	hr = lpContact->GetProps(NULL, MAPI_UNICODE, &cValues, &~ptrContactProps);
	if (FAILED(hr))
		goto exitm;
	hr = MAPIAllocateBuffer(sizeof(SPropValue), (void**)&m_base);
	if (hr != hrSuccess)
		goto exitm;

	sSource.ulPropTag = PR_ENTRYID;
	sSource.Value.bin.cb = cbEntryID;
	sSource.Value.bin.lpb = (LPBYTE)lpEntryID;
	lpProp = &sSource;
	ADD_PROP_OR_EXIT(sValue, lpProp, m_base, PR_ENTRYID);

	if (m_ulObject == MAPI_MAILUSER)
		hr = ConvertMailUser(ptrNameTags, cValues, ptrContactProps, ulIndex);
	else
		hr = ConvertDistList(cValues, ptrContactProps);

 exitm:
	return hr;
}

HRESULT ZCMAPIProp::Create(IMAPIProp *lpContact, ULONG cbEntryID,
    const ENTRYID *lpEntryID, ZCMAPIProp **lppZCMAPIProp)
{
	HRESULT	hr = hrSuccess;
	auto lpCABEntryID = reinterpret_cast<const cabEntryID *>(lpEntryID);

	if (lpCABEntryID->ulObjType != MAPI_MAILUSER && lpCABEntryID->ulObjType != MAPI_DISTLIST)
		return MAPI_E_INVALID_OBJECT;
	object_ptr<ZCMAPIProp> lpZCMAPIProp(new(std::nothrow) ZCMAPIProp(lpCABEntryID->ulObjType));
	if (lpZCMAPIProp == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	hr = lpZCMAPIProp->ConvertProps(lpContact, cbEntryID, lpEntryID, lpCABEntryID->ulOffset);
	if (hr != hrSuccess)
		return hr;
	*lppZCMAPIProp = lpZCMAPIProp.release();
	return hrSuccess;
}

HRESULT ZCMAPIProp::QueryInterface(REFIID refiid, void **lppInterface)
{
	REGISTER_INTERFACE2(ZCMAPIProp, this);
	REGISTER_INTERFACE2(ECUnknown, this);
	REGISTER_INTERFACE2(IMAPIProp, this);
	REGISTER_INTERFACE2(IUnknown, this);
	if (m_ulObject == MAPI_MAILUSER)
		REGISTER_INTERFACE2(IMailUser, this);
	return MAPI_E_INTERFACE_NOT_SUPPORTED;
}

HRESULT ZCMAPIProp::GetLastError(HRESULT hResult, ULONG ulFlags, LPMAPIERROR * lppMAPIError)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCMAPIProp::SaveChanges(ULONG ulFlags)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCMAPIProp::CopyOneProp(convert_context &converter, ULONG ulFlags,
    const std::map<short, SPropValue>::const_iterator &i, LPSPropValue lpProp,
    LPSPropValue lpBase)
{
	HRESULT hr;

	if ((ulFlags & MAPI_UNICODE) == 0 && PROP_TYPE(i->second.ulPropTag) == PT_UNICODE) {
		std::string strAnsi;
		// copy from unicode to string8
		lpProp->ulPropTag = CHANGE_PROP_TYPE(i->second.ulPropTag, PT_STRING8);
		strAnsi = converter.convert_to<std::string>(i->second.Value.lpszW);
		hr = MAPIAllocateMore(strAnsi.size() + 1, lpBase, (void**)&lpProp->Value.lpszA);
		if (hr != hrSuccess)
			return hr;
		strcpy(lpProp->Value.lpszA, strAnsi.c_str());
	} else {
		hr = Util::HrCopyProperty(lpProp, &i->second, lpBase);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

/** 
 * return property data of this object
 * 
 * @param[in] lpPropTagArray requested properties or NULL for all
 * @param[in] ulFlags MAPI_UNICODE when lpPropTagArray is NULL
 * @param[out] lpcValues number of properties returned 
 * @param[out] lppPropArray properties
 * 
 * @return MAPI Error code
 */
HRESULT ZCMAPIProp::GetProps(const SPropTagArray *lpPropTagArray, ULONG ulFlags,
    ULONG *lpcValues, SPropValue **lppPropArray)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpProps;
	convert_context converter;

	if ((lpPropTagArray != nullptr && lpPropTagArray->cValues == 0) ||
	    !Util::ValidatePropTagArray(lpPropTagArray))
		return MAPI_E_INVALID_PARAMETER;

	if (lpPropTagArray == NULL) {
		// check ulFlags for MAPI_UNICODE
		hr = MAPIAllocateBuffer(sizeof(SPropValue) * m_mapProperties.size(), &~lpProps);
		if (hr != hrSuccess)
			return hr;

		ULONG j = 0;
		for (auto i = m_mapProperties.cbegin();
		     i != m_mapProperties.cend(); ++i) {
			hr = CopyOneProp(converter, ulFlags, i, &lpProps[j], lpProps);
			if (hr != hrSuccess)
				return hr;
			++j;
		}
		
		*lpcValues = m_mapProperties.size();
	} else {
		// check lpPropTagArray->aulPropTag[x].ulPropTag for PT_UNICODE or PT_STRING8
		hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpPropTagArray->cValues, &~lpProps);
		if (hr != hrSuccess)
			return hr;

		for (ULONG n = 0, j = 0; n < lpPropTagArray->cValues; ++n, ++j) {
			auto i = m_mapProperties.find(PROP_ID(lpPropTagArray->aulPropTag[n]));
			if (i == m_mapProperties.cend()) {
				lpProps[j].ulPropTag = CHANGE_PROP_TYPE(lpPropTagArray->aulPropTag[n], PT_ERROR);
				lpProps[j].Value.ul = MAPI_E_NOT_FOUND;
				continue;
			}

			hr = CopyOneProp(converter, ulFlags, i, &lpProps[j], lpProps);
			if (hr != hrSuccess)
				return hr;
		}

		*lpcValues = lpPropTagArray->cValues;
	}
	*lppPropArray = lpProps.release();
	return hrSuccess;
}

/** 
 * Return a list of all properties on this object
 * 
 * @param[in] ulFlags 0 for PT_STRING8, MAPI_UNICODE for PT_UNICODE properties
 * @param[out] lppPropTagArray list of all existing properties
 * 
 * @return MAPI Error code
 */
HRESULT ZCMAPIProp::GetPropList(ULONG ulFlags, LPSPropTagArray * lppPropTagArray)
{
	LPSPropTagArray lpPropTagArray = NULL;
	std::map<short, SPropValue>::const_iterator i;
	ULONG j = 0;

	HRESULT hr = MAPIAllocateBuffer(CbNewSPropTagArray(m_mapProperties.size()),
	             reinterpret_cast<void **>(&lpPropTagArray));
	if (hr != hrSuccess)
		return hr;

	lpPropTagArray->cValues = m_mapProperties.size();
	for (i = m_mapProperties.begin(); i != m_mapProperties.end(); ++i, ++j) {
		lpPropTagArray->aulPropTag[j] = i->second.ulPropTag;
		if ((ulFlags & MAPI_UNICODE) == 0 && PROP_TYPE(lpPropTagArray->aulPropTag[j]) == PT_UNICODE)
			lpPropTagArray->aulPropTag[j] = CHANGE_PROP_TYPE(lpPropTagArray->aulPropTag[j], PT_STRING8);
	}

	*lppPropTagArray = lpPropTagArray;
	return hrSuccess;
}

HRESULT ZCMAPIProp::OpenProperty(ULONG ulPropTag, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulFlags, LPUNKNOWN * lppUnk)
{
	return  MAPI_E_NO_SUPPORT;
}

HRESULT ZCMAPIProp::SetProps(ULONG vals, const SPropValue *,
    SPropProblemArray **)
{
	return  MAPI_E_NO_SUPPORT;
}

HRESULT ZCMAPIProp::DeleteProps(const SPropTagArray *lpPropTagArray,
    SPropProblemArray **lppProblems)
{
	return  MAPI_E_NO_SUPPORT;
}

HRESULT ZCMAPIProp::CopyTo(ULONG ciidExclude, LPCIID rgiidExclude,
    const SPropTagArray *, ULONG ui_param, LPMAPIPROGRESS, LPCIID intf,
    void *dest_obj, ULONG flags, SPropProblemArray **)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCMAPIProp::CopyProps(const SPropTagArray *, ULONG ui_param,
    LPMAPIPROGRESS, LPCIID intf, void *dest_obj, ULONG flags,
    SPropProblemArray **)
{
	return MAPI_E_NO_SUPPORT;
}

HRESULT ZCMAPIProp::GetNamesFromIDs(SPropTagArray **tags, const GUID *propset,
    ULONG flags, ULONG *nvals, MAPINAMEID ***names)
{
	return MAPI_E_NO_SUPPORT;
}
 
HRESULT ZCMAPIProp::GetIDsFromNames(ULONG cPropNames, LPMAPINAMEID * lppPropNames, ULONG ulFlags, LPSPropTagArray * lppPropTags)
{
	return MAPI_E_NO_SUPPORT;
}
