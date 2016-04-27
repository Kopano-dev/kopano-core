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

#ifndef TRACE_H
#define TRACE_H

#define TRACE_ENTRY 1
#define TRACE_RETURN 2
#define TRACE_WARNING 3
#define TRACE_INFO 4

void TraceMapi(int time, const char *func, const char *format, ...);
void TraceMapiLib(int time, const char *func, const char *format, ...);
void TraceNotify(int time, const char *func, const char *format, ...);
void TraceSoap(int time, const char *func, const char *format, ...);
void TraceInternals(int time, const char *action, const char *func, const char *format, ...);
void TraceStream(int time, const char *func, const char *format, ...);
void TraceECMapi(int time, const char *func, const char *format, ...);
void TraceExt(int time, const char *func, const char *format, ...);
void TraceRelease(const char *format, ...);

#define TRACE_RELEASE	TraceRelease

#if !defined(WITH_TRACING) && defined(DEBUG)
# define WITH_TRACING
#endif

#ifdef WITH_TRACING
#define TRACE_MAPI		TraceMapi
#define TRACE_MAPILIB	TraceMapiLib
#define TRACE_ECMAPI	TraceECMapi
#define TRACE_NOTIFY	TraceNotify
#define TRACE_INTERNAL	TraceInternals
#define TRACE_SOAP		TraceSoap
#define TRACE_STREAM	TraceStream
#define TRACE_EXT		TraceExt
#else
# ifdef LINUX
#  define TRACE_MAPI(...)
#  define TRACE_MAPILIB(...)
#  define TRACE_ECMAPI(...)
#  define TRACE_NOTIFY(...)
#  define TRACE_INTERNAL(...)
#  define TRACE_SOAP(...)
#  define TRACE_STREAM(...)
#  define TRACE_EXT(...)
# else
#  define TRACE_MAPI		__noop
#  define TRACE_MAPILIB		__noop
#  define TRACE_ECMAPI		__noop
#  define TRACE_NOTIFY		__noop
#  define TRACE_INTERNAL	__noop
#  define TRACE_SOAP		__noop
#  define TRACE_STREAM		__noop
#  define TRACE_EXT			__noop
# endif
#endif

#endif // TRACE_H
