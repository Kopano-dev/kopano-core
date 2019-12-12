/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
// -*- Mode: c++ -*-
#ifndef ECLOGGER_H
#define ECLOGGER_H

#include <atomic>
#include <clocale>
#include <cstdarg>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#ifndef KC_LIKE_PRINTF
#	define KC_LIKE_PRINTF(_fmt, _va)
#endif

namespace KC {

class ECConfig;

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
static const unsigned int EC_LOGLEVEL_SYNC         = 0x00800000;

static const unsigned int EC_LOGLEVEL_EXTENDED_MASK = 0xFFFF0000;

#define EC_LOG_BUFSIZE 10240

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
class KC_EXPORT ECLogger {
	private:
	std::atomic<unsigned> m_ulRef{1};

	protected:
	/**
	 * Returns string with timestamp in current locale.
	 */
	KC_HIDDEN size_t MakeTimestamp(char *, size_t);

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
	KC_HIDDEN virtual int GetFileDescriptor() { return -1; }

	/**
	 * Log a message on a specified loglevel using std::string
	 *
	 * @param	loglevel	Loglevel to log message under
	 * @param	message		std::string logmessage. Expected charset is current locale.
	 */
	KC_HIDDEN virtual void log(unsigned int level, const char *msg) = 0;
	inline void Log(unsigned int level, const std::string &msg) { return log(level, msg.c_str()); }

	/**
	 * Log a message on a specified loglevel using char* format
	 *
	 * @param	loglevel	Loglevel to log message under
	 * @param	format		formatted string for the parameter list
	 */
	KC_HIDDEN virtual void logf(unsigned int level, const char *fmt, ...) KC_LIKE_PRINTF(3, 4) = 0;
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
	KC_HIDDEN virtual void logv(unsigned int level, const char *fmt, va_list &) = 0;

	logprefix prefix;
};

/**
 * Dummy null logger, drops every log message.
 */
class KC_EXPORT ECLogger_Null KC_FINAL : public ECLogger {
	public:
	ECLogger_Null(void);
	KC_HIDDEN virtual void Reset() KC_OVERRIDE;
	KC_HIDDEN virtual void log(unsigned int level, const char *msg) KC_OVERRIDE;
	KC_HIDDEN virtual void logf(unsigned int level, const char *fmt, ...) KC_OVERRIDE KC_LIKE_PRINTF(3, 4);
	KC_HIDDEN virtual void logv(unsigned int level, const char *fmt, va_list &) KC_OVERRIDE;
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
	char prevmsg[EC_LOG_BUFSIZE];
	int prevcount;
	unsigned int prevloglevel;
	KC_HIDDEN bool DupFilter(unsigned int level, const char *);
	KC_HIDDEN char *DoPrefix(char *, size_t);

	public:
	ECLogger_File(unsigned int max_ll, bool add_timestamp, const char *filename, bool compress);
	~ECLogger_File(void);
	KC_HIDDEN void reinit_buffer(size_t size);
	KC_HIDDEN virtual void Reset() KC_OVERRIDE;
	KC_HIDDEN virtual void log(unsigned int level, const char *msg) KC_OVERRIDE;
	KC_HIDDEN virtual void logf(unsigned int level, const char *fmt, ...) KC_OVERRIDE KC_LIKE_PRINTF(3, 4);
	KC_HIDDEN virtual void logv(unsigned int level, const char *fmt, va_list &) KC_OVERRIDE;
	KC_HIDDEN int GetFileDescriptor() KC_OVERRIDE;
	bool IsStdErr() const { return logname == "-"; }

	private:
	KC_HIDDEN void init_for_stderr();
	KC_HIDDEN void init_for_file();
	KC_HIDDEN void init_for_gzfile();
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
	KC_HIDDEN virtual void Reset() KC_OVERRIDE;
	KC_HIDDEN virtual void log(unsigned int level, const char *msg) KC_OVERRIDE;
	KC_HIDDEN virtual void logf(unsigned int level, const char *fmt, ...) KC_OVERRIDE KC_LIKE_PRINTF(3, 4);
	KC_HIDDEN virtual void logv(unsigned int level, const char *fmt, va_list &) KC_OVERRIDE;
	KC_HIDDEN int GetFileDescriptor() KC_OVERRIDE { return m_fd; }
	void Disown();
};

extern KC_EXPORT std::shared_ptr<ECLogger> StartLoggerProcess(ECConfig *, std::shared_ptr<ECLogger> &&file_logger);

/**
 * This class can be used if log messages need to go to
 * multiple destinations. It basically distributes
 * the messages to one or more attached ECLogger objects.
 *
 * Each attached logger can have its own loglevel.
 */
class KC_EXPORT ECLogger_Tee KC_FINAL : public ECLogger {
	private:
	std::list<std::shared_ptr<ECLogger>> m_loggers;

	public:
	ECLogger_Tee();
	KC_HIDDEN virtual void Reset() KC_OVERRIDE;
	KC_HIDDEN virtual bool Log(unsigned int level) KC_OVERRIDE;
	KC_HIDDEN virtual void log(unsigned int level, const char *msg) KC_OVERRIDE;
	KC_HIDDEN virtual void logf(unsigned int level, const char *fmt, ...) KC_OVERRIDE KC_LIKE_PRINTF(3, 4);
	KC_HIDDEN virtual void logv(unsigned int level, const char *fmt, va_list &) KC_OVERRIDE;
	void AddLogger(std::shared_ptr<ECLogger>);
};

extern KC_EXPORT ECLogger *ec_log_get();
extern KC_EXPORT void ec_log_set(std::shared_ptr<ECLogger>);
extern KC_EXPORT void ec_log(unsigned int level, const char *msg, ...) KC_LIKE_PRINTF(2, 3);
extern KC_EXPORT void ec_log(unsigned int level, const std::string &msg);

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

extern KC_EXPORT HRESULT ec_log_hrcode(HRESULT, unsigned int level, const char *fmt, const char *func);
extern KC_EXPORT std::shared_ptr<ECLogger> CreateLogger(ECConfig *, const char *argv0, const char *service, bool audit = false);
extern KC_EXPORT void LogConfigErrors(ECConfig *);
extern KC_EXPORT void ec_setup_segv_handler(const char *app, const char *vers);
extern KC_EXPORT const std::string &ec_os_pretty_name();

} /* namespace */

#endif /* ECLOGGER_H */
