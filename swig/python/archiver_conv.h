/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include "Archiver.h"
#include <Python.h>

namespace KC {
typedef std::list<ArchiveEntry> ArchiveList;
typedef std::list<UserEntry> UserList;
}
extern PyObject *List_from_ArchiveList(const KC::ArchiveList &lst);
extern PyObject *List_from_UserList(const KC::UserList &lst);
