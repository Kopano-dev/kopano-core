/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <memory>
#include <new>
#include <kopano/platform.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/UnixUtil.h>
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <kopano/stringutil.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <spawn.h>
#include <pwd.h>
#include <grp.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <sys/file.h>
#include <sys/resource.h>
#include <string>
#include <libHX/string.h>

namespace KC {

static int unix_runpath()
{
	auto ret = chdir("/");
	if (ret != 0)
		ec_log_err("chdir /: %s", strerror(errno));
	return ret;
}

/**
 * Change group identities for the process.
 * Returns <0 on error, 0 on success (no action), >0 on success (actioned).
 */
static int unix_runasgroup(const struct passwd *pw, const char *group)
{
	char *end;
	auto gid = strtoul(group, &end, 10);
	auto gr = *end == '\0' ? getgrgid(gid) : getgrnam(group);
	if (gr == nullptr) {
		ec_log_err("Looking up group \"%s\" failed: %s", group, strerror(errno));
		return -1;
	}
	bool do_setgid = getgid() != gr->gr_gid || getegid() != gr->gr_gid;
	if (do_setgid && setgid(gr->gr_gid) != 0) {
		ec_log_crit("Changing to group \"%s\" failed: %s", gr->gr_name, strerror(errno));
		return -1;
	}
	if (pw == nullptr)
		/* No user change desired, so no initgroups desired. */
		return do_setgid;
	if (geteuid() == pw->pw_uid) {
		/*
		 * The process is already the (supposedly unprivileged) target
		 * user - initgroup is unlikely to succeed, so ignore its
		 * return value.
		 */
		initgroups(pw->pw_name, gr->gr_gid);
		return do_setgid;
	}
	if (initgroups(pw->pw_name, gr->gr_gid) != 0) {
		ec_log_crit("Changing supplementary groups failed: %s", strerror(errno));
		return -1;
	}
	return do_setgid;
}

/**
 * Change user and group identities for the process.
 * Returns <0 on error, 0 on success (no action), >0 on success (actioned).
 */
int unix_runas(ECConfig *lpConfig)
{
	const char *group = lpConfig->GetSetting("run_as_group");
	const char *user  = lpConfig->GetSetting("run_as_user");
	auto ret = unix_runpath();
	if (ret != 0)
		return ret;

	const struct passwd *pw = nullptr;
	if (user != nullptr && *user != '\0') {
		char *end;
		auto uid = strtoul(user, &end, 10);
		pw = *end == '\0' ? getpwuid(uid) : getpwnam(user);
		if (!pw) {
			ec_log_err("Looking up user \"%s\" failed: %s", user, strerror(errno));
			return -1;
		}
	}
	if (group != nullptr && *group != '\0') {
		ret = unix_runasgroup(pw, group);
		if (ret < 0)
			return ret;
	}
	bool do_setuid = pw != nullptr && (getuid() != pw->pw_uid || geteuid() != pw->pw_uid);
	if (do_setuid && setuid(pw->pw_uid) != 0) {
		ec_log_crit("Changing to user \"%s\" failed: %s", pw->pw_name, strerror(errno));
		return -1;
	}
	if (do_setuid)
		ret = 1;
	return ret;
}

int unix_chown(const char *filename, const char *username, const char *groupname) {
	auto gid = getgid();
	if (groupname && strcmp(groupname,"")) {
		auto gr = getgrnam(groupname);
		if (gr)
			gid = gr->gr_gid;
	}
	auto uid = getuid();
	if (username && strcmp(username,"")) {
		auto pw = getpwnam(username);
		if (pw)
			uid = pw->pw_uid;
	}
	return chown(filename, uid, gid) == 0 ? 0 : -errno;
}

static char *read_one_line(const char *tunable)
{
	/*
	 * Read one byte from a sysctl file. No effect on non-Linux or
	 * when procfs is not mounted.
	 */
	auto fp = fopen(tunable, "r");
	if (fp == nullptr)
		return nullptr;
	hxmc_t *ret = nullptr;
	HX_getl(&ret, fp);
	fclose(fp);
	return ret;
}

void unix_coredump_enable(const char *mode)
{
	if (strcasecmp(mode, "systemdefault") == 0) {
		ec_log_info("Coredump status left at system default.");
		return;
	}
	struct rlimit limit;
	if (!parseBool(mode)) {
		limit.rlim_cur = limit.rlim_max = 0;
		if (setrlimit(RLIMIT_CORE, &limit) == 0)
			ec_log_notice("Coredumps are disabled via configuration file.");
		return;
	}
	auto pattern = read_one_line("/proc/sys/kernel/core_pattern");
	if (pattern == nullptr || *pattern == '\0') {
		ec_log_err("Coredumps are not enabled in the OS: sysctl kernel.core_pattern is empty.");
	} else if (*pattern == '/') {
		HX_chomp(pattern);
		std::unique_ptr<char[], cstdlib_deleter> path(HX_dirname(pattern));
		if (access(path.get(), W_OK) < 0)
			ec_log_err("Coredump path \"%s\" is inaccessible: %s.", path.get(), strerror(errno));
	}
	HXmc_free(pattern);
	limit.rlim_cur = RLIM_INFINITY;
	limit.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_CORE, &limit) < 0) {
		int err = errno;
		limit.rlim_cur = 0;
		limit.rlim_max = 0;
		getrlimit(RLIMIT_CORE, &limit);
		ec_log_err("Cannot set coredump limit to infinity: %s. Current limit: %llu bytes.",
			strerror(err), static_cast<unsigned long long>(limit.rlim_cur));
	}
}

