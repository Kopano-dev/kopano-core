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
	/*
	    - lpPropObj to lookup named properties
	    - Addressbook (Global AddressBook for looking up users)
	 */
	VCFToMapiImpl(IMAPIProp *lpPropObj);
	virtual ~VCFToMapiImpl();

	HRESULT ParseVCF(const std::string& strVCF) _kc_override;
	HRESULT GetItem(LPMESSAGE lpMessage) _kc_override;

private:
	HRESULT SaveProps(const std::list<SPropValue> *lpPropList, LPMAPIPROP lpMapiProp);

	void handle_N(VObject* v);
	void handle_TEL_EMAIL(VObject* v);

	void vobject_to_prop(VObject* v, SPropValue &s, ULONG propType);
	HRESULT vobject_to_named_prop(VObject* v, SPropValue &s, ULONG namedPropType);
	HRESULT unicode_to_named_prop(wchar_t* v, SPropValue &s, ULONG namedPropType);
};

/** 
 * Create a class implementing the ICalToMapi "interface".
 * 
 * @param[in]  lpPropObj MAPI object used to find named properties
 * @param[in]  lpAdrBook MAPI Addressbook
 * @param[in]  bNoRecipients Skip recipients from ical. Used for DAgent, which uses the mail recipients
 * @param[out] lppICalToMapi The ICalToMapi class
 */
