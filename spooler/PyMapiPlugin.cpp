/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <Python.h>
#include <kopano/platform.h>
#include <memory>
#include <new>
#include <mapi.h>
#include <mapix.h>
#include <mapiutil.h>
#include <mapidefs.h>
#include <kopano/mapiext.h>
#include <kopano/memory.hpp>
#include <mapiguid.h>
#include "PythonSWIGRuntime.h"
#include "PyMapiPlugin.h"
#include <kopano/stringutil.h>
#include "frameobject.h"
#include "pymem.hpp"

#define NEW_SWIG_INTERFACE_POINTER_OBJ(pyswigobj, objpointer, typeobj) {\
	if (objpointer) {\
		pyswigobj.reset(SWIG_NewPointerObj((void *)objpointer, typeobj, SWIG_POINTER_OWN | 0)); \
		PY_HANDLE_ERROR(pyswigobj) \
		\
		objpointer->AddRef();\
	} else {\
		pyswigobj.reset(Py_None); \
	    Py_INCREF(Py_None);\
	}\
}

#define BUILD_SWIG_TYPE(pyswigobj, type) {\
	pyswigobj = SWIG_TypeQuery(type); \
	if (!pyswigobj) {\
		assert(false);\
		return S_FALSE;\
	}\
}

using namespace KC;
typedef pyobj_ptr PyObjectAPtr;

class PyMapiPlugin final : public pym_plugin_intf {
	public:
	PyMapiPlugin(void) = default;
	HRESULT Init(PyObject *lpModMapiPlugin, const char* lpPluginManagerClassName, const char *lpPluginPath);
	virtual HRESULT MessageProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *result);
	virtual HRESULT RulesProcessing(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IExchangeModifyTable *emt_rules, ULONG *result);
	virtual HRESULT RequestCallExecution(const char *func, IMAPISession *, IAddrBook *, IMsgStore *, IMAPIFolder *, IMessage *, ULONG *do_callexe, ULONG *result);

	swig_type_info *type_p_ECLogger = nullptr, *type_p_IAddrBook = nullptr;
	swig_type_info *type_p_IMAPIFolder = nullptr;
	swig_type_info *type_p_IMAPISession = nullptr;
	swig_type_info *type_p_IMsgStore = nullptr, *type_p_IMessage = nullptr;
	swig_type_info *type_p_IExchangeModifyTable = nullptr;

	private:
	PyObjectAPtr m_ptrMapiPluginManager{nullptr};

	/* Inhibit (accidental) copying */
	PyMapiPlugin(const PyMapiPlugin &) = delete;
	PyMapiPlugin &operator=(const PyMapiPlugin &) = delete;
};

struct pym_factory_priv {
	PyObjectAPtr m_ptrModMapiPlugin{nullptr};
};

/**
 * Handle the python errors
 *
 * note: The traceback doesn't work very well
 */
static HRESULT PyHandleError(PyObject *pyobj)
{
	if (pyobj != nullptr)
		return hrSuccess;

	PyObject *lpErr = PyErr_Occurred();
	if (lpErr == nullptr) {
		assert(false);
		return S_FALSE;
	}
	if (PyErr_ExceptionMatches(PyExc_KeyboardInterrupt))
		return S_FALSE;
	if (PyErr_ExceptionMatches(PyExc_SystemExit)) {
		ec_log_err("Plugin called exit(), which is meaningless");
		return S_FALSE;
	}
	PyObjectAPtr ptype, pvalue, ptraceback;
	PyErr_Fetch(&~ptype, &~pvalue, &~ptraceback);
	if (ptype == nullptr) {
		assert(false);
		return S_FALSE;
	}
	PyErr_NormalizeException(&+ptype, &+pvalue, &+ptraceback);
	if (ptraceback == nullptr) {
		ptraceback.reset(Py_None);
		Py_INCREF(ptraceback);
	}
	PyException_SetTraceback(pvalue, ptraceback);
	if (ptype == nullptr) {
		assert(false);
		return S_FALSE;
	}
	ec_log_info("Python traceback is on stderr (possibly check journalctl instead of logfile)");
	fprintf(stderr, "Python threw an exception:\n");
	PyErr_Display(ptype, pvalue, ptraceback);
	assert("Python threw an exception");
	return S_FALSE;
}

#define PY_HANDLE_ERROR(pyobj) { \
	hr = PyHandleError(pyobj); \
	if (hr != hrSuccess) \
		return hr; \
}

#define PY_CALL_METHOD(pluginmanager, functionname, returnmacro, format, ...) {\
	PyObjectAPtr ptrResult;\
	{\
		ptrResult.reset(PyObject_CallMethod(pluginmanager, const_cast<char *>(functionname), const_cast<char *>(format), __VA_ARGS__)); \
		PY_HANDLE_ERROR(ptrResult) \
		\
		returnmacro\
	}\
}

