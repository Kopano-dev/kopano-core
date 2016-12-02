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
#include <kopano/ECLogger.h>
#include <kopano/lockhelper.hpp>
#include <cassert>
#include <clocale>
#include <pthread.h>
#include <cstdarg>
#include <csignal>
#include <zlib.h>
#include <kopano/stringutil.h>
#include "charset/localeutil.h"
#include "config.h"
#include <poll.h>
#if HAVE_SYSLOG_H
#include <syslog.h>
#endif
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <kopano/ECConfig.h>

using namespace std;

static const char *const ll_names[] = {
	" notice",
	"crit   ",
	"error  ",
	"warning",
	"notice ",
	"info   ",
	"debug  "
};

static ECLogger_File ec_log_fallback_target(EC_LOGLEVEL_WARNING, false, "-", false);
static ECLogger *ec_log_target = &ec_log_fallback_target;

ECLogger::ECLogger(int max_ll) {
	max_loglevel = max_ll;
	// get system locale for time, NULL is returned if locale was not found.
	timelocale = createlocale(LC_TIME, "C");
	datalocale = createUTF8Locale();
	prefix = LP_NONE;
	m_ulRef = 1;
}

ECLogger::~ECLogger() {
	if (ec_log_target == this)
		ec_log_set(NULL);

	if (timelocale)
		freelocale(timelocale);

	if (datalocale)
		freelocale(datalocale);
}

void ECLogger::SetLoglevel(unsigned int max_ll) {
	max_loglevel = max_ll;
}

std::string ECLogger::MakeTimestamp() {
	time_t now = time(NULL);
	tm local;

	localtime_r(&now, &local);
	char buffer[_LOG_TSSIZE];

	if (timelocale)
		strftime_l(buffer, sizeof buffer, "%c", &local, timelocale);
	else
		strftime(buffer, sizeof buffer, "%c", &local);

	return buffer;
}

bool ECLogger::Log(unsigned int loglevel) {
	unsigned int ext_bits = loglevel & EC_LOGLEVEL_EXTENDED_MASK;
	unsigned int level = loglevel & EC_LOGLEVEL_MASK;

	unsigned int max_loglevel_only = max_loglevel & EC_LOGLEVEL_MASK;

	unsigned int allowed_extended_bits_only = max_loglevel & EC_LOGLEVEL_EXTENDED_MASK;

	// any extended bits used? then only match those
	if (ext_bits) {
		// are any of the extended bits that were selected in the max_loglevel
		// set in the loglevel?
		if (ext_bits & allowed_extended_bits_only)
			return true;

		return false;
	}

	// continue if the maximum level of logging (so not the bits)
	// is not 0
	if (max_loglevel_only == EC_LOGLEVEL_NONE)
		return false;

	if (level == EC_LOGLEVEL_ALWAYS)
		return true;

	// is the parameter log-level <= max_loglevel?
	return level <= max_loglevel_only;
}

void ECLogger::SetLogprefix(logprefix lp) {
	prefix = lp;
}

unsigned int ECLogger::AddRef(void)
{
	scoped_lock locker(m_mutex);
	assert(m_ulRef < UINT_MAX);
	return ++m_ulRef;
}

unsigned ECLogger::Release() {
	ulock_normal locker(m_mutex);
	unsigned int ulRef = --m_ulRef;
	locker.unlock();
	if (ulRef == 0)
		delete this;
	return ulRef;
}

int ECLogger::snprintf(char *str, size_t size, const char *format, ...) {
	va_list va;
	int len = 0;

	va_start(va, format);
	len = _vsnprintf_l(str, size, format, datalocale, va);
	va_end(va);

	return len;
}

ECLogger_Null::ECLogger_Null() : ECLogger(EC_LOGLEVEL_NONE) {}
void ECLogger_Null::Reset() {}
void ECLogger_Null::Log(unsigned int loglevel, const string &message) {}
void ECLogger_Null::Log(unsigned int loglevel, const char *format, ...) {}
void ECLogger_Null::LogVA(unsigned int loglevel, const char *format, va_list& va) {}

/**
 * @param[in]	max_ll			max loglevel passed to ECLogger
 * @param[in]	add_timestamp	true if a timestamp before the logmessage is wanted
 * @param[in]	filename		filename of log in current locale
 */
