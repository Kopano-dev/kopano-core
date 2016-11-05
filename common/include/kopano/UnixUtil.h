/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __UNIXUTIL_H
#define __UNIXUTIL_H

#include <sys/resource.h>
#include <kopano/zcdefs.h>
#include <kopano/ECConfig.h>

struct popen_rlimit {
	int resource;
	struct rlimit limit;
};

struct popen_rlimit_array {
	unsigned int cValues;
	struct popen_rlimit sLimit[1];
};

#define sized_popen_rlimit_array(_climit, _name) \
struct _popen_rlimit_array_ ## _name { \
	unsigned int cValues; \
	struct popen_rlimit sLimit[_climit]; \
} _name

extern "C" {

extern _kc_export int unix_runas(ECConfig *);
extern _kc_export int unix_chown(const char *filename, const char *user, const char *group);
extern _kc_export void unix_coredump_enable(void);
extern _kc_export int unix_create_pidfile(const char *argv0, ECConfig *, bool force = true);
extern _kc_export int unix_daemonize(ECConfig *);
extern _kc_export int unix_fork_function(void *(*)(void *), void *param, int nfds, int *closefds);
extern _kc_export bool unix_system(const char *logname, const char *command, const char **env);

} /* extern "C" */

#endif