int unix_create_pidfile(const char *argv0, ECConfig *lpConfig, bool bForce)
{
	const char *progname = strrchr(argv0, '/');
	if (progname == nullptr)
		progname = argv0;
	auto pidfilename = std::string("/var/run/kopano/") + progname + ".pid";
	int oldpid;
	char tmp[256];
	bool running = false;

	if (strcmp(lpConfig->GetSetting("pid_file"), ""))
		pidfilename = lpConfig->GetSetting("pid_file");

	// test for existing and running process
	auto pidfile = fopen(pidfilename.c_str(), "r");
	if (pidfile) {
		if (fscanf(pidfile, "%d", &oldpid) < 1)
			oldpid = -1;
		fclose(pidfile);

		snprintf(tmp, 255, "/proc/%d/cmdline", oldpid);
		pidfile = fopen(tmp, "r");
		if (pidfile) {
			memset(tmp, '\0', sizeof(tmp));
			if (fscanf(pidfile, "%255s", tmp) < 1)
				/* nothing */;
			fclose(pidfile);

			if (strlen(tmp) < strlen(argv0)) {
				if (strstr(argv0, tmp))
					running = true;
			} else if (strstr(tmp, argv0)) {
				running = true;
			}

			if (running) {
				ec_log_crit("Warning: Process %s is probably already running.", argv0);
				if (!bForce) {
					ec_log_crit("If you are sure the process is stopped, please remove pidfile %s", pidfilename.c_str());
					return -1;
				}
			}
		}
	}

	pidfile = fopen(pidfilename.c_str(), "w");
	if (!pidfile) {
		ec_log_err("Unable to open pidfile \"%s\": %s", pidfilename.c_str(), strerror(errno));
		return 1;
	}

	fprintf(pidfile, "%d\n", getpid());
	fclose(pidfile);
	return 0;
}

/**
 * Starts a new Unix process and calls the given function. Optionally
 * closes some given file descriptors. The child process does not
 * return from this function.
 *
 * @note the child process calls exit(0) at exit, not _exit(0), so
 * atexit() and on_exit() callbacks from the parent are called and
 * tmpfile() created files are removed from either parent and child.
 * This is wanted behaviour for us since we don't use any exit
 * callbacks and tmpfiles, but we do want coverage output from gcov,
 * which seems to use an exit callback to write the usage info.
 *
 * @param[in]	func	Pointer to a function with one void* parameter and returning a void* that should run in the child process.
 * @param[in]	param	Parameter to pass to the func function.
 * @param[in]	nCloseFDs	Number of file descriptors in pCloseFDs.
 * @param[in]	pCloseFDs	Array of file descriptors to close in the child process.
 * @retval	processid of the started child, or a negative value on error.
 */
