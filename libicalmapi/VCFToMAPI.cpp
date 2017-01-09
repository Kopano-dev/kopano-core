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
#include <memory>
#include <new>
#include <kopano/platform.h>
#include <kopano/ECRestriction.h>
#include <kopano/memory.hpp>
#include "VCFToMAPI.h"
#include <mapix.h>
#include <mapiutil.h>
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <algorithm>
#include <vector>
#include <kopano/charset/convert.h>
#include <kopano/namedprops.h>
#include <mapi.h>

#include <stringutil.h>
#include <libical/vobject.h>
#include <libical/vcc.h>

using namespace KCHL;

namespace KC {

class VCFToMapiImpl _kc_final : public VCFToMapi {
public:
	VCFToMapiImpl(IMAPIProp *prop_obj);

	HRESULT ParseVCF(const std::string &str_vcf) _kc_override;
	HRESULT GetItem(LPMESSAGE message) _kc_override;

private:
	HRESULT SaveProps(const std::list<SPropValue> &props, LPMAPIPROP mapiobj);

	void handle_N(VObject *v);
	void handle_TEL_EMAIL(VObject *v);

	void vobject_to_prop(VObject *v, SPropValue &s, ULONG prop_type);
	HRESULT vobject_to_named_prop(VObject *v, SPropValue &s, ULONG named_prop_type);
	HRESULT unicode_to_named_prop(wchar_t *v, SPropValue &s, ULONG named_prop_type);
};

/**
 * Create a class implementing the ICalToVCF "interface".
 *
 * @param[in]  lpPropObj MAPI object used to find named properties
 * @param[out] lppICalToMapi The VCFToMapi class
 */
HRESULT CreateVCFToMapi(IMAPIProp *prop_obj, VCFToMapi **vcf_to_mapi)
{
	if (prop_obj == nullptr || vcf_to_mapi == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	*vcf_to_mapi = new(std::nothrow) VCFToMapiImpl(prop_obj);
	if (*vcf_to_mapi == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;

	return hrSuccess;
}

/**
 * Init VCFToMapi class
 *
 * @param[in] lpPropObj passed to super class
 */
VCFToMapiImpl::VCFToMapiImpl(IMAPIProp *prop_obj) : VCFToMapi(prop_obj)
{
}

void VCFToMapiImpl::handle_N(VObject *v) {
	VObjectIterator t;

	for (initPropIterator(&t, v); moreIteration(&t);) {
		auto vv = nextVObject(&t);
		auto name = vObjectName(vv);

		SPropValue s;

		if (!strcmp(name, VCFamilyNameProp)) {
			vobject_to_prop(vv, s, PR_SURNAME);
			props.push_back(s);
		}
		else if (!strcmp(name, VCGivenNameProp)) {
			vobject_to_prop(vv, s, PR_GIVEN_NAME);
			props.push_back(s);
		}
	}
}

void VCFToMapiImpl::handle_TEL_EMAIL(VObject *v) {
	bool tel = !strcmp(vObjectName(v), VCTelephoneProp);

	VObjectIterator t;

	for (initPropIterator(&t, v); moreIteration(&t);) {
		auto vv = nextVObject(&t);
		auto name = vObjectName(vv);

		const char *namep = NULL;
		if (!strcmp(name, "TYPE"))
			namep = vObjectStringZValue(vv);
		else
			namep = name;

		SPropValue s;
		auto tokens = tokenize(namep, ',');

		for(auto const &token : tokens) {
			/* telephone */
			if (tel &&
			    !strcasecmp(token.c_str(), "HOME")) {
				vobject_to_prop(v, s, PR_HOME_TELEPHONE_NUMBER);
				props.push_back(s);
			}
			else if(tel &&
			   !strcasecmp(token.c_str(), "MOBILE")) {
				vobject_to_prop(v, s, PR_MOBILE_TELEPHONE_NUMBER);
				props.push_back(s);
			}
			/* email */
			else if(!tel) {
				vobject_to_named_prop(v, s, 0x8083);
				props.push_back(s);
				unicode_to_named_prop(const_cast<wchar_t *>(L"SMTP"), s, 0x8082);
				props.push_back(s);
			}

		}
	}
}

/**
 * Parses an VCF string and converts the
 * data in memory. The real MAPI object can be retrieved using
 * GetItem().
 *
 * @param[in] strIcal The ICal data to parse
 *
 * @return MAPI error code
 */
HRESULT VCFToMapiImpl::ParseVCF(const std::string &str_vcf)
{
	HRESULT hr = hrSuccess;

	auto v = Parse_MIME(str_vcf.c_str(), str_vcf.length());
	if (v == nullptr)
		return MAPI_E_CORRUPT_DATA;

	VObjectIterator t;
	SPropValue s;

	for(initPropIterator(&t, v); moreIteration(&t);) {
		auto vv = nextVObject(&t);
		auto name = vObjectName(vv);

		if (!strcmp(name, VCNameProp))
			handle_N(vv);
		else if (!strcmp(name, VCTelephoneProp) ||
			 !strcmp(name, VCEmailAddressProp))
			handle_TEL_EMAIL(vv);
		else if (!strcmp(name, VCFullNameProp)) {
			vobject_to_prop(vv, s, PR_DISPLAY_NAME);
			props.push_back(s);
		}
	}

	cleanVObject(v);

	return hr;
}

void VCFToMapiImpl::vobject_to_prop(VObject *v, SPropValue &s, ULONG prop_type) {
        switch(vObjectValueType(v)) {
	case VCVT_STRINGZ:
		s.ulPropTag = CHANGE_PROP_TYPE(prop_type, PT_STRING8);
		s.Value.lpszA = strdup(vObjectStringZValue(v));
		break;
	case VCVT_USTRINGZ:
		s.ulPropTag = CHANGE_PROP_TYPE(prop_type, PT_UNICODE);
		s.Value.lpszW = wcsdup(vObjectUStringZValue(v));
		break;
	}
}

HRESULT VCFToMapiImpl::vobject_to_named_prop(VObject *v, SPropValue &s, ULONG named_prop_type) {
	HRESULT hr;
        MAPINAMEID name_id;
	LPMAPINAMEID name_id_ptr = &name_id;
	memory_ptr<SPropTagArray> proptag;

	name_id.lpguid = const_cast<GUID *>(&PSETID_Address);
	name_id.ulKind = MNID_ID;
	name_id.Kind.lID = named_prop_type;

	hr = prop_obj->GetIDsFromNames(1, &name_id_ptr, MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;

 	vobject_to_prop(v, s, proptag->aulPropTag[0]);

	return hrSuccess;
}

HRESULT VCFToMapiImpl::unicode_to_named_prop(wchar_t *v, SPropValue &s, ULONG named_prop_type) {
	HRESULT hr;
        MAPINAMEID name_id;
	LPMAPINAMEID name_id_ptr = &name_id;
	memory_ptr<SPropTagArray> proptag;

	name_id.lpguid = const_cast<GUID *>(&PSETID_Address);
	name_id.ulKind = MNID_ID;
	name_id.Kind.lID = named_prop_type;

	hr = prop_obj->GetIDsFromNames(1, &name_id_ptr, MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;

	s.ulPropTag = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_UNICODE);
	s.Value.lpszW = wcsdup(v);

	return hrSuccess;
}

/**
 * Sets mapi properties in Imessage object from the vcfitem.
 *
 * @param[in,out]	lpMessage		IMessage in which properties has to be set
 *
 * @return			MAPI error code
 * @retval			MAPI_E_INVALID_PARAMETER	NULL IMessage parameter
 */
HRESULT VCFToMapiImpl::GetItem(LPMESSAGE message)
{
	HRESULT hr = hrSuccess;

	if (message == NULL)
		return MAPI_E_INVALID_PARAMETER;

	SPropValue s;
	s.ulPropTag = PR_MESSAGE_CLASS_A;
	s.Value.lpszA = const_cast<char *>("IPM.Contact");

	hr = HrSetOneProp(message, &s);
	if (hr != hrSuccess)
		return hr;

	hr = SaveProps(props, message);
	if (hr != hrSuccess)
		return hr;

	return hr;
}

/**
 * Helper function for GetItem. Saves all properties converted from
 * VCF to MAPI in the MAPI object. Does not call SaveChanges.
 *
 * @param[in] props list of properties to save in mapiobj
 * @param[in] mapiobj The MAPI object to save properties in
 *
 * @return MAPI error code
 */
HRESULT VCFToMapiImpl::SaveProps(const std::list<SPropValue> &props,
    LPMAPIPROP mapiobj)
{
	memory_ptr<SPropValue> propvals;

	auto hr = MAPIAllocateBuffer(props.size() * sizeof(SPropValue), &~propvals);
	if (hr != hrSuccess)
		return hr;

	size_t i = 0;
	for (const auto &prop : props)
		propvals[i++] = prop;

	auto retval = mapiobj->SetProps(i, propvals, NULL);

	for (const auto &prop : props) {
		if(PROP_TYPE(prop.ulPropTag) == PT_UNICODE)
			delete prop.Value.lpszW;
		if(PROP_TYPE(prop.ulPropTag) == PT_STRING8)
			delete prop.Value.lpszA;
	}

	return retval;
}


} /* namespace */
