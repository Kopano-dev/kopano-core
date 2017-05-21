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

#include <kopano/zcdefs.h>

#define TRACE_ENTRY 1
#define TRACE_RETURN 2
#define TRACE_WARNING 3
#define TRACE_INFO 4

namespace KC {

void TraceSoap(int time, const char *func, const char *format, ...);
void TraceInternals(int time, const char *action, const char *func, const char *format, ...);
void TraceStream(int time, const char *func, const char *format, ...);
void TraceExt(int time, const char *func, const char *format, ...);
extern _kc_export void TraceRelease(const char *fmt, ...);

} /* namespace */

#define TRACE_RELEASE	TraceRelease

#if !defined(WITH_TRACING) && defined(DEBUG)
# define WITH_TRACING
#endif

#ifdef WITH_TRACING
#define TRACE_INTERNAL	TraceInternals
#define TRACE_SOAP		TraceSoap
#define TRACE_STREAM	TraceStream
#define TRACE_EXT		TraceExt
#else
#  define TRACE_NOTIFY(...)
#  define TRACE_INTERNAL(...)
#  define TRACE_SOAP(...)
#  define TRACE_STREAM(...)
#  define TRACE_EXT(...)
#endif

#endif // TRACE_H
