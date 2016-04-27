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

#include <kopano/platform.h>
#include <kopano/UnixUtil.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <sys/file.h>
#include <sys/resource.h>

#include <string>
using namespace std;

static int unix_runpath(ECConfig *conf)
{
	const char *path = conf->GetSetting("running_path");
	int ret;

	if (path != NULL) {
		ret = chdir(path);
		if (ret != 0)
			ec_log_err("Unable to run in given path \"%s\": %s", path, strerror(errno));
	}
	if (path == NULL || ret != 0) {
		ret = chdir("/");
		if (ret != 0)
			ec_log_err("chdir /: %s\n", strerror(errno));
	}
	return ret;
}

int unix_runas(ECConfig *lpConfig, ECLogger *lpLogger) {
	const char *group = lpConfig->GetSetting("run_as_group");
	const char *user  = lpConfig->GetSetting("run_as_user");
	int ret;

	ret = unix_runpath(lpConfig);
	if (ret != 0)
		return ret;

	if (group != NULL && *group != '\0') {
		const struct group *gr = getgrnam(group);
		if (!gr) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Looking up group \"%s\" failed: %s", group, strerror(errno));
			return -1;
		}
		if (getgid() != gr->gr_gid && setgid(gr->gr_gid) != 0) {
			lpLogger->Log(EC_LOGLEVEL_CRIT, "Changing to group \"%s\" failed: %s", gr->gr_name, strerror(errno));
			return -1;
		}
	}

	if (user != NULL && *user != '\0') {
		const struct passwd *pw = getpwnam(user);
		if (!pw) {
			lpLogger->Log(EC_LOGLEVEL_ERROR, "Looking up user \"%s\" failed: %s", user, strerror(errno));
			return -1;
		}
		if (getuid() != pw->pw_uid && setuid(pw->pw_uid) != 0) {
			lpLogger->Log(EC_LOGLEVEL_CRIT, "Changing to user \"%s\" failed: %s", pw->pw_name, strerror(errno));
			return -1;
		}
	}

	return 0;
}

int unix_chown(const char *filename, const char *username, const char *groupname) {
	const struct group *gr = NULL;
	const struct passwd *pw = NULL;
	uid_t uid;
	gid_t gid;

	gid = getgid();

	if (groupname && strcmp(groupname,"")) {
		gr = getgrnam(groupname);
		if (gr)
			gid = gr->gr_gid;
	}

	uid = getuid();

	if (username && strcmp(username,"")) {
		pw = getpwnam(username);
		if (pw)
			uid = pw->pw_uid;
	}

	return chown(filename, uid, gid);
}

void unix_coredump_enable(ECLogger *logger)
{
	struct rlimit limit;

	limit.rlim_cur = RLIM_INFINITY;
	limit.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_CORE, &limit) < 0 && logger != NULL)
		logger->Log(EC_LOGLEVEL_ERROR, "Unable to raise coredump filesize limit: %s", strerror(errno));
}

int unix_create_pidfile(const char *argv0, ECConfig *lpConfig,
    ECLogger *lpLogger, bool bForce)
{
	string pidfilename = "/var/run/kopano/" + string(argv0) + ".pid";
	FILE *pidfile;
	int oldpid;
	char tmp[255];
	bool running = false;

	if (strcmp(lpConfig->GetSetting("pid_file"), "")) {
		pidfilename = lpConfig->GetSetting("pid_file");
	}

	// test for existing and running process
	pidfile = fopen(pidfilename.c_str(), "r");
	if (pidfile) {
		fscanf(pidfile, "%d", &oldpid);
		fclose(pidfile);

		snprintf(tmp, 255, "/proc/%d/cmdline", oldpid);
		pidfile = fopen(tmp, "r");
		if (pidfile) {
			fscanf(pidfile, "%s", tmp);
			fclose(pidfile);

			if (strlen(tmp) < strlen(argv0)) {
				if (strstr(argv0, tmp))
					running = true;
			} else {
				if (strstr(tmp, argv0))
					running = true;
			}

			if (running) {
				lpLogger->Log(EC_LOGLEVEL_FATAL, "Warning: Process %s is probably already running.", argv0);
				if (!bForce) {
					lpLogger->Log(EC_LOGLEVEL_FATAL, "If you are sure the process is stopped, please remove pidfile %s", pidfilename.c_str());
					return -1;
				}
			}
		}
	}

	pidfile = fopen(pidfilename.c_str(), "w");
	if (!pidfile) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Unable to open pidfile '%s'", pidfilename.c_str());
		return 1;
	}

	fprintf(pidfile, "%d\n", getpid());
	fclose(pidfile);
	return 0;
}

int unix_daemonize(ECConfig *lpConfig, ECLogger *lpLogger) {
	int ret;

	// make sure we daemonize in an always existing directory
	ret = unix_runpath(lpConfig);
	if (ret != 0)
		return ret;

	ret = fork();
	if (ret == -1) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Daemonizing failed on 1st step");
		return -1;
	}
	if (ret)
		_exit(0);				// close parent process

	setsid();					// start new session

	ret = fork();
	if (ret == -1) {
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Daemonizing failed on 2nd step");
		return -1;
	}
	if (ret)
		_exit(0);				// close parent process

	// close output to console. a logger which logged to the console is now diverted to /dev/null
	fclose(stdin);
	freopen("/dev/null", "a+", stdout);
	freopen("/dev/null", "a+", stderr);

	return 0;
}

