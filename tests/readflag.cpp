/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2016, Kopano and its licensors */
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <kopano/platform.h>
#include <kopano/hl.hpp>
#include <kopano/automapi.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/ECRestriction.h>
#include <kopano/MAPIErrors.h>
#include <mapitags.h>
#include "tbi.hpp"

using namespace KC;

static KProp t_set_read(KStore &store, KFolder &root);

static int t_check_flag_in_table(KFolder &root, const KProp &eid,
    unsigned int test_value)
{
	KTable table = root.get_contents_table(MAPI_DEFERRED_ERRORS);
	table.columns({PR_MESSAGE_FLAGS, PR_ENTRYID});

	int ret = ECPropertyRestriction(RELOP_EQ, eid->ulPropTag, eid.get(), ECRestriction::Cheap).RestrictTable(table);
	if (ret != hrSuccess) {
		printf("restrict failed\n");
		return -1;
	}
	auto rowset = table.rows(10, 0);
	if (rowset->cRows != 1) {
		printf("rows = %d\n", rowset->cRows);
		return -1;
	} else if (rowset[0].cValues != 2) {
		printf("cols = %d\n", rowset[0].cValues);
		return -1;
	}
	auto row = rowset[0];
	if (row.lpProps[0].ulPropTag != PR_MESSAGE_FLAGS) {
		printf("PR_MESSAGE_FLAGS missing\n");
		return -1;
	} else if (row.lpProps[1].ulPropTag != PR_ENTRYID) {
		printf("PR_ENTRYID absent\n");
		return -1;
	}
	printf("MESSAGE_FLAGS %x\n", row.lpProps[0].Value.ul & MSGFLAG_READ);
	if ((row.lpProps[0].Value.ul & MSGFLAG_READ) != test_value) {
		printf("Failure\n");
		return 0;
	}
	printf("Success\n");
	return 1;
}

KProp t_set_read(KStore &store, KFolder &root)
{
	auto message = root.create_message();
	auto prop = message.get_prop(PR_MESSAGE_FLAGS);
	if ((prop.ul() & MSGFLAG_READ) != MSGFLAG_READ)
		throw std::runtime_error("Flags failed 1");
	message.set_read_flag(CLEAR_READ_FLAG);
	message.save_changes();

	auto eid = message.get_prop(PR_ENTRYID);
	message = store.open_message(eid, MAPI_MODIFY);
	if (message == NULL)
		abort();
	prop = message.get_prop(PR_MESSAGE_FLAGS);
	if ((prop.ul() & MSGFLAG_READ) != MSGFLAG_READ)
		throw std::runtime_error("Flags failed 2");
	message.set_read_flag(CLEAR_READ_FLAG);
	message.save_changes();

	message = store.open_message(eid, MAPI_MODIFY);
	prop = message.get_prop(PR_MESSAGE_FLAGS);
	if ((prop.ul() & MSGFLAG_READ) != 0)
		throw std::runtime_error("Flags failed 4");

	return eid;
}

int main(void)
{
	AutoMAPI automapi;
	auto hr = automapi.Initialize();
	int ex = EXIT_FAILURE;

	if (hr != hrSuccess) {
		fprintf(stderr, "MAPIInitialize: %s\n", GetMAPIErrorMessage(hr));
		return EXIT_FAILURE;
	}

	try {
		auto store = KSession(L"user1", L"pass").open_default_store();
		auto root = store.open_root(MAPI_MODIFY);
		KProp eid = t_set_read(store, root);
		t_check_flag_in_table(root, eid, 0);
	} catch (const KMAPIError &err) {
		fprintf(stderr, "Ugh, this sucks :-)  %s %d\n",
		        err.what(), err.code());
	}
	return ex;
}
