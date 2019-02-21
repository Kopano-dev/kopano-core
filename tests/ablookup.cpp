/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2018, Kopano and its licensors */
#include <stdexcept>
#include <string>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <mapidefs.h>
#include <kopano/charset/convert.h>
#include <kopano/ECRestriction.h>
#include <kopano/ECLogger.h>
#include <kopano/stringutil.h>
#include "kopano/mapiext.h"
#include "tbi.hpp"

using namespace KC;

static inline const wchar_t *zn(const SPropValue &x)
{
	if (PROP_TYPE(x.ulPropTag) == PT_ERROR) return L"<ERROR>";
	if (PROP_TYPE(x.ulPropTag) != PT_UNICODE) return L"<OTYPE>";
	if (x.Value.lpszW != nullptr) return x.Value.lpszW;
	return L"<NIL>";
}

static HRESULT container_contents(IABContainer *cont, unsigned int lvl)
{
	object_ptr<IMAPITable> tbl;
	auto ret = cont->GetContentsTable(0, &~tbl);
	if (ret != hrSuccess)
		return hrSuccess;

	SPropValue spv;
	spv.ulPropTag = PR_DISPLAY_NAME;
	spv.Value.LPSZ = const_cast<TCHAR *>(KC_T("foo"));
	auto rst = ECContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_DISPLAY_NAME, &spv, ECRestriction::Cheap);
	ret = rst.RestrictTable(tbl, 0);
	if (ret != hrSuccess)
		return kc_perrorf("RestrictTable", ret);

	static constexpr const SizedSPropTagArray(4, cols) = {4, {PR_DISPLAY_NAME, PR_COMPANY_NAME, PR_MOBILE_TELEPHONE_NUMBER, PR_BUSINESS_TELEPHONE_NUMBER}};
	ret = tbl->SetColumns(cols, 0);
	if (ret != hrSuccess)
		return kc_perrorf("SetColumns", ret);

	rowset_ptr rows;
	ret = tbl->QueryRows(INT_MAX, 0, &~rows);
	if (ret != hrSuccess)
		return kc_perrorf("QueryRows", ret);
	wprintf(L"%-*s +  contacts: %u\n", lvl * 4, "", rows.size());
	for (size_t i = 0; i < rows.size(); ++i)
		wprintf(L"%-*s +  \"%ls\" \"%ls\" \"%ls\" \"%ls\"\n",
			lvl * 4, "",
			zn(rows[i].lpProps[0]),
			zn(rows[i].lpProps[1]),
			zn(rows[i].lpProps[2]),
			zn(rows[i].lpProps[3]));
	return hrSuccess;
}

static HRESULT container_do(unsigned int lvl, IAddrBook *ab, const ENTRYID *eid, unsigned int eid_size)
{
	ULONG objtype = 0;
	object_ptr<IABContainer> cont;
	auto ret = ab->OpenEntry(eid_size, eid, &iid_of(cont), 0, &objtype, &~cont);
	if (ret != hrSuccess)
		return kc_perrorf("OpenEntry", ret);
	assert(objtype == MAPI_ABCONT);

	memory_ptr<SPropValue> spv;
	ret = HrGetOneProp(cont, PR_DISPLAY_NAME_A, &~spv);
	if (ret == hrSuccess)
		wprintf(L"%-*s +  Container \"%s\" (%s)\n", lvl++ * 4, "", spv->Value.lpszA, bin2hex(eid_size, eid).c_str());
	else
		wprintf(L"%-*s +  Container (%s)\n", lvl++ * 4, "", bin2hex(eid_size, eid).c_str());

	ret = container_contents(cont, lvl);
	if (ret != hrSuccess)
		return ret;

	object_ptr<IMAPITable> tbl;
	ret = cont->GetHierarchyTable(0, &~tbl);
	assert(ret == hrSuccess);

	rowset_ptr rs;
	ret = HrQueryAllRows(tbl, nullptr, nullptr, nullptr, UINT_MAX, &~rs);
	assert(ret == hrSuccess);
	wprintf(L"%-*s +  subcontainers: %u\n", lvl * 4, "", rs.size());
	for (size_t i = 0; i < rs.size(); ++i) {
		auto eid = PCpropFindProp(rs[i].lpProps, rs[i].cValues, PR_ENTRYID);
		assert(eid != nullptr);
		container_do(lvl, ab, reinterpret_cast<const ENTRYID *>(eid->Value.bin.lpb), eid->Value.bin.cb);
	}
	return hrSuccess;
}

