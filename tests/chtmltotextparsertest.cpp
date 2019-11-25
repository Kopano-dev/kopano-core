/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2016, Kopano and its licensors */
#include <fstream>
#include <iostream>
#include <string>
#include <string.h>
#include <clocale>
#include <cstdlib>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <glob.h>

#include "HtmlToTextParser.h"

#define TEST_FILES "tests/htmltoplain/*.test"

using namespace KC;

int testhtml(std::string file);

int main(int argc, char **argv) {
	glob_t glob_result;

	setlocale(LC_ALL, "");

	memset(&glob_result, 0, sizeof(glob_result));

	int ret = glob(TEST_FILES, GLOB_TILDE, NULL, &glob_result);
	if (ret != 0) {
		globfree(&glob_result);
		std::cerr << "glob failed to find test files: " << ret << std::endl;
		return EXIT_FAILURE;
	}

	for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
		std::string file = std::string(glob_result.gl_pathv[i]);
		ret = testhtml(file);
		if (ret != EXIT_SUCCESS) {
			std::cout << "Failed test for: " << file << std::endl;
			return EXIT_FAILURE;
		} else {
			std::cout << "ok: " << file << std::endl;
		}
	}

	return EXIT_SUCCESS;
}

int testhtml(std::string file) {
	CHtmlToTextParser parser;

	std::wifstream htmlfile(file);
	if (!htmlfile.is_open()) {
		std::cerr << "Unable to open \"" << file << "\"\n";
		return EXIT_FAILURE;
	}

	htmlfile.imbue(std::locale(""));
	std::wstring html{std::istreambuf_iterator<wchar_t>(htmlfile), std::istreambuf_iterator<wchar_t>()};
	if (!parser.Parse(html.c_str())) {
		std::cerr << "Unable to parse HTML\n";
		return EXIT_FAILURE;
	}

	file.replace(file.find(".test"), sizeof(".test")-1, ".result");
	std::wifstream expectedhtmlfile(file);
	if (!expectedhtmlfile.is_open()) {
		std::cerr << "Unable to open \"" << file << "\"\n";
		return EXIT_FAILURE;
	}

	expectedhtmlfile.imbue(std::locale(""));
	std::wstring expectedhtml{std::istreambuf_iterator<wchar_t>(expectedhtmlfile), std::istreambuf_iterator<wchar_t>()};

	return expectedhtml.compare(parser.GetText());
}
