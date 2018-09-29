/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/memory.hpp>
#include <kopano/scope.hpp>
#include <kopano/platform.h>
#include <mapix.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapiutil.h>
#include <edkmdb.h>
#include <Python.h>
#include <kopano/charset/convert.h>
#include <kopano/conversion.h>
#include "pymem.hpp"
#include "scl.h"

using namespace KC;

// From Structs.py
static PyObject *PyTypeSPropValue;
static PyObject *PyTypeSSort;
static PyObject *PyTypeSSortOrderSet;
static PyObject *PyTypeSPropProblem;
static PyObject *PyTypeMAPINAMEID;
static PyObject *PyTypeMAPIError;
static PyObject *PyTypeREADSTATE;
static PyObject *PyTypeSTATSTG;
static PyObject *PyTypeSYSTEMTIME;

static PyObject *PyTypeNEWMAIL_NOTIFICATION;
static PyObject *PyTypeOBJECT_NOTIFICATION;
static PyObject *PyTypeTABLE_NOTIFICATION;

static PyObject *PyTypeMVPROPMAP;
static PyObject *PyTypeECUser;
static PyObject *PyTypeECGroup;
static PyObject *PyTypeECCompany;
static PyObject *PyTypeECQuota;
static PyObject *PyTypeECUserClientUpdateStatus;
static PyObject *PyTypeECServer;
static PyObject *PyTypeECQuotaStatus;

static PyObject *PyTypeSAndRestriction;
static PyObject *PyTypeSOrRestriction;
static PyObject *PyTypeSNotRestriction;
static PyObject *PyTypeSContentRestriction;
static PyObject *PyTypeSBitMaskRestriction;
static PyObject *PyTypeSPropertyRestriction;
static PyObject *PyTypeSComparePropsRestriction;
static PyObject *PyTypeSSizeRestriction;
static PyObject *PyTypeSExistRestriction;
static PyObject *PyTypeSSubRestriction;
static PyObject *PyTypeSCommentRestriction;

static PyObject *PyTypeActMoveCopy;
static PyObject *PyTypeActReply;
static PyObject *PyTypeActDeferAction;
static PyObject *PyTypeActBounce;
static PyObject *PyTypeActFwdDelegate;
static PyObject *PyTypeActTag;
static PyObject *PyTypeAction;
static PyObject *PyTypeACTIONS;

// From Time.py
static PyObject *PyTypeFiletime;

// Work around "bad argument to internal function"
#if defined(_M_X64) || defined(__amd64__)
#define PyLong_AsUINT64 PyLong_AsUnsignedLong
#define PyLong_AsINT64 PyLong_AsLong
#else
#define PyLong_AsUINT64 PyLong_AsUnsignedLongLong
#define PyLong_AsINT64 PyLong_AsLongLong
#endif

// Get Py_ssize_t for older versions of python
#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
# define PY_SSIZE_T_MAX INT_MAX
# define PY_SSIZE_T_MIN INT_MIN
#endif

// Depending on native vs python unicode representation, set NATIVE_UNICODE
  #define WCHAR_T_SIZE __SIZEOF_WCHAR_T__

#if (Py_UNICODE_SIZE == WCHAR_T_SIZE)
  #define NATIVE_UNICODE 1
#else
  #define NATIVE_UNICODE 0
#endif

void Init()
{
	PyObject *lpMAPIStruct = PyImport_ImportModule("MAPI.Struct");
	PyObject *lpMAPITime = PyImport_ImportModule("MAPI.Time");

	if(!lpMAPIStruct) {
		PyErr_SetString(PyExc_RuntimeError, "Unable to import MAPI.Struct");
		return;
	}

	if(!lpMAPITime) {
		PyErr_SetString(PyExc_RuntimeError, "Unable to import MAPI.Time");
		return;
	}

	PyTypeSPropValue = PyObject_GetAttrString(lpMAPIStruct, "SPropValue");
	PyTypeSPropProblem = PyObject_GetAttrString(lpMAPIStruct, "SPropProblem");
	PyTypeSSort = PyObject_GetAttrString(lpMAPIStruct, "SSort");
	PyTypeSSortOrderSet = PyObject_GetAttrString(lpMAPIStruct, "SSortOrderSet");
	PyTypeMAPINAMEID = PyObject_GetAttrString(lpMAPIStruct, "MAPINAMEID");
	PyTypeMAPIError = PyObject_GetAttrString(lpMAPIStruct, "MAPIError");
	PyTypeREADSTATE = PyObject_GetAttrString(lpMAPIStruct, "READSTATE");
	PyTypeSTATSTG = PyObject_GetAttrString(lpMAPIStruct, "STATSTG");
	PyTypeSYSTEMTIME = PyObject_GetAttrString(lpMAPIStruct, "SYSTEMTIME");

        PyTypeMVPROPMAP = PyObject_GetAttrString(lpMAPIStruct, "MVPROPMAP");
	PyTypeECUser = PyObject_GetAttrString(lpMAPIStruct, "ECUSER");
	PyTypeECGroup = PyObject_GetAttrString(lpMAPIStruct, "ECGROUP");
	PyTypeECCompany = PyObject_GetAttrString(lpMAPIStruct, "ECCOMPANY");
	PyTypeECQuota = PyObject_GetAttrString(lpMAPIStruct, "ECQUOTA");
	PyTypeECServer = PyObject_GetAttrString(lpMAPIStruct, "ECSERVER");
	PyTypeECQuotaStatus = PyObject_GetAttrString(lpMAPIStruct, "ECQUOTASTATUS");

	PyTypeNEWMAIL_NOTIFICATION = PyObject_GetAttrString(lpMAPIStruct, "NEWMAIL_NOTIFICATION");
	PyTypeOBJECT_NOTIFICATION = PyObject_GetAttrString(lpMAPIStruct, "OBJECT_NOTIFICATION");
	PyTypeTABLE_NOTIFICATION = PyObject_GetAttrString(lpMAPIStruct, "TABLE_NOTIFICATION");

	PyTypeSAndRestriction = PyObject_GetAttrString(lpMAPIStruct, "SAndRestriction");
	PyTypeSOrRestriction = PyObject_GetAttrString(lpMAPIStruct, "SOrRestriction");
	PyTypeSNotRestriction = PyObject_GetAttrString(lpMAPIStruct, "SNotRestriction");
	PyTypeSContentRestriction = PyObject_GetAttrString(lpMAPIStruct, "SContentRestriction");
	PyTypeSBitMaskRestriction = PyObject_GetAttrString(lpMAPIStruct, "SBitMaskRestriction");
	PyTypeSPropertyRestriction = PyObject_GetAttrString(lpMAPIStruct, "SPropertyRestriction");
	PyTypeSComparePropsRestriction = PyObject_GetAttrString(lpMAPIStruct, "SComparePropsRestriction");
	PyTypeSSizeRestriction = PyObject_GetAttrString(lpMAPIStruct, "SSizeRestriction");
	PyTypeSExistRestriction = PyObject_GetAttrString(lpMAPIStruct, "SExistRestriction");
	PyTypeSSubRestriction = PyObject_GetAttrString(lpMAPIStruct, "SSubRestriction");
	PyTypeSCommentRestriction = PyObject_GetAttrString(lpMAPIStruct, "SCommentRestriction");

	PyTypeActMoveCopy = PyObject_GetAttrString(lpMAPIStruct, "actMoveCopy");
	PyTypeActReply = PyObject_GetAttrString(lpMAPIStruct, "actReply");
	PyTypeActDeferAction = PyObject_GetAttrString(lpMAPIStruct, "actDeferAction");
	PyTypeActBounce = PyObject_GetAttrString(lpMAPIStruct, "actBounce");
	PyTypeActFwdDelegate = PyObject_GetAttrString(lpMAPIStruct, "actFwdDelegate");
	PyTypeActTag = PyObject_GetAttrString(lpMAPIStruct, "actTag");
	PyTypeAction = PyObject_GetAttrString(lpMAPIStruct, "ACTION");
	PyTypeACTIONS = PyObject_GetAttrString(lpMAPIStruct, "ACTIONS");

	PyTypeFiletime = PyObject_GetAttrString(lpMAPITime, "FileTime");
}

// Coerce PyObject into PyUnicodeObject, copy and zero-terminate
wchar_t * CopyPyUnicode(wchar_t **lpWide, PyObject *o, void *lpBase)
{
    int size;
	pyobj_ptr unicode(PyUnicode_FromObject(o));
    if(!unicode) {
        *lpWide = NULL;
        return NULL;
    }
        
    size = PyUnicode_GetSize(unicode);
    
    if (MAPIAllocateMore((size + 1) * sizeof(wchar_t), lpBase, (void **)lpWide) == hrSuccess) {
	    #if PY_MAJOR_VERSION >= 3
		PyUnicode_AsWideChar(unicode, *lpWide, size);
	    #else
		PyUnicode_AsWideChar((PyUnicodeObject *)unicode.get(), *lpWide, size);
	    #endif
	    
	    (*lpWide)[size] = '\0';
	    return *lpWide;
    }
    return NULL;
}

FILETIME Object_to_FILETIME(PyObject *object)
{
	FILETIME ft = {0, 0};
	unsigned long long periods = 0;

	PyObject *filetime = PyObject_GetAttrString(object, "filetime");
	if (!filetime) {
		PyErr_Format(PyExc_TypeError, "PT_SYSTIME object does not have 'filetime' attribute");
		return ft;
	}

	#if PY_MAJOR_VERSION >= 3
		periods = PyLong_AsUnsignedLongLongMask(filetime);
	#else
		periods = PyInt_AsUnsignedLongLongMask(filetime);
	#endif
	ft.dwHighDateTime = periods >> 32;
	ft.dwLowDateTime = periods & 0xffffffff;
	return ft;
}

PyObject *Object_from_FILETIME(FILETIME ft)
{
	pyobj_ptr filetime(PyLong_FromUnsignedLongLong((static_cast<unsigned long long>(ft.dwHighDateTime) << 32) + ft.dwLowDateTime));
	if (PyErr_Occurred())
		return nullptr;
	return PyObject_CallFunction(PyTypeFiletime, "(O)", filetime.get());
}