/**
 * Helper macro to parse the python return values which work together
 * with the macro PY_CALL_METHOD.
 *
 */
#define PY_PARSE_TUPLE_HELPER(format, ...) {\
	if(!PyArg_ParseTuple(ptrResult, format, __VA_ARGS__)) { \
		ec_log_err("  Wrong return value from the pluginmanager or plugin"); \
		PY_HANDLE_ERROR(nullptr) \
	} \
}

/**
 * Initialize the PyMapiPlugin.
 *
 * @param[in]	lpConfig Pointer to the configuration class
 * @param[in]	lpPluginManagerClassName The class name of the plugin handler
 *
 * @return Standard mapi errorcodes
 */
HRESULT PyMapiPlugin::Init(PyObject *lpModMapiPlugin,
    const char *lpPluginManagerClassName, const char *lpPluginPath)
{
	HRESULT			hr = S_OK;
	pyobj_ptr ptrClass, ptrArgs;

	if (!lpModMapiPlugin)
		return S_OK;
	// Init MAPI-swig types
	BUILD_SWIG_TYPE(type_p_IMessage, "_p_IMessage");
	BUILD_SWIG_TYPE(type_p_IMAPISession, "_p_IMAPISession");
	BUILD_SWIG_TYPE(type_p_IMsgStore, "_p_IMsgStore");
	BUILD_SWIG_TYPE(type_p_IAddrBook, "_p_IAddrBook");
	BUILD_SWIG_TYPE(type_p_IMAPIFolder, "_p_IMAPIFolder");
	BUILD_SWIG_TYPE(type_p_IExchangeModifyTable, "_p_IExchangeModifyTable");

	// Init plugin class
	ptrClass.reset(PyObject_GetAttrString(lpModMapiPlugin, /*char* */lpPluginManagerClassName));
	PY_HANDLE_ERROR(ptrClass);
	ptrArgs.reset(Py_BuildValue("(s)", lpPluginPath));
	PY_HANDLE_ERROR(ptrArgs);
	m_ptrMapiPluginManager.reset(PyObject_CallObject(ptrClass, ptrArgs));
	PY_HANDLE_ERROR(m_ptrMapiPluginManager);
	return hr;
}

/**
 * Plugin python call between MAPI and python.
 *
 * @param[in]	lpFunctionName	Python function name to call in the plugin framework.
 * 								 The function must be exist the lpPluginManagerClassName defined in the init function.
 * @param[in] lpMapiSession		Pointer to a mapi session. Not NULL.
 * @param[in] lpAdrBook			Pointer to a mapi Addressbook. Not NULL.
 * @param[in] lpMsgStore		Pointer to a mapi mailbox. Can be NULL.
 * @param[in] lpInbox
 * @param[in] lpMessage			Pointer to a mapi message.
 *
 * @return Default mapi error codes
 *
 * @todo something with exit codes
 */
HRESULT PyMapiPlugin::MessageProcessing(const char *lpFunctionName, IMAPISession *lpMapiSession, IAddrBook *lpAdrBook, IMsgStore *lpMsgStore, IMAPIFolder *lpInbox, IMessage *lpMessage, ULONG *lpulResult)
{
	HRESULT hr = hrSuccess;
	PyObjectAPtr ptrPySession, ptrPyAddrBook, ptrPyStore, ptrPyMessage, ptrPyFolderInbox;

	if (!m_ptrMapiPluginManager)
		return hrSuccess;
	if (!lpFunctionName || !lpMapiSession || !lpAdrBook)
		return MAPI_E_INVALID_PARAMETER;
	if (!m_ptrMapiPluginManager)
		return MAPI_E_CALL_FAILED;

	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPySession, lpMapiSession, type_p_IMAPISession)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPyAddrBook, lpAdrBook, type_p_IAddrBook)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPyStore, lpMsgStore, type_p_IMsgStore)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPyFolderInbox, lpInbox, type_p_IMAPIFolder)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPyMessage, lpMessage, type_p_IMessage)

	// Call the python function and get the (hr) return code back
	PY_CALL_METHOD(m_ptrMapiPluginManager, const_cast<char *>(lpFunctionName),
		PY_PARSE_TUPLE_HELPER("I", lpulResult), "OOOOO",
		ptrPySession.get(), ptrPyAddrBook.get(), ptrPyStore.get(),
		ptrPyFolderInbox.get(), ptrPyMessage.get());
	return hr;
}

/**
 * Hook for change the rules.
 *
 * @param[in] lpFunctionName	Python function name to hook the rules in the plugin framework.
 * 								 The function must be exist the lpPluginManagerClassName defined in the init function.
 * @param[in] lpEMTRules		Pointer to a mapi IExchangeModifyTable object
 */
