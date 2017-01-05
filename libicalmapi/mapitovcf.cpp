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
#include "mapitovcf.hpp"

namespace KC {

class mapitovcf_impl _kc_final : public mapitovcf {
	public:
	HRESULT add_message(IMessage *) _kc_override;
	HRESULT finalize(std::string *) _kc_override;

	private:
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
		char plain[128];
		std::wcstombs(plain, msgprop->Value.lpszW, 128);

		auto node = addProp(root, VCNameProp);
		addPropValue(node, VCGivenNameProp, plain);
		hr = HrGetOneProp(lpMessage, PR_SURNAME, &~msgprop);
		if (hr == hrSuccess) {
			wcstombs(plain, msgprop->Value.lpszW, 128);
			addPropValue(node, VCFamilyNameProp, plain);
		} else if (hr != MAPI_E_NOT_FOUND) {
			return hr;
		}
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_DISPLAY_NAME, &~msgprop);
	if (hr == hrSuccess) {
		char plain[128];
		wcstombs(plain, msgprop->Value.lpszW, sizeof(plain));
		addPropValue(root, VCFullNameProp, plain);
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

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
