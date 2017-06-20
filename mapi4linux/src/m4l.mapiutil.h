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

#ifndef __M4L_UTIL_IMPL_H
#define __M4L_UTIL_IMPL_H

#include <mapiutil.h>

/*
 * Non mapi32 utility function (only used internally withint M4L)
 */
HRESULT GetConnectionProperties(LPSPropValue lpServer, LPSPropValue lpUsername, ULONG *lpcValues, LPSPropValue *lppProps);

#endif
