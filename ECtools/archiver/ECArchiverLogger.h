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

#ifndef LOGGER_H
#define LOGGER_H

#include <kopano/ECLogger.h>
#include <kopano/tstring.h>

class ECArchiverLogger : public ECLogger
{
public:
	ECArchiverLogger(ECLogger *lpLogger);
	~ECArchiverLogger();

	tstring SetUser(tstring strUser = tstring());
	tstring SetFolder(tstring strFolder = tstring());

	const tstring& GetUser() const { return m_strUser; }
	const tstring& GetFolder() const { return m_strFolder; }

	void Reset();
	void Log(unsigned int loglevel, const std::string &message);
	void Log(unsigned int loglevel, const char *format, ...) __LIKE_PRINTF(3, 4);
	void LogVA(unsigned int loglevel, const char *format, va_list& va);

private:
	std::string CreateFormat(const char *format);
	std::string EscapeFormatString(const std::string &strFormat);

private:
	ECArchiverLogger(const ECArchiverLogger&);
	ECArchiverLogger& operator=(const ECArchiverLogger&);

private:
	ECLogger	*m_lpLogger;
	tstring		m_strUser;
	tstring		m_strFolder;
};


class ScopedUserLogging
{
public:
	ScopedUserLogging(ECArchiverLogger *lpLogger, const tstring &strUser);
	~ScopedUserLogging();

private:
	ScopedUserLogging(const ScopedUserLogging&);
	ScopedUserLogging& operator=(const ScopedUserLogging&);

private:
	ECArchiverLogger *m_lpLogger;
	const tstring m_strPrevUser;
};


class ScopedFolderLogging
{
public:
	ScopedFolderLogging(ECArchiverLogger *lpLogger, const tstring &strFolder);
	~ScopedFolderLogging();

private:
	ScopedFolderLogging(const ScopedFolderLogging&);
	ScopedFolderLogging& operator=(const ScopedFolderLogging&);

private:
	ECArchiverLogger *m_lpLogger;
	const tstring m_strPrevFolder;
};

#endif // ndef LOGGER_H