ECLogger_File::ECLogger_File(const unsigned int max_ll, const bool add_timestamp, const char *const filename, const bool compress) : ECLogger(max_ll) {
	logname = filename;
	timestamp = add_timestamp;

	if (logname == "-") {
		init_for_stderr();
	} else {
		if (compress)
			init_for_gzfile();
		else
			init_for_file();
		log = fnOpen(logname.c_str(), szMode);
		if (log == nullptr) {
			init_for_stderr();
			fnPrintf(log, "Unable to open logfile %s: %s. Logging to stderr.\n",
				logname.c_str(), strerror(errno));
		}
	}
	reinit_buffer(0);

	// read/write is for the handle, not the f*-calls
	// so read is because we're only reading the handle ('log')
	// so only Reset() will do a write-lock
	pthread_rwlock_init(&handle_lock, NULL);

	prevcount = 0;
	prevmsg.clear();
	prevloglevel = 0;
	pthread_rwlock_init(&dupfilter_lock, NULL);
}

ECLogger_File::~ECLogger_File() {
	// not required at this stage but only added here for consistency
	pthread_rwlock_rdlock(&handle_lock);

	if (prevcount > 1) {
		fnPrintf(log, "%sPrevious message logged %d times\n", DoPrefix().c_str(), prevcount);
	}

	if (log && fnClose)
		fnClose(log);

	pthread_rwlock_unlock(&handle_lock);

	pthread_rwlock_destroy(&handle_lock);
	pthread_rwlock_destroy(&dupfilter_lock);
}

void ECLogger_File::init_for_stderr(void)
{
	log = stderr;
	fnOpen = nullptr;
	fnClose = nullptr;
	fnPrintf = reinterpret_cast<printf_func>(&fprintf);
	fnFileno = reinterpret_cast<fileno_func>(&fileno);
	szMode = nullptr;
}

void ECLogger_File::init_for_file(void)
{
	fnOpen = reinterpret_cast<open_func>(&fopen);
	fnClose = reinterpret_cast<close_func>(&fclose);
	fnPrintf = reinterpret_cast<printf_func>(&fprintf);
	fnFileno = reinterpret_cast<fileno_func>(&fileno);
	szMode = "a";
}

void ECLogger_File::init_for_gzfile(void)
{
	fnOpen = reinterpret_cast<open_func>(&gzopen);
	fnClose = reinterpret_cast<close_func>(&gzclose);
	fnPrintf = reinterpret_cast<printf_func>(&gzprintf);
	fnFileno = nullptr;
	szMode = "wb";
}

void ECLogger_File::reinit_buffer(size_t size)
{
	if (size == 0)
		setvbuf(static_cast<FILE *>(log), NULL, _IOLBF, size);
	else
		setvbuf(static_cast<FILE *>(log), NULL, _IOFBF, size);
	/* Store value for Reset() to re-invoke this function after reload */
	buffer_size = size;
}

void ECLogger_File::Reset() {
	pthread_rwlock_wrlock(&handle_lock);

	if (log != stderr && fnClose && fnOpen) {
		if (log)
			fnClose(log);

		log = fnOpen(logname.c_str(), szMode);
		reinit_buffer(buffer_size);
	}

	pthread_rwlock_unlock(&handle_lock);
}

int ECLogger_File::GetFileDescriptor() {
	int fd = -1;

	pthread_rwlock_rdlock(&handle_lock);

	if (log && fnFileno)
		fd = fnFileno(log);

	pthread_rwlock_unlock(&handle_lock);

	return fd;
}

std::string ECLogger_File::EmitLevel(const unsigned int loglevel) {
	if (loglevel == EC_LOGLEVEL_ALWAYS)
		return format("[%s] ", ll_names[0]);
	else if (loglevel <= EC_LOGLEVEL_DEBUG)
		return format("[%s] ", ll_names[loglevel]);

	return format("[%7x] ", loglevel);
}

/**
 * Prints the optional timestamp and prefix to the log.
 */
std::string ECLogger_File::DoPrefix() {
	std::string out;

	if (timestamp)
		out += MakeTimestamp() + ": ";

	if (prefix == LP_TID) {
#ifdef HAVE_PTHREAD_GETNAME_NP
		pthread_t th = pthread_self();
		char name[32] = { 0 };

		if (pthread_getname_np(th, name, sizeof name))
			out += format("[0x%08x] ", (unsigned int)th);
		else
			out += format("[%s|0x%08x] ", name, (unsigned int)th);
#else
		out += format("[0x%p] ", pthread_self()); // good enough?
#endif
	}
	else if (prefix == LP_PID) {
		out += format("[%5d] ", getpid());
	}

	return out;
}