PyObject *Object_from_SPropValue(const SPropValue *lpProp)
{
	pyobj_ptr Value, ulPropTag(PyLong_FromUnsignedLong(lpProp->ulPropTag));

	switch(PROP_TYPE(lpProp->ulPropTag)) {
	case PT_STRING8:
		Value.reset(PyString_FromString(lpProp->Value.lpszA));
		break;
	case PT_UNICODE:
		Value.reset(PyUnicode_FromWideChar(lpProp->Value.lpszW, wcslen(lpProp->Value.lpszW)));
		break;
	case PT_BINARY:
		Value.reset(PyString_FromStringAndSize(reinterpret_cast<const char *>(lpProp->Value.bin.lpb), lpProp->Value.bin.cb));
		break;
	case PT_SHORT:
		Value.reset(PyLong_FromLong(lpProp->Value.i));
		break;
	case PT_ERROR:
		Value.reset(PyLong_FromUnsignedLong(static_cast<unsigned int>(lpProp->Value.err)));
		break;
	case PT_LONG:
		/*
		 * Need to use LongLong since it could be either PT_LONG or
		 * PT_ULONG. 'Long' doesn't cover the range.
		 */
		Value.reset(PyLong_FromLongLong(lpProp->Value.l));
		break;
	case PT_FLOAT:
		Value.reset(PyFloat_FromDouble(lpProp->Value.flt));
		break;
	case PT_APPTIME:
	case PT_DOUBLE:
		Value.reset(PyFloat_FromDouble(lpProp->Value.dbl));
		break;
	case PT_LONGLONG:
	case PT_CURRENCY:
		Value.reset(PyLong_FromLongLong(lpProp->Value.cur.int64));
		break;
	case PT_BOOLEAN:
		Value.reset(PyBool_FromLong(lpProp->Value.b));
		break;
	case PT_SYSTIME:
		Value.reset(Object_from_FILETIME(lpProp->Value.ft));
		break;
	case PT_CLSID:
		Value.reset(PyString_FromStringAndSize(reinterpret_cast<const char *>(lpProp->Value.lpguid), sizeof(GUID)));
		break;
	case PT_OBJECT:
		Py_INCREF(Py_None);
		Value.reset(Py_None);
		break;
	case PT_SRESTRICTION:
		Value.reset(Object_from_LPSRestriction(reinterpret_cast<SRestriction *>(lpProp->Value.lpszA)));
		break;
	case PT_ACTIONS:
		Value.reset(Object_from_LPACTIONS(reinterpret_cast<ACTIONS *>(lpProp->Value.lpszA)));
		break;

#define BASE(x) x
#define INT64(x) x.int64
#define QUADPART(x) x.QuadPart
#define PT_MV_CASE(MVname,MVelem,From,Sub) \
	Value.reset(PyList_New(0)); \
	for (unsigned int i = 0; i < lpProp->Value.MV##MVname.cValues; ++i) { \
		pyobj_ptr elem(From(Sub(lpProp->Value.MV##MVname.lp##MVelem[i]))); \
		PyList_Append(Value, elem); \
	} \
	break;

	case PT_MV_SHORT:
		PT_MV_CASE(i, i, PyLong_FromLong,BASE)
	case PT_MV_LONG:
		PT_MV_CASE(l, l, PyLong_FromLong,BASE)
	case PT_MV_FLOAT:
		PT_MV_CASE(flt, flt, PyFloat_FromDouble,BASE)
	case PT_MV_DOUBLE:
		PT_MV_CASE(dbl, dbl, PyFloat_FromDouble,BASE)
	case PT_MV_CURRENCY:
		PT_MV_CASE(cur, cur, PyLong_FromLongLong,INT64)
	case PT_MV_APPTIME:
		PT_MV_CASE(at, at, PyFloat_FromDouble,BASE)
	case PT_MV_LONGLONG:
		PT_MV_CASE(li, li, PyLong_FromLongLong,QUADPART)
	case PT_MV_SYSTIME:
		Value.reset(PyList_New(0));
		for (unsigned int i = 0; i < lpProp->Value.MVft.cValues; ++i) {
			pyobj_ptr elem(Object_from_FILETIME(lpProp->Value.MVft.lpft[i]));
			PyList_Append(Value, elem);
		}
		break;
	case PT_MV_STRING8:
		PT_MV_CASE(szA, pszA, PyString_FromString, BASE)
	case PT_MV_BINARY:
		Value.reset(PyList_New(0));
		for (unsigned int i = 0; i < lpProp->Value.MVbin.cValues; ++i) {
			pyobj_ptr elem(PyString_FromStringAndSize(reinterpret_cast<const char *>(lpProp->Value.MVbin.lpbin[i].lpb), lpProp->Value.MVbin.lpbin[i].cb));
			PyList_Append(Value, elem);
		}
		break;
	case PT_MV_UNICODE:
		Value.reset(PyList_New(0));
		for (unsigned int i = 0; i < lpProp->Value.MVszW.cValues; ++i) {
			int len = wcslen(lpProp->Value.MVszW.lppszW[i]);
			pyobj_ptr elem(PyUnicode_FromWideChar(lpProp->Value.MVszW.lppszW[i], len));
			PyList_Append(Value, elem);
		}
		break;
	case PT_MV_CLSID:
		Value.reset(PyList_New(0));
		for (unsigned int i = 0; i < lpProp->Value.MVguid.cValues; ++i) {
			pyobj_ptr elem(PyString_FromStringAndSize(reinterpret_cast<const char *>(&lpProp->Value.MVguid.lpguid[i]), sizeof(GUID)));
			PyList_Append(Value, elem);
		}
		break;
	case PT_NULL:
		Py_INCREF(Py_None);
		Value.reset(Py_None);
		break;
	default:
		PyErr_Format(PyExc_RuntimeError, "Bad property type %x", PROP_TYPE(lpProp->ulPropTag));
		break;
	}
	if (PyErr_Occurred())
		return nullptr;
	return PyObject_CallFunction(PyTypeSPropValue, "(OO)", ulPropTag.get(), Value.get());
}

PyObject *Object_from_LPSPropValue(const SPropValue *prop)
{
	return Object_from_SPropValue(prop);
}

int Object_is_LPSPropValue(PyObject *object)
{
	return PyObject_IsInstance(object, PyTypeSPropValue);
}

PyObject *List_from_SPropValue(const SPropValue *lpProps, ULONG cValues)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < cValues; ++i) {
		pyobj_ptr item(Object_from_LPSPropValue(&lpProps[i]));
		if(PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

PyObject *List_from_LPSPropValue(const SPropValue *props, ULONG vals)
{
	return List_from_SPropValue(props, vals);
}

void Object_to_p_SPropValue(PyObject *object, SPropValue *lpProp,
    ULONG ulFlags, void *lpBase)
{
	char *lpstr = NULL;
	Py_ssize_t size = 0;
	pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulPropTag"));
	pyobj_ptr Value(PyObject_GetAttrString(object, "Value"));
	if(!ulPropTag || !Value) {
		PyErr_SetString(PyExc_RuntimeError, "ulPropTag or Value missing from SPropValue");
		return;
	}

	lpProp->dwAlignPad = 0;
	lpProp->ulPropTag = (ULONG)PyLong_AsUnsignedLong(ulPropTag);
	switch(PROP_TYPE(lpProp->ulPropTag)) {
	case PT_NULL:
		lpProp->Value.x = 0;
		break;
	case PT_STRING8:
		if (ulFlags == CONV_COPY_SHALLOW) {
			lpProp->Value.lpszA = PyString_AsString(Value);
			break;
		}
		if (PyString_AsStringAndSize(Value, &lpstr, &size) < 0 ||
		    KAllocCopy(lpstr, size + 1, reinterpret_cast<void **>(&lpProp->Value.lpszA), lpBase) != hrSuccess)
			return;
		break;
	case PT_UNICODE:
		// @todo add PyUnicode_Check call?
		if (ulFlags == CONV_COPY_SHALLOW && NATIVE_UNICODE)
			lpProp->Value.lpszW = (WCHAR *)PyUnicode_AsUnicode(Value);
		else
			CopyPyUnicode(&lpProp->Value.lpszW, Value, lpBase);
		break;
	case PT_ERROR:
		lpProp->Value.ul = (ULONG)PyLong_AsUnsignedLong(Value);
		break;
	case PT_SHORT:
		lpProp->Value.i = (short int)PyLong_AsLong(Value);
		break;
	case PT_LONG:
		lpProp->Value.ul = (ULONG)PyLong_AsLongLong(Value); // We have to use LongLong since it could be either PT_LONG or PT_ULONG. 'Long' doesn't cover the range
		break;
	case PT_FLOAT:
		lpProp->Value.flt = (float)PyFloat_AsDouble(Value);
		break;
	case PT_APPTIME:
	case PT_DOUBLE:
		lpProp->Value.dbl = PyFloat_AsDouble(Value);
		break;
	case PT_LONGLONG:
	case PT_CURRENCY:
		lpProp->Value.cur.int64 = PyLong_AsINT64(Value);
		break;
	case PT_BOOLEAN:
		lpProp->Value.b = (Value == Py_True);
		break;
	case PT_OBJECT:
		lpProp->Value.lpszA = NULL;
		break;
	case PT_SYSTIME:
		lpProp->Value.ft = Object_to_FILETIME(Value);
		break;
	case PT_CLSID:
		if (PyString_AsStringAndSize(Value, &lpstr, &size) < 0)
			return;
		if (size != sizeof(GUID)) {
			PyErr_Format(PyExc_TypeError, "PT_CLSID Value must be exactly %d bytes", (int)sizeof(GUID));
			break;
		}
		if (ulFlags == CONV_COPY_SHALLOW) {
			lpProp->Value.lpguid = (LPGUID)lpstr;
			break;
		}
		if (KAllocCopy(lpstr, sizeof(GUID), reinterpret_cast<void **>(&lpProp->Value.lpguid), lpBase) != hrSuccess)
			return;
		break;
	case PT_BINARY:
		if (PyString_AsStringAndSize(Value, &lpstr, &size) < 0)
			return;
		if (ulFlags == CONV_COPY_SHALLOW)
			lpProp->Value.bin.lpb = (LPBYTE)lpstr;
		else if (KAllocCopy(lpstr, size, reinterpret_cast<void **>(&lpProp->Value.bin.lpb), lpBase) != hrSuccess)
			return;
		lpProp->Value.bin.cb = size;
		break;
	case PT_SRESTRICTION:
		if (MAPIAllocateMore(sizeof(SRestriction), lpBase, (void **)&lpProp->Value.lpszA) != hrSuccess)
			return;
		Object_to_LPSRestriction(Value, (LPSRestriction)lpProp->Value.lpszA, lpBase);
		break;
	case PT_ACTIONS:
		if (MAPIAllocateMore(sizeof(ACTIONS), lpBase, (void **)&lpProp->Value.lpszA) != hrSuccess)
			return;
		Object_to_LPACTIONS(Value, (ACTIONS*)lpProp->Value.lpszA, lpBase);
		break;

#undef PT_MV_CASE
#define PT_MV_CASE(MVname,MVelem,As,Sub) \
	{ \
		Py_ssize_t len = PyObject_Size(Value); \
		pyobj_ptr iter(PyObject_GetIter(Value)); \
		int n = 0; \
		\
		if (len) { \
			if (MAPIAllocateMore(sizeof(*lpProp->Value.MV##MVname.lp##MVelem) * len, lpBase, (void **)&lpProp->Value.MV##MVname.lp##MVelem) != hrSuccess) \
				return; \
			do { \
				pyobj_ptr elem(PyIter_Next(iter)); \
				if (elem == nullptr) break; \
				Sub(lpProp->Value.MV##MVname.lp##MVelem[n]) = As(elem); \
				++n;												\
			} while (true); \
		}															\
		lpProp->Value.MV##MVname.cValues = n; \
		break; \
	}
	case PT_MV_SHORT:
		PT_MV_CASE(i,i,PyLong_AsLong,BASE)
	case PT_MV_LONG:
		PT_MV_CASE(l,l,PyLong_AsLong,BASE)
	case PT_MV_FLOAT:
		PT_MV_CASE(flt,flt,PyFloat_AsDouble,BASE)
	case PT_MV_DOUBLE:
		PT_MV_CASE(dbl,dbl,PyFloat_AsDouble,BASE)
	case PT_MV_CURRENCY:
		PT_MV_CASE(cur,cur,PyLong_AsINT64,INT64)
	case PT_MV_APPTIME:
		PT_MV_CASE(at,at,PyFloat_AsDouble,BASE)
	case PT_MV_LONGLONG:
		PT_MV_CASE(li,li,PyLong_AsINT64,QUADPART)
	case PT_MV_SYSTIME:
	{
		Py_ssize_t len = PyObject_Size(Value);
		pyobj_ptr iter(PyObject_GetIter(Value));
		int n = 0;

		if (MAPIAllocateMore(sizeof(SDateTimeArray) * len, lpBase, (void **)&lpProp->Value.MVft.lpft) != hrSuccess)
			return;
		do {
			pyobj_ptr elem(PyIter_Next(iter));
			if (elem == nullptr)
				break;
			lpProp->Value.MVft.lpft[n] = Object_to_FILETIME(elem);
			++n;
		} while (true);
		lpProp->Value.MVft.cValues = n;
		break;
	}
	case PT_MV_STRING8:
	{
		Py_ssize_t len = PyObject_Size(Value);
		pyobj_ptr iter(PyObject_GetIter(Value));
		int n = 0;

		if (MAPIAllocateMore(sizeof(*lpProp->Value.MVszA.lppszA) * len, lpBase, (LPVOID *)&lpProp->Value.MVszA.lppszA) != hrSuccess)
			return;
		do {
			pyobj_ptr elem(PyIter_Next(iter));
			if (elem == nullptr)
				break;
			if (ulFlags == CONV_COPY_SHALLOW)
				lpProp->Value.MVszA.lppszA[n] = PyString_AsString(elem);
			else if (PyString_AsStringAndSize(elem, &lpstr, &size) < 0 ||
			    KAllocCopy(lpstr, size + 1, reinterpret_cast<void **>(&lpProp->Value.MVszA.lppszA[n]), lpBase) != hrSuccess)
				return;
			++n;
		} while (true);
		lpProp->Value.MVszA.cValues = n;
		break;
	}
	case PT_MV_BINARY:
	{
		Py_ssize_t len = PyObject_Size(Value);
		pyobj_ptr iter(PyObject_GetIter(Value));
		int n = 0;

		if (MAPIAllocateMore(sizeof(SBinaryArray) * len, lpBase, (void **)&lpProp->Value.MVbin.lpbin) != hrSuccess)
			return;
		do {
			pyobj_ptr elem(PyIter_Next(iter));
			if (elem == nullptr)
				break;
			if (PyString_AsStringAndSize(elem, &lpstr, &size) < 0)
				return;
			if (ulFlags == CONV_COPY_SHALLOW)
				lpProp->Value.MVbin.lpbin[n].lpb = (LPBYTE)lpstr;
			else if (KAllocCopy(lpstr, size, reinterpret_cast<void **>(&lpProp->Value.MVbin.lpbin[n].lpb), lpBase) != hrSuccess)
				return;
			lpProp->Value.MVbin.lpbin[n].cb = size;
			++n;
		} while (true);
		lpProp->Value.MVbin.cValues = n;
		break;
	}
	case PT_MV_UNICODE:
	{
		Py_ssize_t len = PyObject_Size(Value);
		pyobj_ptr iter(PyObject_GetIter(Value));
		int n = 0;

		if (MAPIAllocateMore(sizeof(*lpProp->Value.MVszW.lppszW) * len, lpBase, (LPVOID *)&lpProp->Value.MVszW.lppszW) != hrSuccess)
			return;
		do {
			pyobj_ptr elem(PyIter_Next(iter));
			if (elem == nullptr)
				break;
			if (ulFlags == CONV_COPY_SHALLOW && NATIVE_UNICODE)
				lpProp->Value.MVszW.lppszW[n] = (WCHAR*)PyUnicode_AsUnicode(elem);
			else
				CopyPyUnicode(&lpProp->Value.MVszW.lppszW[n], Value, lpBase);
			++n;
		} while (true);
		lpProp->Value.MVszW.cValues = n;
		break;
	}
	case PT_MV_CLSID:
	{
		Py_ssize_t len = PyObject_Size(Value);
		pyobj_ptr iter(PyObject_GetIter(Value));
		int n = 0;
		char *guid;

		if (MAPIAllocateMore(sizeof(GUID) * len, lpBase, (void **)&lpProp->Value.MVguid.lpguid) != hrSuccess)
			return;
		do {
			pyobj_ptr elem(PyIter_Next(iter));
			if (elem == nullptr)
				break;
			if (PyString_AsStringAndSize(elem, &guid, &size) < 0)
				return;
			if (size != sizeof(GUID)) {
				PyErr_Format(PyExc_TypeError, "PT_CLSID Value must be exactly %d bytes", (int)sizeof(GUID));
				break;
			}
			memcpy(&lpProp->Value.MVguid.lpguid[n], guid, size);
			++n;
		} while (true);
		lpProp->Value.MVguid.cValues = n;
		break;
	}
	default:
		PyErr_Format(PyExc_TypeError, "ulPropTag has unknown type %x", PROP_TYPE(lpProp->ulPropTag));
		break;
	}
}

void Object_to_LPSPropValue(PyObject *object, SPropValue *prop,
    ULONG flags, void *base)
{
	Object_to_p_SPropValue(object, prop, flags, base);
}

SPropValue *Object_to_p_SPropValue(PyObject *object, ULONG ulFlags,
    void *lpBase)
{
	LPSPropValue lpProp = NULL;

	if (MAPIAllocateMore(sizeof(SPropValue), lpBase, reinterpret_cast<void **>(&lpProp)) != hrSuccess)
		return NULL;
	if (lpBase == nullptr)
		lpBase = lpProp;
	Object_to_LPSPropValue(object, lpProp, ulFlags, lpBase);

	if (!PyErr_Occurred())
		return lpProp;
	if (!lpBase)
		MAPIFreeBuffer(lpProp);
	return NULL;
}

SPropValue *Object_to_LPSPropValue(PyObject *object, ULONG flags, void *base)
{
	return Object_to_p_SPropValue(object, flags, base);
}

SPropValue *List_to_p_SPropValue(PyObject *object, ULONG *cValues,
    ULONG ulFlags, void *lpBase)
{
	Py_ssize_t size = 0;
	LPSPropValue lpProps = NULL;
	LPSPropValue lpResult = NULL;
	int i = 0;

	if(object == Py_None) {
		*cValues = 0;
		return NULL;
	}
	auto laters = make_scope_success([&]() {
		if (PyErr_Occurred() && lpBase == nullptr)
			MAPIFreeBuffer(lpProps);
	});

	pyobj_ptr iter(PyObject_GetIter(object));
	if(!iter)
		return lpResult;

	size = PyObject_Size(object);

	if (MAPIAllocateMore(sizeof(SPropValue)*size, lpBase, reinterpret_cast<void**>(&lpProps)) != hrSuccess)
		return lpResult;

	memset(lpProps, 0, sizeof(SPropValue)*size);
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		Object_to_LPSPropValue(elem, &lpProps[i], ulFlags, lpBase != nullptr? lpBase : lpProps);
		if(PyErr_Occurred())
			return lpResult;
		++i;
	} while (true);
	lpResult = lpProps;
	*cValues = size;

	return lpResult;
}

SPropValue *List_to_LPSPropValue(PyObject *object, ULONG *pvals,
    ULONG flags, void *base)
{
	return List_to_p_SPropValue(object, pvals, flags, base);
}

template <typename T>
static typename T::pointer retval_or_null(T &retval) {
	if (PyErr_Occurred())
		return nullptr;
	return retval.release();
}

SPropTagArray *List_to_p_SPropTagArray(PyObject *object, ULONG /*ulFlags*/)
{
	pyobj_ptr iter;
	Py_ssize_t len = 0;
	memory_ptr<SPropTagArray> lpPropTagArray;
	int n = 0;

	if(object == Py_None)
		return NULL;

	len = PyObject_Length(object);
	if(len < 0) {
		PyErr_Format(PyExc_TypeError, "Invalid list passed as property list");
		return retval_or_null(lpPropTagArray);
	}
	if (MAPIAllocateBuffer(CbNewSPropTagArray(len), &~lpPropTagArray) != hrSuccess)
		return retval_or_null(lpPropTagArray);
	iter.reset(PyObject_GetIter(object));
	if(iter == NULL)
		return retval_or_null(lpPropTagArray);
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		lpPropTagArray->aulPropTag[n] = (ULONG)PyLong_AsUnsignedLong(elem);
		++n;
	} while (true);
	lpPropTagArray->cValues = n;

	return retval_or_null(lpPropTagArray);
}

PyObject *List_from_SPropTagArray(const SPropTagArray *lpPropTagArray)
{
	if(lpPropTagArray == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < lpPropTagArray->cValues; ++i) {
		pyobj_ptr elem(PyLong_FromUnsignedLong(lpPropTagArray->aulPropTag[i]));
		PyList_Append(list, elem);
		if(PyErr_Occurred())
			return nullptr;
	}
	return list.release();
}

SPropTagArray *List_to_LPSPropTagArray(PyObject *obj, ULONG flags)
{
	return List_to_p_SPropTagArray(obj, flags);
}

PyObject *List_from_LPSPropTagArray(const SPropTagArray *a)
{
	return List_from_SPropTagArray(a);
}

void Object_to_LPSRestriction(PyObject *object, LPSRestriction lpsRestriction, void *lpBase)
{
	pyobj_ptr iter;

	if(lpBase == NULL)
		lpBase = lpsRestriction;
	pyobj_ptr rt(PyObject_GetAttrString(object, "rt"));
	if(!rt) {
		PyErr_SetString(PyExc_RuntimeError, "rt (type) missing for restriction");
		return;
	}

	lpsRestriction->rt = (ULONG)PyLong_AsUnsignedLong(rt);

	switch(lpsRestriction->rt) {
	case RES_AND:
	case RES_OR: {
		pyobj_ptr sub(PyObject_GetAttrString(object, "lpRes"));
		if(!sub) {
			PyErr_SetString(PyExc_RuntimeError, "lpRes missing for restriction");
			return;
		}
		Py_ssize_t len = PyObject_Length(sub);

		// Handle RES_AND and RES_OR the same since they are binary-compatible
		if (MAPIAllocateMore(sizeof(SRestriction) * len, lpBase, (void **)&lpsRestriction->res.resAnd.lpRes) != hrSuccess) {
			PyErr_SetString(PyExc_RuntimeError, "Out of memory");
			return;
		}
		iter.reset(PyObject_GetIter(sub));
		if(iter == NULL)
			return;

		int n = 0;
		do {
			pyobj_ptr elem(PyIter_Next(iter));
			if (elem == nullptr)
				break;
			Object_to_LPSRestriction(elem, &lpsRestriction->res.resAnd.lpRes[n], lpBase);

			if(PyErr_Occurred())
				return;
			++n;
		} while (true);
		lpsRestriction->res.resAnd.cRes = n;
		break;
	}
	case RES_NOT: {
		pyobj_ptr sub(PyObject_GetAttrString(object, "lpRes"));
		if(!sub) {
			PyErr_SetString(PyExc_RuntimeError, "lpRes missing for restriction");
			return;
		}

		if (MAPIAllocateMore(sizeof(SRestriction), lpBase, (void **)&lpsRestriction->res.resNot.lpRes) != hrSuccess) {
			PyErr_SetString(PyExc_RuntimeError, "Out of memory");
			return;
		}

		Object_to_LPSRestriction(sub, lpsRestriction->res.resNot.lpRes, lpBase);

		if(PyErr_Occurred())
			return;
		break;
	}
	case RES_CONTENT: {
		pyobj_ptr ulFuzzyLevel(PyObject_GetAttrString(object, "ulFuzzyLevel"));
		pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulPropTag"));
		pyobj_ptr sub(PyObject_GetAttrString(object, "lpProp"));
		if(!ulFuzzyLevel || ! ulPropTag || !sub) {
			PyErr_SetString(PyExc_RuntimeError, "ulFuzzyLevel, ulPropTag or lpProp missing for RES_CONTENT restriction");
			return;
		}

		lpsRestriction->res.resContent.ulFuzzyLevel = PyLong_AsUnsignedLong(ulFuzzyLevel);
		lpsRestriction->res.resContent.ulPropTag = PyLong_AsUnsignedLong(ulPropTag);
		lpsRestriction->res.resContent.lpProp = Object_to_LPSPropValue(sub, CONV_COPY_SHALLOW, lpBase);
		break;
	}
	case RES_PROPERTY: {
		pyobj_ptr relop(PyObject_GetAttrString(object, "relop"));
		pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulPropTag"));
		pyobj_ptr sub(PyObject_GetAttrString(object, "lpProp"));
		if(!relop || !ulPropTag || !sub) {
			PyErr_SetString(PyExc_RuntimeError, "relop, ulPropTag or lpProp missing for RES_PROPERTY restriction");
			return;
		}

		lpsRestriction->res.resProperty.relop = PyLong_AsUnsignedLong(relop);
		lpsRestriction->res.resProperty.ulPropTag = PyLong_AsUnsignedLong(ulPropTag);
		lpsRestriction->res.resProperty.lpProp = Object_to_LPSPropValue(sub, CONV_COPY_SHALLOW, lpBase);
		break;
	}
	case RES_COMPAREPROPS: {
		pyobj_ptr relop(PyObject_GetAttrString(object, "relop"));
		pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulPropTag1"));
		pyobj_ptr ulPropTag2(PyObject_GetAttrString(object, "ulPropTag2"));
		if(!relop || !ulPropTag || !ulPropTag2) {
			PyErr_SetString(PyExc_RuntimeError, "relop, ulPropTag1 or ulPropTag2 missing for RES_COMPAREPROPS restriction");
			return;
		}

		lpsRestriction->res.resCompareProps.relop = PyLong_AsUnsignedLong(relop);
		lpsRestriction->res.resCompareProps.ulPropTag1 = PyLong_AsUnsignedLong(ulPropTag);
		lpsRestriction->res.resCompareProps.ulPropTag2 = PyLong_AsUnsignedLong(ulPropTag2);
		break;
	}
	case RES_BITMASK: {
		pyobj_ptr relop(PyObject_GetAttrString(object, "relBMR"));
		pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulPropTag"));
		pyobj_ptr ulMask(PyObject_GetAttrString(object, "ulMask"));
		if(!relop || !ulPropTag || !ulMask) {
			PyErr_SetString(PyExc_RuntimeError, "relBMR, ulPropTag or ulMask missing for RES_BITMASK restriction");
			return;
		}

		lpsRestriction->res.resBitMask.relBMR = PyLong_AsUnsignedLong(relop);
		lpsRestriction->res.resBitMask.ulPropTag = PyLong_AsUnsignedLong(ulPropTag);
		lpsRestriction->res.resBitMask.ulMask = PyLong_AsUnsignedLong(ulMask);
		break;
	}
	case RES_SIZE: {
		pyobj_ptr relop(PyObject_GetAttrString(object, "relop"));
		pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulPropTag"));
		pyobj_ptr cb(PyObject_GetAttrString(object, "cb"));
		if(!relop || !ulPropTag || !cb) {
			PyErr_SetString(PyExc_RuntimeError, "relop, ulPropTag or cb missing from RES_SIZE restriction");
			return;
		}

		lpsRestriction->res.resSize.relop = PyLong_AsUnsignedLong(relop);
		lpsRestriction->res.resSize.ulPropTag = PyLong_AsUnsignedLong(ulPropTag);
		lpsRestriction->res.resSize.cb = PyLong_AsUnsignedLong(cb);
		break;
	}
	case RES_EXIST: {
		pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulPropTag"));
		if(!ulPropTag) {
			PyErr_SetString(PyExc_RuntimeError, "ulPropTag missing from RES_EXIST restriction");
			return;
		}

		lpsRestriction->res.resExist.ulPropTag = PyLong_AsUnsignedLong(ulPropTag);
		break;
	}
	case RES_SUBRESTRICTION: {
		pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulSubObject"));
		pyobj_ptr sub(PyObject_GetAttrString(object, "lpRes"));
		if(!ulPropTag || !sub) {
			PyErr_SetString(PyExc_RuntimeError, "ulSubObject or lpRes missing from RES_SUBRESTRICTION restriction");
			return;
		}

		lpsRestriction->res.resSub.ulSubObject = PyLong_AsUnsignedLong(ulPropTag);
		if (MAPIAllocateMore(sizeof(SRestriction), lpBase, (void **)&lpsRestriction->res.resSub.lpRes) != hrSuccess) {
			PyErr_SetString(PyExc_RuntimeError, "Out of memory");
			return;
		}
		Object_to_LPSRestriction(sub, lpsRestriction->res.resSub.lpRes, lpBase);

		if(PyErr_Occurred())
			return;
		break;
	}
	case RES_COMMENT: {
		pyobj_ptr lpProp(PyObject_GetAttrString(object, "lpProp"));
		pyobj_ptr sub(PyObject_GetAttrString(object, "lpRes"));
		if(!lpProp || !sub) {
			PyErr_SetString(PyExc_RuntimeError, "lpProp or sub missing from RES_COMMENT restriction");
			return;
		}

		if (MAPIAllocateMore(sizeof(SRestriction), lpBase, (void **)&lpsRestriction->res.resComment.lpRes) != hrSuccess) {
			PyErr_SetString(PyExc_RuntimeError, "Out of memory");
			return;
		}
		
		Object_to_LPSRestriction(sub, lpsRestriction->res.resComment.lpRes, lpBase);

		if(PyErr_Occurred())
			return;
		lpsRestriction->res.resComment.lpProp = List_to_LPSPropValue(lpProp, &lpsRestriction->res.resComment.cValues, CONV_COPY_SHALLOW, lpBase);
		break;
	}
	default:
		PyErr_Format(PyExc_RuntimeError, "Bad restriction type %d", lpsRestriction->rt);
		return;
	}
}

SRestriction *Object_to_p_SRestriction(PyObject *object, void *lpBase)
{
	LPSRestriction lpRestriction = NULL;

	if(object == Py_None)
		return NULL;

	if (MAPIAllocateBuffer(sizeof(SRestriction), (void **)&lpRestriction) != hrSuccess)
		return NULL;

	Object_to_LPSRestriction(object, lpRestriction);

	if(PyErr_Occurred()) {
		MAPIFreeBuffer(lpRestriction);
		return NULL;
	}
	return lpRestriction;
}

SRestriction *Object_to_LPSRestriction(PyObject *obj, void *base)
{
	return Object_to_p_SRestriction(obj, base);
}

PyObject *Object_from_SRestriction(const SRestriction *lpsRestriction)
{
	pyobj_ptr result;
	if (lpsRestriction == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	switch(lpsRestriction->rt) {
	case RES_AND:
	case RES_OR: {
		pyobj_ptr subs(PyList_New(0));
		for (ULONG i = 0; i < lpsRestriction->res.resAnd.cRes; ++i) {
			pyobj_ptr sub(Object_from_LPSRestriction(lpsRestriction->res.resAnd.lpRes + i));
			if (!sub)
				return nullptr;
			PyList_Append(subs, sub);
		}

		if (lpsRestriction->rt == RES_AND)
			result.reset(PyObject_CallFunction(PyTypeSAndRestriction, "O", subs.get()));
		else
			result.reset(PyObject_CallFunction(PyTypeSOrRestriction, "O", subs.get()));
		break;
	}
	case RES_NOT: {
		pyobj_ptr sub(Object_from_LPSRestriction(lpsRestriction->res.resNot.lpRes));
		if(!sub)
			return nullptr;
		result.reset(PyObject_CallFunction(PyTypeSNotRestriction, "O", sub.get()));
		break;
	}
	case RES_CONTENT: {
		pyobj_ptr propval(Object_from_LPSPropValue(lpsRestriction->res.resContent.lpProp));
		if (!propval)
			return nullptr;
		result.reset(PyObject_CallFunction(PyTypeSContentRestriction, "kkO", lpsRestriction->res.resContent.ulFuzzyLevel, lpsRestriction->res.resContent.ulPropTag, propval.get()));
		break;
	}
	case RES_PROPERTY: {
		pyobj_ptr propval(Object_from_LPSPropValue(lpsRestriction->res.resProperty.lpProp));
		if (!propval)
			return nullptr;
		result.reset(PyObject_CallFunction(PyTypeSPropertyRestriction, "kkO", lpsRestriction->res.resProperty.relop, lpsRestriction->res.resProperty.ulPropTag, propval.get()));
		break;
	}
	case RES_COMPAREPROPS:
		result.reset(PyObject_CallFunction(PyTypeSComparePropsRestriction, "kkk", lpsRestriction->res.resCompareProps.relop, lpsRestriction->res.resCompareProps.ulPropTag1, lpsRestriction->res.resCompareProps.ulPropTag2));
		break;

	case RES_BITMASK:
		result.reset(PyObject_CallFunction(PyTypeSBitMaskRestriction, "kkk", lpsRestriction->res.resBitMask.relBMR, lpsRestriction->res.resBitMask.ulPropTag, lpsRestriction->res.resBitMask.ulMask));
		break;

	case RES_SIZE:
		result.reset(PyObject_CallFunction(PyTypeSSizeRestriction, "kkk", lpsRestriction->res.resSize.relop, lpsRestriction->res.resSize.ulPropTag, lpsRestriction->res.resSize.cb));
		break;

	case RES_EXIST:
		result.reset(PyObject_CallFunction(PyTypeSExistRestriction, "k", lpsRestriction->res.resExist.ulPropTag));
		break;

	case RES_SUBRESTRICTION: {
		pyobj_ptr sub(Object_from_LPSRestriction(lpsRestriction->res.resSub.lpRes));
		if (!sub)
			return nullptr;
		result.reset(PyObject_CallFunction(PyTypeSSubRestriction, "kO", lpsRestriction->res.resSub.ulSubObject, sub.get()));
		break;
	}
	case RES_COMMENT: {
		pyobj_ptr sub(Object_from_LPSRestriction(lpsRestriction->res.resComment.lpRes));
		if (!sub)
			return nullptr;
		pyobj_ptr proplist(List_from_LPSPropValue(lpsRestriction->res.resComment.lpProp, lpsRestriction->res.resComment.cValues));
		if (!proplist)
			return nullptr;
		result.reset(PyObject_CallFunction(PyTypeSCommentRestriction, "OO", sub.get(), proplist.get()));
		break;
	}
	default:
		PyErr_Format(PyExc_RuntimeError, "Bad restriction type %d", lpsRestriction->rt);
		return nullptr;
	}
	return result.release();
}

PyObject *Object_from_LPSRestriction(const SRestriction *r)
{
	return Object_from_SRestriction(r);
}

PyObject *		Object_from_LPACTION(LPACTION lpAction)
{
	PyObject *result = NULL;
	PyObject *act = NULL;

	if (lpAction == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	switch(lpAction->acttype) {
	case OP_MOVE:
	case OP_COPY:
#if PY_VERSION_HEX >= 0x03000000	// 3.0.0
		act = PyObject_CallFunction(PyTypeActMoveCopy, "y#y#",
#else
		act = PyObject_CallFunction(PyTypeActMoveCopy, "s#s#",
#endif
									lpAction->actMoveCopy.lpStoreEntryId, lpAction->actMoveCopy.cbStoreEntryId,
									lpAction->actMoveCopy.lpFldEntryId, lpAction->actMoveCopy.cbFldEntryId);
		break;
	case OP_REPLY:
	case OP_OOF_REPLY:
#if PY_VERSION_HEX >= 0x03000000	// 3.0.0
		act = PyObject_CallFunction(PyTypeActReply, "y#y#",
#else
		act = PyObject_CallFunction(PyTypeActReply, "s#s#",
#endif
									lpAction->actReply.lpEntryId, lpAction->actReply.cbEntryId,
									&lpAction->actReply.guidReplyTemplate, sizeof(GUID));
		break;
	case OP_DEFER_ACTION:
#if PY_VERSION_HEX >= 0x03000000	// 3.0.0
		act = PyObject_CallFunction(PyTypeActDeferAction, "y#",
#else
		act = PyObject_CallFunction(PyTypeActDeferAction, "s#",
#endif
									lpAction->actDeferAction.pbData, lpAction->actDeferAction.cbData);
		break;
	case OP_BOUNCE:
		act = PyObject_CallFunction(PyTypeActBounce, "l", lpAction->scBounceCode);
		break;
	case OP_FORWARD:
	case OP_DELEGATE:
		act = PyObject_CallFunction(PyTypeActFwdDelegate, "O", List_from_LPADRLIST(lpAction->lpadrlist));
		break;
	case OP_TAG:
		act = PyObject_CallFunction(PyTypeActTag, "O", Object_from_LPSPropValue(&lpAction->propTag));
		break;
	case OP_DELETE:
	case OP_MARK_AS_READ:
		act = Py_None;
		Py_INCREF(Py_None);
		break;
	};

	// restriction and proptype are always NULL
	Py_INCREF(Py_None);
	Py_INCREF(Py_None);
	result = PyObject_CallFunction(PyTypeAction, "llOOlO", lpAction->acttype, lpAction->ulActionFlavor, Py_None, Py_None, lpAction->ulFlags, act);

	return result;
}

PyObject *		Object_from_LPACTIONS(ACTIONS *lpsActions)
{
	if (lpsActions == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	pyobj_ptr result, subs(PyList_New(0));
	for (UINT i = 0; i < lpsActions->cActions; ++i) {
		pyobj_ptr sub(Object_from_LPACTION(&lpsActions->lpAction[i]));
		if (!sub)
			return nullptr;
		PyList_Append(subs, sub);
	}
	result.reset(PyObject_CallFunction(PyTypeACTIONS, "lO", lpsActions->ulVersion, subs.get()));
	return result.release();
}

void Object_to_LPACTION(PyObject *object, ACTION *lpAction, void *lpBase)
{
	pyobj_ptr poActType(PyObject_GetAttrString(object, "acttype"));
	pyobj_ptr poActionFlavor(PyObject_GetAttrString(object, "ulActionFlavor"));
	pyobj_ptr poRes(PyObject_GetAttrString(object, "lpRes"));
	pyobj_ptr poPropTagArray(PyObject_GetAttrString(object, "lpPropTagArray"));
	pyobj_ptr poFlags(PyObject_GetAttrString(object, "ulFlags"));
	pyobj_ptr poActObject(PyObject_GetAttrString(object, "actobj"));

	lpAction->acttype = (ACTTYPE)PyLong_AsUnsignedLong(poActType);
	lpAction->ulActionFlavor = PyLong_AsUnsignedLong(poActionFlavor);
	// @todo convert (unused) restriction and proptagarray
	lpAction->lpRes = NULL;
	lpAction->lpPropTagArray = NULL;
	lpAction->ulFlags = PyLong_AsUnsignedLong(poFlags);
	lpAction->dwAlignPad = 0;
	switch (lpAction->acttype) {
	case OP_MOVE:
	case OP_COPY:
	{
		pyobj_ptr poStore(PyObject_GetAttrString(poActObject, "StoreEntryId"));
		pyobj_ptr poFolder(PyObject_GetAttrString(poActObject, "FldEntryId"));
		Py_ssize_t size;
		if (PyString_AsStringAndSize(poStore, reinterpret_cast<char **>(&lpAction->actMoveCopy.lpStoreEntryId), &size) < 0)
			break;
		lpAction->actMoveCopy.cbStoreEntryId = size;
		if (PyString_AsStringAndSize(poFolder, reinterpret_cast<char **>(&lpAction->actMoveCopy.lpFldEntryId), &size) < 0)
			break;
		lpAction->actMoveCopy.cbFldEntryId = size;
		break;
	}
	case OP_REPLY:
	case OP_OOF_REPLY:
	{
		pyobj_ptr poEntryId(PyObject_GetAttrString(poActObject, "EntryId"));
		pyobj_ptr poGuid(PyObject_GetAttrString(poActObject, "guidReplyTemplate"));
		char *ptr;
		Py_ssize_t size;
		if (PyString_AsStringAndSize(poEntryId, reinterpret_cast<char **>(&lpAction->actReply.lpEntryId), &size) < 0)
			break;
		lpAction->actReply.cbEntryId = size;
		if (PyString_AsStringAndSize(poGuid, &ptr, &size) < 0)
			break;
		if (size == sizeof(GUID))
			memcpy(&lpAction->actReply.guidReplyTemplate, ptr, size);
		else
			memset(&lpAction->actReply.guidReplyTemplate, 0, sizeof(GUID));
		break;
	}
	case OP_DEFER_ACTION:
	{
		pyobj_ptr poData(PyObject_GetAttrString(poActObject, "data"));
		Py_ssize_t size;
		if (PyString_AsStringAndSize(poData, reinterpret_cast<char **>(&lpAction->actDeferAction.pbData), &size) < 0)
			break;
		lpAction->actDeferAction.cbData = size;
		break;
	}
	case OP_BOUNCE:
	{
		pyobj_ptr poBounce(PyObject_GetAttrString(poActObject, "scBounceCode"));
		lpAction->scBounceCode = PyLong_AsUnsignedLong(poBounce);
		break;
	}
	case OP_FORWARD:
	case OP_DELEGATE:
	{
		pyobj_ptr poAdrList(PyObject_GetAttrString(poActObject, "lpadrlist"));
		// @todo fix memleak
		lpAction->lpadrlist = List_to_LPADRLIST(poAdrList, CONV_COPY_SHALLOW, lpBase);
		break;
	}
	case OP_TAG:
	{
		pyobj_ptr poPropTag(PyObject_GetAttrString(poActObject, "propTag"));
		Object_to_LPSPropValue(poPropTag, &lpAction->propTag, CONV_COPY_SHALLOW, lpBase);
		break;
	}
	case OP_DELETE:
	case OP_MARK_AS_READ:
		break;
	}
}

void Object_to_LPACTIONS(PyObject *object, ACTIONS *lpActions, void *lpBase)
{
	HRESULT hr = hrSuccess;
	pyobj_ptr poVersion, poAction, iter;
	Py_ssize_t len = 0;
	unsigned int i = 0;

	if(object == Py_None)
		return;
	if (lpBase == NULL)
		lpBase = lpActions;
	poVersion.reset(PyObject_GetAttrString(object, "ulVersion"));
	poAction.reset(PyObject_GetAttrString(object, "lpAction"));
	if(!poVersion || !poAction) {
		PyErr_SetString(PyExc_RuntimeError, "Missing ulVersion or lpAction for ACTIONS struct");
		return;
	}

	len = PyObject_Length(poAction);
	if (len == 0) {
		PyErr_SetString(PyExc_RuntimeError, "No actions found in ACTIONS struct");
		return;
	} else if (len == -1) {
		PyErr_SetString(PyExc_RuntimeError, "No action array found in ACTIONS struct");
		return;
	}

	hr = MAPIAllocateMore(sizeof(ACTION)*len, lpBase, (void**)&lpActions->lpAction);
	if (hr != hrSuccess) {
		PyErr_SetString(PyExc_RuntimeError, "Out of memory");
		return;
	}

	lpActions->ulVersion = PyLong_AsUnsignedLong(poVersion); // EDK_RULES_VERSION
	lpActions->cActions = len;
	iter.reset(PyObject_GetIter(poAction));
	if(iter == NULL)
		return;
	i = 0;
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		Object_to_LPACTION(elem, &lpActions->lpAction[i++], lpBase != nullptr? lpBase : lpActions);
	} while (true);
}

SSortOrderSet *Object_to_p_SSortOrderSet(PyObject *object)
{
	pyobj_ptr aSort, cCategories, cExpanded, iter;
	memory_ptr<SSortOrderSet> lpsSortOrderSet;
	Py_ssize_t len = 0;
	unsigned int i = 0;

	if(object == Py_None)
		return retval_or_null(lpsSortOrderSet);
	aSort.reset(PyObject_GetAttrString(object, "aSort"));
	cCategories.reset(PyObject_GetAttrString(object, "cCategories"));
	cExpanded.reset(PyObject_GetAttrString(object, "cExpanded"));
	if(!aSort || !cCategories || !cExpanded) {
		PyErr_SetString(PyExc_RuntimeError, "Missing aSort, cCategories or cExpanded for sort order");
		return retval_or_null(lpsSortOrderSet);
	}

	len = PyObject_Length(aSort);
	if(len < 0) {
		PyErr_SetString(PyExc_RuntimeError, "aSort is not a sequence");
		return retval_or_null(lpsSortOrderSet);
	}
	if (MAPIAllocateBuffer(CbNewSSortOrderSet(len), &~lpsSortOrderSet) != hrSuccess)
		return retval_or_null(lpsSortOrderSet);
	iter.reset(PyObject_GetIter(aSort));
	if(iter == NULL)
		return retval_or_null(lpsSortOrderSet);
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		pyobj_ptr ulOrder(PyObject_GetAttrString(elem, "ulOrder"));
		pyobj_ptr ulPropTag(PyObject_GetAttrString(elem, "ulPropTag"));
		if(!ulOrder || !ulPropTag) {
			PyErr_SetString(PyExc_RuntimeError, "ulOrder or ulPropTag missing for sort order");
			return retval_or_null(lpsSortOrderSet);
		}

		lpsSortOrderSet->aSort[i].ulOrder = PyLong_AsUnsignedLong(ulOrder);
		lpsSortOrderSet->aSort[i].ulPropTag = PyLong_AsUnsignedLong(ulPropTag);
		++i;
	} while (true);

	lpsSortOrderSet->cSorts = i;
	lpsSortOrderSet->cCategories = PyLong_AsUnsignedLong(cCategories);
	lpsSortOrderSet->cExpanded = PyLong_AsUnsignedLong(cExpanded);

	return retval_or_null(lpsSortOrderSet);
}

PyObject *Object_from_SSortOrderSet(const SSortOrderSet *lpSortOrderSet)
{
	if(lpSortOrderSet == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	pyobj_ptr result, sorts(PyList_New(0));
	for (unsigned int i = 0; i < lpSortOrderSet->cSorts; ++i) {
		pyobj_ptr sort(PyObject_CallFunction(PyTypeSSort, "(ll)", lpSortOrderSet->aSort[i].ulPropTag, lpSortOrderSet->aSort[i].ulOrder));
		if(PyErr_Occurred())
			return nullptr;
		PyList_Append(sorts,sort);
	}

	result.reset(PyObject_CallFunction(PyTypeSSortOrderSet, "(Oll)", sorts.get(), lpSortOrderSet->cCategories, lpSortOrderSet->cExpanded));
	return result.release();
}

PyObject *List_from_SRowSet(const SRowSet *lpRowSet)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < lpRowSet->cRows; ++i) {
		pyobj_ptr item(List_from_LPSPropValue(lpRowSet->aRow[i].lpProps, lpRowSet->aRow[i].cValues));
		if(PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

PyObject *List_from_LPSRowSet(const SRowSet *s)
{
	return List_from_SRowSet(s);
}

SRowSet *List_to_p_SRowSet(PyObject *list, ULONG ulFlags, void *lpBase)
{
	pyobj_ptr iter;
	Py_ssize_t len = 0;
	rowset_ptr lpsRowSet;
	int i = 0;

	if (list == Py_None)
		return retval_or_null(lpsRowSet);

	len = PyObject_Length(list);
	iter.reset(PyObject_GetIter(list));
	if(!iter)
		return retval_or_null(lpsRowSet);

	// Zero out the whole struct so that failures halfway don't leave the struct
	// in an uninitialized state for FreeProws()
	if (MAPIAllocateMore(CbNewSRowSet(len), lpBase, &~lpsRowSet) != hrSuccess)
		return retval_or_null(lpsRowSet);
	lpsRowSet->cRows = 0;
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		lpsRowSet->aRow[i].lpProps = List_to_LPSPropValue(elem, &lpsRowSet->aRow[i].cValues, ulFlags, lpBase);

		if(PyErr_Occurred())
			return nullptr;
		lpsRowSet->cRows = ++i;
	} while (true);

	return retval_or_null(lpsRowSet);
}

SRowSet *List_to_LPSRowSet(PyObject *obj, ULONG flags, void *lpBase)
{
	return List_to_p_SRowSet(obj, flags, lpBase);
}

ADRLIST *List_to_p_ADRLIST(PyObject *av, ULONG ulFlags, void *lpBase)
{
	// Binary compatible
	return (LPADRLIST) List_to_LPSRowSet(av, ulFlags, lpBase);
}

ADRLIST *List_to_LPADRLIST(PyObject *av, ULONG ulFlags, void *lpBase)
{
	// Binary compatible
	return (LPADRLIST) List_to_LPSRowSet(av, ulFlags, lpBase);
}

PyObject *List_from_ADRLIST(const ADRLIST *lpAdrList)
{
	// Binary compatible
	return List_from_LPSRowSet((LPSRowSet)lpAdrList);
}

PyObject *List_from_LPADRLIST(const ADRLIST *lpAdrList)
{
	// Binary compatible
	return List_from_LPSRowSet((LPSRowSet)lpAdrList);
}

PyObject *		Object_from_LPSPropProblem(LPSPropProblem lpProblem)
{
	return PyObject_CallFunction(PyTypeSPropProblem, "(lII)", lpProblem->ulIndex, lpProblem->ulPropTag, lpProblem->scode);
}

void	Object_to_LPSPropProblem(PyObject *object, LPSPropProblem lpProblem)
{
	pyobj_ptr scode(PyObject_GetAttrString(object, "scode"));
	pyobj_ptr ulIndex(PyObject_GetAttrString(object, "ulIndex"));
	pyobj_ptr ulPropTag(PyObject_GetAttrString(object, "ulPropTag"));

	lpProblem->scode = PyLong_AsUnsignedLong(scode);
	lpProblem->ulIndex = PyLong_AsUnsignedLong(ulIndex);
	lpProblem->ulPropTag = PyLong_AsUnsignedLong(ulPropTag);
}

PyObject *		List_from_LPSPropProblemArray(LPSPropProblemArray lpProblemArray)
{
	pyobj_ptr list;

	if(lpProblemArray == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	list.reset(PyList_New(0));
	for (unsigned int i = 0; i < lpProblemArray->cProblem; ++i) {
		pyobj_ptr elem(Object_from_LPSPropProblem(&lpProblemArray->aProblem[i]));
		if(PyErr_Occurred())
			return nullptr;
		PyList_Append(list, elem);
	}
	return list.release();
}

LPSPropProblemArray List_to_LPSPropProblemArray(PyObject *list, ULONG /*ulFlags*/)
{
	pyobj_ptr iter;
	Py_ssize_t len = 0;
	memory_ptr<SPropProblemArray> lpsProblems;
	int i = 0;

	if (list == Py_None)
		return retval_or_null(lpsProblems);

	len = PyObject_Length(list);
	iter.reset(PyObject_GetIter(list));
	if(!iter)
		return retval_or_null(lpsProblems);
	if (MAPIAllocateBuffer(CbNewSPropProblemArray(len), &~lpsProblems) != hrSuccess)
		return retval_or_null(lpsProblems);

	memset(lpsProblems, 0, CbNewSPropProblemArray(len));

	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		Object_to_LPSPropProblem(elem, &lpsProblems->aProblem[i]);

		if(PyErr_Occurred())
			return nullptr;
		++i;
	} while (true);
	lpsProblems->cProblem = i;

	return retval_or_null(lpsProblems);
}

PyObject * Object_from_LPMAPINAMEID(LPMAPINAMEID lpMAPINameId)
{
	PyObject *elem = NULL;
	if(lpMAPINameId == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	pyobj_ptr guid(PyString_FromStringAndSize(reinterpret_cast<char *>(lpMAPINameId->lpguid), sizeof(GUID)));
	if(lpMAPINameId->ulKind == MNID_ID)
		return PyObject_CallFunction(PyTypeMAPINAMEID, "(Oll)", guid.get(), MNID_ID, lpMAPINameId->Kind.lID);
	return PyObject_CallFunction(PyTypeMAPINAMEID, "(Olu)", guid.get(), MNID_STRING, lpMAPINameId->Kind.lpwstrName);
}

PyObject * List_from_LPMAPINAMEID(LPMAPINAMEID *lppMAPINameId, ULONG cNames)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < cNames; ++i) {
		pyobj_ptr elem(Object_from_LPMAPINAMEID(lppMAPINameId[i]));
		if(PyErr_Occurred())
			return nullptr;
		PyList_Append(list, elem);
	}
	return list.release();
}

void Object_to_LPMAPINAMEID(PyObject *elem, LPMAPINAMEID *lppName, void *lpBase)
{
	LPMAPINAMEID lpName = NULL;
	pyobj_ptr kind, id, guid;
	ULONG ulKind = 0;
	Py_ssize_t len = 0;

	auto laters = make_scope_success([&]() {
		if (PyErr_Occurred() && lpBase == nullptr)
			MAPIFreeBuffer(lpName);
	});

	if (MAPIAllocateMore(sizeof(MAPINAMEID), lpBase, (void **)&lpName) != hrSuccess) {
		PyErr_SetString(PyExc_RuntimeError, "Out of memory");
		return;
	}
	memset(lpName, 0, sizeof(MAPINAMEID));
	kind.reset(PyObject_GetAttrString(elem, "kind"));
	id.reset(PyObject_GetAttrString(elem, "id"));
	guid.reset(PyObject_GetAttrString(elem, "guid"));
	if(!guid || !id) {
		PyErr_SetString(PyExc_RuntimeError, "Missing id or guid on MAPINAMEID object");
		return;
	}

	if(!kind) {
		// Detect kind from type of 'id' parameter by first trying to use it as an int, then as string
		PyInt_AsLong(id);
		if(PyErr_Occurred()) {
			// Clear error
			PyErr_Clear();
			ulKind = MNID_STRING;
		} else {
			ulKind = MNID_ID;
		}
	} else {
		ulKind = PyInt_AsLong(kind);
	}

	lpName->ulKind = ulKind;
	if(ulKind == MNID_ID) {
		lpName->Kind.lID = PyInt_AsLong(id);
	} else {
		if(!PyUnicode_Check(id)) {
			PyErr_SetString(PyExc_RuntimeError, "Must pass unicode string for MNID_STRING ID part of MAPINAMEID");
			return;
		}
		
		CopyPyUnicode(&lpName->Kind.lpwstrName, id, lpBase);
	}

	if (PyString_AsStringAndSize(guid, reinterpret_cast<char **>(&lpName->lpguid), &len) == -1)
		return;
	if(len != sizeof(GUID)) {
		PyErr_Format(PyExc_RuntimeError, "GUID parameter of MAPINAMEID must be exactly %d bytes", (int)sizeof(GUID));
		return;
	}

	*lppName = lpName;
}

LPMAPINAMEID *	List_to_p_LPMAPINAMEID(PyObject *list, ULONG *lpcNames, ULONG /*ulFlags*/)
{
	memory_ptr<MAPINAMEID *> lpNames;
	Py_ssize_t len = 0;
	unsigned int i = 0;

	pyobj_ptr iter(PyObject_GetIter(list));
	if(!iter)
		return retval_or_null(lpNames);

	len = PyObject_Length(list);
	if (MAPIAllocateBuffer(sizeof(LPMAPINAMEID) * len, &~lpNames) != hrSuccess)
		return retval_or_null(lpNames);

	memset(lpNames, 0, sizeof(LPMAPINAMEID) * len);
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		Object_to_LPMAPINAMEID(elem, &lpNames[i], lpNames);

		if(PyErr_Occurred())
			return nullptr;
		++i;
	} while (true);
	*lpcNames = i;

	return retval_or_null(lpNames);
}

ENTRYLIST *List_to_p_ENTRYLIST(PyObject *list)
{
	memory_ptr<ENTRYLIST> lpEntryList;
	Py_ssize_t len = 0;
	unsigned int i = 0;

	if(list == Py_None)
		return NULL;
	pyobj_ptr iter(PyObject_GetIter(list));
	if(!iter)
		return retval_or_null(lpEntryList);

	len = PyObject_Length(list);
	if (MAPIAllocateBuffer(sizeof(*lpEntryList), &~lpEntryList) != hrSuccess)
		return retval_or_null(lpEntryList);

	if (MAPIAllocateMore(len * sizeof *lpEntryList->lpbin, lpEntryList, (void**)&lpEntryList->lpbin) != hrSuccess)
		return retval_or_null(lpEntryList);

	lpEntryList->cValues = len;
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		char *ptr;
		Py_ssize_t strlen;

		if (PyString_AsStringAndSize(elem, &ptr, &strlen) == -1 ||
		    PyErr_Occurred())
			return retval_or_null(lpEntryList);

		lpEntryList->lpbin[i].cb = strlen;
		if (KAllocCopy(ptr, strlen, reinterpret_cast<void **>(&lpEntryList->lpbin[i].lpb), lpEntryList) != hrSuccess)
			return retval_or_null(lpEntryList);
		++i;
	} while (true);

	return retval_or_null(lpEntryList);
}

ENTRYLIST *List_to_LPENTRYLIST(PyObject *list)
{
	return List_to_p_ENTRYLIST(list);
}

PyObject *		List_from_LPENTRYLIST(LPENTRYLIST lpEntryList)
{
	pyobj_ptr list(PyList_New(0));
	if (lpEntryList == nullptr)
		return list.release();
	for (unsigned int i = 0; i < lpEntryList->cValues; ++i) {
		pyobj_ptr elem(PyString_FromStringAndSize(reinterpret_cast<const char *>(lpEntryList->lpbin[i].lpb), lpEntryList->lpbin[i].cb));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, elem);
	}
	return list.release();
}

LPNOTIFICATION	List_to_LPNOTIFICATION(PyObject *, ULONG *lpcNotifs)
{
	return NULL;
}

PyObject *		List_from_LPNOTIFICATION(LPNOTIFICATION lpNotif, ULONG cNotifs)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < cNotifs; ++i) {
		pyobj_ptr item(Object_from_LPNOTIFICATION(&lpNotif[i]));
		if(PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

PyObject *		Object_from_LPNOTIFICATION(NOTIFICATION *lpNotif)
{
	PyObject *elem = NULL;
	if(lpNotif == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	switch(lpNotif->ulEventType) {
	case fnevObjectCopied:
	case fnevObjectCreated:
	case fnevObjectDeleted:
	case fnevObjectModified:
	case fnevObjectMoved:
	case fnevSearchComplete: {
		pyobj_ptr proptags(List_from_LPSPropTagArray(lpNotif->info.obj.lpPropTagArray));
		if (!proptags)
			return NULL;
#if PY_VERSION_HEX >= 0x03000000	// 3.0.0
		elem = PyObject_CallFunction(PyTypeOBJECT_NOTIFICATION, "(ly#ly#y#y#O)",
#else
		elem = PyObject_CallFunction(PyTypeOBJECT_NOTIFICATION, "(ls#ls#s#s#O)",
#endif
			lpNotif->ulEventType,
			lpNotif->info.obj.lpEntryID, lpNotif->info.obj.cbEntryID,
			lpNotif->info.obj.ulObjType,
			lpNotif->info.obj.lpParentID, lpNotif->info.obj.cbParentID,
			lpNotif->info.obj.lpOldID, lpNotif->info.obj.cbOldID,
			lpNotif->info.obj.lpOldParentID, lpNotif->info.obj.cbOldParentID,
			proptags.get());
		break;
	}
	case fnevTableModified: {
		pyobj_ptr index(Object_from_LPSPropValue(&lpNotif->info.tab.propIndex));
		if (!index)
			return NULL;
		pyobj_ptr prior(Object_from_LPSPropValue(&lpNotif->info.tab.propPrior));
		if (!prior)
			return NULL;
		pyobj_ptr row(List_from_LPSPropValue(lpNotif->info.tab.row.lpProps, lpNotif->info.tab.row.cValues));
		if (!row)
			return NULL;
		elem = PyObject_CallFunction(PyTypeTABLE_NOTIFICATION, "(lIOOO)", lpNotif->info.tab.ulTableEvent, lpNotif->info.tab.hResult, index.get(), prior.get(), row.get());
		break;
	}
	case fnevNewMail:
#if PY_VERSION_HEX >= 0x03000000	// 3.0.0
		elem = PyObject_CallFunction(PyTypeNEWMAIL_NOTIFICATION, "(y#y#lsl)",
#else
		elem = PyObject_CallFunction(PyTypeNEWMAIL_NOTIFICATION, "(s#s#lsl)",
#endif
		        lpNotif->info.newmail.lpEntryID, lpNotif->info.newmail.cbEntryID,
			lpNotif->info.newmail.lpParentID, lpNotif->info.newmail.cbParentID,
			lpNotif->info.newmail.ulFlags,
			lpNotif->info.newmail.lpszMessageClass,
			lpNotif->info.newmail.ulMessageFlags);
		break;
	default:
		PyErr_Format(PyExc_RuntimeError, "Bad notification type %x", lpNotif->ulEventType);
		break;
	}
	return elem;
}

NOTIFICATION *	Object_to_LPNOTIFICATION(PyObject *obj)
{
	memory_ptr<NOTIFICATION> lpNotif;
	if(obj == Py_None)
		return NULL;
	if (MAPIAllocateBuffer(sizeof(NOTIFICATION), &~lpNotif) != hrSuccess)
		return nullptr;
	memset(lpNotif, 0, sizeof(NOTIFICATION));

	if (!PyObject_IsInstance(obj, PyTypeNEWMAIL_NOTIFICATION)) {
		PyErr_Format(PyExc_RuntimeError, "Bad object type %p", obj->ob_type);
		return PyErr_Occurred() ? nullptr : lpNotif.release();
	}
	lpNotif->ulEventType = fnevNewMail;
	Py_ssize_t size;
	pyobj_ptr oTmp(PyObject_GetAttrString(obj, "lpEntryID"));
	if (!oTmp) {
		PyErr_SetString(PyExc_RuntimeError, "lpEntryID missing for newmail notification");
		return retval_or_null(lpNotif);
	}
	if (oTmp != Py_None) {
		if (PyString_AsStringAndSize(oTmp, reinterpret_cast<char **>(&lpNotif->info.newmail.lpEntryID), &size) < 0)
			return retval_or_null(lpNotif);
		lpNotif->info.newmail.cbEntryID = size;
	}
	oTmp.reset(PyObject_GetAttrString(obj, "lpParentID"));
	if (!oTmp) {
		PyErr_SetString(PyExc_RuntimeError, "lpParentID missing for newmail notification");
		return retval_or_null(lpNotif);
	}
	if (oTmp != Py_None) {
		if (PyString_AsStringAndSize(oTmp, reinterpret_cast<char **>(&lpNotif->info.newmail.lpParentID), &size) < 0)
			return retval_or_null(lpNotif);
		lpNotif->info.newmail.cbParentID = size;
	}
	oTmp.reset(PyObject_GetAttrString(obj, "ulFlags"));
	if (!oTmp) {
		PyErr_SetString(PyExc_RuntimeError, "ulFlags missing for newmail notification");
		return retval_or_null(lpNotif);
	}
	if (oTmp != Py_None) {
		lpNotif->info.newmail.ulFlags = (ULONG)PyLong_AsUnsignedLong(oTmp);
	}
	oTmp.reset(PyObject_GetAttrString(obj, "ulMessageFlags"));
	if (!oTmp) {
		PyErr_SetString(PyExc_RuntimeError, "ulMessageFlags missing for newmail notification");
		return retval_or_null(lpNotif);
	}
	if (oTmp != Py_None) {
		lpNotif->info.newmail.ulMessageFlags = (ULONG)PyLong_AsUnsignedLong(oTmp);
	}
	// MessageClass
	oTmp.reset(PyObject_GetAttrString(obj, "lpszMessageClass"));
	if (!oTmp) {
		PyErr_SetString(PyExc_RuntimeError, "lpszMessageClass missing for newmail notification");
		return retval_or_null(lpNotif);
	}
	if (oTmp != Py_None) {
		if (lpNotif->info.newmail.ulFlags & MAPI_UNICODE)
		    CopyPyUnicode(&lpNotif->info.newmail.lpszMessageClass, oTmp, lpNotif);
		else if (PyString_AsStringAndSize(oTmp, reinterpret_cast<char **>(&lpNotif->info.newmail.lpszMessageClass), nullptr) == -1)
			return retval_or_null(lpNotif);
	}

	return retval_or_null(lpNotif);
}

LPFlagList		List_to_LPFlagList(PyObject *list)
{
	Py_ssize_t len = 0;
	memory_ptr<FlagList> lpList;
	int i = 0;
	pyobj_ptr iter(PyObject_GetIter(list));
	if(!iter)
		return retval_or_null(lpList);

	len = PyObject_Length(list);
	if (MAPIAllocateBuffer(CbNewFlagList(len), &~lpList) != hrSuccess)
		return retval_or_null(lpList);
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		lpList->ulFlag[i] = PyLong_AsUnsignedLong(elem);

		if(PyErr_Occurred())
			return nullptr;
		++i;
	} while (true);
	lpList->cFlags = i;

	return retval_or_null(lpList);
}

PyObject *		List_from_LPFlagList(LPFlagList lpFlags)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < lpFlags->cFlags; ++i) {
		pyobj_ptr elem(PyLong_FromUnsignedLong(lpFlags->ulFlag[i]));
		PyList_Append(list, elem);
	}
	return list.release();
}

PyObject *		Object_from_LPMAPIERROR(LPMAPIERROR lpMAPIError)
{
	Py_INCREF(Py_None);
	return Py_None;
}

LPMAPIERROR		Object_to_LPMAPIERROR(PyObject *)
{
	LPMAPIERROR	lpError = NULL;
	if (MAPIAllocateBuffer(sizeof(LPMAPIERROR), (LPVOID*)&lpError) == hrSuccess)
		memset(lpError, 0, sizeof(*lpError));
	return lpError;
}

LPREADSTATE		List_to_LPREADSTATE(PyObject *list, ULONG *lpcElements)
{
	Py_ssize_t len = 0;
	memory_ptr<READSTATE> lpList;
	int i = 0;
	pyobj_ptr iter(PyObject_GetIter(list));
	if(!iter)
		return retval_or_null(lpList);

	len = PyObject_Length(list);
	if (MAPIAllocateBuffer(len * sizeof(*lpList), &~lpList) != hrSuccess)
		return retval_or_null(lpList);

	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		HRESULT hr;

		pyobj_ptr sourcekey(PyObject_GetAttrString(elem, "SourceKey"));
		pyobj_ptr flags(PyObject_GetAttrString(elem, "ulFlags"));
		if (!sourcekey || !flags)
			continue;

		char *ptr = NULL;
		Py_ssize_t len = 0;

		lpList[i].ulFlags = PyLong_AsUnsignedLong(flags);
		if (PyErr_Occurred())
			return nullptr;

		if (PyString_AsStringAndSize(sourcekey, &ptr, &len) == -1 ||
		    PyErr_Occurred())
			return retval_or_null(lpList);
		hr = KAllocCopy(ptr, len, reinterpret_cast<void **>(&lpList[i].pbSourceKey), lpList);
		if (hr != hrSuccess) {
			PyErr_SetString(PyExc_RuntimeError, "Out of memory");
			return retval_or_null(lpList);
		}
		lpList[i].cbSourceKey = len;
		++i;
	} while (true);
	*lpcElements = len;

	return retval_or_null(lpList);
}

PyObject *		List_from_LPREADSTATE(LPREADSTATE lpReadState, ULONG cElements)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < cElements; ++i) {
		pyobj_ptr sourcekey(PyString_FromStringAndSize(reinterpret_cast<char *>(lpReadState[i].pbSourceKey), lpReadState[i].cbSourceKey));
		if (PyErr_Occurred())
			return nullptr;
		pyobj_ptr elem(PyObject_CallFunction(PyTypeREADSTATE, "(Ol)", sourcekey.get(), lpReadState[i].ulFlags));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, elem);
	}
	return list.release();
}

LPCIID			List_to_LPCIID(PyObject *list, ULONG *cInterfaces)
{
	Py_ssize_t len = 0;
	memory_ptr<IID> lpList;
	int i = 0;

	if(list == Py_None) {
		cInterfaces = 0;
		return NULL;
	}
	pyobj_ptr iter(PyObject_GetIter(list));
	if(!iter)
		return retval_or_null(lpList);

	len = PyObject_Length(list);
	if (MAPIAllocateBuffer(len * sizeof(*lpList), &~lpList) != hrSuccess)
		return retval_or_null(lpList);
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		char *ptr = NULL;
		Py_ssize_t strlen = 0;

		if (PyString_AsStringAndSize(elem, &ptr, &strlen) == -1 ||
		    PyErr_Occurred())
			return retval_or_null(lpList);

		if (strlen != sizeof(*lpList)) {
			PyErr_Format(PyExc_RuntimeError, "IID parameter must be exactly %d bytes", (int)sizeof(IID));
			return retval_or_null(lpList);
		}

		memcpy(&lpList[i], ptr, sizeof(*lpList));
		++i;
	} while (true);
	*cInterfaces = len;

	return retval_or_null(lpList);
}

PyObject *List_from_LPCIID(LPCIID iids, ULONG cElements)
{
	if (iids == NULL) {
		Py_INCREF(Py_None);
		return(Py_None);
	}

	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < cElements; ++i) {
		pyobj_ptr iid(PyString_FromStringAndSize(reinterpret_cast<const char *>(&iids[i]), sizeof(IID)));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, iid);
	}
	return list.release();
}

template<typename T> void
Object_to_MVPROPMAP(PyObject *elem, T *&lpObj, ULONG ulFlags)
{
	HRESULT hr = hrSuccess;
	PyObject *Item, *ListItem;
	int MVPropMapsSize = 0;

	/* Multi-Value PropMap support. */
	pyobj_ptr MVPropMaps(PyObject_GetAttrString(elem, "MVPropMap"));
	if (MVPropMaps == nullptr || !PyList_Check(MVPropMaps))
		return;
	MVPropMapsSize = PyList_Size(MVPropMaps);
	/* No PropMaps - bail out */
	if (MVPropMapsSize != 2) {
		PyErr_SetString(PyExc_TypeError, "MVPropMap should contain two entries");
		return;
	}

	/* If we have more mv props than the feature lists, adjust this value! */
	lpObj->sMVPropmap.cEntries = 2;
	hr = MAPIAllocateMore(sizeof(MVPROPMAPENTRY) * lpObj->sMVPropmap.cEntries, lpObj, reinterpret_cast<void **>(&lpObj->sMVPropmap.lpEntries));

	for (int i = 0; i < MVPropMapsSize; ++i) {
		Item = PyList_GetItem(MVPropMaps, i);
		pyobj_ptr PropID(PyObject_GetAttrString(Item, "ulPropId"));
		pyobj_ptr Values(PyObject_GetAttrString(Item, "Values"));

		if (PropID == NULL || Values == NULL || !PyList_Check(Values)) {
			PyErr_SetString(PyExc_TypeError, "ulPropId or Values is empty or values is not a list");
			return;
		}

		/* Set default struct entry to empty stub values */
		lpObj->sMVPropmap.lpEntries[i].ulPropId = PyLong_AsUnsignedLong(PropID);
		lpObj->sMVPropmap.lpEntries[i].cValues = 0;
		lpObj->sMVPropmap.lpEntries[i].lpszValues = NULL;

		//if ((PropID != NULL && PropID != Py_None) && (Values != NULL && Values != Py_None && PyList_Check(Values)))
		int ValuesLength = PyList_Size(Values);
		lpObj->sMVPropmap.lpEntries[i].cValues = ValuesLength;

		if (ValuesLength > 0) {
			hr = MAPIAllocateMore(sizeof(LPTSTR) * lpObj->sMVPropmap.lpEntries[i].cValues, lpObj, reinterpret_cast<void **>(&lpObj->sMVPropmap.lpEntries[i].lpszValues));
			if (hr != hrSuccess) {
				PyErr_SetString(PyExc_RuntimeError, "Out of memory");
				return;
			}
		}

		for (int j = 0; j < ValuesLength; ++j) {
			ListItem = PyList_GetItem(Values, j);

			if (ListItem == Py_None)
				continue;
			if ((ulFlags & MAPI_UNICODE) == 0)
				// XXX: meh, not sure what todo here. Maybe use process_conv_out??
				lpObj->sMVPropmap.lpEntries[i].lpszValues[j] = reinterpret_cast<TCHAR *>(PyString_AsString(ListItem));
			else
				CopyPyUnicode(&lpObj->sMVPropmap.lpEntries[i].lpszValues[j], ListItem, lpObj);
		}
	}
}

PyObject *Object_from_MVPROPMAP(MVPROPMAP propmap, ULONG ulFlags)
{
	/*
	 * Multi-Value PropMap support.
	 *
	 * This holds the enabled/disabled features of a user. It is
	 * represented as a list of PropMaps which contains multiple values. A
	 * Normal Propmap only contains one value, so we use a list to display
	 * these values.
	 *
	 * Note that the enabled/disabled MVPropMap is special since, for
	 * example, setting both PR_EC_ENABLED_FEATUES and
	 * PR_EC_DISALBED_FEATURES to an empty list will still set the features
	 * to either disabled or enabled according to the default set in the
	 * server configuration.
	 */
	pyobj_ptr MVProps(PyList_New(0));
	MVPROPMAP *lpMVPropmap = &propmap;
	for (unsigned int i = 0; i < lpMVPropmap->cEntries; ++i) {
		pyobj_ptr MVPropValues(PyList_New(0));

		// TODO support other types
		if(PROP_TYPE(lpMVPropmap->lpEntries[i].ulPropId) != PT_MV_UNICODE)
			continue;

		for (unsigned int j = 0; j < lpMVPropmap->lpEntries[i].cValues; ++j) {
			LPTSTR strval = lpMVPropmap->lpEntries[i].lpszValues[j];
			std::string str = reinterpret_cast<LPSTR>(strval);

			if (str.empty())
				continue;
			pyobj_ptr MVPropValue;
			if (ulFlags & MAPI_UNICODE)
				MVPropValue.reset(PyUnicode_FromWideChar(strval, wcslen(strval)));
			else
				MVPropValue.reset(PyString_FromStringAndSize(str.c_str(), str.length()));
			PyList_Append(MVPropValues, MVPropValue);
		}

		pyobj_ptr MVPropMap(PyObject_CallFunction(PyTypeMVPROPMAP, "(lO)", lpMVPropmap->lpEntries[i].ulPropId, MVPropValues.get()));
		PyList_Append(MVProps, MVPropMap);
	}
	return MVProps.release();
}

ECUSER *Object_to_LPECUSER(PyObject *elem, ULONG ulFlags)
{
	static conv_out_info<ECUSER> conv_info[] = {
		{conv_out_default<ECUSER, LPTSTR, &ECUSER::lpszUsername>, "Username"},
		{conv_out_default<ECUSER, LPTSTR, &ECUSER::lpszPassword>, "Password"},
		{conv_out_default<ECUSER, LPTSTR, &ECUSER::lpszMailAddress>, "Email"},
		{conv_out_default<ECUSER, LPTSTR, &ECUSER::lpszFullName>, "FullName"},
		{conv_out_default<ECUSER, LPTSTR, &ECUSER::lpszServername>, "Servername"},
		{conv_out_default<ECUSER, objectclass_t, &ECUSER::ulObjClass>, "Class"},
		{conv_out_default<ECUSER, unsigned int, &ECUSER::ulIsAdmin>, "IsAdmin"},
		{conv_out_default<ECUSER, unsigned int, &ECUSER::ulIsABHidden>, "IsHidden"},
		{conv_out_default<ECUSER, unsigned int, &ECUSER::ulCapacity>, "Capacity"},
		{conv_out_default<ECUSER, SBinary, &ECUSER::sUserId>, "UserID"},
	};

	HRESULT hr = hrSuccess;
	ECUSER *lpUser = NULL;

	if (elem == Py_None)
		return nullptr;

	hr = MAPIAllocateBuffer(sizeof *lpUser, (LPVOID*)&lpUser);
	if (hr != hrSuccess) {
		PyErr_SetString(PyExc_RuntimeError, "Out of memory");
		return nullptr;
	}
	memset(lpUser, 0, sizeof *lpUser);
	process_conv_out_array(lpUser, elem, conv_info, lpUser, ulFlags);
	Object_to_MVPROPMAP(elem, lpUser, ulFlags);

	if (PyErr_Occurred()) {
		MAPIFreeBuffer(lpUser);
		return nullptr;
	}

	return lpUser;
}

PyObject *Object_from_LPECUSER(ECUSER *lpUser, ULONG ulFlags)
{
	pyobj_ptr MVProps(Object_from_MVPROPMAP(lpUser->sMVPropmap, ulFlags));
	pyobj_ptr userid(PyBytes_FromStringAndSize(reinterpret_cast<const char *>(lpUser->sUserId.lpb), lpUser->sUserId.cb));

	if (ulFlags & MAPI_UNICODE)
		return PyObject_CallFunction(PyTypeECUser, "(uuuuuIIIIOO)", lpUser->lpszUsername, lpUser->lpszPassword, lpUser->lpszMailAddress, lpUser->lpszFullName, lpUser->lpszServername, lpUser->ulObjClass, lpUser->ulIsAdmin, lpUser->ulIsABHidden, lpUser->ulCapacity, userid.get(), MVProps.get());
	return PyObject_CallFunction(PyTypeECUser, "(sssssIIIIOO)", lpUser->lpszUsername, lpUser->lpszPassword, lpUser->lpszMailAddress, lpUser->lpszFullName, lpUser->lpszServername, lpUser->ulObjClass, lpUser->ulIsAdmin, lpUser->ulIsABHidden, lpUser->ulCapacity, userid.get(), MVProps.get());
}

PyObject *List_from_LPECUSER(ECUSER *lpUser, ULONG cElements, ULONG ulFlags)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < cElements; ++i) {
		pyobj_ptr item(Object_from_LPECUSER(&lpUser[i], ulFlags));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

ECGROUP *Object_to_LPECGROUP(PyObject *elem, ULONG ulFlags)
{
	static conv_out_info<ECGROUP> conv_info[] = {
		{conv_out_default<ECGROUP, LPTSTR, &ECGROUP::lpszGroupname>, "Groupname"},
		{conv_out_default<ECGROUP, LPTSTR, &ECGROUP::lpszFullname>, "Fullname"},
		{conv_out_default<ECGROUP, LPTSTR, &ECGROUP::lpszFullEmail>, "Email"},
		{conv_out_default<ECGROUP, unsigned int, &ECGROUP::ulIsABHidden>, "IsHidden"},
		{conv_out_default<ECGROUP, SBinary, &ECGROUP::sGroupId>, "GroupID"},
	};

	HRESULT hr = hrSuccess;
	ECGROUP *lpGroup = NULL;

	if (elem == Py_None)
		return nullptr;

	hr = MAPIAllocateBuffer(sizeof *lpGroup, (LPVOID*)&lpGroup);
	if (hr != hrSuccess) {
		PyErr_SetString(PyExc_RuntimeError, "Out of memory");
		return nullptr;
	}
	memset(lpGroup, 0, sizeof *lpGroup);

	process_conv_out_array(lpGroup, elem, conv_info, lpGroup, ulFlags);
	Object_to_MVPROPMAP(elem, lpGroup, ulFlags);

	if (PyErr_Occurred()) {
		MAPIFreeBuffer(lpGroup);
		return nullptr;
	}

	return lpGroup;
}

PyObject *Object_from_LPECGROUP(ECGROUP *lpGroup, ULONG ulFlags)
{
	pyobj_ptr MVProps(Object_from_MVPROPMAP(lpGroup->sMVPropmap, ulFlags));
	pyobj_ptr groupid(PyBytes_FromStringAndSize(reinterpret_cast<const char *>(lpGroup->sGroupId.lpb), lpGroup->sGroupId.cb));

	if(ulFlags & MAPI_UNICODE)
		return PyObject_CallFunction(PyTypeECGroup, "(uuuIOO)", lpGroup->lpszGroupname, lpGroup->lpszFullname, lpGroup->lpszFullEmail, lpGroup->ulIsABHidden, groupid.get(), MVProps.get());
	return PyObject_CallFunction(PyTypeECGroup, "(sssIOO)", lpGroup->lpszGroupname, lpGroup->lpszFullname, lpGroup->lpszFullEmail, lpGroup->ulIsABHidden, groupid.get(), MVProps.get());
}

PyObject *List_from_LPECGROUP(ECGROUP *lpGroup, ULONG cElements, ULONG ulFlags)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < cElements; ++i) {
		pyobj_ptr item(Object_from_LPECGROUP(&lpGroup[i], ulFlags));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

ECCOMPANY *Object_to_LPECCOMPANY(PyObject *elem, ULONG ulFlags)
{
	static conv_out_info<ECCOMPANY> conv_info[] = {
		{conv_out_default<ECCOMPANY, LPTSTR, &ECCOMPANY::lpszCompanyname>, "Companyname"},
		{conv_out_default<ECCOMPANY, LPTSTR, &ECCOMPANY::lpszServername>, "Servername"},
		{conv_out_default<ECCOMPANY, unsigned int, &ECCOMPANY::ulIsABHidden>, "IsHidden"},
		{conv_out_default<ECCOMPANY, SBinary, &ECCOMPANY::sCompanyId>, "CompanyID"},
		{conv_out_default<ECCOMPANY, SBinary, &ECCOMPANY::sAdministrator>, "AdministratorID"},
	};

	HRESULT hr = hrSuccess;
	ECCOMPANY *lpCompany = NULL;

	if (elem == Py_None)
		return nullptr;

	hr = MAPIAllocateBuffer(sizeof *lpCompany, (LPVOID*)&lpCompany);
	if (hr != hrSuccess) {
		PyErr_SetString(PyExc_RuntimeError, "Out of memory");
		return nullptr;
	}
	memset(lpCompany, 0, sizeof *lpCompany);

	process_conv_out_array(lpCompany, elem, conv_info, lpCompany, ulFlags);

	Object_to_MVPROPMAP(elem, lpCompany, ulFlags);

	if (PyErr_Occurred()) {
		MAPIFreeBuffer(lpCompany);
		return nullptr;
	}

	return lpCompany;
}

PyObject *Object_from_LPECCOMPANY(ECCOMPANY *lpCompany, ULONG ulFlags)
{
	pyobj_ptr MVProps(Object_from_MVPROPMAP(lpCompany->sMVPropmap, ulFlags));
	pyobj_ptr companyid(PyBytes_FromStringAndSize(reinterpret_cast<const char *>(lpCompany->sCompanyId.lpb), lpCompany->sCompanyId.cb));
	pyobj_ptr adminid(PyBytes_FromStringAndSize(reinterpret_cast<const char *>(lpCompany->sAdministrator.lpb), lpCompany->sAdministrator.cb));

        if(ulFlags & MAPI_UNICODE)
		return PyObject_CallFunction(PyTypeECCompany, "(uuIOOO)", lpCompany->lpszCompanyname, lpCompany->lpszServername, lpCompany->ulIsABHidden, companyid.get(), MVProps.get(), adminid.get());
	return PyObject_CallFunction(PyTypeECCompany, "(ssIOOO)", lpCompany->lpszCompanyname, lpCompany->lpszServername, lpCompany->ulIsABHidden, companyid.get(), MVProps.get(), adminid.get());
}

PyObject *List_from_LPECCOMPANY(ECCOMPANY *lpCompany, ULONG cElements,
    ULONG ulFlags)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < cElements; ++i) {
		pyobj_ptr item(Object_from_LPECCOMPANY(&lpCompany[i], ulFlags));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

LPROWLIST List_to_LPROWLIST(PyObject *object, ULONG ulFlags)
{
	pyobj_ptr iter;
	Py_ssize_t len = 0;
	memory_ptr<ROWLIST> lpRowList;
	int n = 0;

	if (object == Py_None)
		return NULL;

	len = PyObject_Length(object);
	if (len < 0) {
		PyErr_Format(PyExc_TypeError, "Invalid list passed as row list");
		return retval_or_null(lpRowList);
	}
	if (MAPIAllocateBuffer(CbNewROWLIST(len), &~lpRowList) != hrSuccess)
		return retval_or_null(lpRowList);
	lpRowList->cEntries = 0;
	iter.reset(PyObject_GetIter(object));
	if (iter == NULL)
		return retval_or_null(lpRowList);
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		pyobj_ptr rowflags(PyObject_GetAttrString(elem, "ulRowFlags"));
		if (rowflags == NULL)
			return retval_or_null(lpRowList);
		pyobj_ptr props(PyObject_GetAttrString(elem, "rgPropVals"));
		if (props == NULL)
			return retval_or_null(lpRowList);

		lpRowList->aEntries[n].ulRowFlags = (ULONG)PyLong_AsUnsignedLong(rowflags);
		lpRowList->aEntries[n].rgPropVals = List_to_LPSPropValue(props, &lpRowList->aEntries[n].cValues, ulFlags);
		lpRowList->cEntries = ++n;
	} while (true);

	return retval_or_null(lpRowList);
}

