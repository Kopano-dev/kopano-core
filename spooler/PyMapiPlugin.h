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

#include <Python.h>
#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include <kopano/memory.hpp>
#include "PythonSWIGRuntime.h"
#include <edkmdb.h>

class kcpy_decref {
	public:
	void operator()(PyObject *obj) { Py_DECREF(obj); }
};

typedef KCHL::memory_ptr<PyObject, kcpy_decref> PyObjectAPtr;

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
	virtual ~pym_plugin_intf() _kc_impdtor;
	virtual HRESULT MessageProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *result) = 0;
	virtual HRESULT RulesProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IExchangeModifyTable *emt_rules, ULONG *result) = 0;
	virtual HRESULT RequestCallExecution(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *do_callexe, ULONG *result) = 0;
};

class PyMapiPlugin _kc_final : public pym_plugin_intf {
public:
	PyMapiPlugin(void) = default;
	virtual ~PyMapiPlugin(void);

	HRESULT Init(ECLogger *lpLogger, PyObject *lpModMapiPlugin, const char* lpPluginManagerClassName, const char *lpPluginPath);
	virtual HRESULT MessageProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *result);
	virtual HRESULT RulesProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IExchangeModifyTable *emt_rules, ULONG *result);
	virtual HRESULT RequestCallExecution(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *do_callexe, ULONG *result);

	swig_type_info *type_p_ECLogger = nullptr, *type_p_IAddrBook = nullptr;
	swig_type_info *type_p_IMAPIFolder = nullptr;
	swig_type_info *type_p_IMAPISession = nullptr;
	swig_type_info *type_p_IMsgStore = nullptr;
	swig_type_info *type_p_IMessage = nullptr;
	swig_type_info *type_p_IExchangeModifyTable = nullptr;

private:
	PyObjectAPtr m_ptrMapiPluginManager{nullptr};
	ECLogger *m_lpLogger = nullptr;

	// Inhibit (accidental) copying
	PyMapiPlugin(const PyMapiPlugin &) = delete;
	PyMapiPlugin &operator=(const PyMapiPlugin &) = delete;
};

class PyMapiPluginFactory _kc_final {
public:
	PyMapiPluginFactory(void) = default;
	~PyMapiPluginFactory();
	HRESULT create_plugin(ECConfig *, ECLogger *, const char *mgr_class, pym_plugin_intf **);

private:
	PyObjectAPtr m_ptrModMapiPlugin{nullptr};
	bool m_bEnablePlugin = false;
	std::string m_strPluginPath;
	ECLogger *m_lpLogger = nullptr;

	// Inhibit (accidental) copying
	PyMapiPluginFactory(const PyMapiPluginFactory &) = delete;
	PyMapiPluginFactory &operator=(const PyMapiPluginFactory &) = delete;
};

#endif