HRESULT PyMapiPlugin::RulesProcessing(const char *lpFunctionName, IMAPISession *lpMapiSession, IAddrBook *lpAdrBook, IMsgStore *lpMsgStore, IExchangeModifyTable *lpEMTRules, ULONG *lpulResult)
{
	HRESULT hr = hrSuccess;
    PyObjectAPtr  ptrPySession, ptrPyAddrBook, ptrPyStore, ptrEMTIn;

	if (!m_ptrMapiPluginManager)
		return hrSuccess;
	if (!lpFunctionName || !lpMapiSession || !lpAdrBook || !lpEMTRules)
		return MAPI_E_INVALID_PARAMETER;
	if (!m_ptrMapiPluginManager)
		return MAPI_E_CALL_FAILED;

	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPySession, lpMapiSession, type_p_IMAPISession)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPyAddrBook, lpAdrBook, type_p_IAddrBook)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPyStore, lpMsgStore, type_p_IMsgStore)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrEMTIn, lpEMTRules, type_p_IExchangeModifyTable)

	PY_CALL_METHOD(m_ptrMapiPluginManager, const_cast<char *>(lpFunctionName),
		PY_PARSE_TUPLE_HELPER("I", lpulResult), "OOOO",
		ptrPySession.get(), ptrPyAddrBook.get(), ptrPyStore.get(),
		ptrEMTIn.get());
	return hr;
}

HRESULT PyMapiPlugin::RequestCallExecution(const char *lpFunctionName, IMAPISession *lpMapiSession, IAddrBook *lpAdrBook, IMsgStore *lpMsgStore,  IMAPIFolder *lpFolder, IMessage *lpMessage, ULONG *lpulDoCallexe, ULONG *lpulResult)
{
	HRESULT hr = hrSuccess;
	PyObjectAPtr  ptrPySession, ptrPyAddrBook, ptrPyStore, ptrFolder, ptrMessage;

	if (!m_ptrMapiPluginManager)
		return hrSuccess;
	if (!lpFunctionName || !lpMapiSession || !lpAdrBook || !lpulDoCallexe)
		return MAPI_E_INVALID_PARAMETER;
	if (!m_ptrMapiPluginManager)
		return MAPI_E_CALL_FAILED;

	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPySession, lpMapiSession, type_p_IMAPISession)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPyAddrBook, lpAdrBook, type_p_IAddrBook)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrPyStore, lpMsgStore, type_p_IMsgStore)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrFolder, lpFolder, type_p_IMAPIFolder)
	NEW_SWIG_INTERFACE_POINTER_OBJ(ptrMessage, lpMessage, type_p_IMessage)

	PY_CALL_METHOD(m_ptrMapiPluginManager, const_cast<char *>(lpFunctionName),
		PY_PARSE_TUPLE_HELPER("I|I", lpulResult, lpulDoCallexe), "OOOOO",
		ptrPySession.get(), ptrPyAddrBook.get(), ptrPyStore.get(),
		ptrFolder.get(), ptrMessage.get());
	return hr;
}

struct pym_factory_priv m_priv;

void plugin_manager_exit()
{
	if (m_priv.m_ptrModMapiPlugin != nullptr) {
		m_priv.m_ptrModMapiPlugin = nullptr;
		Py_Finalize();
	}
}

HRESULT plugin_manager_init(ECConfig *lpConfig,
    const char *lpPluginManagerClassName, pym_plugin_intf **lppPlugin)
{
	HRESULT			hr = S_OK;
	std::string strPluginPath = lpConfig->GetSetting("plugin_path");
	auto lpEnvPython = getenv("PYTHONPATH");
	ec_log_debug("PYTHONPATH = %s", lpEnvPython);
	Py_Initialize();
	PyObjectAPtr ptrModule(PyImport_ImportModule("MAPI"));
	PY_HANDLE_ERROR(ptrModule);
	// Import python plugin framework
	// @todo error unable to find file xxx
	PyObjectAPtr ptrName(PyUnicode_FromString("mapiplugin"));
	m_priv.m_ptrModMapiPlugin.reset(PyImport_Import(ptrName));
	PY_HANDLE_ERROR(m_priv.m_ptrModMapiPlugin);

	auto lpPlugin = make_unique_nt<PyMapiPlugin>();
	if (lpPlugin == nullptr)
		return MAPI_E_NOT_ENOUGH_MEMORY;
	hr = lpPlugin->Init(m_priv.m_ptrModMapiPlugin, lpPluginManagerClassName, strPluginPath.c_str());
	if (hr != S_OK)
		return hr;
	*lppPlugin = lpPlugin.release();
	return S_OK;
}