void DoException(HRESULT hr)
{
#if PY_VERSION_HEX >= 0x02040300	// 2.4.3
	pyobj_ptr hrObj(Py_BuildValue("I", static_cast<unsigned int>(hr)));
#else
	// Python 2.4.2 and earlier don't support the "I" format so create a
	// PyLong object instead.
	pyobj_ptr hrObj(PyLong_FromUnsignedLong(static_cast<unsigned int>(hr)));
#endif

	#if PY_MAJOR_VERSION >= 3
	pyobj_ptr attr_name(PyUnicode_FromString("_errormap"));
	#else
	pyobj_ptr attr_name(PyString_FromString("_errormap"));
	#endif
	pyobj_ptr errormap(PyObject_GetAttr(PyTypeMAPIError, attr_name)), ex;
	PyObject *errortype = nullptr;
	if (errormap != NULL) {
		errortype = PyDict_GetItem(errormap, hrObj);
		if (errortype)
			ex.reset(PyObject_CallFunction(errortype, nullptr));
	}
	if (!errortype) {
		errortype = PyTypeMAPIError;
		ex.reset(PyObject_CallFunction(PyTypeMAPIError, "O", hrObj.get()));
	}

	PyErr_SetObject(errortype, ex);
}

int GetExceptionError(PyObject *object, HRESULT *lphr)
{
	if (!PyErr_GivenExceptionMatches(object, PyTypeMAPIError))
		return 0;
	pyobj_ptr type, value, traceback;
	PyErr_Fetch(&~type, &~value, &~traceback);
	pyobj_ptr hr(PyObject_GetAttrString(value, "hr"));
	if (!hr) {
		PyErr_SetString(PyExc_RuntimeError, "hr or Value missing from MAPIError");
		return -1;
	}

	*lphr = (HRESULT)PyLong_AsUnsignedLong(hr);
	return 1;
}