int unix_fork_function(void*(func)(void*), void *param, int nCloseFDs, int *pCloseFDs)
{
	if (!func)
		return -1;
	auto pid = fork();
	if (pid != 0)
		return pid;
	// reset the SIGHUP signal to default, not to trigger the config/logfile reload signal too often on 'killall <process>'
	signal(SIGHUP, SIG_DFL);
	// close filedescriptors
	for (int n = 0; n < nCloseFDs && pCloseFDs != NULL; ++n)
		if (pCloseFDs[n] >= 0)
			close(pCloseFDs[n]);
	func(param);
	// call normal cleanup exit
	exit(0);
	return 0;
}

/**
 * Starts a new process with a read and write channel. Optionally sets
 * resource limites and environment variables.
 *
 * @param lpLogger[in] Logger object where error messages during the function may be sent to. Cannot be NULL.
 * @param lpszCommand[in] The command to execute in the new subprocess.
 * @param lpulIn[out] The filedescriptor to read data of the command from.
 * @param lpulOut[out] The filedescriptor to write data to the command to.
 * @param lpLimits[in] Optional resource limits to set for the new subprocess.
 * @param env[in] Optional environment variables to set in the new subprocess.
 * @param bNonBlocking[in] Make the in and out pipes non-blocking on read and write calls.
 * @param bStdErr[in] Add STDERR output to *lpulOut
 *
 * @return new process pid, or -1 on failure.
 */
