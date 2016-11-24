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
#include <string>
#include <openssl/md5.h>
#include <kopano/md5.h>

namespace KC {

std::string zcp_md5_final_hex(MD5_CTX *ctx)
{
	static const char hex[] = "0123456789abcdef";
	unsigned char md[MD5_DIGEST_LENGTH];
	std::string s;
	s.reserve(2 * sizeof(md));

	MD5_Final(md, ctx);
	for (size_t z = 0; z < sizeof(md); ++z) {
		s.push_back(hex[(md[z] & 0xF0) >> 4]);
		s.push_back(hex[md[z] & 0xF]);
	}
	return s;
}

} /* namespace */
