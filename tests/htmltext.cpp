#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include "HtmlToTextParser.h"

using namespace KC;

int main(int argc, char **argv)
{
	std::wstring line, html;
	CHtmlToTextParser parser;

	if (argc < 2)
		std::cerr << "Usage: requires a HTML file\n";

	std::wifstream htmlfile(argv[1]);
	if (!htmlfile.is_open())
		std::cerr << "Unable to open \"" << argv[1] << "\"\n";
	while (std::getline(htmlfile, line))
		html += line;
	if (!parser.Parse(html.c_str()))
		std::cerr << "Unable to parse HTML\n";

	std::wcout << parser.GetText();
	return EXIT_SUCCESS;
}
