/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_UNIXUTIL_H
#define EC_UNIXUTIL_H

#include <string>
#include <vector>
#include <dirent.h>
#include <kopano/zcdefs.h>

namespace KC {

class ECConfig;

class fs_deleter {
	public:
	void operator()(DIR *d) const
	{
		if (d != nullptr)
			closedir(d);
	}
};

extern KC_EXPORT int unix_runas(ECConfig *);
extern KC_EXPORT int unix_chown(const char *filename, const char *user, const char *group);
extern KC_EXPORT void unix_coredump_enable(const char *);
extern KC_EXPORT int unix_create_pidfile(const char *argv0, ECConfig *, bool force = true);
extern KC_EXPORT int unix_fork_function(void *(*)(void *), void *param, int nfds, int *closefds);
extern KC_EXPORT bool unix_system(const char *logname, const std::vector<std::string> &cmd, const char **env);
extern KC_EXPORT int ec_reexec(const char *const *);
extern KC_EXPORT void ec_reexec_finalize();

} /* namespace */

#endif