/**
 * Check if the ECLogger_File instance is logging to stderr.
 * @retval	true	This instance is logging to stderr.
 * @retval	false	This instance is not logging to stderr.
 */
bool ECLogger_File::IsStdErr() {
	return logname == "-";
}

bool ECLogger_File::DupFilter(const unsigned int loglevel, const std::string &message) {
	bool exit_with_true = false;

	pthread_rwlock_rdlock(&dupfilter_lock);
	if (prevmsg == message) {
		++prevcount;

		if (prevcount < 100)
			exit_with_true = true;
	}
	pthread_rwlock_unlock(&dupfilter_lock);

	if (exit_with_true)
		return true;

	if (prevcount > 1) {
		pthread_rwlock_rdlock(&handle_lock);
		fnPrintf(log, "%s%sPrevious message logged %d times\n", DoPrefix().c_str(), EmitLevel(prevloglevel).c_str(), prevcount);
		pthread_rwlock_unlock(&handle_lock);
	}

	pthread_rwlock_wrlock(&dupfilter_lock);
	prevloglevel = loglevel;
	prevmsg = message;
	prevcount = 0;
	pthread_rwlock_unlock(&dupfilter_lock);

	return false;
}

void ECLogger_File::Log(unsigned int loglevel, const string &message) {
	if (!ECLogger::Log(loglevel))
		return;

	if (!DupFilter(loglevel, message)) {
		pthread_rwlock_rdlock(&handle_lock);

		if (log) {
			fnPrintf(log, "%s%s%s\n", DoPrefix().c_str(), EmitLevel(loglevel).c_str(), message.c_str());
			/*
			 * If IOLBF was set (buffer_size==0), the previous
			 * print call already flushed it. Do not flush again
			 * in that case.
			 */
			if (buffer_size > 0 && (loglevel <= EC_LOGLEVEL_WARNING || loglevel == EC_LOGLEVEL_ALWAYS))
				fflush((FILE *)log);
		}

		pthread_rwlock_unlock(&handle_lock);
	}
}

void ECLogger_File::Log(unsigned int loglevel, const char *format, ...) {
	va_list va;

	if (ECLogger::Log(loglevel)) {
		va_start(va, format);
		LogVA(loglevel, format, va);
		va_end(va);
	}
}

void ECLogger_File::LogVA(unsigned int loglevel, const char *format, va_list& va) {
	char msgbuffer[_LOG_BUFSIZE];
	_vsnprintf_l(msgbuffer, sizeof msgbuffer, format, datalocale, va);

	Log(loglevel, std::string(msgbuffer));
}

ECLogger_Syslog::ECLogger_Syslog(unsigned int max_ll, const char *ident, int facility) : ECLogger(max_ll) {
	/*
	 * Because @ident can go away, and openlog(3) does not make a copy for
	 * itself >:-((, we have to do it.
	 */
	if (ident == NULL) {
		m_ident = NULL;
		openlog(ident, LOG_PID, facility);
	} else {
		m_ident = strdup(ident);
		openlog(m_ident, LOG_PID, facility);
	}
	levelmap[EC_LOGLEVEL_NONE] = LOG_DEBUG;
	levelmap[EC_LOGLEVEL_CRIT] = LOG_CRIT;
	levelmap[EC_LOGLEVEL_ERROR] = LOG_ERR;
	levelmap[EC_LOGLEVEL_WARNING] = LOG_WARNING;
	levelmap[EC_LOGLEVEL_NOTICE] = LOG_NOTICE;
	levelmap[EC_LOGLEVEL_INFO] = LOG_INFO;
	levelmap[EC_LOGLEVEL_DEBUG] = LOG_DEBUG;
	levelmap[EC_LOGLEVEL_ALWAYS] = LOG_ALERT;
}

ECLogger_Syslog::~ECLogger_Syslog() {
	closelog();
	free(m_ident);
}

void ECLogger_Syslog::Reset() {
	// not needed.
}

void ECLogger_Syslog::Log(unsigned int loglevel, const string &message) {
	if (!ECLogger::Log(loglevel))
		return;

	syslog(levelmap[loglevel & EC_LOGLEVEL_MASK], "%s", message.c_str());
}

void ECLogger_Syslog::Log(unsigned int loglevel, const char *format, ...) {
	va_list va;

	if (!ECLogger::Log(loglevel))
		return;

	va_start(va, format);
	LogVA(loglevel, format, va);
	va_end(va);
}

