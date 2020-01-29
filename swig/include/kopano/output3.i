#define %append_output3(obj) $result = SWIG3_Python_AppendOutput($result, obj)

%{
SWIGINTERN PyObject *SWIG3_Python_AppendOutput(PyObject *result, PyObject *obj)
{
	PyObject *o2, *o3;
	if (result == NULL) {
		return obj;
	} else if (result == Py_None) {
		Py_DECREF(result);
		return obj;
	}
	if (!PyTuple_Check(result)) {
		o2 = result;
		result = PyTuple_New(1);
		PyTuple_SET_ITEM(result, 0, o2);
	}
	o3 = PyTuple_New(1);
	PyTuple_SET_ITEM(o3, 0, obj);
	o2 = result;
	result = PySequence_Concat(o2, o3);
	Py_DECREF(o2);
	Py_DECREF(o3);
	return result;
}

%}
