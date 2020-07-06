/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <list>
#include "archiver_conv.h"
#include "pymem.hpp"

using namespace KC;

PyObject *PyObject_from_Iterator(const std::list<ArchiveEntry>::const_iterator &i)
{
	return Py_BuildValue("(sssi)", i->StoreName.c_str(), i->FolderName.c_str(), i->StoreOwner.c_str(), i->Rights);
}

PyObject* PyObject_from_Iterator(const UserList::const_iterator &i) {
	return Py_BuildValue("s", i->UserName.c_str());
}

template<typename ListType>
PyObject* List_from(const ListType &lst)
{
	pyobj_ptr list(PyList_New(0));
	for (auto i = lst.cbegin(); i != lst.cend(); ++i) {
		pyobj_ptr item(PyObject_from_Iterator(i));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

PyObject *List_from_ArchiveList(const std::list<ArchiveEntry> &lst)
{
	return List_from<std::list<ArchiveEntry>>(lst);
}

PyObject* List_from_UserList(const UserList &lst)
{
	return List_from<UserList>(lst);
}
