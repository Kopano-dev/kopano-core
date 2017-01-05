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
	HRESULT hr = hrSuccess;
	KCHL::memory_ptr<SPropValue> lpMessageClass;

	if (lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	if (m_lpNamedProps == nullptr) {
		//hr = HrLookupNames(lpMessage, &m_lpNamedProps);
		if (hr != hrSuccess)
			return hr;
	}
	hr = HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMessageClass);
	if (hr != hrSuccess)
		return hr;

	if (strcasecmp(lpMessageClass->Value.lpszA, "IPM.Contact") != 0) {
		return MAPI_E_INVALID_PARAMETER;
	}

	VObject* root = newVObject(VCCardProp);

	KCHL::memory_ptr<SPropValue> msgprop;
	hr = HrGetOneProp(lpMessage, PR_GIVEN_NAME, &~msgprop);
	if(hr == hrSuccess) {
		char plain[128];
		std::wcstombs(plain, msgprop->Value.lpszW, 128);

		VObject* node = addProp(root, VCNameProp);
		addPropValue(node, VCGivenNameProp, plain);

		hr = HrGetOneProp(lpMessage, PR_SURNAME, &~msgprop);
		if(hr == hrSuccess) {
			std::wcstombs(plain, msgprop->Value.lpszW, 128);
			addPropValue(node, VCFamilyNameProp, plain);
		}
		else if(hr != MAPI_E_NOT_FOUND)
			return hr;

	} else if(hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_DISPLAY_NAME, &~msgprop);
	if(hr == hrSuccess) {
		char plain[128];
		std::wcstombs(plain, msgprop->Value.lpszW, 128);

		addPropValue(root, VCFullNameProp, plain);
	}
	else if(hr != MAPI_E_NOT_FOUND)
		return hr;

	int len = 0;
	char* cresult = writeMemVObject(NULL, &len, root);
	if(cresult == nullptr) {
		return MAPI_E_NOT_ENOUGH_MEMORY;
	}

	result = cresult;
	delete cresult;

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
