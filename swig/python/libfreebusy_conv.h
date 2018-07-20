/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef LIBFREEBUSY_CONV_H
#define LIBFREEBUSY_CONV_H

#include "freebusy.h"
#include <Python.h>

void InitFreebusy();
LPFBUser List_to_p_FBUser(PyObject *, ULONG *);
LPFBBlock_1 List_to_p_FBBlock_1(PyObject *, ULONG *);
PyObject* List_from_FBBlock_1(LPFBBlock_1, LONG *);
#endif
