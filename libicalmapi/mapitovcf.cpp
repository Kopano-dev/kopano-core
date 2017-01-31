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
 */
#include <kopano/zcdefs.h>
#include <memory>
#include <new>
#include <cstdlib>
#include <libical/vcc.h>
#include <libical/vobject.h>
#include <mapicode.h>
#include <mapiutil.h>
#include <mapix.h>
#include <kopano/memory.hpp>
#include <kopano/platform.h>
#include <kopano/mapiguidext.h>
#include "mapitovcf.hpp"

namespace KC {

class mapitovcf_impl _kc_final : public mapitovcf {
	public:
	HRESULT add_message(IMessage *) _kc_override;
	HRESULT finalize(std::string *) _kc_override;

	private:
	VObject *to_unicode_prop(VObject *node, const char *prop, wchar_t const *value);

	std::string m_result;
	/*
	 * Since we do not want downstream projects to add include paths for
	 * libical, this is only in the implementation version.
	 */
};

HRESULT create_mapitovcf(mapitovcf **ret)
{
	*ret = new(std::nothrow) mapitovcf_impl();
	return *ret != nullptr ? hrSuccess : MAPI_E_NOT_ENOUGH_MEMORY;
}

VObject *mapitovcf_impl::to_unicode_prop(VObject *node, const char *prop,
    const wchar_t *value)
{
	char plain[128];
	wcstombs(plain, value, sizeof(plain));
	return addPropValue(node, prop, plain);
}

HRESULT mapitovcf_impl::add_message(IMessage *lpMessage)
{
	HRESULT hr = hrSuccess;
	KCHL::memory_ptr<SPropValue> lpMessageClass;

	if (lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	hr = HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMessageClass);
	if (hr != hrSuccess)
		return hr;
	if (strcasecmp(lpMessageClass->Value.lpszA, "IPM.Contact") != 0)
		return MAPI_E_INVALID_PARAMETER;

	auto root = newVObject(VCCardProp);
	KCHL::memory_ptr<SPropValue> msgprop;
	hr = HrGetOneProp(lpMessage, PR_GIVEN_NAME, &~msgprop);
	if (hr == hrSuccess) {
		auto node = addProp(root, VCNameProp);
		to_unicode_prop(node, VCGivenNameProp, msgprop->Value.lpszW);
		hr = HrGetOneProp(lpMessage, PR_SURNAME, &~msgprop);
		if (hr == hrSuccess)
			to_unicode_prop(node, VCFamilyNameProp, msgprop->Value.lpszW);
		else if (hr != MAPI_E_NOT_FOUND)
			return hr;
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_DISPLAY_NAME, &~msgprop);
	if (hr == hrSuccess)
		to_unicode_prop(root, VCFullNameProp, msgprop->Value.lpszW);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_HOME_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_unicode_prop(root, VCTelephoneProp, msgprop->Value.lpszW);
		to_unicode_prop(node, "TYPE", L"HOME");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_MOBILE_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_unicode_prop(root, VCTelephoneProp, msgprop->Value.lpszW);
		to_unicode_prop(node, "TYPE", L"MOBILE");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	/* Email */
	MAPINAMEID name;
	MAPINAMEID *namep = &name;
	name.lpguid = const_cast<GUID *>(&PSETID_Address);
	name.ulKind = MNID_ID;
	name.Kind.lID = 0x8083;

	KCHL::memory_ptr<SPropTagArray> proptag;
	hr = lpMessage->GetIDsFromNames(1, &namep, MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;

	ULONG proptype = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_UNICODE);
	hr = HrGetOneProp(lpMessage, proptype, &~msgprop);
	if (hr == hrSuccess)
		to_unicode_prop(root, VCEmailAddressProp, msgprop->Value.lpszW);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	/* Write memobject */
	int len = 0;
	auto cresult = writeMemVObject(nullptr, &len, root);
	if (cresult == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	m_result = cresult;
	free(cresult);
	return hrSuccess;
}

HRESULT mapitovcf_impl::finalize(std::string *s)
{
	if (s == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	*s = m_result;
	return hrSuccess;
}

} /* namespace */
