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
#include <algorithm>
#include <memory>
#include <new>
#include <kopano/ECRestriction.h>
#include <kopano/charset/convert.h>
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <kopano/memory.hpp>
#include <kopano/namedprops.h>
#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <libical/vcc.h>
#include <libical/vobject.h>
#include <mapi.h>
#include <mapiutil.h>
#include <mapix.h>
#include <vector>
#include "vcftomapi.hpp"

using namespace KCHL;

namespace KC {

class vcftomapi_impl _kc_final : public vcftomapi {
	public:
	/*
	 * - lpPropObj to lookup named properties
	 * - Addressbook (Global AddressBook for looking up users)
	 */
	vcftomapi_impl(IMAPIProp *o) : vcftomapi(o) {}
	HRESULT parse_vcf(const std::string &) _kc_override;
	HRESULT get_item(IMessage *) _kc_override;

	private:
	HRESULT save_props(const std::list<SPropValue> &, IMAPIProp *);
	void handle_N(VObject *);
	void handle_TEL_EMAIL(VObject *);
	void vobject_to_prop(VObject *, SPropValue &, ULONG proptype);
	HRESULT vobject_to_named_prop(VObject *, SPropValue &, ULONG named_proptype);
	HRESULT unicode_to_named_prop(const wchar_t *, SPropValue &, ULONG named_proptype);
};

/**
 * Create a class implementing the ICalToMapi "interface".
 */
HRESULT create_vcftomapi(IMAPIProp *prop, vcftomapi **ret)
{
	if (prop == nullptr || ret == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	*ret = new(std::nothrow) vcftomapi_impl(prop);
	return *ret != nullptr ? hrSuccess : MAPI_E_NOT_ENOUGH_MEMORY;
}

void vcftomapi_impl::handle_N(VObject *v)
{
	VObjectIterator tt;

	for (initPropIterator(&tt, v); moreIteration(&tt); ) {
		auto vv = nextVObject(&tt);
		auto name = vObjectName(vv);
		SPropValue s;

		if (strcmp(name, VCFamilyNameProp) == 0) {
			vobject_to_prop(vv, s, PR_SURNAME);
			props.push_back(s);
		} else if (strcmp(name, VCGivenNameProp) == 0) {
			vobject_to_prop(vv, s, PR_GIVEN_NAME);
			props.push_back(s);
		}
	}
}

void vcftomapi_impl::handle_TEL_EMAIL(VObject *v)
{
	bool tel = strcmp(vObjectName(v), VCTelephoneProp) == 0;
	VObjectIterator t;

	for (initPropIterator(&t, v); moreIteration(&t); ) {
		auto vv = nextVObject(&t);
		auto name = vObjectName(vv);
		SPropValue s;
		const char *namep = strcmp(name, "TYPE") == 0 ? vObjectStringZValue(vv) : name;
		auto tokens = tokenize(namep, ',');

		for (auto const &token : tokens) {
			/* telephone */
			if (tel && strcasecmp(token.c_str(), "HOME") == 0) {
				vobject_to_prop(v, s, PR_HOME_TELEPHONE_NUMBER);
				props.push_back(s);
			}
			if (tel && strcasecmp(token.c_str(), "MOBILE") == 0) {
				vobject_to_prop(v, s, PR_MOBILE_TELEPHONE_NUMBER);
				props.push_back(s);
			}
			if (tel)
				continue;
			/* email */
			vobject_to_named_prop(v, s, 0x8083);
			props.push_back(s);
			unicode_to_named_prop(L"SMTP", s, 0x8082);
			props.push_back(s);
		}
	}
}

/**
 * Parses an ICal string (with a certain charset) and converts the
 * data in memory. The real MAPI object can be retrieved using
 * GetItem().
 */
HRESULT vcftomapi_impl::parse_vcf(const std::string &ical)
{
	HRESULT hr = hrSuccess;
	auto v = Parse_MIME(ical.c_str(), ical.length());
	if (v == nullptr)
		return MAPI_E_CORRUPT_DATA;

	auto v_orig = v;
	VObjectIterator t;
	for (initPropIterator(&t, v); moreIteration(&t); ) {
		v = nextVObject(&t);
		auto name = vObjectName(v);

		if (strcmp(name, VCNameProp) == 0) {
			handle_N(v);
		} else if (strcmp(name, VCFullNameProp) == 0) {
			SPropValue s;
			vobject_to_prop(v, s, PR_DISPLAY_NAME);
			props.push_back(s);
		} else if (strcmp(name, VCTelephoneProp) == 0 ||
		    strcmp(name, VCEmailAddressProp) == 0) {
			handle_TEL_EMAIL(v);
		}
	}
	cleanVObject(v_orig);
	return hr;
}

void vcftomapi_impl::vobject_to_prop(VObject *v, SPropValue &s, ULONG proptype)
{
        switch (vObjectValueType(v)) {
	case VCVT_STRINGZ:
		s.ulPropTag = CHANGE_PROP_TYPE(proptype, PT_STRING8);
		s.Value.lpszA = strdup(vObjectStringZValue(v));
		break;
	case VCVT_USTRINGZ:
		s.ulPropTag = CHANGE_PROP_TYPE(proptype, PT_UNICODE);
		s.Value.lpszW = wcsdup(vObjectUStringZValue(v));
		break;
	}
}

HRESULT vcftomapi_impl::vobject_to_named_prop(VObject *v, SPropValue &s,
    ULONG named_proptype)
{
	HRESULT hr;
        MAPINAMEID name;
	MAPINAMEID *namep = &name;
	memory_ptr<SPropTagArray> proptag;

	name.lpguid = const_cast<GUID *>(&PSETID_Address);
	name.ulKind = MNID_ID;
	name.Kind.lID = named_proptype;
	hr = m_propobj->GetIDsFromNames(1, &namep, MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;
	vobject_to_prop(v, s, proptag->aulPropTag[0]);
	return hrSuccess;
}

HRESULT vcftomapi_impl::unicode_to_named_prop(const wchar_t *v, SPropValue &s,
    ULONG named_proptype)
{
        MAPINAMEID name;
	MAPINAMEID *namep = &name;
	memory_ptr<SPropTagArray> proptag;

	name.lpguid = const_cast<GUID *>(&PSETID_Address);
	name.ulKind = MNID_ID;
	name.Kind.lID = named_proptype;

	HRESULT hr = m_propobj->GetIDsFromNames(1, &namep,
	             MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;
	s.ulPropTag = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_UNICODE);
	s.Value.lpszW = wcsdup(v);
	return hrSuccess;
}

/**
 * Sets mapi properties in Imessage object from the icalitem.
 */
HRESULT vcftomapi_impl::get_item(IMessage *msg)
{
	if (msg == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	SPropValue s;
	s.ulPropTag = PR_MESSAGE_CLASS_A;
	s.Value.lpszA = const_cast<char *>("IPM.Contact");
	HRESULT hr = HrSetOneProp(msg, &s);
	if (hr != hrSuccess)
		return hr;
	return save_props(props, msg);
}

/**
 * Helper function for GetItem. Saves all properties converted from
 * ICal to MAPI in the MAPI object. Does not call SaveChanges.
 *
 * @param[in] lpPropList list of properties to save in lpMapiProp
 * @param[in] lpMapiProp The MAPI object to save properties in
 *
 * @return MAPI error code
 */
HRESULT vcftomapi_impl::save_props(const std::list<SPropValue> &proplist,
    IMAPIProp *mapiprop)
{
	memory_ptr<SPropValue> propvals;
	HRESULT hr = MAPIAllocateBuffer(proplist.size() * sizeof(SPropValue),
	             &~propvals);
	if (hr != hrSuccess)
		return hr;

	size_t i = 0;
	for (const auto &prop : proplist)
		propvals[i++] = prop;
	auto ret = mapiprop->SetProps(i, propvals, nullptr);
	for (const auto &prop : proplist) {
		if (PROP_TYPE(prop.ulPropTag) == PT_UNICODE)
			free(prop.Value.lpszW);
		else if (PROP_TYPE(prop.ulPropTag) == PT_STRING8)
			free(prop.Value.lpszA);
	}
	return ret;
}

} /* namespace */
