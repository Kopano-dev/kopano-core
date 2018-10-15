/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2017 - Kopano and its licensors
 */
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
	VObject *to_prop(VObject *node, const char *prop, const SPropValue &value);
	VObject *to_prop(VObject *node, const char *prop, const wchar_t *value);
	HRESULT add_adr(IMessage *lpMessage, VObject *root);
	HRESULT add_email(IMessage *lpMessage, VObject *root);
	HRESULT add_uid(IMessage *lpMessage, VObject *root);
	HRESULT add_url(IMessage *lpMessage, VObject *root);
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

HRESULT mapitovcf_impl::add_adr(IMessage *lpMessage, VObject *root)
{
	static constexpr const SizedSPropTagArray(5, home_props) =
		{5, {PR_HOME_ADDRESS_STREET, PR_HOME_ADDRESS_CITY,
			 PR_HOME_ADDRESS_STATE_OR_PROVINCE, PR_HOME_ADDRESS_POSTAL_CODE,
			 PR_HOME_ADDRESS_COUNTRY}};
	memory_ptr<SPropValue> msgprop_array;
	unsigned int count;
	memory_ptr<SPropTagArray> proptag;

	auto hr = lpMessage->GetProps(home_props, MAPI_UNICODE, &count, &~msgprop_array);
	if (!FAILED(hr) && PROP_TYPE(msgprop_array[0].ulPropTag) == PT_UNICODE && !prop_is_empty(msgprop_array[0])) {
		auto adrnode = addProp(root, VCAdrProp);
		auto node = addProp(adrnode, "TYPE");
		setVObjectStringZValue(node, "HOME");
		to_prop(adrnode, "STREET", msgprop_array[0].Value.lpszW);
		if (PROP_TYPE(msgprop_array[1].ulPropTag) == PT_UNICODE)
			to_prop(adrnode, "L", msgprop_array[1].Value.lpszW);
		if (PROP_TYPE(msgprop_array[2].ulPropTag) == PT_UNICODE)
			to_prop(adrnode, "R", msgprop_array[2].Value.lpszW);
		if (PROP_TYPE(msgprop_array[3].ulPropTag) == PT_UNICODE)
			to_prop(adrnode, "PC", msgprop_array[3].Value.lpszW);
		if (PROP_TYPE(msgprop_array[4].ulPropTag) == PT_UNICODE)
			to_prop(adrnode, "C", msgprop_array[4].Value.lpszW);
	}

	static constexpr const SizedSPropTagArray(5, other_props) =
		{5, {PR_OTHER_ADDRESS_STREET, PR_OTHER_ADDRESS_CITY,
			 PR_OTHER_ADDRESS_STATE_OR_PROVINCE, PR_OTHER_ADDRESS_POSTAL_CODE,
			 PR_OTHER_ADDRESS_COUNTRY}};

	hr = lpMessage->GetProps(other_props, MAPI_UNICODE, &count, &~msgprop_array);
	if (!FAILED(hr) && PROP_TYPE(msgprop_array[0].ulPropTag) == PT_UNICODE&& !prop_is_empty(msgprop_array[0])) {
		auto adrnode = addProp(root, VCAdrProp);
		to_prop(adrnode, "STREET", msgprop_array[0].Value.lpszW);
		if (PROP_TYPE(msgprop_array[1].ulPropTag) == PT_UNICODE)
			to_prop(adrnode, "L", msgprop_array[1].Value.lpszW);
		if (PROP_TYPE(msgprop_array[2].ulPropTag) == PT_UNICODE)
			to_prop(adrnode, "R", msgprop_array[2].Value.lpszW);
		if (PROP_TYPE(msgprop_array[3].ulPropTag) == PT_UNICODE)
			to_prop(adrnode, "PC", msgprop_array[3].Value.lpszW);
		if (PROP_TYPE(msgprop_array[4].ulPropTag) == PT_UNICODE)
			to_prop(adrnode, "C", msgprop_array[4].Value.lpszW);
	}

	MAPINAMEID nameids[5], *nameids_ptrs[5];
	for (size_t i = 0; i < 5; ++i) {
		nameids[i].lpguid = const_cast<GUID *>(&PSETID_Address);
		nameids[i].ulKind = MNID_ID;
		nameids[i].Kind.lID = 0x8045 + i;
		nameids_ptrs[i] = &nameids[i];
	}

	hr = lpMessage->GetIDsFromNames(5, nameids_ptrs, MAPI_BEST_ACCESS, &~proptag);
	if (FAILED(hr))
		return hrSuccess;
	for (size_t i = 0; i < 5; ++i)
		if (PROP_TYPE(proptag->aulPropTag[i]) != PT_ERROR)
			proptag->aulPropTag[i] = CHANGE_PROP_TYPE(proptag->aulPropTag[i], PT_UNICODE);
	hr = lpMessage->GetProps(proptag, MAPI_UNICODE, &count, &~msgprop_array);
	if (FAILED(hr) || PROP_TYPE(msgprop_array[0].ulPropTag) != PT_UNICODE || prop_is_empty(msgprop_array[0]))
		return hrSuccess;
	auto adrnode = addProp(root, VCAdrProp);
	auto node = addProp(adrnode, "TYPE");
	setVObjectStringZValue(node, "WORK");
	to_prop(adrnode, "STREET", msgprop_array[0].Value.lpszW);
	if (PROP_TYPE(msgprop_array[1].ulPropTag) == PT_UNICODE)
		to_prop(adrnode, "L", msgprop_array[1].Value.lpszW);
	if (PROP_TYPE(msgprop_array[2].ulPropTag) == PT_UNICODE)
		to_prop(adrnode, "R", msgprop_array[2].Value.lpszW);
	if (PROP_TYPE(msgprop_array[3].ulPropTag) == PT_UNICODE)
		to_prop(adrnode, "PC", msgprop_array[3].Value.lpszW);
	if (PROP_TYPE(msgprop_array[4].ulPropTag) == PT_UNICODE)
		to_prop(adrnode, "C", msgprop_array[4].Value.lpszW);
	return hrSuccess;
}

