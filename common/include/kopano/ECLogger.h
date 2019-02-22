/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
// -*- Mode: c++ -*-
#ifndef ECLOGGER_H
#define ECLOGGER_H

#include <atomic>
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <list>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <kopano/memory.hpp>
#ifndef KC_LIKE_PRINTF
#	define KC_LIKE_PRINTF(_fmt, _va)
#endif

namespace KC {

class ECConfig;
class ECLogger;

static const unsigned int EC_LOGLEVEL_NONE       = 0;
static const unsigned int EC_LOGLEVEL_FATAL	 = 1;
static const unsigned int EC_LOGLEVEL_CRIT	 = 1;
static const unsigned int EC_LOGLEVEL_ERROR	 = 2;
static const unsigned int EC_LOGLEVEL_WARNING    = 3;
static const unsigned int EC_LOGLEVEL_NOTICE	 = 4;
static const unsigned int EC_LOGLEVEL_INFO	 = 5;
static const unsigned int EC_LOGLEVEL_DEBUG	 = 6;
static const unsigned int EC_LOGLEVEL_ALWAYS     = 0xf;
static const unsigned int EC_LOGLEVEL_MASK	 = 0xF;

// extended log options
static const unsigned int EC_LOGLEVEL_SQL	   = 0x00010000;
static const unsigned int EC_LOGLEVEL_PLUGIN	   = 0x00020000;
static const unsigned int EC_LOGLEVEL_CACHE	   = 0x00040000;
static const unsigned int EC_LOGLEVEL_USERCACHE    = 0x00080000;
static const unsigned int EC_LOGLEVEL_SOAP         = 0x00100000;
static const unsigned int EC_LOGLEVEL_ICS          = 0x00200000;
static const unsigned int EC_LOGLEVEL_SEARCH       = 0x00400000;

static const unsigned int EC_LOGLEVEL_EXTENDED_MASK = 0xFFFF0000;

#define _LOG_BUFSIZE		10240

#define ZLOG_DEBUG(plog, ...) \
	do { \
		if ((plog)->Log(EC_LOGLEVEL_DEBUG)) \
			(plog)->logf(EC_LOGLEVEL_DEBUG, __VA_ARGS__); \
	} while (false)
#define ZLOG_AUDIT(plog, ...) \
	do { \
		if ((plog) != NULL) \
			(plog)->logf(EC_LOGLEVEL_FATAL, __VA_ARGS__); \
	} while (false)

#define TSTRING_PRINTF "%ls"

/**
 * Prefixes in log message in different process models.
 *
 * LP_NONE: No prefix in log message (default)
 * LP_TID:  Add thread id as prefix
 * LP_PID:  Add linux process id as prefix
 */
enum logprefix { LP_NONE, LP_TID, LP_PID };

/**
 * ECLogger object logs messages to a specific
 * destination. Destinations are created in derived classes.
 */
class _kc_export ECLogger {
	private:
	std::atomic<unsigned> m_ulRef{1};

	protected:
	/**
	 * Returns string with timestamp in current locale.
	 */
	_kc_hidden size_t MakeTimestamp(char *, size_t);

	unsigned int max_loglevel;
	locale_t timelocale, datalocale;

	/**
	 * Implementations should open the log they're writing to.
	 *
	 * @param[in]	max_ll	Max loglevel allowed to enter in the log. Messages with higher loglevel will be skipped.
	 */
	ECLogger(int max_ll);

	public:
	/**
	 * Implementations should close the log they're writing to.
	 */
	virtual ~ECLogger();
	/**
	 * Query if a message would be logged under this loglevel
	 *
	 * @param[in]	loglevel	Loglevel you want to know if it enters the log.
	 * @return		bool
	 * @retval	true	Logging with 'loglevel' will enter log
	 * @retval	false	Logging with 'loglevel' will be dropped
	 */
	virtual bool Log(unsigned int loglevel);

