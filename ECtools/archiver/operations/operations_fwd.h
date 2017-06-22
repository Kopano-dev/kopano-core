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
