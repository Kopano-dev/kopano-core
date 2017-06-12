#include <chrono>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "mapi4linux/src/rtf.h"
int main(int argc, char **argv)
{
	std::string str(static_cast<const std::stringstream &>(std::stringstream() << std::cin.rdbuf()).str());
	if (argc >= 2 && strcmp(argv[1], "-d") == 0) {
		unsigned int outsize = rtf_get_uncompressed_length(str.c_str(), str.size());
		std::unique_ptr<char[]> out(new char[outsize]);
		rtf_decompress(out.get(), str.c_str(), str.size());
		write(STDOUT_FILENO, out.get(), outsize);
	} else {
		char *out = nullptr;
		unsigned int outsize = 0;
		/*
		 * There is a rather high cost to libstdc++/libmapi startup
		 * (0.5s), so you should not use
		 * /usr/bin/time.
		 */
		auto start = std::chrono::steady_clock::now();
		rtf_compress(&out, &outsize, str.c_str(), str.size());
		auto stop = std::chrono::steady_clock::now();
		auto diff = std::chrono::duration<double>(stop - start);
		fprintf(stderr, "%zu bytes in %.6lf seconds = %.0f by/s\n",
			str.size(), diff.count(), str.size() / diff.count());
		write(STDOUT_FILENO, out, outsize);
		free(out);
	}
	return 0;
}
