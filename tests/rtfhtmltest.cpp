/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2016, Kopano and its licensors */
#include <glob.h>
#include <fstream>
#include <iostream>
#include <string>
#include <clocale>
#include <cstring>
#include <kopano/MAPIErrors.h>
#include "rtfutil.h"

#define TEST_FILES "tests/testdata/rtftohtml/*.test"

using namespace KC;

static int test_rtfhtml(std::string file)
{
	std::string html;

	std::ifstream rtftile(file);
	if (!rtftile.is_open()) {
		std::cerr << "Unable to open \"" << file << "\"\n";
		return EXIT_FAILURE;
	}

	rtftile.imbue(std::locale(""));
	const std::string rtf{std::istreambuf_iterator<char>(rtftile), std::istreambuf_iterator<char>()};
	HRESULT hr = HrExtractHTMLFromRealRTF(rtf, html, CP_UTF8);
	if (hr != hrSuccess) {
		std::cerr << "Unable to parse RTF file: " << GetMAPIErrorMessage(hr) << std::endl;
		return EXIT_FAILURE;
	}

	file.replace(file.find(".test"), sizeof(".test")-1, ".result.65001");
	std::ifstream expectedrtftile(file);
	if (!expectedrtftile.is_open()) {
		std::cerr << "Unable to open \"" << file << "\"\n";
		return EXIT_FAILURE;
	}

	expectedrtftile.imbue(std::locale(""));

	std::string expectedhtml{std::istreambuf_iterator<char>(expectedrtftile), std::istreambuf_iterator<char>()};

	return expectedhtml.compare(html);
}

int main(int argc, char **argv) {
	glob_t glob_result;

	setlocale(LC_ALL, "");
	memset(&glob_result, 0, sizeof(glob_result));

	auto ret = glob(TEST_FILES, GLOB_TILDE, nullptr, &glob_result);
	if (ret != 0) {
		globfree(&glob_result);
		std::cerr << "glob failed to find test files: " << ret << std::endl;
		return EXIT_FAILURE;
	}

	for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
		std::string file = glob_result.gl_pathv[i];
		ret = test_rtfhtml(file);
		if (ret != EXIT_SUCCESS) {
			std::cout << "Failed test for: " << file << std::endl;
			return EXIT_FAILURE;
		} else {
			std::cout << "ok: " << file << std::endl;
		}
	}

	return EXIT_SUCCESS;
}
