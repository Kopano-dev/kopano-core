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


#include "libfreebusy_conv.h"

static PyObject *PyTypeFreeBusyBlock;

void InitFreebusy() {
	PyObject *lpMAPIStruct = PyImport_ImportModule("MAPI.Struct");
	if(!lpMAPIStruct) {
		PyErr_SetString(PyExc_RuntimeError, "Unable to import MAPI.Struct");
		return;
	}

	PyTypeFreeBusyBlock = PyObject_GetAttrString(lpMAPIStruct, "FreeBusyBlock");
}

LPFBUser List_to_p_FBUser(PyObject *list, ULONG *cValues) {
	LPFBUser lpFbUsers = NULL;
	LPENTRYID entryid = NULL;
	PyObject *iter = NULL;
	PyObject *elem = NULL;
	char *buf = 0 ;
	int len;
	size_t size;
	int i = 0;

	if (list == Py_None)
		goto exit;

	iter = PyObject_GetIter(list);
	if (!iter)
		goto exit;

	len = PyObject_Length(list);
	if (MAPIAllocateBuffer(len * sizeof(FBUser), (void **)&lpFbUsers) != hrSuccess)
		goto exit;

	while ((elem = PyIter_Next(iter))) {
		if (PyBytes_AsStringAndSize(elem, &buf, (Py_ssize_t *)&size) == -1) {
			PyErr_SetString(PyExc_RuntimeError, "Entryid is missing");
			goto exit;
		}

		entryid = reinterpret_cast< LPENTRYID >(buf);

		lpFbUsers[i].m_cbEid = size;
		lpFbUsers[i].m_lpEid = entryid;
		++i;

		Py_XDECREF(elem);
		elem = nullptr;
	}

	*cValues = i;
 exit:
	Py_XDECREF(elem);
	Py_XDECREF(iter);

	if (PyErr_Occurred() && lpFbUsers) {
		MAPIFreeBuffer(lpFbUsers);
		lpFbUsers = nullptr;
	}

	return lpFbUsers;
}

LPFBBlock_1 List_to_p_FBBlock_1(PyObject *list, ULONG *nBlocks) {
	LPFBBlock_1 lpFBBlocks = nullptr;
	PyObject *iter, *elem, *start, *end, *status;
	size_t i, len;

	if (list == Py_None)
		goto exit;

	iter = PyObject_GetIter(list);
	if (!iter)
		goto exit;

	len = PyObject_Length(list);
	if (MAPIAllocateBuffer(len * sizeof(FBBlock_1), (void **)&lpFBBlocks) != hrSuccess)
		goto exit;

	i=0;

	while ((elem = PyIter_Next(iter))) {
		start = PyObject_GetAttrString(elem, "start");
		end = PyObject_GetAttrString(elem, "end");
		status = PyObject_GetAttrString(elem, "status");

		lpFBBlocks[i].m_tmStart = PyLong_AsLong(start);
		lpFBBlocks[i].m_tmEnd = PyLong_AsLong(end);
		lpFBBlocks[i].m_fbstatus = FBStatus(PyLong_AsLong(status));

		i++;

		Py_XDECREF(elem);
		Py_XDECREF(start);
		Py_XDECREF(end);
		Py_XDECREF(status);

		elem = nullptr;
	}

	*nBlocks = i;

 exit:
	Py_XDECREF(elem);
	Py_XDECREF(iter);

	if (PyErr_Occurred() && lpFBBlocks) {
		MAPIFreeBuffer(lpFBBlocks);
		lpFBBlocks = nullptr;
	}

	return lpFBBlocks;
}

PyObject* Object_from_FBBlock_1(FBBlock_1 const& sFBBlock) {
	PyObject *start = nullptr, *end = nullptr,
		*status = nullptr, *object = nullptr;

	start = PyLong_FromLong(sFBBlock.m_tmStart);
	if (PyErr_Occurred())
		goto exit;

	end = PyLong_FromLong(sFBBlock.m_tmEnd);
	if (PyErr_Occurred())
		goto exit;

	status = PyLong_FromLong(sFBBlock.m_fbstatus);
	if (PyErr_Occurred())
		goto exit;

	object = PyObject_CallFunction(PyTypeFreeBusyBlock, "(OOO)", start, end, status);

 exit:
	Py_XDECREF(start);
	Py_XDECREF(end);
	Py_XDECREF(status);

	if (PyErr_Occurred()) {
		Py_XDECREF(object);
		object = nullptr;
	}

	return object;
}

PyObject* List_from_FBBlock_1(LPFBBlock_1 lpFBBlocks, LONG* nBlocks) {
	size_t i;
	PyObject *list = nullptr, *elem = nullptr;

	list = PyList_New(0);

	for (i = 0; i < *nBlocks; i++) {
		elem = Object_from_FBBlock_1(lpFBBlocks[i]);

		if(PyErr_Occurred())
			goto exit;

		PyList_Append(list, elem);

		Py_XDECREF(elem);
		elem = nullptr;
	}
 exit:
	Py_XDECREF(elem);

	if (PyErr_Occurred()) {
		Py_XDECREF(list);
		list = nullptr;
	}

	return list;
}
