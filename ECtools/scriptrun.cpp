/*
 * Copyright 2018+, Kopano and its licensors
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <kopano/platform.h>
#include <kopano/UnixUtil.h>

static bool ignore_name(const char *n)
{
	size_t len = strlen(n);
	if (len >= 1 && n[0] == '#')
		return true;
	if (len >= 1 && n[len-1] == '~')
		return true;
	if (len >= 4 && strcmp(&n[len-4], ".bak") == 0)
		return true;
	if (len >= 4 && strcmp(&n[len-4], ".old") == 0)
		return true;
	if (len >= 7 && strcmp(&n[len-7], ".rpmnew") == 0)
		return true;
	if (len >= 8 && strcmp(&n[len-8], ".rpmorig") == 0)
		return true;
	return false;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "One or more directories need to be specified where to scan for, and execute, scripts.\n");
		return EXIT_FAILURE;
	}

	std::map<std::string, std::string> scripts; /* base name -> full path */
	for (int i = 1; i < argc; ++i) {
		std::unique_ptr<DIR, KC::fs_deleter> dh(opendir(argv[i]));
		if (dh == nullptr) {
			fprintf(stderr, "Could not process %s: %s", argv[i], strerror(errno));
			continue;
		}
		struct dirent *de;
		while ((de = readdir(dh.get())) != nullptr) {
			if (ignore_name(de->d_name)) {
				printf("Ignoring \"%s/%s\"\n", argv[i], de->d_name);
				continue;
			}
			struct stat sb;
			auto ret = fstatat(dirfd(dh.get()), de->d_name, &sb, 0);
			if (ret < 0)
				continue;
			if (S_ISREG(sb.st_mode))
				scripts[de->d_name] = argv[i] + std::string{"/"} + de->d_name;
			else
				scripts.erase(de->d_name);
		}
	}
	for (const auto &pair : scripts) {
		printf("Executing \"%s\"...\n", pair.second.c_str());
		auto ret = vfork();
		if (ret == 0) {
			execl(pair.second.c_str(), pair.first.c_str(), nullptr);
			_exit(EXIT_FAILURE);
		}
		wait(nullptr);
	}
	return EXIT_SUCCESS;
}
