/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2017 - Kopano and its licensors
 */
#include <algorithm>
#include <memory>
#include <new>
#include <utility>
#include <kopano/ECLogger.h>
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
#include "icaluid.h"
#include "nameids.h"

namespace KC {

class vcftomapi_impl final : public vcftomapi {
	public:
	/*
	 * - lpPropObj to lookup named properties
	 * - Addressbook (Global AddressBook for looking up users)
	 */
	vcftomapi_impl(IMAPIProp *o) : vcftomapi(o) {}
	HRESULT parse_vcf(const std::string &) override;
	HRESULT get_item(IMessage *) override;

	private:
	HRESULT save_photo(IMessage *);
	HRESULT save_props(const std::list<SPropValue> &, IMessage *);
	HRESULT handle_N(VObject *);
	HRESULT handle_TEL(VObject *);
	HRESULT handle_EMAIL(VObject *);
	HRESULT handle_ADR(VObject *);
	HRESULT handle_UID(VObject *);
	HRESULT handle_ORG(VObject *);
	HRESULT handle_PHOTO(VObject *);
	HRESULT vobject_to_prop(VObject *, SPropValue &, ULONG proptype);
	HRESULT vobject_to_named_prop(VObject *, SPropValue &, ULONG named_proptype);
	HRESULT unicode_to_named_prop(const wchar_t *, SPropValue &, ULONG named_proptype);

	size_t email_count = 0;
};

struct ical_deleter {
	void operator()(VObject *x) { cleanVObject(x); }
};

static int check_libical_bug_353()
{
	char utf8string[] = "BEGIN:VCARD\n" "VERSION:3.0\n" "N:\xd0\x91\xd0\x9d;\xd0\x95\n" "END:VCARD\n";
	std::unique_ptr<VObject, ical_deleter> root(Parse_MIME(utf8string, strlen(utf8string)));
	if (root == nullptr)
		return -1;
	VObjectIterator t;
	for (initPropIterator(&t, root.get()); moreIteration(&t); ) {
		auto v = nextVObject(&t);
		if (strcmp(vObjectName(v), VCNameProp) != 0)
			continue;
		VObjectIterator tt;
		initPropIterator(&tt, v);
		if (!moreIteration(&tt))
			return -1;
		v = nextVObject(&tt);
		int type = vObjectValueType(v);
		if (type != VCVT_USTRINGZ)
			return -1;
		auto s = vObjectUStringZValue(v);
		if (s == nullptr)
			return -1;
		if (*s != L'Б')
			return 1;
	}
	return 0;
}

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

static bool date_string_to_filetime(const std::string &date_string, FILETIME &filetime)
{
	struct tm t;
	memset(&t, 0, sizeof(struct tm));
	auto s = strptime(date_string.c_str(), "%Y-%m-%d", &t);
	if (s == nullptr || *s != '\0')
		s = strptime(date_string.c_str(), "%Y%m%d", &t);
	if (s == nullptr || *s != '\0')
		return false;
	filetime = UnixTimeToFileTime(timegm(&t));
	return true;
}

HRESULT vcftomapi_impl::handle_N(VObject *v)
{
	VObjectIterator tt;

	for (initPropIterator(&tt, v); moreIteration(&tt); ) {
		auto vv = nextVObject(&tt);
		auto name = vObjectName(vv);
		SPropValue s;

		if (strcmp(name, VCFamilyNameProp) == 0) {
			auto ret = vobject_to_prop(vv, s, PR_SURNAME);
			if (ret != hrSuccess)
				return ret;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, VCGivenNameProp) == 0) {
			auto ret = vobject_to_prop(vv, s, PR_GIVEN_NAME);
			if (ret != hrSuccess)
				return ret;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, VCAdditionalNamesProp) == 0) {
			auto ret = vobject_to_prop(vv, s, PR_MIDDLE_NAME);
			if (ret != hrSuccess)
				return ret;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, VCNameSuffixesProp) == 0) {
			auto ret = vobject_to_prop(vv, s, PR_GENERATION);
			if (ret != hrSuccess)
				return ret;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, VCNamePrefixesProp) == 0) {
			auto ret = vobject_to_prop(vv, s, PR_DISPLAY_NAME_PREFIX);
			if (ret != hrSuccess)
				return ret;
			props.emplace_back(std::move(s));
		}
	}
	return hrSuccess;
}