pid_t unix_popen_rw(const char *const *argv, int *lpulIn, int *lpulOut,
    int *lpulErr, const char **env)
{
	posix_spawn_file_actions_t fa;
	int ulIn[2] = {-1, -1}, ulOut[2] = {-1, -1};
	int ulErr[2] = {-1, -1}, nullfd = -1;
	pid_t pid = -1;

	if (argv == nullptr || argv[0] == nullptr)
		return -EINVAL;
	auto cl1 = make_scope_success([&]() {
		if (ulIn[0] != -1)
			close(ulIn[0]);
		if (ulIn[1] != -1)
			close(ulIn[1]);
		if (ulOut[0] != -1)
			close(ulOut[0]);
		if (ulOut[1] != -1)
			close(ulOut[1]);
		if (ulErr[0] != -1)
			close(ulErr[0]);
		if (ulErr[1] != -1)
			close(ulErr[1]);
		if (nullfd != -1)
			close(nullfd);
	});
	if (lpulIn == nullptr || lpulOut == nullptr || lpulErr == nullptr) {
		nullfd = open("/dev/null", O_RDWR);
		if (nullfd < 0)
			return -errno;
	}
	memset(&fa, 0, sizeof(fa));
	auto ret = posix_spawn_file_actions_init(&fa);
	if (ret != 0)
		return -ret;
	auto cl2 = make_scope_success([&]() { posix_spawn_file_actions_destroy(&fa); });

	/* Close child-unused ends of the pipes; move child-used ends to fd 0-2. */
	if (lpulIn != nullptr) {
		if (pipe(ulIn) < 0)
			return -errno;
		ret = posix_spawn_file_actions_addclose(&fa, ulIn[1]);
		if (ret != 0)
			return -ret;
		ret = posix_spawn_file_actions_adddup2(&fa, ulIn[0], STDIN_FILENO);
		if (ret != 0)
			return -ret;
	} else {
		ret = posix_spawn_file_actions_adddup2(&fa, nullfd, STDIN_FILENO);
		if (ret != 0)
			return -ret;
	}

	if (lpulOut != nullptr) {
		if (pipe(ulOut) < 0)
			return -errno;
		ret = posix_spawn_file_actions_addclose(&fa, ulOut[0]);
		if (ret != 0)
			return -ret;
		ret = posix_spawn_file_actions_adddup2(&fa, ulOut[1], STDOUT_FILENO);
		if (ret != 0)
			return -ret;
	} else {
		ret = posix_spawn_file_actions_adddup2(&fa, nullfd, STDOUT_FILENO);
		if (ret != 0)
			return -ret;
	}

	if (lpulErr == nullptr) {
		ret = posix_spawn_file_actions_adddup2(&fa, nullfd, STDERR_FILENO);
		if (ret != 0)
			return -ret;
	} else if (lpulErr == lpulOut) {
		if ((ret = posix_spawn_file_actions_adddup2(&fa, ulOut[1], STDERR_FILENO)) != 0)
			return ret;
	} else {
		if (lpulErr != nullptr && lpulErr != lpulOut && pipe(ulErr) < 0)
			return -errno;
		ret = posix_spawn_file_actions_addclose(&fa, ulErr[0]);
		if (ret != 0)
			return -ret;
		if ((ret = posix_spawn_file_actions_adddup2(&fa, ulErr[1], STDERR_FILENO)) != 0)
			return ret;
	}

	/* Close all pipe ends that were not already fd 0-2. */
	if (ulIn[0] != -1 && ulIn[0] != STDIN_FILENO && (ret = posix_spawn_file_actions_addclose(&fa, ulIn[0])) != 0)
		return ret;
	if (lpulErr != lpulOut) {
		if (ulOut[1] != -1 && ulOut[1] != STDOUT_FILENO &&
		    (ret = posix_spawn_file_actions_addclose(&fa, ulOut[1])) != 0)
			return ret;
		if (ulErr[1] != -1 && ulErr[1] != STDERR_FILENO &&
		    (ret = posix_spawn_file_actions_addclose(&fa, ulErr[1])) != 0)
			return ret;
	} else {
		if (ulOut[1] != -1 && ulOut[1] != STDOUT_FILENO && ulOut[1] != STDERR_FILENO &&
		    (ret = posix_spawn_file_actions_addclose(&fa, ulOut[1])) != 0)
			return ret;
	}
	if (nullfd != -1 && nullfd != STDIN_FILENO && nullfd != STDOUT_FILENO && nullfd != STDERR_FILENO &&
	    (ret = posix_spawn_file_actions_addclose(&fa, nullfd)) != 0)
		return ret;

	ret = posix_spawn(&pid, argv[0], &fa, nullptr, const_cast<char **>(argv), const_cast<char **>(env));
	if (ret != 0)
		return -ret;
	if (lpulIn != nullptr) {
		*lpulIn = ulIn[1];
		ulIn[1] = -1;
	}
	if (lpulOut != nullptr) {
		*lpulOut = ulOut[0];
		ulOut[0] = -1;
	}
	if (lpulErr != nullptr && lpulErr != lpulOut) {
		*lpulErr = ulErr[0];
		ulErr[0] = -1;
	}
	return pid;
}

/**
 * Start an external process
 *
 * This function is used to start an external process that requires no STDIN input. The output will be
 * logged via lpLogger if provided.
 *
 * @param lpLogger[in] 		NULL or pointer to logger object to log to (will be logged via EC_LOGLEVEL_INFO)
 * @param lsszLogName[in]	Name to show in the log. Will show NAME[pid]: DATA
 * @param lpszCommand[in] 	String to command to be started, which will be executed with /bin/sh -c "string"
 * @param env[in] 			NULL-terminated array of strings with environment settings in the form ENVNAME=VALUE, see
 *                			execlp(3) for details
 *
 * @return Returns TRUE on success, FALSE on failure
 */