void ECLogger_Syslog::LogVA(unsigned int loglevel, const char *format, va_list& va) {
#if HAVE_VSYSLOG
	vsyslog(levelmap[loglevel & EC_LOGLEVEL_MASK], format, va);
#else
	char msgbuffer[_LOG_BUFSIZE];
	_vsnprintf_l(msgbuffer, sizeof msgbuffer, format, datalocale, va);
	syslog(levelmap[loglevel & EC_LOGLEVEL_MASK], "%s", msgbuffer);
#endif
}

ECLogger_Tee::ECLogger_Tee(): ECLogger(EC_LOGLEVEL_DEBUG) {
}

/**
 * The destructor calls Release on each attached logger so
 * they'll be deleted if it was the last reference.
 */
ECLogger_Tee::~ECLogger_Tee(void)
{
	for (auto log : m_loggers)
		log->Release();
}

/**
 * Reset all loggers attached to this logger.
 */
void ECLogger_Tee::Reset(void)
{
	for (auto log : m_loggers)
		log->Reset();
}

/**
 * Check if anything would be logged with the requested loglevel.
 * Effectively this call is delegated to all attached loggers until
 * one logger is found that returns true.
 *
 * @param[in]	loglevel	The loglevel to test.
 *
 * @retval	true when at least one of the attached loggers would produce output
 */
bool ECLogger_Tee::Log(unsigned int loglevel)
{
	bool bResult = false;

	for (auto log : m_loggers)
		bResult = log->Log(loglevel);

	return bResult;
}

/**
 * Log a message at the reuiqred loglevel to all attached loggers.
 *
 * @param[in]	loglevel	The requierd loglevel
 * @param[in]	message		The message to log
 */
void ECLogger_Tee::Log(unsigned int loglevel, const std::string &message)
{
	for (auto log : m_loggers)
		log->Log(loglevel, message);
}

/**
 * Log a formatted message (printf style) to all attached loggers.
 *
 * @param[in]	loglevel	The required loglevel
 * @param[in]	format		The format string.
 */
void ECLogger_Tee::Log(unsigned int loglevel, const char *format, ...) {
	va_list va;

	va_start(va, format);
	LogVA(loglevel, format, va);
	va_end(va);
}

void ECLogger_Tee::LogVA(unsigned int loglevel, const char *format, va_list &va)
{
	char msgbuffer[_LOG_BUFSIZE];
	_vsnprintf_l(msgbuffer, sizeof msgbuffer, format, datalocale, va);

	for (auto log : m_loggers)
		log->Log(loglevel, std::string(msgbuffer));
}

/**
 * Add a logger to the list of loggers to log to.
 * @note The passed loggers reference will be increased, so
 *       make sure to release the logger in the caller function
 *       if it's going to be 'forgotten' there.
 *
 * @param[in]	lpLogger	The logger to attach.
 */
void ECLogger_Tee::AddLogger(ECLogger *lpLogger) {
	if (lpLogger) {
		lpLogger->AddRef();
		m_loggers.push_back(lpLogger);
	}
}

ECLogger_Pipe::ECLogger_Pipe(int fd, pid_t childpid, int loglevel) : ECLogger(loglevel) {
	m_fd = fd;
	m_childpid = childpid;
}

ECLogger_Pipe::~ECLogger_Pipe() {
	close(m_fd);						// this will make the log child exit

	if (m_childpid)
		waitpid(m_childpid, NULL, 0);	// wait for the child if we're the one that forked it
}

void ECLogger_Pipe::Reset() {
	// send the log process HUP signal again
	kill(m_childpid, SIGHUP);
}

void ECLogger_Pipe::Log(unsigned int loglevel, const std::string &message) {
	int len = 0;
	int off = 0;

	char msgbuffer[_LOG_BUFSIZE];
	msgbuffer[0] = loglevel;
	off += 1;

	if (prefix == LP_TID)
		len = snprintf(msgbuffer + off, sizeof msgbuffer - off, "[0x%08x] ", (unsigned int)pthread_self());
	else if (prefix == LP_PID)
		len = snprintf(msgbuffer + off, sizeof msgbuffer - off, "[%5d] ", getpid());

	if (len < 0)
		len = 0;
	else
		off += len;

	len = min((int)message.length(), (int)sizeof msgbuffer - (off + 1));
	if (len < 0)
		len = 0;
	else {
		memcpy(msgbuffer+off, message.c_str(), len);
		off += len;
	}

	msgbuffer[off] = '\0';
	++off;

	/*
	 * Write as one block to get it to the real logger.
	 * (Atomicity actually only guaranteed up to PIPE_BUF number of bytes.)
	 */
	write(m_fd, msgbuffer, off);
}