ECQUOTA *Object_to_LPECQUOTA(PyObject *elem)
{
	static conv_out_info<ECQUOTA> conv_info[] = {
		{conv_out_default<ECQUOTA, bool, &ECQUOTA::bUseDefaultQuota>, "bUseDefaultQuota"},
		{conv_out_default<ECQUOTA, bool, &ECQUOTA::bIsUserDefaultQuota>, "bIsUserDefaultQuota"},
		{conv_out_default<ECQUOTA, int64_t, &ECQUOTA::llWarnSize>, "llWarnSize"},
		{conv_out_default<ECQUOTA, int64_t, &ECQUOTA::llSoftSize>, "llSoftSize"},
		{conv_out_default<ECQUOTA, int64_t, &ECQUOTA::llHardSize>, "llHardSize"},
	};

	HRESULT hr = hrSuccess;
	ECQUOTA *lpQuota = NULL;

	if (elem == Py_None)
		return nullptr;

	hr = MAPIAllocateBuffer(sizeof *lpQuota, (LPVOID*)&lpQuota);
	if (hr != hrSuccess) {
		PyErr_SetString(PyExc_RuntimeError, "Out of memory");
		return nullptr;
	}
	memset(lpQuota, 0, sizeof *lpQuota);

	process_conv_out_array(lpQuota, elem, conv_info, lpQuota, 0);

	if (PyErr_Occurred()) {
		MAPIFreeBuffer(lpQuota);
		return nullptr;
	}

	return lpQuota;
}