bool unix_system(const char *lpszLogName, const std::vector<std::string> &cmd,
    const char **env)
{
	int argc = 0;
	if (cmd.size() == 0)
		return false;
	auto argv = make_unique_nt<const char *[]>(cmd.size() + 1);
	if (argv == nullptr)
		return false;
	for (const auto &e : cmd)
		argv[argc++] = e.c_str();
	argv[argc] = nullptr;

	auto cmdtxt = "\"" + kc_join(cmd, "\" \"") + "\"";
	int fdin = 0, fdout = 0;
	auto pid = unix_popen_rw(argv.get(), &fdin, &fdout, nullptr, env);
	ec_log_debug("Running command: %s", cmdtxt.c_str());
	if (pid < 0) {
		ec_log_debug("popen(%s) failed: %s", cmdtxt.c_str(), strerror(errno));
		return false;
	}
	close(fdin);
	int newfd = ec_relocate_fd(fdout);
	if (newfd >= 0)
		fdout = newfd;
	FILE *fp = fdopen(fdout, "rb");
	if (fp == nullptr) {
		close(fdout);
		return false;
	}

	char buffer[1024];
	while (fgets(buffer, sizeof(buffer), fp)) {
		size_t z = strlen(buffer);
		if (z > 0 && buffer[z-1] == '\n')
			buffer[--z] = '\0';
		ec_log_debug("%s[%d]: %s", lpszLogName, pid, buffer);
	}

	fclose(fp);
	int status = 0;
	if (waitpid(pid, &status, 0) < 0)
		return false;
	if (status == -1) {
		ec_log_err(std::string("System call \"system\" failed: ") + strerror(errno));
		return false;
	}
	bool rv = true;
#ifdef WEXITSTATUS
	if (WIFEXITED(status)) { /* Child exited by itself */
		if (WEXITSTATUS(status)) {
			ec_log_err("Command %s exited with non-zero status %d", cmdtxt.c_str(), WEXITSTATUS(status));
			rv = false;
		}
		else
			ec_log_info("Command %s ran successfully", cmdtxt.c_str());
	} else if (WIFSIGNALED(status)) {        /* Child was killed by a signal */
		ec_log_err("Command %s was killed by signal %d", cmdtxt.c_str(), WTERMSIG(status));
		rv = false;
	} else {                        /* Something strange happened */
		ec_log_err("Command %s terminated abnormally", cmdtxt.c_str());
		rv = false;
	}
#else
	if (status)
		ec_log_err("Command %s exited with status %d", cmdtxt.c_str(), status);
	else
		ec_log_info("Command %s ran successfully", cmdtxt.c_str());
#endif
	return rv;
}

void ec_reexec_finalize()
{
	auto s = getenv("KC_REEXEC_DONE");
	if (s == nullptr)
		return; /* nothing to do */
	unsetenv("KC_REEXEC_DONE");
	s = getenv("KC_ORIGINAL_PRELOAD");
	if (s == nullptr) {
		unsetenv("LD_PRELOAD");
	} else {
		setenv("LD_PRELOAD", s, true);
		unsetenv("KC_ORIGINAL_PRELOAD");
	}
}

/**
 * Reexecute the program with new UIDs and/or new preloaded
 * libraries.
 *
 *
 * The so-restarted process must invoke ec_reexec() again, so as to
 * get the environment variables cleaned.
 */
int ec_reexec(const char *const *argv)
{
	if (getenv("KC_AVOID_REEXEC") != nullptr)
		return 0;
	auto s = getenv("KC_REEXEC_DONE");
	if (s != nullptr) {
		ec_reexec_finalize();
		return 0;
	}

	/* 1st time ec_reexec is called. */
	setenv("KC_REEXEC_DONE", "1", true);

	/* Resolve "exe" symlink before exec to please the sysadmin */
	std::vector<char> linkbuf(16); /* mutable std::string::data is C++17 only */
	ssize_t linklen;
	while (true) {
		linklen = readlink("/proc/self/exe", &linkbuf[0], linkbuf.size());
		if (linklen < 0 || static_cast<size_t>(linklen) < linkbuf.size())
			break;
		linkbuf.resize(linkbuf.size() * 2);
	}
	if (linklen < 0) {
		int ret = -errno;
		ec_log_debug("ec_reexec: readlink: %s", strerror(errno));
		return ret;
	}
	linkbuf[linklen] = '\0';
	ec_log_debug("Reexecing %s", &linkbuf[0]);
	execv(&linkbuf[0], const_cast<char **>(argv));
	return -errno;
}

} /* namespace */
