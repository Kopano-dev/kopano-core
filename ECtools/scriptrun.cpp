/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2018+, Kopano and its licensors
 */
#include <string>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>
#include <kopano/fileutil.hpp>

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "One or more directories need to be specified where to scan for, and execute, scripts.\n");
		return EXIT_FAILURE;
	}

	auto st = KC::dexec_scan(std::vector<std::string>(&argv[1], &argv[argc]));
	for (const auto &w : st.warnings)
		printf("%s\n", w.c_str());
	st.warnings.clear();
	for (const auto &pair : st.prog) {
		printf("Executing \"%s\"...\n", pair.second.c_str());
		const char *const args[] = {pair.second.c_str(), pair.first.c_str(), nullptr};
		pid_t pid = 0;
		auto ret = posix_spawn(&pid, pair.second.c_str(), nullptr, nullptr, const_cast<char **>(args), environ);
		if (ret == 0)
			waitpid(pid, nullptr, 0);
	}
	return EXIT_SUCCESS;
}
