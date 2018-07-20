/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ARCHIVERSESSIONPTR_INCLUDED
#define ARCHIVERSESSIONPTR_INCLUDED

#include <memory>

namespace KC {

class ArchiverSession;
typedef std::shared_ptr<ArchiverSession> ArchiverSessionPtr;

} /* namespace */

#endif // !defined ARCHIVERSESSIONPTR_INCLUDED
