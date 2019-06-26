/*
 * Copyright 2019 Kopano and its licensors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <algorithm>
#include <map>
#include <new>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <mapix.h>
#include <libHX/misc.h>
#include <kopano/memory.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/charset/convert.h>
#include "dovestore.h"

using namespace KC;

void *kpxx_login()
{
	MAPIInitialize(nullptr);
	object_ptr<IMAPISession> ses;
	auto ret = HrOpenECSession(&~ses, "app_vers", "app_misc",
	           L"foo", L"xfoo", nullptr, 0, 0, 0);
	if (ret != hrSuccess)
		return nullptr;
	return ses.release();
}

void kpxx_logout(void *s)
{
	auto ses = static_cast<IMAPISession *>(s);
	if (ses != nullptr)
		ses->Release();
	MAPIUninitialize();
}

void *kpxx_store_get(void *vses)
{
	auto ses = static_cast<IMAPISession *>(vses);
	object_ptr<IMsgStore> store;
	auto ret = HrOpenDefaultStore(ses, &~store);
	if (ret != hrSuccess)
		return nullptr;
	return store.release();
}

void kpxx_store_put(void *vstor)
{
	static_cast<IMsgStore *>(vstor)->Release();
}

static rowset_ptr kpxx_hierarchy_table(IMsgStore *store)
{
	memory_ptr<SPropValue> pv;
	auto ret = HrGetOneProp(store, PR_IPM_SUBTREE_ENTRYID, &~pv);
	if (ret != hrSuccess)
		return nullptr;
	object_ptr<IMAPIFolder> root;
	unsigned int type = 0;
	ret = store->OpenEntry(pv->Value.bin.cb, reinterpret_cast<ENTRYID *>(pv->Value.bin.lpb),
	      &iid_of(root), 0, &type, &~root);
	if (ret != hrSuccess)
		return nullptr;
	object_ptr<IMAPITable> table;
	ret = root->GetHierarchyTable(CONVENIENT_DEPTH | MAPI_DEFERRED_ERRORS | MAPI_UNICODE, &~table);
	if (ret != hrSuccess)
		return nullptr;

	static constexpr const SizedSPropTagArray(3, spta) =
		{3, {PR_ENTRYID, PR_PARENT_ENTRYID, PR_DISPLAY_NAME}};
	static constexpr const SizedSSortOrderSet(1, order) =
		{1, 0, 0, {{PR_DEPTH, TABLE_SORT_ASCEND}}};
	ret = table->SetColumns(spta, TBL_BATCH);
	if (ret != hrSuccess)
		return nullptr;
	ret = table->SortTable(order, TBL_BATCH);
	if (ret != hrSuccess)
		return nullptr;
	rowset_ptr rows;
	ret = table->QueryRows(-1, 0, &~rows);
	if (ret != hrSuccess)
		return nullptr;
	return rows;
}

char **kpxx_hierarchy_list(void *vstor)
{
	auto rows = kpxx_hierarchy_table(static_cast<IMsgStore *>(vstor));
	std::map<std::string, std::string> eid2parent;
	std::map<std::string, std::wstring> eid2name;
	std::vector<std::string> unames;

	for (unsigned int i = 0; i < rows->cRows; ++i) {
		std::string eid(reinterpret_cast<char *>(rows[i].lpProps[0].Value.bin.lpb), rows[i].lpProps[0].Value.bin.cb);
		std::string peid(reinterpret_cast<char *>(rows[i].lpProps[1].Value.bin.lpb), rows[i].lpProps[1].Value.bin.cb);
		std::wstring name(rows[i].lpProps[2].Value.lpszW);
		std::replace(name.begin(), name.end(), L'/', L'_');
		eid2parent.emplace(eid, peid);
		eid2name.emplace(eid, name);

		auto pit = eid2name.find(peid);
		while (pit != eid2name.cend()) {
			name.insert(0, L"/");
			name.insert(0, pit->second);
			auto gp = eid2parent.find(peid);
			if (gp == eid2parent.cend())
				break;
			pit = eid2name.find(gp->second);
			peid = gp->second;
		}
		unames.emplace_back(convert_to<std::string>("UTF-8", name, rawsize(name), CHARSET_WCHAR));
	}
	auto nl = static_cast<char **>(malloc(sizeof(char *) * (rows->cRows + 1)));
	if (nl == nullptr)
		return nullptr;
	for (unsigned int i = 0; i < rows->cRows; ++i) {
		nl[i] = strdup(unames[i].c_str());
		fprintf(stderr, "dovstor: %s\n", nl[i]);
		if (nl[i] == nullptr) {
			HX_zvecfree(nl);
			return nullptr;
		}
	}
	nl[rows->cRows] = nullptr;
	return nl;
}