void ECLogger_Pipe::Log(unsigned int loglevel, const char *format, ...) {
	va_list va;

	va_start(va, format);
	LogVA(loglevel, format, va);
	va_end(va);
}

void ECLogger_Pipe::LogVA(unsigned int loglevel, const char *format, va_list& va) {
	int len = 0;
	int off = 0;

	char msgbuffer[_LOG_BUFSIZE];
	msgbuffer[0] = loglevel;
	off += 1;

	if (prefix == LP_TID)
		len = snprintf(msgbuffer + off, sizeof msgbuffer - off, "[0x%08x] ", (unsigned int)pthread_self());
	else if (prefix == LP_PID)
		len = snprintf(msgbuffer + off, sizeof msgbuffer - off, "[%5d] ", getpid());

	if (len < 0)
		len = 0;
	else
		off += len;

	// return value is what WOULD have been written if enough space were available in the buffer
	len = _vsnprintf_l(msgbuffer + off, sizeof msgbuffer - off - 1, format, datalocale, va);
	// -1 can be returned on formatting error (eg. %ls in C locale)
	if (len < 0)
		len = 0;

	len = min(len, (int)sizeof msgbuffer - off - 2); // yes, -2, otherwise we could have 2 \0 at the end of the buffer
	off += len;

	msgbuffer[off] = '\0';
	++off;

	/*
	 * Write as one block to get it to the real logger.
	 * (Atomicity actually only guaranteed up to PIPE_BUF number of bytes.)
	 */
	write(m_fd, msgbuffer, off);
}

/**
 * Make sure we do not close the log process when this object is cleaned.
 */
void ECLogger_Pipe::Disown()
{
	m_childpid = 0;
}

namespace PrivatePipe {
	ECLogger_File *m_lpFileLogger;
	ECConfig *m_lpConfig;
	pthread_t signal_thread;
	sigset_t signal_mask;
	static void sighup(int)
	{
		if (m_lpConfig) {
			const char *ll;
			m_lpConfig->ReloadSettings();
			ll = m_lpConfig->GetSetting("log_level");
			if (ll)
				m_lpFileLogger->SetLoglevel(atoi(ll));
		}

		m_lpFileLogger->Reset();
		m_lpFileLogger->Log(EC_LOGLEVEL_INFO, "[%5d] Log process received sighup", getpid());
	}
	static int PipePassLoop(int readfd, ECLogger_File *lpFileLogger,
	    ECConfig *lpConfig)
	{
		ssize_t ret;
		char buffer[_LOG_BUFSIZE] = {0};
		std::string complete;
		const char *p = NULL;
		int s;
		int l;

		m_lpConfig = lpConfig;
		m_lpFileLogger = lpFileLogger;
		m_lpFileLogger->AddRef();

		struct sigaction act;
		act.sa_handler = sighup;
		act.sa_flags = SA_RESTART | SA_ONSTACK;
		sigaction(SIGHUP, &act, nullptr);
		signal(SIGPIPE, SIG_IGN);
		// ignore stop signals to keep logging until the very end
		signal(SIGTERM, SIG_IGN);
		signal(SIGINT, SIG_IGN);
		// close signals we don't want to see from the main program anymore
		signal(SIGCHLD, SIG_IGN);
		signal(SIGUSR1, SIG_IGN);
		signal(SIGUSR2, SIG_IGN);

		// We want the prefix of each individual thread/fork, so don't add that of the Pipe version.
		m_lpFileLogger->SetLogprefix(LP_NONE);

		struct pollfd fds[1] = { { readfd, POLLIN, 0 } };

		while(true) {
			// blocking wait, returns on error or data waiting to log
			fds[0].revents = 0;
			ret = poll(fds, 1, -1);
			if (ret == -1) {
				if (errno == EINTR)
					continue;	// logger received SIGHUP, which wakes the select

				break;
			}
			if (ret == 0)
				continue;

			complete.clear();

			do {
				// if we don't read anything from the fd, it was the end
				ret = read(readfd, buffer, sizeof buffer);
				if (ret <= 0)
					break;
				complete.append(buffer,ret);
			} while (ret == sizeof buffer);

			if (ret <= 0)
				break;

			p = complete.data();
			ret = complete.size();
			while (ret && p) {
				// first char in buffer is loglevel
				l = *p++;
				--ret;
				s = strlen(p);	// find string in read buffer
				if (s) {
					lpFileLogger->Log(l, string(p, s));
					++s;		// add \0
					p += s;		// skip string
					ret -= s;
				} else {
					p = NULL;
				}
			}
		}
		// we need to stop fetching signals
		kill(getpid(), SIGPIPE);
		m_lpFileLogger->Log(EC_LOGLEVEL_INFO, "[%5d] Log process is done", getpid());
		m_lpFileLogger->Release();
		return ret;
	}
}

