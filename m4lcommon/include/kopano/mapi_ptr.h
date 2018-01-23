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

#ifndef mapi_ptr_INCLUDED
#define mapi_ptr_INCLUDED

#include <kopano/memory.hpp>
#include <mapix.h>
#include <mapispi.h>
#include <edkmdb.h>
#include <edkguid.h>
#include <kopano/IECInterfaces.hpp>
#include <kopano/ECGuid.h>
#include <kopano/mapiguidext.h>

namespace KC {

typedef object_ptr<IABContainer> ABContainerPtr;
typedef object_ptr<IAddrBook> AddrBookPtr;
typedef object_ptr<IECServiceAdmin> ECServiceAdminPtr;
typedef object_ptr<IExchangeManageStore> ExchangeManageStorePtr;
typedef object_ptr<IMAPIFolder> MAPIFolderPtr;
typedef object_ptr<IMAPIProp> MAPIPropPtr;
typedef object_ptr<IMAPISession> MAPISessionPtr;
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

#endif // ndef mapi_ptr_INCLUDED