	/**
	 * Set new loglevel for log object
	 *
	 * @param[in]	max_ll	The new maximum loglevel
	 */
	void SetLoglevel(unsigned int max_ll);
	/**
	 * Set new prefix for log
	 *
	 * @param[in]	lp	New logprefix LP_TID or LP_PID. Disable prefix with LP_NONE.
	 */
	void SetLogprefix(logprefix lp);
	/**
	 * Used for log rotation. Implementations should prepare to log in a new log.
	 *
	 * @param[in]	lp	New logprefix LP_TID or LP_PID. Disable prefix with LP_NONE.
	 */
	virtual void Reset() = 0;
	/**
	 * Used to get a direct file descriptor of the log file. Returns
	 * -1 if not available.
	 *
	 * @return	int		The file descriptor of the logfile being written to.
	 */
	_kc_hidden virtual int GetFileDescriptor(void) { return -1; }

	/**
	 * Log a message on a specified loglevel using std::string
	 *
	 * @param	loglevel	Loglevel to log message under
	 * @param	message		std::string logmessage. Expected charset is current locale.
	 */
	_kc_hidden virtual void log(unsigned int level, const char *msg) = 0;
	inline void Log(unsigned int level, const std::string &msg) { return log(level, msg.c_str()); }

	/**
	 * Log a message on a specified loglevel using char* format
	 *
	 * @param	loglevel	Loglevel to log message under
	 * @param	format		formatted string for the parameter list
	 */
	_kc_hidden virtual void logf(unsigned int level, const char *fmt, ...) KC_LIKE_PRINTF(3, 4) = 0;
	inline void Log(unsigned int level, const char *s) { return log(level, s); }
	HRESULT perr(const char *text, HRESULT);
	HRESULT pwarn(const char *text, HRESULT);

	/**
	 * Log a message on a specified loglevel using char* format
	 *
	 * @param	loglevel	Loglevel to log message under
	 * @param	format		formatted string for the parameter list
	 * @param	va			va_list converted from ... parameters
	 */
	_kc_hidden virtual void logv(unsigned int level, const char *fmt, va_list &) = 0;

	logprefix prefix;
};

/**
 * Dummy null logger, drops every log message.
 */
class _kc_export ECLogger_Null KC_FINAL : public ECLogger {
	public:
	ECLogger_Null(void);
	_kc_hidden virtual void Reset(void) _kc_override;
	_kc_hidden virtual void log(unsigned int level, const char *msg) _kc_override;
	_kc_hidden virtual void logf(unsigned int level, const char *fmt, ...) _kc_override KC_LIKE_PRINTF(3, 4);
	_kc_hidden virtual void logv(unsigned int level, const char *fmt, va_list &) _kc_override;
};

/**
 * File logger. Use "-" for stderr logging. Output is in system locale set in LC_CTYPE.
 */
class KC_EXPORT_DYCAST ECLogger_File KC_FINAL : public ECLogger {
	private:
	typedef void *handle_type;
	typedef handle_type (*open_func)(const char *, const char *);
	typedef int (*close_func)(handle_type);
	typedef int (*printf_func)(handle_type, const char *, ...);
	typedef int (*fileno_func)(handle_type);
	typedef int (*flush_func)(handle_type);

	KC::shared_mutex handle_lock, dupfilter_lock;
	handle_type fh;
	std::string logname;
	bool timestamp;
	size_t buffer_size;
	open_func fnOpen;
	close_func fnClose;
	printf_func fnPrintf;
	fileno_func fnFileno;
	const char *szMode;
	char prevmsg[_LOG_BUFSIZE];
	int prevcount;
	unsigned int prevloglevel;
	_kc_hidden bool DupFilter(unsigned int level, const char *);
	_kc_hidden char *DoPrefix(char *, size_t);

	public:
	ECLogger_File(unsigned int max_ll, bool add_timestamp, const char *filename, bool compress);
	~ECLogger_File(void);
	_kc_hidden void reinit_buffer(size_t size);
	_kc_hidden virtual void Reset(void) _kc_override;
	_kc_hidden virtual void log(unsigned int level, const char *msg) _kc_override;
	_kc_hidden virtual void logf(unsigned int level, const char *fmt, ...) _kc_override KC_LIKE_PRINTF(3, 4);
	_kc_hidden virtual void logv(unsigned int level, const char *fmt, va_list &) _kc_override;
	_kc_hidden int GetFileDescriptor(void) _kc_override;
	bool IsStdErr() const { return logname == "-"; }

