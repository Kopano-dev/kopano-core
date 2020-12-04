/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <memory>
#include <string>
#include <utility>
#include <kopano/platform.h>
#include "ECArchiverLogger.h"

namespace KC {

ArchiverLogger::ArchiverLogger(std::shared_ptr<ECLogger> lpLogger) :
	ECLogger(0), m_lpLogger(std::move(lpLogger))
{
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
		auto len = snprintf(buffer, sizeof(buffer), "For \"" TSTRING_PRINTF "\": ", m_strUser.c_str());
		strPrefix = EscapeFormatString(std::string(buffer, len));
	} else {
		auto len = snprintf(buffer, sizeof(buffer), "For \"" TSTRING_PRINTF "\" in folder \"" TSTRING_PRINTF "\": ", m_strUser.c_str(), m_strFolder.c_str());
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

ScopedUserLogging::ScopedUserLogging(std::shared_ptr<ECArchiverLogger> lpLogger,
    const tstring &strUser) :
	m_lpLogger(std::move(lpLogger)),
	m_strPrevUser(m_lpLogger->SetUser(strUser))
{ }

ScopedUserLogging::~ScopedUserLogging()
{
	m_lpLogger->SetUser(m_strPrevUser);
}

ScopedFolderLogging::ScopedFolderLogging(std::shared_ptr<ECArchiverLogger> lpLogger,
    const tstring &strFolder) :
	m_lpLogger(std::move(lpLogger)),
	m_strPrevFolder(m_lpLogger->SetFolder(strFolder))
{ }

ScopedFolderLogging::~ScopedFolderLogging()
{
	m_lpLogger->SetFolder(m_strPrevFolder);
}

} /* namespace */
