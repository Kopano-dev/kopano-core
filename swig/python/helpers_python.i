%{
const char *TypeFromObject(PyObject *op) { return op->ob_type->tp_name; }
%}