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
#include <kopano/ECLogger.h>
#include <kopano/ECConfig.h>
#include "PythonSWIGRuntime.h"
#include <edkmdb.h>

#include <kopano/auto_free.h>

inline void my_DECREF(PyObject *obj) {
	Py_DECREF(obj);
}

//@fixme wrong name, autofree should be auto_decref
typedef auto_free<PyObject, auto_free_dealloc<PyObject*, void, my_DECREF> >PyObjectAPtr;

#define MAKE_CUSTOM_SCODE(sev,fac,code) \
				(((unsigned int)(sev)<<31) | ((unsigned int)(1)<<29) | ((unsigned int)(fac)<<16) | ((unsigned int)(code)))

#define MAPI_E_MP_STOP		MAKE_CUSTOM_SCODE(1, FACILITY_ITF, 0x1)

#define MP_CONTINUE			0	// Continue with the next hook
#define MP_FAILED			1	// Whole process failed
#define MP_STOP_SUCCESS		2	// Stop with the message processing go to the next recipient. Recpient return code OK
#define MP_STOP_FAILED		3	// Stop with the message processing go to the next recipient. Recpient return code failed
#define MP_EXIT				4	// Exit the all the hook calls and go futher with the mail process.
#define MP_RETRY_LATER		5	// Stop Process and retry later


class PyMapiPlugin
{
public:
	PyMapiPlugin(void);
	virtual ~PyMapiPlugin(void);

	HRESULT Init(ECLogger *lpLogger, PyObject *lpModMapiPlugin, const char* lpPluginManagerClassName, const char *lpPluginPath);
	HRESULT MessageProcessing(const char *lpFunctionName, IMAPISession *lpMapiSession, IAddrBook *lpAdrBook, IMsgStore *lpMsgStore, IMAPIFolder *lpInbox, IMessage *lpMessage, ULONG *lpulResult);
	HRESULT RulesProcessing(const char *lpFunctionName, IMAPISession *lpMapiSession, IAddrBook *lpAdrBook, IMsgStore *lpMsgStore, IExchangeModifyTable *lpEMTRules, ULONG *lpulResult);
	HRESULT RequestCallExecution(const char *lpFunctionName, IMAPISession *lpMapiSession, IAddrBook *lpAdrBook, IMsgStore *lpMsgStore,  IMAPIFolder *lpFolder, IMessage *lpMessage, ULONG *lpulDoCallexe, ULONG *lpulResult);

    swig_type_info *type_p_ECLogger;
	swig_type_info *type_p_IAddrBook;
	swig_type_info *type_p_IMAPIFolder;
	swig_type_info *type_p_IMAPISession;
	swig_type_info *type_p_IMsgStore;
	swig_type_info *type_p_IMessage;
	swig_type_info *type_p_IExchangeModifyTable;

private:
	PyObjectAPtr m_ptrMapiPluginManager;
	ECLogger *m_lpLogger;

private:
	// Inhibit (accidental) copying
	PyMapiPlugin(const PyMapiPlugin &);
	PyMapiPlugin& operator=(const PyMapiPlugin &);
};



class PyMapiPluginFactory
{
public:
	PyMapiPluginFactory();
	HRESULT Init(ECConfig* lpConfig, ECLogger *lpLogger);
	~PyMapiPluginFactory();

	HRESULT CreatePlugin(const char* lpPluginManagerClassName, PyMapiPlugin **lppPlugin);

private:
	PyObjectAPtr m_ptrModMapiPlugin;
	bool m_bEnablePlugin;
	std::string m_strPluginPath;
	ECLogger *m_lpLogger;

private:
	// Inhibit (accidental) copying
	PyMapiPluginFactory(const PyMapiPluginFactory &);
	PyMapiPluginFactory& operator=(const PyMapiPluginFactory &);
};



inline void my_delete(PyMapiPlugin *obj) { delete obj; }

typedef auto_free<PyMapiPlugin, auto_free_dealloc<PyMapiPlugin*, void, my_delete> >PyMapiPluginAPtr;


#endif