/**
 * Starts a new process when needed for forked model programs. Only
 * actually replaces your ECLogger object if it's logging to a file.
 *
 * @param[in]	lpConfig	Pointer to your ECConfig object. Cannot be NULL.
 * @param[in]	lpLogger	Pointer to your current ECLogger object.
 * @return		ECLogger	Returns the same or new ECLogger object to use in your program.
 */
ECLogger* StartLoggerProcess(ECConfig* lpConfig, ECLogger* lpLogger) {
	ECLogger_File *lpFileLogger = dynamic_cast<ECLogger_File*>(lpLogger);
	ECLogger_Pipe *lpPipeLogger = NULL;
	int filefd;
	int pipefds[2];
	pid_t child = 0;

	if (lpFileLogger == NULL)
		return lpLogger;

	filefd = lpFileLogger->GetFileDescriptor();

	child = pipe(pipefds);
	if (child < 0)
		return NULL;

	child = fork();
	if (child < 0)
		return NULL;

	if (child == 0) {
		// close all files except the read pipe and the logfile
		int t = getdtablesize();
		for (int i = 3; i < t; ++i) {
			if (i == pipefds[0] || i == filefd) continue;
			close(i);
		}
		PrivatePipe::PipePassLoop(pipefds[0], lpFileLogger, lpConfig);
		close(pipefds[0]);
		delete lpFileLogger;
		delete lpConfig;
		_exit(0);
	}

	// this is the main fork, which doesn't log anything anymore, except through the pipe version.
	// we should release the lpFileLogger
	unsigned int refs = lpLogger->Release();
	if (refs > 0)
		/*
		 * The object must have had a refcount of 1 (the caller);
		 * if not, this means that some other class is carrying a
		 * pointer. This is not critical but is ill-favored.
		 */
		lpLogger->Log(EC_LOGLEVEL_WARNING, "StartLoggerProcess called with large refcount %u", refs + 1);

	close(pipefds[0]);
	lpPipeLogger = new ECLogger_Pipe(pipefds[1], child, atoi(lpConfig->GetSetting("log_level"))); // let destructor wait on child
	lpPipeLogger->SetLogprefix(LP_PID);
	lpPipeLogger->Log(EC_LOGLEVEL_INFO, "Logger process started on pid %d", child);

	return lpPipeLogger;
}

/**
 * Create ECLogger object from configuration.
 *
 * @param[in] lpConfig ECConfig object with config settings from config file. Must have all log_* options.
 * @param argv0 name of the logger
 * @param lpszServiceName service name for windows event logger
 * @param bAudit prepend "audit_" before log settings to create an audit logger (kopano-server)
 *
 * @return Log object, or NULL on error
 */
