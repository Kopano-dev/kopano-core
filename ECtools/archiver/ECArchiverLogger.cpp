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
#include "ECArchiverLogger.h"

namespace KC {

ECArchiverLogger::ECArchiverLogger(ECLogger *lpLogger)
: ECLogger(0)
, m_lpLogger(lpLogger)
{
}

tstring ECArchiverLogger::SetUser(tstring strUser)
{
	std::swap(strUser, m_strUser);
	return strUser;
}

tstring ECArchiverLogger::SetFolder(tstring strFolder)
{
	std::swap(strFolder, m_strFolder);
	return strFolder;
}

void ECArchiverLogger::Reset()
{
	if (m_lpLogger)
		m_lpLogger->Reset();
}

void ECArchiverLogger::log(unsigned int loglevel, const char *message)
{
	if (m_lpLogger)
		m_lpLogger->Log(loglevel, message);
}

void ECArchiverLogger::logf(unsigned int loglevel, const char *format, ...)
{
	if (m_lpLogger == NULL || !m_lpLogger->Log(loglevel))
		return;
	std::string strFormat = CreateFormat(format);
	va_list va;
	va_start(va, format);
	m_lpLogger->logv(loglevel, strFormat.c_str(), va);
	va_end(va);
}

void ECArchiverLogger::logv(unsigned int loglevel, const char *format, va_list &va)
{
	if (m_lpLogger == NULL || !m_lpLogger->Log(loglevel))
		return;
	std::string strFormat = CreateFormat(format);
	m_lpLogger->logv(loglevel, strFormat.c_str(), va);
}

std::string ECArchiverLogger::CreateFormat(const char *format)
{
	char buffer[4096];
	std::string strPrefix;

	if (m_strUser.empty())
		return strPrefix + format;

	if (m_strFolder.empty()) {
		auto len = m_lpLogger->snprintf(buffer, sizeof(buffer), "For '" TSTRING_PRINTF "': ", m_strUser.c_str());
		strPrefix = EscapeFormatString(std::string(buffer, len));
	} else {
		auto len = m_lpLogger->snprintf(buffer, sizeof(buffer), "For '" TSTRING_PRINTF "' in folder '" TSTRING_PRINTF "': ", m_strUser.c_str(), m_strFolder.c_str());
		strPrefix = EscapeFormatString(std::string(buffer, len));
	}

	return strPrefix + format;
}

std::string ECArchiverLogger::EscapeFormatString(const std::string &strFormat)
{
	std::string strEscaped;
	strEscaped.reserve(strFormat.length() * 2);

	for (auto c : strFormat) {
		if (c == '\\')
			strEscaped.append("\\\\");
		else if (c == '%')
			strEscaped.append("%%");
		else
			strEscaped.append(1, c);
	}
	
	return strEscaped;
}

ScopedUserLogging::ScopedUserLogging(ECArchiverLogger *lpLogger, const tstring &strUser)
: m_lpLogger(lpLogger)
, m_strPrevUser(lpLogger->SetUser(strUser))
{ }

ScopedUserLogging::~ScopedUserLogging()
{
	m_lpLogger->SetUser(m_strPrevUser);
}

ScopedFolderLogging::ScopedFolderLogging(ECArchiverLogger *lpLogger, const tstring &strFolder)
: m_lpLogger(lpLogger)
, m_strPrevFolder(lpLogger->SetFolder(strFolder))
{ }

ScopedFolderLogging::~ScopedFolderLogging()
{
	m_lpLogger->SetFolder(m_strPrevFolder);
}

} /* namespace */