HRESULT CreateVCFToMapi(IMAPIProp *lpPropObj, VCFToMapi **lppICalToMapi)
{
	if (lpPropObj == nullptr || lppICalToMapi == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	*lppICalToMapi = new(std::nothrow) VCFToMapiImpl(lpPropObj);
	if (*lppICalToMapi == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;

	return hrSuccess;
}

/** 
 * Init ICalToMapi class
 * 
 * @param[in] lpPropObj passed to super class
 * @param[in] lpAdrBook passed to super class
 * @param[in] bNoRecipients passed to super class
 */
VCFToMapiImpl::VCFToMapiImpl(IMAPIProp *lpPropObj) : VCFToMapi(lpPropObj)
{
}

/** 
 * Frees all used memory of the ICalToMapi class
 */
VCFToMapiImpl::~VCFToMapiImpl()
{
}

void VCFToMapiImpl::handle_N(VObject* v) {
	VObjectIterator tt;

	initPropIterator(&tt, v);

	while (moreIteration(&tt)) {
		VObject *vv = nextVObject(&tt);
		const char *name = vObjectName(vv);
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

void VCFToMapiImpl::handle_TEL_EMAIL(VObject* v) {
	bool tel = !strcmp(vObjectName(v), VCTelephoneProp);

	VObjectIterator t;
	initPropIterator(&t, v);

	while (moreIteration(&t)) {
		VObject *vv = nextVObject(&t);
		const char *name = vObjectName(vv);

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
			if(tel &&
			   !strcasecmp(token.c_str(), "MOBILE")) {
				vobject_to_prop(v, s, PR_MOBILE_TELEPHONE_NUMBER);
				props.push_back(s);
			}
			/* email */
			if(!tel) {
				vobject_to_named_prop(v, s, 0x8083);
				props.push_back(s);
				unicode_to_named_prop(L"SMTP", s, 0x8082);
				props.push_back(s);
			}

		}
	}
}

/** 
 * Parses an ICal string (with a certain charset) and converts the
 * data in memory. The real MAPI object can be retrieved using
 * GetItem().
 * 
 * @param[in] strIcal The ICal data to parse
 * @param[in] strCharset The charset of strIcal (usually UTF-8)
 * @param[in] strServerTZparam ID of default timezone to use if ICal data didn't specify
 * @param[in] lpMailUser IMailUser object of the current user (CalDav: the user logged in, DAgent: the user being delivered for)
 * @param[in] ulFlags Conversion flags - currently unused
 * 
 * @return MAPI error code
 */
HRESULT VCFToMapiImpl::ParseVCF(const std::string& strIcal)
{
	HRESULT hr = hrSuccess;

	VObject *v = Parse_MIME(strIcal.c_str(), strIcal.length());
	if (v == nullptr)
		return MAPI_E_CORRUPT_DATA;

	VObjectIterator t;
	SPropValue s;

	initPropIterator(&t, v);

	while (moreIteration(&t)) {
		VObject* vv = nextVObject(&t);
		const char* name = vObjectName(vv);

		if (!strcmp(name, VCNameProp)) {
			handle_N(vv);
		}
		else if (!strcmp(name, VCFullNameProp)) {
			vobject_to_prop(vv, s, PR_DISPLAY_NAME);
			props.push_back(s);
		}
		else if (!strcmp(name, VCTelephoneProp) ||
			 !strcmp(name, VCEmailAddressProp)) {
			handle_TEL_EMAIL(vv);
		}
	}

	cleanVObject(v);

	return hr;
}

void VCFToMapiImpl::vobject_to_prop(VObject *v, SPropValue &s, ULONG propType) {
        switch(vObjectValueType(v)) {
	case VCVT_STRINGZ:
		s.ulPropTag = CHANGE_PROP_TYPE(propType, PT_STRING8);
		s.Value.lpszA = strdup(vObjectStringZValue(v));
		break;
	case VCVT_USTRINGZ:
		s.ulPropTag = CHANGE_PROP_TYPE(propType, PT_UNICODE);
		s.Value.lpszW = wcsdup(vObjectUStringZValue(v));
		break;
	}
}

HRESULT VCFToMapiImpl::vobject_to_named_prop(VObject *v, SPropValue &s, ULONG namedPropType) {
	HRESULT hr;
        MAPINAMEID sNameID;
	LPMAPINAMEID lpNameID = &sNameID;
	memory_ptr<SPropTagArray> lpPropTag;

	sNameID.lpguid = (GUID*)&PSETID_Address;
	sNameID.ulKind = MNID_ID;
	sNameID.Kind.lID = namedPropType;

	hr = m_lpPropObj->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &~lpPropTag);
	if (hr != hrSuccess) {
		return hr;
	}

 	vobject_to_prop(v, s, lpPropTag->aulPropTag[0]);

	return hrSuccess;
}

HRESULT VCFToMapiImpl::unicode_to_named_prop(wchar_t* v, SPropValue &s, ULONG namedPropType) {
	HRESULT hr;
        MAPINAMEID sNameID;
	LPMAPINAMEID lpNameID = &sNameID;
	memory_ptr<SPropTagArray> lpPropTag;

	sNameID.lpguid = (GUID*)&PSETID_Address;
	sNameID.ulKind = MNID_ID;
	sNameID.Kind.lID = namedPropType;

	hr = m_lpPropObj->GetIDsFromNames(1, &lpNameID, MAPI_CREATE, &~lpPropTag);
	if (hr != hrSuccess) {
		return hr;
	}

	s.ulPropTag = CHANGE_PROP_TYPE(lpPropTag->aulPropTag[0], PT_UNICODE);
	s.Value.lpszW = wcsdup(v);

	return hrSuccess;
}

/**
 * Sets mapi properties in Imessage object from the icalitem.
 *
 * @param[in]		ulPosition		specifies the message that is to be retrieved
 * @param[in]		ulFlags			conversion flags
 * @arg @c IC2M_NO_RECIPIENTS skip recipients in conversion from ICal to MAPI
 * @arg @c IC2M_APPEND_ONLY	do not delete properties in lpMessage that are not present in ICal, but possebly are in lpMessage
 * @param[in,out]	lpMessage		IMessage in which properties has to be set
 *
 * @return			MAPI error code
 * @retval			MAPI_E_INVALID_PARAMETER	invalid position set in ulPosition or NULL IMessage parameter
 */
HRESULT VCFToMapiImpl::GetItem(LPMESSAGE lpMessage)
{
	HRESULT hr = hrSuccess;

	if (lpMessage == NULL)
		return MAPI_E_INVALID_PARAMETER;

	SPropValue s;
	s.ulPropTag = PR_MESSAGE_CLASS_A;
	s.Value.lpszA = const_cast<char *>("IPM.Contact");

	hr = HrSetOneProp(lpMessage, &s);
	if (hr != hrSuccess)
		return hr;

	hr = SaveProps(&props, lpMessage);
	if (hr != hrSuccess)
		return hr;

	return hr;
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
HRESULT VCFToMapiImpl::SaveProps(const std::list<SPropValue> *lpPropList,
    LPMAPIPROP lpMapiProp)
{
	HRESULT hr = hrSuccess;
	memory_ptr<SPropValue> lpsPropVals;
	int i;

	// all props to message
	hr = MAPIAllocateBuffer(lpPropList->size() * sizeof(SPropValue), &~lpsPropVals);
	if (hr != hrSuccess)
		return hr;

	// @todo: add exclude list or something? might set props the caller doesn't want (see vevent::HrAddTimes())
	i = 0;
	for (const auto &prop : *lpPropList)
		lpsPropVals[i++] = prop;

	auto retval = lpMapiProp->SetProps(i, lpsPropVals, NULL);

	for (const auto &prop : *lpPropList) {
		if(PROP_TYPE(prop.ulPropTag) == PT_UNICODE)
			delete prop.Value.lpszW;
		if(PROP_TYPE(prop.ulPropTag) == PT_STRING8)
			delete prop.Value.lpszA;
	}

	return retval;
}


} /* namespace */
