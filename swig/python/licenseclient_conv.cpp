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

#include "licenseclient_conv.h"

// Get Py_ssize_t for older versions of python
#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
# define PY_SSIZE_T_MAX INT_MAX
# define PY_SSIZE_T_MIN INT_MIN
#endif

using namespace std;

PyObject* List_from_StringVector(const vector<string> &v)
{
    PyObject *list = PyList_New(0);
    PyObject *item = NULL;

    vector<string>::const_iterator i;

    for (i = v.begin(); i != v.end(); ++i) {
		item = Py_BuildValue("s", i->c_str());
		if (PyErr_Occurred())
			goto exit;

		PyList_Append(list, item);
		Py_DECREF(item);
		item = NULL;
	}

exit:
    if(PyErr_Occurred()) {
        if(list) {
            Py_DECREF(list);
        }
        list = NULL;
    }
    
    if(item) {
        Py_DECREF(item);
    }
    
    return list;
}

int List_to_StringVector(PyObject *object, vector<string> &v)
{
    Py_ssize_t size = 0;
    vector<string> vTmp;
    PyObject *iter = NULL;
    PyObject *elem = NULL;
    int retval = -1;
    
    if(object == Py_None) {
		v.clear();
		return 0;
	}
    
    iter = PyObject_GetIter(object);
    if(!iter)
        goto exit;
            
    size = PyObject_Size(object);
    vTmp.reserve(size);
    
    while ((elem = PyIter_Next(iter)) != NULL) {
		char *ptr;
		Py_ssize_t strlen;

		#if PY_MAJOR_VERSION >= 3
			PyBytes_AsStringAndSize(elem, &ptr, &strlen);
		#else
			PyString_AsStringAndSize(elem, &ptr, &strlen);
		#endif
        if (PyErr_Occurred())
            goto exit;

		vTmp.push_back(string(ptr, strlen));
            
        Py_DECREF(elem);
        elem = NULL;
    }

	v.swap(vTmp);
	retval = 0;
    
exit:
    if(elem) {
        Py_DECREF(elem);
    }
        
    if(iter) {
        Py_DECREF(iter);
    }
        
    return retval;
}
