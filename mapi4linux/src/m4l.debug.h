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

#ifndef __M4L_DEBUG_H
#define __M4L_DEBUG_H

#include <string>

#include <kopano/Trace.h>
#include <kopano/ECLogger.h>

extern ECLogger *m4l_lpLogger;

/*
 * We want the debug output for Mapi4Linux to go into 2 directions,
 * first it should go to the console using Trace.h, secondly it needs
 * to go to the m4l_lpLogger so it can be written into a file as well.
 *
 * We are going to override the macros from Trace.h here and make the
 * m4l_lpLogger interaction happening using new versions on the define.
 */

/* Format string for function and message combination */
#define LOG_PREFIX(__type) \
	( ((__type) == TRACE_ENTRY) ? "Call" : \
		(((__type) == TRACE_WARNING) ? "Warning" : \
			((((__type) == TRACE_RETURN) ? "Ret " : \
			"Unknown" ))) )

#define LOG_FORMAT(__type, __func, __msg) \
	( std::string("%lu 0x%lx MAPILIB") + LOG_PREFIX(__type) + ": " + __func + "(" + __msg + ")").c_str(), GetTickCount(), kc_threadid()

/* 
 * For Exchange redirector (WIN32) we allow the log message to be send to a logfile.
 * For Linux debug build we allow the log message to be send to a logfile.
 */
#if (defined(LINUX) && defined(DEBUG))
#ifdef USE_VARIADIC_MACRO
#define TRACE_TO_FILE(__level, __message, __args...) \
	if (m4l_lpLogger) \
		m4l_lpLogger->Log(__level, __message, ##__args);

#define TRACE_TO_FILE1 TRACE_TO_FILE
#define TRACE_TO_FILE2 TRACE_TO_FILE
#define TRACE_TO_FILE3 TRACE_TO_FILE
#define TRACE_TO_FILE4 TRACE_TO_FILE
#define TRACE_TO_FILE5 TRACE_TO_FILE
#else /* USE_VARIADIC_MACRO */
#define TRACE_TO_FILE(__level, __message) \
	if (m4l_lpLogger) \
		m4l_lpLogger->Log(__level, __message);

#define TRACE_TO_FILE1(__level, __message, __arg1) \
	if (m4l_lpLogger) \
		m4l_lpLogger->Log(__level, __message, __arg1);

#define TRACE_TO_FILE2(__level, __message, __arg1, __arg2) \
	if (m4l_lpLogger) \
		m4l_lpLogger->Log(__level, __message, __arg1, __arg2);

#define TRACE_TO_FILE3(__level, __message, __arg1, __arg2, __arg3) \
	if (m4l_lpLogger) \
		m4l_lpLogger->Log(__level, __message, __arg1, __arg2, __arg3);

#define TRACE_TO_FILE4(__level, __message, __arg1, __arg2, __arg3, __arg4) \
	if (m4l_lpLogger) \
		m4l_lpLogger->Log(__level, __message, __arg1, __arg2, __arg3, __arg4);

#define TRACE_TO_FILE5(__level, __message, __arg1, __arg2, __arg3, __arg4, __arg5) \
	if (m4l_lpLogger) \
		m4l_lpLogger->Log(__level, __message, __arg1, __arg2, __arg3, __arg4, __arg5);
#endif /* USE_VARIADIC_MACRO */
#else /* (defined(LINUX) && defined(DEBUG)) */
#define TRACE_TO_FILE(__level, __message) {}
#define TRACE_TO_FILE1(__level, __message, __arg1) {}
#define TRACE_TO_FILE2(__level, __message, __arg1, __arg2) {}
#define TRACE_TO_FILE3(__level, __message, __arg1, __arg2, __arg3) {}
#define TRACE_TO_FILE4(__level, __message, __arg1, __arg2, __arg3, __arg4) {}
#define TRACE_TO_FILE5(__level, __message, __arg1, __arg2, __arg3, __arg4, __arg5) {}
#endif /* (defined(LINUX) && defined(DEBUG)) */

/* 
 * If only we could nicely rename the TRACE_MAPILIB macro and undef it
 * that would have been so cool, but now we are stuck with this solution.
 */
#ifdef DEBUG
#define ORIG_TRACE_MAPILIB TraceMapiLib
#else
  #define ORIG_TRACE_MAPILIB(...)
#endif
#undef TRACE_MAPILIB

#ifdef USE_VARIADIC_MACRO
#define TRACE_MAPILIB(__type, __function, __msg, __args...) \
	do { \
		ORIG_TRACE_MAPILIB(__type, __function, __msg, ##__args);		\
		TRACE_TO_FILE(EC_LOGLEVEL_DEBUG, LOG_FORMAT(__type, __function, __msg), ##__args); \
	} while(0)

#define TRACE_MAPILIB1 TRACE_MAPILIB
#define TRACE_MAPILIB2 TRACE_MAPILIB
#define TRACE_MAPILIB3 TRACE_MAPILIB
#define TRACE_MAPILIB4 TRACE_MAPILIB
#define TRACE_MAPILIB5 TRACE_MAPILIB
#else /* USE_VARIADIC_MACRO */
#define TRACE_MAPILIB(__type, __function, __msg) \
	do { \
		ORIG_TRACE_MAPILIB(__type, __function, __msg); \
		TRACE_TO_FILE(EC_LOGLEVEL_DEBUG, LOG_FORMAT(__type, __function, __msg)); \
	} while (0)

#define TRACE_MAPILIB1(__type, __function, __msg, __arg1) \
	do { \
		ORIG_TRACE_MAPILIB(__type, __function, __msg, __arg1); \
		TRACE_TO_FILE1(EC_LOGLEVEL_DEBUG, LOG_FORMAT(__type, __function, __msg), __arg1); \
	} while (0)

#define TRACE_MAPILIB2(__type, __function, __msg, __arg1, __arg2) \
	do { \
		ORIG_TRACE_MAPILIB(__type, __function, __msg, __arg1, __arg2); \
		TRACE_TO_FILE2(EC_LOGLEVEL_DEBUG, LOG_FORMAT(__type, __function, __msg), __arg1, __arg2); \
	} while (0)

#define TRACE_MAPILIB3(__type, __function, __msg, __arg1, __arg2, __arg3) \
	do { \
		ORIG_TRACE_MAPILIB(__type, __function, __msg, __arg1, __arg2, __arg3); \
		TRACE_TO_FILE3(EC_LOGLEVEL_DEBUG, LOG_FORMAT(__type, __function, __msg), __arg1, __arg2, __arg3); \
	} while(0)

#define TRACE_MAPILIB4(__type, __function, __msg, __arg1, __arg2, __arg3, __arg4) \
	do { \
		ORIG_TRACE_MAPILIB(__type, __function, __msg, __arg1, __arg2, __arg3, __arg4); \
		TRACE_TO_FILE4(EC_LOGLEVEL_DEBUG, LOG_FORMAT(__type, __function, __msg), __arg1, __arg2, __arg3, __arg4); \
	} while(0)

#define TRACE_MAPILIB5(__type, __function, __msg, __arg1, __arg2, __arg3, __arg4, __arg5) \
	do { \
		ORIG_TRACE_MAPILIB(__type, __function, __msg, __arg1, __arg2, __arg3, __arg4, __arg5); \
		TRACE_TO_FILE5(EC_LOGLEVEL_DEBUG, LOG_FORMAT(__type, __function, __msg), __arg1, __arg2, __arg3, __arg4, __arg5); \
	} while(0)

#endif /* USE_VARIADIC_MACRO */

#endif /* __M4L_DEBUG_H */
