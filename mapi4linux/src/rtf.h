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

#ifndef RTF_H
#define RTF_H

extern unsigned int rtf_get_uncompressed_length(const char *data, unsigned int size);
extern unsigned int rtf_decompress(char *dst, const char *src, unsigned int src_size);
extern unsigned int rtf_compress(char **dst, unsigned int *dst_size, const char *src, unsigned int src_size);

#endif // RTF_H