PyObject *Object_from_LPECQUOTA(ECQUOTA *lpQuota)
{
	return PyObject_CallFunction(PyTypeECQuota, "(llLLL)", lpQuota->bUseDefaultQuota, lpQuota->bIsUserDefaultQuota, lpQuota->llWarnSize, lpQuota->llSoftSize, lpQuota->llHardSize);
}

PyObject *Object_from_LPECQUOTASTATUS(ECQUOTASTATUS *lpQuotaStatus)
{
	return PyObject_CallFunction(PyTypeECQuotaStatus, "Ll", lpQuotaStatus->llStoreSize, lpQuotaStatus->quotaStatus);
}

ECSVRNAMELIST *List_to_LPECSVRNAMELIST(PyObject *object)
{
	HRESULT hr = hrSuccess;
	Py_ssize_t len = 0;
	pyobj_ptr iter;
	memory_ptr<ECSVRNAMELIST> lpSvrNameList;

	if (object == Py_None)
		return retval_or_null(lpSvrNameList);

	len = PyObject_Length(object);
	if (len < 0) {
		PyErr_Format(PyExc_TypeError, "Invalid list passed as servername list");
		return retval_or_null(lpSvrNameList);
	}
	if (MAPIAllocateBuffer(sizeof(ECSVRNAMELIST) + (sizeof(ECSERVER *) * len), &~lpSvrNameList) != hrSuccess)
		return retval_or_null(lpSvrNameList);

	memset(lpSvrNameList, 0, sizeof(ECSVRNAMELIST) + (sizeof(ECSERVER *) * len) );
	iter.reset(PyObject_GetIter(object));
	if (iter == NULL)
		return retval_or_null(lpSvrNameList);
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		char *ptr = NULL;
		Py_ssize_t strlen = 0;

		if (PyString_AsStringAndSize(elem, &ptr, &strlen) == -1 ||
		    PyErr_Occurred())
			return retval_or_null(lpSvrNameList);
		hr = KAllocCopy(ptr, strlen, reinterpret_cast<void **>(&lpSvrNameList->lpszaServer[lpSvrNameList->cServers]), lpSvrNameList);
		if (hr != hrSuccess) {
			PyErr_SetString(PyExc_RuntimeError, "Out of memory");
			return retval_or_null(lpSvrNameList);
		}
		++lpSvrNameList->cServers;
	} while (true);

	return retval_or_null(lpSvrNameList);
}

