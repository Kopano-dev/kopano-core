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

#include "archiver_conv.h"
#include "pymem.hpp"

using namespace KC;

PyObject* PyObject_from_Iterator(const ArchiveList::const_iterator &i) {
	return Py_BuildValue("(sssi)", i->StoreName.c_str(), i->FolderName.c_str(), i->StoreOwner.c_str(), i->Rights);
}

PyObject* PyObject_from_Iterator(const UserList::const_iterator &i) {
	return Py_BuildValue("s", i->UserName.c_str());
}

template<typename ListType>
PyObject* List_from(const ListType &lst)
{
	pyobj_ptr list(PyList_New(0));
    typename ListType::const_iterator i;

    for (i = lst.begin(); i != lst.end(); ++i) {
		pyobj_ptr item(PyObject_from_Iterator(i));
		if (PyErr_Occurred())
			return nullptr;
		PyList_Append(list, item);
	}
	return list.release();
}

PyObject* List_from_ArchiveList(const ArchiveList &lst)
{
	return List_from<ArchiveList>(lst);
}

PyObject* List_from_UserList(const UserList &lst)
{
	return List_from<UserList>(lst);
}
