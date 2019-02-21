/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2016, Kopano and its licensors */
#include <exception>
#include <string>
#include <cassert>
#include <cstdlib>
#include <kopano/automapi.hpp>
#include <kopano/hl.hpp>
#include <kopano/CommonUtil.h>
#include <kopano/charset/convert.h>
#include "tbi.hpp"

using namespace KC;

int main(int argc, char **argv) try
{
	std::wstring user = L"foo", pass = L"xfoo";
	if (argc >= 3) {
		user = convert_to<std::wstring>(argv[1]);
		pass = convert_to<std::wstring>(argv[2]);
	}
	AutoMAPI automapi;
	auto ret = automapi.Initialize();
	assert(ret == hrSuccess);
	auto msg = KSession(user.c_str(), pass.c_str()).open_default_store()
	           .open_root(MAPI_MODIFY).create_message();
	SPropValue prop[3];
	memset(&prop, 0, sizeof(prop));
	prop[0].ulPropTag = PR_SUBJECT_W;
	prop[0].Value.lpszW = const_cast<wchar_t *>(L"Hi world");
	prop[1].ulPropTag = PROP_TAG(PT_DOUBLE, 0x8072);
	prop[1].Value.dbl = 0.1337;
	prop[2].ulPropTag = PR_MESSAGE_CLASS_A;
	prop[2].Value.lpszA = const_cast<char *>("IPM.Note");
	msg->SetProps(3, prop, nullptr);
	msg->SaveChanges(0);
} catch (...) {
	std::terminate();
}