HRESULT vcftomapi_impl::handle_TEL(VObject *v)
{
	VObjectIterator t;

	for (initPropIterator(&t, v); moreIteration(&t); ) {
		std::vector<std::string> tokens;
		while (true) {
			auto vv = nextVObject(&t);
			auto name = vv ? vObjectName(vv) : nullptr;
			if (vv == nullptr || name == nullptr)
				break;
			auto namep = strcasecmp(name, "TYPE") == 0 ? vObjectStringZValue(vv) : name;
			auto tokenized = tokenize(namep, ",");
			tokens.insert(tokens.end(), tokenized.cbegin(), tokenized.cend());
		}

		SPropValue s;
		bool is_fax = std::find(tokens.cbegin(), tokens.cend(), "FAX") != tokens.cend();

		for (auto const &token : tokens) {
			if (strcasecmp(token.c_str(), "HOME") == 0) {
				auto prop = is_fax ? PR_HOME_FAX_NUMBER : PR_HOME_TELEPHONE_NUMBER;
				auto ret = vobject_to_prop(v, s, prop);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			}
			else if (strcasecmp(token.c_str(), "MOBILE") == 0 || strcasecmp(token.c_str(), "CELL") == 0) {
				if (is_fax)
					continue;

				auto ret = vobject_to_prop(v, s, PR_MOBILE_TELEPHONE_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			}
			else if (strcasecmp(token.c_str(), "WORK") == 0) {
				auto prop = is_fax ? PR_BUSINESS_FAX_NUMBER : PR_BUSINESS_TELEPHONE_NUMBER;
				auto ret = vobject_to_prop(v, s, prop);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			}
			else if (strcasecmp(token.c_str(), "MAIN") == 0) {
				auto prop = is_fax ? PR_PRIMARY_FAX_NUMBER : PR_PRIMARY_TELEPHONE_NUMBER;
				auto ret = vobject_to_prop(v, s, prop);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			}
			else if (strcasecmp(token.c_str(), "PAGER") == 0) {
				if (is_fax)
					continue;

				auto ret = vobject_to_prop(v, s, PR_PAGER_TELEPHONE_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			} else if (strcasecmp(token.c_str(), "CAR") == 0) {
				auto ret = vobject_to_prop(v, s, PR_CAR_TELEPHONE_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			} else if (strcasecmp(token.c_str(), "ISDN") == 0) {
				auto ret = vobject_to_prop(v, s, PR_ISDN_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			} else if (strcasecmp(token.c_str(), "ASSISTANT") == 0) {
				auto ret = vobject_to_prop(v, s, PR_ASSISTANT_TELEPHONE_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			} else if (strcasecmp(token.c_str(), "X-EVOLUTION-CALLBACK") == 0) {
				auto ret = vobject_to_prop(v, s, PR_CALLBACK_TELEPHONE_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			} else if (strcasecmp(token.c_str(), "X-EVOLUTION-RADIO") == 0) {
				auto ret = vobject_to_prop(v, s, PR_RADIO_TELEPHONE_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			} else if (strcasecmp(token.c_str(), "X-EVOLUTION-TELEX") == 0) {
				auto ret = vobject_to_prop(v, s, PR_TELEX_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			} else if (strcasecmp(token.c_str(), "TEXTPHONE") == 0 ||
			    strcasecmp(token.c_str(), "X-EVOLUTION-TTYTTD") == 0) {
				auto ret = vobject_to_prop(v, s, PR_TTYTDD_PHONE_NUMBER);
				if (ret != hrSuccess)
					return ret;
				props.emplace_back(std::move(s));
			}
		}
	}
	return hrSuccess;
}

HRESULT vcftomapi_impl::handle_EMAIL(VObject *v)
{
	// we can only accept 3 email addresses
	if (email_count > 2)
		return hrSuccess;

	unsigned int prop_id = 0x8083 + (email_count * 0x10);
	SPropValue s;
	vobject_to_named_prop(v, s, prop_id);
	props.emplace_back(std::move(s));

	prop_id = 0x8084 + (email_count * 0x10);
	vobject_to_named_prop(v, s, prop_id);
	props.emplace_back(std::move(s));

	// add email as displayname
	prop_id = 0x8080 + (email_count * 0x10);
	auto dname = std::wstring(L"(") + vObjectUStringZValue(v) + std::wstring(L")");
	unicode_to_named_prop(dname.c_str(), s, prop_id);
	props.emplace_back(std::move(s));

	prop_id = 0x8082 + (email_count * 0x10);
	auto ret = unicode_to_named_prop(L"SMTP", s, prop_id);
	if (ret != hrSuccess)
		return ret;
	props.emplace_back(std::move(s));

	email_count++;
	return hrSuccess;
}

HRESULT vcftomapi_impl::handle_ADR(VObject *v)
{
	enum { OTHER, WORK, HOME } adr_type = OTHER;
	VObjectIterator t;

	initPropIterator(&t, v);
	if (moreIteration(&t)) {
		auto vv = nextVObject(&t);
		auto name = vObjectName(vv);

		const char *namep = strcmp(name, "TYPE") == 0 ? vObjectStringZValue(vv) : name;
		if (strcmp(namep, "HOME") == 0)
			adr_type = HOME;
		else if (strcmp(namep, "WORK") == 0)
			adr_type = WORK;
	}

	while (moreIteration(&t)) {
		auto vv = nextVObject(&t);
		auto name = vObjectName(vv);
		SPropValue s;

		s.ulPropTag = 0;

		if (adr_type == HOME && !strcmp(name, "STREET"))
			vobject_to_prop(vv, s, PR_HOME_ADDRESS_STREET);
		else if (adr_type == HOME && !strcmp(name, "L"))
			vobject_to_prop(vv, s, PR_HOME_ADDRESS_CITY);
		else if (adr_type == HOME && !strcmp(name, "R"))
			vobject_to_prop(vv, s, PR_HOME_ADDRESS_STATE_OR_PROVINCE);
		else if (adr_type == HOME && !strcmp(name, "PC"))
			vobject_to_prop(vv, s, PR_HOME_ADDRESS_POSTAL_CODE);
		else if (adr_type == HOME && !strcmp(name, "C"))
			vobject_to_prop(vv, s, PR_HOME_ADDRESS_COUNTRY);
		else if (adr_type == WORK && !strcmp(name, "STREET"))
			vobject_to_named_prop(vv, s, 0x8045);
		else if (adr_type == WORK && !strcmp(name, "L"))
			vobject_to_named_prop(vv, s, 0x8046);
		else if (adr_type == WORK && !strcmp(name, "R"))
			vobject_to_named_prop(vv, s, 0x8047);
		else if (adr_type == WORK && !strcmp(name, "PC"))
			vobject_to_named_prop(vv, s, 0x8048);
		else if (adr_type == WORK && !strcmp(name, "C"))
			vobject_to_named_prop(vv, s, 0x8049);
		else if (adr_type == OTHER && !strcmp(name, "STREET"))
			vobject_to_prop(vv, s, PR_OTHER_ADDRESS_STREET);
		else if (adr_type == OTHER && !strcmp(name, "L"))
			vobject_to_prop(vv, s, PR_OTHER_ADDRESS_CITY);
		else if (adr_type == OTHER && !strcmp(name, "R"))
			vobject_to_prop(vv, s, PR_OTHER_ADDRESS_STATE_OR_PROVINCE);
		else if (adr_type == OTHER && !strcmp(name, "PC"))
			vobject_to_prop(vv, s, PR_OTHER_ADDRESS_POSTAL_CODE);
		else if (adr_type == OTHER && !strcmp(name, "C"))
			vobject_to_prop(vv, s, PR_OTHER_ADDRESS_COUNTRY);

		if (s.ulPropTag > 0)
			props.emplace_back(std::move(s));
	}

	return hrSuccess;
}

HRESULT vcftomapi_impl::handle_UID(VObject *v)
{
	auto value_type = vObjectValueType(v);
	if (value_type != VCVT_USTRINGZ)
		return MAPI_E_INVALID_PARAMETER;

	auto uid_wstring = vObjectUStringZValue(v);
	auto uid_string = convert_to<std::string>(uid_wstring);

	memory_ptr<SPropValue> prop;
	auto hr = MAPIAllocateBuffer(sizeof(SPropValue), &~prop);
	if (hr != hrSuccess)
		return hr;

	hr = HrMakeBinaryUID(uid_string, prop, prop);
	if (hr != hrSuccess)
		return hr;

	MAPINAMEID name, *namep = &name;
	memory_ptr<SPropTagArray> proptag;

	name.lpguid = const_cast<GUID *>(&PSETID_Meeting);
	name.ulKind = MNID_ID;
	name.Kind.lID = dispidGlobalObjectID;
	hr = m_propobj->GetIDsFromNames(1, &namep, MAPI_CREATE, &~proptag);
	if (FAILED(hr))
		return hr;

	SPropValue s;
	s.ulPropTag = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_BINARY);
	s.Value.bin.cb = prop->Value.bin.cb;
	hr = KAllocCopy(prop->Value.bin.lpb, prop->Value.bin.cb, reinterpret_cast<void **>(&s.Value.bin.lpb));
	if (hr != hrSuccess)
		return hr;
	props.emplace_back(std::move(s));

	return hrSuccess;
}

HRESULT vcftomapi_impl::handle_ORG(VObject *v)
{
	VObjectIterator t;
	initPropIterator(&t, v);
	if (!moreIteration(&t))
		return hrSuccess;

	SPropValue s;
	auto vv = nextVObject(&t);
	auto name = vObjectName(vv);
	if (strcmp(name, "ORGNAME") == 0) {
		auto hr = vobject_to_prop(vv, s, PR_COMPANY_NAME);
		if (hr != hrSuccess)
			return hr;
		props.emplace_back(std::move(s));
	}
	if (strcmp(name, "OUN") == 0) {
		auto hr = vobject_to_prop(vv, s, PR_DEPARTMENT_NAME);
		if (hr != hrSuccess)
			return hr;
		props.emplace_back(std::move(s));
	}
	return hrSuccess;
}

HRESULT vcftomapi_impl::handle_PHOTO(VObject *v)
{
	phototype = PHOTO_NONE;
	bool base64 = false;

	VObjectIterator t;
	for (initPropIterator(&t, v); moreIteration(&t); ) {
		auto vv = nextVObject(&t);
		auto name = vObjectName(vv);
		std::string value;
		if(vObjectValueType(vv) == VCVT_USTRINGZ)
			value = convert_to<std::string>(vObjectUStringZValue(vv));
		else if(vObjectValueType(vv) == VCVT_STRINGZ)
			value = convert_to<std::string>(vObjectStringZValue(vv));

		if (strcmp(name, "ENCODING") == 0 && (strcmp(value.c_str(), "b") == 0 || strcmp(value.c_str(), "BASE64"))) {
			base64 = true;
			continue;
		}

		std::pair<std::string, photo_type_enum> mapping[] = { { "JPEG", PHOTO_JPEG }, { "PNG", PHOTO_PNG }, { "GIF", PHOTO_GIF } };
		for (const auto &elem : mapping) {
			auto real_value = strcmp(name, "TYPE") == 0 ? value : name;
			if (elem.first != real_value)
				continue;
			phototype = elem.second;
			break;
		}
	}

	if (!base64 || vObjectValueType(v) != VCVT_USTRINGZ)
		phototype = PHOTO_NONE;

	if (phototype == PHOTO_NONE)
		return hrSuccess;

	auto tmp = convert_to<std::string>(vObjectUStringZValue(v));
	std::copy_if(tmp.cbegin(), tmp.cend(), std::back_inserter(photo), [&](char c) { return c != ' '; });
	return hrSuccess;
}

/**
 * Parses an ICal string (with a certain charset) and converts the
 * data in memory. The real MAPI object can be retrieved using
 * GetItem().
 */
HRESULT vcftomapi_impl::parse_vcf(const std::string &ical)
{
	/* Handle libicalvcal bug: The library does not allow
	 *
	 * PHOTO;TYPE=JPEG;ENCODING=BASE64:
	 *   .... base64 data ....
	 *
	 * Therefore we work around it and replace ":\r\n " or ":\n "
	 * with ":".
	 */

	auto tmp_ical = ical;
	while (true) {
		auto pos = tmp_ical.find(":\r\n ");
		if (pos != std::string::npos) {
			tmp_ical.replace(pos, 4, ":");
			continue;
		}
		pos = tmp_ical.find(":\n ");
		if (pos == std::string::npos)
			break;
		tmp_ical.replace(pos, 3, ":");
	}

	if (check_libical_bug_353())
		ec_log_warn("libical bug #353 detected. VCF import can produce garbage. (KC-1247)");
	std::unique_ptr<VObject, ical_deleter> root(Parse_MIME(tmp_ical.c_str(), tmp_ical.length()));
	if (root == nullptr)
		return MAPI_E_CORRUPT_DATA;

	VObjectIterator t;
	for (initPropIterator(&t, root.get()); moreIteration(&t); ) {
		SPropValue s;
		auto v = nextVObject(&t);
		auto name = vObjectName(v);

		if (strcmp(name, VCNameProp) == 0) {
			auto hr = handle_N(v);
			if (hr != hrSuccess)
				return hr;
		} else if (strcmp(name, VCFullNameProp) == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_DISPLAY_NAME);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, VCTitleProp) == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_TITLE);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, "URL") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_named_prop(v, s, dispidWebPage);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, "NICKNAME") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_NICKNAME);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcasecmp(name, "X-KADDRESSBOOK-X-Profession") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_PROFESSION);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcasecmp(name, "X-KADDRESSBOOK-X-SpouseName") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_SPOUSE_NAME);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcasecmp(name, "X-KADDRESSBOOK-X-ManagersName") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_MANAGER_NAME);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcasecmp(name, "X-KADDRESSBOOK-X-AssistantsName") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_ASSISTANT);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcasecmp(name, "X-KADDRESSBOOK-X-Office") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_OFFICE_LOCATION);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, "NOTE") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			auto hr = vobject_to_prop(v, s, PR_BODY);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, "BDAY") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			FILETIME filetime;
			auto input = convert_to<std::string>(vObjectUStringZValue(v));
			auto res = date_string_to_filetime(input, filetime);
			if (!res)
				continue;

			s.ulPropTag = PR_BIRTHDAY;
			s.Value.ft = filetime;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, "X-ANNIVERSAY") == 0 && vObjectValueType(v) != VCVT_NOVALUE) {
			FILETIME filetime;
			auto res = date_string_to_filetime(convert_to<std::string>(vObjectUStringZValue(v)), filetime);
			if (!res)
				continue;
			s.ulPropTag = PR_WEDDING_ANNIVERSARY;
			s.Value.ft = filetime;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, VCOrgProp) == 0) {
			auto hr = handle_ORG(v);
			if (hr != hrSuccess)
				return hr;
		} else if (strcmp(name, VCTelephoneProp) == 0) {
			auto hr = handle_TEL(v);
			if (hr != hrSuccess)
				return hr;
		} else if (strcmp(name, VCEmailAddressProp) == 0) {
			auto hr = handle_EMAIL(v);
			if (hr != hrSuccess)
				return hr;
		} else if (strcmp(name, "IMPP") == 0) {
			auto hr = vobject_to_named_prop(v, s, dispidInstMsg);
			if (hr != hrSuccess)
				return hr;
			props.emplace_back(std::move(s));
		} else if (strcmp(name, VCAdrProp) == 0) {
			auto hr = handle_ADR(v);
			if (hr != hrSuccess)
				return hr;
		} else if (strcmp(name, "UID") == 0) {
			auto hr = handle_UID(v);
			if (hr != hrSuccess)
				return hr;
		} else if (strcmp(name, "PHOTO") == 0) {
			auto hr = handle_PHOTO(v);
			if (hr != hrSuccess)
				return hr;
		}
	}
	return hrSuccess;
}

