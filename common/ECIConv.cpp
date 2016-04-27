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
#include <kopano/ECIConv.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static const char THIS_FILE[] = __FILE__;
#endif

ECIConv::ECIConv(const std::string &strToCharset, const std::string &strFromCharset) {
	try {
		m_lpContext = new context_t(strToCharset.c_str(), strFromCharset.c_str());
	} catch (const convert_exception &) {
		m_lpContext = NULL;
	}
}

ECIConv::~ECIConv()
{
	delete m_lpContext;
}

bool ECIConv::canConvert() {
	return (m_lpContext != NULL);
}

std::string ECIConv::convert(const std::string &strinput)
{
	return m_lpContext->convert(strinput);
}
