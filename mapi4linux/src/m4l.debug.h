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

#endif /* __M4L_DEBUG_H */
