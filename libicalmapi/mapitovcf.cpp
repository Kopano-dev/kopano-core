/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2017 - Kopano and its licensors
 */
#include <map>
#include <memory>
#include <new>
#include <cstdlib>
#include <libical/vcc.h>
#include <libical/vobject.h>
#include <mapicode.h>
#include <mapiutil.h>
#include <mapix.h>
#include <kopano/CommonUtil.h>
#include <kopano/ECLogger.h>
#include <kopano/memory.hpp>
#include <kopano/platform.h>
#include <kopano/mapiguidext.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convstring.h>
#include <kopano/namedprops.h>
#include <kopano/ecversion.h>
#include <kopano/ECRestriction.h>
#include <kopano/mapiext.h>
#include <kopano/timeutil.hpp>
#include <kopano/Util.h>
#include "mapitovcf.hpp"
#include "icaluid.h"

namespace KC {

class mapitovcf_impl final : public mapitovcf {
	public:
	HRESULT add_message(IMessage *) override;
	HRESULT finalize(std::string *) override;

	private:
	bool prop_is_empty(const SPropValue &s) const;
	inline bool prop_is_empty(const SPropValue *s) const
	{
		return s == nullptr || prop_is_empty(*s);
	}

	VObject *to_prop(VObject *node, const char *prop, const SPropValue &value);
	VObject *to_prop(VObject *node, const char *prop, const wchar_t *value);
	HRESULT add_photo(IMessage *lpMessage, VObject *root);

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

bool mapitovcf_impl::prop_is_empty(const SPropValue &s) const {
	if (PROP_TYPE(s.ulPropTag) == PT_UNICODE && wcslen(s.Value.lpszW) == 0)
		return true;
	else if (PROP_TYPE(s.ulPropTag) == PT_STRING8 && strlen(s.Value.lpszA) == 0)
		return true;
	else if (PROP_TYPE(s.ulPropTag) == PT_BINARY && s.Value.bin.cb == 0)
		return true;
	// assume everything else is not empty
	return false;
}

VObject *mapitovcf_impl::to_prop(VObject *node, const char *prop,
    const SPropValue &s)
{
	if (prop_is_empty(s))
		return nullptr;
	auto newnode = addProp(node, prop);
	if (newnode == nullptr)
		return nullptr;
	if (PROP_TYPE(s.ulPropTag) == PT_UNICODE) {
		auto str = convert_to<std::string>("utf-8", s.Value.lpszW, rawsize(s.Value.lpszW), CHARSET_WCHAR);
		setVObjectStringZValue(newnode, str.c_str());
	}
	else if (PROP_TYPE(s.ulPropTag) == PT_STRING8)
		setVObjectStringZValue(newnode, s.Value.lpszA);
	else if (PROP_TYPE(s.ulPropTag) == PT_BINARY) {
		auto str = bin2hex(s.Value.bin);
		setVObjectStringZValue(newnode, str.c_str());
	}
	else if (PROP_TYPE(s.ulPropTag) == PT_SYSTIME) {
		struct tm t;
		char buf[21];
		gmtime_safe(FileTimeToUnixTime(s.Value.ft), &t);
		if (t.tm_hour == 0 && t.tm_min == 0 && t.tm_sec == 0)
			strftime(buf, 21, "%Y-%m-%d", &t);
		else
			strftime(buf, 21, "%Y-%m-%dT%H:%M:%SZ", &t);
		setVObjectStringZValue(newnode, buf);
	}
	return newnode;
}

VObject *mapitovcf_impl::to_prop(VObject *node, const char *prop,
    const wchar_t *value)
{
	if (node == nullptr || value == nullptr || wcslen(value) == 0)
		return nullptr;
	auto newnode = addProp(node, prop);
	if (newnode == nullptr)
		return nullptr;
	auto str = convert_to<std::string>("utf-8", value, rawsize(value), CHARSET_WCHAR);
	setVObjectStringZValue(newnode, str.c_str());
	return newnode;
}

HRESULT mapitovcf_impl::add_photo(IMessage *lpMessage, VObject *root)
{
	object_ptr<IMAPITable> table;
	static constexpr const SizedSPropTagArray(2, columns) =
		{2, { PR_ATTACH_NUM, PR_ATTACH_MIME_TAG_W }};

	auto hr = lpMessage->GetAttachmentTable(0, &~table);
	if (hr != hrSuccess)
		return hr;

	hr = table->SetColumns(columns, 0);
	if (hr != hrSuccess)
		return hr;

	SPropValue prop;
	prop.ulPropTag = PR_ATTACHMENT_CONTACTPHOTO;
	prop.Value.b = true;

	memory_ptr<SRestriction> restriction;
	hr = ECPropertyRestriction(RELOP_EQ, PR_ATTACHMENT_CONTACTPHOTO, &prop, ECRestriction::Cheap).CreateMAPIRestriction(&~restriction, ECRestriction::Cheap);
	if (hr != hrSuccess)
		return hr;

	hr = table->Restrict(restriction, MAPI_DEFERRED_ERRORS);
	if (hr != hrSuccess)
		return hr;

	rowset_ptr rows;
	hr = HrQueryAllRows(table, nullptr, nullptr, nullptr, 0, &~rows);
	if (hr != hrSuccess)
		return hr;

	if (rows->cRows == 0)
		return hrSuccess;

	auto attach_num_prop = rows[0].cfind(PR_ATTACH_NUM);
	if (attach_num_prop == nullptr)
		return MAPI_E_CALL_FAILED;

	object_ptr<IAttach> attach;
	hr = lpMessage->OpenAttach(attach_num_prop->Value.ul, nullptr, MAPI_BEST_ACCESS, &~attach);
	if (hr != hrSuccess)
		return hr;

	object_ptr<IStream> stream;
	hr = attach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0, MAPI_DEFERRED_ERRORS, &~stream);
	if (hr != hrSuccess)
		return hr;