static void load_zc(IMAPISession *ses)
{
	object_ptr<IMsgServiceAdmin> sa;
	auto ret = ses->AdminServices(0, &~sa);
	assert(ret == hrSuccess);
	auto empty = reinterpret_cast<const TCHAR *>("");
	ret = sa->CreateMsgService(reinterpret_cast<const TCHAR *>("ZCONTACTS"), empty, 0, 0);
	assert(ret == hrSuccess);
}

static HRESULT setup_zc(IMAPISession *ses)
{
	object_ptr<IMsgStore> store;
	auto ret = HrOpenDefaultStore(ses, &~store);
	assert(ret == hrSuccess);
	memory_ptr<SPropValue> contact_store, contact_folder;
	ret = HrGetOneProp(store, PR_STORE_ENTRYID, &~contact_store);
	if (ret != hrSuccess)
		return kc_perrorf("PR_STORE_ENTRYID", ret);
	object_ptr<IMAPIFolder> root;
	unsigned int objtype = 0;
	ret = store->OpenEntry(0, nullptr, &iid_of(root), 0, &objtype, &~root);
	if (ret != hrSuccess)
		return kc_perrorf("root folder", ret);
	ret = HrGetOneProp(root, PR_IPM_CONTACT_ENTRYID, &~contact_folder);
	if (ret != hrSuccess)
		return kc_perrorf("PR_IPM_CONTACT_ENTRYID", ret);
	wprintf(L"Private contact folder: %s\n", bin2hex(contact_folder->Value.bin).c_str());

	/* Tell ZCONTACTS which folder to use */
	SPropValue pv[3];
	auto xname = KC_T("Synthesized AB container");
	pv[0].ulPropTag = PR_ZC_CONTACT_STORE_ENTRYIDS;
	pv[0].Value.MVbin.cValues = 1;
	pv[0].Value.MVbin.lpbin = &contact_store->Value.bin;
	pv[1].ulPropTag = PR_ZC_CONTACT_FOLDER_ENTRYIDS;
	pv[1].Value.MVbin.cValues = 1;
	pv[1].Value.MVbin.lpbin = &contact_folder->Value.bin;
	pv[2].ulPropTag = PR_ZC_CONTACT_FOLDER_NAMES;
	pv[2].Value.MVSZ.cValues = 1;
	pv[2].Value.MVSZ.LPPSZ = const_cast<TCHAR **>(&xname);
	object_ptr<IProfSect> ps;
	ret = ses->OpenProfileSection(reinterpret_cast<const MAPIUID *>(&pbGlobalProfileSectionGuid), &iid_of(ps), MAPI_MODIFY, &~ps);
	assert(ret == hrSuccess);
	ret = ps->SetProps(ARRAY_SIZE(pv), pv, nullptr);
	assert(ret == hrSuccess);
	return hrSuccess;
}

int main(int argc, char **argv) try
{
	std::wstring user = L"foo", pass = L"xfoo";
	if (argc >= 3) {
		user = convert_to<std::wstring>(argv[1]);
		pass = convert_to<std::wstring>(argv[2]);
	}

	auto ses = KSession(user.c_str(), pass.c_str());
	load_zc(ses);
	auto ret = setup_zc(ses);
	if (ret != hrSuccess)
		return EXIT_FAILURE;

	object_ptr<IAddrBook> ab;
	ret = ses->OpenAddressBook(0, nullptr, AB_NO_DIALOG, &~ab);
	assert(ret == hrSuccess);

	memory_ptr<ENTRYID> eid;
	unsigned int eid_size = 0;
	ab->GetDefaultDir(&eid_size, &~eid);
	wprintf(L"Default AB container: %s\n\n", bin2hex(eid_size, eid).c_str());
	/* walk virtual root; contains default dir at some point */
	container_do(0, ab, nullptr, 0);
	return EXIT_SUCCESS;
} catch (...) {
	std::terminate();
}
