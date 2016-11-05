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

#include <kopano/zcdefs.h>
#include <kopano/ECLogger.h>
#include <kopano/tstring.h>

class _kc_export ECArchiverLogger _kc_final : public ECLogger {
public:
	ECArchiverLogger(ECLogger *lpLogger);
	_kc_hidden ~ECArchiverLogger(void);
	_kc_hidden tstring SetUser(tstring = tstring());
	tstring SetFolder(tstring strFolder = tstring());
	_kc_hidden const tstring &GetUser(void) const { return m_strUser; }
	_kc_hidden const tstring &GetFolder(void) const { return m_strFolder; }
	_kc_hidden void Reset(void);
	_kc_hidden void Log(unsigned int level, const std::string &msg);
	void Log(unsigned int level, const char *fmt, ...) __LIKE_PRINTF(3, 4);
	_kc_hidden void LogVA(unsigned int level, const char *fmt, va_list &);

private:
	_kc_hidden std::string CreateFormat(const char *fmt);
	_kc_hidden std::string EscapeFormatString(const std::string &fmt);

private:
	ECArchiverLogger(const ECArchiverLogger &) = delete;
	ECArchiverLogger &operator=(const ECArchiverLogger &) = delete;

private:
	ECLogger	*m_lpLogger;
	tstring		m_strUser;
	tstring		m_strFolder;
};

class _kc_export ScopedUserLogging _kc_final {
public:
	ScopedUserLogging(ECArchiverLogger *lpLogger, const tstring &strUser);
	~ScopedUserLogging();

private:
	ScopedUserLogging(const ScopedUserLogging &) = delete;
	ScopedUserLogging &operator=(const ScopedUserLogging &) = delete;

private:
	ECArchiverLogger *m_lpLogger;
	const tstring m_strPrevUser;
};

class _kc_export ScopedFolderLogging _kc_final {
public:
	ScopedFolderLogging(ECArchiverLogger *lpLogger, const tstring &strFolder);
	~ScopedFolderLogging();

private:
	ScopedFolderLogging(const ScopedFolderLogging &) = delete;
	ScopedFolderLogging &operator=(const ScopedFolderLogging &) = delete;

private:
	ECArchiverLogger *m_lpLogger;
	const tstring m_strPrevFolder;
};

#endif // ndef LOGGER_H
