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

typedef object_ptr<IMAPITable> MAPITablePtr;
typedef object_ptr<IMailUser> MailUserPtr;
typedef object_ptr<IMessage> MessagePtr;
typedef object_ptr<IMsgStore> MsgStorePtr;
typedef object_ptr<IStream> StreamPtr;
typedef object_ptr<IAttach> AttachPtr;

typedef memory_ptr<ENTRYID> EntryIdPtr;
typedef memory_ptr<ENTRYLIST> EntryListPtr;
typedef memory_ptr<SPropValue> SPropValuePtr;
typedef memory_ptr<SPropTagArray> SPropTagArrayPtr;
typedef memory_ptr<SRestriction> SRestrictionPtr;

typedef memory_ptr<SPropValue> SPropArrayPtr;
typedef rowset_ptr SRowSetPtr;

} /* namespace */
