/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright 2016, Kopano and its licensors */
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <clocale>
#include <cstdlib>
#include "HtmlToTextParser.h"

using namespace KC;

int main(int argc, char **argv) try
{
	CHtmlToTextParser parser;
	setlocale(LC_ALL, "");

	if (argc < 2) {
		std::cerr << "Usage: requires a HTML file\n";
		return EXIT_FAILURE;
	}
	std::wifstream htmlfile(argv[1]);
	if (!htmlfile.is_open()) {
		std::cerr << "Unable to open \"" << argv[1] << "\"\n";
		return EXIT_FAILURE;
	}
	htmlfile.imbue(std::locale(""));
	std::wstring html{std::istreambuf_iterator<wchar_t>(htmlfile), std::istreambuf_iterator<wchar_t>()};
	if (!parser.Parse(html.c_str())) {
		std::cerr << "Unable to parse HTML\n";
		return EXIT_FAILURE;
	}
	std::wcout << parser.GetText();
	return EXIT_SUCCESS;
} catch (...) {
	std::terminate();
}