	std::string bytes;
	hr = Util::HrStreamToString(stream, bytes);
	if (hr != hrSuccess)
		return hr;

	auto encoded_bytes = convert_to<std::wstring>(base64_encode(bytes.c_str(), bytes.size()));
	auto node = to_prop(root, "PHOTO", encoded_bytes.c_str());
	to_prop(node, "ENCODING", L"b");

	auto attach_mime_tag = rows[0].cfind(PR_ATTACH_MIME_TAG_W);
	if (attach_mime_tag == nullptr)
		return hrSuccess;

	if (wcscmp(attach_mime_tag->Value.lpszW, L"image/jpeg") == 0)
		to_prop(node, "TYPE", L"JPEG");
	else if (wcscmp(attach_mime_tag->Value.lpszW, L"image/png") == 0)
		to_prop(node, "TYPE", L"PNG");
	else if (wcscmp(attach_mime_tag->Value.lpszW, L"image/gif") == 0)
		to_prop(node, "TYPE", L"GIF");

	return hrSuccess;
}

static const SPropValue *tagbsearch(const SPropValue *pv, size_t z, unsigned int tag)
{
	auto iter = std::lower_bound(&pv[0], &pv[z], tag,
		[](const SPropValue &a, unsigned int tag) { return a.ulPropTag < tag; });
	return iter != &pv[z] && iter->ulPropTag == tag ? iter : nullptr;
}

HRESULT mapitovcf_impl::add_message(IMessage *lpMessage)
{
#define PA const_cast<GUID *>(&PSETID_Address)
#define PM const_cast<GUID *>(&PSETID_Meeting)
#define FIND(xtag) tagbsearch(proplist, pnum, (xtag))
#define ADD(xnode, xkey, xtag) \
	({ \
		auto sp = tagbsearch(proplist, pnum, (xtag)); \
		sp != nullptr ? to_prop((xnode), (xkey), *sp) : nullptr; \
	})
#define FIND_N2(xsect, xtag, xtype) \
	({ \
		auto i = std::find_if(&nameids[0], &nameids[ARRAY_SIZE(nameids)], \
			[](const MAPINAMEID &a) { return memcmp(a.lpguid, (xsect), sizeof(*(xsect))) == 0 && a.Kind.lID == (xtag); }); \
		i != &nameids[ARRAY_SIZE(nameids)] ? FIND(CHANGE_PROP_TYPE(namtags->aulPropTag[i - &nameids[0]], (xtype))) : nullptr; \
	})
#define FIND_N(xsect, xtag) FIND_N2((xsect), (xtag), PT_UNICODE)
#define ADD_N(xnode, xkey, xsect, xtag) \
	({ \
		auto i = std::find_if(&nameids[0], &nameids[ARRAY_SIZE(nameids)], \
			[](const MAPINAMEID &a) { return memcmp(a.lpguid, (xsect), sizeof(*(xsect))) == 0 && a.Kind.lID == (xtag); }); \
		i != &nameids[ARRAY_SIZE(nameids)] ? ADD((xnode), (xkey), CHANGE_PROP_TYPE(namtags->aulPropTag[i - &nameids[0]], PT_UNICODE)) : nullptr; \
	})

	if (lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;

	memory_ptr<SPropValue> proplist;
	unsigned int pnum = 0;
	auto hr = HrGetAllProps(lpMessage, MAPI_UNICODE, &pnum, &~proplist);
	if (FAILED(hr))
		return hr;
	std::sort(&proplist[0], &proplist[pnum],
		[](const SPropValue &a, const SPropValue &b) { return a.ulPropTag < b.ulPropTag; });

	auto sp = FIND(PR_MESSAGE_CLASS);
	if (sp == nullptr || _tcscmp(sp->Value.LPSZ, KC_T("IPM.Contact")) != 0)
		return MAPI_E_INVALID_PARAMETER;

	MAPINAMEID nameids[] = {
		{PM, MNID_ID, dispidGlobalObjectID},
		{PA, MNID_ID, dispidWorkAddressStreet},
		{PA, MNID_ID, dispidWorkAddressCity},
		{PA, MNID_ID, dispidWorkAddressState},
		{PA, MNID_ID, dispidWorkAddressPostalCode},
		{PA, MNID_ID, dispidWorkAddressCountry},
		{PA, MNID_ID, dispidEmail1Address},
		{PA, MNID_ID, dispidEmail2Address},
		{PA, MNID_ID, dispidEmail3Address},
		{PA, MNID_ID, dispidWebPage},
	}, *nameids_ptrs[ARRAY_SIZE(nameids)];
	for (size_t i = 0; i < ARRAY_SIZE(nameids); ++i)
		nameids_ptrs[i] = &nameids[i];

	memory_ptr<SPropTagArray> namtags;
	hr = lpMessage->GetIDsFromNames(ARRAY_SIZE(nameids_ptrs), nameids_ptrs, MAPI_BEST_ACCESS, &~namtags);
	if (FAILED(hr))
		return hr;
	namtags->aulPropTag[0] = CHANGE_PROP_TYPE(namtags->aulPropTag[0], PT_BINARY);
	for (size_t i = 1; i < ARRAY_SIZE(nameids); ++i)
		namtags->aulPropTag[i] = CHANGE_PROP_TYPE(namtags->aulPropTag[i], PT_UNICODE);

	auto root = newVObject(VCCardProp);
	to_prop(root, "VERSION", L"3.0");
	auto prodid = L"-//Kopano//libicalmapi " + convert_to<std::wstring>(PROJECT_VERSION) + L"//EN";
	to_prop(root, "PRODID", prodid.c_str());

	if (!prop_is_empty(FIND(PR_DISPLAY_NAME_PREFIX)) ||
	    !prop_is_empty(FIND(PR_GIVEN_NAME)) ||
	    !prop_is_empty(FIND(PR_MIDDLE_NAME)) ||
	    !prop_is_empty(FIND(PR_SURNAME)) ||
	    !prop_is_empty(FIND(PR_GENERATION))) {
		auto node = addGroup(root, VCNameProp);
		ADD(node, VCNamePrefixesProp, PR_DISPLAY_NAME_PREFIX);
		ADD(node, VCGivenNameProp, PR_GIVEN_NAME);
		ADD(node, VCAdditionalNamesProp, PR_MIDDLE_NAME);
		ADD(node, VCFamilyNameProp, PR_SURNAME);
		ADD(node, VCNameSuffixesProp, PR_GENERATION);
	}
	ADD(root, VCFullNameProp, PR_DISPLAY_NAME);
	ADD(root, VCTitleProp, PR_TITLE);
	ADD(root, "NICKNAME", PR_NICKNAME);

	if (!prop_is_empty(FIND(PR_COMPANY_NAME)) ||
	    !prop_is_empty(FIND(PR_DEPARTMENT_NAME))) {
		auto node = addGroup(root, VCOrgProp);
		ADD(node, VCOrgNameProp, PR_COMPANY_NAME);
		ADD(node, VCOrgUnitProp, PR_DEPARTMENT_NAME);
	}

	auto node = ADD(root, VCTelephoneProp, PR_PRIMARY_TELEPHONE_NUMBER);
	if (node != nullptr)
		to_prop(node, "TYPE", L"MAIN");
	node = ADD(root, VCTelephoneProp, PR_HOME_TELEPHONE_NUMBER);
	if (node != nullptr)
		to_prop(node, "TYPE", L"HOME");
	node = ADD(root, VCTelephoneProp, PR_MOBILE_TELEPHONE_NUMBER);
	if (node != nullptr)
		to_prop(node, "TYPE", L"MOBILE");
	node = ADD(root, VCTelephoneProp, PR_BUSINESS_TELEPHONE_NUMBER);
	if (node != nullptr)
		to_prop(node, "TYPE", L"WORK");
	node = ADD(root, VCTelephoneProp, PR_PAGER_TELEPHONE_NUMBER);
	if (node != nullptr)
		to_prop(node, "TYPE", L"PAGER");
	node = ADD(root, VCTelephoneProp, PR_PRIMARY_FAX_NUMBER);
	if (node != nullptr) {
		to_prop(node, "TYPE", L"MAIN");
		to_prop(node, "TYPE", L"FAX");
	}
	node = ADD(root, VCTelephoneProp, PR_HOME_FAX_NUMBER);
	if (node != nullptr) {
		to_prop(node, "TYPE", L"HOME");
		to_prop(node, "TYPE", L"FAX");
	}
	node = ADD(root, VCTelephoneProp, PR_BUSINESS_FAX_NUMBER);
	if (node != nullptr) {
		to_prop(node, "TYPE", L"WORK");
		to_prop(node, "TYPE", L"FAX");
	}

	if (!prop_is_empty(FIND(PR_HOME_ADDRESS_STREET)) ||
	    !prop_is_empty(FIND(PR_HOME_ADDRESS_CITY)) ||
	    !prop_is_empty(FIND(PR_HOME_ADDRESS_STATE_OR_PROVINCE)) ||
	    !prop_is_empty(FIND(PR_HOME_ADDRESS_POSTAL_CODE)) ||
	    !prop_is_empty(FIND(PR_HOME_ADDRESS_COUNTRY))) {
		auto adrnode = addProp(root, VCAdrProp);
		auto node = addProp(adrnode, "TYPE");
		setVObjectStringZValue(node, "HOME");
		ADD(adrnode, VCStreetAddressProp, PR_HOME_ADDRESS_STREET);
		ADD(adrnode, VCCityProp, PR_HOME_ADDRESS_CITY);
		ADD(adrnode, VCRegionProp, PR_HOME_ADDRESS_STATE_OR_PROVINCE);
		ADD(adrnode, VCPostalCodeProp, PR_HOME_ADDRESS_POSTAL_CODE);
		ADD(adrnode, VCCountryNameProp, PR_HOME_ADDRESS_COUNTRY);
	}
	if (!prop_is_empty(FIND(PR_OTHER_ADDRESS_STREET)) ||
	    !prop_is_empty(FIND(PR_OTHER_ADDRESS_CITY)) ||
	    !prop_is_empty(FIND(PR_OTHER_ADDRESS_STATE_OR_PROVINCE)) ||
	    !prop_is_empty(FIND(PR_OTHER_ADDRESS_POSTAL_CODE)) ||
	    !prop_is_empty(FIND(PR_OTHER_ADDRESS_COUNTRY))) {
		auto adrnode = addProp(root, VCAdrProp);
		ADD(adrnode, VCStreetAddressProp, PR_OTHER_ADDRESS_STREET);
		ADD(adrnode, VCCityProp, PR_OTHER_ADDRESS_CITY);
		ADD(adrnode, VCRegionProp, PR_OTHER_ADDRESS_STATE_OR_PROVINCE);
		ADD(adrnode, VCPostalCodeProp, PR_OTHER_ADDRESS_POSTAL_CODE);
		ADD(adrnode, VCCountryNameProp, PR_OTHER_ADDRESS_COUNTRY);
	}
	if (!prop_is_empty(FIND_N(PA, dispidWorkAddressStreet)) ||
	    !prop_is_empty(FIND_N(PA, dispidWorkAddressCity)) ||
	    !prop_is_empty(FIND_N(PA, dispidWorkAddressState)) ||
	    !prop_is_empty(FIND_N(PA, dispidWorkAddressPostalCode)) ||
	    !prop_is_empty(FIND_N(PA, dispidWorkAddressCountry))) {
		auto adrnode = addProp(root, VCAdrProp);
		to_prop(adrnode, "TYPE", L"WORK");
		ADD_N(adrnode, VCStreetAddressProp, PA, dispidWorkAddressStreet);
		ADD_N(adrnode, VCCityProp, PA, dispidWorkAddressCity);
		ADD_N(adrnode, VCRegionProp, PA, dispidWorkAddressState);
		ADD_N(adrnode, VCPostalCodeProp, PA, dispidWorkAddressPostalCode);
		ADD_N(adrnode, VCCountryNameProp, PA, dispidWorkAddressCountry);
	}

	node = ADD_N(root, VCEmailAddressProp, PA, dispidEmail1Address);
	if (node != nullptr)
		to_prop(node, "TYPE", L"INTERNET,PREF");
	node = ADD_N(root, VCEmailAddressProp, PA, dispidEmail2Address);
	if (node != nullptr)
		to_prop(node, "TYPE", L"INTERNET");
	node = ADD_N(root, VCEmailAddressProp, PA, dispidEmail3Address);
	if (node != nullptr)
		to_prop(node, "TYPE", L"INTERNET");

	std::string icaluid;
	sp = FIND_N2(PM, dispidGlobalObjectID, PT_BINARY);
	if (sp != nullptr) {
		HrGetICalUidFromBinUid(sp->Value.bin, &icaluid);
	} else {
		HrGenerateUid(&icaluid);
		auto binstr = hex2bin(icaluid);
		SPropValue uprop;
		uprop.ulPropTag     = CHANGE_PROP_TYPE(namtags->aulPropTag[0], PT_BINARY);
		uprop.Value.bin.lpb = reinterpret_cast<BYTE *>(const_cast<char *>(binstr.c_str()));
		uprop.Value.bin.cb  = binstr.length();
		hr = HrSetOneProp(lpMessage, &uprop);
		if (hr == hrSuccess)
			lpMessage->SaveChanges(0);
	}
	to_prop(root, "UID", convert_to<std::wstring>(icaluid).c_str());

	ADD_N(root, VCURLProp, PA, dispidWebPage);
	ADD(root, VCNoteProp, PR_BODY);
	ADD(root, VCBirthDateProp, PR_BIRTHDAY);
	ADD(root, VCLastRevisedProp, PR_LAST_MODIFICATION_TIME);

	hr = add_photo(lpMessage, root);
	if (hr != hrSuccess)
		return hr;

	/* Write memobject */
	int len = 0;
	auto cresult = writeMemVObject(nullptr, &len, root);
	if (cresult == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	m_result = cresult;
	free(cresult);
	cleanVObject(root);
	return hrSuccess;
#undef ADD
#undef ADD_N
#undef FIND
#undef FIND_N
#undef FIND_N2
#undef PA
#undef PM
}

HRESULT mapitovcf_impl::finalize(std::string *s)
{
	if (s == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	*s = m_result;
	return hrSuccess;
}

} /* namespace */
