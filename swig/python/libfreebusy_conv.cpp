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
		if (PyBytes_AsStringAndSize(elem, &buf, (Py_ssize_t *)&size) == -1)
			PyErr_SetString(PyExc_RuntimeError, "ulFlags missing for newmail notification");

		entryid = reinterpret_cast< LPENTRYID >(buf);

		lpFbUsers[i].m_cbEid = size;
		lpFbUsers[i].m_lpEid = entryid;
		++i;
	}

	*cValues = i;
 exit:
	return lpFbUsers;
}

LPFBBlock_1 List_to_p_FBBlock_1(PyObject *list, ULONG *nBlocks) {
	LPFBBlock_1 lpFBBlocks = NULL;
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
	}

	*nBlocks = i;

 exit:
	return lpFBBlocks;
}
