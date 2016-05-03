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

#include <cstdio>
#include <cstdarg>
#include <kopano/Trace.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#ifdef WITH_TRACING
// Turn these on/off
#  define DO_TRACE_MAPI		1
#  define DO_TRACE_NOTIF	1
#  define DO_TRACE_SOAP		0
#  define DO_TRACE_INT		0
#  define DO_TRACE_STREAM	0
#  define DO_TRACE_EXT		0
#else
// Leave these alone .. release builds don't print squat
#  define DO_TRACE_MAPI		0
#  define DO_TRACE_NOTIF	0
#  define DO_TRACE_SOAP		0
#  define DO_TRACE_INT		0
#  define DO_TRACE_STREAM	0
#  define DO_TRACE_EXT		0
#endif

// Turn these on/off
#define FILTER_REF			0	// IUnknown interface
#define FILTER_COMPARE		1	// Compare functions

// Limit the debug results, 0 is endless
#define BUFFER_LIMIT		0 // 8096

static void TraceMsg(const char *lpMsg, int time, const char *func,
    const char *format, va_list va)
{
	const char *lpTraceType = NULL;
	char* buffer = NULL;
	char debug[1024];
	int	 len=0;
	int pos = 0;
	va_list va_lentest;

	if(FILTER_REF) {
		if(	strstr(func,"QueryInterface") != NULL ||
			strstr(func,"AddRef") != NULL ||
			strstr(func,"Release") != NULL )
			return;
	}

	if(FILTER_COMPARE) {
		if( strstr(func,"CompareEntryID") != NULL ||
			strstr(func,"CompareStoreIDs") != NULL)
			return;
	}

	switch(time) {
	case TRACE_ENTRY:
		lpTraceType = "Call";
		break;
	case TRACE_WARNING:
		lpTraceType = "Warning";
		break;
	case TRACE_RETURN:
		lpTraceType = "Ret ";
		break;
	default:
		lpTraceType = "Unknown";
		break;
	}


	pos = _snprintf(debug, sizeof(debug), "%lu %08X %s %s: %s(", GetTickCount(), GetCurrentThreadId(), lpMsg, lpTraceType, func);

	len = pos + 3;

	if (format) {
		va_copy(va_lentest, va);
#ifdef LINUX
		len += _vsnprintf(NULL, 0, format, va_lentest);
#else
		len += _vscprintf(format, va_lentest);
#endif
		va_end(va_lentest);
	}

	if (BUFFER_LIMIT != 0 && pos+3 < BUFFER_LIMIT && len > BUFFER_LIMIT)
		len = BUFFER_LIMIT;

	buffer = (char*)malloc(len);

	memcpy(buffer, debug, pos);

	if (format)
		pos = _vsnprintf(buffer+pos, len-pos, format, va);

	if(pos == -1) {
		/* Indicate that the string is not complete */
		buffer[len-6] = '.';
		buffer[len-5] = '.';
		buffer[len-4] = '.';
	}

	buffer[len-3] = ')';
	buffer[len-2] = '\n';
	buffer[len-1] = 0;

	OutputDebugStringA(buffer);

	free( buffer );

}

void TraceMapi(int time, const char *func, const char *format, ...)
{
	va_list va;

	if(!DO_TRACE_MAPI)
		return;

	va_start(va, format);

	TraceMsg("MAPI", time, func, format, va);

	va_end(va);
}

void TraceMapiLib(int time, const char *func, const char *format, ...)
{
	va_list va;

	if(!DO_TRACE_MAPI)
		return;

	va_start(va, format);

	TraceMsg("MAPILIB", time, func, format, va);

	va_end(va);
}

void TraceNotify(int time, const char *func, const char *format, ...)
{
	va_list va; 

	if(!DO_TRACE_NOTIF)
		return;

	va_start(va, format);

	TraceMsg("NOTIFY", time, func, format, va);

	va_end(va);
}

void TraceSoap(int time, const char *func, const char *format, ...)
{
	va_list va; 

	if(!DO_TRACE_SOAP)
		return;

	va_start(va, format);

	TraceMsg("SOAP", time, func, format, va);

	va_end(va);
}

void TraceInternals(int time, const char *tracetype, const char *func,
    const char *format, ...)
{
	va_list va; 

	if(!DO_TRACE_INT)
		return;

	va_start(va, format);

	TraceMsg("INTERNAL", time, func, format, va);

	va_end(va);
}


void TraceStream(int time, const char *func, const char *format, ...)
{
	va_list va; 

	if(!DO_TRACE_STREAM)
		return;

	va_start(va, format);

	TraceMsg("IStream", time, func, format, va);

	va_end(va);
}

void TraceECMapi(int time, const char *func, const char *format, ...)
{
	va_list va; 

	va_start(va, format);

	TraceMsg("ECMAPI", time, func, format, va);

	va_end(va);
}

void TraceExt(int time, const char *func, const char *format, ...)
{
	va_list va; 

	if(!DO_TRACE_EXT)
		return;

	va_start(va, format);

	TraceMsg("EXT", time, func, format, va);

	va_end(va);
}

void TraceRelease(const char *format, ...)
{
	char debug[1024];
	va_list va; 
	va_start(va, format);

	vsnprintf(debug, sizeof(debug), format, va);
	OutputDebugStringA(debug);

	va_end(va);
}