/**
 * Starts a new unix process and calls the given function. Optionally
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
	int pid;

	if (!func)
		return -1;

	pid = fork();
	if (pid < 0)
		return pid;

	if (pid == 0) {
		// reset the SIGHUP signal to default, not to trigger the config/logfile reload signal too often on 'killall <process>'
		signal(SIGHUP, SIG_DFL);
		// close filedescriptors
		for (int n = 0; n < nCloseFDs && pCloseFDs != NULL; ++n)
			if (pCloseFDs[n] >= 0)
				close(pCloseFDs[n]);
		func(param);
		// call normal cleanup exit
		exit(0);
	}

	return pid;
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
static pid_t unix_popen_rw(const char *lpszCommand, int *lpulIn, int *lpulOut,
    popen_rlimit_array *lpLimits, const char **env, bool bNonBlocking,
    bool bStdErr)
{
	int ulIn[2];
	int ulOut[2];
	pid_t pid;

	if (!lpszCommand || !lpulIn || !lpulOut)
		return -1;

	if (pipe(ulIn) || pipe(ulOut))
		return -1;

	if (bNonBlocking) {
		if (fcntl(ulIn[0], F_SETFL, O_NONBLOCK) < 0 || fcntl(ulIn[1], F_SETFL, O_NONBLOCK) < 0 ||
			fcntl(ulOut[0], F_SETFL, O_NONBLOCK) < 0 || fcntl(ulOut[1], F_SETFL, O_NONBLOCK) < 0)
			return -1;
	}

	pid = vfork();
	if (pid < 0)
		return pid;

	if (pid == 0) {
		/* Close pipes we aren't going to use */
		close(ulIn[STDOUT_FILENO]);
		dup2(ulIn[STDIN_FILENO], STDIN_FILENO);
		close(ulOut[STDIN_FILENO]);
		dup2(ulOut[STDOUT_FILENO], STDOUT_FILENO);
		if (bStdErr)
			dup2(ulOut[STDOUT_FILENO], STDERR_FILENO);

		// give the process a new group id, so we can easely kill all sub processes of this child too when needed.
		setsid();

		/* If provided set rlimit settings */
		if (lpLimits != NULL)
			for (unsigned int i = 0; i < lpLimits->cValues; ++i)
				if (setrlimit(lpLimits->sLimit[i].resource, &lpLimits->sLimit[i].limit) != 0)
					ec_log_err("Unable to set rlimit for popen - resource %d, errno %d",
								  lpLimits->sLimit[i].resource, errno);

		if (execle("/bin/sh", "sh", "-c", lpszCommand, NULL, env) == 0)
			_exit(EXIT_SUCCESS);
		else
			_exit(EXIT_FAILURE);
		return 0;
	}

	*lpulIn = ulIn[STDOUT_FILENO];
	close(ulIn[STDIN_FILENO]);

	*lpulOut = ulOut[STDIN_FILENO];
	close(ulOut[STDOUT_FILENO]);

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
bool unix_system(const char *lpszLogName, const char *lpszCommand, const char **env)
{
	int fdin = 0, fdout = 0;
	int pid = unix_popen_rw(lpszCommand, &fdin, &fdout, NULL, env, false, true);
	char buffer[1024];
	int status = 0;
	bool rv = true;
	FILE *fp = fdopen(fdout, "rb");
	close(fdin);
	
	while (fgets(buffer, sizeof(buffer), fp)) {
		buffer[strlen(buffer) - 1] = '\0'; // strip enter
		ec_log_debug("%s[%d]: %s", lpszLogName, pid, buffer);
	}
	
	fclose(fp);
	
	waitpid(pid, &status, 0);

	if(status != -1) {
#ifdef WEXITSTATUS
		if(WIFEXITED(status)) { /* Child exited by itself */
			if(WEXITSTATUS(status)) {
				ec_log_err("Command `%s` exited with non-zero status %d", lpszCommand, WEXITSTATUS(status));
				rv = false;
			}
			else
				ec_log_info("Command `%s` ran successfully", lpszCommand);
		} else if(WIFSIGNALED(status)) {        /* Child was killed by a signal */
			ec_log_err("Command `%s` was killed by signal %d", lpszCommand, WTERMSIG(status));
			rv = false;
		} else {                        /* Something strange happened */
			ec_log_err(string("Command `") + lpszCommand + "` terminated abnormally");
			rv = false;
		}
#else
		if (status)
			ec_log_err("Command `%s` exited with status %d", lpszCommand, status);
		else
			ec_log_info("Command `%s` ran successfully", lpszCommand);
#endif
	} else {
		ec_log_err(string("System call \"system\" failed: ") + strerror(errno));
		rv = false;
	}
	
	return rv;
}