ECLogger* CreateLogger(ECConfig *lpConfig, const char *argv0,
    const char *lpszServiceName, bool bAudit)
{
	ECLogger *lpLogger = NULL;
	string prepend;
	int loglevel = 0;
	int syslog_facility = LOG_MAIL;

	if (bAudit) {
#if 1 /* change to ifdef HAVE_LOG_AUTHPRIV */
		if (!parseBool(lpConfig->GetSetting("audit_log_enabled")))
			return NULL;
		prepend = "audit_";
		syslog_facility = LOG_AUTHPRIV;
#else
		return NULL;    // No auditing in Windows, apparently.
#endif
	}

	loglevel = strtol(lpConfig->GetSetting((prepend+"log_level").c_str()), NULL, 0);

	if (strcasecmp(lpConfig->GetSetting((prepend+"log_method").c_str()), "syslog") == 0) {
		char *argzero = strdup(argv0);
		lpLogger = new ECLogger_Syslog(loglevel, basename(argzero), syslog_facility);
		free(argzero);
	} else if (strcasecmp(lpConfig->GetSetting((prepend+"log_method").c_str()), "eventlog") == 0) {
		fprintf(stderr, "eventlog logging is only available on windows.\n");
	} else if (strcasecmp(lpConfig->GetSetting((prepend+"log_method").c_str()), "file") == 0) {
		int ret = 0;
		const struct passwd *pw = NULL;
		const struct group *gr = NULL;
		if (strcmp(lpConfig->GetSetting((prepend+"log_file").c_str()), "-")) {
			if (lpConfig->GetSetting("run_as_user") && strcmp(lpConfig->GetSetting("run_as_user"),""))
				pw = getpwnam(lpConfig->GetSetting("run_as_user"));
			else
				pw = getpwuid(getuid());
			if (lpConfig->GetSetting("run_as_group") && strcmp(lpConfig->GetSetting("run_as_group"),""))
				gr = getgrnam(lpConfig->GetSetting("run_as_group"));
			else
				gr = getgrgid(getgid());

			// see if we can open the file as the user we're supposed to run as
			if (pw || gr) {
				ret = fork();
				if (ret == 0) {
					// client test program
					setgroups(0, NULL);
					if (gr)
						setgid(gr->gr_gid);
					if (pw)
						setuid(pw->pw_uid);
					FILE *test = fopen(lpConfig->GetSetting((prepend+"log_file").c_str()), "a");
					if (!test) {
						fprintf(stderr, "Unable to open logfile '%s' as user '%s'\n",
								lpConfig->GetSetting((prepend+"log_file").c_str()), pw ? pw->pw_name : "???");
						_exit(1);
					}
					else {
						fclose(test);
					}
					// free known alloced memory in parent before exiting, keep valgrind from complaining
					delete lpConfig;
					_exit(0);
				}
				if (ret > 0) {	// correct parent, (fork != -1)
					wait(&ret);
					ret = WEXITSTATUS(ret);
				}
			}
		}
		if (ret == 0) {
			bool logtimestamp = parseBool(lpConfig->GetSetting((prepend + "log_timestamp").c_str()));

			size_t log_buffer_size = 0;
			const char *log_buffer_size_str = lpConfig->GetSetting("log_buffer_size");
			if (log_buffer_size_str)
				log_buffer_size = strtoul(log_buffer_size_str, NULL, 0);
			ECLogger_File *log = new ECLogger_File(loglevel, logtimestamp, lpConfig->GetSetting((prepend + "log_file").c_str()), false);
			log->reinit_buffer(log_buffer_size);
			lpLogger = log;
			// chown file
			if (pw || gr) {
				uid_t uid = -1;
				gid_t gid = -1;
				if (pw)
					uid = pw->pw_uid;
				if (gr)
					gid = gr->gr_gid;
				chown(lpConfig->GetSetting((prepend+"log_file").c_str()), uid, gid);
			}
		} else {
			fprintf(stderr, "Not enough permissions to append logfile '%s'. Reverting to stderr.\n", lpConfig->GetSetting((prepend+"log_file").c_str()));
			bool logtimestamp = parseBool(lpConfig->GetSetting((prepend + "log_timestamp").c_str()));
			lpLogger = new ECLogger_File(loglevel, logtimestamp, "-", false);
		}
	}

	if (!lpLogger) {
		fprintf(stderr, "Incorrect logging method selected. Reverting to stderr.\n");
		bool logtimestamp = parseBool(lpConfig->GetSetting((prepend + "log_timestamp").c_str()));
		lpLogger = new ECLogger_File(loglevel, logtimestamp, "-", false);
	}

	return lpLogger;
}

int DeleteLogger(ECLogger *lpLogger) {
	if (lpLogger)
		lpLogger->Release();
	return 0;
}

void LogConfigErrors(ECConfig *lpConfig)
{
	if (lpConfig == NULL)
		return;
	for (const auto &i : *lpConfig->GetWarnings())
		ec_log_warn("Config warning: " + i);
	for (const auto &i : *lpConfig->GetErrors())
		ec_log_crit("Config error: " + i);
}