PyObject *Object_from_LPECSERVER(ECSERVER *lpServer)
{
	return PyObject_CallFunction(PyTypeECServer, "(sssssl)", lpServer->lpszName, lpServer->lpszFilePath, lpServer->lpszHttpPath, lpServer->lpszSslPath, lpServer->lpszPreferedPath, lpServer->ulFlags);
}

PyObject *List_from_LPECSERVERLIST(ECSERVERLIST *lpServerList)
{
	pyobj_ptr list(PyList_New(0));
	for (unsigned int i = 0; i < lpServerList->cServers; ++i) {
		pyobj_ptr item(Object_from_LPECSERVER(&lpServerList->lpsaServer[i]));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

void Object_to_STATSTG(PyObject *object, STATSTG *stg)
{
	pyobj_ptr cbSize;

	if(object == Py_None) {
		PyErr_Format(PyExc_TypeError, "Invalid None passed for STATSTG");
		return;
	}

	cbSize.reset(PyObject_GetAttrString(object, "cbSize"));
	if(!cbSize) {
		PyErr_Format(PyExc_TypeError, "STATSTG does not contain cbSize");
		return;
	}

	stg->cbSize.QuadPart = PyLong_AsINT64(cbSize);
}

PyObject *Object_from_STATSTG(STATSTG *lpStatStg)
{
	if(lpStatStg == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}
	pyobj_ptr cbSize(PyLong_FromLongLong(lpStatStg->cbSize.QuadPart));
	pyobj_ptr result(PyObject_CallFunction(PyTypeSTATSTG, "(O)", cbSize.get()));
	if (PyErr_Occurred())
		return nullptr;
	return result.release();
}
