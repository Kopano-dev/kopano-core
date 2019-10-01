/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef SPOOLER_PYMAPIPLUGIN_H
#define SPOOLER_PYMAPIPLUGIN_H 1

#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include <kopano/memory.hpp>
#include <edkmdb.h>

#define MAKE_CUSTOM_SCODE(sev,fac,code) \
				(((unsigned int)(sev)<<31) | ((unsigned int)(1)<<29) | ((unsigned int)(fac)<<16) | ((unsigned int)(code)))

#define MAPI_E_MP_STOP		MAKE_CUSTOM_SCODE(1, FACILITY_ITF, 0x1)

#define MP_CONTINUE			0	// Continue with the next hook
#define MP_FAILED			1	// Whole process failed
#define MP_STOP_SUCCESS		2	// Stop with the message processing go to the next recipient. Recpient return code OK
#define MP_STOP_FAILED		3	// Stop with the message processing go to the next recipient. Recpient return code failed
#define MP_EXIT				4	// Exit the all the hook calls and go further with the mail process.
#define MP_RETRY_LATER		5	// Stop Process and retry later

class pym_plugin_intf {
	public:
	virtual ~pym_plugin_intf() = default;
	virtual HRESULT MessageProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *result) { return hrSuccess; }
	virtual HRESULT RulesProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IExchangeModifyTable *emt_rules, ULONG *result) { return hrSuccess; }
	virtual HRESULT RequestCallExecution(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *do_callexe, ULONG *result) { return hrSuccess; }
};

class PyMapiPluginFactory final {
public:
	PyMapiPluginFactory() = default;
	~PyMapiPluginFactory();
	HRESULT create_plugin(KC::ECConfig *, const char *mgr_class, pym_plugin_intf **);

private:
	void *m_handle = nullptr;
	void (*m_exit)(void) = nullptr;

	// Inhibit (accidental) copying
	PyMapiPluginFactory(const PyMapiPluginFactory &) = delete;
	PyMapiPluginFactory &operator=(const PyMapiPluginFactory &) = delete;
};

extern "C" {

extern KC_EXPORT HRESULT plugin_manager_init(KC::ECConfig *, const char *, pym_plugin_intf **);
extern KC_EXPORT void plugin_manager_exit();

}

#endif
