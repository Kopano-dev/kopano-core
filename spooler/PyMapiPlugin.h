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

#ifndef _PYMAPIPLUGIN_H
#define _PYMAPIPLUGIN_H

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
#define MP_EXIT				4	// Exit the all the hook calls and go futher with the mail process.
#define MP_RETRY_LATER		5	// Stop Process and retry later

class pym_plugin_intf {
	public:
	virtual ~pym_plugin_intf() = default;
	virtual HRESULT MessageProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *result) { return hrSuccess; }
	virtual HRESULT RulesProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IExchangeModifyTable *emt_rules, ULONG *result) { return hrSuccess; }
	virtual HRESULT RequestCallExecution(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *do_callexe, ULONG *result) { return hrSuccess; }
};

struct pym_factory_priv;

class PyMapiPluginFactory _kc_final {
public:
	PyMapiPluginFactory(void);
	~PyMapiPluginFactory();
	HRESULT create_plugin(KC::ECConfig *, KC::ECLogger *, const char *mgr_class, pym_plugin_intf **);

private:
	struct pym_factory_priv *m_priv;
	bool m_bEnablePlugin = false;
	std::string m_strPluginPath;
	KC::object_ptr<KC::ECLogger> m_lpLogger;

	// Inhibit (accidental) copying
	PyMapiPluginFactory(const PyMapiPluginFactory &) = delete;
	PyMapiPluginFactory &operator=(const PyMapiPluginFactory &) = delete;
};

#endif