HRESULT vcftomapi_impl::vobject_to_prop(VObject *v, SPropValue &s, ULONG proptype)
{
	auto value_type = vObjectValueType(v);
	if (value_type == VCVT_STRINGZ) {
		s.ulPropTag = CHANGE_PROP_TYPE(proptype, PT_STRING8);
		auto val = vObjectStringZValue(v);
		auto ret = MAPIAllocateBuffer(strlen(val) + 1, reinterpret_cast<void **>(&s.Value.lpszA));
		if (ret != hrSuccess)
			return ret;
		strcpy(s.Value.lpszA, val);
	}
	else if (value_type == VCVT_USTRINGZ) {
		s.ulPropTag = CHANGE_PROP_TYPE(proptype, PT_UNICODE);
		auto uval = vObjectUStringZValue(v);
		auto ret = MAPIAllocateBuffer(sizeof(wchar_t) * (wcslen(uval) + 1), reinterpret_cast<void **>(&s.Value.lpszW));
		if (ret != hrSuccess)
			return ret;
		wcscpy(s.Value.lpszW, uval);
	}
	else
		return MAPI_E_CALL_FAILED;

	return hrSuccess;
}

HRESULT vcftomapi_impl::vobject_to_named_prop(VObject *v, SPropValue &s,
    ULONG named_proptype)
{
	MAPINAMEID name, *namep = &name;
	memory_ptr<SPropTagArray> proptag;

	name.lpguid = const_cast<GUID *>(&PSETID_Address);
	name.ulKind = MNID_ID;
	name.Kind.lID = named_proptype;
	auto hr = m_propobj->GetIDsFromNames(1, &namep, MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;
	return vobject_to_prop(v, s, proptag->aulPropTag[0]);
}

HRESULT vcftomapi_impl::unicode_to_named_prop(const wchar_t *v, SPropValue &s,
    ULONG named_proptype)
{
	MAPINAMEID name, *namep = &name;
	memory_ptr<SPropTagArray> proptag;

	name.lpguid = const_cast<GUID *>(&PSETID_Address);
	name.ulKind = MNID_ID;
	name.Kind.lID = named_proptype;

	HRESULT hr = m_propobj->GetIDsFromNames(1, &namep,
	             MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;
	s.ulPropTag = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_UNICODE);
	auto ret = MAPIAllocateBuffer(sizeof(wchar_t) * (wcslen(v) + 1),
               reinterpret_cast<void **>(&s.Value.lpszW));
	if (ret != hrSuccess)
		return hr;
	wcscpy(s.Value.lpszW, v);
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
	s.ulPropTag = CHANGE_PROP_TYPE(PR_MESSAGE_CLASS, PT_STRING8);
	s.Value.lpszA = const_cast<char *>("IPM.Contact");
	HRESULT hr = HrSetOneProp(msg, &s);
	if (hr != hrSuccess)
		return hr;

	MAPINAMEID name, *namep = &name;
	memory_ptr<SPropTagArray> proptag;

	name.lpguid = const_cast<GUID *>(&PSETID_Common);
	name.ulKind = MNID_ID;
	name.Kind.lID = 0x8514;

	hr = msg->GetIDsFromNames(1, &namep, MAPI_CREATE, &~proptag);
	if (hr != hrSuccess)
		return hr;
	s.ulPropTag = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_BOOLEAN);
	s.Value.b = true;
	props.emplace_back(std::move(s));

	return save_props(props, msg);
}

