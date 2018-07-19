/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef operations_fwd_INCLUDED
#define operations_fwd_INCLUDED

#include <memory>

namespace KC { namespace operations {

class IArchiveOperation;
typedef std::shared_ptr<IArchiveOperation> ArchiveOperationPtr;

class Deleter;
typedef std::shared_ptr<Deleter> DeleterPtr;

class Stubber;
typedef std::shared_ptr<Stubber> StubberPtr;

}} /* namespace */

#endif // ndef operations_fwd_INCLUDED