	private:
	_kc_hidden void init_for_stderr(void);
	_kc_hidden void init_for_file(void);
	_kc_hidden void init_for_gzfile(void);
};

/**
 * Pipe Logger, only used by forked model processes. Redirects every
 * log message to an ECLogger_File object. This ECLogger_Pipe object
 * can be created by StartLoggerProcess function.
 */
class KC_EXPORT_DYCAST ECLogger_Pipe KC_FINAL : public ECLogger {
	private:
	int m_fd;
	pid_t m_childpid;
	void xwrite(const char *, size_t);

	public:
	ECLogger_Pipe(int fd, pid_t childpid, int loglevel);
	~ECLogger_Pipe(void);
	_kc_hidden virtual void Reset(void) _kc_override;
	_kc_hidden virtual void log(unsigned int level, const char *msg) _kc_override;
	_kc_hidden virtual void logf(unsigned int level, const char *fmt, ...) _kc_override KC_LIKE_PRINTF(3, 4);
	_kc_hidden virtual void logv(unsigned int level, const char *fmt, va_list &) _kc_override;
	_kc_hidden int GetFileDescriptor(void) _kc_override { return m_fd; }
	void Disown();
};

extern _kc_export std::shared_ptr<ECLogger> StartLoggerProcess(ECConfig *, std::shared_ptr<ECLogger> &&file_logger);

/**
 * This class can be used if log messages need to go to
 * multiple destinations. It basically distributes
 * the messages to one or more attached ECLogger objects.
 *
 * Each attached logger can have its own loglevel.
 */
class _kc_export ECLogger_Tee KC_FINAL : public ECLogger {
	private:
	std::list<std::shared_ptr<ECLogger>> m_loggers;

	public:
	ECLogger_Tee();
	_kc_hidden virtual void Reset(void) _kc_override;
	_kc_hidden virtual bool Log(unsigned int level) _kc_override;
	_kc_hidden virtual void log(unsigned int level, const char *msg) _kc_override;
	_kc_hidden virtual void logf(unsigned int level, const char *fmt, ...) _kc_override KC_LIKE_PRINTF(3, 4);
	_kc_hidden virtual void logv(unsigned int level, const char *fmt, va_list &) _kc_override;
	void AddLogger(std::shared_ptr<ECLogger>);
};

extern _kc_export ECLogger *ec_log_get(void);
extern _kc_export void ec_log_set(std::shared_ptr<ECLogger>);
extern _kc_export void ec_log(unsigned int level, const char *msg, ...) KC_LIKE_PRINTF(2, 3);
extern _kc_export void ec_log(unsigned int level, const std::string &msg);

#define ec_log_always(...)  ec_log(EC_LOGLEVEL_ALWAYS, __VA_ARGS__)
#define ec_log_fatal(...)   ec_log(EC_LOGLEVEL_CRIT, __VA_ARGS__)
#define ec_log_crit(...)    ec_log(EC_LOGLEVEL_CRIT, __VA_ARGS__)
#define ec_log_err(...)     ec_log(EC_LOGLEVEL_ERROR, __VA_ARGS__)
#define ec_log_warn(...)    ec_log(EC_LOGLEVEL_WARNING, __VA_ARGS__)
#define ec_log_notice(...)  ec_log(EC_LOGLEVEL_NOTICE, __VA_ARGS__)
#define ec_log_info(...)    ec_log(EC_LOGLEVEL_INFO, __VA_ARGS__)
#define ec_log_debug(...)   ec_log(EC_LOGLEVEL_DEBUG, __VA_ARGS__)
#define kc_perror(s, r)     ec_log_hrcode((r), EC_LOGLEVEL_ERROR, s ": %s (%x)", nullptr)
#define kc_perrorf(s, r)    ec_log_hrcode((r), EC_LOGLEVEL_ERROR, "%s: " s ": %s (%x)", __PRETTY_FUNCTION__)
#define kc_pwarn(s, r)      ec_log_hrcode((r), EC_LOGLEVEL_WARNING, s ": %s (%x)", nullptr)

extern _kc_export HRESULT ec_log_hrcode(HRESULT, unsigned int level, const char *fmt, const char *func);
extern _kc_export ECLogger *CreateLogger(ECConfig *, const char *argv0, const char *service, bool audit = false);
extern _kc_export void LogConfigErrors(ECConfig *);
extern _kc_export void generic_sigsegv_handler(ECLogger *, const char *app, const char *vers, int sig, const siginfo_t *, const void *uctx);

} /* namespace */

#endif /* ECLOGGER_H */