void generic_sigsegv_handler(ECLogger *lpLogger, const char *app_name,
    const char *version_string, int signr, const siginfo_t *si, const void *uc)
{
	ECLogger_Syslog localLogger(EC_LOGLEVEL_DEBUG, app_name, LOG_MAIL);

	if (lpLogger == NULL)
		lpLogger = &localLogger;

	lpLogger->Log(EC_LOGLEVEL_FATAL, "----------------------------------------------------------------------");
	lpLogger->Log(EC_LOGLEVEL_FATAL, "Fatal error detected. Please report all following information.");
	lpLogger->Log(EC_LOGLEVEL_FATAL, "Application %s version: %s", app_name, version_string);

	struct utsname buf;
	if (uname(&buf) == -1)
		lpLogger->Log(EC_LOGLEVEL_FATAL, "uname() failed: %s", strerror(errno));
	else
		lpLogger->Log(EC_LOGLEVEL_FATAL, "OS: %s, release: %s, version: %s, hardware: %s", buf.sysname, buf.release, buf.version, buf.machine);

#ifdef HAVE_PTHREAD_GETNAME_NP
        char name[32] = { 0 };
        int rc = pthread_getname_np(pthread_self(), name, sizeof name);
	if (rc)
		lpLogger->Log(EC_LOGLEVEL_FATAL, "pthread_getname_np failed: %s", strerror(rc));
	else
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Thread name: %s", name);
#endif

	struct rusage rusage;
	if (getrusage(RUSAGE_SELF, &rusage) == -1)
		lpLogger->Log(EC_LOGLEVEL_FATAL, "getrusage() failed: %s", strerror(errno));
	else
		lpLogger->Log(EC_LOGLEVEL_FATAL, "Peak RSS: %ld", rusage.ru_maxrss);
        
	switch (signr) {
		case SIGSEGV:
			lpLogger->Log(EC_LOGLEVEL_FATAL, "Pid %d caught SIGSEGV (%d), traceback:", getpid(), signr);
			break;
		case SIGBUS:
			lpLogger->Log(EC_LOGLEVEL_FATAL, "Pid %d caught SIGBUS (%d), possible invalid mapped memory access, traceback:", getpid(), signr);
			break;
		case SIGABRT:
			lpLogger->Log(EC_LOGLEVEL_FATAL, "Pid %d caught SIGABRT (%d), out of memory or unhandled exception, traceback:", getpid(), signr);
			break;
	}

	ec_log_bt(EC_LOGLEVEL_CRIT, "Backtrace:");
	ec_log_crit("Signal errno: %s, signal code: %d", strerror(si->si_errno), si->si_code);
	ec_log_crit("Sender pid: %d, sender uid: %d, si_status: %d", si->si_pid, si->si_uid, si->si_status);
	ec_log_crit("User time: %ld, system time: %ld, signal value: %d", si->si_utime, si->si_stime, si->si_value.sival_int);
	ec_log_crit("Faulting address: %p, affected fd: %d", si->si_addr, si->si_fd);
	lpLogger->Log(EC_LOGLEVEL_FATAL, "When reporting this traceback, please include Linux distribution name (and version), system architecture and Kopano version.");
	/* Reset to DFL to avoid recursion */
	if (signal(signr, SIG_DFL) < 0)
		ec_log_warn("Cannot reset signal handler: %s", strerror(errno));
	else if (kill(getpid(), signr) <= 0)
		ec_log_warn("Killing self with signal had no effect. Error code: %s", strerror(errno));
	ec_log_warn("Regular exit without coredump.");
	_exit(1);
}

/**
 * The expectation here is that ec_log_set is only called when the program is
 * single-threaded, i.e. during startup or at shutdown. As a consequence, all
 * of this is written without locks and without shared_ptr.
 *
 * This function gets called from destructors, so you must not invoke
 * ec_log_target->anyfunction at this point any more.
 */
void ec_log_set(ECLogger *logger)
{
	if (logger == NULL)
		logger = &ec_log_fallback_target;
	ec_log_target = logger;
}

ECLogger *ec_log_get(void)
{
	return ec_log_target;
}

bool ec_log_has_target(void)
{
	return ec_log_target != &ec_log_fallback_target;
}

void ec_log(unsigned int level, const char *fmt, ...)
{
	if (!ec_log_target->Log(level))
		return;
	va_list argp;
	va_start(argp, fmt);
	ec_log_target->LogVA(level, fmt, argp);
	va_end(argp);
}

void ec_log(unsigned int level, const std::string &msg)
{
	ec_log_target->Log(level, msg);
}

void ec_log_bt(unsigned int level, const char *fmt, ...)
{
	if (!ec_log_target->Log(level))
		return;
	va_list argp;
	va_start(argp, fmt);
	ec_log_target->LogVA(level, fmt, argp);
	va_end(argp);

	static bool notified = false;
	std::vector<std::string> bt = get_backtrace();
	if (!bt.empty()) {
		for (size_t i = 0; i < bt.size(); ++i)
			ec_log(level, "#%zu. %s", i, bt[i].c_str());
	} else if (!notified) {
		ec_log_info("Backtrace not available");
		notified = true;
	}
}
