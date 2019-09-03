/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECCONFIGCHECK_H
#define ECCONFIGCHECK_H

#include <kopano/zcdefs.h>
#include <list>
#include <map>
#include <string>

enum CHECK_STATUS {
	CHECK_OK,
	CHECK_WARNING,
	CHECK_ERROR
};

enum CHECK_FLAGS {
	CONFIG_MANDATORY		= (1 << 0),	/* Configuration option is mandatory */
	CONFIG_HOSTED_USED		= (1 << 1),	/* Configuration option is only usable with hosted enabled */
	CONFIG_HOSTED_UNUSED	= (1 << 2),	/* Configuration option is only usable with hosted disabled */
	CONFIG_MULTI_USED		= (1 << 3),	/* Configuration option is only usable with multi-server enabled */
	CONFIG_MULTI_UNUSED		= (1 << 4)	/* Configuration option is only usable with multi-server disabled */
};

/* Each subclass of ECConfigCheck can register check functions
 * for the configuration options. */
struct config_check_t {
	bool hosted;
	bool multi;
	std::string option1, option2, value1, value2;
	int (*check)(const config_check_t *);
};

class ECConfigCheck {
public:
	ECConfigCheck(const char *lpszName, const char *lpszConfigFile);
	virtual ~ECConfigCheck(void) = default;
	/* Must be overwritten by subclass */
	virtual void loadChecks() = 0;
	bool isDirty() const;
	void setHosted(bool hosted);
	void setMulti(bool multi);
	void validate();
	const std::string &getSetting(const std::string &);

protected:
	static void printError(const std::string &, const std::string &);
	static void printWarning(const std::string &, const std::string &);
	void addCheck(const std::string &, unsigned int, int (*)(const config_check_t *) = NULL);
	void addCheck(const std::string &, const std::string &, unsigned int, int (*)(const config_check_t *) = NULL);

private:
	void readConfigFile(const char *lpszConfigFile);

	/* Generic check functions */
	static int testMandatory(const config_check_t *);
	static int testUsedWithHosted(const config_check_t *);
	static int testUsedWithoutHosted(const config_check_t *);
	static int testUsedWithMultiServer(const config_check_t *);
	static int testUsedWithoutMultiServer(const config_check_t *);

protected:
	static int testDirectory(const config_check_t *);
	static int testFile(const config_check_t *);
	static int testCharset(const config_check_t *);
	static int testBoolean(const config_check_t *);
	static int testNonZero(const config_check_t *);

private:
	/* Generic addCheckFunction */
	void addCheck(const config_check_t &, unsigned int);

	/* private variables */
	const char *m_lpszName = nullptr, *m_lpszConfigFile = nullptr;
	std::map<std::string, std::string> m_mSettings;
	std::list<config_check_t> m_lChecks;
	bool m_bDirty = false, m_bHosted = false, m_bMulti = false;
};

class DAgentConfigCheck final : public ECConfigCheck {
	public:
	DAgentConfigCheck(const char *file);
	void loadChecks() override;
};

class LDAPConfigCheck final : public ECConfigCheck {
	public:
	LDAPConfigCheck(const char *file);
	void loadChecks() override;

	private:
	static int testLdapType(const config_check_t *);
	static int testLdapQuery(const config_check_t *);
	static bool verifyLDAPQuery(const config_check_t *);
};

class MonitorConfigCheck final : public ECConfigCheck {
	public:
	MonitorConfigCheck(const char *file);
	void loadChecks() override;
};

class ServerConfigCheck final : public ECConfigCheck {
	public:
	ServerConfigCheck(const char *file);
	void loadChecks() override;

	private:
	static int testAttachment(const config_check_t *);
	static int testPluginConfig(const config_check_t *);
	static int testAttachmentPath(const config_check_t *);
	static int testPlugin(const config_check_t *);
	static int testPluginPath(const config_check_t *);
	static int testStorename(const config_check_t *);
	static int testLoginname(const config_check_t *);
	static int testAuthMethod(const config_check_t *);
};

class SpoolerConfigCheck final : public ECConfigCheck {
	public:
	SpoolerConfigCheck(const char *file);
	void loadChecks() override;
};

class UnixConfigCheck final : public ECConfigCheck {
	public:
	UnixConfigCheck(const char *file);
	void loadChecks() override;

	private:
	static int testId(const config_check_t *);
};

#endif
