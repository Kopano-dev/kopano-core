/*
 * Copyright 2017 - Kopano and its licensors
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
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <kopano/memory.hpp>
#include "MAPIToVCF.h"
#include <libical/vcc.h>

#include <mapiutil.h>
#include <mapicode.h>
#include <mapix.h>
#include <kopano/ecversion.h>
#include <libical/vobject.h>
#include <kopano/mapi_ptr.h>

namespace KC {

class MapiToVCFImpl _kc_final : public MapiToVCF {
public:
	MapiToVCFImpl();
	virtual ~MapiToVCFImpl();

	HRESULT AddMessage(LPMESSAGE lpMessage) _kc_override;
	HRESULT Finalize(std::string *strIcal) _kc_override;
	HRESULT ResetObject(void) _kc_override;

private:
	VObject* to_unicode_prop(VObject *node, const char *prop, wchar_t const *value); 

	std::string result;
	LPSPropTagArray m_lpNamedProps;
	/* since we don't want depending projects to add include paths for libical, this is only in the implementation version */
};

/** 
 * Create a class implementing the MapiToICal "interface".
 * 
 * @param[in]  lpAdrBook MAPI addressbook
 * @param[in]  strCharset charset of the ical returned by this class
 * @param[out] lppMapiToICal The conversion class
 */
HRESULT CreateMapiToVCF(MapiToVCF **lppMapiToVCF)
{
	*lppMapiToVCF = new(std::nothrow) MapiToVCFImpl();
	if (*lppMapiToVCF == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	return hrSuccess;
}

/** 
 * Init MapiToICal class
 * 
 * @param[in] lpAdrBook MAPI addressbook
 * @param[in] strCharset charset of the ical returned by this class
 */
MapiToVCFImpl::MapiToVCFImpl()
{
	m_lpNamedProps = NULL;
}

/** 
 * Frees all used memory in conversion
 * 
 */
MapiToVCFImpl::~MapiToVCFImpl()
{
	MAPIFreeBuffer(m_lpNamedProps);
}

VObject* MapiToVCFImpl::to_unicode_prop(VObject *node, const char *prop, wchar_t const* value) {
	char plain[128];
	VObject *newnode;

	std::wcstombs(plain, value, 128);

	newnode = addProp(node, prop);
	if(newnode == nullptr) {
		return nullptr;
	}

	setVObjectStringZValue(newnode, plain);

	return newnode;
}

/** 
 * Add a MAPI message to the ical VCALENDAR object.
 * 
 * @param[in] lpMessage Convert this MAPI message to ICal
 * @param[in] strSrvTZ Use this timezone, used for Tasks only (or so it seems)
 * @param[in] ulFlags Conversion flags:
 * @arg @c M2IC_CENSOR_PRIVATE Privacy sensitive data will not be present in the ICal
 * 
 * @return MAPI error code
 */
HRESULT MapiToVCFImpl::AddMessage(LPMESSAGE lpMessage)
{
	KCHL::memory_ptr<SPropValue> lpMessageClass;

	if (lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMessageClass);
	if (hr != hrSuccess)
		return hr;

	if (strcasecmp(lpMessageClass->Value.lpszA, "IPM.Contact") != 0) {
		return MAPI_E_INVALID_PARAMETER;
	}

	VObject* root = newVObject(VCCardProp);
	VObject* node = nullptr;

	KCHL::memory_ptr<SPropValue> msgprop, msgprop2;

	hr = HrGetOneProp(lpMessage, PR_GIVEN_NAME, &~msgprop);
	HRESULT hr2 = HrGetOneProp(lpMessage, PR_SURNAME, &~msgprop2);
	if(hr == hrSuccess && hr2 == hrSuccess) {
		node = addGroup(root, VCNameProp);
		to_unicode_prop(node, VCGivenNameProp, msgprop->Value.lpszW);
		to_unicode_prop(node, VCFamilyNameProp, msgprop2->Value.lpszW);
	} else if(hr != MAPI_E_NOT_FOUND || hr2 != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_DISPLAY_NAME, &~msgprop);
	if(hr == hrSuccess) {
		to_unicode_prop(root, VCFullNameProp, msgprop->Value.lpszW);
	}
	else if(hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_HOME_TELEPHONE_NUMBER, &~msgprop);
	if(hr == hrSuccess) {
		VObject* node = to_unicode_prop(root, VCTelephoneProp, msgprop->Value.lpszW);
		to_unicode_prop(node, "TYPE", L"HOME");
	}
	else if(hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_MOBILE_TELEPHONE_NUMBER, &~msgprop);
	if(hr == hrSuccess) {
		VObject* node = to_unicode_prop(root, VCTelephoneProp, msgprop->Value.lpszW);
		to_unicode_prop(node, "TYPE", L"MOBILE");
	}
	else if(hr != MAPI_E_NOT_FOUND)
		return hr;

	/* Email */
	MAPINAMEID sNameID;
	sNameID.lpguid = (GUID*)&PSETID_Address;
	sNameID.ulKind = MNID_ID;
	sNameID.Kind.lID = 0x8083;

	LPMAPINAMEID lpNameID = &sNameID;
	KCHL::memory_ptr<SPropTagArray> lpPropTag;
	hr = lpMessage->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &~lpPropTag);
	if (hr != hrSuccess) {
		return hr;
	}

	ULONG propType = CHANGE_PROP_TYPE(lpPropTag->aulPropTag[0], PT_UNICODE);
	hr = HrGetOneProp(lpMessage, propType, &~msgprop);
	if(hr == hrSuccess) {
		to_unicode_prop(root, VCEmailAddressProp, msgprop->Value.lpszW);
	}
	else if(hr != MAPI_E_NOT_FOUND)
		return hr;

	/* Write memobject */
	int len = 0;
	char* cresult = writeMemVObject(NULL, &len, root);
	if(cresult == nullptr) {
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}

	result = cresult;
	delete cresult;

	cleanVObject(root);

	return hrSuccess;
}

/** 
 * Create the actual ICal data.
 * 
 * @param[in]  ulFlags Conversion flags
 * @arg @c M2IC_NO_VTIMEZONE Skip the VTIMEZONE parts in the output
 * @param[out] strMethod ICal method (eg. PUBLISH)
 * @param[out] strIcal The ICal data in 8bit string, charset given in constructor
 * 
 * @return MAPI error code
 */
HRESULT MapiToVCFImpl::Finalize(std::string *strVCF)
{
	HRESULT hr = hrSuccess;

	if (strVCF == nullptr) {
		return MAPI_E_INVALID_PARAMETER;
	}

	*strVCF = result;

	return hr;
}

/** 
 * Reset this class to be used for starting a new series of conversions.
 * 
 * @return always hrSuccess
 */
HRESULT MapiToVCFImpl::ResetObject()
{
	return hrSuccess;
}

} /* namespace */
