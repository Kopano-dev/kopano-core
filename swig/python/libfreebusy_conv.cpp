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
	LPENTRYID entryid = nullptr;
	pyobj_ptr iter;
	char *buf = 0 ;
	int len;
	size_t size;
	int i = 0;

	if (list == Py_None)
		return nullptr;
	iter.reset(PyObject_GetIter(list));
	if (!iter)
		return nullptr;

	len = PyObject_Length(list);
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

		entryid = reinterpret_cast< LPENTRYID >(buf);

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
	pyobj_ptr iter;
	size_t i, len;

	if (list == Py_None)
		return nullptr;
	iter.reset(PyObject_GetIter(list));
	if (!iter)
		return nullptr;

	len = PyObject_Length(list);
	if (MAPIAllocateBuffer(len * sizeof(FBBlock_1), &~lpFBBlocks) != hrSuccess)
		return nullptr;

	i=0;
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
