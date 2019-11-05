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

class KC_EXPORT ECArchiverLogger final : public ECLogger {
public:
	ECArchiverLogger(std::shared_ptr<ECLogger>);
	KC_HIDDEN tstring SetUser(tstring = {});
	tstring SetFolder(tstring strFolder = tstring());
	KC_HIDDEN const tstring &GetUser() const { return m_strUser; }
	KC_HIDDEN const tstring &GetFolder() const { return m_strFolder; }
	KC_HIDDEN void Reset();
	void log(unsigned int level, const char *msg);
	void logf(unsigned int level, const char *fmt, ...) KC_LIKE_PRINTF(3, 4);
	void logv(unsigned int level, const char *fmt, va_list &);

private:
	KC_HIDDEN std::string CreateFormat(const char *fmt);
	KC_HIDDEN std::string EscapeFormatString(const std::string &fmt);
	ECArchiverLogger(const ECArchiverLogger &) = delete;
	ECArchiverLogger &operator=(const ECArchiverLogger &) = delete;

	std::shared_ptr<ECLogger> m_lpLogger;
	tstring m_strUser, m_strFolder;
};

class KC_EXPORT ScopedUserLogging final {
public:
	ScopedUserLogging(std::shared_ptr<ECArchiverLogger>, const tstring &strUser);
	~ScopedUserLogging();

private:
	ScopedUserLogging(const ScopedUserLogging &) = delete;
	ScopedUserLogging &operator=(const ScopedUserLogging &) = delete;

	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	const tstring m_strPrevUser;
};

class KC_EXPORT ScopedFolderLogging final {
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