HRESULT vcftomapi_impl::save_photo(IMessage *mapiprop)
{
	auto bytes = base64_decode(photo);
	ULONG tmp = 0;
	object_ptr<IAttach> att;
	auto hr = mapiprop->CreateAttach(nullptr, 0, &tmp, &~att);
	if (hr != hrSuccess)
		return hr;

	object_ptr<IStream> stream;
	hr = att->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE | STGM_TRANSACTED, MAPI_CREATE | MAPI_MODIFY, &~stream);
	if (hr != hrSuccess)
		return hr;

	ULONG written = 0;
	hr = stream->Write(bytes.c_str(), bytes.size(), &written);
	if (hr != hrSuccess)
		return hr;

	if (written != bytes.size())
		return MAPI_E_CALL_FAILED;

	hr = stream->Commit(0);
	if (hr != hrSuccess)
		return hr;

	const wchar_t *filename, *mimetype;
	switch (phototype) {
	case PHOTO_JPEG:
		filename = L"image.jpeg";
		mimetype = L"image/jpeg";
		break;
	case PHOTO_PNG:
		filename = L"image.png";
		mimetype = L"image/png";
		break;
	case PHOTO_GIF:
		filename = L"image.gif";
		mimetype = L"image/gif";
		break;
	default:
		filename = L"unknown";
		mimetype = L"application/octet-stream";
		break;
	}

	SPropValue prop[5];
	prop[0].ulPropTag = PR_ATTACHMENT_CONTACTPHOTO;
	prop[0].Value.b = true;
	prop[1].ulPropTag = PR_ATTACH_METHOD;
	prop[1].Value.ul = ATTACH_BY_VALUE;
	prop[2].ulPropTag = PR_ATTACH_LONG_FILENAME_W;
	prop[2].Value.lpszW = const_cast<wchar_t *>(filename);
	prop[3].ulPropTag = PR_ATTACH_MIME_TAG_W;
	prop[3].Value.lpszW = const_cast<wchar_t *>(mimetype);
	prop[4].ulPropTag = PR_ATTACHMENT_HIDDEN;
	prop[4].Value.b = true;
	hr = att->SetProps(ARRAY_SIZE(prop), prop, nullptr);
	if (hr != hrSuccess)
		return hr;

	hr = att->SaveChanges(KEEP_OPEN_READWRITE);
	if (hr != hrSuccess)
		return hr;

	return hrSuccess;
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
    IMessage *mapiprop)
{
	memory_ptr<SPropValue> propvals;
	HRESULT hr = MAPIAllocateBuffer(proplist.size() * sizeof(SPropValue),
	             &~propvals);
	if (hr != hrSuccess)
		return hr;

	size_t i = 0;
	for (const auto &prop : proplist)
		propvals[i++] = std::move(prop);
	auto ret = mapiprop->SetProps(i, propvals, nullptr);
	for (const auto &prop : proplist) {
		if (PROP_TYPE(prop.ulPropTag) == PT_UNICODE)
			MAPIFreeBuffer(prop.Value.lpszW);
		else if (PROP_TYPE(prop.ulPropTag) == PT_STRING8)
			MAPIFreeBuffer(prop.Value.lpszA);
		else if (PROP_TYPE(prop.ulPropTag) == PT_BINARY)
			MAPIFreeBuffer(prop.Value.bin.lpb);
	}

	if (ret != hrSuccess)
		return ret;

	if (phototype != PHOTO_NONE)
		ret = save_photo(mapiprop);

	return ret;
}

} /* namespace */
