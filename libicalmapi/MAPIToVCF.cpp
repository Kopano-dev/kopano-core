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
	HRESULT AddMessage(LPMESSAGE message) _kc_override;
	HRESULT Finalize(std::string *str_vcf) _kc_override;
	HRESULT ResetObject(void) _kc_override;

private:
	VObject* to_prop(VObject *node, const char *prop, SPropValue const &value);
	VObject* to_prop(VObject *node, const char *prop, wchar_t const *value);

	std::string result;
};

/**
 * Create a class implementing the MapiToVCF "interface".
 *
 * @param[out] lppMapiToVCF The conversion class
 */
HRESULT CreateMapiToVCF(MapiToVCF **mapi_to_vcf)
{
	*mapi_to_vcf = new(std::nothrow) MapiToVCFImpl();
	if (*mapi_to_vcf == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	return hrSuccess;
}

VObject* MapiToVCFImpl::to_prop(VObject *node, const char *prop, SPropValue const &s) {
	char plain[128];

	if(PROP_TYPE(s.ulPropTag) == PT_UNICODE)
		std::wcstombs(plain, s.Value.lpszW, 128);
	else if(PROP_TYPE(s.ulPropTag) == PT_UNICODE)
		strncpy(plain, s.Value.lpszA, 128);

	auto newnode = addProp(node, prop);
	if(newnode == nullptr)
		return nullptr;

	setVObjectStringZValue(newnode, plain);

	return newnode;
}

VObject* MapiToVCFImpl::to_prop(VObject *node, const char *prop, const wchar_t *value) {
	char plain[128];

	std::wcstombs(plain, value, 128);
	auto newnode = addProp(node, prop);
	setVObjectStringZValue(newnode, plain);

	return newnode;
}

/**
 * Add a MAPI message to the VCF object.
 *
 * @param[in] lpMessage Convert this MAPI message to VCF
 *
 * @return MAPI error code
 */
HRESULT MapiToVCFImpl::AddMessage(LPMESSAGE message)
{
	KCHL::memory_ptr<SPropValue> msgprop, msgprop2;

	if (message == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	HRESULT hr = HrGetOneProp(message, PR_MESSAGE_CLASS_A, &~msgprop);
	if (hr != hrSuccess)
		return hr;

	if (strcasecmp(msgprop->Value.lpszA, "IPM.Contact") != 0)
		return MAPI_E_INVALID_PARAMETER;

	auto root = newVObject(VCCardProp);

	hr = HrGetOneProp(message, PR_GIVEN_NAME, &~msgprop);
	HRESULT hr2 = HrGetOneProp(message, PR_SURNAME, &~msgprop2);
	if (hr == hrSuccess && hr2 == hrSuccess) {
		auto node = addGroup(root, VCNameProp);
		to_prop(node, VCGivenNameProp, *msgprop);
		to_prop(node, VCFamilyNameProp, *msgprop2);
	} else if (hr != MAPI_E_NOT_FOUND || hr2 != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(message, PR_DISPLAY_NAME, &~msgprop);
	if (hr == hrSuccess)
		to_prop(root, VCFullNameProp, *msgprop);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(message, PR_HOME_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"HOME");
	}
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(message, PR_MOBILE_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"MOBILE");
	}
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	/* Email */
	MAPINAMEID name_id;
	name_id.lpguid = const_cast<GUID*>(&PSETID_Address);
	name_id.ulKind = MNID_ID;
	name_id.Kind.lID = 0x8083;

	LPMAPINAMEID name_id_ptr = &name_id;
	KCHL::memory_ptr<SPropTagArray> proptag;
	hr = message->GetIDsFromNames(1, &name_id_ptr, MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;

	ULONG prop_type = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_UNICODE);
	hr = HrGetOneProp(message, prop_type, &~msgprop);
	if (hr == hrSuccess)
		to_prop(root, VCEmailAddressProp, *msgprop);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	/* Write memobject */
	int len = 0;
	auto cresult = writeMemVObject(NULL, &len, root);
	if (cresult == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;

	result = cresult;
	delete cresult;

	cleanVObject(root);

	return hrSuccess;
}

/**
 * Create the actual VCF data.
 *
 * @param[out] strVCF The VCF data in 8bit string
 *
 * @return MAPI error code
 */
HRESULT MapiToVCFImpl::Finalize(std::string *str_vcf)
{
	if (str_vcf == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	*str_vcf = result;
	return hrSuccess;
}

/**
 * Reset this class to be used for starting a new series of conversions.
 *
 * @return always hrSuccess
 */
HRESULT MapiToVCFImpl::ResetObject()
{
	result = "";
	return hrSuccess;
}

} /* namespace */
