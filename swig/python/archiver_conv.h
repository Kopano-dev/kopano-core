/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include "Archiver.h"
#include <Python.h>

extern PyObject *List_from_ArchiveList(const KC::ArchiveList &lst);
extern PyObject *List_from_UserList(const KC::UserList &lst);
