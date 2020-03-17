/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <list>
#include <memory>

namespace KC { namespace operations {

class Transaction;
typedef std::shared_ptr<Transaction> TransactionPtr;
typedef std::list<TransactionPtr> TransactionList;

class Rollback;
typedef std::shared_ptr<Rollback> RollbackPtr;
typedef std::list<RollbackPtr> RollbackList;

}} /* namespace */
