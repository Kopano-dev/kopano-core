/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/memory.hpp>
#include "libfreebusy_conv.h"
#include "pymem.hpp"

using KC::pyobj_ptr;

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
	KC::memory_ptr<FBUser> lpFbUsers;
	char *buf = 0 ;
	size_t size;
	int i = 0;

	if (list == Py_None)
		return nullptr;
	pyobj_ptr iter(PyObject_GetIter(list));
	if (!iter)
		return nullptr;
	auto len = PyObject_Length(list);
	if (MAPIAllocateBuffer(len * sizeof(FBUser), &~lpFbUsers) != hrSuccess)
		return nullptr;
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;
		if (PyBytes_AsStringAndSize(elem, &buf, (Py_ssize_t *)&size) == -1) {
			PyErr_SetString(PyExc_RuntimeError, "Entryid is missing");
			return nullptr;
		}

		auto entryid = reinterpret_cast<ENTRYID *>(buf);
		lpFbUsers[i].m_cbEid = size;
		lpFbUsers[i].m_lpEid = entryid;
		++i;
	} while (true);
	*cValues = i;

	if (PyErr_Occurred() && lpFbUsers != nullptr)
		return nullptr;
	return lpFbUsers.release();
}

LPFBBlock_1 List_to_p_FBBlock_1(PyObject *list, ULONG *nBlocks) {
	KC::memory_ptr<FBBlock_1> lpFBBlocks;

	if (list == Py_None)
		return nullptr;
	pyobj_ptr iter(PyObject_GetIter(list));
	if (!iter)
		return nullptr;
	auto len = PyObject_Length(list);
	if (MAPIAllocateBuffer(len * sizeof(FBBlock_1), &~lpFBBlocks) != hrSuccess)
		return nullptr;

	size_t i = 0;
	do {
		pyobj_ptr elem(PyIter_Next(iter));
		if (elem == nullptr)
			break;

		pyobj_ptr start(PyObject_GetAttrString(elem, "start"));
		pyobj_ptr end(PyObject_GetAttrString(elem, "end"));
		pyobj_ptr status(PyObject_GetAttrString(elem, "status"));

		lpFBBlocks[i].m_tmStart = PyLong_AsLong(start);
		lpFBBlocks[i].m_tmEnd = PyLong_AsLong(end);
		lpFBBlocks[i].m_fbstatus = FBStatus(PyLong_AsLong(status));

		i++;
	} while (true);
	*nBlocks = i;

	if (PyErr_Occurred() && lpFBBlocks != nullptr)
		return nullptr;
	return lpFBBlocks.release();
}

PyObject* Object_from_FBBlock_1(FBBlock_1 const& sFBBlock) {
	pyobj_ptr end, status, object;
	pyobj_ptr start(PyLong_FromLong(sFBBlock.m_tmStart));
	if (PyErr_Occurred())
		return nullptr;
	end.reset(PyLong_FromLong(sFBBlock.m_tmEnd));
	if (PyErr_Occurred())
		return nullptr;
	status.reset(PyLong_FromLong(sFBBlock.m_fbstatus));
	if (PyErr_Occurred())
		return nullptr;
	return PyObject_CallFunction(PyTypeFreeBusyBlock, "(OOO)", start.get(), end.get(), status.get());
}

PyObject* List_from_FBBlock_1(LPFBBlock_1 lpFBBlocks, LONG* nBlocks) {
	pyobj_ptr list(PyList_New(0));
	for (size_t i = 0; i < *nBlocks; ++i) {
		pyobj_ptr elem(Object_from_FBBlock_1(lpFBBlocks[i]));
		if(PyErr_Occurred())
			return nullptr;
		PyList_Append(list, elem);
	}
	return list.release();
}
