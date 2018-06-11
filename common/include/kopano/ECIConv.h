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

#ifndef ICONV_H
#define ICONV_H

#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/charset/convert.h>
#include <string>

namespace KC {

class _kc_export ECIConv _kc_final {
public:
    ECIConv(const std::string &strToCharset, const std::string &strFromCharset);
	_kc_hidden bool canConvert(void) const { return m_lpContext != NULL; }
    std::string convert(const std::string &input);

private:
	typedef iconv_context<std::string, std::string> context_t;
	std::unique_ptr<context_t> m_lpContext;
};

} /* namespace */

#endif
