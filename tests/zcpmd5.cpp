/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <kopano/stringutil.h>

using namespace KC;

int main()
{
	MD5_CTX d;
	std::string h;

	MD5_Init(&d);
	h = zcp_md5_final_hex(&d);
	if (h != "d41d8cd98f00b204e9800998ecf8427e")
		return EXIT_FAILURE;

	MD5_Init(&d);
	MD5_Update(&d, "ZarafaZarafaZarafaZarafaZarafaZarafaZarafaZarafa", 48);
	h = zcp_md5_final_hex(&d);
	if (h != "ce5aa67bf57534d72886452a067c5866")
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
