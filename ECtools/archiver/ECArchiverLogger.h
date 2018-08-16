/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/ECLogger.h>

namespace KC {

class _kc_export ECArchiverLogger _kc_final : public ECLogger {
public:
	ECArchiverLogger(std::shared_ptr<ECLogger>);
	_kc_hidden tstring SetUser(tstring = tstring());
	tstring SetFolder(tstring strFolder = tstring());
	_kc_hidden const tstring &GetUser(void) const { return m_strUser; }
	_kc_hidden const tstring &GetFolder(void) const { return m_strFolder; }
	_kc_hidden void Reset(void);
	void log(unsigned int level, const char *msg);
	void logf(unsigned int level, const char *fmt, ...) KC_LIKE_PRINTF(3, 4);
	void logv(unsigned int level, const char *fmt, va_list &);

private:
	_kc_hidden std::string CreateFormat(const char *fmt);
	_kc_hidden std::string EscapeFormatString(const std::string &fmt);
	ECArchiverLogger(const ECArchiverLogger &) = delete;
	ECArchiverLogger &operator=(const ECArchiverLogger &) = delete;

	std::shared_ptr<ECLogger> m_lpLogger;
	tstring		m_strUser;
	tstring		m_strFolder;
};

class _kc_export ScopedUserLogging _kc_final {
public:
	ScopedUserLogging(std::shared_ptr<ECArchiverLogger>, const tstring &strUser);
	~ScopedUserLogging();

private:
	ScopedUserLogging(const ScopedUserLogging &) = delete;
	ScopedUserLogging &operator=(const ScopedUserLogging &) = delete;

	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	const tstring m_strPrevUser;
};

class _kc_export ScopedFolderLogging _kc_final {
public:
	ScopedFolderLogging(std::shared_ptr<ECArchiverLogger>, const tstring &strFolder);
	~ScopedFolderLogging();

private:
	ScopedFolderLogging(const ScopedFolderLogging &) = delete;
	ScopedFolderLogging &operator=(const ScopedFolderLogging &) = delete;

	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	const tstring m_strPrevFolder;
};

} /* namespace */

#endif // ndef LOGGER_H
