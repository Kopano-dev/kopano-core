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

#ifndef SOAPHELPERS_H
#define SOAPHELPERS_H

#include "soapH.h"

namespace KC {

void *mime_file_read_open(struct soap *soap, void *handle, const char *id, const char *type, const char *description);
void mime_file_read_close(struct soap *soap, void *handle);
size_t mime_file_read(struct soap *soap, void *handle, char *buf, size_t len);

void *mime_file_write_open(struct soap *soap, void *handle, const char *id, const char *type, const char *description, enum soap_mime_encoding encoding);
void mime_file_write_close(struct soap *soap, void *handle);
int mime_file_write(struct soap *soap, void *handle, const char *buf, size_t len);

} /* namespace */

#endif