HRESULT mapitovcf_impl::add_email(IMessage *lpMessage, VObject *root)
{
	MAPINAMEID name, *namep = &name;
	const int first_email_id = 0x8083, last_email_id = 0x80a3;

	for (int lid = first_email_id; lid <= last_email_id; lid += 0x10) {
		name.lpguid = const_cast<GUID *>(&PSETID_Address);
		name.ulKind = MNID_ID;
		name.Kind.lID = lid;

		memory_ptr<SPropTagArray> proptag;
		auto hr = lpMessage->GetIDsFromNames(1, &namep, MAPI_BEST_ACCESS, &~proptag);
		if (hr != hrSuccess)
			continue;

		ULONG proptype = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_UNICODE);
		memory_ptr<SPropValue> prop;
		hr = HrGetOneProp(lpMessage, proptype, &~prop);
		if (hr == hrSuccess) {
			auto node = to_prop(root, VCEmailAddressProp, *prop);
			std::wstring email_type = L"INTERNET";
			if (lid == first_email_id)
				/* first email address */
				email_type += L",PREF";
			to_prop(node, "TYPE", email_type.c_str());
		} else if (hr != MAPI_E_NOT_FOUND) {
			continue;
		}
	}

	return hrSuccess;
}

HRESULT mapitovcf_impl::add_uid(IMessage *lpMessage, VObject *root)
{
	MAPINAMEID name, *namep = &name;
	name.lpguid = const_cast<GUID *>(&PSETID_Meeting);
	name.ulKind = MNID_ID;
	name.Kind.lID = dispidGlobalObjectID;

	std::string uid;
	memory_ptr<SPropTagArray> proptag;
	auto hr = lpMessage->GetIDsFromNames(1, &namep, MAPI_BEST_ACCESS, &~proptag);
	if (hr == hrSuccess) {
		proptag->aulPropTag[0] = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_BINARY);

		unsigned int count;
		memory_ptr<SPropValue> msgprop_array;
		hr = lpMessage->GetProps(proptag, 0, &count, &~msgprop_array);
		if (hr == hrSuccess) {
			HrGetICalUidFromBinUid(msgprop_array[0].Value.bin, &uid);
			auto uid_wstr = convert_to<std::wstring>(uid);
			to_prop(root, "UID", uid_wstr.c_str());
		}
	}
	/* Object did not have guid, let us generate one, and save it
	   if possible */
	if (uid.size() != 0)
		return hrSuccess;
	HrGenerateUid(&uid);
	auto binstr = hex2bin(uid);
	SPropValue prop;
	prop.ulPropTag = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_BINARY);
	prop.Value.bin.lpb = (LPBYTE)binstr.c_str();
	prop.Value.bin.cb = binstr.length();
	hr = HrSetOneProp(lpMessage, &prop);
	if (hr == hrSuccess) {
		hr = lpMessage->SaveChanges(0);
		if (hr != hrSuccess)
			/* ignore */;
	}
	to_prop(root, "UID", prop);
	return hrSuccess;
}

