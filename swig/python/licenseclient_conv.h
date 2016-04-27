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

#ifndef licenseclient_conv_INCLUDED
#define licenseclient_conv_INCLUDED

#include <vector>
#include <string>
#include <Python.h>

PyObject *		List_from_StringVector(const std::vector<std::string> &v);
int				List_to_StringVector(PyObject *object, std::vector<std::string> &v);

#endif // ndef licenseclient_conv_INCLUDED
