/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/memory.hpp>
#include <mapix.h>
#include <mapispi.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/ECGuid.h>
#include <kopano/mapiguidext.h>

namespace KC {

typedef memory_ptr<ENTRYLIST> EntryListPtr;
typedef memory_ptr<SPropValue> SPropValuePtr;
typedef memory_ptr<SPropTagArray> SPropTagArrayPtr;
typedef memory_ptr<SRestriction> SRestrictionPtr;

typedef memory_ptr<SPropValue> SPropArrayPtr;
typedef rowset_ptr SRowSetPtr;

} /* namespace */
