/* SPDX-License-Identifier: AGPL-3.0-only */
%{
const char *TypeFromObject(PyObject *op) { return op->ob_type->tp_name; }
%}