HRESULT mapitovcf_impl::add_url(IMessage *lpMessage, VObject *root)
{
	MAPINAMEID name, *namep = &name;
	name.lpguid = const_cast<GUID *>(&PSETID_Address);
	name.ulKind = MNID_ID;
	name.Kind.lID = dispidWebPage;

	memory_ptr<SPropTagArray> proptag;
	auto hr = lpMessage->GetIDsFromNames(1, &namep, MAPI_BEST_ACCESS, &~proptag);
	if (hr != hrSuccess)
		return hrSuccess;
	ULONG proptype = CHANGE_PROP_TYPE(proptag->aulPropTag[0], PT_UNICODE);
	memory_ptr<SPropValue> prop;
	hr = HrGetOneProp(lpMessage, proptype, &~prop);
	if (hr == hrSuccess)
		to_prop(root, "URL", *prop);
	return hrSuccess;
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

HRESULT mapitovcf_impl::add_message(IMessage *lpMessage)
{
	memory_ptr<SPropValue> lpMessageClass;

	if (lpMessage == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	auto hr = HrGetOneProp(lpMessage, PR_MESSAGE_CLASS_A, &~lpMessageClass);
	if (hr != hrSuccess)
		return hr;
	if (strcasecmp(lpMessageClass->Value.lpszA, "IPM.Contact") != 0)
		return MAPI_E_INVALID_PARAMETER;

	auto root = newVObject(VCCardProp);
	to_prop(root, "VERSION", L"3.0");
	auto prodid = L"-//Kopano//libicalmapi " + convert_to<std::wstring>(PROJECT_VERSION) + L"//EN";
	to_prop(root, "PRODID", prodid.c_str());

	memory_ptr<SPropValue> msgprop, msgprop2;
	hr = HrGetOneProp(lpMessage, PR_GIVEN_NAME, &~msgprop);
	HRESULT hr2 = HrGetOneProp(lpMessage, PR_SURNAME, &~msgprop2);
	if (hr == hrSuccess || hr2 == hrSuccess) {
		auto node = addGroup(root, VCNameProp);
		if (msgprop != nullptr)
			to_prop(node, VCGivenNameProp, *msgprop);
		if (msgprop2 != nullptr)
			to_prop(node, VCFamilyNameProp, *msgprop2);
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	} else if (hr2 != MAPI_E_NOT_FOUND) {
		return hr2;
	}

	hr = HrGetOneProp(lpMessage, PR_DISPLAY_NAME, &~msgprop);
	if (hr == hrSuccess)
		to_prop(root, VCFullNameProp, *msgprop);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_TITLE, &~msgprop);
	if (hr == hrSuccess)
		to_prop(root, VCTitleProp, *msgprop);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_NICKNAME, &~msgprop);
	if (hr == hrSuccess)
		to_prop(root, "NICKNAME", *msgprop);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_COMPANY_NAME, &~msgprop);
	if (hr == hrSuccess && !prop_is_empty(*msgprop)) {
		auto node = addGroup(root, VCOrgProp);
		to_prop(node, "ORGNAME", *msgprop);
		hr = HrGetOneProp(lpMessage, PR_DEPARTMENT_NAME, &~msgprop);
		if (hr == hrSuccess)
			to_prop(node, "OUN", *msgprop);
		else if (hr != MAPI_E_NOT_FOUND)
			return hr;
	} else if (hr != MAPI_E_NOT_FOUND && !prop_is_empty(*msgprop)) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_PRIMARY_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"MAIN");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_HOME_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"HOME");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_MOBILE_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"MOBILE");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_BUSINESS_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"WORK");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_PAGER_TELEPHONE_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"PAGER");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_PRIMARY_FAX_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"MAIN");
		to_prop(node, "TYPE", L"FAX");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_HOME_FAX_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"HOME");
		to_prop(node, "TYPE", L"FAX");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = HrGetOneProp(lpMessage, PR_BUSINESS_FAX_NUMBER, &~msgprop);
	if (hr == hrSuccess) {
		auto node = to_prop(root, VCTelephoneProp, *msgprop);
		to_prop(node, "TYPE", L"WORK");
		to_prop(node, "TYPE", L"FAX");
	} else if (hr != MAPI_E_NOT_FOUND) {
		return hr;
	}

	hr = add_adr(lpMessage, root);
	if (hr != hrSuccess)
		return hr;

	hr = add_email(lpMessage, root);
	if (hr != hrSuccess)
		return hr;

	hr = add_uid(lpMessage, root);
	if (hr != hrSuccess)
		return hr;

	hr = add_url(lpMessage, root);
	if (hr != hrSuccess)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_BODY, &~msgprop);
	if (hr == hrSuccess)
		to_prop(root, "NOTE", *msgprop);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_BIRTHDAY, &~msgprop);
	if (hr == hrSuccess)
		to_prop(root, "BDAY", *msgprop);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

	hr = HrGetOneProp(lpMessage, PR_LAST_MODIFICATION_TIME, &~msgprop);
	if (hr == hrSuccess)
		to_prop(root, "REV", *msgprop);
	else if (hr != MAPI_E_NOT_FOUND)
		return hr;

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
}

HRESULT mapitovcf_impl::finalize(std::string *s)
{
	if (s == nullptr)
		return MAPI_E_INVALID_PARAMETER;
	*s = m_result;
	return hrSuccess;
}

} /* namespace */
